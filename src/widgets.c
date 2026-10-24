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
