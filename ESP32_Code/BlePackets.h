#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <math.h>

namespace ble {
enum class Map : uint8_t { FishingRod = 1, Boat = 2, Global = 3 };
constexpr uint8_t header(Map map, uint8_t action, uint8_t type) {
  return (action << 4) | (static_cast<uint8_t>(map) << 2) | type;
}

// Button values are the RELEASE headers. Bit 0 signals a press.
enum class Button : uint8_t {
  Rell          = header(Map::FishingRod, 0, 0),
  Throw         = header(Map::FishingRod, 1, 0),
  CancelCast    = header(Map::FishingRod, 5, 0),
  SwitchBait    = header(Map::FishingRod, 6, 0),
  ToggleReady   = header(Map::FishingRod, 7, 0),
  Anchor        = header(Map::Boat, 1, 0),
  Interact      = header(Map::Boat, 2, 0),
  ToggleFishing = header(Map::Boat, 3, 0),
  Boost         = header(Map::Boat, 4, 0),
  Pause         = header(Map::Global, 0, 0)
};
enum class Axis : uint8_t {
  Drive     = header(Map::Boat, 0, 2),
  CastSpeed = header(Map::FishingRod, 8, 2)
};

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

  bool button(Button action, bool pressed) {
    const uint8_t packet = static_cast<uint8_t>(action) | uint8_t(pressed);
    return send(&packet, 1);
  }

  bool axis(Axis action, float value) {
    uint8_t packet[3] = {static_cast<uint8_t>(action)};
    putAxis(packet + 1, value);
    return send(packet, sizeof(packet));
  }

  bool aim(float x, float y) {
    uint8_t packet[5] = {header(Map::FishingRod, 2, 3)};
    putAxis(packet + 1, x);
    putAxis(packet + 3, y);
    return send(packet, sizeof(packet));
  }

 private:
  NimBLECharacteristic* output = nullptr;

  static void putAxis(uint8_t* dst, float value) {
    if (isnan(value)) value = 0.0f;
    const float limited = fmaxf(-1.0f, fminf(1.0f, value));
    const auto bits = static_cast<uint16_t>(lroundf(limited * 32767.0f));
    dst[0] = static_cast<uint8_t>(bits);
    dst[1] = static_cast<uint8_t>(bits >> 8);
  }

  bool send(const uint8_t* packet, size_t size) {
    if (!output) return false;
    for (int attempt = 0; attempt < 5; ++attempt) {
      if (output->notify(packet, size)) return true;
      delay(2);
    }
    return false;
  }
};
}  // namespace ble
