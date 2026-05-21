#include "GuestMode.h"

#include <Arduino.h>
#include <cstdio>
#include <lvgl.h>

#include "HR_SPO2_BP.h"


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

static lv_obj_t *btn_start = nullptr;

// Start button is decorative in Mode Offline; no event handler is attached.

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
  lv_label_set_text(title, "Mode Offline");
  lv_obj_set_style_text_color(title, primary, 0);
  lv_obj_set_style_text_font(title, pick_font_mid(), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, -15, -5);

  label_state = lv_label_create(header);
  lv_label_set_text(label_state, "");
  lv_obj_set_style_text_color(label_state, accentR, 0);
  lv_obj_set_style_text_font(label_state, pick_font_small(), 0);
  lv_obj_align(label_state, LV_ALIGN_LEFT_MID, 0, 14);

  lv_obj_t *btn_back = lv_btn_create(header);
  lv_obj_set_size(btn_back, 92, 42);
  lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, 10, 0);
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

  // Add a Start button next to Back in the header
  if (!btn_start) {
    btn_start = lv_btn_create(header);
    lv_obj_set_size(btn_start, 92, 42);
    // place to the left of Back
    lv_obj_align(btn_start, LV_ALIGN_RIGHT_MID, -90, 0);
    lv_obj_set_style_radius(btn_start, 14, 0);
    lv_obj_set_style_bg_color(btn_start, lv_color_make(200, 255, 220), 0);
    lv_obj_set_style_bg_color(btn_start, lv_color_make(150, 230, 180), LV_STATE_PRESSED);
    // add a darker green border to make the Start button stand out
    lv_obj_set_style_border_width(btn_start, 2, 0);
    lv_obj_set_style_border_color(btn_start, lv_color_make(0, 120, 60), 0);
    lv_obj_set_style_shadow_width(btn_start, 0, 0);
      // Decorative Start button: attach event handler to trigger BP measurement
      lv_obj_add_event_cb(btn_start, [](lv_event_t *e){
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        // disable the button to prevent re-entry
        lv_obj_add_state(btn_start, LV_STATE_DISABLED);
        if (label_state) lv_label_set_text(label_state, "Dang do huyet ap...");
        // trigger non-blocking background measurement
        startMeasureBloodPressureAsync();
      }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lb = lv_label_create(btn_start);
    lv_label_set_text(lb, "Start");
    lv_obj_set_style_text_color(lb, lv_color_make(0, 40, 20), 0);
    lv_obj_set_style_text_font(lb, pick_font_mid(), 0);
    lv_obj_center(lb);
  }

  lv_obj_t *metrics = lv_obj_create(guest_scr);
  lv_obj_set_size(metrics, lv_pct(100), 176);
  lv_obj_align(metrics, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(metrics, 0, 0);
  lv_obj_set_style_pad_all(metrics, 0, 0);
  lv_obj_set_style_pad_column(metrics, 10, 0);
  lv_obj_set_style_pad_row(metrics, 10, 0);
  lv_obj_clear_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);

  // Create two column layout: left column will contain Heart Rate (top) and SPO2 (bottom)
  // right column will contain Systolic (top) and Diastolic (bottom).
  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(metrics, col, row);

  lv_obj_t *left_col = lv_obj_create(metrics);
  lv_obj_set_grid_cell(left_col, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(left_col, 0, 0);
  lv_obj_set_style_pad_row(left_col, 10, 0);
  lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);
  // Frame border removed per request
  lv_obj_set_style_border_width(left_col, 0, 0);
  lv_obj_set_style_border_color(left_col, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_radius(left_col, 12, 0);

  lv_obj_t *right_col = lv_obj_create(metrics);
  lv_obj_set_grid_cell(right_col, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(right_col, 0, 0);
  lv_obj_set_style_pad_row(right_col, 10, 0);
  lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
  // Frame border removed per request
  lv_obj_set_style_border_width(right_col, 0, 0);
  lv_obj_set_style_border_color(right_col, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_radius(right_col, 12, 0);

  auto make_card = [&](lv_obj_t *parent, const char *title_text, const char *unit_text, lv_color_t title_color, lv_obj_t **value_out) {
    lv_obj_t *card_obj = lv_obj_create(parent);
    // Make each card take roughly half the metrics height
    lv_obj_set_size(card_obj, lv_pct(100), 80);
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

  // Left column: Heart Rate (top), SPO2 (bottom)
  make_card(left_col, "Heart Rate:", "bpm", lv_color_make(220, 40, 40), &label_hr);
  make_card(left_col, "SPO2:", "%", lv_color_make(0, 140, 200), &label_spo2);

  // Right column: Systolic (top), Diastolic (bottom)
  make_card(right_col, "Systolic:", "mmHg", lv_color_make(0, 160, 110), &label_sys);
  make_card(right_col, "Diastolic:", "mmHg", lv_color_make(140, 90, 210), &label_dia);

  lv_obj_t *footer = lv_obj_create(guest_scr);
  // Footer height adjusted so button fits without extra blank space below
  lv_obj_set_size(footer, lv_pct(100), 48);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(footer, 0, 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  // Removed footer tip label to make room for Start button in footer

  // Footer intentionally has no Start button anymore; header holds the Start control.
}

static void refresh_values() {
  // Update metric displays from sensor globals (MAX301 and BP results)
  if (!guest_scr) return;
  char buf[32];
  // SpO2
  if (label_spo2) {
    if (spo2 > 30 && spo2 <= 100) snprintf(buf, sizeof(buf), "%d", spo2);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(label_spo2, buf);
  }
  // Heart rate
  if (label_hr) {
    if (heartRate > 20 && heartRate < 300) snprintf(buf, sizeof(buf), "%d", heartRate);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(label_hr, buf);
  }
  // SYS/DIA are updated by BP run; keep current values
  if (label_sys) {
    if (isfinite(lastSYS) && lastSYS > 0.0f) snprintf(buf, sizeof(buf), "%.1f", lastSYS);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(label_sys, buf);
  }
  if (label_dia) {
    if (isfinite(lastDIA) && lastDIA > 0.0f) snprintf(buf, sizeof(buf), "%.1f", lastDIA);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(label_dia, buf);
  }
}

void GuestMode_Show(GuestBackCallback backCallback) {
  g_back_callback = backCallback;
  build_guest_screen();
  g_active = true;
  g_start_ms = millis();
  if (guest_scr) {
    // Leave metric displays empty
    lv_label_set_text(label_spo2, "");
    lv_label_set_text(label_hr, "");
    lv_label_set_text(label_sys, "");
    lv_label_set_text(label_dia, "");
    lv_scr_load(guest_scr);
    // Measurement modules removed; UI will use simulated/demo values from refresh_values()
  }
}

void GuestMode_Loop() {
  if (!g_active || !guest_scr) return;

  static bool wasMeasuring = false;

  uint32_t elapsed = millis() - g_start_ms;
  if (elapsed < 2500) {
    return;
  }

  // monitor background BP measurement state and update UI when finished
  bool measuring = isBPMeasuring();
  if (measuring && !wasMeasuring) {
    // just started
    wasMeasuring = true;
  } else if (!measuring && wasMeasuring) {
    // just finished
    // update SYS/DIA labels
    if (label_sys) {
      char buf[32];
      if (isfinite(lastSYS) && lastSYS > 0.0f) snprintf(buf, sizeof(buf), "%.1f", lastSYS);
      else snprintf(buf, sizeof(buf), "--");
      lv_label_set_text(label_sys, buf);
    }
    if (label_dia) {
      char buf[32];
      if (isfinite(lastDIA) && lastDIA > 0.0f) snprintf(buf, sizeof(buf), "%.1f", lastDIA);
      else snprintf(buf, sizeof(buf), "--");
      lv_label_set_text(label_dia, buf);
    }
    // re-enable Start button
    lv_obj_clear_state(btn_start, LV_STATE_DISABLED);
    wasMeasuring = false;
  }
  // no persistent status text
  refresh_values();
}

void GuestMode_UpdateStatus(const char *time_str, const char *batt_str) {
  (void)time_str;
  (void)batt_str;
}
