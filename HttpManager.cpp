#include "HttpManager.h"
#include "LEDManager.h"

HttpManager* HttpManager::instance = nullptr;

HttpManager::HttpManager(ConfigManager* config, ServerConfig* serverCfg, NetworkMgr* netMgr)
  : configManager(config), queueManager(nullptr), serverConfig(serverCfg), networkManager(netMgr),
    running(false), taskHandle(nullptr), timeout(10000), retryCount(3), lastSendAttempt(0) {
  queueManager = QueueManager::getInstance();
}

HttpManager* HttpManager::getInstance(ConfigManager* config, ServerConfig* serverCfg, NetworkMgr* netMgr) {
  if (instance == nullptr && config != nullptr && serverCfg != nullptr && netMgr != nullptr) {
    instance = new HttpManager(config, serverCfg, netMgr);
  }
  return instance;
}

bool HttpManager::init() {
  Serial.println("Initializing HTTP Manager...");

  if (!configManager || !queueManager || !serverConfig || !networkManager) {
    Serial.println("ConfigManager, QueueManager, ServerConfig, or NetworkManager is null");
    return false;
  }

  loadHttpConfig();
  Serial.println("HTTP Manager initialized successfully");
  return true;
}

void HttpManager::start() {
  Serial.println("Starting HTTP Manager...");

  if (running) {
    return;
  }

  running = true;
  BaseType_t result = xTaskCreatePinnedToCore(
    httpTask,
    "HTTP_TASK",
    8192,
    this,
    1,
    &taskHandle,
    0);

  if (result == pdPASS) {
    Serial.println("HTTP Manager started successfully");
  } else {
    Serial.println("Failed to create HTTP task");
    running = false;
    taskHandle = nullptr;
  }
}

void HttpManager::stop() {
  running = false;
  if (taskHandle) {
    vTaskDelay(pdMS_TO_TICKS(100));
    vTaskDelete(taskHandle);
    taskHandle = nullptr;
  }
  Serial.println("HTTP Manager stopped");
}

void HttpManager::httpTask(void* parameter) {
  HttpManager* manager = static_cast<HttpManager*>(parameter);
  manager->httpLoop();
}

void HttpManager::httpLoop() {
  bool networkWasAvailable = false;

  Serial.println("[HTTP] Task started");

  while (running) {
    // Check network availability
    bool networkAvailable = isNetworkAvailable();

    if (!networkAvailable) {
      if (networkWasAvailable) {
        Serial.println("[HTTP] Network disconnected");
        networkWasAvailable = false;
      }
      Serial.printf("[HTTP] Waiting for network... Mode: %s, IP: %s\n",
                    networkManager->getCurrentMode().c_str(),
                    networkManager->getLocalIP().toString().c_str());

      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    } else if (!networkWasAvailable) {
      Serial.printf("[HTTP] Network available - %s IP: %s\n",
                    networkManager->getCurrentMode().c_str(),
                    networkManager->getLocalIP().toString().c_str());
      networkWasAvailable = true;
    }

    // Process queue data
    publishQueueData();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool HttpManager::sendHttpRequest(const JsonObject& data) {
  if (endpointUrl.isEmpty()) {
    Serial.println("[HTTP] No endpoint URL configured");
    return false;
  }

  Serial.printf("[HTTP] Sending request to %s\n", endpointUrl.c_str());

  Client* activeClient = networkManager->getActiveClient();
  if (!activeClient) {
    Serial.println("[HTTP] No active network client available.");
    return false;
  }

  HTTPClient httpClient;          // Create local instance
  httpClient.begin(endpointUrl);  // Let HTTPClient manage the client internally
  httpClient.setTimeout(timeout);

  // Set headers from configuration
  // --- PERUBAHAN DI SINI ---
  StaticJsonDocument<1024> configDoc; // Mengganti DynamicJsonDocument(1024)
  // --- AKHIR PERUBAHAN ---
  JsonObject httpConfig = configDoc.to<JsonObject>();

  if (serverConfig->getHttpConfig(httpConfig)) {
    JsonObject configHeaders = httpConfig["headers"];
    for (JsonPair header : configHeaders) {
      httpClient.addHeader(header.key().c_str(), header.value().as<String>());
      Serial.printf("[HTTP] Header: %s = %s\n", header.key().c_str(), header.value().as<String>().c_str());
    }
  }

  // Default headers
  httpClient.addHeader("Content-Type", "application/json");

  // Prepare payload
  String payload;
  serializeJson(data, payload);

  int httpResponseCode = -1;
  int attempts = 0;

  while (attempts < retryCount && httpResponseCode < 0) {
    attempts++;

    if (method == "POST") {
      httpResponseCode = httpClient.POST(payload);
    } else if (method == "PUT") {
      httpResponseCode = httpClient.PUT(payload);
    } else if (method == "PATCH") {
      httpResponseCode = httpClient.PATCH(payload);
    } else {
      Serial.printf("[HTTP] Unsupported method: %s\n", method.c_str());
      httpClient.end();
      return false;
    }

    if (httpResponseCode > 0) {
      Serial.printf("[HTTP] Response code: %d\n", httpResponseCode);

      if (httpResponseCode >= 200 && httpResponseCode < 300) {
        String response = httpClient.getString();
        Serial.printf("[HTTP] Success: %s\n", response.c_str());
        httpClient.end();
        if (ledManager) {
          ledManager->notifySuccess();
        }
        return true;
      } else {
        String response = httpClient.getString();
        Serial.printf("[HTTP] Error response: %s\n", response.c_str());
      }
    } else {
      Serial.printf("[HTTP] Request failed, error: %s\n", httpClient.errorToString(httpResponseCode).c_str());
    }

    if (attempts < retryCount) {
      Serial.printf("[HTTP] Retrying in 2 seconds... (attempt %d/%d)\n", attempts + 1, retryCount);
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }

  httpClient.end();
  return false;
}

void HttpManager::loadHttpConfig() {
  // --- PERUBAHAN DI SINI ---
  StaticJsonDocument<1024> configDoc; // Mengganti DynamicJsonDocument(1024)
  // --- AKHIR PERUBAHAN ---
  JsonObject httpConfig = configDoc.to<JsonObject>();

  Serial.println("[HTTP] Loading HTTP configuration...");

  if (serverConfig->getHttpConfig(httpConfig)) {
    bool enabled = httpConfig["enabled"] | false;
    if (!enabled) {
      Serial.println("[HTTP] HTTP config disabled, clearing endpoint");
      endpointUrl = "";
      return;
    }

    endpointUrl = httpConfig["endpoint_url"] | "";
    method = httpConfig["method"] | "POST";
    bodyFormat = httpConfig["body_format"] | "json";
    timeout = httpConfig["timeout"] | 10000;
    retryCount = httpConfig["retry"] | 3;

    Serial.printf("[HTTP] Config loaded - URL: %s, Method: %s, Timeout: %d, Retry: %d\n",
                  endpointUrl.c_str(), method.c_str(), timeout, retryCount);
  } else {
    Serial.println("[HTTP] Failed to load HTTP config");
    endpointUrl = "";
    method = "POST";
    timeout = 10000;
    retryCount = 3;
  }
}

void HttpManager::publishQueueData() {
  if (endpointUrl.isEmpty()) {
    return;
  }

  // Check if enough time has passed since last attempt
  unsigned long now = millis();
  if (now - lastSendAttempt < 1000) {
    return;
  }
  lastSendAttempt = now;

  // Process up to 5 items per loop to avoid blocking
  for (int i = 0; i < 5; i++) {
    // --- PERUBAHAN DI SINI ---
    StaticJsonDocument<512> dataDoc; // Mengganti DynamicJsonDocument(512)
    // --- AKHIR PERUBAHAN ---
    JsonObject dataPoint = dataDoc.to<JsonObject>();

    if (!queueManager->dequeue(dataPoint)) {
      break;  // No more data in queue
    }

    // Send HTTP request
    if (sendHttpRequest(dataPoint)) {
      Serial.printf("[HTTP] Data sent successfully\n");
    } else {
      Serial.printf("[HTTP] Failed to send data, re-queuing\n");
      queueManager->enqueue(dataPoint);
      break;
    }

    vTaskDelay(pdMS_TO_TICKS(100));  // Small delay between requests
  }
}

bool HttpManager::isNetworkAvailable() {
  if (!networkManager) return false;

  // Check if network manager says available
  if (!networkManager->isAvailable()) {
    return false;
  }

  // Check actual IP from network manager
  IPAddress localIP = networkManager->getLocalIP();
  if (localIP == IPAddress(0, 0, 0, 0)) {
    Serial.printf("[HTTP] Network manager available but no IP (%s)\n", networkManager->getCurrentMode().c_str());
    return false;
  }

  return true;
}

void HttpManager::debugNetworkConnectivity() {
  Serial.println("[HTTP] === Network Debug ===");
  Serial.printf("[HTTP] Current Mode: %s\n", networkManager->getCurrentMode().c_str());
  Serial.printf("[HTTP] Network Available: %s\n", networkManager->isAvailable() ? "YES" : "NO");
  Serial.printf("[HTTP] Local IP: %s\n", networkManager->getLocalIP().toString().c_str());

  // Show specific network details
  String mode = networkManager->getCurrentMode();
  if (mode == "WIFI") {
    Serial.printf("[HTTP] WiFi Status: %d\n", WiFi.status());
    Serial.printf("[HTTP] WiFi SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("[HTTP] WiFi RSSI: %d dBm\n", WiFi.RSSI());
  } else if (mode == "ETH") {
    Serial.println("[HTTP] Using Ethernet connection");
  }
}

void HttpManager::getStatus(JsonObject& status) {
  status["running"] = running;
  status["service_type"] = "http_manager";
  status["network_available"] = isNetworkAvailable();
  status["endpoint_url"] = endpointUrl;
  status["method"] = method;
  status["timeout"] = timeout;
  status["retry_count"] = retryCount;
  status["queue_size"] = queueManager->size();
}

HttpManager::~HttpManager() {
  stop();
}