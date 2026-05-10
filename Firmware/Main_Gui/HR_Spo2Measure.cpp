#include "HR_Spo2Measure.h"

#include <Wire.h>
#include <lvgl.h>
#include <Arduino.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static MAX30105 particleSensor;
static HR_Spo2Measure_done_cb_t g_done_cb = nullptr;

struct async_spo2_t { int spo2; int hr; };

static void notify_spo2(void *arg) {
  async_spo2_t *r = (async_spo2_t*)arg;
  if (g_done_cb) g_done_cb(r->spo2, r->hr);
  free(r);
}

static void spo2_task(void *pv) {
  (void)pv;
  // Use a unique name to avoid collisions with other BUFFER_SIZE macros
  constexpr int SPO2_BUFFER_SIZE = 100;
  uint32_t irBuffer[SPO2_BUFFER_SIZE];
  uint32_t redBuffer[SPO2_BUFFER_SIZE];
  int32_t spo2 = 0; int8_t validSPO2 = 0; int32_t heartRate = 0; int8_t validHeartRate = 0;

  for (int i = 0; i < SPO2_BUFFER_SIZE; i++) {
    while (!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, SPO2_BUFFER_SIZE, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  async_spo2_t *res = (async_spo2_t*)malloc(sizeof(async_spo2_t));
  res->spo2 = (validSPO2) ? (int)spo2 : 0;
  res->hr = (validHeartRate) ? (int)heartRate : 0;
  lv_async_call(notify_spo2, res);
  vTaskDelete(NULL);
}

void HR_Spo2Measure_Init() {
  Wire.begin(21, 22);
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    return;
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x0A);
}

void HR_Spo2Measure_Start(HR_Spo2Measure_done_cb_t cb) {
  g_done_cb = cb;
  xTaskCreate(spo2_task, "spo2_task", 4096, NULL, 1, NULL);
}
