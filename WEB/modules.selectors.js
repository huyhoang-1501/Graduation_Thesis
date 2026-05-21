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
