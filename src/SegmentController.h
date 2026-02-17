#ifndef SEGMENT_CONTROLLER_H
#define SEGMENT_CONTROLLER_H

#include <Arduino.h>

/**
 * @file SegmentController.h
 * Public interface for controlling the 4 split‑flap segments.
 */

/**
 * Initialise the segment controller: set up I2C, PCF8575, and mutex.
 * Must be called once before any other functions.
 */
void setupSegmentController();

/**
 * Update all segments to display a given value (0‑9999).
 * Non‑blocking – moves motors in a background task.
 * @param value Number to display (clamped to 0‑9999).
 */
void updateAllSegments(int value);

/**
 * Set a single segment to a digit (0‑9). Non‑blocking.
 * @param segment 0‑3 (thousands, hundreds, tens, ones)
 * @param value 0‑9
 */
void setSegmentValue(int segment, int value);

/**
 * Set all segments to a 4‑digit value. Non‑blocking.
 * @param value 0‑9999
 */
void setAllSegmentsValue(int value);

/**
 * Start homing (calibration) of all segments.
 * @return true if calibration started, false if already in progress.
 */
bool startCalibration();

/**
 * Check if calibration is currently running.
 * @return true if calibration in progress.
 */
bool isCalibrationInProgress();

/**
 * Check if all motors have been homed successfully.
 * @return true if homed.
 */
bool areMotorsHomed();

/**
 * Get pointer to the current digits array (size 4).
 * Useful for UI updates.
 */
int* getCurrentDigits();

/**
 * Called from main loop every ~10ms, checks timer and triggers
 * motor movements when needed.
 */
void updateTimer();

#endif