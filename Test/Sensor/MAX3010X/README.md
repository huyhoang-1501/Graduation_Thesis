# Tóm tắt giải thuật và công thức

1) Rolling average (BPM):

BPM_avg = (1 / N) * sum_{i=0..N-1} bpmBuf[i]

2) Exponential Moving Average (EMA) để làm mượt:

EMA_t = alpha * x_t + (1 - alpha) * EMA_{t-1}

- Tham số trong code: `HR_EMA_ALPHA` (mặc định 0.28), `SPO2_EMA_ALPHA` (mặc định 0.45).

3) Deadband (hysteresis hiển thị):

Nếu d là ngưỡng deadband và v_t là giá trị mới sau EMA, thì giá trị hiển thị V_t:

Nếu |v_t - V_{t-1}| < d thì V_t = V_{t-1}  
Ngược lại V_t = v_t

- Ngưỡng mặc định trong code: `HR_DISPLAY_DEADBAND = 0.6` (bpm), `SPO2_DISPLAY_DEADBAND = 0.1` (%).

4) Khoảng thời gian hiển thị: `DISPLAY_INTERVAL_MS` (mặc định 5000 ms = 5s).

5) Phạm vi giá trị hợp lệ trước khi cập nhật EMA/buffer:

- HR: 30 — 220 bpm
- SpO2: 50 — 100 %
