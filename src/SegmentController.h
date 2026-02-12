#ifndef SEGMENT_CONTROLLER_H
#define SEGMENT_CONTROLLER_H

#include <Arduino.h>

// Ініціалізація контролера сегментів
void setupSegmentController();

// Переміщення всіх сегментів на задане число (0-9999)
void updateAllSegments(int value);

// Блокувальна версія (не рекомендується)
void setAllSegmentsBlocking(int value);

// Встановлення одного сегмента
void setSegmentValue(int segment, int value);

// Встановлення всіх сегментів (неблокуюче)
void setAllSegmentsValue(int value);

// Калібрування (пошук хому)
bool startCalibration();
bool isCalibrationInProgress();
bool areMotorsHomed();

// Отримання поточних цифр
int* getCurrentDigits();

// Оновлення таймера (викликається в loop)
void updateTimer();

#endif