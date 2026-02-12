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
#include "SegmentController.h"  // для updateAllSegments

extern ConfigManager configManager;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000);

bool timerStopped = true;
time_t lastSyncTime = 0;

void setupTimerController() {
    Serial.println("Initializing Timer Controller...");
    timeClient.begin();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Synchronizing time with NTP...");
        if (timeClient.update()) {
            time_t now = timeClient.getEpochTime();
            lastSyncTime = now;
            struct timeval tv = {now, 0};
            settimeofday(&tv, nullptr);
            Serial.println("Time synchronized successfully");
        } else {
            Serial.println("Failed to sync time");
        }
    }

    // --- ВІДНОВЛЕННЯ СТАНУ ТАЙМЕРА ПІСЛЯ ПЕРЕЗАВАНТАЖЕННЯ ---
    bool wasRunning = configManager.loadTimerState();
    if (wasRunning) {
        Serial.println("Timer was running before reboot – resuming...");
        timerStopped = false;
        
        // Обчислюємо актуальний залишок і переміщуємо двигуни
        int remaining = configManager.getCurrentValueRemaining();
        updateAllSegments(remaining);
        Serial.printf("Resumed with remaining: %d\n", remaining);
    } else {
        Serial.println("Timer was stopped before reboot – staying stopped");
        timerStopped = true;
    }

    Serial.println("Timer Controller ready");
}

void syncTimeWithNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Manual time synchronization...");
        if (timeClient.update()) {
            time_t now = timeClient.getEpochTime();
            lastSyncTime = now;
            struct timeval tv = {now, 0};
            settimeofday(&tv, nullptr);
            Serial.println("Time synchronized manually");
        }
    }
}

void checkAutoSync() {
    if (!configManager.getConfig().autoSync) return;
    time_t now = time(nullptr);
    if (now == 0) return;
    struct tm *timeinfo = localtime(&now);
    if (timeinfo->tm_hour == configManager.getConfig().syncHour24 &&
        timeinfo->tm_min == 0 &&
        timeinfo->tm_sec == 0 &&
        (now - lastSyncTime) > 3600) {
        syncTimeWithNTP();
    }
}

void stopTimer() {
    timerStopped = true;
    configManager.saveTimerState(false);
    Serial.println("Timer stopped");
}

void startTimer() {
    timerStopped = false;
    configManager.saveTimerState(true);
    Serial.println("Timer started");
}

bool isTimerStopped() {
    return timerStopped;
}

String getTimeRemainingString() {
    if (!configManager.isTimerActive() || timerStopped) {
        return "Таймер зупинено";
    }
    int remaining = configManager.getCurrentValueRemaining();
    if (remaining <= 0) {
        return "Час вийшов";
    }
    DurationUnit u = configManager.getConfig().duration.unit;
    const char* unitStr;
    switch(u) {
        case UNIT_DAYS:    unitStr = "дн."; break;
        case UNIT_HOURS:   unitStr = "год."; break;
        case UNIT_MINUTES: unitStr = "хв."; break;
        case UNIT_SECONDS: unitStr = "сек."; break;
        default:           unitStr = "дн."; break;
    }
    char buffer[50];
    sprintf(buffer, "%d %s", remaining, unitStr);
    return String(buffer);
}

void updateTimerController() {
    timeClient.update();
    checkAutoSync();
}