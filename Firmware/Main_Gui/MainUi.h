#pragma once

#include <lvgl.h>

using MainUiSaveUserIdCb = void (*)(const char *userId);
using MainUiGetTextCb = const char *(*)(void);

void MainUi_Init(MainUiSaveUserIdCb saveUserIdCb,
                 MainUiGetTextCb getDeviceIdCb,
                 MainUiGetTextCb getUserIdCb);

void MainUi_UpdateStatus(const char *time_str, const char *batt_str);
