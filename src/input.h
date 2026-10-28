// One frame of the MPC Sample control surface. Parsing does not touch the device.
#pragma once
#include <stdint.h>

enum {
    IN_SAMPLE, IN_SEQ, IN_PAD_FX, IN_KNOB_FX, IN_SHIFT, IN_PAD_BANK, IN_ERASE, IN_NOTE_REPEAT,
    IN_B1, IN_B2, IN_B3, IN_CHOP, IN_MUTE, IN_LOOP, IN_LEVELS, IN_SAMPLE_SELECT, IN_TAP_TEMPO,
    IN_ENCODER, IN_MINUS, IN_PLUS, IN_SAMPLE_REC, IN_SEQ_REC, IN_STOP, IN_PLAY, IN_N
};

extern const unsigned char INPUT_BUTTON_NOTE[IN_N];
extern const unsigned char INPUT_PAD_NOTE[16];

// Held positions. Edges for a single frame live on struct input.
struct input_dev {
    int shift;
    unsigned held; // every button that is down, including ones pressed on earlier frames
    int knob[3];
    int fader, main;
    int pad_down[16];
    int pad_vel[16];
    int pad_at[16];
};

struct input {
    int encoder;       // signed steps this frame
    int encoder_press; // encoder pushed this frame
    int shift;         // held
    unsigned held;     // buttons down now; bit (1u << IN_*) 
    unsigned down;     // bit per button, press edge
    unsigned up;
    int knob_moved[3];
    int knob[3];       // absolute 0..127 when knob_moved
    int knob_pos[3];
    int fader_moved, fader, fader_pos;
    int main_moved, main, main_pos;
    int pad_hit[16];      // velocity, or -1
    int pad_pressure[16]; // 0..127, or -1
    int pad_release[16];
    int pad_down[16];
    int pad_vel[16];
    int pad_at[16];
};

struct parser {
    unsigned char msg[3];
    int n, sysex;
};

void input_begin(struct input *f, const struct input_dev *d);
void input_push(struct input_dev *d, struct input *f, const unsigned char m[3]);
int input_down(const struct input *f, int button);

// 1 when msg holds a channel message. SysEx and realtime bytes are skipped.
int parser_feed(struct parser *p, unsigned char c);
