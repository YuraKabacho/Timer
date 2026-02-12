#include <Arduino.h>
#include <Wire.h>

#include "ConfigManager.h"
#include "SegmentController.h"

extern ConfigManager configManager;
extern bool timerStopped;

// ---- константи ----
#define PCF1_ADDRESS 0x20
#define PCF2_ADDRESS 0x21

const int STEPS_PER_REV = 4076;
const int DIGITS = 10;
const int STEPS_PER_DIGIT = STEPS_PER_REV / DIGITS; // ~407
const int OFFSETS[4] = {256, 256, 256, 256};

// ---------- ПОСЛІДОВНІСТЬ ЦИФР (тільки вперед) ----------
const int forwardSeq[10] = {0, 9, 8, 7, 6, 5, 4, 3, 2, 1};
const int positionOfDigit[10] = {0, 9, 8, 7, 6, 5, 4, 3, 2, 1};

const bool FORWARD_DIR = false;   // stepMotor(..., false) = вперед

const uint8_t steps[8] = {
    0b1000, 0b1100, 0b0100, 0b0110,
    0b0010, 0b0011, 0b0001, 0b1001
};

const int MOTOR_BASES[4] = {3, 11, 3, 11};

int stepIndices[4] = {0,0,0,0};
int currentDigits[4] = {0,0,0,0};
bool motorsHomed = true;
bool calibrationInProgress = false;

uint16_t motorState1 = (1 << 1) | (1 << 9);
uint16_t motorState2 = (1 << 1) | (1 << 9);

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000;

TaskHandle_t calibrationTaskHandle = NULL;

// ---------- НЕБЛОКУЮЧІ РУХИ ----------
TaskHandle_t motorTaskHandle = NULL;
volatile int targetDisplayValue = -1;
volatile bool motorTaskActive = false;
SemaphoreHandle_t motorMutex = NULL;

// ---- прототипи внутрішніх функцій ----
void writePCF(uint8_t address, uint16_t state);
void stepMotor(int segmentIndex, bool reverse);
bool homeSegment(int segmentIndex);
void rotateToDigitBlocking(int segmentIndex, int target);
bool readHallSensor(int segmentIndex);
int getPCFAddressForSegment(int segmentIndex);
int getPCFPinForHall(int segmentIndex);
void calibrationTask(void *pvParameters);
bool calibrateAllSegments();
void motorControlTask(void *pvParameters);

// ---- ініціалізація ----
void setupSegmentController() {
    Serial.println("Initializing Segment Controller...");
    writePCF(PCF1_ADDRESS, motorState1);
    writePCF(PCF2_ADDRESS, motorState2);
    delay(100);
    
    motorMutex = xSemaphoreCreateMutex();
    Serial.println("Segment Controller ready");
}

void writePCF(uint8_t address, uint16_t state) {
    Wire.beginTransmission(address);
    Wire.write(state & 0xFF);
    Wire.write((state >> 8) & 0xFF);
    Wire.endTransmission();
}

bool readHallSensor(int segmentIndex) {
    int pcfAddress = getPCFAddressForSegment(segmentIndex);
    int pin = getPCFPinForHall(segmentIndex);

    Wire.requestFrom((uint8_t)pcfAddress, (uint8_t)2);
    if (Wire.available()) {
        uint8_t lowByte = Wire.read();
        uint8_t highByte = Wire.read();
        uint16_t state = (highByte << 8) | lowByte;
        bool active = (state & (1 << pin)) != 0;
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

int getPCFAddressForSegment(int segmentIndex) {
    return (segmentIndex < 2) ? PCF1_ADDRESS : PCF2_ADDRESS;
}

int getPCFPinForHall(int segmentIndex) {
    return (segmentIndex % 2 == 0) ? 1 : 9;
}

// ---------- КРОКОВИЙ ДВИГУН ----------
void stepMotor(int segmentIndex, bool reverse) {
    if (reverse) {
        stepIndices[segmentIndex] = (stepIndices[segmentIndex] - 1 + 8) % 8;
    } else {
        stepIndices[segmentIndex] = (stepIndices[segmentIndex] + 1) % 8;
    }
    uint8_t stepPattern = steps[stepIndices[segmentIndex]];
    int motorBase = MOTOR_BASES[segmentIndex];

    if (segmentIndex < 2) {
        motorState1 &= ~(0b1111 << motorBase);
        motorState1 |= (stepPattern << motorBase);
        writePCF(PCF1_ADDRESS, motorState1);
    } else {
        motorState2 &= ~(0b1111 << motorBase);
        motorState2 |= (stepPattern << motorBase);
        writePCF(PCF2_ADDRESS, motorState2);
    }
    delay(1);
}

// ---------- ХОМУВАННЯ ----------
bool homeSegment(int segmentIndex) {
    Serial.printf("Homing segment %d...\n", segmentIndex);
    int safety = 0;
    bool homeDirection = true;
    const int MAX_STEPS = 10000;

    while (!readHallSensor(segmentIndex)) {
        stepMotor(segmentIndex, homeDirection);
        if (++safety > MAX_STEPS) {
            Serial.printf("Homing failed – sensor not found (segment %d)\n", segmentIndex);
            return false;
        }
        taskYIELD();
    }
    Serial.printf("[HALL] Segment %d TRIGGERED at step %d\n", segmentIndex, safety);
    for (int i = 0; i < OFFSETS[segmentIndex]; i++) {
        stepMotor(segmentIndex, homeDirection);
        taskYIELD();
    }
    stepIndices[segmentIndex] = 0;
    currentDigits[segmentIndex] = 0;
    Serial.printf("Segment %d homed successfully\n", segmentIndex);
    return true;
}

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

bool isCalibrationInProgress() {
    return calibrationInProgress;
}

bool areMotorsHomed() {
    return motorsHomed;
}

int* getCurrentDigits() {
    return currentDigits;
}

// ========== НЕБЛОКУЮЧЕ ПЕРЕМІЩЕННЯ ==========
void rotateToDigitBlocking(int segmentIndex, int target) {
    if (!motorsHomed) return;
    if (target < 0 || target >= DIGITS) return;

    int current = currentDigits[segmentIndex];
    if (current == target) return;

    int currentPos = positionOfDigit[current];
    int targetPos = positionOfDigit[target];
    int stepsForward = (targetPos - currentPos + 10) % 10;

    Serial.printf("Segment %d: %d→%d, forward steps: %d\n",
                  segmentIndex, current, target, stepsForward);

    for (int d = 0; d < stepsForward; d++) {
        for (int s = 0; s < STEPS_PER_DIGIT; s++) {
            stepMotor(segmentIndex, !FORWARD_DIR);
        }
        delay(1);
        taskYIELD();
    }

    currentDigits[segmentIndex] = target;
}

void motorControlTask(void *pvParameters) {
    Serial.println("Motor control task started");
    motorTaskActive = true;
    
    while (1) {
        if (targetDisplayValue == -1) break;
        
        if (!motorsHomed) {
            Serial.println("Motors not homed – movement skipped");
            targetDisplayValue = -1;
            break;
        }
        
        int value = targetDisplayValue;
        if (value > 9999) value = 9999;
        if (value < 0) value = 0;
        
        int thousands = (value / 1000) % 10;
        int hundreds  = (value / 100) % 10;
        int tens      = (value / 10) % 10;
        int ones      = value % 10;
        int targetDigits[4] = {thousands, hundreds, tens, ones};
        
        for (int i = 0; i < 4; i++) {
            if (currentDigits[i] != targetDigits[i]) {
                rotateToDigitBlocking(i, targetDigits[i]);
            }
        }
        
        if (targetDisplayValue == value) {
            targetDisplayValue = -1;
        }
    }
    
    motorTaskActive = false;
    motorTaskHandle = NULL;
    Serial.println("Motor control task finished");
    vTaskDelete(NULL);
}

void startMotorMovement(int value) {
    if (!motorsHomed) {
        Serial.println("Motors not homed – movement ignored");
        return;
    }

    // --- Оптимізація: не запускати, якщо значення вже відображається ---
    int currentValue = currentDigits[0]*1000 + currentDigits[1]*100 + currentDigits[2]*10 + currentDigits[3];
    if (currentValue == value) {
        Serial.printf("Value %d already displayed – skipping motor movement\n", value);
        return;
    }
    
    if (xSemaphoreTake(motorMutex, portMAX_DELAY) == pdTRUE) {
        targetDisplayValue = value;
        
        if (!motorTaskActive) {
            xTaskCreatePinnedToCore(
                motorControlTask,
                "MotorTask",
                4096,
                NULL,
                1,
                &motorTaskHandle,
                0
            );
        }
        xSemaphoreGive(motorMutex);
    }
}

void updateAllSegments(int value) {
    startMotorMovement(value);
}

void setAllSegmentsBlocking(int value) {
    startMotorMovement(value);
}

void setSegmentValue(int segment, int value) {
    if (segment < 0 || segment >= 4 || value < 0 || value > 9) return;
    
    int current = currentDigits[0]*1000 + currentDigits[1]*100 + currentDigits[2]*10 + currentDigits[3];
    int newFull;
    switch (segment) {
        case 0: newFull = value*1000 + current%1000; break;
        case 1: newFull = (current/1000)*1000 + value*100 + current%100; break;
        case 2: newFull = (current/100)*100 + value*10 + current%10; break;
        case 3: newFull = (current/10)*10 + value; break;
    }
    startMotorMovement(newFull);
}

void setAllSegmentsValue(int value) {
    startMotorMovement(value);
}

void updateTimer() {
    unsigned long now = millis();
    if (now - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = now;
        if (configManager.isTimerActive() && !timerStopped) {
            int remaining = configManager.getCurrentValueRemaining();
            updateAllSegments(remaining);
        }
    }
}