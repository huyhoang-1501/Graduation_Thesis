#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"
#include <TinyGPSPlus.h>
#include <math.h>

// ===== NVS / Preferences để lưu dung lượng pin =====
#include <Preferences.h>
// ===== INA219 =====
#include <Adafruit_INA219.h>

// ===== LVGL =====
#define LV_CONF_SKIP
#include "lv_conf.h"
#include <lvgl.h>

// ===== RTC =====
#include "RTClib.h"

// ===== UI modules =====
#include "GuestMode.h"
#include "MainUi.h"
#include "UserMode.h"
#include "FirebaseSync.h"
#include "UserDashboard.h"
// HR/SpO2 and BP module
#include "HR_SPO2_BP.h"
#include "sim_module.h"

#include <WiFi.h>
#include <DFRobotDFPlayerMini.h>

// Enable Firebase_ESP_Client usage in FirebaseSync (will pass fbdo pointer)
#define USE_FIREBASE_ESP_CLIENT
#ifdef USE_FIREBASE_ESP_CLIENT
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
// Firebase objects (sketch-level)
static FirebaseData fbdo;
static FirebaseAuth auth;
static FirebaseConfig config;
// Leave empty if you don't want to hardcode the API key here.
static const char *FIREBASE_API_KEY = "";
#endif

static const int DFPLAYER_RX_PIN = 27;
static const int DFPLAYER_TX_PIN = 14;
static const int BUTTON_SOUND_PIN = 13;  // nút nhấn đã có điện trở kéo lên
static const uint32_t BUTTON_DEBOUNCE_MS = 50;

// ================= DFPLAYER MINI =================
HardwareSerial DFPlayerSerial(1);
DFRobotDFPlayerMini dfPlayer;
bool dfPlayerReady = false;
static bool lastButtonState = HIGH;
static uint32_t lastButtonChangeMs = 0;
bool g_alert_sound_playing = false;  // Tracks if alert sound is currently playing

// ================= GPS (NEO-6M) =================
// Dùng 2 GPIO khác nhau cho UART GPS.
static const int GPS_RX_PIN = 25;
static const int GPS_TX_PIN = 26;
static const uint32_t GPS_BAUD = 9600;

HardwareSerial GPSSerial(2);
TinyGPSPlus gps;

double g_lastGpsLat = 0.0;
double g_lastGpsLng = 0.0;
bool g_hasGpsLocation = false;
static uint32_t g_lastGpsPushMs = 0;
static const uint32_t GPS_PUSH_INTERVAL_MS = 5000;

static void dfplayer_setup() {
  DFPlayerSerial.begin(9600, SERIAL_8N1, DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  delay(800);

  if (dfPlayer.begin(DFPlayerSerial)) {
    dfPlayerReady = true;
    dfPlayer.volume(25);  // 0..30
    Serial.printf("[DFPlayer] OK RX=%d TX=%d\n", DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  } else {
    dfPlayerReady = false;
    Serial.printf("[DFPlayer] init failed RX=%d TX=%d\n", DFPLAYER_RX_PIN, DFPLAYER_TX_PIN);
  }
}

static void gps_setup() {
  GPSSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.printf("[GPS] UART2 started RX=%d TX=%d baud=%lu\n", GPS_RX_PIN, GPS_TX_PIN, (unsigned long)GPS_BAUD);
}

static void gps_loop() {
  if (SimModule_IsBusy()) {
    return; // Khi đang xử lý SOS, UART2 được nhường cho SIM module
  }

  while (GPSSerial.available()) {
    gps.encode(GPSSerial.read());
  }

  if (gps.location.isValid() && gps.location.age() < 3000) {
    double lat = gps.location.lat();
    double lng = gps.location.lng();
    bool changed = !g_hasGpsLocation || fabs(lat - g_lastGpsLat) > 0.000001 || fabs(lng - g_lastGpsLng) > 0.000001;
    bool intervalOk = (millis() - g_lastGpsPushMs >= GPS_PUSH_INTERVAL_MS);

    g_lastGpsLat = lat;
    g_lastGpsLng = lng;
    g_hasGpsLocation = true;

    if (changed || intervalOk) {
      FirebaseSync_SetLocation(lat, lng);
      FirebaseSync_PushStatusAndBattery();
      g_lastGpsPushMs = millis();
      Serial.printf("[GPS] fix %.6f, %.6f\n", lat, lng);
    }
  }
}

// ================= DISPLAY =================
static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

// TFT_eSPI rotation:
// - 1: Landscape (current/original)
// - 3: Landscape flipped 180° (upside-down)
// NOTE: We intentionally restrict to landscape rotations to keep ALL LVGL UI
// logic/layout (keypad, main screens, etc.) unchanged.
#ifndef TFT_ROTATION
#define TFT_ROTATION 3
#endif

#if (TFT_ROTATION != 1) && (TFT_ROTATION != 3)
#error "TFT_ROTATION must be 1 (landscape) or 3 (landscape 180deg). Other rotations require UI re-layout."
#endif

#define TFT_BL 15
TFT_eSPI tft;

// ================= LVGL BUFFER =================
// Reduced draw buffer lines to save ~11 KB of RAM (was 24)
static const uint16_t LVGL_DRAW_BUF_LINES = 5;
static lv_color_t buf1[SCREEN_WIDTH * LVGL_DRAW_BUF_LINES];
static lv_disp_draw_buf_t draw_buf;

// ================= LVGL TICK =================
static esp_timer_handle_t lvgl_tick_timer;
static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

// ================= I2C PINS =================
#define I2C_SDA 21
#define I2C_SCL 22

// ================= TOUCH (FT6336U) =================
#define FT6336U_ADDR 0x38

static bool ft6336u_read_touch(uint16_t &x, uint16_t &y, bool &touched) {
  touched = false;

  Wire.beginTransmission(FT6336U_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;

  const uint8_t n = 5;
  if (Wire.requestFrom(FT6336U_ADDR, n) != n) return false;

  uint8_t b0 = Wire.read();
  uint8_t b1 = Wire.read();
  uint8_t b2 = Wire.read();
  uint8_t b3 = Wire.read();
  uint8_t b4 = Wire.read();

  uint8_t points = b0 & 0x0F;
  if (points == 0) return true;

  x = ((uint16_t)(b1 & 0x0F) << 8) | b2;
  y = ((uint16_t)(b3 & 0x0F) << 8) | b4;
  touched = true;
  return true;
}

// ================= RTC DS3231 =================
RTC_DS3231 rtc;
static bool rtc_ok = true;

// ================= DEVICE ID / PAIRING DEMO =================
static Preferences devicePref;
static bool devicePrefReady = false;
static const char *DEVICE_NVS_NAMESPACE = "device";
static const char *DEVICE_NVS_KEY_ID    = "device_id";
static const char *DEVICE_ID_FIXED      = "UTE-2026";

static char g_device_id[24] = "DEV-UNKNOWN";

static void load_or_create_device_identity() {
  if (!devicePref.begin(DEVICE_NVS_NAMESPACE, false)) {
    Serial.println("Failed to init device NVS, using RAM-only fixed device id");
    devicePrefReady = false;
    snprintf(g_device_id, sizeof(g_device_id), "%s", DEVICE_ID_FIXED);
    return;
  }

  devicePrefReady = true;

  String storedId = devicePref.getString(DEVICE_NVS_KEY_ID, "");

  snprintf(g_device_id, sizeof(g_device_id), "%s", DEVICE_ID_FIXED);
  if (!storedId.equals(DEVICE_ID_FIXED)) {
    devicePref.putString(DEVICE_NVS_KEY_ID, DEVICE_ID_FIXED);
  }

  Serial.print("Device ID: ");
  Serial.println(g_device_id);
}

static Preferences userPref;
static bool userPrefReady = false;
static const char *USER_NVS_NAMESPACE = "usercfg";
static const char *USER_NVS_KEY_ID = "current_user_id";
static char g_current_user_id[16] = "";

static void load_user_id_from_nvs() {
  if (!userPref.begin(USER_NVS_NAMESPACE, false)) {
    Serial.println("[UserNVS] Failed to open NVS for user id");
    return;
  }

  String storedId = userPref.getString(USER_NVS_KEY_ID, "");
  userPref.end();

  storedId.trim();
  if (storedId.length() > 0) {
    storedId.toCharArray(g_current_user_id, sizeof(g_current_user_id));
    FirebaseSync_SetCurrentUserId(g_current_user_id);
    Serial.print("[UserNVS] Loaded user id: ");
    Serial.println(g_current_user_id);
  } else {
    FirebaseSync_SetCurrentUserId(nullptr);
    Serial.println("[UserNVS] No stored user id");
  }
}

static void save_user_id_to_nvs(const char *userId) {
  if (!userId || !userId[0]) {
    if (userPref.begin(USER_NVS_NAMESPACE, false)) {
      userPref.remove(USER_NVS_KEY_ID);
      userPref.end();
    }
    g_current_user_id[0] = '\0';
    FirebaseSync_SetCurrentUserId(nullptr);
    Serial.println("[UserNVS] Cleared stored user id");
    return;
  }

  snprintf(g_current_user_id, sizeof(g_current_user_id), "%s", userId);

  if (!userPref.begin(USER_NVS_NAMESPACE, false)) {
    Serial.println("[UserNVS] Failed to open NVS for saving user id");
    return;
  }
  userPref.putString(USER_NVS_KEY_ID, g_current_user_id);
  userPref.end();

  FirebaseSync_SetCurrentUserId(g_current_user_id);
  Serial.print("[UserNVS] Saved user id: ");
  Serial.println(g_current_user_id);
}

static const char *ui_get_device_id() {
  return g_device_id;
}

// ui_get_user_id removed; pass nullptr where a user-id getter was used.

// ================= FIREBASE SYNC CONFIG =================
// static const char *WIFI_SSID = "Huy Hoang";
// static const char *WIFI_PASSWORD = "cudiroiseden";
static const char *WIFI_SSID = "NAVI Apartment 2.4G";
static const char *WIFI_PASSWORD = "168168168";
static const char *FIREBASE_DB_URL = "https://graduation-thesis-3a3df-default-rtdb.firebaseio.com";
static const uint32_t FIREBASE_PUSH_INTERVAL_MS = 5000;

// If true, disable all Firebase push/loop/init calls to allow disconnecting WiFi temporarily.
static const bool DISABLE_FIREBASE_PUSH = false;

static void on_user_mode_back() {
  MainUi_ShowMainScreen();
  // Keep the stored user id so GuestMode can continue using it after reboot
  // and after navigating back from UserDashboard.
}

static void on_user_mode_success(const char *userId) {
  save_user_id_to_nvs(userId);
  FirebaseSync_SetCurrentUserId(userId);

  if (!DISABLE_FIREBASE_PUSH) {
    // Push device status (and measurement will be pushed by background worker)
    FirebaseSync_PushStatusAndBattery();
  } else {
    Serial.println("Firebase push disabled: skipping PushStatusAndBattery");
  }
  UserDashboard_Show(MainUi_ShowMainScreen);
}

static void on_open_user_mode() {
  UserMode_Show();
}

// Local validator used when Firebase (and thus WiFi) is disabled.
static bool local_validate_user_id(const char *userId, char *errMsg, size_t errMsgSize) {
  if (errMsg && errMsgSize) errMsg[0] = '\0';
  if (!userId || !userId[0]) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "User ID rong");
    return false;
  }
  // Keep same basic check as UI: 5 digits
  if (strlen(userId) != 5) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "User ID phai dung 5 so");
    return false;
  }
  if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "OK");
  save_user_id_to_nvs(userId);
  FirebaseSync_SetCurrentUserId(userId);
  return true;
}

// ===== RTC sync policy =====
// Chỉ bật true 1 lần khi muốn ép set RTC theo thời gian compile,
// sau đó để lại false để tránh bị reset giờ mỗi lần nạp code.
static const bool RTC_FORCE_SET_ON_BOOT = false;

// NVS-backed RTC freeze detection.
// Luu timestamp RTC vao NVS dinh ky (5 phut).
// Khi boot, neu thoi gian RTC nho hon gia tri da luu qua nhieu
// (pin backup yeu/het -> RTC dung lai khi mat nguon) => ep set lai tu build time.
static Preferences rtcPref;
static const char *RTC_NVS_NAMESPACE    = "rtc";
static const uint32_t RTC_NVS_INTERVAL_MS = 300000;   // 5 phut ghi NVS 1 lan
static const int32_t  RTC_FREEZE_TOLERANCE_SEC = 60;  // sai lech toi da cho phep (giay)


static const int32_t RTC_TIMEZONE_OFFSET_SEC = 0;

static DateTime get_build_time_with_tz() {
  DateTime t(F(__DATE__), F(__TIME__));
  if (RTC_TIMEZONE_OFFSET_SEC != 0) {
    t = t + TimeSpan(RTC_TIMEZONE_OFFSET_SEC);
  }
  return t;
}

static bool rtc_time_looks_invalid(const DateTime &t) {
  // DS3231 hợp lệ lâu dài, nhưng với app này chỉ cần chặn giá trị rác.
  return (t.year() < 2024 || t.year() > 2099 ||
          t.month() < 1 || t.month() > 12 ||
          t.day() < 1 || t.day() > 31);
}

static void rtc_sync_if_needed() {
  if (!rtc_ok) return;

  bool need_adjust = RTC_FORCE_SET_ON_BOOT;

  if (rtc.lostPower()) {
    Serial.println("[RTC] lostPower() true -> will adjust from build time");
    need_adjust = true;
  }

  DateTime current = rtc.now();
  if (rtc_time_looks_invalid(current)) {
    Serial.println("[RTC] invalid datetime -> will adjust from build time");
    need_adjust = true;
  }

  // ===== NVS-based freeze detection =====
  // Khi pin backup DS3231 yeu/het, RTC ngung chay khi mat nguon chinh,
  // nhung lostPower() khong phat hien duoc. Ta kiem tra bang NVS timestamp.
  if (!need_adjust) {
    rtcPref.begin(RTC_NVS_NAMESPACE, false);
    uint32_t stored_ts = rtcPref.getULong("last_ts", 0);
    rtcPref.end();

    if (stored_ts != 0) {
      uint32_t current_ts = current.unixtime();
      // Neu thoi gian RTC nho hon stored_ts qua nhieu => RTC da dung lai khi mat nguon
      if (current_ts < stored_ts && (stored_ts - current_ts) > (uint32_t)RTC_FREEZE_TOLERANCE_SEC) {
        Serial.print("[RTC] FREEZE detected: stored_ts=");
        Serial.print(stored_ts);
        Serial.print(" current_ts=");
        Serial.print(current_ts);
        Serial.print(" diff=");
        Serial.print(stored_ts - current_ts);
        Serial.println("s -> adjusting from build time");
        need_adjust = true;
      } else {
        Serial.println("[RTC] NVS sanity check passed");
      }
    } else {
      Serial.println("[RTC] No NVS timestamp stored yet (first boot)");
    }
  }

  if (need_adjust) {
    DateTime build_time = get_build_time_with_tz();
    rtc.adjust(build_time);
    Serial.print("[RTC] Adjusted to: ");
    Serial.print(build_time.hour()); Serial.print(":");
    Serial.print(build_time.minute()); Serial.print(":");
    Serial.print(build_time.second()); Serial.print("  ");
    Serial.print(build_time.day()); Serial.print("/");
    Serial.print(build_time.month()); Serial.print("/");
    Serial.println(build_time.year());

    // Luu lai timestamp vua set vao NVS
    rtcPref.begin(RTC_NVS_NAMESPACE, false);
    rtcPref.putULong("last_ts", build_time.unixtime());
    rtcPref.end();
  } else {
    // RTC time OK, cap nhat stored_ts de lam moc so sanh cho lan boot sau
    rtcPref.begin(RTC_NVS_NAMESPACE, false);
    rtcPref.putULong("last_ts", current.unixtime());
    rtcPref.end();
  }
}

// ================= INA219 + Battery SOH/SOC bằng tích phân =================
// *** INA219 CLONE GIÁ RẺ: có thể dùng shunt khác (thường 0.01Ω thay vì 0.1Ω)***
// *** và ADC có thể không chính xác. Cần sanity check mạnh! ***

// Dung lượng danh định của pack pin (mAh).
// 2 cell nối tiếp (2S1P) => mAh giữ nguyên như 1 cell.
const float BATTERY_CAPACITY_mAh = 2500.0f;

// Ngưỡng điện áp pack pin Li-ion 2S.
// INA219 đang mắc: pin sau switch -> VIN+ -> VIN- -> IN+ buck.
// Vì vậy điện áp pack phía pin xấp xỉ: busVoltage + shuntVoltage.
const float BATTERY_2S_FULL_V = 8.40f;
const float BATTERY_2S_NEAR_FULL_V = 8.30f;
const float BATTERY_2S_EMPTY_V = 6.40f;
const float BATTERY_2S_CRITICAL_V = 6.00f;

// Ngưỡng sanity check: pack 2S Li-ion KHÔNG THỂ dưới 5.0V (2 cell x 2.5V)
// hoặc trên 9.0V. Nếu ngoài khoảng này => INA219 đọc sai.
const float BATTERY_SANITY_MIN_V = 5.0f;
const float BATTERY_SANITY_MAX_V = 9.5f;

// Nếu % tính bằng coulomb counter/NVS lệch quá xa so với điện áp,
// tự đồng bộ lại để tránh lỗi "pin thấp nhưng vẫn 100%".
const float BATTERY_SOC_RESYNC_DIFF_PERCENT = 25.0f;

// Hiệu chỉnh mềm mỗi lần update nếu coulomb counter bị trôi.
// Với INA219 clone, tăng độ nhạy correction
const float BATTERY_SOC_SOFT_CORRECTION_DIFF_PERCENT = 10.0f;  // giảm từ 15% xuống 10%
const float BATTERY_SOC_SOFT_CORRECTION_ALPHA = 0.15f;          // tăng từ 0.08 lên 0.15

// LPF (Low-Pass Filter) cho dòng điện - giảm nhiễu từ buck converter
const float CURRENT_LPF_ALPHA = 0.20f;  // 0.0=very smooth, 1.0=no filter

// LPF cho điện áp - ổn định voltage
const float VOLTAGE_LPF_ALPHA = 0.30f;

// Shunt resistor của INA219 clone (Adafruit gốc: 0.1 ohm, clone: thường 0.01 ohm)
const float SHUNT_RESISTOR_OHM = 0.01f;

// INA219
Adafruit_INA219 ina219;

// Dung lượng còn lại (mAh), sẽ đọc/lưu vào NVS
float batteryRemaining_mAh = BATTERY_CAPACITY_mAh;

// Biến thời gian để tích phân dòng
unsigned long lastMillis_batt = 0;

// Biến LPF: lưu giá trị đã lọc
static float filteredCurrent_mA = 0.0f;
static float filteredVoltage_V = 0.0f;
static bool   lpf_initialized = false;

// Nếu wiring làm cho chiều dòng ngược, đổi true/false cho phù hợp
// - Nếu XẢ → current_mA dương, SẠC → current_mA âm: để false
// - Nếu ngược lại thì set true
const bool INVERT_CURRENT = true;

// Đếm số lần voltage đọc bất thường liên tiếp để phát hiện INA219 chết
static int g_ina219_sanity_fail_count = 0;
static const int INA219_SANITY_FAIL_MAX = 5;  // sau 5 lần fail liên tiếp => tạm bỏ qua INA219
static bool g_ina219_sane = true;

// NVS
Preferences pref;
const char *NVS_NAMESPACE = "battery";
const char *NVS_KEY_QmAh  = "Q_mAh";

// Thời gian giữa 2 lần save NVS (ms)
const uint32_t NVS_SAVE_INTERVAL_MS = 10000; // 10 giây
static uint32_t last_nvs_save_ms = 0;

static float battery_percent_from_voltage_2s(float voltage_V) {
  // Bảng xấp xỉ SOC theo điện áp nghỉ của pack Li-ion 2S.
  // Khi đang tải nặng, điện áp có thể sụt nên kết quả chỉ dùng để khởi tạo/hiệu chỉnh.
  static const float voltageTable[] = {
    6.40f, 6.60f, 6.80f, 7.00f, 7.20f,
    7.40f, 7.60f, 7.80f, 8.00f, 8.20f, 8.40f
  };
  static const float percentTable[] = {
    0.0f, 5.0f, 10.0f, 15.0f, 25.0f,
    40.0f, 55.0f, 70.0f, 82.0f, 92.0f, 100.0f
  };

  if (!isfinite(voltage_V)) return NAN;
  if (voltage_V <= voltageTable[0]) return 0.0f;
  const size_t lastIdx = (sizeof(voltageTable) / sizeof(voltageTable[0])) - 1;
  if (voltage_V >= voltageTable[lastIdx]) return 100.0f;

  for (size_t i = 1; i <= lastIdx; ++i) {
    if (voltage_V <= voltageTable[i]) {
      float v0 = voltageTable[i - 1];
      float v1 = voltageTable[i];
      float p0 = percentTable[i - 1];
      float p1 = percentTable[i];
      float t = (voltage_V - v0) / (v1 - v0);
      return p0 + t * (p1 - p0);
    }
  }

  return 0.0f;
}

static float battery_mAh_from_percent(float percent) {
  if (!isfinite(percent)) return NAN;
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  return BATTERY_CAPACITY_mAh * percent / 100.0f;
}

static float battery_percent_from_mAh(float q_mAh) {
  if (!isfinite(q_mAh) || BATTERY_CAPACITY_mAh <= 0.0f) return NAN;
  float percent = 100.0f * q_mAh / BATTERY_CAPACITY_mAh;
  if (percent < 0.0f) percent = 0.0f;
  if (percent > 100.0f) percent = 100.0f;
  return percent;
}

static float battery_pack_voltage_from_ina219(float busVoltage_V, float shuntVoltage_mV) {
  if (!isfinite(busVoltage_V) || !isfinite(shuntVoltage_mV)) return NAN;
  return busVoltage_V + shuntVoltage_mV / 1000.0f;
}

static bool is_pack_voltage_sane(float packVoltage_V) {
  if (!isfinite(packVoltage_V)) return false;
  return (packVoltage_V >= BATTERY_SANITY_MIN_V && packVoltage_V <= BATTERY_SANITY_MAX_V);
}

static void sync_battery_counter_from_voltage_if_needed(float packVoltage_V, bool forceIfCritical) {
  if (!is_pack_voltage_sane(packVoltage_V)) return;  // không dùng điện áp sai để sync

  float voltagePercent = battery_percent_from_voltage_2s(packVoltage_V);
  if (!isfinite(voltagePercent)) return;

  float counterPercent = battery_percent_from_mAh(batteryRemaining_mAh);
  if (!isfinite(counterPercent)) {
    batteryRemaining_mAh = battery_mAh_from_percent(voltagePercent);
    return;
  }

  float diff = fabs(counterPercent - voltagePercent);

  if ((forceIfCritical && packVoltage_V <= BATTERY_2S_EMPTY_V) ||
      diff >= BATTERY_SOC_RESYNC_DIFF_PERCENT) {
    batteryRemaining_mAh = battery_mAh_from_percent(voltagePercent);
    Serial.print("[BAT] Resync Q from voltage: Vpack=");
    Serial.print(packVoltage_V, 3);
    Serial.print("V, SOC_voltage=");
    Serial.print(voltagePercent, 1);
    Serial.print("%, old_SOC=");
    Serial.print(counterPercent, 1);
    Serial.println("%");
  }
}

// ================= POWER SAVE / BACKLIGHT =================
static const uint32_t IDLE_OFF_MS = 45000; // 45s không chạm -> tắt backlight

static const uint32_t TAP_MIN_MS = 35;
static const uint32_t TAP_MAX_MS = 450;

static uint32_t g_wake_cooldown_until_ms = 0;
static const uint32_t WAKE_COOLDOWN_MS = 400;

static bool     g_screen_on = true;
static uint32_t g_last_activity_ms = 0;

static void note_activity() { g_last_activity_ms = millis(); }

static void backlight_set(bool on) {
  g_screen_on = on;
  digitalWrite(TFT_BL, on ? HIGH : LOW);

  if (on) {
    g_wake_cooldown_until_ms = millis() + WAKE_COOLDOWN_MS;
    note_activity();
  }
}

static void power_save_task() {
  uint32_t now = millis();
  if (g_screen_on && (now - g_last_activity_ms >= IDLE_OFF_MS)) {
    backlight_set(false);
  }
}

static void sound_button_task() {
  bool buttonState = digitalRead(BUTTON_SOUND_PIN);

  if (buttonState != lastButtonState) {
    lastButtonChangeMs = millis();
    lastButtonState = buttonState;
  }

  if ((millis() - lastButtonChangeMs) < BUTTON_DEBOUNCE_MS) {
    return;
  }

  // Nút dùng pull-up: nhấn = LOW
  static bool buttonHandledWhilePressed = false;
  if (buttonState == LOW && !buttonHandledWhilePressed) {
    buttonHandledWhilePressed = true;

    // Nếu âm thanh đang phát -> tắt âm thanh
    if (g_alert_sound_playing) {
      if (dfPlayerReady) {
        dfPlayer.stop();  // Dừng phát âm thanh
        Serial.println("[DFPlayer] Stopped alert sound");
      }
      g_alert_sound_playing = false;
      // Lưu ý: cuộc gọi/SMS vẫn tiếp tục, chỉ tắt âm thanh
    } else {
      // Âm thanh chưa phát -> phát file 001 và kích hoạt SOS
      if (dfPlayerReady) {
        dfPlayer.play(1);  // phát file 001.mp3 - âm thanh cảnh báo
        g_alert_sound_playing = true;
        Serial.println("[DFPlayer] play 001.mp3 - alert sound");
      } else {
        Serial.println("[DFPlayer] not ready, cannot play");
      }

      // Kích hoạt cuộc gọi khẩn cấp, SMS và còi loa từ SIM module
      const char* phone = GuestMode_GetPhone();
      if (phone && strlen(phone) > 0) {
        SimModule_TriggerSOS(phone, g_lastGpsLat, g_lastGpsLng, g_hasGpsLocation);
        Serial.printf("[SOS] Triggered call/SMS to %s, GPS_OK=%d\n", phone, g_hasGpsLocation);
      } else {
        Serial.println("[SOS] Failed: No phone number saved in settings!");
      }
    }

    // Cancel fallback timer if user presses button (user is responding to alert)
    if (hrspo2bp_is_fallback_pending()) {
      hrspo2bp_cancel_fallback();
      Serial.println("[BUTTON] Fallback cancelled by user button press");
    }
  } else if (buttonState == HIGH) {
    buttonHandledWhilePressed = false;
  }
}

static void handle_valid_tap() {
  uint32_t now = millis();

  if (now < g_wake_cooldown_until_ms) {
    note_activity();
    return;
  }

  if (g_screen_on) {
    note_activity();
    return;
  }

  backlight_set(true);
}

// ================= LVGL indev read =================
static void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  (void) indev_driver;

  static bool was_touched = false;
  static uint32_t touch_down_ms = 0;

  static uint16_t last_x_map = 0;
  static uint16_t last_y_map = 0;

  uint16_t x = 0, y = 0;
  bool touched = false;

  bool ok = ft6336u_read_touch(x, y, touched);
  if (!ok) touched = false;

  if (touched) {
    // Base mapping (calibrated for TFT_ROTATION == 1 landscape):
    // FT6336U reports coordinates in a 320x480 portrait orientation.
    // Convert to our LVGL landscape coordinate space (480x320).
    uint16_t x_map = y;
    uint16_t y_map = (SCREEN_HEIGHT - 1) - x;

    // If we flip the display 180° (rotation 3), we must flip touch too.
    if (TFT_ROTATION == 3) {
      x_map = (SCREEN_WIDTH - 1) - x_map;
      y_map = (SCREEN_HEIGHT - 1) - y_map;
    }

    if (x_map >= SCREEN_WIDTH)  x_map = SCREEN_WIDTH - 1;
    if (y_map >= SCREEN_HEIGHT) y_map = SCREEN_HEIGHT - 1;

    last_x_map = x_map;
    last_y_map = y_map;

    data->point.x = x_map;
    data->point.y = y_map;
    data->state   = LV_INDEV_STATE_PR;

    if (g_screen_on) note_activity();

    if (!was_touched) {
      touch_down_ms = millis();
      was_touched = true;
    }
    return;
  }

  data->state = LV_INDEV_STATE_REL;
  data->point.x = last_x_map;
  data->point.y = last_y_map;

  if (was_touched) {
    uint32_t dur = millis() - touch_down_ms;

    if (dur >= TAP_MIN_MS && dur <= TAP_MAX_MS) {
      handle_valid_tap();
    }

    was_touched = false;
    touch_down_ms = 0;
  }
}

// ================= LVGL FLUSH =================
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  int32_t x1 = area->x1;
  int32_t y1 = area->y1;
  int32_t x2 = area->x2;
  int32_t y2 = area->y2;

  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 >= (int32_t)SCREEN_WIDTH)  x2 = SCREEN_WIDTH - 1;
  if (y2 >= (int32_t)SCREEN_HEIGHT) y2 = SCREEN_HEIGHT - 1;

  uint32_t w = (x2 - x1 + 1);
  uint32_t h = (y2 - y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(x1, y1, w, h);
  tft.pushPixels((uint16_t *)&color_p->full, w * h);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

// UI has been moved to MainUi module (MainUi.h/.cpp)

// ================= format time =================
static void format_datetime(char *out, size_t out_sz, const DateTime &now) {
  snprintf(out, out_sz, "%02d:%02d  %02d/%02d/%04d",
           now.hour(), now.minute(),
           now.day(), now.month(), now.year());
}

static void update_battery_soc_from_ina219(char *out, size_t out_sz) {
  // Kiểm tra INA219 đã init chưa
  static bool ina_ok_checked = false;
  static bool ina_ok = false;
  if (!ina_ok_checked) {
    float vtest = ina219.getBusVoltage_V();
    // Với INA219 clone, chỉ cần không NaN và có địa chỉ I2C là coi như OK
    ina_ok = !isnan(vtest);
    ina_ok_checked = true;
  }
  if (!ina_ok) {
    snprintf(out, out_sz, "--%%");
    return;
  }

  // ===== Đọc thời gian và tính dt =====
  unsigned long now = millis();
  float dt_s = (now - lastMillis_batt) / 1000.0f;
  if (dt_s <= 0.0f) {
    dt_s = 0.001f; // 1ms tối thiểu
  }
  if (dt_s > 10.0f) {
    // Nếu dt_s > 10s, clamp về 10s thay vì gán 1s (giữ nguyên tỷ lệ dòng thực tế)
    dt_s = 10.0f;
  }
  lastMillis_batt = now;

  // ===== Đọc INA219 (RAW) =====
  float busVoltage_V    = ina219.getBusVoltage_V();
  float shuntVoltage_mV = ina219.getShuntVoltage_mV();

  // Nếu bất kỳ cái nào là NaN thì bỏ, không update
  if (isnan(busVoltage_V) || isnan(shuntVoltage_mV)) {
    // Tăng fail count nhưng vẫn giữ % cũ
    g_ina219_sanity_fail_count++;
    if (g_ina219_sanity_fail_count >= INA219_SANITY_FAIL_MAX) {
      g_ina219_sane = false;
    }
    // Format % hiện tại
    float curPct = battery_percent_from_mAh(batteryRemaining_mAh);
    if (!isfinite(curPct)) curPct = 0.0f;
    snprintf(out, out_sz, "%d%%", (int)(curPct + 0.5f));
    return;
  }

  // ===== Tính current từ shunt voltage (manual, vì shunt 0.01 ohm clone) =====
  // I = V_shunt / R_shunt
  // shuntVoltage_mV trong mV, SHUNT_RESISTOR_OHM trong ohm
  // current_mA = (shuntVoltage_mV / 1000.0f) / SHUNT_RESISTOR_OHM * 1000.0f
  //            = shuntVoltage_mV / SHUNT_RESISTOR_OHM
  float current_mA_raw = shuntVoltage_mV / SHUNT_RESISTOR_OHM;

  // ===== Tính pack voltage (RAW) =====
  float packVoltage_V = battery_pack_voltage_from_ina219(busVoltage_V, shuntVoltage_mV);

  // ===== SANITY CHECK voltage =====
  if (isnan(packVoltage_V) || !isfinite(packVoltage_V) || packVoltage_V < 0.5f) {
    // Điện áp gần 0V => INA219 đọc sai (clone chết hoặc mất kết nối)
    g_ina219_sanity_fail_count++;
    if (g_ina219_sanity_fail_count >= INA219_SANITY_FAIL_MAX) {
      g_ina219_sane = false;
    }
    // Không update Q, giữ nguyên % cũ
    float curPct = battery_percent_from_mAh(batteryRemaining_mAh);
    if (!isfinite(curPct)) curPct = 0.0f;
    snprintf(out, out_sz, "%d%%", (int)(curPct + 0.5f));
    Serial.print("[BAT] SANITY FAIL: Vpack=");
    Serial.print(packVoltage_V, 3);
    Serial.print("V (raw bus=");
    Serial.print(busVoltage_V, 3);
    Serial.print("V, shunt=");
    Serial.print(shuntVoltage_mV, 3);
    Serial.println("mV) - keeping old %");
    return;
  }

  // Reset fail count khi đọc OK
  g_ina219_sanity_fail_count = 0;
  g_ina219_sane = true;

  // ===== LPF (Low-Pass Filter) cho voltage =====
  if (!lpf_initialized) {
    filteredVoltage_V = packVoltage_V;
    lpf_initialized = true;
  } else {
    filteredVoltage_V = filteredVoltage_V * (1.0f - VOLTAGE_LPF_ALPHA) + packVoltage_V * VOLTAGE_LPF_ALPHA;
  }
  float packVoltage_filtered = filteredVoltage_V;

  // ===== Xử lý dòng điện =====
  // Nếu packVoltage_filtered nằm trong khoảng sane (5V-9.5V) mới dùng INA219 current
  float current_mA = 0.0f;
  if (is_pack_voltage_sane(packVoltage_filtered)) {
    current_mA = INVERT_CURRENT ? -current_mA_raw : current_mA_raw;

    // LPF cho dòng
    if (lpf_initialized) {
      filteredCurrent_mA = filteredCurrent_mA * (1.0f - CURRENT_LPF_ALPHA) + current_mA * CURRENT_LPF_ALPHA;
    } else {
      filteredCurrent_mA = current_mA;
      lpf_initialized = true;
    }
    current_mA = filteredCurrent_mA;
  } else {
    // Voltage không sane => không tin current, set = 0
    current_mA = 0.0f;
    filteredCurrent_mA = 0.0f;
  }

  // ===== Điện áp sau LPF (dùng cho SOC voltage estimate) =====
  float voltagePercent = battery_percent_from_voltage_2s(packVoltage_filtered);

  // ===== Tích phân dòng (coulomb counter) =====
  float delta_mAh = current_mA * dt_s / 3600.0f;

  if (isfinite(delta_mAh)) {
    batteryRemaining_mAh -= delta_mAh;
  }

  // Bảo vệ khi điện áp pack 2S xuống quá thấp: không cho NVS/counter báo 100%.
  if (isfinite(packVoltage_filtered) && packVoltage_filtered <= BATTERY_2S_EMPTY_V) {
    batteryRemaining_mAh = battery_mAh_from_percent(voltagePercent);
  }

  // Nếu gần đầy và đang sạc/không tải nhiều, cho phép kéo lên 100%.
  if (isfinite(packVoltage_filtered) &&
      packVoltage_filtered >= BATTERY_2S_NEAR_FULL_V &&
      current_mA <= 0.0f &&
      isfinite(voltagePercent)) {
    float qFromVoltage = battery_mAh_from_percent(voltagePercent);
    if (qFromVoltage > batteryRemaining_mAh) {
      batteryRemaining_mAh = qFromVoltage;
    }
  }

  // Giới hạn Q trong [0, capacity]
  if (!isfinite(batteryRemaining_mAh) || batteryRemaining_mAh < 0.0f) {
    batteryRemaining_mAh = 0.0f;
  }
  if (batteryRemaining_mAh > BATTERY_CAPACITY_mAh) {
    batteryRemaining_mAh = BATTERY_CAPACITY_mAh;
  }

  // Hiệu chỉnh mềm nếu counter bị trôi xa so với điện áp (chỉ khi voltage sane).
  if (is_pack_voltage_sane(packVoltage_filtered)) {
    float counterPercent = battery_percent_from_mAh(batteryRemaining_mAh);
    if (isfinite(voltagePercent) &&
        isfinite(counterPercent) &&
        fabs(counterPercent - voltagePercent) >= BATTERY_SOC_SOFT_CORRECTION_DIFF_PERCENT) {
      float qFromVoltage = battery_mAh_from_percent(voltagePercent);
      batteryRemaining_mAh =
        batteryRemaining_mAh * (1.0f - BATTERY_SOC_SOFT_CORRECTION_ALPHA) +
        qFromVoltage * BATTERY_SOC_SOFT_CORRECTION_ALPHA;
      Serial.print("[BAT] Soft correction: coulomb=");
      Serial.print(counterPercent, 1);
      Serial.print("%, voltage=");
      Serial.print(voltagePercent, 1);
      Serial.println("%");
    }
  }

  // ===== Tính % pin =====
  float batPercent_f = battery_percent_from_mAh(batteryRemaining_mAh);

  if (!isfinite(batPercent_f)) {
    batPercent_f = 0.0f;
  }
  if (batPercent_f < 0.0f)   batPercent_f = 0.0f;
  if (batPercent_f > 100.0f) batPercent_f = 100.0f;

  int batPercent = (int)(batPercent_f + 0.5f);
  if (batPercent < 0)   batPercent = 0;
  if (batPercent > 100) batPercent = 100;
  if (!DISABLE_FIREBASE_PUSH) {
    FirebaseSync_SetBatteryPercent(batPercent);
  }

  // Debug (RAW values for diagnostics)
  Serial.print("[BAT] ");
  if (!is_pack_voltage_sane(packVoltage_filtered)) {
    Serial.print("VOLTAGE_UNSTABLE ");
  }
  Serial.print("Vraw=");
  Serial.print(packVoltage_V, 3);
  Serial.print("V, Vfilt=");
  Serial.print(packVoltage_filtered, 3);
  Serial.print("V, Iraw=");
  Serial.print(current_mA_raw, 1);
  Serial.print("mA, Ifilt=");
  Serial.print(current_mA, 1);
  Serial.print("mA, Q=");
  Serial.print(batteryRemaining_mAh, 1);
  Serial.print("mAh, SOC=");
  Serial.print(batPercent_f, 1);
  Serial.print("%, SOC_volt=");
  if (isfinite(voltagePercent)) {
    Serial.print(voltagePercent, 1);
  } else {
    Serial.print("nan");
  }
  Serial.print("%, final=");
  Serial.print(batPercent);
  Serial.println("%");

  // Format chuỗi: "75%"
  snprintf(out, out_sz, "%d%%", batPercent);
}

// ============ Lưu dung lượng vào NVS mỗi NVS_SAVE_INTERVAL_MS =============
static void maybe_save_battery_to_nvs() {
  uint32_t now = millis();
  if (now - last_nvs_save_ms >= NVS_SAVE_INTERVAL_MS) {
    last_nvs_save_ms = now;
    pref.putFloat(NVS_KEY_QmAh, batteryRemaining_mAh);
    Serial.print("NVS save Q_mAh = ");
    Serial.println(batteryRemaining_mAh, 1);
  }
}

// Background task to initialize Firebase without blocking setup()
static void firebase_init_task(void *arg) {
  (void)arg;
#ifdef USE_FIREBASE_ESP_CLIENT
  Serial.println("[Firebase init task] Waiting for WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  Serial.println("[Firebase init task] WiFi connected");
  // configure Firebase client
  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_DB_URL;
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  // pass fbdo pointer into FirebaseSync so it can use the client library
  FirebaseSync_Init(WIFI_SSID,
                    WIFI_PASSWORD,
                    FIREBASE_DB_URL,
                    ui_get_device_id,
                    nullptr,
                    FIREBASE_PUSH_INTERVAL_MS,
                    (void*)&fbdo);
  // initial status push
  FirebaseSync_PushStatusAndBattery();
#else
  // For HTTP fallback, just init FirebaseSync (it will manage wifi non-blocking)
  FirebaseSync_Init(WIFI_SSID,
                    WIFI_PASSWORD,
                    FIREBASE_DB_URL,
                    ui_get_device_id,
                    nullptr,
                    FIREBASE_PUSH_INTERVAL_MS,
                    nullptr);
  FirebaseSync_PushStatusAndBattery();
#endif
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);
  load_user_id_from_nvs();

  // NVS / Preferences
    if (!pref.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to init NVS, using defaults");
  } else {
    float stored_Q = pref.getFloat(NVS_KEY_QmAh, BATTERY_CAPACITY_mAh);

    if (!isfinite(stored_Q) ||
        stored_Q < 0.0f ||
        stored_Q > BATTERY_CAPACITY_mAh * 1.2f) {
      stored_Q = BATTERY_CAPACITY_mAh;
    }

    batteryRemaining_mAh = stored_Q;
    Serial.print("Loaded Q_mAh from NVS: ");
    Serial.println(batteryRemaining_mAh, 1);
  }

  load_or_create_device_identity();

  // TFT init
  tft.init();
  tft.setRotation(TFT_ROTATION);

  pinMode(TFT_BL, OUTPUT);
  pinMode(BUTTON_SOUND_PIN, INPUT_PULLUP);
  backlight_set(true);
  note_activity();

  // DFPlayer init
  dfplayer_setup();

  tft.fillScreen(TFT_BLACK);

  // I2C (chung cho Touch + RTC + INA219)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  // RTC init
  rtc_ok = rtc.begin();
  Serial.println(rtc_ok ? "DS3231 OK" : "DS3231 not found");
  if (rtc_ok) {
    rtc_sync_if_needed();
  }

  // INA219 init
  if (!ina219.begin()) {
    Serial.println("Khong tim thay INA219!");
  } else {
    // Calibration: 32V, 2A (cho shunt 0.1 ohm).
    // Với shunt 0.01 ohm (clone), max current thực tế = 20A.
    // Ta sẽ tự tính current từ shunt voltage, không dùng getCurrent_mA().
    ina219.setCalibration_32V_2A();

    float busVoltage_V = ina219.getBusVoltage_V();
    float shuntVoltage_mV = ina219.getShuntVoltage_mV();
    float packVoltage_V = battery_pack_voltage_from_ina219(busVoltage_V, shuntVoltage_mV);

    // Chỉ sync nếu voltage sane
    if (is_pack_voltage_sane(packVoltage_V)) {
      sync_battery_counter_from_voltage_if_needed(packVoltage_V, true);
    } else {
      Serial.print("[BAT] WARNING: INA219 boot voltage out of range: ");
      Serial.println(packVoltage_V, 3);
    }

    Serial.print("[BAT] INA219 OK, Vbus=");
    Serial.print(busVoltage_V, 3);
    Serial.print("V, Vshunt=");
    Serial.print(shuntVoltage_mV, 3);
    Serial.print("mV, Vpack=");
    Serial.print(packVoltage_V, 3);
    Serial.print("V, Q=");
    Serial.print(batteryRemaining_mAh, 1);
    Serial.println("mAh");

    lastMillis_batt = millis();
  }

  // HR/SpO2 and BP module init
  hrspo2bp_setup();

  // GPS init
  gps_setup();

  // LVGL init
  lv_init();

  // LVGL tick 1ms
  const esp_timer_create_args_t tick_args = {
    .callback = &lv_tick_task,
    .arg = nullptr,
    .dispatch_method = ESP_TIMER_TASK,
    .name = "lvgl_tick"
  };
  esp_timer_create(&tick_args, &lvgl_tick_timer);
  esp_timer_start_periodic(lvgl_tick_timer, 1000);

  // Display driver
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * LVGL_DRAW_BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // Touch driver
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  // Do not provide a user-id getter or saver: user id is no longer stored locally
  MainUi_Init(nullptr, ui_get_device_id, nullptr, on_open_user_mode);
  if (!DISABLE_FIREBASE_PUSH) {
        // create the FreeRTOS task to initialize Firebase in background
        BaseType_t res = xTaskCreate(firebase_init_task, "fb_init", 4096, NULL, 1, NULL);
    if (res != pdPASS) {
      Serial.println("[Firebase] Failed to create fb_init task, falling back to inline init");
      // fallback to inline (blocking) init to preserve behavior
#ifdef USE_FIREBASE_ESP_CLIENT
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      while (WiFi.status() != WL_CONNECTED) { delay(500); }
      config.api_key = FIREBASE_API_KEY;
      config.database_url = FIREBASE_DB_URL;
      config.token_status_callback = tokenStatusCallback;
      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);
      FirebaseSync_Init(WIFI_SSID, WIFI_PASSWORD, FIREBASE_DB_URL, ui_get_device_id, nullptr, FIREBASE_PUSH_INTERVAL_MS, (void*)&fbdo);
      FirebaseSync_PushStatusAndBattery();
#else
      FirebaseSync_Init(WIFI_SSID, WIFI_PASSWORD, FIREBASE_DB_URL, ui_get_device_id, nullptr, FIREBASE_PUSH_INTERVAL_MS, nullptr);
      FirebaseSync_PushStatusAndBattery();
#endif
    }
  } else {
    Serial.println("Firebase disabled by flag: not initializing FirebaseSync");
  }
  UserMode_Init(nullptr,
                nullptr,
                on_user_mode_back,
                (usermode_validate_cb_t)(DISABLE_FIREBASE_PUSH ? local_validate_user_id : FirebaseSync_ValidateUserId),
                on_user_mode_success);

  // Khởi tạo SIM module
  SimModule_Init();
  // Load GuestMode settings (phone number, thresholds) at boot so SOS works even if GuestMode UI hasn't been shown yet
  GuestMode_Init();
  // Load UserDashboard settings (phone number, thresholds) at boot so warning logic works
  UserDashboard_Init();

  // Day trang thai ban dau len Firebase (skipped if disabled).
  if (!DISABLE_FIREBASE_PUSH) {
    FirebaseSync_PushStatusAndBattery();
  } else {
    Serial.println("Firebase disabled: initial push skipped");
  }
}

void loop() {
  lv_timer_handler();
  delay(1);

  // Run HR/SpO2 background loop (updates spo2/heartRate)
  hrspo2bp_loop();
  // Check fallback timer (call default SOS number if 1 min elapsed without cancellation)
  hrspo2bp_fallback_loop();
  gps_loop();
  sound_button_task();
  SimModule_Loop();

  power_save_task();
  GuestMode_Loop();
  UserDashboard_Loop();
  if (!DISABLE_FIREBASE_PUSH) {
    FirebaseSync_Loop();
  }

  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();

    // time string
    char tbuf[32];
    if (rtc_ok) {
      DateTime now = rtc.now();
      format_datetime(tbuf, sizeof(tbuf), now);
    } else {
      snprintf(tbuf, sizeof(tbuf), "--:--  --/--/----");
    }

    // battery string
    char bbuf[16];
    update_battery_soc_from_ina219(bbuf, sizeof(bbuf));

    MainUi_UpdateStatus(tbuf, bbuf);

    // Lưu NVS định kỳ (pin)
    maybe_save_battery_to_nvs();

    // Lưu timestamp RTC vao NVS dinh ky (5 phut) de phat hien RTC treo
    if (rtc_ok) {
      static uint32_t last_rtc_nvs_ms = 0;
      if (millis() - last_rtc_nvs_ms >= RTC_NVS_INTERVAL_MS) {
        last_rtc_nvs_ms = millis();
        DateTime rtcnow = rtc.now();
        rtcPref.begin(RTC_NVS_NAMESPACE, false);
        rtcPref.putULong("last_ts", rtcnow.unixtime());
        rtcPref.end();
        Serial.println("[RTC] Timestamp saved to NVS");
      }
    }
  }
}