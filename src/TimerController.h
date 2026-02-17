#ifndef TIMER_CONTROLLER_H
#define TIMER_CONTROLLER_H

#include <Arduino.h>

/**
 * @file TimerController.h
 * Public interface for timer logic and NTP synchronisation.
 */

/**
 * Initialise timer controller: NTP client and restore previous state.
 */
void setupTimerController();

/**
 * Manually trigger NTP sync.
 */
void syncTimeWithNTP();

/**
 * Pause the countdown timer.
 */
void stopTimer();

/**
 * Resume the countdown timer.
 */
void startTimer();

/**
 * Check if timer is currently paused.
 * @return true if stopped.
 */
bool isTimerStopped();

/**
 * Get a user‑friendly string of remaining time (e.g., "3 дн.").
 */
String getTimeRemainingString();

/**
 * Called from main loop – updates NTP and auto‑sync.
 */
void updateTimerController();

#endif