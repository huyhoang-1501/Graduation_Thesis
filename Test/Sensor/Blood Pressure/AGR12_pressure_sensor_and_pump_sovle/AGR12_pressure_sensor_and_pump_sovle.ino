#include <Arduino.h>
#include <Wire.h>

// ================== KẾT NỐI L298N + PUMP + VALVE ==================

const int ENA = 32;   // PWM Pump
const int ENB = 33;   // PWM Valve

const int pwmFreq = 1000;
const int pwmRes  = 8;

// ================== CẢM BIẾN AGR12 ==================
const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH = 0xAC;
const uint8_t CMD_MEASURE_LOW  = 0x12;

const uint16_t MEASURE_DELAY_MIN = 200;
const uint16_t MEASURE_DELAY_MAX = 500;
const uint8_t  MAX_RETRIES       = 5;

#define MAX_SAMPLES 250

float pressureArr[MAX_SAMPLES];
float oscArr[MAX_SAMPLES];
float oscSignedArr[MAX_SAMPLES];
unsigned long timeArr[MAX_SAMPLES];
int sampleCount = 0;

// Adaptive delay
uint16_t currentDelay = MEASURE_DELAY_MIN;

// Tunable algorithm parameters (adjust at runtime via serial)
// Tuned defaults (based on sample traces) — you can still change at runtime
float SYS_RATIO = 0.5f;  // increased to move SYS detection closer to MAP
float DIA_RATIO = 0.7f;  // tuned for this trace
bool dumpSamplesNextRun = false; // if true, measurement will print CSV of samples

// Height-based threshold ranges for SYS/DIA (use midpoint for crossing)
const float SYS_MIN_RATIO = 0.40f; // 40% of max envelope
const float SYS_MAX_RATIO = 0.60f; // 60% of max envelope
const float DIA_MIN_RATIO = 0.65f; // 65% of max envelope
const float DIA_MAX_RATIO = 0.85f; // 85% of max envelope

// ================== PROTOTYPES ==================
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw);
void startPump(int speed = 200);
void stopPump();
void openValve(int speed = 60);
void closeValve();
void stopAll();
void measureBloodPressure();
void i2c_recovery();
void i2c_init();

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Using only PWM enable pins for speed control (ENA/ENB).
  // Direction inputs removed; driver should be wired for fixed forward direction.
  ledcAttach(ENA, pwmFreq, pwmRes);
  ledcAttach(ENB, pwmFreq, pwmRes);

  i2c_init();

  Serial.println("=== HỆ THỐNG ĐO HUYẾT ÁP AGR12 ĐÃ KHỞI ĐỘNG ===");
  Serial.println("Gõ 'start' để bắt đầu đo\n");

  stopAll();
}

// ====================== LOOP ======================
void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "start") {
      measureBloodPressure();
    }
    else if (cmd == "stop") {
      stopAll();
      Serial.println("Đã dừng tất cả.");
    }
    else if (cmd.startsWith("set sys ")) {
      float v = cmd.substring(8).toFloat();
      if (v > 0.1 && v < 1.0) {
        SYS_RATIO = v;
        Serial.printf("SYS_RATIO set to %.3f\n", SYS_RATIO);
      } else Serial.println("Invalid SYS value (0.1-1.0)");
    }
    else if (cmd.startsWith("set dia ")) {
      float v = cmd.substring(8).toFloat();
      if (v > 0.1 && v < 1.0) {
        DIA_RATIO = v;
        Serial.printf("DIA_RATIO set to %.3f\n", DIA_RATIO);
      } else Serial.println("Invalid DIA value (0.1-1.0)");
    }
    else if (cmd == "dump") {
      dumpSamplesNextRun = true;
      Serial.println("Next measurement will dump sample CSV.");
    }
    else if (cmd == "params") {
      Serial.printf("SYS_RATIO=%.3f DIA_RATIO=%.3f dumpNext=%d\n", SYS_RATIO, DIA_RATIO, dumpSamplesNextRun ? 1 : 0);
    }
  }
}

// ====================== I2C FUNCTIONS ======================
void i2c_init() {
  Wire.setClock(50000);   // 50kHz - ổn định với cảm biến này
  Wire.begin();
  Serial.println("I2C initialized at 50kHz");
}

void i2c_recovery() {
  Serial.println("[RECOVERY] Đang khôi phục bus I2C...");
  Wire.end();
  delay(100);
  i2c_init();
  delay(200);
}
// Helper: Wire.requestFrom with small retry+timeout, returns available byte count
int requestFromWithRetry(uint8_t addr, uint8_t numBytes, uint8_t retries, uint16_t timeoutMs)
{
  for (uint8_t r = 0; r < retries; r++) {
    Wire.requestFrom(addr, numBytes);
    unsigned long start = millis();
    while (Wire.available() < numBytes && (millis() - start) < timeoutMs) {
      delay(1);
    }
    uint8_t avail = Wire.available();
    if (avail >= numBytes) return avail;
    // flush and retry
    while (Wire.available()) Wire.read();
    delay(5);
  }
  return Wire.available();
}

// ====================== ĐỌC ÁP SUẤT (tích hợp từ AGR12_pressure_sensor) ======================
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw) {
  pressure_kPa = 0.0f;
  pressure_mmHg = 0.0f;
  raw = 0;

  for (uint8_t attempt = 0; attempt < MAX_RETRIES; attempt++) {
    // send measure command
    Wire.beginTransmission(AGR12_I2C_ADDRESS);
    Wire.write(CMD_MEASURE_HIGH);
    Wire.write(CMD_MEASURE_LOW);
    uint8_t txErr = Wire.endTransmission();

    if (txErr != 0) {
      Serial.printf("[Attempt %d/%d] I2C TX error: %d\n", attempt + 1, MAX_RETRIES, txErr);
      if (attempt >= 2) i2c_recovery();
      delay(100);
      continue;
    }

    // wait for sensor
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
      // some modules return 3 bytes: DATA0, DATA1, CRC
      data0 = Wire.read();
      data1 = Wire.read();
      crc = Wire.read();
      noData = 0;
      Serial.println("Note: Received 3-byte frame (DATA0,DATA1,CRC)");
    } else {
      Serial.printf("[Attempt %d] Received %d bytes (expected %d)\n", attempt+1, count, expectedBytes);
      while (Wire.available()) Wire.read();
      if (attempt >= 2) i2c_recovery();
      delay(20);
      continue;
    }

    // Some sensors send a leading byte; handle frames where noData != 0xFF
    if (noData != 0xFF && count == expectedBytes) {
      data1 = noData;
      data0 = 0x00;
      crc = data0 ^ data1;
      noData = 0xFF;
    }

    uint8_t crc_xor = data0 ^ data1;
    if (crc != crc_xor) {
      Serial.printf("CRC mismatch: got 0x%02X expected 0x%02X\n", crc, crc_xor);
      // if we see bus pull-up/no-response patterns, try recovery
      if (crc == 0xFF || crc == 0x00 || data0 == 0xFF || data1 == 0xFF) {
        Serial.println("Detected 0xFF/0x00 in frame — possible bus issue; recovering");
        i2c_recovery();
        delay(20);
        continue;
      }
      delay(10);
      continue;
    }

    // valid frame
    raw = (int16_t)((data0 << 8) | data1);
    pressure_kPa = raw / 10.0f;
    pressure_mmHg = pressure_kPa * 7.5006f;

    currentDelay = MEASURE_DELAY_MIN;

    Serial.printf("OK → Raw: %d | %.1f kPa | %.1f mmHg\n", raw, pressure_kPa, pressure_mmHg);
    return true;
  }

  // all attempts failed
  if (currentDelay < MEASURE_DELAY_MAX) currentDelay += 50;
  Serial.printf("Đọc cảm biến thất bại sau %d lần thử!\n", MAX_RETRIES);
  return false;
}

// ====================== ĐIỀU KHIỂN BƠM & VAN ======================
void startPump(int speed) {                 
  ledcWrite(ENA, constrain(speed, 0, 255));
}

void stopPump() {
  // stop PWM but keep direction pin state (fixed forward direction)
  ledcWrite(ENA, 0);
}

void openValve(int speed) {                 
  ledcWrite(ENB, constrain(speed, 0, 255));
}

void closeValve() {
  // stop PWM but keep direction pin state (fixed open direction)
  ledcWrite(ENB, 0);
}

void stopAll() {
  stopPump();
  closeValve();
}
// (statistics removed)

// ====================== ĐO HUYẾT ÁP ======================
void measureBloodPressure() {
  Serial.println("\n=== BẮT ĐẦU ĐO HUYẾT ÁP ===");

  stopAll();

  float pressure_kPa = 0.0;
  float pressure_mmHg = 0.0;
  int16_t raw = 0;

  sampleCount = 0;

  // ================== BƠM ==================
  Serial.println("Đang bơm lên 180 mmHg...");
  startPump(243);
  closeValve();

  unsigned long startTime = millis();

  while (pressure_mmHg < 180.0) {
    if (millis() - startTime > 20000) {
      Serial.println("Timeout bơm!");
      openValve(220);
      delay(4000);
      stopAll();
      return;
    }

    if (readPressure(pressure_kPa, pressure_mmHg, raw)) {
      if (pressure_mmHg < 0 || pressure_mmHg > 300) continue;
    }

    delay(30);
  }

  stopPump();
  Serial.printf("→ Đạt %.1f mmHg. Bắt đầu xả chậm...\n", pressure_mmHg);

  // ================== XẢ CHẬM + LẤY MẪU ==================
  openValve(40);

  float filtered = pressure_mmHg;  // DC filter

  unsigned long deflateStart = millis();

  while (pressure_mmHg > 50.0 && sampleCount < MAX_SAMPLES) {
    if (millis() - deflateStart > 35000) {
      Serial.println("Timeout xả!");
      break;
    }

    if (!readPressure(pressure_kPa, pressure_mmHg, raw)) continue;

    // ===== LOW PASS FILTER (DC) =====
    filtered = 0.9 * filtered + 0.1 * pressure_mmHg;

    // ===== DAO ĐỘNG (AC) =====
    float osc = pressure_mmHg - filtered;

    pressureArr[sampleCount] = pressure_mmHg;
    oscArr[sampleCount] = abs(osc);
    oscSignedArr[sampleCount] = osc;
    timeArr[sampleCount] = millis();

    sampleCount++;

    delay(40); // ~25Hz sampling
  }

  // ================== XẢ NHANH ==================
  Serial.println("Xả nhanh phần còn lại...");
  openValve(245);

  while (pressure_mmHg > 5) {
    readPressure(pressure_kPa, pressure_mmHg, raw);
    delay(30);
  }

  stopAll();

  // ================== XỬ LÝ DỮ LIỆU ==================
  if (sampleCount < 50) {
    Serial.println("Không đủ dữ liệu!");
    return;
  }
  // ===== BUILD A SMOOTHED ENVELOPE =====
  float env[MAX_SAMPLES];
  float envSm[MAX_SAMPLES];
  const int envWin = 4; // window half-width for local max envelope
  // ignore a few initial samples to avoid transient spikes right after deflation starts
  const int IGNORE_FIRST = 3;
  int procStart = min(IGNORE_FIRST, max(0, sampleCount - 1));

  for (int i = 0; i < sampleCount; i++) {
    float m = 0.0f;
    int lo = max(0, i - envWin);
    int hi = min(sampleCount - 1, i + envWin);
    for (int j = lo; j <= hi; j++) {
      if (oscArr[j] > m) m = oscArr[j];
    }
    env[i] = m;
  }

  // simple moving average to smooth envelope
  const int smoothN = 5;
  for (int i = 0; i < sampleCount; i++) {
    float s = 0.0f;
    int cnt = 0;
    int lo = max(0, i - smoothN / 2);
    int hi = min(sampleCount - 1, i + smoothN / 2);
    for (int j = lo; j <= hi; j++) { s += env[j]; cnt++; }
    envSm[i] = (cnt > 0) ? (s / cnt) : env[i];
  }

  // ===== FIND MAP FROM SMOOTHED ENVELOPE =====
  float maxEnv = 0.0f;
  int maxIndex = 0;
  for (int i = procStart; i < sampleCount; i++) {
    if (envSm[i] > maxEnv) { maxEnv = envSm[i]; maxIndex = i; }
  }
  float MAP = pressureArr[maxIndex];

  // ===== FIND SYS and DIA via height-based threshold band (midpoint crossing) =====
  float SYS = 0.0f;
  float DIA = 0.0f;

  // determine target ratios: prefer runtime overrides if set sensibly
  float sysTargetRatio = (SYS_RATIO > 0.1f && SYS_RATIO < 1.0f) ? SYS_RATIO : (SYS_MIN_RATIO + SYS_MAX_RATIO) / 2.0f;
  float diaTargetRatio = (DIA_RATIO > 0.1f && DIA_RATIO < 1.0f) ? DIA_RATIO : (DIA_MIN_RATIO + DIA_MAX_RATIO) / 2.0f;
  float sysTarget = sysTargetRatio * maxEnv;
  float diaTarget = diaTargetRatio * maxEnv;

  // Print debug targets (helps tuning)
  Serial.printf("DEBUG: maxEnv=%.4f maxIndex=%d sysTarget=%.4f diaTarget=%.4f\n", maxEnv, maxIndex, sysTarget, diaTarget);

  // find SYS: search backward from MAP to locate the crossing near MAP
  int idx = -1;
  for (int i = maxIndex - 1; i >= procStart; i--) {
    if (envSm[i] < sysTarget) { idx = i; break; }
  }
  if (idx < 0) {
    // fallback: find first >= sysTarget from start (old behavior)
    for (int i = procStart; i <= maxIndex; i++) { if (envSm[i] >= sysTarget) { idx = i; break; } }
    if (idx <= 0) {
      SYS = pressureArr[0];
    } else {
      float y0 = (idx-1 >= 0) ? envSm[idx-1] : envSm[idx];
      float y1 = envSm[idx];
      float p0 = (idx-1 >= 0) ? pressureArr[idx-1] : pressureArr[idx];
      float p1 = pressureArr[idx];
      float t = (y1 - y0) != 0.0f ? (sysTarget - y0) / (y1 - y0) : 0.0f;
      if (!isfinite(t)) t = 0.0f;
      t = constrain(t, 0.0f, 1.0f);
      SYS = p0 + t * (p1 - p0);
    }
  } else {
    // crossing between idx and idx+1
    int i0 = idx;
    int i1 = min(sampleCount - 1, idx + 1);
    float y0 = envSm[i0];
    float y1 = envSm[i1];
    float p0 = pressureArr[i0];
    float p1 = pressureArr[i1];
    float t = (y1 - y0) != 0.0f ? (sysTarget - y0) / (y1 - y0) : 0.0f;
    if (!isfinite(t)) t = 0.0f;
    t = constrain(t, 0.0f, 1.0f);
    SYS = p0 + t * (p1 - p0);
  }

  // find DIA: search forward from MAP to find first crossing below diaTarget
  idx = -1;
  for (int i = maxIndex + 1; i < sampleCount; i++) {
    if (envSm[i] < diaTarget) { idx = i; break; }
  }
  if (idx < 0) {
    // fallback: last index at or after MAP
    DIA = pressureArr[min(sampleCount - 1, maxIndex)];
  } else {
    int i1 = idx;
    int i0 = max(procStart, idx - 1);
    float y0 = envSm[i0];
    float y1 = envSm[i1];
    float p0 = pressureArr[i0];
    float p1 = pressureArr[i1];
    float t = (y1 - y0) != 0.0f ? (diaTarget - y0) / (y1 - y0) : 0.0f;
    if (!isfinite(t)) t = 0.0f;
    t = constrain(t, 0.0f, 1.0f);
    DIA = p0 + t * (p1 - p0);
  }

  // Fallback sanity clamps
  if (!(SYS > 0 && SYS < 500)) SYS = 0.0f;
  if (!(DIA > 0 && DIA < 500)) DIA = 0.0f;

  // ===== BPM CALCULATION =====
  int peakIdx[MAX_SAMPLES];
  int peakCount = 0;
  // adaptive threshold: avoid tiny absolute thresholds on quiet traces
  float peakThreshold = max(0.08f * maxEnv, 0.4f);
  for (int i = 1; i < sampleCount - 1; i++) {
    if (oscSignedArr[i] > oscSignedArr[i-1] && oscSignedArr[i] >= oscSignedArr[i+1] && oscArr[i] > peakThreshold) {
      peakIdx[peakCount++] = i;
      if (peakCount >= MAX_SAMPLES) break;
    }
  }

  float BPM = 0.0f;
  if (peakCount >= 2) {
    // build intervals (ms) and ignore unusually short intervals (<300ms)
    double intervalsArr[MAX_SAMPLES];
    int intervalsCnt = 0;
    const unsigned long minIntervalMs = 300; // reject beats faster than 200 BPM
    for (int k = 1; k < peakCount; k++) {
      unsigned long t0 = timeArr[peakIdx[k-1]];
      unsigned long t1 = timeArr[peakIdx[k]];
      if (t1 > t0) {
        unsigned long dt = t1 - t0;
        if (dt >= minIntervalMs) {
          intervalsArr[intervalsCnt++] = (double)dt;
        }
      }
    }
    if (intervalsCnt > 0) {
      // simple selection sort to find median
      for (int a = 0; a < intervalsCnt - 1; a++) {
        int mi = a;
        for (int b = a + 1; b < intervalsCnt; b++) if (intervalsArr[b] < intervalsArr[mi]) mi = b;
        if (mi != a) { double tmp = intervalsArr[a]; intervalsArr[a] = intervalsArr[mi]; intervalsArr[mi] = tmp; }
      }
      double medianMs;
      if (intervalsCnt % 2 == 1) medianMs = intervalsArr[intervalsCnt/2];
      else medianMs = 0.5 * (intervalsArr[intervalsCnt/2 - 1] + intervalsArr[intervalsCnt/2]);
      double b = 60000.0 / medianMs;
      if (isfinite(b) && b > 30.0 && b < 220.0) BPM = (float)b;
    }
  }

  // ================== IN KẾT QUẢ ==================
  // If requested, dump samples CSV for offline analysis
  if (dumpSamplesNextRun) {
    Serial.println("index,pressure_mmHg,env,osc");
    for (int i = 0; i < sampleCount; i++) {
      Serial.printf("%d,%.2f,%.4f,%.4f\n", i, pressureArr[i], envSm[i], oscArr[i]);
    }
    dumpSamplesNextRun = false;
    Serial.println("-- end dump --");
  }
  Serial.printf("Using SYS band=%.2f-%.2f DIA band=%.2f-%.2f (targets: %.3f, %.3f)\n",
                SYS_MIN_RATIO, SYS_MAX_RATIO, DIA_MIN_RATIO, DIA_MAX_RATIO, sysTargetRatio, diaTargetRatio);
  Serial.printf("(maxEnv=%.4f maxIndex=%d)\n", maxEnv, maxIndex);
  Serial.println("\n===== KẾT QUẢ =====");
  Serial.printf("SYS: %.1f mmHg\n", SYS);
  Serial.printf("DIA: %.1f mmHg\n", DIA);
  Serial.printf("MAP: %.1f mmHg\n", MAP);
  if (isfinite(BPM) && BPM > 0.0f) Serial.printf("BPM: %.1f\n", BPM);
  else Serial.println("BPM: N/A");
  Serial.println("====================\n");
}