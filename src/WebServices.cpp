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

// External references
extern ConfigManager configManager;
extern bool timerStopped;
extern WiFiUDP ntpUDP;
extern NTPClient timeClient;

// Function prototypes (defined later in this file)
String getTimeStringFromRTC();
String formatDate(time_t t);
String formatTime(time_t t);
String unitToString(DurationUnit u);
DurationUnit stringToUnit(const String& s);

void syncTimeWithNTP();
void stopTimer();
void startTimer();
bool isTimerStopped();
String getTimeRemainingString();

// Global web server and WiFiManager instances
AsyncWebServer server(80);
WiFiManager wm;

// -------------------------------------------------------------------
// WebSocket for real‑time updates
// -------------------------------------------------------------------
AsyncWebSocket ws("/ws");   // WebSocket endpoint

/**
 * Broadcast current state to all connected WebSocket clients.
 * JSON structure matches /api/state.
 */
void broadcastState() {
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
    doc["calibrateOnStart"] = config.calibrateOnStart;

    // Remaining seconds (safe 64‑bit)
    if (!timerStopped && configManager.isTimerActive()) {
        doc["remainingSeconds"] = configManager.getRemainingSeconds();
    } else {
        doc["remainingSeconds"] = 0;
    }

    String response;
    serializeJson(doc, response);
    ws.textAll(response);   // send to all clients
}

/**
 * WebSocket event handler.
 */
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected\n", client->id());
            // Send current state immediately on connect
            broadcastState();
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            // We don't process incoming messages – client only listens
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// -------------------------------------------------------------------
// LittleFS and WiFi helpers
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Time formatting helpers
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Duration unit conversion
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Web server setup – REST endpoints and static files
// -------------------------------------------------------------------
void setupWebServer() {
    timeClient.begin();

    // Attach WebSocket handler
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    // ---------- REST API ----------
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
        doc["calibrateOnStart"] = config.calibrateOnStart;

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
        doc["calibrateOnStart"] = config.calibrateOnStart;
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

            // Read all fields with defaults
            bool newUseCurrentOnStart = doc["useCurrentOnStart"] | config.useCurrentOnStart;
            int newDurationValue = doc["durationValue"] | config.duration.value;
            DurationUnit newDurationUnit = doc["durationUnit"].isNull() ? config.duration.unit : stringToUnit(doc["durationUnit"].as<String>());
            int newSyncHour = doc["syncHour"] | config.syncHour24;
            bool newAutoSync = doc["autoSync"] | config.autoSync;
            bool newCalibrateOnStart = doc["calibrateOnStart"] | config.calibrateOnStart;

            // Handle start time logic
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
            config.calibrateOnStart = newCalibrateOnStart;

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

            // Broadcast updated state to all clients
            broadcastState();

            request->send(200, "application/json", "{\"success\":true}");
        }
    );

    server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        auto& config = configManager.getConfig();

        if (isTimerStopped()) {
            int targetValue = configManager.getCurrentValueRemaining();
            // Встановлюємо прапорець, що після руху треба запустити таймер
            setStartAfterMovement(true);
            updateAllSegments(targetValue);
            if (config.useCurrentOnStart) {
                config.startTime = time(nullptr);
                configManager.save();
            }
            // startTimer() буде викликано після завершення руху в SegmentController
            request->send(200, "application/json", "{\"status\":\"started\"}");
        } else {
            stopTimer();
            request->send(200, "application/json", "{\"status\":\"stopped\"}");
        }
        broadcastState();   // notify all clients
    });

    server.on("/api/sync", HTTP_POST, [](AsyncWebServerRequest *request) {
        syncTimeWithNTP();
        request->send(200, "application/json", "{\"success\":true}");
        broadcastState();   // time may have changed
    });

    server.on("/api/calibrate", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (startCalibration()) {
            request->send(200, "application/json", "{\"success\":true, \"message\":\"Calibration started\"}");
            broadcastState();   // calibration in progress now
        } else {
            request->send(429, "application/json", "{\"error\":\"Calibration already in progress\"}");
        }
    });

    // Новий ендпоінт для скидання цифр на 0
    server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        auto& config = configManager.getConfig();
        config.duration.value = 0;
        configManager.save();
        updateAllSegments(0);
        broadcastState();
        request->send(200, "application/json", "{\"success\":true}");
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
            broadcastState();   // digits may change (async, but will reflect soon)
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
            broadcastState();
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid value (0-9999)\"}");
        }
    });

    // Serve static files from LittleFS
    server.serveStatic("/", LittleFS, "/")
          .setDefaultFile("index.html")
          .setTryGzipFirst(false)
          .setFilter([](AsyncWebServerRequest *request) {
              return !request->url().startsWith("/api");
          });

    server.begin();
    Serial.println("Web server started");
}