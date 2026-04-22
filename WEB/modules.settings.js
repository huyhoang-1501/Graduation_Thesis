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
