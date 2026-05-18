#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

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

  particleSensor.setup(); 
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeIR(0x0A);
}


void loop()
{
  Max30102_hr_spo2();
}