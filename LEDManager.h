#ifndef LED_MANAGER_H
#define LED_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Define the LED pin
#define LED_NET 8

class LEDManager {
private:
  static LEDManager* instance;
  TaskHandle_t ledTaskHandle;

  bool blinkingActive;            // Flag to control blinking
  unsigned long lastBlinkMillis;  // Last time LED state was changed
  unsigned long blinkInterval;    // Interval for blinking (e.g., 500ms for 0.5s on/off cycle)
  int ledState;                   // Current state of the LED (HIGH/LOW)

  // Private constructor for Singleton pattern
  LEDManager();

  // FreeRTOS task function
  static void ledBlinkTask(void* parameter);
  void ledLoop();

public:
  // Singleton getInstance method
  static LEDManager* getInstance();

  // Initialize the LED pin and start the task
  void begin();

  // Notify success (e.g., data sent). This will trigger a short blink sequence.
  void notifySuccess();

  // Stop the LED task (if needed)
  void stop();
};

extern LEDManager* ledManager;  // Declare global instance

#endif  // LED_MANAGER_H
