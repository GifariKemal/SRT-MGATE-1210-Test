#ifndef MODBUS_TCP_SERVICE_H
#define MODBUS_TCP_SERVICE_H

#include <ArduinoJson.h>
#include <Ethernet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "ConfigManager.h"
#include "EthernetManager.h"
#include <vector>
#include <queue>  // For std::priority_queue

class ModbusTcpService {
private:
  ConfigManager* configManager;
  EthernetManager* ethernetManager;
  bool running;
  TaskHandle_t tcpTaskHandle;

  // Device timers for refresh rate control
  struct DeviceTimer {
    String deviceId;
    unsigned long lastRead;
  };

  struct TcpDeviceConfig {
    String deviceId;
    DynamicJsonDocument doc{ 2048 };
  };
  std::vector<TcpDeviceConfig> tcpDevices;

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

  static uint16_t transactionCounter;

  static void readTcpDevicesTask(void* parameter);
  void readTcpDevicesLoop();
  void readTcpDeviceData(const JsonObject& deviceConfig);
  double processRegisterValue(const JsonObject& reg, uint16_t rawValue);
  double processMultiRegisterValue(const JsonObject& reg, uint16_t* values, int count, const String& baseType, const String& endianness_variant);
  void storeRegisterValue(const String& deviceId, const JsonObject& reg, double value);
  bool readModbusRegister(EthernetClient& client, uint8_t slaveId, uint8_t functionCode, uint16_t address, uint16_t qty, uint16_t* resultBuffer);
  bool readModbusCoil(EthernetClient& client, uint8_t slaveId, uint16_t address, bool* result);
  void buildModbusRequest(uint8_t* buffer, uint16_t transId, uint8_t unitId, uint8_t funcCode, uint16_t addr, uint16_t qty);
  bool parseModbusResponse(uint8_t* buffer, int length, uint8_t expectedFunc, uint16_t expectedQty, uint16_t* resultBuffer, bool* boolResult);

  void refreshDeviceList();

public:
  ModbusTcpService(ConfigManager* config, EthernetManager* ethernet);

  bool init();
  void start();
  void stop();
  void getStatus(JsonObject& status);

  void notifyConfigChange();

  ~ModbusTcpService();
};

#endif