#include <Arduino.h>
#include <Wire.h>

#include "ConfigManager.h"

extern ConfigManager configManager;
extern bool timerStopped;

/* ============================================================================
   КОНСТАНТИ ТА НАЛАШТУВАННЯ ДВИГУНІВ І ДАТЧИКІВ
   ============================================================================ */

// Адреси PCF8575 на шині I2C
#define PCF1_ADDRESS 0x20      // перша мікросхема – сегменти 0 та 1
#define PCF2_ADDRESS 0x21      // друга мікросхема – сегменти 2 та 3

// Параметри крокового двигуна 28BYJ-48
const int STEPS_PER_REV = 4076;   // повний оберт (редуктор 64:1, півкроки)
const int DIGITS = 10;            // 10 цифр на циферблаті
const int STEPS_PER_DIGIT = STEPS_PER_REV / DIGITS;   // кроків для переходу на одну цифру

// Калібрувальні зсуви – кількість додаткових кроків після спрацювання датчика,
// щоб опинитись точно по центру цифри "0". Значення підібрані експериментально.
const int OFFSETS[4] = {256, 256, 256, 256};

// Напрямок обертання для кожного сегмента.
// true  = обертання проти годинникової стрілки (reverse)
// false = обертання за годинниковою стрілкою
//
// ⚠ Якщо двигун крутить не в той бік – змініть відповідне значення на протилежне.
const bool REVERSE[4] = {true, true, true, true};

// Фази для напівкрокового режиму (half‑step) драйвера ULN2003
// Порядок відповідає підключенню IN1–IN4 до PCF8575
const uint8_t steps[8] = {
    0b1000, 0b1100, 0b0100, 0b0110,
    0b0010, 0b0011, 0b0001, 0b1001
};

/* ----------------------------------------------------------------------------
   РОЗПОДІЛ ВИВОДІВ PCF8575
   ----------------------------------------------------------------------------
   Мікросхема має 16 ліній вводу/виводу: P00–P07 (молодший байт) та P10–P17 (старший байт).
   Бітові позиції:
     P00 – біт 0, P01 – біт 1, ..., P07 – біт 7,
     P10 – біт 8, P11 – біт 9, P12 – біт 10, P13 – біт 11,
     P14 – біт 12, P15 – біт 13, P16 – біт 14, P17 – біт 15.

   ──────────────────────────────────────────────────────────────────────────
   PCF1 (0x20) – сегменти 0 (тисячі) та 1 (сотні)
     Сегмент 0 (індекс 0):
       - двигун: P03, P04, P05, P06  (біти 3,4,5,6) → база = 3
       - датчик Холла: P01 (біт 1)   – активний ВИСОКИЙ рівень (інверсна логіка)
     Сегмент 1 (індекс 1):
       - двигун: P13, P14, P15, P16  (біти 11,12,13,14) → база = 11
       - датчик Холла: P11 (біт 9)   – активний ВИСОКИЙ рівень

   PCF2 (0x21) – сегменти 2 (десятки) та 3 (одиниці)
     Сегмент 2 (індекс 2):
       - двигун: P03, P04, P05, P06  (біти 3,4,5,6) → база = 3
       - датчик Холла: P01 (біт 1)   – активний ВИСОКИЙ рівень
     Сегмент 3 (індекс 3):
       - двигун: P13, P14, P15, P16  (біти 11,12,13,14) → база = 11
       - датчик Холла: P11 (біт 9)   – активний ВИСОКИЙ рівень
   ---------------------------------------------------------------------------- */

// Бази для управління моторами – номер першого біта в регістрі PCF, з якого
// починаються 4 послідовні виводи для IN1–IN4.
const int MOTOR_BASES[4] = {
    3,   // сегмент 0 (PCF1, P03–P06)
    11,  // сегмент 1 (PCF1, P13–P16)
    3,   // сегмент 2 (PCF2, P03–P06)
    11   // сегмент 3 (PCF2, P13–P16)
};

/* ----------------------------------------------------------------------------
   ГЛОБАЛЬНІ ЗМІННІ
   ---------------------------------------------------------------------------- */

// Поточний індекс кроку (0..7) для кожного двигуна – використовується для
// зберігання фази та обчислення наступного кроку.
int stepIndices[4] = {0, 0, 0, 0};

// Поточне значення цифри (0–9) на кожному сегменті.
// Після калібрування завжди 0, оновлюється під час обертання.
int currentDigits[4] = {0, 0, 0, 0};

// Флаг, який вказує, чи всі двигуни успішно відкалібровані.
// Без цього флага рух неможливий (захист від неправильної позиції).
bool motorsHomed = true;

// Флаг, який вказує, що калібрування зараз виконується (захист від повторного запуску).
bool calibrationInProgress = false;

// Стан виводів кожної PCF (усі 16 біт).
// Початково встановлюємо піни Холла в логічну «1» (високий рівень),
// що відповідає режиму входу з зовнішнім резистором pull-up до 5 В.
uint16_t motorState1 = (1 << 1) | (1 << 9);   // PCF1: біти 1 (P01) та 9 (P11) = 1
uint16_t motorState2 = (1 << 1) | (1 << 9);   // PCF2: біти 1 (P01) та 9 (P11) = 1

// Змінна для періодичного оновлення таймера (використовується в updateTimer)
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000;   // інтервал 1 секунда

// Handle задачі калібрування (для можливого переривання)
TaskHandle_t calibrationTaskHandle = NULL;

/* ============================================================================
   ПРОТОТИПИ ФУНКЦІЙ
   ============================================================================ */
void writePCF(uint8_t address, uint16_t state);
void stepMotor(int segmentIndex, bool reverse);
bool homeSegment(int segmentIndex);
void rotateToDigit(int segmentIndex, int target);
bool readHallSensor(int segmentIndex);
int getPCFAddressForSegment(int segmentIndex);
int getPCFPinForHall(int segmentIndex);
void calibrationTask(void *pvParameters);
bool calibrateAllSegments();

/* ============================================================================
   РЕАЛІЗАЦІЯ
   ============================================================================ */

/**
 * @brief Ініціалізація контролера сегментів.
 *        Записує початковий стан у PCF8575 (піни Холла = HIGH, мотори вимкнені).
 */
void setupSegmentController() {
    Serial.println("Initializing Segment Controller...");
    writePCF(PCF1_ADDRESS, motorState1);
    writePCF(PCF2_ADDRESS, motorState2);
    delay(100);
    Serial.println("Segment Controller ready");
}

/**
 * @brief Запис 16-бітного значення в PCF8575 за вказаною адресою.
 * @param address I2C адреса мікросхеми.
 * @param state   бітова маска стану всіх 16 виводів.
 */
void writePCF(uint8_t address, uint16_t state) {
    Wire.beginTransmission(address);
    Wire.write(state & 0xFF);        // молодший байт (P00–P07)
    Wire.write((state >> 8) & 0xFF); // старший байт (P10–P17)
    Wire.endTransmission();
}

/**
 * @brief Читання стану датчика Холла для заданого сегмента.
 * @param segmentIndex індекс сегмента (0..3).
 * @return true  – магніт виявлено (активний ВИСОКИЙ рівень),
 *         false – магніт відсутній.
 * 
 * 🔁 ІНВЕРСНА ЛОГІКА: активний стан = 1 (пін підтягнутий до 5V через резистор).
 */
bool readHallSensor(int segmentIndex) {
    int pcfAddress = getPCFAddressForSegment(segmentIndex);
    int pin = getPCFPinForHall(segmentIndex);

    Wire.requestFrom((uint8_t)pcfAddress, (uint8_t)2);
    if (Wire.available()) {
        uint8_t lowByte = Wire.read();
        uint8_t highByte = Wire.read();
        uint16_t state = (highByte << 8) | lowByte;

        // 🔁 ІНВЕРСНА ЛОГІКА: активний = 1
        bool active = (state & (1 << pin)) != 0;
        
        // 📍 ЛОГ – тільки при зміні стану
        static bool lastState[4] = {false, false, false, false};
        if (active != lastState[segmentIndex]) {
            Serial.printf("[HALL] Segment %d: %s\n", 
                          segmentIndex, active ? "ACTIVE 🔴" : "INACTIVE ⚪");
            lastState[segmentIndex] = active;
        }
        return active;
    }
    return false;
}

/**
 * @brief Повертає I2C адресу PCF8575 для заданого сегмента.
 */
int getPCFAddressForSegment(int segmentIndex) {
    return (segmentIndex < 2) ? PCF1_ADDRESS : PCF2_ADDRESS;
}

/**
 * @brief Повертає номер біта (0..15) на PCF8575, до якого підключено датчик Холла.
 */
int getPCFPinForHall(int segmentIndex) {
    // Сегменти з парним індексом (0,2) → P01 (біт 1)
    // Сегменти з непарним індексом (1,3) → P11 (біт 9)
    return (segmentIndex % 2 == 0) ? 1 : 9;
}

/**
 * @brief Виконує один крок двигуна (half‑step).
 * @param segmentIndex індекс сегмента.
 * @param reverse      true – крок назад, false – крок вперед.
 */
void stepMotor(int segmentIndex, bool reverse) {
    // Оновлення індексу кроку
    if (reverse) {
        stepIndices[segmentIndex] = (stepIndices[segmentIndex] - 1 + 8) % 8;
    } else {
        stepIndices[segmentIndex] = (stepIndices[segmentIndex] + 1) % 8;
    }

    uint8_t stepPattern = steps[stepIndices[segmentIndex]];
    int motorBase = MOTOR_BASES[segmentIndex];

    if (segmentIndex < 2) {   // PCF1
        // Очищуємо тільки 4 біти, що відповідають двигуну (інші біти – піни Холла – залишаємо)
        motorState1 &= ~(0b1111 << motorBase);
        motorState1 |= (stepPattern << motorBase);
        writePCF(PCF1_ADDRESS, motorState1);
    } else {                  // PCF2
        motorState2 &= ~(0b1111 << motorBase);
        motorState2 |= (stepPattern << motorBase);
        writePCF(PCF2_ADDRESS, motorState2);
    }

    delayMicroseconds(1000);   // мінімальна затримка для стабільності
}

/**
 * @brief Калібрування одного сегмента – пошук нульової позиції за допомогою датчика Холла.
 * @param segmentIndex індекс сегмента.
 * @return true – успішно, false – помилка (не знайдено датчик).
 */
bool homeSegment(int segmentIndex) {
    Serial.printf("Homing segment %d...\n", segmentIndex);

    int safety = 0;
    bool reverse = REVERSE[segmentIndex];
    const int MAX_STEPS = 10000;   // 2000 кроків ≈ 4 секунди (цілком достатньо)

    // Обертаємо, поки датчик Холла не стане активним (шукаємо магніт)
    while (!readHallSensor(segmentIndex)) {
        stepMotor(segmentIndex, reverse);
        delay(1);
        if (++safety > MAX_STEPS) {
            Serial.printf("Homing failed – sensor not found (segment %d)\n", segmentIndex);
            return false;
        }
        taskYIELD();
    }

    // 📍 Датчик щойно спрацював – виведемо додатковий лог
    Serial.printf("[HALL] Segment %d TRIGGERED at step %d\n", segmentIndex, safety);

    // Докрутка до центру цифри "0" (компенсація механічного зміщення)
    for (int i = 0; i < OFFSETS[segmentIndex]; i++) {
        stepMotor(segmentIndex, reverse);
        delay(1);
        taskYIELD();
    }

    // Скидаємо індекс кроку та поточну цифру
    stepIndices[segmentIndex] = 0;
    currentDigits[segmentIndex] = 0;

    Serial.printf("Segment %d homed successfully\n", segmentIndex);
    return true;
}

/**
 * @brief Калібрування всіх чотирьох сегментів.
 * @return true – усі сегменти відкалібровано, false – помилка.
 */
bool calibrateAllSegments() {
    for (int i = 0; i < 4; i++) {
        if (!homeSegment(i)) {
            motorsHomed = false;
            return false;
        }
        delay(500);
        taskYIELD();
    }

    motorsHomed = true;
    Serial.println("All segments calibrated successfully!");
    return true;
}

/**
 * @brief Задача FreeRTOS для виконання калібрування в фоновому режимі.
 */
void calibrationTask(void *pvParameters) {
    Serial.println("Calibration task started");
    calibrationInProgress = true;
    
    bool result = calibrateAllSegments();
    
    if (!result) {
        Serial.println("Calibration failed!");
        motorsHomed = false;
    }
    
    calibrationInProgress = false;
    calibrationTaskHandle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Запускає калібрування в окремій задачі.
 */
bool startCalibration() {
    if (calibrationInProgress) {
        Serial.println("Calibration already in progress");
        return false;
    }
    
    xTaskCreatePinnedToCore(
        calibrationTask,
        "CalibrationTask",
        4096,
        NULL,
        1,
        &calibrationTaskHandle,
        0
    );
    return true;
}

/**
 * @brief Повертає заданий сегмент на вказану цифру.
 */
void rotateToDigit(int segmentIndex, int target) {
    if (!motorsHomed) {
        Serial.println("Motors not homed – rotation skipped");
        return;
    }
    if (target < 0 || target >= DIGITS) return;

    int current = currentDigits[segmentIndex];
    if (current == target) return;

    int diff = target - current;
    if (diff < 0) diff += DIGITS;

    bool reverse = REVERSE[segmentIndex];

    for (int d = 0; d < diff; d++) {
        for (int s = 0; s < STEPS_PER_DIGIT; s++) {
            stepMotor(segmentIndex, reverse);
        }
        delay(1);
        taskYIELD();
    }

    currentDigits[segmentIndex] = target;
    Serial.printf("Segment %d rotated from %d to %d\n", segmentIndex, current, target);
}

/**
 * @brief Оновлює всі сегменти відповідно до кількості днів.
 */
void updateAllSegments(int days) {
    if (!motorsHomed) {
        Serial.println("Motors not homed – display update skipped");
        return;
    }

    if (days > 9999) days = 9999;
    if (days < 0) days = 0;

    int thousands = (days / 1000) % 10;
    int hundreds  = (days / 100) % 10;
    int tens      = (days / 10) % 10;
    int ones      = days % 10;

    int targetDigits[4] = {thousands, hundreds, tens, ones};

    for (int i = 0; i < 4; i++) {
        if (currentDigits[i] != targetDigits[i]) {
            rotateToDigit(i, targetDigits[i]);
        }
    }

    Serial.printf("Display updated to: %d\n", days);
}

/**
 * @brief Встановлює конкретний сегмент на задану цифру.
 */
void setSegmentValue(int segment, int value) {
    if (segment >= 0 && segment < 4 && value >= 0 && value < 10) {
        rotateToDigit(segment, value);
    }
}

/**
 * @brief Встановлює всі сегменти на число 0–9999.
 */
void setAllSegmentsValue(int value) {
    if (value < 0 || value > 9999) return;

    int thousands = (value / 1000) % 10;
    int hundreds  = (value / 100) % 10;
    int tens      = (value / 10) % 10;
    int ones      = value % 10;

    int targetDigits[4] = {thousands, hundreds, tens, ones};

    for (int i = 0; i < 4; i++) {
        if (currentDigits[i] != targetDigits[i]) {
            rotateToDigit(i, targetDigits[i]);
        }
    }
}

/**
 * @brief Повертає статус калібрування двигунів.
 */
bool areMotorsHomed() {
    return motorsHomed;
}

/**
 * @brief Повертає статус виконання калібрування.
 */
bool isCalibrationInProgress() {
    return calibrationInProgress;
}

/**
 * @brief Повертає масив поточних цифр на сегментах.
 */
int* getCurrentDigits() {
    return currentDigits;
}

/**
 * @brief Періодичне оновлення таймера.
 */
void updateTimer() {
    unsigned long now = millis();
    if (now - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = now;

        if (configManager.isTimerActive() && !timerStopped) {
            int daysRemaining = configManager.getCurrentDaysRemaining();
            updateAllSegments(daysRemaining);
        }
    }
}