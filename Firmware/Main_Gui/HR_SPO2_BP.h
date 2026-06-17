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
// Shared thresholds and phone (single source of truth in HR_SPO2_BP.cpp)
extern int g_spo2_min;
extern int g_spo2_max;
extern int g_hr_min;
extern int g_hr_max;
extern int g_sys_min;
extern int g_sys_max;
extern int g_dia_min;
extern int g_dia_max;

// BP calibration offsets
extern int g_sys_offset;
extern int g_dia_offset;

extern char g_phone[32];
extern const char *DEFAULT_SOS_PHONE;

// Warning counters (owned by HR_SPO2_BP.cpp)
extern volatile int g_hr_warning;
extern volatile int g_spo2_warning;
extern volatile int g_mode1_warning;
extern volatile int g_mode2_warning;
extern volatile unsigned long g_warning_last_inc_ms;

// Thresholds and phone config
void hrspo2bp_set_thresholds(int spo2_min, int spo2_max, int hr_min, int hr_max,
                              int sys_min, int sys_max, int dia_min, int dia_max);
void hrspo2bp_set_phone(const char *phone);
void hrspo2bp_set_bp_offsets(int sys_offset, int dia_offset);

// Warning constants
#define WARNING_TRIGGER_COUNT 5
#define WARNING_RESET_MS 30000

// Call this in the main loop. Returns true if a warning alert was triggered
// (plays DFPlayer alarm and initiates SOS call via SIM module).
bool hrspo2bp_warning_check();

// ====================== FALLBACK MECHANISM ======================
// After an alert, if user doesn't cancel within 1 minute, call the default
// SOS number (DEFAULT_SOS_PHONE) as a fallback.
void hrspo2bp_start_fallback_timer(const char *first_called_phone);
void hrspo2bp_cancel_fallback();
bool hrspo2bp_is_fallback_pending();
void hrspo2bp_fallback_loop();

#endif // HR_SPO2_BP_H
