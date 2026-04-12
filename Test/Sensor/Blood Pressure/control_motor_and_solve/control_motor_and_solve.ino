#include <Arduino.h>

// ================== KẾT NỐI ==================
const int IN1 = 25;   // IN1 → Động cơ bơm
const int IN3 = 32;   // IN3 → Van xả

const int ENA = 27;   // PWM cho bơm
const int ENB = 14;   // PWM cho van xả

// Thông số PWM
const int pwmFreq = 1000;      // 1kHz
const int pwmRes  = 8;         // 8 bit → giá trị 0-255

// ====================== HÀM ĐIỀU KHIỂN ======================
void stopAll() {
  stopPump();
  closeValve();
}

void startPump(int speed = 255) {
  digitalWrite(IN1, HIGH);
  ledcWrite(ENA, speed);          // Dùng chân ENA trực tiếp
  Serial.println("→ BƠM ĐANG BẬT");
}

void stopPump() {
  digitalWrite(IN1, LOW);
  ledcWrite(ENA, 0);
  Serial.println("→ BƠM ĐÃ TẮT");
}

void openValve(int speed = 90) {
  digitalWrite(IN3, HIGH);
  ledcWrite(ENB, speed);          // Dùng chân ENB trực tiếp
  Serial.printf("→ VAN XẢ MỞ (PWM = %d)\n", speed);
}

void closeValve() {
  digitalWrite(IN3, LOW);
  ledcWrite(ENB, 0);
  Serial.println("→ VAN XẢ ĐÃ ĐÓNG");
}

// ====================== QUÁ TRÌNH ĐO ======================
void measureBloodPressure() {
  Serial.println("\n=== BẮT ĐẦU QUÁ TRÌNH ĐO HUYẾT ÁP ===");

  // 1. Bơm khí
  Serial.println("Bước 1: Bơm khí vào cuff...");
  startPump(255);
  closeValve();
  delay(6000);                // Thay bằng đọc cảm biến áp suất sau

  stopPump();
  Serial.println("→ Dừng bơm - Đạt áp suất tối đa");

  // 2. Xả chậm để đo
  Serial.println("Bước 2: Xả khí chậm...");
  openValve(80);              // Chỉnh số này để thay đổi tốc độ xả chậm
  delay(25000);               // Thay bằng vòng lặp đọc cảm biến

  // 3. Xả nhanh
  Serial.println("Bước 3: Xả nhanh hết khí...");
  openValve(255);
  delay(8000);

  closeValve();
  stopAll();

  Serial.println("=== HOÀN TẤT ĐO ===\n");
}
void setup() {
  Serial.begin(115200);
  Serial.println("=== ĐO HUYẾT ÁP ESP32 + L298N ===");
  Serial.println("IN1 = Bơm | IN3 = Van");
  Serial.println("Gửi 'start' để đo | 'stop' để dừng khẩn cấp");

  pinMode(IN1, OUTPUT);
  pinMode(IN3, OUTPUT);

  // ================== CẤU HÌNH PWM  ==================
  // Gắn PWM trực tiếp vào chân ENA và ENB
  ledcAttach(ENA, pwmFreq, pwmRes);   // PWM cho bơm
  ledcAttach(ENB, pwmFreq, pwmRes);   // PWM cho van

  stopAll();
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();                 // Sửa lỗi ở đây

    if (cmd == "start") {
      measureBloodPressure();
    } 
    else if (cmd == "stop") {
      Serial.println(">>> DỪNG KHẨN CẤP!");
      stopAll();
    }
  }
}

