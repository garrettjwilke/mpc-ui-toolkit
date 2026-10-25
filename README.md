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
- `ui_grid` draws a columns-by-rows board in the rect. You pass the labels and, if you want, a fill for each cell. `block` draws a heavier line every N cells, which is how a Sudoku board marks its 3×3s. `cursor` outlines one cell. `band_cell` outlines the block that contains that cell.
- `ui_pad_modes` retargets a cluster of pads while another pad is held. You list the cluster in logical order, the hold for each mode, and any tap pads. The first hold that is down wins. A cluster hit returns that slot only while a hold is down. A tap returns only when none is. A hit does not replace the color. Owned pads are sent to the LEDs on that frame, and a held pad brightens. `selected` paints one cluster slot in `selected_color` while a mode is armed.
- `ui_menu` shows rows only while a button combination is held. Let go and the rows disappear. The wheel moves. Encoder press chooses the highlighted row. Each row can also name a pad and a key.
- `ui_text_editor` provides a full-featured single-line text input modal/screen using the 16 MPC pads in a bottom-to-top layout matching the physical hardware pads (Pad 1 at bottom-left: `a-c` through Pad 16 at top-right: `DEL`). The encoder wheel and `-` / `+` buttons move the cursor left and right; holding Shift while moving highlights and selects text. Typing on a pad replaces or cycles the character under the cursor. Clicking the encoder button appends a new character space and advances the cursor to the right, letting the user type the next character. Holding Shift switches to uppercase letters and alternate symbols. Hardware soft keys B1, B2, and B3 provide Cancel, Space, and OK (or Cut, Copy, Paste while Shift is held). Soft button B3 confirms (`UI_TEXT_OK`), and B1 cancels (`UI_TEXT_CANCEL`).

### Hardware UI Consistency Rules

Physical buttons `B1`, `B2`, and `B3` sit directly **above** the 320×240 display. Any widget or screen offering soft key actions for B1, B2, and B3 must place the soft keys bar at the **top** of the screen (typically `y = 0..8`, `h = 24..32`) so on-screen touchpoints visually and spatially align with the physical buttons above them.

Similarly, the MPC's 16 pads sit directly **below** the display. On-screen pad legends are drawn in the lower portion of the display with Pad 1 at the bottom-left and Pad 16 at the top-right, creating an exact 1:1 spatial map of the physical pads.

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

Each file is one screen and one idea. `make` builds eleven static binaries in `bin/`: `clear`, `boxes`, `buttons`, `highlight`, `soft-keys`, `knobs`, `list`, `pads`, `anim`, `sudoku`, `text-edit`. Load one of those with mpc-boot-thingy. Object files and the host tools stay in `build/`. There is no installer.

`text-edit` demonstrates interactive single-line text input for renaming items using pad multi-tap, cursor wheel navigation, Shift selection, and soft keys B1-B3.

`sudoku` is a game on `ui_grid` and `ui_pad_modes`. The top-left 3×3 of pads is the cluster. Hold pad 16 and tap it to pick a block, pad 12 to pick a cell inside that block, and pad 8 to enter 1–9. Hold pad 1 and tap the cluster to mark a possible number. Those marks sit under the pad legend. Pad 3 clears the marks on the selected cell, and pad 4 clears the digit. The middle top button undoes. Hold the restart pad, or hold the top-right New button, then press the middle button to confirm. A matching pair of entered digits turns red. A clue does not. Hold Shift and Quit, as in the other examples.

`anim` slides a block along a track and asks for another frame in 16 ms. The panel is an SPI ST7796S. One update carries every changed row. A full 320×240 frame is about 50 ms on the wire, so a full-screen wipe stays near 20 frames per second. The block is small, and only the rows it leaves and enters are sent.

Every example uses the same menu: hold Shift and Quit appears on pad 1. The wheel moves the row, encoder press or that pad chooses it, and letting go of Shift hides the menu. Choosing Quit leaves the program. The process drops the screen and turns the pad lights off so the boot menu can draw again. Edit `examples/menu.h`, or give `ui_menu` a different `hold` mask and item list.

```sh
make test      # build/ui-test, host pixel checks
make preview   # build/shots/*.ppm
make           # bin/*, aarch64-linux-musl static binaries
```

The font is Terminus, generated into `src/font.h`. The license is `assets/fonts/OFL.txt`.
