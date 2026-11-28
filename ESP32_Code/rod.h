#pragma once  // Include this header only once per compilation unit.
#include <Arduino.h>  // Provide Arduino pin, interrupt, timing and serial functions.
#include <Wire.h>  // Provide the Wire interface for the I2C sensor bus.

namespace rod {  // Group the sensor code under the rod name, as in rod::sample().
constexpr int clk = 4, dt = 5;  // Map the encoder's CLK and DT outputs to Heltec GPIO4 and GPIO5.
volatile int32_t encoderEdges = 0;  // Keep the signed crank count shared with the interrupt handler.
constexpr uint32_t crankIdleMs = 75;  // Inaktiv nach 75 ms ohne gültige Encoder-Flanke.
volatile uint32_t lastCrankEdgeMs = 0;
volatile bool crankActive = false;
uint8_t previousAB;  // Remember the last two encoder input levels to determine direction.
portMUX_TYPE encoderLock = portMUX_INITIALIZER_UNLOCKED;  // Protect the encoder state while the interrupt or sampling code accesses it.
DRAM_ATTR const int8_t quadrature[16] = {  // Keep the direction lookup table in RAM for use during interrupts.
   0, 1,-1, 0, -1, 0, 0, 1, 1, 0, 0,-1, 0,-1, 1, 0  // For each old/new state pair, add +1 or -1 for a valid step, or 0 for no change or an invalid jump.
};  // End the 16-entry direction table.

void ARDUINO_ISR_ATTR encoderChanged() {  // Handle either encoder input changing, even between BLE samples.
  portENTER_CRITICAL_ISR(&encoderLock);  // Lock the encoder state before updating it inside the interrupt.
  uint8_t ab = (digitalRead(clk) << 1) | digitalRead(dt);  // Pack CLK into bit 1 and DT into bit 0 to get a state from 0 to 3.
  const int8_t step = quadrature[(previousAB << 2) | ab];
  if (step != 0) {
    encoderEdges += step;
    lastCrankEdgeMs = millis();
    crankActive = true;
  }
  previousAB = ab;  // Save this state as the starting point for the next edge.
  portEXIT_CRITICAL_ISR(&encoderLock);  // Release the encoder lock so the main loop can read the count.
}  // End the encoder interrupt handler.

constexpr int sdaPin = 41, sclPin = 42;  // External I2C data and clock pins.
enum class ImuState { retry, resetting, warming, running };  // Track recovery across BLE samples.
ImuState imuState = ImuState::retry;  // Start by looking for the sensor.
uint8_t address;  // I2C address of the chip currently being configured or sampled.
bool busReady;  // True while the ESP32 Wire driver owns the pins.
uint32_t stateSince;  // Millisecond timestamp for the current wait.
uint32_t lastFresh;  // Time of the most recent data-ready event or start of sampling.

bool readRegs(uint8_t addr, uint8_t reg, uint8_t *bytes, size_t count) {  // Read count consecutive registers into the caller's byte buffer.
  Wire.beginTransmission(addr);  // Select the I2C device whose registers we want to read.
  Wire.write(reg);  // Queue the address of the first register to read.
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, count) != count) return false;  // Keep the bus for a repeated start, then reject a failed register selection or short read.
  return Wire.readBytes(bytes, count) == count;  // Copy the received bytes into the buffer and report whether all arrived.
}  // End the register read helper.

bool writeReg(uint8_t addr, uint8_t reg, uint8_t value) {  // Write one byte to one device register and report success.
  Wire.beginTransmission(addr);  // Select the device that should receive the setting.
  Wire.write(reg);  // Queue the register address first.
  Wire.write(value);  // Queue the new setting immediately after the address.
  return Wire.endTransmission() == 0;  // Send both bytes and return true if Wire reports success.
}  // End the register write helper.

void retryImu() {  // Abandon the failed transaction and schedule a fresh start.
  if (busReady) Wire.end();  // Release the ESP32 I2C controller and both pins.
  busReady = false;  // The next probe must start a new bus session.
  imuState = ImuState::retry;  // Keep sending crank packets while motion is unavailable.
  stateSince = millis();  // Give a bouncing connector two seconds before retrying.
  Serial.println("MPU6050 offline; retrying in 2 s");  // Show the recovery state on USB.
}  // End recovery scheduling.

bool releaseBus() {  // Let a peripheral finish a byte interrupted by a loose contact.
  pinMode(sdaPin, INPUT_PULLUP);  // Release the data line and read its actual level.
  pinMode(sclPin, INPUT_PULLUP);  // Release the clock line before checking it.
  delayMicroseconds(10);  // Allow the pull-ups to bring idle lines high.
  if (!digitalRead(sclPin)) return false;  // A held-low clock needs a later retry.
  if (digitalRead(sdaPin)) return true;  // An idle bus needs no recovery pulses.
  pinMode(sclPin, OUTPUT_OPEN_DRAIN);  // Permit clock pulses without driving a high level.
  for (int pulse = 0; pulse < 9 && !digitalRead(sdaPin); ++pulse) {  // Clock out at most one byte and its ACK.
    digitalWrite(sclPin, LOW);  // Begin the low half of the clock pulse.
    delayMicroseconds(10);  // Give the sensor time to observe the low level.
    digitalWrite(sclPin, HIGH);  // Release the clock for its rising edge.
    delayMicroseconds(10);  // Allow the clock to rise through its pull-up.
    if (!digitalRead(sclPin)) break;  // Stop if the clock remains physically held low.
  }  // End the bounded bus-clear sequence.
  pinMode(sclPin, INPUT_PULLUP);  // Leave the clock released between attempts.
  return digitalRead(sdaPin) && digitalRead(sclPin);  // Start Wire only after both lines are high.
}  // End bus recovery.

void probe() {  // Start a new I2C session and reset an identified MPU6050.
  if (releaseBus()) busReady = Wire.begin(sdaPin, sclPin, 100000);  // Use 100 kHz when the bus is idle.
  if (busReady) {  // The controller is available for a bounded probe.
    Wire.setTimeOut(10);  // Limit each I2C transfer to ten milliseconds.
    for (uint8_t addr = 0x68; addr <= 0x69; ++addr) {  // Try both MPU6050 address options.
      uint8_t id;  // Hold the WHO_AM_I register value.
      if (!readRegs(addr, 0x75, &id, 1) || id != 0x68) continue;  // Reset only a matching device.
      if (!writeReg(addr, 0x6B, 0x80)) break;  // Request a full device reset through PWR_MGMT_1.
      address = addr;  // Remember which chip acknowledged the reset.
      imuState = ImuState::resetting;  // Wait for reset completion across successive loop calls.
      stateSince = millis();  // Start the reset settling interval.
      return;  // Let the encoder and BLE continue during the wait.
    }  // End the address search.
  }  // End the bus probe.
  retryImu();  // Bus unavailable, sensor absent, or reset write failed.
}  // End the MPU6050 probe.

void advanceImu() {  // Advance initialization without blocking BLE during settling periods.
  const uint32_t elapsed = millis() - stateSince;  // Unsigned subtraction tolerates clock wrap.
  if (imuState == ImuState::retry && elapsed >= 2000) {  // Retry while the contact is open.
    probe();  // Reopen the bus and issue a fresh device reset.
  } else if (imuState == ImuState::resetting && elapsed >= 100) {  // Wait 100 ms after reset.
    if (!writeReg(address, 0x6B, 1)) return retryImu();  // Wake the chip using the X gyro PLL.
    stateSince = millis();  // Give the gyro time to stabilize.
    imuState = ImuState::warming;  // Continue with configuration after warm-up.
  } else if (imuState == ImuState::warming && elapsed >= 100) {  // Wake-up settling is complete.
    if (!writeReg(address, 0x1A, 4) ||  // Restore the roughly 20 Hz low-pass filter.
        !writeReg(address, 0x1B, 0x10) ||  // Restore the +/-1000 degrees/s gyro range.
        !writeReg(address, 0x1C, 0x10) ||  // Restore the +/-8 g acceleration range.
        !writeReg(address, 0x38, 1)) return retryImu();  // Enable the data-ready status bit.
    lastFresh = millis();  // Give the chip time to produce the first fresh reading.
    imuState = ImuState::running;  // Samples may now read the status and motion registers.
    Serial.printf("MPU6050 restarted at 0x%02X\n", address);  // Log successful configuration.
  }  // End recovery-state handling.
}  // End initialization and retry management.

void begin() {  // Initialize the rod inputs once at startup.
  pinMode(clk, INPUT_PULLUP);  // Hold CLK high internally when its encoder contact is open.
  pinMode(dt, INPUT_PULLUP);  // Hold DT high internally when its encoder contact is open.
  previousAB = (digitalRead(clk) << 1) | digitalRead(dt);  // Read the actual resting state so the first transition has a reference.
  attachInterrupt(clk, encoderChanged, CHANGE);  // Count rising and falling CLK edges using the shared handler.
  attachInterrupt(dt, encoderChanged, CHANGE);  // Count rising and falling DT edges so both phases contribute to direction.
  probe();  // Find and configure the motion sensor after enabling crank counting.
}  // End rod input initialization.

// Local sensor sample. BLE encoding is handled by BlePackets.h.
struct Sample {
  int16_t accel[3];
  int16_t gyro[3];
  int32_t encoder;
  bool rellActive;
  bool imuValid;  // Fresh motion reading in this sample.
};

int16_t signedWord(const uint8_t *bytes) {  // Decode a signed two-byte value supplied high byte first by the MPU6050.
  return int16_t((uint16_t(bytes[0]) << 8) | bytes[1]);  // Move the high byte up eight bits, combine it with the low byte, then interpret the sign.
}  // End the sensor word decoder.

Sample sample() {
  Sample p{};
  advanceImu();  // Progress any pending reset while still publishing crank data.
  uint8_t bytes[15];  // Status byte followed by the 14-byte motion and temperature block.
  if (imuState == ImuState::running) {  // Read only after reset, wake-up and configuration.
    if (!readRegs(address, 0x3A, bytes, sizeof(bytes))) {  // Read status and motion together.
      retryImu();  // A failed or short transfer invalidates this sample immediately.
    } else if (bytes[0] & 1) {  // DATA_RDY confirms a new measurement, even on a stationary rod.
      lastFresh = millis();  // Restart the stale-data timer on each fresh sample.
      for (int axis = 0; axis < 3; ++axis) {  // Decode X, Y and Z in order.
        p.accel[axis] = signedWord(bytes + 1 + 2 * axis);  // Acceleration follows the status byte.
        p.gyro[axis] = signedWord(bytes + 9 + 2 * axis);  // Skip status, acceleration and temperature.
      }  // End three-axis conversion.
      p.imuValid = true;  // Mark only fresh motion readings valid.
    } else if (millis() - lastFresh >= 250) {  // The sensor answers but has stopped producing data.
      retryImu();  // Restart a stalled or unexpectedly sleeping sensor.
    }  // End freshness and transfer checks.
  }  // End motion sampling.
  portENTER_CRITICAL(&encoderLock);  // Take the encoder lock before copying the shared count.
  if (crankActive && uint32_t(millis() - lastCrankEdgeMs) >= crankIdleMs) {
    crankActive = false;
  }
  p.rellActive = crankActive;
  p.encoder = encoderEdges;  // Capture the cumulative count even when the motion sensor is unavailable.
  portEXIT_CRITICAL(&encoderLock);  // Release the lock immediately so crank interrupts can continue.
  return p;  // Return the local sample to the sketch.
}  // End sample collection.
}  // End the rod namespace.
