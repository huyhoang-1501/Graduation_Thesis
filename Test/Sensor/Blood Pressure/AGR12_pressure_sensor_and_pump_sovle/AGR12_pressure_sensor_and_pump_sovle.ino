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

// Statistics
uint32_t totalReads      = 0;
uint32_t successReads    = 0;
uint32_t failedReads     = 0;
uint32_t timeoutCount    = 0;
uint32_t busRecoveryCount = 0;

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
void printStats();

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
    else if (cmd == "stats") {
      printStats();
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
  busRecoveryCount++;
  delay(200);
}

// ====================== ĐỌC ÁP SUẤT (Phiên bản mới) ======================
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw) {
  totalReads++;
  uint8_t retry = 0;
  raw = 0;
  pressure_kPa = 0;
  pressure_mmHg = 0;

  while (retry < MAX_RETRIES) {
    // Gửi lệnh đo
    Wire.beginTransmission(AGR12_I2C_ADDRESS);
    Wire.write(CMD_MEASURE_HIGH);
    Wire.write(CMD_MEASURE_LOW);
    uint8_t txStatus = Wire.endTransmission();

    if (txStatus != 0) {
      Serial.printf("[Retry %d/%d] TX Error: %d\n", retry + 1, MAX_RETRIES, txStatus);
      if (retry >= 2) i2c_recovery();
      retry++;
      delay(100);
      continue;
    }

    delay(currentDelay);

    // Đọc dữ liệu
    uint8_t count = Wire.requestFrom(AGR12_I2C_ADDRESS, 4);

    if (count < 4) {
      if (count == 0) timeoutCount++;
      
      Serial.printf("[Retry %d/%d] RX Error: got %d/4 bytes\n", retry + 1, MAX_RETRIES, count);
      
      while (Wire.available()) Wire.read(); // clear buffer
      
      if (retry >= 2) i2c_recovery();
      retry++;
      delay(150);
      continue;
    }

    // Đọc 4 byte
    uint8_t buf[4];
    for (int i = 0; i < 4; i++) {
      buf[i] = Wire.read();
    }

    // Tìm frame hợp lệ (thử 2 vị trí)
    for (int i = 0; i <= 1; i++) {
      uint8_t d0 = buf[i];
      uint8_t d1 = buf[i + 1];
      uint8_t crc = buf[i + 2];

      if ((d0 ^ d1) == crc) {
        raw = (int16_t)((d0 << 8) | d1);
        pressure_kPa  = raw / 10.0f;
        pressure_mmHg = pressure_kPa * 7.5006f;

        successReads++;
        currentDelay = MEASURE_DELAY_MIN;        // Reset delay khi thành công

        Serial.printf("OK → Raw: %d | %.1f kPa | %.1f mmHg\n", 
                      raw, pressure_kPa, pressure_mmHg);
        return true;
      }
    }

    // Nếu không tìm thấy frame hợp lệ
    retry++;
    delay(100);
  }

  // Thất bại sau nhiều lần thử
  failedReads++;
  if (currentDelay < MEASURE_DELAY_MAX) {
    currentDelay += 50;
  }

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
// ====================== THỐNG KÊ ======================
void printStats() {
  float rate = totalReads > 0 ? (successReads * 100.0 / totalReads) : 0;
  Serial.printf("\n=== THỐNG KÊ ĐỌC CẢM BIẾN ===\n");
  Serial.printf("Total: %d | Success: %d | Failed: %d\n", totalReads, successReads, failedReads);
  Serial.printf("Timeout: %d | Bus Recovery: %d\n", timeoutCount, busRecoveryCount);
  Serial.printf("Success Rate: %.1f%% | Current Delay: %dms\n\n", rate, currentDelay);
}

// ====================== ĐO HUYẾT ÁP ======================
void measureBloodPressure() {
  Serial.println("\n=== BẮT ĐẦU ĐO HUYẾT ÁP ===");

  stopAll();

  float pressure_kPa = 0.0;
  float pressure_mmHg = 0.0;
  int16_t raw = 0;

  unsigned long startTime = millis();

  // ================== GIAI ĐOẠN BƠM ==================
  Serial.println("Đang bơm lên 190 mmHg...");
  startPump(200);
  closeValve();

  while (pressure_mmHg < 190.0) {
    if (!readPressure(pressure_kPa, pressure_mmHg, raw)) {
      delay(50);
      if (millis() - startTime > 20000) {
        Serial.println("Timeout bơm! Dừng khẩn cấp.");
        openValve(220);
        delay(6000);
        stopAll();
        return;
      }
      continue;
    }

    if (millis() - startTime > 18000) {
      Serial.println("Timeout bơm 18 giây!");
      stopPump();
      openValve(220);
      delay(5000);
      stopAll();
      return;
    }
    delay(40);
  }

  stopPump();
  Serial.printf("→ Đạt %.1f mmHg. Bắt đầu xả chậm...\n", pressure_mmHg);

  // ================== XẢ CHẬM ==================
  openValve(45);        // Giảm tốc độ xả để đo chính xác hơn

  while (pressure_mmHg > 45.0) {
    readPressure(pressure_kPa, pressure_mmHg, raw);   // không kiểm tra lỗi quá nghiêm ngặt ở giai đoạn này
    delay(50);
  }

  // ================== XẢ NHANH ==================
  Serial.println("Xả nhanh phần còn lại...");
  openValve(220);
  delay(4500);

  stopAll();
  Serial.println("=== HOÀN TẤT ĐO HUYẾT ÁP ===\n");

  printStats();   // In thống kê sau khi đo xong
}