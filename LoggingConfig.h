#ifndef LOGGING_CONFIG_H
#define LOGGING_CONFIG_H

#include <ArduinoJson.h>
#include <LittleFS.h>

class LoggingConfig {
private:
  static const char* CONFIG_FILE;
  
  // --- PERUBAHAN DI SINI ---
  // Ganti 'DynamicJsonDocument config;' menjadi 'StaticJsonDocument<256> config;'
  // Ukuran 256 diambil dari constructor Anda di LoggingConfig.cpp
  StaticJsonDocument<256> config;
  // --- AKHIR PERUBAHAN ---

  bool saveConfig();
  bool loadConfig();
  bool validateConfig(const JsonDocument& config);
  void createDefaultConfig();

public:
  // Constructor tidak perlu diubah, : config(256) akan bekerja
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