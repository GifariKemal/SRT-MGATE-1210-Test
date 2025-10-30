#include "BLEManager.h"
#include "CRUDHandler.h"
#include "QueueManager.h"
#include "MemoryManager.h"  // Include the new memory manager
#include <esp_heap_caps.h>
#include <new>

BLEManager::BLEManager(const String& name, CRUDHandler* cmdHandler)
  : serviceName(name), handler(cmdHandler), processing(false), streamTaskHandle(nullptr), commandBufferIndex(0) {
  memset(commandBuffer, 0, COMMAND_BUFFER_SIZE);
  commandQueue = xQueueCreate(20, sizeof(char*));  // Queue now holds char pointers
}

BLEManager::~BLEManager() {
  stop();
  if (commandQueue) {
    // Purge the queue of any remaining pointers
    char* cmd;
    while (xQueueReceive(commandQueue, &cmd, 0)) {
      heap_caps_free(cmd);
    }
    vQueueDelete(commandQueue);
  }
}

bool BLEManager::begin() {
  // Initialize BLE
  BLEDevice::init(serviceName.c_str());

  // Create BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create Command Characteristic (Write)
  pCommandChar = pService->createCharacteristic(
    COMMAND_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE);
  pCommandChar->setCallbacks(this);

  // Create Response Characteristic (Notify)
  pResponseChar = pService->createCharacteristic(
    RESPONSE_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY);
  // Deskriptor 2902 ditambahkan secara otomatis oleh library

  // Start service
  pService->start();

  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  // Create command processing task with PSRAM stack
  xTaskCreatePinnedToCore(
    commandProcessingTask,
    "BLE_CMD_TASK",
    8192,  // Increased stack size for PSRAM usage
    this,
    1,
    &commandTaskHandle,
    1  // Pin to core 1
  );

  // Create streaming task
  xTaskCreatePinnedToCore(
    streamingTask,
    "BLE_STREAM_TASK",
    4096,
    this,
    1,
    &streamTaskHandle,
    1);

  Serial.println("BLE Manager initialized: " + serviceName);
  return true;
}

void BLEManager::stop() {
  if (commandTaskHandle) {
    vTaskDelete(commandTaskHandle);
    commandTaskHandle = nullptr;
  }

  if (streamTaskHandle) {
    vTaskDelete(streamTaskHandle);
    streamTaskHandle = nullptr;
  }

  if (pServer) {
    BLEDevice::deinit();
    Serial.println("BLE Manager stopped");
  }
}

void BLEManager::onConnect(BLEServer* pServer) {
  Serial.println("BLE Client connected");
}

void BLEManager::onDisconnect(BLEServer* pServer) {
  Serial.println("BLE Client disconnected");

  // Stop streaming when client disconnects
  extern CRUDHandler* crudHandler;
  if (crudHandler) {
    crudHandler->clearStreamDeviceId(); // Harusnya ini memanggil fungsi thread-safe
    QueueManager* queueMgr = QueueManager::getInstance();
    if (queueMgr) {
      queueMgr->clearStream();
    }
    Serial.println("Cleared streaming on disconnect");
  }

  BLEDevice::startAdvertising();  // Restart advertising
}

void BLEManager::onWrite(BLECharacteristic* pCharacteristic) {
  if (pCharacteristic == pCommandChar) {
    String value = pCharacteristic->getValue().c_str();
    receiveFragment(value);
  }
}

void BLEManager::receiveFragment(const String& fragment) {
  if (processing) return;

  if (fragment == "<END>") {
    processing = true;
    commandBuffer[commandBufferIndex] = '\0';  // Null-terminate the command

    // Allocate a buffer in PSRAM for the complete command
    char* cmdBuffer = (char*)heap_caps_malloc(commandBufferIndex + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (cmdBuffer) {
      strcpy(cmdBuffer, commandBuffer);
      if (xQueueSend(commandQueue, &cmdBuffer, 0) != pdPASS) {
        // If queue is full, free the memory we just allocated
        heap_caps_free(cmdBuffer);
        Serial.println("BLE command queue full, command dropped.");
      }
    } else {
      Serial.println("ERROR: Failed to allocate memory for BLE command!");
    }

    // Reset buffer
    commandBufferIndex = 0;
    memset(commandBuffer, 0, COMMAND_BUFFER_SIZE);
    processing = false;
  } else {
    size_t fragmentLen = fragment.length();
    if (commandBufferIndex + fragmentLen < COMMAND_BUFFER_SIZE) {
      memcpy(commandBuffer + commandBufferIndex, fragment.c_str(), fragmentLen);
      commandBufferIndex += fragmentLen;
    } else {
      Serial.println("ERROR: BLE command buffer overflow!");
      commandBufferIndex = 0;
      memset(commandBuffer, 0, COMMAND_BUFFER_SIZE);
      sendError("Command too long, buffer overflow.");
    }
  }
}

void BLEManager::commandProcessingTask(void* parameter) {
  BLEManager* manager = static_cast<BLEManager*>(parameter);
  char* command;

  while (true) {
    if (xQueueReceive(manager->commandQueue, &command, portMAX_DELAY)) {
      manager->handleCompleteCommand(command);
      heap_caps_free(command);  // Free the memory allocated in receiveFragment
    }
  }
}

void BLEManager::handleCompleteCommand(const char* command) {
  Serial.printf("DEBUG: Raw JSON command: %s\n", command);

  // --- PERUBAHAN DI SINI ---
  // Kita KEMBALI menggunakan DynamicJsonDocument HANYA untuk alokasi PSRAM
  // karena MemoryManager.h Anda (make_psram_unique) dibuat untuk v6.
  // Ini akan menghasilkan WARNING, tapi MENGHILANGKAN ERROR kompilasi.
  auto doc = make_psram_unique<DynamicJsonDocument>(4096);
  // --- AKHIR PERUBAHAN ---

  if (!doc) {
    sendError("PSRAM allocation failed for JSON document.");
    return;
  }

  DeserializationError error = deserializeJson(*doc, command);

  if (error) {
    sendError("Invalid JSON: " + String(error.c_str()));
    return;
  }

  if (handler) {
    handler->handle(this, *doc);
  } else {
    sendError("No handler configured");
  }
}

void BLEManager::sendResponse(const JsonDocument& data) {
  String response;
  serializeJson(data, response);
  sendFragmented(response);
}

void BLEManager::sendError(const String& message) {
  // --- PERUBAHAN DI SINI ---
  // Gunakan StaticJsonDocument untuk alokasi di stack
  StaticJsonDocument<256> doc;
  // --- AKHIR PERUBAHAN ---
  doc["status"] = "error";
  doc["message"] = message;
  sendResponse(doc);
}

void BLEManager::sendSuccess() {
  // --- PERUBAHAN DI SINI ---
  // Gunakan StaticJsonDocument untuk alokasi di stack
  StaticJsonDocument<128> doc;
  // --- AKHIR PERUBAHAN ---
  doc["status"] = "ok";
  sendResponse(doc);
}

void BLEManager::sendFragmented(const String& data) {
  if (!pResponseChar) return;

  int i = 0;
  while (i < data.length()) {
    int end = i + CHUNK_SIZE;
    if (end > data.length()) {
      end = data.length();
    }

    String chunk = data.substring(i, end);
    pResponseChar->setValue(chunk.c_str());
    pResponseChar->notify();

    vTaskDelay(pdMS_TO_TICKS(FRAGMENT_DELAY_MS));
    i = end;
  }

  // Send end marker
  pResponseChar->setValue("<END>");
  pResponseChar->notify();
}

void BLEManager::streamingTask(void* parameter) {
  BLEManager* manager = static_cast<BLEManager*>(parameter);
  QueueManager* queueMgr = QueueManager::getInstance();

  Serial.println("BLE Streaming task started");
  int loopCount = 0;

  while (true) {
    if (queueMgr && !queueMgr->isStreamEmpty()) {
      // --- PERUBAHAN DI SINI ---
      // Gunakan StaticJsonDocument untuk alokasi di stack
      StaticJsonDocument<512> dataDoc;
      // --- AKHIR PERUBAHAN ---
      JsonObject dataPoint = dataDoc.to<JsonObject>();

      if (queueMgr->dequeueStream(dataPoint)) {
        Serial.println("Streaming data via BLE");
        // --- PERUBAHAN DI SINI ---
        // Gunakan StaticJsonDocument untuk alokasi di stack
        StaticJsonDocument<512> response;
        // --- AKHIR PERUBAHAN ---
        response["status"] = "data";
        response["data"] = dataPoint;
        manager->sendResponse(response);
      }
    } else {
      loopCount++;
      if (loopCount % 100 == 0) {
        Serial.printf("Stream queue empty, loop count: %d\n", loopCount);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}