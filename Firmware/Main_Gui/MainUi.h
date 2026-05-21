#pragma once

#include <lvgl.h>

using MainUiSaveUserIdCb = void (*)(const char *userId);
using MainUiGetTextCb = const char *(*)(void);
using MainUiOpenUserModeCb = void (*)(void);

void MainUi_Init(MainUiSaveUserIdCb saveUserIdCb,
                 MainUiGetTextCb getDeviceIdCb,
                 MainUiGetTextCb getUserIdCb,
                 MainUiOpenUserModeCb openUserModeCb);

void MainUi_ShowMainScreen();

void MainUi_UpdateStatus(const char *time_str, const char *batt_str);
