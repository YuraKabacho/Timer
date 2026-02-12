#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

extern bool timerStopped; // з TimerController.cpp

enum DurationUnit {
    UNIT_DAYS = 0,
    UNIT_HOURS = 1,
    UNIT_MINUTES = 2,
    UNIT_SECONDS = 3
};

struct Duration {
    int value;
    DurationUnit unit;

    Duration() : value(0), unit(UNIT_DAYS) {}
};

class ConfigManager {
private:
    Preferences preferences;
    bool nvsInitialized = false;

    struct TimerConfig {
        time_t startTime;
        Duration duration;
        int syncHour24;
        bool autoSync;
        bool useCurrentOnStart;

        TimerConfig() {
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm *tm = localtime(&now);
                tm->tm_hour = 12;
                tm->tm_min = 0;
                tm->tm_sec = 0;
                startTime = mktime(tm);
            } else {
                struct tm tm = {0};
                tm.tm_year = 126;
                tm.tm_mon = 0;
                tm.tm_mday = 1;
                tm.tm_hour = 12;
                tm.tm_min = 0;
                tm.tm_sec = 0;
                startTime = mktime(&tm);
            }
            duration = Duration();
            syncHour24 = 3;
            autoSync = true;
            useCurrentOnStart = false;
        }
    };

    TimerConfig config;

    long unitToSeconds(DurationUnit u) const {
        switch (u) {
            case UNIT_DAYS:    return 86400L;
            case UNIT_HOURS:   return 3600L;
            case UNIT_MINUTES: return 60L;
            case UNIT_SECONDS: return 1L;
            default:           return 86400L;
        }
    }

public:
    ConfigManager() {}

    bool begin() {
        nvsInitialized = preferences.begin("timer-config", false);
        if (!nvsInitialized) {
            Serial.println("❌ Preferences begin failed");
        }
        return nvsInitialized;
    }

    void load() {
        if (!nvsInitialized) {
            Serial.println("⚠️ NVS not open, cannot load");
            return;
        }

        // Читаємо startTime як два uint32_t
        uint32_t startTimeLow = preferences.getUInt("startLow", 0);
        uint32_t startTimeHigh = preferences.getUInt("startHigh", 0);
        config.startTime = ((uint64_t)startTimeHigh << 32) | startTimeLow;

        config.duration.value = preferences.getInt("durationValue", 0);
        config.duration.unit = (DurationUnit)preferences.getUChar("durUnit", UNIT_DAYS);
        config.syncHour24 = preferences.getInt("syncHour", 3);
        config.autoSync = preferences.getBool("autoSync", true);
        config.useCurrentOnStart = preferences.getBool("useCurStart", false);

        // Якщо startTime ще не збережено (0) – встановлюємо сьогодні 12:00
        if (config.startTime == 0) {
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm *tm = localtime(&now);
                tm->tm_hour = 12;
                tm->tm_min = 0;
                tm->tm_sec = 0;
                config.startTime = mktime(tm);
            }
        }

        Serial.printf("Loaded startTime: %lld\n", (int64_t)config.startTime);
    }

    bool save() {
        if (!nvsInitialized) {
            Serial.println("❌ NVS not open, cannot save");
            return false;
        }

        bool allOk = true;
        size_t ret;

        // --- Зберігаємо startTime як два 32-бітних числа ---
        uint64_t st = (uint64_t)config.startTime;
        uint32_t low = st & 0xFFFFFFFF;
        uint32_t high = (st >> 32) & 0xFFFFFFFF;

        ret = preferences.putUInt("startLow", low);
        Serial.printf("SAVE startLow: %s → %u\n", ret ? "OK" : "FAIL", low);
        allOk &= (ret != 0);

        ret = preferences.putUInt("startHigh", high);
        Serial.printf("SAVE startHigh: %s → %u\n", ret ? "OK" : "FAIL", high);
        allOk &= (ret != 0);

        // --- Інші параметри ---
        ret = preferences.putInt("durationValue", config.duration.value);
        Serial.printf("SAVE durationValue: %s → %d\n", ret ? "OK" : "FAIL", config.duration.value);
        allOk &= (ret != 0);

        ret = preferences.putUChar("durUnit", (uint8_t)config.duration.unit);
        Serial.printf("SAVE durUnit: %s → %d\n", ret ? "OK" : "FAIL", config.duration.unit);
        allOk &= (ret != 0);

        ret = preferences.putInt("syncHour", config.syncHour24);
        Serial.printf("SAVE syncHour: %s → %d\n", ret ? "OK" : "FAIL", config.syncHour24);
        allOk &= (ret != 0);

        ret = preferences.putBool("autoSync", config.autoSync);
        Serial.printf("SAVE autoSync: %s → %d\n", ret ? "OK" : "FAIL", config.autoSync);
        allOk &= (ret != 0);

        ret = preferences.putBool("useCurStart", config.useCurrentOnStart);
        Serial.printf("SAVE useCurStart: %s → %d\n", ret ? "OK" : "FAIL", config.useCurrentOnStart);
        allOk &= (ret != 0);

        if (allOk) {
            Serial.println("✅ All preferences saved successfully.");
        } else {
            Serial.println("❌ One or more preferences write failed (returned 0)");
        }

        return allOk;
    }

    TimerConfig& getConfig() { return config; }

    void setConfig(const TimerConfig& newConfig) {
        config = newConfig;
        save();
    }

    void saveTimerState(bool isRunning) {
        if (!nvsInitialized) return;
        size_t ret = preferences.putBool("timerRunning", isRunning);
        Serial.printf("SAVE timerState: %s → %s\n", ret ? "OK" : "FAIL", isRunning ? "RUNNING" : "STOPPED");
    }

    bool loadTimerState() {
        if (!nvsInitialized) return false;
        return preferences.getBool("timerRunning", false);
    }

    // --- Обчислення залишку в одиницях (без переповнення) ---
    int getCurrentValueRemaining() const {
        time_t now = time(nullptr);
        if (now == 0) return config.duration.value;

        if (now < config.startTime) {
            return config.duration.value;
        }

        long diffSeconds = now - config.startTime;
        long unitSecs = unitToSeconds(config.duration.unit);
        long elapsedUnits = diffSeconds / unitSecs;
        long remaining = config.duration.value - elapsedUnits;
        return (remaining > 0) ? (int)remaining : 0;
    }

    // --- Обчислення залишку в секундах (int64_t, без переповнення) ---
    int64_t getRemainingSeconds() const {
        time_t now = time(nullptr);
        if (now == 0) return 0;
        if (now < config.startTime) {
            return (int64_t)config.duration.value * unitToSeconds(config.duration.unit);
        }

        int64_t start64 = (int64_t)config.startTime;
        int64_t now64 = (int64_t)now;
        int64_t diffSeconds = now64 - start64;
        int64_t unitSecs = (int64_t)unitToSeconds(config.duration.unit);
        int64_t totalSecs = (int64_t)config.duration.value * unitSecs;
        int64_t remainingSecs = totalSecs - diffSeconds;
        return (remainingSecs > 0) ? remainingSecs : 0;
    }

    bool isTimerActive() const {
        if (timerStopped) return false;
        return getCurrentValueRemaining() > 0;
    }

    // Залишено для сумісності, але не використовується в критичних місцях
    time_t calculateEndTime() const {
        int64_t start64 = (int64_t)config.startTime;
        int64_t addSecs = (int64_t)config.duration.value * unitToSeconds(config.duration.unit);
        int64_t end64 = start64 + addSecs;
        if (end64 > INT32_MAX) return (time_t)INT32_MAX;
        if (end64 < 0) return 0;
        return (time_t)end64;
    }
};

#endif