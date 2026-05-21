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
      statusEl.textContent = "Vui lòng nhập ít nhất Tên và Mã Mode Online (5 số).";
      statusEl.style.color = "#dc2626";
      return;
    }

    if (patientId.length !== 5 || /\D/.test(patientId)) {
      statusEl.textContent = "Mã Mode Online phải đúng 5 số.";
      statusEl.style.color = "#dc2626";
      return;
    }

    if (deviceId && !isValidDeviceId(deviceId)) {
      statusEl.textContent = "DEVICE ID không hợp lệ.";
      statusEl.style.color = "#dc2626";
      return;
    }

    // Do not store deviceId inside /patients — devices are managed under /devices
    // and linked by setting devices/<deviceId>.patientId. Keeping deviceId in
    // both places causes duplication and potential out-of-sync states.
    const patientData = {
      name,
      age: age || "",
      sex: sex || "Nam",
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

        statusEl.textContent = "Đã thêm bệnh nhân với Mode Online ID: " + patientId + (deviceId ? " (DEVICE ID: " + deviceId + ")" : "") + ".";
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
      // Require DEVICE ID present in Overview before allowing view via Patients tab
      const did = document.getElementById('ov-device-id-input')?.value || '';
      if (!did.trim()) {
        alert('Vui lòng nhập DEVICE ID ở tab Tổng quan trước khi xem bệnh nhân.');
        return;
      }
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

// Helper to attach the realtime listener to a device node (reused by both
// showOverviewForPatient and showDeviceById)
function attachListenerToDevice(devId) {
  if (!devId) return;
  const devRef = db.ref("devices/" + devId);
  // detach previous listener if any
  if (window._currentDeviceRef && window._currentDeviceListener) {
    try { window._currentDeviceRef.off('value', window._currentDeviceListener); } catch (e) { /* ignore */ }
  }
  window._currentDeviceRef = devRef;
  window._currentDeviceListener = devRef.on('value', snap => {
    const d = snap.val() || {};

    // battery
    const battEl = document.getElementById("ov-battery");
    if (battEl) battEl.textContent = (d.batteryPercent != null) ? (d.batteryPercent + " %") : "-- %";

    // last seen & freshness
    const now = Date.now();
    const lastSeenRaw = d.lastSeen || d.lastSeenAt || 0;
    const lastSeen = Number(lastSeenRaw) || 0;
    const lastEl = document.getElementById('ov-lastseen');
    const freshnessEl = document.getElementById('ov-freshness');
    if (lastEl) {
      if (lastSeen) lastEl.textContent = new Date(lastSeen).toLocaleString();
      else lastEl.textContent = '--';
    }

    // compute derived status based on lastSeen
    let derivedStatus = (d.status || 'offline');
    const ONLINE_THRESHOLD = 3 * 60 * 1000; // 3 minutes
    if (lastSeen) {
      const age = now - lastSeen;
      derivedStatus = (age < ONLINE_THRESHOLD) ? 'online' : 'offline';
    } else {
      derivedStatus = d.status || 'offline';
    }

    // badge
    const badge = document.getElementById('ov-device-badge');
    if (badge) {
      badge.textContent = derivedStatus.toUpperCase();
      badge.classList.remove('badge-online','badge-offline');
      if (derivedStatus === 'online') badge.classList.add('badge-online');
      else badge.classList.add('badge-offline');
    }

    // freshness text
    if (freshnessEl) {
      if (!lastSeen) freshnessEl.textContent = '--';
      else {
        const sec = Math.floor((now - lastSeen) / 1000);
        if (sec < 60) freshnessEl.textContent = 'Cập nhật mới nhất';
        else if (sec < 3600) freshnessEl.textContent = 'Cách đây ' + Math.floor(sec / 60) + ' phút';
        else freshnessEl.textContent = 'Cách đây ' + Math.floor(sec / 3600) + ' giờ';
      }
    }

    // optional network indicator
    const netEl = document.getElementById('ov-network');
    if (netEl) netEl.textContent = d.network || '--';
  });

  const idInput = document.getElementById('ov-device-id-input'); if (idInput) idInput.value = devId;
}

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

  // Attach realtime listener to the device linked to this patient (if any).
  // We no longer store deviceId inside /patients; instead, find devices with
  // devices/<deviceId>.patientId === patientId.
  try {
    // If the user explicitly opened a device view by ID, do not override
    // that listener when switching patients — the device panel should
    // remain showing the manually-entered DEVICE ID.
    if (window._deviceViewMode === 'device') {
      // still load patient info/measurements below, but skip attaching
      // a device listener that would replace the explicit device view.
    } else {
    // detach previous listener
    if (window._currentDeviceRef && window._currentDeviceListener) {
      window._currentDeviceRef.off('value', window._currentDeviceListener);
      window._currentDeviceRef = null;
      window._currentDeviceListener = null;
    }

    // Query devices for a device linked to this patientId
    db.ref('devices').orderByChild('patientId').equalTo(patientId).once('value')
      .then(devSnap => {
        const devs = devSnap.val() || {};
        const keys = Object.keys(devs);
        if (keys.length === 0) {
          // no linked device found via query. Try a loose fallback scan to handle
          // possible type mismatches (e.g. numeric vs string patientId stored in DB).
          return db.ref('devices').once('value').then(allSnap => {
            const all = allSnap.val() || {};
            let foundId = null;
            Object.keys(all).forEach(k => {
              const dv = all[k] || {};
              try {
                if (String(dv.patientId) === String(patientId)) {
                  if (!foundId) foundId = k;
                } else if (!isNaN(Number(dv.patientId)) && !isNaN(Number(patientId)) && Number(dv.patientId) === Number(patientId)) {
                  if (!foundId) foundId = k;
                }
              } catch (e) {
                // ignore coercion errors
              }
            });

            if (!foundId) {
              const battEl = document.getElementById("ov-battery"); if (battEl) battEl.textContent = "-- %";
              const lastEl = document.getElementById("ov-lastseen"); if (lastEl) lastEl.textContent = "--";
              const badge = document.getElementById("ov-device-badge"); if (badge) { badge.textContent = "OFFLINE"; badge.classList.remove("badge-online","badge-stale"); badge.classList.add("badge-offline"); }
              const idInput = document.getElementById('ov-device-id-input'); if (idInput) idInput.value = '';
              return;
            }

            // attach listener to the loosely-matched device
            attachListenerToDevice(foundId);
            return;
          }).catch(e => {
            console.error('device fallback lookup error', e);
          });
        }

        // pick the first device found (system expects at most one device per patient)
        const deviceId = keys[0];

        attachListenerToDevice(deviceId);
      })
      .catch(e => console.error('device lookup error', e));
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
