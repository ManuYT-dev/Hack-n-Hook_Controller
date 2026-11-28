#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>

namespace ble {
enum class Mode : uint8_t { Rell = 0, CastSpeed = 1, ButtonPress = 3 };

class Output {
 public:
  void begin() {
    constexpr auto name = "Heltec-Random-Data";
    constexpr auto serviceId = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc01";
    constexpr auto characteristicId = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc02";
    NimBLEDevice::init(name);
    auto* server = NimBLEDevice::createServer();
    server->advertiseOnDisconnect(true);
    auto* service = server->createService(serviceId);
    output = service->createCharacteristic(characteristicId, NIMBLE_PROPERTY::NOTIFY);
    service->start();
    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->enableScanResponse(true);
    advertising->addServiceUUID(serviceId);
    advertising->setName(name);
    advertising->start();
  }

  bool send(Mode mode, bool active, int16_t value = 0) {
    const uint16_t bits = static_cast<uint16_t>(value);
    const uint8_t packet[3] = {
      static_cast<uint8_t>((static_cast<uint8_t>(mode) << 1) | uint8_t(active)),
      static_cast<uint8_t>(bits),
      static_cast<uint8_t>(bits >> 8)
    };
    return output && output->notify(packet, sizeof(packet));
  }

 private:
  NimBLECharacteristic* output = nullptr;
};
}  // namespace ble
