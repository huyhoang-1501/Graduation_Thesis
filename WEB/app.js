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

function makeUserModeId() {
  const min = 10000;
  const max = 99999;
  const span = max - min + 1;

  if (window.crypto?.getRandomValues) {
    const arr = new Uint32Array(1);
    window.crypto.getRandomValues(arr);
    return String(min + (arr[0] % span));
  }

  return String(min + Math.floor(Math.random() * span));
}

function normalizeUserModeId(raw) {
  return String(raw || "").replace(/\D/g, "").slice(0, 5);
}

function normalizeDeviceId(raw) {
  return String(raw || "").trim().toUpperCase();
}

// map an identifier (username or full email) to an email usable by Firebase
function normalizeIdentifierToEmail(raw) {
  const v = String(raw || "").trim();
  if (!v) return "";
  // if looks like an email, return as-is
  if (v.indexOf('@') !== -1) return v;
  // otherwise treat as username -> synthetic local email
  return v + '@local.app';
}

function isValidDeviceId(deviceId) {
  return /^(UTE-2026|DEV-[0-9A-Z]{8,16})$/.test(deviceId);
}

function getCurrentUserUid() {
  return auth.currentUser?.uid || "";
}

// Claim / mark a device as linked by the current dashboard user.
// - deviceId: device identifier (required)
// - patientId: optional patientId to associate with the device
// - setMode: if true (default) set mode:'user', set to false to leave mode unchanged
// Returns: Promise<boolean> true on success, false on error
async function claimDevice(deviceId, patientId, setMode = false) {
  if (!deviceId) {
    console.error('claimDevice: deviceId required');
    return false;
  }
  const ownerUid = getCurrentUserUid();
  if (!ownerUid) {
    console.error('claimDevice: no authenticated user');
    return false;
  }

  const updates = {
    ownerUid,
    linked: true,
    status: 'linked',
    updatedAt: firebase.database.ServerValue.TIMESTAMP
  };
  if (patientId) updates.patientId = patientId;
  // Do not change device 'mode' by default to keep device-sent fields minimal.
  if (setMode) updates.mode = 'user';

  try {
    await db.ref('devices/' + deviceId).update(updates);
    return true;
  } catch (err) {
    console.error('claimDevice error:', err);
    return false;
  }
}

// ========== TOGGLE LOGIN / REGISTER FORM ==========
document.getElementById("show-register")?.addEventListener("click", e => {
  e.preventDefault();
  document.getElementById("login-form").style.display = "none";
  document.getElementById("register-form").style.display = "block";
  // Ensure reset page is hidden when opening register
  const rf = document.getElementById("reset-form");
  if (rf) rf.style.display = "none";
  // Update the big title to reflect current pane (mobile/desktop)
  const titleEl = document.querySelector('.login-title h2');
  if (titleEl) titleEl.textContent = 'ĐĂNG KÝ';
});

document.getElementById("show-login")?.addEventListener("click", e => {
  e.preventDefault();
  document.getElementById("register-form").style.display = "none";
  document.getElementById("login-form").style.display = "block";
  // Ensure reset page is hidden when returning to login
  const rf = document.getElementById("reset-form");
  if (rf) rf.style.display = "none";
  // Restore main title
  const titleEl = document.querySelector('.login-title h2');
  if (titleEl) titleEl.textContent = 'ĐĂNG NHẬP';
});

// ========== REGISTER (username -> synthetic email)
// ========== REGISTER (email required)
document.getElementById("register-btn")?.addEventListener("click", () => {
  const identifier = document.getElementById("identifier-register").value.trim();
  const pass = document.getElementById("password-register").value;
  const passConfirm = document.getElementById("password-register-confirm").value;

  if (!identifier || !pass) return alert("Vui lòng nhập Tên đăng nhập/Email và mật khẩu!");
  if (pass.length < 6) return alert("Mật khẩu phải ≥ 6 ký tự!");
  if (pass !== passConfirm) return alert("Mật khẩu và xác nhận mật khẩu không khớp.");

  let regEmail = "";
  let displayName = "";

  if (identifier.indexOf('@') !== -1) {
    // Treat as email
    if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(identifier)) return alert("Vui lòng nhập một địa chỉ email hợp lệ.");
    regEmail = identifier;
    displayName = identifier.split('@')[0];
  } else {
    // Treat as username
    if (!/^[a-zA-Z0-9._-]{3,20}$/.test(identifier)) return alert("Tên đăng nhập chỉ gồm chữ, số, . _ - và 3-20 ký tự.");
    regEmail = identifier + "@local.app";
    displayName = identifier;
  }

  auth.createUserWithEmailAndPassword(regEmail, pass)
    .then(async (userCred) => {
      // update display name
      await userCred.user.updateProfile({ displayName });

      // If registered with a real email, send verification email (non-blocking)
      try {
        if (!regEmail.endsWith('@local.app')) {
          await userCred.user.sendEmailVerification();
          // Inform user gently; do not block usage
          alert('Đăng ký thành công! Một email xác thực đã được gửi (không bắt buộc). Bạn vẫn có thể đăng nhập.');
        } else {
          alert('Đăng ký thành công! Bạn có thể đăng nhập bằng tên đăng nhập và mật khẩu.');
        }
      } catch (err) {
        console.warn('Không gửi được email xác thực:', err);
        alert('Đăng ký thành công! (Không gửi được email xác thực). Bạn vẫn có thể đăng nhập.');
      }
    })
    .catch(err => alert("Lỗi đăng ký: " + err.message));
});

// ========== USERNAME SIGN IN (maps to synthetic email)
document.getElementById("username-signin-btn")?.addEventListener("click", () => {
  const identifier = document.getElementById("username-login").value.trim();
  const pass = document.getElementById("password-login").value;

  if (!identifier || !pass) return alert("Vui lòng nhập tên đăng nhập / email và mật khẩu!");
  const email = normalizeIdentifierToEmail(identifier);

  auth.signInWithEmailAndPassword(email, pass)
    .then((userCred) => {
      // Login successful. Email verification is not required; proceed.
    })
    .catch(err => {
      console.error('Sign-in error:', err);
      alert("Sai tên đăng nhập hoặc mật khẩu!");
    });
});

// ========== FORGOT PASSWORD / RESET ==========
document.getElementById("forgot-password-link")?.addEventListener("click", (e) => {
  e.preventDefault();
  // Hide other auth panes and show the standalone reset page
  const loginForm = document.getElementById("login-form");
  const registerForm = document.getElementById("register-form");
  const resetForm = document.getElementById("reset-form");
  if (loginForm) loginForm.style.display = 'none';
  if (registerForm) registerForm.style.display = 'none';
  if (resetForm) resetForm.style.display = 'block';
  // Update main title to indicate reset mode
  const titleEl = document.querySelector('.login-title h2');
  if (titleEl) titleEl.textContent = 'KHÔI PHỤC MẬT KHẨU';
});

document.getElementById("reset-cancel-btn")?.addEventListener("click", () => {
  // Close reset page and return to login
  const resetForm = document.getElementById("reset-form");
  if (resetForm) resetForm.style.display = 'none';
  const loginForm = document.getElementById("login-form");
  if (loginForm) loginForm.style.display = 'block';
  // Restore main title
  const titleEl = document.querySelector('.login-title h2');
  if (titleEl) titleEl.textContent = 'ĐĂNG NHẬP';
});

// (removed explicit back button - reset flow returns to login automatically)

document.getElementById("reset-btn")?.addEventListener("click", async () => {
  const id = document.getElementById("reset-identifier").value.trim();
  if (!id) return alert("Vui lòng nhập tên đăng nhập hoặc email để khôi phục mật khẩu.");

  const email = normalizeIdentifierToEmail(id);
  // If the identifier was a plain username, normalizeIdentifierToEmail
  // returns a synthetic local email (username@local.app). Those addresses
  // are not real mailboxes — inform user instead of trying to send.
  if (email.endsWith('@local.app')) {
    alert('Tài khoản này được tạo bằng tên đăng nhập (không phải email).\n' +
          'Khôi phục mật khẩu qua email không khả dụng cho tài khoản nội bộ.\n' +
          'Bạn có thể đăng ký lại bằng một email thực hoặc liên hệ quản trị viên để được hỗ trợ.');
    return;
  }

  try {
    await auth.sendPasswordResetEmail(email);
    alert('Một email đặt lại mật khẩu đã được gửi tới: ' + email + '. Vui lòng kiểm tra hộp thư (và cả mục spam).');
    const resetForm = document.getElementById("reset-form");
    if (resetForm) resetForm.style.display = 'none';
    const loginForm = document.getElementById("login-form");
    if (loginForm) loginForm.style.display = 'block';
    // Restore main title after successful send
    const titleEl = document.querySelector('.login-title h2');
    if (titleEl) titleEl.textContent = 'ĐĂNG NHẬP';
  } catch (err) {
    console.error('Lỗi khi gửi email khôi phục:', err);
    // Friendly messages for common errors
    if (err.code === 'auth/user-not-found') {
      alert('Không tìm thấy tài khoản liên quan. Hãy kiểm tra tên đăng nhập / email.');
    } else if (err.code === 'auth/invalid-email') {
      alert('Địa chỉ email không hợp lệ. Vui lòng kiểm tra lại.');
    } else {
      alert('Lỗi khi gửi email khôi phục: ' + err.message);
    }
  }
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

// show/hide password checkbox for register form
document.getElementById('register-show-pass')?.addEventListener('change', (e) => {
  const show = e.target.checked;
  const p = document.getElementById('password-register');
  const cp = document.getElementById('password-register-confirm');
  if (p) p.type = show ? 'text' : 'password';
  if (cp) cp.type = show ? 'text' : 'password';
});

// ========== ON AUTH STATE CHANGED ==========
const loginSection = document.getElementById("login-section");
const appRoot = document.getElementById("app");

auth.onAuthStateChanged(user => {
  if (user) {
    // Đã đăng nhập (email verification is not enforced)
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
  const displayName = user?.displayName || (email ? email.split("@")[0] : "");

  // show friendly name (username) in header and sidebar; fallback to email
  document.getElementById("current-user-email").textContent = displayName || email;
  document.getElementById("sidebar-user-name").textContent = displayName || "Người dùng";

  initSidebarNavigation();
  initMobileMenu();
  initSwipeHandlers();
  detectPwaMode();
  initOverview();
  initOtherPages();
  initPatientSelectors();
  initDeviceBindingModule();
  initPatientsModule(); 
  initSettingsModule();

  // Wire "View device by ID" button in Overview header
  const viewBtn = document.getElementById('ov-view-device-btn');
  const devInput = document.getElementById('ov-device-id-input');
  if (viewBtn && devInput) {
    viewBtn.addEventListener('click', () => {
      const raw = devInput.value || '';
      const deviceId = normalizeDeviceId(raw);
      if (!deviceId) return alert('Vui lòng nhập DEVICE ID hợp lệ.');
      showDeviceById(deviceId);
    });
  }


// Show device-level info without requiring ownership. Detaches previous listener.
function showDeviceById(deviceId) {
  try {
    // Detach any previous fine-grained listeners
    if (window._currentDeviceRefs && Array.isArray(window._currentDeviceRefs)) {
      window._currentDeviceRefs.forEach(it => {
        try { it.ref.off(it.event || 'value', it.handler); } catch (e) { /* ignore */ }
      });
    }
    window._currentDeviceRefs = [];

    const base = 'devices/' + deviceId;
    const ONLINE_THRESHOLD = 3 * 60 * 1000; // 3 minutes

    // Initial one-time read to populate UI quickly
    db.ref(base).once('value').then(snap => {
      const d = snap.val() || {};
      // battery
      const battEl = document.getElementById('ov-battery');
      if (battEl) battEl.textContent = (d.batteryPercent != null) ? (d.batteryPercent + ' %') : '-- %';

      // lastSeen
      const lastEl = document.getElementById('ov-lastseen');
      const freshnessEl = document.getElementById('ov-freshness');
      const now = Date.now();
      const lastSeenRaw = d.lastSeen || d.lastSeenAt || 0;
      const lastSeen = Number(lastSeenRaw) || 0;
      if (lastEl) {
        if (lastSeen) lastEl.textContent = new Date(lastSeen).toLocaleString();
        else lastEl.textContent = '--';
      }
      if (freshnessEl) {
        if (!lastSeen) freshnessEl.textContent = '--';
        else {
          const sec = Math.floor((now - lastSeen) / 1000);
          if (sec < 60) freshnessEl.textContent = 'Cập nhật mới nhất';
          else if (sec < 3600) freshnessEl.textContent = 'Cách đây ' + Math.floor(sec / 60) + ' phút';
          else freshnessEl.textContent = 'Cách đây ' + Math.floor(sec / 3600) + ' giờ';
        }
      }

      // badge & network
      const badge = document.getElementById('ov-device-badge');
      let derivedStatus = (d.status || 'offline');
      if (lastSeen) {
        const age = Date.now() - lastSeen;
        derivedStatus = (age < ONLINE_THRESHOLD) ? 'online' : 'offline';
      }
      if (badge) {
        badge.textContent = derivedStatus.toUpperCase();
        badge.classList.remove('badge-online','badge-offline');
        if (derivedStatus === 'online') badge.classList.add('badge-online');
        else badge.classList.add('badge-offline');
      }
      const netEl = document.getElementById('ov-network');
      if (netEl) netEl.textContent = d.network || '--';

      // If device is linked to a patient, remember for measurement listener
      window._devicePatientId = d.patientId || '';
    }).catch(err => console.warn('device initial read failed', err));

    // Small-field realtime listeners (cheaper than 'value' for whole node)
    const addFieldListener = (field, cb) => {
      const r = db.ref(base + '/' + field);
      const handler = snap => cb(snap.val());
      r.on('value', handler);
      window._currentDeviceRefs.push({ ref: r, handler, event: 'value' });
    };

    addFieldListener('batteryPercent', v => {
      const battEl = document.getElementById('ov-battery');
      if (battEl) battEl.textContent = (v != null) ? (v + ' %') : '-- %';
    });

    addFieldListener('lastSeen', v => {
      const last = Number(v) || 0;
      const lastEl = document.getElementById('ov-lastseen');
      const freshnessEl = document.getElementById('ov-freshness');
      if (lastEl) lastEl.textContent = last ? new Date(last).toLocaleString() : '--';
      if (freshnessEl) {
        if (!last) freshnessEl.textContent = '--';
        else {
          const sec = Math.floor((Date.now() - last) / 1000);
          if (sec < 60) freshnessEl.textContent = 'Cập nhật mới nhất';
          else if (sec < 3600) freshnessEl.textContent = 'Cách đây ' + Math.floor(sec / 60) + ' phút';
          else freshnessEl.textContent = 'Cách đây ' + Math.floor(sec / 3600) + ' giờ';
        }
      }
    });

    addFieldListener('status', v => {
      const badge = document.getElementById('ov-device-badge');
      if (!badge) return;
      const lastSeenText = document.getElementById('ov-lastseen')?.textContent || '';
      const derived = (v || 'offline');
      badge.textContent = derived.toUpperCase();
      badge.classList.remove('badge-online','badge-offline');
      if (derived === 'online') badge.classList.add('badge-online');
      else badge.classList.add('badge-offline');
    });

    addFieldListener('network', v => {
      const netEl = document.getElementById('ov-network');
      if (netEl) netEl.textContent = v || '--';
    });

    // Measurements: listen to latest measurement for patient if available
    // We'll create a measurement listener once we know patientId (from initial read)
    // Poll for patientId availability (short-lived) and then attach listener
    const attachMeasWhenReady = () => {
      const pid = window._devicePatientId || '';
      if (!pid) return;

      // Ensure chart is reset to the patient context for this device
      try { window.resetOverviewMiniChart(pid); } catch (e) { /* ignore */ }

      const applyMeas = (rec) => {
        const r = (rec && typeof rec === 'object') ? rec : {};
        if (r.hr != null) {
          const hrEl = document.getElementById('ov-hr-value');
          if (hrEl) hrEl.textContent = r.hr + ' bpm';
        }
        if (r.spo2 != null) {
          const spoEl = document.getElementById('ov-spo2-value');
          if (spoEl) spoEl.textContent = r.spo2 + ' %';
        }
        if (r.bpSys != null && r.bpDia != null) {
          const bpEl = document.getElementById('ov-bp-value');
          if (bpEl) bpEl.textContent = r.bpSys + ' / ' + r.bpDia + ' mmHg';
        }
        try { window.pushOverviewMiniChartSample(r, pid); } catch (e) { /* ignore */ }
      };

      try {
        // Support both structures:
        // - legacy push: measurements/<pid>/<pushId>
        // - current firmware: measurements/<pid>/latest
        const legacyRef = db.ref('measurements/' + pid).limitToLast(1);
        const legacyHandler = snap => {
          const m = snap.val();
          const record = (typeof m === 'object' && m !== null) ? (Object.values(m)[0] || Object.values(m)) : m;
          applyMeas(record);
        };
        legacyRef.on('child_added', legacyHandler);
        window._currentDeviceRefs.push({ ref: legacyRef, handler: legacyHandler, event: 'child_added' });

        const latestRef = db.ref('measurements/' + pid + '/latest');
        const latestHandler = snap => { applyMeas(snap.val() || {}); };
        latestRef.on('value', latestHandler);
        window._currentDeviceRefs.push({ ref: latestRef, handler: latestHandler, event: 'value' });
      } catch (e) {
        console.warn('attachMeasWhenReady failed', e);
      }
    };

    // Try attaching measurement listener after small delays until patientId available
    let tries = 0;
    const tryInterval = setInterval(() => {
      tries++;
      if (window._devicePatientId) {
        attachMeasWhenReady();
        clearInterval(tryInterval);
      } else if (tries > 6) {
        clearInterval(tryInterval);
      }
    }, 500);

    // Clear patient selector selection to indicate device view
    const ovSel = document.getElementById('ov-patient-select');
    if (ovSel) ovSel.value = '';
    // mark that user is viewing device by ID explicitly so patient selection
    // does not override this view
    window._deviceViewMode = 'device';
    window._deviceViewedId = deviceId;
  } catch (err) {
    console.error('showDeviceById error', err);
    alert('Lỗi khi đọc device từ Firebase.');
  }
}
  // If user's email is not verified, show a gentle banner with resend option (non-blocking)
  try {
    const email = user?.email || auth.currentUser?.email || '';
    const isLocal = email.endsWith('@local.app');
    if (!isLocal && !user.emailVerified) {
      if (!document.getElementById('email-verify-banner')) {
        const mainEl = document.querySelector('.main');
        if (mainEl) {
          const b = document.createElement('div');
          b.id = 'email-verify-banner';
          b.style.cssText = 'background:#fff3cd;border:1px solid #ffeeba;padding:10px;margin-bottom:10px;border-radius:6px;display:flex;justify-content:space-between;align-items:center;gap:12px;';
          b.innerHTML = `<div style="color:#7a4f01;">Email của bạn chưa được xác thực. Chúng tôi đã gửi email xác thực — bạn vẫn có thể dùng ứng dụng.</div><div><button id="resend-verify-btn" class="btn-ghost" style="margin-right:8px;">Gửi lại</button><button id="dismiss-verify-banner" class="btn-ghost">Đóng</button></div>`;
          mainEl.insertBefore(b, mainEl.firstChild);
          document.getElementById('resend-verify-btn')?.addEventListener('click', async () => {
            try {
              await auth.currentUser.sendEmailVerification();
              alert('Email xác thực đã được gửi lại. Vui lòng kiểm tra hộp thư.');
            } catch (err) {
              alert('Lỗi khi gửi email xác thực: ' + err.message);
            }
          });
          document.getElementById('dismiss-verify-banner')?.addEventListener('click', () => {
            const el = document.getElementById('email-verify-banner');
            if (el) el.remove();
          });
        }
      }
    }
  } catch (err) {
    console.warn('Error checking email verification status:', err);
  }
}

// ========== MOBILE MENU (OFF-CANVAS SIDEBAR) ==========
function initMobileMenu() {
  // Ensure we only initialize once
  if (window._mobileMenuInit) return;
  window._mobileMenuInit = true;

  const topbarLeft = document.querySelector('.topbar-left');
  const menuBtn = document.querySelector('.menu-btn');
  const overlay = document.querySelector('.overlay');

  // Toggle body class to open/close sidebar
  const toggle = () => {
    const open = document.body.classList.toggle('sidebar-open');
    if (menuBtn) menuBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
  };
  const close = () => {
    document.body.classList.remove('sidebar-open');
    if (menuBtn) menuBtn.setAttribute('aria-expanded', 'false');
  };

  if (menuBtn) menuBtn.addEventListener('click', toggle);
  if (overlay) overlay.addEventListener('click', close);

  // Close sidebar when clicking a navigation item (mobile)
  document.addEventListener('click', (e) => {
    if (e.target.closest('.sidebar-item')) {
      // small delay to allow navigation to take effect visually
      setTimeout(close, 150);
    }
  });

  // Close with Escape key
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') close();
  });

  // When resizing to desktop, ensure sidebar-open removed
  window.addEventListener('resize', () => {
    if (window.innerWidth > 768) close();
  });
}

// ========== LOGOUT ==========
function logout() {
  auth.signOut()
    .then(() => {
      const usernameLogin = document.getElementById("username-login");
      const passLogin = document.getElementById("password-login");
      if (usernameLogin) usernameLogin.value = "";
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
    location: "VỊ TRÍ CỦA THIẾT BỊ",
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
    // If user opened the Location page, ensure the Leaflet map is initialized
    if (pageName === 'location') {
      // small delay so the container is visible/animated; then set height and init or resize
      setTimeout(() => {
        try {
          adjustMapHeight();
          if (!leafletMap) initMap();
          else leafletMap.invalidateSize();
        } catch (e) { /* ignore */ }
      }, 200);
    }
  }
    // no global page helper required (bottom-nav removed)
}

  // bottom navigation removed

  // ========== PWA DETECTION (switch to bottom nav) ==========
  function detectPwaMode() {
    const isStandalone = window.matchMedia('(display-mode: standalone)').matches || window.navigator.standalone === true || location.search.indexOf('pwa=1') !== -1;
    if (isStandalone) {
      document.body.classList.add('pwa-mode');
    } else {
      document.body.classList.remove('pwa-mode');
    }
  }

  // ========== SWIPE HANDLERS for opening/closing sidebar ==========
  function initSwipeHandlers() {
    if (window._swipeInit) return;
    window._swipeInit = true;

    let startX = 0;
    let startY = 0;
    let tracking = false;

    document.addEventListener('touchstart', (e) => {
      if (window.innerWidth > 768) return;
      const t = e.touches[0];
      startX = t.clientX;
      startY = t.clientY;
      tracking = true;
    }, { passive: true });

    document.addEventListener('touchmove', (e) => {
      if (!tracking) return;
      const t = e.touches[0];
      const dx = t.clientX - startX;
      const dy = Math.abs(t.clientY - startY);
      // ignore vertical scrolls
      if (dy > 30) { tracking = false; return; }

      // swipe right from left edge
      if (startX < 30 && dx > 50) {
        document.body.classList.add('sidebar-open');
        const menuBtn = document.querySelector('.menu-btn');
        if (menuBtn) menuBtn.setAttribute('aria-expanded', 'true');
        tracking = false;
      }

      // swipe left to close
      if (document.body.classList.contains('sidebar-open') && dx < -50) {
        document.body.classList.remove('sidebar-open');
        const menuBtn = document.querySelector('.menu-btn');
        if (menuBtn) menuBtn.setAttribute('aria-expanded', 'false');
        tracking = false;
      }
    }, { passive: true });

    document.addEventListener('touchend', () => { tracking = false; });
  }

// ========== OVERVIEW: CHART + MAP ==========
let miniChart;
let leafletMap;
let leafletMarker;

// Expose chart helpers so other modules (e.g., modules.patients.js) can update
// the Overview mini chart even though `miniChart` is declared with `let` here.
window._miniChart = null;
window._overviewChartPatientId = '';
window._overviewChartLastKey = '';

function resetOverviewMiniChart(patientId = '') {
  const chart = window._miniChart;
  if (patientId) window._overviewChartPatientId = String(patientId);
  window._overviewChartLastKey = '';
  if (!chart) return;
  try {
    chart.data.labels = [];
    chart.data.datasets?.forEach(ds => { ds.data = []; });
    chart.update();
  } catch (e) {
    // ignore
  }
}

function pushOverviewMiniChartSample(rec, patientId = '') {
  const chart = window._miniChart;
  if (!chart || !rec || typeof rec !== 'object') return;

  const pid = String(patientId || window._overviewChartPatientId || '');
  // If switching patient context, reset chart automatically
  if (pid && pid !== String(window._overviewChartPatientId || '')) {
    resetOverviewMiniChart(pid);
  }

  const hr = Number.isFinite(Number(rec.hr)) ? Number(rec.hr) : null;
  const spo2 = Number.isFinite(Number(rec.spo2)) ? Number(rec.spo2) : null;
  const bpSys = Number.isFinite(Number(rec.bpSys)) ? Number(rec.bpSys) : null;
  const bpDia = Number.isFinite(Number(rec.bpDia)) ? Number(rec.bpDia) : null;

  // Only plot if there is at least one numeric metric
  const hasAny = (hr !== null) || (spo2 !== null) || (bpSys !== null) || (bpDia !== null);
  if (!hasAny) return;

  const ts = (rec.timestamp != null) ? Number(rec.timestamp) : NaN;
  const tsKey = Number.isFinite(ts) && ts > 0 ? String(ts) : '';
  const key = pid + ':' + tsKey + ':' + [hr, spo2, bpSys, bpDia].join(',');
  if (key && key === window._overviewChartLastKey) return;
  window._overviewChartLastKey = key;

  try {
    const MAX_POINTS = 60;
    chart.data.labels.push('');
    // Keep datasets aligned; push null when value missing
    if (chart.data.datasets[0]) chart.data.datasets[0].data.push(hr);
    if (chart.data.datasets[1]) chart.data.datasets[1].data.push(spo2);
    if (chart.data.datasets[2]) chart.data.datasets[2].data.push(bpSys);
    if (chart.data.datasets[3]) chart.data.datasets[3].data.push(bpDia);

    if (chart.data.labels.length > MAX_POINTS) {
      chart.data.labels.shift();
      chart.data.datasets.forEach(ds => ds.data.shift());
    }
    chart.update();
  } catch (e) {
    // ignore chart errors
  }
}

// make helpers available for other scripts
window.resetOverviewMiniChart = resetOverviewMiniChart;
window.pushOverviewMiniChartSample = pushOverviewMiniChartSample;

function initOverviewEqualCardHeights() {
  // Keep "Thông báo" card height equal to "Trạng thái thiết bị" on mobile.
  // We do this via JS because pure CSS can't reliably match heights when cards stack.
  if (window._ovEqualHeightInit) return;
  window._ovEqualHeightInit = true;

  const deviceCard = document.querySelector("#page-overview .device-card");
  const notifyCard = document.getElementById("ov-notify-card");
  if (!deviceCard || !notifyCard) return;

  const mq = window.matchMedia("(max-width: 768px)");

  let rafId = 0;
  function apply() {
    rafId = 0;

    // Clear on desktop
    if (!mq.matches) {
      deviceCard.style.height = "";
      notifyCard.style.height = "";
      return;
    }

    // Measure natural heights first
    deviceCard.style.height = "";
    notifyCard.style.height = "";

    const devH = deviceCard.getBoundingClientRect().height;
    const notH = notifyCard.getBoundingClientRect().height;
    const h = Math.max(devH || 0, notH || 0);

    if (h > 0) {
      const px = Math.round(h) + "px";
      deviceCard.style.height = px;
      notifyCard.style.height = px;
    }
  }

  function schedule() {
    if (rafId) return;
    rafId = requestAnimationFrame(apply);
  }

  // Initial
  schedule();

  // Update on viewport changes
  window.addEventListener("resize", schedule, { passive: true });
  if (mq.addEventListener) mq.addEventListener("change", schedule);
  else if (mq.addListener) mq.addListener(schedule);

  // Update when content changes (device last seen/battery, notify text/count)
  if (window.ResizeObserver) {
    const ro = new ResizeObserver(() => schedule());
    ro.observe(deviceCard);
    ro.observe(notifyCard);
  }

  if (window.MutationObserver) {
    const mo = new MutationObserver(() => schedule());
    mo.observe(deviceCard, { subtree: true, childList: true, characterData: true });
    mo.observe(notifyCard, { subtree: true, childList: true, characterData: true });
  }
}

function initOverview() {
  initMiniChart();
  initMap();
  initOverviewEqualCardHeights();
}

function initDeviceBindingModule() {
  const deviceIdInput = document.getElementById("bind-device-id");
  const pairCodeInput = document.getElementById("bind-pair-code");
  const createBtn = document.getElementById("device-create-btn");
  const linkBtn = document.getElementById("device-link-btn");
  const statusEl = document.getElementById("device-bind-status");

  // require at least pairCode input and the action button
  if (!pairCodeInput || !linkBtn) return;

  // Mode Online ID chỉ 5 số
  pairCodeInput.addEventListener("beforeinput", (e) => {
    if (e.data && /\D/.test(e.data)) {
      e.preventDefault();
    }
  });

  pairCodeInput.addEventListener("input", (e) => {
    e.target.value = normalizeUserModeId(e.target.value);
  });
  pairCodeInput.setAttribute("inputmode", "numeric");
  pairCodeInput.setAttribute("maxlength", "5");
  pairCodeInput.placeholder = "Mã 5 số cho Mode Online";

  // If deviceIdInput and createBtn exist (legacy layout), keep full behavior
  if (deviceIdInput && createBtn) {
    // DEVICE ID input normalization
    deviceIdInput.addEventListener("input", (e) => {
      e.target.value = normalizeDeviceId(e.target.value);
    });
    deviceIdInput.removeAttribute("inputmode");
    deviceIdInput.removeAttribute("maxlength");

    // show concise instructions only if statusEl present
    if (statusEl) {
      statusEl.innerHTML = "Tạo mã Mode Online 5 số để nhập trên thiết bị.";
      statusEl.style.color = "#6b7280";
    }

    createBtn.addEventListener("click", async () => {
      const deviceId = normalizeDeviceId(deviceIdInput.value);
      let patientId = normalizeUserModeId(pairCodeInput.value);
      const ownerUid = getCurrentUserUid();

      if (!deviceId) return alert("Vui lòng nhập DEVICE ID.");
      if (!isValidDeviceId(deviceId)) return alert("DEVICE ID không hợp lệ.");
      if (patientId.length !== 5) patientId = makeUserModeId();
      if (!ownerUid) return alert("Phiên đăng nhập không hợp lệ. Vui lòng đăng nhập lại.");

      deviceIdInput.value = deviceId;
      pairCodeInput.value = patientId;

      try {
        const deviceSnap = await db.ref("devices/" + deviceId).once("value");
        const oldDevice = deviceSnap.val();
        if (oldDevice?.ownerUid && oldDevice.ownerUid !== ownerUid) {
          return alert("Mã thiết bị này đang thuộc tài khoản khác.");
        }

        await db.ref("devices/" + deviceId).set({
          deviceId,
          ownerUid,
          patientId,
          status: "ready_for_user_mode",
          linked: true,
          mode: "user",
          createdAt: firebase.database.ServerValue.TIMESTAMP
        });

        const patientSnap = await db.ref("patients/" + patientId).once("value");
        const oldPatient = patientSnap.val();
        if (oldPatient?.ownerUid && oldPatient.ownerUid !== ownerUid) {
          return alert("Mã bệnh nhân này đang thuộc tài khoản khác.");
        }

        // Do not store deviceId inside the patients node. Devices are tracked
        // under /devices and linked to a patient via devices/<deviceId>.patientId.
        // Keeping deviceId in two places leads to duplication and sync issues.
        await db.ref("patients/" + patientId).set({
          name: oldPatient?.name || ("Bệnh nhân " + patientId.slice(-4)),
          age: oldPatient?.age || "",
          sex: oldPatient?.sex || "Nam",
          ownerUid,
          mode: "user",
          status: oldPatient?.status || "offline",
          updatedAt: firebase.database.ServerValue.TIMESTAMP
        });

        // Ensure there's a settings entry for this patientId. We keep settings keyed by patientId
        // (safer than using patient name as key because names may not be unique or key-safe).
        try {
          await db.ref('settings/' + patientId).set({
            patientId,
            name: oldPatient?.name || ("Bệnh nhân " + patientId.slice(-4)),
            thresholds: oldPatient?.thresholds || {},
            phone: oldPatient?.phone || ''
          });
        } catch (err) {
          console.warn('Không lưu được settings mặc định cho bệnh nhân:', err);
        }

        if (statusEl) {
          statusEl.textContent = "Đã lưu DEVICE ID " + deviceId + " và tạo mã Mode Online: " + patientId + ". Hãy nhập mã 5 số này trên thiết bị.";
          statusEl.style.color = "#16a34a";
        } else {
          alert("Đã tạo mã Mode Online: " + patientId + ".");
        }
      } catch (err) {
        console.error(err);
        alert("Lỗi tạo thiết bị: " + err.message);
      }
    });

    linkBtn.addEventListener("click", async () => {
      const deviceId = normalizeDeviceId(deviceIdInput.value);
      const patientId = normalizeUserModeId(pairCodeInput.value);

      if (!deviceId) return alert("Hãy nhập DEVICE ID trước.");
      if (!isValidDeviceId(deviceId)) return alert("DEVICE ID không hợp lệ.");
      if (patientId.length !== 5) return alert("Mã Mode Online phải đúng 5 số. Hãy bấm Lưu DEVICE ID để tạo mã.");

      const ownerUid = getCurrentUserUid();

      try {
        const deviceSnap = await db.ref("devices/" + deviceId).once("value");
        const deviceData = deviceSnap.val();
        if (!deviceData) return alert("Mã chưa được tạo trên dashboard.");
        if (deviceData.ownerUid && deviceData.ownerUid !== ownerUid) return alert("Mã này thuộc tài khoản khác.");

        await db.ref("devices/" + deviceId).update({
          ownerUid,
          patientId,
          status: "linked",
          linked: true,
          mode: "user",
          updatedAt: firebase.database.ServerValue.TIMESTAMP
        });

        pairCodeInput.value = patientId;
        if (statusEl) {
          statusEl.textContent = "Đã xác nhận Mode Online cho ID " + patientId + ". Thiết bị sẽ đẩy dữ liệu lên dashboard của tài khoản này.";
          statusEl.style.color = "#2563eb";
        } else {
          alert("Đã xác nhận Mode Online cho ID " + patientId + ".");
        }
      } catch (err) {
        console.error(err);
        alert("Lỗi xác nhận thiết bị: " + err.message);
      }
    });

    return; // legacy flow handled
  }

  // Simplified flow: no deviceId input present -> clicking the button *confirms* the 5-digit Mode Online ID
  // It will NOT create the patient immediately. User must fill Name/Age/Sex and press Thêm to actually add.
  linkBtn.addEventListener("click", async () => {
    let patientId = normalizeUserModeId(pairCodeInput.value);
    const ownerUid = getCurrentUserUid();

    if (!ownerUid) return alert("Vui lòng đăng nhập để tạo mã.");
    if (patientId.length !== 5) patientId = makeUserModeId();

    try {
      const patientSnap = await db.ref("patients/" + patientId).once("value");
      const oldPatient = patientSnap.val();
      if (oldPatient?.ownerUid && oldPatient.ownerUid !== ownerUid) return alert("Mã này thuộc tài khoản khác.");

      // Prefill the add-patient form with existing data if any, but DO NOT write to DB yet
      const nameInput = document.getElementById('pt-name');
      const ageInput = document.getElementById('pt-age');
      const sexInput = document.getElementById('pt-sex');
      const statusElLocal = document.getElementById('pt-status');

      if (nameInput) nameInput.value = oldPatient?.name || "";
      if (ageInput) ageInput.value = oldPatient?.age || "";
      if (sexInput) sexInput.value = oldPatient?.sex || "Nam";

      pairCodeInput.value = patientId;

      if (statusElLocal) {
        statusElLocal.textContent = "Mã ID " + patientId + " đã xác nhận. Điền tên/tuổi/giới tính rồi bấm 'Thêm' để lưu.";
        statusElLocal.style.color = "#2563eb";
      } else {
        alert("Mã ID " + patientId + " đã xác nhận. Điền tên/tuổi/giới tính rồi bấm 'Thêm' để lưu.");
      }
    } catch (err) {
      console.error(err);
      alert("Lỗi khi kiểm tra mã: " + err.message);
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
      datasets: [
        {
          label: "Nhịp Tim (bpm)",
          data: [],
          borderColor: "#ef4444",
          backgroundColor: "rgba(239,68,68,0.15)",
          fill: true,
          tension: 0.3,
          yAxisID: 'y'
        },
        {
          label: "SpO₂ (%)",
          data: [],
          borderColor: "#3b82f6",
          backgroundColor: "rgba(59,130,246,0.12)",
          fill: true,
          tension: 0.3,
          yAxisID: 'ySpo2'
        },
        {
          label: "BP Sys (mmHg)",
          data: [],
          borderColor: "#f59e0b",
          backgroundColor: "rgba(245,158,11,0.08)",
          fill: false,
          tension: 0.2,
          yAxisID: 'yBp'
        },
        {
          label: "BP Dia (mmHg)",
          data: [],
          borderColor: "#10b981",
          backgroundColor: "rgba(16,185,129,0.08)",
          fill: false,
          tension: 0.2,
          yAxisID: 'yBp'
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        x: { display: false },
        y: {
          // primary axis for HR
          type: 'linear',
          display: true,
          position: 'left',
          beginAtZero: false
        },
        ySpo2: {
          // SpO2 percentage (0-100)
          type: 'linear',
          display: true,
          position: 'right',
          suggestedMin: 70,
          suggestedMax: 100,
          grid: { drawOnChartArea: false }
        },
        yBp: {
          // Blood pressure axis
          type: 'linear',
          display: false,
          position: 'right',
          suggestedMin: 40,
          suggestedMax: 200,
          grid: { drawOnChartArea: false }
        }
      }
    }
  });

  // Expose the instance so other scripts can update it
  window._miniChart = miniChart;
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

  // Leaflet needs an invalidateSize call if container size changed; defer slightly
  setTimeout(() => {
    try { adjustMapHeight(); leafletMap.invalidateSize(); } catch (e) { /* ignore */ }
  }, 250);
}

function mockUpdateOverview() {
  document.getElementById("ov-hr-value").textContent = "78 bpm";
  document.getElementById("ov-bp-value").textContent = "120 / 80 mmHg";
  document.getElementById("ov-spo2-value").textContent = "97 %";

  // Status sub-lines for the stat cards were removed from the UI; skip setting them

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
    const hrData = [];
    const spo2Data = [];
    const bpSysData = [];
    const bpDiaData = [];
    for (let i = 0; i < 12; i++) {
      labels.push("");
      hrData.push(60 + Math.round(Math.random() * 30));
      spo2Data.push(94 + Math.round(Math.random() * 5));
      bpSysData.push(110 + Math.round(Math.random() * 20));
      bpDiaData.push(70 + Math.round(Math.random() * 15));
    }
    miniChart.data.labels = labels;
    miniChart.data.datasets[0].data = hrData;
    if (miniChart.data.datasets[1]) miniChart.data.datasets[1].data = spo2Data;
    if (miniChart.data.datasets[2]) miniChart.data.datasets[2].data = bpSysData;
    if (miniChart.data.datasets[3]) miniChart.data.datasets[3].data = bpDiaData;
    miniChart.update();
  }

  if (leafletMap && leafletMarker) {
    const lat = 10.85, lng = 106.77;
    leafletMarker.setLatLng([lat, lng]);
    leafletMap.setView([lat, lng], 15);
  }
}

// ========== SERVICE WORKER UPDATE NOTIFICATION ==========
function initSWUpdateHandler() {
  if (!('serviceWorker' in navigator)) return;

  navigator.serviceWorker.getRegistration().then(reg => {
    if (!reg) return;

    if (reg.waiting) {
      // there's an updated SW waiting to activate
      askUserToRefresh();
    }

    reg.addEventListener('updatefound', () => {
      const newSW = reg.installing;
      newSW.addEventListener('statechange', () => {
        if (newSW.state === 'installed' && navigator.serviceWorker.controller) {
          askUserToRefresh();
        }
      });
    });
  });

  // when the new SW takes control, reload so user sees latest
  navigator.serviceWorker.addEventListener('controllerchange', () => {
    window.location.reload();
  });
}

function askUserToRefresh() {
  // simple native confirm — can be replaced with nicer UI
  if (confirm('Bản cập nhật mới đã sẵn sàng. Tải lại để cập nhật?')) {
    navigator.serviceWorker.getRegistration().then(reg => {
      if (!reg || !reg.waiting) return;
      reg.waiting.postMessage({ type: 'SKIP_WAITING' });
    });
  }
}

// run SW handler early
initSWUpdateHandler();

// Helper cập nhật vị trí trên map theo data từ Realtime Database
function updateMapLocation(lat, lng) {
  if (!leafletMap || !leafletMarker) return;
  leafletMarker.setLatLng([lat, lng]);
  leafletMap.setView([lat, lng], 15);
}

// Adjust map container height to fill available viewport space
function adjustMapHeight() {
  const mapDiv = document.getElementById('map');
  if (!mapDiv) return;
  // distance from top of viewport to top of map
  const top = mapDiv.getBoundingClientRect().top;
  // leave a small bottom gap (20px)
  let avail = window.innerHeight - top - 20;
  if (avail < 260) avail = 260; // ensure usable minimum
  mapDiv.style.height = avail + 'px';
  // if map already exists, invalidate size so Leaflet redraws
  if (leafletMap) {
    try { leafletMap.invalidateSize(); } catch (e) { /* ignore */ }
  }
}

// Adjust chart canvas heights to fill available vertical space similar to the map
function adjustChartHeights() {
  const canvases = document.querySelectorAll('.panel-body canvas');
  canvases.forEach(canvas => {
    const rect = canvas.getBoundingClientRect();
    if (rect.top === 0 && rect.height === 0) return; // not in layout yet
    // leave a 20px bottom gap
    let avail = window.innerHeight - rect.top - 20;
    // ensure reasonable bounds
    if (avail < 260) avail = 260;
    if (avail > 900) avail = 900;
    // set style height so Chart.js (maintainAspectRatio:false) will fill
    canvas.style.height = avail + 'px';

    // if a Chart.js instance is attached to this canvas, trigger a resize
    try {
      const chart = Chart.getChart(canvas);
      if (chart) chart.resize();
    } catch (e) {
      // ignore if Chart.js not yet initialized for this canvas
    }
  });
}

// Resize handler with small debounce
window.addEventListener('resize', () => {
  if (window._mapResizeTimer) clearTimeout(window._mapResizeTimer);
  window._mapResizeTimer = setTimeout(() => {
    adjustMapHeight();
    // also adjust charts when window resizes
    try { adjustChartHeights(); } catch (e) { /* ignore */ }
  }, 120);
});

// Run chart height adjust on initial load and after overview init
window.addEventListener('load', () => {
  setTimeout(() => { try { adjustChartHeights(); } catch (e) { } }, 150);
});

function initOtherPages() {
  // Initialize optional pages/modules
  try {
    if (window.initHistoryModule) {
      window.initHistoryModule();
    }
  } catch (e) {
    console.warn('initHistoryModule failed', e);
  }
}
