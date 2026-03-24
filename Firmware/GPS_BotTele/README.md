# Cách tạo bot 
- Mở Telegram, tìm @BotFather
- Nhấn Start, gõ: /newbot
- Đặt tên bot (ví dụ: GPS ESP32 Bot)
- Đặt username kết thúc bằng bot (ví dụ: gps_esp32_vitri_bot)
- BotFather sẽ trả về HTTP API token dạng:1234567890:AAxxxxxx...
- Copy token này, lát nữa dán vào code.
## Cách lấy chat_ID
- Tìm @userinfobot
- Nhấn Start --> Ra ID.
## Cách lấy Token mới:
- Vào lại @BotFather
- Gõ: /revoke
- Chọn bot @gps_sensor_alert_bot(hoặc tên mà bạn đã tạo).
- BotFather sẽ cấp token mới → token cũ bị vô hiệu.
## Library
- TinyGPSPlus (tên trong Library Manager thường là “TinyGPSPlus” by Mikal Hart)
- UniversalTelegramBot (by Brian Lough)
- ArduinoJson
UniversalTelegramBot thường cần ArduinoJson. Cài luôn để khỏi lỗi.