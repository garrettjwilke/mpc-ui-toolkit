#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include "filebrowser.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ROW_COUNT 8
#define FONT_H 12
#define FONT_W 6

struct ui_filebrowser_style ui_filebrowser_style_default(void) {
    struct ui_filebrowser_style st = {
        .bg = UI_BG,
        .box = 0x181c24,
        .text = UI_TEXT,
        .text_dim = 0x8890a0,
        .dir_color = 0x50b0ff,
        .drive_color = 0xffb000,
        .highlight = UI_HL,
        .highlight_fill = 0x283244,
        .title = UI_HL,
        .border = 0x3e4656,
        .cut_color = 0x6e7684,
        .status_color = 0x60e080,
        .error_color = 0xff5555,
        .row_h = 21,
        .border_px = 1,
    };
    return st;
}

static void normalize_path(char *path) {
    if (!path) return;
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
        len--;
    }
}

// Buffer-safe path concatenation (avoids undefined behavior if out == dir)
static void path_combine(char *out, size_t out_sz, const char *dir, const char *file) {
    char tmp[UI_FB_MAX_PATH];
    if (!dir || !dir[0] || strcmp(dir, ".") == 0) {
        snprintf(tmp, sizeof tmp, "%s", file ? file : "");
    } else if (strcmp(dir, "/") == 0) {
        snprintf(tmp, sizeof tmp, "/%s", file ? file : "");
    } else {
        snprintf(tmp, sizeof tmp, "%s/%s", dir, file ? file : "");
    }
    snprintf(out, out_sz, "%s", tmp);
}

// Buffer-safe parent directory extraction
static void path_parent(char *out, size_t out_sz, const char *path) {
    char tmp[UI_FB_MAX_PATH];
    if (!path || !path[0] || strcmp(path, "/") == 0 || strcmp(path, ".") == 0) {
        snprintf(out, out_sz, "");
        return;
    }
    snprintf(tmp, sizeof tmp, "%s", path);
    normalize_path(tmp);
    char *last = strrchr(tmp, '/');
    if (!last || last == tmp) {
        snprintf(out, out_sz, "");
    } else {
        *last = '\0';
        snprintf(out, out_sz, "%s", tmp);
    }
}

int ui_filebrowser_is_allowed(const struct ui_filebrowser *fb, const char *path) {
    if (!fb || !path || !path[0]) return 1; // Storage Locations view is always allowed

    // Forced single-root lock
    if (fb->forced_root[0]) {
        size_t len = strlen(fb->forced_root);
        return strncmp(path, fb->forced_root, len) == 0 &&
               (path[len] == '\0' || path[len] == '/');
    }

    // Check against registered storage drives
    for (int i = 0; i < fb->drive_count; i++) {
        size_t len = strlen(fb->drives[i].path);
        if (strncmp(path, fb->drives[i].path, len) == 0 &&
            (path[len] == '\0' || path[len] == '/')) {
            return 1;
        }
    }
    return 0;
}

static int is_at_drive_root(const struct ui_filebrowser *fb, const char *path) {
    if (!path || !path[0]) return 1; // Top-level Storage Locations view
    if (fb->forced_root[0]) {
        return strcmp(path, fb->forced_root) == 0;
    }
    for (int i = 0; i < fb->drive_count; i++) {
        if (strcmp(path, fb->drives[i].path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ends_with_nocase(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t str_len = strlen(str);
    size_t suf_len = strlen(suffix);
    if (str_len < suf_len) return 0;
    return strcasecmp(str + str_len - suf_len, suffix) == 0;
}

static void format_size(uint32_t bytes, char *buf, size_t n) {
    if (bytes >= 1024 * 1024 * 1024) {
        snprintf(buf, n, "%.1fG", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        snprintf(buf, n, "%.1fM", (double)bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(buf, n, "%.1fK", (double)bytes / 1024.0);
    } else {
        snprintf(buf, n, "%uB", bytes);
    }
}

int ui_filebrowser_copy_file(const char *src, const char *dst) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0) return -1;

    struct stat st;
    mode_t mode = 0644;
    if (fstat(in_fd, &st) == 0) mode = st.st_mode & 0777;

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out_fd < 0) {
        close(in_fd);
        return -1;
    }

    char buf[16384];
    ssize_t r;
    while ((r = read(in_fd, buf, sizeof buf)) > 0) {
        char *p = buf;
        ssize_t left = r;
        while (left > 0) {
            ssize_t w = write(out_fd, p, (size_t)left);
            if (w < 0) {
                if (errno == EINTR) continue;
                close(in_fd);
                close(out_fd);
                unlink(dst);
                return -1;
            }
            left -= w;
            p += w;
        }
    }

    close(in_fd);
    close(out_fd);
    return (r < 0) ? -1 : 0;
}

int ui_filebrowser_move_file(const char *src, const char *dst) {
    if (strcmp(src, dst) == 0) return 0;
    if (rename(src, dst) == 0) return 0;
    if (errno == EXDEV) { // Cross-mount move
        if (ui_filebrowser_copy_file(src, dst) == 0) {
            unlink(src);
            return 0;
        }
    }
    return -1;
}

int ui_filebrowser_delete_path(const char *path, int is_dir) {
    return is_dir ? rmdir(path) : unlink(path);
}

int ui_filebrowser_make_dir(const char *path) {
    return mkdir(path, 0755);
}

static void make_unique_copy_name(const char *dir, const char *src_name, char *dst_path, size_t dst_sz) {
    char base[UI_FB_MAX_NAME];
    char ext[32] = {0};
    snprintf(base, sizeof base, "%s", src_name);

    char *dot = strrchr(base, '.');
    if (dot && dot != base) {
        snprintf(ext, sizeof ext, "%s", dot);
        *dot = '\0';
    }

    for (int counter = 1; counter < 1000; counter++) {
        char candidate[UI_FB_MAX_NAME];
        if (counter == 1) {
            snprintf(candidate, sizeof candidate, "%s_copy%s", base, ext);
        } else {
            snprintf(candidate, sizeof candidate, "%s_copy%d%s", base, counter, ext);
        }
        path_combine(dst_path, dst_sz, dir, candidate);
        struct stat st;
        if (stat(dst_path, &st) != 0) {
            return;
        }
    }
}

static int entry_cmp(const void *pa, const void *pb) {
    const struct ui_fb_entry *a = pa;
    const struct ui_fb_entry *b = pb;

    // Parent ".." always first
    if (a->is_parent) return -1;
    if (b->is_parent) return 1;

    // Drives before other entries
    if (a->is_drive != b->is_drive) {
        return a->is_drive ? -1 : 1;
    }

    // Directories before files
    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }

    // Alphabetical case-insensitive
    return strcasecmp(a->name, b->name);
}

static void detect_storage_drives(struct ui_filebrowser *fb, const struct ui_filebrowser_config *cfg) {
    fb->drive_count = 0;

    // If forced single root is specified, use only that root
    if (cfg && cfg->forced_root && cfg->forced_root[0]) {
        snprintf(fb->forced_root, sizeof fb->forced_root, "%s", cfg->forced_root);
        normalize_path(fb->forced_root);
        struct ui_fb_drive *d = &fb->drives[fb->drive_count++];
        snprintf(d->path, sizeof d->path, "%s", fb->forced_root);
        const char *base = strrchr(d->path, '/');
        snprintf(d->name, sizeof d->name, "%s", (base && base[1]) ? base + 1 : d->path);
        d->is_removable = 0;
        return;
    }

    // 1. External Storage (SD Card under /media/...)
    if (cfg && cfg->external_path && cfg->external_path[0]) {
        struct ui_fb_drive *d = &fb->drives[fb->drive_count++];
        snprintf(d->path, sizeof d->path, "%s", cfg->external_path);
        normalize_path(d->path);
        const char *base = strrchr(d->path, '/');
        snprintf(d->name, sizeof d->name, "SD Card (%s)", (base && base[1]) ? base + 1 : d->path);
        d->is_removable = 1;
        snprintf(fb->external_path, sizeof fb->external_path, "%s", d->path);
    } else {
        // Auto-detect mounted volume under /media
        DIR *md = opendir("/media");
        if (md) {
            struct dirent *de;
            while ((de = readdir(md)) != NULL && fb->drive_count < (UI_FB_MAX_DRIVES - 1)) {
                if (de->d_name[0] == '.') continue;
                if (strcmp(de->d_name, "az01-settings") == 0) continue;

                char full[UI_FB_MAX_PATH];
                snprintf(full, sizeof full, "/media/%s", de->d_name);
                struct stat st;
                if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
                    struct ui_fb_drive *d = &fb->drives[fb->drive_count++];
                    snprintf(d->path, sizeof d->path, "%s", full);
                    snprintf(d->name, sizeof d->name, "SD Card (%s)", de->d_name);
                    d->is_removable = 1;
                    if (!fb->external_path[0]) {
                        snprintf(fb->external_path, sizeof fb->external_path, "%s", full);
                    }
                }
            }
            closedir(md);
        }

        // If /media had no subdirectories, but /media is present
        if (fb->drive_count == 0) {
            struct stat st;
            if (stat("/media", &st) == 0 && S_ISDIR(st.st_mode)) {
                struct ui_fb_drive *d = &fb->drives[fb->drive_count++];
                snprintf(d->path, sizeof d->path, "/media");
                snprintf(d->name, sizeof d->name, "SD Card (/media)");
                d->is_removable = 1;
                snprintf(fb->external_path, sizeof fb->external_path, "/media");
            }
        }
    }

    // 2. Internal Storage (/data)
    if (cfg && cfg->internal_path && cfg->internal_path[0]) {
        struct ui_fb_drive *d = &fb->drives[fb->drive_count++];
        snprintf(d->path, sizeof d->path, "%s", cfg->internal_path);
        normalize_path(d->path);
        snprintf(d->name, sizeof d->name, "Internal (%s)", d->path);
        d->is_removable = 0;
        snprintf(fb->internal_path, sizeof fb->internal_path, "%s", d->path);
    } else {
        struct ui_fb_drive *d = &fb->drives[fb->drive_count++];
        snprintf(d->path, sizeof d->path, "/data");
        snprintf(d->name, sizeof d->name, "Internal (/data)");
        d->is_removable = 0;
        snprintf(fb->internal_path, sizeof fb->internal_path, "/data");
    }

    // Host dev fallback (if neither /media nor /data exist on host machine)
    if (fb->drive_count == 1 && fb->drives[0].path[0] == '/' && strcmp(fb->drives[0].path, "/data") == 0) {
        struct stat st;
        if (stat("/data", &st) != 0 && stat("/media", &st) != 0) {
            // Running on host/Mac: fallback to local test sandbox
            fb->drive_count = 0;
            struct ui_fb_drive *d1 = &fb->drives[fb->drive_count++];
            snprintf(d1->path, sizeof d1->path, "build/test_fb/media/MPC");
            snprintf(d1->name, sizeof d1->name, "SD Card (MPC)");
            d1->is_removable = 1;
            snprintf(fb->external_path, sizeof fb->external_path, "%s", d1->path);

            struct ui_fb_drive *d2 = &fb->drives[fb->drive_count++];
            snprintf(d2->path, sizeof d2->path, "build/test_fb/data");
            snprintf(d2->name, sizeof d2->name, "Internal (/data)");
            d2->is_removable = 0;
            snprintf(fb->internal_path, sizeof fb->internal_path, "%s", d2->path);
        }
    }
}

void ui_filebrowser_init(struct ui_filebrowser *fb, const struct ui_filebrowser_config *cfg) {
    if (!fb) return;
    memset(fb, 0, sizeof *fb);

    if (cfg) {
        fb->options = cfg->options;
        fb->extensions = cfg->extensions;
        fb->filter = cfg->filter;
        fb->user_data = cfg->user_data;

        if (cfg->title && cfg->title[0]) {
            snprintf(fb->title, sizeof fb->title, "%s", cfg->title);
        } else {
            snprintf(fb->title, sizeof fb->title, "FILE BROWSER");
        }

        if (cfg->action_label && cfg->action_label[0]) {
            snprintf(fb->action_label, sizeof fb->action_label, "%s", cfg->action_label);
        } else {
            snprintf(fb->action_label, sizeof fb->action_label, "OPEN");
        }

        if (cfg->entries && cfg->capacity > 0) {
            fb->entries = cfg->entries;
            fb->capacity = cfg->capacity;
        } else {
            fb->entries = fb->default_entries;
            fb->capacity = UI_FB_DEFAULT_CAPACITY;
        }
    } else {
        fb->options = UI_FB_PRESET_DEFAULT;
        snprintf(fb->title, sizeof fb->title, "FILE BROWSER");
        snprintf(fb->action_label, sizeof fb->action_label, "OPEN");
        fb->entries = fb->default_entries;
        fb->capacity = UI_FB_DEFAULT_CAPACITY;
    }

    fb->hide_dotfiles = (fb->options & UI_FB_OPT_HIDE_DOTFILES) ? 1 : 0;
    fb->state = UI_FB_STATE_BROWSE;

    // Detect and configure allowed storage drives
    detect_storage_drives(fb, cfg);

    // Initial path resolution
    if (cfg && cfg->initial_path && cfg->initial_path[0]) {
        char init_norm[UI_FB_MAX_PATH];
        snprintf(init_norm, sizeof init_norm, "%s", cfg->initial_path);
        normalize_path(init_norm);

        if (ui_filebrowser_is_allowed(fb, init_norm)) {
            struct stat st;
            if (stat(init_norm, &st) == 0 && S_ISDIR(st.st_mode)) {
                snprintf(fb->cwd, sizeof fb->cwd, "%s", init_norm);
            } else {
                fb->cwd[0] = '\0'; // Start at Storage Locations view
            }
        } else {
            fb->cwd[0] = '\0';
        }
    } else if (fb->forced_root[0]) {
        snprintf(fb->cwd, sizeof fb->cwd, "%s", fb->forced_root);
    } else {
        // Default: Start at Storage Locations view
        fb->cwd[0] = '\0';
    }

    ui_filebrowser_refresh(fb);
}

int ui_filebrowser_set_cwd(struct ui_filebrowser *fb, const char *path) {
    if (!fb) return -1;
    if (!path || !path[0]) {
        fb->cwd[0] = '\0';
        ui_filebrowser_refresh(fb);
        return 0;
    }

    char norm[UI_FB_MAX_PATH];
    snprintf(norm, sizeof norm, "%s", path);
    normalize_path(norm);

    if (!ui_filebrowser_is_allowed(fb, norm)) {
        snprintf(fb->status_msg, sizeof fb->status_msg, "Access restricted to storage");
        fb->status_is_error = 1;
        return -1;
    }

    struct stat st;
    if (stat(norm, &st) != 0 || !S_ISDIR(st.st_mode)) {
        snprintf(fb->status_msg, sizeof fb->status_msg, "Folder does not exist");
        fb->status_is_error = 1;
        return -1;
    }

    snprintf(fb->cwd, sizeof fb->cwd, "%s", norm);
    ui_filebrowser_refresh(fb);
    return 0;
}

void ui_filebrowser_refresh(struct ui_filebrowser *fb) {
    if (!fb || !fb->entries || fb->capacity <= 0) return;

    fb->count = 0;
    fb->cursor = 0;
    fb->scroll_offset = 0;

    // View 1: Top-level Storage Locations / Drives view
    if (fb->cwd[0] == '\0') {
        for (int i = 0; i < fb->drive_count && fb->count < fb->capacity; i++) {
            struct ui_fb_entry *e = &fb->entries[fb->count++];
            snprintf(e->name, sizeof e->name, "%s", fb->drives[i].name);
            e->size = 0;
            e->is_dir = 1;
            e->is_parent = 0;
            e->is_cut = 0;
            e->is_drive = 1;
        }
        return;
    }

    // View 2: Inside an allowed storage directory
    int at_drive_root = is_at_drive_root(fb, fb->cwd);

    // If inside a drive, add parent ".." navigation
    if (!at_drive_root || !fb->forced_root[0]) {
        struct ui_fb_entry *p = &fb->entries[fb->count++];
        snprintf(p->name, sizeof p->name, "..");
        p->size = 0;
        p->is_dir = 1;
        p->is_parent = 1;
        p->is_cut = 0;
        p->is_drive = 0;
    }

    DIR *d = opendir(fb->cwd);
    if (!d) {
        snprintf(fb->status_msg, sizeof fb->status_msg, "Cannot open folder");
        fb->status_is_error = 1;
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (fb->count >= fb->capacity) break;

        const char *name = de->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        // Dotfile filtering
        if (fb->hide_dotfiles && name[0] == '.') continue;

        char full[UI_FB_MAX_PATH];
        path_combine(full, sizeof full, fb->cwd, name);

        struct stat st;
        if (stat(full, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);

        if ((fb->options & UI_FB_OPT_DIRS_ONLY) && !is_dir) continue;
        if (fb->filter && !fb->filter(full, name, is_dir, fb->user_data)) continue;

        if (!is_dir && fb->extensions) {
            int matched = 0;
            for (int i = 0; fb->extensions[i]; i++) {
                if (ends_with_nocase(name, fb->extensions[i])) {
                    matched = 1;
                    break;
                }
            }
            if (!matched) continue;
        }

        struct ui_fb_entry *e = &fb->entries[fb->count++];
        snprintf(e->name, sizeof e->name, "%s", name);
        e->size = is_dir ? 0 : (uint32_t)st.st_size;
        e->is_dir = is_dir ? 1 : 0;
        e->is_parent = 0;
        e->is_drive = 0;
        e->is_cut = (fb->clip_op == UI_FB_CLIP_CUT && strcmp(fb->clip_path, full) == 0) ? 1 : 0;
    }
    closedir(d);

    if (fb->count > 1) {
        qsort(fb->entries, (size_t)fb->count, sizeof(struct ui_fb_entry), entry_cmp);
    }
}

int ui_filebrowser_copy(struct ui_filebrowser *fb) {
    if (!fb || fb->cwd[0] == '\0' || fb->count <= 0 || fb->cursor < 0 || fb->cursor >= fb->count) return -1;
    struct ui_fb_entry *e = &fb->entries[fb->cursor];
    if (e->is_parent || e->is_drive) return -1;

    path_combine(fb->clip_path, sizeof fb->clip_path, fb->cwd, e->name);
    snprintf(fb->clip_name, sizeof fb->clip_name, "%s", e->name);
    fb->clip_op = UI_FB_CLIP_COPY;
    fb->clip_is_dir = e->is_dir;

    snprintf(fb->status_msg, sizeof fb->status_msg, "Copied: %s", e->name);
    fb->status_is_error = 0;

    for (int i = 0; i < fb->count; i++) fb->entries[i].is_cut = 0;
    return 0;
}

int ui_filebrowser_cut(struct ui_filebrowser *fb) {
    if (!fb || fb->cwd[0] == '\0' || fb->count <= 0 || fb->cursor < 0 || fb->cursor >= fb->count) return -1;
    struct ui_fb_entry *e = &fb->entries[fb->cursor];
    if (e->is_parent || e->is_drive) return -1;

    path_combine(fb->clip_path, sizeof fb->clip_path, fb->cwd, e->name);
    snprintf(fb->clip_name, sizeof fb->clip_name, "%s", e->name);
    fb->clip_op = UI_FB_CLIP_CUT;
    fb->clip_is_dir = e->is_dir;

    snprintf(fb->status_msg, sizeof fb->status_msg, "Cut: %s", e->name);
    fb->status_is_error = 0;

    for (int i = 0; i < fb->count; i++) {
        fb->entries[i].is_cut = (i == fb->cursor);
    }
    return 0;
}

int ui_filebrowser_paste(struct ui_filebrowser *fb) {
    if (!fb || fb->cwd[0] == '\0' || fb->clip_op == UI_FB_CLIP_NONE || !fb->clip_path[0]) return -1;

    char dst_path[UI_FB_MAX_PATH];
    if (fb->clip_op == UI_FB_CLIP_COPY) {
        make_unique_copy_name(fb->cwd, fb->clip_name, dst_path, sizeof dst_path);
        if (ui_filebrowser_copy_file(fb->clip_path, dst_path) != 0) {
            snprintf(fb->status_msg, sizeof fb->status_msg, "Paste copy failed");
            fb->status_is_error = 1;
            return -1;
        }
        snprintf(fb->status_msg, sizeof fb->status_msg, "Pasted copy: %s", fb->clip_name);
        fb->status_is_error = 0;
    } else if (fb->clip_op == UI_FB_CLIP_CUT) {
        path_combine(dst_path, sizeof dst_path, fb->cwd, fb->clip_name);
        if (ui_filebrowser_move_file(fb->clip_path, dst_path) != 0) {
            snprintf(fb->status_msg, sizeof fb->status_msg, "Move failed");
            fb->status_is_error = 1;
            return -1;
        }
        fb->clip_op = UI_FB_CLIP_NONE;
        fb->clip_path[0] = '\0';
        fb->clip_name[0] = '\0';
        snprintf(fb->status_msg, sizeof fb->status_msg, "Moved: %s", dst_path);
        fb->status_is_error = 0;
    }

    ui_filebrowser_refresh(fb);
    return 0;
}

int ui_filebrowser_delete(struct ui_filebrowser *fb) {
    if (!fb || fb->cwd[0] == '\0' || fb->count <= 0 || fb->cursor < 0 || fb->cursor >= fb->count) return -1;
    struct ui_fb_entry *e = &fb->entries[fb->cursor];
    if (e->is_parent || e->is_drive) return -1;

    char full[UI_FB_MAX_PATH];
    path_combine(full, sizeof full, fb->cwd, e->name);

    if (ui_filebrowser_delete_path(full, e->is_dir) != 0) {
        snprintf(fb->status_msg, sizeof fb->status_msg, e->is_dir ? "Cannot remove dir" : "Delete failed");
        fb->status_is_error = 1;
        return -1;
    }

    snprintf(fb->status_msg, sizeof fb->status_msg, "Deleted: %s", e->name);
    fb->status_is_error = 0;
    ui_filebrowser_refresh(fb);
    return 0;
}

// Action menu definitions
enum {
    ACT_OPEN = 1,
    ACT_RENAME,
    ACT_COPY,
    ACT_CUT,
    ACT_PASTE,
    ACT_DELETE,
    ACT_MKDIR,
    ACT_TOGGLE_DOT,
    ACT_CANCEL
};

struct menu_act {
    int id;
    const char *label;
};

static int build_actions(const struct ui_filebrowser *fb, struct menu_act *acts, int max_acts) {
    int n = 0;
    const struct ui_fb_entry *e = (fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count)
                                  ? &fb->entries[fb->cursor] : NULL;
    int valid_item = (e && !e->is_parent && !e->is_drive && fb->cwd[0] != '\0');

    if (n < max_acts) acts[n++] = (struct menu_act){ACT_OPEN, fb->action_label[0] ? fb->action_label : "Open / Select"};

    if (valid_item && (fb->options & UI_FB_OPT_RENAME) && n < max_acts)
        acts[n++] = (struct menu_act){ACT_RENAME, "Rename"};

    if (valid_item && (fb->options & UI_FB_OPT_COPY) && n < max_acts)
        acts[n++] = (struct menu_act){ACT_COPY, "Copy"};

    if (valid_item && (fb->options & UI_FB_OPT_MOVE) && n < max_acts)
        acts[n++] = (struct menu_act){ACT_CUT, "Move (Cut)"};

    if (fb->cwd[0] != '\0' && (fb->options & UI_FB_OPT_PASTE) && fb->clip_op != UI_FB_CLIP_NONE && n < max_acts)
        acts[n++] = (struct menu_act){ACT_PASTE, "Paste"};

    if (valid_item && (fb->options & UI_FB_OPT_DELETE) && n < max_acts)
        acts[n++] = (struct menu_act){ACT_DELETE, "Delete"};

    if (fb->cwd[0] != '\0' && (fb->options & UI_FB_OPT_MKDIR) && n < max_acts)
        acts[n++] = (struct menu_act){ACT_MKDIR, "New Folder"};

    if (fb->cwd[0] != '\0' && (fb->options & UI_FB_OPT_TOGGLE_DOTFILES) && n < max_acts)
        acts[n++] = (struct menu_act){ACT_TOGGLE_DOT, fb->hide_dotfiles ? "Show Hidden Files" : "Hide Hidden Files"};

    if (n < max_acts) acts[n++] = (struct menu_act){ACT_CANCEL, "Cancel"};
    return n;
}

static void draw_top_soft_keys(struct ui *u, int shift, const struct ui_filebrowser *fb,
                               struct ui_filebrowser_style st) {
    int at_top_level = (fb->cwd[0] == '\0');

    const char *b1_text = (fb->options & UI_FB_OPT_BACK) ? "BACK"
        : (shift ? ((!at_top_level && (fb->options & UI_FB_OPT_MKDIR)) ? "NEW FOLDER" : "")
                 : (at_top_level ? "EXIT" : "UP (..)"));
    const char *b2_text = shift ? ((!at_top_level && (fb->options & UI_FB_OPT_PASTE) && fb->clip_op != UI_FB_CLIP_NONE) ? "PASTE" : (at_top_level ? "" : "ACTIONS"))
                                : (at_top_level ? "" : (fb->clip_op != UI_FB_CLIP_NONE ? "ACTIONS (*)" : "ACTIONS"));
    const char *b3_text = shift ? ((!at_top_level && (fb->options & UI_FB_OPT_TOGGLE_DOTFILES)) ? (fb->hide_dotfiles ? "SHOW DOT" : "HIDE DOT") : "")
                                : (at_top_level ? "OPEN" : (fb->action_label[0] ? fb->action_label : "OPEN"));

    const char *labels[3] = {b1_text, b2_text, b3_text};
    int sw = (UI_W - 16) / 3;

    for (int i = 0; i < 3; i++) {
        int sx = 4 + i * (sw + 4);
        int sy = 2;
        int sh = 22;

        struct ui_paint p = {
            .fill_set = 1,
            .fill = (shift && labels[i][0]) ? 0x223040 : st.box,
            .border_set = 1,
            .border = st.border,
            .border_px = 1
        };
        ui_box(u, sx, sy, sw, sh, p);

        if (labels[i][0]) {
            int text_w = (int)strlen(labels[i]) * FONT_W;
            int tx = sx + (sw - text_w) / 2;
            int ty = sy + (sh - FONT_H) / 2;
            ui_text(u, tx, ty, labels[i], shift ? st.highlight : st.text, 1);
        }
    }
}

static void navigate_up(struct ui_filebrowser *fb) {
    if (fb->cwd[0] == '\0') return;

    if (is_at_drive_root(fb, fb->cwd)) {
        if (fb->forced_root[0]) return;

        // Remember which drive we were in
        int drive_idx = 0;
        for (int i = 0; i < fb->drive_count; i++) {
            if (strcmp(fb->cwd, fb->drives[i].path) == 0) {
                drive_idx = i;
                break;
            }
        }
        fb->cwd[0] = '\0';
        ui_filebrowser_refresh(fb);
        fb->cursor = drive_idx;
    } else {
        char prev[UI_FB_MAX_NAME] = {0};
        const char *last = strrchr(fb->cwd, '/');
        if (last && last[1]) snprintf(prev, sizeof prev, "%s", last + 1);

        path_parent(fb->cwd, sizeof fb->cwd, fb->cwd);
        ui_filebrowser_refresh(fb);

        if (prev[0]) {
            for (int i = 0; i < fb->count; i++) {
                if (strcmp(fb->entries[i].name, prev) == 0) {
                    fb->cursor = i;
                    break;
                }
            }
        }
    }
}

int ui_filebrowser(struct ui *u, int x, int y, int w, int h, struct ui_filebrowser *fb,
                   struct ui_filebrowser_style st, const struct input *in) {
    if (!u || !fb) return UI_FB_ACTIVE;
    (void)x; (void)y; (void)w; (void)h;

    int shift = in ? in->shift : 0;

    // Handle Rename modal
    if (fb->state == UI_FB_STATE_RENAME) {
        struct ui_text_editor_style ed_st = ui_text_editor_style_default();
        int res = ui_text_editor(u, 0, 0, UI_W, UI_H, &fb->editor, ed_st, in);
        if (res == UI_TEXT_OK) {
            if (fb->edit_buf[0] && fb->cwd[0] != '\0') {
                const struct ui_fb_entry *e = &fb->entries[fb->cursor];
                char old_path[UI_FB_MAX_PATH];
                char new_path[UI_FB_MAX_PATH];
                path_combine(old_path, sizeof old_path, fb->cwd, e->name);
                path_combine(new_path, sizeof new_path, fb->cwd, fb->edit_buf);

                if (rename(old_path, new_path) == 0) {
                    snprintf(fb->status_msg, sizeof fb->status_msg, "Renamed: %s", fb->edit_buf);
                    fb->status_is_error = 0;
                    ui_filebrowser_refresh(fb);
                    for (int i = 0; i < fb->count; i++) {
                        if (strcmp(fb->entries[i].name, fb->edit_buf) == 0) {
                            fb->cursor = i;
                            break;
                        }
                    }
                } else {
                    snprintf(fb->status_msg, sizeof fb->status_msg, "Rename failed");
                    fb->status_is_error = 1;
                }
            }
            fb->state = UI_FB_STATE_BROWSE;
        } else if (res == UI_TEXT_CANCEL) {
            fb->state = UI_FB_STATE_BROWSE;
        }
        return UI_FB_ACTIVE;
    }

    // Handle Mkdir modal
    if (fb->state == UI_FB_STATE_MKDIR) {
        struct ui_text_editor_style ed_st = ui_text_editor_style_default();
        int res = ui_text_editor(u, 0, 0, UI_W, UI_H, &fb->editor, ed_st, in);
        if (res == UI_TEXT_OK) {
            if (fb->edit_buf[0] && fb->cwd[0] != '\0') {
                char new_path[UI_FB_MAX_PATH];
                path_combine(new_path, sizeof new_path, fb->cwd, fb->edit_buf);

                if (ui_filebrowser_make_dir(new_path) == 0) {
                    snprintf(fb->status_msg, sizeof fb->status_msg, "Created: %s", fb->edit_buf);
                    fb->status_is_error = 0;
                    ui_filebrowser_refresh(fb);
                    for (int i = 0; i < fb->count; i++) {
                        if (strcmp(fb->entries[i].name, fb->edit_buf) == 0) {
                            fb->cursor = i;
                            break;
                        }
                    }
                } else {
                    snprintf(fb->status_msg, sizeof fb->status_msg, "Folder creation failed");
                    fb->status_is_error = 1;
                }
            }
            fb->state = UI_FB_STATE_BROWSE;
        } else if (res == UI_TEXT_CANCEL) {
            fb->state = UI_FB_STATE_BROWSE;
        }
        return UI_FB_ACTIVE;
    }

    // Handle Delete Confirmation Modal
    if (fb->state == UI_FB_STATE_CONFIRM_DELETE) {
        ui_clear(u, st.bg);
        const struct ui_fb_entry *e = &fb->entries[fb->cursor];

        int mw = 272, mh = 130;
        int mx = (UI_W - mw) / 2;
        int my = (UI_H - mh) / 2;

        ui_box(u, mx, my, mw, mh, (struct ui_paint){
            .fill_set = 1, .fill = 0x201014,
            .border_set = 1, .border = st.error_color, .border_px = 2
        });

        ui_text(u, mx + 16, my + 14, "CONFIRM DELETE", st.error_color, 1);

        char prompt[64];
        snprintf(prompt, sizeof prompt, "Delete %s \"%s\"?", e->is_dir ? "directory" : "file", e->name);
        ui_text(u, mx + 16, my + 40, prompt, st.text, 1);
        ui_text(u, mx + 16, my + 60, "This action cannot be undone.", st.text_dim, 1);

        ui_box(u, mx + 16, my + 90, 110, 26, (struct ui_paint){
            .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.border, .border_px = 1
        });
        ui_text(u, mx + 32, my + 98, "B1: CANCEL", st.text, 1);

        ui_box(u, mx + 146, my + 90, 110, 26, (struct ui_paint){
            .fill_set = 1, .fill = 0x4a1818, .border_set = 1, .border = st.error_color, .border_px = 1
        });
        ui_text(u, mx + 162, my + 98, "B3: DELETE", 0xffffff, 1);

        if (in) {
            if (input_down(in, IN_B1)) {
                fb->state = UI_FB_STATE_BROWSE;
            } else if (input_down(in, IN_B3) || in->encoder_press) {
                ui_filebrowser_delete(fb);
                fb->state = UI_FB_STATE_BROWSE;
            }
        }
        return UI_FB_ACTIVE;
    }

    // Handle Action Menu Modal
    if (fb->state == UI_FB_STATE_MENU) {
        ui_clear(u, st.bg);
        draw_top_soft_keys(u, shift, fb, st);

        struct menu_act acts[10];
        int nacts = build_actions(fb, acts, 10);

        if (in) {
            if (in->encoder) {
                fb->menu_cursor += in->encoder;
                if (fb->menu_cursor < 0) fb->menu_cursor = nacts - 1;
                if (fb->menu_cursor >= nacts) fb->menu_cursor = 0;
            }
            if (input_down(in, IN_B1)) {
                fb->state = UI_FB_STATE_BROWSE;
                return UI_FB_ACTIVE;
            }
            for (int p = 0; p < nacts && p < 16; p++) {
                if (in->pad_hit[p] >= 0) {
                    fb->menu_cursor = p;
                    in = NULL;
                    break;
                }
            }
        }

        int mw = 220, mh = 22 + nacts * 20;
        if (mh > 190) mh = 190;
        int mx = (UI_W - mw) / 2;
        int my = 28 + (190 - mh) / 2;

        ui_box(u, mx, my, mw, mh, (struct ui_paint){
            .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.highlight, .border_px = 1
        });
        ui_text(u, mx + 10, my + 6, "FILE ACTIONS", st.highlight, 1);

        int row_y = my + 20;
        int chosen_act = 0;

        for (int i = 0; i < nacts; i++) {
            int is_sel = (i == fb->menu_cursor);
            if (is_sel) {
                ui_box(u, mx + 4, row_y + i * 20, mw - 8, 18, (struct ui_paint){
                    .fill_set = 1, .fill = st.highlight_fill, .border_set = 1, .border = st.highlight, .border_px = 1
                });
            }

            char item_lbl[48];
            if (i < 16) {
                snprintf(item_lbl, sizeof item_lbl, "%d. %s", i + 1, acts[i].label);
            } else {
                snprintf(item_lbl, sizeof item_lbl, "   %s", acts[i].label);
            }
            ui_text(u, mx + 8, row_y + i * 20 + 4, item_lbl, is_sel ? st.highlight : st.text, 1);
        }

        for (int p = 0; p < nacts && p < 16; p++) {
            if (p == fb->menu_cursor) {
                ui_led(u, p, 255, 176, 0);
            } else {
                ui_led(u, p, 40, 50, 70);
            }
        }

        if (in && (in->encoder_press || input_down(in, IN_B3))) {
            chosen_act = acts[fb->menu_cursor].id;
        } else if (!in) {
            chosen_act = acts[fb->menu_cursor].id;
        }

        if (chosen_act > 0) {
            fb->state = UI_FB_STATE_BROWSE;
            switch (chosen_act) {
                case ACT_OPEN:
                    if (fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count) {
                        struct ui_fb_entry *e = &fb->entries[fb->cursor];
                        if (e->is_drive) {
                            snprintf(fb->cwd, sizeof fb->cwd, "%s", fb->drives[fb->cursor].path);
                            ui_filebrowser_refresh(fb);
                        } else if (e->is_parent) {
                            navigate_up(fb);
                        } else if (e->is_dir) {
                            path_combine(fb->cwd, sizeof fb->cwd, fb->cwd, e->name);
                            ui_filebrowser_refresh(fb);
                        } else {
                            path_combine(fb->selected_path, sizeof fb->selected_path, fb->cwd, e->name);
                            snprintf(fb->selected_name, sizeof fb->selected_name, "%s", e->name);
                            fb->selected_is_dir = 0;
                            return UI_FB_SELECT;
                        }
                    }
                    break;
                case ACT_RENAME:
                    if (fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count) {
                        struct ui_fb_entry *e = &fb->entries[fb->cursor];
                        snprintf(fb->edit_buf, sizeof fb->edit_buf, "%s", e->name);
                        ui_text_editor_init(&fb->editor, fb->edit_buf, sizeof fb->edit_buf, "RENAME ITEM");
                        fb->state = UI_FB_STATE_RENAME;
                    }
                    break;
                case ACT_COPY:
                    ui_filebrowser_copy(fb);
                    break;
                case ACT_CUT:
                    ui_filebrowser_cut(fb);
                    break;
                case ACT_PASTE:
                    ui_filebrowser_paste(fb);
                    break;
                case ACT_DELETE:
                    if (fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count) {
                        if (fb->options & UI_FB_OPT_CONFIRM_DELETE) {
                            fb->state = UI_FB_STATE_CONFIRM_DELETE;
                        } else {
                            ui_filebrowser_delete(fb);
                        }
                    }
                    break;
                case ACT_MKDIR:
                    snprintf(fb->edit_buf, sizeof fb->edit_buf, "New_Folder");
                    ui_text_editor_init(&fb->editor, fb->edit_buf, sizeof fb->edit_buf, "NEW FOLDER NAME");
                    fb->state = UI_FB_STATE_MKDIR;
                    break;
                case ACT_TOGGLE_DOT:
                    fb->hide_dotfiles = !fb->hide_dotfiles;
                    snprintf(fb->status_msg, sizeof fb->status_msg,
                             fb->hide_dotfiles ? "Hidden files: OFF" : "Hidden files: ON");
                    fb->status_is_error = 0;
                    ui_filebrowser_refresh(fb);
                    break;
                case ACT_CANCEL:
                default:
                    break;
            }
        }
        return UI_FB_ACTIVE;
    }

    // Normal Browsing State
    if (in) {
        if (in->encoder && fb->count > 0) {
            fb->cursor += in->encoder;
            if (fb->cursor < 0) fb->cursor = 0;
            if (fb->cursor >= fb->count) fb->cursor = fb->count - 1;
        }

        if (input_down(in, IN_MINUS) && fb->cursor > 0) fb->cursor--;
        if (input_down(in, IN_PLUS) && fb->cursor + 1 < fb->count) fb->cursor++;

        // Soft Key B1: BACK, or UP / EXIT
        if (input_down(in, IN_B1)) {
            if (fb->options & UI_FB_OPT_BACK) {
                return UI_FB_CANCEL;
            }
            if (shift) {
                if (fb->cwd[0] != '\0' && (fb->options & UI_FB_OPT_MKDIR)) {
                    snprintf(fb->edit_buf, sizeof fb->edit_buf, "New_Folder");
                    ui_text_editor_init(&fb->editor, fb->edit_buf, sizeof fb->edit_buf, "NEW FOLDER NAME");
                    fb->state = UI_FB_STATE_MKDIR;
                }
            } else {
                if (fb->cwd[0] == '\0') {
                    return UI_FB_CANCEL;
                } else if (is_at_drive_root(fb, fb->cwd) && fb->forced_root[0]) {
                    return UI_FB_CANCEL;
                } else {
                    navigate_up(fb);
                }
            }
        }

        // Soft Key B2: ACTIONS Menu (or Shift: PASTE)
        if (input_down(in, IN_B2) && fb->cwd[0] != '\0') {
            if (shift && (fb->options & UI_FB_OPT_PASTE) && fb->clip_op != UI_FB_CLIP_NONE) {
                ui_filebrowser_paste(fb);
            } else {
                fb->menu_cursor = 0;
                fb->state = UI_FB_STATE_MENU;
            }
        }

        // Soft Key B3: OPEN / SELECT (or Shift: TOGGLE DOTFILES)
        if (input_down(in, IN_B3)) {
            if (shift) {
                if (fb->cwd[0] != '\0' && (fb->options & UI_FB_OPT_TOGGLE_DOTFILES)) {
                    fb->hide_dotfiles = !fb->hide_dotfiles;
                    snprintf(fb->status_msg, sizeof fb->status_msg,
                             fb->hide_dotfiles ? "Hidden files: OFF" : "Hidden files: ON");
                    fb->status_is_error = 0;
                    ui_filebrowser_refresh(fb);
                }
            } else {
                if ((fb->options & UI_FB_OPT_SELECT_DIR) && fb->cwd[0] != '\0') {
                    snprintf(fb->selected_path, sizeof fb->selected_path, "%s", fb->cwd);
                    const char *slash = strrchr(fb->cwd, '/');
                    const char *name = (slash && slash[1]) ? slash + 1 : fb->cwd;
                    snprintf(fb->selected_name, sizeof fb->selected_name, "%s", name);
                    fb->selected_is_dir = 1;
                    return UI_FB_SELECT;
                }
                if (fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count) {
                    struct ui_fb_entry *e = &fb->entries[fb->cursor];
                    if (e->is_drive) {
                        snprintf(fb->cwd, sizeof fb->cwd, "%s", fb->drives[fb->cursor].path);
                        ui_filebrowser_refresh(fb);
                    } else if (e->is_parent) {
                        navigate_up(fb);
                    } else if (e->is_dir) {
                        path_combine(fb->cwd, sizeof fb->cwd, fb->cwd, e->name);
                        ui_filebrowser_refresh(fb);
                    } else {
                        path_combine(fb->selected_path, sizeof fb->selected_path, fb->cwd, e->name);
                        snprintf(fb->selected_name, sizeof fb->selected_name, "%s", e->name);
                        fb->selected_is_dir = 0;
                        return UI_FB_SELECT;
                    }
                }
            }
        }

        // Encoder Press: Enter folder or Select file
        if (in->encoder_press && fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count) {
            struct ui_fb_entry *e = &fb->entries[fb->cursor];
            if (e->is_drive) {
                snprintf(fb->cwd, sizeof fb->cwd, "%s", fb->drives[fb->cursor].path);
                ui_filebrowser_refresh(fb);
            } else if (e->is_parent) {
                navigate_up(fb);
            } else if (e->is_dir) {
                path_combine(fb->cwd, sizeof fb->cwd, fb->cwd, e->name);
                ui_filebrowser_refresh(fb);
            } else {
                path_combine(fb->selected_path, sizeof fb->selected_path, fb->cwd, e->name);
                snprintf(fb->selected_name, sizeof fb->selected_name, "%s", e->name);
                fb->selected_is_dir = 0;
                return UI_FB_SELECT;
            }
        }
    }

    if (fb->cursor < fb->scroll_offset) {
        fb->scroll_offset = fb->cursor;
    }
    if (fb->cursor >= fb->scroll_offset + ROW_COUNT) {
        fb->scroll_offset = fb->cursor - ROW_COUNT + 1;
    }

    // DRAWING
    ui_clear(u, st.bg);
    draw_top_soft_keys(u, shift, fb, st);

    // Current Path / Header Bar (y = 26, h = 18)
    ui_box(u, 4, 26, UI_W - 8, 18, (struct ui_paint){
        .fill_set = 1, .fill = 0x141820, .border_set = 1, .border = st.border, .border_px = 1
    });

    char path_disp[64];
    if (fb->cwd[0] == '\0') {
        snprintf(path_disp, sizeof path_disp, "STORAGE LOCATIONS");
    } else {
        size_t cwd_len = strlen(fb->cwd);
        if (cwd_len > 32) {
            snprintf(path_disp, sizeof path_disp, "...%s", fb->cwd + cwd_len - 29);
        } else {
            snprintf(path_disp, sizeof path_disp, "%s", fb->cwd);
        }
    }
    ui_text(u, 8, 30, path_disp, (fb->cwd[0] == '\0') ? st.title : st.dir_color, 1);

    char count_disp[24];
    if (fb->cwd[0] == '\0') {
        snprintf(count_disp, sizeof count_disp, "%d drives", fb->count);
    } else {
        snprintf(count_disp, sizeof count_disp, "%d items", fb->count);
    }
    int count_w = (int)strlen(count_disp) * FONT_W;
    ui_text(u, UI_W - 12 - count_w, 30, count_disp, st.text_dim, 1);

    // File List Area (y = 46, h = 168)
    int list_y = 46;
    int list_w = UI_W - 16;
    int list_h = ROW_COUNT * st.row_h;

    if (fb->count == 0) {
        ui_box(u, 16, list_y + 30, UI_W - 32, 60, (struct ui_paint){
            .fill_set = 1, .fill = st.box, .border_set = 1, .border = st.border, .border_px = 1
        });
        ui_text(u, 30, list_y + 45, "Directory is empty.", st.text_dim, 1);
        ui_text(u, 30, list_y + 65, "Press Shift+B1 to create a folder.", st.highlight, 1);
    } else {
        for (int r = 0; r < ROW_COUNT; r++) {
            int idx = fb->scroll_offset + r;
            if (idx >= fb->count) break;

            const struct ui_fb_entry *e = &fb->entries[idx];
            int is_cursor = (idx == fb->cursor);
            int ry = list_y + r * st.row_h;

            if (is_cursor) {
                ui_box(u, 4, ry, list_w, st.row_h - 1, (struct ui_paint){
                    .fill_set = 1, .fill = st.highlight_fill,
                    .border_set = 1, .border = st.highlight, .border_px = 1
                });
            } else {
                ui_box(u, 4, ry, list_w, st.row_h - 1, (struct ui_paint){
                    .fill_set = 1, .fill = (r % 2 == 0) ? 0x12141a : 0x161822,
                    .border_set = 0
                });
            }

            int text_y = ry + (st.row_h - FONT_H) / 2;

            if (e->is_drive) {
                int is_removable = fb->drives[idx].is_removable;
                uint32_t tag_color = is_removable ? st.drive_color : 0x60e080;
                ui_text(u, 8, text_y, is_removable ? "[SD]" : "[INT]", tag_color, 1);
                ui_text(u, 44, text_y, e->name, is_cursor ? st.highlight : st.text, 1);
            } else if (e->is_parent) {
                int at_drive_root = is_at_drive_root(fb, fb->cwd);
                ui_text(u, 8, text_y, "[..]", st.dir_color, 1);
                if (at_drive_root) {
                    ui_text(u, 38, text_y, "Storage Locations", is_cursor ? st.highlight : st.text_dim, 1);
                } else {
                    ui_text(u, 38, text_y, "Parent Directory", is_cursor ? st.highlight : st.text_dim, 1);
                }
            } else if (e->is_dir) {
                ui_text(u, 8, text_y, "[DIR]", st.dir_color, 1);
                ui_text(u, 44, text_y, e->name, is_cursor ? st.highlight : st.dir_color, 1);
            } else {
                uint32_t color = e->is_cut ? st.cut_color : (is_cursor ? st.highlight : st.text);
                if (e->is_cut) {
                    ui_text(u, 8, text_y, "[CUT]", st.cut_color, 1);
                    ui_text(u, 44, text_y, e->name, color, 1);
                } else {
                    ui_text(u, 8, text_y, " * ", st.text_dim, 1);
                    ui_text(u, 30, text_y, e->name, color, 1);
                }

                if (fb->options & UI_FB_OPT_SHOW_SIZE) {
                    char sz_buf[16];
                    format_size(e->size, sz_buf, sizeof sz_buf);
                    int sz_w = (int)strlen(sz_buf) * FONT_W;
                    ui_text(u, list_w - 6 - sz_w, text_y, sz_buf, st.text_dim, 1);
                }
            }
        }

        if (fb->count > ROW_COUNT) {
            int sb_x = UI_W - 8;
            int sb_y = list_y;
            int sb_h = list_h;
            ui_box(u, sb_x, sb_y, 4, sb_h, (struct ui_paint){
                .fill_set = 1, .fill = 0x222632, .border_set = 0
            });

            int thumb_h = (ROW_COUNT * sb_h) / fb->count;
            if (thumb_h < 8) thumb_h = 8;
            int thumb_y = sb_y + (fb->scroll_offset * (sb_h - thumb_h)) / (fb->count - ROW_COUNT);
            ui_box(u, sb_x, thumb_y, 4, thumb_h, (struct ui_paint){
                .fill_set = 1, .fill = st.highlight, .border_set = 0
            });
        }
    }

    // Bottom Status / Footer Bar (y = 218, h = 20)
    ui_box(u, 4, 218, UI_W - 8, 20, (struct ui_paint){
        .fill_set = 1, .fill = 0x141820, .border_set = 1, .border = st.border, .border_px = 1
    });

    if (fb->status_msg[0]) {
        ui_text(u, 8, 222, fb->status_msg, fb->status_is_error ? st.error_color : st.status_color, 1);
    } else {
        if (fb->cwd[0] == '\0') {
            ui_text(u, 8, 222, "Click Encoder or B3 to open storage drive", st.text_dim, 1);
        } else if (fb->count > 0 && fb->cursor >= 0 && fb->cursor < fb->count) {
            const struct ui_fb_entry *e = &fb->entries[fb->cursor];
            char info[64];
            if (e->is_parent) {
                int at_drive_root = is_at_drive_root(fb, fb->cwd);
                snprintf(info, sizeof info, at_drive_root ? "Return to Storage Locations" : "Go to parent folder");
            } else if (e->is_dir) {
                snprintf(info, sizeof info, "Folder: %s", e->name);
            } else {
                char sz_buf[16];
                format_size(e->size, sz_buf, sizeof sz_buf);
                snprintf(info, sizeof info, "%s (%s)", e->name, sz_buf);
            }
            ui_text(u, 8, 222, info, st.text_dim, 1);
        } else {
            ui_text(u, 8, 222, "B2: Actions  Shift: Extra Options", st.text_dim, 1);
        }
    }

    return UI_FB_ACTIVE;
}
