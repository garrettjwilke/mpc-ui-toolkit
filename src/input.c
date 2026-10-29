#include "input.h"
#include <string.h>

const unsigned char INPUT_BUTTON_NOTE[IN_N] = {
    6, 7, 1, 0, 49, 35, 9, 11, 12, 13, 14, 75, 43, 74, 40, 36, 53, 111, 55, 54, 73, 80, 81, 82,
};

const unsigned char INPUT_PAD_NOTE[16] = {37, 36, 42, 82, 40, 38, 46, 44, 48, 47, 45, 43, 49, 55, 51, 53};

static int button_from_note(int note) {
    for (int i = 0; i < IN_N; i++)
        if (INPUT_BUTTON_NOTE[i] == note) return i;
    return -1;
}

static int pad_index(int note) {
    for (int i = 0; i < 16; i++)
        if (INPUT_PAD_NOTE[i] == note) return i;
    return -1;
}

int input_down(const struct input *f, int button) {
    if (button < 0 || button >= IN_N) return 0;
    return (f->down >> button) & 1;
}

void input_begin(struct input *f, const struct input_dev *d) {
    memset(f, 0, sizeof *f);
    for (int i = 0; i < 16; i++) f->pad_hit[i] = f->pad_pressure[i] = -1;
    if (!d) return;
    f->held = d->held;
    if (d->shift) f->held |= 1u << IN_SHIFT;
    f->shift = (f->held & (1u << IN_SHIFT)) != 0;
    for (int i = 0; i < 3; i++) f->knob_pos[i] = d->knob[i];
    f->fader_pos = d->fader;
    f->main_pos = d->main;
    for (int i = 0; i < 16; i++) {
        f->pad_down[i] = d->pad_down[i];
        f->pad_vel[i] = d->pad_vel[i];
        f->pad_at[i] = d->pad_at[i];
    }
}

void input_push(struct input_dev *d, struct input *f, const unsigned char m[3]) {
    int st = m[0] & 0xf0, ch = (m[0] & 0x0f) + 1, d1 = m[1], d2 = m[2];
    if (ch == 10) {
        int i = pad_index(d1);
        if (i < 0) return;
        if (st == 0xa0) {
            if (d) d->pad_at[i] = d2;
            f->pad_pressure[i] = d2;
            f->pad_at[i] = d2;
            return;
        }
        int down = st == 0x90 && d2 > 0;
        if (down) {
            if (d) d->pad_down[i] = 1, d->pad_vel[i] = d2;
            f->pad_hit[i] = d2;
            f->pad_vel[i] = d2;
            f->pad_down[i] = 1;
        } else if (st == 0x80 || (st == 0x90 && d2 == 0)) {
            if (d) d->pad_down[i] = 0, d->pad_at[i] = 0;
            f->pad_release[i] = 1;
            f->pad_down[i] = 0;
            f->pad_at[i] = 0;
        }
        return;
    }
    if (ch != 1) return;
    if (st == 0xb0) {
        int v = d2 & 0x7f;
        if (d1 >= 16 && d1 <= 18) {
            int k = d1 - 16;
            if (d) d->knob[k] = v;
            f->knob_moved[k] = 1;
            f->knob[k] = v;
            f->knob_pos[k] = v;
        } else if (d1 == 19) {
            if (d) d->main = v;
            f->main_moved = 1;
            f->main = v;
            f->main_pos = v;
        } else if (d1 == 20) {
            if (d) d->fader = v;
            f->fader_moved = 1;
            f->fader = v;
            f->fader_pos = v;
        } else if (d1 == 100) {
            if (v == 1) f->encoder += 1;
            else if (v == 0x7f) f->encoder -= 1;
        }
        return;
    }
    if (st == 0x90 || st == 0x80) {
        int b = button_from_note(d1);
        if (b < 0) return;
        unsigned bit = 1u << b;
        // The surface only sends 7F for a press. Other velocities are LED
        // traffic or a torn message, and they must not latch a button.
        int press = st == 0x90 && d2 == 0x7f;
        int release = st == 0x80 || (st == 0x90 && d2 == 0);
        if (press) {
            if ((f->held & bit) == 0) {
                f->down |= bit;
                f->held |= bit;
                if (d) d->held |= bit;
                if (b == IN_ENCODER) f->encoder_press = 1;
            }
        } else if (release) {
            f->held &= ~bit;
            if (d) d->held &= ~bit;
            if (f->down & bit) {
                // Press and release arrived in this same drain. The finger
                // is up, so the app never sees the edge.
                f->down &= ~bit;
                f->up &= ~bit;
                if (b == IN_ENCODER) f->encoder_press = 0;
            } else {
                f->up |= bit;
            }
        } else {
            return;
        }
        if (d) d->shift = (d->held & (1u << IN_SHIFT)) != 0;
        f->shift = (f->held & (1u << IN_SHIFT)) != 0;
    }
}

int parser_feed(struct parser *p, unsigned char c) {
    if (c >= 0xf8) return 0;
    if (c == 0xf0) {
        p->sysex = 1;
        p->n = 0;
        return 0;
    }
    if (c >= 0xf1) {
        p->sysex = 0;
        p->n = 0;
        return 0;
    }
    if (c & 0x80) {
        p->sysex = 0;
        p->msg[0] = c;
        p->n = 1;
        return 0;
    }
    if (p->sysex || p->n == 0) return 0;
    p->msg[p->n++] = c;
    int len = (p->msg[0] & 0xe0) == 0xc0 ? 2 : 3;
    if (p->n < len) return 0;
    p->n = 0;
    if (len == 2) p->msg[2] = 0;
    return 1;
}
