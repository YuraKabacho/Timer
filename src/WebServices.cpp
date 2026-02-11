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

extern ConfigManager configManager;

// Зовнішні змінні (визначені в інших файлах)
extern bool timerStopped;
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;

// Зовнішні функції (визначені в SegmentController.cpp та TimerController.cpp)
extern void setupSegmentController();
extern bool calibrateAllSegments();
extern bool startCalibration();              // <-- НОВА ФУНКЦІЯ
extern bool isCalibrationInProgress();       // <-- НОВА ФУНКЦІЯ (опціонально)
extern void updateAllSegments(int days);
extern void syncTimeWithNTP();
extern void stopTimer();
extern void startTimer();
extern bool isTimerStopped();
extern bool areMotorsHomed();
extern int* getCurrentDigits();
extern String getTimeRemainingString();
extern String getEndDateDisplay();
extern void setSegmentValue(int segment, int value);
extern void setAllSegmentsValue(int value);

// Глобальні об'єкти
AsyncWebServer server(80);
WiFiManager wm;

// ------------------------------------------------------------
// Функції ініціалізації
// ------------------------------------------------------------

void setupLittleFS() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return;
    }
    Serial.println("LittleFS mounted successfully");
}

void setupWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("timer");

    wm.setConfigPortalTimeout(180);
    wm.setHostname("timer");

    if (!wm.autoConnect("ESP32-Timer")) {
        Serial.println("WiFi failed, rebooting...");
        delay(1000);
        ESP.restart();
    }

    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void setupMDNS() {
    if (!MDNS.begin("timer")) {
        Serial.println("Error starting mDNS responder!");
        return;
    }
    Serial.println("mDNS responder started");
    Serial.println("Access via: http://timer.local");
    MDNS.addService("http", "tcp", 80);
}

// ------------------------------------------------------------
// Допоміжні функції форматування
// ------------------------------------------------------------
String getTimeStringFromRTC() {
    time_t now = time(nullptr);
    if (now == 0) return "--:--:--";
    struct tm *timeinfo = localtime(&now);
    char timeStr[20];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", timeinfo);
    return String(timeStr);
}

String formatDate(time_t t) {
    struct tm *tm = localtime(&t);
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday);
    return String(buf);
}

String formatTime(time_t t) {
    struct tm *tm = localtime(&t);
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    return String(buf);
}

// ------------------------------------------------------------
// Налаштування веб‑сервера та API
// ------------------------------------------------------------
void setupWebServer() {
    timeClient.begin();   // запуск NTP‑клієнта

    // ---------------------- API маршрути ----------------------

    // GET /api/state – отримати повний стан системи
    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        auto& config = configManager.getConfig();

        doc["motorsHomed"] = areMotorsHomed();
        doc["timerStopped"] = isTimerStopped();
        doc["currentTimeFormatted"] = getTimeStringFromRTC();
        doc["timeRemaining"] = getTimeRemainingString();
        doc["endDateDisplay"] = getEndDateDisplay();
        doc["calibrationInProgress"] = isCalibrationInProgress(); // статус калібрування

        int* digits = getCurrentDigits();
        JsonArray segmentValues = doc["segmentValues"].to<JsonArray>();
        for (int i = 0; i < 4; i++) segmentValues.add(digits[i]);

        doc["durationDays"] = config.duration.days;
        doc["syncHour"] = config.syncHour24;
        doc["autoSync"] = config.autoSync;
        doc["startDate"] = formatDate(config.startTime);
        doc["startTime"] = formatTime(config.startTime);

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // GET /api/config – отримати лише конфігурацію (використовується UI)
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        auto& config = configManager.getConfig();
        doc["durationDays"] = config.duration.days;
        doc["syncHour"] = config.syncHour24;
        doc["autoSync"] = config.autoSync;
        doc["startDate"] = formatDate(config.startTime);
        doc["startTime"] = formatTime(config.startTime);
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // POST /api/config – зберегти конфігурацію з UI
    server.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("body", true)) {
            request->send(400, "application/json", "{\"error\":\"No body\"}");
            return;
        }

        String body = request->getParam("body", true)->value();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        auto& config = configManager.getConfig();

        // Оновлення дати/часу початку
        if (doc["startDate"].is<String>() && doc["startTime"].is<String>()) {
            String startDate = doc["startDate"];
            String startTime = doc["startTime"];
            String datetimeStr = startDate + "T" + startTime;

            struct tm tm = {0};
            sscanf(datetimeStr.c_str(), "%d-%d-%dT%d:%d:%d",
                   &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
            tm.tm_year -= 1900;
            tm.tm_mon -= 1;
            config.startTime = mktime(&tm);
        }

        // Оновлення тривалості (дні)
        if (doc["durationDays"].is<int>()) {
            config.duration.days = doc["durationDays"];
        }

        // Оновлення налаштувань NTP
        if (doc["syncHour"].is<int>()) {
            config.syncHour24 = doc["syncHour"];
        }
        if (doc["autoSync"].is<bool>()) {
            config.autoSync = doc["autoSync"];
        }

        bool saveResult = configManager.save();

        if (saveResult) {
            int daysRemaining = configManager.getCurrentDaysRemaining();
            updateAllSegments(daysRemaining);
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Помилка збереження налаштувань\"}");
        }
    });

    // POST /api/sync – ручна синхронізація часу з NTP
    server.on("/api/sync", HTTP_POST, [](AsyncWebServerRequest *request) {
        syncTimeWithNTP();
        request->send(200, "application/json", "{\"success\":true}");
    });
    
    // POST /api/calibrate – запуск калібрування двигунів (асинхронно)
    server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (startCalibration()) {
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Calibration started\"}");
        } else {
            request->send(429, "application/json", "{\"error\":\"Calibration already in progress\"}");
        }
    });
    
    // POST /api/stop – зупинка / запуск таймера
    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (isTimerStopped()) {
            startTimer();
            request->send(200, "application/json", "{\"status\":\"started\"}");
        } else {
            stopTimer();
            request->send(200, "application/json", "{\"status\":\"stopped\"}");
        }
    });
    
    // POST /api/test – тестування окремого сегмента (встановлення цифри)
    server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("segment", true) || !request->hasParam("value", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing parameters\"}");
            return;
        }

        int segment = request->getParam("segment", true)->value().toInt();
        int value = request->getParam("value", true)->value().toInt();

        if (segment >= 0 && segment < 4 && value >= 0 && value <= 9) {
            setSegmentValue(segment, value);
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid parameters\"}");
        }
    });
    
    // POST /api/testall – тестування всіх сегментів (0-9999)
    server.on("/api/testall", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("value", true)) {
            request->send(400, "application/json", "{\"error\":\"Missing value parameter\"}");
            return;
        }

        int value = request->getParam("value", true)->value().toInt();

        if (value >= 0 && value <= 9999) {
            setAllSegmentsValue(value);
            request->send(200, "application/json", "{\"success\":true}");
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid value (0-9999)\"}");
        }
    });
    
    // ---------------------- Статичні файли ----------------------
    server.serveStatic("/", LittleFS, "/")
          .setDefaultFile("index.html");

    server.begin();
    Serial.println("Web server started");
}