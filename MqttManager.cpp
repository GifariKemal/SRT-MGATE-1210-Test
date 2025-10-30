#include "MqttManager.h"
#include "LEDManager.h"

MqttManager* MqttManager::instance = nullptr;

MqttManager::MqttManager(ConfigManager* config, ServerConfig* serverCfg, NetworkMgr* netMgr)
  : configManager(config), queueManager(nullptr), serverConfig(serverCfg), networkManager(netMgr), mqttClient(PubSubClient()),
    running(false), taskHandle(nullptr), brokerPort(1883), lastReconnectAttempt(0) {
  queueManager = QueueManager::getInstance();
}

MqttManager* MqttManager::getInstance(ConfigManager* config, ServerConfig* serverCfg, NetworkMgr* netMgr) {
  if (instance == nullptr && config != nullptr && serverCfg != nullptr && netMgr != nullptr) {
    instance = new MqttManager(config, serverCfg, netMgr);
  }
  return instance;
}

bool MqttManager::init() {
  Serial.println("Initializing MQTT Manager...");

  if (!configManager || !queueManager || !serverConfig || !networkManager) {
    Serial.println("ConfigManager, QueueManager, ServerConfig, or NetworkManager is null");
    return false;
  }

  loadMqttConfig();
  Serial.println("MQTT Manager initialized successfully");
  return true;
}

void MqttManager::start() {
  Serial.println("Starting MQTT Manager...");

  if (running) {
    return;
  }

  running = true;
  BaseType_t result = xTaskCreatePinnedToCore(
    mqttTask,
    "MQTT_TASK",
    8192,
    this,
    1,
    &taskHandle,
    0);

  if (result == pdPASS) {
    Serial.println("MQTT Manager started successfully");
  } else {
    Serial.println("Failed to create MQTT task");
    running = false;
    taskHandle = nullptr;
  }
}

void MqttManager::stop() {
  running = false;
  if (taskHandle) {
    vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelete(taskHandle);
    taskHandle = nullptr;
  }
  if (mqttClient.connected()) {
    mqttClient.disconnect();
  }
  Serial.println("MQTT Manager stopped");
}

void MqttManager::mqttTask(void* parameter) {
  MqttManager* manager = static_cast<MqttManager*>(parameter);
  manager->mqttLoop();
}

void MqttManager::mqttLoop() {
  bool wasConnected = false;
  bool networkWasAvailable = false; // Mengganti nama dari wifiWasConnected

  Serial.println("[MQTT] Task started");

  while (running) {
    // Check network availability
    bool networkAvailable = isNetworkAvailable();

    if (!networkAvailable) {
      if (networkWasAvailable) { // Mengganti nama
        Serial.println("[MQTT] Network disconnected");
        networkWasAvailable = false; // Mengganti nama
        wasConnected = false;
      }
      Serial.printf("[MQTT] Waiting for network... Mode: %s, IP: %s\n",
                    networkManager->getCurrentMode().c_str(),
                    networkManager->getLocalIP().toString().c_str());

      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    } else if (!networkWasAvailable) { // Mengganti nama
      Serial.printf("[MQTT] Network available - %s IP: %s\n",
                    networkManager->getCurrentMode().c_str(),
                    networkManager->getLocalIP().toString().c_str());
      networkWasAvailable = true; // Mengganti nama
    }

    // Connect to MQTT if not connected
    if (!mqttClient.connected()) {
      if (wasConnected) {
        Serial.println("[MQTT] Connection lost, attempting reconnect...");
        wasConnected = false;
      }

      unsigned long now = millis();
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        static unsigned long lastDebug = 0;
        if (now - lastDebug > 30000) {
          debugNetworkConnectivity();
          lastDebug = now;
        }

        if (connectToMqtt()) {
          Serial.println("[MQTT] Successfully connected to broker");
          wasConnected = true;
        }
      }
    } else {
      if (!wasConnected) {
        Serial.println("[MQTT] Connection active, publishing data...");
        wasConnected = true;
      }
      mqttClient.loop();
      publishQueueData();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool MqttManager::connectToMqtt() {
  Serial.printf("[MQTT] Connecting to broker %s:%d (Client: %s)...\n",
                brokerAddress.c_str(), brokerPort, clientId.c_str());

  // Check network connectivity first
  IPAddress localIP = networkManager->getLocalIP();
  String networkMode = networkManager->getCurrentMode();
  Serial.printf("[MQTT] Network Mode: %s\n", networkMode.c_str());
  Serial.printf("[MQTT] Local IP: %s\n", localIP.toString().c_str());

  Client* activeClient = networkManager->getActiveClient();
  if (!activeClient) {
    Serial.println("[MQTT] No active network client available.");
    return false;
  }

  mqttClient.setClient(*activeClient);

  // Set buffer sizes and timeouts
  mqttClient.setBufferSize(512, 512);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(5);

  mqttClient.setServer(brokerAddress.c_str(), brokerPort);

  bool connected;
  if (username.length() > 0 && password.length() > 0) {
    Serial.println("[MQTT] Using authentication");
    connected = mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str());
  } else {
    Serial.println("[MQTT] No authentication");
    connected = mqttClient.connect(clientId.c_str());
  }

  if (connected) {
    Serial.printf("[MQTT] Connected to %s:%d\n", brokerAddress.c_str(), brokerPort);
  } else {
    Serial.printf("[MQTT] Connection failed, error: %d\n", mqttClient.state());
  }

  return connected;
}

void MqttManager::loadMqttConfig() {
  // --- PERUBAHAN DI SINI ---
  StaticJsonDocument<1024> configDoc; // Mengganti DynamicJsonDocument(1024)
  // --- AKHIR PERUBAHAN ---
  JsonObject mqttConfig = configDoc.to<JsonObject>();

  Serial.println("[MQTT] Loading MQTT configuration...");

  if (serverConfig->getMqttConfig(mqttConfig)) {
    brokerAddress = mqttConfig["broker_address"] | "broker.hivemq.com";
    brokerPort = mqttConfig["broker_port"] | 1883;
    clientId = mqttConfig["client_id"] | "esp32_gateway";
    username = mqttConfig["username"] | "";
    password = mqttConfig["password"] | "";
    topicPublish = mqttConfig["topic_publish"] | "device/data";

    Serial.printf("[MQTT] Config loaded - Broker: %s:%d, Client: %s, Topic: %s\n",
                  brokerAddress.c_str(), brokerPort, clientId.c_str(), topicPublish.c_str());
    Serial.printf("[MQTT] Auth: %s\n", (username.length() > 0) ? "YES" : "NO");
  } else {
    Serial.println("[MQTT] Failed to load config, using public test broker");
    brokerAddress = "broker.hivemq.com";
    brokerPort = 1883;
    clientId = "esp32_gateway_" + String(random(1000, 9999));
    topicPublish = "device/data";
    Serial.printf("[MQTT] Default config - Broker: %s:%d, Client: %s\n",
                  brokerAddress.c_str(), brokerPort, clientId.c_str());
  }
}

void MqttManager::publishQueueData() {
  if (!mqttClient.connected()) {
    return;
  }

  // Process up to 10 items per loop to avoid blocking
  for (int i = 0; i < 10; i++) {
    // --- PERUBAHAN DI SINI ---
    StaticJsonDocument<512> dataDoc; // Mengganti DynamicJsonDocument(512)
    // --- AKHIR PERUBAHAN ---
    JsonObject dataPoint = dataDoc.to<JsonObject>();

    if (!queueManager->dequeue(dataPoint)) {
      break;  // No more data in queue
    }

    // Create MQTT payload
    String payload;
    serializeJson(dataPoint, payload);

    // Publish to MQTT
    String topic = topicPublish;

    if (mqttClient.publish(topic.c_str(), payload.c_str())) {
      Serial.printf("[MQTT] Published: %s\n", topic.c_str());
      if (ledManager) {
        ledManager->notifySuccess();
      }
    } else {
      Serial.printf("[MQTT] Publish failed: %s\n", topic.c_str());
      queueManager->enqueue(dataPoint);
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(10));  // Small delay between publishes
  }
}

bool MqttManager::isNetworkAvailable() {
  if (!networkManager) return false;

  // Check if network manager says available
  if (!networkManager->isAvailable()) {
    return false;
  }

  // Check actual IP from network manager
  IPAddress localIP = networkManager->getLocalIP();
  if (localIP == IPAddress(0, 0, 0, 0)) {
    Serial.printf("[MQTT] Network manager available but no IP (%s)\n", networkManager->getCurrentMode().c_str());
    return false;
  }

  return true;
}

void MqttManager::debugNetworkConnectivity() {
  Serial.println("[MQTT] === Network Debug ===");
  Serial.printf("[MQTT] Current Mode: %s\n", networkManager->getCurrentMode().c_str());
  Serial.printf("[MQTT] Network Available: %s\n", networkManager->isAvailable() ? "YES" : "NO");
  Serial.printf("[MQTT] Local IP: %s\n", networkManager->getLocalIP().toString().c_str());

  // Show specific network details
  String mode = networkManager->getCurrentMode();
  if (mode == "WIFI") {
    Serial.printf("[MQTT] WiFi Status: %d\n", WiFi.status());
    Serial.printf("[MQTT] WiFi SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("[MQTT] WiFi RSSI: %d dBm\n", WiFi.RSSI());
  } else if (mode == "ETH") {
    Serial.println("[MQTT] Using Ethernet connection");
  }
}

void MqttManager::getStatus(JsonObject& status) {
  status["running"] = running;
  status["service_type"] = "mqtt_manager";
  status["mqtt_connected"] = mqttClient.connected();
  status["network_available"] = isNetworkAvailable(); // Lebih akurat daripada wifi_connected
  status["broker_address"] = brokerAddress;
  status["broker_port"] = brokerPort;
  status["client_id"] = clientId;
  status["topic_publish"] = topicPublish;
  status["queue_size"] = queueManager->size();
}

MqttManager::~MqttManager() {
  stop();
}