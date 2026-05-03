#include <Wire.h>

/*
    Y251216: Test I2C AGR12 Pressure Sensor 0~100kPa ASAIR với Vietduino ESP32
        - Clock I2C 50kHz
        - Với data từ I2C, giá trị áp suất đã được tính toán trong cảm biến nên code này đọc cho cả 3 loại ngưỡng áp suất.
            - I2C AGR12100C00 (0~100kPa): ~6.5kPA (Bơm chỗ Quí)
            -

            - chạy OK với Vietduino ESP32 (có tích hợp Logic convert I2C 3V3-5V)

        - Kết nối dây:
            - Vietduino ESP32 -----------------------Sensor
                  5V      ----------------------------- 1
                  GND     ----------------------------- 2
                  SCL     ----------------------------- 3
                  SDA     ----------------------------- 4
*/

// Định nghĩa I2C Constants
const uint8_t AGR12_I2C_ADDRESS = 0x50; // Địa chỉ I2C 7-bit (từ 0xA0/0xA1)
const uint8_t CMD_MEASURE_HIGH = 0xAC; // Byte lệnh 1 [8]
const uint8_t CMD_MEASURE_LOW = 0x12;  // Byte lệnh 2 [8]
const int WAIT_TIME_MS = 120;           // Thời gian chờ sau khi gửi lệnh đo (ms) [8]
const uint8_t I2C_RETRY_COUNT = 3;     // Số lần thử lại khi không nhận đủ byte
const uint16_t I2C_REQUEST_TIMEOUT_MS = 12; // Thời gian chờ (ms) để dữ liệu sẵn sàng
const uint8_t MEASURE_RETRY_COUNT = 3; // Số lần toàn bộ phép đo khi CRC sai

void setup() {
  Serial.begin(115200);
  // Wire.begin(); // Khởi tạo I2C bus
  i2c_50Khz();
  Serial.println("Khoi tao cam bien AGR12 I2C...");
}

void loop() {
  if (readPressure()) {
    // Đã đọc thành công, kết quả được in trong hàm readPressure
  } else {
    Serial.println("Loi doc cam bien.");
  }
  delay(1000); // Đọc mỗi 1 giây
}

bool readPressure() {
  // Thực hiện toàn bộ chu trình đo nhiều lần nếu CRC sai
  for (uint8_t attempt = 0; attempt < MEASURE_RETRY_COUNT; attempt++) {
    // BƯỚC 1: Gửi lệnh đo lường (0xAC 0x12) [8]
    Wire.beginTransmission(AGR12_I2C_ADDRESS);
    Wire.write(CMD_MEASURE_HIGH); 
    Wire.write(CMD_MEASURE_LOW); 
    uint8_t error = Wire.endTransmission(); 

    if (error != 0) {
      Serial.print("Loi I2C khi gui lenh: ");
      Serial.println(error);
      return false;
    }
    
    // BƯỚC 2: Chờ cảm biến hoàn tất phép đo
    delay(WAIT_TIME_MS);

    // BƯỚC 3: Yêu cầu đọc 4 byte dữ liệu (noData, DATA0, DATA1, CRC)
    const uint8_t expectedBytes = 4;
    int tempCount = requestFromWithRetry(AGR12_I2C_ADDRESS, expectedBytes, I2C_RETRY_COUNT, I2C_REQUEST_TIMEOUT_MS);

    Serial.println("count: " + String(tempCount));
    uint8_t noData = 0;
    uint8_t data0 = 0;
    uint8_t data1 = 0;
    uint8_t crc = 0;

    if (tempCount == expectedBytes) {
      noData = Wire.read();
      data0 = Wire.read();
      data1 = Wire.read();
      crc = Wire.read();
    } else if (tempCount == 3) {
      // Một số module trả đúng 3 byte: DATA0, DATA1, CRC
      data0 = Wire.read();
      data1 = Wire.read();
      crc = Wire.read();
      noData = 0;
      Serial.println("Note: Received 3-byte frame (DATA0,DATA1,CRC)");
    } else {
      Serial.print("Khong nhan du du lieu (yeu cau ");
      Serial.print(expectedBytes);
      Serial.print(" byte). Nhận: ");
      Serial.println(tempCount);
      // flush nếu có byte thừa
      while (Wire.available()) Wire.read();
      continue; // thử lại toàn bộ phép đo
    }
    Serial.print("Raw bytes: ");
      if (tempCount == 4) {
        if (noData < 16) Serial.print("0"); Serial.print(noData, HEX); Serial.print(" ");
      }
      if (data0 < 16) Serial.print("0"); Serial.print(data0, HEX); Serial.print(" ");
      if (data1 < 16) Serial.print("0"); Serial.print(data1, HEX); Serial.print(" \n");

    if(noData != 0xFF)
    {
      data1 = noData;
      data0 = 0x00;
      crc = data0 ^ data1;
      noData = 0xFF; // Đánh dấu noData không hợp lệ
    }
    
    // BƯỚC 4: Tính toán và kiểm tra CRC
    uint8_t crc_xor = data0 ^ data1; // theo Tài liệu ban đầu

    if (crc == crc_xor) {
      // ok
    } else {
      Serial.print("Loi CRC! Du lieu khong hop le. CRC nhan: ");
      Serial.print(crc, HEX);
      Serial.print(", CRC xor: ");
      Serial.print(crc_xor, HEX);
      Serial.print(", CRC sum: ");
      Serial.println(crc_sum, HEX);

      // In debug bytes thô
      Serial.print("Raw bytes: ");
      if (tempCount == 4) {
        if (noData < 16) Serial.print("0"); Serial.print(noData, HEX); Serial.print(" ");
      }
      if (data0 < 16) Serial.print("0"); Serial.print(data0, HEX); Serial.print(" ");
      if (data1 < 16) Serial.print("0"); Serial.print(data1, HEX); Serial.print(" \n");

      // Nếu thấy byte dữ liệu = 0xFF hoặc CRC = 0xFF/0x00 thì thường là lỗi bus/thiết bị
      if (crc == 0xFF || crc == 0x00 || data0 == 0xFF || data1 == 0xFF) {
        Serial.println("Detected 0xFF/0x00 in frame — likely bus pull-up or no response; retrying");
        delay(20);
        continue; // thử lần đo khác
      }

      delay(10);
      continue;
    }

    // BƯỚC 5: Chuyển đổi dữ liệu thành giá trị áp suất 
    uint16_t raw_pressure_data = (data0 << 8) | data1;
    int16_t signed_raw_data = (int16_t)raw_pressure_data; 
    float pressure_kPa = (float)signed_raw_data / 10.0;

    // BƯỚC 6: Hiển thị kết quả
    Serial.print("Raw: ");
    Serial.print(signed_raw_data);
    Serial.print(" (0x");
    if (data0 < 16) Serial.print("0"); Serial.print(data0, HEX);
    if (data1 < 16) Serial.print("0"); Serial.print(data1, HEX);
    Serial.print(") | Ap suat: ");
    Serial.print(pressure_kPa, 1);
    Serial.println(" kPa");

    return true;
  }

  // Nếu hết attempt mà vẫn lỗi
  Serial.println("Loi: Het so lan thu khi CRC van khong hop le.");
  return false;
}

// Thực hiện Wire.requestFrom với retry và timeout nhỏ, trả về số byte sẵn sàng (available)
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
    // Nếu chỉ nhận được một phần, flush và thử lại
    while (Wire.available()) Wire.read();
    delay(5);
  }
  return Wire.available();
}

void i2c_50Khz()
{
  // *** THIẾT LẬP TỐC ĐỘ I2C ***
  Wire.setClock(50000); 
  Wire.begin(); 
  
  Serial.println("Khoi tao cam bien AGR12 I2C voi toc do 50 kHz...");
}