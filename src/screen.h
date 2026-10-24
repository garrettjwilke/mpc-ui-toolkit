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
void screen_flush(struct screen *s, int x0, int y0, int x1, int y1);
void screen_close(struct screen *s);
