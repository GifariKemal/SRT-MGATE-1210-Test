#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <time.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class RTCManager {
private:
  static RTCManager* instance;
  RTC_DS3231 rtc;
  bool initialized;
  bool syncRunning;
  TaskHandle_t syncTaskHandle;
  unsigned long lastNtpSync;

  // NTP settings
  const char* ntpServer = "pool.ntp.org";
  const long gmtOffset_sec = 7 * 3600;
  const int daylightOffset_sec = 0;

  RTCManager();

  static void timeSyncTask(void* parameter);
  void timeSyncLoop();
  bool syncWithNTP();
  void updateSystemTime(DateTime rtcTime);

public:
  static RTCManager* getInstance();

  bool init();
  void startSync();
  void stopSync();

  DateTime getCurrentTime();
  bool setTime(DateTime newTime);
  bool forceNtpSync();
  void getStatus(JsonObject& status);

  ~RTCManager();
};

#endif