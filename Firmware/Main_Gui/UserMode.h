#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usermode_back_cb_t)(void);
typedef bool (*usermode_validate_cb_t)(const char *userId, char *errMsg, size_t errMsgSize);
typedef void (*usermode_success_cb_t)(const char *userId);

void UserMode_Init(const lv_font_t *btn_font,
                   const lv_font_t *back_font,
                   usermode_back_cb_t back_cb,
                   usermode_validate_cb_t validate_cb,
                   usermode_success_cb_t success_cb);

void UserMode_Show(void);
void UserMode_SetStatus(const char *text, bool is_error);

#ifdef __cplusplus
}
#endif
