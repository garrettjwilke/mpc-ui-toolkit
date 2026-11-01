// Help pages compiled into this program.
// The wheel scrolls. Shift scrolls faster and shows pad combos. Shift and Erase open the menu.
// Closing the window stays on this screen. Press the encoder to open it again.
#include "ui.h"
#include "input.h"
#include "menu.h"
#include "help.h"

extern const struct ui_help_file help_pages[];
extern const int help_pages_count;

struct help_demo {
    struct ui_help help;
    int opened;
};

int help_frame(struct ui *u, const struct input *in, void *state) {
    struct help_demo *s = state;
    struct ui_help_style st;
    int open;
    if (!example_menu.hold) example_menu.hold = (1u << IN_SHIFT) | (1u << IN_ERASE);
    example_menu_prepare();
    open = ui_menu_open(&example_menu, in);
    ui_clear(u, UI_BG);
    if (!s->opened) {
        ui_help_open_files(&s->help, help_pages, help_pages_count, "index.md");
        s->opened = 1;
    }
    st = ui_help_style_default();
    if (s->help.closed) {
        ui_text(u, 16, 100, "Help closed", UI_TEXT, 1);
        ui_text(u, 16, 120, "Press the encoder to open it", UI_TEXT, 1);
        if (!open && in && in->encoder_press)
            ui_help_open_files(&s->help, help_pages, help_pages_count, "index.md");
    } else if (ui_help(u, 0, 0, UI_W, UI_H, &s->help, st, open ? 0 : in) == UI_HELP_CLOSED) {
        ui_clear(u, UI_BG);
        ui_text(u, 16, 100, "Help closed", UI_TEXT, 1);
        ui_text(u, 16, 120, "Press the encoder to open it", UI_TEXT, 1);
    }
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct help_demo s = {0};
    return app_run(help_frame, &s);
}
#endif
