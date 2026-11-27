#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Custom service and characteristic UUIDs. Use these on the receiving device.
constexpr char DEVICE_NAME[] = "Heltec-Random-Data";
constexpr char SERVICE_UUID[] = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc01";
constexpr char DATA_UUID[] = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc02";

BLECharacteristic *dataCharacteristic = nullptr;
BLEServer *bleServer = nullptr;
bool wasConnected = false;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(300);
  randomSeed(esp_random());

  BLEDevice::init(DEVICE_NAME);
  bleServer = BLEDevice::createServer();
  BLEService *service = bleServer->createService(SERVICE_UUID);
  dataCharacteristic = service->createCharacteristic(
      DATA_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  dataCharacteristic->addDescriptor(new BLE2902());
  dataCharacteristic->setValue("{\"value\":0}");
  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("Advertising as Heltec-Random-Data");
}

void loop() {
  const bool connected = bleServer->getConnectedCount() > 0;
  if (wasConnected && !connected) {
    BLEDevice::startAdvertising();
    Serial.println("Disconnected; advertising again");
  }
  wasConnected = connected;

  if (millis() - lastSend >= 1000) {
    lastSend = millis();
    const int value = random(0, 1001);
    char payload[24];
    snprintf(payload, sizeof(payload), "{\"value\":%d}", value);
    dataCharacteristic->setValue(payload);
    if (connected) {
      dataCharacteristic->notify();
    }
    Serial.println(payload);
  }
  delay(10);
}
