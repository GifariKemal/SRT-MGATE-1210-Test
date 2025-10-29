#include "NetworkManager.h"
#include "ServerConfig.h"  // Ensure ServerConfig is included here

NetworkMgr* NetworkMgr::instance = nullptr;

NetworkMgr* NetworkMgr::getInstance() {
  if (!instance) {
    instance = new NetworkMgr();
  }
  return instance;
}

NetworkMgr::NetworkMgr()
  : wifiManager(nullptr), ethernetManager(nullptr), primaryMode(""), activeMode(""), networkAvailable(false), failoverTaskHandle(nullptr) {}

bool NetworkMgr::init(ServerConfig* serverConfig) {
  if (!serverConfig) {
    Serial.println("ServerConfig is null");
    return false;
  }

  // Get primary network mode preference
  String oldMode = serverConfig->getCommunicationMode();
  if (oldMode == "ETH" || oldMode == "WIFI") {
    primaryMode = oldMode;
    Serial.printf("Using old 'mode' field for primary network: %s\n", primaryMode.c_str());
  } else {
    primaryMode = serverConfig->getPrimaryNetworkMode();
    Serial.printf("Using 'primary_network_mode' for primary network: %s\n", primaryMode.c_str());
  }

  // Get WiFi config
  DynamicJsonDocument wifiDoc(256);
  JsonObject wifiConfig = wifiDoc.to<JsonObject>();
  bool wifiConfigPresent = serverConfig->getWifiConfig(wifiConfig);
  bool wifiEnabled = wifiConfigPresent && (wifiConfig["enabled"] | false);

  // Get Ethernet config
  DynamicJsonDocument ethDoc(256);
  JsonObject ethernetConfig = ethDoc.to<JsonObject>();
  bool ethernetConfigPresent = serverConfig->getEthernetConfig(ethernetConfig);
  bool ethernetEnabled = ethernetConfigPresent && (ethernetConfig["enabled"] | false);

  // Initialize WiFi if enabled
  if (wifiEnabled) {
    Serial.println("Initializing WiFi...");
    if (!initWiFi(wifiConfig)) {
      Serial.println("Failed to initialize WiFi");
    }
  }

  // Initialize Ethernet if enabled
  if (ethernetEnabled) {
    Serial.println("Initializing Ethernet...");
    // Extract static IP config if not using DHCP
    bool useDhcp = ethernetConfig["use_dhcp"] | true;
    IPAddress staticIp, gateway, subnet;
    if (!useDhcp) {
      staticIp.fromString(ethernetConfig["static_ip"] | "192.168.1.177");
      gateway.fromString(ethernetConfig["gateway"] | "192.168.1.1");
      subnet.fromString(ethernetConfig["subnet"] | "255.255.255.0");
    }
    if (!initEthernet(useDhcp, staticIp, gateway, subnet)) {
      Serial.println("Failed to initialize Ethernet");
    }
  }

  // Determine initial active mode
  if (primaryMode == "ETH" && ethernetManager && ethernetManager->isAvailable()) {
    activeMode = "ETH";
  } else if (primaryMode == "WIFI" && wifiManager && wifiManager->isAvailable()) {
    activeMode = "WIFI";
  } else if (ethernetManager && ethernetManager->isAvailable()) {
    activeMode = "ETH";
  } else if (wifiManager && wifiManager->isAvailable()) {
    activeMode = "WIFI";
  } else {
    activeMode = "NONE";
  }

  if (activeMode != "NONE") {
    networkAvailable = true;
    Serial.printf("Initial active network: %s. IP: %s\n", activeMode.c_str(), getLocalIP().toString().c_str());
  } else {
    networkAvailable = false;
    Serial.println("No network available initially.");
  }

  startFailoverTask();
  return true;
}

bool NetworkMgr::initWiFi(const JsonObject& wifiConfig) {
  String ssid = wifiConfig["ssid"] | "";
  String password = wifiConfig["password"] | "";

  if (ssid.isEmpty()) {
    Serial.println("WiFi SSID not provided");
    return false;
  }

  wifiManager = WiFiManager::getInstance();
  if (wifiManager->init(ssid, password)) {
    Serial.printf("Network initialized: WiFi (%s)\n", ssid.c_str());
    return true;
  }

  return false;
}

bool NetworkMgr::initEthernet(bool useDhcp, IPAddress staticIp, IPAddress gateway, IPAddress subnet) {
  ethernetManager = EthernetManager::getInstance();
  if (ethernetManager->init(useDhcp, staticIp, gateway, subnet)) {
    Serial.println("Network initialized: Ethernet");
    return true;
  }

  return false;
}

void NetworkMgr::startFailoverTask() {
  if (failoverTaskHandle == nullptr) {
    xTaskCreatePinnedToCore(
      failoverTask,
      "NET_FAILOVER_TASK",
      4096,  // Stack size
      this,
      1,  // Priority
      &failoverTaskHandle,
      1  // Core 1
    );
    Serial.println("Network failover task started.");
  }
}

void NetworkMgr::failoverTask(void* parameter) {
  NetworkMgr* manager = static_cast<NetworkMgr*>(parameter);
  manager->failoverLoop();
}

void NetworkMgr::failoverLoop() {
  unsigned long lastCheck = 0;
  const unsigned long checkInterval = 5000;  // Check every 5 seconds

  while (true) {
    unsigned long now = millis();
    if (now - lastCheck >= checkInterval) {
      lastCheck = now;

      bool primaryAvailable = false;
      bool secondaryAvailable = false;

      // Check primary mode availability
      if (primaryMode == "ETH" && ethernetManager) {
        primaryAvailable = ethernetManager->isAvailable();
      } else if (primaryMode == "WIFI" && wifiManager) {
        primaryAvailable = wifiManager->isAvailable();
      }

      // Check secondary mode availability
      if (primaryMode == "ETH" && wifiManager) {
        secondaryAvailable = wifiManager->isAvailable();
      } else if (primaryMode == "WIFI" && ethernetManager) {
        secondaryAvailable = ethernetManager->isAvailable();
      }

      // Logic for switching
      if (activeMode == "NONE") {
        if (primaryAvailable) {
          switchMode(primaryMode);
        } else if (secondaryAvailable) {
          switchMode((primaryMode == "ETH") ? "WIFI" : "ETH");
        }
      } else if (activeMode == primaryMode) {
        if (!primaryAvailable) {
          Serial.printf("Primary network (%s) lost. Attempting to switch to secondary.\n", primaryMode.c_str());
          if (secondaryAvailable) {
            switchMode((primaryMode == "ETH") ? "WIFI" : "ETH");
          } else {
            switchMode("NONE");  // Both down
          }
        }
      } else {  // activeMode is secondary
        if (!secondaryAvailable) {
          Serial.printf("Secondary network (%s) lost. Attempting to switch to primary.\n", activeMode.c_str());
          if (primaryAvailable) {
            switchMode(primaryMode);
          }
        } else if (primaryAvailable) {
          // Primary is back, switch back to primary
          Serial.printf("Primary network (%s) restored. Switching back.\n", primaryMode.c_str());
          switchMode(primaryMode);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));  // Small delay to yield
  }
}

void NetworkMgr::switchMode(const String& newMode) {
  if (newMode == activeMode) return;  // No change needed

  Serial.printf("Switching network mode from %s to %s\n", activeMode.c_str(), newMode.c_str());

  // Cleanup old active mode if any
  if (activeMode == "WIFI" && wifiManager) {
    wifiManager->removeReference();  // Decrement reference count
  } else if (activeMode == "ETH" && ethernetManager) {
    ethernetManager->removeReference();  // Decrement reference count
  }

  // Set new active mode
  activeMode = newMode;
  if (activeMode == "WIFI" && wifiManager) {
    wifiManager->addReference();  // Increment reference count
    networkAvailable = true;
  } else if (activeMode == "ETH" && ethernetManager) {
    ethernetManager->addReference();  // Increment reference count
    networkAvailable = true;
  } else {
    networkAvailable = false;
  }

  if (networkAvailable) {
    Serial.printf("Successfully switched to %s. IP: %s\n", activeMode.c_str(), getLocalIP().toString().c_str());
  } else {
    Serial.println("No network active.");
  }
}

bool NetworkMgr::isAvailable() {
  if (activeMode == "WIFI" && wifiManager) {
    return wifiManager->isAvailable();
  } else if (activeMode == "ETH" && ethernetManager) {
    return ethernetManager->isAvailable();
  }
  return false;
}

IPAddress NetworkMgr::getLocalIP() {
  if (activeMode == "WIFI" && wifiManager) {
    return wifiManager->getLocalIP();
  }
  return IPAddress(0, 0, 0, 0);
}

String NetworkMgr::getCurrentMode() {
  return activeMode;
}

Client* NetworkMgr::getActiveClient() {
  if (activeMode == "WIFI") {
    return &_wifiClient;
  }
  return nullptr;
}

void NetworkMgr::cleanup() {
  if (failoverTaskHandle) {
    vTaskDelete(failoverTaskHandle);
    failoverTaskHandle = nullptr;
  }
  if (wifiManager) {
    wifiManager->cleanup();
  }
  if (ethernetManager) {
    ethernetManager->cleanup();
  }
  networkAvailable = false;
  activeMode = "NONE";
}

NetworkMgr::~NetworkMgr() {
  cleanup();
}