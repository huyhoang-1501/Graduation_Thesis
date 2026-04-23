#include "FirebaseSync.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static String g_wifi_ssid;
static String g_wifi_password;
static String g_firebase_db_url;

static FirebaseSyncGetTextCb g_get_device_id_cb = nullptr;
static FirebaseSyncGetTextCb g_get_user_id_cb = nullptr;

static int g_battery_percent = -1;
static uint32_t g_push_interval_ms = 5000;
static uint32_t g_last_push_ms = 0;

// background task state
static volatile bool g_pending_push = false;
static TaskHandle_t g_firebase_task_handle = NULL;

static void firebase_push_impl();

static bool wifi_connect_if_needed();

static void firebase_task(void *arg) {
  (void)arg;
  for (;;) {
    // do lightweight wifi connect attempt (non-blocking) so worker can trigger connection
    wifi_connect_if_needed();

    if (g_pending_push || (millis() - g_last_push_ms >= g_push_interval_ms)) {
      g_pending_push = false;
      g_last_push_ms = millis();
      firebase_push_impl();
    }

    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

static bool g_wifi_connecting = false;
static uint32_t g_wifi_connect_start_ms = 0;
static uint32_t g_wifi_next_retry_ms = 0;

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 3000;
static const uint32_t WIFI_RETRY_DELAY_MS = 2500;
static const uint16_t HTTP_CONNECT_TIMEOUT_MS = 900;
static const uint16_t HTTP_RW_TIMEOUT_MS = 1200;

static bool wifi_connect_if_needed() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    g_wifi_connecting = false;
    return true;
  }

  if (g_wifi_ssid.length() == 0 || g_wifi_ssid == "YOUR_WIFI_SSID") {
    Serial.println("[WiFi] Chua cau hinh SSID/PASSWORD");
    return false;
  }

  uint32_t now = millis();
  if (now < g_wifi_next_retry_ms) return false;

  if (!g_wifi_connecting) {
    Serial.println("[WiFi] Dang ket noi (non-blocking)...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(g_wifi_ssid.c_str(), g_wifi_password.c_str());
    g_wifi_connecting = true;
    g_wifi_connect_start_ms = now;
    return false;
  }

  if (now - g_wifi_connect_start_ms >= WIFI_CONNECT_TIMEOUT_MS) {
    Serial.println("[WiFi] Timeout ket noi, se thu lai");
    g_wifi_connecting = false;
    g_wifi_next_retry_ms = now + WIFI_RETRY_DELAY_MS;
  }

  return false;
}

static void http_prepare(HTTPClient &http) {
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_RW_TIMEOUT_MS);
}

static bool firebase_patch(const String &path, const String &jsonBody) {
  if (!wifi_connect_if_needed()) return false;
  if (g_firebase_db_url.length() == 0) return false;

  HTTPClient http;
  String url = g_firebase_db_url + "/" + path + ".json";
  http.begin(url);
  http_prepare(http);
  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", jsonBody);
  bool ok = (code >= 200 && code < 300);
  if (!ok) {
    Serial.print("[Firebase] PATCH fail ");
    Serial.print(path);
    Serial.print(" code=");
    Serial.println(code);
  }
  http.end();
  return ok;
}

static bool firebase_get(const String &path, String &outBody) {
  outBody = "";
  if (!wifi_connect_if_needed()) return false;
  if (g_firebase_db_url.length() == 0) return false;

  HTTPClient http;
  String url = g_firebase_db_url + "/" + path + ".json";
  http.begin(url);
  http_prepare(http);
  int code = http.GET();
  if (code >= 200 && code < 300) {
    outBody = http.getString();
    http.end();
    return true;
  }

  Serial.print("[Firebase] GET fail ");
  Serial.print(path);
  Serial.print(" code=");
  Serial.println(code);
  http.end();
  return false;
}

void FirebaseSync_Init(const char *wifiSsid,
                       const char *wifiPassword,
                       const char *firebaseDbUrl,
                       FirebaseSyncGetTextCb getDeviceIdCb,
                       FirebaseSyncGetTextCb getUserIdCb,
                       uint32_t pushIntervalMs) {
  g_wifi_ssid = wifiSsid ? wifiSsid : "";
  g_wifi_password = wifiPassword ? wifiPassword : "";
  g_firebase_db_url = firebaseDbUrl ? firebaseDbUrl : "";
  g_get_device_id_cb = getDeviceIdCb;
  g_get_user_id_cb = getUserIdCb;
  g_push_interval_ms = pushIntervalMs > 0 ? pushIntervalMs : 5000;
  g_last_push_ms = 0;
  // start background firebase task if not running
  if (g_firebase_task_handle == NULL) {
    BaseType_t res = xTaskCreate(firebase_task, "firebase_task", 4096, NULL, 1, &g_firebase_task_handle);
    if (res != pdPASS) {
      Serial.println("[Firebase] Failed to create firebase_task");
      g_firebase_task_handle = NULL;
    } else {
      Serial.println("[Firebase] firebase_task started");
    }
  }
}

void FirebaseSync_SetBatteryPercent(int batteryPercent) {
  g_battery_percent = batteryPercent;
}

// Queue a push for the background task. Non-blocking.
bool FirebaseSync_PushStatusAndBattery() {
  const char *deviceId = g_get_device_id_cb ? g_get_device_id_cb() : "";
  if (!deviceId || !deviceId[0]) return false;

  g_pending_push = true;
  return true;
}

// Blocking push implementation used by the background worker or synchronous validation path
static void firebase_push_impl() {
  const char *deviceId = g_get_device_id_cb ? g_get_device_id_cb() : "";
  const char *userId = g_get_user_id_cb ? g_get_user_id_cb() : "";
  if (!deviceId || !deviceId[0]) return;

  String battStr = (g_battery_percent >= 0) ? String(g_battery_percent) : String("null");
  String payload = String("{\"deviceId\":\"") + deviceId +
                   "\",\"status\":\"online\",\"batteryPercent\":" + battStr +
                   ",\"mode\":\"user\",\"updatedAt\":{\".sv\":\"timestamp\"}}";

  bool ok = firebase_patch(String("devices/") + deviceId, payload);
  if (ok && userId && userId[0]) {
    String patientPayload = String("{\"status\":\"online\",\"updatedAt\":{\".sv\":\"timestamp\"}}");
    firebase_patch(String("patients/") + userId, patientPayload);
  }
}

bool FirebaseSync_ValidateUserId(const char *userId, char *errMsg, size_t errMsgSize) {
  if (errMsg && errMsgSize) errMsg[0] = '\0';

  const char *deviceId = g_get_device_id_cb ? g_get_device_id_cb() : "";
  if (!deviceId || !deviceId[0]) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "Device ID rong");
    return false;
  }

  if (!userId || !userId[0]) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "User ID rong");
    return false;
  }

  // Yeu cau: day trang thai + pin len Firebase truoc.
  FirebaseSync_PushStatusAndBattery();

  String body;
  if (!firebase_get(String("patients/") + userId, body)) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "Khong doc duoc Firebase");
    return false;
  }

  body.trim();
  if (body == "null") {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "User ID chua dang ky tren Web");
    return false;
  }

  String expectDevice = String("\"deviceId\":\"") + deviceId + "\"";
  if (body.indexOf(expectDevice) < 0) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "User ID nay khong gan voi %s", deviceId);
    return false;
  }

  String bindPayload = String("{\"patientId\":\"") + userId +
                       "\",\"linked\":true,\"status\":\"linked\",\"updatedAt\":{\".sv\":\"timestamp\"}}";
  firebase_patch(String("devices/") + deviceId, bindPayload);

  if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "OK");
  return true;
}

void FirebaseSync_Loop() {
  // Luon goi de tien trinh ket noi WiFi theo kieu non-blocking.
  wifi_connect_if_needed();

  if (millis() - g_last_push_ms >= g_push_interval_ms) {
    g_last_push_ms = millis();
    FirebaseSync_PushStatusAndBattery();
  }
}
