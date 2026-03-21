#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(1000);

  tft.init();

  for (int r = 0; r < 4; r++) {
    tft.setRotation(r);
    tft.fillScreen(TFT_BLACK);
    Serial.print("Rotation "); Serial.print(r);
    Serial.print(" -> width=");  Serial.print(tft.width());
    Serial.print(" height=");    Serial.println(tft.height());
    delay(1000);
  }
}

void loop() {}