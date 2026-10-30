#define _GNU_SOURCE
#include "usb_sd.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/mount.h>
#endif

#define LUN_PATH "/sys/kernel/config/usb_gadget/ac50/functions/mass_storage.block/lun.0/file"
#define GADGET_TOOL "/usr/bin/az0x-usb-gadget"
#define EDISKSCTL "/usr/bin/edisksctl"

static int s_mock_enabled = 0;
static int s_mock_fail = 0;

void ui_usb_sd_set_mock(int enable, int fail_start) {
    s_mock_enabled = enable;
    s_mock_fail = fail_start;
}

static int usb_sd_is_mock(void) {
#ifdef __linux__
    return s_mock_enabled;
#else
    (void)s_mock_enabled;
    return 1;
#endif
}

static int run_cmd(char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static void set_err(char *buf, size_t len, const char *msg) {
    if (buf && len > 0) snprintf(buf, len, "%s", msg ? msg : "");
}

static int find_sd_device(char *out, size_t n) {
    if (usb_sd_is_mock()) {
        snprintf(out, n, "/dev/mock_mmcblk1");
        return 0;
    }

    /* External MMC controller on the RK3566 (SDMMC at fe2b0000.mmc). */
    DIR *d = opendir("/sys/bus/platform/devices/fe2b0000.mmc/mmc_host");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strncmp(de->d_name, "mmc", 3) != 0) continue;
            char path[512];
            snprintf(path, sizeof path, "/sys/bus/platform/devices/fe2b0000.mmc/mmc_host/%s", de->d_name);
            DIR *host_d = opendir(path);
            if (!host_d) continue;
            struct dirent *hde;
            while ((hde = readdir(host_d)) != NULL) {
                if (strncmp(hde->d_name, "mmc", 3) == 0 && strchr(hde->d_name, ':')) {
                    char blk_path[512];
                    snprintf(blk_path, sizeof blk_path, "%s/%s/block", path, hde->d_name);
                    DIR *blk_d = opendir(blk_path);
                    if (blk_d) {
                        struct dirent *bde;
                        while ((bde = readdir(blk_d)) != NULL) {
                            if (strncmp(bde->d_name, "mmcblk", 6) == 0) {
                                snprintf(out, n, "/dev/%s", bde->d_name);
                                closedir(blk_d);
                                closedir(host_d);
                                closedir(d);
                                return 0;
                            }
                        }
                        closedir(blk_d);
                    }
                }
            }
            closedir(host_d);
        }
        closedir(d);
    }

    if (access("/dev/mmcblk1", F_OK) == 0) {
        snprintf(out, n, "/dev/mmcblk1");
        return 0;
    }
    return -1;
}

static int sd_is_mounted(const char *sd_dev) {
    if (!sd_dev || !sd_dev[0]) return 0;
    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return 0;
    char line[1024];
    size_t dev_len = strlen(sd_dev);
    int mounted = 0;
    while (fgets(line, sizeof line, f)) {
        char dev[256], mnt[512];
        if (sscanf(line, "%255s %511s", dev, mnt) == 2) {
            if (strncmp(dev, sd_dev, dev_len) == 0 && strncmp(mnt, "/media/", 7) == 0) {
                mounted = 1;
                break;
            }
        }
    }
    fclose(f);
    return mounted;
}

static void unmount_sd_partitions(const char *sd_dev) {
    /* Protect against busy cwd locking the mount */
    if (chdir("/tmp") != 0) {
        (void)chdir("/");
    }

    sync();
    usleep(50 * 1000);
    sync();

    FILE *f = fopen("/proc/mounts", "r");
    if (!f) return;

    char line[1024];
    char devs[32][256];
    char mounts[32][512];
    int nmounts = 0;

    size_t dev_len = strlen(sd_dev);
    while (fgets(line, sizeof line, f) && nmounts < 32) {
        char dev[256], mnt[512];
        if (sscanf(line, "%255s %511s", dev, mnt) == 2) {
            if (strncmp(dev, sd_dev, dev_len) == 0) {
                char decoded[512];
                size_t di = 0;
                for (size_t si = 0; mnt[si] && di + 1 < sizeof decoded; si++) {
                    if (mnt[si] == '\\' && mnt[si + 1] >= '0' && mnt[si + 1] <= '7' &&
                        mnt[si + 2] >= '0' && mnt[si + 2] <= '7' &&
                        mnt[si + 3] >= '0' && mnt[si + 3] <= '7') {
                        int code = (mnt[si + 1] - '0') * 64 + (mnt[si + 2] - '0') * 8 + (mnt[si + 3] - '0');
                        decoded[di++] = (char)code;
                        si += 3;
                    } else {
                        decoded[di++] = mnt[si];
                    }
                }
                decoded[di] = 0;
                snprintf(devs[nmounts], sizeof devs[0], "%s", dev);
                snprintf(mounts[nmounts], sizeof mounts[0], "%s", decoded);
                nmounts++;
            }
        }
    }
    fclose(f);

    for (int i = 0; i < nmounts; i++) {
        if (access(EDISKSCTL, X_OK) == 0) {
            char *av_edisks_dev[] = {EDISKSCTL, "unmount", devs[i], NULL};
            run_cmd(av_edisks_dev);
        }
#ifdef __linux__
        /* Attempt clean unmount first */
        if (umount(mounts[i]) != 0) {
            /* Try remounting read-only before detaching */
            char *av_ro[] = {"mount", "-o", "remount,ro", mounts[i], NULL};
            run_cmd(av_ro);
            umount2(mounts[i], MNT_DETACH);
        }
#endif
        char *av_umount[] = {"umount", mounts[i], NULL};
        if (run_cmd(av_umount) != 0) {
            char *av_lazy[] = {"umount", "-l", mounts[i], NULL};
            run_cmd(av_lazy);
        }
    }
}

static int start_backend(char *err_buf, size_t err_len) {
    if (usb_sd_is_mock()) {
        if (s_mock_fail) {
            set_err(err_buf, err_len, "Mock failure starting USB storage");
            return -1;
        }
        return 0;
    }

    char sd_dev[64];
    if (find_sd_device(sd_dev, sizeof sd_dev) != 0) {
        set_err(err_buf, err_len, "SD card not detected in slot");
        return -1;
    }

    unmount_sd_partitions(sd_dev);

    char *av_switch[] = {GADGET_TOOL, "switch", "sd-access", NULL};
    if (run_cmd(av_switch) != 0) {
        set_err(err_buf, err_len, "Failed to switch USB gadget to sd-access");
        return -2;
    }

    int ready = 0;
    for (int retry = 0; retry < 50; retry++) {
        if (access(LUN_PATH, W_OK) == 0) {
            ready = 1;
            break;
        }
        usleep(100 * 1000);
    }
    if (!ready) {
        set_err(err_buf, err_len, "Timeout waiting for USB mass storage gadget sysfs");
        return -3;
    }

    int fd = open(LUN_PATH, O_WRONLY);
    if (fd < 0) {
        set_err(err_buf, err_len, "Cannot open USB storage LUN configfs node");
        return -4;
    }

    size_t dev_str_len = strlen(sd_dev);
    char write_buf[128];
    int n = snprintf(write_buf, sizeof write_buf, "%s\n", sd_dev);
    ssize_t written = write(fd, write_buf, (size_t)n);
    close(fd);

    if (written < (ssize_t)dev_str_len) {
        set_err(err_buf, err_len, "Failed to attach SD device to USB storage LUN");
        return -5;
    }
    return 0;
}

static int stop_backend(char *err_buf, size_t err_len) {
    if (usb_sd_is_mock()) return 0;

    sync();

    int fd = open(LUN_PATH, O_WRONLY);
    if (fd >= 0) {
        write(fd, "\n", 1);
        close(fd);
    }

    char *av_switch[] = {GADGET_TOOL, "switch", NULL};
    if (run_cmd(av_switch) != 0)
        set_err(err_buf, err_len, "Warning: failed to restore default USB gadget");

    usleep(300 * 1000);
    sync();

    int cfd = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (cfd >= 0) {
        write(cfd, "3\n", 2);
        close(cfd);
    }

    char *av_udev[] = {"udevadm", "trigger", "--subsystem-match=block", NULL};
    run_cmd(av_udev);

    char part[64];
    if (find_sd_device(part, sizeof part) == 0) {
        char part1[80];
        snprintf(part1, sizeof part1, "%sp1", part);
        for (int retry = 0; retry < 50; retry++) {
            if (sd_is_mounted(part)) break;
            if ((retry == 3 || retry == 15 || retry == 30) && access(EDISKSCTL, X_OK) == 0) {
                const char *target = access(part1, F_OK) == 0 ? part1 : part;
                char *av_mount[] = {EDISKSCTL, "mount", (char *)target, NULL};
                run_cmd(av_mount);
                if (sd_is_mounted(part)) break;
            }
            usleep(100 * 1000);
        }
    }

    sync();
    int cfd2 = open("/proc/sys/vm/drop_caches", O_WRONLY);
    if (cfd2 >= 0) {
        write(cfd2, "3\n", 2);
        close(cfd2);
    }

    return 0;
}

void ui_usb_sd_init(struct ui_usb_sd *s) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->phase = UI_USB_SD_PHASE_PREPARING;
}

struct ui_usb_sd_style ui_usb_sd_style_default(void) {
    struct ui_usb_sd_style st = {
        .bg = UI_BG,
        .text = UI_TEXT,
        .dim = 0x9aa0a6,
        .hl = UI_HL,
        .box = UI_BOX,
        .border = 0x505868,
        .bar = 0x2a2418,
        .active_text = UI_BG,
    };
    return st;
}

int ui_usb_sd_is_active(const struct ui_usb_sd *s) {
    return s && s->bound;
}

int ui_usb_sd_stop(struct ui_usb_sd *s) {
    if (!s || !s->bound) return 0;
    char err[160];
    int rc = stop_backend(err, sizeof err);
    s->bound = 0;
    s->presented = 0;
    s->phase = UI_USB_SD_PHASE_DONE;
    return rc;
}

static void draw_card(struct ui *u, struct ui_usb_sd_style st, const char *a, const char *b, const char *c,
                      const char *d) {
    struct ui_paint card = {
        .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.border, .border_px = 1,
    };
    ui_box(u, 16, 76, 288, 144, card);
    if (a) ui_text(u, 28, 92, a, st.hl, 1);
    if (b) ui_text(u, 28, 114, b, st.text, 1);
    if (c) ui_text(u, 28, 132, c, st.dim, 1);
    if (d) ui_text(u, 28, 146, d, st.dim, 1);
}

static struct ui_button exit_button(struct ui_usb_sd_style st) {
    struct ui_button btn = {
        .fill = st.bar,
        .text = st.active_text,
        .border_set = 1,
        .border = st.hl,
        .border_px = 1,
        .highlighted = 1,
        .highlight = st.hl,
        .highlight_px = 2,
    };
    return btn;
}

int ui_usb_sd(struct ui *u, struct ui_usb_sd *s, struct ui_usb_sd_style st, const struct input *in) {
    if (!u || !s) return UI_USB_SD_FAILED;
    if (s->phase == UI_USB_SD_PHASE_DONE) return UI_USB_SD_DONE;
    if (s->phase == UI_USB_SD_PHASE_FAILED) return UI_USB_SD_FAILED;

    if (s->phase == UI_USB_SD_PHASE_PREPARING && s->presented) {
        char err[160];
        err[0] = 0;
        if (start_backend(err, sizeof err) == 0) {
            s->bound = 1;
            s->err[0] = 0;
            s->phase = UI_USB_SD_PHASE_ACTIVE;
        } else {
            snprintf(s->err, sizeof s->err, "%s", err[0] ? err : "Failed to start");
            s->phase = UI_USB_SD_PHASE_ERROR;
        }
        s->presented = 0;
    } else if (s->phase == UI_USB_SD_PHASE_EXITING && s->presented) {
        char err[160];
        stop_backend(err, sizeof err);
        s->bound = 0;
        s->presented = 0;
        s->phase = UI_USB_SD_PHASE_DONE;
        return UI_USB_SD_DONE;
    }

    if (in && s->phase == UI_USB_SD_PHASE_ACTIVE) {
        if (input_down(in, IN_B1) || in->encoder_press) {
            s->phase = UI_USB_SD_PHASE_EXITING;
            s->presented = 0;
        }
    } else if (in && s->phase == UI_USB_SD_PHASE_ERROR) {
        if (input_down(in, IN_B1) || in->encoder_press) {
            s->phase = UI_USB_SD_PHASE_FAILED;
            return UI_USB_SD_FAILED;
        }
    }

    ui_clear(u, st.bg);
    const char *labels[3] = {"Exit", "", ""};
    struct ui_soft_style soft = {
        .fill = st.box,
        .text = st.text,
        .active_text = st.active_text,
        .highlight = st.hl,
        .gap = 4,
        .feedback = UI_SOFT_FLASH,
        .flash_ms = 120,
    };
    ui_soft_keys(u, 8, 4, 304, 28, labels, &s->soft_active, soft, in, &s->soft);

    const char *hint = "";
    if (s->phase == UI_USB_SD_PHASE_PREPARING) hint = "preparing USB interface...";
    else if (s->phase == UI_USB_SD_PHASE_ACTIVE) hint = "connected via USB";
    else if (s->phase == UI_USB_SD_PHASE_EXITING) hint = "exiting USB storage...";
    else if (s->phase == UI_USB_SD_PHASE_ERROR) hint = "storage error";
    ui_text(u, 12, 40, "SD Card Access", st.text, 1);
    if (hint[0]) ui_text(u, 12, 56, hint, st.dim, 1);

    if (s->phase == UI_USB_SD_PHASE_ACTIVE) {
        draw_card(u, st, "USB STORAGE ACTIVE", "SD card is mounted on your PC.",
                  "Do not unplug until safely", "ejected on your computer.");
        ui_button(u, 64, 172, 192, 32, "Exit USB Mode", exit_button(st), in);
    } else if (s->phase == UI_USB_SD_PHASE_PREPARING) {
        struct ui_paint card = {
            .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.border, .border_px = 1,
        };
        ui_box(u, 16, 76, 288, 144, card);
        ui_text(u, 28, 96, "Preparing USB interface...", st.hl, 1);
        ui_text(u, 28, 120, "Unmounting local SD partitions...", st.text, 1);
        ui_text(u, 28, 140, "Please wait...", st.dim, 1);
    } else if (s->phase == UI_USB_SD_PHASE_EXITING) {
        struct ui_paint card = {
            .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.border, .border_px = 1,
        };
        ui_box(u, 16, 76, 288, 144, card);
        ui_text(u, 28, 96, "Exiting USB storage...", st.hl, 1);
        ui_text(u, 28, 120, "Restoring network & remounting...", st.text, 1);
        ui_text(u, 28, 140, "Please wait...", st.dim, 1);
    } else if (s->phase == UI_USB_SD_PHASE_ERROR) {
        struct ui_paint card = {
            .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.border, .border_px = 1,
        };
        ui_box(u, 16, 76, 288, 144, card);
        ui_text(u, 28, 92, "Could not start USB storage", st.hl, 1);
        ui_text(u, 28, 116, s->err[0] ? s->err : "Unknown error occurred", st.text, 1);
        ui_button(u, 80, 172, 160, 32, "Back", exit_button(st), in);
    }

    if (s->phase == UI_USB_SD_PHASE_PREPARING || s->phase == UI_USB_SD_PHASE_EXITING) {
        s->presented = 1;
        ui_wake(u, 1);
    }
    return UI_USB_SD_ACTIVE;
}
