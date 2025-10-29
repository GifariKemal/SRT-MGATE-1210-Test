#include "LEDManager.h"

LEDManager* LEDManager::instance = nullptr;

// Private constructor
LEDManager::LEDManager()
  : ledTaskHandle(nullptr), blinkingActive(false),
    lastBlinkMillis(0), blinkInterval(100), ledState(LOW) {  // Default blink interval 100ms
}

// Singleton getInstance method
LEDManager* LEDManager::getInstance() {
  if (instance == nullptr) {
    instance = new LEDManager();
  }
  return instance;
}

// Initialize the LED pin and start the task
void LEDManager::begin() {
  pinMode(LED_NET, OUTPUT);
  digitalWrite(LED_NET, LOW);  // Ensure LED is off initially

  // Create LED blinking task
  xTaskCreatePinnedToCore(
    ledBlinkTask,
    "LED_Blink_Task",
    1024,  // Stack size
    this,
    0,  // Priority (lowest)
    &ledTaskHandle,
    APP_CPU_NUM  // Pin to APP_CPU_NUM (core 1 on ESP32)
  );
  Serial.println("LED Manager initialized and task started.");
}

// Notify success (e.g., data sent). This will trigger a short blink sequence.
void LEDManager::notifySuccess() {
  // Trigger a short blink sequence
  blinkingActive = true;
  lastBlinkMillis = millis();
  ledState = HIGH;  // Turn on LED immediately
  digitalWrite(LED_NET, ledState);
  // The ledLoop will handle turning it off and subsequent blinks
}

// Stop the LED task (if needed)
void LEDManager::stop() {
  if (ledTaskHandle) {
    vTaskDelete(ledTaskHandle);
    ledTaskHandle = nullptr;
    digitalWrite(LED_NET, LOW);  // Ensure LED is off
    Serial.println("LED Manager task stopped.");
  }
}

// FreeRTOS task function
void LEDManager::ledBlinkTask(void* parameter) {
  LEDManager* manager = static_cast<LEDManager*>(parameter);
  manager->ledLoop();
}

// Main loop for the LED task
void LEDManager::ledLoop() {
  while (true) {
    if (blinkingActive) {
      if (millis() - lastBlinkMillis >= blinkInterval) {
        lastBlinkMillis = millis();
        ledState = !ledState;  // Toggle LED state
        digitalWrite(LED_NET, ledState);

        // After a few blinks, turn off and stop blinking
        // For simplicity, let's say 2 blinks (4 state changes: ON-OFF-ON-OFF)
        // This means 4 * blinkInterval duration
        if (millis() - lastBlinkMillis > (blinkInterval * 4)) {  // Check if sequence is over
          blinkingActive = false;
          digitalWrite(LED_NET, LOW);  // Ensure LED is off
        }
      }
    } else {
      // Ensure LED is off when not blinking
      if (ledState == HIGH) {
        digitalWrite(LED_NET, LOW);
        ledState = LOW;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));  // Small delay to yield to other tasks
  }
}
