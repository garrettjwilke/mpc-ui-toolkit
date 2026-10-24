# MPC Sample UI toolkit

A small C library for drawing on the Akai MPC Sample. The panel is always 320×240. Colors are `uint32_t` values, `0xRRGGBB`. Position and size are pixels, origin at the top left. Nothing is drawn unless the call asks for it.

`zig cc` in the Makefile is only the cross-compiler, the same way the firmware makefile uses it. The source is C.

## Draw

```c
ui_clear(ui, 0x1a2744);
ui_box(ui, x, y, w, h, (struct ui_paint){
    .fill_set = 1, .fill = 0x2244aa,
    .border_set = 1, .border = 0xf0f0f0, .border_px = 3,
});
ui_text(ui, x, y, "clear", 0xe8e8e8, 1);          /* scale 1 is 6x12 */
ui_text(ui, x, y, "Two", 0xe8e8e8, 2);            /* scale 2 is 8x16 at 2x */
ui_highlight(ui, x, y, w, h, 0xff44aa, 3);

struct ui_button st = {
    .fill = 0x303840, .text = 0xe8e8e8,
    .border_set = 1, .border = 0x505868, .border_px = 1,
    .highlighted = focused, .highlight = 0xffb000, .highlight_px = 3,
};
if (ui_button(ui, x, y, w, h, "Off", st, in)) { /* encoder press on the highlighted button */ }
```

`fill_set` or `border_set` at 0 skips that part. `ui_highlight` draws an outline on top of what is already there.

Optional starting colors: `UI_BG` `0x101014`, `UI_TEXT` `0xe8e8e8`, `UI_HL` `0xffb000`, `UI_BOX` `0x303840`.

## Widgets

Each widget stays inside the rectangle you pass.

- `ui_soft_keys` splits the rect into three keys for B1, B2, and B3. These buttons have no LEDs. `feedback` chooses the light: `UI_SOFT_STICKY` (the default) keeps the last press lit, `UI_SOFT_HOLD` stays lit while the key is down, and `UI_SOFT_FLASH` lights for `flash_ms` after the press and then goes dark even if the finger stays. A flash sets `ui.wake_ms` so the frame loop can draw the key going dark. `ui_feedback` is the same rule for a control an app draws itself.
- `ui_knobs` draws three value boxes for K1, K2, and K3. You own the three `float` values (0..1) and the text callback. The encoder moves focus. Plus and Minus step by `step`. A knob does not jump: it follows only after it crosses the current value. `struct ui_knob_state` `.last` starts at -1.
- `ui_list` scrolls rows. `row_h` is the row size. The encoder moves, encoder press returns that row, and the current row gets an outline plus an optional fill. When the rows do not fit, a bar on the right shows where that window sits in the list. The thumb uses the highlight color.
- `ui_pads` fits a 4×4 grid to the rect, pad 1 at the bottom left. A hit stores a color (blue when soft, red when hard). Pressure brightens the cell while it is held.
- `ui_menu` shows rows only while a button combination is held. Let go and the rows disappear. The wheel moves. Encoder press chooses the highlighted row. Each row can also name a pad and a key.

`hold` is every button that has to be down. `items`, `n`, and `hold` can be replaced on any frame. `ui_menu_prepare` fills those in once, and leaves a later change alone. `pad` `-1` uses the row number (pad 1 is the first row). A row's `key` is an `IN_*` button, or `-1`.

```c
static const struct ui_menu_item items[] = {
    {.label = "Quit", .id = UI_MENU_QUIT, .pad = -1, .key = -1},
    {.label = "Save", .id = 2, .pad = 4, .key = IN_B2},
};
struct ui_menu menu = {.title = "Menu", .items = items, .n = 2};
menu.hold = (1u << IN_SHIFT) | (1u << IN_ERASE); /* both keys, not only Shift */

struct ui_menu_style st = ui_menu_style_default();
if (!ui_menu_open(&menu, in)) {
    /* wheel and pads belong to the screen under the menu */
}
int id = ui_menu(ui, 48, 44, 224, 152, &menu, st, in);
if (id == UI_MENU_QUIT) return 1;
```

While the menu is open its pads light. Releasing the combination turns those pads off. `input.held` is the buttons that are still down, so a combination can span more than one frame.

## Examples

Each file is one screen and one idea. `make` builds nine static binaries in `bin/`: `clear`, `boxes`, `buttons`, `highlight`, `soft-keys`, `knobs`, `list`, `pads`, `anim`. Load one of those with mpc-boot-thingy. Object files and the host tools stay in `build/`. There is no installer.

`anim` slides a block along a track and asks for another frame in 16 ms. The panel is an SPI ST7796S. One update carries every changed row. A full 320×240 frame is about 50 ms on the wire, so a full-screen wipe stays near 20 frames per second. The block is small, and only the rows it leaves and enters are sent.

Every example uses the same menu: hold Shift and Quit appears on pad 1. The wheel moves the row, encoder press or that pad chooses it, and letting go of Shift hides the menu. Choosing Quit leaves the program. The process drops the screen and turns the pad lights off so the boot menu can draw again. Edit `examples/menu.h`, or give `ui_menu` a different `hold` mask and item list.

```sh
make test      # build/ui-test, host pixel checks
make preview   # build/shots/*.ppm
make           # bin/*, aarch64-linux-musl static binaries
```

The font is Terminus, generated into `src/font.h`. The license is `assets/fonts/OFL.txt`.
