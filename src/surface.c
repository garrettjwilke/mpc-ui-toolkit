#include "surface.h"
#include "input.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void send_bytes(int fd, const unsigned char *b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, b + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            perror("write");
            return;
        }
        if (w == 0) return;
        off += (size_t)w;
    }
}

static int card_number(void) {
    char link[64];
    ssize_t n = readlink("/dev/snd/by-path/platform-fe670000.serial", link, sizeof link - 1);
    if (n <= 0) return -1;
    link[n] = 0;
    char *c = strstr(link, "controlC");
    if (!c) return -1;
    int card = 0;
    for (c += 8; *c >= '0' && *c <= '9'; c++) card = card * 10 + (*c - '0');
    return card;
}

int surface_open(struct surface *s) {
    s->fd = -1;
    int card = card_number();
    if (card < 0) {
        fputs("control surface not found\n", stderr);
        return -1;
    }
    char path[32];
    snprintf(path, sizeof path, "/dev/snd/midiC%dD0", card);
    s->fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (s->fd < 0) fprintf(stderr, "%s: %s\n", path, strerror(errno));
    return s->fd < 0 ? -1 : 0;
}

void surface_pad(struct surface *s, int pad, int r, int g, int b) {
    unsigned char m[12] = {0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04, (unsigned char)(pad & 0x7f),
                           (unsigned char)(r & 0x7f), (unsigned char)(g & 0x7f), (unsigned char)(b & 0x7f), 0xF7};
    if (s->fd >= 0) send_bytes(s->fd, m, sizeof m);
}

static void led(struct surface *s, int note, int value) {
    unsigned char m[3] = {0xB0, (unsigned char)(note & 0x7f), (unsigned char)(value & 0x7f)};
    send_bytes(s->fd, m, 3);
}

void surface_all_off(struct surface *s) {
    if (s->fd < 0) return;
    for (int i = 0; i < IN_N; i++) led(s, INPUT_BUTTON_NOTE[i], 0);
    for (int i = 0; i < 16; i++) surface_pad(s, i, 0, 0, 0);
}

void surface_close(struct surface *s) {
    if (s->fd >= 0) close(s->fd);
    s->fd = -1;
}
