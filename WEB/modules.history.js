// ========== HISTORY: load measurements by time range + summary + CSV export ==========
// This module expects global `db` (Firebase RTDB) and shared state variables.

(function () {
  "use strict";

  function pad2(n) {
    return String(n).padStart(2, "0");
  }

  function toDatetimeLocalValue(d) {
    if (!(d instanceof Date)) d = new Date(d);
    if (isNaN(d.getTime())) return "";
    return (
      d.getFullYear() +
      "-" +
      pad2(d.getMonth() + 1) +
      "-" +
      pad2(d.getDate()) +
      "T" +
      pad2(d.getHours()) +
      ":" +
      pad2(d.getMinutes())
    );
  }

  function parseDatetimeLocalToTs(v) {
    const s = String(v || "").trim();
    if (!s) return null;
    const ts = new Date(s).getTime();
    return Number.isFinite(ts) ? ts : null;
  }

  function formatLocal(ts) {
    const n = Number(ts);
    if (!Number.isFinite(n) || n <= 0) return "--";
    try {
      return new Date(n).toLocaleString();
    } catch {
      return "--";
    }
  }

  function numOrNull(v) {
    if (v === null || v === undefined || v === "") return null;
    const n = Number(v);
    return Number.isFinite(n) ? n : null;
  }

  function pickNumber(raw, keys) {
    if (!raw || typeof raw !== "object") return null;
    for (const k of keys) {
      if (raw[k] !== undefined) {
        const n = numOrNull(raw[k]);
        if (n !== null) return n;
      }
    }
    return null;
  }

  function pickTimestamp(raw) {
    if (!raw || typeof raw !== "object") return 0;
    const ts =
      raw.timestamp ??
      raw.ts ??
      raw.time ??
      raw.createdAt ??
      raw.updatedAt ??
      raw.lastSeen;
    const n = Number(ts);
    return Number.isFinite(n) ? n : 0;
  }

  function normalizeMeasurement(raw) {
    if (!raw || typeof raw !== "object") return null;

    const timestamp = pickTimestamp(raw);
    const hr = pickNumber(raw, ["hr", "heartRate", "HR"]);
    const spo2 = pickNumber(raw, ["spo2", "spO2", "SpO2"]);
    const bpSys = pickNumber(raw, ["bpSys", "systolic", "sys", "SYS"]);
    const bpDia = pickNumber(raw, ["bpDia", "diastolic", "dia", "DIA"]);

    // Keep rows that have at least a timestamp OR one metric present.
    const hasAny = timestamp || hr !== null || spo2 !== null || bpSys !== null || bpDia !== null;
    if (!hasAny) return null;

    return { timestamp, hr, spo2, bpSys, bpDia };
  }

  function computeMetricStats(values) {
    const nums = values.filter((v) => Number.isFinite(v));
    const n = nums.length;
    if (!n) {
      return { n: 0, min: null, max: null, avg: null };
    }
    let min = nums[0];
    let max = nums[0];
    let sum = 0;
    for (const x of nums) {
      if (x < min) min = x;
      if (x > max) max = x;
      sum += x;
    }
    return { n, min, max, avg: sum / n };
  }

  function fmtNum(v, digits = 1) {
    if (v === null || v === undefined || !Number.isFinite(v)) return "--";
    // For integers (BP) we keep 0 decimals by default
    const isInt = Math.abs(v - Math.round(v)) < 1e-9;
    const d = isInt ? 0 : digits;
    return Number(v).toFixed(d);
  }

  function escapeCsvCell(v) {
    const s = v === null || v === undefined ? "" : String(v);
    if (/[",\n\r]/.test(s)) return '"' + s.replace(/"/g, '""') + '"';
    return s;
  }

  async function loadHistoryRows(patientId) {
    // Prefer /measurements/<pid>/history when present, otherwise fall back
    // to scanning /measurements/<pid> (excluding latest/history).

    // Try history node first
    try {
      const histSnap = await db.ref("measurements/" + patientId + "/history").once("value");
      const hist = histSnap.val();
      if (hist && typeof hist === "object") {
        const rows = [];
        Object.keys(hist).forEach((k) => {
          const r = normalizeMeasurement(hist[k]);
          if (r) rows.push(r);
        });
        return rows;
      }
    } catch (err) {
      // fall back to base node
      console.warn("History read failed, falling back:", err);
    }

    const baseSnap = await db.ref("measurements/" + patientId).once("value");
    const base = baseSnap.val();
    const out = [];

    if (!base) return out;

    // If base is itself a measurement record
    if (typeof base === "object" && (base.hr !== undefined || base.spo2 !== undefined || base.bpSys !== undefined || base.bpDia !== undefined)) {
      const r = normalizeMeasurement(base);
      if (r) out.push(r);
      return out;
    }

    // If base contains latest record
    if (base.latest && typeof base.latest === "object") {
      const r = normalizeMeasurement(base.latest);
      if (r) out.push(r);
    }

    // Legacy push-style children under /measurements/<pid>/<pushId>
    if (typeof base === "object") {
      Object.keys(base).forEach((k) => {
        if (k === "latest" || k === "history") return;
        const r = normalizeMeasurement(base[k]);
        if (r) out.push(r);
      });
    }

    return out;
  }

  function filterRowsByRange(rows, startTs, endTs) {
    return rows.filter((r) => {
      const ts = Number(r.timestamp) || 0;
      if (!ts) return false;
      if (startTs !== null && ts < startTs) return false;
      if (endTs !== null && ts > endTs) return false;
      return true;
    });
  }

  function renderDetailTable(tbody, rows) {
    if (!tbody) return;

    if (!rows.length) {
      tbody.innerHTML = `
        <tr>
          <td colspan="5" class="history-empty">Không có dữ liệu trong khoảng thời gian đã chọn.</td>
        </tr>
      `;
      return;
    }

    const sorted = [...rows].sort((a, b) => (Number(b.timestamp) || 0) - (Number(a.timestamp) || 0));
    const html = sorted
      .map((r) => {
        const t = formatLocal(r.timestamp);
        const hr = r.hr === null ? "--" : fmtNum(r.hr, 0);
        const spo2 = r.spo2 === null ? "--" : fmtNum(r.spo2, 0);
        const sys = r.bpSys === null ? "--" : fmtNum(r.bpSys, 0);
        const dia = r.bpDia === null ? "--" : fmtNum(r.bpDia, 0);
        return `
          <tr>
            <td>${t}</td>
            <td class="num">${hr}</td>
            <td class="num">${spo2}</td>
            <td class="num">${sys}</td>
            <td class="num">${dia}</td>
          </tr>
        `;
      })
      .join("");

    tbody.innerHTML = html;
  }

  function exportCsv(patientId, rows) {
    if (!rows || !rows.length) {
      alert("Chưa có dữ liệu để xuất CSV. Hãy bấm Lọc trước.");
      return;
    }

    const sorted = [...rows].sort((a, b) => (Number(a.timestamp) || 0) - (Number(b.timestamp) || 0));

    const header = ["timestamp_ms", "time_local", "hr_bpm", "spo2_percent", "systolic_mmhg", "diastolic_mmhg"];
    const lines = [header.join(",")];

    for (const r of sorted) {
      const row = [
        r.timestamp || "",
        formatLocal(r.timestamp),
        r.hr ?? "",
        r.spo2 ?? "",
        r.bpSys ?? "",
        r.bpDia ?? ""
      ].map(escapeCsvCell);
      lines.push(row.join(","));
    }

    const csv = lines.join("\n");
    const blob = new Blob(["\ufeff", csv], { type: "text/csv;charset=utf-8" });

    const a = document.createElement("a");
    const url = URL.createObjectURL(blob);

    const safePid = String(patientId || "patient").replace(/[^a-zA-Z0-9_-]+/g, "_");
    const filename = `history_${safePid}_${Date.now()}.csv`;

    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();

    setTimeout(() => URL.revokeObjectURL(url), 3000);
  }

  async function doLoad({ patientId, startTs, endTs, statusEl, tbodyEl, exportBtn }) {
    if (!patientId) {
      alert("Vui lòng chọn bệnh nhân.");
      return;
    }
    if (!db) {
      alert("Chưa khởi tạo Firebase Database.");
      return;
    }

    if (startTs !== null && endTs !== null && startTs > endTs) {
      alert("Thời gian bắt đầu phải nhỏ hơn hoặc bằng thời gian kết thúc.");
      return;
    }

    if (statusEl) {
      statusEl.textContent = "Đang lọc dữ liệu...";
      statusEl.style.color = "#6b7280";
    }

    if (exportBtn) exportBtn.disabled = true;

    try {
      const rowsAll = await loadHistoryRows(patientId);

      const filtered = filterRowsByRange(rowsAll, startTs, endTs);
      // Save for CSV export
      window._historyLastRows = filtered;
      window._historyLastPatientId = patientId;

      renderDetailTable(tbodyEl, filtered);

      if (statusEl) {
        statusEl.textContent = `Đã lọc ${filtered.length} bản ghi.`;
        statusEl.style.color = filtered.length ? "#16a34a" : "#6b7280";
      }

      if (exportBtn) exportBtn.disabled = filtered.length === 0;
    } catch (err) {
      console.error("History load error:", err);
      if (statusEl) {
        statusEl.textContent = "Lỗi tải dữ liệu. Vui lòng thử lại.";
        statusEl.style.color = "#dc2626";
      }
      if (tbodyEl) {
        tbodyEl.innerHTML = `<tr><td colspan="5" class="history-empty">Lỗi tải dữ liệu.</td></tr>`;
      }
    }
  }

  function initHistoryModule() {
    const sel = document.getElementById("hi-patient-select");
    const startEl = document.getElementById("hi-start");
    const endEl = document.getElementById("hi-end");
    const loadBtn = document.getElementById("hi-load-btn");
    const exportBtn = document.getElementById("hi-export-btn");
    const statusEl = document.getElementById("hi-status");
    const tbodyEl = document.getElementById("hi-table-body");

    if (!sel || !startEl || !endEl || !loadBtn || !exportBtn || !tbodyEl) return;

    // Avoid double init
    if (window._historyInit) return;
    window._historyInit = true;

    // Default range: last 24 hours
    const now = new Date();
    const start = new Date(now.getTime() - 24 * 60 * 60 * 1000);
    if (!startEl.value) startEl.value = toDatetimeLocalValue(start);
    if (!endEl.value) endEl.value = toDatetimeLocalValue(now);

    // Auto clear state when switching patient
    sel.addEventListener("change", () => {
      exportBtn.disabled = true;
      window._historyLastRows = [];
      window._historyLastPatientId = sel.value;
      if (statusEl) {
        statusEl.textContent = "Chọn thời gian rồi bấm Lọc.";
        statusEl.style.color = "#6b7280";
      }
    });

    loadBtn.addEventListener("click", async () => {
      const patientId = sel.value;
      const startTs = parseDatetimeLocalToTs(startEl.value);
      const endTs = parseDatetimeLocalToTs(endEl.value);

      await doLoad({
        patientId,
        startTs,
        endTs,
        statusEl,
        tbodyEl,
        exportBtn
      });
    });

    exportBtn.addEventListener("click", () => {
      const pid = window._historyLastPatientId || sel.value;
      const rows = window._historyLastRows || [];
      exportCsv(pid, rows);
    });

    if (statusEl) {
      statusEl.textContent = "Chọn bệnh nhân và khoảng thời gian, sau đó bấm Lọc.";
      statusEl.style.color = "#6b7280";
    }
  }

  // Expose to app.js
  window.initHistoryModule = initHistoryModule;
})();
