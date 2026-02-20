#include <Arduino.h>
#include <Wire.h>

#include "ConfigManager.h"
#include "SegmentController.h"
#include "TimerController.h"  // for stopTimer() and startTimer()

// External references
extern ConfigManager configManager;
extern bool timerStopped;

// Forward declaration of broadcast function from WebServices
extern void broadcastState();

// -------------------------------------------------------------------
// Hardware constants
// -------------------------------------------------------------------
#define PCF1_ADDRESS 0x20          // I2C address for first PCF8575 (segments 0,1)
#define PCF2_ADDRESS 0x21          // I2C address for second PCF8575 (segments 2,3)

const int STEPS_PER_REV = 4080;    // 28BYJ-48 full rotation steps (with gearbox)
const int DIGITS = 10;             // 0-9
const int STEPS_PER_DIGIT = STEPS_PER_REV / DIGITS; // ~407 steps per digit

// Homing offset steps after hitting Hall sensor (to align digit 0)
const int OFFSET = 0;             // однаковий для всіх сегментів

// -------------------------------------------------------------------
// Digit position mapping (forward rotation order)
// -------------------------------------------------------------------
// The physical order of digits when moving forward.
// Here forward moves from 0 → 9 → 8 … → 1 → 0.
const int forwardSeq[10] = {0, 9, 8, 7, 6, 5, 4, 3, 2, 1};

// Position of each digit in the forward sequence (inverse mapping)
const int positionOfDigit[10] = {0, 9, 8, 7, 6, 5, 4, 3, 2, 1};

// Direction: false = forward (as defined above)
const bool FORWARD_DIR = false;

// -------------------------------------------------------------------
// Stepper motor phase patterns for ULN2003 (half‑step)
// -------------------------------------------------------------------
const uint8_t steps[8] = {
    0b1000, 0b1100, 0b0100, 0b0110,
    0b0010, 0b0011, 0b0001, 0b1001
};

// Base pin index for each motor on its PCF8575 (4 consecutive pins per motor)
const int MOTOR_BASES[4] = {3, 11, 3, 11};

const int HALL_PINS[2] = {8, 9};

// Current step index (0-7) for each motor
int stepIndices[4] = {0,0,0,0};

// Current displayed digit (0-9) for each segment
int currentDigits[4] = {0,0,0,0};

// Homing status
bool motorsHomed = true;           // assume homed until calibration needed
bool calibrationInProgress = false;

// Current output states for the two PCF8575 (both outputs always written together)
uint16_t motorState1 = (1 << 8) | (1 << 9);  // default: Hall pull‑ups active
uint16_t motorState2 = (1 << 8) | (1 << 9);

unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 1000;  // timer check interval

TaskHandle_t calibrationTaskHandle = NULL;

// -------------------------------------------------------------------
// Non‑blocking motor movement (FreeRTOS task)
// -------------------------------------------------------------------
TaskHandle_t motorTaskHandle = NULL;
volatile int targetDisplayValue = -1;         // desired 4‑digit value
volatile bool motorTaskActive = false;
SemaphoreHandle_t motorMutex = NULL;

// Прапорець, що після завершення руху треба запустити таймер
volatile bool startAfterMovement = false;

// -------------------------------------------------------------------
// Low‑level I2C write to a PCF8575
// -------------------------------------------------------------------
void writePCF(uint8_t address, uint16_t state) {
    Wire.beginTransmission(address);
    Wire.write(state & 0xFF);        // low byte first
    Wire.write((state >> 8) & 0xFF); // high byte
    Wire.endTransmission();
}

// -------------------------------------------------------------------
// Read Hall sensor for a given segment.
// Returns true if magnet is near (active low on PCF8575 input).
// -------------------------------------------------------------------
bool readHallSensor(int segmentIndex) {
    int pcfAddress = (segmentIndex < 2) ? PCF1_ADDRESS : PCF2_ADDRESS;
    int pin = HALL_PINS[segmentIndex % 2];   // Hall on pins 8 or 9

    Wire.requestFrom((uint8_t)pcfAddress, (uint8_t)2);
    if (Wire.available()) {
        uint8_t lowByte = Wire.read();
        uint8_t highByte = Wire.read();
        uint16_t state = (highByte << 8) | lowByte;
        bool active = (state & (1 << pin)) == 0;  // active low
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

// -------------------------------------------------------------------
// Step a single motor by one microstep.
// reverse = true → opposite direction (used only for homing).
// -------------------------------------------------------------------
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
    delay(1);   // small delay for motor coil settling
}

// -------------------------------------------------------------------
// Home a single segment: rotate until Hall sensor triggers, then apply offset.
// Returns true on success.
// -------------------------------------------------------------------
bool homeSegment(int segmentIndex) {
    Serial.printf("Homing segment %d...\n", segmentIndex);
    int safety = 0;
    bool homeDirection = true;      // direction that moves towards sensor
    const int MAX_STEPS = 5000;

    while (!readHallSensor(segmentIndex)) {
        stepMotor(segmentIndex, homeDirection);
        if (++safety > MAX_STEPS) {
            Serial.printf("Homing failed – sensor not found (segment %d)\n", segmentIndex);
            return false;
        }
        taskYIELD();
    }
    Serial.printf("[HALL] Segment %d TRIGGERED at step %d\n", segmentIndex, safety);

    // Move additional offset steps to align digit 0 with window
    for (int i = 0; i < OFFSET; i++) {
        stepMotor(segmentIndex, homeDirection);
        taskYIELD();
    }

    stepIndices[segmentIndex] = 0;      // reset step index (optional)
    currentDigits[segmentIndex] = 0;    // now showing 0
    Serial.printf("Segment %d homed successfully\n", segmentIndex);
    return true;
}

// -------------------------------------------------------------------
// Calibrate all four segments sequentially.
// Returns true if all succeeded.
// -------------------------------------------------------------------
bool calibrateAllSegments() {
    for (int i = 0; i < 4; i++) {
        if (!homeSegment(i)) {
            motorsHomed = false;
            return false;
        }
        delay(500);      // pause between segments
        taskYIELD();
    }
    motorsHomed = true;
    Serial.println("All segments calibrated successfully!");
    return true;
}

// -------------------------------------------------------------------
// FreeRTOS task for calibration (runs on core 0).
// -------------------------------------------------------------------
void calibrationTask(void *pvParameters) {
    Serial.println("Calibration task started");
    calibrationInProgress = true;
    motorsHomed = false;                // під час калібрування двигуни не готові
    bool result = calibrateAllSegments();
    if (!result) {
        Serial.println("Calibration failed!");
        motorsHomed = false;
    }
    calibrationInProgress = false;
    calibrationTaskHandle = NULL;

    // Notify web clients that calibration finished
    broadcastState();

    vTaskDelete(NULL);
}

// -------------------------------------------------------------------
// Public: start calibration (non‑blocking).
// Returns false if already calibrating.
// -------------------------------------------------------------------
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
        0          // core 0
    );
    return true;
}

// -------------------------------------------------------------------
// Public: check if calibration is ongoing.
// -------------------------------------------------------------------
bool isCalibrationInProgress() {
    return calibrationInProgress;
}

// -------------------------------------------------------------------
// Public: check if motors are homed.
// -------------------------------------------------------------------
bool areMotorsHomed() {
    return motorsHomed;
}

// -------------------------------------------------------------------
// Public: get current digits array (pointer to internal storage).
// -------------------------------------------------------------------
int* getCurrentDigits() {
    return currentDigits;
}

// -------------------------------------------------------------------
// Initialize segment controller: set initial PCF states.
// -------------------------------------------------------------------
void setupSegmentController() {
    Serial.println("Initializing Segment Controller...");
    writePCF(PCF1_ADDRESS, motorState1);
    writePCF(PCF2_ADDRESS, motorState2);
    delay(100);

    motorMutex = xSemaphoreCreateMutex();
    Serial.println("Segment Controller ready");
}

// -------------------------------------------------------------------
// Blocking rotation from current digit to target digit.
// Moves only forward (the split‑flap mechanism is unidirectional).
// -------------------------------------------------------------------
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
            stepMotor(segmentIndex, !FORWARD_DIR);   // forward = !reverse
        }
        delay(1);
        taskYIELD();
    }

    currentDigits[segmentIndex] = target;
}

// -------------------------------------------------------------------
// FreeRTOS task for non‑blocking motor movement.
// Reads targetDisplayValue and moves all segments to that value.
// -------------------------------------------------------------------
void motorControlTask(void *pvParameters) {
    Serial.println("Motor control task started");
    motorTaskActive = true;

    while (1) {
        if (targetDisplayValue == -1) break;   // no target, exit

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

        // Якщо був запит на запуск таймера після руху, виконуємо
        if (startAfterMovement) {
            startTimer();   // з TimerController
            startAfterMovement = false;
        }

        // If another movement was requested while we were moving, loop again
        if (targetDisplayValue == value) {
            targetDisplayValue = -1;   // done
        }
    }

    motorTaskActive = false;
    motorTaskHandle = NULL;
    Serial.println("Motor control task finished");

    // Broadcast updated digits after movement completes
    broadcastState();

    vTaskDelete(NULL);
}

// -------------------------------------------------------------------
// Public: set flag to start timer after current movement finishes.
// -------------------------------------------------------------------
void setStartAfterMovement(bool enable) {
    startAfterMovement = enable;
}

// -------------------------------------------------------------------
// Public: start non‑blocking movement to a 4‑digit value.
// -------------------------------------------------------------------
void startMotorMovement(int value) {
    // Не запускаємо рух, якщо триває калібрування
    if (calibrationInProgress) {
        Serial.println("Calibration in progress – movement ignored");
        return;
    }
    if (!motorsHomed) {
        Serial.println("Motors not homed – movement ignored");
        return;
    }

    // Avoid unnecessary movement if already at that value
    int currentValue = currentDigits[0]*1000 + currentDigits[1]*100 + currentDigits[2]*10 + currentDigits[3];
    if (currentValue == value) {
        Serial.printf("Value %d already displayed – skipping motor movement\n", value);
        // Якщо є запит на запуск, все одно запускаємо таймер (без руху)
        if (startAfterMovement) {
            startTimer();
            startAfterMovement = false;
        }
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

// -------------------------------------------------------------------
// Public: update all segments to show a number (non‑blocking).
// -------------------------------------------------------------------
void updateAllSegments(int value) {
    startMotorMovement(value);
}

// -------------------------------------------------------------------
// Public: set a single segment (non‑blocking).
// -------------------------------------------------------------------
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

// -------------------------------------------------------------------
// Public: set all segments to a 4‑digit value (non‑blocking).
// -------------------------------------------------------------------
void setAllSegmentsValue(int value) {
    startMotorMovement(value);
}

// -------------------------------------------------------------------
// Timer update: called from loop() every second.
// If timer is running, compute remaining value and move digits.
// If countdown finished, stop timer and start calibration.
// -------------------------------------------------------------------
void updateTimer() {
    unsigned long now = millis();
    if (now - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = now;

        // Якщо таймер не зупинено (тобто він має працювати)
        if (!timerStopped) {
            int remaining = configManager.getCurrentValueRemaining();
            if (remaining <= 0) {
                // Час вийшов
                stopTimer();               // встановлює timerStopped = true
                startCalibration();         // запускаємо калібрування
                Serial.println("Countdown finished – timer stopped and calibration started");
            } else {
                // Оновлюємо сегменти до поточного залишку
                updateAllSegments(remaining);
            }
        }
    }
}