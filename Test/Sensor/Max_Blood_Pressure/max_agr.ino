#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 particleSensor;

//==================================================
// MODE
//==================================================
#define MODE_MAX30102 1
#define MODE_BP       2
int currentMode = MODE_MAX30102;   // mặc định mode 1

//==================================================
// MAX30102
//==================================================
#define MAX301_I2C_ADDR 0x57
#define BUFFER_SIZE 100
#define NO_FINGER_THRESHOLD 10000

uint32_t irBuffer[BUFFER_SIZE];
uint32_t redBuffer[BUFFER_SIZE];

int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;
int32_t bufferLength = BUFFER_SIZE;

#define EMA_ALPHA       0.08f
#define EMA_ALPHA_HR    0.45f
#define EMA_ALPHA_SPO2  0.75f

float emaIR=0, emaRed=0;
float emaHR=0, emaSPO2=0;

bool emaIRInit=false;
bool emaRedInit=false;
bool emaHRInit=false;
bool emaSPO2Init=false;

//==================================================
// PUMP + VALVE
//==================================================
const int ENA = 32;
const int ENB = 33;

const int pwmFreq = 1000;
const int pwmRes  = 8;

//==================================================
// AGR12
//==================================================
const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH  = 0xAC;
const uint8_t CMD_MEASURE_LOW   = 0x12;
const int WAIT_TIME_MS = 40;

//==================================================
// BP arrays
//==================================================
#define MAX_SAMPLES 300

float cuff[MAX_SAMPLES];
float osc[MAX_SAMPLES];
float ampBuf[MAX_SAMPLES];

int sampleCount = 0;

//==================================================
// EMA
//==================================================
float applyEMA(float value, float &ema, float alpha, bool &init)
{
  if(!init)
  {
    ema = value;
    init = true;
    return ema;
  }

  ema = alpha*value + (1-alpha)*ema;
  return ema;
}

//==================================================
// PWM
//==================================================
void startPump(int speed=200)
{
  ledcWrite(ENA, constrain(speed,0,255));
}

void stopPump()
{
  ledcWrite(ENA,0);
}

void openValve(int speed=80)
{
  ledcWrite(ENB, constrain(speed,0,255));
}

void closeValve()
{
  ledcWrite(ENB,0);
}

void stopAll()
{
  stopPump();
  closeValve();
}

//==================================================
// READ AGR12
//==================================================
bool readPressure(float &kPa, float &mmHg, int16_t &raw)
{
  Wire.beginTransmission(AGR12_I2C_ADDRESS);
  Wire.write(CMD_MEASURE_HIGH);
  Wire.write(CMD_MEASURE_LOW);

  if(Wire.endTransmission()!=0)
    return false;

  delay(WAIT_TIME_MS);

  if(Wire.requestFrom(AGR12_I2C_ADDRESS,4)<4)
    return false;

  Wire.read();

  uint8_t d0 = Wire.read();
  uint8_t d1 = Wire.read();

  Wire.read();

  raw = (int16_t)((d0<<8)|d1);

  kPa = raw/10.0;
  mmHg = kPa*7.5006;

  Serial.printf("P = %.1f mmHg\n", mmHg);

  return true;
}

//==================================================
// MAX30102
//==================================================
void Max30102_hr_spo2()
{
  uint64_t sumIR=0;
  uint64_t sumRed=0;

  uint32_t minIR=0xFFFFFFFF;
  uint32_t maxIR=0;

  uint32_t minRed=0xFFFFFFFF;
  uint32_t maxRed=0;

  int idx=0;

  while(idx<BUFFER_SIZE)
  {
    particleSensor.check();

    if(particleSensor.available())
    {
      redBuffer[idx]=particleSensor.getRed();
      irBuffer[idx]=particleSensor.getIR();

      sumIR += irBuffer[idx];
      sumRed += redBuffer[idx];

      if(irBuffer[idx]<minIR) minIR=irBuffer[idx];
      if(irBuffer[idx]>maxIR) maxIR=irBuffer[idx];

      if(redBuffer[idx]<minRed) minRed=redBuffer[idx];
      if(redBuffer[idx]>maxRed) maxRed=redBuffer[idx];

      idx++;
      particleSensor.nextSample();
    }
    else
      delay(5);
  }

  uint32_t avgIR=sumIR/BUFFER_SIZE;
  uint32_t avgRed=sumRed/BUFFER_SIZE;

  if(avgIR < NO_FINGER_THRESHOLD)
  {
    Serial.println("No finger");
    delay(100);
    return;
  }

  float fIR = applyEMA(avgIR, emaIR, EMA_ALPHA, emaIRInit);
  float fRed = applyEMA(avgRed, emaRed, EMA_ALPHA, emaRedInit);

  Serial.printf("IR=%d RED=%d\n",(int)fIR,(int)fRed);

  maxim_heart_rate_and_oxygen_saturation(
      irBuffer,
      bufferLength,
      redBuffer,
      &spo2,
      &validSPO2,
      &heartRate,
      &validHeartRate);

  if(validHeartRate)
  {
    float hr=applyEMA(heartRate, emaHR, EMA_ALPHA_HR, emaHRInit);
    Serial.printf("HR=%.1f ", hr);
  }
  else
    Serial.print("HR=- ");

  if(validSPO2)
  {
    float s=applyEMA(spo2, emaSPO2, EMA_ALPHA_SPO2, emaSPO2Init);
    Serial.printf("SpO2=%.1f%%\n", s);
  }
  else
    Serial.println("SpO2=-");
}

//==================================================
// PROCESS BP (FIX SYS/DIA)
//==================================================
void processOscillometric()
{
  if(sampleCount < 20)
  {
    Serial.println("Khong du data");
    return;
  }

  int ampCount = 0;

  //==================================================
  // TIM BIEN DO DAO DONG
  //==================================================
  for(int i=1;i<sampleCount-1;i++)
  {
    // peak
    if(osc[i] > osc[i-1] && osc[i] > osc[i+1])
    {
      // valley truoc peak
      for(int j=i-1;j>=1;j--)
      {
        if(osc[j] < osc[j-1] && osc[j] < osc[j+1])
        {
          float amp = fabs(osc[i] - osc[j]);

          // loc nhieu nho
          if(amp > 0.5)
          {
            ampBuf[ampCount] = amp;
            cuff[ampCount]   = cuff[i];
            ampCount++;
          }

          break;
        }
      }
    }
  }

  if(ampCount < 5)
  {
    Serial.println("Khong tim thay dao dong");
    return;
  }

  //==================================================
  // SMOOTH ENVELOPE
  //==================================================
  float smoothAmp[MAX_SAMPLES];

  for(int i=0;i<ampCount;i++)
  {
    if(i==0 || i==ampCount-1)
      smoothAmp[i] = ampBuf[i];
    else
      smoothAmp[i] =
      (
        ampBuf[i-1] +
        ampBuf[i] +
        ampBuf[i+1]
      ) / 3.0;
  }

  //==================================================
  // SHOW ENVELOPE
  //==================================================
  Serial.println("\n===== ENVELOPE =====");

  for(int i=0;i<ampCount;i++)
  {
    Serial.printf(
      "%d %.1f %.4f\n",
      i,
      cuff[i],
      smoothAmp[i]
    );
  }

  //==================================================
  // TIM MAP
  //==================================================
  float maxAmp = 0;
  int mapIndex = 0;

  for(int i=0;i<ampCount;i++)
  {
    if(smoothAmp[i] > maxAmp)
    {
      maxAmp = smoothAmp[i];
      mapIndex = i;
    }
  }

  float MAP = cuff[mapIndex];

  //==================================================
  // TARGET
  //==================================================
  float sysTarget = 0.7 * maxAmp;
  float diaTarget = 0.65 * maxAmp;

  float SYS = 0;
  float DIA = 0;

  //==================================================
// TIM SYS (0.7 * MAP)
//==================================================
SYS = cuff[0];

for(int i = mapIndex; i >= 1; i--)
{
    if(smoothAmp[i] < sysTarget)
    {
        float x1 = cuff[i];
        float y1 = smoothAmp[i];

        float x2 = cuff[i+1];
        float y2 = smoothAmp[i+1];

        SYS = x1 +
              (sysTarget - y1) *
              (x2 - x1) /
              (y2 - y1);

        break;
    }
}

//==================================================
// TIM DIA (0.65 * MAP)
//==================================================
DIA = cuff[ampCount - 1];

for(int i = mapIndex; i < ampCount - 1; i++)
{
    if(smoothAmp[i+1] < diaTarget)
    {
        float x1 = cuff[i];
        float y1 = smoothAmp[i];

        float x2 = cuff[i+1];
        float y2 = smoothAmp[i+1];

        DIA = x1 +
              (diaTarget - y1) *
              (x2 - x1) /
              (y2 - y1);

        break;
    }
}
  //==================================================
  // NEU DIA KHONG HOP LE -> TIM LAI
  //==================================================
  if((SYS - DIA) < 15)
  {
    for(int i=mapIndex+1;i<ampCount;i++)
    {
      if((SYS - cuff[i]) >= 15)
      {
        DIA = cuff[i];
        break;
      }
    }
  }

  //==================================================
  // FIX DAO NGUOC
  //==================================================
  if(SYS < DIA)
  {
    float t = SYS;
    SYS = DIA;
    DIA = t;
  }

  //==================================================
  // GIOI HAN
  //==================================================
  SYS = constrain(SYS, 90, 180);
  DIA = constrain(DIA, 50, 120);

  //==================================================
  // RESULT
  //==================================================
  Serial.println("\n===== RESULT =====");

  Serial.printf("SYS = %.1f mmHg\n", SYS);
  Serial.printf("DIA = %.1f mmHg\n", DIA);
  Serial.printf("MAP = %.1f mmHg\n", MAP);
}
//==================================================
// MEASURE BP
//==================================================
void measureBloodPressure()
{
  float kPa=0;
  float mmHg=0;
  int16_t raw=0;

  Serial.println("Bat dau do huyet ap");

  startPump(240);
  closeValve();

  while(mmHg < 180)
  {
    readPressure(kPa, mmHg, raw);
    delay(10);
  }

  stopPump();

  Serial.println("Xa cham");
  openValve(120);

  sampleCount=0;

  float prev=mmHg;
  float y_prev=0;
  float alpha=0.95;

  while(mmHg > 45)
  {
    if(readPressure(kPa, mmHg, raw))
    {
      cuff[sampleCount]=mmHg;

      float y=alpha*y_prev + mmHg - prev;

      osc[sampleCount]=y;

      y_prev=y;
      prev=mmHg;

      sampleCount++;
    }

    delay(80);
  }

  processOscillometric();

  Serial.println("Xa nhanh");
  openValve(255);

  while(mmHg > 5)
  {
    readPressure(kPa, mmHg, raw);
    delay(10);
  }

  stopAll();

  Serial.println("Done");
}

//==================================================
// SERIAL COMMAND
//==================================================
void checkSerial()
{
  if(!Serial.available()) return;

  String cmd=Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if(cmd=="1")
  {
    currentMode=MODE_MAX30102;
    Serial.println("Mode 1: MAX30102");
  }
  else if(cmd=="2")
  {
    currentMode=MODE_BP;
    Serial.println("Mode 2: Blood Pressure");
  }
  else if(cmd=="start")
  {
    if(currentMode==MODE_BP)
      measureBloodPressure();
    else
      Serial.println("Switch to mode 2 first");
  }
  else if(cmd=="stop")
  {
    stopAll();
    Serial.println("Stopped");
  }
  else if(cmd=="help")
  {
    Serial.println("1 -> MAX30102");
    Serial.println("2 -> Blood Pressure");
    Serial.println("start -> do huyet ap");
    Serial.println("stop -> dung");
  }
}

//==================================================
void setup()
{
  Serial.begin(115200);

  Wire.begin(21,22);
  Wire.setClock(100000);

  ledcAttach(ENA,pwmFreq,pwmRes);
  ledcAttach(ENB,pwmFreq,pwmRes);

  stopAll();

  if(!particleSensor.begin(Wire,I2C_SPEED_FAST))
  {
    Serial.println("MAX30102 not found");
  }
  else
  {
    byte ledBrightness = 32;
    byte sampleAverage = 4;
    byte ledMode = 2;
    byte sampleRate = 200;
    int pulseWidth = 411;
    int adcRange = 4096;

    particleSensor.setup(
      ledBrightness,
      sampleAverage,
      ledMode,
      sampleRate,
      pulseWidth,
      adcRange
    );

    Serial.println("MAX30102 ready");
  }

  Serial.println("Default mode = 1 (MAX30102)");
  Serial.println("type help");
}

//==================================================
void loop()
{
  checkSerial();

  if(currentMode == MODE_MAX30102)
    Max30102_hr_spo2();
}