// Widgets built from ui_box, ui_text, ui_button, and ui_highlight.
// Each one draws only inside the rectangle it is given.
#pragma once
#include "ui.h"

enum {
    UI_SOFT_STICKY = 0,             // the last press stays lit
    UI_SOFT_HOLD = UI_FEEDBACK_HOLD,   // lit while that key is down
    UI_SOFT_FLASH = UI_FEEDBACK_FLASH  // lit briefly after the press, then dark
};

struct ui_soft_style {
    uint32_t fill, text, active_text, highlight;
    int gap;
    int feedback; // UI_SOFT_*. 0 keeps the last press lit.
    int flash_ms; // UI_SOFT_FLASH length. 0 is 120 ms.
};

// Press times for UI_SOFT_FLASH. Zero means that key has not been pressed.
struct ui_soft_state {
    int64_t at[3];
};

// labels has 3 strings. *active is the highlighted key and is updated on B1/B2/B3
// while feedback is UI_SOFT_STICKY. HOLD and FLASH light each key on its own.
// ks may be NULL. FLASH uses it so the light survives later events until flash_ms.
// Returns 0, 1, or 2 when that key was pressed, otherwise -1.
int ui_soft_keys(struct ui *u, int x, int y, int w, int h, const char *labels[3], int *active,
                 struct ui_soft_style st, const struct input *in, struct ui_soft_state *ks);

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
// When the rows do not fit, a bar on the right shows the window's place in the list.
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

struct ui_grid_style {
    uint32_t fill, text, line, block_line, cursor, band;
    int gap;       // pixels between cells
    int block;     // heavier line every N cells. 0 draws none.
    int block_px;  // thickness of that line. 0 means 2 when block is set.
    int cursor_px; // outline thickness for the cell and its block. 0 means 1.
};

// Draws a cols-by-rows board inside the rect. labels and fills are row-major
// and may be NULL. A NULL label is an empty cell. fills NULL uses style fill.
// cursor outlines that cell, or -1. band_cell outlines the block around that
// cell when block is set, or -1. The block line is only the rule between groups.
void ui_grid(struct ui *u, int x, int y, int w, int h, int cols, int rows, const char **labels,
             const uint32_t *fills, int cursor, int band_cell, struct ui_grid_style st);

// One way a held pad retargets a cluster. pad is the hold. color is the cluster
// while this mode is armed. held is this pad's color while it is down.
// names has one caption per cluster slot, or is NULL.
struct ui_pad_mode {
    int pad;
    uint32_t color, held;
    const char *label;
    const char **names;
};

// A pad that reports a tap when no mode is armed.
struct ui_pad_tap {
    int pad;
    uint32_t color;
    const char *label;
};

struct ui_pad_mode_style {
    int gap, border_px;
    uint32_t border, idle, mod_idle, text;
    int selected; // cluster slot to paint while a mode is armed, or -1
    uint32_t selected_color;
};

struct ui_pad_hit {
    int mode; // armed mode, or -1
    int slot; // cluster index hit this frame, or -1
    int tap;  // tap index, or -1
};

// group lists physical pads in logical order. The first held mode in the list wins.
// A cluster hit counts only while a mode is armed. Taps count only when none is.
// A hit does not replace colors. Owned pads are sent with ui_led on every draw
// that has input. A held pad brightens. Pass NULL input to draw without LEDs or hits.
// Pads not listed stay dark and unlabeled, and their LEDs are left alone.
struct ui_pad_hit ui_pad_modes(struct ui *u, int x, int y, int w, int h, const int *group, int n,
                               const struct ui_pad_mode *modes, int nmodes, const struct ui_pad_tap *taps,
                               int ntaps, struct ui_pad_mode_style st, const struct input *in);

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

// Return status codes from ui_text_editor
enum {
    UI_TEXT_ACTIVE = 0,   // Still editing
    UI_TEXT_OK     = 1,   // User confirmed (Encoder press or B3)
    UI_TEXT_CANCEL = -1   // User cancelled (B1)
};

struct ui_text_editor_style {
    uint32_t fill;         // Editor background fill
    uint32_t text_box;     // Text input box fill
    uint32_t text;         // Default text color
    uint32_t title;        // Header title color
    uint32_t border;       // Outer & box border color
    uint32_t highlight;    // Selection highlight color
    uint32_t cursor;       // Cursor block color (dulled for legibility)
    uint32_t cursor_text;  // Text color on cursor
    uint32_t pad_fill;     // Inactive pad fill
    uint32_t pad_text;     // Pad text color
    uint32_t pad_active;   // Active/hit pad color
    int border_px;
};

struct ui_text_editor {
    char *buf;             // Target string buffer
    int max_len;           // Buffer capacity (including null terminator)
    int len;               // Current string length
    int cursor;            // Cursor position: 0 <= cursor <= len
    int sel_anchor;        // Selection anchor (-1 when no selection)
    int sel_start;         // Selection start index (-1 when none)
    int sel_end;           // Selection end index (exclusive, -1 when none)
    int last_pad;          // Last pad index pressed (0..15, or -1)
    int pad_cycle;         // Current cycle index on last_pad
    const char *title;     // Header title
    unsigned pad_lit;      // Mask of lit pad LEDs
};

// Initializes editor state with target buffer and capacity
void ui_text_editor_init(struct ui_text_editor *ed, char *buf, int max_len, const char *title);

// Default styling matching the toolkit aesthetic
struct ui_text_editor_style ui_text_editor_style_default(void);

// Programmatic editing & clipboard operations
void ui_text_editor_cut(struct ui_text_editor *ed);
void ui_text_editor_copy(struct ui_text_editor *ed);
void ui_text_editor_paste(struct ui_text_editor *ed);
void ui_text_editor_delete(struct ui_text_editor *ed);
void ui_text_editor_clear(struct ui_text_editor *ed);

// Draws the text editor within rect (x, y, w, h) and processes hardware inputs.
// Soft keys bar is placed at top of rect under physical B1, B2, B3.
// Returns UI_TEXT_ACTIVE, UI_TEXT_OK, or UI_TEXT_CANCEL.
int ui_text_editor(struct ui *u, int x, int y, int w, int h, struct ui_text_editor *ed,
                   struct ui_text_editor_style st, const struct input *in);

#include "filebrowser.h"
