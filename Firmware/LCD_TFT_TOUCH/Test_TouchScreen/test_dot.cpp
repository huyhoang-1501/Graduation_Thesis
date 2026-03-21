#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "esp_timer.h"

#define LV_CONF_SKIP
#include "lv_conf.h"
#include <lvgl.h>

#define TFT_BL 15

static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

#define FT6336U_ADDR 0x38
#define I2C_SDA 21
#define I2C_SCL 22

TFT_eSPI tft;
static lv_color_t buf1[SCREEN_WIDTH * 40];
static lv_disp_draw_buf_t draw_buf;

static lv_obj_t *dot = nullptr;
static lv_obj_t *label_xy = nullptr;

static esp_timer_handle_t lvgl_tick_timer;

static void lv_tick_task(void *arg) {
  (void)arg;
  lv_tick_inc(1);
}

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

static void create_touch_debug_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

  lv_obj_t *border = lv_obj_create(scr);
  lv_obj_set_size(border, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2);
  lv_obj_align(border, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(border, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(border, 2, 0);
  lv_obj_set_style_border_color(border, lv_color_make(0, 255, 0), 0);
  lv_obj_move_background(border);

  label_xy = lv_label_create(scr);
  lv_label_set_text(label_xy, "x=? y=?");
  lv_obj_align(label_xy, LV_ALIGN_TOP_LEFT, 8, 6);
  lv_obj_set_style_text_color(label_xy, lv_color_white(), 0);

  dot = lv_obj_create(scr);
  lv_obj_set_size(dot, 12, 12);
  lv_obj_set_style_bg_color(dot, lv_color_make(255, 0, 0), 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_radius(dot, 6, 0);
  lv_obj_set_pos(dot, SCREEN_WIDTH/2, SCREEN_HEIGHT/2);
}

static void my_touch_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  (void) indev_driver;

  uint16_t x = 0, y = 0;
  bool touched = false;

  bool ok = ft6336u_read_touch(x, y, touched);

  if (!ok || !touched) {
    data->state = LV_INDEV_STATE_REL;
    return;
  }

  // FIX: swap + invert Y
  uint16_t x_map = y;
  uint16_t y_map = (SCREEN_HEIGHT - 1) - x;

  if (x_map >= SCREEN_WIDTH)  x_map = SCREEN_WIDTH - 1;
  if (y_map >= SCREEN_HEIGHT) y_map = SCREEN_HEIGHT - 1;

  data->point.x = x_map;
  data->point.y = y_map;
  data->state   = LV_INDEV_STATE_PR;

  if (dot) lv_obj_set_pos(dot, x_map - 6, y_map - 6);
  if (label_xy) lv_label_set_text_fmt(label_xy, "x=%d  y=%d", (int)x_map, (int)y_map);
}

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

  create_touch_debug_ui();
}

void loop() {
  lv_timer_handler();
  delay(5);
}