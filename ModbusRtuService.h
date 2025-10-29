#ifndef MODBUS_RTU_SERVICE_H
#define MODBUS_RTU_SERVICE_H

#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ModbusMaster.h>
#include "ConfigManager.h"
#include <vector>
#include <queue>  // For std::priority_queue

class ModbusRtuService {
private:
  ConfigManager* configManager;
  bool running;
  TaskHandle_t rtuTaskHandle;

  // Device timers for refresh rate control
  struct DeviceTimer {
    String deviceId;
    unsigned long lastRead;
  };

  struct RtuDeviceConfig {
    String deviceId;
    DynamicJsonDocument doc{ 2048 };
  };
  std::vector<RtuDeviceConfig> rtuDevices;

  // --- New Scheduler Structures ---
  struct PollingTask {
    String deviceId;
    unsigned long nextPollTime;

    // Overload > operator for the priority queue (min-heap)
    bool operator>(const PollingTask& other) const {
      return nextPollTime > other.nextPollTime;
    }
  };

  std::priority_queue<PollingTask, std::vector<PollingTask>, std::greater<PollingTask>> pollingQueue;
  // --------------------------------

  static const int RTU_RX1 = 15;
  static const int RTU_TX1 = 16;
  static const int RTU_RX2 = 17;
  static const int RTU_TX2 = 18;

  HardwareSerial* serial1;
  HardwareSerial* serial2;
  ModbusMaster* modbus1;
  ModbusMaster* modbus2;

  static void readRtuDevicesTask(void* parameter);
  void readRtuDevicesLoop();
  void readRtuDeviceData(const JsonObject& deviceConfig);
  double processRegisterValue(const JsonObject& reg, uint16_t rawValue);
  double processMultiRegisterValue(const JsonObject& reg, uint16_t* values, int count, const String& baseType, const String& endianness_variant);
  bool readMultipleRegisters(ModbusMaster* modbus, uint8_t functionCode, uint16_t address, int count, uint16_t* values);
  void storeRegisterValue(const String& deviceId, const JsonObject& reg, double value);
  ModbusMaster* getModbusForBus(int serialPort);

  void refreshDeviceList();

public:
  ModbusRtuService(ConfigManager* config);

  bool init();
  void start();
  void stop();
  void getStatus(JsonObject& status);

  void notifyConfigChange();

  ~ModbusRtuService();
};

#endif