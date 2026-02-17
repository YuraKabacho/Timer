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

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000); // UTC+2

bool timerStopped = true;          // current run state
time_t lastSyncTime = 0;           // last successful NTP sync

// Forward declaration of broadcast function
extern void broadcastState();

/**
 * Initialise timer controller: start NTP and restore previous timer state.
 */
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

    // Restore timer running state from NVS
    bool wasRunning = configManager.loadTimerState();
    if (wasRunning) {
        Serial.println("Timer was running before reboot – resuming...");
        timerStopped = false;

        int remaining = configManager.getCurrentValueRemaining();
        updateAllSegments(remaining);
        Serial.printf("Resumed with remaining: %d\n", remaining);
    } else {
        Serial.println("Timer was stopped before reboot – staying stopped");
        timerStopped = true;
    }

    Serial.println("Timer Controller ready");
}

/**
 * Manually trigger NTP sync and update system time.
 */
void syncTimeWithNTP() {
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Manual time synchronization...");
        if (timeClient.update()) {
            time_t now = timeClient.getEpochTime();
            lastSyncTime = now;
            struct timeval tv = {now, 0};
            settimeofday(&tv, nullptr);
            Serial.println("Time synchronized manually");
            broadcastState();   // notify clients of new time
        }
    }
}

/**
 * Check if auto‑sync is due (once per day at configured hour).
 */
void checkAutoSync() {
    if (!configManager.getConfig().autoSync) return;
    time_t now = time(nullptr);
    if (now == 0) return;
    struct tm *timeinfo = localtime(&now);
    if (timeinfo->tm_hour == configManager.getConfig().syncHour24 &&
        timeinfo->tm_min == 0 &&
        timeinfo->tm_sec == 0 &&
        (now - lastSyncTime) > 3600) {   // at least one hour since last sync
        syncTimeWithNTP();
    }
}

/**
 * Stop the timer (pause countdown).
 */
void stopTimer() {
    timerStopped = true;
    configManager.saveTimerState(false);
    Serial.println("Timer stopped");
    broadcastState();
}

/**
 * Start the timer (resume countdown).
 */
void startTimer() {
    timerStopped = false;
    configManager.saveTimerState(true);
    Serial.println("Timer started");
    broadcastState();
}

/**
 * Check if timer is currently stopped.
 */
bool isTimerStopped() {
    return timerStopped;
}

/**
 * Get a human‑readable string of remaining time (e.g., "5 дн.").
 */
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

/**
 * Called from main loop – updates NTP client and checks auto‑sync.
 */
void updateTimerController() {
    timeClient.update();    // keep NTP client updated
    checkAutoSync();
}