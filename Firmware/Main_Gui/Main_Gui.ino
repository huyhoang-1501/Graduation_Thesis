#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"

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

// ================= DISPLAY =================
static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

#define TFT_BL 15
TFT_eSPI tft;

// ================= LVGL BUFFER =================
static lv_color_t buf1[SCREEN_WIDTH * 40];
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
static bool rtc_ok = false;

// ================= DEVICE ID / PAIRING DEMO =================
static Preferences devicePref;
static bool devicePrefReady = false;
static const char *DEVICE_NVS_NAMESPACE = "device";
static const char *DEVICE_NVS_KEY_ID    = "device_id";
static const char *DEVICE_NVS_KEY_USER  = "user_id";

static char g_device_id[24] = "DEV-UNKNOWN";
static char g_user_id[24] = "";

static void generate_device_id(char *out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  unsigned long long mac = (unsigned long long)ESP.getEfuseMac();
  snprintf(out, out_sz, "DEV-%012llX", (mac & 0xFFFFFFFFFFFFULL));
}

static void load_or_create_device_identity() {
  if (!devicePref.begin(DEVICE_NVS_NAMESPACE, false)) {
    Serial.println("Failed to init device NVS, using RAM-only device id");
    devicePrefReady = false;
    generate_device_id(g_device_id, sizeof(g_device_id));
    return;
  }

  devicePrefReady = true;

  String storedId = devicePref.getString(DEVICE_NVS_KEY_ID, "");
  String storedUser = devicePref.getString(DEVICE_NVS_KEY_USER, "");

  if (storedId.length() == 0) {
    generate_device_id(g_device_id, sizeof(g_device_id));
    devicePref.putString(DEVICE_NVS_KEY_ID, g_device_id);
  } else {
    storedId.toCharArray(g_device_id, sizeof(g_device_id));
  }

  if (storedUser.length() > 0) {
    storedUser.toCharArray(g_user_id, sizeof(g_user_id));
  } else {
    g_user_id[0] = '\0';
  }

  Serial.print("Device ID: ");
  Serial.println(g_device_id);
}

static void save_user_id(const char *userId) {
  if (!userId || !*userId) return;
  strncpy(g_user_id, userId, sizeof(g_user_id) - 1);
  g_user_id[sizeof(g_user_id) - 1] = '\0';
  if (devicePrefReady) {
    devicePref.putString(DEVICE_NVS_KEY_USER, g_user_id);
  }
  Serial.print("Saved User ID: ");
  Serial.println(g_user_id);
}

static const char *ui_get_device_id() {
  return g_device_id;
}

static const char *ui_get_user_id() {
  return g_user_id;
}

// ===== RTC sync policy =====
// Chỉ bật true 1 lần khi muốn ép set RTC theo thời gian compile,
// sau đó để lại false để tránh bị reset giờ mỗi lần nạp code.
static const bool RTC_FORCE_SET_ON_BOOT = false;

// Nếu toolchain của bạn compile theo UTC và muốn cộng múi giờ VN (+7h),
// đặt thành 7 * 3600. Mặc định 0 (dùng giờ local của máy build).
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
    Serial.println("RTC lost power -> will adjust from build time");
    need_adjust = true;
  }

  DateTime current = rtc.now();
  if (rtc_time_looks_invalid(current)) {
    Serial.println("RTC invalid datetime -> will adjust from build time");
    need_adjust = true;
  }

  if (need_adjust) {
    DateTime build_time = get_build_time_with_tz();
    rtc.adjust(build_time);
    Serial.print("RTC adjusted to: ");
    Serial.print(build_time.hour()); Serial.print(":");
    Serial.print(build_time.minute()); Serial.print(":");
    Serial.print(build_time.second()); Serial.print("  ");
    Serial.print(build_time.day()); Serial.print("/");
    Serial.print(build_time.month()); Serial.print("/");
    Serial.println(build_time.year());
  }
}

// ================= INA219 + Battery SOH/SOC bằng tích phân =================

// Dung lượng danh định của pack pin (mAh).
// 2 cell nối tiếp (2S1P) => mAh giữ nguyên như 1 cell.
const float BATTERY_CAPACITY_mAh = 2600.0f;

// INA219
Adafruit_INA219 ina219;

// Dung lượng còn lại (mAh), sẽ đọc/lưu vào NVS
float batteryRemaining_mAh = BATTERY_CAPACITY_mAh;

// Biến thời gian để tích phân dòng
unsigned long lastMillis_batt = 0;

// Nếu wiring làm cho chiều dòng ngược, đổi true/false cho phù hợp
// - Nếu XẢ → current_mA dương, SẠC → current_mA âm: để false
// - Nếu ngược lại thì set true
const bool INVERT_CURRENT = false;

// NVS
Preferences pref;
const char *NVS_NAMESPACE = "battery";
const char *NVS_KEY_QmAh  = "Q_mAh";

// Thời gian giữa 2 lần save NVS (ms)
const uint32_t NVS_SAVE_INTERVAL_MS = 10000; // 10 giây
static uint32_t last_nvs_save_ms = 0;

// ================= POWER SAVE / BACKLIGHT =================
static const uint32_t IDLE_OFF_MS = 30000; // 30s không chạm -> tắt backlight

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
    uint16_t x_map = y;
    uint16_t y_map = (SCREEN_HEIGHT - 1) - x;

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
  // Nếu INA219 chưa khởi tạo được thì báo "--%"
  static bool ina_ok_checked = false;
  static bool ina_ok = false;
  if (!ina_ok_checked) {
    // Giả sử nếu begin() trong setup fail, ta in ra Serial nhưng vẫn chạy.
    // Ở đây ta coi như nếu điện áp đọc được là NaN thì coi như fail.
    float vtest = ina219.getBusVoltage_V();
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
  if (dt_s <= 0.0f || dt_s > 10.0f) {
    // Nếu dt_s bất thường (âm hoặc lớn hơn 10s), bỏ qua để tránh nhảy ác
    dt_s = 1.0f;
  }
  lastMillis_batt = now;

  // ===== Đọc INA219 =====
  float busVoltage_V    = ina219.getBusVoltage_V();
  float shuntVoltage_mV = ina219.getShuntVoltage_mV();
  float current_mA_raw  = ina219.getCurrent_mA();

  // Nếu bất kỳ cái nào là NaN thì bỏ, không update
  if (isnan(busVoltage_V) || isnan(shuntVoltage_mV) || isnan(current_mA_raw)) {
    snprintf(out, out_sz, "--%%");
    return;
  }

  float current_mA = INVERT_CURRENT ? -current_mA_raw : current_mA_raw;

  // ===== Tích phân dòng =====
  float delta_mAh = current_mA * dt_s / 3600.0f;

  if (!isnan(delta_mAh) && isfinite(delta_mAh)) {
    batteryRemaining_mAh -= delta_mAh;
  }

  // Giới hạn Q trong [0, capacity]
  if (!isfinite(batteryRemaining_mAh) || batteryRemaining_mAh < 0.0f) {
    batteryRemaining_mAh = 0.0f;
  }
  if (batteryRemaining_mAh > BATTERY_CAPACITY_mAh) {
    batteryRemaining_mAh = BATTERY_CAPACITY_mAh;
  }

  // ===== Tính % pin =====
  float batPercent_f = 0.0f;

  if (BATTERY_CAPACITY_mAh > 0.0f) {
    batPercent_f = 100.0f * batteryRemaining_mAh / BATTERY_CAPACITY_mAh;
  }

  // Nếu NaN hoặc vô cực thì coi như 0%
  if (!isfinite(batPercent_f)) {
    batPercent_f = 0.0f;
  }

  if (batPercent_f < 0.0f)   batPercent_f = 0.0f;
  if (batPercent_f > 100.0f) batPercent_f = 100.0f;

  // Làm tròn
  int batPercent = (int)(batPercent_f + 0.5f);
  if (batPercent < 0)   batPercent = 0;
  if (batPercent > 100) batPercent = 100;

  // Debug
  Serial.print("Q = ");
  Serial.print(batteryRemaining_mAh, 1);
  Serial.print(" mAh, SoC = ");
  Serial.print(batPercent);
  Serial.println(" %");

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

void setup() {
  Serial.begin(115200);

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
  tft.setRotation(1);

  pinMode(TFT_BL, OUTPUT);
  backlight_set(true);
  note_activity();

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
    // Calibration: 32V, 1A (tùy tải)
    ina219.setCalibration_32V_1A();
    lastMillis_batt = millis();
  }

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
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 40);

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

  MainUi_Init(save_user_id, ui_get_device_id, ui_get_user_id);
}

void loop() {
  lv_timer_handler();
  delay(5);

  power_save_task();
  GuestMode_Loop();

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

    // Lưu NVS định kỳ
    maybe_save_battery_to_nvs();
  }
}
