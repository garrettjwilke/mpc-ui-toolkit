#include "ui.h"
#include "font.h"
#include "input.h"
#include <string.h>
#include <time.h>

static uint32_t rgb(uint32_t c) { return c & 0x00ffffff; }

static void plot(struct ui *u, int x, int y, uint32_t c) {
    if (x < 0 || y < 0 || x >= UI_W || y >= UI_H) return;
    u->px[y * UI_W + x] = rgb(c);
}

static void fill(struct ui *u, int x, int y, int w, int h, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > UI_W ? UI_W : x + w;
    int y1 = y + h > UI_H ? UI_H : y + h;
    c = rgb(c);
    for (int row = y0; row < y1; row++)
        for (int col = x0; col < x1; col++) u->px[row * UI_W + col] = c;
}

static void frame(struct ui *u, int x, int y, int w, int h, uint32_t c, int t) {
    if (t <= 0 || w <= 0 || h <= 0) return;
    if (t > w) t = w;
    if (t > h) t = h;
    fill(u, x, y, w, t, c);
    fill(u, x, y + h - t, w, t, c);
    fill(u, x, y, t, h, c);
    fill(u, x + w - t, y, t, h, c);
}

void ui_clear(struct ui *u, uint32_t color) { fill(u, 0, 0, UI_W, UI_H, color); }

void ui_box(struct ui *u, int x, int y, int w, int h, struct ui_paint p) {
    if (p.fill_set) fill(u, x, y, w, h, p.fill);
    if (p.border_set) frame(u, x, y, w, h, p.border, p.border_px > 0 ? p.border_px : 1);
}

static void glyph(struct ui *u, int x, int y, const unsigned char *g, int gw, int gh, int scale, uint32_t c) {
    for (int r = 0; r < gh; r++)
        for (int col = 0; col < gw; col++) {
            if (!(g[r] & (0x80 >> col))) continue;
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    plot(u, x + col * scale + sx, y + r * scale + sy, c);
        }
}

static int text_limited(struct ui *u, int x, int y, const char *s, uint32_t c, int scale, int x_max) {
    if (!s) return x;
    int sc = scale <= 1 ? 1 : scale;
    int gw, gh;
    if (scale <= 1) gw = FONT_S_W, gh = FONT_S_H;
    else gw = FONT_L_W * sc, gh = FONT_L_H * sc;
    for (; *s && x + (scale <= 1 ? FONT_S_W : FONT_L_W * sc) <= x_max; s++) {
        int ch = (unsigned char)*s;
        if (ch < 32 || ch > 126) ch = '?';
        const unsigned char *g = scale <= 1 ? FONT_S[ch - 32] : FONT_L[ch - 32];
        int src_w = scale <= 1 ? FONT_S_W : FONT_L_W;
        int src_h = scale <= 1 ? FONT_S_H : FONT_L_H;
        glyph(u, x, y, g, src_w, src_h, sc, c);
        x += gw;
        (void)gh;
    }
    return x;
}

void ui_text(struct ui *u, int x, int y, const char *s, uint32_t color, int scale) {
    text_limited(u, x, y, s, color, scale, UI_W);
}

static int text_px(const char *s, int scale) {
    if (!s) return 0;
    int n = 0;
    while (s[n]) n++;
    int gw = scale <= 1 ? FONT_S_W : FONT_L_W * (scale < 2 ? 2 : scale);
    return n * gw;
}

static int text_px_h(int scale) {
    return scale <= 1 ? FONT_S_H : FONT_L_H * (scale < 2 ? 2 : scale);
}

int ui_button(struct ui *u, int x, int y, int w, int h, const char *label, struct ui_button st, const struct input *in) {
    struct ui_paint p = {.fill_set = 1, .fill = st.fill};
    if (st.highlighted) {
        p.border_set = 1;
        p.border = st.highlight;
        p.border_px = st.highlight_px > 0 ? st.highlight_px : 2;
    } else if (st.border_set) {
        p.border_set = 1;
        p.border = st.border;
        p.border_px = st.border_px > 0 ? st.border_px : 1;
    }
    ui_box(u, x, y, w, h, p);
    int scale = 1;
    int tw = text_px(label, scale), th = text_px_h(scale);
    int tx = x + (w - tw) / 2;
    int ty = y + (h - th) / 2;
    int inset = p.border_set ? p.border_px : 0;
    text_limited(u, tx, ty, label, st.text, scale, x + w - inset);
    return st.highlighted && in && in->encoder_press && !in->shift;
}

void ui_highlight(struct ui *u, int x, int y, int w, int h, uint32_t color, int thickness) {
    frame(u, x, y, w, h, color, thickness);
}

void ui_led(struct ui *u, int index, int r, int g, int b) {
    if (index < 0 || index > 15) return;
    unsigned char R = (unsigned char)(r & 0x7f);
    unsigned char G = (unsigned char)(g & 0x7f);
    unsigned char B = (unsigned char)(b & 0x7f);
    for (int i = 0; i < u->nled; i++) {
        if (u->led[i].index != (unsigned char)index) continue;
        u->led[i].r = R;
        u->led[i].g = G;
        u->led[i].b = B;
        return;
    }
    if (u->nled >= 16) return;
    u->led[u->nled].index = (unsigned char)index;
    u->led[u->nled].r = R;
    u->led[u->nled].g = G;
    u->led[u->nled].b = B;
    u->nled++;
}

void ui_lift(struct ui *u, unsigned buttons, unsigned pads) {
    if (!u) return;
    u->lift_buttons |= buttons;
    u->lift_pads |= pads;
}

void ui_button_led(struct ui *u, int button, int level) {
    if (!u || button < 0 || button >= 32) return;
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    u->button_mask |= 1u << button;
    u->button_level[button] = (unsigned char)level;
}

void ui_meter(struct ui *u, int left_mask, int right_mask) {
    if (!u) return;
    u->meter_left = left_mask & 0x1f;
    u->meter_right = right_mask & 0x1f;
}

void ui_track_led(struct ui *u, int track_index) {
    if (!u) return;
    u->track_led = track_index;
}

int ui_changes(const uint32_t *back, uint32_t *front, int rects[][4]) {
    int n = 0;
    for (int y = 0; y < UI_H;) {
        int y0 = y, x0 = UI_W, x1 = 0;
        for (; y < UI_H; y++) {
            const uint32_t *b = back + y * UI_W, *f = front + y * UI_W;
            int l = 0, r = UI_W;
            while (l < UI_W && b[l] == f[l]) l++;
            if (l == UI_W) break;
            while (b[r - 1] == f[r - 1]) r--;
            if (l < x0) x0 = l;
            if (r > x1) x1 = r;
        }
        if (y == y0) {
            y++;
            continue;
        }
        for (int row = y0; row < y; row++)
            memcpy(front + row * UI_W + x0, back + row * UI_W + x0, (size_t)(x1 - x0) * 4);
        rects[n][0] = x0;
        rects[n][1] = y0;
        rects[n][2] = x1;
        rects[n][3] = y;
        n++;
    }
    return n;
}

void ui_wake(struct ui *u, int ms) {
    if (!u || ms < 1) return;
    if (u->wake_ms < 1 || ms < u->wake_ms) u->wake_ms = ms;
}

void ui_quit(struct ui *u) {
    if (u) u->quit = 1;
}

static int64_t ui_now(struct ui *u) {
    if (u && u->now_ms) return u->now_ms;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int ui_feedback(struct ui *u, int held, int edge, int mode, int flash_ms, int64_t *at) {
    if (mode == UI_FEEDBACK_HOLD) return held || edge;
    if (mode != UI_FEEDBACK_FLASH) return 0;
    if (flash_ms <= 0) flash_ms = 120;
    int64_t now = ui_now(u);
    if (edge && at && now) *at = now;
    if (!at) {
        if (edge) ui_wake(u, flash_ms);
        return edge;
    }
    if (!*at || !now) return edge;
    int64_t age = now - *at;
    if (age < 0 || age >= flash_ms) return 0;
    int left = (int)(flash_ms - age);
    if (left < 1) left = 1;
    ui_wake(u, left);
    return 1;
}
