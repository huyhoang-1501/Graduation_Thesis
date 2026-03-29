#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <TinyGPSPlus.h>

const char* WIFI_SSID = "HUY HOANG";
const char* WIFI_PASS = "123456789";

const char* BOT_TOKEN = "";
const char* CHAT_ID   = ""; // chat riêng: số dương

static const int GPS_RX_PIN = 16;
static const int GPS_TX_PIN = 17;
static const uint32_t GPS_BAUD = 9600;

HardwareSerial GPSSerial(2);
TinyGPSPlus gps;

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

unsigned long lastTelegramCheck = 0;
const unsigned long telegramIntervalMs = 1200;

// --- NEW: last known location cache ---
double lastKnownLat = 0;
double lastKnownLng = 0;
bool hasLastKnown = false;
unsigned long lastKnownFixMillis = 0;

// trạng thái fix hiện tại
bool hasFixNow = false;

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

  // fix "tươi" trong vòng 2s
  if (gps.location.isValid() && gps.location.age() < 2000) {
    hasFixNow = true;

    // cập nhật last known
    lastKnownLat = gps.location.lat();
    lastKnownLng = gps.location.lng();
    hasLastKnown = true;
    lastKnownFixMillis = millis();
  } else {
    hasFixNow = false;
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

  unsigned long t = millis();
  while (client.connected() && millis() - t < 5000) {
    while (client.available()) client.read();
    delay(10);
  }
  client.stop();
  return true;
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    if (chat_id != String(CHAT_ID)) {
      bot.sendMessage(chat_id, "Khong duoc phep.", "");
      continue;
    }

    if (text == "/start") {
      bot.sendMessage(chat_id, "Chao ban! Goi /vitri de lay vi tri (pin map).", "");
    }
    else if (text == "/vitri") {
  // cố gắng lấy fix mới tối đa 5s (nhưng vẫn cho fallback last known)
  unsigned long t0 = millis();
  while (!hasFixNow && millis() - t0 < 5000) {
    readGPS();
    delay(50);
  }

  if (hasFixNow) {
    // Gửi 1 dòng title giống hình
    bot.sendMessage(chat_id, "Vị trí của thiết bị", "");

    // Gửi location -> Telegram sẽ hiện card map như bạn chụp
    telegramSendLocation(secured_client, BOT_TOKEN, chat_id, lastKnownLat, lastKnownLng);

  } else if (hasLastKnown) {
    bot.sendMessage(chat_id, "Vị trí của thiết bị (lần gần nhất)", "");
    telegramSendLocation(secured_client, BOT_TOKEN, chat_id, lastKnownLat, lastKnownLng);

  } else {
    bot.sendMessage(chat_id, "Chưa từng lấy được GPS.", "");
  }
}
    else {
      bot.sendMessage(chat_id, "Lenh khong ho tro. Dung /vitri", "");
    }
  }
}

void setup() {
  Serial.begin(115200);
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  connectWiFi();
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