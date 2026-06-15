#pragma once

#include <Arduino.h>

using FirebaseSyncGetTextCb = const char *(*)(void);

void FirebaseSync_Init(const char *wifiSsid,
                       const char *wifiPassword,
                       const char *firebaseDbUrl,
                       FirebaseSyncGetTextCb getDeviceIdCb,
                       FirebaseSyncGetTextCb getUserIdCb,
                       uint32_t pushIntervalMs,
                       void *firebaseDataPtr = nullptr);

void FirebaseSync_SetBatteryPercent(int batteryPercent);
void FirebaseSync_SetLocation(double latitude, double longitude);
bool FirebaseSync_PushStatusAndBattery();
bool FirebaseSync_ValidateUserId(const char *userId, char *errMsg, size_t errMsgSize);
void FirebaseSync_Loop();

// Set/Clear the current userId on the device (5-digit string). When set,
// the sync task will also push measurement data under /measurements/<userId>.
void FirebaseSync_SetCurrentUserId(const char *userId);

// Get the current userId (for use by UI modules that need to fetch user settings)
const char* FirebaseSync_GetCurrentUserId();

// Force push of the latest measurement (reads globals from HR_SPO2_BP).
void FirebaseSync_PushMeasurementNow();

// Fetch user settings from Firebase Realtime Database
// Returns true if settings were successfully fetched and applied
bool FirebaseSync_FetchUserSettings(const char *userId, char *phoneOut, size_t phoneOutSize,
                                    int *spo2Min, int *spo2Max,
                                    int *hrMin, int *hrMax,
                                    int *sysMin, int *sysMax,
                                    int *diaMin, int *diaMax);