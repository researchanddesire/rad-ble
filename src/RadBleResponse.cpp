#include "RadBleResponse.h"

namespace radble {
namespace response {
namespace {

// Fill a single task-owned buffer. A write either fits or causes the entire
// frame to be replaced. No partial JSON is ever returned to the transport.
class Writer {
public:
  explicit Writer(Frame &frame) : frame_(frame) { frame_.size = 0; }

  void append(const char *s, size_t n) {
    if (!fits_ || n > MAX_MESSAGE_BYTES - frame_.size) {
      fits_ = false;
      return;
    }
    if (n)
      std::memcpy(frame_.bytes + frame_.size, s, n);
    frame_.size += n;
  }
  void append(const char *s) { append(s, std::strlen(s)); }
  void number(uint32_t n) {
    char digits[10];
    char *p = digits + sizeof(digits);
    do {
      *--p = static_cast<char>('0' + n % 10);
    } while ((n /= 10));
    append(p, digits + sizeof(digits) - p);
  }
  void quoted(Text value);
  void field(const char *key, Text value) {
    if (value.size) {
      append(key);
      quoted(value);
    }
  }
  bool finish() {
    frame_.bytes[frame_.size] = '\0';
    return fits_;
  }

private:
  Frame &frame_;
  bool fits_ = true;
};

bool digit(char c) { return c >= '0' && c <= '9'; }
bool hex(char c) {
  return digit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

// Validate UTF-8 without reading past the supplied field. Return zero on an
// invalid sequence (including overlong, surrogate, and out-of-range values).
size_t utf8(const char *p, size_t left) {
  const auto *s = reinterpret_cast<const unsigned char *>(p);
  const unsigned c = s[0];
  if (c < 0x80)
    return 1;
  if (c < 0xc2 || c > 0xf4)
    return 0;
  const size_t n = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
  if (left < n)
    return 0;
  for (size_t i = 1; i < n; ++i)
    if ((s[i] & 0xc0) != 0x80)
      return 0;
  if ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] > 0x9f) ||
      (c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] > 0x8f))
    return 0;
  return n;
}

// Handlers supply already-serialized JSON. Detect truncated Strings without
// materializing another document. One bit per level suffices for the closing
// delimiter; even the most deeply nested legal 509-byte value fits.
class JsonValue {
public:
  explicit JsonValue(Text text) : p_(text.data), end_(p_ + text.size) {}

  bool complete() {
    space();
    for (;;) {
      if (take('{')) {
        push(true);
        space();
        if (!take('}')) {
          if (!key())
            return false;
          continue;
        }
        --depth_;
      } else if (take('[')) {
        push(false);
        space();
        if (!take(']'))
          continue;
        --depth_;
      } else if (take('"')) {
        if (!stringBody())
          return false;
      } else if (!(word("true") || word("false") || word("null") || number())) {
        return false;
      }
      space();
      while (depth_) {
        const size_t level = depth_ - 1;
        const bool object = (levels_[level / 8] >> (level % 8)) & 1;
        if (take(object ? '}' : ']')) {
          --depth_;
          space();
          continue;
        }
        if (!take(','))
          return false;
        space();
        if (object && !key())
          return false;
        break;
      }
      if (!depth_)
        return p_ == end_;
    }
  }

private:
  // Invalid inputs can consist entirely of opening delimiters.
  unsigned char levels_[(MAX_MESSAGE_BYTES + 7) / 8]{};
  size_t depth_ = 0;
  const char *p_, *end_;

  void push(bool object) {
    const unsigned char bit = 1U << (depth_ % 8);
    if (object)
      levels_[depth_ / 8] |= bit;
    else
      levels_[depth_ / 8] &= ~bit;
    ++depth_;
  }
  void space() {
    while (p_ != end_ &&
           (*p_ == ' ' || *p_ == '\t' || *p_ == '\r' || *p_ == '\n'))
      ++p_;
  }
  bool take(char c) {
    if (p_ == end_ || *p_ != c)
      return false;
    ++p_;
    return true;
  }
  bool word(const char *s) {
    const auto n = std::strlen(s);
    if (static_cast<size_t>(end_ - p_) < n || std::memcmp(p_, s, n))
      return false;
    p_ += n;
    return true;
  }
  bool key() {
    if (!take('"') || !stringBody())
      return false;
    space();
    if (!take(':'))
      return false;
    space();
    return true;
  }
  bool stringBody() {
    while (p_ != end_) {
      if (take('"'))
        return true;
      if (take('\\')) {
        if (take('u')) {
          for (int i = 0; i < 4; ++i)
            if (p_ == end_ || !hex(*p_++))
              return false;
        } else {
          if (p_ == end_ || !*p_ || !std::strchr("\"\\/bfnrt", *p_++))
            return false;
        }
      } else {
        if (static_cast<unsigned char>(*p_) < 0x20)
          return false;
        const size_t n = utf8(p_, end_ - p_);
        if (!n)
          return false;
        p_ += n;
      }
    }
    return false;
  }
  bool digits() {
    const char *start = p_;
    while (p_ != end_ && digit(*p_))
      ++p_;
    return p_ != start;
  }
  bool number() {
    take('-');
    if (!take('0') && !digits())
      return false;
    if (take('.') && !digits())
      return false;
    if (take('e') || take('E')) {
      if (!take('-'))
        take('+');
      if (!digits())
        return false;
    }
    return true;
  }
};

void prefix(Writer &w, uint32_t id, const char *stage, bool ok) {
  w.append("{\"v\":");
  w.number(PROTOCOL_VERSION);
  w.append(",\"id\":");
  w.number(id);
  w.append(",\"stage\":");
  w.quoted(stage);
  w.append(ok ? ",\"ok\":true" : ",\"ok\":false");
}

void Writer::quoted(Text value) {
  static constexpr char digits[] = "0123456789abcdef";
  append("\"");
  for (size_t i = 0; fits_ && i < value.size;) {
    const char *p = value.data + i;
    const unsigned c = static_cast<unsigned char>(*p);
    if (c >= 8 && c <= 13 && c != 11) {
      static constexpr char escapes[] = {'b', 't', 'n', 0, 'f', 'r'};
      const char escape[] = {'\\', escapes[c - 8]};
      append(escape, sizeof(escape));
    } else if (c < 0x20) {
      const char escape[] = {'\\',           'u',           '0', '0',
                             digits[c >> 4], digits[c & 15]};
      append(escape, sizeof(escape));
    } else if (c >= 0x80) {
      const size_t n = utf8(p, value.size - i);
      if (n) {
        append(p, n);
        i += n;
        continue;
      }
      append("\\ufffd");
    } else {
      if (c == '"' || c == '\\')
        append("\\");
      append(p, 1);
    }
    ++i;
  }
  append("\"");
}

} // namespace

const char *encode(Frame &frame, const Stage &s) {
  Writer w(frame);
  prefix(w, s.id, s.name, s.ok);
  const bool completed = std::strcmp(s.name, "completed") == 0;
  w.field(",\"code\":",
          s.ok && completed && !s.code.size ? Text("ok") : s.code);
  w.field(",\"message\":", s.message);
  w.field(",\"stateBefore\":", s.before);
  w.field(",\"stateAfter\":", s.after);
  if (s.result.size) {
    w.append(",\"result\":");
    w.append(s.result.data, s.result.size);
  }
  w.append("}");
  const bool fits = w.finish();
  if (fits && s.result.data &&
      (!s.result.size || JsonValue(s.result).complete()))
    return nullptr;

  Writer fallback(frame);
  const char *errorCode = nullptr;
  // An accepted stage is provisional: a handler may still run and complete.
  if (std::strcmp(s.name, "accepted") == 0) {
    prefix(fallback, s.id, "accepted", true);
  } else {
    errorCode = fits ? "resource_unavailable" : "response_too_large";
    prefix(fallback, s.id, "failed", false);
    fallback.field(",\"code\":", errorCode);
  }
  fallback.append("}");
  fallback.finish();
  return errorCode;
}

} // namespace response
} // namespace radble
