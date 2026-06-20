#include "FirebaseSync.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
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
static uint32_t g_push_interval_ms = 2500;
static uint32_t g_last_push_ms = 0;
static char g_current_user_id[16] = ""; // null-terminated
static bool g_current_user_id_loaded_from_nvs = false;
static const char *USER_NVS_NAMESPACE = "usercfg";
static const char *USER_NVS_KEY_ID = "current_user_id";

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
    int curHr = (hr >= 30 && hr <= 220) ? (int)roundf(hr) : -1;
    int curSpo2 = (s >= 50 && s <= 100) ? (int)roundf(s) : -1;
    // Upload BP values when they come from UserDashboard or GuestMode.
    // GuestMode can upload after a user id has been stored in NVS and restored
    // into FirebaseSync_SetCurrentUserId() at boot.
    bool bpUploadOrigin = (lastBPOrigin == BP_ORIGIN_USER || lastBPOrigin == BP_ORIGIN_GUEST);
    int curSys = (bpUploadOrigin && lastSYS > 0.0f) ? (int)roundf(lastSYS) : -1;
    int curDia = (bpUploadOrigin && lastDIA > 0.0f) ? (int)roundf(lastDIA) : -1;

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
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

static bool g_wifi_connecting = false;
static uint32_t g_wifi_connect_start_ms = 0;
static uint32_t g_wifi_next_retry_ms = 0;

static const uint32_t WIFI_CONNECT_TIMEOUT_MS = 3000;
static const uint32_t WIFI_RETRY_DELAY_MS = 2500;
// HTTP timeouts kept moderate so Firebase validation fails faster on bad networks.
static const uint16_t HTTP_CONNECT_TIMEOUT_MS = 5000;
static const uint16_t HTTP_RW_TIMEOUT_MS = 3000;
// Wait timeout used when validating a user id (allow WiFi to come up)
static const uint32_t VALIDATE_WIFI_WAIT_MS = 3000;

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

static bool firebase_get_shallow(const String &path, String &outBody) {
  outBody = "";
  if (!wifi_connect_if_needed()) return false;
  if (g_firebase_db_url.length() == 0) return false;

#ifdef USE_FIREBASE_ESP_CLIENT
  // Fallback to normal getJSON when shallow query is not available.
  if (!g_fbdo) {
    Serial.println("[Firebase] getShallow: fbdo not provided");
    return false;
  }
  if (!Firebase.RTDB.getJSON(g_fbdo, path.c_str())) {
    Serial.print("[Firebase] getShallow fallback getJSON fail "); Serial.print(path); Serial.print(" reason="); Serial.println(g_fbdo->errorReason());
    return false;
  }
  outBody = g_fbdo->payload();
  return true;
#else
  HTTPClient http;
  String url = g_firebase_db_url + "/" + path + ".json?shallow=true";
  http.begin(url);
  http_prepare(http);
  http.addHeader("Connection", "keep-alive");
  int code = http.GET();
  if (code >= 200 && code < 300) {
    outBody = http.getString();
    http.end();
    return true;
  }

  Serial.print("[Firebase] GET shallow fail ");
  Serial.print(path);
  Serial.print(" code=");
  Serial.println(code);
  http.end();
  return false;
#endif
}

static void firebase_load_current_user_id_from_nvs() {
  if (g_current_user_id_loaded_from_nvs) return;
  g_current_user_id_loaded_from_nvs = true;

  Preferences pref;
  if (!pref.begin(USER_NVS_NAMESPACE, true)) {
    Serial.println("[Firebase] Failed to open NVS for current user id");
    return;
  }

  String storedId = pref.getString(USER_NVS_KEY_ID, "");
  pref.end();
  storedId.trim();

  if (storedId.length() > 0) {
    storedId.toCharArray(g_current_user_id, sizeof(g_current_user_id));
    Serial.print("[Firebase] Loaded current user id from NVS: ");
    Serial.println(g_current_user_id);
  }
}

void FirebaseSync_SetCurrentUserId(const char *userId) {
  g_current_user_id_loaded_from_nvs = true;
  if (!userId || !userId[0]) {
    g_current_user_id[0] = '\0';
    Serial.println("[Firebase] Cleared current user id");
    return;
  }
  strncpy(g_current_user_id, userId, sizeof(g_current_user_id) - 1);
  g_current_user_id[sizeof(g_current_user_id) - 1] = '\0';
  Serial.print("[Firebase] Current user id set: "); Serial.println(g_current_user_id);
}

const char* FirebaseSync_GetCurrentUserId() {
  if (g_current_user_id[0] == '\0') {
    firebase_load_current_user_id_from_nvs();
  }
  return g_current_user_id;
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
  if ((!userId || !userId[0]) && g_current_user_id[0] == '\0') {
    firebase_load_current_user_id_from_nvs();
  }
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

  // If a userId is set, also push latest measurement under /patients/<userId>/measurements.
  // The userId may come from the active UserDashboard session or from NVS after reboot,
  // allowing GuestMode to keep uploading without asking the user to enter the ID again.
  if (userId && userId[0]) {
    // To reduce round-trips, we skip an existence GET and optimistically push
    // the measurement. The web/dashboard can ignore measurements for unknown
    // users if necessary. This avoids a costly GET before every push.
    // Read globals from HR_SPO2_BP (declared extern in header)
    // Only include fields that look valid (non-zero or in-range)
    String mHr = (hr >= 30 && hr <= 220) ? String(hr, 1) : String("null");
    String mSpo2 = (s >= 50 && s <= 100) ? String(s, 1) : String("null");
    bool bpUploadOrigin = (lastBPOrigin == BP_ORIGIN_USER || lastBPOrigin == BP_ORIGIN_GUEST);
    String mSys = (bpUploadOrigin && lastSYS > 0.0f) ? String((int)roundf(lastSYS)) : String("null");
    String mDia = (bpUploadOrigin && lastDIA > 0.0f) ? String((int)roundf(lastDIA)) : String("null");

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
    int iHr = (mHr == "null") ? -1 : (int)roundf(hr);
    int iSpo2 = (mSpo2 == "null") ? -1 : (int)roundf(s);
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

  if (!userId || !userId[0]) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "User ID rong");
    return false;
  }

  // Wait a short time for WiFi to connect (non-blocking connect may be in progress).
  uint32_t start = millis();
  bool connected = false;
  while (millis() - start < VALIDATE_WIFI_WAIT_MS) {
    if (wifi_connect_if_needed() && WiFi.status() == WL_CONNECTED) {
      connected = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  if (!connected) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "Loi ket noi Firebase");
    return false;
  }

  // Fast validation: only check whether /patients/<id> exists.
  // This avoids loading measurements/history/settings and reduces delay.
  const int maxTries = 2;
  String out;
  bool got = false;
  for (int t = 0; t < maxTries; ++t) {
    got = firebase_get_shallow(String("patients/") + String(userId), out);
    if (got) break;
    vTaskDelay(pdMS_TO_TICKS(200));
  }

  if (!got) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "Loi ket noi Firebase");
    return false;
  }

  if (out == "null" || out.length() == 0) {
    if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "ID chua duoc dang ky tren web/app");
    return false;
  }

  // Queue push only after ID is confirmed valid.
  FirebaseSync_PushStatusAndBattery();

  if (errMsg && errMsgSize) snprintf(errMsg, errMsgSize, "OK");
  return true;
}

static bool parse_json_string_value(const String &json, const char *key, String &out) {
  // Simple JSON parsing for "key": "value"
  String searchKey = String("\"") + key + "\":\"";
  int idx = json.indexOf(searchKey);
  if (idx < 0) return false;
  idx += searchKey.length();
  int endIdx = json.indexOf('"', idx);
  if (endIdx < 0) return false;
  out = json.substring(idx, endIdx);
  return true;
}

static bool parse_json_int_value(const String &json, const char *key, int &out) {
  // Simple JSON parsing for "key": number
  String searchKey = String("\"") + key + "\":";
  int idx = json.indexOf(searchKey);
  if (idx < 0) return false;
  idx += searchKey.length();
  // skip whitespace
  while (idx < (int)json.length() && json[idx] == ' ') idx++;
  // read number (may be negative)
  int startIdx = idx;
  if (idx < (int)json.length() && json[idx] == '-') idx++;
  while (idx < (int)json.length() && json[idx] >= '0' && json[idx] <= '9') idx++;
  if (idx == startIdx) return false;
  String numStr = json.substring(startIdx, idx);
  out = numStr.toInt();
  return true;
}

bool FirebaseSync_FetchUserSettings(const char *userId, char *phoneOut, size_t phoneOutSize,
                                    int *spo2Min, int *spo2Max,
                                    int *hrMin, int *hrMax,
                                    int *sysMin, int *sysMax,
                                    int *diaMin, int *diaMax) {
  if (!userId || !userId[0]) return false;
  
  // Ensure WiFi is connected
  if (!wifi_connect_if_needed() && WiFi.status() != WL_CONNECTED) {
    // Try to wait a bit for WiFi
    uint32_t start = millis();
    while (millis() - start < 5000) {
      if (wifi_connect_if_needed() && WiFi.status() == WL_CONNECTED) break;
      vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (WiFi.status() != WL_CONNECTED) return false;
  }
  
  String basePath = String("patients/") + userId + "/settings";
  String response;
  bool fetchedAny = false;
  
  // Fetch phone number from patients/<userId>/settings/alertphone
  if (firebase_get(basePath + "/alertphone", response)) {
    if (response.length() > 0 && response != "null") {
      // Remove surrounding quotes if present
      String phoneStr = response;
      if (phoneStr.startsWith("\"") && phoneStr.endsWith("\"")) {
        phoneStr = phoneStr.substring(1, phoneStr.length() - 1);
      }
      phoneStr.trim();
      if (phoneStr.length() > 0) {
        strncpy(phoneOut, phoneStr.c_str(), phoneOutSize - 1);
        phoneOut[phoneOutSize - 1] = '\0';
        fetchedAny = true;
        Serial.print("[FirebaseSync] Fetched alertphone: "); Serial.println(phoneOut);
      }
    }
  }
  
  // Fetch thresholds from patients/<userId>/settings/thresholds
  String thresholdsPath = basePath + "/thresholds";
  if (firebase_get(thresholdsPath, response)) {
    if (response.length() > 0 && response != "null") {
      int val;
      if (parse_json_int_value(response, "bpDiaMax", val)) { *diaMax = val; fetchedAny = true; }
      if (parse_json_int_value(response, "bpDiaMin", val)) { *diaMin = val; fetchedAny = true; }
      if (parse_json_int_value(response, "bpSysMax", val)) { *sysMax = val; fetchedAny = true; }
      if (parse_json_int_value(response, "bpSysMin", val)) { *sysMin = val; fetchedAny = true; }
      if (parse_json_int_value(response, "hrMax", val))    { *hrMax = val; fetchedAny = true; }
      if (parse_json_int_value(response, "hrMin", val))    { *hrMin = val; fetchedAny = true; }
      if (parse_json_int_value(response, "spo2Max", val))  { *spo2Max = val; fetchedAny = true; }
      if (parse_json_int_value(response, "spo2Min", val))  { *spo2Min = val; fetchedAny = true; }
      
      if (fetchedAny) {
        Serial.println("[FirebaseSync] Fetched thresholds from Firebase");
        Serial.print("  bpSys: "); Serial.print(*sysMin); Serial.print(" - "); Serial.println(*sysMax);
        Serial.print("  bpDia: "); Serial.print(*diaMin); Serial.print(" - "); Serial.println(*diaMax);
        Serial.print("  hr: "); Serial.print(*hrMin); Serial.print(" - "); Serial.println(*hrMax);
        Serial.print("  spo2: "); Serial.print(*spo2Min); Serial.print(" - "); Serial.println(*spo2Max);
      }
    }
  }
  
  return fetchedAny;
}

void FirebaseSync_Loop() {
  // Luon goi de tien trinh ket noi WiFi theo kieu non-blocking.
  wifi_connect_if_needed();

  if (millis() - g_last_push_ms >= g_push_interval_ms) {
    g_last_push_ms = millis();
    FirebaseSync_PushStatusAndBattery();
  }
}
