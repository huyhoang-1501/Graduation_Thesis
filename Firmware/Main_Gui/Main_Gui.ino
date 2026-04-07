#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"

#define LV_CONF_SKIP
#include "lv_conf.h"
#include <lvgl.h>

#include "RTClib.h"

#include "monitoring_icon.h"       // monitoring_icon
#include "wifi_icon.h"  // wifi_icon

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

// ================= MAX17043 (Battery gauge) =================
#define MAX17043_ADDR 0x32
#define VCELL_REG   0x02
#define SOC_REG     0x04
#define MODE_REG    0x06

static bool max17043_ok = false;

static bool i2c_device_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

static void max17043_reset() {
  Wire.beginTransmission(MAX17043_ADDR);
  Wire.write(MODE_REG);
  Wire.write(0x54);
  Wire.write(0x00);
  Wire.endTransmission();
}

static void max17043_quickStart() {
  Wire.beginTransmission(MAX17043_ADDR);
  Wire.write(MODE_REG);
  Wire.write(0x40);
  Wire.write(0x00);
  Wire.endTransmission();
}

// trả về mV
static float max17043_readVoltage_mV() {
  Wire.beginTransmission(MAX17043_ADDR);
  Wire.write(VCELL_REG);
  Wire.endTransmission();

  if (Wire.requestFrom(MAX17043_ADDR, (uint8_t)2) != 2) return NAN;

  uint16_t value = (Wire.read() << 8) | Wire.read();
  return (value >> 4) * 1.25f; // mỗi bit = 1.25mV
}

// trả về %
static float max17043_readSOC_percent() {
  Wire.beginTransmission(MAX17043_ADDR);
  Wire.write(SOC_REG);
  Wire.endTransmission();

  if (Wire.requestFrom(MAX17043_ADDR, (uint8_t)2) != 2) return NAN;

  uint16_t value = (Wire.read() << 8) | Wire.read();
  float percent = value >> 8;
  percent += (value & 0xFF) / 256.0f;
  return percent;
}

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
static const lv_font_t* pick_font_14() { return &lv_font_montserrat_14; }

// ================= MAIN GUI objects =================
static lv_obj_t *label_time = nullptr;
static lv_obj_t *label_batt = nullptr;
static lv_obj_t *btn_guest  = nullptr;
static lv_obj_t *btn_user   = nullptr;

// Update status bar
static void ui_set_status(const char *time_str, const char *batt_str) {
  if (label_time) lv_label_set_text(label_time, time_str);
  if (label_batt) lv_label_set_text(label_batt, batt_str);
}

// Create Main GUI 
static void create_main_gui() {
  lv_obj_t *scr = lv_scr_act();

  lv_color_t bg      = lv_color_make(245, 252, 255);
  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark    = lv_color_make(10, 60, 90);
  lv_color_t card    = lv_color_white();

  lv_obj_set_style_bg_color(scr, bg, 0);
  lv_obj_set_style_pad_all(scr, 12, 0);

  // ===== STATUS BAR =====
  lv_obj_t *status = lv_obj_create(scr);
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

  // HCMUTE badge
  lv_obj_t *hcmute_box = lv_obj_create(status);
  lv_obj_set_size(hcmute_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(hcmute_box, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_set_style_bg_opa(hcmute_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hcmute_box, 2, 0);
  lv_obj_set_style_border_color(hcmute_box, lv_color_make(220, 40, 40), 0);
  lv_obj_set_style_radius(hcmute_box, 10, 0);
  lv_obj_set_style_pad_left(hcmute_box, 10, 0);
  lv_obj_set_style_pad_right(hcmute_box, 10, 0);
  lv_obj_set_style_pad_top(hcmute_box, 5, 0);
  lv_obj_set_style_pad_bottom(hcmute_box, 5, 0);

  lv_obj_t *label_hcmute = lv_label_create(hcmute_box);
  lv_label_set_text(label_hcmute, "HCMUTE");
  lv_obj_set_style_text_color(label_hcmute, primary, 0);
  lv_obj_set_style_text_font(label_hcmute, pick_font_18_or_14(), 0);
  lv_obj_center(label_hcmute);

  // Battery
  label_batt = lv_label_create(status);
  lv_label_set_text(label_batt, "--%");
  lv_obj_set_style_text_color(label_batt, dark, 0);
  lv_obj_set_style_text_font(label_batt, pick_font_18_or_14(), 0);
  lv_obj_align(label_batt, LV_ALIGN_RIGHT_MID, 0, 0);

  // WiFi icon cạnh phần trăm pin (đổi màu đậm hơn bằng recolor)
  lv_obj_t *img_wifi = lv_img_create(status);
  lv_img_set_src(img_wifi, &wifi_icon);
  lv_obj_align_to(img_wifi, label_batt, LV_ALIGN_OUT_LEFT_MID, -8, 0);
  lv_obj_set_style_img_recolor_opa(img_wifi, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_img_recolor(img_wifi, lv_color_black(), LV_PART_MAIN);

  // Time
  label_time = lv_label_create(status);
  lv_label_set_text(label_time, "--:--  --/--/----");
  lv_obj_set_style_text_color(label_time, dark, 0);
  lv_obj_set_style_text_font(label_time, pick_font_14(), 0);
  lv_obj_align(label_time, LV_ALIGN_RIGHT_MID, -78, 0);

    // ===== FRAME CHỨA TEXT + ẢNH =====
  lv_obj_t *frame = lv_obj_create(scr);
  // giữ cao 170, bạn có thể giảm nếu muốn gọn hơn (ví dụ 160)
  lv_obj_set_size(frame, lv_pct(100), 160);
  // KÉO FRAME LÊN CAO HƠN: giảm offset Y từ 80 -> 65
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
  lv_obj_align(img_monitor, LV_ALIGN_RIGHT_MID, -5, 0);

  // NHÓM TEXT "Health / Monitoring" BÊN TRÁI, ĐỐI DIỆN ẢNH
  lv_obj_t *text_cont = lv_obj_create(frame);
  lv_obj_set_size(text_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(text_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(text_cont, 0, 0);
  lv_obj_align(text_cont, LV_ALIGN_LEFT_MID, 5, 0);

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

  // ===== CỤM NÚT Guest/User – NHỎ HƠN & THẤP HƠN =====
  lv_obj_t *cont_btn = lv_obj_create(scr);
  // giảm chiều cao container từ 90 -> 75 (nhỏ tổng thể)
  lv_obj_set_size(cont_btn, lv_pct(100), 75);
  // vẫn căn đáy, nhưng kéo xuống thêm chút (xa frame hơn)
  lv_obj_align(cont_btn, LV_ALIGN_BOTTOM_MID, 0, -5);
  lv_obj_set_style_bg_opa(cont_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_btn, 0, 0);
  lv_obj_set_style_pad_all(cont_btn, 0, 0);
  lv_obj_set_style_pad_column(cont_btn, 12, 0); 
  lv_obj_clear_flag(cont_btn, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(cont_btn, col, row);

  // Guest (nút nhỏ lại bằng cách giảm height và font)
  btn_guest = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_guest, LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_guest, 16, 0);
  lv_obj_set_style_bg_color(btn_guest, card, 0);
  lv_obj_set_style_bg_color(btn_guest, lv_color_make(210, 245, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_guest, 3, 0);
  lv_obj_set_style_border_color(btn_guest, primary, 0);
  lv_obj_set_style_shadow_width(btn_guest, 0, 0);
  // chiều cao nút thấp hơn (nếu muốn nữa bạn có thể set size cụ thể)
  lv_obj_set_style_pad_top(btn_guest, 6, 0);
  lv_obj_set_style_pad_bottom(btn_guest, 6, 0);

  lv_obj_t *lg = lv_label_create(btn_guest);
  lv_label_set_text(lg, "Guest");
  lv_obj_center(lg);
  lv_obj_set_style_text_color(lg, dark, 0);
  lv_obj_set_style_text_font(lg, pick_font_20_or_14(), 0);

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

  ui_set_status("--:--  --/--/----", "--%");
}

// ================= format time =================
static void format_datetime(char *out, size_t out_sz, const DateTime &now) {
  snprintf(out, out_sz, "%02d:%02d  %02d/%02d/%04d",
           now.hour(), now.minute(),
           now.day(), now.month(), now.year());
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);

  // TFT init
  tft.init();
  tft.setRotation(1);

  pinMode(TFT_BL, OUTPUT);
  backlight_set(true);
  note_activity();

  tft.fillScreen(TFT_BLACK);

  // I2C (chung cho Touch + RTC + MAX17043)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  // RTC init
  rtc_ok = rtc.begin();
  Serial.println(rtc_ok ? "DS3231 OK" : "DS3231 not found");
  if (rtc_ok) {
  rtc.adjust(DateTime(2026, 4, 7, 19, 43, 0));   
}

  // MAX17043 init
  max17043_ok = i2c_device_present(MAX17043_ADDR);
  Serial.println(max17043_ok ? "MAX17043 OK" : "MAX17043 not found");
  if (max17043_ok) {
    max17043_reset();
    delay(250);
    max17043_quickStart();
    delay(125);
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

  // Update status mỗi 1 giây
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
    if (max17043_ok) {
      float soc = max17043_readSOC_percent();
      if (isnan(soc)) {
        snprintf(bbuf, sizeof(bbuf), "--%%");
      } else {
        int pct = (int)lroundf(soc);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        snprintf(bbuf, sizeof(bbuf), "%d%%", pct);
      }
    } else {
      snprintf(bbuf, sizeof(bbuf), "--%%");
    }

    ui_set_status(tbuf, bbuf);
  }
}