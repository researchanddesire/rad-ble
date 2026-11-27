#include "RadBleResponse.h"

#include <ArduinoJson.h>
#include <cassert>
#include <cstdlib>
#include <new>
#include <string>

using namespace radble;
using namespace radble::response;

namespace {

struct NoHeap : ArduinoJson::Allocator {
  void *allocate(size_t) override { return nullptr; }
  void *reallocate(void *, size_t) override { return nullptr; }
  void deallocate(void *) override {}
};

bool forbidNew = false;
Frame frame;
const uint32_t id = UINT32_MAX;

JsonDocument reply(const Stage &s) {
  forbidNew = true;
  encode(frame, s);
  forbidNew = false;
  assert(frame.size <= MAX_MESSAGE_BYTES && !frame.bytes[frame.size]);
  assert(frame.size == std::strlen(frame.bytes));
  JsonDocument d;
  assert(!deserializeJson(d, frame.bytes, frame.size));
  assert(d["v"].as<int>() == 1 && d["id"].as<uint32_t>() == s.id);
  assert(d["ok"].is<bool>() && d["stage"].is<const char *>());
  return d;
}

void stageTests() {
  // Reproduce the original encoder with the actual ArduinoJson allocator
  // disabled: inserting even mandatory fields silently emitted an empty object.
  NoHeap memory;
  JsonDocument old(&memory);
  old["v"] = 1;
  old["id"] = id;
  old["stage"] = "completed";
  old["ok"] = true;
  char broken[32];
  serializeJson(old, broken, sizeof(broken));
  assert(old.overflowed() && std::strcmp(broken, "{}") == 0);

  auto acquired = reply({id, "completed", true, "", "",
                         R"({"lease":4294967295,"ttlMs":30000})", "", ""});
  assert(acquired["result"]["lease"].as<uint32_t>() == UINT32_MAX);
  auto renewal = reply({id, "completed", true, "", "", R"({"ttlMs":10000})",
                        "streaming.idle", "streaming.idle"});
  assert(renewal["result"]["ttlMs"] == 10000 && renewal["code"] == "ok");

  auto accepted = reply({id, "accepted", true, "", "", "{}", "run", ""});
  assert(accepted["stage"] == "accepted" && accepted["code"].isNull());
  auto stopped = reply({id, "completed", true, "", "", R"({"active":false})",
                        "streaming.idle", "menu.idle"});
  assert(stopped["stage"] == "completed" &&
         stopped["stateAfter"] == "menu.idle");
  assert(stopped["result"]["active"].is<bool>() &&
         !stopped["result"]["active"]);

  const std::string before("one\0two", 7);
  auto escaped = reply({id, "failed", false, "bad\"id", "\\ \t\n\r\x01 🐛",
                        "null", before, "a\xffz"});
  assert(escaped["code"] == "bad\"id" &&
         escaped["message"] == "\\ \t\n\r\x01 🐛");
  assert(strstr(frame.bytes, "one\\u0000two") &&
         escaped["stateAfter"] == "a�z");
  assert(!escaped["ok"] && escaped["result"].isNull());
}

void boundaries() {
  std::string message(1, 'x');
  reply({id, "completed", true, "", message, "", "", ""});
  message.resize(1 + MAX_MESSAGE_BYTES - frame.size, 'x');
  reply({id, "completed", true, "", message, "", "", ""});
  assert(frame.size == MAX_MESSAGE_BYTES);
  message += 'x';
  auto large = reply({id, "completed", true, "", message, "", "", ""});
  assert(large["stage"] == "failed" && !large["ok"] &&
         large["code"] == "response_too_large");
  auto provisional =
      reply({id, "accepted", true, "", "", "{}", message, message});
  assert(provisional["stage"] == "accepted" && provisional["ok"]);
  assert(provisional["stateBefore"].isNull() && provisional["code"].isNull());

  for (const char *bad :
       {"{", "[", R"({"ttlMs":})", R"({"a":0}junk)", R"([1,])", R"({"a":1,})",
        "01", "1e", "1.", R"("\q")", R"("\u234x")", R"(["hello"}])",
        "true false", "[+1]", "{a:1}", "   ", "\"\xff\"", "\""}) {
    auto d = reply({id, "completed", true, "", "", bad, "", ""});
    assert(d["stage"] == "failed" && d["code"] == "resource_unavailable");
  }
  for (const char *good : {"[]", "{}", "-1.50e+10", "true", " false ", "null",
                           R"([{"n":0},[{"s":"\u0041/🐛"}],false])"}) {
    const auto d = reply({id, "completed", true, "", "", good, "", ""});
    assert(d["stage"] == "completed");
  }
  auto missing = reply({id, "completed", true, "", "", nullptr, "", ""});
  assert(missing["stage"] == "failed" &&
         missing["code"] == "resource_unavailable");
  // Nesting depth does not consume recursive stack or require a DOM.
  std::string deep(190, '[');
  deep += '0';
  deep.append(190, ']');
  encode(frame, {id, "completed", true, "", "", deep, "", ""});
  assert(strstr(frame.bytes, deep.c_str()));
  auto bad =
      reply({id, "completed", true, "", "", std::string(420, '['), "", ""});
  assert(bad["code"] == "resource_unavailable");
}

// Arduino String::concat reports false if reserve/realloc fails. Mimic its
// failure (including leaving prior bytes in storage) and exercise the same
// cache as the firmware.
struct Copy {
  static bool deny;
  std::string data;
  void clear() { data.clear(); }
  bool concat(const char *s, size_t n) {
    if (deny)
      return false;
    data.assign(s, n);
    return true;
  }
};
bool Copy::deny = false;

void cacheAndSessions() {
  Cache<Copy, 4> cache;
  Sessions<2> sessions;
  const uint32_t a = sessions.get(7, true), b = sessions.get(9, true);
  assert(a && b && a != b && sessions.get(7, true) == a &&
         !sessions.get(4, true));
  int mutations = 0;
  auto execute = [&](uint32_t session, uint32_t key) {
    if (!cache.lookup(session, key)) {
      ++mutations;
      encode(frame, {key, "completed", true, "", "", "{}", "", ""});
      cache.remember(session, key, frame);
    }
  };
  execute(a, 10);
  execute(a, 10);
  assert(mutations == 1 && cache.lookup(a, 10)->data == frame.bytes);
  Copy::deny = true;
  execute(a, 11);
  execute(a, 11);
  assert(mutations == 2 && cache.lookup(a, 11)->data.empty());
  sessions.close(7);
  assert(!sessions.get(7, false) && sessions.get(9, false) == b);
  const auto next = sessions.get(7, true);
  assert(next != a && !cache.lookup(next, 11));
  execute(next, 11);
  execute(b, 11);
  execute(b, 11);
  assert(mutations == 4 && cache.lookup(a, 10));
  execute(b, 12);
  assert(!cache.lookup(a, 10) && cache.lookup(a, 11) && cache.lookup(b, 11));
  sessions.closeAll();
  assert(!sessions.get(7, false) && !sessions.get(9, false));
  assert(sessions.get(9, true) != b);
}

} // namespace

void *operator new(std::size_t n) {
  assert(!forbidNew);
  if (void *p = std::malloc(n))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

int main() {
  stageTests();
  boundaries();
  cacheAndSessions();
}
