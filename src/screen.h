// SPI panel through DRM. /dev/fb0 stays dark while az0x-splash holds the device.
#pragma once
#include <stdint.h>

struct screen {
    uint32_t *px;
    int stride; // pixels
    int fd;
    unsigned fb_id;
    unsigned long bytes;
    int ok;
};

int screen_open(struct screen *s); // 0 on success. On failure s->px stays NULL.
// One DIRTYFB for every rect. rects are x0,y0,x1,y1 with x1,y1 exclusive.
void screen_flush(struct screen *s, const int rects[][4], int n);
void screen_close(struct screen *s);
