#pragma once

#include "RadBleProtocol.generated.h"

#include <cstring>

namespace radble {
namespace response {

// Borrow literals and Arduino Strings, including temporaries, for one call.
struct Text {
  const char *data;
  size_t size;

  Text(const char *s) : data(s), size(s ? std::strlen(s) : 0) {}
  template <class StringType>
  Text(const StringType &s, decltype(s.c_str()) * = nullptr)
      : data(s.c_str()), size(s.length()) {}
};

struct Frame {
  char bytes[MAX_MESSAGE_BYTES + 1]{};
  size_t size = 0;
};

struct Stage {
  uint32_t id;
  const char *name;
  bool ok;
  Text code, message, result, before, after;
};

// Returns the wire error code if a terminal response had to be replaced.
const char *encode(Frame &frame, const Stage &stage);

// Only the server task touches reply strings. Connection generations also
// distinguish queued requests when NimBLE reuses a disconnected handle.
template <size_t Count> class Sessions {
public:
  uint32_t get(uint16_t handle, bool create) {
    Entry *empty = nullptr;
    for (auto &entry : entries_) {
      if (entry.generation && entry.handle == handle)
        return entry.generation;
      if (!entry.generation)
        empty = &entry;
    }
    if (!create || !empty)
      return 0;
    if (++next_ == 0)
      ++next_;
    empty->handle = handle;
    return empty->generation = next_;
  }
  void closeAll() {
    for (auto &entry : entries_)
      entry.generation = 0;
  }
  void close(uint16_t handle) {
    for (auto &entry : entries_)
      if (entry.handle == handle)
        entry.generation = 0;
  }

private:
  struct Entry {
    uint32_t generation;
    uint16_t handle;
  };
  Entry entries_[Count]{};
  uint32_t next_ = 0;
};

template <class StringType, size_t Count> class Cache {
public:
  const StringType *lookup(uint32_t session, uint32_t id) const {
    for (const auto &entry : entries_)
      if (session && entry.session == session && entry.id == id)
        return &entry.payload;
    return nullptr;
  }
  void remember(uint32_t session, uint32_t id, const Frame &frame) {
    auto &entry = entries_[next_];
    entry.session = session;
    entry.id = id;
    entry.payload.clear();
    // Retain the key even when the optional copy cannot allocate. A duplicate
    // can then receive a correlated failure without repeating a mutation.
    if (!entry.payload.concat(frame.bytes, frame.size))
      entry.payload.clear();
    next_ = (next_ + 1) % Count;
  }

private:
  struct Entry {
    uint32_t session, id;
    StringType payload;
  };
  Entry entries_[Count]{};
  size_t next_ = 0;
};

} // namespace response
} // namespace radble
