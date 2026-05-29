// ========== HISTORY: load measurements by time range + summary + CSV export ==========
// This module expects global `db` (Firebase RTDB) and shared state variables.

(function () {
  "use strict";

  // Chart.js instance for History tab
  let historyChart = null;

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

  function formatShort(ts) {
    const n = Number(ts);
    if (!Number.isFinite(n) || n <= 0) return "--";
    try {
      // Compact label for the x-axis
      return new Date(n).toLocaleString(undefined, {
        month: "2-digit",
        day: "2-digit",
        hour: "2-digit",
        minute: "2-digit"
      });
    } catch {
      return "--";
    }
  }

  function downsampleSortedRows(sortedAsc, maxPoints) {
    if (!Array.isArray(sortedAsc)) return [];
    if (!maxPoints || sortedAsc.length <= maxPoints) return sortedAsc;
    const stride = Math.ceil(sortedAsc.length / maxPoints);
    const out = [];
    for (let i = 0; i < sortedAsc.length; i += stride) out.push(sortedAsc[i]);
    // Ensure last point is included (helps chart end at the expected time)
    const last = sortedAsc[sortedAsc.length - 1];
    if (out.length && out[out.length - 1] !== last) out.push(last);
    return out;
  }

  function clearHistoryChart(statusEl) {
    if (historyChart) {
      historyChart.data.labels = [];
      historyChart.data.datasets.forEach((ds) => (ds.data = []));
      try {
        historyChart.update();
      } catch {
        // ignore
      }
    }
    if (statusEl) {
      statusEl.textContent = "Chưa có dữ liệu. Chọn khoảng thời gian rồi bấm Lọc.";
      statusEl.style.color = "#6b7280";
    }
  }

  function ensureHistoryChart(canvasEl) {
    if (!canvasEl) return null;
    if (historyChart) return historyChart;
    if (typeof Chart === "undefined") {
      console.warn("Chart.js not found; history chart disabled");
      return null;
    }

    const ctx = canvasEl.getContext("2d");
    if (!ctx) return null;

    historyChart = new Chart(ctx, {
      type: "line",
      data: {
        labels: [],
        datasets: [
          {
            label: "Nhịp Tim (bpm)",
            data: [],
            borderColor: "#ef4444",
            backgroundColor: "rgba(239,68,68,0.15)",
            fill: true,
            tension: 0.3,
            pointRadius: 2,
            yAxisID: "y"
          },
          {
            label: "SpO₂ (%)",
            data: [],
            borderColor: "#3b82f6",
            backgroundColor: "rgba(59,130,246,0.12)",
            fill: true,
            tension: 0.3,
            pointRadius: 2,
            yAxisID: "ySpo2"
          },
          {
            label: "BP Sys (mmHg)",
            data: [],
            borderColor: "#f59e0b",
            backgroundColor: "rgba(245,158,11,0.08)",
            fill: false,
            tension: 0.2,
            pointRadius: 3,
            spanGaps: false,
            yAxisID: "yBp"
          },
          {
            label: "BP Dia (mmHg)",
            data: [],
            borderColor: "#10b981",
            backgroundColor: "rgba(16,185,129,0.08)",
            fill: false,
            tension: 0.2,
            pointRadius: 3,
            spanGaps: false,
            yAxisID: "yBp"
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: "index", intersect: false },
        plugins: {
          legend: { position: "top" },
          tooltip: {
            callbacks: {
              title: (items) => {
                const i = items && items[0];
                const label = i ? i.label : "";
                return label || "";
              }
            }
          }
        },
        scales: {
          x: { display: false },
          y: {
            type: "linear",
            display: true,
            position: "left",
            beginAtZero: false,
            // Keep HR axis scaling visually consistent with the Overview mini chart
            // so users see comparable ranges/scale between tabs.
            suggestedMin: 40,
            suggestedMax: 220
          },
          ySpo2: {
            type: "linear",
            display: true,
            position: "right",
            suggestedMin: 70,
            suggestedMax: 100,
            grid: { drawOnChartArea: false }
          },
          yBp: {
            type: "linear",
            display: false,
            position: "right",
            suggestedMin: 40,
            suggestedMax: 200,
            grid: { drawOnChartArea: false }
          }
        }
      }
    });

    return historyChart;
  }

  function renderHistoryChart(canvasEl, statusEl, rows, startTs, endTs) {
    if (!canvasEl) return;

    const chart = ensureHistoryChart(canvasEl);
    if (!chart) return;

    if (!rows || !rows.length) {
      clearHistoryChart(statusEl);
      return;
    }

    const sortedAsc = [...rows].sort((a, b) => (Number(a.timestamp) || 0) - (Number(b.timestamp) || 0));
    const MAX_POINTS = 600;
    const sampled = downsampleSortedRows(sortedAsc, MAX_POINTS);

    const labels = sampled.map((r) => formatShort(r.timestamp));
    const hr = sampled.map((r) => (r.hr === null ? null : r.hr));
    const spo2 = sampled.map((r) => (r.spo2 === null ? null : r.spo2));
    const sys = sampled.map((r) => (r.bpSys === null ? null : r.bpSys));
    const dia = sampled.map((r) => (r.bpDia === null ? null : r.bpDia));

    // Prevent Chart.js from connecting BP points across large time gaps.
    // If two consecutive samples are farther apart than GAP_MS, mark the
    // later BP values as null so the line is broken (datasets have spanGaps:false).
    const GAP_MS = 10 * 60 * 1000; // 10 minutes
    if (Array.isArray(sampled) && sampled.length > 1) {
      for (let i = 1; i < sampled.length; i++) {
        const prevTs = Number(sampled[i - 1].timestamp) || 0;
        const curTs = Number(sampled[i].timestamp) || 0;
        if (prevTs && curTs && curTs - prevTs > GAP_MS) {
          // break the connection by setting the later point to null
          sys[i] = null;
          dia[i] = null;
        }
      }
    }

    chart.data.labels = labels;
    if (chart.data.datasets[0]) chart.data.datasets[0].data = hr;
    if (chart.data.datasets[1]) chart.data.datasets[1].data = spo2;
    if (chart.data.datasets[2]) chart.data.datasets[2].data = sys;
    if (chart.data.datasets[3]) chart.data.datasets[3].data = dia;

    // Auto-hide datasets that are completely empty in the selected range.
    // Important: do NOT force-show datasets (so legend toggles remain respected).
    chart.data.datasets.forEach((ds) => {
      const hasAny = Array.isArray(ds.data) && ds.data.some((v) => Number.isFinite(v));
      if (!hasAny) ds.hidden = true;
    });

    try {
      chart.update();
      // Some layouts need a delayed resize when switching tabs
      setTimeout(() => {
        try {
          chart.resize();
        } catch {
          // ignore
        }
      }, 80);
    } catch (e) {
      console.warn("historyChart update failed", e);
    }

    if (statusEl) {
      const rangeText =
        (startTs ? formatLocal(startTs) : "--") +
        " → " +
        (endTs ? formatLocal(endTs) : "--");
      const pointText = sampled.length !== sortedAsc.length
        ? `${sampled.length}/${sortedAsc.length} điểm (đã lấy mẫu)`
        : `${sampled.length} điểm`;
      statusEl.textContent = `Biểu đồ: ${pointText}. Khoảng thời gian: ${rangeText}.`;
      statusEl.style.color = "#6b7280";
    }
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

  async function exportCsv(patientId, rows) {
      if (!rows || !rows.length) {
        alert("Chưa có dữ liệu để xuất CSV. Hãy bấm Lọc trước.");
        return;
      }

      // Try to read patient name from DB; fall back to patientId when absent.
      let patientName = "";
      try {
        if (patientId && db) {
          const snap = await db.ref("patients/" + patientId).once("value");
          const p = snap.val();
          if (p && p.name) patientName = String(p.name);
        }
      } catch (err) {
        console.warn('Could not read patient name for CSV export:', err);
      }

      const displayName = patientName || String(patientId || "patient");

      // CSV: include patient name as the first column for clarity.
      const header = ["patient_name", "time_local", "hr_bpm", "spo2_percent", "systolic_mmhg", "diastolic_mmhg"];

      const sorted = [...rows].sort((a, b) => (Number(a.timestamp) || 0) - (Number(b.timestamp) || 0));

      const lines = [header.join(",")];

      for (const r of sorted) {
        const row = [
          displayName,
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

      // Normalize name to ASCII (remove diacritics) then sanitize to filename-safe chars.
      // Example: "Nguyễn Văn Á" -> "Nguyen_Van_A"
      const rawName = String(displayName || patientId || "patient");
      const normalized = rawName.normalize && rawName.normalize('NFD')
        ? rawName.normalize('NFD').replace(/\p{Diacritic}/gu, '')
        : rawName.replace(/[\u0300-\u036f]/g, '');
      const safeName = String(normalized).replace(/[^a-zA-Z0-9_-]+/g, '_').replace(/_+/g, '_').replace(/^_|_$/g, '');
      const filename = `history_${safeName || 'patient'}.csv`;

      a.href = url;
      a.download = filename;
      document.body.appendChild(a);
      a.click();
      a.remove();

      setTimeout(() => URL.revokeObjectURL(url), 3000);
  }

  async function doLoad({ patientId, startTs, endTs, statusEl, tbodyEl, exportBtn, chartCanvas, chartStatusEl }) {
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
      renderHistoryChart(chartCanvas, chartStatusEl, filtered, startTs, endTs);

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
      clearHistoryChart(chartStatusEl);
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
    const chartCanvas = document.getElementById("historyChart");
    // Ensure any previous inline height set by older JS runs is cleared so CSS rules apply
    if (chartCanvas && chartCanvas.style) chartCanvas.style.height = "";
    const chartStatusEl = document.getElementById("hi-chart-status");

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
      clearHistoryChart(chartStatusEl);
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
        exportBtn,
        chartCanvas,
        chartStatusEl
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

    // Provide an initial hint for the chart area
    clearHistoryChart(chartStatusEl);
  }

  // Expose to app.js
  window.initHistoryModule = initHistoryModule;
})();
