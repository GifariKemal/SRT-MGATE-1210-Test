#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <ArduinoJson.h>

class WiFiManager {
private:
  static WiFiManager* instance;
  bool initialized;
  int referenceCount;
  String ssid;
  String password;

  WiFiManager();

public:
  static WiFiManager* getInstance();

  bool init(const String& ssid, const String& password);
  void addReference();
  void removeReference();
  void cleanup();

  bool isAvailable();
  IPAddress getLocalIP();
  String getSSID();
  void getStatus(JsonObject& status);

  ~WiFiManager();
};

#endif