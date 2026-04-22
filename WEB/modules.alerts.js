// ========== ALERTS: realtime + filter + ack ==========
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
