/*
  ADS8232 + ESP32 debug sketch (ổn định tín hiệu)
  - Không kéo đồ thị về 0 khi lead-off (tránh "gãy" giả)
  - Lọc ECG: High-pass (~0.7Hz) + Notch 50Hz + Low-pass (~40Hz)
  - In telemetry mỗi 2 giây: lead-off %, saturation %, mean/std/rms
  - Mở Serial Plotter (115200)
*/

#include <Arduino.h>
#include <math.h>

#define PIN_ECG_OUT   34   // ADC1_CH6
#define PIN_LO_PLUS   32
#define PIN_LO_MINUS  33

// Sampling
const float FS_HZ = 250.0f;
const unsigned long SAMPLE_PERIOD_US = 4000; // 1/250s
unsigned long lastSampleUs = 0;

// Filters
const float HPF_FC_HZ = 0.7f;   // bỏ DC drift
const float LPF_FC_HZ = 40.0f;  // giữ thành phần ECG chính
const float NOTCH_F0_HZ = 50.0f;
const float NOTCH_Q = 10.0f;

// HPF one-pole state: y[n] = a * (y[n-1] + x[n] - x[n-1])
float hpfA = 0.0f;
float hpfXPrev = 0.0f;
float hpfYPrev = 0.0f;

// Notch biquad state
float nb0 = 1.0f, nb1 = 0.0f, nb2 = 0.0f;
float na1 = 0.0f, na2 = 0.0f;
float nx1 = 0.0f, nx2 = 0.0f;
float ny1 = 0.0f, ny2 = 0.0f;

// LPF one-pole: y[n] = y[n-1] + a * (x[n] - y[n-1])
float lpfA = 0.0f;
float lpfYPrev = 0.0f;

// Lead-off debounce
bool leadOffState = true;
int leadOffHighCount = 0;
int leadOffLowCount = 0;
const int LEAD_DEBOUNCE_SAMPLES = 3;

// Last good outputs for plotting continuity
int lastGoodRaw = 0;
int lastGoodEcgPlot = 1800;

// Stats window
unsigned long statStart = 0;
const unsigned long STAT_WINDOW_MS = 2000;
long totalSamples = 0;
long validSamples = 0;
long leadOffSamples = 0;
long satSamples = 0;
float statSum = 0.0f;
float statSumSq = 0.0f;
float statMin = 1e9f;
float statMax = -1e9f;

void initFilters() {
  const float dt = 1.0f / FS_HZ;

  // HPF coefficient
  const float hpRC = 1.0f / (2.0f * PI * HPF_FC_HZ);
  hpfA = hpRC / (hpRC + dt);

  // LPF coefficient
  const float lpRC = 1.0f / (2.0f * PI * LPF_FC_HZ);
  lpfA = dt / (lpRC + dt);

  // Notch coefficients (a0 normalized to 1)
  const float w0 = 2.0f * PI * NOTCH_F0_HZ / FS_HZ;
  const float cw0 = cosf(w0);
  const float sw0 = sinf(w0);
  const float alpha = sw0 / (2.0f * NOTCH_Q);

  const float b0 = 1.0f;
  const float b1 = -2.0f * cw0;
  const float b2 = 1.0f;
  const float a0 = 1.0f + alpha;
  const float a1 = -2.0f * cw0;
  const float a2 = 1.0f - alpha;

  nb0 = b0 / a0;
  nb1 = b1 / a0;
  nb2 = b2 / a0;
  na1 = a1 / a0;
  na2 = a2 / a0;
}

void resetFilterState(float seed) {
  hpfXPrev = seed;
  hpfYPrev = 0.0f;

  nx1 = nx2 = 0.0f;
  ny1 = ny2 = 0.0f;

  lpfYPrev = 0.0f;
}

float applyFilters(float x) {
  // HPF
  const float hp = hpfA * (hpfYPrev + x - hpfXPrev);
  hpfXPrev = x;
  hpfYPrev = hp;

  // Notch
  const float notch = nb0 * hp + nb1 * nx1 + nb2 * nx2 - na1 * ny1 - na2 * ny2;
  nx2 = nx1;
  nx1 = hp;
  ny2 = ny1;
  ny1 = notch;

  // LPF
  const float lp = lpfYPrev + lpfA * (notch - lpfYPrev);
  lpfYPrev = lp;

  return lp;
}

void resetStats() {
  totalSamples = 0;
  validSamples = 0;
  leadOffSamples = 0;
  satSamples = 0;
  statSum = 0.0f;
  statSumSq = 0.0f;
  statMin = 1e9f;
  statMax = -1e9f;
}

void updateStats(float ecgBand, bool leadOff, bool saturated) {
  totalSamples++;
  if (leadOff) {
    leadOffSamples++;
    return;
  }

  if (saturated) satSamples++;

  validSamples++;
  statSum += ecgBand;
  statSumSq += ecgBand * ecgBand;
  if (ecgBand < statMin) statMin = ecgBand;
  if (ecgBand > statMax) statMax = ecgBand;
}

void printStatsAndReset() {
  const float leadPct = (totalSamples > 0) ? (100.0f * (float)leadOffSamples / (float)totalSamples) : 0.0f;
  const float satPct = (validSamples > 0) ? (100.0f * (float)satSamples / (float)validSamples) : 0.0f;

  float mean = 0.0f;
  float stddev = 0.0f;
  float rms = 0.0f;
  float p2p = 0.0f;

  if (validSamples > 0) {
    mean = statSum / (float)validSamples;
    float var = (statSumSq / (float)validSamples) - (mean * mean);
    if (var < 0.0f) var = 0.0f;
    stddev = sqrtf(var);
    rms = sqrtf(statSumSq / (float)validSamples);
    p2p = statMax - statMin;
  }

  Serial.print("# DBG ");
  Serial.print("N="); Serial.print(totalSamples);
  Serial.print(" valid="); Serial.print(validSamples);
  Serial.print(" leadOff%="); Serial.print(leadPct, 1);
  Serial.print(" sat%="); Serial.print(satPct, 1);
  Serial.print(" mean="); Serial.print(mean, 2);
  Serial.print(" std="); Serial.print(stddev, 2);
  Serial.print(" rms="); Serial.print(rms, 2);
  Serial.print(" p2p="); Serial.println(p2p, 2);

  resetStats();
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LO_PLUS, INPUT);
  pinMode(PIN_LO_MINUS, INPUT);

  analogReadResolution(12); // 0..4095
  // analogSetPinAttenuation(PIN_ECG_OUT, ADC_11db); // bật nếu cần dải rộng hơn

  initFilters();

  int seed = analogRead(PIN_ECG_OUT);
  lastGoodRaw = seed;
  resetFilterState((float)seed);

  statStart = millis();
  resetStats();

  // 4 channels cho Serial Plotter:
  // raw, ecg_plot, lead_off_marker, saturation_marker
  Serial.println("raw,ecg,leadOff,sat");
}

void loop() {
  const unsigned long nowUs = micros();
  if (nowUs - lastSampleUs < SAMPLE_PERIOD_US) return;
  lastSampleUs = nowUs;

  const int loPlus = digitalRead(PIN_LO_PLUS);
  const int loMinus = digitalRead(PIN_LO_MINUS);
  const bool loHigh = (loPlus == HIGH || loMinus == HIGH);

  // debounce lead-off tránh nhảy trạng thái quá nhanh
  if (loHigh) {
    leadOffHighCount++;
    leadOffLowCount = 0;
    if (!leadOffState && leadOffHighCount >= LEAD_DEBOUNCE_SAMPLES) {
      leadOffState = true;
    }
  } else {
    leadOffLowCount++;
    leadOffHighCount = 0;
    if (leadOffState && leadOffLowCount >= LEAD_DEBOUNCE_SAMPLES) {
      leadOffState = false;
      resetFilterState((float)lastGoodRaw);
    }
  }

  const int raw = analogRead(PIN_ECG_OUT);
  const bool saturated = (raw <= 5 || raw >= 4090);

  int outRaw = lastGoodRaw;
  int outEcgPlot = lastGoodEcgPlot;

  if (!leadOffState) {
    const float ecgBand = applyFilters((float)raw);

    // scale band signal để dễ xem trên trục ADC (quanh mức 1800)
    const int ecgPlot = constrain((int)(ecgBand * 3.0f + 1800.0f), 0, 4095);

    lastGoodRaw = raw;
    lastGoodEcgPlot = ecgPlot;
    outRaw = raw;
    outEcgPlot = ecgPlot;

    updateStats(ecgBand, false, saturated);
  } else {
    updateStats(0.0f, true, saturated);
  }

  const int leadMark = leadOffState ? 2200 : 0;
  const int satMark = saturated ? 2000 : 0;

  Serial.print(outRaw);
  Serial.print(',');
  Serial.print(outEcgPlot);
  Serial.print(',');
  Serial.print(leadMark);
  Serial.print(',');
  Serial.println(satMark);

  if (millis() - statStart >= STAT_WINDOW_MS) {
    printStatsAndReset();
    statStart = millis();
  }
}