#ifndef CRUD_HANDLER_H
#define CRUD_HANDLER_H

#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "ServerConfig.h"
#include "LoggingConfig.h"
#include <map>
#include <functional>
// Tambahan untuk thread safety
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class BLEManager;  // Forward declaration

class CRUDHandler {
private:
  ConfigManager* configManager;
  ServerConfig* serverConfig;
  LoggingConfig* loggingConfig;
  String streamDeviceId;
  SemaphoreHandle_t streamIdMutex; // <-- TAMBAHAN untuk thread-safety

  // Define a type for our command handler functions
  using CommandHandler = std::function<void(BLEManager*, const JsonDocument&)>;

  // Maps to hold handlers for each operation type
  std::map<String, CommandHandler> readHandlers;
  std::map<String, CommandHandler> createHandlers;
  std::map<String, CommandHandler> updateHandlers;
  std::map<String, CommandHandler> deleteHandlers;

  // Private method to populate the handler maps
  void setupCommandHandlers();

public:
  CRUDHandler(ConfigManager* config, ServerConfig* serverCfg, LoggingConfig* loggingCfg);
  ~CRUDHandler(); // <-- TAMBAHAN

  void handle(BLEManager* manager, const JsonDocument& command);
  
  // Fungsi-fungsi ini sekarang diimplementasikan di .cpp
  String getStreamDeviceId();
  void clearStreamDeviceId();
  bool isStreaming();
};

#endif