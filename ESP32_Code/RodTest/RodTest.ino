#include "rod.h"
#include "BlePackets.h"

constexpr uint8_t crankButtonPin = 6;  // Taster zwischen GPIO und GND
constexpr uint32_t debounceMs = 20;
constexpr uint32_t refreshMs = 500;
constexpr int castDeadband = 164;  // ~5 Grad/s, MPU6050 bei +/-1000 Grad/s

ble::Output controller;
bool pressed = false, lastButtonReading = false;
uint32_t buttonChangedAt = 0;
int16_t castSpeed = 0;

struct SentState {
  bool active;
  int16_t value;
  uint32_t sentAt;
  bool valid;
};
SentState lastSent[4]{};

void publish(ble::Mode mode, bool active, int16_t value) {
  SentState& last = lastSent[static_cast<uint8_t>(mode)];
  const uint32_t now = millis();
  if (last.valid && last.active == active && last.value == value &&
      uint32_t(now - last.sentAt) < refreshMs) return;
  if (controller.send(mode, active, value)) last = {active, value, now, true};
}

bool readCrankButton() {
  const bool reading = digitalRead(crankButtonPin) == LOW;
  const uint32_t now = millis();
  if (reading != lastButtonReading) {
    lastButtonReading = reading;
    buttonChangedAt = now;
  }
  if (uint32_t(now - buttonChangedAt) >= debounceMs) pressed = reading;
  return pressed;
}

void setup() {
  Serial.begin(115200);
  pinMode(crankButtonPin, INPUT_PULLUP);
  controller.begin();
  rod::begin();
}

void loop() {
  const rod::Sample sample = rod::sample();
  if (sample.imuValid) {
    const int16_t raw = sample.gyro[1];
    castSpeed = (raw >= castDeadband || raw <= -castDeadband) ? raw : 0;
  } else if (rod::imuState != rod::ImuState::running) {
    castSpeed = 0;
  }
  publish(ble::Mode::Rell, sample.rellDirection != 0, sample.rellDirection);
  publish(ble::Mode::CastSpeed, castSpeed != 0, castSpeed);
  publish(ble::Mode::ButtonPress, readCrankButton(), 0);
  delay(10);
}
