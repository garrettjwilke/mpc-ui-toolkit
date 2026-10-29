// Hardware and input diagnostics example.
// Tests pads, all 24 buttons, knobs, fader, encoder, LEDs, meters, and raw MIDI event stream.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"
#include "diagnostics.h"
#include <stdio.h>
#include <string.h>

static const struct {
    int btn;
    const char *name;
    int x, y, w, h;
} BUTTON_LAYOUT[] = {
    {IN_SAMPLE,        "SAMPLE",  10, 30, 46, 13},
    {IN_SEQ,           "SEQ",     59, 30, 46, 13},
    {IN_PAD_FX,        "PAD FX", 108, 30, 48, 13},
    {IN_KNOB_FX,       "KNOB FX",159, 30, 50, 13},
    {IN_SHIFT,         "SHIFT",  212, 30, 46, 13},
    {IN_PAD_BANK,      "BANK",   261, 30, 46, 13},

    {IN_ERASE,         "ERASE",   10, 45, 46, 13},
    {IN_NOTE_REPEAT,   "REPEAT",  59, 45, 46, 13},
    {IN_B1,            "B1",     108, 45, 48, 13},
    {IN_B2,            "B2",     159, 45, 50, 13},
    {IN_B3,            "B3",     212, 45, 46, 13},
    {IN_CHOP,          "CHOP",   261, 45, 46, 13},

    {IN_MUTE,          "MUTE",    10, 60, 46, 13},
    {IN_LOOP,          "LOOP",    59, 60, 46, 13},
    {IN_LEVELS,        "LEVELS", 108, 60, 48, 13},
    {IN_SAMPLE_SELECT, "SELECT", 159, 60, 50, 13},
    {IN_TAP_TEMPO,     "TAP",    212, 60, 46, 13},
    {IN_ENCODER,       "PUSH",   261, 60, 46, 13},

    {IN_MINUS,         "MINUS",   10, 75, 46, 13},
    {IN_PLUS,          "PLUS",    59, 75, 46, 13},
    {IN_SAMPLE_REC,    "SMP REC",108, 75, 48, 13},
    {IN_SEQ_REC,       "SEQ REC",159, 75, 50, 13},
    {IN_STOP,          "STOP",   212, 75, 46, 13},
    {IN_PLAY,          "PLAY",   261, 75, 46, 13},
};
#define N_BUTTON_LAYOUT (sizeof(BUTTON_LAYOUT) / sizeof(BUTTON_LAYOUT[0]))


static void log_event(struct diagnostics_state *s, const char *msg) {
    s->total_events++;
    for (int i = 3; i > 0; i--) {
        strncpy(s->event_log[i], s->event_log[i - 1], sizeof(s->event_log[i]) - 1);
        s->event_log[i][sizeof(s->event_log[i]) - 1] = 0;
    }
    strncpy(s->event_log[0], msg, sizeof(s->event_log[0]) - 1);
    s->event_log[0][sizeof(s->event_log[0]) - 1] = 0;
    if (s->event_log_count < 4) s->event_log_count++;
}

int diagnostics_frame(struct ui *u, const struct input *in, void *state) {
    struct diagnostics_state *s = state;
    if (!s) return 0;

    example_menu_prepare();
    (void)ui_menu_open(&example_menu, in);

    // Process Encoder
    if (in && in->encoder != 0) {
        s->encoder_last_delta = in->encoder;
        s->encoder_accum += in->encoder;
        char buf[64];
        snprintf(buf, sizeof buf, "ENCODER: delta=%+d (tot=%d)", in->encoder, s->encoder_accum);
        log_event(s, buf);
    }
    if (in && in->encoder_press) {
        char buf[64];
        snprintf(buf, sizeof buf, "ENCODER PUSH [Click]");
        log_event(s, buf);
    }

    // Process Track +/- to cycle hardware track LEDs (CC 57..64)
    if (in && (in->down & (1u << IN_MINUS))) {
        s->selected_track = (s->selected_track + 7) % 8;
        char buf[64];
        snprintf(buf, sizeof buf, "TRACK - (Track %c LED)", 'A' + s->selected_track);
        log_event(s, buf);
    }
    if (in && (in->down & (1u << IN_PLUS))) {
        s->selected_track = (s->selected_track + 1) % 8;
        char buf[64];
        snprintf(buf, sizeof buf, "TRACK + (Track %c LED)", 'A' + s->selected_track);
        log_event(s, buf);
    }

    // Process Knobs & Fader
    if (in && in->main_moved) {
        char buf[64];
        snprintf(buf, sizeof buf, "MAIN VOLUME (CC 19): %d", in->main);
        log_event(s, buf);
    }
    for (int k = 0; k < 3; k++) {
        if (in && in->knob_moved[k]) {
            char buf[64];
            snprintf(buf, sizeof buf, "KNOB K%d (CC %d): %d", k + 1, 16 + k, in->knob[k]);
            log_event(s, buf);
        }
    }
    if (in && in->fader_moved) {
        char buf[64];
        snprintf(buf, sizeof buf, "FADER (CC 20): %d", in->fader);
        log_event(s, buf);
        s->test_meter_val = in->fader;
    }

    // Process Buttons Down
    if (in && in->down) {
        for (size_t b = 0; b < N_BUTTON_LAYOUT; b++) {
            int btn_idx = BUTTON_LAYOUT[b].btn;
            if (in->down & (1u << btn_idx)) {
                char buf[64];
                snprintf(buf, sizeof buf, "BTN %s DOWN (Note %d vel 127)", BUTTON_LAYOUT[b].name, INPUT_BUTTON_NOTE[btn_idx]);
                log_event(s, buf);
            }
        }
    }

    // Process Pads Hit / AT / Release
    for (int p = 0; p < 16; p++) {
        if (in && in->pad_hit[p] > 0) {
            s->pad_vel[p] = in->pad_hit[p];
            char buf[64];
            snprintf(buf, sizeof buf, "PAD %d HIT: vel=%d (Note %d)", p + 1, in->pad_hit[p], INPUT_PAD_NOTE[p]);
            log_event(s, buf);
        }
        if (in && in->pad_pressure[p] >= 0) {
            s->pad_at[p] = in->pad_pressure[p];
        }
        if (in && in->pad_release[p]) {
            s->pad_at[p] = 0;
            char buf[64];
            snprintf(buf, sizeof buf, "PAD %d RELEASE", p + 1);
            log_event(s, buf);
        }
    }

    // --- Render Frame ---
    ui_clear(u, 0x10141a);

    // Title Header
    ui_box(u, 0, 0, UI_W, 22, (struct ui_paint){
        .fill_set = 1, .fill = 0x1a222e,
        .border_set = 1, .border = 0x2e3d52, .border_px = 1
    });
    ui_text(u, 8, 5, "HARDWARE DIAGNOSTICS", 0x38bdf8, 1);
    char ev_buf[32];
    snprintf(ev_buf, sizeof ev_buf, "EV: %lu", (unsigned long)s->total_events);
    ui_text(u, 250, 5, ev_buf, 0x4ade80, 1);

    // 24 Buttons Panel
    for (size_t b = 0; b < N_BUTTON_LAYOUT; b++) {
        int btn = BUTTON_LAYOUT[b].btn;
        int held = in ? ((in->held >> btn) & 1) : 0;
        uint32_t fill = held ? 0xf59e0b : 0x18202c;
        uint32_t text_col = held ? 0x000000 : 0x94a3b8;
        uint32_t border = held ? 0xfef08a : 0x2d3b4e;

        ui_box(u, BUTTON_LAYOUT[b].x, BUTTON_LAYOUT[b].y, BUTTON_LAYOUT[b].w, BUTTON_LAYOUT[b].h, (struct ui_paint){
            .fill_set = 1, .fill = fill,
            .border_set = 1, .border = border, .border_px = 1
        });
        ui_text(u, BUTTON_LAYOUT[b].x + 3, BUTTON_LAYOUT[b].y + 2, BUTTON_LAYOUT[b].name, text_col, 1);

        // Update hardware LED on device
        ui_button_led(u, btn, held ? 2 : 0);
    }

    // 4x4 Pad Grid (y=92..168, x=10..150)
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            int p = (3 - row) * 4 + col; // MPC pad orientation: Pad 1 bottom-left, Pad 16 top-right
            int px = 10 + col * 35;
            int py = 92 + row * 19;

            int down = in ? in->pad_down[p] : 0;
            int at = s->pad_at[p];
            uint32_t fill = down ? (at > 64 ? 0xf43f5e : 0x38bdf8) : (s->pad_vel[p] > 0 ? 0x1c2b3d : 0x131a24);
            uint32_t text_col = down ? 0xffffff : 0x64748b;
            uint32_t border = down ? 0xffffff : 0x223042;

            ui_box(u, px, py, 32, 16, (struct ui_paint){
                .fill_set = 1, .fill = fill,
                .border_set = 1, .border = border, .border_px = 1
            });
            char pstr[8];
            snprintf(pstr, sizeof pstr, "P%d", p + 1);
            ui_text(u, px + 4, py + 2, pstr, text_col, 1);

            // Hardware RGB Pad LED feedback
            if (down) {
                ui_led(u, p, 127, at, 127 - at);
            } else {
                ui_led(u, p, 15, 60, 100); // Ambient cyan idle
            }
        }
    }

    // Controls Status Box (x=155..310, y=92..168)
    ui_box(u, 155, 92, 155, 74, (struct ui_paint){
        .fill_set = 1, .fill = 0x121922,
        .border_set = 1, .border = 0x233144, .border_px = 1
    });

    char info[48];
    int mv = in ? in->main_pos : 0;
    int k1 = in ? in->knob_pos[0] : 0;
    int k2 = in ? in->knob_pos[1] : 0;
    int k3 = in ? in->knob_pos[2] : 0;
    int fad = in ? in->fader_pos : 0;
    int enc_push = in ? ((in->held >> IN_ENCODER) & 1) : 0;

    snprintf(info, sizeof info, "VOL:%3d  K1:%3d  K2:%3d", mv, k1, k2);
    ui_text(u, 160, 96, info, 0x93c5fd, 1);
    snprintf(info, sizeof info, "K3 :%3d  FAD:%3d", k3, fad);
    ui_text(u, 160, 112, info, 0x93c5fd, 1);
    snprintf(info, sizeof info, "ENC:%+3d (TOT %d)", s->encoder_last_delta, s->encoder_accum);
    ui_text(u, 160, 128, info, 0x38bdf8, 1);
    snprintf(info, sizeof info, "PUSH:%s  TRK:%c", enc_push ? "PRESSED" : "UP", 'A' + s->selected_track);
    ui_text(u, 160, 144, info, enc_push ? 0xf59e0b : 0xa5b4fc, 1);

    // Event Log Box (y=170..236, full width)
    ui_box(u, 6, 170, UI_W - 12, 66, (struct ui_paint){
        .fill_set = 1, .fill = 0x0a0e14,
        .border_set = 1, .border = 0x1e293b, .border_px = 1
    });
    ui_text(u, 12, 172, "LIVE EVENT MONITOR:", 0x64748b, 1);
    for (int i = 0; i < s->event_log_count && i < 4; i++) {
        uint32_t col = (i == 0) ? 0xf8fafc : (0x64748b - i * 0x121212);
        ui_text(u, 12, 185 + i * 12, s->event_log[i], col, 1);
    }

    // Drive Track LED and Level Meter on Hardware
    ui_track_led(u, s->selected_track);
    int meter_mask = (s->test_meter_val * 31) / 127;
    ui_meter(u, meter_mask, meter_mask);

    if (example_menu_draw(u, in)) return 1;

    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct diagnostics_state s = {0};
    return app_run(diagnostics_frame, &s);
}
#endif
