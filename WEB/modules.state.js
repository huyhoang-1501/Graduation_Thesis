// Shared state for web modules
let currentPatientId = null;        // patient đang được xem ở Overview/History/Alerts
let patientsCache = {};             // lưu danh sách bệnh nhân để fill dropdown
let alertsRef = null;
let currentAlertsData = {};
