#include <Wire.h>

// MPU6050
#define MPU_ADDR 0x68

// Rotary Encoder
#define CLK_PIN 4
#define DT_PIN 5

// I2C Pins
#define SDA_PIN 41
#define SCL_PIN 42

int16_t accelX, accelY, accelZ;
int16_t gyroX, gyroY, gyroZ;

// Rotary Encoder
volatile long encoderCount = 0;
volatile uint8_t lastState = 0;

long lastEncoderCount = 0;

const int8_t encoderTable[16] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0
};

unsigned long lastPrint = 0;
unsigned long lastReconnect = 0;
unsigned long lastEncoderPrint = 0;

bool mpuConnected = false;

// Encoder Interrupt
void IRAM_ATTR readEncoder() {

  uint8_t state =
    (digitalRead(CLK_PIN) << 1) |
     digitalRead(DT_PIN);

  encoderCount += encoderTable[
    (lastState << 2) | state
  ];

  lastState = state;
}

// MPU initialisieren
bool initMPU() {

  Wire.beginTransmission(MPU_ADDR);

  if (Wire.endTransmission() != 0) {
    return false;
  }

  // MPU aufwecken
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);

  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(100);

  Serial.println("MPU6050 initialisiert!");
  return true;
}

// Alle MPU-Messwerte auf einmal auslesen
bool readMPU() {

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  int bytes = Wire.requestFrom(
    MPU_ADDR, 14, true
  );

  if (bytes != 14) {
    return false;
  }

  accelX = (Wire.read() << 8) | Wire.read();
  accelY = (Wire.read() << 8) | Wire.read();
  accelZ = (Wire.read() << 8) | Wire.read();

  // Temperatur ueberspringen
  Wire.read();
  Wire.read();

  gyroX = (Wire.read() << 8) | Wire.read();
  gyroY = (Wire.read() << 8) | Wire.read();
  gyroZ = (Wire.read() << 8) | Wire.read();

  return true;
}

void setup() {

  Serial.begin(115200);
  delay(1000);

  // Rotary Encoder initialisieren
  pinMode(CLK_PIN, INPUT_PULLUP);
  pinMode(DT_PIN, INPUT_PULLUP);

  lastState =
    (digitalRead(CLK_PIN) << 1) |
     digitalRead(DT_PIN);

  attachInterrupt(
    digitalPinToInterrupt(CLK_PIN),
    readEncoder, CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(DT_PIN),
    readEncoder, CHANGE
  );

  Serial.println("Rotary Encoder bereit!");

  // I2C initialisieren
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(50000);
  Wire.setTimeOut(100);

  Serial.println("Suche MPU6050...");

  mpuConnected = initMPU();

  if (mpuConnected) {
    Serial.println("MPU6050 bereit!");
  } else {
    Serial.println("MPU nicht gefunden!");
    Serial.println("Encoder trotzdem bereit!");
  }
}

void loop() {

  // ==========================
  // ROTARY ENCODER
  // ==========================

  if (millis() - lastEncoderPrint >= 20) {

    lastEncoderPrint = millis();

    long count = encoderCount;

    if (count != lastEncoderCount) {

      Serial.print("[ENCODER] Richtung: ");

      if (count > lastEncoderCount) {
        Serial.print("Rechts");
      } else {
        Serial.print("Links");
      }

      Serial.print(" | Impulse: ");
      Serial.print(count);

      Serial.print(" | Schritte: ");
      Serial.println(count / 4.0);

      lastEncoderCount = count;
    }
  }

  // ==========================
  // MPU VERBINDUNG PRUEFEN
  // ==========================

  if (millis() - lastReconnect >= 1000) {

    lastReconnect = millis();

    Wire.beginTransmission(MPU_ADDR);
    byte error = Wire.endTransmission();

    if (error == 0) {

      if (!mpuConnected) {
        Serial.println("MPU wieder gefunden!");
        mpuConnected = initMPU();
      }

    } else {

      if (mpuConnected) {
        Serial.println("MPU Verbindung verloren!");
      }

      mpuConnected = false;
    }
  }

  // ==========================
  // MPU MESSWERTE AUSGEBEN
  // ==========================

  if (millis() - lastPrint >= 500) {

    lastPrint = millis();

    if (mpuConnected) {

      if (!readMPU()) {
        Serial.println("MPU Lesefehler!");
        mpuConnected = false;
        return;
      }

      Serial.println();
      Serial.println("----- MPU6050 -----");

      Serial.print("Accel X: ");
      Serial.print(accelX / 16384.0);

      Serial.print(" g | Y: ");
      Serial.print(accelY / 16384.0);

      Serial.print(" g | Z: ");
      Serial.print(accelZ / 16384.0);
      Serial.println(" g");

      Serial.print("Gyro X: ");
      Serial.print(gyroX / 131.0);

      Serial.print(" deg/s | Y: ");
      Serial.print(gyroY / 131.0);

      Serial.print(" deg/s | Z: ");
      Serial.print(gyroZ / 131.0);
      Serial.println(" deg/s");

      Serial.print("Encoder Position: ");
      Serial.println(encoderCount / 4.0);

      Serial.println("-------------------");
    }
  }
}