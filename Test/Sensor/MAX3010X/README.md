# Tóm tắt giải thuật và công thức

1) Rolling average (BPM):

\[\text{BPM}_{avg} = \frac{1}{N} \sum_{i=0}^{N-1} \text{bpmBuf}[i]\]

2) Exponential Moving Average (EMA) để làm mượt:

\[\text{EMA}_t = \alpha \cdot x_t + (1-\alpha) \cdot \text{EMA}_{t-1}\]

- Tham số trong code: `HR_EMA_ALPHA` (mặc định 0.28), `SPO2_EMA_ALPHA` (mặc định 0.45).

3) Deadband (hysteresis hiển thị):

Nếu \(d\) là ngưỡng deadband và \(v_t\) là giá trị mới sau EMA, thì giá trị hiển thị \(V_t\):

\[V_t = \begin{cases}
V_{t-1} & \text{nếu } |v_t - V_{t-1}| < d \\
v_t & \text{ngược lại}
\end{cases}\]

- Ngưỡng mặc định trong code: `HR_DISPLAY_DEADBAND = 0.6` (bpm), `SPO2_DISPLAY_DEADBAND = 0.1` (%).

4) Khoảng thời gian hiển thị: `DISPLAY_INTERVAL_MS` (mặc định 5000 ms = 5s).

5) Phạm vi giá trị hợp lệ trước khi cập nhật EMA/buffer:

- HR: 30 — 220 bpm
- SpO2: 50 — 100 %
