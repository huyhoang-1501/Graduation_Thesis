// Simple MAX30100 reader: minimal, robust
// - Uses MAX30100_PulseOximeter library
// - Reads library HR/SpO2 and applies a small rolling average for stability
// - Provides LED current tuning via serial commands: 'u' = up, 'd' = down

#include <Wire.h>
#include "MAX30100_PulseOximeter.h"

PulseOximeter pox;

// Rolling average for library BPM
#define BPM_AVG_SIZE 6
float bpmBuf[BPM_AVG_SIZE];
uint8_t bpmIdx = 0;
uint8_t bpmCount = 0;

// Smoothing (EMA) to reduce display jitter
const float HR_EMA_ALPHA = 0.28;    // lower = smoother, higher = more responsive
const float SPO2_EMA_ALPHA = 0.45; // increased to make SpO2 respond faster
float emaHr = 0.0;
float emaSpO2 = 0.0;
// Optional deadband thresholds (small changes under threshold ignored visually)
const float HR_DISPLAY_DEADBAND = 0.6;   // bpm
const float SPO2_DISPLAY_DEADBAND = 0.1; // percentage points (smaller deadband)
float lastPrintedHr = 0.0;
float lastPrintedSpO2 = 0.0;
// Display/update interval (ms). 
const unsigned long DISPLAY_INTERVAL_MS = 5000UL;

// LED currents available in library enum order
const LEDCurrent ledCurrents[] = {
  MAX30100_LED_CURR_0MA,
  MAX30100_LED_CURR_4_4MA,
  MAX30100_LED_CURR_7_6MA,
  MAX30100_LED_CURR_11MA,
  MAX30100_LED_CURR_14_2MA,
  MAX30100_LED_CURR_17_4MA,
  MAX30100_LED_CURR_20_8MA,
  MAX30100_LED_CURR_24MA
};
int ledIndex = 2; // start at 7.6mA

void pushBpm(float v) {
  bpmBuf[bpmIdx] = v;
  bpmIdx = (bpmIdx + 1) % BPM_AVG_SIZE;
  if (bpmCount < BPM_AVG_SIZE) bpmCount++;
}
float getBpmAvg() {
  if (bpmCount == 0) return 0.0;
  float s = 0;
  for (uint8_t i=0;i<bpmCount;i++) s += bpmBuf[i];
  return s / bpmCount;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Simple MAX30100 reader starting...");
  Wire.begin();
  if (!pox.begin()) {
    Serial.println("ERROR: MAX30100 not found. Check wiring.");
    while (1) delay(1000);
  }
  pox.setIRLedCurrent(ledCurrents[ledIndex]);
  Serial.print("Initial LED current set. Send 'u' or 'd' over Serial to change.\n");
}

void loop() {
  // Keep library running
  pox.update();

  static uint32_t last = 0;
  // Finger-detection removed: always consider sensor ready
  static bool fingerPresent = true;
  if (millis() - last >= DISPLAY_INTERVAL_MS) {
    last = millis();
    float hr = pox.getHeartRate();
    float spO2 = pox.getSpO2();

    // Basic validity checks
    bool hrValid = (hr >= 30.0 && hr <= 220.0);
    bool spO2Valid = (spO2 >= 50.0 && spO2 <= 100.0);

    // Finger-detection disabled: accept valid samples and smooth them
    if (hrValid) {
      // update EMA for HR
      if (emaHr <= 0.0) emaHr = hr;
      else emaHr = HR_EMA_ALPHA * hr + (1.0 - HR_EMA_ALPHA) * emaHr;
      // keep raw buffer as well
      pushBpm(hr);
    }
    if (spO2Valid) {
      if (emaSpO2 <= 0.0) emaSpO2 = spO2;
      else emaSpO2 = SPO2_EMA_ALPHA * spO2 + (1.0 - SPO2_EMA_ALPHA) * emaSpO2;
    }

    // Determine display values using EMA (fall back to raw when EMA not yet initialized)
    float displayHr = (emaHr > 0.0) ? emaHr : ((hr >= 30.0 && hr <= 220.0) ? hr : 0.0);
    float displaySpO2 = (emaSpO2 > 0.0) ? emaSpO2 : ((spO2 >= 50.0 && spO2 <= 100.0) ? spO2 : 0.0);
    // Apply simple deadband so very small fluctuations aren't printed as changes
    if (displayHr > 0.0 && fabs(displayHr - lastPrintedHr) < HR_DISPLAY_DEADBAND) displayHr = lastPrintedHr;
    if (displaySpO2 > 0.0 && fabs(displaySpO2 - lastPrintedSpO2) < SPO2_DISPLAY_DEADBAND) displaySpO2 = lastPrintedSpO2;

    Serial.print("Heart rate: ");
    if (displayHr >= 30.0) {
      Serial.print(displayHr, 1);
      lastPrintedHr = displayHr;
    } else Serial.print("--");
    Serial.print(" bpm    SpO2: ");
    if (displaySpO2 >= 50.0 && displaySpO2 <= 100.0) {
      Serial.print(displaySpO2, 1);
      lastPrintedSpO2 = displaySpO2;
    } else Serial.print("--");
    Serial.print("  LED:"); Serial.print(ledIndex);
    Serial.println();

    // Simple heuristics: if HR=0 but SpO2 valid, recommend repositioning
    if ((displayHr < 30.0) && (spO2 >= 50.0 && spO2 <= 100.0)) {
      Serial.println("Warning: HR invalid but SpO2 OK. Check finger placement and contact.");
    }
  }

  // Serial commands to change LED current
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'u') {
      if (ledIndex < (int)(sizeof(ledCurrents)/sizeof(LEDCurrent)) - 1) ledIndex++;
      pox.setIRLedCurrent(ledCurrents[ledIndex]);
      Serial.print("LED increased to index "); Serial.println(ledIndex);
    } else if (c == 'd') {
      if (ledIndex > 0) ledIndex--;
      pox.setIRLedCurrent(ledCurrents[ledIndex]);
      Serial.print("LED decreased to index "); Serial.println(ledIndex);
    }
  }
}
