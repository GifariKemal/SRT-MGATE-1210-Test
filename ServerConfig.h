#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

class ServerConfig {
private:
  static const char* CONFIG_FILE;
  DynamicJsonDocument* config;

  bool saveConfig();
  bool loadConfig();
  bool validateConfig(const JsonDocument& config);
  void createDefaultConfig();
  static void restartDeviceTask(void* parameter);
  void scheduleDeviceRestart();

public:
  ServerConfig();
  ~ServerConfig();

  bool begin();

  // Configuration operations
  bool getConfig(JsonObject& result);
  bool updateConfig(JsonObjectConst newConfig);

  // Specific config getters
  bool getCommunicationConfig(JsonObject& result);
  String getProtocol();
  bool getDataIntervalConfig(JsonObject& result);
  bool getMqttConfig(JsonObject& result);
  bool getHttpConfig(JsonObject& result);
  bool getWifiConfig(JsonObject& result);
  bool getEthernetConfig(JsonObject& result);
  String getPrimaryNetworkMode();
  String getCommunicationMode();
};

#endif