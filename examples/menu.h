// The same menu for every example: hold Shift, then Quit.
// Change `items` or the hold mask here, or build another struct ui_menu in the app
// and pass that to ui_menu instead. Fields can be replaced on any frame.
#pragma once
#include "input.h"
#include "widgets.h"

static const struct ui_menu_item example_menu_items[] = {
    {.label = "Quit", .id = UI_MENU_QUIT, .pad = -1, .key = -1},
};

static struct ui_menu example_menu;

static void example_menu_prepare(void) {
    ui_menu_prepare(&example_menu, 1u << IN_SHIFT, "Menu", example_menu_items, 1);
}

// Draws the menu while Shift is held. 1 when Quit was chosen.
static int example_menu_draw(struct ui *u, const struct input *in) {
    example_menu_prepare();
    struct ui_menu_style st = ui_menu_style_default();
    return ui_menu(u, 48, 44, 224, 152, &example_menu, st, in) == UI_MENU_QUIT;
}
