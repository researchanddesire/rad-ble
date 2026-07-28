#include <Arduino.h>
#include <NimBLEDevice.h>
#include <RadBle.h>

namespace {

radble::Server server;

radble::Result handleCommand(JsonObjectConst, void *) {
  return radble::Result::failure("unsupported",
                                 "No product commands configured");
}

String snapshot(radble::Surface surface, void *) {
  if (surface == radble::Surface::Essential)
    return R"({"state":"ready","powered":true})";
  return R"({"state":"ready"})";
}

} // namespace

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("RAD BLE Example");
  auto *nimbleServer = NimBLEDevice::createServer();
  const radble::Config config = {
      .identity =
          {
              .deviceType = "EXAMPLE",
              .deviceName = "RAD BLE Example",
              .serviceUuid = "522b443a-4558-414d-0001-420badbabe69",
              .firmwareVersion = "1.0.0",
              .build = "example",
              .partitionLayout = nullptr,
          },
      .capabilities = 0,
      .channels = radble::CHANNEL_NONE,
      .resources = nullptr,
      .resourceCount = 0,
      .callbacks =
          {
              .commandHandler = handleCommand,
              .snapshotHandler = snapshot,
              .otaDataHandler = nullptr,
              .otaSafetyHandler = nullptr,
              .leaseReleaseHandler = nullptr,
              .streamSafetyHandler = nullptr,
          },
      .context = nullptr,
  };
  if (!server.begin(nimbleServer, config))
    return;
  auto *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(server.serviceUuid());
  advertising->start();
}

void loop() { delay(1000); }
