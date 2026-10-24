// Three values. K1-K3, the encoder, and Plus/Minus edit them.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"
#include <stdio.h>

struct knobs_state {
    float norm[3];
    struct ui_knob_state ks;
};

static void fmt_pct(float norm, char *buf, int n) {
    int pct = (int)(norm * 100.0f + 0.5f);
    snprintf(buf, (size_t)n, "%d", pct);
}

int knobs_frame(struct ui *u, const struct input *in, void *state) {
    struct knobs_state *s = state;
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);
    ui_clear(u, UI_BG);
    const char *names[3] = {"Level", "Pan", "Tune"};
    struct ui_knob_style st = {
        .fill = UI_BG, .text = UI_TEXT, .name = 0x9aa0a6, .border = UI_BOX, .highlight = UI_HL,
        .border_px = 1, .highlight_px = 3, .gap = 4, .step = 0.01f,
    };
    ui_knobs(u, 8, 156, 304, 72, names, s->norm, fmt_pct, st, open ? 0 : in, &s->ks);
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct knobs_state s = {.norm = {0.25f, 0.50f, 0.75f}, .ks = {.last = {-1, -1, -1}}};
    return app_run(knobs_frame, &s);
}
#endif
