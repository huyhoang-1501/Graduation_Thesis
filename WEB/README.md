# Các điểm cần lưu ý về giao diện Website của đồ án:

## 1/ Mục tiêu của đồ án (đúng ngữ cảnh người cao tuổi)
 - Caregiver theo dõi sức khỏe từ xa theo thời gian thực.
 - Có lịch sử để bác sĩ/caregiver xem lại theo ngày/tuần.
 - Có cảnh báo sớm khi bất thường và khi thiết bị “mất tín hiệu”.
 - Có vị trí gần nhất (phục vụ tình huống khẩn cấp)
 
## 2/ Các chức năng cần triển khai
 - Must-have (bắt buộc có)
 - Auth + phân quyền (Firebase Auth + Security Rules): user chỉ thấy device mình link.
 - Link thiết bị bằng DeviceID/QR: thao tác cực thực tế.
 - Realtime dashboard: latest HR/BP + trạng thái thiết bị (lastSeen, pin).
 - History + filter + export: chứng minh hệ thống có lưu trữ và truy vết.
 - Cảnh báo:
   --> Vượt ngưỡng HR/BP
   --> Thiết bị offline (mất kết nối)
  - Thống kê tuần/tháng(cái này nếu còn time thì làm)
  
## 3/ Điểm nhấn
 - Device health / reliability
  --> Có lastSeen + “offline after X minutes”.
  --> Có biểu đồ/nhãn “data freshness”.
  
 - Data model rõ ràng để scale
  --> Một người chăm sóc quản lý nhiều thiết bị/patient.
  --> Lưu latest riêng để load nhanh, history phân vùng theo ngày để nhẹ.
  
## 4/ UI/UX
 - Sidebar
   --> Overview
   --> History
   --> Alerts
   --> Devices/Patients
   --> Settings
   
 - Overview (làm phải clear và đầy đủ các nội dung)
   --> Card: HR / BP / SpO2 (nếu có)
   --> Badge trạng thái: Normal/Warning/Danger
   --> Last seen / Pin / Network
   --> Map vị trí gần nhất
   --> Mini chart 1h
   
 - History
  --> Lọc ngày + loại dữ liệu
  --> Chart lớn + table
  --> Export CSV/Excel
  
 - Alerts
  --> List + severity + ack (“Đã xem”)
  --> Link sang thời điểm dữ liệu tương ứng (click alert → mở history đúng timestamp)
  --> Cái “click alert → nhảy đúng thời điểm
  
## 5/ Công nghệ
 - Frontend: Bootstrap + Chart.js
 - DB: Firestore or Realtime Database (query theo thời gian dễ hơn cho lịch sử)
 - Realtime: dùng snapshot listener cho latest (cũng realtime)
 - Map: Leaflet + OpenStreetMap (free)