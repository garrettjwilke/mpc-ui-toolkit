// Fills the panel with one color. Hold Shift, then choose Quit.
#include "ui.h"
#include "input.h"
#include "menu.h"

int clear_frame(struct ui *u, const struct input *in, void *state) {
    (void)state;
    ui_clear(u, 0x1a2744);
    ui_text(u, 24, 108, "clear", UI_TEXT, 1);
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) { return app_run(clear_frame, 0); }
#endif
