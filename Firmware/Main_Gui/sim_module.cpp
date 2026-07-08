#include "sim_module.h"
#include "HR_SPO2_BP.h"

#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define GPS_RX_PIN 25
#define GPS_TX_PIN 26

// Custom alert message (empty = use default SOS message)
static String sos_custom_msg = "";

// Chia sẻ UART2 với GPS
extern HardwareSerial GPSSerial;

 // Các trạng thái của máy trạng thái SOS
enum SOSState {
  SOS_IDLE,
  SOS_STARTING,       // Khởi động UART cho SIM
  SOS_AT_INIT,        // gửi AT, chờ OK để xác nhận SIM sẵn sàng
  SOS_CMGF_CMD,       // gửi AT+CMGF=1, chờ OK
  SOS_SEND_SMS_CMD,   // gửi AT+CMGS="phone", chờ dấu >
  SOS_SEND_SMS_BODY,  // gửi nội dung SMS + Ctrl+Z
  SOS_WAIT_SMS_OK,    // chờ +CMGS: hoặc OK
  SOS_PRE_CALL,       // chờ rồi gửi AT+CHUP
  SOS_MAKE_CALL,      // gửi ATD, chờ xác nhận
  SOS_IN_CALL,        // đang trong cuộc gọi 30s
  SOS_FINISHED,
  SOS_CLEANUP         // Trả UART về cho GPS
};

static SOSState sos_state = SOS_IDLE;
static String sos_phone = "";
static double sos_lat = 0.0;
static double sos_lng = 0.0;
static bool sos_has_gps = false;
static int sos_at_retry = 0;   // số lần thử gửi AT

static unsigned long state_start_ms = 0;
static unsigned long last_at_send_ms = 0;
static String sim_rx_buffer = "";
static bool sms_success = false;

static void sendATCommand(const String &cmd) {
  GPSSerial.println(cmd);
  last_at_send_ms = millis();
}


void SimModule_Init() {
  // Không cấu hình UART ở đây để GPS chạy bình thường
  sos_state = SOS_IDLE;
}

void SimModule_TriggerSOS(const char* phone, double lat, double lng, bool hasGps) {
  if (sos_state != SOS_IDLE && sos_state != SOS_FINISHED) {
    return; // Đang bận
  }

  if (phone == nullptr || strlen(phone) == 0) {
    return; // Không có số điện thoại
  }

  Serial.println("[SIM] Triggered SOS! Switching GPSSerial to SIM...");
  GPSSerial.end();

  sos_phone = String(phone);
  sos_lat = lat;
  sos_lng = lng;
  sos_has_gps = hasGps;
  sms_success = false;
  sos_at_retry = 0;
  sim_rx_buffer = "";

  state_start_ms = millis();
  sos_state = SOS_STARTING;
}

bool SimModule_IsBusy() {
  return (sos_state != SOS_IDLE && sos_state != SOS_FINISHED);
}

void SimModule_TriggerAlert(const char* phone, double lat, double lng, bool hasGps, const char* message) {
  sos_custom_msg = (message && strlen(message) > 0) ? String(message) : "";
  SimModule_TriggerSOS(phone, lat, lng, hasGps);
}

void SimModule_Loop() {
  if (sos_state == SOS_IDLE) {
    return; // Rảnh, nhường UART2 cho GPS
  }

  // Đọc dữ liệu từ SIM liên tục đưa vào buffer
  while (GPSSerial.available()) {
    char c = GPSSerial.read();
    sim_rx_buffer += c;
    // Giới hạn buffer để tránh tràn bộ nhớ
    if (sim_rx_buffer.length() > 512) {
      sim_rx_buffer = sim_rx_buffer.substring(sim_rx_buffer.length() - 256);
    }
  }

  unsigned long now = millis();

  switch (sos_state) {

    case SOS_STARTING:
      if (now - state_start_ms > 100) {
        GPSSerial.begin(115200, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
        Serial.println("[SIM] UART started at 115200");
        sim_rx_buffer = "";
        sendATCommand("AT");
        state_start_ms = now;
        sos_state = SOS_AT_INIT;
      }
      break;

    // ── AT INIT: xác nhận SIM sẵn sàng ──────────────────────────────────
    case SOS_AT_INIT:
      if (sim_rx_buffer.indexOf("OK") != -1) {
        Serial.println("[SIM] AT OK – SIM ready");
        sim_rx_buffer = "";
        state_start_ms = now;
        sos_state = SOS_CMGF_CMD;
      } else if (now - state_start_ms > 2000) {
        // Chưa nhận OK, thử lại tối đa 3 lần
        sos_at_retry++;
        Serial.printf("[SIM] AT timeout, retry %d\n", sos_at_retry);
        if (sos_at_retry >= 3) {
          // Bỏ qua init, cố gắng tiếp tục
          Serial.println("[SIM] AT init failed, proceeding anyway");
          sim_rx_buffer = "";
          state_start_ms = now;
          sos_state = SOS_CMGF_CMD;
        } else {
          sim_rx_buffer = "";
          sendATCommand("AT");
          state_start_ms = now;
        }
      }
      break;

    // ── SMS: đặt text mode, chờ OK ───────────────────────────────────────
    case SOS_CMGF_CMD:
      if (now - state_start_ms > 100) {
        sim_rx_buffer = "";
        sendATCommand("AT+CMGF=1");
        state_start_ms = now;
        sos_state = SOS_SEND_SMS_CMD;
      }
      break;

    // ── SMS: gửi AT+CMGS, chờ > ──────────────────────────────────────────
    case SOS_SEND_SMS_CMD:
      if (sim_rx_buffer.indexOf("OK") != -1 || now - state_start_ms > 2000) {
        sim_rx_buffer = "";
        GPSSerial.print("AT+CMGS=\"");
        GPSSerial.print(sos_phone);
        GPSSerial.println("\"");
        state_start_ms = now;
        sos_state = SOS_SEND_SMS_BODY;
      }
      break;

    // ── SMS: gửi body khi nhận được > ────────────────────────────────────
    case SOS_SEND_SMS_BODY:
      if (sim_rx_buffer.indexOf(">") != -1 || now - state_start_ms > 3000) {
        sim_rx_buffer = "";

        // Tạo tin nhắn
        String msg = "";
        if (sos_custom_msg.length() > 0) {
          msg = sos_custom_msg + "\n";
          sos_custom_msg = "";
        } else {
          msg = "Canh bao suc khoe!\n";
        }
        if (sos_has_gps) {
          String lat_s = String(sos_lat, 6);
          String lng_s = String(sos_lng, 6);
          msg += "Vi tri (GPS):\nhttp://maps.google.com/?q=" + lat_s + "," + lng_s;
        } else {
          msg += "Khong lay duoc toa do vi tri.";
        }

        GPSSerial.print(msg);
        GPSSerial.write((char)26); // Ctrl+Z gửi SMS
        Serial.println("[SIM] SMS body sent");
        uint32_t sendSMS_time =  millis() - warning_detect_ms;
        Serial.printf("[CALL] Setup time: %lu ms\n",sendSMS_time);
        state_start_ms = now;
        sos_state = SOS_WAIT_SMS_OK;
      }
      break;

    // ── Chờ xác nhận SMS ─────────────────────────────────────────────────
    case SOS_WAIT_SMS_OK:
      if (sim_rx_buffer.indexOf("+CMGS:") != -1) {
        Serial.println("[SIM] SMS sent OK");
        sms_success = true;
        sim_rx_buffer = "";
        state_start_ms = now;
        sos_state = SOS_PRE_CALL;
      } else if (sim_rx_buffer.indexOf("OK") != -1) {
        Serial.println("[SIM] SMS OK");
        sms_success = true;
        sim_rx_buffer = "";
        state_start_ms = now;
        sos_state = SOS_PRE_CALL;
      } else if (sim_rx_buffer.indexOf("ERROR") != -1) {
        // SMS lỗi, vẫn thực hiện cuộc gọi
        Serial.println("[SIM] SMS ERROR – proceeding to call");
        sim_rx_buffer = "";
        state_start_ms = now;
        sos_state = SOS_PRE_CALL;
      } else if (now - state_start_ms > 15000) {
        Serial.println("[SIM] SMS timeout – proceeding to call");
        sim_rx_buffer = "";
        state_start_ms = now;
        sos_state = SOS_PRE_CALL;
      }
      break;

    // ── Chuẩn bị gọi: kết thúc cuộc gọi cũ (nếu có) ────────────────────
    case SOS_PRE_CALL:
      if (now - state_start_ms > 300) {
        sim_rx_buffer = "";
        sendATCommand("AT+CHUP");  // tắt cuộc gọi cũ (nếu có) – không chờ
        state_start_ms = now;
        sos_state = SOS_MAKE_CALL;
      }
      break;

    // ── Thực hiện cuộc gọi ───────────────────────────────────────────────
    case SOS_MAKE_CALL:
      // Chờ 600ms sau AT+CHUP rồi quay số (không dùng delay() blocking)
      if (now - state_start_ms > 600) {
        sim_rx_buffer = "";
        GPSSerial.print("ATD");
        GPSSerial.print(sos_phone);
        GPSSerial.println(";");
        uint32_t call_time =  millis() - warning_detect_ms;
        Serial.printf("[CALL] Setup time: %lu ms\n",call_time);
        Serial.printf("[SIM] Calling %s\n", sos_phone.c_str());
        state_start_ms = now;
        sos_state = SOS_IN_CALL;
      }
      break;

    // ── Trong cuộc gọi – kết thúc sau 30s ───────────────────────────────
    case SOS_IN_CALL:
      if (now - state_start_ms > 30000) {
        sendATCommand("AT+CHUP"); // tắt cuộc gọi sau 30s
        Serial.println("[SIM] Call ended (30s timeout)");
        state_start_ms = now;
        sos_state = SOS_FINISHED;
      }
      break;

    // ── Hoàn tất: trả UART về GPS ────────────────────────────────────────
    case SOS_FINISHED:
      Serial.println("[SIM] SOS finished. Switching GPSSerial back to GPS...");
      GPSSerial.end();
      state_start_ms = now;
      sos_state = SOS_CLEANUP;
      break;

    case SOS_CLEANUP:
      if (now - state_start_ms > 100) {
        GPSSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
        Serial.println("[SIM] UART restored to GPS (9600)");
        sos_state = SOS_IDLE;
      }
      break;

    case SOS_IDLE:
      sim_rx_buffer = "";
      break;
  }
}