#include "widgets.h"
#include "font.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

static int wrap(int v, int n) {
    if (n <= 0) return 0;
    v %= n;
    if (v < 0) v += n;
    return v;
}

static void third(int x, int w, int gap, int i, int *ox, int *ow) {
    int gaps = gap * 2;
    int inner = w > gaps ? w - gaps : 0;
    int base = inner / 3;
    int extra = inner - base * 3;
    *ow = i == 2 ? base + extra : base;
    *ox = x + i * (base + gap);
}

static void center_text(struct ui *u, int x, int y, int w, int h, const char *s, uint32_t color) {
    int n = s ? (int)strlen(s) : 0;
    int tw = n * FONT_S_W;
    int tx = x + (w - tw) / 2;
    int ty = y + (h - FONT_S_H) / 2;
    ui_text(u, tx, ty, s ? s : "", color, 1);
}

static const int soft_button[3] = {IN_B1, IN_B2, IN_B3};

int ui_soft_keys(struct ui *u, int x, int y, int w, int h, const char *labels[3], int *active,
                 struct ui_soft_style st, const struct input *in, struct ui_soft_state *ks) {
    int pressed = -1;
    int lit[3] = {0, 0, 0};
    if (st.feedback == UI_SOFT_STICKY) {
        if (in && active) {
            if (input_down(in, IN_B1)) *active = 0, pressed = 0;
            if (input_down(in, IN_B2)) *active = 1, pressed = 1;
            if (input_down(in, IN_B3)) *active = 2, pressed = 2;
        }
        int on = active ? *active : 0;
        if (on >= 0 && on < 3) lit[on] = 1;
    } else if (in || ks) {
        for (int i = 0; i < 3; i++) {
            int edge = in && input_down(in, soft_button[i]);
            int held = in && ((in->held >> soft_button[i]) & 1);
            if (edge) {
                pressed = i;
                if (active) *active = i;
            }
            int64_t *at = ks ? &ks->at[i] : 0;
            lit[i] = ui_feedback(u, held, edge, st.feedback, st.flash_ms, at);
        }
    }
    for (int i = 0; i < 3; i++) {
        int cx, cw;
        third(x, w, st.gap, i, &cx, &cw);
        struct ui_paint p = {.fill_set = 1, .fill = lit[i] ? st.highlight : st.fill};
        ui_box(u, cx, y, cw, h, p);
        center_text(u, cx, y, cw, h, labels ? labels[i] : "", lit[i] ? st.active_text : st.text);
    }
    return pressed;
}

static float clampf(float v) {
    if (v < 0) return 0;
    if (v > 1) return 1;
    return v;
}

// Pickup: the value follows only after the knob crosses the current position.
static int pickup(float *norm, int *last, int *engaged, int pos) {
    if (*last < 0) {
        *last = pos;
        return 0;
    }
    float cur = *norm;
    float a = *last / 127.0f - cur;
    float b = pos / 127.0f - cur;
    if (!*engaged && a * b <= 0) *engaged = 1;
    *last = pos;
    if (!*engaged) return 0;
    *norm = clampf(pos / 127.0f);
    return 1;
}

static void default_format(float norm, char *buf, int n) {
    int pct = (int)(norm * 100.0f + 0.5f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    snprintf(buf, (size_t)n, "%d", pct);
}

void ui_knobs(struct ui *u, int x, int y, int w, int h, const char *names[3], float norm[3], ui_format_fn format,
              struct ui_knob_style st, const struct input *in, struct ui_knob_state *ks) {
    if (!ks || !norm) return;
    if (ks->focus < 0 || ks->focus > 2) ks->focus = 0;
    if (in) {
        if (in->encoder) ks->focus = wrap(ks->focus + in->encoder, 3);
        if (input_down(in, IN_PLUS) || input_down(in, IN_MINUS)) {
            float dir = input_down(in, IN_PLUS) ? st.step : -st.step;
            norm[ks->focus] = clampf(norm[ks->focus] + dir);
            ks->engaged[ks->focus] = 0;
        }
        for (int k = 0; k < 3; k++) {
            if (!in->knob_moved[k]) continue;
            pickup(&norm[k], &ks->last[k], &ks->engaged[k], in->knob[k]);
            ks->focus = k;
        }
    }
    for (int i = 0; i < 3; i++) {
        int cx, cw;
        third(x, w, st.gap, i, &cx, &cw);
        int hot = i == ks->focus;
        struct ui_paint p = {
            .fill_set = 1,
            .fill = st.fill,
            .border_set = 1,
            .border = hot ? st.highlight : st.border,
            .border_px = hot ? (st.highlight_px > 0 ? st.highlight_px : 2) : (st.border_px > 0 ? st.border_px : 1),
        };
        ui_box(u, cx, y, cw, h, p);
        char buf[16];
        if (format) format(norm[i], buf, (int)sizeof buf);
        else default_format(norm[i], buf, (int)sizeof buf);
        ui_text(u, cx + 8, y + 8, names && names[i] ? names[i] : "", st.name, 1);
        ui_text(u, cx + 8, y + 28, buf, st.text, 1);
    }
}

static void fill_row(struct ui *u, int x, int y, int w, int h, uint32_t c) {
    struct ui_paint p = {.fill_set = 1, .fill = c};
    ui_box(u, x, y, w, h, p);
}

static uint32_t shade(uint32_t c, int num, int den) {
    if (den <= 0) return c & 0x00ffffff;
    int ch[3] = {(int)((c >> 16) & 255), (int)((c >> 8) & 255), (int)(c & 255)};
    for (int i = 0; i < 3; i++) {
        ch[i] = ch[i] * num / den;
        if (ch[i] < 0) ch[i] = 0;
        if (ch[i] > 255) ch[i] = 255;
    }
    return ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | (uint32_t)ch[2];
}

// Width taken from the right of the rect when the rows do not fit. 0 when they do.
static int scroll_reserve(int w, int h, int visible, int n) {
    if (n <= visible || visible < 1 || w < 16 || h < 4) return 0;
    return 6;
}

static void scroll_bar(struct ui *u, int x, int y, int w, int h, int first, int visible, int n, uint32_t color) {
    if (!scroll_reserve(w, h, visible, n)) return;
    int bw = 4;
    int bx = x + w - bw;
    uint32_t thumb = color ? color : (uint32_t)UI_HL;
    uint32_t track = shade(thumb, 1, 3);
    fill_row(u, bx, y, bw, h, track);
    int thumb_h = h * visible / n;
    if (thumb_h < 8) thumb_h = h < 8 ? h : 8;
    if (thumb_h > h) thumb_h = h;
    int max_first = n - visible;
    int travel = h - thumb_h;
    int ty = y;
    if (max_first > 0 && travel > 0) ty = y + travel * first / max_first;
    fill_row(u, bx, ty, bw, thumb_h, thumb);
}

int ui_list(struct ui *u, int x, int y, int w, int h, const char **rows, int n, struct ui_list_style st,
            const struct input *in, struct ui_list_state *ls) {
    if (!ls) return -1;
    if (n < 0) n = 0;
    int row_h = st.row_h > 0 ? st.row_h : FONT_S_H + 4;
    if (in && n > 0 && in->encoder) ls->cursor = wrap(ls->cursor + in->encoder, n);
    if (n == 0) ls->cursor = 0;
    else if (ls->cursor < 0 || ls->cursor >= n) ls->cursor = wrap(ls->cursor, n);
    int hit = -1;
    if (in && n > 0 && in->encoder_press && !in->shift) hit = ls->cursor;
    int visible = h / row_h;
    if (visible < 1) visible = 1;
    int first = 0;
    if (ls->cursor >= visible) first = ls->cursor - visible + 1;
    int reserve = scroll_reserve(w, h, visible, n);
    int row_w = w - reserve;
    for (int i = first; i < n && i < first + visible; i++) {
        int yy = y + (i - first) * row_h;
        if (i == ls->cursor) {
            if (st.current_fill_set) fill_row(u, x, yy, row_w, row_h, st.current_fill);
            ui_highlight(u, x, yy, row_w, row_h, st.highlight, st.highlight_px > 0 ? st.highlight_px : 1);
        }
        int ty = yy + (row_h - FONT_S_H) / 2;
        ui_text(u, x + 8, ty, rows && rows[i] ? rows[i] : "", st.text, 1);
    }
    scroll_bar(u, x, y, w, h, first, visible, n, st.highlight);
    return hit;
}

static void pad_cell(int x, int y, int w, int h, int gap, int index, int *ox, int *oy, int *ow, int *oh) {
    int gaps = gap * 3;
    int cw = w > gaps ? (w - gaps) / 4 : 0;
    int ch = h > gaps ? (h - gaps) / 4 : 0;
    int col = index % 4;
    int visual = 3 - index / 4;
    *ox = x + col * (cw + gap);
    *oy = y + visual * (ch + gap);
    *ow = cw;
    *oh = ch;
}

static uint32_t vel_color(int vel) {
    if (vel < 0) vel = 0;
    if (vel > 127) vel = 127;
    return ((uint32_t)(vel * 2) << 16) | (uint32_t)((127 - vel) * 2);
}

static uint32_t brighten(uint32_t c, int pressure) {
    if (pressure <= 0) return c;
    if (pressure > 127) pressure = 127;
    int ch[3] = {(int)((c >> 16) & 255), (int)((c >> 8) & 255), (int)(c & 255)};
    for (int i = 0; i < 3; i++) ch[i] = ch[i] + (255 - ch[i]) * pressure / 127;
    return ((uint32_t)ch[0] << 16) | ((uint32_t)ch[1] << 8) | (uint32_t)ch[2];
}

static void led_of(struct ui *u, int index, uint32_t c) {
    ui_led(u, index, (int)((c >> 17) & 0x7f), (int)((c >> 9) & 0x7f), (int)((c >> 1) & 0x7f));
}

static void grid_cell(int x, int y, int w, int h, int gap, int cols, int rows, int col, int row, int *ox,
                      int *oy, int *ow, int *oh) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (gap < 0) gap = 0;
    int gw = cols > 1 ? gap * (cols - 1) : 0;
    int gh = rows > 1 ? gap * (rows - 1) : 0;
    int inner_w = w > gw ? w - gw : 0;
    int inner_h = h > gh ? h - gh : 0;
    int cw = inner_w / cols;
    int ch = inner_h / rows;
    int extra_w = inner_w - cw * cols;
    int extra_h = inner_h - ch * rows;
    *ow = col == cols - 1 ? cw + extra_w : cw;
    *oh = row == rows - 1 ? ch + extra_h : ch;
    *ox = x + col * (cw + gap);
    *oy = y + row * (ch + gap);
}

static void paint_rule(struct ui *u, int x, int y, int w, int h, int rx, int ry, int rw, int rh, uint32_t color) {
    int x0 = rx < x ? x : rx;
    int y0 = ry < y ? y : ry;
    int x1 = rx + rw > x + w ? x + w : rx + rw;
    int y1 = ry + rh > y + h ? y + h : ry + rh;
    if (x1 <= x0 || y1 <= y0) return;
    fill_row(u, x0, y0, x1 - x0, y1 - y0, color);
}

static void block_rules(struct ui *u, int x, int y, int w, int h, int gap, int cols, int rows, int block, int thick,
                        uint32_t color) {
    if (block <= 0 || thick <= 0) return;
    for (int c = block; c < cols; c += block) {
        int x0, y0, cw, ch;
        grid_cell(x, y, w, h, gap, cols, rows, c - 1, 0, &x0, &y0, &cw, &ch);
        int gx = x0 + cw;
        int lx = gx + (gap - thick) / 2;
        paint_rule(u, x, y, w, h, lx, y, thick, h, color);
    }
    for (int r = block; r < rows; r += block) {
        int x0, y0, cw, ch;
        grid_cell(x, y, w, h, gap, cols, rows, 0, r - 1, &x0, &y0, &cw, &ch);
        int gy = y0 + ch;
        int ly = gy + (gap - thick) / 2;
        paint_rule(u, x, y, w, h, x, ly, w, thick, color);
    }
}

static void outline_band(struct ui *u, int x, int y, int w, int h, int gap, int cols, int rows, int block, int cell,
                         uint32_t color, int thick) {
    if (block <= 0 || cell < 0 || cell >= cols * rows || thick <= 0) return;
    int col = cell % cols;
    int row = cell / cols;
    int bc = col - col % block;
    int br = row - row % block;
    int ec = bc + block - 1;
    int er = br + block - 1;
    if (ec >= cols) ec = cols - 1;
    if (er >= rows) er = rows - 1;
    int x0, y0, w0, h0, x1, y1, w1, h1;
    grid_cell(x, y, w, h, gap, cols, rows, bc, br, &x0, &y0, &w0, &h0);
    grid_cell(x, y, w, h, gap, cols, rows, ec, er, &x1, &y1, &w1, &h1);
    ui_highlight(u, x0, y0, x1 + w1 - x0, y1 + h1 - y0, color, thick);
}

void ui_grid(struct ui *u, int x, int y, int w, int h, int cols, int rows, const char **labels, const uint32_t *fills,
             int cursor, int band_cell, struct ui_grid_style st) {
    if (!u || cols < 1 || rows < 1 || w <= 0 || h <= 0) return;
    int gap = st.gap > 0 ? st.gap : 0;
    int n = cols * rows;
    fill_row(u, x, y, w, h, st.line);
    for (int i = 0; i < n; i++) {
        int cx, cy, cw, ch;
        grid_cell(x, y, w, h, gap, cols, rows, i % cols, i / cols, &cx, &cy, &cw, &ch);
        uint32_t fill = fills ? fills[i] : st.fill;
        fill_row(u, cx, cy, cw, ch, fill);
    }
    int thick = st.block > 0 ? (st.block_px > 0 ? st.block_px : 2) : 0;
    block_rules(u, x, y, w, h, gap, cols, rows, st.block, thick, st.block_line);
    for (int i = 0; i < n; i++) {
        if (!labels || !labels[i] || !labels[i][0]) continue;
        int cx, cy, cw, ch;
        grid_cell(x, y, w, h, gap, cols, rows, i % cols, i / cols, &cx, &cy, &cw, &ch);
        center_text(u, cx, cy, cw, ch, labels[i], st.text);
    }
    int edge = st.cursor_px > 0 ? st.cursor_px : 1;
    outline_band(u, x, y, w, h, gap, cols, rows, st.block, band_cell, st.band, edge);
    if (cursor >= 0 && cursor < n && edge > 0) {
        int cx, cy, cw, ch;
        grid_cell(x, y, w, h, gap, cols, rows, cursor % cols, cursor / cols, &cx, &cy, &cw, &ch);
        ui_highlight(u, cx, cy, cw, ch, st.cursor, edge);
    }
}

void ui_pads(struct ui *u, int x, int y, int w, int h, uint32_t colors[16], struct ui_pad_style st,
             const struct input *in) {
    if (!colors) return;
    for (int i = 0; i < 16; i++) {
        int hit = in && in->pad_hit[i] >= 0;
        if (hit) colors[i] = vel_color(in->pad_hit[i]);
        uint32_t shown = colors[i];
        int pressure = -1;
        if (in && in->pad_down[i]) {
            pressure = in->pad_pressure[i] >= 0 ? in->pad_pressure[i] : in->pad_at[i];
            shown = brighten(shown, pressure);
        }
        int cx, cy, cw, ch;
        pad_cell(x, y, w, h, st.gap, i, &cx, &cy, &cw, &ch);
        struct ui_paint p = {.fill_set = 1, .fill = shown, .border_set = st.border_px > 0, .border = st.border,
                             .border_px = st.border_px};
        ui_box(u, cx, cy, cw, ch, p);
        if (!in) continue;
        if (hit || in->pad_pressure[i] >= 0) led_of(u, i, shown);
        else if (in->pad_release[i]) led_of(u, i, colors[i]);
    }
}

enum { PAD_NONE, PAD_GROUP, PAD_MOD, PAD_TAP };

static int pad_held(const struct input *in, int pad) {
    if (!in || pad < 0 || pad > 15) return 0;
    return in->pad_down[pad] || in->pad_hit[pad] >= 0;
}

static int held_pressure(const struct input *in, int pad) {
    if (!pad_held(in, pad)) return 0;
    if (in->pad_pressure[pad] >= 0) return in->pad_pressure[pad];
    if (in->pad_at[pad] > 0) return in->pad_at[pad];
    return 48;
}

struct ui_pad_hit ui_pad_modes(struct ui *u, int x, int y, int w, int h, const int *group, int n,
                               const struct ui_pad_mode *modes, int nmodes, const struct ui_pad_tap *taps, int ntaps,
                               struct ui_pad_mode_style st, const struct input *in) {
    struct ui_pad_hit hit = {-1, -1, -1};
    if (!u) return hit;
    if (!group) n = 0;
    if (!modes) nmodes = 0;
    if (!taps) ntaps = 0;
    if (n < 0) n = 0;
    if (nmodes < 0) nmodes = 0;
    if (ntaps < 0) ntaps = 0;

    int role[16], slot_of[16], mode_of[16], tap_of[16];
    for (int i = 0; i < 16; i++) role[i] = PAD_NONE, slot_of[i] = mode_of[i] = tap_of[i] = -1;
    for (int i = 0; i < n; i++) {
        int p = group[i];
        if (p < 0 || p > 15 || role[p] != PAD_NONE) continue;
        role[p] = PAD_GROUP;
        slot_of[p] = i;
    }
    for (int i = 0; i < nmodes; i++) {
        int p = modes[i].pad;
        if (p < 0 || p > 15) continue;
        role[p] = PAD_MOD;
        mode_of[p] = i;
    }
    for (int i = 0; i < ntaps; i++) {
        int p = taps[i].pad;
        if (p < 0 || p > 15 || role[p] == PAD_MOD) continue;
        role[p] = PAD_TAP;
        tap_of[p] = i;
    }

    if (in) {
        for (int i = 0; i < nmodes; i++) {
            if (!pad_held(in, modes[i].pad)) continue;
            hit.mode = i;
            break;
        }
        if (hit.mode >= 0) {
            for (int i = 0; i < n; i++) {
                int p = group[i];
                if (p < 0 || p > 15 || role[p] != PAD_GROUP) continue;
                if (in->pad_hit[p] < 0) continue;
                hit.slot = i;
                break;
            }
        } else {
            for (int i = 0; i < ntaps; i++) {
                int p = taps[i].pad;
                if (p < 0 || p > 15 || role[p] != PAD_TAP) continue;
                if (in->pad_hit[p] < 0) continue;
                hit.tap = i;
                break;
            }
        }
    }

    for (int p = 0; p < 16; p++) {
        uint32_t color = 0x000000;
        const char *label = 0;
        if (role[p] == PAD_GROUP) {
            if (hit.mode >= 0) {
                color = modes[hit.mode].color;
                if (slot_of[p] == st.selected) color = st.selected_color;
                if (modes[hit.mode].names) label = modes[hit.mode].names[slot_of[p]];
            } else {
                color = st.idle;
            }
        } else if (role[p] == PAD_MOD) {
            int mi = mode_of[p];
            color = pad_held(in, p) ? modes[mi].held : st.mod_idle;
            label = modes[mi].label;
        } else if (role[p] == PAD_TAP) {
            color = taps[tap_of[p]].color;
            label = taps[tap_of[p]].label;
        }
        if (role[p] != PAD_NONE) color = brighten(color, held_pressure(in, p));
        int cx, cy, cw, ch;
        pad_cell(x, y, w, h, st.gap, p, &cx, &cy, &cw, &ch);
        struct ui_paint box = {.fill_set = 1, .fill = color, .border_set = st.border_px > 0, .border = st.border,
                               .border_px = st.border_px};
        ui_box(u, cx, cy, cw, ch, box);
        if (label && label[0]) center_text(u, cx, cy, cw, ch, label, st.text);
        if (in && role[p] != PAD_NONE) led_of(u, p, color);
    }
    return hit;
}

static int bound_pad(const struct ui_menu_item *it, int index) {
    if (!it || it->pad >= 16) return -1;
    if (it->pad >= 0) return it->pad;
    if (index >= 0 && index < 16) return index;
    return -1;
}

static void pads_off(struct ui *u, unsigned bits) {
    for (int i = 0; i < 16; i++)
        if (bits & (1u << i)) ui_led(u, i, 0, 0, 0);
}

static unsigned buttons_held(const struct input *in) {
    unsigned held = in->held;
    if (in->shift) held |= 1u << IN_SHIFT;
    return held;
}

void ui_menu_prepare(struct ui_menu *m, unsigned hold, const char *title, const struct ui_menu_item *items, int n) {
    if (!m || m->items || !items) return;
    if (n < 0) n = 0;
    if (!m->hold) m->hold = hold;
    if (!m->title) m->title = title;
    m->items = items;
    m->n = n;
}

struct ui_menu_style ui_menu_style_default(void) {
    struct ui_menu_style st = {
        .fill = 0x18181e,
        .text = UI_TEXT,
        .title = UI_HL,
        .highlight = UI_HL,
        .border = 0x505868,
        .border_px = 2,
        .row_h = 24,
        .highlight_px = 2,
        .current_fill_set = 1,
        .current_fill = 0x2a2418,
        .light_pads = 1,
    };
    return st;
}

int ui_menu_open(const struct ui_menu *m, const struct input *in) {
    if (!m || !in || !m->hold) return 0;
    unsigned held = buttons_held(in);
    return (held & m->hold) == m->hold;
}

int ui_menu(struct ui *u, int x, int y, int w, int h, struct ui_menu *m, struct ui_menu_style st,
            const struct input *in) {
    if (!u || !m) return -1;
    if (!ui_menu_open(m, in)) {
        if (m->pad_lit) pads_off(u, m->pad_lit);
        m->pad_lit = 0;
        m->shown = 0;
        return -1;
    }
    m->shown = 1;
    int n = m->n > 0 && m->items ? m->n : 0;
    int chosen = -1;
    if (n > 0) {
        if (in->encoder) m->cursor = wrap(m->cursor + in->encoder, n);
        if (m->cursor < 0 || m->cursor >= n) m->cursor = wrap(m->cursor, n);
        for (int i = 0; i < n; i++) {
            int pad = bound_pad(&m->items[i], i);
            if (pad < 0 || in->pad_hit[pad] < 0) continue;
            m->cursor = i;
            chosen = m->items[i].id;
            break;
        }
        if (chosen < 0) {
            for (int i = 0; i < n; i++) {
                int key = m->items[i].key;
                if (key < 0 || key >= IN_N) continue;
                if ((m->hold & (1u << key)) || !input_down(in, key)) continue;
                m->cursor = i;
                chosen = m->items[i].id;
                break;
            }
        }
        int press = in->encoder_press && (m->hold & (1u << IN_ENCODER)) == 0;
        if (chosen < 0 && press) chosen = m->items[m->cursor].id;
    }

    int row_h = st.row_h > 0 ? st.row_h : FONT_S_H + 8;
    int border = st.border_px > 0 ? st.border_px : 0;
    struct ui_paint panel = {.fill_set = 1, .fill = st.fill, .border_set = border > 0, .border = st.border, .border_px = border};
    ui_box(u, x, y, w, h, panel);
    int x_in = x + border;
    int y_rows = y + border;
    int w_in = w - border * 2;
    int h_in = h - border * 2;
    if (m->title && m->title[0] && h_in > 0) {
        int ty = y_rows + (row_h - FONT_S_H) / 2;
        ui_text(u, x_in + 8, ty, m->title, st.title ? st.title : st.text, 1);
        y_rows += row_h;
        h_in -= row_h;
    }
    if (n > 0 && w_in > 0 && h_in > 0) {
        int visible = h_in / row_h;
        if (visible < 1) visible = 1;
        int first = m->cursor >= visible ? m->cursor - visible + 1 : 0;
        int reserve = scroll_reserve(w_in, h_in, visible, n);
        int row_w = w_in - reserve;
        for (int i = first; i < n && i < first + visible; i++) {
            int yy = y_rows + (i - first) * row_h;
            if (i == m->cursor) {
                if (st.current_fill_set) fill_row(u, x_in, yy, row_w, row_h, st.current_fill);
                ui_highlight(u, x_in, yy, row_w, row_h, st.highlight, st.highlight_px > 0 ? st.highlight_px : 1);
            }
            int pad = bound_pad(&m->items[i], i);
            char line[48];
            const char *label = m->items[i].label ? m->items[i].label : "";
            if (pad >= 0) snprintf(line, sizeof line, "%d  %s", pad + 1, label);
            else snprintf(line, sizeof line, "%s", label);
            ui_text(u, x_in + 8, yy + (row_h - FONT_S_H) / 2, line, st.text, 1);
        }
        scroll_bar(u, x_in, y_rows, w_in, h_in, first, visible, n, st.highlight);
    }

    unsigned lit = 0;
    if (st.light_pads && n > 0) {
        for (int i = 0; i < n; i++) {
            int pad = bound_pad(&m->items[i], i);
            if (pad < 0) continue;
            lit |= 1u << pad;
            led_of(u, pad, i == m->cursor ? st.highlight : st.border);
        }
    }
    if (m->pad_lit & ~lit) pads_off(u, m->pad_lit & ~lit);
    m->pad_lit = lit;
    return chosen;
}

static const char *const PAD_MAP_NORMAL[16] = {
    "abc",   // Pad 1 (bottom-left)
    "def",   // Pad 2
    "ghi",   // Pad 3
    "jkl",   // Pad 4
    "mno",   // Pad 5
    "pqr",   // Pad 6
    "stu",   // Pad 7
    "vwx",   // Pad 8
    "yz",    // Pad 9
    "123",   // Pad 10
    "456",   // Pad 11
    "7890",  // Pad 12
    "_-. ",  // Pad 13 (top-left)
    "/:;+",  // Pad 14
    " ",     // Pad 15 (Space)
    ""       // Pad 16 (DEL action)
};

static const char *const PAD_MAP_SHIFT[16] = {
    "ABC",   // Pad 1
    "DEF",   // Pad 2
    "GHI",   // Pad 3
    "JKL",   // Pad 4
    "MNO",   // Pad 5
    "PQR",   // Pad 6
    "STU",   // Pad 7
    "VWX",   // Pad 8
    "YZ",    // Pad 9
    "!@#",   // Pad 10
    "$%^",   // Pad 11
    "&*()",  // Pad 12
    "+=~|",  // Pad 13
    "?<>\\", // Pad 14
    " ",     // Pad 15
    ""       // Pad 16 (CLR action)
};

static char g_ui_clipboard[128] = {0};

void ui_text_editor_init(struct ui_text_editor *ed, char *buf, int max_len, const char *title) {
    if (!ed) return;
    memset(ed, 0, sizeof *ed);
    ed->buf = buf;
    ed->max_len = max_len;
    ed->title = title ? title : "TEXT INPUT";
    ed->len = buf ? (int)strlen(buf) : 0;
    ed->cursor = ed->len;
    ed->sel_anchor = -1;
    ed->sel_start = -1;
    ed->sel_end = -1;
    ed->last_pad = -1;
    ed->pad_cycle = 0;
    ed->pad_lit = 0;
}

struct ui_text_editor_style ui_text_editor_style_default(void) {
    struct ui_text_editor_style st = {
        .fill = 0x14161c,
        .text_box = 0x0e1014,
        .text = UI_TEXT,
        .title = UI_HL,
        .border = 0x3e4656,
        .highlight = UI_HL,
        .pad_fill = 0x222632,
        .pad_text = 0xd0d4dc,
        .pad_active = 0x404860,
        .border_px = 1,
    };
    return st;
}

void ui_text_editor_copy(struct ui_text_editor *ed) {
    if (!ed || !ed->buf) return;
    if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start && ed->sel_start < ed->len) {
        int count = ed->sel_end - ed->sel_start;
        if (count >= (int)sizeof(g_ui_clipboard)) count = (int)sizeof(g_ui_clipboard) - 1;
        memcpy(g_ui_clipboard, ed->buf + ed->sel_start, (size_t)count);
        g_ui_clipboard[count] = '\0';
    }
}

void ui_text_editor_delete(struct ui_text_editor *ed) {
    if (!ed || !ed->buf) return;
    if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
        int s0 = ed->sel_start;
        int s1 = ed->sel_end;
        if (s1 > ed->len) s1 = ed->len;
        int count = s1 - s0;
        if (count > 0) {
            memmove(ed->buf + s0, ed->buf + s1, (size_t)(ed->len - s1 + 1));
            ed->len -= count;
            ed->cursor = s0;
        }
        ed->sel_anchor = -1;
        ed->sel_start = -1;
        ed->sel_end = -1;
        ed->last_pad = -1;
        ed->pad_cycle = 0;
    } else if (ed->cursor > 0 && ed->len > 0) {
        memmove(ed->buf + ed->cursor - 1, ed->buf + ed->cursor, (size_t)(ed->len - ed->cursor + 1));
        ed->cursor--;
        ed->len--;
        ed->last_pad = -1;
        ed->pad_cycle = 0;
    }
}

void ui_text_editor_cut(struct ui_text_editor *ed) {
    ui_text_editor_copy(ed);
    ui_text_editor_delete(ed);
}

void ui_text_editor_paste(struct ui_text_editor *ed) {
    if (!ed || !ed->buf) return;
    if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
        ui_text_editor_delete(ed);
    }
    int clip_len = (int)strlen(g_ui_clipboard);
    if (clip_len <= 0) return;
    int avail = ed->max_len - 1 - ed->len;
    if (clip_len > avail) clip_len = avail;
    if (clip_len <= 0) return;

    if (ed->cursor < 0) ed->cursor = 0;
    if (ed->cursor > ed->len) ed->cursor = ed->len;

    memmove(ed->buf + ed->cursor + clip_len, ed->buf + ed->cursor, (size_t)(ed->len - ed->cursor + 1));
    memcpy(ed->buf + ed->cursor, g_ui_clipboard, (size_t)clip_len);
    ed->cursor += clip_len;
    ed->len += clip_len;
    ed->last_pad = -1;
    ed->pad_cycle = 0;
}

void ui_text_editor_clear(struct ui_text_editor *ed) {
    if (!ed || !ed->buf) return;
    ed->buf[0] = '\0';
    ed->len = 0;
    ed->cursor = 0;
    ed->sel_anchor = -1;
    ed->sel_start = -1;
    ed->sel_end = -1;
    ed->last_pad = -1;
    ed->pad_cycle = 0;
}

int ui_text_editor(struct ui *u, int x, int y, int w, int h, struct ui_text_editor *ed,
                   struct ui_text_editor_style st, const struct input *in) {
    if (!u || !ed || !ed->buf) return UI_TEXT_CANCEL;

    int status = UI_TEXT_ACTIVE;

    // Process navigation and input
    if (in) {
        // Navigation: encoder wheel and - / + buttons
        int step = in->encoder;
        if (input_down(in, IN_PLUS)) step++;
        if (input_down(in, IN_MINUS)) step--;

        if (step != 0) {
            if (in->shift) {
                // Expanding or contracting selection
                if (ed->sel_anchor < 0) {
                    ed->sel_anchor = ed->cursor;
                }
                ed->cursor += step;
                if (ed->cursor < 0) ed->cursor = 0;
                if (ed->cursor > ed->len) ed->cursor = ed->len;

                if (ed->cursor == ed->sel_anchor) {
                    ed->sel_start = -1;
                    ed->sel_end = -1;
                } else if (ed->cursor < ed->sel_anchor) {
                    ed->sel_start = ed->cursor;
                    ed->sel_end = ed->sel_anchor;
                } else {
                    ed->sel_start = ed->sel_anchor;
                    ed->sel_end = ed->cursor;
                }
            } else {
                // Moving without shift clears selection
                ed->sel_anchor = -1;
                ed->sel_start = -1;
                ed->sel_end = -1;
                ed->cursor += step;
                if (ed->cursor < 0) ed->cursor = 0;
                if (ed->cursor > ed->len) ed->cursor = ed->len;
            }
            ed->last_pad = -1;
            ed->pad_cycle = 0;
        }

        // Hardware ERASE button
        if (input_down(in, IN_ERASE)) {
            ui_text_editor_delete(ed);
        }

        // Soft buttons B1, B2, B3 (placed at the top of the screen)
        if (in->shift) {
            if (input_down(in, IN_B1)) ui_text_editor_cut(ed);
            if (input_down(in, IN_B2)) ui_text_editor_copy(ed);
            if (input_down(in, IN_B3)) ui_text_editor_paste(ed);
        } else {
            if (input_down(in, IN_B1)) status = UI_TEXT_CANCEL;
            if (input_down(in, IN_B2)) {
                // Insert space
                if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
                    ui_text_editor_delete(ed);
                }
                if (ed->cursor == ed->len) {
                    if (ed->len < ed->max_len - 1) {
                        ed->buf[ed->cursor] = ' ';
                        ed->len++;
                        ed->buf[ed->len] = '\0';
                        ed->cursor++;
                    }
                } else {
                    ed->buf[ed->cursor] = ' ';
                }
                ed->last_pad = -1;
                ed->pad_cycle = 0;
            }
            if (input_down(in, IN_B3)) status = UI_TEXT_OK;
        }

        // Encoder press: append / insert character space at cursor and advance cursor
        if (in->encoder_press) {
            if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
                ui_text_editor_delete(ed);
            }
            if (ed->len < ed->max_len - 1) {
                int insert_pos;
                if (ed->len == 0) {
                    insert_pos = 0;
                } else if (ed->cursor < ed->len) {
                    insert_pos = ed->cursor + 1;
                } else {
                    insert_pos = ed->len;
                }
                memmove(ed->buf + insert_pos + 1, ed->buf + insert_pos, (size_t)(ed->len - insert_pos + 1));
                ed->buf[insert_pos] = ' ';
                ed->len++;
                ed->cursor = insert_pos;
                ed->last_pad = -1;
                ed->pad_cycle = 0;
                ed->sel_anchor = -1;
                ed->sel_start = -1;
                ed->sel_end = -1;
            }
        }

        // Pad hits
        for (int p = 0; p < 16; p++) {
            if (in->pad_hit[p] < 0) continue;
            int shift = in->shift;

            if (p == 15) {
                // Pad 16: DEL (or CLR if shifted)
                if (shift) ui_text_editor_clear(ed);
                else ui_text_editor_delete(ed);
            } else if (p == 14) {
                // Pad 15: SPC
                if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
                    ui_text_editor_delete(ed);
                }
                if (ed->cursor == ed->len) {
                    if (ed->len < ed->max_len - 1) {
                        ed->buf[ed->cursor] = ' ';
                        ed->len++;
                        ed->buf[ed->len] = '\0';
                        ed->cursor++;
                    }
                } else {
                    ed->buf[ed->cursor] = ' ';
                }
                ed->last_pad = -1;
                ed->pad_cycle = 0;
            } else {
                // Character pad (Pads 1..14)
                const char *chars = shift ? PAD_MAP_SHIFT[p] : PAD_MAP_NORMAL[p];
                int nchars = (int)strlen(chars);
                if (nchars > 0) {
                    if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
                        ui_text_editor_delete(ed);
                    }
                    if (ed->cursor == ed->len) {
                        if (ed->len < ed->max_len - 1) {
                            ed->buf[ed->cursor] = chars[0];
                            ed->len++;
                            ed->buf[ed->len] = '\0';
                            ed->last_pad = p;
                            ed->pad_cycle = 0;
                        }
                    } else {
                        if (ed->last_pad == p) {
                            ed->pad_cycle = (ed->pad_cycle + 1) % nchars;
                            ed->buf[ed->cursor] = chars[ed->pad_cycle];
                        } else {
                            ed->last_pad = p;
                            ed->pad_cycle = 0;
                            ed->buf[ed->cursor] = chars[0];
                        }
                    }
                }
            }
        }
    }

    // Outer frame
    struct ui_paint panel = {
        .fill_set = 1, .fill = st.fill,
        .border_set = st.border_px > 0, .border = st.border, .border_px = st.border_px
    };
    ui_box(u, x, y, w, h, panel);

    // 1. Soft Keys Bar (at the top of the screen)
    int sk_x = x + 8;
    int sk_y = y + 6;
    int sk_w = w - 16;
    int sk_h = 24;

    const char *labels[3];
    if (in && in->shift) {
        labels[0] = "B1: CUT";
        labels[1] = "B2: COPY";
        labels[2] = "B3: PASTE";
    } else {
        labels[0] = "B1: CANCEL";
        labels[1] = "B2: SPACE";
        labels[2] = "B3: OK";
    }

    for (int i = 0; i < 3; i++) {
        int bx, bw;
        third(sk_x, sk_w, 4, i, &bx, &bw);
        int down = in && ((in->held >> soft_button[i]) & 1);
        struct ui_paint bp = {
            .fill_set = 1, .fill = down ? st.highlight : st.pad_fill,
            .border_set = 1, .border = st.border, .border_px = 1
        };
        ui_box(u, bx, sk_y, bw, sk_h, bp);
        center_text(u, bx, sk_y, bw, sk_h, labels[i], down ? st.fill : st.text);
    }

    // 2. Title and Length Counter
    int info_y = sk_y + sk_h + 6;
    ui_text(u, x + 10, info_y, ed->title ? ed->title : "TEXT INPUT", st.title, 1);

    char count_str[16];
    snprintf(count_str, sizeof count_str, "%d/%d", ed->len, ed->max_len > 0 ? ed->max_len - 1 : 0);
    int cw = (int)strlen(count_str) * FONT_S_W;
    ui_text(u, x + w - 10 - cw, info_y, count_str, 0x8890a0, 1);

    // 3. Text Input Box
    int tb_x = x + 8;
    int tb_y = info_y + FONT_S_H + 4;
    int tb_w = w - 16;
    int tb_h = 24;

    struct ui_paint tbp = {
        .fill_set = 1, .fill = st.text_box,
        .border_set = 1, .border = st.highlight, .border_px = 1
    };
    ui_box(u, tb_x, tb_y, tb_w, tb_h, tbp);

    int tx = tb_x + 8;
    int ty = tb_y + (tb_h - FONT_S_H) / 2;

    // Selection highlight background
    if (ed->sel_start >= 0 && ed->sel_end > ed->sel_start) {
        int s0 = ed->sel_start;
        int s1 = ed->sel_end;
        if (s1 > ed->len) s1 = ed->len;
        int hx = tx + s0 * FONT_S_W;
        int hw = (s1 - s0) * FONT_S_W;
        fill_row(u, hx, ty - 2, hw, FONT_S_H + 4, 0x4a3a18);
    }

    // Block cursor indicator
    int cur_x = tx + ed->cursor * FONT_S_W;
    fill_row(u, cur_x, ty - 1, FONT_S_W, FONT_S_H + 2, st.highlight);

    // Text characters
    for (int c = 0; c < ed->len; c++) {
        char ch_buf[2] = {ed->buf[c], 0};
        int in_sel = (c >= ed->sel_start && c < ed->sel_end);
        uint32_t c_color = in_sel ? UI_HL : st.text;
        if (c == ed->cursor) {
            c_color = st.text_box; // inverted text on the block cursor
        }
        ui_text(u, tx + c * FONT_S_W, ty, ch_buf, c_color, 1);
    }

    // 4. 4x4 Pad Grid Legend (at the bottom of the screen)
    int grid_x = x + 8;
    int grid_y = tb_y + tb_h + 6;
    int grid_w = w - 16;
    int grid_h = (y + h) - grid_y - 6;
    int gap = 3;

    unsigned lit = 0;
    for (int p = 0; p < 16; p++) {
        int cx, cy, cw, ch;
        pad_cell(grid_x, grid_y, grid_w, grid_h, gap, p, &cx, &cy, &cw, &ch);

        int hit = in && (in->pad_down[p] || in->pad_hit[p] >= 0);
        int is_last = (ed->last_pad == p);

        uint32_t pad_bg = hit ? st.pad_active : (is_last ? 0x282c3c : st.pad_fill);
        uint32_t pad_border = is_last ? st.highlight : st.border;

        struct ui_paint pad_p = {
            .fill_set = 1, .fill = pad_bg,
            .border_set = 1, .border = pad_border, .border_px = 1
        };
        ui_box(u, cx, cy, cw, ch, pad_p);

        // Pad number in corner
        char num_buf[4];
        snprintf(num_buf, sizeof num_buf, "%d", p + 1);
        ui_text(u, cx + 3, cy + 2, num_buf, 0x687080, 1);

        // Pad character legend
        if (p == 14) {
            center_text(u, cx, cy, cw, ch, "SPC", hit ? UI_TEXT : 0xc0c8d8);
        } else if (p == 15) {
            center_text(u, cx, cy, cw, ch, in && in->shift ? "CLR" : "DEL", hit ? UI_TEXT : 0xd88080);
        } else {
            const char *s = in && in->shift ? PAD_MAP_SHIFT[p] : PAD_MAP_NORMAL[p];
            int n = (int)strlen(s);
            if (is_last && ed->pad_cycle >= 0 && ed->pad_cycle < n) {
                // Highlight active cycle character
                int tw = (n * 2 - 1) * FONT_S_W;
                int sx = cx + (cw - tw) / 2;
                int sy = cy + (ch - FONT_S_H) / 2;
                for (int k = 0; k < n; k++) {
                    char ch_str[2] = {s[k], 0};
                    uint32_t col = (k == ed->pad_cycle) ? st.highlight : st.pad_text;
                    ui_text(u, sx + k * 2 * FONT_S_W, sy, ch_str, col, 1);
                }
            } else {
                char spaced[16];
                int sp_len = 0;
                for (int k = 0; s[k] && sp_len < (int)sizeof(spaced) - 2; k++) {
                    spaced[sp_len++] = s[k];
                    if (s[k + 1]) spaced[sp_len++] = ' ';
                }
                spaced[sp_len] = '\0';
                center_text(u, cx, cy, cw, ch, spaced, st.pad_text);
            }
        }

        // Pad LED updates
        uint32_t led_col = 0;
        if (hit) led_col = st.highlight;
        else if (is_last) led_col = in && in->shift ? 0xff8800 : 0x0088ff;
        else if (in && in->shift) led_col = 0x301800;
        else led_col = 0x081020;

        led_of(u, p, led_col);
        lit |= (1u << p);
    }

    if (status != UI_TEXT_ACTIVE) {
        if (ed->pad_lit) pads_off(u, ed->pad_lit);
        ed->pad_lit = 0;
    } else {
        if (ed->pad_lit & ~lit) pads_off(u, ed->pad_lit & ~lit);
        ed->pad_lit = lit;
    }

    return status;
}
