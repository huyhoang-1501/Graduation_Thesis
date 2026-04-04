#include <Wire.h>
#define SDA_PIN 21
#define SCL_PIN 22
#define ADDR 0x32

bool read16(uint8_t reg, uint16_t &val){
  Wire.beginTransmission(ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(ADDR, (uint8_t)2) != 2) return false;
  val = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  Serial.println("Dump 16-bit regs at 0x32:");
  for (uint8_t reg = 0x00; reg <= 0x1E; reg += 2) {
    uint16_t v;
    if (read16(reg, v)) {
      Serial.printf("reg 0x%02X = 0x%04X\n", reg, v);
    } else {
      Serial.printf("reg 0x%02X = (read fail)\n", reg);
    }
  }
}

void loop() {}