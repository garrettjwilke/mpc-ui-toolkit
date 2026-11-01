#include "image.h"

#include <stdlib.h>

#define STBI_ONLY_PNG
#define STBI_ONLY_PNM
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

static int store_rgba(unsigned char *rgba, int iw, int ih, uint32_t **px, int *w, int *h) {
    uint32_t *out;
    int i, count;
    count = iw * ih;
    out = malloc((size_t)count * sizeof(uint32_t));
    if (!out) {
        stbi_image_free(rgba);
        return -1;
    }
    for (i = 0; i < count; i++) {
        unsigned char *p = rgba + i * 4;
        out[i] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
    }
    stbi_image_free(rgba);
    *px = out;
    *w = iw;
    *h = ih;
    return 0;
}

static int size_ok(int iw, int ih) { return iw >= 1 && ih >= 1 && iw <= 640 && ih <= 480; }

int image_decode(const char *path, uint32_t **px, int *w, int *h) {
    int iw = 0, ih = 0, n = 0;
    unsigned char *rgba;
    if (!path || !px || !w || !h) return -1;
    if (!stbi_info(path, &iw, &ih, &n) || !size_ok(iw, ih)) return -1;
    rgba = stbi_load(path, &iw, &ih, &n, 4);
    if (!rgba) return -1;
    return store_rgba(rgba, iw, ih, px, w, h);
}

int image_decode_mem(const void *data, int size, uint32_t **px, int *w, int *h) {
    int iw = 0, ih = 0, n = 0;
    unsigned char *rgba;
    if (!data || size < 1 || !px || !w || !h) return -1;
    if (!stbi_info_from_memory(data, size, &iw, &ih, &n) || !size_ok(iw, ih)) return -1;
    rgba = stbi_load_from_memory(data, size, &iw, &ih, &n, 4);
    if (!rgba) return -1;
    return store_rgba(rgba, iw, ih, px, w, h);
}

uint32_t *image_scale(const uint32_t *src, int sw, int sh, int dw, int dh) {
    uint32_t *dst;
    int y;
    if (!src || sw < 1 || sh < 1 || dw < 1 || dh < 1) return 0;
    dst = malloc((size_t)dw * (size_t)dh * sizeof(uint32_t));
    if (!dst) return 0;
    for (y = 0; y < dh; y++) {
        int sy = y * sh / dh;
        int x;
        if (sy >= sh) sy = sh - 1;
        for (x = 0; x < dw; x++) {
            int sx = x * sw / dw;
            if (sx >= sw) sx = sw - 1;
            dst[y * dw + x] = src[sy * sw + sx];
        }
    }
    return dst;
}
