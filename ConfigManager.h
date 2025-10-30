#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

class ConfigManager {
private:
  static const char* DEVICES_FILE;
  static const char* REGISTERS_FILE;

  // --- PERBAIKAN: Kembalikan ke DynamicJsonDocument ---
  // Ini diperlukan agar 'new (devicesCache) DynamicJsonDocument(8192)' di .cpp valid
  DynamicJsonDocument* devicesCache;
  DynamicJsonDocument* registersCache;
  // --- AKHIR PERBAIKAN ---

  bool devicesCacheValid;
  bool registersCacheValid;

  String generateId(const String& prefix);
  bool saveJson(const String& filename, const JsonDocument& doc);
  bool loadJson(const String& filename, JsonDocument& doc);
  void invalidateDevicesCache();
  void invalidateRegistersCache();
  bool loadDevicesCache();
  bool loadRegistersCache();

public:
  ConfigManager();
  ~ConfigManager();

  bool begin();

  // Device operations
  String createDevice(JsonObjectConst config);
  bool readDevice(const String& deviceId, JsonObject& result);
  bool updateDevice(const String& deviceId, JsonObjectConst config);
  bool deleteDevice(const String& deviceId);
  void listDevices(JsonArray& devices);
  void getDevicesSummary(JsonArray& summary);

  // Clear all configurations
  void clearAllConfigurations();

  // Cache management
  void refreshCache();
  void debugDevicesFile();
  void fixCorruptDeviceIds();
  void removeCorruptKeys();

  // Register operations
  String createRegister(const String& deviceId, JsonObjectConst config);
  bool listRegisters(const String& deviceId, JsonArray& registers);
  bool getRegistersSummary(const String& deviceId, JsonArray& summary);
  bool updateRegister(const String& deviceId, const String& registerId, JsonObjectConst config);
  bool deleteRegister(const String& deviceId, const String& registerId);
};

#endif