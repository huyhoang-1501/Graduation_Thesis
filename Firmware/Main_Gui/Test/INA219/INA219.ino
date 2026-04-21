#include <Wire.h>
#include <Adafruit_INA219.h>
#include <LiquidCrystal_PCF8574.h>

// ===== Cấu hình pin =====
const float BATTERY_CAPACITY_mAh = 2600.0;  // 2S1P: dung lượng mAh bằng 1 cell

// INA219
Adafruit_INA219 ina219;

// LCD PCF8574: địa chỉ 0x27, 16 cột, 2 hàng
LiquidCrystal_PCF8574 lcd(0x27);

// Dung lượng còn lại (mAh)
float batteryRemaining_mAh = BATTERY_CAPACITY_mAh;

// Biến thời gian để tích phân dòng
unsigned long lastMillis = 0;

// Nếu bạn đã kiểm tra và thấy:
//  - XẢ -> current_mA dương
//  - SẠC -> current_mA âm
// thì giữ INVERT_CURRENT = false.
// Nếu ngược lại, set nó = true để đảo dấu.
const bool INVERT_CURRENT = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C trên ESP32: SDA=21, SCL=22
  Wire.begin(21, 22);

  // Khởi tạo LCD PCF8574
  lcd.begin(16, 2);      // 16 cột, 2 hàng
  lcd.setBacklight(255); // bật đèn nền
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("INA219 + PCF");
  lcd.setCursor(0, 1);
  lcd.print("Dang khoi dong");
  delay(1500);

  // Khởi tạo INA219
  if (!ina219.begin()) {
    Serial.println("Khong tim thay INA219!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ERR: INA219");
    while (1) {
      delay(1000);
    }
  }

  // Calibration: 32V, 1A (tuy nhu cau dong tai cua ban)
  ina219.setCalibration_32V_1A();

  lcd.clear();

  // Khởi tạo mốc thời gian
  lastMillis = millis();

  // TODO (tùy bạn): nếu khởi động khi pin khong day,
  // co the set batteryRemaining_mAh lai theo % uoc tinh
  // vi du: batteryRemaining_mAh = BATTERY_CAPACITY_mAh * 0.7;
}

void loop() {
  // ===== Đọc thời gian và tính dt =====
  unsigned long now = millis();
  float dt_s = (now - lastMillis) / 1000.0;  // giây
  if (dt_s <= 0) dt_s = 0.001;
  lastMillis = now;

  // ===== Đọc INA219 =====
  float busVoltage_V    = ina219.getBusVoltage_V();   // V
  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float current_mA_raw  = ina219.getCurrent_mA();     // mA
  float loadVoltage_V   = busVoltage_V + (shuntVoltage_mV / 1000.0);

  // Đảo dấu nếu wiring thực tế cho ra chiều ngược
  float current_mA = INVERT_CURRENT ? -current_mA_raw : current_mA_raw;

  // current_mA > 0  -> XẢ (pin cap cho buck/ESP32)
  // current_mA < 0  -> SẠC (TP4056 bom vao pin)

  // ===== Tích phân dòng để tính dung lượng còn lại =====
  float delta_mAh = current_mA * dt_s / 3600.0f;
  // Xả: current_mA > 0 -> delta mAh > 0 -> trừ đi
  // Sạc: current_mA < 0 -> delta mAh < 0 -> trừ âm = cộng thêm
  batteryRemaining_mAh -= delta_mAh;

  // Giới hạn trong khoảng 0..capacity
  if (batteryRemaining_mAh < 0) batteryRemaining_mAh = 0;
  if (batteryRemaining_mAh > BATTERY_CAPACITY_mAh) batteryRemaining_mAh = BATTERY_CAPACITY_mAh;

  // Tính % pin từ dung lượng còn lại
  float batPercent_f = 100.0f * batteryRemaining_mAh / BATTERY_CAPACITY_mAh;
  if (batPercent_f > 100.0f) batPercent_f = 100.0f;
  if (batPercent_f < 0.0f)   batPercent_f = 0.0f;
  int batPercent = (int)(batPercent_f + 0.5f);  // làm tròn

  // ===== Debug Serial =====
  Serial.print("V: "); Serial.print(loadVoltage_V, 3);
  Serial.print(" V, I: "); Serial.print(current_mA, 1);
  Serial.print(" mA, Q: "); Serial.print(batteryRemaining_mAh, 1);
  Serial.print(" mAh ("); Serial.print(batPercent); Serial.println(" %)");

  // ===== Hiển thị lên LCD =====
  // Dòng 1: V và I
  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(loadVoltage_V, 2);   // V
  lcd.print(" I:");
  lcd.print(current_mA, 0);      // mA
  lcd.print("   ");              // xoá ký tự thừa

  // Dòng 2: Q (mAh) và %
  lcd.setCursor(0, 1);
  lcd.print("Q:");
  lcd.print(batteryRemaining_mAh, 0); // mAh
  lcd.print("m ");
  lcd.print(batPercent);
  lcd.print("%   ");

  delay(500);
}