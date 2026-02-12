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
#include "SegmentController.h"  // додано

ConfigManager configManager;

// Зовнішні функції
void setupLittleFS();
void setupWiFi();
void setupMDNS();
void setupWebServer();
void setupSegmentController();   // тепер з .h
void setupTimerController();

// Зовнішні змінні
extern bool timerStopped;
extern AsyncWebServer server;
extern WiFiManager wm;

// Функції оновлення
extern void updateTimer();          // з SegmentController.cpp
extern void updateTimerController(); // з TimerController.cpp

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(8, 9);
    Wire.setClock(400000);

    setupLittleFS();
    setupWiFi();

    configManager.begin();
    configManager.load();

    setupMDNS();
    setupSegmentController();
    setupTimerController();
    setupWebServer();

    Serial.println("Setup complete!");
    Serial.print("Open: http://");
    Serial.println(WiFi.localIP());
    Serial.println("Or: http://timer.local");
}

void loop() {
    updateTimer();
    updateTimerController();
    delay(10);
}