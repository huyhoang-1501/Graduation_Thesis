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

// Expose BP read helper if other modules want to call it
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw);

#endif // HR_SPO2_BP_H
