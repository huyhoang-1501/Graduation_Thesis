// monitoring_icon.h
#pragma once

#include <lvgl.h>
#include <stdint.h>

// Kích thước logo 
#define LOGO_W 160
#define LOGO_H 138

// Mảng pixel raw RGB565 (PROGMEM) – được định nghĩa trong .c
extern const uint16_t image_logo_pixels[LOGO_W * LOGO_H];

// Image descriptor cho LVGL
extern const lv_img_dsc_t monitoring_icon;