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
    int meter_left;  // 5-bit mask for Left output level meter LEDs (CC 83, right of screen)
    int meter_right; // 5-bit mask for Right output level meter LEDs (CC 84, right of screen)
    int track_led;   // 0..7 for active Track A..H LED (CC 57..64, left of screen), or -1 for off
    unsigned button_mask;          // bit i requests button_level[i] (IN_* index)
    unsigned char button_level[32]; // 0 off, 1 dim, 2 bright
    int64_t now_ms; // 0 reads the clock. A test can set a fake time.
    int wake_ms;    // widgets ask for another frame this soon. 0 means no request.
};

enum {
    UI_FEEDBACK_HOLD = 1,  // draw pressed while the control is held
    UI_FEEDBACK_FLASH = 2  // draw pressed for a short time after the press, then stop
};

// Asks the frame loop for another frame within ms. The soonest request wins.
void ui_wake(struct ui *u, int ms);

// 1 when a control should draw pressed.
// held is the physical state, edge is the press this frame.
// UI_FEEDBACK_HOLD follows the finger. UI_FEEDBACK_FLASH lights for flash_ms
// (120 when flash_ms is 0) after the edge and then goes dark even if the finger stays.
// *at stores the press time for a flash. NULL flashes for this call only.
int ui_feedback(struct ui *u, int held, int edge, int mode, int flash_ms, int64_t *at);

struct input;

void ui_clear(struct ui *u, uint32_t color);
void ui_box(struct ui *u, int x, int y, int w, int h, struct ui_paint paint);
void ui_text(struct ui *u, int x, int y, const char *s, uint32_t color, int scale);
// 1 when this button is the highlighted one and the encoder was pressed.
int ui_button(struct ui *u, int x, int y, int w, int h, const char *label, struct ui_button st, const struct input *in);
void ui_highlight(struct ui *u, int x, int y, int w, int h, uint32_t color, int thickness);
void ui_led(struct ui *u, int index, int r, int g, int b);
/* level: 0 off, 1 dim, 2 bright. button is an IN_* index. */
void ui_button_led(struct ui *u, int button, int level);
void ui_meter(struct ui *u, int left_mask, int right_mask);
void ui_track_led(struct ui *u, int track_index);

// Copies changed row-bands from back into front. rects are x0,y0,x1,y1 with x1,y1 exclusive.
// Returns how many rectangles changed. Both buffers are UI_W by UI_H.
int ui_changes(const uint32_t *back, uint32_t *front, int rects[][4]);
