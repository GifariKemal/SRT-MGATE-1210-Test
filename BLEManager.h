#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// BLE UUIDs
#define SERVICE_UUID "00001830-0000-1000-8000-00805f9b34fb"
#define COMMAND_CHAR_UUID "11111111-1111-1111-1111-111111111101"
#define RESPONSE_CHAR_UUID "11111111-1111-1111-1111-111111111102"

// Constants
#define CHUNK_SIZE 18
#define FRAGMENT_DELAY_MS 50
#define COMMAND_BUFFER_SIZE 4096  // Increased for PSRAM usage

class CRUDHandler;  // Forward declaration

class BLEManager : public BLEServerCallbacks, public BLECharacteristicCallbacks {
private:
  BLEServer* pServer;
  BLEService* pService;
  BLECharacteristic* pCommandChar;
  BLECharacteristic* pResponseChar;

  String serviceName;
  CRUDHandler* handler;

  // Command processing
  char commandBuffer[COMMAND_BUFFER_SIZE];
  size_t commandBufferIndex;
  bool processing;
  QueueHandle_t commandQueue;
  TaskHandle_t commandTaskHandle;
  TaskHandle_t streamTaskHandle;

  // FreeRTOS task functions
  static void commandProcessingTask(void* parameter);
  static void streamingTask(void* parameter);

  // Fragment handling
  void receiveFragment(const String& fragment);
  void handleCompleteCommand(const char* command);
  void sendFragmented(const String& data);

public:
  BLEManager(const String& name, CRUDHandler* cmdHandler);
  ~BLEManager();

  bool begin();
  void stop();

  // Response methods
  void sendResponse(const JsonDocument& data);
  void sendError(const String& message);
  void sendSuccess();

  // BLE callbacks
  void onConnect(BLEServer* pServer) override;
  void onDisconnect(BLEServer* pServer) override;
  void onWrite(BLECharacteristic* pCharacteristic) override;
};

#endif