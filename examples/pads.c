// A 4x4 pad grid. A hit lights that cell. The device loop sends the pad color.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"

struct pads_state {
    uint32_t color[16];
    int ready;
};

int pads_frame(struct ui *u, const struct input *in, void *state) {
    struct pads_state *s = state;
    int was = example_menu.shown;
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);
    if (!s->ready) {
        for (int i = 0; i < 16; i++) s->color[i] = 0x202028;
        s->ready = 1;
    }
    ui_clear(u, UI_BG);
    struct ui_pad_style st = {.gap = 4, .border_px = 1, .border = UI_BG};
    ui_pads(u, 48, 16, 224, 208, s->color, st, open ? 0 : in);
    if (example_menu_draw(u, in)) return 1;
    if (was && !example_menu.shown) {
        for (int i = 0; i < 16; i++)
            ui_led(u, i, (int)((s->color[i] >> 17) & 0x7f), (int)((s->color[i] >> 9) & 0x7f),
                   (int)((s->color[i] >> 1) & 0x7f));
    }
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct pads_state s = {0};
    return app_run(pads_frame, &s);
}
#endif
