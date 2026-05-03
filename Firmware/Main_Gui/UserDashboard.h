#pragma once

#include <stdbool.h>

typedef void (*UserDashboardBackCallback)();

void UserDashboard_Show(UserDashboardBackCallback backCallback);
void UserDashboard_Loop();
void UserDashboard_UpdateStatus(const char *time_str, const char *batt_str);
