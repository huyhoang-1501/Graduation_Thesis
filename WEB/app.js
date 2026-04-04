// ========== CONFIG FIREBASE AUTH ==========
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

const appAuth = firebase.initializeApp(firebaseConfigAuth, "authApp");
const auth = appAuth.auth();
auth.setPersistence(firebase.auth.Auth.Persistence.SESSION);

const googleProvider = new firebase.auth.GoogleAuthProvider();
googleProvider.setCustomParameters({ prompt: 'select_account' });

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
  const pass  = document.getElementById("password-register").value;

  if (!email || !pass) return alert("Vui lòng nhập đầy đủ!");
  if (pass.length < 6) return alert("Mật khẩu phải ≥ 6 ký tự!");

  auth.createUserWithEmailAndPassword(email, pass)
    .then(() => {
      alert("Đăng ký thành công! Hãy đăng nhập lại.");
    })
    .catch(err => alert("Lỗi đăng ký: " + err.message));
});

// ========== EMAIL SIGN IN ==========
document.getElementById("email-signin-btn")?.addEventListener("click", () => {
  const email = document.getElementById("email-login").value.trim();
  const pass  = document.getElementById("password-login").value;

  if (!email || !pass) return alert("Vui lòng nhập email và mật khẩu!");

  auth.signInWithEmailAndPassword(email, pass)
    .catch(err => alert("Sai email hoặc mật khẩu!"));
});

// ========== GOOGLE SIGN IN ==========
document.getElementById("login-form")?.addEventListener("submit", function(e) {
  e.preventDefault();

  const googleBtn = document.getElementById("google-signin-btn");
  const oldHtml   = googleBtn.innerHTML;

  googleBtn.disabled = true;
  googleBtn.innerHTML =
    `<i class="fas fa-spinner fa-spin"></i> Đang mở Google...`;

  auth.signInWithPopup(googleProvider)
    .catch(err => {
      console.error("Google login error:", err);
      googleBtn.disabled = false;
      googleBtn.innerHTML = oldHtml;
      if (err.code !== 'auth/popup-closed-by-user') {
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
    nameFromEmail || "Người d��ng";

  initSidebarNavigation();
  initOverview();
}

// ========== LOGOUT ==========
function logout() {
  auth.signOut()
    .then(() => {
      const emailLogin = document.getElementById('email-login');
      const passLogin  = document.getElementById('password-login');
      if (emailLogin) emailLogin.value = '';
      if (passLogin)  passLogin.value  = '';

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

  sideItems.forEach(btn => {
    btn.addEventListener("click", () => {
      sideItems.forEach(b => b.classList.remove("active"));
      btn.classList.add("active");
      showPage(btn.dataset.page);
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
  mockUpdateOverview();
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
    attribution: '&copy; OpenStreetMap'
  }).addTo(leafletMap);

  leafletMarker = L.marker([10.85, 106.77]).addTo(leafletMap);
}

function mockUpdateOverview() {
  document.getElementById("ov-hr-value").textContent    = "78 bpm";
  document.getElementById("ov-bp-value").textContent    = "120 / 80 mmHg";
  document.getElementById("ov-spo2-value").textContent  = "97 %";

  document.getElementById("ov-hr-status").textContent   = "Trạng thái: Bình thường";
  document.getElementById("ov-bp-status").textContent   = "Trạng thái: Bình thường";
  document.getElementById("ov-spo2-status").textContent = "Trạng thái: Bình thường";

  const badge = document.getElementById("ov-device-badge");
  badge.textContent = "ONLINE";
  badge.classList.remove("badge-offline","badge-stale");
  badge.classList.add("badge-online");

  document.getElementById("ov-lastseen").textContent  = "Cách đây 1 phút";
  document.getElementById("ov-battery").textContent   = "85 %";
  document.getElementById("ov-network").textContent   = "WiFi";
  document.getElementById("ov-freshness").textContent = "Dữ liệu mới";

  if (miniChart) {
    const labels = [];
    const data   = [];
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
// ========== MOCK DATA & UI FOR OTHER PAGES ==========

function initOtherPages() {
  renderMockHistory();
  renderMockAlerts();
  renderMockPatients();
 /* fillPersonalInfo();*/
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

  // History chart demo (1 series HR)
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

function renderMockAlerts() {
  const container = document.getElementById("alerts-list");
  if (!container) return;
  container.innerHTML = "";

  const mocks = [
    {
      type: "HR_HIGH",
      severity: "danger",
      message: "HR high: 110 bpm",
      time: "2026-04-01 21:32:10",
      status: "unacked"
    },
    {
      type: "DEVICE_OFFLINE",
      severity: "warning",
      message: "Device offline > 5 minutes",
      time: "2026-04-01 20:05:00",
      status: "acked"
    }
  ];

  mocks.forEach(a => {
    const div = document.createElement("div");
    div.className = "alert-item";
    const iconClass = a.severity === "danger"
      ? "fa-triangle-exclamation" : "fa-circle-exclamation";
    const badgeClass = a.severity === "danger" ? "badge-offline" : "badge-stale";
    const statusText = a.status === "unacked" ? "Chưa xem" : "Đã xem";

    div.innerHTML = `
      <div class="alert-main">
        <div class="alert-icon">
          <i class="fas ${iconClass}"></i>
        </div>
        <div class="alert-content">
          <div><span class="badge ${badgeClass}">${a.severity.toUpperCase()}</span></div>
          <div>${a.message}</div>
          <div>Time: ${a.time}</div>
          <div>Type: ${a.type}</div>
          <div>Status: ${statusText}</div>
        </div>
      </div>
      <div class="alert-actions">
        <button class="btn-ghost">View in History</button>
        <button class="btn-ghost">Mark as Ack</button>
      </div>
    `;
    container.appendChild(div);
  });
}

function renderMockPatients() {
  const container = document.getElementById("patients-list");
  if (!container) return;
  container.innerHTML = "";

  const mocks = [
    { name: "Nguyễn Văn A", age: 72, sex: "Nam", deviceId: "DEV001", status: "online" },
    { name: "Trần Thị B", age: 68, sex: "Nữ", deviceId: "DEV002", status: "offline" }
  ];

  mocks.forEach(p => {
    const row = document.createElement("div");
    row.className = "patient-row";
    const statusBadge = p.status === "online" ? "badge-online" : "badge-offline";

    row.innerHTML = `
      <div class="patient-info">
        <div><strong>${p.name}</strong> (${p.age}, ${p.sex})</div>
        <div>DeviceId: ${p.deviceId}</div>
        <div>Trạng thái: <span class="badge ${statusBadge}">${p.status.toUpperCase()}</span></div>
      </div>
      <div class="patient-actions">
        <button class="btn-ghost">View Overview</button>
        <button class="btn-ghost">Edit</button>
      </div>
    `;
    container.appendChild(row);
  });
}

/*function fillPersonalInfo() {
  const email = auth.currentUser?.email || "";
  const el = document.getElementById("pi-email");
  if (el) el.textContent = email;
}*/