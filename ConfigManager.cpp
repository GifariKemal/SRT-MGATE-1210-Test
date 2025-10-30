#include "ConfigManager.h"
#include <esp_heap_caps.h>
#include <new>
#include <vector>

// #define DEBUG_CONFIG_MANAGER // Uncomment to enable detailed debug logs

const char* ConfigManager::DEVICES_FILE = "/devices.json";
const char* ConfigManager::REGISTERS_FILE = "/registers.json";

ConfigManager::ConfigManager()
  : devicesCache(nullptr), registersCache(nullptr),
    devicesCacheValid(false), registersCacheValid(false) {
  // --- PERBAIKAN: Gunakan DynamicJsonDocument (sesuai .h) ---
  devicesCache = (DynamicJsonDocument*)heap_caps_malloc(sizeof(DynamicJsonDocument), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (devicesCache) {
    new (devicesCache) DynamicJsonDocument(8192);
  } else {
    devicesCache = new DynamicJsonDocument(4096);  // Fallback
  }

  registersCache = (DynamicJsonDocument*)heap_caps_malloc(sizeof(DynamicJsonDocument), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (registersCache) {
    new (registersCache) DynamicJsonDocument(16384);
  } else {
    registersCache = new DynamicJsonDocument(8192);  // Fallback
  }
  // --- AKHIR PERBAIKAN ---
}

ConfigManager::~ConfigManager() {
  // --- PERBAIKAN: Sesuaikan destructor (sesuai .h) ---
  if (devicesCache) {
    devicesCache->~DynamicJsonDocument();
    heap_caps_free(devicesCache);
  }
  if (registersCache) {
    registersCache->~DynamicJsonDocument();
    heap_caps_free(registersCache);
  }
  // --- AKHIR PERBAIKAN ---
}

bool ConfigManager::begin() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Mount Failed");
    return false;
  }

  if (!LittleFS.exists(DEVICES_FILE)) {
    // --- PERBAIKAN: Gunakan StaticJsonDocument untuk alokasi di STACK ---
    StaticJsonDocument<64> doc;
    // --- AKHIR PERBAIKAN ---
    doc.to<JsonObject>();
    saveJson(DEVICES_FILE, doc);
    Serial.println("Created empty devices file");
  }
  if (!LittleFS.exists(REGISTERS_FILE)) {
    // --- PERBAIKAN: Gunakan StaticJsonDocument untuk alokasi di STACK ---
    StaticJsonDocument<64> doc;
    // --- AKHIR PERBAIKAN ---
    doc.to<JsonObject>();
    saveJson(REGISTERS_FILE, doc);
    Serial.println("Created empty registers file");
  }

  // Initialize cache as invalid - will be loaded on first access
  devicesCacheValid = false;
  registersCacheValid = false;

  // Clear cache content
  devicesCache->clear();
  registersCache->clear();

  Serial.println("ConfigManager initialized - cache will be loaded on demand");
  return true;
}

String ConfigManager::generateId(const String& prefix) {
  return prefix + String(random(100000, 999999), HEX).substring(0, 6);
}

bool ConfigManager::saveJson(const String& filename, const JsonDocument& doc) {
  File file = LittleFS.open(filename, "w");
  if (!file) return false;

  serializeJson(doc, file);
  file.close();
  return true;
}

bool ConfigManager::loadJson(const String& filename, JsonDocument& doc) {
  File file = LittleFS.open(filename, "r");
  if (!file) return false;

  // Read file to PSRAM buffer for large files
  size_t fileSize = file.size();
  char* buffer = (char*)heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buffer) {
    // Fallback to direct parsing
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    return error == DeserializationError::Ok;
  }

  file.readBytes(buffer, fileSize);
  buffer[fileSize] = '\0';
  file.close();

  DeserializationError error = deserializeJson(doc, buffer);
  heap_caps_free(buffer);

  return error == DeserializationError::Ok;
}

String ConfigManager::createDevice(JsonObjectConst config) {
  if (!loadDevicesCache()) return "";

  String deviceId = generateId("D");
  // --- PERBAIKAN: Sintaks v7 untuk createNestedObject ---
  JsonObject device = (*devicesCache)[deviceId].to<JsonObject>();
  // --- AKHIR PERBAIKAN ---

  // Copy config with proper type conversion
  for (JsonPairConst kv : config) {
    String key = kv.key().c_str();
    if (key == "slave_id" || key == "port" || key == "timeout" || key == "retry_count" || key == "refresh_rate_ms" || key == "baud_rate" || key == "data_bits" || key == "stop_bits" || key == "serial_port") {
      // Convert string numbers to integers
      int value = kv.value().is<String>() ? kv.value().as<String>().toInt() : kv.value().as<int>();
      device[kv.key()] = value;
    } else {
      device[kv.key()] = kv.value();
    }
  }
  device["device_id"] = deviceId;
  // --- PERBAIKAN: Sintaks v7 untuk createNestedArray ---
  JsonArray registers = device["registers"].to<JsonArray>();
  // --- AKHIR PERBAIKAN ---
  Serial.printf("Created device %s with empty registers array\n", deviceId.c_str());

  // Save to file and keep cache valid
  if (saveJson(DEVICES_FILE, *devicesCache)) {
    Serial.printf("Device %s created and cache updated\n", deviceId.c_str());
    return deviceId;
  }

  invalidateDevicesCache();
  return "";
}

bool ConfigManager::readDevice(const String& deviceId, JsonObject& result) {
  if (!loadDevicesCache()) {
    Serial.println("Failed to load devices cache for readDevice");
    return false;
  }

#ifdef DEBUG_CONFIG_MANAGER
  Serial.printf("[DEBUG] Looking for device ID: '%s'\n", deviceId.c_str());
  Serial.printf("[DEBUG] Device ID length: %d\n", deviceId.length());
#endif

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (!(*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    JsonObject device = (*devicesCache)[deviceId];
    for (JsonPair kv : device) {
      result[kv.key()] = kv.value();
    }
#ifdef DEBUG_CONFIG_MANAGER
    Serial.printf("Device %s read from cache\n", deviceId.c_str());
#endif
    return true;
  }

#ifdef DEBUG_CONFIG_MANAGER
  // Debug: Show all available keys
  Serial.printf("Device %s not found in cache. Available devices:\n", deviceId.c_str());
  for (JsonPair kv : devicesCache->as<JsonObject>()) {
    Serial.printf("  - '%s' (length: %d)\n",
                  kv.key().c_str(), String(kv.key().c_str()).length());
  }
#endif
  return false;
}

bool ConfigManager::updateDevice(const String& deviceId, JsonObjectConst config) {
  if (!loadDevicesCache()) return false;

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if ((*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.printf("Device %s not found for update\n", deviceId.c_str());
    return false;
  }

  JsonObject device = (*devicesCache)[deviceId];

  // Update device configuration while preserving device_id and registers
  JsonArray existingRegisters = device["registers"];

  // Update all config fields with proper type conversion
  for (JsonPairConst kv : config) {
    String key = kv.key().c_str();
    if (key == "slave_id" || key == "port" || key == "timeout" || key == "retry_count" || key == "refresh_rate_ms" || key == "baud_rate" || key == "data_bits" || key == "stop_bits" || key == "serial_port") {
      // Convert string numbers to integers
      int value = kv.value().is<String>() ? kv.value().as<String>().toInt() : kv.value().as<int>();
      device[kv.key()] = value;
    } else {
      device[kv.key()] = kv.value();
    }
  }

  // Ensure device_id and registers are preserved
  device["device_id"] = deviceId;
  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (device["registers"].isNull()) {
  // --- AKHIR PERBAIKAN ---
    device["registers"] = existingRegisters;
  }

  // Save to file and keep cache valid
  if (saveJson(DEVICES_FILE, *devicesCache)) {
    Serial.printf("Device %s updated successfully\n", deviceId.c_str());
    return true;
  }

  invalidateDevicesCache();
  return false;
}

bool ConfigManager::deleteDevice(const String& deviceId) {
  if (!loadDevicesCache()) return false;

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (!(*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    devicesCache->as<JsonObject>().remove(deviceId);  // Perlu cast ke JsonObject untuk remove
    if (saveJson(DEVICES_FILE, *devicesCache)) {
      return true;
    }
    invalidateDevicesCache();
  }
  return false;
}

void ConfigManager::listDevices(JsonArray& devices) {
  if (!loadDevicesCache()) {
    Serial.println("Failed to load devices cache for listDevices");
    return;
  }

  JsonObject devicesObj = devicesCache->as<JsonObject>();
  int count = 0;

#ifdef DEBUG_CONFIG_MANAGER
  Serial.printf("[DEBUG] Cache size: %d devices\n", devicesObj.size());
#endif

  for (JsonPair kv : devicesObj) {
    const char* keyPtr = kv.key().c_str();
    String deviceId = String(keyPtr);

#ifdef DEBUG_CONFIG_MANAGER
    Serial.printf("[DEBUG] Raw key: '%s', String ID: '%s' (len: %d)\n",
                  keyPtr, deviceId.c_str(), deviceId.length());
#endif

    // Validate device ID before adding
    if (deviceId.length() > 0 && deviceId != "{}" && deviceId.indexOf('{') == -1) {
      devices.add(deviceId);
      count++;
#ifdef DEBUG_CONFIG_MANAGER
      Serial.printf("[DEBUG] Added valid device ID: '%s'\n", deviceId.c_str());
#endif
    } else {
#ifdef DEBUG_CONFIG_MANAGER
      Serial.printf("[DEBUG] Skipped invalid device ID: '%s'\n", deviceId.c_str());
#endif
    }
  }
#ifdef DEBUG_CONFIG_MANAGER
  Serial.printf("Listed %d devices from cache\n", count);
#endif
}

void ConfigManager::getDevicesSummary(JsonArray& summary) {
  // --- PERBAIKAN: Gunakan StaticJsonDocument untuk alokasi di STACK ---
  StaticJsonDocument<4096> devices;
  // --- AKHIR PERBAIKAN ---
  if (!loadJson(DEVICES_FILE, devices)) return;

  for (JsonPair kv : devices.as<JsonObject>()) {
    JsonObject device = kv.value();
    // --- PERBAIKAN: Sintaks v7 untuk createNestedObject di Array ---
    JsonObject deviceSummary = summary.add<JsonObject>();
    // --- AKHIR PERBAIKAN ---

    deviceSummary["device_id"] = kv.key();
    deviceSummary["device_name"] = device["device_name"];
    deviceSummary["protocol"] = device["protocol"];
    deviceSummary["register_count"] = device["registers"].size();
  }
}

String ConfigManager::createRegister(const String& deviceId, JsonObjectConst config) {
  Serial.printf("[CREATE_REGISTER] Starting for device: %s\n", deviceId.c_str());

  // Debug: Print all config fields
  Serial.println("[CREATE_REGISTER] Config fields:");
  for (JsonPairConst kv : config) {
    Serial.printf("  %s: %s (type: %s)\n",
                  kv.key().c_str(),
                  kv.value().as<String>().c_str(),
                  kv.value().is<String>() ? "string" : "other");
  }

  if (!loadDevicesCache()) {
    Serial.println("[CREATE_REGISTER] Failed to load devices cache");
    return "";
  }

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if ((*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.printf("[CREATE_REGISTER] Device %s not found in cache\n", deviceId.c_str());
    return "";
  }

  // Validate required fields
  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (config["address"].isNull() || config["register_name"].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.println("[CREATE_REGISTER] Missing required register fields: address or register_name");
    return "";
  }

  String registerId = generateId("R");
  JsonObject device = (*devicesCache)[deviceId];

  // Ensure registers array exists
  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (device["registers"].isNull()) {
    device["registers"].to<JsonArray>();  // Sintaks v7
  // --- AKHIR PERBAIKAN ---
    Serial.println("Created registers array for device");
  }

  JsonArray registers = device["registers"];

  // Parse address - handle both string and integer formats
  int address = 0;
  if (config["address"].is<String>()) {
    address = config["address"].as<String>().toInt();
  } else {
    address = config["address"].as<int>();
  }

  if (address < 0) {
    Serial.printf("Invalid address: %d\n", address);
    return "";
  }

  // Check for duplicate address
  for (JsonVariant regVar : registers) {
    JsonObject existingReg = regVar.as<JsonObject>();
    int existingAddress = existingReg["address"].is<String>() ? existingReg["address"].as<String>().toInt() : existingReg["address"].as<int>();

    if (existingAddress == address) {
      Serial.printf("Register address %d already exists in device %s\n", address, deviceId.c_str());
      return "";
    }
  }

  Serial.printf("[CREATE_REGISTER] Registers array size before: %d\n", registers.size());

  // --- PERBAIKAN: Sintaks v7 untuk createNestedObject di Array ---
  JsonObject newRegister = registers.add<JsonObject>();
  // --- AKHIR PERBAIKAN ---
  for (JsonPairConst kv : config) {
    String key = kv.key().c_str();
    if (key == "address") {
      // Always store address as integer
      newRegister[kv.key()] = address;
    } else if (key == "function_code" || key == "refresh_rate_ms") {
      // Convert string numbers to integers
      int value = kv.value().is<String>() ? kv.value().as<String>().toInt() : kv.value().as<int>();
      newRegister[kv.key()] = value;
    } else {
      newRegister[kv.key()] = kv.value();
    }
  }
  newRegister["register_id"] = registerId;

  Serial.printf("[CREATE_REGISTER] Registers array size after: %d\n", registers.size());
  Serial.printf("[CREATE_REGISTER] Created register %s (address: %d) for device %s\n", registerId.c_str(), address, deviceId.c_str());

  // Debug: Print the new register content
  Serial.println("[CREATE_REGISTER] New register content:");
  for (JsonPair kv : newRegister) {
    Serial.printf("  %s: %s\n", kv.key().c_str(), kv.value().as<String>().c_str());
  }

  // Save to file and keep cache valid
  if (saveJson(DEVICES_FILE, *devicesCache)) {
    Serial.println("[CREATE_REGISTER] Successfully saved devices file and updated cache");
    return registerId;
  } else {
    Serial.println("[CREATE_REGISTER] Failed to save devices file");
    invalidateDevicesCache();
  }
  return "";
}

bool ConfigManager::listRegisters(const String& deviceId, JsonArray& registers) {
  if (!loadDevicesCache()) {
    Serial.println("Failed to load devices cache for listRegisters");
    return false;
  }

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (!(*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    JsonObject device = (*devicesCache)[deviceId];
    // --- PERBAIKAN: Sintaks v7 untuk containsKey/is<T> ---
    if (device["registers"].is<JsonArray>()) {
    // --- AKHIR PERBAIKAN ---
      JsonArray deviceRegisters = device["registers"];
      Serial.printf("Device %s has %d registers in cache\n", deviceId.c_str(), deviceRegisters.size());
      for (JsonVariant reg : deviceRegisters) {
        registers.add(reg);
      }
      return true;
    } else {
      Serial.printf("Device %s has no registers array\n", deviceId.c_str());
    }
  } else {
    Serial.printf("Device %s not found in cache\n", deviceId.c_str());
  }
  return false;
}

bool ConfigManager::getRegistersSummary(const String& deviceId, JsonArray& summary) {
  if (!loadDevicesCache()) {
    Serial.println("Failed to load devices cache for getRegistersSummary");
    return false;
  }

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (!(*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    JsonObject device = (*devicesCache)[deviceId];
    // --- PERBAIKAN: Sintaks v7 untuk containsKey/is<T> ---
    if (device["registers"].is<JsonArray>()) {
    // --- AKHIR PERBAIKAN ---
      JsonArray registers = device["registers"];
      for (JsonVariant reg : registers) {
        // --- PERBAIKAN: Sintaks v7 untuk createNestedObject di Array ---
        JsonObject regSummary = summary.add<JsonObject>();
        // --- AKHIR PERBAIKAN ---
        regSummary["register_id"] = reg["register_id"];
        regSummary["register_name"] = reg["register_name"];
        regSummary["address"] = reg["address"];
        regSummary["data_type"] = reg["data_type"];
        regSummary["description"] = reg["description"];
      }
      return true;
    }
  }
  return false;
}

bool ConfigManager::updateRegister(const String& deviceId, const String& registerId, JsonObjectConst config) {
  if (!loadDevicesCache()) return false;

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if ((*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.printf("Device %s not found for register update\n", deviceId.c_str());
    return false;
  }

  JsonObject device = (*devicesCache)[deviceId];
  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (device["registers"].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.printf("No registers found for device %s\n", deviceId.c_str());
    return false;
  }

  JsonArray registers = device["registers"];
  for (JsonVariant regVar : registers) {
    JsonObject reg = regVar.as<JsonObject>();
    if (reg["register_id"] == registerId) {
      // Check for duplicate address if address is being updated
      // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
      if (!config["address"].isNull()) {
      // --- AKHIR PERBAIKAN ---
        int newAddress = config["address"].is<String>() ? config["address"].as<String>().toInt() : config["address"].as<int>();

        int currentAddress = reg["address"].is<String>() ? reg["address"].as<String>().toInt() : reg["address"].as<int>();

        if (newAddress != currentAddress) {
          for (JsonVariant otherRegVar : registers) {
            JsonObject otherReg = otherRegVar.as<JsonObject>();
            if (otherReg["register_id"] != registerId) {
              int otherAddress = otherReg["address"].is<String>() ? otherReg["address"].as<String>().toInt() : otherReg["address"].as<int>();

              if (otherAddress == newAddress) {
                Serial.printf("Address %d already exists in another register\n", newAddress);
                return false;
              }
            }
          }
        }
      }

      // Update register configuration while preserving register_id
      for (JsonPairConst kv : config) {
        String key = kv.key().c_str();
        if (key == "address" || key == "function_code" || key == "refresh_rate_ms") {
          int value = kv.value().is<String>() ? kv.value().as<String>().toInt() : kv.value().as<int>();
          reg[kv.key()] = value;
        } else {
          reg[kv.key()] = kv.value();
        }
      }
      reg["register_id"] = registerId;  // Ensure register_id is preserved

      // Save to file and keep cache valid
      if (saveJson(DEVICES_FILE, *devicesCache)) {
        Serial.printf("Register %s updated successfully\n", registerId.c_str());
        return true;
      }

      invalidateDevicesCache();
      return false;
    }
  }

  Serial.printf("Register %s not found in device %s\n", registerId.c_str(), deviceId.c_str());
  return false;
}

bool ConfigManager::deleteRegister(const String& deviceId, const String& registerId) {
  if (!loadDevicesCache()) {
    Serial.println("Failed to load devices cache for deleteRegister");
    return false;
  }

  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if ((*devicesCache)[deviceId].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.printf("Device %s not found for register deletion\n", deviceId.c_str());
    return false;
  }

  JsonObject device = (*devicesCache)[deviceId];
  // --- PERBAIKAN: Sintaks v7 untuk containsKey ---
  if (device["registers"].isNull()) {
  // --- AKHIR PERBAIKAN ---
    Serial.printf("No registers found for device %s\n", deviceId.c_str());
    return false;
  }

  JsonArray registers = device["registers"];
  for (int i = 0; i < registers.size(); i++) {
    if (registers[i]["register_id"] == registerId) {
      registers.remove(i);

      // Save to file and keep cache valid
      if (saveJson(DEVICES_FILE, *devicesCache)) {
        Serial.printf("Register %s deleted successfully\n", registerId.c_str());
        return true;
      }

      invalidateDevicesCache();
      return false;
    }
  }

  Serial.printf("Register %s not found in device %s\n", registerId.c_str(), deviceId.c_str());
  return false;
}

bool ConfigManager::loadDevicesCache() {
  // If cache is already loaded and valid, do nothing.
  if (devicesCacheValid) {
    return true;
  }

  Serial.println("[CACHE] Loading devices cache from file...");

  // If the file doesn't exist, create an empty JSON object in the cache and exit.
  if (!LittleFS.exists(DEVICES_FILE)) {
    Serial.println("Devices file not found. Initializing empty cache.");
    devicesCache->clear();
    devicesCache->to<JsonObject>();
    devicesCacheValid = true;
    return true;
  }

  // Clear the cache before loading new data from the file.
  devicesCache->clear();

  // Attempt to load and parse the JSON from the file.
  if (loadJson(DEVICES_FILE, *devicesCache)) {
    devicesCacheValid = true;
    Serial.printf("Devices cache loaded successfully. Found %d devices.\n", devicesCache->as<JsonObject>().size());
    return true;
  }

  // If parsing fails, log the error and create an empty cache to ensure stable operation.
  Serial.println("ERROR: Failed to parse devices.json. Initializing empty cache to prevent data corruption.");
  devicesCache->clear();
  devicesCache->to<JsonObject>();
  devicesCacheValid = true;  // Mark as valid to prevent repeated failed parsing attempts.
  return false;              // Indicate that loading failed.
}

bool ConfigManager::loadRegistersCache() {
  if (registersCacheValid) return true;

  // Check if file exists
  if (!LittleFS.exists(REGISTERS_FILE)) {
    Serial.println("Registers file does not exist, creating empty cache");
    registersCache->clear();
    registersCache->to<JsonObject>();
    registersCacheValid = true;
    return true;
  }

  registersCache->clear();

  if (loadJson(REGISTERS_FILE, *registersCache)) {
    registersCacheValid = true;
    return true;
  }
  return false;
}

void ConfigManager::invalidateDevicesCache() {
  devicesCacheValid = false;
}

void ConfigManager::invalidateRegistersCache() {
  registersCacheValid = false;
}

void ConfigManager::refreshCache() {
  Serial.println("[CACHE] Forcing cache refresh...");

  // Force invalidate
  devicesCacheValid = false;
  registersCacheValid = false;

  // Clear cache content
  devicesCache->clear();
  registersCache->clear();

  // Reload from files
  bool devicesLoaded = loadDevicesCache();
  bool registersLoaded = loadRegistersCache();

  Serial.printf("[CACHE] Refresh complete - Devices: %s, Registers: %s\n",
                devicesLoaded ? "OK" : "FAIL",
                registersLoaded ? "OK" : "FAIL");
}

void ConfigManager::debugDevicesFile() {
  Serial.println("=== DEBUG DEVICES FILE ===");

  if (!LittleFS.exists(DEVICES_FILE)) {
    Serial.println("Devices file does not exist");
    return;
  }

  File file = LittleFS.open(DEVICES_FILE, "r");
  if (!file) {
    Serial.println("Failed to open devices file");
    return;
  }

  Serial.printf("File size: %d bytes\n", file.size());
  Serial.println("File content:");

  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println();

  file.close();
  Serial.println("=== END DEBUG ===");
}

void ConfigManager::fixCorruptDeviceIds() {
  Serial.println("=== FIXING CORRUPT DEVICE IDS ===");

  // --- PERBAIKAN: Gunakan StaticJsonDocument untuk alokasi di STACK ---
  StaticJsonDocument<8192> originalDoc;
  // --- AKHIR PERBAIKAN ---
  if (!loadJson(DEVICES_FILE, originalDoc)) {
    Serial.println("Failed to load devices file for fixing");
    return;
  }

  // --- PERBAIKAN: Gunakan StaticJsonDocument untuk alokasi di STACK ---
  StaticJsonDocument<8192> fixedDoc;
  // --- AKHIR PERBAIKAN ---
  JsonObject fixedDevices = fixedDoc.to<JsonObject>();

  bool foundCorruption = false;

  for (JsonPair kv : originalDoc.as<JsonObject>()) {
    const char* keyPtr = kv.key().c_str();
    String deviceId = String(keyPtr);

    // Check if device ID is corrupt
    if (deviceId.isEmpty() || deviceId == "{}" || deviceId.indexOf('{') != -1 || deviceId.length() < 3) {
      Serial.printf("Found corrupt device ID: '%s' - generating new ID\n", deviceId.c_str());

      // Generate new device ID
      String newDeviceId = generateId("D");
      JsonObject deviceObj = kv.value().as<JsonObject>();
      deviceObj["device_id"] = newDeviceId;

      fixedDevices[newDeviceId] = deviceObj;
      foundCorruption = true;

      Serial.printf("Replaced with new ID: %s\n", newDeviceId.c_str());
    } else {
      // Keep valid device ID
      fixedDevices[deviceId] = kv.value();
      Serial.printf("Kept valid device ID: %s\n", deviceId.c_str());
    }
  }

  if (foundCorruption) {
    Serial.println("Saving fixed devices file...");
    if (saveJson(DEVICES_FILE, fixedDoc)) {
      Serial.println("Fixed devices file saved successfully");
      // Force invalidate cache to reload fixed data
      invalidateDevicesCache();
      devicesCacheValid = false;
    } else {
      Serial.println("Failed to save fixed devices file");
    }
  } else {
    Serial.println("No corruption found in device IDs");
  }

  // Always invalidate cache after this operation
  invalidateDevicesCache();
  devicesCacheValid = false;

  Serial.println("=== END FIXING ===");
}

void ConfigManager::removeCorruptKeys() {
  Serial.println("=== REMOVING CORRUPT KEYS ===");

  // Force load current cache
  devicesCacheValid = false;
  if (!loadDevicesCache()) {
    Serial.println("Failed to load cache for corrupt key removal");
    return;
  }

  JsonObject devicesObj = devicesCache->as<JsonObject>();

  // Find and remove corrupt keys
  std::vector<String> keysToRemove;

  for (JsonPair kv : devicesObj) {
    const char* keyPtr = kv.key().c_str();
    String deviceId = String(keyPtr);

    if (deviceId.isEmpty() || deviceId == "{}" || deviceId.indexOf('{') != -1 || deviceId.length() < 3) {
      keysToRemove.push_back(deviceId);
      Serial.printf("Marking corrupt key for removal: '%s'\n", deviceId.c_str());
    }
  }

  // Remove corrupt keys
  for (const String& key : keysToRemove) {
    devicesCache->as<JsonObject>().remove(key);  // Perbaikan: Pastikan cast ke JsonObject
    Serial.printf("Removed corrupt key: '%s'\n", key.c_str());
  }

  if (keysToRemove.size() > 0) {
    // Save cleaned cache
    if (saveJson(DEVICES_FILE, *devicesCache)) {
      Serial.printf("Removed %d corrupt keys and saved file\n", keysToRemove.size());
    } else {
      Serial.println("Failed to save cleaned devices file");
    }
  } else {
    Serial.println("No corrupt keys found to remove");
  }

  Serial.println("=== END REMOVING ===");
}



void ConfigManager::clearAllConfigurations() {
  Serial.println("Clearing all device and register configurations...");
  // --- PERBAIKAN: Gunakan StaticJsonDocument untuk alokasi di STACK ---
  StaticJsonDocument<64> emptyDoc;
  // --- AKHIR PERBAIKAN ---
  emptyDoc.to<JsonObject>();
  saveJson(DEVICES_FILE, emptyDoc);
  saveJson(REGISTERS_FILE, emptyDoc);
  invalidateDevicesCache();
  invalidateRegistersCache();
  Serial.println("All configurations cleared");
}