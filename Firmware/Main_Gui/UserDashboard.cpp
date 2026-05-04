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
static lv_obj_t *settings_scr = nullptr;
static lv_obj_t *settings_label_phone = nullptr;
// labels for thresholds in settings
static lv_obj_t *settings_label_spo2_min = nullptr;
static lv_obj_t *settings_label_spo2_max = nullptr;
static lv_obj_t *settings_label_hr_min = nullptr;
static lv_obj_t *settings_label_hr_max = nullptr;
static lv_obj_t *settings_label_sys_min = nullptr;
static lv_obj_t *settings_label_sys_max = nullptr;
static lv_obj_t *settings_label_dia_min = nullptr;
static lv_obj_t *settings_label_dia_max = nullptr;
// summary labels for main metric rows
static lv_obj_t *settings_label_spo2_summary = nullptr;
static lv_obj_t *settings_label_hr_summary = nullptr;
static lv_obj_t *settings_label_sys_summary = nullptr;
static lv_obj_t *settings_label_dia_summary = nullptr;

// per-metric subscreens
static lv_obj_t *spo2_scr = nullptr;
static lv_obj_t *hr_scr = nullptr;
static lv_obj_t *sys_scr = nullptr;
static lv_obj_t *dia_scr = nullptr;

// forward declare metric screen builder
static void build_metric_screen();

// edit target for keypad (forward-declare early so callbacks can use it)
enum EditTarget {
  EDIT_NONE = 0,
  EDIT_PHONE,
  EDIT_SPO2_MIN,
  EDIT_SPO2_MAX,
  EDIT_HR_MIN,
  EDIT_HR_MAX,
  EDIT_SYS_MIN,
  EDIT_SYS_MAX,
  EDIT_DIA_MIN,
  EDIT_DIA_MAX,
};

// forward declare keypad opener so callbacks can call it
static void open_keypad_for_threshold(EditTarget target, const char *placeholder);

// small struct to pass to event callback
struct MetricEditData {
  EditTarget target;
  const char *placeholder;
};

static void metric_edit_event_cb(lv_event_t *ev) {
  if (lv_event_get_code(ev) != LV_EVENT_CLICKED) return;
  MetricEditData *d = (MetricEditData*)lv_event_get_user_data(ev);
  if (!d) return;
  open_keypad_for_threshold(d->target, d->placeholder);
}

static UserDashboardBackCallback g_back_callback = nullptr;
static bool g_active = false;
static uint32_t g_start_ms = 0;
static lv_obj_t *g_prev_scr = nullptr;
// saved original previous screen for multi-step keypad flows
static lv_obj_t *g_saved_prev_scr = nullptr;

static char g_phone[32] = "";
// thresholds (min/max for each metric)
static int g_spo2_min = 92;
static int g_spo2_max = 100;
static int g_hr_min   = 55;
static int g_hr_max   = 130;
static int g_sys_min  = 90;
static int g_sys_max  = 160;
static int g_dia_min  = 55;
static int g_dia_max  = 110;

// current edit target (uses enum defined above)
static EditTarget g_current_edit = EDIT_NONE;

// NVS for persisting phone and threshold
static Preferences userPref;
static bool userPrefReady = false;
static const char *USER_NVS_NS = "usercfg";
static const char *USER_NVS_KEY_PHONE = "phone";
static const char *USER_NVS_KEY_SPO2_MIN = "spo2_min";
static const char *USER_NVS_KEY_SPO2_MAX = "spo2_max";
static const char *USER_NVS_KEY_HR_MIN   = "hr_min";
static const char *USER_NVS_KEY_HR_MAX   = "hr_max";
static const char *USER_NVS_KEY_SYS_MIN  = "sys_min";
static const char *USER_NVS_KEY_SYS_MAX  = "sys_max";
static const char *USER_NVS_KEY_DIA_MIN  = "dia_min";
static const char *USER_NVS_KEY_DIA_MAX  = "dia_max";

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
    if (p.length() > 0) p.toCharArray(g_phone, sizeof(g_phone));
    // load thresholds
    g_spo2_min = userPref.getInt(USER_NVS_KEY_SPO2_MIN, g_spo2_min);
    g_spo2_max = userPref.getInt(USER_NVS_KEY_SPO2_MAX, g_spo2_max);
    g_hr_min   = userPref.getInt(USER_NVS_KEY_HR_MIN, g_hr_min);
    g_hr_max   = userPref.getInt(USER_NVS_KEY_HR_MAX, g_hr_max);
    g_sys_min  = userPref.getInt(USER_NVS_KEY_SYS_MIN, g_sys_min);
    g_sys_max  = userPref.getInt(USER_NVS_KEY_SYS_MAX, g_sys_max);
    g_dia_min  = userPref.getInt(USER_NVS_KEY_DIA_MIN, g_dia_min);
    g_dia_max  = userPref.getInt(USER_NVS_KEY_DIA_MAX, g_dia_max);
  }
}

static void save_settings_to_nvs() {
  // ensure preferences is opened for write; if not ready, try to begin now
  if (!userPrefReady) {
    if (userPref.begin(USER_NVS_NS, false)) {
      userPrefReady = true;
    } else {
      // cannot open NVS, skip saving
      return;
    }
  }
  userPref.putString(USER_NVS_KEY_PHONE, g_phone);
  userPref.putInt(USER_NVS_KEY_SPO2_MIN, g_spo2_min);
  userPref.putInt(USER_NVS_KEY_SPO2_MAX, g_spo2_max);
  userPref.putInt(USER_NVS_KEY_HR_MIN, g_hr_min);
  userPref.putInt(USER_NVS_KEY_HR_MAX, g_hr_max);
  userPref.putInt(USER_NVS_KEY_SYS_MIN, g_sys_min);
  userPref.putInt(USER_NVS_KEY_SYS_MAX, g_sys_max);
  userPref.putInt(USER_NVS_KEY_DIA_MIN, g_dia_min);
  userPref.putInt(USER_NVS_KEY_DIA_MAX, g_dia_max);
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
  if (g_saved_prev_scr) {
    lv_scr_load(g_saved_prev_scr);
    g_saved_prev_scr = nullptr;
    g_current_edit = EDIT_NONE;
  }
}

// Safely load the saved previous screen. Sometimes the saved pointer may
// refer to a deleted/invalid object (e.g. the keypad screen); in that case
// fall back to known persistent screens to avoid loading a bad pointer and
// causing a blank/white screen.
static void safe_load_saved_prev_scr(void) {
  lv_obj_t *kp = keypad_get_screen();
  if (g_saved_prev_scr && g_saved_prev_scr != kp) {
    lv_scr_load(g_saved_prev_scr);
  } else if (settings_scr) {
    lv_scr_load(settings_scr);
  } else if (ud_scr) {
    lv_scr_load(ud_scr);
  }
  g_saved_prev_scr = nullptr;
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
  safe_load_saved_prev_scr();
}

static bool parse_int_str(const char *text, int &out) {
  if (!text) return false;
  char *endptr = nullptr;
  long v = strtol(text, &endptr, 10);
  if (endptr == text || *endptr != '\0') return false;
  out = (int)v;
  return true;
}

static void update_settings_label_int(lv_obj_t *lbl, int val, const char *none_text="(none)") {
  if (!lbl) return;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", val);
  lv_label_set_text(lbl, buf);
}

static void on_kp_next_threshold(const char *text) {
  if (!text) text = "";
  int v = 0;
  if (!parse_int_str(text, v)) {
    keypad_set_placeholder_text("Value must be numeric");
    return;
  }

  // validate and assign based on current edit target
  switch (g_current_edit) {
    case EDIT_SPO2_MIN:
      if (v < 50 || v > 100) { keypad_set_placeholder_text("SPO2 min must be 50-100"); return; }
      if (v > g_spo2_max) { keypad_set_placeholder_text("Min cannot be > Max"); return; }
      g_spo2_min = v;
      update_settings_label_int(settings_label_spo2_min, g_spo2_min);
      break;
    case EDIT_SPO2_MAX:
      if (v < 50 || v > 100) { keypad_set_placeholder_text("SPO2 max must be 50-100"); return; }
      if (v < g_spo2_min) { keypad_set_placeholder_text("Max cannot be < Min"); return; }
      g_spo2_max = v;
      update_settings_label_int(settings_label_spo2_max, g_spo2_max);
      break;
    case EDIT_HR_MIN:
      if (v < 30 || v > 220) { keypad_set_placeholder_text("HR min must be 30-220"); return; }
      if (v > g_hr_max) { keypad_set_placeholder_text("Min cannot be > Max"); return; }
      g_hr_min = v;
      update_settings_label_int(settings_label_hr_min, g_hr_min);
      break;
    case EDIT_HR_MAX:
      if (v < 30 || v > 220) { keypad_set_placeholder_text("HR max must be 30-220"); return; }
      if (v < g_hr_min) { keypad_set_placeholder_text("Max cannot be < Min"); return; }
      g_hr_max = v;
      update_settings_label_int(settings_label_hr_max, g_hr_max);
      break;
    case EDIT_SYS_MIN:
      if (v < 60 || v > 250) { keypad_set_placeholder_text("Sys min must be 60-250"); return; }
      if (v > g_sys_max) { keypad_set_placeholder_text("Min cannot be > Max"); return; }
      g_sys_min = v;
      update_settings_label_int(settings_label_sys_min, g_sys_min);
      break;
    case EDIT_SYS_MAX:
      if (v < 60 || v > 250) { keypad_set_placeholder_text("Sys max must be 60-250"); return; }
      if (v < g_sys_min) { keypad_set_placeholder_text("Max cannot be < Min"); return; }
      g_sys_max = v;
      update_settings_label_int(settings_label_sys_max, g_sys_max);
      break;
    case EDIT_DIA_MIN:
      if (v < 30 || v > 180) { keypad_set_placeholder_text("Dia min must be 30-180"); return; }
      if (v > g_dia_max) { keypad_set_placeholder_text("Min cannot be > Max"); return; }
      g_dia_min = v;
      update_settings_label_int(settings_label_dia_min, g_dia_min);
      break;
    case EDIT_DIA_MAX:
      if (v < 30 || v > 180) { keypad_set_placeholder_text("Dia max must be 30-180"); return; }
      if (v < g_dia_min) { keypad_set_placeholder_text("Max cannot be < Min"); return; }
      g_dia_max = v;
      update_settings_label_int(settings_label_dia_max, g_dia_max);
      break;
    default:
      // unknown target, ignore
      break;
  }

  // persist and return
  save_settings_to_nvs();
  // update any summary labels on the settings main screen
  if (settings_label_spo2_summary) {
    char b[32]; snprintf(b, sizeof(b), "%d - %d", g_spo2_min, g_spo2_max);
    lv_label_set_text(settings_label_spo2_summary, b);
  }
  if (settings_label_hr_summary) {
    char b[32]; snprintf(b, sizeof(b), "%d - %d", g_hr_min, g_hr_max);
    lv_label_set_text(settings_label_hr_summary, b);
  }
  if (settings_label_sys_summary) {
    char b[32]; snprintf(b, sizeof(b), "%d - %d", g_sys_min, g_sys_max);
    lv_label_set_text(settings_label_sys_summary, b);
  }
  if (settings_label_dia_summary) {
    char b[32]; snprintf(b, sizeof(b), "%d - %d", g_dia_min, g_dia_max);
    lv_label_set_text(settings_label_dia_summary, b);
  }
  safe_load_saved_prev_scr();
  g_current_edit = EDIT_NONE;
}

static void open_keypad_for_threshold(EditTarget target, const char *placeholder) {
  // remember previous screen so we can return correctly
  g_prev_scr = lv_scr_act();
  g_saved_prev_scr = g_prev_scr;
  g_current_edit = target;
  // set initial text based on target
  char buf[32];
  switch (target) {
    case EDIT_SPO2_MIN: snprintf(buf, sizeof(buf), "%d", g_spo2_min); break;
    case EDIT_SPO2_MAX: snprintf(buf, sizeof(buf), "%d", g_spo2_max); break;
    case EDIT_HR_MIN:   snprintf(buf, sizeof(buf), "%d", g_hr_min); break;
    case EDIT_HR_MAX:   snprintf(buf, sizeof(buf), "%d", g_hr_max); break;
    case EDIT_SYS_MIN:  snprintf(buf, sizeof(buf), "%d", g_sys_min); break;
    case EDIT_SYS_MAX:  snprintf(buf, sizeof(buf), "%d", g_sys_max); break;
    case EDIT_DIA_MIN:  snprintf(buf, sizeof(buf), "%d", g_dia_min); break;
    case EDIT_DIA_MAX:  snprintf(buf, sizeof(buf), "%d", g_dia_max); break;
    default: buf[0] = '\0'; break;
  }
  keypad_init_screen(NULL, NULL, on_kp_back_from_edit, on_kp_next_threshold, "Save");
  keypad_set_text(buf);
  if (placeholder) keypad_set_placeholder_text(placeholder);
  lv_obj_t *scr = keypad_get_screen();
  if (scr) lv_scr_load(scr);
}

static void open_keypad_for_phone() {
  // remember previous screen so we can return correctly
  g_prev_scr = lv_scr_act();
  // Always save the current screen as the return target so the keypad
  // back/finish handlers return to the screen that opened the keypad.
  g_saved_prev_scr = g_prev_scr;
  // re-init keypad with our callbacks
  keypad_init_screen(NULL, NULL, on_kp_back_from_edit, on_kp_next_phone, "Save");
  keypad_set_text(g_phone);
  keypad_set_placeholder_text("Nhap so dien thoai...");
  lv_obj_t *scr = keypad_get_screen();
  if (scr) lv_scr_load(scr);
}
// threshold editing removed

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
  lv_obj_set_size(cont, lv_pct(100), 260);
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
  // four main metric rows: each shows "min - max" summary and Edit button to open metric screen
  auto make_metric_row = [&](const char *name, lv_obj_t **summary_lbl, lv_event_cb_t cb) {
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, pick_font_small(), 0);
    // vertical placement: find next Y by counting children? simple fixed offsets
    static int row_y = 48;
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, row_y);

    *summary_lbl = lv_label_create(cont);
    lv_label_set_text(*summary_lbl, "-- - --");
    lv_obj_set_style_text_font(*summary_lbl, pick_font_small(), 0);
    lv_obj_align(*summary_lbl, LV_ALIGN_TOP_LEFT, 120, row_y);

    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 80, 34);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, row_y-4);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lblb = lv_label_create(btn);
    lv_label_set_text(lblb, "Edit");
    lv_obj_center(lblb);

    row_y += 44;
  };

  make_metric_row("SPO2", &settings_label_spo2_summary, [](lv_event_t *e){ build_metric_screen(); if (spo2_scr) lv_scr_load(spo2_scr); });
  make_metric_row("Heart Rate", &settings_label_hr_summary, [](lv_event_t *e){ build_metric_screen(); if (hr_scr) lv_scr_load(hr_scr); });
  make_metric_row("Systolic", &settings_label_sys_summary, [](lv_event_t *e){ build_metric_screen(); if (sys_scr) lv_scr_load(sys_scr); });
  make_metric_row("Diastolic", &settings_label_dia_summary, [](lv_event_t *e){ build_metric_screen(); if (dia_scr) lv_scr_load(dia_scr); });

  // initialize summaries from loaded settings
  if (settings_label_spo2_summary) { char b[32]; snprintf(b,sizeof(b),"%d - %d", g_spo2_min, g_spo2_max); lv_label_set_text(settings_label_spo2_summary, b); }
  if (settings_label_hr_summary)   { char b[32]; snprintf(b,sizeof(b),"%d - %d", g_hr_min, g_hr_max); lv_label_set_text(settings_label_hr_summary, b); }
  if (settings_label_sys_summary)  { char b[32]; snprintf(b,sizeof(b),"%d - %d", g_sys_min, g_sys_max); lv_label_set_text(settings_label_sys_summary, b); }
  if (settings_label_dia_summary)  { char b[32]; snprintf(b,sizeof(b),"%d - %d", g_dia_min, g_dia_max); lv_label_set_text(settings_label_dia_summary, b); }
}

static void build_metric_screen() {
  if (spo2_scr || hr_scr || sys_scr || dia_scr) return;

  // Helper to build a simple metric screen with Min/Max rows
  auto make_metric_screen = [&](lv_obj_t **out_scr, const char *title_text,
                                lv_obj_t **lbl_min_out, lv_obj_t **lbl_max_out,
                                EditTarget min_tgt, EditTarget max_tgt) {
    if (*out_scr) return;
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_make(245, 252, 255), 0);
    lv_obj_set_style_pad_all(scr, 12, 0);

    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_set_size(h, lv_pct(100), 56);
    lv_obj_set_style_bg_opa(h, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = lv_label_create(h);
    lv_label_set_text(t, title_text);
    lv_obj_set_style_text_font(t, pick_font_mid(), 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -8);

    lv_obj_t *bback = lv_btn_create(h);
    lv_obj_set_size(bback, 92, 42);
    lv_obj_align(bback, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *bbt = lv_label_create(bback);
    lv_label_set_text(bbt, "Back");
    lv_obj_center(bbt);
    lv_obj_add_event_cb(bback, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) { if (settings_scr) lv_scr_load(settings_scr); } }, LV_EVENT_ALL, nullptr);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, lv_pct(100), 200);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_color(cont, lv_color_white(), 0);

    // Min row
    lv_obj_t *lbl_min = lv_label_create(cont);
    lv_label_set_text(lbl_min, "Min:");
    lv_obj_set_style_text_font(lbl_min, pick_font_small(), 0);
    lv_obj_align(lbl_min, LV_ALIGN_TOP_LEFT, 8, 8);

    *lbl_min_out = lv_label_create(cont);
    lv_obj_set_style_text_font(*lbl_min_out, pick_font_small(), 0);
    lv_obj_align(*lbl_min_out, LV_ALIGN_TOP_LEFT, 120, 8);

    lv_obj_t *btn_min = lv_btn_create(cont);
    lv_obj_set_size(btn_min, 100, 34);
    lv_obj_align(btn_min, LV_ALIGN_TOP_RIGHT, -8, 4);
    // allocate small struct to keep target and placeholder alive
    MetricEditData *dmin = new MetricEditData();
    dmin->target = min_tgt;
    dmin->placeholder = "Enter min value";
    lv_obj_add_event_cb(btn_min, metric_edit_event_cb, LV_EVENT_CLICKED, dmin);
    lv_obj_t *lminb = lv_label_create(btn_min); lv_label_set_text(lminb, "Edit"); lv_obj_center(lminb);

    // Max row
    lv_obj_t *lbl_max = lv_label_create(cont);
    lv_label_set_text(lbl_max, "Max:");
    lv_obj_set_style_text_font(lbl_max, pick_font_small(), 0);
    lv_obj_align(lbl_max, LV_ALIGN_TOP_LEFT, 8, 56);

    *lbl_max_out = lv_label_create(cont);
    lv_obj_set_style_text_font(*lbl_max_out, pick_font_small(), 0);
    lv_obj_align(*lbl_max_out, LV_ALIGN_TOP_LEFT, 120, 56);

    lv_obj_t *btn_max = lv_btn_create(cont);
    lv_obj_set_size(btn_max, 100, 34);
    lv_obj_align(btn_max, LV_ALIGN_TOP_RIGHT, -8, 52);
    MetricEditData *dmax = new MetricEditData();
    dmax->target = max_tgt;
    dmax->placeholder = "Enter max value";
    lv_obj_add_event_cb(btn_max, metric_edit_event_cb, LV_EVENT_CLICKED, dmax);
    lv_obj_t *lmaxb = lv_label_create(btn_max); lv_label_set_text(lmaxb, "Edit"); lv_obj_center(lmaxb);

    *out_scr = scr;
  };

  make_metric_screen(&spo2_scr, "SPO2 Settings", &settings_label_spo2_min, &settings_label_spo2_max, EDIT_SPO2_MIN, EDIT_SPO2_MAX);
  make_metric_screen(&hr_scr, "Heart Rate Settings", &settings_label_hr_min, &settings_label_hr_max, EDIT_HR_MIN, EDIT_HR_MAX);
  make_metric_screen(&sys_scr, "Systolic Settings", &settings_label_sys_min, &settings_label_sys_max, EDIT_SYS_MIN, EDIT_SYS_MAX);
  make_metric_screen(&dia_scr, "Diastolic Settings", &settings_label_dia_min, &settings_label_dia_max, EDIT_DIA_MIN, EDIT_DIA_MAX);

  // populate label values
  if (settings_label_spo2_min) { char b[16]; snprintf(b,sizeof(b),"%d", g_spo2_min); lv_label_set_text(settings_label_spo2_min, b); }
  if (settings_label_spo2_max) { char b[16]; snprintf(b,sizeof(b),"%d", g_spo2_max); lv_label_set_text(settings_label_spo2_max, b); }
  if (settings_label_hr_min)   { char b[16]; snprintf(b,sizeof(b),"%d", g_hr_min); lv_label_set_text(settings_label_hr_min, b); }
  if (settings_label_hr_max)   { char b[16]; snprintf(b,sizeof(b),"%d", g_hr_max); lv_label_set_text(settings_label_hr_max, b); }
  if (settings_label_sys_min)  { char b[16]; snprintf(b,sizeof(b),"%d", g_sys_min); lv_label_set_text(settings_label_sys_min, b); }
  if (settings_label_sys_max)  { char b[16]; snprintf(b,sizeof(b),"%d", g_sys_max); lv_label_set_text(settings_label_sys_max, b); }
  if (settings_label_dia_min)  { char b[16]; snprintf(b,sizeof(b),"%d", g_dia_min); lv_label_set_text(settings_label_dia_min, b); }
  if (settings_label_dia_max)  { char b[16]; snprintf(b,sizeof(b),"%d", g_dia_max); lv_label_set_text(settings_label_dia_max, b); }
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
  // make the "Setting" label text black instead of the default (was white)
  lv_obj_set_style_text_color(menu_lbl, lv_color_make(0, 0, 0), 0);
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
