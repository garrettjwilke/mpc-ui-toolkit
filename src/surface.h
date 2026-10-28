// Control-surface rawmidi. The parser itself is in input.c.
#pragma once

struct surface {
    int fd;
    int last_left_meter;
    int last_right_meter;
    int last_track_mask;
    unsigned char last_button[32];
    unsigned char last_pad[16][3];
    int pad_valid;
};

int surface_open(struct surface *s); // 0 on success
void surface_pad(struct surface *s, int pad, int r, int g, int b);
void surface_meter(struct surface *s, int left_mask, int right_mask);
void surface_track_led(struct surface *s, int track_index);
/* level[i] is 0 off, 1 dim, 2 bright for IN_* index i. Only bits in mask are sent. */
void surface_buttons(struct surface *s, const unsigned char *level, unsigned mask);
void surface_all_off(struct surface *s);
void surface_close(struct surface *s);
