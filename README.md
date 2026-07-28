# RadBLE

RadBLE is the shared ESP32 implementation and machine-readable contract for the Research and Desire BLE Control Protocol v1. It gives OSSM, RADR, DTT, and LKBX one wire language while keeping product behavior in each firmware repository.

## PlatformIO

Pin an immutable release in `lib_deps`:

```ini
lib_deps =
    https://github.com/researchanddesire/rad-ble.git#v1.0.0
```

The library requires Arduino on ESP32, NimBLE-Arduino 2.4.0, and ArduinoJson 7.4 or newer.

## Device integration

Every server declares identity, capabilities, enabled optional GATT channels, resources, and callbacks. Core protocol, catalog, name, identity, request, response, state, essential-state, and event characteristics are always available. Everything else is opt-in through `Config::channels`.

```cpp
const radble::Config config = {
    .identity = {
        .deviceType = "EXAMPLE",
        .deviceName = "EXAMPLE",
        .serviceUuid = "522b443a-4558-414d-0001-420badbabe69",
        .firmwareVersion = "1.0.0",
        .build = "release",
        .partitionLayout = nullptr,
    },
    .capabilities = radble::CAP_BUTTON | radble::CAP_SENSOR_STREAM,
    .channels = radble::CHANNEL_BUTTON | radble::CHANNEL_SENSOR_STREAM,
    .resources = resources,
    .resourceCount = resourceCount,
    .callbacks = {
        .commandHandler = handleCommand,
        .snapshotHandler = snapshot,
        .otaDataHandler = nullptr,
        .otaSafetyHandler = nullptr,
        .leaseReleaseHandler = nullptr,
        .streamSafetyHandler = nullptr,
    },
    .context = nullptr,
};
```

Invalid combinations are rejected during `Server::begin` with a specific `[RAD BLE] invalid config:` serial message. A channel that represents a capability must have the matching capability bit; filesystem OTA requires application OTA.

## Protocol contract

[`protocol/rad-ble-v1.json`](protocol/rad-ble-v1.json) is authoritative for product service UUIDs, characteristic suffixes and properties, operations, error codes, and size limits. Generated C++ and TypeScript constants keep firmware and clients aligned.

Regenerate or verify the checked-in C++ header:

```sh
python3 scripts/generate_protocol.py
python3 scripts/generate_protocol.py --check
```

Generate a TypeScript module for a client repository:

```sh
python3 scripts/generate_protocol.py --typescript /path/to/ble.generated.ts
```

## Compatibility

Releases follow semantic versioning. Protocol-compatible additions use a minor release; breaking C++ or wire changes use a major release. Firmware consumers should always pin a tag.
