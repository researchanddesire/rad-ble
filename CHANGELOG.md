# Changelog

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
