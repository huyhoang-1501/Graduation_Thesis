#pragma once

#include <stdint.h>

typedef void (*bp_measure_done_cb_t)(float sys, float dia, float map, float bpm);

// Initialize BP measure module (call from setup)
void BPMeasure_Init();

// Start a blood-pressure measurement asynchronously. The provided callback
// will be invoked on the LVGL main task via lv_async_call when measurement
// completes (or fails with zeros).
void BPMeasure_Start(bp_measure_done_cb_t cb);
