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

// Expose BP read helper if other modules want to call it
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw);

#endif // HR_SPO2_BP_H
