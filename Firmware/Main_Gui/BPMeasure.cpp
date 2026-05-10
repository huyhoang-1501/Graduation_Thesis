#include "BPMeasure.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Use same pins as test harness
static const int ENA = 32; // PWM Pump
static const int ENB = 33; // PWM Valve

const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH = 0xAC;
const uint8_t CMD_MEASURE_LOW = 0x12;

// Copies from the test implementation but simplified for library use
#define MAX_SAMPLES 250

// allocate large buffers at runtime to avoid DRAM bss overflow
static float *pressureArr = nullptr;
static float *oscArr = nullptr;
static float *oscSignedArr = nullptr;
static unsigned long *timeArr = nullptr;
static int sampleCount = 0;

// adaptive delay
static uint16_t currentDelay = 200;

static bp_measure_done_cb_t g_done_cb = nullptr;

static void i2c_init_local() {
  Wire.begin();
  Wire.setClock(50000);
}

static int requestFromWithRetry(uint8_t addr, uint8_t numBytes, uint8_t retries, uint16_t timeoutMs) {
  for (uint8_t r = 0; r < retries; r++) {
    Wire.requestFrom(addr, numBytes);
    unsigned long start = millis();
    while (Wire.available() < numBytes && (millis() - start) < timeoutMs) {
      delay(1);
    }
    uint8_t avail = Wire.available();
    if (avail >= numBytes) return avail;
    while (Wire.available()) Wire.read();
    delay(5);
  }
  return Wire.available();
}

static void i2c_recovery_local() {
  Wire.end();
  delay(100);
  i2c_init_local();
  delay(200);
}

static bool readPressure_local(float &pressure_kPa, float &pressure_mmHg, int16_t &raw) {
  pressure_kPa = 0.0f; pressure_mmHg = 0.0f; raw = 0;
  const uint8_t MAX_RETRIES = 5;
  for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
    Wire.beginTransmission(AGR12_I2C_ADDRESS);
    Wire.write(CMD_MEASURE_HIGH);
    Wire.write(CMD_MEASURE_LOW);
    uint8_t txErr = Wire.endTransmission();
    if (txErr != 0) {
      if (attempt >= 2) i2c_recovery_local();
      delay(100);
      continue;
    }
    delay(currentDelay);
    const uint8_t expectedBytes = 4;
    int count = requestFromWithRetry(AGR12_I2C_ADDRESS, expectedBytes, 3, 12);
    uint8_t noData = 0, data0 = 0, data1 = 0, crc = 0;
    if (count == expectedBytes) {
      noData = Wire.read();
      data0 = Wire.read();
      data1 = Wire.read();
      crc = Wire.read();
    } else if (count == 3) {
      data0 = Wire.read();
      data1 = Wire.read();
      crc = Wire.read();
      noData = 0;
    } else {
      while (Wire.available()) Wire.read();
      if (attempt >= 2) i2c_recovery_local();
      delay(20);
      continue;
    }
    if (noData != 0xFF && count == expectedBytes) {
      data1 = noData;
      data0 = 0x00;
      crc = data0 ^ data1;
      noData = 0xFF;
    }
    uint8_t crc_xor = data0 ^ data1;
    if (crc != crc_xor) {
      if (crc == 0xFF || crc == 0x00 || data0 == 0xFF || data1 == 0xFF) {
        i2c_recovery_local();
        delay(20);
        continue;
      }
      delay(10);
      continue;
    }
    raw = (int16_t)((data0 << 8) | data1);
    pressure_kPa = raw / 10.0f;
    pressure_mmHg = pressure_kPa * 7.5006f;
    currentDelay = 200;
    return true;
  }
  if (currentDelay < 500) currentDelay += 50;
  return false;
}

// Structure passed to lv_async_call when measurement completes
struct async_result_t { float sys; float dia; float map; float bpm; };

static void notify_ui(void *arg) {
  async_result_t *r = (async_result_t*)arg;
  if (g_done_cb) g_done_cb(r->sys, r->dia, r->map, r->bpm);
  free(r);
}

// Measurement task runs the pump/valve and samples until deflation completes
static void bp_task(void *pv) {
  (void)pv;
  float pressure_kPa = 0.0f;
  float pressure_mmHg = 0.0f;
  int16_t raw = 0;
  sampleCount = 0;

  // allocate large buffers on heap for this measurement
  pressureArr = (float*)malloc(sizeof(float) * MAX_SAMPLES);
  oscArr = (float*)malloc(sizeof(float) * MAX_SAMPLES);
  oscSignedArr = (float*)malloc(sizeof(float) * MAX_SAMPLES);
  timeArr = (unsigned long*)malloc(sizeof(unsigned long) * MAX_SAMPLES);
  if (!pressureArr || !oscArr || !oscSignedArr || !timeArr) {
    // allocation failed -> cleanup and notify failure
    if (pressureArr) free(pressureArr); pressureArr = nullptr;
    if (oscArr) free(oscArr); oscArr = nullptr;
    if (oscSignedArr) free(oscSignedArr); oscSignedArr = nullptr;
    if (timeArr) free(timeArr); timeArr = nullptr;
    async_result_t *resFail = (async_result_t*)malloc(sizeof(async_result_t));
    resFail->sys = 0; resFail->dia = 0; resFail->map = 0; resFail->bpm = 0;
    lv_async_call(notify_ui, resFail);
    vTaskDelete(NULL);
    return;
  }

  // Start pump: PWM using ledcWrite
  ledcWrite(ENA, 243);
  // Ensure valve closed initially
  ledcWrite(ENB, 0);

  unsigned long startTime = millis();
  while (pressure_mmHg < 180.0f) {
    if (millis() - startTime > 20000) {
      // timeout
      ledcWrite(ENA, 0);
      ledcWrite(ENB, 220);
      delay(4000);
      ledcWrite(ENB, 0);
      break;
    }
    if (readPressure_local(pressure_kPa, pressure_mmHg, raw)) {
      if (pressure_mmHg < 0 || pressure_mmHg > 300) continue;
    }
    delay(30);
  }
  ledcWrite(ENA, 0); // stop pump

  // slow deflate and sample
  ledcWrite(ENB, 40);
  float filtered = pressure_mmHg;
  unsigned long deflateStart = millis();
  while (pressure_mmHg > 50.0f && sampleCount < MAX_SAMPLES) {
    if (millis() - deflateStart > 35000) break;
    if (!readPressure_local(pressure_kPa, pressure_mmHg, raw)) continue;
    filtered = 0.9f * filtered + 0.1f * pressure_mmHg;
    float osc = pressure_mmHg - filtered;
    pressureArr[sampleCount] = pressure_mmHg;
    oscArr[sampleCount] = abs(osc);
    oscSignedArr[sampleCount] = osc;
    timeArr[sampleCount] = millis();
    sampleCount++;
    delay(40);
  }

  // fast release
  ledcWrite(ENB, 245);
  while (pressure_mmHg > 5) {
    readPressure_local(pressure_kPa, pressure_mmHg, raw);
    delay(30);
  }
  ledcWrite(ENB, 0);

  // process
  if (sampleCount < 10) {
    // not enough data
    async_result_t *res = (async_result_t*)malloc(sizeof(async_result_t));
    res->sys = 0; res->dia = 0; res->map = 0; res->bpm = 0;
    lv_async_call(notify_ui, res);
    // free buffers
    free(pressureArr); pressureArr = nullptr;
    free(oscArr); oscArr = nullptr;
    free(oscSignedArr); oscSignedArr = nullptr;
    free(timeArr); timeArr = nullptr;
    vTaskDelete(NULL);
    return;
  }

  // envelope smoothing
  // allocate processing buffers on heap to avoid large stack usage
  float *env = (float*)malloc(sizeof(float) * MAX_SAMPLES);
  float *envSm = (float*)malloc(sizeof(float) * MAX_SAMPLES);
  if (!env || !envSm) {
    if (env) free(env);
    if (envSm) free(envSm);
    async_result_t *res = (async_result_t*)malloc(sizeof(async_result_t));
    res->sys = 0; res->dia = 0; res->map = 0; res->bpm = 0;
    lv_async_call(notify_ui, res);
    free(pressureArr); pressureArr = nullptr;
    free(oscArr); oscArr = nullptr;
    free(oscSignedArr); oscSignedArr = nullptr;
    free(timeArr); timeArr = nullptr;
    vTaskDelete(NULL);
    return;
  }
  const int envWin = 4;
  const int IGNORE_FIRST = 3;
  int procStart = min(IGNORE_FIRST, max(0, sampleCount - 1));
  for (int i = 0; i < sampleCount; i++) {
    float m = 0.0f;
    int lo = max(0, i - envWin);
    int hi = min(sampleCount - 1, i + envWin);
    for (int j = lo; j <= hi; j++) if (oscArr[j] > m) m = oscArr[j];
    env[i] = m;
  }
  const int smoothN = 5;
  for (int i = 0; i < sampleCount; i++) {
    float s = 0.0f; int cnt = 0;
    int lo = max(0, i - smoothN / 2);
    int hi = min(sampleCount - 1, i + smoothN / 2);
    for (int j = lo; j <= hi; j++) { s += env[j]; cnt++; }
    envSm[i] = (cnt > 0) ? (s / cnt) : env[i];
  }
  float maxEnv = 0.0f; int maxIndex = 0;
  for (int i = procStart; i < sampleCount; i++) if (envSm[i] > maxEnv) { maxEnv = envSm[i]; maxIndex = i; }
  float MAP = pressureArr[maxIndex];
  float SYS = 0.0f; float DIA = 0.0f;
  float sysTargetRatio = 0.5f; float diaTargetRatio = 0.7f;
  float sysTarget = sysTargetRatio * maxEnv;
  float diaTarget = diaTargetRatio * maxEnv;
  int idx = -1;
  for (int i = maxIndex - 1; i >= procStart; i--) { if (envSm[i] < sysTarget) { idx = i; break; } }
  if (idx < 0) {
    for (int i = procStart; i <= maxIndex; i++) { if (envSm[i] >= sysTarget) { idx = i; break; } }
    if (idx <= 0) SYS = pressureArr[0];
    else {
      float y0 = (idx-1>=0)?envSm[idx-1]:envSm[idx];
      float y1 = envSm[idx];
      float p0 = (idx-1>=0)?pressureArr[idx-1]:pressureArr[idx];
      float p1 = pressureArr[idx];
      float t = (y1 - y0) != 0.0f ? (sysTarget - y0) / (y1 - y0) : 0.0f;
      if (!isfinite(t)) t = 0.0f; t = constrain(t, 0.0f, 1.0f);
      SYS = p0 + t * (p1 - p0);
    }
  } else {
    int i0 = idx; int i1 = min(sampleCount - 1, idx + 1);
    float y0 = envSm[i0]; float y1 = envSm[i1]; float p0 = pressureArr[i0]; float p1 = pressureArr[i1];
    float t = (y1 - y0) != 0.0f ? (sysTarget - y0) / (y1 - y0) : 0.0f;
    if (!isfinite(t)) t = 0.0f; t = constrain(t, 0.0f, 1.0f);
    SYS = p0 + t * (p1 - p0);
  }
  idx = -1;
  for (int i = maxIndex + 1; i < sampleCount; i++) { if (envSm[i] < diaTarget) { idx = i; break; } }
  if (idx < 0) { DIA = pressureArr[min(sampleCount - 1, maxIndex)]; }
  else {
    int i1 = idx; int i0 = max(procStart, idx - 1);
    float y0 = envSm[i0]; float y1 = envSm[i1]; float p0 = pressureArr[i0]; float p1 = pressureArr[i1];
    float t = (y1 - y0) != 0.0f ? (diaTarget - y0) / (y1 - y0) : 0.0f;
    if (!isfinite(t)) t = 0.0f; t = constrain(t, 0.0f, 1.0f);
    DIA = p0 + t * (p1 - p0);
  }
  if (!(SYS > 0 && SYS < 500)) SYS = 0.0f;
  if (!(DIA > 0 && DIA < 500)) DIA = 0.0f;

  // BPM estimation: detect peaks on oscSignedArr
  int *peakIdx = (int*)malloc(sizeof(int) * MAX_SAMPLES);
  int peakCount = 0;
  float peakThreshold = max(0.08f * maxEnv, 0.4f);
  for (int i = 1; i < sampleCount - 1; i++) {
    if (oscSignedArr[i] > oscSignedArr[i-1] && oscSignedArr[i] >= oscSignedArr[i+1] && oscArr[i] > peakThreshold) {
      peakIdx[peakCount++] = i; if (peakCount >= MAX_SAMPLES) break;
    }
  }
  float BPM = 0.0f;
  if (peakCount >= 2) {
    double *intervalsArr = (double*)malloc(sizeof(double) * MAX_SAMPLES);
    int intervalsCnt = 0; const unsigned long minIntervalMs = 300;
    for (int k = 1; k < peakCount; k++) {
      unsigned long t0 = timeArr[peakIdx[k-1]]; unsigned long t1 = timeArr[peakIdx[k]]; if (t1 > t0) {
        unsigned long dt = t1 - t0; if (dt >= minIntervalMs) intervalsArr[intervalsCnt++] = (double)dt;
      }
    }
    if (intervalsCnt > 0) {
      for (int a = 0; a < intervalsCnt - 1; a++) { int mi = a; for (int b = a + 1; b < intervalsCnt; b++) if (intervalsArr[b] < intervalsArr[mi]) mi = b; if (mi != a) { double tmp = intervalsArr[a]; intervalsArr[a] = intervalsArr[mi]; intervalsArr[mi] = tmp; } }
      double medianMs = (intervalsCnt % 2 == 1) ? intervalsArr[intervalsCnt/2] : 0.5 * (intervalsArr[intervalsCnt/2 - 1] + intervalsArr[intervalsCnt/2]);
      double b = 60000.0 / medianMs; if (isfinite(b) && b > 30.0 && b < 220.0) BPM = (float)b;
    }
    if (intervalsArr) free(intervalsArr);
  }

  async_result_t *res = (async_result_t*)malloc(sizeof(async_result_t));
  res->sys = SYS; res->dia = DIA; res->map = MAP; res->bpm = BPM;
  lv_async_call(notify_ui, res);
  // free all allocated buffers
  if (env) free(env);
  if (envSm) free(envSm);
  if (peakIdx) free(peakIdx);
  if (pressureArr) { free(pressureArr); pressureArr = nullptr; }
  if (oscArr) { free(oscArr); oscArr = nullptr; }
  if (oscSignedArr) { free(oscSignedArr); oscSignedArr = nullptr; }
  if (timeArr) { free(timeArr); timeArr = nullptr; }
  vTaskDelete(NULL);
}

void BPMeasure_Init() {
  // Setup PWM channels for pump/valve
  ledcAttach(ENA, 1000, 8);
  ledcAttach(ENB, 1000, 8);
  i2c_init_local();
}

void BPMeasure_Start(bp_measure_done_cb_t cb) {
  g_done_cb = cb;
  // spawn freeRTOS task
  xTaskCreate(bp_task, "bp_task", 8192, NULL, 1, NULL);
}
