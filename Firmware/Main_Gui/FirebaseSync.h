#pragma once

#include <Arduino.h>

using FirebaseSyncGetTextCb = const char *(*)(void);

void FirebaseSync_Init(const char *wifiSsid,
                       const char *wifiPassword,
                       const char *firebaseDbUrl,
                       FirebaseSyncGetTextCb getDeviceIdCb,
                       FirebaseSyncGetTextCb getUserIdCb,
                       uint32_t pushIntervalMs);

void FirebaseSync_SetBatteryPercent(int batteryPercent);
bool FirebaseSync_PushStatusAndBattery();
bool FirebaseSync_ValidateUserId(const char *userId, char *errMsg, size_t errMsgSize);
void FirebaseSync_Loop();

// Set/Clear the current userId on the device (5-digit string). When set,
// the sync task will also push measurement data under /measurements/<userId>.
void FirebaseSync_SetCurrentUserId(const char *userId);

// Force push of the latest measurement (reads globals from HR_SPO2_BP).
void FirebaseSync_PushMeasurementNow();
