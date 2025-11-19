# CLAUDE.md - AI Assistant Guide for SRT-MGATE-1210 IoT Gateway

## Project Overview

**Project Name:** SRT-MGATE-1210 Industrial IoT Gateway
**Purpose:** Bridge legacy Modbus devices (PLCs, sensors, machinery) with modern IoT platforms (AWS, Azure, on-premises servers)
**Platform:** ESP32 with PSRAM, Arduino Framework
**Total Code:** ~5,773 lines across 31 files
**Language:** C++ (Arduino)
**Key Technologies:** FreeRTOS, Modbus RTU/TCP, MQTT, HTTP, BLE

### What This Gateway Does

Converts industrial Modbus data to IoT protocols (MQTT/HTTP) with:
- Dual connectivity: WiFi + Ethernet with automatic failover
- Dual Modbus interfaces: RS485 RTU (2 buses) + Ethernet TCP
- BLE configuration interface for field deployment
- Real-time data streaming and queuing
- RTC-based timestamping with NTP sync

---

## Architecture Overview

### System Layer Model

```
┌─────────────────────────────────────────────────────────────┐
│                    MAIN.INO (Entry Point)                    │
│                  ESP32 + FreeRTOS Kernel                     │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
┌───────▼────────┐  ┌─────────▼────────┐  ┌────────▼────────┐
│ Configuration  │  │  Network Layer   │  │  BLE Interface  │
│ - ConfigMgr    │  │ - NetworkMgr     │  │ - BLEManager    │
│ - ServerConfig │  │ - WiFiManager    │  │ - CRUDHandler   │
│ - LoggingCfg   │  │ - EthernetMgr    │  │                 │
└────────────────┘  └──────────────────┘  └─────────────────┘
        │                     │                     │
        └─────────────────────┼─────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
┌───────▼────────┐  ┌─────────▼────────┐  ┌────────▼────────┐
│ Modbus Layer   │  │  Data Pipeline   │  │  IoT Protocols  │
│ - ModbusRTU    │  │ - QueueManager   │  │ - MqttManager   │
│ - ModbusTCP    │  │ - RTCManager     │  │ - HttpManager   │
└────────────────┘  └──────────────────┘  └─────────────────┘
```

### Data Flow

```
[Modbus Device]
    ↓ (RS485 / Ethernet)
[ModbusRTU/TCP Service] ← polls at configured refresh_rate_ms
    ↓
[processRegisterValue()] ← handles endianness, data types
    ↓
[storeRegisterValue()] ← adds RTC timestamp
    ↓
[QueueManager.enqueue()] ← thread-safe FIFO
    ↓
    ├─→ [StreamQueue] → BLE → Mobile App (if streaming)
    └─→ [DataQueue] → MQTT/HTTP → Cloud Platform
```

---

## Key Components Reference

### Core Managers

| Component | File | Line | Purpose | Critical Methods |
|-----------|------|------|---------|------------------|
| **ConfigManager** | `ConfigManager.h` | 8 | Device/register configuration with PSRAM cache | `loadDevices()`, `createDevice()`, `updateDevice()`, `deleteDevice()` |
| **ServerConfig** | `ServerConfig.h` | 8 | Server connectivity & protocol settings | `loadConfig()`, `saveConfig()` |
| **NetworkManager** | `NetworkManager.h` | 9 | Auto-failover WiFi/Ethernet | `begin()`, `getActiveClient()`, `getActiveMode()` |
| **QueueManager** | `QueueManager.h` | 9 | Dual FreeRTOS queues (data + streaming) | `enqueue()`, `dequeue()`, `enqueueStream()` |
| **ModbusRtuService** | `ModbusRtuService.h` | 13 | RS485 Modbus RTU (2 buses) | `begin()`, `readRtuDeviceData()`, `notifyConfigChange()` |
| **ModbusTcpService** | `ModbusTcpService.h` | 13 | Ethernet Modbus TCP/IP | `begin()`, `readTcpDeviceData()` |
| **MqttManager** | `MqttManager.h` | 13 | MQTT publishing | `begin()`, `connect()`, `publish()` |
| **HttpManager** | `HttpManager.h` | 13 | HTTP POST with retry | `begin()`, `sendHttpData()` |
| **BLEManager** | `BLEManager.h` | 25 | BLE GATT configuration server | `begin()`, `isDeviceConnected()` |
| **CRUDHandler** | `CRUDHandler.h` | 16 | BLE command router | `processCommand()` |
| **RTCManager** | `RTCManager.h` | 12 | DS3231 RTC + NTP sync | `begin()`, `getCurrentTimestamp()` |
| **LEDManager** | `LEDManager.h` | 11 | Status indicators | `blinkData()`, `setNetworkStatus()` |

### Hardware Configuration

**ESP32 Pins:**
- **Ethernet (W5500):** CS=48, INT=9, MOSI=14, MISO=21, SCK=47
- **Serial Bus 1:** RX=15, TX=16 (Modbus RTU)
- **Serial Bus 2:** RX=17, TX=18 (Modbus RTU)
- **I2C (RTC):** Default SDA/SCL
- **LED Indicators:** Network status, data activity

---

## File Structure & Responsibilities

```
SRT-MGATE-1210-Test/
├── main.ino                    # Entry point, initialization sequence
├── ConfigManager.{h,cpp}       # Device/register CRUD, PSRAM caching
├── ServerConfig.{h,cpp}        # Protocol & connectivity settings
├── NetworkManager.{h,cpp}      # WiFi/Ethernet failover orchestration
├── WiFiManager.{h,cpp}         # WiFi connection with ref counting
├── EthernetManager.{h,cpp}     # W5500 Ethernet driver
├── QueueManager.{h,cpp}        # FreeRTOS dual queues
├── ModbusRtuService.{h,cpp}    # RS485 Modbus polling & processing
├── ModbusTcpService.{h,cpp}    # Ethernet Modbus polling & processing
├── MqttManager.{h,cpp}         # MQTT client & publishing
├── HttpManager.{h,cpp}         # HTTP POST client
├── BLEManager.{h,cpp}          # BLE GATT server
├── CRUDHandler.{h,cpp}         # BLE command processor
├── RTCManager.{h,cpp}          # RTC + NTP time management
├── LEDManager.{h,cpp}          # Visual feedback
├── LoggingConfig.{h,cpp}       # Logging settings
└── MemoryManager.h             # PSRAM allocation utilities
```

---

## Development Conventions

### Naming Conventions

| Type | Pattern | Example |
|------|---------|---------|
| Classes | PascalCase | `ModbusRtuService` |
| Methods | camelCase | `readRtuDeviceData()` |
| Variables | camelCase | `devicesCacheValid` |
| Constants | UPPER_SNAKE_CASE | `DEVICES_FILE`, `MAX_QUEUE_SIZE` |
| Pointers | prefix `p` | `pServer`, `pCommandChar` |
| File names | MatchClassName | `ConfigManager.h/.cpp` |

### Design Patterns Used

1. **Singleton Pattern:** QueueManager, LEDManager, RTCManager
   ```cpp
   static QueueManager* getInstance();
   ```

2. **Factory Pattern:** PSRAM allocation utilities (`MemoryManager.h:61-69`)
   ```cpp
   PsramUniquePtr<T> make_psram_unique(Args&&... args);
   ```

3. **Observer Pattern:** FreeRTOS tasks notify each other via queues/semaphores

4. **Command Pattern:** CRUDHandler routes JSON commands to handlers
   ```cpp
   std::map<String, CommandHandler> readHandlers;
   ```

### Memory Management Strategy

**PSRAM (External RAM):**
- Large objects (>1KB): ConfigManager, CRUDHandler, BLEManager
- JSON caches: DynamicJsonDocument
- Queue items: Serialized JSON strings
- **Allocation:** `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
- **Fallback:** Automatic to internal RAM if PSRAM unavailable

**Internal RAM:**
- Small temporary objects: `StaticJsonDocument<256>`
- Task stacks
- FreeRTOS control structures

**Custom Deleters:** `PsramTypeDeleter` in `MemoryManager.h:31-38`

### Thread Safety

**FreeRTOS Concurrency:**
- **Mutexes:** QueueManager (`QueueManager.h:24`), CRUDHandler (`CRUDHandler.h:22`)
- **Queues:** Lock-free message passing between tasks
- **Task Pinning:**
  - Core 0: MQTT, HTTP, LED
  - Core 1: BLE, Modbus, Network Failover

**Example Task Creation:**
```cpp
xTaskCreatePinnedToCore(
  taskFunction,       // Function pointer
  "TASK_NAME",        // Debug name
  8192,               // Stack size (bytes)
  this,               // Parameter
  1,                  // Priority
  &taskHandle,        // Handle
  0                   // Core (0 or 1)
);
```

### Error Handling Patterns

1. **Null Checks (pervasive):**
   ```cpp
   if (!configManager) {
     Serial.println("ConfigManager is null");
     return false;
   }
   ```

2. **Fallback Allocation (`main.ino:84-93`):**
   ```cpp
   ptr = (Type*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
   if (ptr) {
     new (ptr) Type();  // Placement new
   } else {
     ptr = new Type();  // Fallback to internal RAM
   }
   ```

3. **Return Types:**
   - `bool` for success/failure
   - Empty `String` for failed operations
   - `nullptr` for failed allocations

4. **Cleanup on Error:** See `cleanup()` function in `main.ino:46-64`

### Logging Conventions

**Serial Logging Format:**
```cpp
Serial.printf("[COMPONENT] Message with %d parameters\n", value);
```

**Component Tags:**
- `[MQTT]` - MQTT operations
- `[RTU]` - Modbus RTU
- `[TCP]` - Modbus TCP
- `[NetworkMgr]` - Network failover
- `[BLE]` - BLE operations
- `[HTTP]` - HTTP operations

**Debug Macros:** Conditional compilation with `#define DEBUG_CONFIG_MANAGER`

---

## Configuration Management

### Configuration Files (LittleFS)

| File | Purpose | Structure |
|------|---------|-----------|
| `/devices.json` | Modbus devices | Array of device objects with protocol, slave_id, registers |
| `/registers.json` | Register definitions | Array of register objects per device |
| `/server_config.json` | Network & IoT settings | MQTT/HTTP broker, WiFi/ETH config |
| `/logging_config.json` | Logging settings | Retention, intervals |

### Device Configuration Schema

```json
{
  "device_id": "Da3f41c",          // Format: D[6-hex-digits]
  "protocol": "RTU",               // "RTU" or "TCP"
  "slave_id": 1,                   // Modbus slave address (1-247)
  "serial_port": 1,                // 1 or 2 (RTU only)
  "baud_rate": 9600,               // RTU: 9600, 19200, 38400, 115200
  "ip_address": "192.168.1.100",   // TCP only
  "port": 502,                     // TCP only
  "refresh_rate_ms": 5000,         // Polling interval
  "registers": []                  // Array of register IDs
}
```

### Register Configuration Schema

```json
{
  "register_id": "R8a2bc1",        // Format: R[6-hex-digits]
  "device_id": "Da3f41c",          // Parent device
  "name": "Temperature",           // Human-readable name
  "address": 100,                  // Modbus register address
  "function_code": "FC03",         // FC01, FC02, FC03, FC04
  "datatype": "FLOAT32_BE",        // See Data Types section
  "quantity": 2                    // Number of registers (1, 2, or 4)
}
```

### Supported Data Types

**Single Register (16-bit):**
- `INT16`, `UINT16`, `BOOL`, `BINARY`

**Two Registers (32-bit):**
- `INT32_BE`, `INT32_LE`, `INT32_BE_BS`, `INT32_LE_BS`
- `UINT32_BE`, `UINT32_LE`, `UINT32_BE_BS`, `UINT32_LE_BS`
- `FLOAT32_BE`, `FLOAT32_LE`, `FLOAT32_BE_BS`, `FLOAT32_LE_BS`

**Four Registers (64-bit):**
- `INT64_BE`, `INT64_LE`, `INT64_BE_BS`, `INT64_LE_BS`
- `UINT64_BE`, `UINT64_LE`, `UINT64_BE_BS`, `UINT64_LE_BS`
- `DOUBLE64_BE`, `DOUBLE64_LE`, `DOUBLE64_BE_BS`, `DOUBLE64_LE_BS`

**Endianness Variants:**
- `BE` (Big Endian): ABCD
- `LE` (Little Endian): DCBA
- `BE_BS` (Big Endian Byte Swap): BADC
- `LE_BS` (Little Endian Word Swap): CDAB

### Hot Configuration Reload

**Trigger:** Call `notifyConfigChange()` on ModbusRtuService or ModbusTcpService

**Process:**
1. Invalidates ConfigManager cache
2. Notifies Modbus task via `xTaskNotifyGive()`
3. Task reloads devices on next iteration
4. No system restart required

**Example:**
```cpp
ConfigManager* cm = ConfigManager::getInstance();
cm->createDevice(deviceData);  // Writes to LittleFS
ModbusRtuService* rtu = ModbusRtuService::getInstance();
rtu->notifyConfigChange();     // Triggers reload
```

---

## Common Development Tasks

### Adding a New Manager Class

1. **Create header/source pair:** `NewManager.h` and `NewManager.cpp`

2. **Use Singleton pattern if global state needed:**
   ```cpp
   // NewManager.h
   class NewManager {
   private:
     static NewManager* instance;
     NewManager();  // Private constructor
   public:
     static NewManager* getInstance();
     void begin();
     void stop();
   };
   ```

3. **Initialize in `main.ino` setup():**
   ```cpp
   NewManager* newMgr = NewManager::getInstance();
   if (!newMgr->begin()) {
     Serial.println("NewManager init failed");
     cleanup();
     return;
   }
   ```

4. **Create FreeRTOS task if async work needed:**
   ```cpp
   xTaskCreatePinnedToCore(
     newManagerTask,
     "NEW_MGR_TASK",
     4096,
     this,
     1,
     &taskHandle,
     0  // Core 0 or 1
   );
   ```

### Adding Support for New Modbus Function Code

**Location:** `ModbusRtuService.cpp` or `ModbusTcpService.cpp`

1. **Add to readRegister method** (~line 230-313 in RTU):
   ```cpp
   if (functionCode == "FC05") {  // Write Single Coil
     uint8_t result = node.writeSingleCoil(address, value);
     if (result == node.ku8MBSuccess) {
       // Process response
     }
   }
   ```

2. **Update register reading logic** in `readRtuDeviceData()`:
   ```cpp
   // Add new case in function code handling
   } else if (fc == "FC05") {
     // Implementation
   ```

3. **Test with real Modbus device or simulator**

### Adding New BLE Command

**Location:** `CRUDHandler.cpp`

1. **Add handler to appropriate map** (~line 77-150):
   ```cpp
   void CRUDHandler::setupHandlers() {
     // For read operations
     readHandlers["new_type"] = [this](JsonObject& input, JsonDocument& response) {
       // Implementation
       response["status"] = "success";
       response["data"] = /* your data */;
     };

     // For create operations
     createHandlers["new_type"] = [this](JsonObject& input, JsonDocument& response) {
       // Implementation
     };
   }
   ```

2. **Handle in processCommand()** - automatic routing via maps

3. **Test via BLE:**
   ```json
   {"op":"read","type":"new_type"}
   {"op":"create","type":"new_type","param1":"value1"}
   ```

### Modifying Network Failover Logic

**Location:** `NetworkManager.cpp:183-240`

1. **Understand current logic:**
   - Checks primary mode every 5 seconds
   - Falls back to secondary if primary down
   - Automatically recovers when primary restored

2. **Modify priority or timing:**
   ```cpp
   static void networkFailoverTask(void* parameter) {
     while (true) {
       // Change delay here (currently 5000ms)
       vTaskDelay(pdMS_TO_TICKS(10000));  // 10 second checks

       // Modify failover conditions
       if (primaryConnected) {
         // Custom logic
       }
     }
   }
   ```

3. **Update client switching** in `updateActiveMode()` method

### Adding New IoT Protocol (e.g., CoAP)

1. **Create `CoAPManager.h` and `CoAPManager.cpp`**

2. **Follow MQTT/HTTP pattern:**
   ```cpp
   class CoAPManager {
   private:
     static CoAPManager* instance;
     Client* activeClient;  // WiFiClient or EthernetClient
     SemaphoreHandle_t taskSemaphore;
   public:
     void begin(QueueManager* queue);
     void updateClient(Client* client);
     void publishQueueData();
   };
   ```

3. **Create FreeRTOS task** that dequeues from QueueManager

4. **Add initialization** in `main.ino`:
   ```cpp
   if (protocol == "COAP") {
     coapManager = CoAPManager::getInstance();
     coapManager->begin(queueManager);
   }
   ```

---

## Testing and Debugging

### Serial Monitor Configuration

**Baud Rate:** 115200
**Line Ending:** Newline (NL)

### Debug Output Interpretation

**Startup Sequence Checklist:**
```
[✓] PSRAM available: XXXXXX bytes
[✓] ConfigManager initialized
[✓] LED Manager initialized
[✓] Queue Manager initialized
[✓] Server Config loaded successfully
[✓] Network Manager initialized
[✓] RTC initialized successfully
[✓] MQTT Manager initialized
[✓] BLE Server started
```

**Common Error Messages:**

| Error | Cause | Solution |
|-------|-------|----------|
| `ConfigManager is null` | PSRAM allocation failed | Check PSRAM availability |
| `Failed to load devices.json` | LittleFS not formatted | Upload filesystem |
| `MQTT connection failed` | Network down or wrong credentials | Check `server_config.json` |
| `Modbus timeout` | Device not responding | Check wiring, slave ID, baud rate |
| `Primary network lost` | Cable unplugged or WiFi down | Normal - automatic failover |

### Memory Debugging

**Check PSRAM usage:**
```cpp
Serial.printf("PSRAM Free: %d bytes\n", ESP.getFreePsram());
Serial.printf("Heap Free: %d bytes\n", ESP.getFreeHeap());
```

**Monitor task stack:**
```cpp
Serial.printf("Stack high water: %d\n", uxTaskGetStackHighWaterMark(taskHandle));
```

### Modbus Debugging

**Enable verbose logging:**
```cpp
// In ModbusRtuService.cpp, uncomment:
// #define DEBUG_MODBUS_RTU
```

**Common Modbus Issues:**
- **Timeout:** Check baud rate, wiring, slave ID
- **CRC Error:** EMI interference, wrong baud rate, cable length
- **Wrong Data:** Endianness mismatch - try different datatype variants

### BLE Testing

**Using nRF Connect App:**
1. Scan for "SURIOTA GW"
2. Connect to service `00001830-0000-1000-8000-00805f9b34fb`
3. Enable notifications on characteristic `11111111-1111-1111-1111-111111111102`
4. Write JSON to characteristic `11111111-1111-1111-1111-111111111101`

**Test Commands:**
```json
// List all devices
{"op":"read","type":"devices"}

// Get device details
{"op":"read","type":"device","device_id":"Da3f41c"}

// Create new device
{"op":"create","type":"device","protocol":"RTU","slave_id":1,...}

// Start streaming device data
{"op":"update","type":"stream","device_id":"Da3f41c"}

// Stop streaming
{"op":"delete","type":"stream"}
```

---

## Working with This Codebase as an AI Assistant

### Understanding Context

**When asked about functionality:**
1. Identify which manager is responsible
2. Check both `.h` (interface) and `.cpp` (implementation)
3. Trace data flow through QueueManager
4. Consider thread safety implications

**When asked to add features:**
1. Determine if new manager needed or extend existing
2. Consider memory implications (PSRAM vs heap)
3. Plan FreeRTOS task structure
4. Identify configuration changes needed

### Code Modification Guidelines

**DO:**
- Follow existing naming conventions strictly
- Use PSRAM for large allocations (>1KB)
- Add comprehensive null checks
- Use Serial.printf for debugging
- Pin tasks to appropriate cores
- Update hot reload mechanisms

**DON'T:**
- Mix Indonesian and English comments (standardize to English)
- Create tasks without cleanup in failure cases
- Forget mutex protection for shared resources
- Ignore memory allocation failures
- Block in critical sections

### Common Patterns to Replicate

**Singleton Implementation:**
```cpp
ClassName* ClassName::instance = nullptr;

ClassName* ClassName::getInstance() {
  if (instance == nullptr) {
    instance = new ClassName();
  }
  return instance;
}
```

**PSRAM Allocation with Fallback:**
```cpp
obj = (ClassName*)heap_caps_malloc(sizeof(ClassName), MALLOC_CAP_SPIRAM);
if (obj != nullptr) {
  new (obj) ClassName();  // Placement new
  Serial.println("Allocated in PSRAM");
} else {
  obj = new ClassName();
  Serial.println("Fallback to internal RAM");
}
```

**FreeRTOS Task Template:**
```cpp
static void taskFunction(void* parameter) {
  ClassName* self = static_cast<ClassName*>(parameter);

  while (self->running) {
    // Do work

    vTaskDelay(pdMS_TO_TICKS(1000));  // Yield to other tasks
  }

  vTaskDelete(NULL);  // Clean exit
}
```

**Queue Operations (Thread-Safe):**
```cpp
bool enqueue(const String& item) {
  if (xSemaphoreTake(mutex, portMAX_DELAY)) {
    if (!isFull()) {
      xQueueSend(queue, &item, 0);
      xSemaphoreGive(mutex);
      return true;
    }
    xSemaphoreGive(mutex);
  }
  return false;
}
```

### File References Format

When referencing code, use: `FileName.cpp:LineNumber`

Example: "The MQTT connection logic is in `MqttManager.cpp:76-137`"

---

## Important Considerations

### Security Concerns

**Current Status:**
- No TLS/SSL for MQTT or HTTP (plaintext transmission)
- No authentication for BLE (anyone can connect)
- No encryption for configuration files

**Recommendations for Production:**
- Enable TLS for MQTT (port 8883)
- Use HTTPS endpoints
- Implement BLE pairing/bonding
- Encrypt sensitive config data in LittleFS

### Performance Characteristics

**Polling Rates:**
- Modbus polling: Per-device `refresh_rate_ms` (default 5000ms)
- Network failover check: 5 seconds
- Queue processing: Continuous (yield on empty)
- BLE streaming: Real-time on data arrival

**Queue Limits:**
- Data queue: 100 items
- Stream queue: 50 items
- Oldest items dropped when full

**Network Reconnection:**
- MQTT: 5-second retry interval
- HTTP: Retry on next queue item
- Modbus: Per-request timeout (configurable)

### Scalability

**Current Limits:**
- ~10 Modbus devices (PSRAM dependent)
- ~100 registers total
- 100 queued messages before data loss
- 2 Modbus RTU buses, 1 TCP connection

**To Scale Up:**
- Increase queue sizes (memory permitting)
- Add more serial ports for RTU
- Implement message persistence (SD card)
- Add queue overflow handling (e.g., drop oldest)

### Known Issues & Workarounds

**Issue:** Mixed language comments (English/Indonesian)
**Workaround:** Standardize to English for new code

**Issue:** ArduinoJson v6/v7 transition artifacts
**Workaround:** Use v7 syntax for all new code (`doc["key"]` not `doc.get()`)

**Issue:** No OTA update mechanism
**Workaround:** Requires USB/serial upload for firmware updates

**Issue:** Watchdog timer disabled
**Workaround:** Uncomment in `main.ino:263` if long-running tasks cause issues

---

## Quick Reference: File Locations

### Configuration & Persistence
- Device/Register CRUD: `ConfigManager.cpp:116-220`
- Server settings: `ServerConfig.cpp:18-109`
- LittleFS initialization: `ConfigManager.cpp:8-9`

### Network & Connectivity
- Failover logic: `NetworkManager.cpp:183-240`
- WiFi connection: `WiFiManager.cpp:60-89`
- Ethernet init: `EthernetManager.cpp:43-93`

### Modbus Communication
- RTU polling task: `ModbusRtuService.cpp:129-226`
- TCP polling task: `ModbusTcpService.cpp:143-255`
- Register processing: `ModbusRtuService.cpp:282-332` (RTU), similar in TCP
- Endianness handling: `ModbusRtuService.cpp:405-466`

### Data Pipeline
- Queue operations: `QueueManager.cpp:91-233`
- Data point creation: `ModbusRtuService.cpp:338-386`
- Timestamp injection: `ModbusRtuService.cpp:340`

### IoT Publishing
- MQTT publish: `MqttManager.cpp:212-248`
- HTTP POST: `HttpManager.cpp:99-195`
- Connection management: `MqttManager.cpp:76-137`, `HttpManager.cpp:51-97`

### BLE Configuration
- GATT server: `BLEManager.cpp:117-189`
- Command processing: `CRUDHandler.cpp:252-329`
- Command handlers: `CRUDHandler.cpp:77-150`
- Streaming task: `BLEManager.cpp:72-115`

### Utilities
- PSRAM allocators: `MemoryManager.h:61-69`
- LED control: `LEDManager.cpp:34-103`
- RTC/NTP sync: `RTCManager.cpp:30-119`

---

## Development Workflow

### Initial Setup
1. Clone repository
2. Install Arduino IDE + ESP32 core
3. Install required libraries (see Library Dependencies section)
4. Configure board: ESP32 Dev Module with PSRAM enabled
5. Upload filesystem (LittleFS) with default configs
6. Compile and upload firmware
7. Monitor serial output for initialization

### Making Changes
1. Identify affected manager(s)
2. Read both `.h` and `.cpp` files
3. Make changes following conventions
4. Update `main.ino` if new manager added
5. Test with serial monitor
6. Test BLE commands if config changed
7. Test Modbus devices if protocol changed

### Testing Checklist
- [ ] Serial output shows successful initialization
- [ ] PSRAM allocated successfully
- [ ] ConfigManager loads devices/registers
- [ ] Network connects (WiFi or Ethernet)
- [ ] Modbus devices respond
- [ ] Data appears in queue
- [ ] MQTT/HTTP publishes successfully
- [ ] BLE connects and accepts commands
- [ ] Failover works when network switches
- [ ] System recovers from errors gracefully

### Git Workflow
- **Working Branch:** `claude/claude-md-mi6730w9dgsx9z6i-01DzMd1FZ6TDy3MPpznQfshk`
- **Commit Messages:** Clear, descriptive (see recent commits for style)
- **Push:** Always use `git push -u origin <branch-name>`
- **Pull Requests:** To main branch after testing

---

## Library Dependencies

**Required Arduino Libraries:**
- ArduinoJson (v7.x)
- PubSubClient (MQTT)
- ModbusMaster
- RTClib (Adafruit)
- Ethernet (W5500)
- ESP32 BLE Arduino
- LittleFS (ESP32 core)

**Install via Arduino Library Manager or PlatformIO**

---

## Additional Resources

**ESP32 Documentation:**
- FreeRTOS API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html
- PSRAM Usage: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html

**Modbus Protocol:**
- Modbus.org specifications
- Function codes reference

**MQTT:**
- MQTT 3.1.1 specification
- PubSubClient library docs

**BLE:**
- Bluetooth Core Specification v4.2+
- GATT specification

---

## Changelog

**Version 1.0 (Current):**
- Initial production release
- Dual Modbus interfaces (RTU/TCP)
- MQTT and HTTP protocols
- BLE configuration interface
- Auto-failover networking
- PSRAM optimization

---

## Contact & Support

For issues, feature requests, or questions:
- Check serial logs for errors
- Review this CLAUDE.md for guidance
- Consult source code comments
- Test with minimal configuration first

---

**Document Version:** 1.0
**Last Updated:** 2025-11-19
**Codebase Version:** Based on commit 78218d9
