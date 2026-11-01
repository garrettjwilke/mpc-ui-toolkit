// PNG and PPM pictures for help pages. The caller frees *px.
#pragma once
#include <stdint.h>

// 0 on success. *px is 0xRRGGBB, tightly packed, width * height.
// A picture wider than 640 or taller than 480 is rejected.
int image_decode(const char *path, uint32_t **px, int *w, int *h);
int image_decode_mem(const void *data, int size, uint32_t **px, int *w, int *h);

// Nearest-neighbor scale. The caller frees the result. NULL if the size is empty.
uint32_t *image_scale(const uint32_t *src, int sw, int sh, int dw, int dh);
