#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "ConfigManager.h"
#include "ServerConfig.h"
#include "QueueManager.h"
#include "NetworkManager.h"
#include <Ethernet.h>

class MqttManager {
private:
  static MqttManager* instance;
  ConfigManager* configManager;
  QueueManager* queueManager;
  ServerConfig* serverConfig;
  NetworkMgr* networkManager;
  PubSubClient mqttClient;

  bool running;
  TaskHandle_t taskHandle;

  String brokerAddress;
  int brokerPort;
  String clientId;
  String username;
  String password;
  String topicPublish;
  unsigned long lastReconnectAttempt;

  MqttManager(ConfigManager* config, ServerConfig* serverCfg, NetworkMgr* netMgr);

  static void mqttTask(void* parameter);
  void mqttLoop();
  bool connectToMqtt();
  void loadMqttConfig();
  void publishQueueData();
  void debugNetworkConnectivity();
  bool isNetworkAvailable();

public:
  static MqttManager* getInstance(ConfigManager* config = nullptr, ServerConfig* serverCfg = nullptr, NetworkMgr* netMgr = nullptr);

  bool init();
  void start();
  void stop();
  void getStatus(JsonObject& status);

  ~MqttManager();
};

#endif