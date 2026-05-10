#pragma once

#include <stdint.h>

typedef void (*HR_Spo2Measure_done_cb_t)(int spo2, int hr);

void HR_Spo2Measure_Init();
void HR_Spo2Measure_Start(HR_Spo2Measure_done_cb_t cb);
