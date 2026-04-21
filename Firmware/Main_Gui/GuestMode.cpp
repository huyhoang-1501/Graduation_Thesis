#include "GuestMode.h"

#include <Arduino.h>
#include <cstdio>
#include <lvgl.h>

static lv_obj_t *guest_scr = nullptr;
static lv_obj_t *label_state = nullptr;
static lv_obj_t *label_spo2 = nullptr;
static lv_obj_t *label_hr   = nullptr;
static lv_obj_t *label_sys  = nullptr;
static lv_obj_t *label_dia  = nullptr;

static GuestBackCallback g_back_callback = nullptr;
static bool g_active = false;
static uint32_t g_start_ms = 0;

static const lv_font_t *pick_font_large() {
#if defined(LV_FONT_MONTSERRAT_24) && (LV_FONT_MONTSERRAT_24 == 1)
  return &lv_font_montserrat_24;
#else
  return &lv_font_montserrat_20;
#endif
}

static const lv_font_t *pick_font_mid() {
#if defined(LV_FONT_MONTSERRAT_16) && (LV_FONT_MONTSERRAT_16 == 1)
  return &lv_font_montserrat_16;
#else
  return &lv_font_montserrat_14;
#endif
}

static const lv_font_t *pick_font_small() {
#if defined(LV_FONT_MONTSERRAT_14) && (LV_FONT_MONTSERRAT_14 == 1)
  return &lv_font_montserrat_14;
#else
  return &lv_font_montserrat_12;
#endif
}

static void back_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  if (g_back_callback) g_back_callback();
}

static void build_guest_screen() {
  if (guest_scr) return;

  guest_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(guest_scr, lv_color_make(245, 252, 255), 0);
  lv_obj_clear_flag(guest_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(guest_scr, 12, 0);

  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark    = lv_color_make(10, 60, 90);
  lv_color_t accentR  = lv_color_make(220, 40, 40);
  lv_color_t card     = lv_color_white();

  lv_obj_t *header = lv_obj_create(guest_scr);
  lv_obj_set_size(header, lv_pct(100), 56);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Guest Monitoring");
  lv_obj_set_style_text_color(title, primary, 0);
  lv_obj_set_style_text_font(title, pick_font_mid(), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, -10);

  label_state = lv_label_create(header);
  lv_label_set_text(label_state, "Dang do sinh hieu...");
  lv_obj_set_style_text_color(label_state, accentR, 0);
  lv_obj_set_style_text_font(label_state, pick_font_small(), 0);
  lv_obj_align(label_state, LV_ALIGN_LEFT_MID, 0, 14);

  lv_obj_t *btn_back = lv_btn_create(header);
  lv_obj_set_size(btn_back, 92, 42);
  lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_radius(btn_back, 14, 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_make(210, 245, 255), 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_make(190, 235, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_back, 2, 0);
  lv_obj_set_style_border_color(btn_back, primary, 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_back_label = lv_label_create(btn_back);
  lv_label_set_text(btn_back_label, "Back");
  lv_obj_set_style_text_color(btn_back_label, dark, 0);
  lv_obj_set_style_text_font(btn_back_label, pick_font_mid(), 0);
  lv_obj_center(btn_back_label);

  lv_obj_t *metrics = lv_obj_create(guest_scr);
  lv_obj_set_size(metrics, lv_pct(100), 176);
  lv_obj_align(metrics, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(metrics, 0, 0);
  lv_obj_set_style_pad_all(metrics, 0, 0);
  lv_obj_set_style_pad_column(metrics, 10, 0);
  lv_obj_set_style_pad_row(metrics, 10, 0);
  lv_obj_clear_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(metrics, col, row);

  auto make_card = [&](const char *title_text, const char *unit_text, lv_coord_t c, lv_coord_t r, lv_color_t title_color, lv_obj_t **value_out) {
    lv_obj_t *card_obj = lv_obj_create(metrics);
    lv_obj_set_grid_cell(card_obj, LV_GRID_ALIGN_STRETCH, c, 1, LV_GRID_ALIGN_STRETCH, r, 1);
    lv_obj_set_style_radius(card_obj, 16, 0);
    lv_obj_set_style_border_width(card_obj, 2, 0);
    lv_obj_set_style_border_color(card_obj, lv_color_make(200, 235, 250), 0);
    lv_obj_set_style_bg_color(card_obj, card, 0);
    lv_obj_set_style_shadow_width(card_obj, 0, 0);
    lv_obj_set_style_pad_all(card_obj, 10, 0);
    lv_obj_clear_flag(card_obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *ttl = lv_label_create(card_obj);
    lv_label_set_text(ttl, title_text);
    lv_obj_set_style_text_color(ttl, title_color, 0);
    lv_obj_set_style_text_font(ttl, pick_font_mid(), 0);
    lv_obj_align(ttl, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *val = lv_label_create(card_obj);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_make(15, 75, 110), 0);
    lv_obj_set_style_text_font(val, pick_font_large(), 0);
    lv_obj_align(val, LV_ALIGN_LEFT_MID, 0, 4);

    lv_obj_t *unit = lv_label_create(card_obj);
    lv_label_set_text(unit, unit_text);
    lv_obj_set_style_text_color(unit, lv_color_make(90, 120, 140), 0);
    lv_obj_set_style_text_font(unit, pick_font_small(), 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    if (value_out) *value_out = val;
  };

  make_card("SPO2", "%", 0, 0, lv_color_make(0, 140, 200), &label_spo2);
  make_card("Heart Rate", "bpm", 1, 0, lv_color_make(220, 40, 40), &label_hr);
  make_card("Systolic", "mmHg", 0, 1, lv_color_make(0, 160, 110), &label_sys);
  make_card("Diastolic", "mmHg", 1, 1, lv_color_make(140, 90, 210), &label_dia);

  lv_obj_t *footer = lv_obj_create(guest_scr);
  lv_obj_set_size(footer, lv_pct(100), 22);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(footer, 0, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tip = lv_label_create(footer);
  lv_label_set_text(tip, "Guest mode: auto-start do SPO2, HR, BP ngay khi vao man hinh");
  lv_obj_set_style_text_color(tip, lv_color_make(90, 120, 140), 0);
  lv_obj_set_style_text_font(tip, pick_font_small(), 0);
  lv_obj_center(tip);
}

static void refresh_values() {
  if (!guest_scr) return;

  uint32_t t = millis() / 1000;
  int spo2 = 97 + (int)((t % 7 == 0) ? 1 : 0) - (int)((t % 13 == 0) ? 1 : 0);
  int hr   = 74 + (int)((t * 7) % 9) - 4;
  int sys  = 116 + (int)((t * 5) % 11) - 5;
  int dia  = 76 + (int)((t * 3) % 9) - 4;

  if (spo2 < 92) spo2 = 92;
  if (spo2 > 100) spo2 = 100;
  if (hr < 55) hr = 55;
  if (hr > 130) hr = 130;
  if (sys < 90) sys = 90;
  if (sys > 160) sys = 160;
  if (dia < 55) dia = 55;
  if (dia > 110) dia = 110;

  char buf[24];
  snprintf(buf, sizeof(buf), "%d", spo2);
  lv_label_set_text(label_spo2, buf);
  snprintf(buf, sizeof(buf), "%d", hr);
  lv_label_set_text(label_hr, buf);
  snprintf(buf, sizeof(buf), "%d", sys);
  lv_label_set_text(label_sys, buf);
  snprintf(buf, sizeof(buf), "%d", dia);
  lv_label_set_text(label_dia, buf);
}

void GuestMode_Show(GuestBackCallback backCallback) {
  g_back_callback = backCallback;
  build_guest_screen();
  g_active = true;
  g_start_ms = millis();
  if (guest_scr) {
    lv_label_set_text(label_spo2, "--");
    lv_label_set_text(label_hr, "--");
    lv_label_set_text(label_sys, "--");
    lv_label_set_text(label_dia, "--");
    lv_label_set_text(label_state, "Dang do sinh hieu...");
    lv_scr_load(guest_scr);
  }
}

void GuestMode_Loop() {
  if (!g_active || !guest_scr) return;

  uint32_t elapsed = millis() - g_start_ms;
  if (elapsed < 2500) {
    if (label_state) {
      uint32_t phase = (elapsed / 400) % 3;
      if (phase == 0) lv_label_set_text(label_state, "Dang do sinh hieu.");
      else if (phase == 1) lv_label_set_text(label_state, "Dang do sinh hieu..");
      else lv_label_set_text(label_state, "Dang do sinh hieu...");
    }
    return;
  }

  if (label_state) lv_label_set_text(label_state, "Da cap nhat du lieu suc khoe");
  refresh_values();
}

void GuestMode_UpdateStatus(const char *time_str, const char *batt_str) {
  (void)time_str;
  (void)batt_str;
}
