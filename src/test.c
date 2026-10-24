// Host checks. No device, no DRM. Build: make test
#include "input.h"
#include "ui.h"
#include "widgets.h"
#include <stdio.h>
#include <string.h>

int clear_frame(struct ui *u, const struct input *in, void *state);
int boxes_frame(struct ui *u, const struct input *in, void *state);
struct buttons_state { int focus; int on[3]; };
int buttons_frame(struct ui *u, const struct input *in, void *state);
struct highlight_state { int focus; };
int highlight_frame(struct ui *u, const struct input *in, void *state);
struct soft_state { int active; };
int soft_keys_frame(struct ui *u, const struct input *in, void *state);
struct knobs_state { float norm[3]; struct ui_knob_state ks; };
int knobs_frame(struct ui *u, const struct input *in, void *state);
struct list_state { struct ui_list_state ls; const char *chosen; };
int list_frame(struct ui *u, const struct input *in, void *state);
struct pads_state { uint32_t color[16]; int ready; };
int pads_frame(struct ui *u, const struct input *in, void *state);
struct anim_state { int x; int dir; };
int anim_frame(struct ui *u, const struct input *in, void *state);

static int failed;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "fail: %s\n", msg);
        failed++;
    }
}

static struct input blank(void) {
    struct input in;
    input_begin(&in, 0);
    return in;
}

static uint32_t px(const struct ui *u, int x, int y) { return u->px[y * UI_W + x]; }

static void test_parser(void) {
    const unsigned char bytes[] = {0x99, 37, 100, 0xA9, 37, 0xF8, 5, 0xF0, 0x47, 0x01, 0xF7, 0x90, 6, 127, 0xC0, 5};
    const unsigned char want[][3] = {{0x99, 37, 100}, {0xA9, 37, 5}, {0x90, 6, 127}, {0xC0, 5, 0}};
    struct parser p = {0};
    int got = 0;
    for (size_t i = 0; i < sizeof bytes; i++)
        if (parser_feed(&p, bytes[i])) {
            expect(got < 4 && memcmp(p.msg, want[got], 3) == 0, "parser message");
            got++;
        }
    expect(got == 4, "parser count");

    struct input_dev dev = {0};
    struct input in;
    input_begin(&in, &dev);
    unsigned char enc[3] = {0xb0, 100, 0x7f};
    input_push(&dev, &in, enc);
    expect(in.encoder == -1, "encoder step");
    unsigned char sh[3] = {0x90, 49, 127};
    unsigned char push[3] = {0x90, 111, 127};
    input_push(&dev, &in, sh);
    input_push(&dev, &in, push);
    expect(input_quit(&in), "quit chord");
    expect(in.shift && (in.held & (1u << IN_SHIFT)), "shift held in the frame");

    struct input next;
    input_begin(&next, &dev);
    expect(next.shift && (next.held & (1u << IN_SHIFT)), "shift stays held");
    unsigned char up[3] = {0x80, 49, 0};
    input_push(&dev, &next, up);
    expect(!next.shift && (next.held & (1u << IN_SHIFT)) == 0, "shift released");
}

static void test_changes(void) {
    static uint32_t back[UI_W * UI_H], front[UI_W * UI_H];
    memset(back, 0, sizeof back);
    memset(front, 0, sizeof front);
    back[2 * UI_W + 4] = 1;
    back[3 * UI_W + 10] = 1;
    int rects[UI_H][4];
    int n = ui_changes(back, front, rects);
    expect(n == 1, "one dirty band");
    expect(rects[0][0] == 4 && rects[0][1] == 2 && rects[0][2] == 11 && rects[0][3] == 4, "dirty rect");
    expect(front[3 * UI_W + 10] == 1 && front[UI_W] == 0, "dirty copy");
}

static void test_clear(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct input in = blank();
    expect(clear_frame(&u, &in, 0) == 0, "clear runs");
    expect(px(&u, 0, 0) == 0x1a2744 && px(&u, UI_W - 1, UI_H - 1) == 0x1a2744, "clear color");
    int text = 0;
    for (int y = 108; y < 120 && !text; y++)
        for (int x = 24; x < 24 + 6 * 5; x++)
            if (px(&u, x, y) == UI_TEXT) text = 1;
    expect(text, "clear text");
    in.encoder_press = 1;
    expect(clear_frame(&u, &in, 0) == 0, "encoder alone does not quit");
    in.shift = 1;
    expect(clear_frame(&u, &in, 0) == 1, "shift menu quits");
    struct input pad = blank();
    pad.shift = 1;
    pad.pad_hit[0] = 80;
    expect(clear_frame(&u, &pad, 0) == 1, "pad 1 quits");
}

static void test_boxes(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct input in = blank();
    boxes_frame(&u, &in, 0);
    expect(px(&u, 16, 16) == 0xf0f0f0, "box border");
    expect(px(&u, 20, 20) == 0x2244aa, "box fill");
    expect(px(&u, 140, 30) == 0xcc5533, "filled box");
    expect(px(&u, 40, 120) == 0x44cc66, "open box border");
    expect(px(&u, 50, 140) == UI_BG, "open box inside");
}

static void test_buttons(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct buttons_state s = {0};
    struct input in = blank();
    buttons_frame(&u, &in, &s);
    expect(px(&u, 12, 48) == UI_HL, "first button highlight");
    expect(px(&u, 108, 48) == 0x505868, "second button border");
    in.encoder = 1;
    buttons_frame(&u, &in, &s);
    expect(px(&u, 108, 48) == UI_HL, "moved button highlight");
}

static void test_highlight(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct highlight_state s = {0};
    struct input in = blank();
    highlight_frame(&u, &in, &s);
    expect(px(&u, 21, 40) == 0xff44aa, "highlight outline");
    expect(px(&u, 26, 42) == 0x303840, "box under highlight");
    in.encoder = 1;
    highlight_frame(&u, &in, &s);
    expect(px(&u, 21, 40) == UI_BG, "old outline cleared");
    expect(px(&u, 93, 40) == 0xff44aa, "next outline");
    expect(px(&u, 100, 50) == 0x204060, "second box color");
}

static void test_soft_keys(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct soft_state s = {0};
    struct input in = blank();
    soft_keys_frame(&u, &in, &s);
    expect(px(&u, 12, 12) == UI_HL, "active soft key");
    in.down = 1u << IN_B2;
    soft_keys_frame(&u, &in, &s);
    expect(px(&u, 114, 12) == UI_HL, "second soft key");
    expect(px(&u, 12, 12) == UI_BOX, "first soft key idle");

    const char *labels[3] = {"A", "B", "C"};
    struct ui_soft_style hold = {
        .fill = UI_BOX, .text = UI_TEXT, .active_text = UI_BG, .highlight = UI_HL, .gap = 4,
        .feedback = UI_SOFT_HOLD,
    };
    struct ui_soft_style flash = hold;
    flash.feedback = UI_SOFT_FLASH;
    flash.flash_ms = 100;
    struct ui_soft_state ks = {0};
    int active = 0;
    struct input key = blank();
    u.now_ms = 1000;
    key.down = 1u << IN_B3;
    key.held = 1u << IN_B3;
    ui_clear(&u, UI_BG);
    ui_soft_keys(&u, 8, 8, 304, 32, labels, &active, hold, &key, 0);
    expect(px(&u, 220, 12) == UI_HL, "held key stays lit");
    key.down = 0;
    ui_clear(&u, UI_BG);
    ui_soft_keys(&u, 8, 8, 304, 32, labels, &active, hold, &key, 0);
    expect(px(&u, 220, 12) == UI_HL, "held key follows the finger");
    key.held = 0;
    ui_clear(&u, UI_BG);
    ui_soft_keys(&u, 8, 8, 304, 32, labels, &active, hold, &key, 0);
    expect(px(&u, 220, 12) == UI_BOX, "released key goes dark");

    u.now_ms = 5000;
    key.down = 1u << IN_B3;
    key.held = 1u << IN_B3;
    ui_clear(&u, UI_BG);
    ui_soft_keys(&u, 8, 8, 304, 32, labels, &active, flash, &key, &ks);
    expect(px(&u, 220, 12) == UI_HL, "flash lights on the press");
    expect(u.wake_ms == 100, "flash asks for another frame");
    u.now_ms = 5050;
    key.down = 0;
    ui_clear(&u, UI_BG);
    u.wake_ms = 0;
    ui_soft_keys(&u, 8, 8, 304, 32, labels, &active, flash, &key, &ks);
    expect(px(&u, 220, 12) == UI_HL, "flash ignores a held finger");
    expect(u.wake_ms == 50, "flash keeps the remaining time");
    u.now_ms = 5100;
    ui_clear(&u, UI_BG);
    u.wake_ms = 0;
    ui_soft_keys(&u, 8, 8, 304, 32, labels, &active, flash, &key, &ks);
    expect(px(&u, 220, 12) == UI_BOX, "flash ends while the finger is down");
    expect(u.wake_ms == 0, "finished flash does not ask for a frame");
}

static void test_knobs(void) {
    static uint32_t buf[UI_W * UI_H];
    static uint32_t before[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct knobs_state s = {.norm = {0.10f, 0.40f, 0.70f}, .ks = {.last = {-1, -1, -1}}};
    struct input in = blank();
    knobs_frame(&u, &in, &s);
    expect(px(&u, 8, 156) == UI_HL, "focused knob border");
    memcpy(before, buf, sizeof before);
    in.down = 1u << IN_PLUS;
    knobs_frame(&u, &in, &s);
    expect(memcmp(before, buf, sizeof before) != 0, "knob value changed");
    expect(s.norm[0] > 0.10f && s.norm[0] < 0.12f, "knob stepped");

    float norm[3] = {0.5f, 0.5f, 0.5f};
    struct ui_knob_state ks = {.last = {-1, -1, -1}};
    struct ui_knob_style st = {.step = 0.01f, .gap = 4, .fill = UI_BG, .text = UI_TEXT, .name = UI_TEXT, .border = UI_BOX, .highlight = UI_HL, .border_px = 1, .highlight_px = 2};
    const char *names[3] = {"A", "B", "C"};
    struct input step = blank();
    step.knob_moved[0] = 1;
    step.knob[0] = 10;
    ui_knobs(&u, 0, 0, 300, 60, names, norm, 0, st, &step, &ks);
    expect(norm[0] == 0.5f, "pickup ignores the first sample");
    step = blank();
    step.knob_moved[0] = 1;
    step.knob[0] = 20;
    ui_knobs(&u, 0, 0, 300, 60, names, norm, 0, st, &step, &ks);
    expect(norm[0] == 0.5f, "pickup waits to cross");
    step.knob[0] = 80;
    ui_knobs(&u, 0, 0, 300, 60, names, norm, 0, st, &step, &ks);
    expect(norm[0] > 0.6f && norm[0] < 0.65f, "pickup follows after the cross");
}

static void test_list(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct list_state s = {.chosen = "Alpha"};
    struct input in = blank();
    list_frame(&u, &in, &s);
    expect(px(&u, 16, 40) == UI_HL, "list cursor outline");
    expect(px(&u, 20, 44) == 0x2a2418, "list cursor fill");
    in.encoder = 1;
    list_frame(&u, &in, &s);
    expect(px(&u, 16, 40) == UI_BG, "old list row");
    expect(px(&u, 16, 60) == UI_HL, "next list row");

    struct list_state held_state = {.chosen = "Alpha"};
    struct input held = blank();
    held.shift = 1;
    held.encoder = 1;
    list_frame(&u, &held, &held_state);
    expect(held_state.ls.cursor == 0, "open menu keeps the list");
    expect(px(&u, 16, 40) == UI_HL, "list row stays put");

    const char *long_rows[] = {"a", "b", "c", "d", "e", "f", "g", "h"};
    struct ui_list_style st = {
        .text = UI_TEXT, .highlight = UI_HL, .row_h = 20, .highlight_px = 2,
        .current_fill_set = 1, .current_fill = 0x2a2418,
    };
    struct ui_list_state ls = {0};
    struct input wheel = blank();
    ui_clear(&u, UI_BG);
    ui_list(&u, 10, 10, 100, 60, long_rows, 8, st, &wheel, &ls);
    expect(px(&u, 107, 11) == UI_HL, "scrollbar thumb starts at the top");
    expect(px(&u, 107, 68) != UI_HL && px(&u, 107, 68) != UI_BG, "scrollbar track shows the rest");
    expect(px(&u, 105, 12) == UI_BG, "scrollbar leaves a gap beside the rows");
    wheel.encoder = 7;
    ui_clear(&u, UI_BG);
    ui_list(&u, 10, 10, 100, 60, long_rows, 8, st, &wheel, &ls);
    expect(ls.cursor == 7, "scrollbar follows the last row");
    expect(px(&u, 107, 68) == UI_HL, "scrollbar thumb ends at the bottom");
    expect(px(&u, 107, 11) != UI_HL, "scrollbar top is track at the end");
}

static void test_menu(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    const struct ui_menu_item items[] = {
        {.label = "One", .id = 10, .pad = -1, .key = -1},
        {.label = "Two", .id = 20, .pad = -1, .key = -1},
        {.label = "Three", .id = 30, .pad = 5, .key = IN_B1},
    };
    struct ui_menu menu = {.title = "Menu", .hold = 1u << IN_SHIFT, .items = items, .n = 3};
    struct ui_menu_style st = ui_menu_style_default();
    struct input in = blank();
    ui_clear(&u, UI_BG);
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == -1, "menu stays closed");
    expect(px(&u, 54, 74) == UI_BG, "closed menu draws nothing");

    in.held = 1u << IN_SHIFT;
    expect(ui_menu_open(&menu, &in), "shift alone opens this menu");
    menu.hold = (1u << IN_SHIFT) | (1u << IN_ERASE);
    expect(!ui_menu_open(&menu, &in), "combination waits for every key");
    in.held |= 1u << IN_ERASE;
    expect(ui_menu_open(&menu, &in), "combination opens");
    menu.hold = 1u << IN_SHIFT;
    in = blank();
    in.shift = 1;

    ui_clear(&u, UI_BG);
    u.nled = 0;
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == -1, "open menu waits for a choice");
    expect(px(&u, 54, 74) == 0x2a2418, "menu row");
    expect(u.nled == 3 && u.led[0].index == 0, "menu lights bound pads");

    in.encoder = 1;
    ui_clear(&u, UI_BG);
    u.nled = 0;
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == -1, "wheel only moves");
    expect(menu.cursor == 1, "wheel scrolls");
    expect(px(&u, 54, 74) == 0x18181e, "old menu row");
    expect(px(&u, 54, 98) == 0x2a2418, "next menu row");

    in.encoder = 0;
    in.pad_hit[1] = 60;
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == 20, "pad number chooses");
    expect(menu.cursor == 1, "pad moves the cursor");

    in.pad_hit[1] = -1;
    in.pad_hit[5] = 40;
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == 30, "explicit pad chooses");

    in.pad_hit[5] = -1;
    in.down = 1u << IN_B1;
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == 30, "item key chooses");

    const struct ui_menu_item swapped[] = {{.label = "Go", .id = 7, .pad = 3, .key = -1}};
    menu.items = swapped;
    menu.n = 1;
    in.down = 0;
    in.pad_hit[3] = 90;
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == 7, "menu items can be replaced");

    in.shift = 0;
    in.held = 0;
    in.pad_hit[3] = -1;
    u.nled = 0;
    ui_clear(&u, UI_BG);
    expect(ui_menu(&u, 48, 44, 224, 152, &menu, st, &in) == -1, "release closes");
    expect(px(&u, 54, 74) == UI_BG, "closed menu is gone");
    expect(u.nled >= 1 && u.led[0].r == 0 && u.led[0].g == 0 && u.led[0].b == 0, "menu pads go dark");
}

static void test_pads(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct pads_state s = {0};
    struct input in = blank();
    in.pad_hit[0] = 127;
    in.pad_down[0] = 1;
    pads_frame(&u, &in, &s);
    uint32_t c = px(&u, 74, 199);
    expect((c >> 16) > 200 && (c & 255) < 30, "lit pad cell");
    expect(u.nled >= 1 && u.led[0].index == 0 && u.led[0].r > 60, "pad led queued");
}

static void test_anim(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct anim_state s = {.x = 24, .dir = 4};
    struct input in = blank();
    int x0 = s.x;
    expect(anim_frame(&u, &in, &s) == 0, "anim runs");
    expect(s.x == x0 + 4, "block stepped right");
    expect(px(&u, x0 + 2, 110) == UI_HL, "block is on the track");
    expect(u.wake_ms == 16, "anim asks for the next frame");
    int drawn = x0;
    for (int i = 0; i < 6; i++) {
        drawn = s.x;
        anim_frame(&u, &in, &s);
    }
    expect(px(&u, x0 + 2, 110) != UI_HL, "old block is gone");
    expect(px(&u, drawn + 2, 110) == UI_HL, "block drew at its new place");

    s.x = 276;
    s.dir = 4;
    anim_frame(&u, &in, &s);
    expect(s.dir < 0 && s.x < 276, "block bounces at the end of the track");

    in.encoder_press = 1;
    expect(anim_frame(&u, &in, &s) == 0, "encoder alone does not quit");
    in.shift = 1;
    expect(anim_frame(&u, &in, &s) == 1, "shift menu quits");
    struct input pad = blank();
    pad.shift = 1;
    pad.pad_hit[0] = 80;
    expect(anim_frame(&u, &pad, &s) == 1, "pad 1 quits");
}

int main(void) {
    test_parser();
    test_changes();
    test_clear();
    test_boxes();
    test_buttons();
    test_highlight();
    test_soft_keys();
    test_knobs();
    test_list();
    test_menu();
    test_pads();
    test_anim();
    if (failed) {
        fprintf(stderr, "%d check(s) failed\n", failed);
        return 1;
    }
    puts("ok");
    return 0;
}
