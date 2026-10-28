// Standalone interactive file browser example for MPC Sample.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "filebrowser.h"
#include "menu.h"
#include <stdio.h>
#include <string.h>

struct filebrowser_demo_state {
    struct ui_filebrowser fb;
    char last_selected[UI_FB_MAX_PATH];
    int show_banner;
};

int filebrowser_frame(struct ui *u, const struct input *in, void *state) {
    struct filebrowser_demo_state *s = state;

    // Shift is the browser's modifier, so this app shows its menu while Stop is held.
    if (!example_menu.items) example_menu.hold = 1u << IN_STOP;
    example_menu_prepare();
    int open = in && ui_menu_open(&example_menu, in);

    struct ui_filebrowser_style st = ui_filebrowser_style_default();
    int res = ui_filebrowser(u, 0, 0, UI_W, UI_H, &s->fb, st, open ? 0 : in);

    if (res == UI_FB_SELECT) {
        snprintf(s->last_selected, sizeof s->last_selected, "%s", s->fb.selected_name);
        snprintf(s->fb.status_msg, sizeof s->fb.status_msg, "Selected: %s", s->fb.selected_name);
        s->fb.status_is_error = 0;
        s->show_banner = 1;
    } else if (res == UI_FB_CANCEL) {
        ui_quit(u);
    }

    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"

int main(void) {
    struct filebrowser_demo_state s = {0};

    struct ui_filebrowser_config cfg = {
        .initial_path = NULL, // Starts at Storage Locations (Internal / External SD)
        .title = "MPC FILE MANAGER",
        .action_label = "SELECT",
        .options = UI_FB_PRESET_FULL,
        .extensions = NULL,
    };

    ui_filebrowser_init(&s.fb, &cfg);
    return app_run(filebrowser_frame, &s);
}
#endif
