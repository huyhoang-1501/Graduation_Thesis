#include <Arduino.h>
#include <Wire.h>

// Kết nối L289N, Pump, sovle
const int IN1 = 25;
const int IN3 = 32;
const int ENA = 27;
const int ENB = 14;

// PWM
const int pwmFreq = 1000;
const int pwmRes  = 8;

// Cảm biến AGR12
const uint8_t AGR12_I2C_ADDRESS = 0x50;
const uint8_t CMD_MEASURE_HIGH = 0xAC;
const uint8_t CMD_MEASURE_LOW  = 0x12;
const int WAIT_TIME_MS = 80;

float kPaToMmHg(float kPa) {
  return kPa * 7.5006;
}

// Hàm đọc dữ liệu cảm biến, trả về cả raw và kPa
bool readPressure(float &pressure_kPa, float &pressure_mmHg, int16_t &raw) {

  Wire.beginTransmission(AGR12_I2C_ADDRESS);
  Wire.write(CMD_MEASURE_HIGH); 
  Wire.write(CMD_MEASURE_LOW); 
  uint8_t error = Wire.endTransmission(); 

  if (error != 0) {
    Serial.print("Loi I2C khi gui lenh: ");
    Serial.println(error);
    return false;
  }

  delay(WAIT_TIME_MS);

  uint8_t tempCount = Wire.requestFrom(AGR12_I2C_ADDRESS, 4);
  Serial.println("count: " + String(tempCount));

  if (tempCount >= 3) {
    uint8_t noData = Wire.read();
    uint8_t data0 = Wire.read();
    uint8_t data1 = Wire.read();
    uint8_t crc   = Wire.read();

    uint8_t calculated_crc = data0 ^ data1;
    if (calculated_crc != crc) {
      Serial.print("Loi CRC! ");
      Serial.print(crc, HEX);
      Serial.print(" != ");
      Serial.println(calculated_crc, HEX);
    }

    raw = (int16_t)((data0 << 8) | data1);

    pressure_kPa  = raw / 10.0;
    pressure_mmHg = pressure_kPa * 7.5006;

    // In luôn (giữ behavior cũ)
    Serial.print("Raw: ");
    Serial.print(raw);
    Serial.print(" | ");
    Serial.print(pressure_kPa, 1);
    Serial.print(" kPa | ");
    Serial.print(pressure_mmHg, 1);
    Serial.println(" mmHg");

    return true;

  } else {
    Serial.println("Khong nhan du du lieu: " + String(tempCount));
    return false;
  }
}
// Điều khiển bơm
void startPump(int speed = 200) {
  digitalWrite(IN1, HIGH);
  speed = constrain(speed, 0, 255);
  ledcWrite(ENA, speed);   // ESP32 core v3.x: dùng pin trực tiếp
}

void stopPump() {
  ledcWrite(ENA, 0);
  digitalWrite(IN1, LOW);
}

// Điều khiển van xả
void openValve(int speed = 60) {
  digitalWrite(IN3, HIGH);
  speed = constrain(speed, 0, 255);
  ledcWrite(ENB, speed);
}

void closeValve() {
  ledcWrite(ENB, 0);
  digitalWrite(IN3, LOW);
}

// Dừng toàn bộ
void stopAll() {
  stopPump();
  closeValve();
}

void measureBloodPressure() {
  Serial.println("\n=== BẮT ĐẦU ĐO HUYẾT ÁP ===");

  stopAll();

  float pressure_kPa = 0.0;
  float pressure_mmHg = 0.0;
  int16_t raw = 0;

  unsigned long startTime = millis();
  bool timeoutError = false;

  // ================= BƠM =================
  Serial.println("Đang bơm lên đến 190 mmHg... (tối đa 15 giây)");

  startPump(200);
  closeValve();

  while (pressure_mmHg < 190.0) {

    if (readPressure(pressure_kPa, pressure_mmHg, raw)) {
      Serial.printf("Bơm → Raw: %d | %.1f kPa (%.1f mmHg) | %lu ms\n",
                    raw, pressure_kPa, pressure_mmHg, millis() - startTime);
    }

    if (millis() - startTime > 15000) {
      Serial.println("⚠️ Timeout bơm 15 giây! Dừng khẩn.");
      timeoutError = true;
      break;
    }

    delay(50);
  }

  stopPump();

  // ================= TIMEOUT =================
  if (timeoutError) {
    openValve(220);
    delay(6000);
    stopAll();
    Serial.println("→ Đã dừng do timeout.");
    return;
  }

  Serial.printf("→ Đạt %.1f mmHg → Bắt đầu xả chậm...\n", pressure_mmHg);

  // ================= XẢ CHẬM =================
  openValve(70);

  while (pressure_mmHg > 45.0) {

    if (readPressure(pressure_kPa, pressure_mmHg, raw)) {
      Serial.printf("Xả chậm → Raw: %d | %.1f kPa (%.1f mmHg)\n",
                    raw, pressure_kPa, pressure_mmHg);
    }

    delay(50);
  }

  // ================= XẢ NHANH =================
  Serial.println("Xả nhanh...");
  openValve(220);
  delay(4500);

  stopAll();

  Serial.println("=== HOÀN TẤT ĐO ===\n");
}

void setup() {
  Serial.begin(115200);
  pinMode(IN1, OUTPUT);
  pinMode(IN3, OUTPUT);

  // PWM cho bơm/van 
  ledcAttach(ENA, pwmFreq, pwmRes);
  ledcAttach(ENB, pwmFreq, pwmRes);

  Wire.begin();
  Wire.setClock(50000);

  Serial.println("Khởi tạo cảm biến AGR12 I2C...");
  stopAll();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim(); cmd.toLowerCase();
    if (cmd == "start") measureBloodPressure();
    else if (cmd == "stop") stopAll();
  }
}
