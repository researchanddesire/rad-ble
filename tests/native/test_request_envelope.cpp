// Host-compiled tests for RAD BLE v1 request envelope parsing.
// Build: c++ -std=c++17 -I src -I <dir with ArduinoJson.h> tests/native/test_request_envelope.cpp

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "RadBleRequest.h"

using radble::fitsRequestQueue;
using radble::MAX_MESSAGE_BYTES;
using radble::parseRequestEnvelope;
using radble::RequestCheck;

namespace {

int failures = 0;

void expect(bool condition, const char *what, int line) {
  if (condition) return;
  ++failures;
  std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
}
#define EXPECT(condition) expect((condition), #condition, __LINE__)

RequestCheck parse(JsonDocument &document, const std::string &text,
                   const char *defaultOperation = "request",
                   bool deviceName = false, bool otaControl = false,
                   uint32_t fallbackId = 77) {
  return parseRequestEnvelope(
      document, reinterpret_cast<const uint8_t *>(text.data()), text.size(),
      defaultOperation, deviceName, otaControl, fallbackId, 1);
}

bool rejected(const std::string &text, uint32_t fallbackId = 77) {
  JsonDocument document;
  const RequestCheck check =
      parse(document, text, "request", false, false, fallbackId);
  return !check.ok && check.code != nullptr &&
         std::strcmp(check.code, "invalid_request") == 0;
}

void testValidRequest() {
  JsonDocument document;
  const RequestCheck check =
      parse(document, R"({"v":1,"id":42,"op":"target.set","path":"target.menu","lease":7})");
  EXPECT(check.ok);
  EXPECT(check.id == 42);
  EXPECT(std::strcmp(document["op"].as<const char *>(), "target.set") == 0);
  EXPECT(document["lease"].as<int>() == 7);
}

void testDefaults() {
  JsonDocument document;
  const RequestCheck check = parse(document, "{}", "input.emit");
  EXPECT(check.ok);
  EXPECT(check.id == 77);
  EXPECT(document["v"].as<int>() == 1);
  EXPECT(std::strcmp(document["op"].as<const char *>(), "input.emit") == 0);
}

void testNonIntegerIdUsesFallback() {
  JsonDocument document;
  EXPECT(parse(document, R"({"v":1,"id":"5","op":"state.read"})").id == 77);
  JsonDocument negative;
  EXPECT(parse(negative, R"({"v":1,"id":-3,"op":"state.read"})").id == 77);
  JsonDocument fraction;
  EXPECT(parse(fraction, R"({"v":1,"id":1.5,"op":"state.read"})").id == 77);
}

void testZeroIdRejected() {
  EXPECT(rejected(R"({"v":1,"id":0,"op":"state.read"})"));
  EXPECT(rejected(R"({"v":1,"op":"state.read"})", 0));
}

void testVersionChecks() {
  EXPECT(rejected(R"({"v":2,"id":1,"op":"state.read"})"));
  EXPECT(rejected(R"({"v":0,"id":1,"op":"state.read"})"));
  // 257 must not wrap to 1.
  EXPECT(rejected(R"({"v":257,"id":1,"op":"state.read"})"));
  EXPECT(rejected(R"({"v":"1","id":1,"op":"state.read"})"));
  EXPECT(rejected(R"({"v":1.5,"id":1,"op":"state.read"})"));
  EXPECT(rejected(R"({"v":true,"id":1,"op":"state.read"})"));
}

void testOperationChecks() {
  EXPECT(rejected(R"({"v":1,"id":1,"op":""})"));
  EXPECT(rejected(R"({"v":1,"id":1,"op":123})"));
  EXPECT(rejected(R"({"v":1,"id":1,"op":["target.set"]})"));
  JsonDocument document;
  EXPECT(!parse(document, R"({"v":1,"id":1})", nullptr).ok);
}

void testMalformedJson() {
  EXPECT(rejected(""));
  EXPECT(rejected("{"));
  EXPECT(rejected(R"({"v":1,"id":2,)"));
  EXPECT(rejected(R"({"v":1,"id":2,"op":"state.read")"));
  EXPECT(rejected("not json"));
  EXPECT(rejected("[1,2,3]"));
  EXPECT(rejected("42"));
  EXPECT(rejected(R"("text")"));
  EXPECT(rejected("null"));
  JsonDocument document;
  EXPECT(!parseRequestEnvelope(document, nullptr, 10, "request", false, false,
                               1, 1).ok);
}

void testBinaryGarbage() {
  std::string bytes;
  for (int value = 0; value < 256; ++value)
    bytes.push_back(static_cast<char>(value));
  EXPECT(rejected(bytes));
  EXPECT(rejected(std::string("\x00\x01\xff{", 4)));
}

void testDeepNestingRejected() {
  std::string text = R"({"v":1,"id":1,"op":"state.read","args":)";
  for (int level = 0; level < 64; ++level) text += "[";
  for (int level = 0; level < 64; ++level) text += "]";
  text += "}";
  EXPECT(rejected(text));
}

void testLengthIsRespected() {
  // A valid prefix followed by bytes outside the given length is parsed
  // without reading past `length`.
  const std::string text = R"({"v":1,"id":9,"op":"state.read"}GARBAGE)";
  JsonDocument document;
  const RequestCheck check = parseRequestEnvelope(
      document, reinterpret_cast<const uint8_t *>(text.data()),
      text.size() - 7, "request", false, false, 1, 1);
  EXPECT(check.ok);
  EXPECT(check.id == 9);
}

void testQueueBounds() {
  EXPECT(!fitsRequestQueue(0));
  EXPECT(fitsRequestQueue(1));
  EXPECT(fitsRequestQueue(MAX_MESSAGE_BYTES));
  EXPECT(!fitsRequestQueue(MAX_MESSAGE_BYTES + 1));
  EXPECT(!fitsRequestQueue(static_cast<size_t>(-1)));
}

void testDeviceNameShorthand() {
  JsonDocument document;
  const RequestCheck check =
      parse(document, R"({"name":"Rig"})", "setting.write", true);
  EXPECT(check.ok);
  EXPECT(std::strcmp(document["path"].as<const char *>(), "device.name") == 0);
  EXPECT(std::strcmp(document["args"]["value"].as<const char *>(), "Rig") == 0);
  // An explicit value wins over the shorthand; non-object args are replaced.
  JsonDocument explicitValue;
  EXPECT(parse(explicitValue, R"({"name":"A","args":{"value":"B"}})",
               "setting.write", true).ok);
  EXPECT(std::strcmp(explicitValue["args"]["value"].as<const char *>(), "B") == 0);
  JsonDocument badArgs;
  EXPECT(parse(badArgs, R"({"name":"A","args":5})", "setting.write", true).ok);
  EXPECT(std::strcmp(badArgs["args"]["value"].as<const char *>(), "A") == 0);
}

void testOtaControlShorthand() {
  JsonDocument document;
  EXPECT(parse(document, R"({"v":1,"id":3,"op":"ota.control","args":{"action":"begin"}})",
               "ota.control", false, true).ok);
  EXPECT(std::strcmp(document["op"].as<const char *>(), "ota.begin") == 0);
  JsonDocument noAction;
  EXPECT(parse(noAction, R"({"v":1,"id":3,"op":"ota.control"})", "ota.control",
               false, true).ok);
  EXPECT(std::strcmp(noAction["op"].as<const char *>(), "ota.control") == 0);
  JsonDocument otherWrite;
  EXPECT(parse(otherWrite, R"({"v":1,"id":3,"op":"ota.control","args":{"action":"begin"}})",
               "request", false, false).ok);
  EXPECT(std::strcmp(otherWrite["op"].as<const char *>(), "ota.control") == 0);
}

void testMaximumSizeRequest() {
  std::string text = R"({"v":1,"id":5,"op":"setting.write","args":{"value":")";
  const std::string tail = R"("}})";
  text += std::string(MAX_MESSAGE_BYTES - text.size() - tail.size(), 'x');
  text += tail;
  EXPECT(text.size() == MAX_MESSAGE_BYTES);
  JsonDocument document;
  EXPECT(parse(document, text).ok);
}

}  // namespace

int main() {
  testValidRequest();
  testDefaults();
  testNonIntegerIdUsesFallback();
  testZeroIdRejected();
  testVersionChecks();
  testOperationChecks();
  testMalformedJson();
  testBinaryGarbage();
  testDeepNestingRejected();
  testLengthIsRespected();
  testQueueBounds();
  testDeviceNameShorthand();
  testOtaControlShorthand();
  testMaximumSizeRequest();
  if (failures != 0) {
    std::fprintf(stderr, "%d request envelope check(s) failed\n", failures);
    return 1;
  }
  std::puts("request envelope tests passed");
  return 0;
}
