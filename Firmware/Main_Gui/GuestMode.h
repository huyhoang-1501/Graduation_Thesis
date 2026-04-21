#pragma once

using GuestBackCallback = void (*)();

void GuestMode_Show(GuestBackCallback backCallback);
void GuestMode_Loop();
void GuestMode_UpdateStatus(const char *time_str, const char *batt_str);
