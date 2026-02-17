#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

extern bool timerStopped; // from TimerController.cpp

/**
 * Units for countdown duration.
 */
enum DurationUnit {
    UNIT_DAYS = 0,
    UNIT_HOURS = 1,
    UNIT_MINUTES = 2,
    UNIT_SECONDS = 3
};

/**
 * Duration value + unit.
 */
struct Duration {
    int value;
    DurationUnit unit;

    Duration() : value(0), unit(UNIT_DAYS) {}
};

/**
 * Main configuration structure stored in NVS.
 */
struct TimerConfig {
    time_t startTime;               // when countdown started (epoch)
    Duration duration;              // length of countdown
    int syncHour24;                 // hour for auto NTP sync (0‑23)
    bool autoSync;                  // enable auto sync?
    bool useCurrentOnStart;          // when starting, use current moment as start?
    bool calibrateOnStart;           // NEW: run calibration before each start?

    TimerConfig() {
        // Default to today at 12:00:00
        time_t now = time(nullptr);
        if (now > 0) {
            struct tm *tm = localtime(&now);
            tm->tm_hour = 12;
            tm->tm_min = 0;
            tm->tm_sec = 0;
            startTime = mktime(tm);
        } else {
            // Fallback: 2026-01-01 12:00:00
            struct tm tm = {0};
            tm.tm_year = 126;   // 2026
            tm.tm_mon = 0;
            tm.tm_mday = 1;
            tm.tm_hour = 12;
            tm.tm_min = 0;
            tm.tm_sec = 0;
            startTime = mktime(&tm);
        }
        duration = Duration();
        syncHour24 = 3;          // default 3 AM
        autoSync = true;
        useCurrentOnStart = false;
        calibrateOnStart = false; // default off
    }
};

/**
 * ConfigManager – saves/loads configuration to/from NVS (Preferences).
 * Also provides helper methods to compute remaining time.
 */
class ConfigManager {
private:
    Preferences preferences;
    bool nvsInitialized = false;

    TimerConfig config;

    /**
     * Convert a duration unit to seconds.
     */
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

    /**
     * Initialise NVS namespace.
     */
    bool begin() {
        nvsInitialized = preferences.begin("timer-config", false);
        if (!nvsInitialized) {
            Serial.println("❌ Preferences begin failed");
        }
        return nvsInitialized;
    }

    /**
     * Load configuration from NVS.
     */
    void load() {
        if (!nvsInitialized) {
            Serial.println("⚠️ NVS not open, cannot load");
            return;
        }

        // startTime stored as two uint32_t
        uint32_t startTimeLow = preferences.getUInt("startLow", 0);
        uint32_t startTimeHigh = preferences.getUInt("startHigh", 0);
        config.startTime = ((uint64_t)startTimeHigh << 32) | startTimeLow;

        config.duration.value = preferences.getInt("durationValue", 0);
        config.duration.unit = (DurationUnit)preferences.getUChar("durUnit", UNIT_DAYS);
        config.syncHour24 = preferences.getInt("syncHour", 3);
        config.autoSync = preferences.getBool("autoSync", true);
        config.useCurrentOnStart = preferences.getBool("useCurStart", false);
        config.calibrateOnStart = preferences.getBool("calibStart", false);   // NEW

        // If startTime was 0 (uninitialised), set to today 12:00
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
        Serial.printf("Loaded calibrateOnStart: %d\n", config.calibrateOnStart);
    }

    /**
     * Save current configuration to NVS.
     */
    bool save() {
        if (!nvsInitialized) {
            Serial.println("❌ NVS not open, cannot save");
            return false;
        }

        bool allOk = true;

        // Save startTime as two 32‑bit halves
        uint64_t st = (uint64_t)config.startTime;
        uint32_t low = st & 0xFFFFFFFF;
        uint32_t high = (st >> 32) & 0xFFFFFFFF;

        allOk &= (preferences.putUInt("startLow", low) != 0);
        allOk &= (preferences.putUInt("startHigh", high) != 0);

        allOk &= (preferences.putInt("durationValue", config.duration.value) != 0);
        allOk &= (preferences.putUChar("durUnit", (uint8_t)config.duration.unit) != 0);
        allOk &= (preferences.putInt("syncHour", config.syncHour24) != 0);
        allOk &= (preferences.putBool("autoSync", config.autoSync) != 0);
        allOk &= (preferences.putBool("useCurStart", config.useCurrentOnStart) != 0);
        allOk &= (preferences.putBool("calibStart", config.calibrateOnStart) != 0); // NEW

        if (allOk) {
            Serial.println("✅ All preferences saved successfully.");
        } else {
            Serial.println("❌ One or more preferences write failed.");
        }

        return allOk;
    }

    /**
     * Get mutable reference to current config.
     */
    TimerConfig& getConfig() { return config; }

    /**
     * Replace entire config and save.
     */
    void setConfig(const TimerConfig& newConfig) {
        config = newConfig;
        save();
    }

    /**
     * Save only the timer running state (used on stop/start).
     */
    void saveTimerState(bool isRunning) {
        if (!nvsInitialized) return;
        preferences.putBool("timerRunning", isRunning);
    }

    /**
     * Load timer running state.
     */
    bool loadTimerState() {
        if (!nvsInitialized) return false;
        return preferences.getBool("timerRunning", false);
    }

    /**
     * Compute remaining value in the chosen unit (e.g., days).
     * Returns 0 if timer expired.
     */
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

    /**
     * Compute remaining seconds as 64‑bit integer (safe for long durations).
     */
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

    /**
     * Check if timer is currently active (running and remaining > 0).
     */
    bool isTimerActive() const {
        if (timerStopped) return false;
        return getCurrentValueRemaining() > 0;
    }

    // Legacy – not used in critical paths
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