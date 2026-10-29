#include "app.h"
#include "input.h"
#include "screen.h"
#include "surface.h"
#include <errno.h>
#include <poll.h>
#include <pthread.h>
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

// DIRTYFB holds the SPI bus for the whole clip. A full frame is about 50 ms,
// long enough to stall the game. The flush thread owns the dumb buffer and
// that ioctl. Both have to stay on that thread: the cache clean inside the
// ioctl only sees the caller's cache, so pixels written by the game thread
// go out as zeros and the panel stays blank. `front` keeps the newest pixels.
// `pending` is every region not copied yet. When the bus is busy the game
// keeps drawing; the next free send ships the union, so a skipped frame is
// not lost.
#define FLUSH_MAX 128

struct panel {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    pthread_t thread;
    int started;
    int stop;
    struct screen *scr;
    const uint32_t *front;
    int pending[FLUSH_MAX][4];
    int pending_n;
};

static struct panel panel = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .cv = PTHREAD_COND_INITIALIZER,
};

static void copy_rects(struct screen *scr, const uint32_t *front, int rects[][4], int n) {
    if (!scr || !scr->px || !front) return;
    for (int i = 0; i < n; i++) {
        int x0 = rects[i][0], y0 = rects[i][1], x1 = rects[i][2], y1 = rects[i][3];
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > UI_W) x1 = UI_W;
        if (y1 > UI_H) y1 = UI_H;
        if (x1 <= x0 || y1 <= y0) continue;
        for (int y = y0; y < y1; y++)
            memcpy(scr->px + y * scr->stride + x0, front + y * UI_W + x0, (size_t)(x1 - x0) * 4);
    }
}

static void collapse_pending(struct panel *p) {
    if (p->pending_n <= 1) return;
    int x0 = UI_W, y0 = UI_H, x1 = 0, y1 = 0;
    for (int i = 0; i < p->pending_n; i++) {
        if (p->pending[i][0] < x0) x0 = p->pending[i][0];
        if (p->pending[i][1] < y0) y0 = p->pending[i][1];
        if (p->pending[i][2] > x1) x1 = p->pending[i][2];
        if (p->pending[i][3] > y1) y1 = p->pending[i][3];
    }
    p->pending[0][0] = x0;
    p->pending[0][1] = y0;
    p->pending[0][2] = x1;
    p->pending[0][3] = y1;
    p->pending_n = 1;
}

static void pending_add(struct panel *p, int rects[][4], int n) {
    for (int i = 0; i < n; i++) {
        int x0 = rects[i][0], y0 = rects[i][1], x1 = rects[i][2], y1 = rects[i][3];
        if (x1 <= x0 || y1 <= y0) continue;
        int merged = 0;
        for (int j = 0; j < p->pending_n; j++) {
            if (x0 >= p->pending[j][0] && x1 <= p->pending[j][2]
                && y0 >= p->pending[j][1] && y1 <= p->pending[j][3]) {
                merged = 1;
                break;
            }
            if (p->pending[j][0] == x0 && p->pending[j][2] == x1
                && y0 <= p->pending[j][3] && y1 >= p->pending[j][1]) {
                if (y0 < p->pending[j][1]) p->pending[j][1] = y0;
                if (y1 > p->pending[j][3]) p->pending[j][3] = y1;
                merged = 1;
                break;
            }
        }
        if (merged) continue;
        if (p->pending_n >= FLUSH_MAX) {
            collapse_pending(p);
            if (x0 < p->pending[0][0]) p->pending[0][0] = x0;
            if (y0 < p->pending[0][1]) p->pending[0][1] = y0;
            if (x1 > p->pending[0][2]) p->pending[0][2] = x1;
            if (y1 > p->pending[0][3]) p->pending[0][3] = y1;
            continue;
        }
        p->pending[p->pending_n][0] = x0;
        p->pending[p->pending_n][1] = y0;
        p->pending[p->pending_n][2] = x1;
        p->pending[p->pending_n][3] = y1;
        p->pending_n++;
    }
}

// Caller holds p->mu. Copies `front` into the dumb buffer on this thread, which
// is the flush thread, and returns the clips DIRTYFB should send.
static int take_pending(struct panel *p, int rects[][4]) {
    if (p->pending_n <= 0 || !p->scr || !p->front) return 0;
    int n = p->pending_n;
    memcpy(rects, p->pending, (size_t)n * 4 * sizeof(int));
    copy_rects(p->scr, p->front, rects, n);
    p->pending_n = 0;
    return n;
}

static void *flush_thread(void *arg) {
    struct panel *p = arg;
    for (;;) {
        int rects[FLUSH_MAX][4];
        pthread_mutex_lock(&p->mu);
        while (!p->stop && p->pending_n == 0)
            pthread_cond_wait(&p->cv, &p->mu);
        int n = take_pending(p, rects);
        struct screen *scr = p->scr;
        int leave = n == 0;
        pthread_mutex_unlock(&p->mu);
        if (leave) break;
        if (scr)
            screen_flush(scr, rects, n);
    }
    return NULL;
}

static void panel_start(struct screen *scr, const uint32_t *front) {
    pthread_mutex_lock(&panel.mu);
    panel.scr = scr;
    panel.front = front;
    panel.stop = 0;
    panel.pending_n = 0;
    pthread_mutex_unlock(&panel.mu);
    panel.started = pthread_create(&panel.thread, NULL, flush_thread, &panel) == 0;
}

static void panel_stop(void) {
    if (!panel.started) return;
    pthread_mutex_lock(&panel.mu);
    panel.stop = 1;
    pthread_cond_signal(&panel.cv);
    pthread_mutex_unlock(&panel.mu);
    pthread_join(panel.thread, NULL);
    panel.started = 0;
}

static void lift_holds(struct input_dev *dev, unsigned buttons, unsigned pads) {
    if (!dev) return;
    dev->held &= ~buttons;
    if (buttons & (1u << IN_SHIFT)) dev->shift = 0;
    for (int p = 0; p < 16; p++) {
        if ((pads & (1u << p)) == 0) continue;
        dev->pad_down[p] = 0;
        dev->pad_vel[p] = 0;
        dev->pad_at[p] = 0;
    }
}

static int draw_frame(struct screen *scr, uint32_t *back, uint32_t *front, struct surface *midi, ui_frame frame,
                      struct input_dev *dev, const struct input *in, void *state, int *wake_ms) {
    struct ui u = {.px = back};
    int quit = frame(&u, in, state) || u.quit;
    lift_holds(dev, u.lift_buttons, u.lift_pads);
    int rects[UI_H][4];
    // ui_changes writes `front`. The flush thread is the only one that copies
    // `front` into the dumb buffer, and it does that just before DIRTYFB.
    if (panel.started)
        pthread_mutex_lock(&panel.mu);
    int n = ui_changes(back, front, rects);
    if (panel.started) {
        panel.front = front;
        pending_add(&panel, rects, n);
        if (panel.pending_n > 0)
            pthread_cond_signal(&panel.cv);
        pthread_mutex_unlock(&panel.mu);
    } else {
        copy_rects(scr, front, rects, n);
        screen_flush(scr, rects, n);
    }
    for (int i = 0; i < u.nled; i++)
        surface_pad(midi, u.led[i].index, u.led[i].r, u.led[i].g, u.led[i].b);
    surface_meter(midi, u.meter_left, u.meter_right);
    surface_track_led(midi, u.track_led);
    surface_buttons(midi, u.button_level, u.button_mask);
    if (wake_ms) *wake_ms = u.wake_ms;
    return quit;
}

void diag_log_raw_midi(const unsigned char m[3]) __attribute__((weak));

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
            if (parser_feed(p, buf[i])) {
                if (diag_log_raw_midi) diag_log_raw_midi(p->msg);
                input_push(dev, in, p->msg);
            }
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
    panel_start(&scr, front);
    int wake = 0;
    int64_t started = mono_ms();
    int quit = draw_frame(&scr, back, front, &midi, frame, &dev, &in, state, &wake);

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
            quit = draw_frame(&scr, back, front, &midi, frame, &dev, &in, state, &wake);
            continue;
        }
        int got = read_frame(&midi, &parser, &dev, &in);
        if (got < 0) break;
        if (!got) continue;
        started = mono_ms();
        quit = draw_frame(&scr, back, front, &midi, frame, &dev, &in, state, &wake);
    }

    surface_all_off(&midi);
    panel_stop();
    free(back);
    free(front);
    surface_close(&midi);
    screen_close(&scr);
    return 0;
}
