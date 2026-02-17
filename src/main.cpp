#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include <LittleFS.h>
#include <ESPmDNS.h>

#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include "ConfigManager.h"
#include "SegmentController.h"

// Global config manager instance
ConfigManager configManager;

// External functions from other modules
void setupLittleFS();
void setupWiFi();
void setupMDNS();
void setupWebServer();
void setupSegmentController();
void setupTimerController();

// External variables (declared elsewhere)
extern bool timerStopped;
extern AsyncWebServer server;
extern WiFiManager wm;

// External update functions
extern void updateTimer();          // from SegmentController.cpp
extern void updateTimerController(); // from TimerController.cpp

/**
 * Arduino setup – runs once at startup.
 */
void setup() {
    Serial.begin(115200);
    delay(500);

    // Initialize I2C for PCF8575 and DS3231
    Wire.begin(8, 9);
    Wire.setClock(400000);

    // Mount LittleFS for web files
    setupLittleFS();
    setupWiFi();                     // WiFiManager portal if needed

    // Load configuration from NVS
    configManager.begin();
    configManager.load();

    // mDNS for http://timer.local
    setupMDNS();

    // Initialize hardware controllers
    setupSegmentController();        // motors, Hall sensors, PCFs
    setupTimerController();          // NTP, timer logic

    // Start web server with REST API and WebSocket
    setupWebServer();

    Serial.println("Setup complete!");
    Serial.print("Open: http://");
    Serial.println(WiFi.localIP());
    Serial.println("Or: http://timer.local");
}

/**
 * Arduino main loop – calls non‑blocking updates.
 */
void loop() {
    updateTimer();                   // checks if timer needs to move digits
    updateTimerController();         // NTP sync, auto‑sync logic
    delay(10);                       // small yield
}