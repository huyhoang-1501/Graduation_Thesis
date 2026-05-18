/*
    Lưu ý: thẻ nhớ Micro SD phải format Fat 32
    Tạo Folder có tên "xx" (xx: từ 01 đến 99)
    Trong floder có các tệp tên "xxx.mp3" (xxx: từ 001 đến 255)
    Tệp phải có định dạng đuôi mp3, 128kbps, mono.
*/
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

HardwareSerial mySerial(2);
DFRobotDFPlayerMini player;

// UART ESP32
#define DF_RX 27   // ESP32 RX <- DFPlayer TX
#define DF_TX 14   // ESP32 TX -> DFPlayer RX

void setup() {

  Serial.begin(115200);

  // UART2
  mySerial.begin(9600, SERIAL_8N1, DF_RX, DF_TX);

  Serial.println("Dang khoi dong DFPlayer...");

  // Kiem tra ket noi DFPlayer
  if (!player.begin(mySerial)) {

    Serial.println("Khong tim thay DFPlayer!");
    Serial.println("Kiem tra day TX RX");
    Serial.println("Kiem tra the nho");

    while (true);
  }

  Serial.println("DFPlayer OK");

  // Am luong 0 -> 30
  // Đặt âm lượng lớn nhất (30)
  player.volume(30);

  delay(2000);

  Serial.println("Phat file 001.mp3");

  // Phat file:
  // /01/001.mp3
  player.playFolder(1, 1);
  Serial.println("Commands: n=next  p=prev  s=stop");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') {
      player.next();
      Serial.println("Next");
    } else if (c == 'p') {
      player.previous();
      Serial.println("Previous");
    } else if (c == 's') {
      player.stop();
      Serial.println("Stop");
    }
  }
}