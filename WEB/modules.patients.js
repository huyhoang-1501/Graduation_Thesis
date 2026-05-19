// ========== PATIENTS: CRUD cơ bản ==========
function initPatientsModule() {
  const listEl   = document.getElementById("patients-list");
  const addBtn   = document.getElementById("pt-add-btn");
  const nameEl   = document.getElementById("pt-name");
  const ageEl    = document.getElementById("pt-age");
  const sexEl    = document.getElementById("pt-sex");
  const statusEl = document.getElementById("pt-status");
  const bindIdEl  = document.getElementById("bind-device-id");
  const pairCodeEl = document.getElementById("bind-pair-code");

  if (!listEl) return;

  const ref = db.ref("patients");
  const ownerUid = getCurrentUserUid();

  // Lắng nghe realtime danh sách patients
  ref.on("value", snap => {
    const data = snap.val() || {};
    const ownedPatients = {};
    Object.keys(data).forEach(pid => {
      const p = data[pid] || {};
      if (p.ownerUid === ownerUid) {
        ownedPatients[pid] = p;
      }
    });

    patientsCache = ownedPatients;
    listEl.innerHTML = "";

    Object.keys(ownedPatients).forEach(pid => {
      const p = ownedPatients[pid];
      const row = document.createElement("div");
      row.className = "patient-row";

      row.innerHTML = `
          <div class="patient-info">
            <div><strong>${p.name || "(chưa có tên)"}</strong> (${p.age || "?"}, ${p.sex || "?"})</div>
            <div>patientId: <code>${pid}</code></div>
          </div>
          <div class="patient-actions">
            <button class="btn-ghost" data-pid="${pid}" data-action="view">View</button>
            <button class="btn-ghost" data-pid="${pid}" data-action="delete">Delete</button>
          </div>
        `;
      listEl.appendChild(row);
    });

    // Cập nhật các dropdown chọn bệnh nhân ở Overview/History/Alerts
    refreshPatientDropdowns(ownedPatients);
  });

  // Thêm patient: ghi vào /patients (deviceId là tùy chọn)
  addBtn?.addEventListener("click", () => {
    const name = nameEl.value.trim();
    const age  = ageEl.value.trim();
    const sex  = sexEl.value;
    const deviceId = normalizeDeviceId(bindIdEl?.value || "");
    const patientId = normalizeUserModeId(pairCodeEl?.value || "");

    if (!name || !patientId) {
      statusEl.textContent = "Vui lòng nhập ít nhất Tên và Mã User mode (5 số).";
      statusEl.style.color = "#dc2626";
      return;
    }

    if (patientId.length !== 5 || /\D/.test(patientId)) {
      statusEl.textContent = "Mã User mode phải đúng 5 số.";
      statusEl.style.color = "#dc2626";
      return;
    }

    if (deviceId && !isValidDeviceId(deviceId)) {
      statusEl.textContent = "DEVICE ID không hợp lệ.";
      statusEl.style.color = "#dc2626";
      return;
    }

    const patientData = {
      name,
      age: age || "",
      sex: sex || "Nam",
      deviceId: deviceId || "",
      ownerUid,
      mode: "user",
      status: "offline"   // mặc định, phần cứng cập nhật sau
    };

    const patientRef = ref.child(patientId);
    patientRef.set(patientData)
      .then(() => {
        // if deviceId provided, update device node
        if (deviceId) {
          return db.ref("devices/" + deviceId).update({
            ownerUid,
            patientId,
            status: "linked",
            linked: true,
            mode: "user",
            updatedAt: firebase.database.ServerValue.TIMESTAMP
          });
        }
        return Promise.resolve();
      })
      .then(async () => {
        // create/update settings node for this patientId
        try {
          await db.ref('settings/' + patientId).set({
            patientId,
            name,
            thresholds: {},
            phone: ''
          });
        } catch (err) {
          console.warn('Không lưu được settings mặc định cho bệnh nhân:', err);
        }

        statusEl.textContent = "Đã thêm bệnh nhân với User mode ID: " + patientId + (deviceId ? " (DEVICE ID: " + deviceId + ")" : "") + ".";
        statusEl.style.color = "#16a34a";
        nameEl.value = "";
        ageEl.value = "";
        pairCodeEl.value = "";
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

// Global device listener state to keep Overview in sync with /devices/<deviceId>
window._currentDeviceRef = null;
window._currentDeviceListener = null;

function showOverviewForPatient(patientId) {
  const pCache = patientsCache[patientId];
  if (!pCache || (pCache.ownerUid && pCache.ownerUid !== getCurrentUserUid())) {
    alert("Bạn không có quyền truy cập bệnh nhân này.");
    return;
  }

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

  // Attach realtime listener to /devices/<deviceId> for this patient (if any)
  try {
    // detach previous listener
    if (window._currentDeviceRef && window._currentDeviceListener) {
      window._currentDeviceRef.off('value', window._currentDeviceListener);
      window._currentDeviceRef = null;
      window._currentDeviceListener = null;
    }

    const deviceId = pCache.deviceId || null;
    if (deviceId) {
      const devRef = db.ref("devices/" + deviceId);
      window._currentDeviceRef = devRef;
      window._currentDeviceListener = devRef.on('value', snap => {
        const d = snap.val() || {};

        // battery
        const battEl = document.getElementById("ov-battery");
        if (battEl) battEl.textContent = (d.batteryPercent != null) ? (d.batteryPercent + " %") : "-- %";

        // last seen
        const lastEl = document.getElementById("ov-lastseen");
        if (lastEl) {
          if (d.lastSeen) lastEl.textContent = new Date(d.lastSeen).toLocaleString();
        }

        // device status badge
        const badge = document.getElementById("ov-device-badge");
        if (badge) {
          const status = (d.status || 'offline');
          badge.textContent = status.toUpperCase();
          badge.classList.remove("badge-online","badge-offline","badge-stale");
          if (status === 'online') badge.classList.add("badge-online");
          else badge.classList.add("badge-offline");
        }
      });
    }
  } catch (e) {
    console.error('device listener error', e);
  }

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
