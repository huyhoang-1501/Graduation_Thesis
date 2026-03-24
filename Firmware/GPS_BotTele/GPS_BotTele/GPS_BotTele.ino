#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>


const char* WIFI_SSID = "Phong Tro Tang 3.2";
const char* WIFI_PASS = "99999999";

const char* BOT_TOKEN = "";
const char* CHAT_ID   = ""; // user id (chat riêng) hoặc group id (âm)
// ===================================

// GPS: cấu hình UART2 (GPIO16 RX2, GPIO17 TX2)
static const int GPS_RX_PIN = 16; // ESP32 RX2  <- GPS TX
static const int GPS_TX_PIN = 17; // ESP32 TX2  -> GPS RX
static const uint32_t GPS_BAUD = 9600;

HardwareSerial GPSSerial(2);
TinyGPSPlus gps;

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

unsigned long lastTelegramCheck = 0;
const unsigned long telegramIntervalMs = 1200;

double lastLat = 0;
double lastLng = 0;
bool hasFix = false;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
  }
}

void readGPS() {
  while (GPSSerial.available()) {
    gps.encode(GPSSerial.read());
  }

  if (gps.location.isValid() && gps.location.age() < 2000) {
    lastLat = gps.location.lat();
    lastLng = gps.location.lng();
    hasFix = true;
  }
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    // Chỉ cho phép đúng CHAT_ID (đỡ bị người khác spam)
    if (chat_id != String(CHAT_ID)) {
      bot.sendMessage(chat_id, "Khong duoc phep.", "");
      continue;
    }

    if (text == "/start") {
      bot.sendMessage(chat_id, "Chao ban! Goi /vitri de lay vi tri (pin map).", "");
    }
    else if (text == "/vitri") {
      // Cố gắng đọc GPS thêm chút để có fix
      unsigned long t0 = millis();
      while (!hasFix && millis() - t0 < 5000) {
        readGPS();
        delay(50);
      }

      if (!hasFix) {
        bot.sendMessage(chat_id, "Chua bat duoc GPS fix. Hay dem ra ngoai troi va doi 30-60s.", "");
      } else {
        // GỬI LOCATION (pin)
        telegramSendLocation(secured_client, BOT_TOKEN, chat_id, lastLat, lastLng);

        // (Tuỳ chọn) gửi thêm link Google Maps
        String link = "https://maps.google.com/?q=" + String(lastLat, 6) + "," + String(lastLng, 6);
        bot.sendMessage(chat_id, "Link: " + link, "");
      }
    }
    else {
      bot.sendMessage(chat_id, "Lenh khong ho tro. Dung /vitri", "");
    }
  }
}
bool telegramSendLocation(WiFiClientSecure &client,
                          const String &botToken,
                          const String &chatId,
                          double lat,
                          double lon) {
  client.setInsecure();

  String host = "api.telegram.org";
  if (!client.connect(host.c_str(), 443)) return false;

  // Telegram expects application/x-www-form-urlencoded
  String postData = "chat_id=" + chatId +
                    "&latitude=" + String(lat, 6) +
                    "&longitude=" + String(lon, 6);

  String url = "/bot" + botToken + "/sendLocation";

  client.print(String("POST ") + url + " HTTP/1.1\r\n");
  client.print(String("Host: ") + host + "\r\n");
  client.print("User-Agent: ESP32\r\n");
  client.print("Connection: close\r\n");
  client.print("Content-Type: application/x-www-form-urlencoded\r\n");
  client.print(String("Content-Length: ") + postData.length() + "\r\n\r\n");
  client.print(postData);

  // đọc response (không cần parse, chỉ cần biết có phản hồi)
  unsigned long t = millis();
  while (client.connected() && millis() - t < 5000) {
    while (client.available()) client.read();
    delay(10);
  }
  client.stop();
  return true;
}
void setup() {
  Serial.begin(115200);

  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  connectWiFi();

  // Bỏ kiểm tra chứng chỉ để đơn giản (dễ chạy)
  secured_client.setInsecure();
}

void loop() {
  readGPS();

  if (millis() - lastTelegramCheck > telegramIntervalMs) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTelegramCheck = millis();
  }
}