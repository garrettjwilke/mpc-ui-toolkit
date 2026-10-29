// One list row that opens USB SD card access, then returns here when it finishes.
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"

struct usb_sd_demo_state {
    int open;
    struct ui_usb_sd usb;
    struct ui_list_state ls;
};

int usb_sd_frame(struct ui *u, const struct input *in, void *state) {
    struct usb_sd_demo_state *s = state;
    example_menu_prepare();
    int menu_open = in && ui_menu_open(&example_menu, in);
    const struct input *use = menu_open ? 0 : in;
    struct ui_usb_sd_style st = ui_usb_sd_style_default();

    if (s->open) {
        int rc = ui_usb_sd(u, &s->usb, st, use);
        if (rc == UI_USB_SD_DONE || rc == UI_USB_SD_FAILED) s->open = 0;
    }

    if (!s->open) {
        ui_clear(u, UI_BG);
        ui_text(u, 12, 40, "USB SD card", UI_HL, 1);
        ui_text(u, 12, 56, UI_USB_SD_HINT, 0x9aa0a6, 1);
        const char *rows[] = {UI_USB_SD_LABEL};
        struct ui_list_style rows_st = {
            .text = UI_TEXT, .highlight = UI_HL, .row_h = 20, .highlight_px = 2,
            .current_fill_set = 1, .current_fill = 0x2a2418,
        };
        int hit = ui_list(u, 8, 76, 304, 156, rows, 1, rows_st, use, &s->ls);
        if (hit == 0) {
            ui_usb_sd_init(&s->usb);
            s->open = 1;
            ui_usb_sd(u, &s->usb, st, 0);
        }
    }

    if (example_menu_draw(u, in)) {
        if (ui_usb_sd_is_active(&s->usb)) ui_usb_sd_stop(&s->usb);
        return 1;
    }
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct usb_sd_demo_state s = {0};
    return app_run(usb_sd_frame, &s);
}
#endif
