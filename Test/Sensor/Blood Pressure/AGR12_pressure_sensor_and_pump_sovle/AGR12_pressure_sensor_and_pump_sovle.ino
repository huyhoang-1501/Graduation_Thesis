#include <Arduino.h>
#include <Wire.h>

// ================== KẾT NỐI L298N + PUMP + VALVE ==================
const int IN1 = 25;   // Pump direction
const int IN3 = 32;   // Valve direction
const int ENA = 27;   // PWM Pump
const int ENB = 14;   // PWM Valve

const int pwmFreq = 1000;
const int pwmRes  = 8;

// ================== CẢM BIẾN AGR12 ==================
const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH = 0xAC;
const uint8_t CMD_MEASURE_LOW  = 0x12;

const uint16_t MEASURE_DELAY_MIN = 200;
const uint16_t MEASURE_DELAY_MAX = 500;
const uint8_t  MAX_RETRIES       = 5;
const unsigned long I2C_TIMEOUT_MS = 2000;

// (Statistics removed)

// Adaptive delay
uint16_t currentDelay = MEASURE_DELAY_MIN;

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

  pinMode(IN1, OUTPUT);
  pinMode(IN3, OUTPUT);

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
  digitalWrite(IN1, HIGH);
  ledcWrite(ENA, constrain(speed, 0, 255));
}

void stopPump() {
  ledcWrite(ENA, 0);
  digitalWrite(IN1, LOW);
}

void openValve(int speed) {                 
  digitalWrite(IN3, HIGH);
  ledcWrite(ENB, constrain(speed, 0, 255));
}

void closeValve() {
  ledcWrite(ENB, 0);
  digitalWrite(IN3, LOW);
}

void stopAll() {
  stopPump();
  closeValve();
}
// (Statistics printing removed)

// ====================== ĐO HUYẾT ÁP ======================
void measureBloodPressure() {
  Serial.println("\n=== BẮT ĐẦU ĐO HUYẾT ÁP ===");

  stopAll();

  float pressure_kPa = 0.0;
  float pressure_mmHg = 0.0;
  int16_t raw = 0;

  unsigned long startTime = millis();

  // ===== BƠM =====
  Serial.println("Đang bơm lên 190 mmHg...");
  startPump(243);
  closeValve();

  while (pressure_mmHg < 190.0) {
    if (millis() - startTime > 20000) {
      Serial.println("Timeout bơm!");
      openValve(220);
      delay(5000);
      stopAll();
      return;
    }

    if (readPressure(pressure_kPa, pressure_mmHg, raw)) {
      if (pressure_mmHg < 0 || pressure_mmHg > 300) continue;
    }

    delay(40);
  }

  stopPump();
  Serial.printf("→ Đạt %.1f mmHg. Bắt đầu xả chậm...\n", pressure_mmHg);

  // ===== XẢ CHẬM =====
  openValve(48);

  unsigned long deflateStart = millis();

  while (pressure_mmHg > 45.0) {
    if (millis() - deflateStart > 30000) {
      Serial.println("Timeout xả!");
      break;
    }

    if (!readPressure(pressure_kPa, pressure_mmHg, raw)) continue;

    delay(50);
  }

  // ===== XẢ NHANH =====
  Serial.println("Xả nhanh phần còn lại...");
  openValve(245);

  while (pressure_mmHg > 5) {
    readPressure(pressure_kPa, pressure_mmHg, raw);
    delay(50);
  }

  stopAll();
  Serial.println("=== HOÀN TẤT ĐO HUYẾT ÁP ===\n");
}