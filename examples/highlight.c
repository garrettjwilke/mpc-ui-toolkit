// Four boxes keep their own colors. The outline is the only marker.
#include "ui.h"
#include "input.h"
#include "menu.h"

struct highlight_state {
    int focus;
};

int highlight_frame(struct ui *u, const struct input *in, void *state) {
    struct highlight_state *s = state;
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);
    if (!open && in->encoder) {
        int f = s->focus + in->encoder;
        f %= 4;
        if (f < 0) f += 4;
        s->focus = f;
    }
    ui_clear(u, UI_BG);
    const uint32_t fill[4] = {0x303840, 0x204060, 0x403020, 0x204030};
    for (int i = 0; i < 4; i++) {
        int x = 24 + i * 72;
        ui_box(u, x, 40, 56, 40, (struct ui_paint){.fill_set = 1, .fill = fill[i]});
        if (i == s->focus) ui_highlight(u, x - 3, 37, 62, 46, 0xff44aa, 3);
    }
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct highlight_state s = {0};
    return app_run(highlight_frame, &s);
}
#endif
