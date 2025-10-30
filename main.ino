/*
 * BLE CRUD Manager for ESP32
 * Production-ready BLE service for Modbus device configuration
 * 
 * Hardware: ESP32 with BLE support
 * Features: CRUD operations, fragmentation, FreeRTOS tasks
 */

#include "BLEManager.h"
#include "CRUDHandler.h"
#include "ConfigManager.h"
#include "ServerConfig.h"
#include "LoggingConfig.h"
#include "NetworkManager.h"
#include "RTCManager.h"
#include "ModbusTcpService.h"
#include "ModbusRtuService.h"
#include "QueueManager.h"
#include "MqttManager.h"
#include "HttpManager.h"
#include "LEDManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <esp_system.h>  // Required for esp_random()

// Add missing include
#include <new>

// Global objects - initialized to nullptr for safety
BLEManager* bleManager = nullptr;
CRUDHandler* crudHandler = nullptr;
ConfigManager* configManager = nullptr;
ServerConfig* serverConfig = nullptr;
LoggingConfig* loggingConfig = nullptr;
NetworkMgr* networkManager = nullptr;
RTCManager* rtcManager = nullptr;
ModbusTcpService* modbusTcpService = nullptr;
ModbusRtuService* modbusRtuService = nullptr;
QueueManager* queueManager = nullptr;
MqttManager* mqttManager = nullptr;
HttpManager* httpManager = nullptr;
LEDManager* ledManager = nullptr;

// Cleanup function for failed initialization
void cleanup() {
  if (configManager) {
    configManager->~ConfigManager();
    heap_caps_free(configManager);
  }
  if (serverConfig) delete serverConfig;
  if (loggingConfig) delete loggingConfig;
  if (modbusTcpService) delete modbusTcpService;
  if (modbusRtuService) delete modbusRtuService;
  if (crudHandler) {
    crudHandler->~CRUDHandler();
    heap_caps_free(crudHandler);
  }
  if (bleManager) {
    bleManager->~BLEManager();
    heap_caps_free(bleManager);
  }
}

void setup() {
  Serial.begin(115200);
  vTaskDelay(pdMS_TO_TICKS(1000));
  uint32_t seed = esp_random();
  randomSeed(seed);  // Seed the random number generator for unique IDs
  Serial.printf("Random seed initialized with: %u\n", seed);

  // Check PSRAM availability
  if (esp_psram_is_initialized()) {
    Serial.printf("PSRAM available: %d bytes\n", ESP.getPsramSize());
    Serial.printf("PSRAM free: %d bytes\n", ESP.getFreePsram());
  } else {
    Serial.println("PSRAM not available - using internal RAM");
  }

  Serial.println("Starting BLE CRUD Manager...");

  // Initialize configuration manager in PSRAM
  configManager = (ConfigManager*)heap_caps_malloc(sizeof(ConfigManager), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (configManager) {
    new (configManager) ConfigManager();
  } else {
    configManager = new ConfigManager();  // Fallback to internal RAM
    if (!configManager) {
      Serial.println("Failed to allocate ConfigManager");
      return;
    }
  }

  if (!configManager->begin()) {
    Serial.println("Failed to initialize ConfigManager");
    cleanup();
    return;
  }

  // Debug: Show devices file content
  configManager->debugDevicesFile();

  // Pre-load the cache once to prevent race conditions from other tasks
  Serial.println("[MAIN] Forcing initial cache load...");
  configManager->refreshCache();

  // Clear all existing configurations for fresh start
  // configManager->clearAllConfigurations();  // Commented out to preserve existing devices

  // Fix corrupt device IDs by recreating clean file
  configManager->fixCorruptDeviceIds();


  Serial.println("[MAIN] Configuration initialization completed.");

  // Initialize LED Manager
  ledManager = LEDManager::getInstance();
  if (ledManager) {
    ledManager->begin();
  } else {
    Serial.println("Failed to get LEDManager instance");
    cleanup();
    return;
  }

  // Initialize queue manager
  queueManager = QueueManager::getInstance();
  if (!queueManager || !queueManager->init()) {
    Serial.println("Failed to initialize QueueManager");
    cleanup();
    return;
  }

  // Initialize server config
  serverConfig = new ServerConfig();
  if (!serverConfig || !serverConfig->begin()) {
    Serial.println("Failed to initialize ServerConfig");
    cleanup();
    return;
  }

  // Initialize logging config
  loggingConfig = new LoggingConfig();
  if (!loggingConfig || !loggingConfig->begin()) {
    Serial.println("Failed to initialize LoggingConfig");
    cleanup();
    return;
  }

  // Initialize network manager
  networkManager = NetworkMgr::getInstance();
  if (!networkManager) {
    Serial.println("Failed to get NetworkManager instance");
    cleanup();
    return;
  }
  if (!networkManager->init(serverConfig)) {
    Serial.println("Failed to initialize NetworkManager");
  }

  // Initialize RTC manager
  rtcManager = RTCManager::getInstance();
  if (!rtcManager || !rtcManager->init()) {
    Serial.println("Failed to initialize RTCManager");
  } else {
    rtcManager->startSync();
  }

  // Initialize Modbus TCP service
  EthernetManager* ethernetMgr = EthernetManager::getInstance();
  if (ethernetMgr) {
    modbusTcpService = new ModbusTcpService(configManager, ethernetMgr);
    if (modbusTcpService && modbusTcpService->init()) {
      modbusTcpService->start();
      Serial.println("Modbus TCP service started");
    } else {
      Serial.println("Failed to initialize Modbus TCP service");
    }
  }

  // Initialize CRUD handler in PSRAM FIRST (before Modbus services)
  crudHandler = (CRUDHandler*)heap_caps_malloc(sizeof(CRUDHandler), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (crudHandler) {
    new (crudHandler) CRUDHandler(configManager, serverConfig, loggingConfig);
  } else {
    crudHandler = new CRUDHandler(configManager, serverConfig, loggingConfig);  // Fallback to internal RAM
    if (!crudHandler) {
      Serial.println("Failed to allocate CRUDHandler");
      cleanup();
      return;
    }
  }
  Serial.println("CRUDHandler initialized");

  // Initialize Modbus RTU service (after CRUDHandler)
  modbusRtuService = new ModbusRtuService(configManager);
  if (modbusRtuService && modbusRtuService->init()) {
    modbusRtuService->start();
    Serial.println("Modbus RTU service started");
  } else {
    Serial.println("Failed to initialize Modbus RTU service");
  }

  // Initialize protocol managers based on server configuration
  String protocol = serverConfig->getProtocol();
  Serial.printf("Selected protocol: %s\n", protocol.c_str());

  // Initialize MQTT Manager
  mqttManager = MqttManager::getInstance(configManager, serverConfig, networkManager);
  if (mqttManager && mqttManager->init()) {
    if (protocol == "mqtt") {
      mqttManager->start();
      Serial.println("MQTT Manager started (active protocol)");
    } else {
      Serial.println("MQTT Manager initialized but not started (inactive protocol)");
    }
  } else {
    Serial.println("Failed to initialize MQTT Manager");
  }

  // Initialize HTTP Manager
  httpManager = HttpManager::getInstance(configManager, serverConfig, networkManager);
  if (httpManager && httpManager->init()) {
    if (protocol == "http") {
      httpManager->start();
      Serial.println("HTTP Manager started (active protocol)");
    } else {
      Serial.println("HTTP Manager initialized but not started (inactive protocol)");
    }
  } else {
    Serial.println("Failed to initialize HTTP Manager");
  }

  // CRUDHandler already initialized above

  // Initialize BLE manager in PSRAM
  bleManager = (BLEManager*)heap_caps_malloc(sizeof(BLEManager), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (bleManager) {
    new (bleManager) BLEManager("SURIOTA GW", crudHandler);
  } else {
    bleManager = new BLEManager("SURIOTA GW", crudHandler);  // Fallback to internal RAM
    if (!bleManager) {
      Serial.println("Failed to allocate BLE Manager");
      cleanup();
      return;
    }
  }
  if (!bleManager->begin()) {
    Serial.println("Failed to initialize BLE Manager");
    cleanup();
    return;
  }

  Serial.println("BLE CRUD Manager started successfully");
}

void loop() {
  // FreeRTOS-friendly delay - yields to other tasks
  vTaskDelay(pdMS_TO_TICKS(100));

  // Optional: Add watchdog feed or system monitoring here
  // esp_task_wdt_reset();
}