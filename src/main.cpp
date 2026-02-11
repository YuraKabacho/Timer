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

ConfigManager configManager;

// Оголошення зовнішніх функцій (визначені в інших .cpp файлах)
void setupLittleFS();
void setupWiFi();
void setupMDNS();
void setupWebServer();
void setupSegmentController();
void setupTimerController();

// Зовнішні змінні (визначені в TimerController.cpp)
extern bool timerStopped;
extern AsyncWebServer server;
extern WiFiManager wm;

void setup() {
    Serial.begin(115200);
    delay(500);

    // Ініціалізація I2C (SDA=8, SCL=9 для ESP32-S3-Zero)
    Wire.begin(8, 9);
    Wire.setClock(400000);

    setupLittleFS();          // 1. Файлова система (LittleFS)
    setupWiFi();             // 2. Підключення до WiFi / точка доступу

    configManager.begin();   // 3. Ініціалізація Preferences
    configManager.load();    // 4. Завантаження збережених налаштувань

    setupMDNS();             // 5. mDNS (timer.local)
    setupSegmentController(); // 6. Контролер двигунів та PCF8575
    setupTimerController();   // 7. Контролер часу (NTP)
    setupWebServer();        // 8. Веб‑сервер (API + статика)

    Serial.println("Setup complete!");
    Serial.print("Open: http://");
    Serial.println(WiFi.localIP());
    Serial.println("Or: http://timer.local");
}

void loop() {
    // Головний цикл – все працює асинхронно (таймери, сервер)
    delay(10);
}