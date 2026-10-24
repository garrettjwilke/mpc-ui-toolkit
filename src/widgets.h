// Widgets built from ui_box, ui_text, ui_button, and ui_highlight.
// Each one draws only inside the rectangle it is given.
#pragma once
#include "ui.h"

struct ui_soft_style {
    uint32_t fill, text, active_text, highlight;
    int gap;
};

// labels has 3 strings. *active is the highlighted key and is updated on B1/B2/B3.
// Returns 0, 1, or 2 when that key was pressed, otherwise -1.
int ui_soft_keys(struct ui *u, int x, int y, int w, int h, const char *labels[3], int *active,
                 struct ui_soft_style st, const struct input *in);

struct ui_knob_style {
    uint32_t fill, text, name, border, highlight;
    int border_px, highlight_px, gap;
    float step;
};

struct ui_knob_state {
    int focus; // 0..2
    int last[3]; // last knob position, -1 until the first message
    int engaged[3];
};

// format may be NULL, which prints the value as a percent. norms are 0..1 and belong to the app.
typedef void (*ui_format_fn)(float norm, char *buf, int n);
void ui_knobs(struct ui *u, int x, int y, int w, int h, const char *names[3], float norm[3], ui_format_fn format,
              struct ui_knob_style st, const struct input *in, struct ui_knob_state *ks);

struct ui_list_style {
    uint32_t text, highlight;
    int row_h, highlight_px;
    int current_fill_set;
    uint32_t current_fill;
};

struct ui_list_state {
    int cursor;
};

// Returns the activated row, or -1. rows may be empty.
int ui_list(struct ui *u, int x, int y, int w, int h, const char **rows, int n, struct ui_list_style st,
            const struct input *in, struct ui_list_state *ls);

struct ui_pad_style {
    int gap, border_px;
    uint32_t border;
};

// colors[16] is the resting color of each pad. Pad 1 is the bottom-left cell.
// A hit replaces that color. Pressure brightens the cell while the pad is held.
// Changed pads are queued with ui_led for the device loop.
void ui_pads(struct ui *u, int x, int y, int w, int h, uint32_t colors[16], struct ui_pad_style st,
             const struct input *in);

// Suggested id for a Quit row. ui_menu returns the item's own id, so any value other than -1 works.
enum { UI_MENU_QUIT = 1 };

struct ui_menu_item {
    const char *label;
    int id;  // returned when this row is chosen. -1 is "nothing chosen".
    int pad; // 0..15 selects that pad. -1 uses this row's index while it is under 16.
    int key; // IN_* chooses this row while the menu is open, or -1.
};

struct ui_menu {
    const char *title;
    unsigned hold; // open while every one of these buttons is held
    const struct ui_menu_item *items;
    int n;
    int cursor;
    int shown;         // 1 after a frame in which the menu was open
    unsigned pad_lit;  // pads lit for the menu on the previous open frame
};

struct ui_menu_style {
    uint32_t fill, text, title, highlight, border;
    int border_px, row_h, highlight_px;
    int current_fill_set;
    uint32_t current_fill;
    int light_pads;
};

// Fills a menu that does not have items yet. A later change to hold, items, or n is left alone.
void ui_menu_prepare(struct ui_menu *m, unsigned hold, const char *title, const struct ui_menu_item *items, int n);
struct ui_menu_style ui_menu_style_default(void);
int ui_menu_open(const struct ui_menu *m, const struct input *in);

// Draws the menu while its hold combination is down and draws nothing after it is released.
// The wheel moves the row. Encoder press chooses the highlighted row.
// A pad or key bound to a row chooses that row. Returns the item id, or -1.
int ui_menu(struct ui *u, int x, int y, int w, int h, struct ui_menu *m, struct ui_menu_style st,
            const struct input *in);
