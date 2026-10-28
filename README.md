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
- `ui_filebrowser` provides a complete, shared file navigation and management API for all MPC applications. Features can be mixed and matched via bitmask options (`UI_FB_OPT_COPY`, `UI_FB_OPT_PASTE`, `UI_FB_OPT_MOVE`, `UI_FB_OPT_RENAME`, `UI_FB_OPT_DELETE`, `UI_FB_OPT_MKDIR`, `UI_FB_OPT_HIDE_DOTFILES`, `UI_FB_OPT_TOGGLE_DOTFILES`, `UI_FB_OPT_DIRS_ONLY`, `UI_FB_OPT_SELECT_DIR`, `UI_FB_OPT_SHOW_SIZE`, `UI_FB_OPT_CONFIRM_DELETE`). Includes presets (`UI_FB_PRESET_READONLY`, `UI_FB_PRESET_DEFAULT`, `UI_FB_PRESET_FULL`), extension filtering (`.wav`, `.mpcmd`, etc.), custom filter callbacks, and an internal clipboard for file copy/move operations with automatic name collision handling. **Storage Sandboxing**: Users can never see or escape to root (`/`). Access is strictly constrained to external SD card (`/media/<SDCARD>`, auto-detected) and internal storage (`/data`). The top-level Storage Locations view lets the user choose between Internal and External storage; navigating up (`..` or B1) from a drive root returns to Storage Locations rather than escaping to the OS filesystem. Apps can optionally provide `initial_path` to start in a specific folder, or `forced_root` to lock browsing strictly to a single directory tree. Hardware soft keys B1, B2, and B3 provide [Up / Parent / Exit], [Actions Menu], and [Select / Open] (with Shift alternatives for New Folder, Paste, and Dotfiles Toggle). Encoder wheel navigates, encoder press enters directories or selects files, and modal dialogs integrate seamlessly with `ui_text_editor` for naming/renaming and confirmation alerts for deletion.

### Hardware UI Consistency Rules

Physical buttons `B1`, `B2`, and `B3` sit directly **above** the 320×240 display. Any widget or screen offering soft key actions for B1, B2, and B3 must place the soft keys bar at the **top** of the screen (typically `y = 0..8`, `h = 24..32`) so on-screen touchpoints visually and spatially align with the physical buttons above them.

Similarly, the MPC's 16 pads sit directly **below** the display. On-screen pad legends are drawn in the lower portion of the display with Pad 1 at the bottom-left and Pad 16 at the top-right, creating an exact 1:1 spatial map of the physical pads.

`hold` is every button that has to be down. `items`, `n`, and `hold` can be replaced on any frame. `ui_menu_prepare` fills those in once, and leaves a later change alone. `pad` `-1` uses the row number (pad 1 is the first row). A row's `key` is an `IN_*` button, or `-1`.

```c
enum { APP_SAVE = 1, APP_QUIT = 2 };

static const struct ui_menu_item items[] = {
    {.label = "Save", .id = APP_SAVE, .pad = 4, .key = IN_B2},
    {.label = "Quit", .id = APP_QUIT, .pad = -1, .key = -1},
};
struct ui_menu menu = {.title = "Menu", .items = items, .n = 2};
menu.hold = (1u << IN_SHIFT) | (1u << IN_ERASE); /* this app chooses the buttons */

struct ui_menu_style st = ui_menu_style_default();
if (!ui_menu_open(&menu, in)) {
    /* wheel and pads belong to the screen under the menu */
}
int id = ui_menu(ui, 48, 44, 224, 152, &menu, st, in);
if (id == APP_SAVE) { /* the app saves */ }
if (id == APP_QUIT) ui_quit(ui);
```

`ui_quit` asks `app_run` to exit after this frame. The app calls it when its own UI decides to leave. Nothing in the toolkit binds that call to a button or a chord. A frame that returns non-zero also exits.

While the menu is open its pads light. Releasing the combination turns those pads off. `input.held` is the buttons that are still down, so a combination can span more than one frame. Row ids belong to the app. `-1` means nothing was chosen.

## Examples

Each file is one screen and one idea. `make` builds thirteen static binaries in `bin/`: `clear`, `boxes`, `buttons`, `highlight`, `soft-keys`, `knobs`, `list`, `pads`, `anim`, `sudoku`, `text-edit`, `diagnostics`, `filebrowser`. Load one of those with mpc-boot-thingy. Object files and the host tools stay in `build/`. There is no installer.

`filebrowser` provides an interactive, full-featured file manager showcasing directory navigation, file copy/cut/paste, renaming, deletion confirmation, folder creation, and hidden dotfiles toggle.

`diagnostics` provides a comprehensive hardware and input testing suite. It visualizes and tests all 24 front-panel buttons (illuminating corresponding hardware LEDs on press), all 16 RGB velocity and aftertouch pads (with real-time RGB pressure feedback), K1–K3 knobs, fader, main volume knob, rotary encoder wheel rotation and click, track select LEDs A–H (cycled with Minus/Plus buttons), hardware VU level meters (driven by fader position), and displays a live 4-line event monitor showing raw MIDI note numbers and values.

`text-edit` demonstrates interactive single-line text input for renaming items using pad multi-tap, cursor wheel navigation, Shift selection, and soft keys B1-B3.

`sudoku` is a game on `ui_grid` and `ui_pad_modes`. The top-left 3×3 of pads is the cluster. Hold pad 16 and tap it to pick a block, pad 12 to pick a cell inside that block, and pad 8 to enter 1–9. Hold pad 1 and tap the cluster to mark a possible number. Those marks sit under the pad legend. Pad 3 clears the marks on the selected cell, and pad 4 clears the digit. The middle top button undoes. Hold the restart pad, or hold the top-right New button, then press the middle button to confirm. A matching pair of entered digits turns red. A clue does not. Leaving uses the same menu as the other examples.

`anim` slides a block along a track and asks for another frame in 16 ms. The panel is an SPI ST7796S. One update carries every changed row. A full 320×240 frame is about 50 ms on the wire, so a full-screen wipe stays near 20 frames per second. The block is small, and only the rows it leaves and enters are sent.

Each example leaves from a menu it owns. `examples/menu.h` is that menu. Most of them show it while Shift is held. The filebrowser example shows it while Stop is held, because that screen already uses Shift. Resume is the first row. Choosing Quit calls `ui_quit`. The process then drops the screen and turns the pad lights off so the boot menu can draw again. Change the hold mask or the rows in that file, or build another `struct ui_menu` and call `ui_quit` for the row that should leave.

```sh
make test      # build/ui-test, host pixel checks
make preview   # build/shots/*.ppm
make           # bin/*, aarch64-linux-musl static binaries
```

The font is Terminus, generated into `src/font.h`. The license is `assets/fonts/OFL.txt`.
