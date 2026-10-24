// One row of soft keys. B1, B2, and B3 each change the line under the row.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"

struct soft_state {
    int active;
};

int soft_keys_frame(struct ui *u, const struct input *in, void *state) {
    struct soft_state *s = state;
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);
    ui_clear(u, UI_BG);
    const char *labels[3] = {"One", "Two", "Three"};
    struct ui_soft_style st = {.fill = UI_BOX, .text = UI_TEXT, .active_text = UI_BG, .highlight = UI_HL, .gap = 4};
    ui_soft_keys(u, 8, 8, 304, 32, labels, &s->active, st, open ? 0 : in);
    ui_text(u, 16, 64, labels[s->active], UI_TEXT, 2);
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct soft_state s = {0};
    return app_run(soft_keys_frame, &s);
}
#endif
