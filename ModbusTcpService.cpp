#include "ModbusTcpService.h"
#include "QueueManager.h"
#include "CRUDHandler.h"
#include "RTCManager.h"
#include <byteswap.h>

extern CRUDHandler* crudHandler;

uint16_t ModbusTcpService::transactionCounter = 1;

ModbusTcpService::ModbusTcpService(ConfigManager* config, EthernetManager* ethernet)
  : configManager(config), ethernetManager(ethernet), running(false), tcpTaskHandle(nullptr) {}

bool ModbusTcpService::init() {
  Serial.println("Initializing custom Modbus TCP service...");

  if (!configManager) {
    Serial.println("ConfigManager is null");
    return false;
  }

  if (!ethernetManager) {
    Serial.println("EthernetManager is null");
    return false;
  }

  Serial.printf("Ethernet available: %s\n", ethernetManager->isAvailable() ? "YES" : "NO");
  Serial.println("Custom Modbus TCP service initialized successfully");
  return true;
}

void ModbusTcpService::start() {
  Serial.println("Starting custom Modbus TCP service...");

  if (running) {
    Serial.println("Service already running");
    return;
  }

  running = true;
  BaseType_t result = xTaskCreatePinnedToCore(
    readTcpDevicesTask,
    "MODBUS_TCP_TASK",
    8192,
    this,
    2,
    &tcpTaskHandle,  // Store the task handle
    1);

  if (result == pdPASS) {
    Serial.println("Custom Modbus TCP service started successfully");
  } else {
    Serial.println("Failed to create Modbus TCP task");
    running = false;
    tcpTaskHandle = nullptr;
  }
}

void ModbusTcpService::stop() {
  running = false;
  if (tcpTaskHandle) {
    vTaskDelay(pdMS_TO_TICKS(100));  // Allow task to exit gracefully
    vTaskDelete(tcpTaskHandle);
    tcpTaskHandle = nullptr;
  }
  Serial.println("Custom Modbus TCP service stopped");
}

void ModbusTcpService::notifyConfigChange() {
  if (tcpTaskHandle != nullptr) {
    xTaskNotifyGive(tcpTaskHandle);
  }
}

void ModbusTcpService::refreshDeviceList() {
  Serial.println("[TCP Task] Refreshing device list and schedule...");
  tcpDevices.clear();

  // Clear the priority queue
  std::priority_queue<PollingTask, std::vector<PollingTask>, std::greater<PollingTask>> emptyQueue;
  pollingQueue.swap(emptyQueue);

  // --- PERUBAHAN DI SINI ---
  StaticJsonDocument<2048> devicesIdList; // Ganti JsonDocument(2048)
  // --- AKHIR PERUBAHAN ---
  JsonArray deviceIds = devicesIdList.to<JsonArray>();
  configManager->listDevices(deviceIds);

  unsigned long now = millis();

  for (JsonVariant deviceIdVar : deviceIds) {
    String deviceId = deviceIdVar.as<String>();
    if (deviceId.isEmpty() || deviceId == "{}" || deviceId.length() < 3) {
      continue;
    }

    // --- PERUBAHAN DI SINI ---
    StaticJsonDocument<2048> tempDeviceDoc; // Ganti JsonDocument(2048)
    // --- AKHIR PERUBAHAN ---
    JsonObject deviceObj = tempDeviceDoc.to<JsonObject>();
    if (configManager->readDevice(deviceId, deviceObj)) {
      String protocol = deviceObj["protocol"] | "";
      if (protocol == "TCP") {
        TcpDeviceConfig newDeviceEntry;
        newDeviceEntry.deviceId = deviceId;
        newDeviceEntry.doc.set(deviceObj); // doc sekarang adalah StaticJsonDocument
        tcpDevices.push_back(std::move(newDeviceEntry));

        // Add device to the polling schedule for an immediate first poll
        pollingQueue.push({ deviceId, now });
      }
    }
  }
  Serial.printf("[TCP Task] Found %d TCP devices. Schedule rebuilt.\n", tcpDevices.size());
}

void ModbusTcpService::readTcpDevicesTask(void* parameter) {
  ModbusTcpService* service = static_cast<ModbusTcpService*>(parameter);
  service->readTcpDevicesLoop();
}

void ModbusTcpService::readTcpDevicesLoop() {

  DeviceTimer deviceTimers[10]; // Support up to 10 devices

  int timerCount = 0;



  while (running) {

    if (!ethernetManager || !ethernetManager->isAvailable()) {

      vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for network

      continue;

    }


    // --- PERUBAHAN DI SINI ---
    StaticJsonDocument<2048> devicesDoc; // Ganti JsonDocument(2048)
    // --- AKHIR PERUBAHAN ---
    JsonArray devices = devicesDoc.to<JsonArray>();
    configManager->listDevices(devices);



    unsigned long currentTime = millis();



    for (JsonVariant deviceVar : devices) {

      if (!running) break; // Exit if stopped



      String deviceId = deviceVar.as<String>();


      // --- PERUBAHAN DI SINI ---
      StaticJsonDocument<2048> deviceDoc; // Ganti JsonDocument(2048)
      // --- AKHIR PERUBAHAN ---
      JsonObject deviceObj = deviceDoc.to<JsonObject>();

      if (configManager->readDevice(deviceId, deviceObj)) {

        String protocol = deviceObj["protocol"] | "";



        if (protocol == "TCP") {

          int refreshRate = deviceObj["refresh_rate_ms"] | 5000;



          int timerIndex = -1;

          for (int i = 0; i < timerCount; i++) {

            if (deviceTimers[i].deviceId == deviceId) {

              timerIndex = i;

              break;

            }

          }



          if (timerIndex == -1 && timerCount < 10) {

            timerIndex = timerCount++;

            deviceTimers[timerIndex].deviceId = deviceId;

            deviceTimers[timerIndex].lastRead = 0;

          }



          if (timerIndex >= 0) {

            if (currentTime - deviceTimers[timerIndex].lastRead >= refreshRate) {

              readTcpDeviceData(deviceObj);

              deviceTimers[timerIndex].lastRead = currentTime;

            }

          }

        }

      }

    }



    vTaskDelay(pdMS_TO_TICKS(2000));

  }

}

void ModbusTcpService::readTcpDeviceData(const JsonObject& deviceConfig) {
  String deviceId = deviceConfig["device_id"] | "UNKNOWN";
  String ip = deviceConfig["ip"] | "";
  int port = deviceConfig["port"] | 502;
  uint8_t slaveId = deviceConfig["slave_id"] | 1;
  JsonArray registers = deviceConfig["registers"];

  if (ip.isEmpty() || registers.size() == 0) {
    return;
  }

  Serial.printf("Reading Ethernet device %s at %s:%d\n", deviceId.c_str(), ip.c_str(), port);

  EthernetClient client;
  if (!client.connect(ip.c_str(), port)) {
    Serial.printf("Failed to connect to %s:%d\n", ip.c_str(), port);
    return;
  }

  for (JsonVariant regVar : registers) {
    if (!running) break;

    JsonObject reg = regVar.as<JsonObject>();
    uint8_t functionCode = reg["function_code"] | 3;
    uint16_t address = reg["address"] | 0;
    String registerName = reg["register_name"] | "Unknown";
    String dataType = reg["data_type"] | "INT16";
    dataType.toUpperCase();

    // Extract base type and endianness from dataType
    String baseType = dataType;
    String endianness_variant = "";
    int underscoreIndex = dataType.indexOf('_');
    if (underscoreIndex != -1) {
      baseType = dataType.substring(0, underscoreIndex);
      endianness_variant = dataType.substring(underscoreIndex + 1);
    }

    uint16_t numRegisters = 1;
    if (baseType == "FLOAT32" || baseType == "INT32" || baseType == "UINT32") {
      numRegisters = 2;
    } else if (baseType == "DOUBLE64" || baseType == "INT64" || baseType == "UINT64") {
      numRegisters = 4;
    }

    uint16_t rawValues[numRegisters];  // Buffer to hold raw register values
    bool success = false;

    if (functionCode == 1 || functionCode == 2) {
      // Read coils/discrete inputs
      bool coilResult = false;
      if (readModbusCoil(client, slaveId, address, &coilResult)) {
        rawValues[0] = coilResult ? 1 : 0;  // Store as 0 or 1 for processRegisterValue
        success = true;
      }
    } else {
      // Read registers
      if (readModbusRegister(client, slaveId, functionCode, address, numRegisters, rawValues)) {
        success = true;
      }
    }

    if (success) {
      double value = (numRegisters == 1) ? processRegisterValue(reg, rawValues[0]) : processMultiRegisterValue(reg, rawValues, numRegisters, baseType, endianness_variant);

      storeRegisterValue(deviceId, reg, value);
      Serial.printf("%s: %s = %.6f\n", deviceId.c_str(), registerName.c_str(), value);
    } else {
      Serial.printf("%s: %s = ERROR\n", deviceId.c_str(), registerName.c_str());
    }

    vTaskDelay(pdMS_TO_TICKS(50));  // Small delay between registers
  }

  client.stop();
}

double ModbusTcpService::processRegisterValue(const JsonObject& reg, uint16_t rawValue) {
  String dataType = reg["data_type"];
  dataType.toUpperCase();

  if (dataType == "INT16") {
    return (int16_t)rawValue;
  } else if (dataType == "UINT16") {
    return rawValue;
  } else if (dataType == "BOOL") {
    return rawValue != 0 ? 1.0 : 0.0;
  } else if (dataType == "BINARY") {
    return rawValue;
  }

  return rawValue;
}

double ModbusTcpService::processMultiRegisterValue(const JsonObject& reg, uint16_t* values, int count, const String& baseType, const String& endianness_variant) {

  if (count == 2) {

    uint32_t combined;

    if (endianness_variant == "BE") {  // Big Endian (ABCD)

      combined = ((uint32_t)values[0] << 16) | values[1];

    } else if (endianness_variant == "LE") {  // True Little Endian (DCBA)

      combined = (((uint32_t)values[1] & 0xFF) << 24) | (((uint32_t)values[1] & 0xFF00) << 8) |

                 (((uint32_t)values[0] & 0xFF) << 8) | ((uint32_t)values[0] >> 8);

    } else if (endianness_variant == "BE_BS") {  // Big Endian + Byte Swap (BADC)

      combined = (((uint32_t)values[0] & 0xFF) << 24) | (((uint32_t)values[0] & 0xFF00) << 8) |

                 (((uint32_t)values[1] & 0xFF) << 8) | ((uint32_t)values[1] >> 8);

    } else if (endianness_variant == "LE_BS") {  // Little Endian + Word Swap (CDAB)

      combined = ((uint32_t)values[1] << 16) | values[0];

    } else {  // Default to Big Endian

      combined = ((uint32_t)values[0] << 16) | values[1];
    }



    if (baseType == "INT32") {

      return (int32_t)combined;

    } else if (baseType == "UINT32") {

      return combined;

    } else if (baseType == "FLOAT32") {

      return *(float*)&combined;
    }

  } else if (count == 4) {

    uint64_t combined;



    if (endianness_variant == "BE") {  // Big Endian (W0, W1, W2, W3)

      combined = ((uint64_t)values[0] << 48) | ((uint64_t)values[1] << 32) | ((uint64_t)values[2] << 16) | values[3];

    } else if (endianness_variant == "LE") {  // True Little Endian (B8..B1)

      uint64_t b1 = values[0] >> 8;

      uint64_t b2 = values[0] & 0xFF;

      uint64_t b3 = values[1] >> 8;

      uint64_t b4 = values[1] & 0xFF;

      uint64_t b5 = values[2] >> 8;

      uint64_t b6 = values[2] & 0xFF;

      uint64_t b7 = values[3] >> 8;

      uint64_t b8 = values[3] & 0xFF;

      combined = (b8 << 56) | (b7 << 48) | (b6 << 40) | (b5 << 32) | (b4 << 24) | (b3 << 16) | (b2 << 8) | b1;

    } else if (endianness_variant == "BE_BS") {  // Big Endian with Byte Swap (B2B1, B4B3, B6B5, B8B7)

      auto bswap_16 = [](uint16_t val) {
        return (val << 8) | (val >> 8);
      };

      combined = ((uint64_t)bswap_16(values[0]) << 48) |

                 ((uint64_t)bswap_16(values[1]) << 32) |

                 ((uint64_t)bswap_16(values[2]) << 16) |

                 ((uint64_t)bswap_16(values[3]));

    } else if (endianness_variant == "LE_BS") {  // Little Endian with Word Swap (W3, W2, W1, W0)

      combined = ((uint64_t)values[3] << 48) | ((uint64_t)values[2] << 32) | ((uint64_t)values[1] << 16) | (uint64_t)values[0];

    } else {  // Default to Big Endian

      combined = ((uint64_t)values[0] << 48) | ((uint64_t)values[1] << 32) | ((uint64_t)values[2] << 16) | values[3];
    }



    if (baseType == "INT64") {

      return (double)(int64_t)combined;

    } else if (baseType == "UINT64") {

      return (double)combined;

    } else if (baseType == "DOUBLE64") {

      return *(double*)&combined;
    }
  }



  return values[0];  // Fallback
}

bool ModbusTcpService::readModbusRegister(EthernetClient& client, uint8_t slaveId, uint8_t functionCode, uint16_t address, uint16_t qty, uint16_t* resultBuffer) {
  // Build Modbus TCP request
  uint8_t request[12];
  uint16_t transId = transactionCounter++;
  buildModbusRequest(request, transId, slaveId, functionCode, address, qty);

  // Send request
  client.write(request, 12);

  // Wait for response with timeout
  unsigned long timeout = millis() + 5000;
  
  // Perkiraan panjang response minimal
  int minResponseLength = 9; // Header minimal
  if (functionCode == 3 || functionCode == 4) {
      minResponseLength = 9 + 1 + (qty * 2); // Header + byte count + data
  } else if (functionCode == 1 || functionCode == 2) {
      minResponseLength = 9 + 1 + (qty + 7) / 8; // Header + byte count + data (bit-packed)
  }

  while (client.available() < minResponseLength && millis() < timeout) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (client.available() < minResponseLength) {
    Serial.printf("[TCP Read] Timeout or insufficient data. Expected %d, got %d\n", minResponseLength, client.available());
    while(client.available()) client.read(); // Kosongkan buffer
    return false;
  }

  // Read response
  uint8_t response[256];  // Max Modbus PDU is 253 bytes, so 256 is safe for header + data
  int bytesRead = client.readBytes(response, client.available());

  return parseModbusResponse(response, bytesRead, functionCode, qty, resultBuffer, nullptr);
}

bool ModbusTcpService::readModbusCoil(EthernetClient& client, uint8_t slaveId, uint16_t address, bool* result) {

  // Build Modbus TCP request for coil (FC 1, qty 1)
  uint8_t request[12];
  uint16_t transId = transactionCounter++;
  buildModbusRequest(request, transId, slaveId, 1, address, 1);  // Function code 1 for coils

  // Send request
  client.write(request, 12);

  // Wait for response with timeout (Expected: 9 header + 1 byte count + 1 data byte = 11 bytes)
  unsigned long timeout = millis() + 5000;
  int expectedLength = 11;
  while (client.available() < expectedLength && millis() < timeout) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  if (client.available() < expectedLength) {
    Serial.printf("[TCP ReadCoil] Timeout or insufficient data. Expected %d, got %d\n", expectedLength, client.available());
    while(client.available()) client.read(); // Kosongkan buffer
    return false;
  }

  // Read response
  uint8_t response[256];
  int bytesRead = client.readBytes(response, client.available());

  return parseModbusResponse(response, bytesRead, 1, 1, nullptr, result);
}

void ModbusTcpService::buildModbusRequest(uint8_t* buffer, uint16_t transId, uint8_t unitId, uint8_t funcCode, uint16_t addr, uint16_t qty) {
  // Modbus TCP header
  buffer[0] = (transId >> 8) & 0xFF;  // Transaction ID high
  buffer[1] = transId & 0xFF;         // Transaction ID low
  buffer[2] = 0x00;                   // Protocol ID high
  buffer[3] = 0x00;                   // Protocol ID low
  buffer[4] = 0x00;                   // Length high
  buffer[5] = 0x06;                   // Length low (6 bytes following)
  buffer[6] = unitId;                 // Unit ID

  // Modbus PDU
  buffer[7] = funcCode;            // Function code
  buffer[8] = (addr >> 8) & 0xFF;  // Start address high
  buffer[9] = addr & 0xFF;         // Start address low
  buffer[10] = (qty >> 8) & 0xFF;  // Quantity high
  buffer[11] = qty & 0xFF;         // Quantity low
}

bool ModbusTcpService::parseModbusResponse(uint8_t* buffer, int length, uint8_t expectedFunc, uint16_t expectedQty, uint16_t* resultBuffer, bool* boolResult) {
  if (length < 9) {
    Serial.println("[TCP Parse] Response too short.");
    return false;
  }

  // Check function code
  uint8_t funcCode = buffer[7];
  if (funcCode != expectedFunc) {
    // Check for error response (MSB set)
    if (funcCode == (expectedFunc | 0x80)) {
      Serial.printf("[TCP Parse] Modbus error response: Function Code 0x%02X, Exception Code 0x%02X\n", funcCode, buffer[8]);
    } else {
      Serial.printf("[TCP Parse] Function code mismatch. Expected 0x%02X, got 0x%02X\n", expectedFunc, funcCode);
    }
    return false;
  }

  // Parse data based on function code
  if (funcCode == 1 || funcCode == 2) { // Read Coils / Read Discrete Inputs
    uint8_t byteCount = buffer[8];
    if (length >= (9 + 1 + byteCount) && boolResult && expectedQty == 1) { 
      if (byteCount > 0) {
        *boolResult = (buffer[9] & 0x01) != 0;
        return true;
      }
    } else if (length < (9 + 1 + byteCount)) {
        Serial.println("[TCP Parse] Mismatch in coil data length.");
        return false;
    }
  } else if (funcCode == 3 || funcCode == 4) { // Read Holding / Read Input Registers
    uint8_t byteCount = buffer[8];
    if (byteCount != (expectedQty * 2)) {
        Serial.printf("[TCP Parse] Byte count mismatch. Expected %d, got %d\n", (expectedQty * 2), byteCount);
        return false;
    }
    if (length >= (9 + 1 + byteCount) && resultBuffer) {
      for (int i = 0; i < expectedQty; i++) {
        resultBuffer[i] = (buffer[9 + (i * 2)] << 8) | buffer[10 + (i * 2)];
      }
      return true;
    } else if (length < (9 + 1 + byteCount)) {
        Serial.println("[TCP Parse] Mismatch in register data length.");
        return false;
    }
  }

  Serial.println("[TCP Parse] Unknown parse error.");
  return false;
}

void ModbusTcpService::storeRegisterValue(const String& deviceId, const JsonObject& reg, double value) {
  QueueManager* queueMgr = QueueManager::getInstance();

  // Create data point in required format
  // --- PERUBAHAN DI SINI ---
  StaticJsonDocument<256> dataDoc; // Mengganti DynamicJsonDocument(256)
  // --- AKHIR PERUBAHAN ---
  JsonObject dataPoint = dataDoc.to<JsonObject>();

  RTCManager* rtc = RTCManager::getInstance();
  if (rtc) {
    DateTime now = rtc->getCurrentTime();
    dataPoint["time"] = now.unixtime();
  }
  dataPoint["name"] = reg["register_name"].as<String>();
  dataPoint["address"] = reg["address"];
  dataPoint["datatype"] = reg["data_type"].as<String>();
  dataPoint["value"] = value;
  dataPoint["device_id"] = deviceId;
  dataPoint["register_id"] = reg["register_id"].as<String>();

  // Serial.printf("Data queued: %s\n", dataPoint["name"].as<String>().c_str()); // Dikomentari agar tidak spam log

  // Add to message queue
  if (queueMgr) {
    queueMgr->enqueue(dataPoint);
  }

  // Check if this device is being streamed
  String streamId = "";
  bool crudHandlerAvailable = (crudHandler != nullptr);

  if (crudHandler) {
    streamId = crudHandler->getStreamDeviceId(); // Ini harusnya fungsi thread-safe
  }

  // Komentari log spam
  // Serial.printf("TCP: Device %s, CRUDHandler: %s, StreamID '%s', Match: %s\n",
  //               deviceId.c_str(),
  //               crudHandlerAvailable ? "OK" : "NULL",
  //               streamId.c_str(),
  //               (streamId == deviceId) ? "YES" : "NO");

  if (!streamId.isEmpty() && streamId == deviceId && queueMgr) {
    Serial.printf("[TCP] Streaming data for device %s to BLE\n", deviceId.c_str());
    queueMgr->enqueueStream(dataPoint);
  } 
  // else if (!streamId.isEmpty() && streamId != deviceId) {
  //   Serial.printf("[TCP] Device %s not streaming (StreamID: %s)\n", deviceId.c_str(), streamId.c_str());
  // } else if (streamId.isEmpty()) {
  //   Serial.printf("[TCP] No streaming active (StreamID empty)\n");
  // }
}

void ModbusTcpService::getStatus(JsonObject& status) {
  status["running"] = running;
  status["service_type"] = "modbus_tcp";
  status["tcp_device_count"] = tcpDevices.size();
}

ModbusTcpService::~ModbusTcpService() {
  stop();
}