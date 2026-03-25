#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"

#include <lvgl.h>
#include "RTClib.h"

// ================= DISPLAY =================
static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

#define TFT_BL 15
TFT_eSPI tft;

// ================= LVGL BUFFER =================
// buffer 40 lines
static lv_color_t buf1[SCREEN_WIDTH * 40];
static lv_disp_draw_buf_t draw_buf;

// ================= LVGL TICK =================
static esp_timer_handle_t lvgl_tick_timer;
static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

// ================= RTC DS3231 =================
RTC_DS3231 rtc;
static bool rtc_ok = false;

// ================= BATTERY ADC =================
#define BAT_ADC_PIN 32
static float g_vbat_filt = 3.9f;   // giá trị lọc (khởi tạo tạm)
static int   g_pct_hold  = 50;

static int voltage_to_percent(float v) {
  // Lookup kiểu "giống điện thoại" (xấp xỉ Li-ion)
  // bạn có thể chỉnh lại theo pin thực tế
  if (v >= 4.20f) return 100;
  if (v >= 4.10f) return 90;
  if (v >= 4.00f) return 78;
  if (v >= 3.90f) return 62;
  if (v >= 3.80f) return 46;
  if (v >= 3.70f) return 30;
  if (v >= 3.60f) return 15;
  if (v >= 3.50f) return 5;
  return 0;
}

static int read_battery_percent() {
  // đọc nhiều mẫu nhanh, bỏ delay dài
  uint32_t sum = 0;
  const int N = 32;
  for (int i = 0; i < N; i++) {
    sum += analogRead(BAT_ADC_PIN);
    delayMicroseconds(200);
  }
  float raw = sum / (float)N;

  // đổi ra điện áp ADC (xấp xỉ)
  float v_adc = (raw / 4095.0f) * 3.3f;

  // cầu chia 10k-10k
  float v_bat = v_adc * 2.0f;

  // lọc mượt kiểu EMA để không nhảy
  // alpha nhỏ => mượt hơn (0.05~0.15)
  const float alpha = 0.10f;
  g_vbat_filt = g_vbat_filt + alpha * (v_bat - g_vbat_filt);

  int pct = voltage_to_percent(g_vbat_filt);

  // chống tụt ảo: không cho rơi quá nhanh trong 1 giây
  // (ví dụ tối đa giảm 2%/giây)
  if (pct < g_pct_hold - 2) pct = g_pct_hold - 2;
  if (pct > g_pct_hold + 5) pct = g_pct_hold + 5; // tăng nhanh hơn chút ok

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  g_pct_hold = pct;

  // Debug (xài tạm)
  // Serial.printf("vbat=%.3f filt=%.3f pct=%d\n", v_bat, g_vbat_filt, pct);

  return pct;
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
static const lv_font_t* pick_font_44_or_14() {
#if defined(LV_FONT_MONTSERRAT_44) && (LV_FONT_MONTSERRAT_44 == 1)
  return &lv_font_montserrat_44;
#else
  return &lv_font_montserrat_14;
#endif
}
static const lv_font_t* pick_font_22_or_14() {
#if defined(LV_FONT_MONTSERRAT_22) && (LV_FONT_MONTSERRAT_22 == 1)
  return &lv_font_montserrat_22;
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
static const lv_font_t* pick_font_14() {
  return &lv_font_montserrat_14;
}

// ================= MAIN GUI objects =================
static lv_obj_t *label_time = nullptr;
static lv_obj_t *label_batt = nullptr;
static lv_obj_t *btn_guest  = nullptr;
static lv_obj_t *btn_user   = nullptr;

// Update status bar (RTC + battery)
static void ui_set_status(const char *time_str, int batt_percent) {
  if (label_time) lv_label_set_text(label_time, time_str);

  if (label_batt) {
    if (batt_percent < 0) batt_percent = 0;
    if (batt_percent > 100) batt_percent = 100;
    lv_label_set_text_fmt(label_batt, "%d%%", batt_percent);

    // đổi màu theo mức pin
    if (batt_percent <= 15) {
      lv_obj_set_style_text_color(label_batt, lv_color_make(220, 40, 40), 0);
    } else if (batt_percent <= 40) {
      lv_obj_set_style_text_color(label_batt, lv_color_make(230, 160, 0), 0);
    } else {
      lv_obj_set_style_text_color(label_batt, lv_color_make(0, 150, 110), 0);
    }
  }
}

// Create GUI (Medical theme)
static void create_main_gui() {
  lv_obj_t *scr = lv_scr_act();

  lv_color_t bg      = lv_color_make(245, 252, 255);   // trắng xanh nhạt
  lv_color_t primary = lv_color_make(0, 140, 200);     // xanh y tế
  lv_color_t dark    = lv_color_make(10, 60, 90);      // chữ xanh đậm
  lv_color_t card    = lv_color_white();

  lv_obj_set_style_bg_color(scr, bg, 0);
  lv_obj_set_style_pad_all(scr, 12, 0);

  // ===== Status bar =====
  lv_obj_t *status = lv_obj_create(scr);
  lv_obj_set_size(status, lv_pct(100), 44);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_set_style_bg_color(status, card, 0);
  lv_obj_set_style_radius(status, 14, 0);
  lv_obj_set_style_border_width(status, 2, 0);
  lv_obj_set_style_border_color(status, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_pad_left(status, 12, 0);
  lv_obj_set_style_pad_right(status, 12, 0);
  lv_obj_set_style_shadow_width(status, 0, 0);

  // HCMUTE (top-left)
  lv_obj_t *hcmute_box = lv_obj_create(status);
  lv_obj_set_size(hcmute_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(hcmute_box, LV_ALIGN_LEFT_MID, 0, 0);

  // style khung đỏ
  lv_obj_set_style_bg_opa(hcmute_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hcmute_box, 2, 0);
  lv_obj_set_style_border_color(hcmute_box, lv_color_make(220, 40, 40), 0); // đỏ
  lv_obj_set_style_radius(hcmute_box, 10, 0);
  lv_obj_set_style_pad_left(hcmute_box, 10, 0);
  lv_obj_set_style_pad_right(hcmute_box, 10, 0);
  lv_obj_set_style_pad_top(hcmute_box, 5, 0);
  lv_obj_set_style_pad_bottom(hcmute_box, 5, 0);

  lv_obj_t *label_hcmute = lv_label_create(hcmute_box);
  lv_label_set_text(label_hcmute, "HCMUTE");
  lv_obj_set_style_text_color(label_hcmute, lv_color_make(0, 140, 200), 0); // xanh
  lv_obj_set_style_text_font(label_hcmute, pick_font_18_or_14(), 0);
  lv_obj_center(label_hcmute);

  // Battery (top-right)
  label_batt = lv_label_create(status);
  lv_label_set_text(label_batt, "--%");
  lv_obj_set_style_text_font(label_batt, pick_font_18_or_14(), 0);
  lv_obj_align(label_batt, LV_ALIGN_RIGHT_MID, 0, 0);

  // Time (top-right)
  label_time = lv_label_create(status);
  lv_label_set_text(label_time, "--:--  --/--/----");
  lv_obj_set_style_text_color(label_time, dark, 0);
  lv_obj_set_style_text_font(label_time, pick_font_14(), 0);
  lv_obj_align(label_time, LV_ALIGN_RIGHT_MID, -78, 0);

  // ===== Main title center =====
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "Health Monitoring");
  lv_obj_set_style_text_color(title, primary, 0);
  lv_obj_set_style_text_font(title, pick_font_44_or_14(), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

  // ===== Buttons =====
  lv_obj_t *cont_btn = lv_obj_create(scr);
  lv_obj_set_size(cont_btn, lv_pct(100), 90);
  lv_obj_align(cont_btn, LV_ALIGN_CENTER, 0, 90);
  lv_obj_set_style_bg_opa(cont_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_btn, 0, 0);
  lv_obj_set_style_pad_all(cont_btn, 0, 0);
  lv_obj_set_style_pad_column(cont_btn, 16, 0);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(cont_btn, col, row);

  // Guest (white)
  btn_guest = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_guest, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_guest, 18, 0);
  lv_obj_set_style_bg_color(btn_guest, card, 0);
  lv_obj_set_style_bg_color(btn_guest, lv_color_make(210, 245, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_guest, 3, 0);
  lv_obj_set_style_border_color(btn_guest, primary, 0);
  lv_obj_set_style_shadow_width(btn_guest, 0, 0);

  lv_obj_t *lg = lv_label_create(btn_guest);
  lv_label_set_text(lg, "Guest");
  lv_obj_center(lg);
  lv_obj_set_style_text_color(lg, dark, 0);
  lv_obj_set_style_text_font(lg, pick_font_20_or_14(), 0);

  // User (primary)
  btn_user = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_user, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_user, 18, 0);
  lv_obj_set_style_bg_color(btn_user, primary, 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(0, 175, 235), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_user, 0, 0);
  lv_obj_set_style_shadow_width(btn_user, 0, 0);

  lv_obj_t *lu = lv_label_create(btn_user);
  lv_label_set_text(lu, "User");
  lv_obj_center(lu);
  lv_obj_set_style_text_color(lu, lv_color_white(), 0);
  lv_obj_set_style_text_font(lu, pick_font_20_or_14(), 0);

  ui_set_status("--:--  --/--/----", 0);
}

// ================= format time =================
static void format_datetime(char *out, size_t out_sz, const DateTime &now) {
  snprintf(out, out_sz, "%02d:%02d  %02d/%02d/%04d",
           now.hour(), now.minute(),
           now.day(), now.month(), now.year());
}

// ================= setup/loop =================
void setup() {
  Serial.begin(115200);

  // TFT init
  tft.init();
  tft.setRotation(1);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.fillScreen(TFT_BLACK);

  // ADC for battery
  pinMode(BAT_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);  // đo tới ~3.3V ổn hơn (thực tế ~3.1-3.3)

  // I2C (DS3231 chung bus với touch nếu có)
  Wire.begin(21, 22);
  Wire.setClock(400000);

  // RTC init
  rtc_ok = rtc.begin();
  if (!rtc_ok) {
    Serial.println("DS3231 not found");
  } else {
    Serial.println("DS3231 OK");
    // Nếu cần set giờ 1 lần, bỏ comment dòng dưới rồi nạp 1 lần:
    // rtc.adjust(DateTime(2026, 3, 25, 20, 43, 10));
  }

  // LVGL init
  lv_init();

  // Tick 1ms
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

  // Create GUI
  create_main_gui();
}

void loop() {
  lv_timer_handler();
  delay(5);

  // Update status mỗi 1 giây
  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();

    char tbuf[32];
    if (rtc_ok) {
      DateTime now = rtc.now();
      format_datetime(tbuf, sizeof(tbuf), now);
    } else {
      snprintf(tbuf, sizeof(tbuf), "--:--  --/--/----");
    }

    int batt = read_battery_percent();
    ui_set_status(tbuf, batt);
  }
}