#pragma once
#include <Arduino.h>

// ===== SIM Module (A7682S) =====
// UART: TX=17, RX=16
// Chức năng: Gọi điện, gửi SMS kèm vị trí GPS/LBS khi nhấn nút SOS (GPIO13)

void SimModule_Init();
void SimModule_Loop();

// Kích hoạt SOS: gọi điện + gửi SMS vị trí
// phone: số điện thoại (từ settings)
// lat, lng: tọa độ GPS (nếu có)
// hasGps: true nếu GPS có fix, false nếu cần dùng LBS fallback
void SimModule_TriggerSOS(const char* phone, double lat, double lng, bool hasGps);

// Kích hoạt cảnh báo sức khoẻ: gọi điện + gửi SMS với nội dung tuỳ chỉnh
// message: nội dung tin nhắn cảnh báo (nếu nullptr → dùng nội dung SOS mặc định)
void SimModule_TriggerAlert(const char* phone, double lat, double lng, bool hasGps, const char* message);

// Kiểm tra SIM module đang bận xử lý SOS hay không
bool SimModule_IsBusy();

// Gửi lệnh AT trực tiếp đến SIM module (blocking, dùng cho cảnh báo sức khoẻ)
void sendAT(const String &cmd);
