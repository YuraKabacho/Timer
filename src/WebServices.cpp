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

extern ConfigManager configManager;

extern bool timerStopped;
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;

extern void syncTimeWithNTP();
extern void stopTimer();
extern void startTimer();
extern bool isTimerStopped();
extern String getTimeRemainingString();

AsyncWebServer server(80);
WiFiManager wm;

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
String unitToString(DurationUnit u) {
    switch(u) {
        case UNIT_DAYS:    return "days";
        case UNIT_HOURS:   return "hours";
        case UNIT_MINUTES: return "minutes";
        case UNIT_SECONDS: return "seconds";
        default:           return "days";
    }
}

DurationUnit stringToUnit(const String& s) {
    if (s == "hours") return UNIT_HOURS;
    if (s == "minutes") return UNIT_MINUTES;
    if (s == "seconds") return UNIT_SECONDS;
    return UNIT_DAYS;
}

// ------------------------------------------------------------
void setupWebServer() {
    timeClient.begin();

    server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        auto& config = configManager.getConfig();

        doc["motorsHomed"] = areMotorsHomed();
        doc["timerStopped"] = isTimerStopped();
        doc["currentTimeFormatted"] = getTimeStringFromRTC();
        doc["timeRemaining"] = getTimeRemainingString();
        doc["calibrationInProgress"] = isCalibrationInProgress();

        int* digits = getCurrentDigits();
        JsonArray segmentValues = doc["segmentValues"].to<JsonArray>();
        for (int i = 0; i < 4; i++) segmentValues.add(digits[i]);

        doc["durationValue"] = config.duration.value;
        doc["durationUnit"] = unitToString(config.duration.unit);
        doc["syncHour"] = config.syncHour24;
        doc["autoSync"] = config.autoSync;
        doc["startDate"] = formatDate(config.startTime);
        doc["startTime"] = formatTime(config.startTime);
        doc["useCurrentOnStart"] = config.useCurrentOnStart;
        doc["startTimestamp"] = config.startTime;

        // Залишок у секундах (безпечне 64-бітне обчислення)
        if (!timerStopped && configManager.isTimerActive()) {
            doc["remainingSeconds"] = configManager.getRemainingSeconds();
        } else {
            doc["remainingSeconds"] = 0;
        }

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        auto& config = configManager.getConfig();
        doc["durationValue"] = config.duration.value;
        doc["durationUnit"] = unitToString(config.duration.unit);
        doc["syncHour"] = config.syncHour24;
        doc["autoSync"] = config.autoSync;
        doc["startDate"] = formatDate(config.startTime);
        doc["startTime"] = formatTime(config.startTime);
        doc["useCurrentOnStart"] = config.useCurrentOnStart;
        doc["startTimestamp"] = config.startTime;
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    server.on(
        "/api/config",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            static String body;

            if (index == 0) body = "";
            body += String((char*)data).substring(0, len);
            if (index + len != total) return;

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, body);
            if (error) {
                request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
                return;
            }

            auto& config = configManager.getConfig();
            bool oldUseCurrentOnStart = config.useCurrentOnStart;

            bool newUseCurrentOnStart = config.useCurrentOnStart;
            if (!doc["useCurrentOnStart"].isNull()) {
                newUseCurrentOnStart = doc["useCurrentOnStart"];
            }

            int newDurationValue = config.duration.value;
            if (!doc["durationValue"].isNull()) {
                newDurationValue = doc["durationValue"];
            }

            DurationUnit newDurationUnit = config.duration.unit;
            if (!doc["durationUnit"].isNull()) {
                newDurationUnit = stringToUnit(doc["durationUnit"].as<String>());
            }

            int newSyncHour = config.syncHour24;
            if (!doc["syncHour"].isNull()) {
                newSyncHour = doc["syncHour"];
            }

            bool newAutoSync = config.autoSync;
            if (!doc["autoSync"].isNull()) {
                newAutoSync = doc["autoSync"];
            }

            if (newUseCurrentOnStart && !oldUseCurrentOnStart) {
                if (!timerStopped) {
                    stopTimer();
                }
                config.startTime = time(nullptr);
            }

            config.useCurrentOnStart = newUseCurrentOnStart;
            config.duration.value = newDurationValue;
            config.duration.unit = newDurationUnit;
            config.syncHour24 = newSyncHour;
            config.autoSync = newAutoSync;

            if (newUseCurrentOnStart && timerStopped) {
                config.startTime = time(nullptr);
            }

            if (!newUseCurrentOnStart) {
                if (!doc["startDate"].isNull() && !doc["startTime"].isNull()) {
                    String datetimeStr = doc["startDate"].as<String>() + "T" + doc["startTime"].as<String>();
                    struct tm tm = {0};
                    if (sscanf(datetimeStr.c_str(), "%d-%d-%dT%d:%d:%d",
                               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
                        tm.tm_year -= 1900;
                        tm.tm_mon -= 1;
                        config.startTime = mktime(&tm);
                    }
                }
            }

            if (!configManager.save()) {
                request->send(500, "application/json", "{\"error\":\"Save failed\"}");
                return;
            }

            if (!timerStopped) {
                int remaining = configManager.getCurrentValueRemaining();
                updateAllSegments(remaining);
            }

            request->send(200, "application/json", "{\"success\":true}");
        }
    );

    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        auto& config = configManager.getConfig();

        if (isTimerStopped()) {
            int targetValue = configManager.getCurrentValueRemaining();
            updateAllSegments(targetValue);
            if (config.useCurrentOnStart) {
                config.startTime = time(nullptr);
                configManager.save();
            }
            startTimer();
            request->send(200, "application/json", "{\"status\":\"started\"}");
        } else {
            stopTimer();
            request->send(200, "application/json", "{\"status\":\"stopped\"}");
        }
    });

    server.on("/api/sync", HTTP_POST, [](AsyncWebServerRequest *request) {
        syncTimeWithNTP();
        request->send(200, "application/json", "{\"success\":true}");
    });

    server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (startCalibration()) {
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Calibration started\"}");
        } else {
            request->send(429, "application/json", "{\"error\":\"Calibration already in progress\"}");
        }
    });

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

    server.serveStatic("/", LittleFS, "/")
          .setDefaultFile("index.html")
          .setTryGzipFirst(false)
          .setFilter([](AsyncWebServerRequest *request) {
              return !request->url().startsWith("/api");
          });

    server.begin();
    Serial.println("Web server started");
}