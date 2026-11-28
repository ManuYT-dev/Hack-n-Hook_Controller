#include "rod.h"
#include "BlePackets.h"

ble::Output controller;
float aimX = 0.0f, aimY = 0.0f, castSpeed = 0.0f;

// MPU6050 configuration in rod.h: +/-8 g and +/-1000 degrees per second.
constexpr float accelCountsPerG = 4096.0f;
constexpr float gyroCountsPerDegreePerSecond = 32.8f;
constexpr float castFullScaleDegreesPerSecond = 1000.0f;

void setup() {
  Serial.begin(115200);
  controller.begin();
  rod::begin();
}

void loop() {
  const rod::Sample p = rod::sample();
  controller.button(ble::Button::Rell, p.rellActive);

  if (p.imuValid) {
    aimX = p.accel[0] / accelCountsPerG;
    aimY = p.accel[1] / accelCountsPerG;
    castSpeed = p.gyro[1] / (gyroCountsPerDegreePerSecond * castFullScaleDegreesPerSecond);
  } else if (rod::imuState != rod::ImuState::running) {
    aimX = aimY = castSpeed = 0.0f;
  }
  // Refresh levels so a newly subscribed client receives the current state.
  controller.aim(aimX, aimY);
  controller.axis(ble::Axis::CastSpeed, castSpeed);
  Serial.printf("Rell=%d | Encoder: %ld | MPU6050: %s\n", int(p.rellActive),
                static_cast<long>(p.encoder),
                p.imuValid ? "OK" : "keine neuen Daten");
  delay(100);
}
