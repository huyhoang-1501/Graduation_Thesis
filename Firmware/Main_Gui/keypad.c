#include "keypad.h"

#include <string.h>

static lv_obj_t *g_keypad_scr = NULL;
static lv_obj_t *g_ta_number  = NULL;

static const lv_font_t *g_btn_font  = NULL;
static const lv_font_t *g_back_font = NULL;

static keypad_back_cb_t g_back_cb = NULL;
static keypad_next_cb_t g_next_cb = NULL;

static void keypad_add_digit(const char *d) {
  if (!g_ta_number || !d) return;

  const char *cur = lv_textarea_get_text(g_ta_number);
  if (cur && strlen(cur) >= 18) return;
  lv_textarea_add_text(g_ta_number, d);
}

static void keypad_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

  const char *txt = (const char *)lv_event_get_user_data(e);
  if (!txt || !g_ta_number) return;

  if (strcmp(txt, "DEL") == 0) {
    lv_textarea_del_char(g_ta_number);
    return;
  }
  if (strcmp(txt, "CLR") == 0) {
    lv_textarea_set_text(g_ta_number, "");
    return;
  }
  if (strcmp(txt, "->") == 0) {
    if (g_next_cb) g_next_cb(lv_textarea_get_text(g_ta_number));
    return;
  }

  keypad_add_digit(txt);
}

static void back_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_back_cb) g_back_cb();
}

static lv_obj_t* keypad_make_btn(lv_obj_t *parent, const char *label) {
  lv_color_t primary   = lv_color_make(0, 140, 200);
  lv_color_t dark      = lv_color_make(10, 60, 90);
  lv_color_t card      = lv_color_white();
  lv_color_t pressedBg = lv_color_make(210, 245, 255);
  lv_color_t border    = lv_color_make(200, 235, 250);

  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_style_radius(btn, 18, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

  lv_obj_set_style_bg_color(btn, card, 0);
  lv_obj_set_style_border_width(btn, 3, 0);
  lv_obj_set_style_border_color(btn, border, 0);

  lv_obj_set_style_bg_color(btn, pressedBg, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, primary, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(btn, 0, 0);

  lv_obj_t *lab = lv_label_create(btn);
  lv_label_set_text(lab, label);
  lv_obj_center(lab);
  lv_obj_set_style_text_color(lab, dark, 0);
  if (g_btn_font) lv_obj_set_style_text_font(lab, g_btn_font, 0);

  lv_obj_add_event_cb(btn, keypad_btn_event_cb, LV_EVENT_CLICKED, (void*)label);
  return btn;
}

void keypad_init_screen(const lv_font_t *btn_font,
                        const lv_font_t *back_font,
                        keypad_back_cb_t back_cb,
                        keypad_next_cb_t next_cb) {
  if (g_keypad_scr) return;

  g_btn_font  = btn_font;
  g_back_font = back_font;
  g_back_cb   = back_cb;
  g_next_cb   = next_cb;

  lv_color_t bg      = lv_color_make(245, 252, 255);
  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark    = lv_color_make(10, 60, 90);
  lv_color_t card    = lv_color_white();

  g_keypad_scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(g_keypad_scr, bg, 0);
  lv_obj_set_style_pad_all(g_keypad_scr, 12, 0);
  lv_obj_clear_flag(g_keypad_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(g_keypad_scr, LV_OBJ_FLAG_GESTURE_BUBBLE);

  // Back button: chỉ bấm nút này mới quay lại main
  lv_obj_t *btn_back = lv_btn_create(g_keypad_scr);
  lv_obj_set_size(btn_back, 100, 40);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, -4);
  lv_obj_set_style_radius(btn_back, 12, 0);
  lv_obj_set_style_bg_color(btn_back, card, 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_make(210, 245, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_back, 2, 0);
  lv_obj_set_style_border_color(btn_back, primary, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lb_back = lv_label_create(btn_back);
  lv_label_set_text(lb_back, "Back");
  lv_obj_center(lb_back);
  lv_obj_set_style_text_color(lb_back, dark, 0);
  if (g_back_font) lv_obj_set_style_text_font(lb_back, g_back_font, 0);

  // Textarea hiển thị
  g_ta_number = lv_textarea_create(g_keypad_scr);
  // Giảm nhẹ chiều cao ô nhập để nhường không gian cho keypad
  lv_obj_set_size(g_ta_number, 460, 66);
  lv_obj_align(g_ta_number, LV_ALIGN_TOP_MID, 0, 42);
  lv_textarea_set_one_line(g_ta_number, true);
  lv_textarea_set_placeholder_text(g_ta_number, "Nhap ID...");
  lv_textarea_set_cursor_click_pos(g_ta_number, false);
  lv_obj_clear_flag(g_ta_number, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(g_ta_number, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_set_style_bg_color(g_ta_number, card, 0);
  lv_obj_set_style_text_color(g_ta_number, dark, 0);
  lv_obj_set_style_border_width(g_ta_number, 2, 0);
  lv_obj_set_style_border_color(g_ta_number, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_border_color(g_ta_number, primary, LV_STATE_FOCUSED);
  lv_obj_set_style_radius(g_ta_number, 14, 0);
  lv_obj_set_style_shadow_width(g_ta_number, 0, 0);
  lv_obj_set_style_pad_left(g_ta_number, 16, 0);
  lv_obj_set_style_pad_top(g_ta_number, 14, 0);
  if (g_btn_font) lv_obj_set_style_text_font(g_ta_number, g_btn_font, 0);

  // Grid keypad
  lv_obj_t *cont = lv_obj_create(g_keypad_scr);
  // Tăng chiều cao vùng keypad để nút 0-9/CLR to hơn, dễ bấm hơn
  lv_obj_set_size(cont, 460, 200);
  lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_style_pad_row(cont, 6, 0);
  lv_obj_set_style_pad_column(cont, 6, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t col[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  static lv_coord_t row[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
  lv_obj_set_grid_dsc_array(cont, col, row);

  // Row 0
  lv_obj_set_grid_cell(keypad_make_btn(cont, "1"), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "2"), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "3"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

  // Row 1
  lv_obj_set_grid_cell(keypad_make_btn(cont, "4"), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "5"), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "6"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 1, 1);

  // Row 2
  lv_obj_set_grid_cell(keypad_make_btn(cont, "7"), LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "8"), LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 2, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "9"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 2, 1);

  // Row 3
  lv_obj_set_grid_cell(keypad_make_btn(cont, "0"),   LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 3, 1);
  lv_obj_set_grid_cell(keypad_make_btn(cont, "CLR"), LV_GRID_ALIGN_STRETCH, 2, 1, LV_GRID_ALIGN_STRETCH, 3, 1);

  // Function column
  lv_obj_t *bdel = keypad_make_btn(cont, "DEL");
  lv_obj_set_grid_cell(bdel, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 0, 2);
  lv_obj_set_style_bg_color(bdel, lv_color_make(255, 235, 235), 0);
  lv_obj_set_style_bg_color(bdel, lv_color_make(255, 210, 210), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(bdel, lv_color_make(220, 40, 40), 0);
  lv_obj_set_style_border_color(bdel, lv_color_make(220, 40, 40), LV_STATE_PRESSED);

  // SAVE đổi thành nút mũi tên ->
  lv_obj_t *bnext = keypad_make_btn(cont, "->");
  lv_obj_set_grid_cell(bnext, LV_GRID_ALIGN_STRETCH, 3, 1, LV_GRID_ALIGN_STRETCH, 2, 2);
  lv_obj_set_style_bg_color(bnext, lv_color_make(230, 250, 255), 0);
  lv_obj_set_style_bg_color(bnext, lv_color_make(200, 240, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(bnext, primary, 0);
  lv_obj_set_style_border_color(bnext, primary, LV_STATE_PRESSED);
}

lv_obj_t *keypad_get_screen(void) {
  return g_keypad_scr;
}

const char *keypad_get_text(void) {
  if (!g_ta_number) return "";
  return lv_textarea_get_text(g_ta_number);
}

void keypad_set_text(const char *text) {
  if (!g_ta_number) return;
  lv_textarea_set_text(g_ta_number, text ? text : "");
}
