// Three buttons. The encoder moves the highlight. Encoder press toggles the label.
#include "ui.h"
#include "input.h"
#include "menu.h"

struct buttons_state {
    int focus;
    int on[3];
};

int buttons_frame(struct ui *u, const struct input *in, void *state) {
    struct buttons_state *s = state;
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);
    if (!open && in->encoder) {
        int f = s->focus + in->encoder;
        f %= 3;
        if (f < 0) f += 3;
        s->focus = f;
    }
    ui_clear(u, UI_BG);
    const int x[3] = {12, 108, 210};
    const int w[3] = {80, 88, 96};
    const int h[3] = {32, 40, 48};
    const int y[3] = {48, 44, 40};
    const uint32_t fill[3] = {0x303840, 0x243044, 0x3a2a24};
    for (int i = 0; i < 3; i++) {
        struct ui_button st = {
            .fill = fill[i],
            .text = UI_TEXT,
            .border_set = 1,
            .border = 0x505868,
            .border_px = 1,
            .highlighted = i == s->focus,
            .highlight = UI_HL,
            .highlight_px = 3,
        };
        if (ui_button(u, x[i], y[i], w[i], h[i], s->on[i] ? "On" : "Off", st, open ? 0 : in)) s->on[i] = !s->on[i];
    }
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct buttons_state s = {0};
    return app_run(buttons_frame, &s);
}
#endif
