#include "sim_module.h"

#define SIM_RX_PIN 16
#define SIM_TX_PIN 17
#define GPS_RX_PIN 25
#define GPS_TX_PIN 26

// Chia sẻ UART2 với GPS
extern HardwareSerial GPSSerial;

// APN của nhà mạng dùng cho LBS định vị (mặc định Viettel v-internet)
const String APN = "v-internet";

// Các trạng thái của máy trạng thái SOS
enum SOSState {
  SOS_IDLE,
  SOS_SETUP_LBS_1,
  SOS_SETUP_LBS_2,
  SOS_SETUP_LBS_3,
  SOS_SETUP_LBS_4,
  SOS_GET_LBS,
  SOS_SEND_SMS_CMD,
  SOS_SEND_SMS_BODY,
  SOS_WAIT_SMS_OK,
  SOS_MAKE_CALL,
  SOS_IN_CALL,
  SOS_FINISHED
};

static SOSState sos_state = SOS_IDLE;
static String sos_phone = "";
static double sos_lat = 0.0;
static double sos_lng = 0.0;
static bool sos_has_gps = false;
static String sos_lbs_url = "";

static unsigned long state_start_ms = 0;
static unsigned long last_at_send_ms = 0;
static String sim_rx_buffer = "";
static bool sms_success = false;

// Gửi lệnh AT phi blocking
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

  Serial.println("[SIM] Triggered SOS! Switching GPSSerial to SIM (115200 baud, pins 16,17)...");
  GPSSerial.end();
  delay(100);
  GPSSerial.begin(115200, SERIAL_8N1, SIM_RX_PIN, SIM_TX_PIN);
  delay(100);

  sos_phone = String(phone);
  sos_lat = lat;
  sos_lng = lng;
  sos_has_gps = hasGps;
  sos_lbs_url = "";
  sms_success = false;

  state_start_ms = millis();
  
  // Gửi lệnh AT đầu tiên để kích hoạt kết nối SIM
  sendATCommand("AT");
  
  if (sos_has_gps) {
    // Đã có GPS, bỏ qua LBS, nhảy thẳng sang gửi SMS
    sos_state = SOS_SEND_SMS_CMD;
  } else {
    // Chưa có GPS, khởi động LBS để lấy vị trí
    sos_state = SOS_SETUP_LBS_1;
  }
}

bool SimModule_IsBusy() {
  return (sos_state != SOS_IDLE && sos_state != SOS_FINISHED);
}

void SimModule_Loop() {
  if (sos_state == SOS_IDLE) {
    return; // Rảnh, nhường UART2 cho GPS
  }

  // Đọc dữ liệu từ SIM liên tục đưa vào buffer
  while (GPSSerial.available()) {
    char c = GPSSerial.read();
    sim_rx_buffer += c;
  }

  // Máy trạng thái xử lý SOS
  unsigned long now = millis();
  
  switch (sos_state) {
    case SOS_IDLE:
      sim_rx_buffer = "";
      break;

    case SOS_SETUP_LBS_1:
      // AT+SAPBR=3,1,"CONTYPE","GPRS"
      if (now - state_start_ms > 100) {
        sim_rx_buffer = "";
        sendATCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"");
        state_start_ms = now;
        sos_state = SOS_SETUP_LBS_2;
      }
      break;

    case SOS_SETUP_LBS_2:
      // Chờ phản hồi OK từ LBS_1 (tối đa 2 giây)
      if (sim_rx_buffer.indexOf("OK") != -1) {
        sim_rx_buffer = "";
        sendATCommand("AT+SAPBR=3,1,\"APN\",\"" + APN + "\"");
        state_start_ms = now;
        sos_state = SOS_SETUP_LBS_3;
      } else if (now - state_start_ms > 2000) {
        // Hết thời gian chờ, cố gắng chuyển tiếp
        sim_rx_buffer = "";
        sendATCommand("AT+SAPBR=3,1,\"APN\",\"" + APN + "\"");
        state_start_ms = now;
        sos_state = SOS_SETUP_LBS_3;
      }
      break;

    case SOS_SETUP_LBS_3:
      // Chờ phản hồi OK từ LBS_2
      if (sim_rx_buffer.indexOf("OK") != -1) {
        sim_rx_buffer = "";
        sendATCommand("AT+SAPBR=1,1");
        state_start_ms = now;
        sos_state = SOS_SETUP_LBS_4;
      } else if (now - state_start_ms > 2000) {
        sim_rx_buffer = "";
        sendATCommand("AT+SAPBR=1,1");
        state_start_ms = now;
        sos_state = SOS_SETUP_LBS_4;
      }
      break;

    case SOS_SETUP_LBS_4:
      // Chờ phản hồi OK từ LBS_3 (mở kết nối internet)
      if (sim_rx_buffer.indexOf("OK") != -1 || sim_rx_buffer.indexOf("ERROR") != -1) {
        sim_rx_buffer = "";
        sendATCommand("AT+CLBS=1,1");
        state_start_ms = now;
        sos_state = SOS_GET_LBS;
      } else if (now - state_start_ms > 3000) {
        sim_rx_buffer = "";
        sendATCommand("AT+CLBS=1,1");
        state_start_ms = now;
        sos_state = SOS_GET_LBS;
      }
      break;

    case SOS_GET_LBS:
      // Đợi lấy vị trí từ CLBS (tối đa 5 giây)
      {
        int index = sim_rx_buffer.indexOf("+CLBS: 0,");
        if (index != -1) {
          String data = sim_rx_buffer.substring(index + 9);
          int firstComma = data.indexOf(',');
          int secondComma = data.indexOf(',', firstComma + 1);
          if (firstComma != -1 && secondComma != -1) {
            String lat = data.substring(0, firstComma);
            String lng = data.substring(firstComma + 1, secondComma);
            sos_lbs_url = "http://maps.google.com/?q=" + lat + "," + lng;
          }
          sim_rx_buffer = "";
          state_start_ms = now;
          sos_state = SOS_SEND_SMS_CMD;
        } else if (now - state_start_ms > 5000) {
          // LBS thất bại
          sim_rx_buffer = "";
          state_start_ms = now;
          sos_state = SOS_SEND_SMS_CMD;
        }
      }
      break;

    case SOS_SEND_SMS_CMD:
      // Cấu hình SMS text mode + bắt đầu gửi SMS
      if (now - state_start_ms > 100) {
        sendATCommand("AT+CMGF=1");
        delay(100);
        sim_rx_buffer = "";
        GPSSerial.print("AT+CMGS=\"");
        GPSSerial.print(sos_phone);
        GPSSerial.println("\"");
        state_start_ms = now;
        sos_state = SOS_SEND_SMS_BODY;
      }
      break;

    case SOS_SEND_SMS_BODY:
      // Chờ dấu '>' từ SIM module để gửi nội dung SMS
      if (sim_rx_buffer.indexOf(">") != -1 || now - state_start_ms > 1500) {
        sim_rx_buffer = "";
        
        // Tạo tin nhắn cảnh báo
        String msg = "Canh bao suc khoe!\n";
        if (sos_has_gps) {
          String lat_s = String(sos_lat, 6);
          String lng_s = String(sos_lng, 6);
          msg += "SOS! Vi tri cua toi (GPS):\nhttp://maps.google.com/?q=" + lat_s + "," + lng_s;
        } else if (sos_lbs_url.length() > 0) {
          msg += "SOS! Vi tri cua toi (LBS):\n" + sos_lbs_url;
        } else {
          msg += "SOS! Khong lay duoc toa do vi tri.";
        }

        GPSSerial.print(msg);
        GPSSerial.write((char)26); // Ký tự Ctrl+Z để gửi
        state_start_ms = now;
        sos_state = SOS_WAIT_SMS_OK;
      }
      break;

    case SOS_WAIT_SMS_OK:
      // Chờ xác nhận SMS thành công (tối đa 10 giây)
      if (sim_rx_buffer.indexOf("+CMGS:") != -1 || sim_rx_buffer.indexOf("OK") != -1) {
        sim_rx_buffer = "";
        sos_state = SOS_MAKE_CALL;
        state_start_ms = now;
      } else if (now - state_start_ms > 10000) {
        // Hết thời gian chờ SMS
        sim_rx_buffer = "";
        sos_state = SOS_MAKE_CALL;
        state_start_ms = now;
      }
      break;

    case SOS_MAKE_CALL:
      // Thực hiện cuộc gọi khẩn cấp
      if (now - state_start_ms > 500) {
        // Tắt cuộc gọi cũ trước
        GPSSerial.println("AT+CHUP");
        delay(500);
        sim_rx_buffer = "";
        // Thực hiện cuộc gọi
        GPSSerial.print("ATD");
        GPSSerial.print(sos_phone);
        GPSSerial.println(";");
        state_start_ms = now;
        sos_state = SOS_IN_CALL;
      }
      break;

    case SOS_IN_CALL:
      // Cuộc gọi đã được kích hoạt, cho phép kết thúc sau 30 giây
      if (now - state_start_ms > 30000) {
        sos_state = SOS_FINISHED;
      }
      break;

    case SOS_FINISHED:
      // Trả UART2 về cho GPS
      Serial.println("[SIM] SOS finished. Switching GPSSerial back to GPS (9600 baud, pins 25,26)...");
      GPSSerial.end();
      delay(100);
      GPSSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
      delay(100);
      
      sos_state = SOS_IDLE;
      break;
  }
}
