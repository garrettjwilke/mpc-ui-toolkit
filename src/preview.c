// Draw each example once and write build/shots/<name>.ppm. Build: make preview
#include "input.h"
#include "ui.h"
#include "widgets.h"
#include "../examples/sudoku.h"
#include <stdio.h>
#include <string.h>

int clear_frame(struct ui *u, const struct input *in, void *state);
int boxes_frame(struct ui *u, const struct input *in, void *state);
struct buttons_state { int focus; int on[3]; };
int buttons_frame(struct ui *u, const struct input *in, void *state);
struct highlight_state { int focus; };
int highlight_frame(struct ui *u, const struct input *in, void *state);
struct soft_state { int active; };
int soft_keys_frame(struct ui *u, const struct input *in, void *state);
struct knobs_state { float norm[3]; struct ui_knob_state ks; };
int knobs_frame(struct ui *u, const struct input *in, void *state);
struct list_state { struct ui_list_state ls; const char *chosen; };
int list_frame(struct ui *u, const struct input *in, void *state);
struct pads_state { uint32_t color[16]; int ready; };
int pads_frame(struct ui *u, const struct input *in, void *state);
struct anim_state { int x; int dir; };
int anim_frame(struct ui *u, const struct input *in, void *state);
struct text_edit_state { char name[32]; struct ui_text_editor editor; int editing; const char *last_action; };
int text_edit_frame(struct ui *u, const struct input *in, void *state);

static int write_ppm(const char *path, const uint32_t *px) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    for (int i = 0; i < UI_W * UI_H; i++) {
        unsigned char rgb[3] = {(unsigned char)((px[i] >> 16) & 255), (unsigned char)((px[i] >> 8) & 255), (unsigned char)(px[i] & 255)};
        if (fwrite(rgb, 1, 3, f) != 3) {
            fclose(f);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

static int shot(const char *name, int (*frame)(struct ui *, const struct input *, void *), void *state) {
    uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    struct input in;
    input_begin(&in, 0);
    frame(&u, &in, state);
    char path[64];
    snprintf(path, sizeof path, "build/shots/%s.ppm", name);
    return write_ppm(path, buf);
}

int main(void) {
    struct buttons_state buttons = {0};
    struct highlight_state highlight = {0};
    struct soft_state soft = {0};
    struct knobs_state knobs = {.norm = {0.25f, 0.50f, 0.75f}, .ks = {.last = {-1, -1, -1}}};
    struct list_state list = {.chosen = "Alpha"};
    struct pads_state pads = {0};
    struct anim_state anim = {.x = 17, .dir = 4};
    struct input hit;
    input_begin(&hit, 0);
    hit.pad_hit[0] = 100;
    hit.pad_hit[5] = 40;
    hit.pad_down[0] = hit.pad_down[5] = 1;

    int rc = 0;
    rc |= shot("clear", clear_frame, 0);
    rc |= shot("boxes", boxes_frame, 0);
    rc |= shot("buttons", buttons_frame, &buttons);
    rc |= shot("highlight", highlight_frame, &highlight);
    rc |= shot("soft-keys", soft_keys_frame, &soft);
    rc |= shot("knobs", knobs_frame, &knobs);
    rc |= shot("list", list_frame, &list);
    rc |= shot("anim", anim_frame, &anim);
    struct sudoku_state sudoku = {0};
    rc |= shot("sudoku", sudoku_frame, &sudoku);
    struct text_edit_state text_edit = {.editing = 1};
    strncpy(text_edit.name, "Kick_808", sizeof text_edit.name - 1);
    ui_text_editor_init(&text_edit.editor, text_edit.name, sizeof text_edit.name, "RENAME SAMPLE");
    rc |= shot("text-edit", text_edit_frame, &text_edit);

    uint32_t buf[UI_W * UI_H];
    struct ui u = {.px = buf};
    pads_frame(&u, &hit, &pads);
    rc |= write_ppm("build/shots/pads.ppm", buf);
    return rc ? 1 : 0;
}
