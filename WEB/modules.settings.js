// ========== SETTINGS: thresholds theo từng patient + alert phone ==========
function initSettingsModule() {
  const setSel = document.getElementById("set-patient-select");
  const thHrMin   = document.getElementById("th-hr-min");
  const thHrMax   = document.getElementById("th-hr-max");
  const thBpSys   = document.getElementById("th-bp-sys-max");
  const thBpDia   = document.getElementById("th-bp-dia-max");
  const thBpSysMin = document.getElementById("th-bp-sys-min");
  const thBpDiaMin = document.getElementById("th-bp-dia-min");
  const thSpo2Min = document.getElementById("th-spo2-min");
  const thSpo2Max = document.getElementById("th-spo2-max");
  const thSaveBtn = document.getElementById("threshold-save-btn");
  const thDelBtn  = document.getElementById("threshold-delete-btn");
  const thStatus  = document.getElementById("threshold-status");

  const phoneInput  = document.getElementById("alert-phone");
  const phoneSaveBtn = document.getElementById("phone-save-btn");
  const phoneDelBtn  = document.getElementById("phone-delete-btn");
  const phoneStatus  = document.getElementById("phone-status");

  if (!thHrMin || !thHrMax || !thBpSys || !thBpDia || !thBpSysMin || !thBpDiaMin || !thSpo2Min || !thSpo2Max || !phoneInput) return;

  function getSelectedPatientId() {
    const selectedPid = currentPatientId
      || setSel?.value
      || document.getElementById("ov-patient-select")?.value
      || document.getElementById("al-patient-select")?.value
      || "";

    if (selectedPid) currentPatientId = selectedPid;
    return selectedPid;
  }

  function formatThresholdValue(v) {
    return (v === null || v === undefined || v === "") ? "--" : String(v);
  }

  function renderThresholdStatus(prefix, pid, th, color) {
    thStatus.innerHTML =
      prefix + " bệnh nhân <strong>" + pid + "</strong>: " +
      "HR " + formatThresholdValue(th.hrMin) + " - " + formatThresholdValue(th.hrMax) + ", " +
      "BP Sys " + formatThresholdValue(th.bpSysMin) + " - " + formatThresholdValue(th.bpSysMax) + ", " +
      "BP Dia " + formatThresholdValue(th.bpDiaMin) + " - " + formatThresholdValue(th.bpDiaMax) + ", " +
      "SpO₂ " + formatThresholdValue(th.spo2Min) + " - " + formatThresholdValue(th.spo2Max);
    thStatus.style.color = color;
  }

  // khi người dùng chuyển qua tab Cài đặt, ta nên load ngưỡng nếu đã chọn bệnh nhân
  function loadThresholdsForCurrentPatient() {
    const pid = getSelectedPatientId();
    if (!pid) {
      // No patient selected: do not show a persistent message here (handled in UI elsewhere)
      return;
    }

    db.ref("patients/" + pid + "/settings/thresholds")
      .once("value")
      .then(snap => {
        const th = snap.val() || {};
        thHrMin.value = th.hrMin ?? "";
        thHrMax.value = th.hrMax ?? "";
        thBpSys.value = th.bpSysMax ?? "";
        // bp min/max
        thBpSysMin.value = th.bpSysMin ?? "";
        thBpDia.value = th.bpDiaMax ?? "";
        thBpDiaMin.value = th.bpDiaMin ?? "";
        // SpO2 thresholds
        thSpo2Min.value = th.spo2Min ?? "";
        thSpo2Max.value = th.spo2Max ?? "";
        renderThresholdStatus("Ngưỡng đã lưu cho", pid, th, "#6b7280");
      });
  }

  // Gọi lúc init
  loadThresholdsForCurrentPatient();

  // Lưu
  thSaveBtn?.addEventListener("click", () => {
    const pid = getSelectedPatientId();
    if (!pid) {
      alert("Chưa chọn bệnh nhân.");
      return;
    }

    const th = {
      hrMin:    thHrMin.value ? Number(thHrMin.value) : null,
      hrMax:    thHrMax.value ? Number(thHrMax.value) : null,
      bpSysMax: thBpSys.value ? Number(thBpSys.value) : null,
      bpSysMin: thBpSysMin.value ? Number(thBpSysMin.value) : null,
      bpDiaMax: thBpDia.value ? Number(thBpDia.value) : null,
      bpDiaMin: thBpDiaMin.value ? Number(thBpDiaMin.value) : null,
      spo2Min:  thSpo2Min.value ? Number(thSpo2Min.value) : null,
      spo2Max:  thSpo2Max.value ? Number(thSpo2Max.value) : null
    };

    db.ref("patients/" + pid + "/settings/thresholds")
      .set(th)
      .then(() => {
        renderThresholdStatus("Đã lưu ngưỡng cho", pid, th, "#16a34a");
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
    db.ref("patients/" + pid + "/settings/thresholds")
      .remove()
      .then(() => {
        thHrMin.value = "";
        thHrMax.value = "";
        thBpSys.value = "";
        thBpSysMin.value = "";
        thBpDia.value = "";
        thBpDiaMin.value = "";
        thSpo2Min.value = "";
        thSpo2Max.value = "";
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
      // No patient selected: avoid showing a message here to keep UI cleaner
      return;
    }

    db.ref("patients/" + pid + "/settings/alertphone")
      .once("value")
      .then(snap => {
        const phone = snap.val() || "";
        phoneInput.value = phone;
        phoneStatus.innerHTML = phone
          ? "Số điện thoại đã lưu cho bệnh nhân <strong>" + pid + "</strong>: <strong>" + phone + "</strong>"
          : "Chưa có số điện thoại lưu cho bệnh nhân này.";
        phoneStatus.style.color = "#6b7280";
      });
  }

  // Lưu số điện thoại
  phoneSaveBtn?.addEventListener("click", () => {
    const pid = getSelectedPatientId();
    if (!pid) {
      alert("Chưa chọn bệnh nhân.");
      return;
    }
    const phone = phoneInput.value.trim();
    if (!phone) {
      phoneStatus.textContent = "Vui lòng nhập số điện thoại.";
      phoneStatus.style.color = "#dc2626";
      return;
    }

    db.ref("patients/" + pid + "/settings/alertphone")
      .set(phone)
      .then(() => {
        phoneStatus.innerHTML =
          "Đã lưu số điện thoại cho bệnh nhân <strong>" + pid + "</strong>: <strong>" + phone + "</strong>";
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

    db.ref("patients/" + pid + "/settings/alertphone")
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
