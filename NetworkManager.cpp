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

  // 1. Ambil seluruh konfigurasi server ke dalam JsonObject sementara
  DynamicJsonDocument serverConfigDoc(4096);  // Ukuran disesuaikan jika perlu
  JsonObject serverRoot = serverConfigDoc.to<JsonObject>();
  if (!serverConfig->getConfig(serverRoot)) {
    Serial.println("[NetworkMgr] Failed to get full server config");
    // Pertimbangkan penanganan error, misalnya kembali ke default atau return false
  }

  // 2. Tentukan primaryMode (Logika ini bisa tetap, atau disesuaikan jika 'mode' juga pindah)
  // Periksa apakah field 'communication' masih ada dan berisi 'primary_network_mode' atau 'mode'
  String oldMode = "";
  String primaryNetworkMode = "ETH";  // Default jika tidak ditemukan

  if (serverRoot.containsKey("communication")) {
    JsonObject comm = serverRoot["communication"].as<JsonObject>();
    oldMode = comm["mode"] | "";                                // Baca field lama jika ada
    primaryNetworkMode = comm["primary_network_mode"] | "ETH";  // Baca field baru
  }

  if (oldMode == "ETH" || oldMode == "WIFI") {
    primaryMode = oldMode;
    Serial.printf("[NetworkMgr] Using old 'mode' field for primary network: %s\n", primaryMode.c_str());
  } else {
    primaryMode = primaryNetworkMode;  // Gunakan field baru jika field lama tidak valid
    Serial.printf("[NetworkMgr] Using 'primary_network_mode' for primary network: %s\n", primaryMode.c_str());
  }


  // 3. Ambil config WiFi dari ROOT server config
  JsonObject wifiConfig;
  bool wifiConfigPresent = false;
  if (serverRoot.containsKey("wifi")) {
    wifiConfig = serverRoot["wifi"].as<JsonObject>();
    wifiConfigPresent = true;
    Serial.println("[NetworkMgr] Found 'wifi' config at root level.");
  } else {
    Serial.println("[NetworkMgr] 'wifi' config not found at root level.");
  }
  // Ambil status 'enabled' dari objek wifi yang ditemukan
  bool wifiEnabled = wifiConfigPresent && (wifiConfig["enabled"] | false);
  Serial.printf("[NetworkMgr] WiFi Enabled: %s\n", wifiEnabled ? "true" : "false");


  // 4. Ambil config Ethernet dari ROOT server config
  JsonObject ethernetConfig;
  bool ethernetConfigPresent = false;
  if (serverRoot.containsKey("ethernet")) {
    ethernetConfig = serverRoot["ethernet"].as<JsonObject>();
    ethernetConfigPresent = true;
    Serial.println("[NetworkMgr] Found 'ethernet' config at root level.");
  } else {
    Serial.println("[NetworkMgr] 'ethernet' config not found at root level.");
  }
  // Ambil status 'enabled' dari objek ethernet yang ditemukan
  bool ethernetEnabled = ethernetConfigPresent && (ethernetConfig["enabled"] | false);
  Serial.printf("[NetworkMgr] Ethernet Enabled: %s\n", ethernetEnabled ? "true" : "false");


  // Initialize WiFi if enabled (Gunakan wifiConfig dari root)
  if (wifiEnabled) {
    Serial.println("[NetworkMgr] Initializing WiFi...");
    if (!initWiFi(wifiConfig)) {  // initWiFi sekarang menerima JsonObject langsung
      Serial.println("[NetworkMgr] Failed to initialize WiFi");
    }
  }

  // Initialize Ethernet if enabled (Gunakan ethernetConfig dari root)
  if (ethernetEnabled) {
    Serial.println("[NetworkMgr] Initializing Ethernet...");
    bool useDhcp = ethernetConfig["use_dhcp"] | true;
    IPAddress staticIp, gateway, subnet;
    if (!useDhcp) {
      staticIp.fromString(ethernetConfig["static_ip"] | "0.0.0.0");  // Beri default 0.0.0.0 jika kosong
      gateway.fromString(ethernetConfig["gateway"] | "0.0.0.0");
      subnet.fromString(ethernetConfig["subnet"] | "0.0.0.0");
      // Tambahkan validasi IP di sini jika perlu
      if (staticIp.toString() == "0.0.0.0") {
        Serial.println("[NetworkMgr] Warning: Static IP is 0.0.0.0 or invalid.");
      }
    }
    if (!initEthernet(useDhcp, staticIp, gateway, subnet)) {
      Serial.println("[NetworkMgr] Failed to initialize Ethernet");
    }
  }

  // --- Logika penentuan activeMode tetap sama ---
  // Tentukan mode aktif awal berdasarkan primaryMode dan ketersediaan
  if (primaryMode == "ETH" && ethernetManager && ethernetManager->isAvailable()) {
    activeMode = "ETH";
  } else if (primaryMode == "WIFI" && wifiManager && wifiManager->isAvailable()) {
    activeMode = "WIFI";
  } else if (ethernetManager && ethernetManager->isAvailable()) {  // Fallback ke Ethernet jika ada
    activeMode = "ETH";
  } else if (wifiManager && wifiManager->isAvailable()) {  // Fallback ke WiFi jika ada
    activeMode = "WIFI";
  } else {
    activeMode = "NONE";
  }

  if (activeMode != "NONE") {
    networkAvailable = true;
    Serial.printf("[NetworkMgr] Initial active network: %s. IP: %s\n", activeMode.c_str(), getLocalIP().toString().c_str());
  } else {
    networkAvailable = false;
    Serial.println("[NetworkMgr] No network available initially.");
  }

  startFailoverTask();  // Mulai task failover
  return true;          // Kembalikan true jika inisialisasi (walaupun tanpa koneksi) berhasil
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
  if (activeMode == "WIFI" && wifiManager && wifiManager->isAvailable()) {
    return wifiManager->getLocalIP();
  } else if (activeMode == "ETH" && ethernetManager && ethernetManager->isAvailable()) {
    // Tambahkan ini untuk mendapatkan IP Ethernet
    return ethernetManager->getLocalIP();
  }
  return IPAddress(0, 0, 0, 0);
}

String NetworkMgr::getCurrentMode() {
  return activeMode;
}

Client* NetworkMgr::getActiveClient() {
  if (activeMode == "WIFI" && wifiManager && wifiManager->isAvailable()) {
    return &_wifiClient;
  } else if (activeMode == "ETH" && ethernetManager && ethernetManager->isAvailable()) {
    // Kembalikan EthernetClient jika Ethernet aktif
    return &_ethernetClient;
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