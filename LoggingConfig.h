#ifndef LOGGING_CONFIG_H
#define LOGGING_CONFIG_H

#include <ArduinoJson.h>
#include <LittleFS.h>

class LoggingConfig {
private:
  static const char* CONFIG_FILE;
  DynamicJsonDocument config;

  bool saveConfig();
  bool loadConfig();
  bool validateConfig(const JsonDocument& config);
  void createDefaultConfig();

public:
  LoggingConfig();

  bool begin();

  // Configuration operations
  bool getConfig(JsonObject& result);
  bool updateConfig(JsonObjectConst newConfig);

  // Specific getters
  String getLoggingRetention();
  String getLoggingInterval();
};

#endif