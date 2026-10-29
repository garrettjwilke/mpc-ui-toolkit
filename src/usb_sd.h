// Full-screen USB SD card access mode. Opt in by linking usb_sd.c.
// The screen unmounts the MPC Sample SD card, switches the USB gadget to
// mass storage, and binds the card so a computer can read and write it.
// Leaving restores the normal gadget and waits for the card to mount again.
#pragma once

#include "widgets.h"
#include <stddef.h>

#define UI_USB_SD_LABEL "USB storage (SD card)"
#define UI_USB_SD_HINT "press to mount SD card over USB"

// Return codes from ui_usb_sd().
enum {
    UI_USB_SD_ACTIVE = 0,  // screen is up: preparing, connected, exiting, or error
    UI_USB_SD_DONE = 1,    // user ejected; the card is local again
    UI_USB_SD_FAILED = -1  // start failed and the user left the error screen
};

// What the screen is showing. Apps can read phase; they do not set it.
enum {
    UI_USB_SD_PHASE_PREPARING = 0,
    UI_USB_SD_PHASE_ACTIVE,
    UI_USB_SD_PHASE_EXITING,
    UI_USB_SD_PHASE_ERROR,
    UI_USB_SD_PHASE_DONE,
    UI_USB_SD_PHASE_FAILED
};

struct ui_usb_sd_style {
    uint32_t bg, text, dim, hl, box, border, bar, active_text;
};

struct ui_usb_sd {
    int phase;
    int presented; // 1 after the preparing or exiting text has been drawn
    int bound;     // 1 while the card is attached to the USB gadget
    char err[160];
    int soft_active;
    struct ui_soft_state soft;
};

void ui_usb_sd_init(struct ui_usb_sd *s);
struct ui_usb_sd_style ui_usb_sd_style_default(void);

// Draws the 320x240 screen and handles B1, the encoder, and the on-screen button.
// The first call draws "Preparing..." and returns ACTIVE without blocking.
// The next call runs the gadget switch, then draws Active or Error.
// Exit draws "Exiting..." once. The following call restores the gadget and
// returns DONE. Call ui_wake's request: both waits set wake_ms to 1.
// On the error screen, B1, encoder press, or Back returns FAILED.
int ui_usb_sd(struct ui *u, struct ui_usb_sd *s, struct ui_usb_sd_style st, const struct input *in);

// 1 while the card is bound to the gadget. Reboot and process exit should stop.
int ui_usb_sd_is_active(const struct ui_usb_sd *s);

// Unbind the card and restore the default gadget when bound. No-op otherwise.
int ui_usb_sd_stop(struct ui_usb_sd *s);

// Host tests. Non-Linux always uses the mock, so start and stop do not touch a device.
// On Linux, call this before ui_usb_sd or a host test can unmount a real card.
void ui_usb_sd_set_mock(int enable, int fail_start);
