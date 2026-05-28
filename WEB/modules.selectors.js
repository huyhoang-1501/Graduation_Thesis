// ========== PATIENT SELECTORS (Overview / History / Alerts) ==========
function initPatientSelectors() {
  const ovSel  = document.getElementById("ov-patient-select");
  const hiSel  = document.getElementById("hi-patient-select");
  const alSel  = document.getElementById("al-patient-select");
  const setSel = document.getElementById("set-patient-select");

  function syncAllPatientSelectors(selectedPid, sourceSel) {
    [ovSel, hiSel, alSel, setSel].forEach(other => {
      if (other && other !== sourceSel) other.value = selectedPid;
    });
  }

  function attachHandler(sel, options = {}) {
    if (!sel) return;
    sel.addEventListener("change", () => {
      const pid = sel.value;
      if (!pid) return;

      // If this selector requires a DEVICE ID to be present (Overview), enforce it
      if (options.requireDeviceId) {
        const did = document.getElementById('ov-device-id-input')?.value || '';
        if (!did.trim()) {
          alert('Vui lòng nhập DEVICE ID trước khi chọn bệnh nhân.');
          // reset select to placeholder
          try { sel.selectedIndex = 0; sel.value = ''; } catch (e) { /* ignore */ }
          return;
        }
      }

      if (options.keepCurrentPage) {
        currentPatientId = pid;

        const p = patientsCache[pid] || {};
        const displayName = p.name || pid;
        const ovName = document.getElementById("ov-patient-name");
        const alName = document.getElementById("al-patient-name");
        if (ovName) ovName.textContent = displayName;
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

  attachHandler(ovSel, { requireDeviceId: true });
  // History stays on the same page (doesn't auto-jump to Overview)
  attachHandler(hiSel, { keepCurrentPage: true });
  attachHandler(alSel);
  attachHandler(setSel, { keepCurrentPage: true });
}

function refreshPatientDropdowns(patientsData) {
  const ovSel  = document.getElementById("ov-patient-select");
  const hiSel  = document.getElementById("hi-patient-select");
  const alSel  = document.getElementById("al-patient-select");
  const setSel = document.getElementById("set-patient-select");

  const sels = [ovSel, hiSel, alSel, setSel];
  if (!sels.some(Boolean)) return;

  sels.forEach(sel => {
    if (!sel) return;
    // If there's only one (or zero) patient in the list, avoid auto-selecting
    // it on page load/refresh. This prevents the browser restoring a previous
    // selection and makes the dashboard start with no patient chosen.
    const totalPatients = Object.keys(patientsData).length;
    const current = (totalPatients <= 1) ? '' : sel.value; // cố gắng giữ lựa chọn hiện tại
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

    // Force placeholder selection on page load when there is only one (or
    // zero) patient. Some browsers restore form state on reload which can
    // re-select the option — override that by explicitly setting selectedIndex
    // to 0 and clearing the value.
    if (totalPatients <= 1) {
      try {
        sel.selectedIndex = 0;
        sel.value = '';
      } catch (e) {
        // ignore
      }
    }
  });
}
