#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

/**
 * @struct Duration
 * @brief  Тривалість відліку в днях.
 */
struct Duration {
    int days;
};

// Зовнішнє оголошення стану таймера (визначено в TimerController.cpp)
extern bool timerStopped;

/**
 * @class ConfigManager
 * @brief  Керування збереженням налаштувань у Flash (Preferences).
 *
 *         Зберігає:
 *         - startTime   (time_t) – дата/час початку відліку
 *         - duration    (int)    – кількість днів відліку
 *         - syncHour24  (int)    – година автоматичної синхронізації NTP
 *         - autoSync    (bool)   – чи ввімкнено автосинхронізацію
 */
class ConfigManager {
private:
    Preferences preferences;

    // Внутрішня структура конфігурації
    struct TimerConfig {
        time_t startTime;      // початкова точка (секунди з 01.01.1970)
        Duration duration;     // тривалість в днях
        int syncHour24;        // година синхронізації (0..23)
        bool autoSync;         // прапорець автосинхронізації

        // Конструктор за замовчуванням
        TimerConfig() {
            time_t now = time(nullptr);
            if (now > 0) {
                struct tm *tm = localtime(&now);
                tm->tm_hour = 12;
                tm->tm_min = 0;
                tm->tm_sec = 0;
                startTime = mktime(tm);
            } else {
                // Якщо час не встановлено – використовуємо 01.01.2026 12:00
                struct tm tm = {0};
                tm.tm_year = 126; // 2026 - 1900
                tm.tm_mon = 0;
                tm.tm_mday = 1;
                tm.tm_hour = 12;
                tm.tm_min = 0;
                tm.tm_sec = 0;
                startTime = mktime(&tm);
            }
            duration = {0};
            syncHour24 = 3;      // 3:00 ночі
            autoSync = true;
        }
    };

    TimerConfig config;

public:
    ConfigManager() {}

    /**
     * @brief Відкриває сховище Preferences.
     * @return true – успішно, false – помилка.
     */
    bool begin() {
        return preferences.begin("timer-config", false);
    }

    /**
     * @brief Завантажує конфігурацію з Flash.
     */
    void load() {
        config.startTime = preferences.getULong64("startTime", 0);
        config.duration.days = preferences.getInt("durationDays", 0);
        config.syncHour24 = preferences.getInt("syncHour", 3);
        config.autoSync = preferences.getBool("autoSync", true);

        // Якщо час не було збережено – встановлюємо поточний
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
    }

    /**
     * @brief Зберігає конфігурацію у Flash.
     * @return true – успішно, false – помилка.
     */
    bool save() {
        bool success = true;
        success &= preferences.putULong64("startTime", config.startTime);
        success &= preferences.putInt("durationDays", config.duration.days);
        success &= preferences.putInt("syncHour", config.syncHour24);
        success &= preferences.putBool("autoSync", config.autoSync);

        // Завершуємо і відкриваємо знову для коректної роботи
        preferences.end();
        preferences.begin("timer-config", false);
        return success;
    }

    /**
     * @brief Повертає посилання на поточну конфігурацію.
     */
    TimerConfig& getConfig() { return config; }

    /**
     * @brief Встановлює нову конфігурацію і зберігає її.
     */
    void setConfig(const TimerConfig& newConfig) {
        config = newConfig;
        save();
    }

    /**
     * @brief Обчислює час завершення відліку.
     * @return time_t (секунди з 01.01.1970).
     */
    time_t calculateEndTime() {
        time_t durationSeconds = config.duration.days * 86400L;
        return config.startTime + durationSeconds;
    }

    /**
     * @brief Повертає кількість днів, що залишилась до завершення.
     * @return int (0..9999).
     */
    int getCurrentDaysRemaining() {
        if (timerStopped) {
            return config.duration.days;
        }

        time_t now = time(nullptr);
        if (now == 0) return config.duration.days;

        time_t endTime = calculateEndTime();
        if (endTime <= now) return 0;

        int secondsRemaining = endTime - now;
        int daysRemaining = secondsRemaining / 86400;
        return daysRemaining;
    }

    /**
     * @brief Перевіряє, чи активний таймер (поточний час менший за час завершення).
     */
    bool isTimerActive() {
        time_t now = time(nullptr);
        if (now == 0) return false;
        return now < calculateEndTime();
    }
};

#endif