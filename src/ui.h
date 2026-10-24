// 320x240 software framebuffer. Colors are 0xRRGGBB. The caller chooses every color and size.
#pragma once
#include <stdint.h>

#define UI_W 320
#define UI_H 240

enum {
    UI_BG = 0x101014,
    UI_TEXT = 0xe8e8e8,
    UI_HL = 0xffb000,
    UI_BOX = 0x303840
};

struct ui_paint {
    int fill_set;
    uint32_t fill;
    int border_set;
    uint32_t border;
    int border_px;
};

struct ui_button {
    uint32_t fill;
    uint32_t text;
    int border_set;
    uint32_t border;
    int border_px;
    int highlighted;
    uint32_t highlight;
    int highlight_px;
};

struct ui_led {
    unsigned char index, r, g, b;
};

struct ui {
    uint32_t *px; // UI_W * UI_H, tightly packed
    struct ui_led led[16];
    int nled;
};

struct input;

void ui_clear(struct ui *u, uint32_t color);
void ui_box(struct ui *u, int x, int y, int w, int h, struct ui_paint paint);
void ui_text(struct ui *u, int x, int y, const char *s, uint32_t color, int scale);
// 1 when this button is the highlighted one and the encoder was pressed.
int ui_button(struct ui *u, int x, int y, int w, int h, const char *label, struct ui_button st, const struct input *in);
void ui_highlight(struct ui *u, int x, int y, int w, int h, uint32_t color, int thickness);
void ui_led(struct ui *u, int index, int r, int g, int b);

// Copies changed row-bands from back into front. rects are x0,y0,x1,y1 with x1,y1 exclusive.
// Returns how many rectangles changed. Both buffers are UI_W by UI_H.
int ui_changes(const uint32_t *back, uint32_t *front, int rects[][4]);
