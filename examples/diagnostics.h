#pragma once
#include "ui.h"
#include <stdint.h>

struct diagnostics_state {
    int pad_vel[16];
    int pad_at[16];
    int encoder_accum;
    int encoder_last_delta;
    int selected_track;
    int test_meter_val;

    char event_log[4][64];
    int event_log_count;
    uint32_t total_events;
};

int diagnostics_frame(struct ui *u, const struct input *in, void *state);
