// Reusable, feature-rich file browser component for MPC Sample applications.
// Enforces storage sandboxing: restricts browsing to external SD Card (/media/...)
// and internal storage (/data), never exposing root (/), /bin, /etc, etc.
// Provides copy, paste, move (cut), rename, delete, mkdir, and dotfile filtering.
#pragma once

#include "ui.h"
#include "input.h"
#include "widgets.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_FB_MAX_NAME 128
#define UI_FB_MAX_PATH 512
#define UI_FB_MAX_DRIVES 8
#define UI_FB_DEFAULT_CAPACITY 256

// Options bitmask: apps can mix and match any combination of features
enum {
    UI_FB_OPT_COPY            = 1 << 0, // Enable Copy file/directory
    UI_FB_OPT_PASTE           = 1 << 1, // Enable Paste
    UI_FB_OPT_MOVE            = 1 << 2, // Enable Move (Cut)
    UI_FB_OPT_RENAME          = 1 << 3, // Enable Rename (via text editor modal)
    UI_FB_OPT_DELETE          = 1 << 4, // Enable Delete
    UI_FB_OPT_MKDIR           = 1 << 5, // Enable New Folder creation
    UI_FB_OPT_HIDE_DOTFILES    = 1 << 6, // Hide files/directories starting with '.'
    UI_FB_OPT_TOGGLE_DOTFILES = 1 << 7, // Allow toggling hidden files (via menu/Shift)
    UI_FB_OPT_DIRS_ONLY       = 1 << 8, // Show and allow selecting directories only
    UI_FB_OPT_SELECT_DIR      = 1 << 9, // Allow picking current directory as selection
    UI_FB_OPT_SHOW_SIZE       = 1 << 10,// Show file sizes in row
    UI_FB_OPT_CONFIRM_DELETE  = 1 << 11, // Show confirmation dialog before delete
    UI_FB_OPT_BACK            = 1 << 12  // B1 is always Back and leaves from any folder
};

// Convenience presets
#define UI_FB_PRESET_READONLY (UI_FB_OPT_HIDE_DOTFILES | UI_FB_OPT_SHOW_SIZE)
#define UI_FB_PRESET_DEFAULT  (UI_FB_OPT_HIDE_DOTFILES | UI_FB_OPT_TOGGLE_DOTFILES | \
                               UI_FB_OPT_SHOW_SIZE | UI_FB_OPT_CONFIRM_DELETE)
#define UI_FB_PRESET_FULL     (UI_FB_OPT_COPY | UI_FB_OPT_PASTE | UI_FB_OPT_MOVE | \
                               UI_FB_OPT_RENAME | UI_FB_OPT_DELETE | UI_FB_OPT_MKDIR | \
                               UI_FB_OPT_HIDE_DOTFILES | UI_FB_OPT_TOGGLE_DOTFILES | \
                               UI_FB_OPT_SHOW_SIZE | UI_FB_OPT_CONFIRM_DELETE)

// Return status codes from ui_filebrowser(...)
enum {
    UI_FB_ACTIVE =  0, // Browsing normally
    UI_FB_SELECT =  1, // User confirmed/selected a file or directory
    UI_FB_CANCEL = -1  // User left (Back, or B1 at the storage list)
};

// Internal modal states
enum {
    UI_FB_STATE_BROWSE = 0,
    UI_FB_STATE_MENU,
    UI_FB_STATE_RENAME,
    UI_FB_STATE_MKDIR,
    UI_FB_STATE_CONFIRM_DELETE
};

// Clipboard operations
enum {
    UI_FB_CLIP_NONE = 0,
    UI_FB_CLIP_COPY = 1,
    UI_FB_CLIP_CUT  = 2
};

// Single file/directory entry
struct ui_fb_entry {
    char name[UI_FB_MAX_NAME];
    uint32_t size;     // File size in bytes (0 for directories)
    uint8_t is_dir;    // 1 if directory, 0 if regular file
    uint8_t is_parent; // 1 if ".." parent entry
    uint8_t is_cut;    // 1 if marked for Move (Cut)
    uint8_t is_drive;  // 1 if top-level storage location / drive
};

// Storage location representation (External SD card or Internal)
struct ui_fb_drive {
    char name[64];             // Display label, e.g. "SD Card (MPC)" or "Internal (/data)"
    char path[UI_FB_MAX_PATH]; // Real mount point path, e.g. "/media/MPC" or "/data"
    int is_removable;          // 1 for SD/USB, 0 for internal
};

// Custom filter callback: return 1 to include, 0 to filter out
typedef int (*ui_fb_filter_fn)(const char *path, const char *name, int is_dir, void *user_data);

// Configuration passed to ui_filebrowser_init
struct ui_filebrowser_config {
    const char *initial_path;   // Optional starting directory (NULL starts at Storage Locations view)
    const char *internal_path;  // Internal storage root (NULL defaults to "/data")
    const char *external_path;  // External storage root (NULL auto-detects /media/<SDCARD>)
    const char *forced_root;    // If set, locks browsing strictly to this directory and its subfolders
    const char *title;          // Header title (e.g. "SELECT SAMPLE", NULL for "FILE BROWSER")
    const char *action_label;   // B3 label on file (e.g. "OPEN", "LOAD", NULL for "OPEN")
    uint32_t options;           // UI_FB_OPT_* bitmask (e.g. UI_FB_PRESET_FULL)
    const char **extensions;    // NULL-terminated allowed extensions (e.g. {".wav", NULL})
    ui_fb_filter_fn filter;     // Optional custom filter function
    void *user_data;            // Passed to filter function
    struct ui_fb_entry *entries;// Custom storage buffer (NULL uses default 256 entries)
    int capacity;               // Capacity of custom entries buffer (0 uses default 256)
};

// Visual styling matching mpc-ui-toolkit
struct ui_filebrowser_style {
    uint32_t bg;             // Background fill (UI_BG)
    uint32_t box;            // Panel/row background (UI_BOX)
    uint32_t text;           // File text color (UI_TEXT)
    uint32_t text_dim;       // Muted / size text color (0x8890a0)
    uint32_t dir_color;      // Directory text/icon color (0x50b0ff)
    uint32_t drive_color;    // Storage location color (0xffb000)
    uint32_t highlight;      // Selection highlight / border (UI_HL)
    uint32_t highlight_fill; // Active row fill (0x283244)
    uint32_t title;          // Header title color (UI_HL)
    uint32_t border;         // Border color (0x3e4656)
    uint32_t cut_color;      // Dimmed text color for cut items (0x707888)
    uint32_t status_color;   // Success status banner (0x60e080)
    uint32_t error_color;    // Error status banner (0xff5555)
    int row_h;               // Row height in pixels (default 21)
    int border_px;           // Border thickness (default 1)
};

// Main file browser state
struct ui_filebrowser {
    char cwd[UI_FB_MAX_PATH];            // Current directory (empty "" = Storage Locations view)
    char forced_root[UI_FB_MAX_PATH];    // Single forced root boundary (if configured)
    char internal_path[UI_FB_MAX_PATH];  // Internal storage path ("/data")
    char external_path[UI_FB_MAX_PATH];  // External storage path ("/media/<SDCARD>")
    char selected_path[UI_FB_MAX_PATH];  // Full path of selected item
    char selected_name[UI_FB_MAX_NAME];  // Filename of selected item
    int selected_is_dir;

    struct ui_fb_drive drives[UI_FB_MAX_DRIVES];
    int drive_count;

    uint32_t options;
    const char **extensions;
    ui_fb_filter_fn filter;
    void *user_data;
    char title[64];
    char action_label[32];

    int hide_dotfiles;
    int state;                           // UI_FB_STATE_*

    // Entries list
    struct ui_fb_entry *entries;
    int capacity;
    int count;
    int cursor;                          // Selected index in entries
    int scroll_offset;                   // Top visible entry index
    struct ui_fb_entry default_entries[UI_FB_DEFAULT_CAPACITY];

    // Clipboard for Copy / Move / Paste
    char clip_path[UI_FB_MAX_PATH];
    char clip_name[UI_FB_MAX_NAME];
    int clip_op;                         // UI_FB_CLIP_*
    int clip_is_dir;

    // Sub-modals & editors
    struct ui_text_editor editor;
    char edit_buf[UI_FB_MAX_NAME];
    int menu_cursor;

    // Feedback status banner
    char status_msg[64];
    int status_is_error;
};

// Initialize file browser state with the given configuration
void ui_filebrowser_init(struct ui_filebrowser *fb, const struct ui_filebrowser_config *cfg);

// Default toolkit styling
struct ui_filebrowser_style ui_filebrowser_style_default(void);

// Change current directory and reload entries. Returns 0 on success, -1 on error.
// Refuses paths outside allowed storage roots.
int ui_filebrowser_set_cwd(struct ui_filebrowser *fb, const char *path);

// Check if a path is allowed within the configured storage locations
int ui_filebrowser_is_allowed(const struct ui_filebrowser *fb, const char *path);

// Reload the contents of the current directory
void ui_filebrowser_refresh(struct ui_filebrowser *fb);

// Programmatic clipboard actions
int ui_filebrowser_copy(struct ui_filebrowser *fb);
int ui_filebrowser_cut(struct ui_filebrowser *fb);
int ui_filebrowser_paste(struct ui_filebrowser *fb);
int ui_filebrowser_delete(struct ui_filebrowser *fb);

// Low-level filesystem helpers
int ui_filebrowser_copy_file(const char *src, const char *dst);
int ui_filebrowser_move_file(const char *src, const char *dst);
int ui_filebrowser_delete_path(const char *path, int is_dir);
int ui_filebrowser_make_dir(const char *path);

// Storage safety and persistence flushing helpers
int ui_fsync_fd(int fd);
int ui_fsync_file(FILE *f);
int ui_sync_dir(const char *dir_path);
int ui_safe_atomic_write(const char *path, const void *buf, size_t len);
void ui_sync_storage(void);

// Draw the file browser within rect (x, y, w, h) and handle input.
// Returns UI_FB_ACTIVE, UI_FB_SELECT, or UI_FB_CANCEL.
int ui_filebrowser(struct ui *u, int x, int y, int w, int h, struct ui_filebrowser *fb,
                   struct ui_filebrowser_style st, const struct input *in);

#ifdef __cplusplus
}
#endif
