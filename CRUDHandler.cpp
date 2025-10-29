#include "CRUDHandler.h"
#include "BLEManager.h"
#include "QueueManager.h"
#include "ModbusRtuService.h"
#include "ModbusTcpService.h"
#include "MemoryManager.h"  // For make_psram_unique

// Make service pointers available to the handler
extern ModbusRtuService* modbusRtuService;
extern ModbusTcpService* modbusTcpService;

CRUDHandler::CRUDHandler(ConfigManager* config, ServerConfig* serverCfg, LoggingConfig* loggingCfg)
  : configManager(config), serverConfig(serverCfg), loggingConfig(loggingCfg), streamDeviceId("") {
  setupCommandHandlers();
}

void CRUDHandler::handle(BLEManager* manager, const JsonDocument& command) {
  String op = command["op"] | "";
  String type = command["type"] | "";

  Serial.printf("DEBUG: Command - op: '%s', type: '%s'\n", op.c_str(), type.c_str());

  if (op == "read" && readHandlers.count(type)) {
    readHandlers[type](manager, command);
  } else if (op == "create" && createHandlers.count(type)) {
    createHandlers[type](manager, command);
  } else if (op == "update" && updateHandlers.count(type)) {
    updateHandlers[type](manager, command);
  } else if (op == "delete" && deleteHandlers.count(type)) {
    deleteHandlers[type](manager, command);
  } else {
    manager->sendError("Unsupported operation or type: " + op + "/" + type);
  }
}

void CRUDHandler::setupCommandHandlers() {
  // === READ HANDLERS ===
  readHandlers["devices"] = [this](BLEManager* manager, const JsonDocument& command) {
    auto response = make_psram_unique<DynamicJsonDocument>(1024);
    (*response)["status"] = "ok";
    JsonArray devices = response->createNestedArray("devices");
    configManager->listDevices(devices);
    manager->sendResponse(*response);
  };

  readHandlers["devices_summary"] = [this](BLEManager* manager, const JsonDocument& command) {
    auto response = make_psram_unique<DynamicJsonDocument>(2048);
    (*response)["status"] = "ok";
    JsonArray summary = response->createNestedArray("devices_summary");
    configManager->getDevicesSummary(summary);
    manager->sendResponse(*response);
  };

  readHandlers["device"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    auto response = make_psram_unique<DynamicJsonDocument>(2048);
    (*response)["status"] = "ok";
    JsonObject data = response->createNestedObject("data");
    if (configManager->readDevice(deviceId, data)) {
      manager->sendResponse(*response);
    } else {
      manager->sendError("Device not found");
    }
  };

  readHandlers["registers"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    auto response = make_psram_unique<DynamicJsonDocument>(4096);
    (*response)["status"] = "ok";
    JsonArray registers = response->createNestedArray("registers");
    if (configManager->listRegisters(deviceId, registers)) {
      manager->sendResponse(*response);
    } else {
      manager->sendError("No registers found");
    }
  };

  readHandlers["registers_summary"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    auto response = make_psram_unique<DynamicJsonDocument>(4096);
    (*response)["status"] = "ok";
    JsonArray summary = response->createNestedArray("registers_summary");
    if (configManager->getRegistersSummary(deviceId, summary)) {
      manager->sendResponse(*response);
    } else {
      manager->sendError("No registers found");
    }
  };

  readHandlers["server_config"] = [this](BLEManager* manager, const JsonDocument& command) {
    auto response = make_psram_unique<DynamicJsonDocument>(2048);
    (*response)["status"] = "ok";
    JsonObject serverConfigObj = response->createNestedObject("server_config");
    if (serverConfig->getConfig(serverConfigObj)) {
      manager->sendResponse(*response);
    } else {
      manager->sendError("Failed to get server config");
    }
  };

  readHandlers["logging_config"] = [this](BLEManager* manager, const JsonDocument& command) {
    auto response = make_psram_unique<DynamicJsonDocument>(512);
    (*response)["status"] = "ok";
    JsonObject loggingConfigObj = response->createNestedObject("logging_config");
    if (loggingConfig->getConfig(loggingConfigObj)) {
      manager->sendResponse(*response);
    } else {
      manager->sendError("Failed to get logging config");
    }
  };

  readHandlers["data"] = [this](BLEManager* manager, const JsonDocument& command) {
    String device = command["device_id"] | "";
    if (device == "stop") {
      streamDeviceId = "";
      QueueManager::getInstance()->clearStream();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Data streaming stopped";
      manager->sendResponse(*response);
    } else if (!device.isEmpty()) {
      streamDeviceId = device;
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Data streaming started for device: " + device;
      manager->sendResponse(*response);
    } else {
      manager->sendError("Empty device ID");
    }
  };

  // === CREATE HANDLERS ===
  createHandlers["device"] = [this](BLEManager* manager, const JsonDocument& command) {
    JsonObjectConst config = command["config"];
    String deviceId = configManager->createDevice(config);
    if (!deviceId.isEmpty()) {
      if (modbusRtuService) modbusRtuService->notifyConfigChange();
      if (modbusTcpService) modbusTcpService->notifyConfigChange();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["device_id"] = deviceId;
      manager->sendResponse(*response);
    } else {
      manager->sendError("Device creation failed");
    }
  };

  createHandlers["register"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    JsonObjectConst config = command["config"];
    String registerId = configManager->createRegister(deviceId, config);
    if (!registerId.isEmpty()) {
      if (modbusRtuService) modbusRtuService->notifyConfigChange();
      if (modbusTcpService) modbusTcpService->notifyConfigChange();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["register_id"] = registerId;
      manager->sendResponse(*response);
    } else {
      manager->sendError("Register creation failed");
    }
  };

  // === UPDATE HANDLERS ===
  updateHandlers["device"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    JsonObjectConst config = command["config"];
    if (configManager->updateDevice(deviceId, config)) {
      if (modbusRtuService) modbusRtuService->notifyConfigChange();
      if (modbusTcpService) modbusTcpService->notifyConfigChange();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Device updated";
      manager->sendResponse(*response);
    } else {
      manager->sendError("Device update failed");
    }
  };

  updateHandlers["register"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    String registerId = command["register_id"] | "";
    JsonObjectConst config = command["config"];
    if (configManager->updateRegister(deviceId, registerId, config)) {
      if (modbusRtuService) modbusRtuService->notifyConfigChange();
      if (modbusTcpService) modbusTcpService->notifyConfigChange();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Register updated";
      manager->sendResponse(*response);
    } else {
      manager->sendError("Register update failed");
    }
  };

  updateHandlers["server_config"] = [this](BLEManager* manager, const JsonDocument& command) {
    JsonObjectConst config = command["config"];
    if (serverConfig->updateConfig(config)) {
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Server configuration updated";
      manager->sendResponse(*response);
    } else {
      manager->sendError("Server configuration update failed");
    }
  };

  updateHandlers["logging_config"] = [this](BLEManager* manager, const JsonDocument& command) {
    JsonObjectConst config = command["config"];
    if (loggingConfig->updateConfig(config)) {
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Logging configuration updated";
      manager->sendResponse(*response);
    } else {
      manager->sendError("Logging configuration update failed");
    }
  };

  // === DELETE HANDLERS ===
  deleteHandlers["device"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    if (configManager->deleteDevice(deviceId)) {
      if (modbusRtuService) modbusRtuService->notifyConfigChange();
      if (modbusTcpService) modbusTcpService->notifyConfigChange();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Device deleted";
      manager->sendResponse(*response);
    } else {
      manager->sendError("Device deletion failed");
    }
  };

  deleteHandlers["register"] = [this](BLEManager* manager, const JsonDocument& command) {
    String deviceId = command["device_id"] | "";
    String registerId = command["register_id"] | "";
    if (configManager->deleteRegister(deviceId, registerId)) {
      if (modbusRtuService) modbusRtuService->notifyConfigChange();
      if (modbusTcpService) modbusTcpService->notifyConfigChange();
      auto response = make_psram_unique<DynamicJsonDocument>(128);
      (*response)["status"] = "ok";
      (*response)["message"] = "Register deleted";
      manager->sendResponse(*response);
    } else {
      manager->sendError("Register deletion failed");
    }
  };
}
