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

  uint16_t v;
  if (read16(0x00, v)) Serial.printf("STATUS  0x00: 0x%04X\n", v); else Serial.println("STATUS read fail");
  if (read16(0x02, v)) Serial.printf("VCELL   0x02: 0x%04X\n", v); else Serial.println("VCELL read fail");
  if (read16(0x04, v)) Serial.printf("SOC     0x04: 0x%04X\n", v); else Serial.println("SOC read fail");
  if (read16(0x0C, v)) Serial.printf("CONFIG  0x0C: 0x%04X\n", v); else Serial.println("CONFIG read fail");
  if (read16(0x08, v)) Serial.printf("VERSION 0x08: 0x%04X\n", v); else Serial.println("VERSION read fail");
}

void loop() {}