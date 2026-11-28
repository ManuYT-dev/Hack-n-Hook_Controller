#include <BLEDevice.h>  // Load the ESP32 Bluetooth Low Energy device and server interfaces.
#include <BLE2902.h>  // Load the BLE descriptor that clients use to subscribe to notifications.
#include "RodSensors.h"  // Bring in the crank decoder, MPU6050 reader and sensor packet layout.

// Set false to run the original 1 Hz random JSON test with its original UUID.
constexpr bool sensorMode = true;  // Choose live rod data at compile time when true.
constexpr char serviceId[] = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc01";  // Identify the BLE service a laptop can discover in either mode.
constexpr char randomId[]  = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc02";  // Identify the characteristic carrying JSON in random test mode.
constexpr char sensorId[]  = "2bcae4c0-7d55-4ce6-a628-823f5f3bdc03";  // Identify the characteristic carrying binary packets in sensor mode.
BLEServer *server;  // Keep the server available to check for connected clients.
BLECharacteristic *data;  // Keep the active characteristic available for reads and notifications.

class RodConnections : public BLEServerCallbacks {  // Define the rod's response to BLE connection events.
  void onDisconnect(BLEServer *s) override { s->startAdvertising(); }  // Make the rod discoverable again when a client disconnects.
};  // End the BLE callback class.

void setup() {  // Run hardware and BLE initialization once after reset.
  Serial.begin(115200);  // Open the serial monitor connection at 115200 baud.
  if (sensorMode) rod::begin();  // Initialize the encoder and motion sensor only in sensor mode.
  const char *name = sensorMode ? "Hack-n-Hook" : "Heltec-Random-Data";  // Choose the device name the laptop will see when scanning.
  BLEDevice::init(name);  // Start the BLE stack with the selected name.
  server = BLEDevice::createServer();  // Create the server that will expose the data to the laptop.
  static RodConnections connections;  // Keep the callback object alive after setup returns.
  server->setCallbacks(&connections);  // Register that object to receive server events.
  BLEService *service = server->createService(serviceId);  // Create the rod's service using its shared identifier.
  data = service->createCharacteristic(sensorMode ? sensorId : randomId,  // Select the data characteristic identifier for the active mode.
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);  // Allow a client to read the latest value or subscribe to updates.
  data->addDescriptor(new BLE2902());  // Add the standard client configuration descriptor for notifications.
  if (sensorMode) {  // Prepare the first readable value for sensor mode.
    auto p = rod::sample();  // Take an initial motion and crank sample.
    data->setValue(reinterpret_cast<uint8_t*>(&p), sizeof(p));  // Copy the entire packet into the characteristic as raw bytes.
  } else {  // Prepare the initial value for random test mode instead.
    data->setValue("{\"value\":0}");  // Set a valid JSON reading before the first random number is generated.
  }  // End the choice of initial value.
  service->start();  // Make the service and its characteristic available to clients.
  auto advertising = BLEDevice::getAdvertising();  // Get the object that controls BLE discovery broadcasts.
  advertising->addServiceUUID(serviceId);  // Include the rod service identifier so a client can find it by service.
  advertising->setScanResponse(true);  // Enable a scan response for clients requesting extra advertising data.
  BLEDevice::startAdvertising();  // Begin broadcasting so nearby BLE clients can discover the rod.
  Serial.printf("Advertising as %s\n", name);  // Print the device name the user should look for on the laptop.
}  // End startup. Arduino now calls loop repeatedly.

void loop() {  // Update the characteristic at the interval selected for this mode.
  static uint32_t lastSample = 0;  // Keep the last update time in milliseconds between loop calls.
  if (millis() - lastSample < (sensorMode ? 20U : 1000U)) {  // Wait until 20 ms have elapsed for sensors or 1000 ms for random data.
    delay(1);  // Yield for one millisecond while waiting for the next sample.
    return;  // Finish this pass through loop without sending early.
  }  // End the sample timing check.
  lastSample = millis();  // Record this update's start time for the next interval.
  if (sensorMode) {  // Generate a live packet when the rod sensors are selected.
    auto p = rod::sample();  // Capture three acceleration axes, three gyro axes and the crank count.
    data->setValue(reinterpret_cast<uint8_t*>(&p), sizeof(p));  // Set the characteristic to the packet's 20 bytes for reading or notification.
    if (p.sequence % 50 == 0)  // Print one diagnostic line every 50 samples, roughly once a second.
      Serial.printf("IMU valid=%u accel=%d,%d,%d gyro=%d,%d,%d crank=%ld\n",  // Format the validity flag, raw motion axes and crank count for the serial monitor.
          p.imuValid, p.accel[0], p.accel[1], p.accel[2],  // Supply the motion validity flag followed by acceleration X, Y and Z.
          p.gyro[0], p.gyro[1], p.gyro[2], long(p.encoder));  // Supply gyro X, Y and Z followed by the signed crank count.
  } else {  // Generate a text reading when random test mode is selected.
    char json[20];  // Reserve space for the JSON string and its terminating zero.
    snprintf(json, sizeof(json), "{\"value\":%ld}", random(1001));  // Format a random integer from 0 through 1000 into the JSON buffer.
    data->setValue(json);  // Store the JSON text as the readable BLE value.
    Serial.println(json);  // Show the same test reading in the serial monitor.
  }  // End payload generation for this update.
  if (server->getConnectedCount()) data->notify();  // If a client is connected, notify subscribers of the updated value.
}  // End this update and let Arduino call loop again.
