// Interactive text editor example for MPC Sample.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"
#include <string.h>

struct text_edit_state {
    char name[32];
    struct ui_text_editor editor;
    int editing;
    const char *last_action;
};

int text_edit_frame(struct ui *u, const struct input *in, void *state) {
    struct text_edit_state *s = state;
    ui_clear(u, UI_BG);

    if (s->editing) {
        struct ui_text_editor_style st = ui_text_editor_style_default();
        int res = ui_text_editor(u, 0, 0, UI_W, UI_H, &s->editor, st, in);
        if (res == UI_TEXT_OK) {
            s->editing = 0;
            s->last_action = "Saved";
        } else if (res == UI_TEXT_CANCEL) {
            s->editing = 0;
            s->last_action = "Cancelled";
        }
        // Menu is disabled while editing so Shift can be used for text input & selection!
        return 0;
    }

    // Main screen when not currently editing
    example_menu_prepare();
    int open = ui_menu_open(&example_menu, in);

    // Soft keys bar at top of screen (under physical B1, B2, B3)
    const char *soft_labels[3] = {"B1: RENAME", "B2: CLEAR", "B3: DEFAULT"};
    int active = -1;
    struct ui_soft_style sk_st = {
        .fill = UI_BOX, .text = UI_TEXT, .active_text = UI_BG, .highlight = UI_HL, .gap = 4
    };
    int pressed = ui_soft_keys(u, 8, 8, 304, 26, soft_labels, &active, sk_st, open ? 0 : in, 0);

    if (!open && in) {
        if (pressed == 0 || in->encoder_press) {
            ui_text_editor_init(&s->editor, s->name, sizeof s->name, "RENAME SAMPLE");
            s->editing = 1;
        } else if (pressed == 1) {
            s->name[0] = '\0';
            s->last_action = "Cleared";
        } else if (pressed == 2) {
            strncpy(s->name, "Kick_808", sizeof s->name - 1);
            s->name[sizeof s->name - 1] = '\0';
            s->last_action = "Reset Default";
        }
    }

    ui_text(u, 16, 56, "SAMPLE NAME:", UI_HL, 1);
    ui_box(u, 16, 76, 288, 36, (struct ui_paint){
        .fill_set = 1, .fill = UI_BOX,
        .border_set = 1, .border = 0x505868, .border_px = 1
    });
    ui_text(u, 28, 86, s->name[0] ? s->name : "(empty)", s->name[0] ? 0xffffff : 0x8890a0, 2);

    ui_text(u, 16, 130, "STATUS:", 0x8890a0, 1);
    ui_text(u, 70, 130, s->last_action ? s->last_action : "Ready", UI_TEXT, 1);

    ui_text(u, 16, 160, "Press B1 or Click Encoder to edit.", 0xa0a8b8, 1);
    ui_text(u, 16, 180, "Hold Shift to open the menu.", 0x707888, 1);

    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct text_edit_state s = {
        .editing = 1,
        .last_action = "Ready"
    };
    strncpy(s.name, "Kick_808", sizeof s.name - 1);
    ui_text_editor_init(&s.editor, s.name, sizeof s.name, "RENAME SAMPLE");
    return app_run(text_edit_frame, &s);
}
#endif
