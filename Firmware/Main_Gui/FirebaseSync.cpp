#include "FirebaseSync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "HR_SPO2_BP.h"

#ifdef USE_FIREBASE_ESP_CLIENT
#include <Firebase_ESP_Client.h>
// optional pointer to FirebaseData provided by main sketch when using Firebase_ESP_Client
static FirebaseData *g_fbdo = nullptr;
#endif

static String g_wifi_ssid;
static String g_wifi_password;
static String g_firebase_db_url;

static FirebaseSyncGetTextCb g_get_device_id_cb = nullptr;
static FirebaseSyncGetTextCb g_get_user_id_cb = nullptr;

static int g_battery_percent = -1;
static uint32_t g_push_interval_ms = 5000;
static uint32_t g_last_push_ms = 0;
static char g_current_user_id[16] = ""; // null-terminated

static double g_location_lat = 0.0;
static double g_location_lng = 0.0;
static bool g_has_location = false;

// background task state
static volatile bool g_pending_push = false;
static TaskHandle_t g_firebase_task_handle = NULL;

// last-sent measurement snapshot to detect changes and push immediately
static int g_last_sent_hr = -1;
static int g_last_sent_spo2 = -1;
static int g_last_sent_sys = -1;
static int g_last_sent_dia = -1;

// History append throttling (avoid creating too many rows)
static uint32_t g_last_history_push_ms = 0;
static const uint32_t HISTORY_PUSH_INTERVAL_MS = 15000; // 15s per sample max
static int g_last_hist_hr = -9999;
static int g_last_hist_spo2 = -9999;
static int g_last_hist_sys = -9999;
static int g_last_hist_dia = -9999;

static void firebase_push_impl();

static bool wifi_connect_if_needed();

static void firebase_task(void *arg) {
  (void)arg;
  for (;;) {
    // do lightweight wifi connect attempt (non-blocking) so worker can trigger connection
    wifi_connect_if_needed();

    // Detect measurement changes (read globals from HR_SPO2_BP) and queue an immediate push.
    // This allows near-realtime pushes when sensor values update.
    int curHr = (heartRate >= 30 && heartRate <= 220) ? (int)heartRate : -1;
    int curSpo2 = (spo2 >= 50 && spo2 <= 100) ? (int)spo2 : -1;
    // Only consider BP values for upload when the last measurement was initiated
    // from the User dashboard (online mode). Measurements initiated from Guest
    // mode should not be uploaded.
    int curSys = (lastBPOrigin == BP_ORIGIN_USER && lastSYS > 0.0f) ? (int)roundf(lastSYS) : -1;
    int curDia = (lastBPOrigin == BP_ORIGIN_USER && lastDIA > 0.0f) ? (int)roundf(lastDIA) : -1;

    if (curHr != g_last_sent_hr || curSpo2 != g_last_sent_spo2 || curSys != g_last_sent_sys || curDia != g_last_sent_dia) {
      // update snapshot
      g_last_sent_hr = curHr;
      g_last_sent_spo2 = curSpo2;
      g_last_sent_sys = curSys;
      g_last_sent_dia = curDia;
      // queue immediate push
      g_pending_push = true;
      g_last_push_ms = millis();
    }

    if (g_pending_push || (millis() - g_last_push_ms >= g_push_interval_ms)) {
      g_pending_push = false;
      g_last_push_ms = millis();
      Serial.println("[Firebase] Task: triggering push");
      firebase_push_impl();
    }

    // delay between iterations — keep moderate to avoid CPU/heap pressure
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}

static bool g_wifi_connecting = false;
static uint32_t g_wifi_connect_start_ms = 0;
static uint32_t g_wifi_next_retry_ms = 0;

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 3000;
static const uint32_t WIFI_RETRY_DELAY_MS = 2500;
// Increased HTTP timeouts — networks can be slow right after WiFi connects
static const uint16_t HTTP_CONNECT_TIMEOUT_MS = 5000;
static const uint16_t HTTP_RW_TIMEOUT_MS = 5000;
// Wait timeout used when validating a user id (allow WiFi to come up)
static const uint32_t VALIDATE_WIFI_WAIT_MS = 5000;

static bool wifi_connect_if_needed() {
  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    // If we were actively connecting and now connected, trigger an immediate
    // push so pending data goes out without waiting for the next interval.
    if (g_wifi_connecting) {
      Serial.println("[WiFi] Connected — triggering immediate push");
      g_pending_push = true;
    }
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
  Serial.print("[HTTP] timeouts (ms): connect="); Serial.print(HTTP_CONNECT_TIMEOUT_MS);
  Serial.print(" rw="); Serial.println(HTTP_RW_TIMEOUT_MS);
}

// POST helper (used to append to history nodes)
static bool firebase_post(const String &path, const String &jsonBody) {
  if (!wifi_connect_if_needed()) return false;
  if (g_firebase_db_url.length() == 0) return false;

#ifdef USE_FIREBASE_ESP_CLIENT
  // Use Firebase_ESP_Client push (creates a unique child under path)
  if (!g_fbdo) {
    Serial.println("[Firebase] pushJSON: fbdo not provided");
    return false;
  }
  bool ok = Firebase.RTDB.pushJSON(g_fbdo, path.c_str(), jsonBody.c_str());
  if (!ok) {
    Serial.print("[Firebase] pushJSON fail "); Serial.print(path); Serial.print(" reason="); Serial.println(g_fbdo->errorReason());
  }
  return ok;
#else
  HTTPClient http;
  String url = g_firebase_db_url + "/" + path + ".json";
  http.begin(url);
  http_prepare(http);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "keep-alive");
  int code = http.POST(jsonBody);
  bool ok = (code >= 200 && code < 300);
  if (!ok) {
    Serial.print("[Firebase] POST fail "); Serial.print(path); Serial.print(" code="); Serial.println(code);
  }
  http.end();
  return ok;
#endif
}

static bool firebase_patch(const String &path, const String &jsonBody) {
  if (!wifi_connect_if_needed()) return false;
  if (g_firebase_db_url.length() == 0) return false;

#ifdef USE_FIREBASE_ESP_CLIENT
  // Use setJSON to write/overwrite the node with provided JSON
  if (!g_fbdo) {
    Serial.println("[Firebase] setJSON: fbdo not provided");
    return false;
  }
  bool ok = Firebase.RTDB.setJSON(g_fbdo, path.c_str(), jsonBody.c_str());
  if (!ok) {
    Serial.print("[Firebase] setJSON fail "); Serial.print(path); Serial.print(" reason="); Serial.println(g_fbdo->errorReason());
  }
  return ok;
#else
  HTTPClient http;
  String url = g_firebase_db_url + "/" + path + ".json";
  http.begin(url);
  http_prepare(http);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "keep-alive");
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
#endif
}

static bool firebase_get(const String &path, String &outBody) {
  outBody = "";
  if (!wifi_connect_if_needed()) return false;
  if (g_firebase_db_url.length() == 0) return false;

#ifdef USE_FIREBASE_ESP_CLIENT
  if (!g_fbdo) {
    Serial.println("[Firebase] getJSON: fbdo not provided");
    return false;
  }
  if (!Firebase.RTDB.getJSON(g_fbdo, path.c_str())) {
    Serial.print("[Firebase] getJSON fail "); Serial.print(path); Serial.print(" reason="); Serial.println(g_fbdo->errorReason());
    return false;
  }
  outBody = g_fbdo->payload();
  return true;
#else
  HTTPClient http;
  String url = g_firebase_db_url + "/" + path + ".json";
  http.begin(url);
  http_prepare(http);
  http.addHeader("Connection", "keep-alive");
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
#endif
}

void FirebaseSync_SetCurrentUserId(const char *userId) {
  if (!userId || !userId[0]) {
    g_current_user_id[0] = '\0';
    Serial.println("[Firebase] Cleared current user id");
    return;
  }
  strncpy(g_current_user_id, userId, sizeof(g_current_user_id) - 1);
  g_current_user_id[sizeof(g_current_user_id) - 1] = '\0';
  Serial.print("[Firebase] Current user id set: "); Serial.println(g_current_user_id);
}

// NOTE: auth token support removed — this project does not use Firebase REST auth tokens.

// Public: force push measurement now (non-blocking queued)
void FirebaseSync_PushMeasurementNow() {
  // set pending push so the background task triggers firebase_push_impl
  g_pending_push = true;
}

void FirebaseSync_Init(const char *wifiSsid,
                       const char *wifiPassword,
                       const char *firebaseDbUrl,
                       FirebaseSyncGetTextCb getDeviceIdCb,
                       FirebaseSyncGetTextCb getUserIdCb,
                       uint32_t pushIntervalMs,
                       void *firebaseDataPtr) {
  g_wifi_ssid = wifiSsid ? wifiSsid : "";
  g_wifi_password = wifiPassword ? wifiPassword : "";
  g_firebase_db_url = firebaseDbUrl ? firebaseDbUrl : "";
  g_get_device_id_cb = getDeviceIdCb;
  g_get_user_id_cb = getUserIdCb;
  g_push_interval_ms = pushIntervalMs > 0 ? pushIntervalMs : 5000;
  g_last_push_ms = 0;
  // If caller provided a FirebaseData pointer, store it for use by Firebase_ESP_Client calls
#ifdef USE_FIREBASE_ESP_CLIENT
  g_fbdo = firebaseDataPtr ? (FirebaseData *)firebaseDataPtr : nullptr;
#endif
  // start background firebase task if not running
  if (g_firebase_task_handle == NULL) {
    BaseType_t res = xTaskCreate(firebase_task, "firebase_task", 8192, NULL, 1, &g_firebase_task_handle);
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

void FirebaseSync_SetLocation(double latitude, double longitude) {
  g_location_lat = latitude;
  g_location_lng = longitude;
  g_has_location = true;
  g_pending_push = true;
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
  // fallback to internal user id when no callback provided
  if ((!userId || !userId[0]) && g_current_user_id[0]) userId = g_current_user_id;
  if (!deviceId || !deviceId[0]) return;

  String battStr = (g_battery_percent >= 0) ? String(g_battery_percent) : String("null");
  // Send device-level info. Do NOT include user ownership or application-mode here;
  // dashboard/web is responsible for claim/link and mode management.
  String payload = String("{\"deviceId\":\"") + deviceId +
                   "\",\"status\":\"online\",\"batteryPercent\":" + battStr;

  if (g_has_location) {
    payload += String(",\"location\":{\"lat\":") + String(g_location_lat, 6) +
               String(",\"lng\":") + String(g_location_lng, 6) +
               String(",\"updatedAt\":{\".sv\":\"timestamp\"}}" );
  }

  payload += String(",\"lastSeen\":{\".sv\":\"timestamp\"},\"updatedAt\":{\".sv\":\"timestamp\"}}");

  // Push device info to /devices/<deviceId>. Do NOT update patients/<userId> here
  // (we intentionally send device-level info without depending on userId).
  bool ok = firebase_patch(String("devices/") + deviceId, payload);

  // If a userId is set, also push latest measurement under /measurements/<userId>
  if (userId && userId[0]) {
    // To reduce round-trips, we skip an existence GET and optimistically push
    // the measurement. The web/dashboard can ignore measurements for unknown
    // users if necessary. This avoids a costly GET before every push.
    // Read globals from HR_SPO2_BP (declared extern in header)
    // Only include fields that look valid (non-zero or in-range)
    String mHr = (heartRate >= 30 && heartRate <= 220) ? String(heartRate) : String("null");
    String mSpo2 = (spo2 >= 50 && spo2 <= 100) ? String(spo2) : String("null");
    String mSys = (lastBPOrigin == BP_ORIGIN_USER && lastSYS > 0.0f) ? String((int)roundf(lastSYS)) : String("null");
    String mDia = (lastBPOrigin == BP_ORIGIN_USER && lastDIA > 0.0f) ? String((int)roundf(lastDIA)) : String("null");

    // The timestamp expression needs to be valid JSON for PATCH — embed using server-value
    // Firebase REST expects {"timestamp":{" .sv":"timestamp"}} but previous code used
    // lastSeen style; we will send timestamp as a number using client's millis/epoch is not safe,
    // so use a two-step: PATCH with ".sv":"timestamp" requires proper quoting; instead,
    // construct JSON with server timestamp token as string value for key 'timestamp'.
    // Workaround: use a small JSON and then immediately set timestamp on server via client-side patch

    // Simpler: write measurement object with a timestamp property set to server timestamp via
    // the token: "\"timestamp\":{.sv:\"timestamp\"}" – but since dots aren't allowed in key,
    // use the same pattern as device payload used earlier
    String measPayload = String("{\"hr\":") + mHr + ",\"bpSys\":" + mSys + ",\"bpDia\":" + mDia + ",\"spo2\":" + mSpo2 + ",\"timestamp\":{\".sv\":\"timestamp\"}}";

    // Write latest measurement under /patients/<userId>/measurements/latest
    // (cheap for listeners that only need newest value). Also append samples to
    // /patients/<userId>/measurements/history so the web History tab can filter/export by time.
    bool mokLatest = firebase_patch(String("patients/") + userId + String("/measurements/latest"), measPayload);
    if (!mokLatest) {
      Serial.print("[Firebase] measurement latest push fail for "); Serial.println(userId);
    }

    // Append to history (periodic sampling).
    // Policy A: save a row every HISTORY_PUSH_INTERVAL_MS (even if values do not change)
    // so the web History tab can draw a proper timeline. Still allow an immediate one-off
    // append when a new BP result appears (even if inside the interval).
    const bool hasMetric = (mHr != "null") || (mSpo2 != "null") || (mSys != "null") || (mDia != "null");
    const uint32_t nowMs = millis();
    const bool intervalOk = (g_last_history_push_ms == 0) || (nowMs - g_last_history_push_ms >= HISTORY_PUSH_INTERVAL_MS);

    // Convert to ints for dedup comparisons (use sentinel when null)
    int iHr = (mHr == "null") ? -1 : (int)roundf(heartRate);
    int iSpo2 = (mSpo2 == "null") ? -1 : (int)roundf(spo2);
    int iSys = (mSys == "null") ? -1 : (int)roundf(lastSYS);
    int iDia = (mDia == "null") ? -1 : (int)roundf(lastDIA);
    const bool bpChanged = (iSys != g_last_hist_sys) || (iDia != g_last_hist_dia);
    const bool forceImmediateBpAppend = (!intervalOk) && (mSys != "null" || mDia != "null") && bpChanged;

    if (hasMetric && (intervalOk || forceImmediateBpAppend)) {
      // Include deviceId to help debugging / future multi-device support
      String histPayload = String("{\"deviceId\":\"") + deviceId + String("\",") +
                           String("\"hr\":") + mHr + String(",") +
                           String("\"bpSys\":") + mSys + String(",") +
                           String("\"bpDia\":") + mDia + String(",") +
                           String("\"spo2\":") + mSpo2 + String(",") +
                           String("\"timestamp\":{\".sv\":\"timestamp\"}") +
                           String("}");

      bool mokHist = firebase_post(String("patients/") + userId + String("/measurements/history"), histPayload);
      if (mokHist) {
        g_last_history_push_ms = nowMs;
        g_last_hist_hr = iHr;
        g_last_hist_spo2 = iSpo2;
        g_last_hist_sys = iSys;
        g_last_hist_dia = iDia;
      } else {
        Serial.print("[Firebase] measurement history append fail for "); Serial.println(userId);
      }
    }
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

  // Yêu cầu: đẩy trạng thái + pin lên Firebase trước (queue)
  FirebaseSync_PushStatusAndBattery();

  // Wait a short time for WiFi to connect (non-blocking connect may be in progress).
  uint32_t start = millis();
  bool connected = false;
  while (millis() - start < VALIDATE_WIFI_WAIT_MS) {
    if (wifi_connect_if_needed() && WiFi.status() == WL_CONNECTED) { connected = true; break; }
    // give other tasks time to progress
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (!connected) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "Loi ket noi Firebase");
    return false;
  }

  // Check that the patient/user exists in Firebase under /patients/<userId>.
  // First, try a fast path: check whether patients/<id>/measurements/latest exists
  const int maxTries = 3;
  String measOut;
  bool gotMeas = false;
  for (int t = 0; t < maxTries; ++t) {
    gotMeas = firebase_get(String("patients/") + String(userId) + String("/measurements/latest"), measOut);
    if (gotMeas) break;
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  // If measurement exists (non-null), accept the ID immediately (fast path)
  if (gotMeas && measOut != "null" && measOut.length() > 0) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "OK");
    return true;
  }

  // Otherwise fallback to checking patients/<userId>
  String out;
  bool got = false;
  for (int t = 0; t < maxTries; ++t) {
    got = firebase_get(String("patients/") + String(userId), out);
    if (got) break;
    // small delay before retry
    vTaskDelay(pdMS_TO_TICKS(300));
  }
  if (!got) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "Loi ket noi Firebase");
    return false;
  }

  // firebase returns null when key not found
  if (out == "null" || out.length() == 0) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "ID chua duoc dang ky tren web/app");
    return false;
  }

  // Optionally, we could verify /devices/<deviceId>.patientId == userId here.
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
