#include <Wire.h>

/*
    Y251216: Test I2C AGR12 Pressure Sensor 0~40kPa ASAIR 
  
    
    FIX: Tăng delay, thêm I2C bus recovery, xử lý timeout
*/

const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH = 0xAC;
const uint8_t CMD_MEASURE_LOW = 0x12;
const uint16_t MEASURE_DELAY_MIN = 200;   // ← Tăng lên 200ms
const uint16_t MEASURE_DELAY_MAX = 500;   // ← Max 500ms cho trường hợp quá tải
const uint8_t MAX_RETRIES = 5;            // ← Tăng lên 5 lần
const unsigned long I2C_TIMEOUT_MS = 2000; // ← Master timeout 2 giây

// Statistics
uint32_t totalReads = 0;
uint32_t successReads = 0;
uint32_t failedReads = 0;
uint32_t timeoutCount = 0;
uint32_t busRecoveryCount = 0;

// Adaptive delay
uint16_t currentDelay = MEASURE_DELAY_MIN;

void setup() {
  Serial.begin(115200);
  delay(1000);
  i2c_init();
  Serial.println("Khoi tao cam bien AGR12 I2C...\n");
}

void loop() {
  totalReads++;
  
  if (readPressure()) {
    successReads++;
    // Reset delay về bình thường nếu thành công
    currentDelay = MEASURE_DELAY_MIN;
  } else {
    failedReads++;
    // Tăng delay nếu thất bại (adaptive)
    if (currentDelay < MEASURE_DELAY_MAX) {
      currentDelay += 50;
    }
  }
  
  // In thống kê mỗi 20 lần
  if (totalReads % 20 == 0) {
    printStats();
  }
  
  delay(500);
}

void printStats() {
  float rate = totalReads > 0 ? (successReads * 100.0 / totalReads) : 0;
  Serial.printf("\n=== STATS (Read #%d) ===\n", totalReads);
  Serial.printf("Success: %d | Failed: %d | Timeout: %d | BusRecovery: %d\n", 
                successReads, failedReads, timeoutCount, busRecoveryCount);
  Serial.printf("Rate: %.1f%% | Current Delay: %dms\n\n", rate, currentDelay);
}

// ===== I2C Recovery: Khởi động lại I2C =====
void i2c_recovery() {
  Serial.println("[RECOVERY] Attempting I2C bus recovery...");
  
  // Dừng Wire
  Wire.end();
  delay(100);
  
  // Khởi động lại
  i2c_init();
  
  busRecoveryCount++;
  Serial.println("[RECOVERY] I2C reinitialized\n");
}

void i2c_init() {
  Wire.setClock(25000);  // 50kHz
  Wire.begin();
  Serial.println("I2C initialized at 50kHz (25000Hz)");
}

bool readPressure() {
  uint8_t retry = 0;
  
  while (retry < MAX_RETRIES) {
    // ===== GỬI LỆNH =====
    Wire.beginTransmission(AGR12_I2C_ADDRESS);
    Wire.write(CMD_MEASURE_HIGH);
    Wire.write(CMD_MEASURE_LOW);

    uint8_t txStatus = Wire.endTransmission();
    
    if (txStatus != 0) {
      Serial.printf("[ATTEMPT %d/%d] TX Error: %d", retry + 1, MAX_RETRIES, txStatus);
      
      // Nếu TX error liên tục → recover
      if (retry >= 2) {
        Serial.println(" → Recovering I2C bus");
        i2c_recovery();
      } else {
        Serial.println();
      }
      
      retry++;
      delay(100);
      continue;
    }

    // ===== ĐỢI SENSOR XỬ LÝ =====
    delay(currentDelay);

    // ===== ĐỌC 4 BYTE =====
    unsigned long startTime = millis();
    uint8_t count = Wire.requestFrom(AGR12_I2C_ADDRESS, 4);
    unsigned long elapsedTime = millis() - startTime;

    if (count < 4) {
      if (count == 0) {
        // Không nhận được byte nào = timeout hoặc bus stuck
        Serial.printf("[ATTEMPT %d/%d] RX Timeout (%ldms, got 0/4)\n", 
                      retry + 1, MAX_RETRIES, elapsedTime);
        timeoutCount++;
        
        // Clear buffer
        while (Wire.available()) {
          Wire.read();
        }
        
        // Nếu timeout 3 lần liên tiếp → recover bus
        if (retry >= 2) {
          Serial.println("[TIMEOUT] Bus recovery triggered\n");
          i2c_recovery();
        }
      } else {
        Serial.printf("[ATTEMPT %d/%d] RX Incomplete: got %d/4 bytes (%ldms)\n", 
                      retry + 1, MAX_RETRIES, count, elapsedTime);
        
        // Clear buffer
        while (Wire.available()) {
          Wire.read();
        }
      }
      
      retry++;
      delay(150);
      continue;
    }

    // ===== ĐỌC DỮ LIỆU =====
    uint8_t buf[4];
    for (int i = 0; i < 4; i++) {
      int b = Wire.read();
      if (b < 0) {
        Serial.printf("[ATTEMPT %d/%d] Read Error at byte %d\n", retry + 1, MAX_RETRIES, i);
        retry++;
        delay(100);
        continue;
      }
      buf[i] = (uint8_t)(b & 0xFF);
    }

    // ===== DEBUG: In raw buffer =====
    Serial.print("BUF: ");
    for (int i = 0; i < 4; i++) {
      Serial.printf("0x%02X ", buf[i]);
    }
    Serial.println();

    // ===== TÌM FRAME =====
    for (int i = 0; i <= 1; i++) {
      uint16_t d0 = buf[i];
      uint16_t d1 = buf[i + 1];
      uint8_t crc = buf[i + 2];

      if ((d0 ^ d1) == crc) {
        uint16_t raw = (d0 << 8) | d1;
        float kPa = raw / 10.0;

        Serial.print("OK | data0: 0x");
        Serial.print(d0, HEX);
        Serial.print(" data1: 0x");
        Serial.print(d1, HEX);
        Serial.print(" | Raw: ");
        Serial.print(raw);
        Serial.print(" | Ap suat: ");
        Serial.print(kPa, 1);
        Serial.println(" kPa");

        return true;
      }
    }

    Serial.println("Sai frame");
    retry++;
    delay(100);
  }

  Serial.printf("Loi doc cam bien sau %d lan thu.\n\n", MAX_RETRIES);
  return false;
}
