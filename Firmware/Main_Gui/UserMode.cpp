#include "UserMode.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "BPMeasure.h"

#include "keypad.h"
#include "FirebaseSync.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

typedef struct {
  char userId[16];
  usermode_success_cb_t success_cb;
} validate_ctx_t;

typedef struct {
  bool ok;
  char msg[128];
  char userId[16];
  usermode_success_cb_t success_cb;
} result_t;

static void ui_result(void *arg);
static void validation_task(void *pv);

static usermode_back_cb_t g_back_cb = NULL;
static usermode_validate_cb_t g_validate_cb = NULL;
static usermode_success_cb_t g_success_cb = NULL;

static const lv_font_t *g_btn_font = NULL;
static const lv_font_t *g_back_font = NULL;

static lv_obj_t *g_status_label = NULL;
static bool g_inited = false;
static lv_obj_t *g_start_btn = NULL;

static void ensure_status_label() {
  lv_obj_t *scr = keypad_get_screen();
  if (!scr || g_status_label) return;

  g_status_label = lv_label_create(scr);
  lv_label_set_long_mode(g_status_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(g_status_label, 360);
  lv_obj_align(g_status_label, LV_ALIGN_TOP_RIGHT, -4, 6);
  lv_obj_set_style_text_align(g_status_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(g_status_label, lv_color_make(80, 120, 140), 0);
  lv_label_set_text(g_status_label, "Nhap User ID 5 so da tao tren Web");
}

void UserMode_SetStatus(const char *text, bool is_error) {
  if (!g_status_label) return;
  lv_obj_set_style_text_color(g_status_label,
                              is_error ? lv_color_make(220, 40, 40) : lv_color_make(60, 120, 80),
                              0);
  lv_label_set_text(g_status_label, text && text[0] ? text : "");
}

static bool is_valid_user_id_5digits(const char *userId) {
  if (!userId) return false;
  if (strlen(userId) != 5) return false;
  for (size_t i = 0; i < 5; ++i) {
    if (!isdigit((unsigned char)userId[i])) return false;
  }
  return true;
}

static void on_keypad_back_internal() {
  if (g_back_cb) g_back_cb();
}

static void on_keypad_view_internal(const char *text) {
  char errMsg[96] = {0};
  const char *userId = text ? text : "";

  if (!is_valid_user_id_5digits(userId)) {
    UserMode_SetStatus("User ID phai dung 5 so", true);
    return;
  }

  if (!g_validate_cb) {
    UserMode_SetStatus("Chua cau hinh bo kiem tra User ID", true);
    return;
  }

  UserMode_SetStatus("Dang kiem tra User ID tren Firebase...", false);

  // Run validation by creating a background task (validation_task).
  validate_ctx_t *ctx = (validate_ctx_t *)malloc(sizeof(validate_ctx_t));
  if (!ctx) {
    UserMode_SetStatus("Loi bo nho", true);
    return;
  }
  strncpy(ctx->userId, userId, sizeof(ctx->userId) - 1);
  ctx->userId[sizeof(ctx->userId) - 1] = '\0';
  ctx->success_cb = g_success_cb;

  BaseType_t r = xTaskCreate(validation_task, "validate_user", 4096, ctx, 1, NULL);
  if (r != pdPASS) {
    free(ctx);
    UserMode_SetStatus("Khong tao duoc task", true);
    return;
  }
}

void UserMode_Init(const lv_font_t *btn_font,
                   const lv_font_t *back_font,
                   usermode_back_cb_t back_cb,
                   usermode_validate_cb_t validate_cb,
                   usermode_success_cb_t success_cb) {
  g_btn_font = btn_font;
  g_back_font = back_font;
  g_back_cb = back_cb;
  g_validate_cb = validate_cb;
  g_success_cb = success_cb;

  if (!g_inited) {
    keypad_init_screen(g_btn_font,
               g_back_font,
               on_keypad_back_internal,
               on_keypad_view_internal,
               "VIEW");
    g_inited = true;
  }

  keypad_set_placeholder_text("Nhap User ID 5 so da dang ky tren Web...");
  ensure_status_label();
}

void UserMode_Show(void) {
  if (!g_inited) return;
  keypad_set_text("");
  keypad_set_placeholder_text("Nhap User ID 5 so da dang ky tren Web...");
  ensure_status_label();
  UserMode_SetStatus("Nhap User ID 5 so da tao tren Web", false);

  lv_obj_t *scr = keypad_get_screen();
  if (scr) {
    lv_scr_load(scr);
    // create a Start button on the keypad screen (to trigger BP measurement)
    // moved to bottom-left so it doesn't appear on the keypad area
    if (!g_start_btn) {
      g_start_btn = lv_btn_create(scr);
      lv_obj_set_size(g_start_btn, 92, 42);
      lv_obj_align(g_start_btn, LV_ALIGN_BOTTOM_LEFT, 8, -8);
      lv_obj_set_style_radius(g_start_btn, 12, 0);
      lv_obj_t *lbl = lv_label_create(g_start_btn);
      lv_label_set_text(lbl, "Start");
      lv_obj_set_style_text_color(lbl, lv_color_make(0,0,0), 0); // black text
      if (g_btn_font) lv_obj_set_style_text_font(lbl, g_btn_font, 0);
      lv_obj_center(lbl);
      lv_obj_add_event_cb(g_start_btn, [](lv_event_t *e){ if (lv_event_get_code(e)!=LV_EVENT_CLICKED) return; UserMode_SetStatus("Measuring BP...", false); BPMeasure_Start([](float sys, float dia, float map, float bpm){ char buf[128]; if (sys>0) snprintf(buf,sizeof(buf),"SYS %.0f DIA %.0f BPM %.0f",sys,dia,bpm); else snprintf(buf,sizeof(buf),"BP failed"); size_t n = strlen(buf)+1; char *copy = (char*)malloc(n); if (copy) { memcpy(copy, buf, n); lv_async_call([](void* a){ UserMode_SetStatus(((char*)a), false); free(a); }, copy); } }); }, LV_EVENT_CLICKED, NULL);
    }
  }
}

// --- validation task and UI result handler (file-scope) ---
static void ui_result(void *arg) {
  result_t *r = (result_t *)arg;
  if (!r) return;
  if (!r->ok) {
    UserMode_SetStatus(r->msg[0] ? r->msg : "User ID khong hop le", true);
  } else {
    UserMode_SetStatus("User ID hop le. Dang vao man hinh do...", false);
    if (r->success_cb) r->success_cb(r->userId);
  }
  free(r);
}

static void validation_task(void *pv) {
  validate_ctx_t *c = (validate_ctx_t *)pv;
  if (!c) {
    vTaskDelete(NULL);
    return;
  }
  result_t *res = (result_t *)malloc(sizeof(result_t));
  if (!res) {
    free(c);
    vTaskDelete(NULL);
    return;
  }

  char err[128] = {0};
  bool ok = false;
  // Use configured validation callback if provided (allows local validation when Firebase is disabled)
  if (g_validate_cb) {
    ok = g_validate_cb(c->userId, err, sizeof(err));
  } else {
    ok = FirebaseSync_ValidateUserId(c->userId, err, sizeof(err));
  }
  res->ok = ok;
  strncpy(res->msg, err, sizeof(res->msg) - 1);
  res->msg[sizeof(res->msg) - 1] = '\0';
  strncpy(res->userId, c->userId, sizeof(res->userId) - 1);
  res->userId[sizeof(res->userId) - 1] = '\0';
  res->success_cb = c->success_cb;

  free(c);
  lv_async_call(ui_result, res);
  vTaskDelete(NULL);
}
