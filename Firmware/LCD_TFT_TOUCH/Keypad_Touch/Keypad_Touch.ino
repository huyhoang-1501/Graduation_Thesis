#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"

#define LV_CONF_SKIP
#include "lv_conf.h"
#include <lvgl.h>

// ================= DISPLAY =================
#define TFT_BL 15
static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

TFT_eSPI tft;

// LVGL buffer (40 lines)
static lv_color_t buf1[SCREEN_WIDTH * 40];
static lv_disp_draw_buf_t draw_buf;

// LVGL tick timer
static esp_timer_handle_t lvgl_tick_timer;
static void lv_tick_task(void *arg) { (void)arg; lv_tick_inc(1); }

// ================= TOUCH (FT6336U) =================
#define FT6336U_ADDR 0x38
#define I2C_SDA 21
#define I2C_SCL 22

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

// ================= TOUCH READ (mapping đã fix) =================
static void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  (void) indev_driver;

  uint16_t x = 0, y = 0;
  bool touched = false;

  bool ok = ft6336u_read_touch(x, y, touched);
  if (!ok || !touched) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  // swap + invert Y
  uint16_t x_map = y;
  uint16_t y_map = (SCREEN_HEIGHT - 1) - x;

  if (x_map >= SCREEN_WIDTH)  x_map = SCREEN_WIDTH - 1;
  if (y_map >= SCREEN_HEIGHT) y_map = SCREEN_HEIGHT - 1;

  data->point.x = x_map;
  data->point.y = y_map;
  data->state   = LV_INDEV_STATE_PR;
}

// ================= UI =================
static lv_obj_t *ta_number = nullptr;

static void add_digit(const char *d) {
  const char *cur = lv_textarea_get_text(ta_number);
  if (strlen(cur) >= 18) return;
  lv_textarea_add_text(ta_number, d);
}

static void btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  const char *txt = (const char *)lv_event_get_user_data(e);
  if (!txt) return;

  if (strcmp(txt, "DEL") == 0) { lv_textarea_del_char(ta_number); return; }
  if (strcmp(txt, "CLR") == 0) { lv_textarea_set_text(ta_number, ""); return; }
  if (strcmp(txt, "SAVE") == 0) {
    Serial.print("SAVE = ");
    Serial.println(lv_textarea_get_text(ta_number));
    return;
  }
  add_digit(txt);
}

static lv_obj_t* make_btn(lv_obj_t *parent, const char *label) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_style_radius(btn, 16, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

  // normal
  lv_obj_set_style_bg_color(btn, lv_color_make(40, 40, 40), 0);
  lv_obj_set_style_border_width(btn, 3, 0);
  lv_obj_set_style_border_color(btn, lv_color_white(), 0);

  // pressed highlight rõ
  lv_obj_set_style_bg_color(btn, lv_color_make(110, 110, 110), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_make(255, 220, 0), LV_STATE_PRESSED);

  lv_obj_set_style_shadow_width(btn, 0, 0);

  lv_obj_t *lab = lv_label_create(btn);
  lv_label_set_text(lab, label);
  lv_obj_center(lab);
  lv_obj_set_style_text_color(lab, lv_color_white(), 0);

  lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED, (void*)label);
  return btn;
}

static void create_ui_keypad() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 10, 0);

  // Display (to hơn + padding nhiều hơn)
  ta_number = lv_textarea_create(scr);
  lv_obj_set_size(ta_number, 460, 80);
  lv_obj_align(ta_number, LV_ALIGN_TOP_MID, 0, 0);
  lv_textarea_set_one_line(ta_number, true);
  lv_textarea_set_placeholder_text(ta_number, "Nhap so...");

  lv_obj_set_style_bg_color(ta_number, lv_color_black(), 0);
  lv_obj_set_style_text_color(ta_number, lv_color_white(), 0);
  lv_obj_set_style_border_color(ta_number, lv_color_make(255, 180, 0), 0);
  lv_obj_set_style_border_width(ta_number, 3, 0);
  lv_obj_set_style_radius(ta_number, 14, 0);
  lv_obj_set_style_shadow_width(ta_number, 0, 0);
  lv_obj_set_style_pad_left(ta_number, 16, 0);
  lv_obj_set_style_pad_top(ta_number, 14, 0);

 #if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(ta_number, &lv_font_montserrat_28, 0);
#elif LV_FONT_MONTSERRAT_20
  lv_obj_set_style_text_font(ta_number, &lv_font_montserrat_20, 0);
#elif LV_FONT_MONTSERRAT_16
  lv_obj_set_style_text_font(ta_number, &lv_font_montserrat_16, 0);
#else
  lv_obj_set_style_text_font(ta_number, &lv_font_montserrat_14, 0);
#endif

  // Container grid 4x4
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, 460, 210);                 // cân lại vì display cao hơn
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 12, 0);
  lv_obj_set_style_pad_column(cont, 12, 0);

  static lv_coord_t col[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  static lv_coord_t row[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  lv_obj_set_grid_dsc_array(cont, col, row);

  // Row 0
  lv_obj_set_grid_cell(make_btn(cont, "1"), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(make_btn(cont, "2"), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(make_btn(cont, "3"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  // Row 1
  lv_obj_set_grid_cell(make_btn(cont, "4"), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(make_btn(cont, "5"), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(make_btn(cont, "6"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

  // Row 2
  lv_obj_set_grid_cell(make_btn(cont, "7"), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(make_btn(cont, "8"), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(make_btn(cont, "9"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

  // Row 3
  lv_obj_set_grid_cell(make_btn(cont, "0"),   LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 3, 1);
  lv_obj_set_grid_cell(make_btn(cont, "CLR"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

  // ===== Cột chức năng: DEL 2 hàng + SAVE 2 hàng (cân đối) =====
  lv_obj_t *bdel = make_btn(cont, "DEL");
  lv_obj_set_grid_cell(bdel, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
  lv_obj_set_style_bg_color(bdel, lv_color_make(150, 40, 40), 0);
  lv_obj_set_style_bg_color(bdel, lv_color_make(230, 80, 80), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(bdel, lv_color_white(), 0);
  lv_obj_set_style_border_color(bdel, lv_color_make(255, 220, 0), LV_STATE_PRESSED);

  lv_obj_t *bsave = make_btn(cont, "SAVE");
  lv_obj_set_grid_cell(bsave, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 2);
  lv_obj_set_style_bg_color(bsave, lv_color_make(20, 130, 70), 0);
  lv_obj_set_style_bg_color(bsave, lv_color_make(60, 210, 120), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(bsave, lv_color_white(), 0);
  lv_obj_set_style_border_color(bsave, lv_color_make(255, 220, 0), LV_STATE_PRESSED);
}

// ================= SETUP / LOOP =================
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.fillScreen(TFT_BLACK);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

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

  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touch_read;
  lv_indev_drv_register(&indev_drv);

  create_ui_keypad();
}

void loop() {
  lv_timer_handler();
  delay(1);   // nhạy hơn
}