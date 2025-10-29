#ifndef ETHERNET_MANAGER_H
#define ETHERNET_MANAGER_H

#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoJson.h>

#define VSPI FSPI      // Define VSPI if not already defined
extern SPIClass SPI3;  // Declare SPI3 as external


class EthernetManager {
private:
  static EthernetManager* instance;
  bool initialized;
  int referenceCount;
  byte mac[6];

  // W5500 pins
  static const int CS_PIN = 48;
  static const int INT_PIN = 9;
  static const int MOSI_PIN = 14;
  static const int MISO_PIN = 21;
  static const int SCK_PIN = 47;

  EthernetManager();
  void generateMacAddress();

public:
  static EthernetManager* getInstance();

  bool init(bool useDhcp = true, IPAddress staticIp = IPAddress(0, 0, 0, 0), IPAddress gateway = IPAddress(0, 0, 0, 0), IPAddress subnet = IPAddress(0, 0, 0, 0));
  void addReference();
  void removeReference();
  void cleanup();

  bool isAvailable();
  IPAddress getLocalIP();
  void getStatus(JsonObject& status);

  ~EthernetManager();
};

#endif