#pragma once

// Request envelope parsing for RAD BLE v1. Pure: depends only on ArduinoJson
// and the generated protocol constants, so tests/native builds it with a host
// compiler.

#include <ArduinoJson.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "RadBleProtocol.generated.h"

namespace radble {

struct RequestCheck {
  bool ok;
  uint32_t id;          // request id; 0 when the request could not be read
  const char *code;     // error code when !ok
  const char *message;  // error message when !ok
};

// A characteristic write fits the server queue (enqueue copies it into a
// fixed MAX_MESSAGE_BYTES buffer).
inline bool fitsRequestQueue(size_t length) {
  return length > 0 && length <= MAX_MESSAGE_BYTES;
}

// Parses `length` bytes (no terminator needed) into `document`, applies the
// defaults v1 allows, and validates the envelope:
// - a missing or non-integer `id` becomes `fallbackId`;
// - a missing `v` becomes `protocolVersion`, a missing `op` becomes
//   `defaultOperation` (the operation implied by the characteristic written);
// - device-name writes may send {"name": ...} and omit path/args.value;
// - OTA control writes may send op "ota.control" with args.action.
// The request must then be a JSON object with v == protocolVersion (an
// integer), a non-zero id and a non-empty string op.
inline RequestCheck parseRequestEnvelope(JsonDocument &document,
                                         const uint8_t *data, size_t length,
                                         const char *defaultOperation,
                                         bool deviceNameWrite,
                                         bool otaControlWrite,
                                         uint32_t fallbackId,
                                         int protocolVersion) {
  if (data == nullptr || length == 0 ||
      deserializeJson(document, data, length) != DeserializationError::Ok)
    return {false, 0, "invalid_request", "Request is not valid JSON"};
  if (!document.is<JsonObject>())
    return {false, 0, "invalid_request", "Request must be a JSON object"};

  if (!document["id"].is<uint32_t>())
    document["id"] = fallbackId;
  if (document["v"].isNull())
    document["v"] = protocolVersion;
  if (document["op"].isNull() && defaultOperation != nullptr)
    document["op"] = defaultOperation;

  if (deviceNameWrite) {
    if (document["path"].isNull())
      document["path"] = "device.name";
    JsonObject args = document["args"].is<JsonObject>()
                          ? document["args"].as<JsonObject>()
                          : document["args"].to<JsonObject>();
    if (args["value"].isNull() && !document["name"].isNull())
      args["value"] = document["name"];
  }

  if (otaControlWrite && document["op"].is<const char *>() &&
      strcmp(document["op"].as<const char *>(), "ota.control") == 0) {
    const char *action = document["args"]["action"] | "";
    if (*action != '\0')
      document["op"] = std::string("ota.") + action;
  }

  const uint32_t id = document["id"].as<uint32_t>();
  // Compare as an integer: reading v into a uint8_t would let 257 pass as 1.
  const bool versionOk = document["v"].is<int>() &&
                         document["v"].as<int>() == protocolVersion;
  const char *operation =
      document["op"].is<const char *>() ? document["op"].as<const char *>()
                                        : "";
  if (!versionOk || id == 0 || *operation == '\0')
    return {false, id, "invalid_request",
            "v=1, a non-zero id, and op are required"};
  return {true, id, nullptr, nullptr};
}

}  // namespace radble
