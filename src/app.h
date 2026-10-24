// Device loop: poll the surface, draw one frame, flush dirty bands.
#pragma once
#include "ui.h"

typedef int (*ui_frame)(struct ui *u, const struct input *in, void *state);

// Returns 0 when the frame asks to quit. Does not stop or start the stock app.
int app_run(ui_frame frame, void *state);
