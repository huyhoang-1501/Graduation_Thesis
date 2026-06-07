#include "HR_SPO2_BP.h"

MAX30105 particleSensor;

#define MAX301_I2C_ADDR 0x57 // 7-bit I2C address for MAX30102/05

#define BUFFER_SIZE 100

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t bufferLength = BUFFER_SIZE;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// last BP results exposed to other modules
float lastSYS = 0.0f;
float lastDIA = 0.0f;

// last measurement origin (BP_ORIGIN_*)
volatile int lastBPOrigin = BP_ORIGIN_NONE;

// origin marker for the BP run about to be started
static volatile int bpOriginBeforeStart = BP_ORIGIN_NONE;

// ========== SMOOTHING & LED CONTROL FOR MAX301 ==========
const float HR_EMA_ALPHA = 0.28f;    // lower = smoother, higher = more responsive
const float SPO2_EMA_ALPHA = 0.45f;  // increased to make SpO2 respond faster
float emaHr = 0.0f;
float emaSpO2 = 0.0f;
// Display deadbands to avoid jitter
const float HR_DISPLAY_DEADBAND = 0.6f;   // bpm
const float SPO2_DISPLAY_DEADBAND = 0.1f; // percent
float lastPrintedHr = 0.0f;
float lastPrintedSpO2 = 0.0f;
// Display interval (matching existing behavior)
const unsigned long DISPLAY_INTERVAL_MS = 5000UL;

// LED amplitude (single value to save RAM)
uint8_t ledAmpVal = 60; // initial LED amplitude (approx 60)

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

// Pump/pressure targets and timeouts
const float TARGET_PRESSURE_MMHG = 180.0f;           // desired inflation pressure
const float MIN_PROCEED_PRESSURE_MMHG = 160.0f;     // if target not reached, allow proceed if >= this
const unsigned long PUMP_TIMEOUT_MS = 30000UL;     // timeout for pump inflation (ms)
// Final deflation target (was 5 mmHg) - set to 5 mmHg to match original .ino behavior
const float FINAL_DEFLATION_MMHG = 5.0f;

#define MAX_SAMPLES 300

// Store scaled integer arrays to save DRAM on devices without PSRAM
// pressureArr: store mmHg * 10 (0.1 mmHg resolution) in int16_t
// oscArr / oscSignedArr: store values * 1000 in int16_t
int16_t pressureArr[MAX_SAMPLES];
int16_t oscArr[MAX_SAMPLES];
int16_t oscSignedArr[MAX_SAMPLES];
unsigned long timeArr[MAX_SAMPLES];
int sampleCount = 0;

// Large temporary arrays moved to static memory to avoid stack pressure
static float env[MAX_SAMPLES];
static float envSm[MAX_SAMPLES];
static int peakIdx[MAX_SAMPLES];
// intervals for BPM calculation (ms)
static float intervalsArr[MAX_SAMPLES];

// Adaptive delay
uint16_t currentDelay = MEASURE_DELAY_MIN;

// Tunable algorithm parameters (adjust at runtime via serial)
float SYS_RATIO = 0.5f;  // increased to move SYS detection closer to MAP
float DIA_RATIO = 0.7f;  // tuned for this trace
bool dumpSamplesNextRun = false; // if true, measurement will print CSV of samples

// Early-accept settings: if we have this many samples after MAP, skip final rapid deflation
const int MIN_POST_MAP_SAMPLES = 5;

// Height-based threshold ranges for SYS/DIA (use midpoint for crossing)
const float SYS_MIN_RATIO = 0.40f; // 40% of max envelope
const float SYS_MAX_RATIO = 0.60f; // 60% of max envelope
const float DIA_MIN_RATIO = 0.65f; // 65% of max envelope
const float DIA_MAX_RATIO = 0.85f; // 85% of max envelope

// ================== PROTOTYPES (internal) ==================
void startPump(int speed = 200);
void stopPump();
void openValve(int speed = 60);
void closeValve();
void stopAll();
void measureBloodPressure();
void processOscillometric();
void i2c_recovery();
void i2c_init();
int requestFromWithRetry(uint8_t addr, uint8_t numBytes, uint8_t retries, uint16_t timeoutMs);
void Max30102_hr_spo2();

// MAX301 task forward declaration and handle (created at setup)
static TaskHandle_t max301TaskHandle = NULL;
static void max301_task_entry(void *pvParameters);

// ====================== SETUP (migrated) ======================
void hrspo2bp_setup() {
  Serial.begin(115200);
  delay(1000);

  // Using only PWM enable pins for speed control (ENA/ENB).
  // Direction inputs removed; driver should be wired for fixed forward direction.
  ledcAttach(ENA, pwmFreq, pwmRes);
  ledcAttach(ENB, pwmFreq, pwmRes);

  i2c_init();

  // Initialize MAX301 sensor (optional)
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX301 not found (or not initialized). MAX mode will be unavailable.");
  } else {
    Serial.println("MAX301 initialized");
    particleSensor.setup();
    // initialise amplitudes
    particleSensor.setPulseAmplitudeRed(ledAmpVal);
    particleSensor.setPulseAmplitudeIR(ledAmpVal);
  }

   Serial.println("=== HỆ THỐNG ĐO HUYẾT ÁP AGR12 ĐÃ KHỞI ĐỘNG ===");
  // Serial.println("Gõ 'start' để bắt đầu đo\n");

  stopAll();

  // start MAX301 sampling task (non-blocking)
  if (max301TaskHandle == NULL) {
    xTaskCreate(max301_task_entry, "MAX301", 4096, NULL, 1, &max301TaskHandle);
  }
}

// ====================== LOOP (migrated) ======================
void hrspo2bp_loop() {
  static bool bpInProgress = false;
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "start") {
        // trigger AGR12 blood pressure measurement on demand (run async to avoid blocking)
        if (!isBPMeasuring()) {
          startMeasureBloodPressureAsync();
          Serial.println("BP measurement started (async)...");
        } else {
          Serial.println("BP measurement already in progress...");
        }
    }
    else if (cmd == "stop") {
      // stop pumps/valve (if BP in progress)
      stopAll();
      Serial.println("Đã dừng tất cả.");
    }
    else if (cmd == "u") {
      // increase LED amplitude (step)
      ledAmpVal = (uint8_t)min(255, ledAmpVal + 30);
      particleSensor.setPulseAmplitudeRed(ledAmpVal);
      particleSensor.setPulseAmplitudeIR(ledAmpVal);
      Serial.print("LED amplitude increased to "); Serial.println(ledAmpVal);
    }
    else if (cmd == "d") {
      // decrease LED amplitude (step)
      ledAmpVal = (uint8_t)max(0, ledAmpVal - 30);
      particleSensor.setPulseAmplitudeRed(ledAmpVal);
      particleSensor.setPulseAmplitudeIR(ledAmpVal);
      Serial.print("LED amplitude decreased to "); Serial.println(ledAmpVal);
    }
    else {
      Serial.println("Unknown command. Use 'start' to measure BP or 'stop' to stop pumps.");
    }

    // flush any extra serial characters to avoid repeated 'start' triggers
    while (Serial.available()) Serial.read();
  }

  // NOTE: MAX301 continuous sampling runs in its own FreeRTOS task to avoid blocking UI.
}

// ===== MAX301 sampling task (runs separately so UI is not blocked) =====
static void max301_task_entry(void *pvParameters) {
  (void)pvParameters;
  unsigned long lastDisplay = 0;
  while (1) {
    Max30102_hr_spo2();

    // Basic validity checks
    bool hrValid = (heartRate >= 30 && heartRate <= 220);
    bool spO2Valid = (spo2 >= 50 && spo2 <= 100);

    // update EMA values when valid
    if (hrValid) {
      if (emaHr <= 0.0f) emaHr = heartRate;
      else emaHr = HR_EMA_ALPHA * heartRate + (1.0f - HR_EMA_ALPHA) * emaHr;
    }
    if (spO2Valid) {
      if (emaSpO2 <= 0.0f) emaSpO2 = spo2;
      else emaSpO2 = SPO2_EMA_ALPHA * spo2 + (1.0f - SPO2_EMA_ALPHA) * emaSpO2;
    }

    unsigned long now = millis();
    if (now - lastDisplay >= DISPLAY_INTERVAL_MS) {
      lastDisplay = now;

      float displayHr = (emaHr > 0.0f) ? emaHr : (hrValid ? (float)heartRate : 0.0f);
      float displaySpO2 = (emaSpO2 > 0.0f) ? emaSpO2 : (spO2Valid ? (float)spo2 : 0.0f);

      // Apply deadband
      if (displayHr > 0.0f && fabs(displayHr - lastPrintedHr) < HR_DISPLAY_DEADBAND) displayHr = lastPrintedHr;
      if (displaySpO2 > 0.0f && fabs(displaySpO2 - lastPrintedSpO2) < SPO2_DISPLAY_DEADBAND) displaySpO2 = lastPrintedSpO2;

      Serial.print("HR = ");
      if (displayHr >= 30.0f) {
        Serial.print(displayHr, 1);
        lastPrintedHr = displayHr;
      } else Serial.print("--");
      Serial.print(" bpm | SpO2 = ");
      if (displaySpO2 >= 50.0f && displaySpO2 <= 100.0f) {
        Serial.print(displaySpO2, 1);
        lastPrintedSpO2 = displaySpO2;
      } else Serial.print("--");
      Serial.print(" %   LED:"); Serial.print(ledAmpVal);
      Serial.println();

      if ((displayHr < 30.0f) && (spo2 >= 50.0 && spo2 <= 100.0)) {
        Serial.println("Warning: HR invalid but SpO2 OK. Check finger placement and contact.");
      }
    }

    // small delay to yield CPU and avoid tight-looping
    vTaskDelay(pdMS_TO_TICKS(50));
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

// ---- Constants borrowed from max_agr.ino
#define NO_FINGER_THRESHOLD 10000


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

// ===== MAX30102: HR + SpO2 measurement (single-run helper) =====
void Max30102_hr_spo2()
{
  // Adapted from max_agr.ino: collect BUFFER_SIZE samples, detect finger, then run algorithm
  uint64_t sumIR = 0;
  uint64_t sumRed = 0;
  uint32_t minIR = 0xFFFFFFFF;
  uint32_t maxIR = 0;
  uint32_t minRed = 0xFFFFFFFF;
  uint32_t maxRed = 0;

  int idx = 0;

  unsigned long startTimeout;
  while (idx < BUFFER_SIZE) {
    particleSensor.check();
    startTimeout = millis();
    unsigned long sampleStart = millis();
    // wait a short while for a sample to become available
    while (!particleSensor.available() && (millis() - sampleStart) < 200) {
      particleSensor.check();
      delay(1);
    }
    if (!particleSensor.available()) {
      // timeout waiting for sample
      Serial.println("MAX301: no sample available (timeout)");
      return;
    }

    redBuffer[idx] = particleSensor.getRed();
    irBuffer[idx] = particleSensor.getIR();

    sumIR += irBuffer[idx];
    sumRed += redBuffer[idx];

    if (irBuffer[idx] < minIR) minIR = irBuffer[idx];
    if (irBuffer[idx] > maxIR) maxIR = irBuffer[idx];
    if (redBuffer[idx] < minRed) minRed = redBuffer[idx];
    if (redBuffer[idx] > maxRed) maxRed = redBuffer[idx];

    idx++;
    particleSensor.nextSample();
  }

  uint32_t avgIR = (uint32_t)(sumIR / BUFFER_SIZE);
  uint32_t avgRed = (uint32_t)(sumRed / BUFFER_SIZE);

  if (avgIR < NO_FINGER_THRESHOLD) {
    Serial.println("No finger");
    delay(100);
    return;
  }

  Serial.printf("IR=%d RED=%d\n", (int)avgIR, (int)avgRed);

  // run Maxim algorithm to populate global result variables
  maxim_heart_rate_and_oxygen_saturation(
    irBuffer,
    bufferLength,
    redBuffer,
    &spo2,
    &validSPO2,
    &heartRate,
    &validHeartRate
  );

  // Do not update EMA here - that is performed by the MAX301 task loop to keep timing consistent
}

// ===== Background BP task support (non-blocking wrapper) =====
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FirebaseSync.h"

static TaskHandle_t bpTaskHandle = NULL;

static void bp_task_entry(void *pvParameters) {
  (void)pvParameters;
  // call the blocking measurement
  measureBloodPressure();

  // After finishing a BP run, queue a sync immediately so /latest and /history
  // get a fresh timestamped record as soon as results are available.
  FirebaseSync_PushMeasurementNow();
  // mark task as finished
  bpTaskHandle = NULL;
  vTaskDelete(NULL);
}

void startMeasureBloodPressureAsync() {
  if (bpTaskHandle != NULL) return; // already running
  // create task with larger stack size and low priority to avoid stack overflow
  xTaskCreate(bp_task_entry, "BPTask", 8192, NULL, 1, &bpTaskHandle);
}

void startMeasureBloodPressureAsyncForOrigin(int origin) {
  bpOriginBeforeStart = origin;
  startMeasureBloodPressureAsync();
}

bool isBPMeasuring() {
  return bpTaskHandle != NULL;
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

// ====================== ĐO HUYẾT ÁP ======================
void measureBloodPressure() {
  Serial.println("Bat dau do huyet ap");

  float pressure_kPa = 0.0f;
  float pressure_mmHg = 0.0f;
  int16_t raw = 0;

  stopAll();

  // start pump and inflate
  startPump(248);
  closeValve();

  unsigned long startTime = millis();
  while (pressure_mmHg < TARGET_PRESSURE_MMHG) {
    // timeout fallback like original code: if exceeded, allow proceed if above minimum
    if (millis() - startTime > PUMP_TIMEOUT_MS) {
      Serial.println("Timeout bơm!");
      if (pressure_mmHg >= MIN_PROCEED_PRESSURE_MMHG) {
        Serial.printf("Reached %.1f mmHg which is >= %.1f mmHg (min). Proceeding...\n", pressure_mmHg, MIN_PROCEED_PRESSURE_MMHG);
        stopPump();
        break;
      } else {
        Serial.println("Inflation insufficient (< minimum). Aborting measurement.");
        openValve(220);
        delay(4000);
        stopAll();
        return;
      }
    }

    readPressure(pressure_kPa, pressure_mmHg, raw);
    delay(40);
  }

  stopPump();
  Serial.println("Xa cham");
  openValve(45);

  sampleCount = 0;
  float prev = pressure_mmHg;
  float y_prev = 0.0f;
  const float alpha = 0.95f;

  while (pressure_mmHg > 45.0f && sampleCount < MAX_SAMPLES) {
    if (readPressure(pressure_kPa, pressure_mmHg, raw)) {
      // store scaled integer representations to save RAM
      pressureArr[sampleCount] = (int16_t)constrain((int)roundf(pressure_mmHg * 10.0f), -32768, 32767);
      float y = alpha * y_prev + pressure_mmHg - prev;
      oscArr[sampleCount] = (int16_t)constrain((int)roundf(fabsf(y) * 1000.0f), 0, 32767);
      oscSignedArr[sampleCount] = (int16_t)constrain((int)roundf(y * 1000.0f), -32768, 32767);
      timeArr[sampleCount] = millis();
      y_prev = y;
      prev = pressure_mmHg;
      sampleCount++;
    }
    delay(50);
  }

  // process oscillometric envelope and compute SYS/DIA
  processOscillometric();

  // rapid deflate to safe level
  Serial.println("Xa nhanh");
  openValve(245);
  while (pressure_mmHg > FINAL_DEFLATION_MMHG) {
    readPressure(pressure_kPa, pressure_mmHg, raw);
    delay(10);
  }

  stopAll();
  Serial.println("Done");
}


// Process oscillometric data (ported from max_agr.ino)
void processOscillometric()
{
  if (sampleCount < 20) {
    Serial.println("Khong du data");
    return;
  }

  static float ampBuf[MAX_SAMPLES];
  static float cuffBuf[MAX_SAMPLES];
  int ampCount = 0;

  for (int i = 1; i < sampleCount - 1; i++) {
    // oscArr currently stored as scaled int (value*1000)
    if (oscArr[i] > oscArr[i-1] && oscArr[i] > oscArr[i+1]) {
      // find preceding local minimum
      for (int j = i - 1; j >= 1; j--) {
        if (oscArr[j] < oscArr[j-1] && oscArr[j] < oscArr[j+1]) {
          // convert back to float with proper scaling
          ampBuf[ampCount] = fabsf((float)(oscArr[i] - oscArr[j]) / 1000.0f);
          cuffBuf[ampCount] = (float)pressureArr[i] / 10.0f; // convert back to mmHg
          ampCount++;
          break;
        }
      }
    }
  }

  if (ampCount < 5) {
    Serial.println("Khong tim thay dao dong");
    return;
  }

  Serial.println("\n===== ENVELOPE =====");
  for (int i = 0; i < ampCount; i++) Serial.printf("%d %.1f %.4f\n", i, cuffBuf[i], ampBuf[i]);

  float maxAmp = 0.0f;
  int mapIndex = 0;
  for (int i = 0; i < ampCount; i++) {
    if (ampBuf[i] > maxAmp) { maxAmp = ampBuf[i]; mapIndex = i; }
  }

  float MAP = cuffBuf[mapIndex];

  float SYS = 0.0f;
  float bestErr = 9999.0f;
  for (int i = 0; i < mapIndex; i++) {
    float err = fabs(ampBuf[i] - 0.7f * maxAmp);
    if (err < bestErr) { bestErr = err; SYS = cuffBuf[i]; }
  }

  // Fallback: if we didn't find a sensible SYS (e.g., SYS==0), pick a reasonable neighbor
  if (!(SYS > 0.0f && SYS < 500.0f)) {
    int fallbackIdx = max(0, mapIndex - 1);
    if (fallbackIdx < ampCount) {
      SYS = cuffBuf[fallbackIdx];
      Serial.printf("Fallback SYS from idx %d -> %.1f\n", fallbackIdx, SYS);
    }
  }

  float DIA = 0.0f;
  bestErr = 9999.0f;
  for (int i = mapIndex + 1; i < ampCount; i++) {
    float err = fabs(ampBuf[i] - 0.8f * maxAmp);
    if (err < bestErr) { bestErr = err; DIA = cuffBuf[i]; }
  }

  // Fallback for DIA
  if (!(DIA > 0.0f && DIA < 500.0f)) {
    int fallbackIdx = min(ampCount - 1, mapIndex + 1);
    if (fallbackIdx >= 0 && fallbackIdx < ampCount) {
      DIA = cuffBuf[fallbackIdx];
      Serial.printf("Fallback DIA from idx %d -> %.1f\n", fallbackIdx, DIA);
    }
  }

  Serial.println("\n===== RESULT =====");
  Serial.printf("ampCount=%d mapIndex=%d maxAmp=%.4f\n", ampCount, mapIndex, maxAmp);
  Serial.printf("SYS = %.1f\n", SYS);
  Serial.printf("DIA = %.1f\n", DIA);
  Serial.printf("MAP = %.1f\n", MAP);

  // publish results for UI
  lastSYS = SYS;
  lastDIA = DIA;
  // mark who started this measurement
  lastBPOrigin = bpOriginBeforeStart;
  bpOriginBeforeStart = BP_ORIGIN_NONE;
}
