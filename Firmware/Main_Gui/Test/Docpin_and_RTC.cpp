#include <Wire.h>
#include "RTClib.h"

RTC_DS3231 rtc;

bool rtc_init() {
  if (!rtc.begin()) return false;

  // Nếu RTC mất nguồn / mới nạp, bạn set thời gian 1 lần:
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // set theo thời gian máy tính lúc compile
  return true;
}

void format_datetime(char *out, size_t out_sz, const DateTime &now) {
  // HH:MM  DD/MM/YYYY
  snprintf(out, out_sz, "%02d:%02d  %02d/%02d/%04d",
           now.hour(), now.minute(),
           now.day(), now.month(), now.year());
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////
int read_battery_percent(int adcPin) {
  // ESP32 ADC read: 0..4095
  int raw = analogRead(adcPin);

  // Bạn cần tự calibrate theo mạch chia áp của bạn:
  // Ví dụ giả sử raw->voltagePin = raw * (3.3/4095)
  float v_adc = (raw * 3.3f) / 4095.0f;

  // Nếu chia 1/2:
  float v_bat = v_adc * 2.0f;

  // Map điện áp pin Li-ion: 3.3V~4.2V -> 0~100% (chỉ xấp xỉ)
  int pct = (int)((v_bat - 3.3f) * (100.0f / (4.2f - 3.3f)));
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

/// Cách đọc ADC để tính % Pin