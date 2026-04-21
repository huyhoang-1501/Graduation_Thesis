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

// ===== ICONS =====
#include "monitoring_icon.h"       // monitoring_icon
#include "wifi_icon.h"             // wifi_icon
#include "keypad.h"
#include "GuestMode.h"

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

// ================= Fonts fallback =================
static const lv_font_t* pick_font_40_or_14() {
#if defined(LV_FONT_MONTSERRAT_40) && (LV_FONT_MONTSERRAT_40 == 1)
  return &lv_font_montserrat_40;
#else
  return &lv_font_montserrat_14;
#endif
}
static const lv_font_t* pick_font_20_or_14() {
#if defined(LV_FONT_MONTSERRAT_20) && (LV_FONT_MONTSERRAT_20 == 1)
  return &lv_font_montserrat_20;
#else
  return &lv_font_montserrat_14;
#endif
}
static const lv_font_t* pick_font_18_or_14() {
#if defined(LV_FONT_MONTSERRAT_18) && (LV_FONT_MONTSERRAT_18 == 1)
  return &lv_font_montserrat_18;
#else
  return &lv_font_montserrat_14;
#endif
}
static const lv_font_t* pick_font_16_or_14() {
#if defined(LV_FONT_MONTSERRAT_16) && (LV_FONT_MONTSERRAT_16 == 1)
  return &lv_font_montserrat_16;
#else
  return &lv_font_montserrat_14;
#endif
}
static const lv_font_t* pick_font_14() { return &lv_font_montserrat_14; }

// ================= MAIN GUI objects =================
static lv_obj_t *label_time = nullptr;
static lv_obj_t *label_batt = nullptr;
static lv_obj_t *main_label_time = nullptr;
static lv_obj_t *main_label_batt = nullptr;
static lv_obj_t *btn_guest  = nullptr;
static lv_obj_t *btn_user   = nullptr;

static lv_obj_t *main_scr   = nullptr;

static void show_main_screen();
static void show_keypad_screen();

static void on_keypad_back() {
  show_main_screen();
}

static void on_keypad_next(const char *text) {
  Serial.print("NEXT = ");
  Serial.println(text ? text : "");
}

static void user_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  show_keypad_screen();
}

static void guest_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  GuestMode_Show(show_main_screen);
}

static void show_keypad_screen() {
  static bool keypad_inited = false;
  if (!keypad_inited) {
    keypad_init_screen(pick_font_20_or_14(),
                       pick_font_16_or_14(),
                       on_keypad_back,
                       on_keypad_next);
    keypad_inited = true;
  }

  lv_obj_t *scr = keypad_get_screen();
  if (scr) lv_scr_load(scr);
}

static void show_main_screen() {
  if (main_scr) {
    lv_scr_load(main_scr);
  }
}

static lv_obj_t* create_status_bar(lv_obj_t *parent, lv_color_t primary, lv_color_t dark, lv_color_t card,
                                   lv_obj_t **out_time, lv_obj_t **out_batt) {
  lv_obj_t *status = lv_obj_create(parent);
  lv_obj_set_size(status, lv_pct(100), 44);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_bg_color(status, card, 0);
  lv_obj_set_style_radius(status, 14, 0);
  lv_obj_set_style_border_width(status, 2, 0);
  lv_obj_set_style_border_color(status, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_pad_left(status, 12, 0);
  lv_obj_set_style_pad_right(status, 12, 0);
  lv_obj_set_style_shadow_width(status, 0, 0);

  lv_obj_t *hcmute_box = lv_obj_create(status);
  lv_obj_set_size(hcmute_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(hcmute_box, LV_ALIGN_LEFT_MID, -5, 0);

  lv_obj_set_style_bg_opa(hcmute_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hcmute_box, 2, 0);
  lv_obj_set_style_border_color(hcmute_box, lv_color_make(220, 40, 40), 0);
  lv_obj_set_style_radius(hcmute_box, 10, 0);
  lv_obj_set_style_pad_left(hcmute_box, 10, 0);
  lv_obj_set_style_pad_right(hcmute_box, 10, 0);
  lv_obj_set_style_pad_top(hcmute_box, 5, 0);
  lv_obj_set_style_pad_bottom(hcmute_box, 5, 0);

  lv_obj_t *label_hcmute = lv_label_create(hcmute_box);
  lv_label_set_text(label_hcmute, "HCM-UTE");
  lv_obj_set_style_text_color(label_hcmute, primary, 0);
  lv_obj_set_style_text_font(label_hcmute, pick_font_18_or_14(), 0);
  lv_obj_center(label_hcmute);

  lv_obj_t *batt = lv_label_create(status);
  lv_label_set_text(batt, "--%");
  lv_obj_set_style_text_color(batt, dark, 0);
  lv_obj_set_style_text_font(batt, pick_font_16_or_14(), 0);
  lv_obj_align(batt, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *img_wifi = lv_img_create(status);
  lv_img_set_src(img_wifi, &wifi_icon);
  lv_obj_align_to(img_wifi, batt, LV_ALIGN_OUT_LEFT_MID, -20, 0);
  lv_obj_set_style_img_recolor(img_wifi, lv_color_make(0,0,0), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(img_wifi, 255, LV_PART_MAIN);
  lv_obj_set_style_opa(img_wifi, 255, LV_PART_MAIN);

  lv_obj_t *time_lbl = lv_label_create(status);
  lv_label_set_text(time_lbl, "--:--  --/--/----");
  lv_obj_set_style_text_color(time_lbl, dark, 0);
  lv_obj_set_style_text_font(time_lbl, pick_font_16_or_14(), 0);
  lv_obj_align(time_lbl, LV_ALIGN_RIGHT_MID, -70, 0);

  if (out_time) *out_time = time_lbl;
  if (out_batt) *out_batt = batt;
  return status;
}

static lv_obj_t* create_metric_card(lv_obj_t *parent,
                                    const char *title,
                                    const char *unit,
                                    lv_coord_t col,
                                    lv_coord_t row,
                                    lv_color_t title_color,
                                    lv_obj_t **value_out) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1,
                       LV_GRID_ALIGN_STRETCH, row, 1);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_bg_color(card, lv_color_white(), 0);
  lv_obj_set_style_shadow_width(card, 0, 0);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *ttl = lv_label_create(card);
  lv_label_set_text(ttl, title);
  lv_obj_set_style_text_color(ttl, title_color, 0);
  lv_obj_set_style_text_font(ttl, pick_font_16_or_14(), 0);
  lv_obj_align(ttl, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *val = lv_label_create(card);
  lv_label_set_text(val, "--");
  lv_obj_set_style_text_color(val, lv_color_make(15, 75, 110), 0);
  lv_obj_set_style_text_font(val, pick_font_40_or_14(), 0);
  lv_obj_align(val, LV_ALIGN_LEFT_MID, 0, 4);

  lv_obj_t *u = lv_label_create(card);
  lv_label_set_text(u, unit);
  lv_obj_set_style_text_color(u, lv_color_make(90, 120, 140), 0);
  lv_obj_set_style_text_font(u, pick_font_14(), 0);
  lv_obj_align(u, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  if (value_out) *value_out = val;
  return card;
}

static lv_obj_t *guest_label_state = nullptr;
static lv_obj_t *guest_label_spo2  = nullptr;
static lv_obj_t *guest_label_hr    = nullptr;
static lv_obj_t *guest_label_sys   = nullptr;
static lv_obj_t *guest_label_dia   = nullptr;
static bool     guest_measuring = false;
static uint32_t guest_measure_start_ms = 0;

static void create_guest_screen() {
  static bool guest_created = false;
  if (guest_created) return;

  GuestMode_Show(show_main_screen);
  guest_created = true;
}

static void ui_set_status(const char *time_str, const char *batt_str) {
  if (main_label_time) lv_label_set_text(main_label_time, time_str);
  if (main_label_batt) lv_label_set_text(main_label_batt, batt_str);
  GuestMode_UpdateStatus(time_str, batt_str);

  if (label_time) lv_label_set_text(label_time, time_str);
  if (label_batt) lv_label_set_text(label_batt, batt_str);
}

// Create Main GUI 
static void create_main_gui() {
  main_scr = lv_obj_create(NULL);
  lv_obj_t *scr = main_scr;

  lv_color_t bg      = lv_color_make(245, 252, 255);
  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark    = lv_color_make(10, 60, 90);
  lv_color_t card    = lv_color_white();

  lv_obj_set_style_bg_color(scr, bg, 0);
  lv_obj_set_style_pad_all(scr, 12, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // ===== STATUS BAR =====
  create_status_bar(scr, primary, dark, card, &main_label_time, &main_label_batt);

  // ===== FRAME CHỨA TEXT + ẢNH =====
  lv_obj_t *frame = lv_obj_create(scr);
  lv_obj_set_size(frame, lv_pct(100), 160);
  lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 50);

  lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(frame, 3, 0);
  lv_obj_set_style_border_color(frame, primary, 0);
  lv_obj_set_style_radius(frame, 20, 0);
  lv_obj_set_style_pad_all(frame, 10, 0);
  lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

  // ICON MÀN HÌNH BÊN PHẢI TRONG FRAME
  lv_obj_t *img_monitor = lv_img_create(frame);
  lv_img_set_src(img_monitor, &monitoring_icon);
  lv_obj_align(img_monitor, LV_ALIGN_RIGHT_MID, -10, 0);

  // NHÓM TEXT "Health / Monitoring" BÊN TRÁI
  lv_obj_t *text_cont = lv_obj_create(frame);
  lv_obj_set_size(text_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(text_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(text_cont, 0, 0);
  lv_obj_align(text_cont, LV_ALIGN_LEFT_MID, -10, 0);

  lv_obj_t *label_health = lv_label_create(text_cont);
  lv_label_set_text(label_health, "Health");
  lv_obj_set_style_text_color(label_health, primary, 0);
  lv_obj_set_style_text_font(label_health, pick_font_40_or_14(), 0);
  lv_obj_align(label_health, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *label_monitor = lv_label_create(text_cont);
  lv_label_set_text(label_monitor, "Monitoring");
  lv_obj_set_style_text_color(label_monitor, primary, 0);
  lv_obj_set_style_text_font(label_monitor, pick_font_40_or_14(), 0);
  lv_obj_align_to(label_monitor, label_health, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);

  // ===== CỤM NÚT Guest/User =====
  lv_obj_t *cont_btn = lv_obj_create(scr);
  lv_obj_set_size(cont_btn, lv_pct(100), 75);
  lv_obj_align(cont_btn, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_opa(cont_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_btn, 0, 0);
  lv_obj_set_style_pad_all(cont_btn, 0, 0);
  lv_obj_set_style_pad_column(cont_btn, 12, 0);
  lv_obj_clear_flag(cont_btn, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(cont_btn, col, row);

  // Guest
  btn_guest = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_guest, LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_guest, 16, 0);
  lv_obj_set_style_bg_color(btn_guest, card, 0);
  lv_obj_set_style_bg_color(btn_guest, lv_color_make(210, 245, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_guest, 3, 0);
  lv_obj_set_style_border_color(btn_guest, primary, 0);
  lv_obj_set_style_shadow_width(btn_guest, 0, 0);
  lv_obj_set_style_pad_top(btn_guest, 6, 0);
  lv_obj_set_style_pad_bottom(btn_guest, 6, 0);

  lv_obj_t *lg = lv_label_create(btn_guest);
  lv_label_set_text(lg, "Guest");
  lv_obj_center(lg);
  lv_obj_set_style_text_color(lg, dark, 0);
  lv_obj_set_style_text_font(lg, pick_font_20_or_14(), 0);
  lv_obj_add_event_cb(btn_guest, guest_btn_event_cb, LV_EVENT_CLICKED, NULL);

  // User
  btn_user = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_user, LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_user, 16, 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(0, 140, 200), 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(0, 175, 235), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_user, 0, 0);
  lv_obj_set_style_shadow_width(btn_user, 0, 0);
  lv_obj_set_style_pad_top(btn_user, 6, 0);
  lv_obj_set_style_pad_bottom(btn_user, 6, 0);

  lv_obj_t *lu = lv_label_create(btn_user);
  lv_label_set_text(lu, "User");
  lv_obj_center(lu);
  lv_obj_set_style_text_color(lu, lv_color_white(), 0);
  lv_obj_set_style_text_font(lu, pick_font_20_or_14(), 0);

  lv_obj_add_event_cb(btn_user, user_btn_event_cb, LV_EVENT_CLICKED, NULL);

  ui_set_status("--:--  --/--/----", "--%");

  lv_scr_load(main_scr);
}

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

  create_main_gui();
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

    ui_set_status(tbuf, bbuf);

    // Lưu NVS định kỳ
    maybe_save_battery_to_nvs();
  }
}
