// Host checks. No device, no DRM. Build: make test
#include "input.h"
#include "ui.h"
#include "widgets.h"
#include "../examples/sudoku.h"
#include "../examples/diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
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
struct text_edit_state { char name[32]; struct ui_text_editor editor; int editing; const char *last_action; };
int text_edit_frame(struct ui *u, const struct input *in, void *state);

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
    expect(in.encoder_press, "encoder press");
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
    expect(!u.quit, "quit flag starts clear");
    ui_quit(0);
    expect(!u.quit, "null ui_quit is ignored");
    ui_quit(&u);
    expect(u.quit, "ui_quit requests exit");
    u.quit = 0;
    in.encoder_press = 1;
    expect(clear_frame(&u, &in, 0) == 0, "encoder alone does not quit");
    expect(!u.quit, "encoder does not request quit");
    in.shift = 1;
    expect(clear_frame(&u, &in, 0) == 0, "shift and encoder do not quit");
    expect(!u.quit, "no quit until the app asks");
    in.encoder_press = 0;
    in.encoder = 1;
    expect(clear_frame(&u, &in, 0) == 0, "moving the menu does not quit");
    in.encoder = 0;
    in.encoder_press = 1;
    expect(clear_frame(&u, &in, 0) == 1, "quit row calls ui_quit");
    expect(u.quit, "ui_quit flag is set");
    struct input pad = blank();
    pad.shift = 1;
    pad.pad_hit[0] = 80;
    u.quit = 0;
    expect(clear_frame(&u, &pad, 0) == 0, "first menu row does not quit");
    pad.pad_hit[0] = -1;
    pad.pad_hit[1] = 80;
    expect(clear_frame(&u, &pad, 0) == 1, "quit row pad calls ui_quit");
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

static void test_grid(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct ui_grid_style st = {
        .fill = 0x111111,
        .text = 0xffffff,
        .line = 0x222222,
        .block_line = 0xff0000,
        .cursor = 0x00ff00,
        .band = 0x0000ff,
        .gap = 2,
        .block = 3,
        .block_px = 2,
        .cursor_px = 2,
    };
    const char *labels[81] = {0};
    labels[4] = "5";
    uint32_t fills[81];
    for (int i = 0; i < 81; i++) fills[i] = 0x111111;
    fills[8] = 0xabcdee;
    ui_clear(&u, UI_BG);
    ui_grid(&u, 10, 10, 160, 160, 9, 9, labels, fills, 3, 0, st);
    expect(px(&u, 10, 10) == 0x0000ff, "block outline");
    expect(px(&u, 12, 12) == 0x111111, "cell inside the block");
    expect(px(&u, 26, 20) == 0x222222, "thin grid line");
    expect(px(&u, 62, 20) == 0xff0000, "heavy block line");
    expect(px(&u, 64, 10) == 0x00ff00, "cursor outline");
    expect(px(&u, 66, 12) == 0x111111, "cell inside the cursor");
    expect(px(&u, 156, 12) == 0xabcdee, "per-cell fill");
    int digit = 0;
    for (int y = 10; y < 26 && !digit; y++)
        for (int x = 82; x < 98; x++)
            if (px(&u, x, y) == 0xffffff) digit = 1;
    expect(digit, "cell label");

    st.block = 0;
    ui_clear(&u, UI_BG);
    ui_grid(&u, 10, 10, 160, 160, 9, 9, labels, fills, 3, 0, st);
    expect(px(&u, 10, 10) == 0x111111, "no block outline without a block size");
    expect(px(&u, 62, 20) == 0x222222, "no heavy line without a block size");
}

static void test_pad_modes(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    const int group[9] = {12, 13, 14, 8, 9, 10, 4, 5, 6};
    const char *names[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    struct ui_pad_mode modes[2] = {
        {.pad = 7, .color = 0x00aa00, .held = 0x00ff00, .label = "Nu", .names = names},
        {.pad = 15, .color = 0x0000aa, .held = 0x0000ff, .label = "Bl"},
    };
    struct ui_pad_tap taps[1] = {{.pad = 0, .color = 0xaaaaaa, .label = "Un"}};
    struct ui_pad_mode_style st = {
        .gap = 0,
        .border_px = 0,
        .idle = 0x111111,
        .mod_idle = 0x222222,
        .text = 0xffffff,
        .selected = 0,
        .selected_color = 0xffff00,
    };

    ui_clear(&u, UI_BG);
    u.nled = 0;
    struct ui_pad_hit hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, 0);
    expect(hit.mode == -1 && hit.slot == -1 && hit.tap == -1, "no input draws only");
    expect(u.nled == 0, "no input leaves leds alone");

    struct input in = blank();
    in.pad_hit[12] = 100;
    in.pad_down[12] = 1;
    ui_clear(&u, UI_BG);
    hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, &in);
    expect(hit.mode == -1 && hit.slot == -1 && hit.tap == -1, "cluster hit waits for a hold");
    expect(px(&u, 20, 0) == 0x111111, "idle cluster pad");
    expect(px(&u, 60, 40) == 0x222222, "resting modifier");
    expect(px(&u, 0, 60) == 0xaaaaaa, "tap pad");
    expect(u.nled == 12, "owned pads are sent");

    in = blank();
    in.pad_down[15] = 1;
    in.pad_hit[12] = 80;
    hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, &in);
    expect(hit.mode == 1 && hit.slot == 0 && hit.tap == -1, "held mode returns the cluster slot");

    in.pad_down[7] = 1;
    in.pad_down[15] = 1;
    in.pad_hit[12] = 80;
    in.pad_down[12] = 0;
    hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, &in);
    expect(hit.mode == 0 && hit.slot == 0 && hit.tap == -1, "first held mode wins");
    in.pad_hit[12] = -1;
    ui_clear(&u, UI_BG);
    hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, &in);
    expect(hit.mode == 0 && hit.slot == -1, "hold alone does not pick a slot");
    expect(px(&u, 0, 0) == 0xffff00, "selected cluster slot");
    expect(px(&u, 20, 0) == 0x00aa00, "armed mode colors the cluster");

    in = blank();
    in.pad_down[7] = 1;
    in.pad_hit[0] = 70;
    in.pad_down[0] = 1;
    hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, &in);
    expect(hit.mode == 0 && hit.slot == -1 && hit.tap == -1, "tap waits until the hold is released");

    in = blank();
    in.pad_hit[0] = 70;
    in.pad_down[0] = 1;
    hit = ui_pad_modes(&u, 0, 0, 80, 80, group, 9, modes, 2, taps, 1, st, &in);
    expect(hit.mode == -1 && hit.slot == -1 && hit.tap == 0, "tap fires on its own");
}

static void test_sudoku(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct sudoku_state s = {0};
    expect(sudoku_puzzles_ok(), "built-in puzzles");
    struct input in = blank();
    expect(sudoku_frame(&u, &in, &s) == 0, "sudoku runs");
    expect(s.ready && s.grid[0] == '5' && s.given[0] && s.grid[1] == '0' && !s.given[1], "first clue and hole");
    expect(px(&u, 4, 26) == UI_HL, "selected cell is outlined");

    in = blank();
    in.pad_down[7] = 1;
    in.pad_hit[12] = 60;
    s.cell = 0;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[0] == '5' && s.undo_n == 0, "a given stays put");

    in = blank();
    in.pad_down[7] = 1;
    in.pad_hit[12] = 60;
    s.cell = 1;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '1' && s.undo_n == 1, "digit lands in the hole");

    in = blank();
    in.down = 1u << IN_B2;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '0' && s.undo_n == 0, "undo restores the hole");

    in = blank();
    in.pad_down[7] = 1;
    in.pad_hit[12] = 60;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '1', "digit written again");

    in = blank();
    in.pad_hit[3] = 60;
    in.pad_down[3] = 1;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '0', "clear empties the cell");

    in = blank();
    in.down = 1u << IN_B2;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '1', "undo restores the digit");

    in = blank();
    in.pad_down[1] = 1;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '1', "restart waits for confirm");
    in.down = 1u << IN_B2;
    sudoku_frame(&u, &in, &s);
    expect(s.puzzle == 0 && s.grid[1] == '0' && s.given[0], "restart restores the clues");

    in = blank();
    in.down = 1u << IN_B2;
    sudoku_frame(&u, &in, &s);
    expect(s.grid[1] == '1', "undo restores the board from before restart");

    in = blank();
    in.held = 1u << IN_B3;
    in.down = 1u << IN_B3;
    sudoku_frame(&u, &in, &s);
    expect(s.puzzle == 0 && s.grid[1] == '1', "new waits for confirm");
    in.down = 1u << IN_B2;
    sudoku_frame(&u, &in, &s);
    expect(s.puzzle == 1 && s.grid[0] == '6', "new puzzle");

    in = blank();
    in.down = 1u << IN_B2;
    sudoku_frame(&u, &in, &s);
    expect(s.puzzle == 0 && s.grid[1] == '1', "undo restores the board from before new");

    struct sudoku_state notes = {0};
    in = blank();
    sudoku_frame(&u, &in, &notes);
    notes.cell = 2;
    in.pad_down[0] = 1;
    in.pad_hit[12] = 50;
    sudoku_frame(&u, &in, &notes);
    expect(notes.grid[2] == '0' && (notes.notes[2] & 1), "note marks a possible number");
    expect(px(&u, 200, 152) == 0x1e4a32, "note shows under the pads");
    in = blank();
    in.pad_hit[2] = 40;
    in.pad_down[2] = 1;
    sudoku_frame(&u, &in, &notes);
    expect(notes.notes[2] == 0, "clear notes removes the mark");

    struct sudoku_state clash = {0};
    in = blank();
    sudoku_frame(&u, &in, &clash);
    clash.cell = 1;
    in.pad_down[7] = 1;
    in.pad_hit[9] = 40;
    sudoku_frame(&u, &in, &clash);
    expect(clash.grid[1] == '5', "guess repeats the clue");
    expect(px(&u, 8, 32) != 0x6a2832 && px(&u, 29, 30) != 0x6a2832, "a clue is not marked wrong");
    clash.cell = 2;
    in = blank();
    in.pad_down[7] = 1;
    in.pad_hit[9] = 40;
    sudoku_frame(&u, &in, &clash);
    expect(px(&u, 29, 30) == 0x6a2832 && px(&u, 50, 30) == 0x6a2832, "two entered digits match");
    expect(px(&u, 8, 32) != 0x6a2832, "the clue stays unmarked");

    in = blank();
    in.shift = 1;
    in.encoder_press = 1;
    expect(sudoku_frame(&u, &in, &s) == 0, "shift and encoder do not quit");
    expect(!u.quit, "sudoku does not quit on that chord");
    in.encoder_press = 0;
    in.encoder = 1;
    expect(sudoku_frame(&u, &in, &s) == 0, "moving the sudoku menu does not quit");
    in.encoder = 0;
    in.encoder_press = 1;
    expect(sudoku_frame(&u, &in, &s) == 1, "sudoku menu quit calls ui_quit");
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
    expect(anim_frame(&u, &in, &s) == 0, "shift and encoder do not quit");
    in.encoder_press = 0;
    in.encoder = 1;
    expect(anim_frame(&u, &in, &s) == 0, "moving the anim menu does not quit");
    in.encoder = 0;
    in.encoder_press = 1;
    expect(anim_frame(&u, &in, &s) == 1, "anim menu quit calls ui_quit");
    struct input pad = blank();
    pad.shift = 1;
    pad.pad_hit[1] = 80;
    u.quit = 0;
    expect(anim_frame(&u, &pad, &s) == 1, "quit row pad calls ui_quit");
}

static void test_text_editor(void) {
    static uint32_t buf_px[UI_W * UI_H];
    struct ui u = {.px = buf_px};
    struct ui_text_editor_style st = ui_text_editor_style_default();

    char buf[32] = "";
    struct ui_text_editor ed;
    ui_text_editor_init(&ed, buf, sizeof buf, "TEST");
    expect(ed.len == 0, "init len 0");
    expect(ed.cursor == 0, "init cursor 0");
    expect(ed.sel_start == -1, "init sel_start -1");

    // 1. Pad 1 rollover: 'a' -> 'b' -> 'c' -> 'a'
    struct input in1 = blank();
    in1.pad_hit[0] = 100;
    expect(ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in1) == UI_TEXT_ACTIVE, "editor active");
    expect(strcmp(buf, "a") == 0, "pad 1 once 'a'");
    expect(ed.len == 1 && ed.cursor == 0 && ed.last_pad == 0 && ed.pad_cycle == 0, "pad 1 state 'a'");

    expect(ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in1) == UI_TEXT_ACTIVE, "editor active 2");
    expect(strcmp(buf, "b") == 0, "pad 1 twice 'b'");
    expect(ed.len == 1 && ed.pad_cycle == 1, "pad 1 state 'b'");

    expect(ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in1) == UI_TEXT_ACTIVE, "editor active 3");
    expect(strcmp(buf, "c") == 0, "pad 1 thrice 'c'");
    expect(ed.len == 1 && ed.pad_cycle == 2, "pad 1 state 'c'");

    expect(ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in1) == UI_TEXT_ACTIVE, "editor active 4");
    expect(strcmp(buf, "a") == 0, "pad 1 4th rollover 'a'");
    expect(ed.len == 1 && ed.pad_cycle == 0, "pad 1 state rollover 'a'");

    // 2. Shift Pad 1 uppercase: 'A' -> 'B' -> 'C'
    struct input in_shift = blank();
    in_shift.shift = 1;
    in_shift.pad_hit[0] = 100;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_shift);
    expect(strcmp(buf, "B") == 0, "shift pad 1 gives 'B'");

    // 3. Wheel right navigation and auto-append
    struct input in_right = blank();
    in_right.encoder = 1;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_right);
    expect(ed.cursor == 1, "cursor moves right to 1");

    // Hit Pad 2 (index 1) to append 'd'
    struct input in_pad2 = blank();
    in_pad2.pad_hit[1] = 100;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_pad2);
    expect(strcmp(buf, "Bd") == 0, "appends 'd' to form 'Bd'");
    expect(ed.len == 2, "len becomes 2");

    // 4. Cursor boundary checks
    struct input in_left3 = blank();
    in_left3.encoder = -3;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_left3);
    expect(ed.cursor == 0, "cursor clamped at 0");

    in_right.encoder = 5;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_right);
    expect(ed.cursor == 2, "cursor clamped at len 2");

    // 5. Selection with Shift + wheel
    ed.cursor = 0;
    struct input in_sel = blank();
    in_sel.shift = 1;
    in_sel.encoder = 2;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_sel);
    expect(ed.sel_start == 0 && ed.sel_end == 2, "selected range [0, 2)");

    // Moving without shift clears selection
    struct input in_step = blank();
    in_step.encoder = -1;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_step);
    expect(ed.sel_start == -1 && ed.sel_end == -1, "clears selection on unshifted move");

    // 6. Copy, Cut, Paste via Soft buttons
    // Select both characters again
    ed.cursor = 0;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_sel);

    // Shift + B2 = Copy
    struct input in_copy = blank();
    in_copy.shift = 1;
    in_copy.down = (1u << IN_B2);
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_copy);

    // Shift + B3 = Paste (overwriting selection)
    struct input in_paste = blank();
    in_paste.shift = 1;
    in_paste.down = (1u << IN_B3);
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_paste);
    expect(strcmp(buf, "Bd") == 0, "paste replaces selection with 'Bd'");

    // Paste again at end: "BdBd"
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_paste);
    expect(strcmp(buf, "BdBd") == 0, "paste appends 'Bd' to make 'BdBd'");
    expect(ed.len == 4, "len 4 after paste");

    // Select first 2 chars and Cut (Shift + B1)
    ed.cursor = 0;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_sel);
    struct input in_cut = blank();
    in_cut.shift = 1;
    in_cut.down = (1u << IN_B1);
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_cut);
    expect(strcmp(buf, "Bd") == 0, "cut leaves 'Bd'");
    expect(ed.len == 2, "len 2 after cut");

    // 7. Delete / Backspace (Pad 16 and ERASE)
    struct input in_erase = blank();
    in_erase.down = (1u << IN_ERASE);
    ed.cursor = 2; // at end of "Bd"
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_erase);
    expect(strcmp(buf, "B") == 0, "erase deletes 'd'");
    expect(ed.len == 1, "len 1 after erase");

    struct input in_del = blank();
    in_del.pad_hit[15] = 100; // Pad 16
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_del);
    expect(strcmp(buf, "") == 0, "pad 16 deletes 'B'");
    expect(ed.len == 0, "len 0 after delete");

    // 8. Space entry via Pad 15 and B2
    struct input in_spc_pad = blank();
    in_spc_pad.pad_hit[14] = 100;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_spc_pad);
    expect(strcmp(buf, " ") == 0, "pad 15 inserts space");

    struct input in_b2 = blank();
    in_b2.down = (1u << IN_B2);
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_b2);
    expect(strcmp(buf, "  ") == 0, "b2 inserts second space");

    // 9. Clear all via Shift + Pad 16
    struct input in_clr = blank();
    in_clr.shift = 1;
    in_clr.pad_hit[15] = 100;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_clr);
    expect(strcmp(buf, "") == 0, "shift pad 16 clears all text");
    expect(ed.len == 0, "len 0 after clear");

    // 10. Confirmation (B3), Cancel (B1), and Encoder Press (Append character space)
    struct input in_cancel = blank();
    in_cancel.down = (1u << IN_B1);
    expect(ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_cancel) == UI_TEXT_CANCEL, "B1 cancels");

    struct input in_ok = blank();
    in_ok.down = (1u << IN_B3);
    expect(ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_ok) == UI_TEXT_OK, "B3 confirms");

    // Encoder press appends a character space and advances cursor
    ed.cursor = 0;
    struct input in_pad8 = blank();
    in_pad8.pad_hit[7] = 100; // Pad 8 -> 'v'
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_pad8);
    expect(strcmp(buf, "v") == 0, "pad 8 types 'v'");
    expect(ed.len == 1 && ed.cursor == 0, "cursor at 0 on 'v'");

    struct input in_enc_press = blank();
    in_enc_press.encoder_press = 1;
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in_enc_press);
    expect(strcmp(buf, "v ") == 0, "encoder press appends character space");
    expect(ed.len == 2 && ed.cursor == 1, "cursor advanced to 1 on space");

    // Typing pad 1 now replaces that space with 'a'
    ui_text_editor(&u, 0, 0, UI_W, UI_H, &ed, st, &in1);
    expect(strcmp(buf, "va") == 0, "next pad replaces space with 'a'");
    expect(ed.len == 2 && ed.cursor == 1, "cursor remains at 1 on 'a'");

    // 11. Test text_edit_frame integration
    struct text_edit_state tes = {.editing = 1};
    strncpy(tes.name, "Track_1", sizeof tes.name - 1);
    ui_text_editor_init(&tes.editor, tes.name, sizeof tes.name, "RENAME");
    struct input blank_in = blank();
    expect(text_edit_frame(&u, &blank_in, &tes) == 0, "text edit frame active");
    expect(text_edit_frame(&u, &in_ok, &tes) == 0, "text edit frame confirms");
    expect(tes.editing == 0, "editing stopped after confirm");
    expect(strcmp(tes.last_action, "Saved") == 0, "last action saved");

    // 12. Shift while editing does not leave
    struct text_edit_state tes2 = {.editing = 1};
    strncpy(tes2.name, "Kick", sizeof tes2.name - 1);
    ui_text_editor_init(&tes2.editor, tes2.name, sizeof tes2.name, "RENAME");
    struct input shift_in = blank();
    shift_in.shift = 1;
    expect(text_edit_frame(&u, &shift_in, &tes2) == 0, "shift does not quit while editing");
    expect(tes2.editing == 1, "still editing when shift is pressed");
}

static void test_diagnostics(void) {
    static uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct diagnostics_state s = {0};
    struct input in = blank();

    // 1. Initial frame runs
    expect(diagnostics_frame(&u, &in, &s) == 0, "diagnostics runs");
    expect(s.total_events == 0, "initial event count 0");
    expect(u.track_led == 0, "initial track LED is A (0)");

    // 2. Encoder turning
    in.encoder = 3;
    diagnostics_frame(&u, &in, &s);
    expect(s.encoder_accum == 3, "encoder accum +3");
    expect(s.encoder_last_delta == 3, "encoder last delta +3");
    expect(s.total_events == 1, "encoder event logged");
    expect(strstr(s.event_log[0], "ENCODER: delta=+3") != NULL, "encoder log text");

    // 3. Button press (MUTE)
    in = blank();
    in.down = (1u << IN_MUTE);
    in.held = (1u << IN_MUTE);
    diagnostics_frame(&u, &in, &s);
    expect(s.total_events == 2, "button down event logged");
    expect(strstr(s.event_log[0], "BTN MUTE DOWN") != NULL, "mute button log text");
    expect((u.button_mask & (1u << IN_MUTE)) != 0, "mute led updated");
    expect(u.button_level[IN_MUTE] == 2, "mute led bright while held");

    // 4. Pad hit
    in = blank();
    in.pad_hit[3] = 110;
    in.pad_down[3] = 1;
    diagnostics_frame(&u, &in, &s);
    expect(s.pad_vel[3] == 110, "pad 4 velocity stored");
    expect(s.total_events == 3, "pad hit event logged");
    expect(strstr(s.event_log[0], "PAD 4 HIT: vel=110") != NULL, "pad hit log text");
    int pad_led_ok = 0;
    for (int i = 0; i < u.nled; i++) {
        if (u.led[i].index == 3 && u.led[i].r == 127) pad_led_ok = 1;
    }
    expect(pad_led_ok, "pad 4 rgb led lit");

    // 5. Track stepping
    in = blank();
    in.down = (1u << IN_PLUS);
    diagnostics_frame(&u, &in, &s);
    expect(s.selected_track == 1, "track advanced to 1 (B)");
    expect(u.track_led == 1, "track B led lit");

    // 6. Fader movement / Meter drive
    in = blank();
    in.fader_moved = 1;
    in.fader = 127;
    diagnostics_frame(&u, &in, &s);
    expect(s.test_meter_val == 127, "fader sets meter val");
    expect(u.meter_left == 31 && u.meter_right == 31, "meter full scale");

    in = blank();
    in.shift = 1;
    in.encoder_press = 1;
    expect(diagnostics_frame(&u, &in, &s) == 0, "shift and encoder do not quit diagnostics");
    in.encoder_press = 0;
    in.encoder = 1;
    expect(diagnostics_frame(&u, &in, &s) == 0, "moving the diagnostics menu does not quit");
    in.encoder = 0;
    in.encoder_press = 1;
    expect(diagnostics_frame(&u, &in, &s) == 1, "diagnostics menu quit calls ui_quit");
}

static void test_filebrowser(void) {
    // 1. Setup temporary sandbox directory with mock SD card and internal storage
    int ret = system("rm -rf build/test_fb && mkdir -p build/test_fb/media/MPC/drums build/test_fb/media/MPC/subfolder build/test_fb/media/MPC/.hidden_dir build/test_fb/data");
    expect(ret == 0, "create test_fb sandbox");

    FILE *f;
    f = fopen("build/test_fb/media/MPC/zebra.wav", "w"); if (f) { fputs("zebra", f); fclose(f); }
    f = fopen("build/test_fb/media/MPC/alpha.wav", "w"); if (f) { fputs("alpha data", f); fclose(f); }
    f = fopen("build/test_fb/media/MPC/beta.mpcmd", "w"); if (f) { fputs("project", f); fclose(f); }
    f = fopen("build/test_fb/media/MPC/notes.txt", "w"); if (f) { fputs("text file", f); fclose(f); }
    f = fopen("build/test_fb/media/MPC/.hidden.txt", "w"); if (f) { fputs("hidden", f); fclose(f); }

    // 2. Test top-level Storage Locations view (Internal & External SD Card only)
    struct ui_filebrowser fb;
    struct ui_filebrowser_config cfg = {
        .initial_path = NULL, // Start at Storage Locations
        .external_path = "build/test_fb/media/MPC",
        .internal_path = "build/test_fb/data",
        .title = "TEST BROWSER",
        .action_label = "LOAD",
        .options = UI_FB_PRESET_FULL,
    };
    ui_filebrowser_init(&fb, &cfg);

    expect(fb.cwd[0] == '\0', "starts at Storage Locations view");
    expect(fb.drive_count == 2, "2 storage locations detected");
    expect(fb.count == 2, "storage view lists 2 drives");
    expect(fb.entries[0].is_drive == 1, "entry 0 is drive");
    expect(fb.entries[1].is_drive == 1, "entry 1 is drive");

    // 3. Test Security Sandbox: root (/) and system directories are strictly forbidden
    expect(ui_filebrowser_is_allowed(&fb, "/") == 0, "root / is forbidden");
    expect(ui_filebrowser_is_allowed(&fb, "/bin") == 0, "/bin is forbidden");
    expect(ui_filebrowser_is_allowed(&fb, "/etc") == 0, "/etc is forbidden");
    expect(ui_filebrowser_is_allowed(&fb, "/proc") == 0, "/proc is forbidden");
    expect(ui_filebrowser_set_cwd(&fb, "/etc") == -1, "set_cwd to /etc rejected");
    expect(ui_filebrowser_is_allowed(&fb, "build/test_fb/media/MPC/drums") == 1, "subfolder in SD is allowed");
    expect(ui_filebrowser_is_allowed(&fb, "build/test_fb/data") == 1, "data folder is allowed");

    // 4. Enter SD Card and verify file listing & sorting & dotfiles
    ui_filebrowser_set_cwd(&fb, "build/test_fb/media/MPC");
    expect(strcmp(fb.cwd, "build/test_fb/media/MPC") == 0, "entered SD card");

    // ".." is present at drive root to return to Storage Locations
    expect(fb.entries[0].is_parent == 1, "entry 0 is .. (parent)");

    // Directories first, then files sorted alphabetically
    // Entries: .. [0], drums [1], subfolder [2], alpha.wav [3], beta.mpcmd [4], notes.txt [5], zebra.wav [6]
    expect(fb.entries[1].is_dir == 1 && strcmp(fb.entries[1].name, "drums") == 0, "first dir is drums");
    expect(fb.entries[2].is_dir == 1 && strcmp(fb.entries[2].name, "subfolder") == 0, "second dir is subfolder");
    expect(fb.entries[3].is_dir == 0 && strcmp(fb.entries[3].name, "alpha.wav") == 0, "file alpha.wav");
    expect(fb.entries[4].is_dir == 0 && strcmp(fb.entries[4].name, "beta.mpcmd") == 0, "file beta.mpcmd");
    expect(fb.entries[5].is_dir == 0 && strcmp(fb.entries[5].name, "notes.txt") == 0, "file notes.txt");
    expect(fb.entries[6].is_dir == 0 && strcmp(fb.entries[6].name, "zebra.wav") == 0, "file zebra.wav");
    expect(fb.count == 7, "count without dotfiles is 7 (including ..)");

    // 5. Toggle dotfiles
    fb.hide_dotfiles = 0;
    ui_filebrowser_refresh(&fb);
    int found_hidden_file = 0, found_hidden_dir = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, ".hidden.txt") == 0) found_hidden_file = 1;
        if (strcmp(fb.entries[i].name, ".hidden_dir") == 0) found_hidden_dir = 1;
    }
    expect(found_hidden_file, "found hidden file after toggle");
    expect(found_hidden_dir, "found hidden dir after toggle");

    // 6. Test extension filtering
    const char *exts[] = {".wav", ".mpcmd", NULL};
    fb.extensions = exts;
    fb.hide_dotfiles = 1;
    ui_filebrowser_refresh(&fb);
    int found_notes = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "notes.txt") == 0) found_notes = 1;
    }
    expect(!found_notes, "notes.txt filtered out by extension");
    fb.extensions = NULL; // reset

    // 7. Test Copy & Paste
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "alpha.wav") == 0) {
            fb.cursor = i;
            break;
        }
    }
    expect(ui_filebrowser_copy(&fb) == 0, "copy file");
    expect(fb.clip_op == UI_FB_CLIP_COPY, "clipboard op is copy");
    expect(strcmp(fb.clip_name, "alpha.wav") == 0, "clipboard name");

    // Paste in same directory -> creates alpha_copy.wav
    expect(ui_filebrowser_paste(&fb) == 0, "paste copy");
    int found_copy = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "alpha_copy.wav") == 0) {
            found_copy = 1;
            fb.cursor = i;
            break;
        }
    }
    expect(found_copy, "alpha_copy.wav created by paste");

    // 8. Test Cut (Move) & Paste
    expect(ui_filebrowser_cut(&fb) == 0, "cut file");
    expect(fb.clip_op == UI_FB_CLIP_CUT, "clipboard op is cut");

    // Navigate into drums/
    ui_filebrowser_set_cwd(&fb, "build/test_fb/media/MPC/drums");
    expect(ui_filebrowser_paste(&fb) == 0, "paste move into drums/");
    expect(fb.clip_op == UI_FB_CLIP_NONE, "cut cleared after paste");

    // Verify alpha_copy.wav is now in drums/
    int in_drums = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "alpha_copy.wav") == 0) {
            in_drums = 1;
            fb.cursor = i;
            break;
        }
    }
    expect(in_drums, "alpha_copy.wav moved into drums/");

    // Verify alpha_copy.wav is unlinked from parent
    ui_filebrowser_set_cwd(&fb, "build/test_fb/media/MPC");
    int in_parent = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "alpha_copy.wav") == 0) in_parent = 1;
    }
    expect(!in_parent, "alpha_copy.wav unlinked from source directory");

    // 9. Test Delete
    ui_filebrowser_set_cwd(&fb, "build/test_fb/media/MPC/drums");
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "alpha_copy.wav") == 0) {
            fb.cursor = i;
            break;
        }
    }
    expect(ui_filebrowser_delete(&fb) == 0, "delete file");
    in_drums = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "alpha_copy.wav") == 0) in_drums = 1;
    }
    expect(!in_drums, "file deleted from drums/");

    // 10. Test Mkdir
    char new_dir[256];
    snprintf(new_dir, sizeof new_dir, "build/test_fb/media/MPC/created_folder");
    expect(ui_filebrowser_make_dir(new_dir) == 0, "mkdir created_folder");
    ui_filebrowser_set_cwd(&fb, "build/test_fb/media/MPC");
    int found_new_dir = 0;
    for (int i = 0; i < fb.count; i++) {
        if (strcmp(fb.entries[i].name, "created_folder") == 0 && fb.entries[i].is_dir) found_new_dir = 1;
    }
    expect(found_new_dir, "new directory recognized in file listing");

    // 11. Test Navigation Back to Storage Locations View
    // At root of SD Card, pressing B1 (UP) or clicking .. must return to Storage Locations
    struct input in = blank();
    in.down = (1u << IN_B1); // Press UP (B1)
    uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct ui_filebrowser_style st = ui_filebrowser_style_default();
    ui_filebrowser(&u, 0, 0, UI_W, UI_H, &fb, st, &in);
    expect(fb.cwd[0] == '\0', "navigating UP from drive root returns to Storage Locations");
    expect(fb.count == 2, "shows 2 storage locations");

    // 12. Test Entering Internal Storage (/data) from Storage Locations
    fb.cursor = 1; // Internal (/data)
    in = blank();
    in.encoder_press = 1;
    ui_filebrowser(&u, 0, 0, UI_W, UI_H, &fb, st, &in);
    expect(strcmp(fb.cwd, "build/test_fb/data") == 0, "entered internal storage (/data)");

    // 13. Test B1 at Storage Locations view returns UI_FB_CANCEL
    fb.cwd[0] = '\0';
    ui_filebrowser_refresh(&fb);
    in = blank();
    in.down = (1u << IN_B1);
    int res = ui_filebrowser(&u, 0, 0, UI_W, UI_H, &fb, st, &in);
    expect(res == UI_FB_CANCEL, "B1 at Storage Locations returns CANCEL");

    // 14. Test Forced Root Lock
    struct ui_filebrowser forced_fb;
    struct ui_filebrowser_config forced_cfg = {
        .forced_root = "build/test_fb/media/MPC/drums",
    };
    ui_filebrowser_init(&forced_fb, &forced_cfg);
    expect(strcmp(forced_fb.cwd, "build/test_fb/media/MPC/drums") == 0, "forced root set as cwd");
    expect(forced_fb.entries[0].is_parent == 0, "no .. allowed at forced root");
    expect(ui_filebrowser_is_allowed(&forced_fb, "build/test_fb/media/MPC") == 0, "parent of forced root is forbidden");

    // Cleanup temp dir
    ret = system("rm -rf build/test_fb");
    (void)ret;
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
    test_grid();
    test_pad_modes();
    test_sudoku();
    test_anim();
    test_text_editor();
    test_diagnostics();
    test_filebrowser();
    if (failed) {
        fprintf(stderr, "%d check(s) failed\n", failed);
        return 1;
    }
    puts("ok");
    return 0;
}

