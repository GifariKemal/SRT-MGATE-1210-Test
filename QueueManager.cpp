#include "QueueManager.h"
#include <esp_heap_caps.h>

QueueManager* QueueManager::instance = nullptr;

QueueManager::QueueManager()
  : dataQueue(nullptr), streamQueue(nullptr), queueMutex(nullptr), streamMutex(nullptr) {}

QueueManager* QueueManager::getInstance() {
  if (instance == nullptr) {
    instance = new QueueManager();
  }
  return instance;
}

bool QueueManager::init() {
  // Create FreeRTOS queue for data points
  dataQueue = xQueueCreate(MAX_QUEUE_SIZE, sizeof(char*));
  if (dataQueue == nullptr) {
    Serial.println("Failed to create data queue");
    return false;
  }

  // Create mutex for thread safety
  queueMutex = xSemaphoreCreateMutex();
  if (queueMutex == nullptr) {
    Serial.println("Failed to create queue mutex");
    return false;
  }

  // Create streaming queue
  streamQueue = xQueueCreate(MAX_STREAM_QUEUE_SIZE, sizeof(char*));
  if (streamQueue == nullptr) {
    Serial.println("Failed to create stream queue");
    return false;
  }

  // Create streaming mutex
  streamMutex = xSemaphoreCreateMutex();
  if (streamMutex == nullptr) {
    Serial.println("Failed to create stream mutex");
    return false;
  }

  Serial.println("QueueManager initialized successfully");
  return true;
}

bool QueueManager::enqueue(const JsonObject& dataPoint) {
  // Serialize JSON to string first, outside the mutex
  String jsonString;
  serializeJson(dataPoint, jsonString);

  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  // Check if queue is full
  if (uxQueueMessagesWaiting(dataQueue) >= MAX_QUEUE_SIZE) {
    // Remove oldest item to make space
    char* oldItem;
    if (xQueueReceive(dataQueue, &oldItem, 0) == pdTRUE) {
      free(oldItem);
    }
  }

  // Allocate memory for string in PSRAM
  char* jsonCopy = (char*)heap_caps_malloc(jsonString.length() + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (jsonCopy == nullptr) {
    // Fallback to internal RAM
    jsonCopy = (char*)malloc(jsonString.length() + 1);
    if (jsonCopy == nullptr) {
      xSemaphoreGive(queueMutex);
      return false;
    }
  }

  strcpy(jsonCopy, jsonString.c_str());

  // Add to queue
  bool success = xQueueSend(dataQueue, &jsonCopy, 0) == pdTRUE;

  if (success) {
    Serial.printf("Data queued: %s\n", dataPoint["name"].as<String>().c_str());
  } else {
    heap_caps_free(jsonCopy);
  }

  xSemaphoreGive(queueMutex);
  return success;
}

bool QueueManager::dequeue(JsonObject& dataPoint) {
  if (dataQueue == nullptr || queueMutex == nullptr) {
    return false;
  }

  char* jsonString = nullptr;
  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (xQueueReceive(dataQueue, &jsonString, 0) != pdTRUE) {
      jsonString = nullptr;
    }
    xSemaphoreGive(queueMutex);
  }

  if (jsonString == nullptr) {
    return false;
  }

  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, jsonString);
  bool success = false;

  if (error == DeserializationError::Ok) {
    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair kv : obj) {
      dataPoint[kv.key()] = kv.value();
    }
    success = true;
  }

  heap_caps_free(jsonString);
  return success;
}

bool QueueManager::peek(JsonObject& dataPoint) {
  if (dataQueue == nullptr || queueMutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return false;
  }

  char* jsonString;
  bool success = false;

  if (xQueuePeek(dataQueue, &jsonString, 0) == pdTRUE) {
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, jsonString);

    if (error == DeserializationError::Ok) {
      JsonObject obj = doc.as<JsonObject>();
      for (JsonPair kv : obj) {
        dataPoint[kv.key()] = kv.value();
      }
      success = true;
    }
  }

  xSemaphoreGive(queueMutex);
  return success;
}

bool QueueManager::isEmpty() {
  if (dataQueue == nullptr) {
    return true;
  }
  return uxQueueMessagesWaiting(dataQueue) == 0;
}

bool QueueManager::isFull() {
  if (dataQueue == nullptr) {
    return false;
  }
  return uxQueueMessagesWaiting(dataQueue) >= MAX_QUEUE_SIZE;
}

int QueueManager::size() {
  if (dataQueue == nullptr) {
    return 0;
  }
  return uxQueueMessagesWaiting(dataQueue);
}

void QueueManager::clear() {
  if (dataQueue == nullptr || queueMutex == nullptr) {
    return;
  }

  if (xSemaphoreTake(queueMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  char* jsonString;
  while (xQueueReceive(dataQueue, &jsonString, 0) == pdTRUE) {
    heap_caps_free(jsonString);
  }

  xSemaphoreGive(queueMutex);
  Serial.println("Queue cleared");
}

void QueueManager::getStats(JsonObject& stats) {
  stats["size"] = size();
  stats["max_size"] = MAX_QUEUE_SIZE;
  stats["is_empty"] = isEmpty();
  stats["is_full"] = isFull();
}

bool QueueManager::enqueueStream(const JsonObject& dataPoint) {
  if (streamQueue == nullptr || streamMutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(streamMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }

  // Remove oldest if full
  if (uxQueueMessagesWaiting(streamQueue) >= MAX_STREAM_QUEUE_SIZE) {
    char* oldItem;
    if (xQueueReceive(streamQueue, &oldItem, 0) == pdTRUE) {
      free(oldItem);
    }
  }

  String jsonString;
  serializeJson(dataPoint, jsonString);

  char* jsonCopy = (char*)heap_caps_malloc(jsonString.length() + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (jsonCopy == nullptr) {
    jsonCopy = (char*)malloc(jsonString.length() + 1);
    if (jsonCopy == nullptr) {
      xSemaphoreGive(streamMutex);
      return false;
    }
  }

  strcpy(jsonCopy, jsonString.c_str());
  bool success = xQueueSend(streamQueue, &jsonCopy, 0) == pdTRUE;

  if (success) {
    Serial.printf("Stream queue: Added data, size now: %d\n", uxQueueMessagesWaiting(streamQueue));
  } else {
    Serial.println("Stream queue: Failed to add data");
    heap_caps_free(jsonCopy);
  }

  xSemaphoreGive(streamMutex);
  return success;
}

bool QueueManager::dequeueStream(JsonObject& dataPoint) {
  if (streamQueue == nullptr || streamMutex == nullptr) {
    return false;
  }

  if (xSemaphoreTake(streamMutex, pdMS_TO_TICKS(10)) != pdTRUE) {
    return false;
  }

  char* jsonString;
  bool success = false;

  if (xQueueReceive(streamQueue, &jsonString, 0) == pdTRUE) {
    Serial.printf("Stream queue: Dequeued data, size now: %d\n", uxQueueMessagesWaiting(streamQueue));
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, jsonString) == DeserializationError::Ok) {
      JsonObject obj = doc.as<JsonObject>();
      for (JsonPair kv : obj) {
        dataPoint[kv.key()] = kv.value();
      }
      success = true;
    }
    heap_caps_free(jsonString);
  }

  xSemaphoreGive(streamMutex);
  return success;
}

bool QueueManager::isStreamEmpty() {
  return streamQueue == nullptr || uxQueueMessagesWaiting(streamQueue) == 0;
}

void QueueManager::clearStream() {
  if (streamQueue == nullptr || streamMutex == nullptr) {
    return;
  }

  if (xSemaphoreTake(streamMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  char* jsonString;
  while (xQueueReceive(streamQueue, &jsonString, 0) == pdTRUE) {
    heap_caps_free(jsonString);
  }

  xSemaphoreGive(streamMutex);
}

QueueManager::~QueueManager() {
  clear();
  clearStream();
  if (dataQueue) {
    vQueueDelete(dataQueue);
  }
  if (streamQueue) {
    vQueueDelete(streamQueue);
  }
  if (queueMutex) {
    vSemaphoreDelete(queueMutex);
  }
  if (streamMutex) {
    vSemaphoreDelete(streamMutex);
  }
}