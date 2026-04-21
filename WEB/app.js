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

function makeDeviceId() {
  const bytes = new Uint8Array(6);
  if (window.crypto?.getRandomValues) {
    window.crypto.getRandomValues(bytes);
  } else {
    for (let i = 0; i < bytes.length; i++) bytes[i] = Math.floor(Math.random() * 256);
  }

  return "DEV-" + Array.from(bytes, b => b.toString(16).padStart(2, "0")).join("").toUpperCase();
}

function makePairCode() {
  return String(Math.floor(100000 + Math.random() * 900000));
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

  createBtn.addEventListener("click", async () => {
    const deviceId = deviceIdInput.value.trim() || makeDeviceId();
    const ownerUid = getCurrentUserUid();
    const patientId = currentPatientId || deviceId;

    deviceIdInput.value = deviceId;
    pairCodeInput.value = "";

    try {
      await db.ref("devices/" + deviceId).set({
        deviceId,
        ownerUid,
        patientId,
        status: "created",
        linked: false,
        createdAt: firebase.database.ServerValue.TIMESTAMP
      });

      statusEl.textContent = "Đã tạo thiết bị " + deviceId + " (patientId gợi ý: " + patientId + ").";
      statusEl.style.color = "#16a34a";
    } catch (err) {
      console.error(err);
      alert("Lỗi tạo thiết bị: " + err.message);
    }
  });

  linkBtn.addEventListener("click", async () => {
    const deviceId = deviceIdInput.value.trim();
    if (!deviceId) {
      alert("Hãy tạo hoặc nhập Device ID trước.");
      return;
    }

    const ownerUid = getCurrentUserUid();
    const patientId = currentPatientId || deviceId;
    const pairCode = makePairCode();
    pairCodeInput.value = pairCode;

    try {
      await db.ref("pairings/" + pairCode).set({
        pairCode,
        deviceId,
        ownerUid,
        patientId,
        status: "pending",
        createdAt: firebase.database.ServerValue.TIMESTAMP
      });

      await db.ref("devices/" + deviceId).update({
        ownerUid,
        patientId,
        pairCode,
        status: "waiting_pair",
        linked: false,
        updatedAt: firebase.database.ServerValue.TIMESTAMP
      });

      statusEl.textContent = "Dashboard đã sinh Pair Code: " + pairCode + ". Hãy nhập mã này lên device để hoàn tất.";
      statusEl.style.color = "#2563eb";
    } catch (err) {
      console.error(err);
      alert("Lỗi liên kết thiết bị: " + err.message);
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
function initPatientSelectors() {
  const ovSel  = document.getElementById("ov-patient-select");
  const hisSel = document.getElementById("his-patient-select");
  const alSel  = document.getElementById("al-patient-select");
  const setSel = document.getElementById("set-patient-select");

  function syncAllPatientSelectors(selectedPid, sourceSel) {
    [ovSel, hisSel, alSel, setSel].forEach(other => {
      if (other && other !== sourceSel) other.value = selectedPid;
    });
  }

  function attachHandler(sel, options = {}) {
    if (!sel) return;
    sel.addEventListener("change", () => {
      const pid = sel.value;
      if (!pid) return;

      if (options.keepCurrentPage) {
        currentPatientId = pid;

        const p = patientsCache[pid] || {};
        const displayName = p.name || pid;
        const ovName = document.getElementById("ov-patient-name");
        const hisName = document.getElementById("his-patient-name");
        const alName = document.getElementById("al-patient-name");
        if (ovName) ovName.textContent = displayName;
        if (hisName) hisName.textContent = displayName;
        if (alName) alName.textContent = displayName;

        if (window.loadThresholdsForCurrentPatient) {
          window.loadThresholdsForCurrentPatient();
        }
        if (window.loadPhoneForCurrentPatient) {
          window.loadPhoneForCurrentPatient();
        }
      } else {
        // Khi chọn bệnh nhân ở Overview/History/Alerts, hiển thị Overview cho bệnh nhân đó
        showOverviewForPatient(pid);
      }

      // Đồng bộ value giữa các dropdown
      syncAllPatientSelectors(pid, sel);
    });
  }

  attachHandler(ovSel);
  attachHandler(hisSel);
  attachHandler(alSel);
  attachHandler(setSel, { keepCurrentPage: true });
}

function refreshPatientDropdowns(patientsData) {
  const ovSel  = document.getElementById("ov-patient-select");
  const hisSel = document.getElementById("his-patient-select");
  const alSel  = document.getElementById("al-patient-select");
  const setSel = document.getElementById("set-patient-select");

  const sels = [ovSel, hisSel, alSel, setSel];
  if (!sels.some(Boolean)) return;

  sels.forEach(sel => {
    if (!sel) return;

    const current = sel.value; // cố gắng giữ lựa chọn hiện tại
    sel.innerHTML = "";

    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Chọn bệnh nhân...";
    placeholder.disabled = true;
    sel.appendChild(placeholder);

    Object.keys(patientsData).forEach(pid => {
      const p = patientsData[pid] || {};
      const opt = document.createElement("option");
      opt.value = pid;
      opt.textContent = p.name || pid;
      if (pid === current) opt.selected = true;
      sel.appendChild(opt);
    });

    // Nếu current rỗng hoặc không còn tồn tại thì để placeholder được chọn
    if (!sel.value) {
      placeholder.selected = true;
    }
  });
}
// ========== PATIENTS: CRUD cơ bản ==========
let currentPatientId = null;        // patient đang được xem ở Overview/History/Alerts
let patientsCache = {};             // lưu danh sách bệnh nhân để fill dropdown

function  initPatientsModule() {
  const listEl   = document.getElementById("patients-list");
  const addBtn   = document.getElementById("pt-add-btn");
  const nameEl   = document.getElementById("pt-name");
  const ageEl    = document.getElementById("pt-age");
  const sexEl    = document.getElementById("pt-sex");
  const devEl    = document.getElementById("pt-device-id");
  const statusEl = document.getElementById("pt-status");

  if (!listEl) return;

  const ref = db.ref("patients");

  // Lắng nghe realtime danh sách patients
  ref.on("value", snap => {
    const data = snap.val() || {};
    patientsCache = data;
    listEl.innerHTML = "";

    Object.keys(data).forEach(pid => {
      const p = data[pid];
      const row = document.createElement("div");
      row.className = "patient-row";
      const statusBadge = p.status === "online" ? "badge-online" : "badge-offline";

      row.innerHTML = `
        <div class="patient-info">
          <div><strong>${p.name || "(chưa có tên)"}</strong> (${p.age || "?"}, ${p.sex || "?"})</div>
          <div>DeviceId: ${p.deviceId || "-"}</div>
          <div>patientId: <code>${pid}</code></div>
          <div>Trạng thái: <span class="badge ${statusBadge}">${(p.status || "offline").toUpperCase()}</span></div>
        </div>
        <div class="patient-actions">
          <button class="btn-ghost" data-pid="${pid}" data-action="view">View</button>
          <button class="btn-ghost" data-pid="${pid}" data-action="delete">Delete</button>
        </div>
      `;
      listEl.appendChild(row);
    });

    // Cập nhật các dropdown chọn bệnh nhân ở Overview/History/Alerts
    refreshPatientDropdowns(patientsCache);
  });

  // Thêm patient: ghi vào /patients
  addBtn?.addEventListener("click", () => {
    const name = nameEl.value.trim();
    const age  = ageEl.value.trim();
    const sex  = sexEl.value;
    const deviceId = devEl.value.trim();

    if (!name || !age || !deviceId) {
      statusEl.textContent = "Vui lòng nhập đầy đủ Tên, Tuổi và Mã thiết bị.";
      statusEl.style.color = "#dc2626";
      return;
    }

    // Dùng luôn deviceId làm patientId để ESP32 chỉ cần biết mỗi deviceId
    const patientId = deviceId;

    const patientData = {
      name,
      age,
      sex,
      deviceId,
      status: "offline"   // mặc định, phần cứng cập nhật sau
    };

    const patientRef = ref.child(patientId);
    patientRef.set(patientData)
      .then(() => {
        statusEl.textContent = "Đã thêm bệnh nhân, patientId (DeviceId): " + patientId;
        statusEl.style.color = "#16a34a";
        nameEl.value = "";
        ageEl.value = "";
        devEl.value = "";
      })
      .catch(err => {
        console.error(err);
        alert("Lỗi thêm bệnh nhân: " + err.message);
      });
  });

  // View / Delete
  listEl.addEventListener("click", e => {
    const btn = e.target.closest("button[data-pid]");
    if (!btn) return;
    const pid = btn.dataset.pid;
    const action = btn.dataset.action;

    if (action === "delete") {
      if (!confirm("Xóa bệnh nhân này?")) return;
      db.ref("patients/" + pid).remove();
      // tùy bạn có muốn xóa measurements/alerts/settings của pid này không
    }

    if (action === "view") {
      currentPatientId = pid;
      showOverviewForPatient(pid);
    }
    if (window.loadThresholdsForCurrentPatient) {
       window.loadThresholdsForCurrentPatient();
    }
  });
}
// ========== SETTINGS: thresholds theo từng patient + alert phone ==========
function initSettingsModule() {
  const setSel = document.getElementById("set-patient-select");
  const thHrMin   = document.getElementById("th-hr-min");
  const thHrMax   = document.getElementById("th-hr-max");
  const thBpSys   = document.getElementById("th-bp-sys-max");
  const thBpDia   = document.getElementById("th-bp-dia-max");
  const thSaveBtn = document.getElementById("threshold-save-btn");
  const thDelBtn  = document.getElementById("threshold-delete-btn");
  const thStatus  = document.getElementById("threshold-status");

  const phoneInput  = document.getElementById("alert-phone");
  const phoneSaveBtn = document.getElementById("phone-save-btn");
  const phoneDelBtn  = document.getElementById("phone-delete-btn");
  const phoneStatus  = document.getElementById("phone-status");

  if (!thHrMin || !thHrMax || !thBpSys || !thBpDia || !phoneInput) return;

  function getSelectedPatientId() {
    const selectedPid = currentPatientId
      || setSel?.value
      || document.getElementById("ov-patient-select")?.value
      || document.getElementById("his-patient-select")?.value
      || document.getElementById("al-patient-select")?.value
      || "";

    if (selectedPid) currentPatientId = selectedPid;
    return selectedPid;
  }

  // khi người dùng chuyển qua tab Cài đặt, ta nên load ngưỡng nếu đã chọn bệnh nhân
  function loadThresholdsForCurrentPatient() {
    const pid = getSelectedPatientId();
    if (!pid) {
      thStatus.textContent = "Chưa chọn bệnh nhân. Hãy chọn ở dropdown trong tab Cài đặt.";
      thStatus.style.color = "#6b7280";
      return;
    }

    db.ref("settings/" + pid + "/thresholds")
      .once("value")
      .then(snap => {
        const th = snap.val() || {};
        thHrMin.value = th.hrMin ?? "";
        thHrMax.value = th.hrMax ?? "";
        thBpSys.value = th.bpSysMax ?? "";
        thBpDia.value = th.bsSysMin ?? ""; // field name theo schema: bsSysMin
        thStatus.textContent = "Đã tải ngưỡng (nếu có) cho bệnh nhân " + pid;
        thStatus.style.color = "#6b7280";
      });
  }

  // Gọi lúc init
  loadThresholdsForCurrentPatient();

  // Lưu
  thSaveBtn?.addEventListener("click", () => {
    const pid = getSelectedPatientId();
    if (!pid) {
      alert("Chưa chọn bệnh nhân. Hãy chọn ở dropdown trong tab Cài đặt.");
      return;
    }

    const th = {
      hrMin:   thHrMin.value ? Number(thHrMin.value) : null,
      hrMax:   thHrMax.value ? Number(thHrMax.value) : null,
      bpSysMax: thBpSys.value ? Number(thBpSys.value) : null,
      bsSysMin: thBpDia.value ? Number(thBpDia.value) : null
    };

    db.ref("settings/" + pid + "/thresholds")
      .set(th)
      .then(() => {
        thStatus.textContent = "Đã lưu ngưỡng cho bệnh nhân " + pid;
        thStatus.style.color = "#16a34a";
      })
      .catch(err => alert("Lỗi lưu ngưỡng: " + err.message));
  });

  // Xóa
  thDelBtn?.addEventListener("click", () => {
    const pid = getSelectedPatientId();
    if (!pid) {
      alert("Chưa chọn bệnh nhân.");
      return;
    }
    db.ref("settings/" + pid + "/thresholds")
      .remove()
      .then(() => {
        thHrMin.value = "";
        thHrMax.value = "";
        thBpSys.value = "";
        thBpDia.value = "";
        thStatus.textContent = "Đã xóa ngưỡng cho bệnh nhân " + pid;
        thStatus.style.color = "#6b7280";
      })
      .catch(err => alert("Lỗi xóa ngưỡng: " + err.message));
  });

  // để khi chọn patient khác (bấm View), có thể gọi lại loadThresholdsForCurrentPatient()
  window.loadThresholdsForCurrentPatient = loadThresholdsForCurrentPatient;

  // --- Số điện thoại theo từng bệnh nhân ---
  function loadPhoneForCurrentPatient() {
    const pid = getSelectedPatientId();
    if (!pid) {
      phoneStatus.textContent = "Chưa chọn bệnh nhân. Hãy chọn ở dropdown trong tab Cài đặt.";
      phoneStatus.style.color = "#6b7280";
      return;
    }

    db.ref("settings/" + pid + "/alertphone")
      .once("value")
      .then(snap => {
        const phone = snap.val() || "";
        phoneInput.value = phone;
        phoneStatus.textContent = phone
          ? "Đã tải số điện thoại cho bệnh nhân " + pid
          : "Chưa có số điện thoại lưu cho bệnh nhân này.";
        phoneStatus.style.color = "#6b7280";
      });
  }

  // Lưu số điện thoại
  phoneSaveBtn?.addEventListener("click", () => {
    const pid = getSelectedPatientId();
    if (!pid) {
      alert("Chưa chọn bệnh nhân. Hãy chọn ở dropdown trong tab Cài đặt.");
      return;
    }
    const phone = phoneInput.value.trim();
    if (!phone) {
      phoneStatus.textContent = "Vui lòng nhập số điện thoại.";
      phoneStatus.style.color = "#dc2626";
      return;
    }

    db.ref("settings/" + pid + "/alertphone")
      .set(phone)
      .then(() => {
        phoneStatus.textContent = "Đã lưu số điện thoại cho bệnh nhân " + pid;
        phoneStatus.style.color = "#16a34a";
      })
      .catch(err => alert("Lỗi lưu số điện thoại: " + err.message));
  });

  // Xóa số điện thoại
  phoneDelBtn?.addEventListener("click", () => {
    const pid = getSelectedPatientId();
    if (!pid) {
      alert("Chưa chọn bệnh nhân.");
      return;
    }

    db.ref("settings/" + pid + "/alertphone")
      .remove()
      .then(() => {
        phoneInput.value = "";
        phoneStatus.textContent = "Đã xóa số điện thoại cho bệnh nhân " + pid;
        phoneStatus.style.color = "#6b7280";
      })
      .catch(err => alert("Lỗi xóa số điện thoại: " + err.message));
  });

  // cho phép module khác (Overview/Patients) gọi để reload khi đổi patient
  window.loadPhoneForCurrentPatient = loadPhoneForCurrentPatient;

  // Lần đầu init, nếu đã có currentPatientId thì load luôn
  if (currentPatientId) {
    loadThresholdsForCurrentPatient();
    loadPhoneForCurrentPatient();
  }
}
function showOverviewForPatient(patientId) {
  currentPatientId = patientId;

  const ovSel = document.getElementById("ov-patient-select");
  const hisSel = document.getElementById("his-patient-select");
  const alSel = document.getElementById("al-patient-select");
  const setSel = document.getElementById("set-patient-select");
  [ovSel, hisSel, alSel, setSel].forEach(sel => {
    if (sel) sel.value = patientId;
  });

  // Đổi sidebar active sang Tổng quan
  const sideItems = document.querySelectorAll(".sidebar-item");
  sideItems.forEach(b => {
    if (b.dataset.page === "overview") b.classList.add("active");
    else b.classList.remove("active");
  });

  // Hiện page overview
  const pages = document.querySelectorAll(".page");
  pages.forEach(p => p.classList.remove("active"));
  const target = document.getElementById("page-overview");
  if (target) target.classList.add("active");

  const tabTitle = document.getElementById("main-tab-title");
  if (tabTitle) tabTitle.textContent = "TỔNG QUAN";

  // 1) Load thông tin bệnh nhân
  db.ref("patients/" + patientId).once("value").then(snap => {
    const p = snap.val();
    if (!p) return;
    const nameSpan = document.getElementById("ov-patient-name");
    const hisNameSpan = document.getElementById("his-patient-name");
    const alNameSpan  = document.getElementById("al-patient-name");
    if (nameSpan)    nameSpan.textContent    = p.name || patientId;
    if (hisNameSpan) hisNameSpan.textContent = p.name || patientId;
    if (alNameSpan)  alNameSpan.textContent  = p.name || patientId;
  });

  // 2) Đọc measurements hiện tại (1 record)
  db.ref("measurements/" + patientId)
    .once("value")
    .then(snap => {
      const m = snap.val();
      if (!m) {
        document.getElementById("ov-hr-value").textContent   = "-- bpm";
        document.getElementById("ov-bp-value").textContent   = "-- / -- mmHg";
        document.getElementById("ov-spo2-value").textContent = "-- %";
        return;
      }

      document.getElementById("ov-hr-value").textContent   = (m.hr ?? "--") + " bpm";
      document.getElementById("ov-bp-value").textContent   = (m.bpSys ?? "--") + " / " + (m.bpDia ?? "--") + " mmHg";
      document.getElementById("ov-spo2-value").textContent = (m.spo2 ?? "--") + " %";

      // cập nhật lastseen nếu bạn dùng timestamp
      if (m.timestamp) {
        document.getElementById("ov-lastseen").textContent =
          new Date(m.timestamp).toLocaleString();
      }

      // location cho map
      if (m.location && m.location.lat != null && m.location.lng != null) {
        updateMapLocation(m.location.lat, m.location.lng);
      }
    });

  // 3) Có thể load alerts lịch sử cho patient này (tab Alerts)
  loadAlertsForPatient(patientId);

  // 4) Load thresholds cho patient này (nếu đang ở tab Cài đặt)
  if (window.loadThresholdsForCurrentPatient) {
    window.loadThresholdsForCurrentPatient();
  }
  if (window.loadPhoneForCurrentPatient) {
    window.loadPhoneForCurrentPatient();
  }
}
// ========== ALERTS: realtime + filter + ack ==========
let alertsRef = null;
let currentAlertsData = {};

function loadAlertsForPatient(patientId) {
  const container = document.getElementById("alerts-list");
  if (!container) return;

  // Hủy listener cũ nếu có
  if (alertsRef) {
    alertsRef.off("value");
  }

  alertsRef = db.ref("alerts/" + patientId);
  alertsRef.on("value", snap => {
    currentAlertsData = snap.val() || {};
    renderAlertsWithFilters();
  });
}

function renderAlertsWithFilters() {
  const container = document.getElementById("alerts-list");
  if (!container) return;

  const typeSel     = document.getElementById("al-type");
  const severitySel = document.getElementById("al-severity");
  const statusSel   = document.getElementById("al-status");

  const typeFilter = typeSel?.value || "all";
  const sevFilter  = severitySel?.value || "all";
  const stFilter   = statusSel?.value || "all";

  const keys = Object.keys(currentAlertsData);
  container.innerHTML = "";

  let unackedCount = 0;

  keys.forEach(alertId => {
    const a = currentAlertsData[alertId];
    if (!a) return;

    if (typeFilter !== "all" && a.type !== typeFilter) return;
    if (sevFilter !== "all" && a.severity !== sevFilter) return;
    if (stFilter !== "all" && a.status !== stFilter) return;

    if (a.status === "unacked") unackedCount++;

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
          <div><span class="badge ${badgeClass}">${(a.severity || "").toUpperCase()}</span></div>
          <div>${a.message || ""}</div>
          <div>Time: ${a.time || ""}</div>
          <div>Type: ${a.type || ""}</div>
          <div>Status: ${statusText}</div>
        </div>
      </div>
      <div class="alert-actions">
        <button class="btn-ghost" data-alert-id="${alertId}" data-action="view-history">View in History</button>
        <button class="btn-ghost" data-alert-id="${alertId}" data-action="ack">Mark as Ack</button>
      </div>
    `;
    container.appendChild(div);
  });

  // Cập nhật ô thông báo ở Overview
  const notifyCountEl = document.getElementById("ov-notify-count");
  const notifyTextEl  = document.getElementById("ov-notify-text");
  if (notifyCountEl && notifyTextEl) {
    notifyCountEl.textContent = unackedCount;
    notifyTextEl.textContent = unackedCount > 0
      ? "Có " + unackedCount + " cảnh báo chưa xem"
      : "Không có cảnh báo mới";
  }
}

// Lắng nghe thay đổi filter
document.getElementById("al-type")?.addEventListener("change", renderAlertsWithFilters);
document.getElementById("al-severity")?.addEventListener("change", renderAlertsWithFilters);
document.getElementById("al-status")?.addEventListener("change", renderAlertsWithFilters);

// Click actions trong danh sách alerts
document.getElementById("alerts-list")?.addEventListener("click", e => {
  const btn = e.target.closest("button[data-alert-id]");
  if (!btn || !currentPatientId) return;
  const alertId = btn.dataset.alertId;
  const action  = btn.dataset.action;

  if (action === "ack") {
    db.ref("alerts/" + currentPatientId + "/" + alertId + "/status")
      .set("acked");
  }

  if (action === "view-history") {
    // Chuyển sang tab Lịch sử, tạm thời chỉ đổi tab
    const sideItems = document.querySelectorAll(".sidebar-item");
    sideItems.forEach(b => {
      if (b.dataset.page === "history") b.classList.add("active");
      else b.classList.remove("active");
    });

    const pages = document.querySelectorAll(".page");
    pages.forEach(p => p.classList.remove("active"));
    const pageHistory = document.getElementById("page-history");
    if (pageHistory) pageHistory.classList.add("active");

    const tabTitle = document.getElementById("main-tab-title");
    if (tabTitle) tabTitle.textContent = "LỊCH SỬ";
  }
});