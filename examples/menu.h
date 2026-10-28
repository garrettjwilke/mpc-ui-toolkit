// Shared example of an app-owned menu. The toolkit does not bind any control to leaving.
// Most examples show this while Shift is held. Set example_menu.hold before the first prepare to pick another.
// Resume is first, so confirming the highlighted row does not leave.
// Choosing Quit is this app calling ui_quit.
#pragma once
#include "ui.h"
#include "input.h"
#include "widgets.h"

enum { EXAMPLE_RESUME = 1, EXAMPLE_QUIT = 2 };

static const struct ui_menu_item example_menu_items[] = {
    {.label = "Resume", .id = EXAMPLE_RESUME, .pad = -1, .key = -1},
    {.label = "Quit", .id = EXAMPLE_QUIT, .pad = -1, .key = -1},
};

static struct ui_menu example_menu;

static void example_menu_prepare(void) {
    int n = (int)(sizeof example_menu_items / sizeof example_menu_items[0]);
    ui_menu_prepare(&example_menu, 1u << IN_SHIFT, "Menu", example_menu_items, n);
}

// Draws the menu while its hold buttons are down. 1 when this app has called ui_quit.
static int example_menu_draw(struct ui *u, const struct input *in) {
    example_menu_prepare();
    struct ui_menu_style st = ui_menu_style_default();
    int id = ui_menu(u, 48, 44, 224, 152, &example_menu, st, in);
    if (!example_menu.shown) example_menu.cursor = 0;
    if (id == EXAMPLE_QUIT) ui_quit(u);
    return u && u->quit;
}
