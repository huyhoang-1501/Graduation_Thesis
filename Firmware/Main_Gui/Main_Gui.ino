#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"

#include <lvgl.h>

// ===== Screen size (landscape) =====
static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

#define TFT_BL 15

TFT_eSPI tft;

// ===== LVGL draw buffer =====
static lv_color_t buf1[SCREEN_WIDTH * 40];
static lv_disp_draw_buf_t draw_buf;

// ===== LVGL tick =====
static esp_timer_handle_t lvgl_tick_timer;
static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

// ===== Display flush =====
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

// ================= MAIN GUI (UI objects) =================
static lv_obj_t *label_time = nullptr;
static lv_obj_t *label_batt = nullptr;
static lv_obj_t *btn_guest  = nullptr;
static lv_obj_t *btn_user   = nullptr;

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

// Gọi hàm này để update status bar từ code ngoài (RTC + pin)
void ui_set_status(const char *time_str, int batt_percent) {
  if (label_time) lv_label_set_text(label_time, time_str);

  if (label_batt) {
    if (batt_percent < 0) batt_percent = 0;
    if (batt_percent > 100) batt_percent = 100;
    lv_label_set_text_fmt(label_batt, "%d%%", batt_percent);
  }
}

// Tạo GUI chính
void create_main_gui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 12, 0);

  // ===== Status bar =====
  lv_obj_t *status = lv_obj_create(scr);
  lv_obj_set_size(status, lv_pct(100), 40);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(status, lv_color_make(20, 20, 20), 0);
  lv_obj_set_style_border_width(status, 0, 0);
  lv_obj_set_style_radius(status, 10, 0);
  lv_obj_set_style_pad_left(status, 10, 0);
  lv_obj_set_style_pad_right(status, 10, 0);

  label_time = lv_label_create(status);
  lv_label_set_text(label_time, "--:--  --/--/----");
  lv_obj_set_style_text_color(label_time, lv_color_white(), 0);
  lv_obj_align(label_time, LV_ALIGN_RIGHT_MID, -70, 0);

  label_batt = lv_label_create(status);
  lv_label_set_text(label_batt, "--%");
  lv_obj_set_style_text_color(label_batt, lv_color_make(0, 255, 120), 0);
  lv_obj_align(label_batt, LV_ALIGN_RIGHT_MID, 0, 0);

  // ===== Center titles =====
  lv_obj_t *title1 = lv_label_create(scr);
  lv_label_set_text(title1, "HCMUTE");
  lv_obj_set_style_text_color(title1, lv_color_white(), 0);
  lv_obj_set_style_text_font(title1, pick_font_22_or_14(), 0);
  lv_obj_align(title1, LV_ALIGN_CENTER, 0, -40);

  lv_obj_t *title2 = lv_label_create(scr);
  lv_label_set_text(title2, "Health Monitoring");
  lv_obj_set_style_text_color(title2, lv_color_make(200, 200, 200), 0);
  lv_obj_set_style_text_font(title2, pick_font_18_or_14(), 0);
  lv_obj_align(title2, LV_ALIGN_CENTER, 0, -10);

  // ===== Login buttons =====
  lv_obj_t *cont_btn = lv_obj_create(scr);
  lv_obj_set_size(cont_btn, lv_pct(100), 100);
  lv_obj_align(cont_btn, LV_ALIGN_CENTER, 0, 90);
  lv_obj_set_style_bg_opa(cont_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_btn, 0, 0);
  lv_obj_set_style_pad_all(cont_btn, 0, 0);
  lv_obj_set_style_pad_column(cont_btn, 16, 0);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(cont_btn, col, row);

  // Guest
  btn_guest = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_guest, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_guest, 16, 0);
  lv_obj_set_style_bg_color(btn_guest, lv_color_make(40, 40, 40), 0);
  lv_obj_set_style_bg_color(btn_guest, lv_color_make(100, 100, 100), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_guest, 3, 0);
  lv_obj_set_style_border_color(btn_guest, lv_color_white(), 0);

  lv_obj_t *lg = lv_label_create(btn_guest);
  lv_label_set_text(lg, "Guest");
  lv_obj_center(lg);
  lv_obj_set_style_text_color(lg, lv_color_white(), 0);
  lv_obj_set_style_text_font(lg, pick_font_20_or_14(), 0);

  // User
  btn_user = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_user, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_user, 16, 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(20, 120, 70), 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(40, 180, 100), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_user, 3, 0);
  lv_obj_set_style_border_color(btn_user, lv_color_white(), 0);

  lv_obj_t *lu = lv_label_create(btn_user);
  lv_label_set_text(lu, "User");
  lv_obj_center(lu);
  lv_obj_set_style_text_color(lu, lv_color_white(), 0);
  lv_obj_set_style_text_font(lu, pick_font_20_or_14(), 0);

  ui_set_status("--:--  --/--/----", 0);
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

  // TODO: sau này bạn update DS3231 + pin tại đây (mỗi 1s)
}