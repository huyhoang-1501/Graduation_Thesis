#include "MainUi.h"

#include <cstdio>

#include "GuestMode.h"
#include "keypad.h"
#include "monitoring_icon.h"
#include "wifi_icon.h"

static MainUiSaveUserIdCb g_save_user_id_cb = nullptr;
static MainUiGetTextCb g_get_device_id_cb = nullptr;
static MainUiGetTextCb g_get_user_id_cb = nullptr;

static lv_obj_t *main_scr = nullptr;
static lv_obj_t *main_label_time = nullptr;
static lv_obj_t *main_label_batt = nullptr;
static lv_obj_t *label_device_id = nullptr;
static lv_obj_t *label_user_id = nullptr;

static const lv_font_t* pick_font_36_or_14() {
#if defined(LV_FONT_MONTSERRAT_36) && (LV_FONT_MONTSERRAT_36 == 1)
  return &lv_font_montserrat_40;
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

static const lv_font_t* pick_font_16_or_14() {
#if defined(LV_FONT_MONTSERRAT_16) && (LV_FONT_MONTSERRAT_16 == 1)
  return &lv_font_montserrat_16;
#else
  return &lv_font_montserrat_14;
#endif
}

static const lv_font_t* pick_font_14() {
  return &lv_font_montserrat_14;
}

static void show_main_screen();

static void refresh_device_id_label() {
  if (!label_device_id) return;

  const char *deviceId = g_get_device_id_cb ? g_get_device_id_cb() : "--";
  char buf[48];
  snprintf(buf, sizeof(buf), "Device ID: %s", deviceId && deviceId[0] ? deviceId : "--");
  lv_label_set_text(label_device_id, buf);
}

static void refresh_user_id_label() {
  if (!label_user_id) return;

  const char *userId = g_get_user_id_cb ? g_get_user_id_cb() : "";
  char buf[64];
  if (userId && userId[0]) {
    snprintf(buf, sizeof(buf), "User ID: %s", userId);
  } else {
    snprintf(buf, sizeof(buf), "User ID: (chua nhap)");
  }
  lv_label_set_text(label_user_id, buf);
}

static void on_keypad_back() {
  show_main_screen();
}

static void on_keypad_next(const char *text) {
  const char *value = text ? text : "";
  if (!value[0]) return;

  if (g_save_user_id_cb) {
    g_save_user_id_cb(value);
  }

  refresh_user_id_label();
  show_main_screen();
}

static void show_user_mode_screen() {
  static bool keypad_inited = false;
  if (!keypad_inited) {
    keypad_init_screen(pick_font_20_or_14(),
                       pick_font_16_or_14(),
                       on_keypad_back,
                       on_keypad_next);
    keypad_inited = true;
  }

  keypad_set_placeholder_text("Nhap ID da tao tren web...");
  keypad_set_text("");

  lv_obj_t *scr = keypad_get_screen();
  if (scr) lv_scr_load(scr);
}

static void guest_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  GuestMode_Show(show_main_screen);
}

static void user_btn_event_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
  show_user_mode_screen();
}

static void show_main_screen() {
  if (main_scr) lv_scr_load(main_scr);
}

static lv_obj_t* create_status_bar(lv_obj_t *parent,
                                   lv_color_t primary,
                                   lv_color_t dark,
                                   lv_color_t card,
                                   lv_obj_t **out_time,
                                   lv_obj_t **out_batt) {
  lv_obj_t *status = lv_obj_create(parent);
  lv_obj_set_size(status, lv_pct(98), 44);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, -4);
  lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_bg_color(status, card, 0);
  lv_obj_set_style_radius(status, 14, 0);
  lv_obj_set_style_border_width(status, 2, 0);
  lv_obj_set_style_border_color(status, lv_color_make(200, 235, 250), 0);
  lv_obj_set_style_pad_left(status, 12, 0);
  lv_obj_set_style_pad_right(status, 12, 0);
  lv_obj_set_style_shadow_width(status, 0, 0);

  lv_obj_t *hcmute_box = lv_obj_create(status);
  lv_obj_set_size(hcmute_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_align(hcmute_box, LV_ALIGN_LEFT_MID, -5, 0);

  lv_obj_set_style_bg_opa(hcmute_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(hcmute_box, 2, 0);
  lv_obj_set_style_border_color(hcmute_box, lv_color_make(220, 40, 40), 0);
  lv_obj_set_style_radius(hcmute_box, 10, 0);
  lv_obj_set_style_pad_left(hcmute_box, 10, 0);
  lv_obj_set_style_pad_right(hcmute_box, 10, 0);
  lv_obj_set_style_pad_top(hcmute_box, 5, 0);
  lv_obj_set_style_pad_bottom(hcmute_box, 5, 0);

  lv_obj_t *label_hcmute = lv_label_create(hcmute_box);
  lv_label_set_text(label_hcmute, "HCM-UTE");
  lv_obj_set_style_text_color(label_hcmute, primary, 0);
  lv_obj_set_style_text_font(label_hcmute, pick_font_18_or_14(), 0);
  lv_obj_center(label_hcmute);

  lv_obj_t *batt = lv_label_create(status);
  lv_label_set_text(batt, "--%");
  lv_obj_set_style_text_color(batt, dark, 0);
  lv_obj_set_style_text_font(batt, pick_font_16_or_14(), 0);
  lv_obj_align(batt, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t *img_wifi = lv_img_create(status);
  lv_img_set_src(img_wifi, &wifi_icon);
  lv_obj_align_to(img_wifi, batt, LV_ALIGN_OUT_LEFT_MID, -20, 0);
  lv_obj_set_style_img_recolor(img_wifi, lv_color_make(0, 0, 0), LV_PART_MAIN);
  lv_obj_set_style_img_recolor_opa(img_wifi, 255, LV_PART_MAIN);
  lv_obj_set_style_opa(img_wifi, 255, LV_PART_MAIN);

  lv_obj_t *time_lbl = lv_label_create(status);
  lv_label_set_text(time_lbl, "--:--  --/--/----");
  lv_obj_set_style_text_color(time_lbl, dark, 0);
  lv_obj_set_style_text_font(time_lbl, pick_font_16_or_14(), 0);
  lv_obj_align(time_lbl, LV_ALIGN_RIGHT_MID, -70, 0);

  if (out_time) *out_time = time_lbl;
  if (out_batt) *out_batt = batt;
  return status;
}

static void create_main_gui() {
  main_scr = lv_obj_create(NULL);
  lv_obj_t *scr = main_scr;

  lv_color_t bg = lv_color_make(245, 252, 255);
  lv_color_t primary = lv_color_make(0, 140, 200);
  lv_color_t dark = lv_color_make(10, 60, 90);
  lv_color_t card = lv_color_white();

  lv_obj_set_style_bg_color(scr, bg, 0);
  lv_obj_set_style_pad_all(scr, 12, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_status_bar(scr, primary, dark, card, &main_label_time, &main_label_batt);

  lv_obj_t *frame = lv_obj_create(scr);
  lv_obj_set_size(frame, lv_pct(100), 184);
  lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 44);
  lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(frame, 3, 0);
  lv_obj_set_style_border_color(frame, primary, 0);
  lv_obj_set_style_radius(frame, 20, 0);
  lv_obj_set_style_pad_all(frame, 10, 0);
  lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *img_monitor = lv_img_create(frame);
  lv_img_set_src(img_monitor, &monitoring_icon);
  lv_obj_align(img_monitor, LV_ALIGN_RIGHT_MID, -5, 0);

  lv_obj_t *text_cont = lv_obj_create(frame);
  lv_obj_set_size(text_cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(text_cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(text_cont, 0, 0);
  lv_obj_align(text_cont, LV_ALIGN_LEFT_MID, -10, 0);

  lv_obj_t *label_health = lv_label_create(text_cont);
  lv_label_set_text(label_health, "Health");
  lv_obj_set_style_text_color(label_health, primary, 0);
  lv_obj_set_style_text_font(label_health, pick_font_36_or_14(), 0);
  lv_obj_align(label_health, LV_ALIGN_TOP_LEFT, 0, 2);

  lv_obj_t *label_monitor = lv_label_create(text_cont);
  lv_label_set_text(label_monitor, "Monitoring");
  lv_obj_set_style_text_color(label_monitor, primary, 0);
  lv_obj_set_style_text_font(label_monitor, pick_font_36_or_14(), 0);
  lv_obj_align_to(label_monitor, label_health, LV_ALIGN_OUT_BOTTOM_LEFT, 0, -4);

  label_device_id = lv_label_create(text_cont);
  lv_label_set_text(label_device_id, "Device ID: --");
  lv_obj_set_style_text_color(label_device_id, dark, 0);
  lv_obj_set_style_text_font(label_device_id, pick_font_16_or_14(), 0);
  lv_obj_align_to(label_device_id, label_monitor, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

  label_user_id = lv_label_create(text_cont);
  lv_label_set_text(label_user_id, "User ID: (chua nhap)");
  lv_obj_set_style_text_color(label_user_id, dark, 0);
  lv_obj_set_style_text_font(label_user_id, pick_font_16_or_14(), 0);
  lv_obj_align_to(label_user_id, label_device_id, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lv_obj_t *label_mode_hint = lv_label_create(text_cont);
  lv_label_set_text(label_mode_hint, "Guest: do tai cho | User: nhap ID de dong bo web");
  lv_label_set_long_mode(label_mode_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label_mode_hint, 230);
  lv_obj_set_style_text_color(label_mode_hint, lv_color_make(70, 110, 130), 0);
  lv_obj_set_style_text_font(label_mode_hint, pick_font_14(), 0);
  lv_obj_align_to(label_mode_hint, label_user_id, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);

  lv_obj_t *cont_btn = lv_obj_create(scr);
  lv_obj_set_size(cont_btn, lv_pct(100), 74);
  lv_obj_align(cont_btn, LV_ALIGN_BOTTOM_MID, 0, 8);
  lv_obj_set_style_bg_opa(cont_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_btn, 0, 0);
  lv_obj_set_style_pad_left(cont_btn, 0, 0);
  lv_obj_set_style_pad_right(cont_btn, 0, 0);
  lv_obj_set_style_pad_top(cont_btn, 0, 0);
  lv_obj_set_style_pad_bottom(cont_btn, 0, 0);
  lv_obj_set_style_pad_column(cont_btn, 10, 0);
  lv_obj_clear_flag(cont_btn, LV_OBJ_FLAG_SCROLLABLE);

  static lv_coord_t col[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static lv_coord_t row[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(cont_btn, col, row);

  lv_obj_t *btn_guest = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_guest, LV_GRID_ALIGN_STRETCH, 0, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_guest, 14, 0);
  lv_obj_set_style_bg_color(btn_guest, card, 0);
  lv_obj_set_style_bg_color(btn_guest, lv_color_make(210, 245, 255), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_guest, 2, 0);
  lv_obj_set_style_border_color(btn_guest, primary, 0);
  lv_obj_set_style_shadow_width(btn_guest, 0, 0);
  lv_obj_set_style_pad_top(btn_guest, 4, 0);
  lv_obj_set_style_pad_bottom(btn_guest, 4, 0);
  lv_obj_add_event_cb(btn_guest, guest_btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lg = lv_label_create(btn_guest);
  lv_label_set_text(lg, "Guest mode");
  lv_obj_center(lg);
  lv_obj_set_style_text_color(lg, dark, 0);
  lv_obj_set_style_text_font(lg, pick_font_20_or_14(), 0);

  lv_obj_t *btn_user = lv_btn_create(cont_btn);
  lv_obj_set_grid_cell(btn_user, LV_GRID_ALIGN_STRETCH, 1, 1,
                       LV_GRID_ALIGN_STRETCH, 0, 1);
  lv_obj_set_style_radius(btn_user, 14, 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(0, 140, 200), 0);
  lv_obj_set_style_bg_color(btn_user, lv_color_make(0, 175, 235), LV_STATE_PRESSED);
  lv_obj_set_style_border_width(btn_user, 0, 0);
  lv_obj_set_style_shadow_width(btn_user, 0, 0);
  lv_obj_set_style_pad_top(btn_user, 4, 0);
  lv_obj_set_style_pad_bottom(btn_user, 4, 0);
  lv_obj_add_event_cb(btn_user, user_btn_event_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lu = lv_label_create(btn_user);
  lv_label_set_text(lu, "User mode (nhap ID)");
  lv_obj_center(lu);
  lv_obj_set_style_text_color(lu, lv_color_white(), 0);
  lv_obj_set_style_text_font(lu, pick_font_20_or_14(), 0);

  MainUi_UpdateStatus("--:--  --/--/----", "--%");
  refresh_device_id_label();
  refresh_user_id_label();

  lv_scr_load(main_scr);
}

void MainUi_Init(MainUiSaveUserIdCb saveUserIdCb,
                 MainUiGetTextCb getDeviceIdCb,
                 MainUiGetTextCb getUserIdCb) {
  g_save_user_id_cb = saveUserIdCb;
  g_get_device_id_cb = getDeviceIdCb;
  g_get_user_id_cb = getUserIdCb;

  if (!main_scr) {
    create_main_gui();
  } else {
    refresh_device_id_label();
    refresh_user_id_label();
    show_main_screen();
  }
}

void MainUi_UpdateStatus(const char *time_str, const char *batt_str) {
  if (main_label_time && time_str) lv_label_set_text(main_label_time, time_str);
  if (main_label_batt && batt_str) lv_label_set_text(main_label_batt, batt_str);
  GuestMode_UpdateStatus(time_str, batt_str);
}
