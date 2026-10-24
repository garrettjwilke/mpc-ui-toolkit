// Control-surface rawmidi. The parser itself is in input.c.
#pragma once

struct surface {
    int fd;
};

int surface_open(struct surface *s); // 0 on success
void surface_pad(struct surface *s, int pad, int r, int g, int b);
void surface_all_off(struct surface *s);
void surface_close(struct surface *s);
