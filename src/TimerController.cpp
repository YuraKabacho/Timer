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

// Глобальні об'єкти для NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000); // GMT+2, оновлення кожні 60с

// Стан таймера: зупинений чи активний
bool timerStopped = false;
time_t lastSyncTime = 0;   // час останньої успішної синхронізації NTP

/* ============================================================================
   Ініціалізація контролера часу
   ============================================================================ */
void setupTimerController() {
    Serial.println("Initializing Timer Controller...");

    timeClient.begin();

    // Перша синхронізація при старті (якщо WiFi є)
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Synchronizing time with NTP...");
        if (timeClient.update()) {
            time_t now = timeClient.getEpochTime();
            lastSyncTime = now;
            // Встановлюємо системний час ESP32
            struct timeval tv = {now, 0};
            settimeofday(&tv, nullptr);
            Serial.println("Time synchronized successfully");
        } else {
            Serial.println("Failed to sync time");
        }
    }

    Serial.println("Timer Controller ready");
}

/* ============================================================================
   Синхронізація часу (ручний виклик)
   ============================================================================ */
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

/* ============================================================================
   Автоматична синхронізація за розкладом
   ============================================================================ */
void checkAutoSync() {
    if (!configManager.getConfig().autoSync) return;

    time_t now = time(nullptr);
    if (now == 0) return;

    struct tm *timeinfo = localtime(&now);

    // Якщо поточна година збігається із заданою, хвилини = 0, секунди = 0,
    // і минуло більше години після останньої синхронізації – виконуємо
    if (timeinfo->tm_hour == configManager.getConfig().syncHour24 &&
        timeinfo->tm_min == 0 &&
        timeinfo->tm_sec == 0 &&
        (now - lastSyncTime) > 3600) {

        syncTimeWithNTP();
    }
}

/* ============================================================================
   Керування станом таймера
   ============================================================================ */
void stopTimer() {
    timerStopped = true;
    Serial.println("Timer stopped");
}

void startTimer() {
    timerStopped = false;
    Serial.println("Timer started");
}

bool isTimerStopped() {
    return timerStopped;
}

/* ============================================================================
   Рядкові представлення для UI
   ============================================================================ */
String getTimeRemainingString() {
    if (!configManager.isTimerActive() || timerStopped) {
        return "Таймер зупинено";
    }

    int daysRemaining = configManager.getCurrentDaysRemaining();

    if (daysRemaining <= 0) {
        return "Час вийшов";
    }

    char buffer[50];
    sprintf(buffer, "%d днів", daysRemaining);
    return String(buffer);
}

String getEndDateDisplay() {
    time_t endTime = configManager.calculateEndTime();
    struct tm *timeinfo = localtime(&endTime);
    char buffer[50];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return String(buffer);
}

/* ============================================================================
   Оновлення контролера (викликати в loop)
   ============================================================================ */
void updateTimerController() {
    timeClient.update();    // опитування NTP‑сервера (без блокування)
    checkAutoSync();        // перевірка автоматичної синхронізації
}