#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <ArduinoJson.h>
#include "WiFiManager.h"
#include "EthernetManager.h"
#include "ServerConfig.h"  // Added for ServerConfig declaration

class NetworkMgr {
private:
  static NetworkMgr* instance;
  WiFiManager* wifiManager;
  EthernetManager* ethernetManager;

  String primaryMode;     // Configured primary mode (e.g., "ETH", "WIFI")
  String activeMode;      // Currently active mode
  bool networkAvailable;  // Overall network availability

  TaskHandle_t failoverTaskHandle;
  static void failoverTask(void* parameter);
  void failoverLoop();

  WiFiClient _wifiClient;          // Internal WiFiClient for MQTT/HTTP
  EthernetClient _ethernetClient;  // Internal EthernetClient for MQTT/HTTP

  NetworkMgr();
  bool initWiFi(const JsonObject& wifiConfig);
  bool initEthernet(bool useDhcp, IPAddress staticIp, IPAddress gateway, IPAddress subnet);
  void startFailoverTask();
  void switchMode(const String& newMode);

public:
  static NetworkMgr* getInstance();

  bool init(ServerConfig* serverConfig);  // Changed parameter type
  bool isAvailable();
  IPAddress getLocalIP();
  String getCurrentMode();
  Client* getActiveClient();  // New method to get active client
  void cleanup();
  void getStatus(JsonObject& status);

  ~NetworkMgr();
};

#endif