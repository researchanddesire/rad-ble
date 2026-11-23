# Changelog

## 1.2.0 - 2026-11-23

- Add `Server::currentRequestConnection()`: the connection whose request the command handler is serving, so products can tell which central commanded what.
- Move request envelope parsing into `RadBleRequest.h` (`parseRequestEnvelope`, `fitsRequestQueue`), host-compiled and tested in `tests/native` with AddressSanitizer and UBSan: malformed, truncated, binary, deeply nested, non-object and oversized input.
- Reject a protocol version that is not the integer 1. Previously `"v": 257` wrapped to 1 and was accepted. Also reject non-string `op` and non-object JSON with a clear message.
- Report the correct `libraryVersion`. 1.1.0 still reported 1.0.0; a test now ties `library.json` to the manifest.

## 1.1.0 - 2026-11-23

- Add the optional `readSnapshotHandler` callback: full state reads stay complete while state notifications stay compact (first shipped untagged in OSSM firmware).
- Add `Config::taskStackBytes` to size the server task stack for products whose handlers do heavy work. 0 keeps the 12288-byte default.
- Log a warning when the server task's remaining stack drops below 1536 bytes, once per new low, so a handler that nearly overflows shows up before it crashes.
- Build with mbedTLS 3 (ESP-IDF 5, Arduino 3) as well as mbedTLS 2: the OTA SHA-256 calls no longer use the removed `*_ret` functions.

## 1.0.0 - 2026-07-28

- Publish the shared RAD BLE v1 server implementation.
- Add an authoritative JSON protocol manifest and generated constants.
- Add explicit optional channel selection and configuration validation.
- Preserve device-specific resource catalogs and callbacks in product firmware.
