#include "screen.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

static int drm_call(int fd, unsigned long req, void *arg, const char *what) {
    int r;
    while ((r = ioctl(fd, req, arg)) < 0 && errno == EINTR) {}
    if (r < 0) fprintf(stderr, "drm %s: %s\n", what, strerror(errno));
    return r;
}

int screen_open(struct screen *s) {
    memset(s, 0, sizeof *s);
    s->fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (s->fd < 0) {
        perror("/dev/dri/card0");
        return -1;
    }
    for (int i = 0; i < 50; i++) {
        if (ioctl(s->fd, DRM_IOCTL_SET_MASTER, 0) == 0) break;
        if (errno != EBUSY || i == 49) fprintf(stderr, "drm set master: %s\n", strerror(errno));
        if (errno != EBUSY) {
            screen_close(s);
            return -1;
        }
        usleep(20 * 1000);
    }
    uint32_t crtcs[4], conns[4];
    struct drm_mode_card_res res = {.crtc_id_ptr = (uintptr_t)crtcs, .connector_id_ptr = (uintptr_t)conns,
                                    .count_crtcs = 4, .count_connectors = 4};
    if (drm_call(s->fd, DRM_IOCTL_MODE_GETRESOURCES, &res, "getresources") < 0) goto fail;
    if (!res.count_crtcs || !res.count_connectors) {
        fputs("drm: no crtc or connector\n", stderr);
        goto fail;
    }
    struct drm_mode_modeinfo modes[8];
    struct drm_mode_get_connector con = {.connector_id = conns[0]};
    if (drm_call(s->fd, DRM_IOCTL_MODE_GETCONNECTOR, &con, "getconnector") < 0) goto fail;
    if (!con.count_modes || con.count_modes > 8) {
        fprintf(stderr, "drm: %u modes\n", con.count_modes);
        goto fail;
    }
    con = (struct drm_mode_get_connector){.connector_id = conns[0], .modes_ptr = (uintptr_t)modes, .count_modes = con.count_modes};
    if (drm_call(s->fd, DRM_IOCTL_MODE_GETCONNECTOR, &con, "getconnector modes") < 0) goto fail;
    struct drm_mode_modeinfo mode = modes[0];
    if (mode.hdisplay != 320 || mode.vdisplay != 240) {
        fprintf(stderr, "drm: mode %ux%u\n", mode.hdisplay, mode.vdisplay);
        goto fail;
    }
    struct drm_mode_create_dumb dumb = {.width = mode.hdisplay, .height = mode.vdisplay, .bpp = 32};
    if (drm_call(s->fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb, "create dumb") < 0) goto fail;
    struct drm_mode_fb_cmd2 add = {.width = dumb.width, .height = dumb.height, .pixel_format = DRM_FORMAT_XRGB8888,
                                   .handles = {dumb.handle}, .pitches = {dumb.pitch}};
    if (drm_call(s->fd, DRM_IOCTL_MODE_ADDFB2, &add, "addfb2") < 0) goto fail;
    struct drm_mode_map_dumb map = {.handle = dumb.handle};
    if (drm_call(s->fd, DRM_IOCTL_MODE_MAP_DUMB, &map, "map dumb") < 0) goto fail;
    void *p = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, (off_t)map.offset);
    if (p == MAP_FAILED) {
        perror("mmap dumb");
        goto fail;
    }
    memset(p, 0, dumb.size);
    struct drm_mode_crtc set = {.crtc_id = crtcs[0], .fb_id = add.fb_id, .set_connectors_ptr = (uintptr_t)&conns[0],
                                .count_connectors = 1, .mode = mode, .mode_valid = 1};
    if (drm_call(s->fd, DRM_IOCTL_MODE_SETCRTC, &set, "setcrtc") < 0) {
        munmap(p, dumb.size);
        goto fail;
    }
    s->px = p;
    s->stride = (int)(dumb.pitch / 4);
    s->bytes = dumb.size;
    s->fb_id = add.fb_id;
    s->ok = 1;
    fprintf(stderr, "drm %ux%u crtc %u connector %u fb %u\n", mode.hdisplay, mode.vdisplay, crtcs[0], conns[0], s->fb_id);
    return 0;
fail:
    screen_close(s);
    return -1;
}

void screen_flush(struct screen *s, const int rects[][4], int n) {
    if (!s->px || n <= 0 || !rects) return;
    struct drm_clip_rect clips[256];
    int used = 0;
    if (n > 256) n = 256;
    for (int i = 0; i < n; i++) {
        int x0 = rects[i][0], y0 = rects[i][1], x1 = rects[i][2], y1 = rects[i][3];
        if (x1 <= x0 || y1 <= y0) continue;
        clips[used++] = (struct drm_clip_rect){(unsigned short)x0, (unsigned short)y0, (unsigned short)x1, (unsigned short)y1};
    }
    if (!used) return;
    struct drm_mode_fb_dirty_cmd dirty = {.fb_id = s->fb_id, .num_clips = (uint32_t)used, .clips_ptr = (uintptr_t)clips};
    drm_call(s->fd, DRM_IOCTL_MODE_DIRTYFB, &dirty, "dirtyfb");
}

void screen_close(struct screen *s) {
    if (s->ok && s->px) {
        memset(s->px, 0, s->bytes);
        int rect[1][4] = {{0, 0, 320, 240}};
        screen_flush(s, rect, 1);
    }
    if (s->px) munmap(s->px, s->bytes);
    s->px = NULL;
    if (s->fd >= 0) {
        ioctl(s->fd, DRM_IOCTL_DROP_MASTER, 0);
        close(s->fd);
        s->fd = -1;
    }
    s->ok = 0;
}
