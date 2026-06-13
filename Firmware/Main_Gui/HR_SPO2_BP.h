#ifndef HR_SPO2_BP_H
#define HR_SPO2_BP_H

#include <Arduino.h>
#include <Wire.h>
// MAX301 support
#include "MAX30105.h"
#include "spo2_algorithm.h"

// Public API for migrated HR/SpO2 and AGR12 BP logic
void hrspo2bp_setup();
void hrspo2bp_loop();

// Expose raw/latest measurements so UI modules can display them
extern int32_t spo2;
extern int32_t heartRate;
// Last computed systolic/diastolic from the most recent BP run
extern float lastSYS;
extern float lastDIA;

// Allow UI to trigger a blood pressure measurement (blocking)
void measureBloodPressure();
// Start BP measurement in a background FreeRTOS task (non-blocking)
void startMeasureBloodPressureAsync();
// Query whether a background BP measurement is in progress
bool isBPMeasuring();
// Cancel the current BP measurement
void cancelMeasureBloodPressure();

// Track origin of the last BP measurement so UI and upload logic can
// distinguish measurements initiated from different modes.
enum BPOrigin { BP_ORIGIN_NONE = 0, BP_ORIGIN_USER = 1, BP_ORIGIN_GUEST = 2 };
extern volatile int lastBPOrigin;

// Start an async BP measurement and mark the origin (User / Guest)
void startMeasureBloodPressureAsyncForOrigin(int origin);

// Expose BP read helper if other modules want to call it
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw);

// ====================== WARNING SYSTEM ======================
// Configure thresholds and phone number for health warnings
void hrspo2bp_set_thresholds(int spo2_min, int spo2_max, int hr_min, int hr_max,
                              int sys_min, int sys_max, int dia_min, int dia_max);
void hrspo2bp_set_phone(const char *phone);

// Call this in the main loop. Returns true if a warning alert was triggered
// (plays DFPlayer alarm and initiates SOS call via SIM module).
bool hrspo2bp_warning_check();

#endif // HR_SPO2_BP_H
