// ========== CONFIG FIREBASE (AUTH + REALTIME DB) ==========
const firebaseConfigAuth = {
  apiKey: "AIzaSyBu0jPAKMiMOw_ZL61ta7vE8CFbozb4f8g",
  authDomain: "graduation-thesis-3a3df.firebaseapp.com",
  databaseURL: "https://graduation-thesis-3a3df-default-rtdb.firebaseio.com",
  projectId: "graduation-thesis-3a3df",
  storageBucket: "graduation-thesis-3a3df.firebasestorage.app",
  messagingSenderId: "394222704990",
  appId: "1:394222704990:web:a1931dca2d24a0e21557cf",
  measurementId: "G-TKZJQCK7Z2"
};

// App default dùng cho Realtime Database
const appCore = firebase.apps.find(a => a.name === "[DEFAULT]")
  || firebase.initializeApp(firebaseConfigAuth);
const db = appCore.database();

// App riêng cho Auth (để sau này tách cấu hình nếu cần)
const appAuth = firebase.apps.find(a => a.name === "authApp")
  || firebase.initializeApp(firebaseConfigAuth, "authApp");
const auth = appAuth.auth();
auth.setPersistence(firebase.auth.Auth.Persistence.SESSION);

const googleProvider = new firebase.auth.GoogleAuthProvider();
googleProvider.setCustomParameters({ prompt: "select_account" });

function makeUserModeId() {
  const min = 10000;
  const max = 99999;
  const span = max - min + 1;

  if (window.crypto?.getRandomValues) {
    const arr = new Uint32Array(1);
    window.crypto.getRandomValues(arr);
    return String(min + (arr[0] % span));
  }

  return String(min + Math.floor(Math.random() * span));
}

function normalizeUserModeId(raw) {
  return String(raw || "").replace(/\D/g, "").slice(0, 5);
}

function normalizeDeviceId(raw) {
  return String(raw || "").trim().toUpperCase();
}

function isValidDeviceId(deviceId) {
  return /^DEV-[0-9A-Z]{8,16}$/.test(deviceId);
}

function getCurrentUserUid() {
  return auth.currentUser?.uid || "";
}

// ========== TOGGLE LOGIN / REGISTER FORM ==========
document.getElementById("show-register")?.addEventListener("click", e => {
  e.preventDefault();
  document.getElementById("login-form").style.display = "none";
  document.getElementById("register-form").style.display = "block";
});

document.getElementById("show-login")?.addEventListener("click", e => {
  e.preventDefault();
  document.getElementById("register-form").style.display = "none";
  document.getElementById("login-form").style.display = "block";
});

// ========== REGISTER ==========
document.getElementById("register-btn")?.addEventListener("click", () => {
  const email = document.getElementById("email-register").value.trim();
  const pass = document.getElementById("password-register").value;

  if (!email || !pass) return alert("Vui lòng nhập đầy đủ!");
  if (pass.length < 6) return alert("Mật khẩu phải ≥ 6 ký tự!");

  auth.createUserWithEmailAndPassword(email, pass)
    .then(() => {
      alert("Đăng ký thành công! Hãy đăng nhập lại.");
      document.getElementById("register-form").style.display = "none";
      document.getElementById("login-form").style.display = "block";
    })
    .catch(err => alert("Lỗi đăng ký: " + err.message));
});

// ========== EMAIL SIGN IN ==========
document.getElementById("email-signin-btn")?.addEventListener("click", () => {
  const email = document.getElementById("email-login").value.trim();
  const pass = document.getElementById("password-login").value;

  if (!email || !pass) return alert("Vui lòng nhập email và mật khẩu!");

  auth.signInWithEmailAndPassword(email, pass)
    .catch(err => alert("Sai email hoặc mật khẩu!"));
});

// ========== GOOGLE SIGN IN ==========
document.getElementById("login-form")?.addEventListener("submit", function (e) {
  e.preventDefault();

  const googleBtn = document.getElementById("google-signin-btn");
  const oldHtml = googleBtn.innerHTML;

  googleBtn.disabled = true;
  googleBtn.innerHTML =
    `<i class="fas fa-spinner fa-spin"></i> Đang mở Google...`;

  auth.signInWithPopup(googleProvider)
    .catch(err => {
      console.error("Google login error:", err);
      googleBtn.disabled = false;
      googleBtn.innerHTML = oldHtml;
      if (err.code !== "auth/popup-closed-by-user") {
        alert("Lỗi Google login: " + err.message);
      }
    });
});

// ========== ON AUTH STATE CHANGED ==========
const loginSection = document.getElementById("login-section");
const appRoot = document.getElementById("app");

auth.onAuthStateChanged(user => {
  if (user) {
    // Đã đăng nhập
    loginSection.classList.remove("show-flex");
    loginSection.classList.add("hidden");
    appRoot.classList.remove("hidden");
    showApp(user);
  } else {
    // Chưa đăng nhập
    appRoot.classList.add("hidden");
    loginSection.classList.add("show-flex");
    loginSection.classList.remove("hidden");
  }
});

// ========== SHOW APP DASHBOARD ==========
function showApp(user) {
  const app = document.getElementById("app");
  app.classList.remove("hidden");

  const email = user?.email || auth.currentUser?.email || "";
  const nameFromEmail = email ? email.split("@")[0] : "";

  document.getElementById("current-user-email").textContent = email;
  document.getElementById("sidebar-user-name").textContent =
    nameFromEmail || "Người dùng";

  initSidebarNavigation();
  initOverview();
  initOtherPages();
  initPatientSelectors();
  initDeviceBindingModule();
  initPatientsModule(); 
  initSettingsModule();
}

// ========== LOGOUT ==========
function logout() {
  auth.signOut()
    .then(() => {
      const emailLogin = document.getElementById("email-login");
      const passLogin = document.getElementById("password-login");
      if (emailLogin) emailLogin.value = "";
      if (passLogin) passLogin.value = "";

      const googleBtn = document.getElementById("google-signin-btn");
      if (googleBtn) {
        googleBtn.disabled = false;
        googleBtn.innerHTML =
          `<i class="fab fa-google" style="margin-right:12px;"></i> Đăng nhập bằng Google`;
      }
    })
    .catch(err => alert("Lỗi đăng xuất: " + err.message));
}

// ========== SIDEBAR & TAB SWITCH ==========
function initSidebarNavigation() {
  const sideItems = document.querySelectorAll(".sidebar-item");
  const pages = document.querySelectorAll(".page");
  const tabTitle = document.getElementById("main-tab-title");

  const titleMap = {
    overview: "TỔNG QUAN",
    history: "LỊCH SỬ",
    alerts: "CẢNH BÁO",
    patients: "THIẾT BỊ / BỆNH NHÂN",
    settings: "CÀI ĐẶT"
  };

  sideItems.forEach(btn => {
    btn.addEventListener("click", () => {
      sideItems.forEach(b => b.classList.remove("active"));
      btn.classList.add("active");

      const pageName = btn.dataset.page;

      showPage(pageName);

      if (tabTitle && titleMap[pageName]) {
        tabTitle.textContent = titleMap[pageName];
      }
    });
  });

  function showPage(pageName) {
    pages.forEach(p => p.classList.remove("active"));
    const target = document.getElementById("page-" + pageName);
    if (target) target.classList.add("active");
  }
}

// ========== OVERVIEW: CHART + MAP ==========
let miniChart;
let leafletMap;
let leafletMarker;

function initOverview() {
  initMiniChart();
  initMap();
}

function initDeviceBindingModule() {
  const deviceIdInput = document.getElementById("bind-device-id");
  const pairCodeInput = document.getElementById("bind-pair-code");
  const createBtn = document.getElementById("device-create-btn");
  const linkBtn = document.getElementById("device-link-btn");
  const statusEl = document.getElementById("device-bind-status");

  if (!deviceIdInput || !pairCodeInput || !createBtn || !linkBtn || !statusEl) return;

  // DEVICE ID nhập theo dạng DEV-... (không giới hạn 5 số)
  deviceIdInput.addEventListener("input", (e) => {
    e.target.value = normalizeDeviceId(e.target.value);
  });
  deviceIdInput.removeAttribute("inputmode");
  deviceIdInput.removeAttribute("maxlength");

  // User mode ID chỉ 5 số
  pairCodeInput.addEventListener("input", (e) => {
    e.target.value = normalizeUserModeId(e.target.value);
  });
  pairCodeInput.setAttribute("inputmode", "numeric");
  pairCodeInput.setAttribute("maxlength", "5");

  pairCodeInput.placeholder = "Mã 5 số cho User mode";
  statusEl.textContent = "Bước 1: Nhập DEVICE ID dạng DEV-... Bước 2: Tạo mã User mode 5 số và nhập mã đó trên thiết bị.";
  statusEl.style.color = "#6b7280";

  createBtn.addEventListener("click", async () => {
    const deviceId = normalizeDeviceId(deviceIdInput.value);
    let patientId = normalizeUserModeId(pairCodeInput.value);
    const ownerUid = getCurrentUserUid();

    if (!deviceId) {
      alert("Vui lòng nhập DEVICE ID (ví dụ: DEV-6C068C8E720).");
      return;
    }

    if (!isValidDeviceId(deviceId)) {
      alert("DEVICE ID không hợp lệ. Định dạng mong muốn: DEV-XXXXXXXX (chữ/số).");
      return;
    }

    if (patientId.length !== 5) {
      patientId = makeUserModeId();
    }

    if (!ownerUid) {
      alert("Phiên đăng nhập không hợp lệ. Vui lòng đăng nhập lại.");
      return;
    }

    deviceIdInput.value = deviceId;
    pairCodeInput.value = patientId;

    try {
      const deviceSnap = await db.ref("devices/" + deviceId).once("value");
      const oldDevice = deviceSnap.val();
      if (oldDevice?.ownerUid && oldDevice.ownerUid !== ownerUid) {
        alert("Mã thiết bị này đang thuộc tài khoản khác.");
        return;
      }

      await db.ref("devices/" + deviceId).set({
        deviceId,
        ownerUid,
        patientId,
        status: "ready_for_user_mode",
        linked: true,
        mode: "user",
        createdAt: firebase.database.ServerValue.TIMESTAMP
      });

      const patientSnap = await db.ref("patients/" + patientId).once("value");
      const oldPatient = patientSnap.val();
      if (oldPatient?.ownerUid && oldPatient.ownerUid !== ownerUid) {
        alert("Mã bệnh nhân này đang thuộc tài khoản khác.");
        return;
      }

      await db.ref("patients/" + patientId).set({
        name: oldPatient?.name || ("Bệnh nhân " + patientId.slice(-4)),
        age: oldPatient?.age || "",
        sex: oldPatient?.sex || "Nam",
        deviceId,
        ownerUid,
        mode: "user",
        status: oldPatient?.status || "offline",
        updatedAt: firebase.database.ServerValue.TIMESTAMP
      });

      statusEl.textContent = "Đã lưu DEVICE ID " + deviceId + " và tạo mã User mode: " + patientId + ". Hãy nhập mã 5 số này trên thiết bị.";
      statusEl.style.color = "#16a34a";
    } catch (err) {
      console.error(err);
      alert("Lỗi tạo thiết bị: " + err.message);
    }
  });

  linkBtn.addEventListener("click", async () => {
    const deviceId = normalizeDeviceId(deviceIdInput.value);
    const patientId = normalizeUserModeId(pairCodeInput.value);

    if (!deviceId) {
      alert("Hãy nhập DEVICE ID trước.");
      return;
    }

    if (!isValidDeviceId(deviceId)) {
      alert("DEVICE ID không hợp lệ. Định dạng mong muốn: DEV-XXXXXXXX (chữ/số).");
      return;
    }

    if (patientId.length !== 5) {
      alert("Mã User mode phải đúng 5 số. Hãy bấm Lưu DEVICE ID để tạo mã.");
      return;
    }

    const ownerUid = getCurrentUserUid();

    try {
      const deviceSnap = await db.ref("devices/" + deviceId).once("value");
      const deviceData = deviceSnap.val();
      if (!deviceData) {
        alert("Mã chưa được tạo trên dashboard.");
        return;
      }
      if (deviceData.ownerUid && deviceData.ownerUid !== ownerUid) {
        alert("Mã này thuộc tài khoản khác.");
        return;
      }

      await db.ref("devices/" + deviceId).update({
        ownerUid,
        patientId,
        status: "linked",
        linked: true,
        mode: "user",
        updatedAt: firebase.database.ServerValue.TIMESTAMP
      });

      pairCodeInput.value = patientId;
      statusEl.textContent = "Đã xác nhận User mode cho ID " + patientId + ". Thiết bị sẽ đẩy dữ liệu lên dashboard của tài khoản này.";
      statusEl.style.color = "#2563eb";
    } catch (err) {
      console.error(err);
      alert("Lỗi xác nhận thiết bị: " + err.message);
    }
  });
}

function initMiniChart() {
  const ctx = document.getElementById("miniChart")?.getContext("2d");
  if (!ctx) return;

  miniChart = new Chart(ctx, {
    type: "line",
    data: {
      labels: [],
      datasets: [{
        label: "Heart Rate (bpm)",
        data: [],
        borderColor: "#ef4444",
        backgroundColor: "rgba(239,68,68,0.15)",
        fill: true,
        tension: 0.3
      }]
    },
    options: {
      responsive: true,
      scales: {
        x: { display: false },
        y: { beginAtZero: false }
      }
    }
  });
}

function initMap() {
  const mapDiv = document.getElementById("map");
  if (!mapDiv) return;

  leafletMap = L.map(mapDiv).setView([10.85, 106.77], 15);
  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
    attribution: "&copy; OpenStreetMap"
  }).addTo(leafletMap);

  leafletMarker = L.marker([10.85, 106.77]).addTo(leafletMap);
}

function mockUpdateOverview() {
  document.getElementById("ov-hr-value").textContent = "78 bpm";
  document.getElementById("ov-bp-value").textContent = "120 / 80 mmHg";
  document.getElementById("ov-spo2-value").textContent = "97 %";

  document.getElementById("ov-hr-status").textContent = "Trạng thái: Bình thường";
  document.getElementById("ov-bp-status").textContent = "Trạng thái: Bình thường";
  document.getElementById("ov-spo2-status").textContent = "Trạng thái: Bình thường";

  const badge = document.getElementById("ov-device-badge");
  badge.textContent = "ONLINE";
  badge.classList.remove("badge-offline", "badge-stale");
  badge.classList.add("badge-online");

  document.getElementById("ov-lastseen").textContent = "Cách đây 1 phút";
  document.getElementById("ov-battery").textContent = "85 %";
  document.getElementById("ov-network").textContent = "WiFi";
  document.getElementById("ov-freshness").textContent = "Dữ liệu mới";

  // Ô thông báo
  const notifyCountEl = document.getElementById("ov-notify-count");
  const notifyTextEl = document.getElementById("ov-notify-text");
  if (notifyCountEl && notifyTextEl) {
    const newAlerts = 2; // demo
    notifyCountEl.textContent = newAlerts;
    notifyTextEl.textContent = newAlerts > 0
      ? "Có " + newAlerts + " cảnh báo chưa xem"
      : "Không có cảnh báo mới";
  }

  if (miniChart) {
    const labels = [];
    const data = [];
    for (let i = 0; i < 12; i++) {
      labels.push("");
      data.push(70 + Math.round(Math.random() * 10));
    }
    miniChart.data.labels = labels;
    miniChart.data.datasets[0].data = data;
    miniChart.update();
  }

  if (leafletMap && leafletMarker) {
    const lat = 10.85, lng = 106.77;
    leafletMarker.setLatLng([lat, lng]);
    leafletMap.setView([lat, lng], 15);
  }
}

// Helper cập nhật vị trí trên map theo data từ Realtime Database
function updateMapLocation(lat, lng) {
  if (!leafletMap || !leafletMarker) return;
  leafletMarker.setLatLng([lat, lng]);
  leafletMap.setView([lat, lng], 15);
}

// ========== HISTORY: (tạm thời) mock UI, có thể nâng cấp sau ==========
function initOtherPages() {
  renderMockHistory();
}

function renderMockHistory() {
  const tbody = document.getElementById("history-table-body");
  if (!tbody) return;
  tbody.innerHTML = "";

  const mockRows = [
    { time: "21:30:15", hr: 78, sys: 120, dia: 80, spo2: 97 },
    { time: "21:31:10", hr: 80, sys: 122, dia: 82, spo2: 96 },
    { time: "21:32:05", hr: 76, sys: 118, dia: 78, spo2: 98 }
  ];

  mockRows.forEach(r => {
    const tr = document.createElement("tr");
    tr.innerHTML = `
      <td>${r.time}</td>
      <td>${r.hr}</td>
      <td>${r.sys}</td>
      <td>${r.dia}</td>
      <td>${r.spo2}</td>
    `;
    tbody.appendChild(tr);
  });

  const ctx = document.getElementById("historyChart")?.getContext("2d");
  if (!ctx) return;
  new Chart(ctx, {
    type: "line",
    data: {
      labels: mockRows.map(r => r.time),
      datasets: [{
        label: "Heart Rate (bpm)",
        data: mockRows.map(r => r.hr),
        borderColor: "#3b82f6",
        backgroundColor: "rgba(59,130,246,0.2)",
        fill: true,
        tension: 0.3
      }]
    },
    options: {
      responsive: true,
      scales: {
        y: { beginAtZero: false }
      }
    }
  });
}

// ========== PATIENT SELECTORS (Overview / History / Alerts) ==========
// Các module nặng đã tách sang:
// - modules.state.js
// - modules.selectors.js
// - modules.alerts.js
// - modules.patients.js
// - modules.settings.js