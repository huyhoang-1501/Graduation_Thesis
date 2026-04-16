#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*keypad_back_cb_t)(void);
typedef void (*keypad_next_cb_t)(const char *text);

void keypad_init_screen(const lv_font_t *btn_font,
                        const lv_font_t *back_font,
                        keypad_back_cb_t back_cb,
                        keypad_next_cb_t next_cb);

lv_obj_t *keypad_get_screen(void);
const char *keypad_get_text(void);
void keypad_set_text(const char *text);

#ifdef __cplusplus
}
#endif
