#include "app.h"
#include "input.h"
#include "screen.h"
#include "surface.h"
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int64_t mono_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

// A wake waits only for the time the draw did not already use. Idle stays at 50 ms.
static int poll_ms(int wake, int64_t started) {
    if (wake <= 0) return 50;
    int64_t spent = mono_ms() - started;
    if (spent < 0) spent = 0;
    if (spent >= wake) return 0;
    return (int)(wake - spent);
}

static void present(struct screen *scr, const uint32_t *front, int rects[][4], int n) {
    for (int i = 0; i < n; i++) {
        int x0 = rects[i][0], y0 = rects[i][1], x1 = rects[i][2], y1 = rects[i][3];
        for (int y = y0; y < y1; y++)
            memcpy(scr->px + y * scr->stride + x0, front + y * UI_W + x0, (size_t)(x1 - x0) * 4);
    }
    screen_flush(scr, rects, n);
}

static int draw_frame(struct screen *scr, uint32_t *back, uint32_t *front, struct surface *midi, ui_frame frame,
                      const struct input *in, void *state, int *wake_ms) {
    struct ui u = {.px = back};
    int quit = frame(&u, in, state);
    int rects[UI_H][4];
    int n = ui_changes(back, front, rects);
    for (int i = 0; i < u.nled; i++)
        surface_pad(midi, u.led[i].index, u.led[i].r, u.led[i].g, u.led[i].b);
    surface_meter(midi, u.meter_left, u.meter_right);
    surface_track_led(midi, u.track_led);
    present(scr, front, rects, n);
    if (wake_ms) *wake_ms = u.wake_ms;
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
    int wake = 0;
    int64_t started = mono_ms();
    int quit = draw_frame(&scr, back, front, &midi, frame, &in, state, &wake);

    while (!quit) {
        struct pollfd pfd = {.fd = midi.fd, .events = POLLIN};
        int pr = poll(&pfd, 1, poll_ms(wake, started));
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) {
            if (wake <= 0) continue;
            input_begin(&in, &dev);
            started = mono_ms();
            quit = draw_frame(&scr, back, front, &midi, frame, &in, state, &wake);
            continue;
        }
        int got = read_frame(&midi, &parser, &dev, &in);
        if (got < 0) break;
        if (!got) continue;
        started = mono_ms();
        quit = draw_frame(&scr, back, front, &midi, frame, &in, state, &wake);
    }

    surface_all_off(&midi);
    free(back);
    free(front);
    surface_close(&midi);
    screen_close(&scr);
    return 0;
}
