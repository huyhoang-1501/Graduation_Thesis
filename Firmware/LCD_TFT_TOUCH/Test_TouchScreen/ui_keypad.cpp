#include <Arduino.h>
#include <TFT_eSPI.h>

#define LV_CONF_SKIP
#include "lv_conf.h"
#include <lvgl.h>

#define TFT_BL 15

static const uint16_t SCREEN_WIDTH  = 480;
static const uint16_t SCREEN_HEIGHT = 320;

TFT_eSPI tft;

// buffer 40 lines
static lv_color_t buf1[SCREEN_WIDTH * 40];
static lv_disp_draw_buf_t draw_buf;

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

static lv_obj_t* make_cell(lv_obj_t *parent, const char *text) {
  lv_obj_t *b = lv_obj_create(parent);

  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_make(40, 40, 40), 0);

  lv_obj_set_style_border_width(b, 3, 0);
  lv_obj_set_style_border_color(b, lv_color_white(), 0);

  lv_obj_set_style_shadow_width(b, 0, 0); // không shadow
  lv_obj_set_style_pad_all(b, 0, 0);

  lv_obj_t *t = lv_label_create(b);
  lv_label_set_text(t, text);
  lv_obj_center(t);
  lv_obj_set_style_text_color(t, lv_color_white(), 0);

  return b;
}

static void draw_layout_only() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_pad_all(scr, 10, 0);

  // Ô hiển thị
  lv_obj_t *box = lv_obj_create(scr);
  lv_obj_set_size(box, 460, 70);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 0);

  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(box, lv_color_black(), 0);
  lv_obj_set_style_border_color(box, lv_color_make(255, 180, 0), 0);
  lv_obj_set_style_border_width(box, 3, 0);
  lv_obj_set_style_radius(box, 14, 0);
  lv_obj_set_style_shadow_width(box, 0, 0);

  lv_obj_t *lbl = lv_label_create(box);
  lv_label_set_text(lbl, "DISPLAY");
  lv_obj_center(lbl);
  lv_obj_set_style_text_color(lbl, lv_color_white(), 0);

  // Grid nút
  lv_obj_t *cont = lv_obj_create(scr);
  lv_obj_set_size(cont, 460, 220);
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);

  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 12, 0);
  lv_obj_set_style_pad_column(cont, 12, 0);

  static lv_coord_t col[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST
  };
  static lv_coord_t row[] = {
    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
    LV_GRID_TEMPLATE_LAST
  };
  lv_obj_set_grid_dsc_array(cont, col, row);

  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 4; c++) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d,%d", r, c);
      lv_obj_t *cell = make_cell(cont, buf);
      lv_obj_set_grid_cell(cell,
                           LV_GRID_ALIGN_STRETCH, c, 1,
                           LV_GRID_ALIGN_STRETCH, r, 1);
    }
  }
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  // Clear toàn màn để tránh artefact/vệt cũ
  tft.fillScreen(TFT_BLACK);

  Serial.print("TFT width="); Serial.println(tft.width());
  Serial.print("TFT height="); Serial.println(tft.height());

  lv_init();

  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, SCREEN_WIDTH * 40);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  draw_layout_only();
}

void loop() {
  lv_timer_handler();
  delay(5);
}