#pragma once
#include <Arduino.h>

// ===== SIM Module (A7682S) =====
// UART: TX=17, RX=16
// Chức năng: Gọi điện, gửi SMS kèm vị trí GPS/LBS khi nhấn nút SOS (GPIO13)
//
// LƯU Ý UART: ESP32 chỉ có 3 UART phần cứng (0, 1, 2).
//   - UART0: Serial USB debug
//   - UART1: DFPlayer Mini (RX=27, TX=14)
//   - UART2: GPS NEO-6M (RX=25, TX=26)
// Giải pháp: Module SIM sẽ chia sẻ (Share) UART2 với module GPS.
// Khi nhấn nút SOS, hệ thống sẽ tạm ngưng GPS, chuyển UART2 sang SIM để gọi/nhắn tin,
// và trả lại UART2 cho GPS sau khi xử lý xong khẩn cấp.
// Điều này giúp giữ nguyên cấu hình WiFi và Serial Debug của hệ thống.

void SimModule_Init();
void SimModule_Loop();

// Kích hoạt SOS: gọi điện + gửi SMS vị trí
// phone: số điện thoại (từ settings)
// lat, lng: tọa độ GPS (nếu có)
// hasGps: true nếu GPS có fix, false nếu cần dùng LBS fallback
void SimModule_TriggerSOS(const char* phone, double lat, double lng, bool hasGps);

// Kiểm tra SIM module đang bận xử lý SOS hay không
bool SimModule_IsBusy();