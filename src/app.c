#include "app.h"
#include "input.h"
#include "screen.h"
#include "surface.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void present(struct screen *scr, const uint32_t *front, int rects[][4], int n) {
    for (int i = 0; i < n; i++) {
        int x0 = rects[i][0], y0 = rects[i][1], x1 = rects[i][2], y1 = rects[i][3];
        for (int y = y0; y < y1; y++)
            memcpy(scr->px + y * scr->stride + x0, front + y * UI_W + x0, (size_t)(x1 - x0) * 4);
        screen_flush(scr, x0, y0, x1, y1);
    }
}

static int draw_frame(struct screen *scr, uint32_t *back, uint32_t *front, struct surface *midi, ui_frame frame,
                      const struct input *in, void *state) {
    struct ui u = {.px = back};
    int quit = frame(&u, in, state);
    int rects[UI_H][4];
    int n = ui_changes(back, front, rects);
    present(scr, front, rects, n);
    for (int i = 0; i < u.nled; i++)
        surface_pad(midi, u.led[i].index, u.led[i].r, u.led[i].g, u.led[i].b);
    return quit;
}

static int read_frame(struct surface *midi, struct parser *p, struct input_dev *dev, struct input *in) {
    input_begin(in, dev);
    unsigned char buf[256];
    int any = 0;
    for (;;) {
        ssize_t n = read(midi->fd, buf, sizeof buf);
        if (n < 0) return errno == EAGAIN || errno == EINTR ? any : -1;
        if (n == 0) return any;
        any = 1;
        for (ssize_t i = 0; i < n; i++)
            if (parser_feed(p, buf[i])) input_push(dev, in, p->msg);
    }
}

int app_run(ui_frame frame, void *state) {
    if (!frame) return 1;
    struct screen scr;
    if (screen_open(&scr) != 0) return 1;
    struct surface midi;
    if (surface_open(&midi) != 0) {
        screen_close(&scr);
        return 1;
    }
    uint32_t *back = calloc((size_t)UI_W * UI_H, sizeof(uint32_t));
    uint32_t *front = calloc((size_t)UI_W * UI_H, sizeof(uint32_t));
    if (!back || !front) {
        free(back);
        free(front);
        surface_close(&midi);
        screen_close(&scr);
        return 1;
    }
    struct input_dev dev = {0};
    struct parser parser = {0};
    struct input in;
    input_begin(&in, &dev);
    int quit = draw_frame(&scr, back, front, &midi, frame, &in, state);

    while (!quit) {
        struct pollfd pfd = {.fd = midi.fd, .events = POLLIN};
        int pr = poll(&pfd, 1, 50);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;
        int got = read_frame(&midi, &parser, &dev, &in);
        if (got < 0) break;
        if (!got) continue;
        quit = draw_frame(&scr, back, front, &midi, frame, &in, state);
    }

    surface_all_off(&midi);
    free(back);
    free(front);
    surface_close(&midi);
    screen_close(&scr);
    return 0;
}
