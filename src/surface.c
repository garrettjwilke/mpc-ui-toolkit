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

static void send_ch16_meter_power(int fd, int on) {
    unsigned char m[6] = {0xBF, 0x00, (unsigned char)(on ? 0x7F : 0x00),
                          0xBF, 0x01, (unsigned char)(on ? 0x7F : 0x00)};
    send_bytes(fd, m, sizeof m);
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
    s->last_left_meter = 0;
    s->last_right_meter = 0;
    s->last_track_mask = 0;
    memset(s->last_button, 0, sizeof s->last_button);
    int card = card_number();
    if (card < 0) {
        fputs("control surface not found\n", stderr);
        return -1;
    }
    char path[32];
    snprintf(path, sizeof path, "/dev/snd/midiC%dD0", card);
    s->fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (s->fd < 0) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    send_ch16_meter_power(s->fd, 1);
    return 0;
}

void surface_pad(struct surface *s, int pad, int r, int g, int b) {
    unsigned char m[12] = {0xF0, 0x47, 0x7F, 0x5C, 0x65, 0x00, 0x04, (unsigned char)(pad & 0x7f),
                           (unsigned char)(r & 0x7f), (unsigned char)(g & 0x7f), (unsigned char)(b & 0x7f), 0xF7};
    if (s->fd >= 0) send_bytes(s->fd, m, sizeof m);
}

void surface_meter(struct surface *s, int left_mask, int right_mask) {
    if (s->fd < 0) return;
    left_mask &= 0x1F;
    right_mask &= 0x1F;
    if (left_mask == s->last_left_meter && right_mask == s->last_right_meter) return;

    if (s->last_left_meter == 0 && s->last_right_meter == 0 && (left_mask != 0 || right_mask != 0)) {
        send_ch16_meter_power(s->fd, 1);
    }

    if (left_mask != s->last_left_meter) {
        unsigned char m[3] = {0xB0, 83, (unsigned char)left_mask};
        send_bytes(s->fd, m, sizeof m);
        s->last_left_meter = left_mask;
    }
    if (right_mask != s->last_right_meter) {
        unsigned char m[3] = {0xB0, 84, (unsigned char)right_mask};
        send_bytes(s->fd, m, sizeof m);
        s->last_right_meter = right_mask;
    }

    if (left_mask == 0 && right_mask == 0 && s->last_track_mask == 0) {
        send_ch16_meter_power(s->fd, 0);
    }
}

void surface_track_led(struct surface *s, int track_index) {
    if (s->fd < 0) return;
    int mask = (track_index >= 0 && track_index < 8) ? (1 << track_index) : 0;
    int diff = mask ^ s->last_track_mask;
    if (!diff) return;

    if (s->last_track_mask == 0 && mask != 0) {
        send_ch16_meter_power(s->fd, 1);
    }

    for (int i = 0; i < 8; i++) {
        if (diff & (1 << i)) {
            int on = (mask >> i) & 1;
            unsigned char m[3] = {0xB0, (unsigned char)(57 + i), (unsigned char)(on ? 127 : 0)};
            send_bytes(s->fd, m, sizeof m);
        }
    }
    s->last_track_mask = mask;

    if (mask == 0 && s->last_left_meter == 0 && s->last_right_meter == 0) {
        send_ch16_meter_power(s->fd, 0);
    }
}

static int bicolour_note(int note) {
    switch (note) {
    case 0: case 1: case 6: case 7: case 9: case 36: case 40: case 43:
    case 53: case 74: case 75:
        return 1;
    default:
        return 0;
    }
}

static int button_level_value(int note, int level) {
    if (level <= 0) return 0;
    if (bicolour_note(note)) return level == 1 ? 4 : 8;
    return level == 1 ? 1 : 2;
}

static void led(struct surface *s, int note, int value) {
    unsigned char m[3] = {0xB0, (unsigned char)(note & 0x7f), (unsigned char)(value & 0x7f)};
    send_bytes(s->fd, m, 3);
}

void surface_buttons(struct surface *s, const unsigned char *level, unsigned mask) {
    if (!s || s->fd < 0) return;
    for (int i = 0; i < IN_N && i < 32; i++) {
        if ((mask & (1u << i)) == 0) continue;
        int want = level ? button_level_value(INPUT_BUTTON_NOTE[i], level[i]) : 0;
        if (s->last_button[i] == (unsigned char)want) continue;
        led(s, INPUT_BUTTON_NOTE[i], want);
        s->last_button[i] = (unsigned char)want;
    }
}

void surface_all_off(struct surface *s) {
    if (s->fd < 0) return;
    for (int i = 0; i < IN_N; i++) led(s, INPUT_BUTTON_NOTE[i], 0);
    memset(s->last_button, 0, sizeof s->last_button);
    for (int i = 0; i < 16; i++) surface_pad(s, i, 0, 0, 0);
    surface_meter(s, 0, 0);
    surface_track_led(s, -1);
    send_ch16_meter_power(s->fd, 0);
}

void surface_close(struct surface *s) {
    if (s->fd >= 0) {
        surface_meter(s, 0, 0);
        surface_track_led(s, -1);
        send_ch16_meter_power(s->fd, 0);
        close(s->fd);
    }
    s->fd = -1;
    s->last_left_meter = 0;
    s->last_right_meter = 0;
    s->last_track_mask = 0;
}
