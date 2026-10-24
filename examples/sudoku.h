#pragma once
#include "ui.h"

enum { SUDOKU_UNDO = 32 };

struct sudoku_snap {
    char grid[81];
    char given[81];
    int puzzle, block, cell;
};

struct sudoku_state {
    int puzzle;
    char grid[81];
    char given[81];
    int block, cell;
    int ready;
    struct sudoku_snap undo[SUDOKU_UNDO];
    int undo_n;
};

int sudoku_frame(struct ui *u, const struct input *in, void *state);
// 1 when every built-in puzzle matches its solution and that solution is a full board.
int sudoku_puzzles_ok(void);
