#include "GuestMode.h"
#include <Arduino.h>
#include <cstdio>
#include <ctype.h>
#include <lvgl.h>

#include "keypad.h"
#include <Preferences.h>
// Sensor and BP API
#include "HR_SPO2_BP.h"
#include "sim_module.h"
#include <DFRobotDFPlayerMini.h>

// Forward declare settings sync helper
static void apply_settings_to_hrspo2bp();

// External references from Main_Gui.ino
extern bool dfPlayerReady;
extern DFRobotDFPlayerMini dfPlayer;
extern double g_lastGpsLat;
extern double g_lastGpsLng;
extern bool g_hasGpsLocation;

static lv_obj_t *gm_scr = nullptr;
static lv_obj_t *label_spo2 = nullptr;
static lv_obj_t *label_hr   = nullptr;
static lv_obj_t *label_sys  = nullptr;
static lv_obj_t *label_dia  = nullptr;
static lv_obj_t *label_phone = nullptr;
static lv_obj_t *label_state = nullptr;
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
static lv_obj_t *gm_btn_start = nullptr;
static lv_obj_t *gm_btn_mode = nullptr;
static lv_obj_t *gm_btn_back = nullptr;

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

static GuestBackCallback g_back_callback = nullptr;
static bool g_active = false;
static uint32_t g_start_ms = 0;

// Mode: default runs continuous HR/SpO2. BP mode enables Start button to trigger BP measurement.
enum GuestModeType { GMODE_HR_SPO2 = 0, GMODE_BP = 1 };
static GuestModeType g_gm_mode = GMODE_HR_SPO2;
// When a BP result is available we display it for this many milliseconds before auto-returning
static const uint32_t k_gm_bp_result_display_ms = 15000;
static uint32_t g_gm_bp_result_ms = 0;
static bool g_gm_bp_result_displaying = false;
static lv_obj_t *g_prev_scr = nullptr;
// saved original previous screen for multi-step keypad flows
static lv_obj_t *g_saved_prev_scr = nullptr;

// current edit target (uses enum defined above)
static EditTarget g_current_edit = EDIT_NONE;

#include "FirebaseSync.h"

// NVS for persisting phone, threshold, and user ID
static Preferences userPref;
static bool userPrefReady = false;
static const char *USER_NVS_NS = "usercfg";   // use the same NVS namespace as UserDashboard
static const char *USER_NVS_KEY_PHONE = "phone";
static const char *USER_NVS_KEY_SPO2_MIN = "spo2_min";
static const char *USER_NVS_KEY_SPO2_MAX = "spo2_max";
static const char *USER_NVS_KEY_HR_MIN   = "hr_min";
static const char *USER_NVS_KEY_HR_MAX   = "hr_max";
static const char *USER_NVS_KEY_SYS_MIN  = "sys_min";
static const char *USER_NVS_KEY_SYS_MAX  = "sys_max";
static const char *USER_NVS_KEY_DIA_MIN  = "dia_min";
static const char *USER_NVS_KEY_DIA_MAX  = "dia_max";
static const char *USER_NVS_KEY_ID       = "current_user_id";

static void load_settings_from_nvs() {
  snprintf(g_phone, sizeof(g_phone), "%s", DEFAULT_SOS_PHONE);

  if (!userPrefReady) {
    if (userPref.begin(USER_NVS_NS, false)) {
      userPrefReady = true;
    } else {
      userPrefReady = false;
    }
  }

  if (userPrefReady) {
    String p = userPref.getString(USER_NVS_KEY_PHONE, "");
    if (p.length() > 0) {
      p.toCharArray(g_phone, sizeof(g_phone));
    }

    // restore stored user id so GuestMode can keep uploading to
    // patients/<id>/measurements after reboot without re-entering the ID
    String uid = userPref.getString(USER_NVS_KEY_ID, "");
    if (uid.length() > 0) {
      FirebaseSync_SetCurrentUserId(uid.c_str());
      Serial.print("[GuestMode] Restored user id from NVS: ");
      Serial.println(uid);
    } else {
      FirebaseSync_SetCurrentUserId(nullptr);
      Serial.println("[GuestMode] No stored user id in NVS");
    }

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

// Push local settings (thresholds + phone) to HR_SPO2_BP engine
static void apply_settings_to_hrspo2bp() {
  hrspo2bp_set_thresholds(g_spo2_min, g_spo2_max, g_hr_min, g_hr_max,
                          g_sys_min, g_sys_max, g_dia_min, g_dia_max);
  hrspo2bp_set_phone(g_phone);
}

// Preload settings (especially phone number) at boot so SOS works even before GuestMode UI is shown
void GuestMode_Init(void) {
  load_settings_from_nvs();
  apply_settings_to_hrspo2bp();
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
  // Sync to HR_SPO2_BP engine immediately so warning system uses the latest values
  apply_settings_to_hrspo2bp();
}

static const lv_font_t *pick_font_large() {
#if defined(LV_FONT_MONTSERRAT_24) && (LV_FONT_MONTSERRAT_24 == 1)
  return &lv_font_montserrat_24;
#else
  return &lv_font_montserrat_20;
#endif
}

// Pick an extra-large font when available (falls back to pick_font_large)
static const lv_font_t *pick_font_xlarge() {
#if defined(LV_FONT_MONTSERRAT_28) && (LV_FONT_MONTSERRAT_28 == 1)
  return &lv_font_montserrat_28;
#else
  return pick_font_large();
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

// Match the Dashboard Back button styling (light red background + red border)
static void style_back_button_like_dashboard(lv_obj_t *btn, lv_obj_t *lbl) {
  if (!btn) return;
  lv_obj_set_style_radius(btn, 14, 0);
  lv_obj_set_style_bg_color(btn, lv_color_make(255, 180, 180), 0);
  lv_obj_set_style_bg_color(btn, lv_color_make(255, 150, 150), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_make(200, 30, 30), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  if (lbl) {
    // keep text readable like on the dashboard button
    lv_obj_set_style_text_color(lbl, lv_color_make(10, 60, 90), 0);
  }
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
  } else if (gm_scr) {
    lv_scr_load(gm_scr);
  }
  g_saved_prev_scr = nullptr;
}



static void on_kp_next_phone(const char *text) {
  if (!text) text = "";
  // validate phone: require exactly 10 digits
  size_t len = strlen(text);
  if (len != 10) {
    // keep keypad open and show hint
    keypad_set_placeholder_text("So dien thoai phai du 10 so");
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    if (!isdigit((unsigned char)text[i])) {
      keypad_set_placeholder_text("So dien thoai phai la so");
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
    keypad_set_placeholder_text("Gia tri phai la so");
    return;
  }

  // Khong gioi han nguong: chi can nhap so hop le.
  switch (g_current_edit) {
    case EDIT_SPO2_MIN:
      g_spo2_min = v;
      update_settings_label_int(settings_label_spo2_min, g_spo2_min);
      break;
    case EDIT_SPO2_MAX:
      g_spo2_max = v;
      update_settings_label_int(settings_label_spo2_max, g_spo2_max);
      break;
    case EDIT_HR_MIN:
      g_hr_min = v;
      update_settings_label_int(settings_label_hr_min, g_hr_min);
      break;
    case EDIT_HR_MAX:
      g_hr_max = v;
      update_settings_label_int(settings_label_hr_max, g_hr_max);
      break;
    case EDIT_SYS_MIN:
      g_sys_min = v;
      update_settings_label_int(settings_label_sys_min, g_sys_min);
      break;
    case EDIT_SYS_MAX:
      g_sys_max = v;
      update_settings_label_int(settings_label_sys_max, g_sys_max);
      break;
    case EDIT_DIA_MIN:
      g_dia_min = v;
      update_settings_label_int(settings_label_dia_min, g_dia_min);
      break;
    case EDIT_DIA_MAX:
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
  // use white background like the content cards below
  lv_obj_set_style_bg_color(settings_scr, lv_color_white(), 0);
  lv_obj_set_style_pad_all(settings_scr, 12, 0);
  // prevent the settings screen from showing a scrollbar
  lv_obj_clear_flag(settings_scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *h = lv_obj_create(settings_scr);
  lv_obj_set_size(h, lv_pct(100), 56);
  // make header match card white background
  lv_obj_set_style_bg_color(h, lv_color_white(), 0);
  lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *t = lv_label_create(h);
  lv_label_set_text(t, "Settings");
  lv_obj_set_style_text_font(t, pick_font_large(), 0);
  lv_obj_set_style_text_color(t, lv_color_black(), 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);

  // back button
  lv_obj_t *bback = lv_btn_create(h);
  lv_obj_set_size(bback, 92, 42);
  lv_obj_align(bback, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_t *bbt = lv_label_create(bback);
  lv_label_set_text(bbt, "Back");
  lv_obj_set_style_text_font(bbt, pick_font_large(), 0);
  // style Back like the dashboard (light red background + red border)
  style_back_button_like_dashboard(bback, bbt);
  lv_obj_center(bbt);
  lv_obj_add_event_cb(bback, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) { if (gm_scr) lv_scr_load(gm_scr); } }, LV_EVENT_ALL, nullptr);

  // content
  lv_obj_t *cont = lv_obj_create(settings_scr);
  lv_obj_set_size(cont, lv_pct(100), 240);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_set_style_pad_all(cont, 8, 0);
  lv_obj_set_style_bg_color(cont, lv_color_white(), 0);
  // disable scrolling/scrollbar on the content container
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  // Values (phone + ranges) are right-aligned in a column just left of the Edit buttons
  // to prevent overlapping with the larger left-side labels.
  const lv_coord_t kEditBtnW = 110;
  const lv_coord_t kValueRightOffset = -(kEditBtnW + 16);

  // Phone row: label on left, value next to it, edit button aligned to top-right
  lv_obj_t *lblp = lv_label_create(cont);
  lv_label_set_text(lblp, "Phone:");
  lv_obj_set_style_text_font(lblp, pick_font_large(), 0);
  lv_obj_set_style_text_color(lblp, lv_color_black(), 0);
  // nudge the Phone label slightly higher
  // move only the Phone text up a bit without changing other elements
  lv_obj_align(lblp, LV_ALIGN_TOP_LEFT, 8, -4);

  // place phone value to the right of the "Phone:" label on the same line
  settings_label_phone = lv_label_create(cont);
  lv_label_set_text(settings_label_phone, g_phone[0] ? g_phone : "(none)");
  lv_obj_set_style_text_font(settings_label_phone, pick_font_large(), 0);
  lv_obj_set_style_text_color(settings_label_phone, lv_color_black(), 0);
  lv_obj_set_style_text_align(settings_label_phone, LV_TEXT_ALIGN_RIGHT, 0);
  // Right-align the phone value into the value column so it sits left of the Edit buttons
  lv_obj_align(settings_label_phone, LV_ALIGN_TOP_RIGHT, kValueRightOffset, 0);

  // edit button stays to the right of the phone value and vertically centered
  lv_obj_t *ep = lv_btn_create(cont);
  // Make size match other Edit buttons and slightly reduced height
  lv_obj_set_size(ep, 90, 35);
  // place phone Edit button at the right edge of the content area
  lv_obj_align(ep, LV_ALIGN_TOP_RIGHT, -8, -4);
  lv_obj_add_event_cb(ep, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) open_keypad_for_phone(); }, LV_EVENT_ALL, nullptr);
  lv_obj_t *ep_l = lv_label_create(ep);
  lv_label_set_text(ep_l, "Edit");
  lv_obj_set_style_text_font(ep_l, pick_font_large(), 0);
  lv_obj_set_style_text_color(ep_l, lv_color_black(), 0);
  lv_obj_center(ep_l);
  // four main metric rows: each shows "min - max" summary and Edit button to open metric screen
  int row_y = 54; // raise metric rows closer to Phone row by 8px
  auto make_metric_row = [&](const char *name, lv_obj_t **summary_lbl, lv_event_cb_t cb) {
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, pick_font_large(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    // vertical placement: move only the left-side metric label slightly up
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 8, row_y - 8);

    *summary_lbl = lv_label_create(cont);
    lv_label_set_text(*summary_lbl, "-- - --");
    lv_obj_set_style_text_font(*summary_lbl, pick_font_large(), 0);
    lv_obj_set_style_text_color(*summary_lbl, lv_color_black(), 0);
    lv_obj_set_style_text_align(*summary_lbl, LV_TEXT_ALIGN_RIGHT, 0);
    // Right-align the summary into the value column so all values line up
    lv_obj_align(*summary_lbl, LV_ALIGN_TOP_RIGHT, kValueRightOffset, row_y - 8);

    lv_obj_t *btn = lv_btn_create(cont);
    // make Edit buttons match the Phone edit button size/style (slightly reduced height)
    lv_obj_set_size(btn, 90, 35);
    // place the Edit button at the right edge of the container (consistent column)
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -8, row_y - 12);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lblb = lv_label_create(btn);
    lv_label_set_text(lblb, "Edit");
    lv_obj_set_style_text_font(lblb, pick_font_large(), 0);
    lv_obj_set_style_text_color(lblb, lv_color_black(), 0);
    lv_obj_center(lblb);

    row_y += 48;
  };

  make_metric_row("SPO2:", &settings_label_spo2_summary, [](lv_event_t *e){ build_metric_screen(); if (spo2_scr) lv_scr_load(spo2_scr); });
  make_metric_row("Heart Rate:", &settings_label_hr_summary, [](lv_event_t *e){ build_metric_screen(); if (hr_scr) lv_scr_load(hr_scr); });
  make_metric_row("Systolic:", &settings_label_sys_summary, [](lv_event_t *e){ build_metric_screen(); if (sys_scr) lv_scr_load(sys_scr); });
  make_metric_row("Diastolic:", &settings_label_dia_summary, [](lv_event_t *e){ build_metric_screen(); if (dia_scr) lv_scr_load(dia_scr); });

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
    lv_obj_set_style_text_font(t, pick_font_large(), 0);
    lv_obj_set_style_text_color(t, lv_color_black(), 0);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, -8);

    lv_obj_t *bback = lv_btn_create(h);
    lv_obj_set_size(bback, 92, 42);
    lv_obj_align(bback, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *bbt = lv_label_create(bback);
    lv_label_set_text(bbt, "Back");
    lv_obj_set_style_text_font(bbt, pick_font_large(), 0);
    // apply dashboard-like red Back style
    style_back_button_like_dashboard(bback, bbt);
    lv_obj_center(bbt);
    lv_obj_add_event_cb(bback, [](lv_event_t *ev){ if (lv_event_get_code(ev)==LV_EVENT_CLICKED) { if (settings_scr) lv_scr_load(settings_scr); } }, LV_EVENT_ALL, nullptr);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, lv_pct(100), 200);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_pad_all(cont, 8, 0);
    lv_obj_set_style_bg_color(cont, lv_color_white(), 0);

    // Min row
    lv_obj_t *lbl_min = lv_label_create(cont);
    lv_label_set_text(lbl_min, "Min:");
    lv_obj_set_style_text_font(lbl_min, pick_font_large(), 0);
    lv_obj_set_style_text_color(lbl_min, lv_color_black(), 0);
    lv_obj_align(lbl_min, LV_ALIGN_TOP_LEFT, 8, 8);

    *lbl_min_out = lv_label_create(cont);
    lv_obj_set_style_text_font(*lbl_min_out, pick_font_large(), 0);
    lv_obj_set_style_text_color(*lbl_min_out, lv_color_black(), 0);
    lv_obj_align(*lbl_min_out, LV_ALIGN_TOP_LEFT, 120, 8);

    lv_obj_t *btn_min = lv_btn_create(cont);
    lv_obj_set_size(btn_min, 100, 34);
    lv_obj_align(btn_min, LV_ALIGN_TOP_RIGHT, -8, 4);
    // allocate small struct to keep target and placeholder alive
    MetricEditData *dmin = new MetricEditData();
    dmin->target = min_tgt;
    dmin->placeholder = "Nhap gia tri nho nhat";
    lv_obj_add_event_cb(btn_min, metric_edit_event_cb, LV_EVENT_CLICKED, dmin);
    lv_obj_t *lminb = lv_label_create(btn_min);
    lv_label_set_text(lminb, "Edit");
    lv_obj_set_style_text_font(lminb, pick_font_large(), 0);
    lv_obj_set_style_text_color(lminb, lv_color_black(), 0);
    lv_obj_center(lminb);

    // Max row
    lv_obj_t *lbl_max = lv_label_create(cont);
    lv_label_set_text(lbl_max, "Max:");
    lv_obj_set_style_text_font(lbl_max, pick_font_large(), 0);
    lv_obj_set_style_text_color(lbl_max, lv_color_black(), 0);
    lv_obj_align(lbl_max, LV_ALIGN_TOP_LEFT, 8, 56);

    *lbl_max_out = lv_label_create(cont);
    lv_obj_set_style_text_font(*lbl_max_out, pick_font_large(), 0);
    lv_obj_set_style_text_color(*lbl_max_out, lv_color_black(), 0);
    lv_obj_align(*lbl_max_out, LV_ALIGN_TOP_LEFT, 120, 56);

    lv_obj_t *btn_max = lv_btn_create(cont);
    lv_obj_set_size(btn_max, 100, 34);
    lv_obj_align(btn_max, LV_ALIGN_TOP_RIGHT, -8, 52);
    MetricEditData *dmax = new MetricEditData();
    dmax->target = max_tgt;
    dmax->placeholder = "Nhap gia tri lon nhat";
    lv_obj_add_event_cb(btn_max, metric_edit_event_cb, LV_EVENT_CLICKED, dmax);
    lv_obj_t *lmaxb = lv_label_create(btn_max);
    lv_label_set_text(lmaxb, "Edit");
    lv_obj_set_style_text_font(lmaxb, pick_font_large(), 0);
    lv_obj_set_style_text_color(lmaxb, lv_color_black(), 0);
    lv_obj_center(lmaxb);

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

static void build_gm_screen() {
  // load persisted values once before building UI
  load_settings_from_nvs();
  if (gm_scr) return;

  gm_scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(gm_scr, lv_color_make(245, 252, 255), 0);
  lv_obj_clear_flag(gm_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(gm_scr, 12, 0);

  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark    = lv_color_make(10, 60, 90);
  lv_color_t card     = lv_color_white();

  lv_obj_t *header = lv_obj_create(gm_scr);
  lv_obj_set_size(header, lv_pct(100), 56);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(header);
  lv_label_set_text(title, "Health Guardian");
  lv_obj_set_style_text_color(title, primary, 0);
  // use larger title font like GuestMode
  lv_obj_set_style_text_font(title, pick_font_large(), 0);
  lv_obj_align(title, LV_ALIGN_LEFT_MID, -15, -10);

  // transient status label (e.g. "Nhan nut Start de do huyet ap...", "Dang do huyet ap...")
  label_state = lv_label_create(header);
  lv_label_set_text(label_state, "");
  lv_obj_set_style_text_color(label_state, lv_color_make(220, 40, 40), 0);
  lv_obj_set_style_text_font(label_state, pick_font_small(), 0);
  lv_obj_align(label_state, LV_ALIGN_LEFT_MID, -15, 14);

  lv_obj_t *btn_back = lv_btn_create(header);
  lv_obj_set_size(btn_back, 92, 42);
  lv_obj_align(btn_back, LV_ALIGN_RIGHT_MID, 10, 0);
  lv_obj_set_style_radius(btn_back, 14, 0);
  // set background to a stronger light red and make the border red as well
  lv_obj_set_style_bg_color(btn_back, lv_color_make(255, 180, 180), 0);
  lv_obj_set_style_bg_color(btn_back, lv_color_make(255, 150, 150), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_back, 2, 0);
  lv_obj_set_style_border_color(btn_back, lv_color_make(200, 30, 30), 0);
  lv_obj_set_style_shadow_width(btn_back, 0, 0);
  lv_obj_add_event_cb(btn_back, back_btn_event_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *btn_back_label = lv_label_create(btn_back);
  lv_label_set_text(btn_back_label, "Back");
  lv_obj_set_style_text_color(btn_back_label, dark, 0);
  // make Back label larger to match other header buttons
  lv_obj_set_style_text_font(btn_back_label, pick_font_large(), 0);
  lv_obj_center(btn_back_label);
  // keep a reference to this back button so we can disable it during BP measurement
  gm_btn_back = btn_back;

  // Metrics area (two stacked cards per column like GuestMode)
  lv_obj_t *metrics = lv_obj_create(gm_scr);
  lv_obj_set_size(metrics, lv_pct(100), 176);
  // move metrics slightly up to match GuestMode
  lv_obj_align(metrics, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(metrics, 0, 0);
  lv_obj_set_style_pad_all(metrics, 0, 0);
  lv_obj_set_style_pad_column(metrics, 10, 0);
  lv_obj_set_style_pad_row(metrics, 10, 0);
  lv_obj_clear_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(metrics, col, row);

  lv_obj_t *left_col = lv_obj_create(metrics);
  lv_obj_set_grid_cell(left_col, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_flex_flow(left_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(left_col, 0, 0);
  lv_obj_set_style_pad_row(left_col, 10, 0);
  lv_obj_clear_flag(left_col, LV_OBJ_FLAG_SCROLLABLE);
  // remove any visible border for the left frame and match GuestMode styling
  lv_obj_set_style_border_width(left_col, 0, 0);
  lv_obj_set_style_border_color(left_col, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_radius(left_col, 12, 0);

  lv_obj_t *right_col = lv_obj_create(metrics);
  lv_obj_set_grid_cell(right_col, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_flex_flow(right_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(right_col, 0, 0);
  lv_obj_set_style_pad_row(right_col, 10, 0);
  lv_obj_clear_flag(right_col, LV_OBJ_FLAG_SCROLLABLE);
  // remove any visible border for the right frame and match GuestMode styling
  lv_obj_set_style_border_width(right_col, 0, 0);
  lv_obj_set_style_border_color(right_col, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_radius(right_col, 12, 0);

  auto make_card = [&](lv_obj_t *parent, const char *title_text, const char *unit_text, lv_color_t title_color, lv_obj_t **value_out) {
    lv_obj_t *card_obj = lv_obj_create(parent);
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
    // use larger title font like GuestMode
    lv_obj_set_style_text_font(ttl, pick_font_large(), 0);
    // match GuestMode: move only the card title slightly (smaller negative offset)
    lv_obj_align(ttl, LV_ALIGN_TOP_LEFT, 0, -6);

    lv_obj_t *val = lv_label_create(card_obj);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val, lv_color_make(15, 75, 110), 0);
    lv_obj_set_style_text_font(val, pick_font_large(), 0);
    // slight vertical tweak for value label
    lv_obj_align(val, LV_ALIGN_LEFT_MID, 0, 3);

    lv_obj_t *unit = lv_label_create(card_obj);
    lv_label_set_text(unit, unit_text);
    lv_obj_set_style_text_color(unit, lv_color_make(90, 120, 140), 0);
    // use mid font for unit similar to GuestMode
    lv_obj_set_style_text_font(unit, pick_font_mid(), 0);
    lv_obj_align(unit, LV_ALIGN_BOTTOM_LEFT, 0, 6);

    if (value_out) *value_out = val;
  };

  // Left column: Heart Rate (top), SPO2 (bottom)
  make_card(left_col, "Heart Rate:", "bpm", lv_color_make(220, 40, 40), &label_hr);
  make_card(left_col, "SPO2:", "%", lv_color_make(0, 140, 200), &label_spo2);

  // Right column: Systolic (top), Diastolic (bottom)
  make_card(right_col, "Systolic:", "mmHg", lv_color_make(0, 160, 110), &label_sys);
  make_card(right_col, "Diastolic:", "mmHg", lv_color_make(140, 90, 210), &label_dia);

  // Do not show any sensor values in dashboard until sensors are added.
  if (label_spo2) lv_label_set_text(label_spo2, "");
  if (label_hr)   lv_label_set_text(label_hr, "");
  if (label_sys)  lv_label_set_text(label_sys, "");
  if (label_dia)  lv_label_set_text(label_dia, "");

  // Create Setting button in header (moved to where Start used to be)
  {
    lv_obj_t *btn_setting_hdr = lv_btn_create(header);
    // make Setting button a bit wider
    lv_obj_set_size(btn_setting_hdr, 110, 42);
    // place to the left of Back (adjust offset for wider button)
    lv_obj_align(btn_setting_hdr, LV_ALIGN_RIGHT_MID, -90, 0);
    lv_obj_set_style_radius(btn_setting_hdr, 14, 0);
    lv_obj_set_style_bg_color(btn_setting_hdr, lv_color_make(240, 240, 240), 0);
    lv_obj_set_style_bg_color(btn_setting_hdr, lv_color_make(220, 220, 220), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_setting_hdr, 2, 0);
    lv_obj_set_style_border_color(btn_setting_hdr, lv_color_make(200,200,200), 0);
    lv_obj_set_style_shadow_width(btn_setting_hdr, 0, 0);
    lv_obj_add_event_cb(btn_setting_hdr, [](lv_event_t *e){ if (lv_event_get_code(e)==LV_EVENT_CLICKED) {
      build_settings_screen();
      if (settings_scr) lv_scr_load(settings_scr);
    } }, LV_EVENT_ALL, nullptr);
    lv_obj_t *hdr_lbl = lv_label_create(btn_setting_hdr);
    lv_label_set_text(hdr_lbl, "Setting");
    lv_obj_set_style_text_font(hdr_lbl, pick_font_large(), 0);
    lv_obj_set_style_text_color(hdr_lbl, lv_color_make(0,0,0), 0);
    lv_obj_center(hdr_lbl);
  }

  // Footer with Start button (same visual as GuestMode Start)
  lv_obj_t *footer = lv_obj_create(gm_scr);
  lv_obj_set_size(footer, lv_pct(100), 48);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  // ensure footer has no visible border (remove long rounded border)
  lv_obj_set_style_border_width(footer, 0, 0);
  lv_obj_set_style_border_color(footer, lv_color_make(0,0,0), 0);
  lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

  // Create Mode and Start buttons in footer area (Start moved to footer like GuestMode)
  // Mode button: to the left of Start
  if (!gm_btn_start) {
    // Mode button (created as child of gm_scr so it sits above footer)
    lv_obj_t *btn_mode = lv_btn_create(gm_scr);
    lv_obj_set_size(btn_mode, 92, 42);
    // place to the left of Start (approx one button width + spacing)
    lv_obj_align(btn_mode, LV_ALIGN_BOTTOM_RIGHT, -108, -6);
    lv_obj_set_style_radius(btn_mode, 14, 0);
    lv_obj_set_style_bg_color(btn_mode, lv_color_make(220, 240, 255), 0);
    lv_obj_set_style_bg_color(btn_mode, lv_color_make(190, 215, 255), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn_mode, 2, 0);
    lv_obj_set_style_border_color(btn_mode, lv_color_make(60, 110, 180), 0);
    lv_obj_set_style_shadow_width(btn_mode, 0, 0);
    lv_obj_t *mbl = lv_label_create(btn_mode);
    lv_label_set_text(mbl, "Mode");
    lv_obj_set_style_text_color(mbl, lv_color_make(0, 40, 80), 0);
    // use larger font like GuestMode
    lv_obj_set_style_text_font(mbl, pick_font_large(), 0);
    lv_obj_center(mbl);
    // keep a reference to the Mode button so we can disable it during BP measurement
    gm_btn_mode = btn_mode;
    // Mode button: Toggle between HR/SpO2 mode and BP mode. During measurement, it cancels.
    lv_obj_add_event_cb(btn_mode, [](lv_event_t *e){
      if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
      
      if (isBPMeasuring()) {
        cancelMeasureBloodPressure();
        g_gm_mode = GMODE_HR_SPO2;
        g_gm_bp_result_displaying = false;
        if (label_state) lv_label_set_text(label_state, "Da huy do huyet ap");
        if (gm_btn_start) lv_obj_add_state(gm_btn_start, LV_STATE_DISABLED);
        if (gm_btn_back) lv_obj_clear_state(gm_btn_back, LV_STATE_DISABLED);
      } else {
        // Toggle between GMODE_HR_SPO2 and GMODE_BP
        if (g_gm_mode == GMODE_HR_SPO2) {
          g_gm_mode = GMODE_BP;
          g_gm_bp_result_displaying = false;
          // clear any previous transient state
          if (label_state) lv_label_set_text(label_state, "Nhan Start de do huyet ap...");
          // clear previous SYS/DIA so user knows results will be new
          if (label_sys) lv_label_set_text(label_sys, "--");
          if (label_dia) lv_label_set_text(label_dia, "--");
          if (gm_btn_start) lv_obj_clear_state(gm_btn_start, LV_STATE_DISABLED);
        } else {
          g_gm_mode = GMODE_HR_SPO2;
          if (label_state) lv_label_set_text(label_state, "");
          if (gm_btn_start) lv_obj_add_state(gm_btn_start, LV_STATE_DISABLED);
        }
      }
    }, LV_EVENT_CLICKED, nullptr);

    // Create Start button and position it at the bottom-right corner
    lv_obj_t *btn_start = lv_btn_create(gm_scr);
    lv_obj_set_size(btn_start, 92, 42);
    // place in the bottom-right corner with a small inset
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
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
      // show measuring status
      if (label_state) lv_label_set_text(label_state, "Dang do huyet ap...");
      // disable the button to prevent re-entry (use global gm_btn_start)
      if (gm_btn_start) lv_obj_add_state(gm_btn_start, LV_STATE_DISABLED);
      // disable Back while measurement is in progress
      if (gm_btn_back) lv_obj_add_state(gm_btn_back, LV_STATE_DISABLED);
      // Mode button is intentionally left ENABLED during measurement to allow cancellation
      // trigger non-blocking background measurement (mark origin=GUEST)
      startMeasureBloodPressureAsyncForOrigin(BP_ORIGIN_GUEST);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lb = lv_label_create(btn_start);
    lv_label_set_text(lb, "Start");
    lv_obj_set_style_text_color(lb, lv_color_make(0, 40, 20), 0);
    lv_obj_set_style_text_font(lb, pick_font_large(), 0);
    lv_obj_center(lb);

    // update global gm_btn_start to point to the footer Start button
    gm_btn_start = btn_start;
    // Start is disabled by default (only active when user switches to BP mode)
    lv_obj_add_state(gm_btn_start, LV_STATE_DISABLED);
  }

}

static void refresh_values() {
  if (!gm_scr) return;
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
  // SYS/DIA
  if (label_sys) {
    if (lastBPOrigin == BP_ORIGIN_GUEST && isfinite(lastSYS) && lastSYS > 0.0f) snprintf(buf, sizeof(buf), "%.1f", lastSYS);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(label_sys, buf);
  }
  if (label_dia) {
    if (lastBPOrigin == BP_ORIGIN_GUEST && isfinite(lastDIA) && lastDIA > 0.0f) snprintf(buf, sizeof(buf), "%.1f", lastDIA);
    else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(label_dia, buf);
  }

  
}

void GuestMode_Show(GuestBackCallback backCallback) {
  g_back_callback = backCallback;
  build_gm_screen();
  g_active = true;
  g_start_ms = millis();
  if (gm_scr) {
    // Keep metric displays empty until real sensors are hooked up
    if (label_spo2) lv_label_set_text(label_spo2, "");
    if (label_hr)   lv_label_set_text(label_hr, "");
    if (label_sys)  lv_label_set_text(label_sys, "");
    if (label_dia)  lv_label_set_text(label_dia, "");
    // Sync local settings to HR_SPO2_BP engine before starting measurements
    apply_settings_to_hrspo2bp();
    lv_scr_load(gm_scr);
    // initialize to HR/SPO2 mode and ensure Start is disabled by default
    g_gm_mode = GMODE_HR_SPO2;
    g_gm_bp_result_displaying = false;
    if (gm_btn_start) lv_obj_add_state(gm_btn_start, LV_STATE_DISABLED);
    // ensure Mode button is enabled on show
    if (gm_btn_mode) lv_obj_clear_state(gm_btn_mode, LV_STATE_DISABLED);
    // ensure Back is enabled
    if (gm_btn_back) lv_obj_clear_state(gm_btn_back, LV_STATE_DISABLED);
    // clear transient status
    if (label_state) lv_label_set_text(label_state, "");
    // reset warning state so alerts can re-trigger in this session
    g_hr_warning = 0;
    g_spo2_warning = 0;
    g_mode1_warning = 0;
    g_mode2_warning = 0;
    g_warning_last_inc_ms = 0;
  }
}

void GuestMode_Loop() {
  if (!g_active || !gm_scr) return;

  static bool wasMeasuring = false;

  uint32_t elapsed = millis() - g_start_ms;
  if (elapsed < 2500) {
    return;
  }

  bool measuring = isBPMeasuring();

  // As soon as calculations are done (lastBPOrigin is updated to GUEST),
  // clear the measuring label and show the results immediately, even while deflating.
  if (measuring && lastBPOrigin == BP_ORIGIN_GUEST) {
    if (label_state) lv_label_set_text(label_state, "");
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
  }

  if (measuring && !wasMeasuring) {
    wasMeasuring = true;
  } else if (!measuring && wasMeasuring) {
    wasMeasuring = false;
    
    if (lastBPOrigin == BP_ORIGIN_GUEST) {
      // update SYS/DIA labels (backup/ensure updated)
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
      if (label_state) lv_label_set_text(label_state, "");
      // mark time when result became available; we'll display it for a while then auto-return to HR/SPO2 mode
      g_gm_bp_result_ms = millis();
      g_gm_bp_result_displaying = true;
    }
    
    // measurement finished -> allow Back and Mode buttons now that result is shown or cancelled
    if (gm_btn_back) lv_obj_clear_state(gm_btn_back, LV_STATE_DISABLED);
    if (gm_btn_mode) lv_obj_clear_state(gm_btn_mode, LV_STATE_DISABLED);
  }

  // If a BP result is being displayed, check whether the display interval elapsed
  if (g_gm_bp_result_displaying) {
    if ((millis() - g_gm_bp_result_ms) >= k_gm_bp_result_display_ms) {
      // clear transient state text and return to HR/SPO2 mode
      if (label_state) lv_label_set_text(label_state, "");
      g_gm_bp_result_displaying = false;
      g_gm_mode = GMODE_HR_SPO2;
      // ensure Start is disabled again (only active in BP mode)
      if (gm_btn_start) lv_obj_add_state(gm_btn_start, LV_STATE_DISABLED);
    }
  }

  // Check for health warnings (HR/SpO2/BP) and trigger alerts if needed
  hrspo2bp_warning_check();

  refresh_values();
}

const char* GuestMode_GetPhone() {
  return g_phone;
}

void GuestMode_UpdateStatus(const char *time_str, const char *batt_str) {
  (void)time_str;
  (void)batt_str;
}
