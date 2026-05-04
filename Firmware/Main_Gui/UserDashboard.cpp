#include "UserDashboard.h"

#include <Arduino.h>
#include <cstdio>
#include <ctype.h>
#include <lvgl.h>

#include "keypad.h"
#include <Preferences.h>

static lv_obj_t *ud_scr = nullptr;
static lv_obj_t *label_spo2 = nullptr;
static lv_obj_t *label_hr   = nullptr;
static lv_obj_t *label_sys  = nullptr;
static lv_obj_t *label_dia  = nullptr;
static lv_obj_t *label_phone = nullptr;
// settings UI labels for thresholds
static lv_obj_t *settings_label_spo2 = nullptr;
static lv_obj_t *settings_label_hr = nullptr;
static lv_obj_t *settings_label_sys = nullptr;
static lv_obj_t *settings_label_dia = nullptr;
static lv_obj_t *settings_scr = nullptr;
static lv_obj_t *settings_label_phone = nullptr;

static UserDashboardBackCallback g_back_callback = nullptr;
static bool g_active = false;
static uint32_t g_start_ms = 0;
static lv_obj_t *g_prev_scr = nullptr;

static char g_phone[32] = "";
static char g_thresh_spo2[16] = "";
static char g_thresh_hr[16] = "";
static char g_thresh_sys[16] = "";
static char g_thresh_dia[16] = "";

// NVS for persisting phone and threshold
static Preferences userPref;
static bool userPrefReady = false;
static const char *USER_NVS_NS = "usercfg";
static const char *USER_NVS_KEY_PHONE = "phone";
static const char *USER_NVS_KEY_THRESH_SPO2 = "th_spo2";
static const char *USER_NVS_KEY_THRESH_HR   = "th_hr";
static const char *USER_NVS_KEY_THRESH_SYS  = "th_sys";
static const char *USER_NVS_KEY_THRESH_DIA  = "th_dia";

static void load_settings_from_nvs() {
  if (!userPrefReady) {
    if (userPref.begin(USER_NVS_NS, false)) {
      userPrefReady = true;
    } else {
      userPrefReady = false;
    }
  }

  if (userPrefReady) {
    String p = userPref.getString(USER_NVS_KEY_PHONE, "");
    String s_spo2 = userPref.getString(USER_NVS_KEY_THRESH_SPO2, "");
    String s_hr   = userPref.getString(USER_NVS_KEY_THRESH_HR, "");
    String s_sys  = userPref.getString(USER_NVS_KEY_THRESH_SYS, "");
    String s_dia  = userPref.getString(USER_NVS_KEY_THRESH_DIA, "");
    if (p.length() > 0) p.toCharArray(g_phone, sizeof(g_phone));
    if (s_spo2.length() > 0) s_spo2.toCharArray(g_thresh_spo2, sizeof(g_thresh_spo2));
    if (s_hr.length() > 0)   s_hr.toCharArray(g_thresh_hr, sizeof(g_thresh_hr));
    if (s_sys.length() > 0)  s_sys.toCharArray(g_thresh_sys, sizeof(g_thresh_sys));
    if (s_dia.length() > 0)  s_dia.toCharArray(g_thresh_dia, sizeof(g_thresh_dia));
  }
}

static void save_settings_to_nvs() {
  if (!userPrefReady) return;
  userPref.putString(USER_NVS_KEY_PHONE, g_phone);
  userPref.putString(USER_NVS_KEY_THRESH_SPO2, g_thresh_spo2);
  userPref.putString(USER_NVS_KEY_THRESH_HR,   g_thresh_hr);
  userPref.putString(USER_NVS_KEY_THRESH_SYS,  g_thresh_sys);
  userPref.putString(USER_NVS_KEY_THRESH_DIA,  g_thresh_dia);
}

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

// keypad callbacks for editing phone / threshold
// keypad callbacks for editing phone / threshold
static void on_kp_back_from_edit(void) {
  // return to previous screen (dashboard or settings)
  if (g_prev_scr) lv_scr_load(g_prev_scr);
}

static void on_kp_next_phone(const char *text) {
  if (!text) text = "";
  // validate phone: require exactly 10 digits
  size_t len = strlen(text);
  if (len != 10) {
    // keep keypad open and show hint
    keypad_set_placeholder_text("Phone must be 10 digits");
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!isdigit((unsigned char)text[i])) {
      keypad_set_placeholder_text("Phone must be numeric (10 digits)");
      return;
    }
  }

  strncpy(g_phone, text, sizeof(g_phone)-1);
  g_phone[sizeof(g_phone)-1] = '\0';
  // update any visible labels
  if (settings_label_phone) lv_label_set_text(settings_label_phone, g_phone[0] ? g_phone : "(none)");
  if (label_phone) lv_label_set_text(label_phone, g_phone[0] ? g_phone : "(none)");
  // persist and return to previous screen
  save_settings_to_nvs();
  if (g_prev_scr) lv_scr_load(g_prev_scr);
}

// Generic threshold editing support
typedef enum { TT_SPO2 = 0, TT_HR = 1, TT_SYS = 2, TT_DIA = 3 } ThreshTarget;
static ThreshTarget g_thresh_target = TT_SPO2;

static void on_kp_next_thresh_generic(const char *text) {
  if (!text) text = "";
  switch (g_thresh_target) {
    case TT_SPO2:
      strncpy(g_thresh_spo2, text, sizeof(g_thresh_spo2)-1);
      g_thresh_spo2[sizeof(g_thresh_spo2)-1] = '\0';
      if (settings_label_spo2) lv_label_set_text(settings_label_spo2, g_thresh_spo2[0] ? g_thresh_spo2 : "(none)");
      break;
    case TT_HR:
      strncpy(g_thresh_hr, text, sizeof(g_thresh_hr)-1);
      g_thresh_hr[sizeof(g_thresh_hr)-1] = '\0';
      if (settings_label_hr) lv_label_set_text(settings_label_hr, g_thresh_hr[0] ? g_thresh_hr : "(none)");
      break;
    case TT_SYS:
      strncpy(g_thresh_sys, text, sizeof(g_thresh_sys)-1);
      g_thresh_sys[sizeof(g_thresh_sys)-1] = '\0';
      if (settings_label_sys) lv_label_set_text(settings_label_sys, g_thresh_sys[0] ? g_thresh_sys : "(none)");
      break;
    case TT_DIA:
      strncpy(g_thresh_dia, text, sizeof(g_thresh_dia)-1);
      g_thresh_dia[sizeof(g_thresh_dia)-1] = '\0';
      if (settings_label_dia) lv_label_set_text(settings_label_dia, g_thresh_dia[0] ? g_thresh_dia : "(none)");
      break;
  }
  // persist and return
  save_settings_to_nvs();
  if (g_prev_scr) lv_scr_load(g_prev_scr);
}

static void open_keypad_for_phone() {
  // remember previous screen so we can return correctly
  g_prev_scr = lv_scr_act();
  // re-init keypad with our callbacks
  keypad_init_screen(NULL, NULL, on_kp_back_from_edit, on_kp_next_phone, "Save");
  keypad_set_text(g_phone);
  keypad_set_placeholder_text("Nhap so dien thoai...");
  lv_obj_t *scr = keypad_get_screen();
  if (scr) lv_scr_load(scr);
}

static void open_keypad_for_thresh(ThreshTarget target) {
  g_prev_scr = lv_scr_act();
  g_thresh_target = target;
  keypad_init_screen(NULL, NULL, on_kp_back_from_edit, on_kp_next_thresh_generic, "Save");
  switch (g_thresh_target) {
    case TT_SPO2: keypad_set_text(g_thresh_spo2); keypad_set_placeholder_text("Nhap nguong SPO2..."); break;
    case TT_HR:   keypad_set_text(g_thresh_hr);   keypad_set_placeholder_text("Nhap nguong Heart Rate..."); break;
    case TT_SYS:  keypad_set_text(g_thresh_sys);  keypad_set_placeholder_text("Nhap nguong Systolic..."); break;
    case TT_DIA:  keypad_set_text(g_thresh_dia);  keypad_set_placeholder_text("Nhap nguong Diastolic..."); break;
  }
  lv_obj_t *scr = keypad_get_screen();
  if (scr) lv_scr_load(scr);
}

static void build_settings_screen() {
  if (settings_scr) return;
  settings_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(settings_scr, lv_color_make(245, 252, 255), 0);
  lv_obj_set_style_pad_all(settings_scr, 12, 0);

  lv_obj_t *h = lv_obj_create(settings_scr);
  lv_obj_set_size(h, lv_pct(100), 56);
  lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = lv_label_create(h);
  lv_label_set_text(t, "User Settings");
  lv_obj_set_style_text_font(t, pick_font_mid(), 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -8);

  // back button
  lv_obj_t *bback = lv_btn_create(h);
  lv_obj_set_size(bback, 92, 42);
  lv_obj_align(bback, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *bbt = lv_label_create(bback);
  lv_label_set_text(bbt, "Back");
  lv_obj_center(bbt);
  lv_obj_add_event_cb(bback, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) { if (ud_scr) lv_scr_load(ud_scr); } }, LV_EVENT_ALL, nullptr);

  // content
  lv_obj_t *cont = lv_obj_create(settings_scr);
  lv_obj_set_size(cont, lv_pct(100), 200);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_set_style_pad_all(cont, 8, 0);
  lv_obj_set_style_bg_color(cont, lv_color_white(), 0);

  // Phone row: label on left, value next to it, edit button aligned to top-right
  lv_obj_t *lblp = lv_label_create(cont);
  lv_label_set_text(lblp, "Phone:");
  lv_obj_set_style_text_font(lblp, pick_font_small(), 0);
  lv_obj_align(lblp, LV_ALIGN_TOP_LEFT, 8, 8);

  // place phone value to the right of the "Phone:" label on the same line
  settings_label_phone = lv_label_create(cont);
  lv_label_set_text(settings_label_phone, g_phone[0] ? g_phone : "(none)");
  lv_obj_set_style_text_font(settings_label_phone, pick_font_small(), 0);
  lv_obj_align(settings_label_phone, LV_ALIGN_TOP_LEFT, 80, 8);

  // edit button stays on the top-right of the container (same vertical alignment as the label)
  lv_obj_t *ep = lv_btn_create(cont);
  // Make size match other Edit buttons and remove trailing space from label
  lv_obj_set_size(ep, 100, 30);
  lv_obj_align(ep, LV_ALIGN_TOP_RIGHT, -8, 4);
  lv_obj_add_event_cb(ep, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) open_keypad_for_phone(); }, LV_EVENT_ALL, nullptr);
  lv_obj_t *ep_l = lv_label_create(ep);
  lv_label_set_text(ep_l, "Edit");
  lv_obj_center(ep_l);

  // Thresholds: show four editable threshold values (SPO2, Heart Rate, Systolic, Diastolic)
  lv_obj_t *lbl_spo2 = lv_label_create(cont);
  lv_label_set_text(lbl_spo2, "SPO2 Threshold:");
  lv_obj_set_style_text_font(lbl_spo2, pick_font_small(), 0);
  lv_obj_align(lbl_spo2, LV_ALIGN_TOP_LEFT, 8, 56);
  settings_label_spo2 = lv_label_create(cont);
  lv_label_set_text(settings_label_spo2, g_thresh_spo2[0] ? g_thresh_spo2 : "(none)");
  lv_obj_set_style_text_font(settings_label_spo2, pick_font_small(), 0);
  /* Align the value to the right side of the container so it doesn't overlap the label text */
  lv_obj_align(settings_label_spo2, LV_ALIGN_TOP_RIGHT, -136, 56);
  lv_obj_t *btn_spo2 = lv_btn_create(cont);
  lv_obj_set_size(btn_spo2, 100, 30);
  lv_obj_align(btn_spo2, LV_ALIGN_TOP_RIGHT, -8, 52);
  lv_obj_add_event_cb(btn_spo2, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) open_keypad_for_thresh(TT_SPO2); }, LV_EVENT_ALL, nullptr);
  lv_obj_t *btn_spo2_l = lv_label_create(btn_spo2); lv_label_set_text(btn_spo2_l, "Edit"); lv_obj_center(btn_spo2_l);

  lv_obj_t *lbl_hr = lv_label_create(cont);
  lv_label_set_text(lbl_hr, "Heart Rate Threshold:");
  lv_obj_set_style_text_font(lbl_hr, pick_font_small(), 0);
  lv_obj_align(lbl_hr, LV_ALIGN_TOP_LEFT, 8, 96);
  settings_label_hr = lv_label_create(cont);
  lv_label_set_text(settings_label_hr, g_thresh_hr[0] ? g_thresh_hr : "(none)");
  lv_obj_set_style_text_font(settings_label_hr, pick_font_small(), 0);
  lv_obj_align(settings_label_hr, LV_ALIGN_TOP_RIGHT, -136, 96);
  lv_obj_t *btn_hr = lv_btn_create(cont);
  lv_obj_set_size(btn_hr, 100, 30);
  lv_obj_align(btn_hr, LV_ALIGN_TOP_RIGHT, -8, 92);
  lv_obj_add_event_cb(btn_hr, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) open_keypad_for_thresh(TT_HR); }, LV_EVENT_ALL, nullptr);
  lv_obj_t *btn_hr_l = lv_label_create(btn_hr); lv_label_set_text(btn_hr_l, "Edit"); lv_obj_center(btn_hr_l);

  lv_obj_t *lbl_sys = lv_label_create(cont);
  lv_label_set_text(lbl_sys, "Systolic Threshold:");
  lv_obj_set_style_text_font(lbl_sys, pick_font_small(), 0);
  lv_obj_align(lbl_sys, LV_ALIGN_TOP_LEFT, 8, 136);
  settings_label_sys = lv_label_create(cont);
  lv_label_set_text(settings_label_sys, g_thresh_sys[0] ? g_thresh_sys : "(none)");
  lv_obj_set_style_text_font(settings_label_sys, pick_font_small(), 0);
  lv_obj_align(settings_label_sys, LV_ALIGN_TOP_RIGHT, -136, 136);
  lv_obj_t *btn_sys = lv_btn_create(cont);
  lv_obj_set_size(btn_sys, 100, 30);
  lv_obj_align(btn_sys, LV_ALIGN_TOP_RIGHT, -8, 132);
  lv_obj_add_event_cb(btn_sys, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) open_keypad_for_thresh(TT_SYS); }, LV_EVENT_ALL, nullptr);
  lv_obj_t *btn_sys_l = lv_label_create(btn_sys); lv_label_set_text(btn_sys_l, "Edit"); lv_obj_center(btn_sys_l);

  lv_obj_t *lbl_dia = lv_label_create(cont);
  lv_label_set_text(lbl_dia, "Diastolic Threshold:");
  lv_obj_set_style_text_font(lbl_dia, pick_font_small(), 0);
  lv_obj_align(lbl_dia, LV_ALIGN_TOP_LEFT, 8, 176);
  settings_label_dia = lv_label_create(cont);
  lv_label_set_text(settings_label_dia, g_thresh_dia[0] ? g_thresh_dia : "(none)");
  lv_obj_set_style_text_font(settings_label_dia, pick_font_small(), 0);
  lv_obj_align(settings_label_dia, LV_ALIGN_TOP_RIGHT, -136, 176);
  lv_obj_t *btn_dia = lv_btn_create(cont);
  lv_obj_set_size(btn_dia, 100, 30);
  lv_obj_align(btn_dia, LV_ALIGN_TOP_RIGHT, -8, 172);
  lv_obj_add_event_cb(btn_dia, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) open_keypad_for_thresh(TT_DIA); }, LV_EVENT_ALL, nullptr);
  lv_obj_t *btn_dia_l = lv_label_create(btn_dia); lv_label_set_text(btn_dia_l, "Edit"); lv_obj_center(btn_dia_l);
}

static void build_ud_screen() {
  // load persisted values once before building UI
  load_settings_from_nvs();
  if (ud_scr) return;

  ud_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(ud_scr, lv_color_make(245, 252, 255), 0);
  lv_obj_clear_flag(ud_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(ud_scr, 12, 0);

  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark    = lv_color_make(10, 60, 90);
  lv_color_t card     = lv_color_white();

  lv_obj_t *header = lv_obj_create(ud_scr);
  lv_obj_set_size(header, lv_pct(100), 56);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "User Monitoring");
  lv_obj_set_style_text_color(title, primary, 0);
  lv_obj_set_style_text_font(title, pick_font_mid(), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, -8);

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

  // Metrics area (similar to guest)
  lv_obj_t *metrics = lv_obj_create(ud_scr);
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

  // Menu (hamburger) button in header to open settings screen
  lv_obj_t *btn_menu = lv_btn_create(header);
  // make wider to fit the "Setting" label
  lv_obj_set_size(btn_menu, 110, 42);
  // place left of back button
  lv_obj_align(btn_menu, LV_ALIGN_RIGHT_MID, -100, 0);
  lv_obj_set_style_radius(btn_menu, 12, 0);
  lv_obj_set_style_bg_color(btn_menu, lv_color_make(240, 240, 240), 0);
  lv_obj_set_style_border_width(btn_menu, 0, 0);
  lv_obj_add_event_cb(btn_menu, [](lv_event_t *e){ if (lv_event_get_code(e)==LV_EVENT_CLICKED) {
    // lazy build then show settings screen
    build_settings_screen();
    if (settings_scr) lv_scr_load(settings_scr);
  } }, LV_EVENT_ALL, nullptr);
  lv_obj_t *menu_lbl = lv_label_create(btn_menu);
  lv_label_set_text(menu_lbl, "Setting");
  lv_obj_set_style_text_font(menu_lbl, pick_font_mid(), 0);
  lv_obj_center(menu_lbl);

}

static void refresh_values() {
  if (!ud_scr) return;

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

void UserDashboard_Show(UserDashboardBackCallback backCallback) {
  g_back_callback = backCallback;
  build_ud_screen();
  g_active = true;
  g_start_ms = millis();
  if (ud_scr) {
    lv_label_set_text(label_spo2, "--");
    lv_label_set_text(label_hr, "--");
    lv_label_set_text(label_sys, "--");
    lv_label_set_text(label_dia, "--");
    if (settings_label_phone) lv_label_set_text(settings_label_phone, g_phone[0] ? g_phone : "(none)");
    if (settings_label_spo2) lv_label_set_text(settings_label_spo2, g_thresh_spo2[0] ? g_thresh_spo2 : "(none)");
    if (settings_label_hr)   lv_label_set_text(settings_label_hr,   g_thresh_hr[0] ? g_thresh_hr : "(none)");
    if (settings_label_sys)  lv_label_set_text(settings_label_sys,  g_thresh_sys[0] ? g_thresh_sys : "(none)");
    if (settings_label_dia)  lv_label_set_text(settings_label_dia,  g_thresh_dia[0] ? g_thresh_dia : "(none)");
    lv_scr_load(ud_scr);
  }
}

void UserDashboard_Loop() {
  if (!g_active || !ud_scr) return;

  uint32_t elapsed = millis() - g_start_ms;
  if (elapsed < 2500) {
    return;
  }

  refresh_values();
}

void UserDashboard_UpdateStatus(const char *time_str, const char *batt_str) {
  (void)time_str;
  (void)batt_str;
}
