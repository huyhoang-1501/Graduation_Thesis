#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR & DISPLAY
 *====================*/

#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   1

/* Màn hình 320x480 (Landscape) */
#define LV_HOR_RES_MAX     320
#define LV_VER_RES_MAX      480

/*====================
   MEMORY SETTINGS
 *====================*/

#define LV_MEM_SIZE        (32U * 1024U)

/*====================
   MISC
 *====================*/

#define LV_TICK_CUSTOM     0   // Dùng lv_timer_handler() trong loop()

/*====================
   MODULES (WIDGETS)
 *====================*/

#define LV_USE_LOG         0

#define LV_USE_LABEL       1
#define LV_USE_BTN         1
#define LV_USE_TEXTAREA    1
#define LV_USE_BTNmatrix   1
#define LV_USE_KEYBOARD    1
#define LV_FONT_MONTSERRAT_28 1
/* Bật mấy cái widget nền tảng mà slider/spinner cần */
#define LV_USE_BAR         1
#define LV_USE_ARC         1

/* Có thể thêm nếu sau này bạn cần:
   #define LV_USE_SLIDER   1
   #define LV_USE_SPINNER  1
*/

/*====================
   THEME
 *====================*/

#define LV_USE_THEME_DEFAULT 1

#endif /*LV_CONF_H*/