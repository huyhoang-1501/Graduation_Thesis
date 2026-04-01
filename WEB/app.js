// ========== CONFIG FIREBASE AUTH ==========
// Giữ đúng config bạn đang dùng
const firebaseConfigAuth = {
  apiKey: "AIzaSyDH4COmuxxveRdD9zQXGJ29vHLR8SJuK78",
  authDomain: "project2-98ad4.firebaseapp.com",
  projectId: "project2-98ad4",
  storageBucket: "project2-98ad4.firebasestorage.app",
  messagingSenderId: "807037680757",
  appId: "1:807037680757:web:0b2444f98b6de11cde8c3c",
  measurementId: "G-PJYML82PZW"
};

const appAuth = firebase.initializeApp(firebaseConfigAuth, "authApp");
const auth = appAuth.auth();
auth.setPersistence(firebase.auth.Auth.Persistence.NONE);

const googleProvider = new firebase.auth.GoogleAuthProvider();
googleProvider.setCustomParameters({ prompt: 'select_account' });

// ========== TOGGLE LOGIN / REGISTER FORM ==========
document.getElementById("show-register")?.addEventListener("click", e => {
  e.preventDefault();
  document.getElementById("login-form").style.display = "none";
  document.getElementById("register-form").style.display = "block";
});
document.getElementById("show-login")?.addEventListener("click", e => {
  e.preventDefault();
  document.getElementById("register-form").style.display = "none";
  document.getElementById("login-form").style.display = "block";
});

// ========== REGISTER ==========
document.getElementById("register-btn")?.addEventListener("click", () => {
  const email = document.getElementById("email-register").value.trim();
  const pass  = document.getElementById("password-register").value;

  if (!email || !pass) return alert("Vui lòng nhập đầy đủ!");
  if (pass.length < 6) return alert("Mật khẩu phải ≥ 6 ký tự!");

  auth.createUserWithEmailAndPassword(email, pass)
    .then(() => {
      alert("Đăng ký thành công!");
      document.getElementById("login-section").style.display = "none";
      showApp();
    })
    .catch(err => alert("Lỗi đăng ký: " + err.message));
});

// ========== EMAIL SIGN IN ==========
document.getElementById("email-signin-btn")?.addEventListener("click", () => {
  const email = document.getElementById("email-login").value.trim();
  const pass  = document.getElementById("password-login").value;

  if (!email || !pass) return alert("Vui lòng nhập email và mật khẩu!");

  auth.signInWithEmailAndPassword(email, pass)
    .then(() => {
      document.getElementById("login-section").style.display = "none";
      showApp();
    })
    .catch(err => alert("Sai email hoặc mật khẩu!"));
});

// ========== GOOGLE SIGN IN ==========
document.getElementById("login-form")?.addEventListener("submit", function(e) {
  e.preventDefault();
  const btn = document.querySelector("#login-form .btn-login:last-of-type");
  const old = btn.innerHTML;
  btn.disabled = true;
  btn.innerHTML = `<i class="fas fa-spinner fa-spin"></i> Đang mở Google...`;

  auth.signInWithPopup(googleProvider)
    .then(() => {
      document.getElementById("login-section").style.display = "none";
      showApp();
    })
    .catch(err => {
      btn.disabled = false;
      btn.innerHTML = old;
      alert("Lỗi Google login: " + err.message);
    });
});

// ========== ON AUTH STATE CHANGED ==========
auth.onAuthStateChanged(user => {
  if (user) {
    document.getElementById("login-section").style.display = "none";
    showApp(user);
  }
});

// ========== SHOW APP DASHBOARD ==========
function showApp(user) {
  const app = document.getElementById("app");
  app.classList.remove("hidden");

  const email = user?.email || auth.currentUser?.email || "";
  document.getElementById("current-user-email").textContent = email;
  document.getElementById("sidebar-user-name").textContent = email || "Người dùng";

  initSidebarNavigation();
}

// ========== LOGOUT ==========
function logout() {
  auth.signOut().then(() => {
    document.getElementById("app").classList.add("hidden");
    document.getElementById("login-section").style.display = "flex";

    // clear input
    const emailLogin = document.getElementById('email-login');
    const passLogin = document.getElementById('password-login');
    if (emailLogin) emailLogin.value = '';
    if (passLogin) passLogin.value = '';

    console.log("Đăng xuất thành công");
  }).catch(err => alert("Lỗi đăng xuất: " + err.message));
}

// ========== SIDEBAR & TAB SWITCH ==========
function initSidebarNavigation() {
  const sideItems = document.querySelectorAll(".sidebar-item");
  const pages = document.querySelectorAll(".page");
  const tabBtns = document.querySelectorAll(".tab-btn");

  sideItems.forEach(btn => {
    btn.addEventListener("click", () => {
      sideItems.forEach(b => b.classList.remove("active"));
      btn.classList.add("active");

      const page = btn.dataset.page;
      showPage(page);
    });
  });

  tabBtns.forEach(btn => {
    btn.addEventListener("click", () => {
      tabBtns.forEach(b => b.classList.remove("active"));
      btn.classList.add("active");

      const page = btn.dataset.page;
      showPage(page);
    });
  });

  function showPage(pageName) {
    pages.forEach(p => p.classList.remove("active"));
    const target = document.getElementById("page-" + pageName);
    if (target) target.classList.add("active");
  }
}