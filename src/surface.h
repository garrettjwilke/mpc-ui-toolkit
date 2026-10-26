// Control-surface rawmidi. The parser itself is in input.c.
#pragma once

struct surface {
    int fd;
    int last_left_meter;
    int last_right_meter;
    int last_track_mask;
};

int surface_open(struct surface *s); // 0 on success
void surface_pad(struct surface *s, int pad, int r, int g, int b);
void surface_meter(struct surface *s, int left_mask, int right_mask);
void surface_track_led(struct surface *s, int track_index);
void surface_all_off(struct surface *s);
void surface_close(struct surface *s);
