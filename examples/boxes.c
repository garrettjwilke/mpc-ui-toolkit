// Several rectangles. Each one picks its own size, fill, and border.
#include "ui.h"
#include "input.h"
#include "menu.h"

int boxes_frame(struct ui *u, const struct input *in, void *state) {
    (void)state;
    ui_clear(u, UI_BG);
    ui_box(u, 16, 16, 100, 60, (struct ui_paint){.fill_set = 1, .fill = 0x2244aa, .border_set = 1, .border = 0xf0f0f0, .border_px = 3});
    ui_box(u, 140, 30, 50, 80, (struct ui_paint){.fill_set = 1, .fill = 0xcc5533});
    ui_box(u, 40, 120, 120, 40, (struct ui_paint){.border_set = 1, .border = 0x44cc66, .border_px = 2});
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) { return app_run(boxes_frame, 0); }
#endif
