// A folder of Markdown pages drawn inside a rectangle the app chooses.
// Links open other pages in that folder. A dialogue is the same thing: the page
// is the line, and the links are the replies. Nothing here binds a button to help.
//
// Zero the struct before the first ui_help_open. ui_help_close frees the page.
#pragma once

#include "widgets.h"

#define UI_HELP_PATH 512
#define UI_HELP_STACK 16

enum {
    UI_HELP_ACTIVE = 0,
    UI_HELP_CLOSED = 1
};

// One file baked into the program. path is relative, like "index.md" or "shots/a.png".
struct ui_help_file {
    const char *path;
    const void *data;
    int size;
};

struct ui_help_style {
    uint32_t fill, text, heading, link, code_fill, code_text, border, highlight;
    const char *back;  // B1. NULL draws an empty key and does nothing.
    const char *home;  // B2. NULL draws an empty key and does nothing.
    const char *close; // B3. NULL leaves the window open. The app decides when to leave.
};

struct ui_help {
    char canon[UI_HELP_PATH];
    char start[UI_HELP_PATH];
    char page[UI_HELP_PATH];
    char stack_page[UI_HELP_STACK][UI_HELP_PATH];
    int stack_scroll[UI_HELP_STACK];
    int stack_link[UI_HELP_STACK];
    int nstack;
    int scroll;
    int link; // link last opened on this page, or -1
    int hint_pad; // first pad of a two-pad combo, or 0
    int closed;
    int ready;
    const struct ui_help_file *files;
    int nfiles;
    struct ui_soft_state keys;
    void *doc;
};

struct ui_help_style ui_help_style_default(void);

// 0 when the start page is open. -1 when the folder or the start page cannot be read.
// ui_help_open reads a directory. ui_help_open_files reads pages already linked into the program.
int ui_help_open(struct ui_help *h, const char *root, const char *start);
int ui_help_open_files(struct ui_help *h, const struct ui_help_file *files, int n, const char *start);
void ui_help_close(struct ui_help *h);

// Draws the window and handles the wheel, the pads, and B1 B2 B3.
// Soft keys sit at the top of the rectangle. The wheel scrolls the page.
// Shift scrolls faster, lights pads 1 through 6, and draws each pad combo on its link.
// The border uses that pad's color. The number is white. Up to six links use one pad each.
// More links use a two-pad combo from those same six, shortest hop first.
// Press the combo while Shift is held. The page stays where it was scrolled.
// Returns UI_HELP_CLOSED after the close key. Later calls draw nothing until open.
int ui_help(struct ui *u, int x, int y, int w, int h, struct ui_help *hp, struct ui_help_style st,
            const struct input *in);

// The page open on the last successful load, relative to the help folder. Empty if none.
const char *ui_help_page(const struct ui_help *h);
// Scroll in pixels from the last draw.
// The link is the one last opened on this page, or -1.
int ui_help_scroll(const struct ui_help *h);
int ui_help_link(const struct ui_help *h);
