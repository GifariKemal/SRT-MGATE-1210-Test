#include "WiFiManager.h"

WiFiManager* WiFiManager::instance = nullptr;

WiFiManager::WiFiManager()
  : initialized(false), referenceCount(0) {}

WiFiManager* WiFiManager::getInstance() {
  if (!instance) {
    instance = new WiFiManager();
  }
  return instance;
}

bool WiFiManager::init(const String& ssidParam, const String& passwordParam) {
  if (initialized) {
    referenceCount++;
    Serial.printf("WiFi already initialized (refs: %d)\n", referenceCount);
    return true;
  }

  ssid = ssidParam;
  password = passwordParam;

  // Check if already connected to same network
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
    initialized = true;
    referenceCount = 1;
    Serial.printf("WiFi already connected to: %s\n", ssid.c_str());
    return true;
  }

  // Disconnect if connected to different network
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect();
    delay(1000);
  }

  Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());

  // Wait for connection with timeout
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    initialized = true;
    referenceCount = 1;
    Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  } else {
    Serial.println("\nWiFi connection failed");
    return false;
  }
}

void WiFiManager::addReference() {
  if (initialized) {
    referenceCount++;
    Serial.printf("WiFi reference added (refs: %d)\n", referenceCount);
  }
}

void WiFiManager::removeReference() {
  if (referenceCount > 0) {
    referenceCount--;
    Serial.printf("WiFi reference removed (refs: %d)\n", referenceCount);

    if (referenceCount == 0) {
      cleanup();
    }
  }
}

void WiFiManager::cleanup() {
  if (initialized) {
    WiFi.disconnect();
    initialized = false;
  }
  referenceCount = 0;
  Serial.println("WiFi resources cleaned up");
}

bool WiFiManager::isAvailable() {
  return initialized && WiFi.status() == WL_CONNECTED;
}

IPAddress WiFiManager::getLocalIP() {
  if (initialized) {
    return WiFi.localIP();
  }
  return IPAddress(0, 0, 0, 0);
}

String WiFiManager::getSSID() {
  return ssid;
}

void WiFiManager::getStatus(JsonObject& status) {
  status["initialized"] = initialized;
  status["available"] = isAvailable();
  status["reference_count"] = referenceCount;
  status["ssid"] = ssid;

  if (initialized) {
    status["ip_address"] = getLocalIP().toString();
    status["rssi"] = WiFi.RSSI();

    String statusStr;
    switch (WiFi.status()) {
      case WL_CONNECTED: statusStr = "connected"; break;
      case WL_DISCONNECTED: statusStr = "disconnected"; break;
      case WL_CONNECTION_LOST: statusStr = "connection_lost"; break;
      case WL_CONNECT_FAILED: statusStr = "connect_failed"; break;
      default: statusStr = "unknown"; break;
    }
    status["connection_status"] = statusStr;
  }
}

WiFiManager::~WiFiManager() {
  cleanup();
}