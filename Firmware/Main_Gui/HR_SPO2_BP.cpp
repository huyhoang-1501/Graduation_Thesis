#include "HR_SPO2_BP.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "FirebaseSync.h"
#include "sim_module.h"
#include <DFRobotDFPlayerMini.h>

MAX30105 particleSensor;

#define MAX301_I2C_ADDR 0x57

#define BUFFER_SIZE 100

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;
int32_t bufferLength = BUFFER_SIZE;

float hr;
float s;

// last BP results exposed to other modules
float lastSYS = 0.0f;
float lastDIA = 0.0f;

volatile int lastBPOrigin = BP_ORIGIN_NONE;
static volatile int bpOriginBeforeStart = BP_ORIGIN_NONE;
static volatile bool bpCancelRequested = false;

// ========== EMA (matching .ino exactly) ==========
#define EMA_ALPHA       0.08f
#define EMA_ALPHA_HR    0.45f
#define EMA_ALPHA_SPO2  0.75f

float emaIR = 0, emaRed = 0;
float emaHR = 0, emaSPO2 = 0;

bool emaIRInit = false;
bool emaRedInit = false;
bool emaHRInit = false;
bool emaSPO2Init = false;

float applyEMA(float value, float &ema, float alpha, bool &init)
{
  if (!init) {
    ema = value;
    init = true;
    return ema;
  }
  ema = alpha * value + (1 - alpha) * ema;
  return ema;
}

// ================== WARNING SYSTEM ======================
// Thresholds (matching .ino defaults)
int g_spo2_min = 92;
int g_spo2_max = 100;
int g_hr_min   = 55;
int g_hr_max   = 130;
int g_sys_min  = 90;
int g_sys_max  = 150;
int g_dia_min  = 55;
int g_dia_max  = 110;

char g_phone[32] = "0365089063";

// Warning counters
volatile int hr_warning = 0;
volatile int spo2_warning = 0;
volatile int mode1_warning = 0;
volatile int mode2_warning = 0;

volatile unsigned long time_incr_warn = 0;

// ================== PUMP + VALVE ==================
const int ENA = 32;
const int ENB = 33;

const int pwmFreq = 1000;
const int pwmRes  = 8;

// ================== AGR12 ==================
const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH  = 0xAC;
const uint8_t CMD_MEASURE_LOW   = 0x12;
const int WAIT_TIME_MS = 40;

#define MAX_SAMPLES 300

float cuff[MAX_SAMPLES];
float osc[MAX_SAMPLES];
float ampBuf[MAX_SAMPLES];

int sampleCount = 0;

// ================== PROTOTYPES (internal) ==================
void startPump(int speed = 200);
void stopPump();
void openValve(int speed = 80);
void closeValve();
void stopAll();
void measureBloodPressure();
void processOscillometric();
void Max30102_hr_spo2();

// FreeRTOS task handles
static TaskHandle_t max301TaskHandle = NULL;
static TaskHandle_t bpTaskHandle = NULL;
static void max301_task_entry(void *pvParameters);
static void bp_task_entry(void *pvParameters);

// DFPlayer (external reference from Main_Gui)
extern DFRobotDFPlayerMini dfPlayer;
extern bool dfPlayerReady;
extern bool g_alert_sound_playing;

// GPS location (external reference from Main_Gui.ino)
extern double g_lastGpsLat;
extern double g_lastGpsLng;
extern bool g_hasGpsLocation;

// Cooldown to prevent repeated alert triggers
static unsigned long g_alert_cooldown_ms = 0;
static const unsigned long ALERT_COOLDOWN_PERIOD_MS = 60000; // 60 seconds between alerts

// ====================== FREE RTOS TASK ENTRY POINTS ======================
static void max301_task_entry(void *pvParameters) {
  (void)pvParameters;
  while (1) {
    Max30102_hr_spo2();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

static void bp_task_entry(void *pvParameters) {
  (void)pvParameters;
  // Run the blocking measurement
  measureBloodPressure();
  // After finishing a BP run, queue a sync immediately
  FirebaseSync_PushMeasurementNow();
  // Signal task finished
  bpTaskHandle = NULL;
  vTaskDelete(NULL);
}

// ====================== SETUP ======================
void hrspo2bp_setup() {
  // NOTE: I2C and LEDC must be initialized externally (in main setup)
  // to avoid conflicts with other modules.

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
  } else {
    byte ledBrightness = 32;
    byte sampleAverage = 4;
    byte ledMode = 2;
    byte sampleRate = 200;
    int pulseWidth = 411;
    int adcRange = 4096;
    particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
    Serial.println("MAX30102 ready");
  }

  stopAll();

  // start MAX301 sampling task (non-blocking)
  if (max301TaskHandle == NULL) {
    xTaskCreate(max301_task_entry, "MAX301", 4096, NULL, 1, &max301TaskHandle);
  }
}

void hrspo2bp_loop() {
  // Nothing to do here by default - MAX301 runs in its own task.
  // BP is also managed via FreeRTOS task.
}

// ====================== PWM ======================
void startPump(int speed) {
  ledcWrite(ENA, constrain(speed, 0, 255));
}

void stopPump() {
  ledcWrite(ENA, 0);
}

void openValve(int speed) {
  ledcWrite(ENB, constrain(speed, 0, 255));
}

void closeValve() {
  ledcWrite(ENB, 0);
}

void stopAll() {
  stopPump();
  closeValve();
}

// ====================== READ AGR12 (matching .ino) ======================
bool readPressure(float &kPa, float &mmHg, int16_t &raw) {
  Wire.beginTransmission(AGR12_I2C_ADDRESS);
  Wire.write(CMD_MEASURE_HIGH);
  Wire.write(CMD_MEASURE_LOW);

  if (Wire.endTransmission() != 0)
    return false;

  vTaskDelay(pdMS_TO_TICKS(WAIT_TIME_MS));

  if (Wire.requestFrom(AGR12_I2C_ADDRESS, 4) < 4)
    return false;

  Wire.read(); // dummy

  uint8_t d0 = Wire.read();
  uint8_t d1 = Wire.read();

  Wire.read(); // CRC

  raw = (int16_t)((d0 << 8) | d1);

  kPa = raw / 10.0;
  mmHg = kPa * 7.5006;

  Serial.printf("P = %.1f mmHg\n", mmHg);

  return true;
}

// ====================== MAX30102 (matching .ino) ======================
void Max30102_hr_spo2() {
  uint64_t sumIR = 0;
  uint64_t sumRed = 0;

  uint32_t minIR = 0xFFFFFFFF;
  uint32_t maxIR = 0;

  uint32_t minRed = 0xFFFFFFFF;
  uint32_t maxRed = 0;

  int idx = 0;

  while (idx < BUFFER_SIZE) {
    particleSensor.check();

    if (particleSensor.available()) {
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
    } else
      vTaskDelay(pdMS_TO_TICKS(5));
  }

  uint32_t avgIR = sumIR / BUFFER_SIZE;
  uint32_t avgRed = sumRed / BUFFER_SIZE;

  if (avgIR < 10000)  // NO_FINGER_THRESHOLD
  {
    Serial.println("No finger");
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  maxim_heart_rate_and_oxygen_saturation(
      irBuffer,
      bufferLength,
      redBuffer,
      &spo2,
      &validSPO2,
      &heartRate,
      &validHeartRate);

  if (validHeartRate) {
    hr = applyEMA(heartRate, emaHR, EMA_ALPHA_HR, emaHRInit);
    Serial.printf("HR=%.1f ", hr);
  } else
    Serial.print("HR=- ");

  if (validSPO2) {
    s = applyEMA(spo2, emaSPO2, EMA_ALPHA_SPO2, emaSPO2Init);
    Serial.printf("SpO2=%.1f%%\n", s);
  } else
    Serial.println("SpO2=-");

  // Warning counting (matching .ino)
  if ((hr < g_hr_min) || (hr > g_hr_max)) {
    hr_warning++;
    Serial.printf("hr_warning %d\n", hr_warning);
    time_incr_warn = millis();
  }
  if ((s < g_spo2_min) || (s > g_spo2_max)) {
    spo2_warning++;
    Serial.printf("spo2_warning %d\n", spo2_warning);
    time_incr_warn = millis();
  }
}

// ====================== MEASURE BP (matching .ino) ======================
void measureBloodPressure() {
  float kPa = 0;
  float mmHg = 0;
  int16_t raw = 0;

  Serial.println("Bat dau do huyet ap");

  startPump(240);
  closeValve();

  while (mmHg < 180) {
    readPressure(kPa, mmHg, raw);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  stopPump();

  Serial.println("Xa cham");
  openValve(120);

  sampleCount = 0;

  float prev = mmHg;
  float y_prev = 0;
  float alpha = 0.95;

  while (mmHg > 45) {
    if (readPressure(kPa, mmHg, raw)) {
      cuff[sampleCount] = mmHg;

      float y = alpha * y_prev + mmHg - prev;

      osc[sampleCount] = y;

      y_prev = y;
      prev = mmHg;

      sampleCount++;
    }

    vTaskDelay(pdMS_TO_TICKS(80));
  }

  processOscillometric();

  Serial.println("Xa nhanh");
  openValve(255);

  while (mmHg > 5) {
    readPressure(kPa, mmHg, raw);
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  stopAll();

  Serial.println("Done");

  // Publish results
  // lastSYS/lastDIA are set inside processOscillometric()
  lastBPOrigin = bpOriginBeforeStart;
  bpOriginBeforeStart = BP_ORIGIN_NONE;
}

// ====================== PROCESS OSCILLOMETRIC (matching .ino) ======================
void processOscillometric() {
  if (sampleCount < 20) {
    Serial.println("Khong du data");
    return;
  }

  int ampCount = 0;

  // Find oscillation amplitudes
  // (using global arrays: osc[], cuff[], ampBuf[])
  for (int i = 1; i < sampleCount - 1; i++) {
    if (osc[i] > osc[i - 1] && osc[i] > osc[i + 1]) {
      // valley before peak
      for (int j = i - 1; j >= 1; j--) {
        if (osc[j] < osc[j - 1] && osc[j] < osc[j + 1]) {
          float amp = fabs(osc[i] - osc[j]);

          // filter small noise
          if (amp > 0.5) {
            ampBuf[ampCount] = amp;
            cuff[ampCount]   = cuff[i];
            ampCount++;
          }

          break;
        }
      }
    }
  }

  if (ampCount < 5) {
    Serial.println("Khong tim thay dao dong");
    return;
  }

  // Smooth envelope
  float smoothAmp[MAX_SAMPLES];

  for (int i = 0; i < ampCount; i++) {
    if (i == 0 || i == ampCount - 1)
      smoothAmp[i] = ampBuf[i];
    else
      smoothAmp[i] = (ampBuf[i - 1] + ampBuf[i] + ampBuf[i + 1]) / 3.0;
  }

  // Show envelope
  Serial.println("\n===== ENVELOPE =====");
  for (int i = 0; i < ampCount; i++) {
    Serial.printf("%d %.1f %.4f\n", i, cuff[i], smoothAmp[i]);
  }

  // Find MAP
  float maxAmp = 0;
  int mapIndex = 0;

  for (int i = 0; i < ampCount; i++) {
    if (smoothAmp[i] > maxAmp) {
      maxAmp = smoothAmp[i];
      mapIndex = i;
    }
  }

  float MAP = cuff[mapIndex];

  // Targets (matching .ino)
  float sysTarget = 0.7 * maxAmp;
  float diaTarget = 0.65 * maxAmp;

  float SYS = 0;
  float DIA = 0;

  // Find SYS (0.7 * maxAmp)
  SYS = cuff[0];
  for (int i = mapIndex; i >= 1; i--) {
    if (smoothAmp[i] < sysTarget) {
      float x1 = cuff[i];
      float y1 = smoothAmp[i];
      float x2 = cuff[i + 1];
      float y2 = smoothAmp[i + 1];
      SYS = x1 + (sysTarget - y1) * (x2 - x1) / (y2 - y1);
      break;
    }
  }

  // Find DIA (0.65 * maxAmp)
  DIA = cuff[ampCount - 1];
  for (int i = mapIndex; i < ampCount - 1; i++) {
    if (smoothAmp[i + 1] < diaTarget) {
      float x1 = cuff[i];
      float y1 = smoothAmp[i];
      float x2 = cuff[i + 1];
      float y2 = smoothAmp[i + 1];
      DIA = x1 + (diaTarget - y1) * (x2 - x1) / (y2 - y1);
      break;
    }
  }

  // If DIA invalid, search again
  if ((SYS - DIA) < 15) {
    for (int i = mapIndex + 1; i < ampCount; i++) {
      if ((SYS - cuff[i]) >= 15) {
        DIA = cuff[i];
        break;
      }
    }
  }

  // Fix inversion
  if (SYS < DIA) {
    float t = SYS;
    SYS = DIA;
    DIA = t;
  }
  SYS = constrain(SYS, 90, 180);
  DIA = constrain(DIA, 50, 120);

  // Result
  Serial.println("\n===== RESULT =====");
  Serial.printf("SYS = %.1f mmHg\n", SYS);
  Serial.printf("DIA = %.1f mmHg\n", DIA);
  Serial.printf("MAP = %.1f mmHg\n", MAP);

  // Publish to global variables
  lastSYS = SYS;
  lastDIA = DIA;

  // Warning counting for BP (matching .ino)
  if ((SYS < g_sys_min) || (SYS > g_sys_max) || (DIA < g_dia_min) || (DIA > g_dia_max)) {
    mode2_warning = 1;
    Serial.printf("BP warning triggered\n");
    time_incr_warn = millis();
  }
}

// ====================== WARNING CHECK (matching .ino warning_measure()) ======================
bool hrspo2bp_warning_check() {
  // Reset counters if no warnings for 30 seconds
  if (time_incr_warn > 0 && (millis() - time_incr_warn >= 30000)) {
    hr_warning = 0;
    spo2_warning = 0;
    time_incr_warn = 0;
  }

  if ((hr_warning >= 5) || (spo2_warning >= 5)) {
    mode1_warning = 1;
  }

  // If any mode is triggered, fire alert
  if ((mode1_warning == 1) || (mode2_warning == 1)) {
    // Check cooldown to prevent repeated triggers in quick succession
    if (millis() - g_alert_cooldown_ms < ALERT_COOLDOWN_PERIOD_MS) {
      Serial.println("[ALERT] Cooldown active, skipping...");
      return false;
    }

    // Check if SIM module is busy (already processing another SOS/alert)
    if (SimModule_IsBusy()) {
      Serial.println("[ALERT] SIM module busy, skipping...");
      return false;
    }

    Serial.println("=== HEALTH WARNING: Initiating alert ===");
    
    // Play warning sound via DFPlayer
    if (dfPlayerReady) {
      dfPlayer.play(1); // Play file 001.mp3 in root
      g_alert_sound_playing = true;
      Serial.println("[DFPlayer] Playing alert sound (file 001.mp3)");
    }
    
    // Make emergency call + SMS via SimModule state machine (non-blocking)
    if (g_phone[0] != '\0') {
      SimModule_TriggerAlert(g_phone, g_lastGpsLat, g_lastGpsLng, g_hasGpsLocation, "Canh bao suc khoe!");
      Serial.printf("[ALERT] Triggered call/SMS to %s\n", g_phone);
    } else {
      Serial.println("[ALERT] No phone number configured!");
    }

    // Set cooldown timer to prevent re-triggering too soon
    g_alert_cooldown_ms = millis();

    // Reset all flags
    mode1_warning = 0;
    mode2_warning = 0;
    hr_warning = 0;
    spo2_warning = 0;

    return true;  // Alert was triggered
  }

  return false;
}

// ====================== THRESHOLD CONFIG ======================
void hrspo2bp_set_thresholds(int spo2_min, int spo2_max, int hr_min, int hr_max,
                              int sys_min, int sys_max, int dia_min, int dia_max) {
  g_spo2_min = spo2_min;
  g_spo2_max = spo2_max;
  g_hr_min   = hr_min;
  g_hr_max   = hr_max;
  g_sys_min  = sys_min;
  g_sys_max  = sys_max;
  g_dia_min  = dia_min;
  g_dia_max  = dia_max;
}

void hrspo2bp_set_phone(const char *phone) {
  if (phone) {
    strncpy(g_phone, phone, sizeof(g_phone) - 1);
    g_phone[sizeof(g_phone) - 1] = '\0';
  }
}

// ====================== ASYNC BP (FreeRTOS task) ======================

void startMeasureBloodPressureAsync() {
  if (bpTaskHandle != NULL) return; // already running
  bpCancelRequested = false;
  lastBPOrigin = BP_ORIGIN_NONE;
  // Create task with larger stack size and low priority to avoid stack overflow
  xTaskCreate(bp_task_entry, "BPTask", 8192, NULL, 1, &bpTaskHandle);
}

void startMeasureBloodPressureAsyncForOrigin(int origin) {
  bpOriginBeforeStart = origin;
  startMeasureBloodPressureAsync();
}

bool isBPMeasuring() {
  return bpTaskHandle != NULL;
}

void cancelMeasureBloodPressure() {
  if (bpTaskHandle != NULL) {
    bpCancelRequested = true;
  }
}