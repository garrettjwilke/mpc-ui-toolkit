// A block slides along a track. Each frame moves it a few pixels.
#include "ui.h"
#include "input.h"
#include "menu.h"

#define ANIM_TRACK_X 16
#define ANIM_TRACK_Y 100
#define ANIM_TRACK_W 288
#define ANIM_TRACK_H 40
#define ANIM_BLOCK 24
#define ANIM_STEP 4

struct anim_state {
    int x;
    int dir;
};

int anim_frame(struct ui *u, const struct input *in, void *state) {
    struct anim_state *s = state;
    if (!s->dir) s->dir = ANIM_STEP;
    ui_clear(u, UI_BG);
    ui_text(u, 16, 24, "anim", UI_TEXT, 2);
    ui_box(u, ANIM_TRACK_X, ANIM_TRACK_Y, ANIM_TRACK_W, ANIM_TRACK_H, (struct ui_paint){
        .fill_set = 1, .fill = 0x1c1c22, .border_set = 1, .border = 0x505868, .border_px = 1,
    });
    int y = ANIM_TRACK_Y + (ANIM_TRACK_H - ANIM_BLOCK) / 2;
    ui_box(u, s->x, y, ANIM_BLOCK, ANIM_BLOCK, (struct ui_paint){.fill_set = 1, .fill = UI_HL});

    int min_x = ANIM_TRACK_X + 1;
    int max_x = ANIM_TRACK_X + ANIM_TRACK_W - 1 - ANIM_BLOCK;
    int next = s->x + s->dir;
    if (next > max_x || next < min_x) {
        s->dir = -s->dir;
        next = s->x + s->dir;
    }
    if (next > max_x) next = max_x;
    if (next < min_x) next = min_x;
    s->x = next;
    ui_wake(u, 16);
    if (example_menu_draw(u, in)) return 1;
    return 0;
}

#ifndef UI_NO_MAIN
#include "app.h"
int main(void) {
    struct anim_state s = {.x = ANIM_TRACK_X + 1, .dir = ANIM_STEP};
    return app_run(anim_frame, &s);
}
#endif
