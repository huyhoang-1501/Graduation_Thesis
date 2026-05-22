#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 particleSensor;

#define MAX301_I2C_ADDR 0x57 // 7-bit I2C address for MAX30102/05

#define BUFFER_SIZE 100
// Threshold for detecting no finger on sensor (adjust if needed)
#define NO_FINGER_THRESHOLD 10000
// Amplitude thresholds (tune if needed)

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];
// (No processed buffers — pass raw data to Maxim algorithm)
int32_t bufferLength = BUFFER_SIZE;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// EMA parameters and state
#define EMA_ALPHA 0.08f // smoothing factor: 0 < EMA_ALPHA <= 1 (larger = less smoothing)
float emaIR = 0.0f;
float emaRed = 0.0f;
bool emaIRInit = false;
bool emaRedInit = false;

// EMA for computed results
#define EMA_ALPHA_HR 0.25f // smoothing for heart rate (0-1)
#define EMA_ALPHA_SPO2 0.15f // smoothing for SpO2 (0-1)
float emaHR = 0.0f;
float emaSPO2 = 0.0f;
bool emaHRInit = false;
bool emaSPO2Init = false;

// If RED amplitude is low we may try boosting LED once in software
bool redBoosted = false;

// Apply Exponential Moving Average. Initializes on first call.
float applyEMA(float value, float &ema, float alpha, bool &initialized)
{
  if (!initialized) {
    ema = value;
    initialized = true;
    return ema;
  }
  ema = alpha * value + (1.0f - alpha) * ema;
  return ema;
}

// Preprocessing removed: Maxim's `maxim_heart_rate_and_oxygen_saturation`
// expects raw sensor samples (DC removal and peak detection happen
// inside the algorithm). Pass `irBuffer` and `redBuffer` directly.

void Max30102_hr_spo2()
{
  uint64_t sumIR = 0;
  uint64_t sumRed = 0;
  uint32_t minIR = 0xFFFFFFFF;
  uint32_t maxIR = 0;
  uint32_t minRed = 0xFFFFFFFF;
  uint32_t maxRed = 0;
  // Fill buffers: wait for available samples, with a timeout for diagnostics
  int idx = 0;
  unsigned long startMillis = millis();
  const unsigned long READ_TIMEOUT = 5000; // ms
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
      // reset timeout after successful read
      startMillis = millis();
    } else {
      if (millis() - startMillis > READ_TIMEOUT) {
        Serial.println("Sensor read timeout - no samples available");
        return;
      }
      delay(5);
    }
  }
}

  uint32_t avgIR = sumIR / BUFFER_SIZE;
  uint32_t avgRed = sumRed / BUFFER_SIZE;

  uint32_t ampIR = maxIR - minIR;
  uint32_t ampRed = maxRed - minRed;

  // Apply EMA to the averaged raw values to reduce noise
  float filteredIR = applyEMA((float)avgIR, emaIR, EMA_ALPHA, emaIRInit);
  float filteredRed = applyEMA((float)avgRed, emaRed, EMA_ALPHA, emaRedInit);

  Serial.print("IR avg = ");
  Serial.print((uint32_t)filteredIR);
  Serial.print(" | RED avg = ");
  Serial.println((uint32_t)filteredRed);

  Serial.print("IR min = ");
  Serial.print(minIR);
  Serial.print(" | IR max = ");
  Serial.println(maxIR);

  Serial.print("RED min = ");
  Serial.print(minRed);
  Serial.print(" | RED max = ");
  Serial.println(maxRed);


  if (avgIR < NO_FINGER_THRESHOLD)
  {
    Serial.println("No finger detected");
    delay(100);
    return;
  }

  // Pass raw buffers directly to Maxim's algorithm (it handles DC removal)

void Max30102_hr_spo2()
{
  for (byte i = 0; i < BUFFER_SIZE; i++)
  {
    while (particleSensor.available() == false)
      particleSensor.check();

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();

  }


  maxim_heart_rate_and_oxygen_saturation(
    irBuffer,
    bufferLength,
    redBuffer,
    &spo2,
    &validSPO2,
    &heartRate,
    &validHeartRate
  );
  // Apply EMA to computed HR and SpO2 if valid
  if (validHeartRate) {
    float filteredHR = applyEMA((float)heartRate, emaHR, EMA_ALPHA_HR, emaHRInit);
    Serial.print("HR = ");
    Serial.print(filteredHR, 1);
    Serial.print(" bpm");
  } else {
    Serial.print("HR = -");
  }

  if (validSPO2) {
    float filteredSPO2 = applyEMA((float)spo2, emaSPO2, EMA_ALPHA_SPO2, emaSPO2Init);
    Serial.print(" | SpO2 = ");
    Serial.print(filteredSPO2, 1);
    Serial.println(" %");
  } else {
    Serial.println(" | SpO2 = - %");
  }
  Serial.print("HR = ");
  Serial.print(heartRate);
  Serial.print(" bpm");

  Serial.print(" | SpO2 = ");
  Serial.print(spo2);
  Serial.println(" %");

  delay(500);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("MAX30102 Test");
  Serial.print("I2C address: 0x"); Serial.println(MAX301_I2C_ADDR, HEX);

  Wire.begin(21, 22);

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
  {
    Serial.println("MAX30102 not found");
    while (1);
  }

  Serial.println("Place your finger on sensor");
  byte ledBrightness = 30; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 200; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  // particleSensor.setup(); 
  // particleSensor.setPulseAmplitudeRed(0x0A);
  // particleSensor.setPulseAmplitudeIR(0x0A);
}


void loop()
{
  Max30102_hr_spo2();
}