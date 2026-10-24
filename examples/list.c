// A short list. The encoder moves. Encoder press shows the chosen name.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"

struct list_state {
    struct ui_list_state ls;
    const char *chosen;
};

int list_frame(struct ui *u, const struct input *in, void *state) {
    struct list_state *s = state;
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);
    ui_clear(u, UI_BG);
    const char *rows[] = {"Alpha", "Beta", "Gamma", "Delta"};
    struct ui_list_style st = {
        .text = UI_TEXT, .highlight = UI_HL, .row_h = 20, .highlight_px = 2,
        .current_fill_set = 1, .current_fill = 0x2a2418,
    };
    int hit = ui_list(u, 16, 40, 288, 140, rows, 4, st, open ? 0 : in, &s->ls);
    if (hit >= 0) s->chosen = rows[hit];
    ui_text(u, 16, 196, s->chosen ? s->chosen : rows[0], UI_TEXT, 1);
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct list_state s = {.chosen = "Alpha"};
    return app_run(list_frame, &s);
}
#endif
