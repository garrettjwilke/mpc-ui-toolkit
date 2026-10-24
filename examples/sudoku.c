// Sudoku. Hold a side pad and tap the top-left 3x3.
// Pad 16 picks a block, pad 12 a cell, pad 8 a digit, pad 1 a note.
// B2 undoes. Hold B3 or the restart pad, then press B2, to start over.
#include "sudoku.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"
#include <string.h>

enum {
    BOARD_X = 4,
    BOARD_Y = 26,
    BOARD_W = 188,
    BOARD_H = 188,
    PAD_X = 198,
    PAD_Y = 26,
    PAD_W = 118,
    PAD_H = 118,
    FILL_EMPTY = 0x18181e,
    FILL_GIVEN = 0x243044,
    FILL_ENTRY = 0x1c3324,
    FILL_BAD = 0x6a2832,
    FILL_NOTE = 0x1e4a32,
    BAR_X = 4,
    BAR_Y = 1,
    BAR_W = 312,
    BAR_H = 24,
    NOTE_X = 198,
    NOTE_Y = 150,
    NOTE_W = 118,
    NOTE_H = 84
};

// Top-left cluster matches the screen. Indexes are pads 13,14,15 / 9,10,11 / 5,6,7.
static const int cluster[9] = {12, 13, 14, 8, 9, 10, 4, 5, 6};

// Restart is first so a confirm hold does not also edit the board.
enum { MODE_RESTART, MODE_NOTE, MODE_NUM, MODE_CELL, MODE_BLOCK };
enum { TAP_CLEAR_NOTES, TAP_CLEAR };

static const struct {
    const char *givens;
    const char *solution;
} puzzles[] = {
    {"5...7...."
     "6..195..."
     ".98....6."
     "8...6...3"
     "4..8.3..1"
     "7...2...6"
     ".6....28."
     "...419..5"
     "....8..79",
     "534678912"
     "672195348"
     "198342567"
     "859761423"
     "426853791"
     "713924856"
     "961537284"
     "287419635"
     "345286179"},
    {"6.5.8..2."
     ".8..1.4.."
     "2..4..67."
     ".6..7...4"
     ".3.9.4.1."
     "8...3..6."
     ".7...83.."
     "..8.2..4."
     "4..3..28.",
     "645789123"
     "783216459"
     "219453678"
     "961872534"
     "537964812"
     "824135967"
     "172648395"
     "398521746"
     "456397281"},
    {".7.4..1.."
     "4...1..6."
     ".12..8..3"
     "..1.4.6.."
     "6..2.7..9"
     "..7.8.2.."
     "1..5..82."
     ".2..9..7."
     "..5..4.3.",
     "576432198"
     "438915762"
     "912768543"
     "251349687"
     "684257319"
     "397186254"
     "149573826"
     "823691475"
     "765824931"},
};

static int puzzle_count(void) { return (int)(sizeof puzzles / sizeof puzzles[0]); }

static int at(int block, int cell) {
    int br = block / 3, bc = block % 3;
    int cr = cell / 3, cc = cell % 3;
    return (br * 3 + cr) * 9 + bc * 3 + cc;
}

static int same_unit(int a, int b) {
    int ar = a / 9, ac = a % 9, br = b / 9, bc = b % 9;
    return ar == br || ac == bc || (ar / 3 == br / 3 && ac / 3 == bc / 3);
}

static int clash(const char *g, int i) {
    if (g[i] < '1' || g[i] > '9') return 0;
    for (int j = 0; j < 81; j++)
        if (j != i && g[j] == g[i] && same_unit(i, j)) return 1;
    return 0;
}

// Red marks two numbers the player typed. A clue is never marked, and a guess
// that only repeats a clue stays unmarked so the puzzle does not point at the answer.
static int player_clash(const struct sudoku_state *s, int i) {
    if (s->given[i] || s->grid[i] < '1' || s->grid[i] > '9') return 0;
    for (int j = 0; j < 81; j++) {
        if (j == i || s->given[j] || s->grid[j] != s->grid[i]) continue;
        if (same_unit(i, j)) return 1;
    }
    return 0;
}

static int solved(const char *g) {
    for (int i = 0; i < 81; i++)
        if (g[i] < '1' || g[i] > '9' || clash(g, i)) return 0;
    return 1;
}

static int len81(const char *s) {
    int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n == 81;
}

int sudoku_puzzles_ok(void) {
    for (int p = 0; p < puzzle_count(); p++) {
        const char *sol = puzzles[p].solution;
        const char *givens = puzzles[p].givens;
        if (!len81(sol) || !len81(givens) || !solved(sol)) return 0;
        int empty = 0;
        for (int i = 0; i < 81; i++) {
            char c = givens[i];
            if (c == '.' || c == '0') {
                empty = 1;
                continue;
            }
            if (c != sol[i]) return 0;
        }
        if (!empty) return 0;
    }
    return 1;
}

static void load_puzzle(struct sudoku_state *s, int n) {
    int count = puzzle_count();
    if (count < 1) return;
    if (n < 0) n = 0;
    n %= count;
    s->puzzle = n;
    const char *givens = puzzles[n].givens;
    const char *sol = puzzles[n].solution;
    for (int i = 0; i < 81; i++) {
        int keep = givens[i] >= '1' && givens[i] <= '9' && givens[i] == sol[i];
        s->given[i] = (char)(keep ? 1 : 0);
        s->grid[i] = keep ? givens[i] : '0';
        s->notes[i] = 0;
    }
}

static void push_undo(struct sudoku_state *s) {
    if (s->undo_n >= SUDOKU_UNDO) {
        memmove(&s->undo[0], &s->undo[1], sizeof s->undo[0] * (SUDOKU_UNDO - 1));
        s->undo_n = SUDOKU_UNDO - 1;
    }
    struct sudoku_snap *snap = &s->undo[s->undo_n++];
    memcpy(snap->grid, s->grid, 81);
    memcpy(snap->given, s->given, 81);
    memcpy(snap->notes, s->notes, sizeof snap->notes);
    snap->puzzle = s->puzzle;
    snap->block = s->block;
    snap->cell = s->cell;
}

static void pop_undo(struct sudoku_state *s) {
    if (s->undo_n <= 0) return;
    struct sudoku_snap *snap = &s->undo[--s->undo_n];
    memcpy(s->grid, snap->grid, 81);
    memcpy(s->given, snap->given, 81);
    memcpy(s->notes, snap->notes, sizeof s->notes);
    s->puzzle = snap->puzzle;
    s->block = snap->block;
    s->cell = snap->cell;
}

static int pad_down_now(const struct input *in, int pad) {
    return in && pad >= 0 && pad < 16 && (in->pad_down[pad] || in->pad_hit[pad] >= 0);
}

static int button_down(const struct input *in, int button) {
    return in && input_down(in, button);
}

static int button_held(const struct input *in, int button) {
    if (!in || button < 0 || button >= IN_N) return 0;
    return ((in->held >> button) & 1) || input_down(in, button);
}

static void apply_hit(struct sudoku_state *s, struct ui_pad_hit hit) {
    if (s->block < 0 || s->block > 8) s->block = 0;
    if (s->cell < 0 || s->cell > 8) s->cell = 0;
    int idx = at(s->block, s->cell);
    if (hit.slot >= 0 && hit.slot < 9) {
        if (hit.mode == MODE_NOTE) {
            if (!s->given[idx] && s->grid[idx] == '0') {
                unsigned short bit = (unsigned short)(1u << hit.slot);
                push_undo(s);
                s->notes[idx] = (unsigned short)(s->notes[idx] ^ bit);
            }
        } else if (hit.mode == MODE_NUM) {
            char digit = (char)('1' + hit.slot);
            if (!s->given[idx] && s->grid[idx] != digit) {
                push_undo(s);
                s->grid[idx] = digit;
                s->notes[idx] = 0;
            }
        } else if (hit.mode == MODE_CELL) {
            s->cell = hit.slot;
        } else if (hit.mode == MODE_BLOCK) {
            s->block = hit.slot;
        }
    }
    if (hit.tap == TAP_CLEAR_NOTES) {
        if (s->notes[idx]) {
            push_undo(s);
            s->notes[idx] = 0;
        }
    } else if (hit.tap == TAP_CLEAR) {
        if (!s->given[idx] && s->grid[idx] != '0') {
            push_undo(s);
            s->grid[idx] = '0';
        }
    }
}

// Hold restart or New, then press the middle button. A tap alone does nothing.
static void apply_bar(struct sudoku_state *s, const struct input *in) {
    int yes = button_down(in, IN_B2);
    if (pad_down_now(in, 1) && yes) {
        push_undo(s);
        load_puzzle(s, s->puzzle);
    } else if (button_held(in, IN_B3) && yes) {
        push_undo(s);
        load_puzzle(s, s->puzzle + 1);
    } else if (yes) {
        pop_undo(s);
    }
}

static const char *status_of(const struct sudoku_state *s, const struct input *in) {
    if (pad_down_now(in, 1) || button_held(in, IN_B3)) return "Press Yes";
    if (solved(s->grid)) return "Solved";
    if (pad_down_now(in, 0)) return "Note";
    if (pad_down_now(in, 7)) return "Number";
    if (pad_down_now(in, 11)) return "Cell";
    if (pad_down_now(in, 15)) return "Block";
    return "Hold a side pad";
}

static void draw_board(struct ui *u, const struct sudoku_state *s) {
    char text[81][2];
    const char *labels[81];
    uint32_t fills[81];
    for (int i = 0; i < 81; i++) {
        int on = s->grid[i] >= '1' && s->grid[i] <= '9';
        text[i][0] = on ? s->grid[i] : 0;
        text[i][1] = 0;
        labels[i] = on ? text[i] : 0;
        if (player_clash(s, i)) fills[i] = FILL_BAD;
        else if (s->given[i]) fills[i] = FILL_GIVEN;
        else if (on) fills[i] = FILL_ENTRY;
        else fills[i] = FILL_EMPTY;
    }
    struct ui_grid_style st = {
        .fill = FILL_EMPTY,
        .text = UI_TEXT,
        .line = 0x3a4050,
        .block_line = 0xd8dce4,
        .cursor = UI_HL,
        .band = 0x4e86e0,
        .gap = 1,
        .block = 3,
        .block_px = 3,
        .cursor_px = 2,
    };
    int cursor = at(s->block, s->cell);
    ui_grid(u, BOARD_X, BOARD_Y, BOARD_W, BOARD_H, 9, 9, labels, fills, cursor, cursor, st);
}

static void draw_notes(struct ui *u, const struct sudoku_state *s) {
    const char *labels[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    uint32_t fills[9];
    unsigned short bits = s->notes[at(s->block, s->cell)];
    for (int i = 0; i < 9; i++) fills[i] = (bits & (1u << i)) ? FILL_NOTE : FILL_EMPTY;
    struct ui_grid_style st = {
        .fill = FILL_EMPTY,
        .text = UI_TEXT,
        .line = 0x3a4050,
        .block_line = 0x3a4050,
        .cursor = UI_HL,
        .band = UI_HL,
        .gap = 2,
        .block = 0,
        .cursor_px = 1,
    };
    ui_grid(u, NOTE_X, NOTE_Y, NOTE_W, NOTE_H, 3, 3, labels, fills, -1, -1, st);
}

static void draw_bar(struct ui *u, const struct input *in) {
    int armed = pad_down_now(in, 1) || button_held(in, IN_B3);
    const char *labels[3] = {pad_down_now(in, 1) ? "Rst?" : button_held(in, IN_B3) ? "New?" : "Sudoku",
                             armed ? "Yes" : "Undo", "New"};
    struct ui_soft_style st = {
        .fill = 0x2a3038,
        .text = UI_TEXT,
        .active_text = UI_BG,
        .highlight = UI_HL,
        .gap = 4,
        .feedback = UI_SOFT_HOLD,
    };
    ui_soft_keys(u, BAR_X, BAR_Y, BAR_W, BAR_H, labels, 0, st, in, 0);
}

static struct ui_pad_hit draw_pads(struct ui *u, const struct input *in, const struct sudoku_state *s) {
    const char *digits[9] = {"1", "2", "3", "4", "5", "6", "7", "8", "9"};
    char cell_txt[9][2];
    const char *cell_names[9];
    for (int i = 0; i < 9; i++) {
        int idx = at(s->block, i);
        int on = s->grid[idx] >= '1' && s->grid[idx] <= '9';
        cell_txt[i][0] = on ? s->grid[idx] : 0;
        cell_txt[i][1] = 0;
        cell_names[i] = on ? cell_txt[i] : 0;
    }
    struct ui_pad_mode modes[5] = {
        {.pad = 1, .color = 0x3a3030, .held = 0xc45c4a, .label = "Rst"},
        {.pad = 0, .color = 0x14382e, .held = 0x2f9e62, .label = "Note", .names = digits},
        {.pad = 7, .color = 0x163d2c, .held = 0x2f9e62, .label = "Num", .names = digits},
        {.pad = 11, .color = 0x4a3410, .held = 0xe09a2b, .label = "Cel", .names = cell_names},
        {.pad = 15, .color = 0x152848, .held = 0x4e86e0, .label = "Blk"},
    };
    struct ui_pad_tap taps[2] = {
        {.pad = 2, .color = 0x2a3038, .label = "NClr"},
        {.pad = 3, .color = 0x2a3038, .label = "Clr"},
    };
    uint32_t selected_color = 0xffffff;
    int selected = -1;
    if (pad_down_now(in, 0)) {
        selected_color = 0x2f9e62;
        unsigned short bits = s->notes[at(s->block, s->cell)];
        for (int i = 0; i < 9; i++)
            if (bits & (1u << i)) selected = i;
    } else if (pad_down_now(in, 7)) {
        selected_color = 0x2f9e62;
        int idx = at(s->block, s->cell);
        if (s->grid[idx] >= '1' && s->grid[idx] <= '9') selected = s->grid[idx] - '1';
    } else if (pad_down_now(in, 11)) {
        selected_color = 0xe09a2b;
        selected = s->cell;
    } else if (pad_down_now(in, 15)) {
        selected_color = 0x4e86e0;
        selected = s->block;
    }
    struct ui_pad_mode_style st = {
        .gap = 2,
        .border_px = 1,
        .border = 0x505868,
        .idle = 0x18181e,
        .mod_idle = 0x2a3038,
        .text = UI_TEXT,
        .selected = selected,
        .selected_color = selected_color,
    };
    return ui_pad_modes(u, PAD_X, PAD_Y, PAD_W, PAD_H, cluster, 9, modes, 5, taps, 2, st, in);
}

int sudoku_frame(struct ui *u, const struct input *in, void *state) {
    struct sudoku_state *s = state;
    if (!s) return 1;
    example_menu_prepare();
    int was = example_menu.shown;
    int open = in && ui_menu_open(&example_menu, in);
    if (!s->ready) {
        load_puzzle(s, 0);
        s->block = 0;
        s->cell = 0;
        s->undo_n = 0;
        s->ready = 1;
    }
    if (s->block < 0 || s->block > 8) s->block = 0;
    if (s->cell < 0 || s->cell > 8) s->cell = 0;
    ui_clear(u, UI_BG);
    // The hit is taken from the board as it was. The draw below shows the result.
    struct ui_pad_hit hit = {-1, -1, -1};
    if (!open) hit = draw_pads(u, in, s);
    if (!open) {
        apply_hit(s, hit);
        apply_bar(s, in);
    }
    draw_bar(u, open ? 0 : in);
    draw_board(u, s);
    ui_text(u, 4, 220, status_of(s, in), UI_TEXT, 1);
    draw_notes(u, s);
    draw_pads(u, open ? 0 : in, s);
    if (example_menu_draw(u, in)) return 1;
    if (was && !example_menu.shown) draw_pads(u, in, s);
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct sudoku_state s = {0};
    return app_run(sudoku_frame, &s);
}
#endif
