/*
 * Copyright (c) 2026, Adel Noureddine.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the
 * GNU General Public License v3.0 only (GPL-3.0-only)
 * which accompanies this distribution, and is available at:
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Author : Adel Noureddine
 */

/* File browser dialog for LVGL 9, see file_browser.h. One dialog, built on the first opening
 * and kept hidden between them. */

#ifdef _WIN32
/* GetFinalPathNameByHandleW, which older MinGW headers hide */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#else
/* realpath() and the POSIX names, under a strict -std=c11 too */
#define _XOPEN_SOURCE 700
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#else
#include <dirent.h>
#include <pwd.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "file_browser.h"

#ifdef _WIN32
#define SEPARATOR '\\'
#define SEPARATORS "\\/"
#define ROOT 2   /* where the root's separator is: C:\ */
#else
#define SEPARATOR '/'
#define SEPARATORS "/"
#define ROOT 0
#endif

/* Longest path kept, in bytes */
#define PATH_SIZE 1024
#define MAX_ENTRIES 5000
/* Letters typed within this jump to a name together */
#define TYPE_AHEAD_MS 1000

static struct {
    lv_obj_t *dialog, *title, *name_row, *name, *where, *table, *choose, *message;
    lv_group_t *group, *before;
    char folder[PATH_SIZE];   /* "" for the drives on Windows */
    char extension[64];
    bool folders_only, saving;
    file_browser_cb on_choose;
    void *user_data;
    uint32_t selected, clicked_before;   /* rows clicked last and the time before; LV_TABLE_CELL_NONE */
    char typed[64];
    uint32_t typed_at;
    file_browser_theme theme;
    struct {
        lv_style_t backdrop, plate, muted, pressed;
        lv_style_t field, field_focused, cursor;
        lv_style_t key, go, go_pressed, disabled, ring;
        lv_style_t list, cell, scrollbar;
    } st;
} fb;

static void show_choose(void);
static void on_choose(lv_event_t *e);

/* -------------------------------------------------------------------------------------- */
/* Text                                                                                   */
/* -------------------------------------------------------------------------------------- */

/* snprintf; false if it did not fit */
static bool fits(char *out, size_t n, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int used = vsnprintf(out, n, format, args);
    va_end(args);
    return used >= 0 && (size_t)used < n;
}

static bool ends_with(const char *s, const char *end)
{
    size_t n = strlen(s), m = strlen(end);
    return n >= m && strcasecmp(s + n - m, end) == 0;
}

static bool join(char *out, size_t n, const char *folder, const char *name)
{
    static const char separator[] = { SEPARATOR, '\0' };
    size_t len = strlen(folder);

    if (len == 0) return fits(out, n, "%s", name);
    return fits(out, n, "%s%s%s", folder, folder[len - 1] == SEPARATOR ? "" : separator, name);
}

/* path becomes its folder, and name (if not NULL) the name it left; false at the top */
static bool cut_name(char *path, char *name, size_t n)
{
    size_t len = strlen(path);
    char *cut;

    while (len > 1 && path[len - 1] == SEPARATOR) path[--len] = '\0';
    cut = strrchr(path, SEPARATOR);
    if (cut == NULL || cut[1] == '\0') return false;
    if (name) fits(name, n, "%s", cut + 1);
    cut[cut == path + ROOT ? 1 : 0] = '\0';   /* the root keeps its separator */
    return true;
}

/* -------------------------------------------------------------------------------------- */
/* Folders                                                                                */
/* -------------------------------------------------------------------------------------- */

typedef void (*each_entry)(void *ctx, const char *name, bool folder);

#ifdef _WIN32

/* malloc'd, free() it */
static wchar_t *wide(const char *text)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof *w);

    if (w == NULL) abort();
    if (n <= 0 || MultiByteToWideChar(CP_UTF8, 0, text, -1, w, n) <= 0) w[0] = L'\0';
    return w;
}

static bool narrow(const wchar_t *w, char *out, size_t n)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)n, NULL, NULL) > 0;
}

/* "" lists the drives as folders "C:\" */
static bool list_folder(const char *folder, each_entry each, void *ctx)
{
    WIN32_FIND_DATAW found;
    HANDLE search;
    char name[PATH_SIZE], pattern[PATH_SIZE];
    wchar_t *w;

    if (folder[0] == '\0') {
        wchar_t drives[256];
        DWORD n = GetLogicalDriveStringsW(256, drives);
        for (wchar_t *d = drives; n > 0 && n < 256 && *d; d += wcslen(d) + 1)
            if (narrow(d, name, sizeof name)) each(ctx, name, true);
        return true;
    }
    if (!join(pattern, sizeof pattern, folder, "*")) return false;
    w = wide(pattern);
    search = FindFirstFileW(w, &found);
    free(w);
    if (search == INVALID_HANDLE_VALUE) return false;
    do {
        /* Hidden: dot names, and hidden and system files as Explorer hides them */
        if (found.cFileName[0] == L'.' || (found.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))) continue;
        if (narrow(found.cFileName, name, sizeof name)) each(ctx, name, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return true;
}

static DWORD attributes(const char *path)
{
    wchar_t *w = wide(path);
    DWORD a = GetFileAttributesW(w);

    free(w);
    return a;
}

static bool is_folder(const char *path)
{
    DWORD a = attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool is_file(const char *path)
{
    DWORD a = attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Through links; without the \\?\ prefix */
static bool real_path(const char *path, char *out, size_t n)
{
    wchar_t *w = wide(path), real[PATH_SIZE];
    HANDLE h = CreateFileW(w, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, NULL);
    char text[PATH_SIZE];
    DWORD got;

    free(w);
    if (h == INVALID_HANDLE_VALUE) return false;
    got = GetFinalPathNameByHandleW(h, real, PATH_SIZE, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (got == 0 || got >= PATH_SIZE || !narrow(real, text, sizeof text)) return false;
    if (strncmp(text, "\\\\?\\UNC\\", 8) == 0) return fits(out, n, "\\\\%s", text + 8);
    return fits(out, n, "%s", strncmp(text, "\\\\?\\", 4) == 0 ? text + 4 : text);
}

static const char *home(void)
{
    static char path[PATH_SIZE];
    const wchar_t *profile = _wgetenv(L"USERPROFILE");

    if (profile == NULL || !narrow(profile, path, sizeof path)) fits(path, sizeof path, "C:\\");
    return path;
}

#else

static bool list_folder(const char *folder, each_entry each, void *ctx)
{
    DIR *dir = opendir(folder);
    struct dirent *entry;
    struct stat st;

    if (dir == NULL) return false;
    while ((entry = readdir(dir)) != NULL) {
        /* Hidden, with . and .. */
        if (entry->d_name[0] == '.') continue;
        /* Through links, as a file manager shows them */
        each(ctx, entry->d_name, fstatat(dirfd(dir), entry->d_name, &st, 0) == 0 && S_ISDIR(st.st_mode));
    }
    closedir(dir);
    return true;
}

static bool is_folder(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool real_path(const char *path, char *out, size_t n)
{
    char *real = realpath(path, NULL);
    bool done = real != NULL && fits(out, n, "%s", real);

    free(real);
    return done;
}

static const char *home(void)
{
    const char *path = getenv("HOME");
    struct passwd *pw;

    if (path && path[0] == '/') return path;
    pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "/";
}

#endif

/* -------------------------------------------------------------------------------------- */
/* The look                                                                               */
/* -------------------------------------------------------------------------------------- */

file_browser_theme file_browser_default_theme(void)
{
    file_browser_theme t;

    t.backdrop = lv_color_hex(0x12292e);
    t.plate = lv_color_hex(0xfbfdfd);
    t.pressed = lv_color_hex(0xe0ecee);
    t.edge = lv_color_hex(0x5f7b80);
    t.text = lv_color_hex(0x12292e);
    t.muted = lv_color_hex(0x506569);
    t.accent = lv_color_hex(0x1f7568);
    t.accent_text = lv_color_hex(0xfbfdfd);
    t.selection = lv_color_hex(0x12292e);
    t.selection_text = lv_color_hex(0xfbfdfd);
    t.focus = lv_color_hex(0xa86400);
    t.font = NULL;
    return t;
}

static void fill(lv_style_t *s, lv_color_t color)
{
    lv_style_set_bg_color(s, color);
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

static void edge(lv_style_t *s, lv_color_t color, int32_t radius)
{
    lv_style_set_border_color(s, color);
    lv_style_set_border_width(s, lv_dpx(1));
    lv_style_set_radius(s, lv_dpx(radius));
}

/* The controls inherit the plate's text colour and font */
static void make_styles(void)
{
    const file_browser_theme *t = &fb.theme;
    const lv_font_t *font = t->font ? t->font : LV_FONT_DEFAULT;
    lv_style_t *all = (lv_style_t *)&fb.st;

    for (size_t i = 0; i < sizeof fb.st / sizeof *all; i++) lv_style_init(&all[i]);

    fill(&fb.st.backdrop, t->backdrop);
    lv_style_set_bg_opa(&fb.st.backdrop, LV_OPA_30);

    fill(&fb.st.plate, t->plate);
    edge(&fb.st.plate, t->edge, 8);
    lv_style_set_border_opa(&fb.st.plate, LV_OPA_40);
    lv_style_set_shadow_color(&fb.st.plate, t->text);
    lv_style_set_shadow_width(&fb.st.plate, lv_dpx(24));
    lv_style_set_shadow_offset_y(&fb.st.plate, lv_dpx(6));
    lv_style_set_shadow_opa(&fb.st.plate, LV_OPA_20);
    lv_style_set_pad_all(&fb.st.plate, lv_dpx(20));
    lv_style_set_pad_gap(&fb.st.plate, lv_dpx(12));
    lv_style_set_text_color(&fb.st.plate, t->text);
    lv_style_set_text_font(&fb.st.plate, font);

    lv_style_set_text_color(&fb.st.muted, t->muted);
    fill(&fb.st.pressed, t->pressed);

    fill(&fb.st.field, t->plate);
    edge(&fb.st.field, t->edge, 6);
    lv_style_set_pad_hor(&fb.st.field, lv_dpx(10));
    /* Extra pixel as outline, so the text doesn't move */
    lv_style_set_border_color(&fb.st.field_focused, t->text);
    lv_style_set_outline_color(&fb.st.field_focused, t->text);
    lv_style_set_outline_width(&fb.st.field_focused, lv_dpx(1));
    /* A caret that doesn't blink: no animation time on the cursor part */
    lv_style_set_border_color(&fb.st.cursor, t->text);
    lv_style_set_border_width(&fb.st.cursor, lv_dpx(1));
    lv_style_set_border_side(&fb.st.cursor, LV_BORDER_SIDE_LEFT);

    fill(&fb.st.key, t->plate);
    edge(&fb.st.key, t->edge, 6);
    lv_style_set_pad_hor(&fb.st.key, lv_dpx(14));
    fill(&fb.st.go, t->accent);
    lv_style_set_border_color(&fb.st.go, t->accent);
    lv_style_set_text_color(&fb.st.go, t->accent_text);
    fill(&fb.st.go_pressed, lv_color_darken(t->accent, LV_OPA_20));
    /* Disabled: outline only */
    lv_style_set_bg_opa(&fb.st.disabled, LV_OPA_TRANSP);
    lv_style_set_border_color(&fb.st.disabled, t->muted);
    lv_style_set_text_color(&fb.st.disabled, t->muted);
    /* Keyboard focus only */
    lv_style_set_outline_color(&fb.st.ring, t->focus);
    lv_style_set_outline_width(&fb.st.ring, lv_dpx(2));
    lv_style_set_outline_pad(&fb.st.ring, lv_dpx(2));

    fill(&fb.st.list, t->plate);
    edge(&fb.st.list, t->edge, 6);
    lv_style_set_border_opa(&fb.st.list, LV_OPA_40);
    lv_style_set_pad_all(&fb.st.list, 0);
    /* A fill on every cell, which the selected row recolours */
    fill(&fb.st.cell, t->plate);
    lv_style_set_pad_ver(&fb.st.cell, lv_dpx(5));
    lv_style_set_pad_hor(&fb.st.cell, lv_dpx(10));
    lv_style_set_border_width(&fb.st.cell, 0);

    fill(&fb.st.scrollbar, t->edge);
    lv_style_set_width(&fb.st.scrollbar, lv_dpx(4));
    lv_style_set_radius(&fb.st.scrollbar, LV_RADIUS_CIRCLE);
    lv_style_set_pad_right(&fb.st.scrollbar, lv_dpx(3));
}

/* -------------------------------------------------------------------------------------- */
/* The list                                                                               */
/* -------------------------------------------------------------------------------------- */

typedef struct entry {
    char *name;
    bool folder;
} entry;

typedef struct listing {
    entry *entries;
    size_t count;
} listing;

static void collect(void *ctx, const char *name, bool folder)
{
    listing *l = ctx;

    if (l->count >= MAX_ENTRIES) return;
    if (!folder && (fb.folders_only || !ends_with(name, fb.extension))) return;
    l->entries[l->count].name = lv_strdup(name);
    if (l->entries[l->count].name == NULL) abort();
    l->entries[l->count++].folder = folder;
}

/* Folders first, then by name, ignoring case */
static int by_kind(const void *a, const void *b)
{
    const entry *x = a, *y = b;

    if (x->folder != y->folder) return x->folder ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void say(const char *text)
{
    lv_label_set_text(fb.message, text);
}

/* For Enter acting on its press, in the table and the path field: its release would click what
 * has the focus then, and its press, sent after the KEY event, would leave it pressed. Buttons
 * act on the release, the end of it. */
static void end_enter(void)
{
    lv_indev_t *active = lv_indev_active();

    if (active == NULL || lv_indev_get_type(active) != LV_INDEV_TYPE_KEYPAD || lv_indev_get_key(active) != LV_KEY_ENTER)
        return;
    lv_indev_wait_release(active);
    lv_indev_reset(active, NULL);
}

/* The path field shows the end of the path, or all of it when it fits */
static void show_path(const char *path)
{
    lv_textarea_set_text(fb.where, path);
    lv_obj_update_layout(fb.where);
    /* As far right as the text goes, at once: the cursor alone would slide from the old scroll */
    lv_obj_scroll_to_x(fb.where, LV_COORD_MAX, LV_ANIM_OFF);
    lv_textarea_set_cursor_pos(fb.where, LV_TEXTAREA_CURSOR_LAST);
}

static bool show(const char *where)
{
    listing l = { calloc(MAX_ENTRIES, sizeof(entry)), 0 };
    char folder[PATH_SIZE];

    if (l.entries == NULL) abort();
    /* As a file manager shows it: absolute, no . or .., links followed */
    if (where[0] == '\0' || !real_path(where, folder, sizeof folder)) fits(folder, sizeof folder, "%s", where);
    if (!list_folder(folder, collect, &l)) {
        free(l.entries);
        say("This folder can't be read.");
        return false;
    }
    fits(fb.folder, sizeof fb.folder, "%s", folder);
    /* Laid out first, so the rows are measured once, at the table's width */
    show_path(fb.folder);
    qsort(l.entries, l.count, sizeof *l.entries, by_kind);

    /* Down to the Up row first: rows written over old ones are measured twice */
    lv_table_set_row_count(fb.table, 1);
    lv_table_set_row_count(fb.table, (uint32_t)l.count + 1);
    for (size_t i = 0; i < l.count; i++) {
        lv_table_set_cell_value(fb.table, (uint32_t)i + 1, 0, l.entries[i].folder ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
        lv_table_set_cell_value(fb.table, (uint32_t)i + 1, 1, l.entries[i].name);
        lv_free(l.entries[i].name);
    }
    free(l.entries);

    fb.selected = fb.clicked_before = LV_TABLE_CELL_NONE;
    fb.typed[0] = '\0';
    say("");
    show_choose();
    lv_obj_scroll_to_y(fb.table, 0, LV_ANIM_OFF);
    return true;
}

/* From the keyboard: scrolled into view, and not the first click of a double click */
static void select_row(uint32_t row)
{
    uint32_t count = lv_table_get_row_count(fb.table);

    if (row >= count) row = count - 1;
    /* The table scrolls to its cell only when the cell changes: the other column first. Its
     * size must be up to date after show(). Both go through on_row. */
    lv_obj_update_layout(fb.table);
    lv_table_set_selected_cell(fb.table, (uint16_t)row, 0);
    lv_table_set_selected_cell(fb.table, (uint16_t)row, 1);
    fb.clicked_before = LV_TABLE_CELL_NONE;
}

/* The first row whose name starts with prefix, ignoring case; the Up row left out */
static void select_name(const char *prefix)
{
    size_t n = strlen(prefix);
    uint32_t count = lv_table_get_row_count(fb.table);

    for (uint32_t row = 1; n > 0 && row < count; row++) {
        if (strncasecmp(lv_table_get_cell_value(fb.table, row, 1), prefix, n) == 0) {
            select_row(row);
            return;
        }
    }
}

/* "/" stays; on Windows "C:\" goes to the drives. The folder left is selected, as in Finder. */
static void up(void)
{
    char parent[PATH_SIZE], left[PATH_SIZE];

    fits(parent, sizeof parent, "%s", fb.folder);
    if (!cut_name(parent, left, sizeof left)) {
#ifdef _WIN32
        show("");
#endif
        return;
    }
    if (show(parent)) select_name(left);
}

static bool selected_is_folder(void)
{
    return fb.selected != LV_TABLE_CELL_NONE
           && strcmp(lv_table_get_cell_value(fb.table, fb.selected, 0), LV_SYMBOL_DIRECTORY) == 0;
}

/* Path of the selected row, the Up row left out */
static bool selected_path(char *out, size_t n)
{
    if (fb.selected == LV_TABLE_CELL_NONE || fb.selected == 0) return false;
    return join(out, n, fb.folder, lv_table_get_cell_value(fb.table, fb.selected, 1));
}

/* What Choose takes: to save, the name in the folder shown; else the selected row of the kind
 * asked, or for a folder the one shown. Never the drives. */
static bool choice(char *out, size_t n)
{
    const char *name = lv_textarea_get_text(fb.name);

    if (fb.saving)
        return fb.folder[0] && name[0] && !strpbrk(name, SEPARATORS) && join(out, n, fb.folder, name);
    if (selected_path(out, n) && fb.folders_only == selected_is_folder()) return true;
    return fb.folders_only && fb.folder[0] && fits(out, n, "%s", fb.folder);
}

static void show_choose(void)
{
    char path[PATH_SIZE];

    lv_obj_set_disabled(fb.choose, !choice(path, sizeof path));
}

static void choose(const char *path)
{
    file_browser_cb on_choose = fb.on_choose;
    void *user_data = fb.user_data;
    char chosen[PATH_SIZE];

    /* Closing may reuse what path points to */
    fits(chosen, sizeof chosen, "%s", path);
    file_browser_close();
    if (on_choose) on_choose(chosen, user_data);
}

/* A folder opens, a file is chosen, the Up row goes up */
static void open_selected(void)
{
    char path[PATH_SIZE];

    if (fb.selected == 0) {
        up();
    } else if (selected_path(path, sizeof path)) {
        if (selected_is_folder())
            show(path);
        else
            choose(path);
    }
}

/* -------------------------------------------------------------------------------------- */
/* Events                                                                                 */
/* -------------------------------------------------------------------------------------- */

/* The table only knows the row while it is pressed: VALUE_CHANGED, sent on release, selects
 * it, and LV_EVENT_DOUBLE_CLICKED comes after it */
static void on_row(lv_event_t *e)
{
    uint32_t row, col;

    (void)e;
    lv_table_get_selected_cell(fb.table, &row, &col);
    if (row == LV_TABLE_CELL_NONE) return;
    fb.clicked_before = fb.selected;
    fb.selected = row;
    /* To save over a file, as a desktop dialog does */
    if (fb.saving && row > 0 && !selected_is_folder())
        lv_textarea_set_text(fb.name, lv_table_get_cell_value(fb.table, row, 1));
    show_choose();
    lv_obj_invalidate(fb.table);
}

static void on_row_double_clicked(lv_event_t *e)
{
    (void)e;
    /* Both clicks on the same row */
    if (fb.selected != LV_TABLE_CELL_NONE && fb.selected == fb.clicked_before) open_selected();
}

/* Before the table's own handling, which this replaces. Esc goes on to on_escape. */
static void on_table_key(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    bool none = fb.selected == LV_TABLE_CELL_NONE;
    uint32_t at = none ? 0 : fb.selected;

    switch (key) {
    case LV_KEY_UP: select_row(at > 0 ? at - 1 : 0); break;
    case LV_KEY_DOWN: select_row(none ? 0 : at + 1); break;
    case LV_KEY_HOME: select_row(0); break;
    case LV_KEY_END: select_row(lv_table_get_row_count(fb.table) - 1); break;
    case LV_KEY_BACKSPACE: up(); break;
    case LV_KEY_LEFT:
    case LV_KEY_RIGHT: break;
    case LV_KEY_ENTER:
        end_enter();
        if (none)
            on_choose(e);
        else
            open_selected();
        break;
    default:
        if (key == LV_KEY_ESC || key < 32 || key == LV_KEY_DEL) return;
#ifndef _WIN32
        if (key == '/' || key == '~') {
            show_path(key == '/' ? "/" : home());
            lv_group_focus_obj(fb.where);
            break;
        }
#endif
        /* A typed character is the key's UTF-8 bytes in memory order, as lv_textarea reads it */
        if (lv_tick_elaps(fb.typed_at) > TYPE_AHEAD_MS) fb.typed[0] = '\0';
        fb.typed_at = lv_tick_get();
        char letter[5] = { 0 };
        memcpy(letter, &key, 4);
        if (strlen(fb.typed) + strlen(letter) < sizeof fb.typed) strcat(fb.typed, letter);
        select_name(fb.typed);
        break;
    }
    lv_event_stop_processing(e);
}

/* The selected row in the theme's selection colours */
static void on_draw_task(lv_event_t *e)
{
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = lv_draw_task_get_draw_dsc(task);
    lv_draw_fill_dsc_t *fill_dsc;
    lv_draw_label_dsc_t *label_dsc;

    if (base->part != LV_PART_ITEMS || base->id1 != fb.selected) return;
    if ((fill_dsc = lv_draw_task_get_fill_dsc(task)) != NULL) fill_dsc->color = fb.theme.selection;
    if ((label_dsc = lv_draw_task_get_label_dsc(task)) != NULL) label_dsc->color = fb.theme.selection_text;
}

/* The name gets what the icon leaves. Only on a change: a column width measures every row. */
static void on_resize(lv_event_t *e)
{
    int32_t name = lv_obj_get_content_width(fb.table) - lv_table_get_column_width(fb.table, 0);

    (void)e;
    if (name != lv_table_get_column_width(fb.table, 1)) lv_table_set_column_width(fb.table, 1, name);
}

static void on_typed(lv_event_t *e)
{
    char path[PATH_SIZE];
    const char *text = lv_textarea_get_text(fb.where);

    (void)e;
    end_enter();
    while (*text == ' ') text++;
    if (!fits(path, sizeof path, "%s", text)) return;
    for (size_t n = strlen(path); n > 0 && path[n - 1] == ' '; n--) path[n - 1] = '\0';
    if (path[0] == '\0') return;
    if (is_folder(path)) {
        if (show(path)) lv_group_focus_obj(fb.table);
    } else if (!fb.folders_only && is_file(path)) {
        choose(path);
    } else {
        say("There is no such folder.");
    }
}

static void on_home(lv_event_t *e)
{
    (void)e;
    show(home());
}

static void on_up(lv_event_t *e)
{
    (void)e;
    up();
}

/* To save, a name that is a folder opens it */
static void on_choose(lv_event_t *e)
{
    char path[PATH_SIZE];

    (void)e;
    if (!choice(path, sizeof path)) {
        if (fb.saving && strpbrk(lv_textarea_get_text(fb.name), SEPARATORS))
            say("Type a name only, it goes in the folder shown.");
    } else if (fb.saving && is_folder(path)) {
        if (show(path)) {
            lv_textarea_set_text(fb.name, "");
            show_choose();   /* setting "" sends no VALUE_CHANGED */
        }
    } else {
        choose(path);
    }
}

static void on_name_ready(lv_event_t *e)
{
    end_enter();
    on_choose(e);
}

static void on_name_edited(lv_event_t *e)
{
    (void)e;
    say("");
    show_choose();
}

static void on_cancel(lv_event_t *e)
{
    (void)e;
    file_browser_close();
}

static void on_escape(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ESC) file_browser_close();
}

/* -------------------------------------------------------------------------------------- */
/* Building                                                                               */
/* -------------------------------------------------------------------------------------- */

/* A row of controls */
static lv_obj_t *box(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(obj, lv_dpx(8), 0);
    /* Focus rings drawn outside the controls */
    lv_obj_set_overflow_visible(obj, true);
    lv_obj_set_ext_draw_size(obj, lv_dpx(4));
    return obj;
}

/* A one-line text field; Enter sends LV_EVENT_READY */
static lv_obj_t *field(lv_obj_t *parent, const char *placeholder)
{
    lv_obj_t *ta = lv_textarea_create(parent);

    lv_obj_remove_style_all(ta);
    lv_obj_add_style(ta, &fb.st.field, 0);
    lv_obj_add_style(ta, &fb.st.field_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(ta, &fb.st.muted, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_style(ta, &fb.st.cursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, PATH_SIZE - 1);
    /* Centre the text vertically */
    int32_t line = lv_font_get_line_height(lv_obj_get_style_text_font(ta, LV_PART_MAIN));
    lv_obj_set_style_pad_ver(ta, (lv_dpx(32) - line) / 2, 0);
    lv_obj_set_size(ta, 0, lv_dpx(32));
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_add_event_cb(ta, on_escape, LV_EVENT_KEY, NULL);
    return ta;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, bool go, lv_event_cb_t on_click)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_t *label;

    lv_obj_remove_style_all(b);
    /* Choose is a plain button in the accent colours */
    lv_obj_add_style(b, &fb.st.key, 0);
    if (go) lv_obj_add_style(b, &fb.st.go, 0);
    lv_obj_add_style(b, go ? &fb.st.go_pressed : &fb.st.pressed, LV_STATE_PRESSED);
    if (!go) lv_obj_add_style(b, &fb.st.pressed, LV_STATE_HOVERED);
    lv_obj_add_style(b, &fb.st.disabled, LV_STATE_DISABLED);
    lv_obj_add_style(b, &fb.st.ring, LV_STATE_FOCUS_KEY);
    lv_obj_set_ext_draw_size(b, lv_dpx(4));
    lv_obj_set_height(b, lv_dpx(32));
    label = lv_label_create(b);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(b, on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(b, on_escape, LV_EVENT_KEY, NULL);
    return b;
}

static void build(void)
{
    lv_group_t *default_group = lv_group_get_default();
    lv_obj_t *plate, *top, *keys;

    make_styles();
    /* What is made here joins the dialog's own group */
    fb.group = lv_group_create();
    lv_group_set_default(fb.group);

    /* Dims the window and takes every click outside the plate */
    fb.dialog = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(fb.dialog);
    lv_obj_add_style(fb.dialog, &fb.st.backdrop, 0);
    lv_obj_set_size(fb.dialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_scrollable(fb.dialog, false);
    lv_obj_set_hidden(fb.dialog, true);

    plate = lv_obj_create(fb.dialog);
    lv_obj_remove_style_all(plate);
    lv_obj_add_style(plate, &fb.st.plate, 0);
    lv_obj_set_size(plate, lv_dpx(640), LV_PCT(80));
    lv_obj_set_style_max_width(plate, LV_PCT(92), 0);
    lv_obj_set_scrollable(plate, false);
    lv_obj_set_flex_flow(plate, LV_FLEX_FLOW_COLUMN);
    lv_obj_center(plate);

    fb.title = lv_label_create(plate);

    /* Shown to save only */
    fb.name_row = box(plate);
    lv_label_set_text(lv_label_create(fb.name_row), "Name");
    fb.name = field(fb.name_row, "File name");
    lv_obj_add_event_cb(fb.name, on_name_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(fb.name, on_name_edited, LV_EVENT_VALUE_CHANGED, NULL);

    top = box(plate);
    fb.where = field(top, "Type or paste a path, then Enter");
    lv_obj_add_event_cb(fb.where, on_typed, LV_EVENT_READY, NULL);
    button(top, LV_SYMBOL_HOME " Home", false, on_home);
    button(top, LV_SYMBOL_UP " Up", false, on_up);

    fb.table = lv_table_create(plate);
    lv_obj_remove_style_all(fb.table);
    lv_obj_add_style(fb.table, &fb.st.list, 0);
    lv_obj_add_style(fb.table, &fb.st.cell, LV_PART_ITEMS);
    lv_obj_add_style(fb.table, &fb.st.pressed, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_style(fb.table, &fb.st.scrollbar, LV_PART_SCROLLBAR);
    lv_obj_add_style(fb.table, &fb.st.ring, LV_STATE_FOCUS_KEY);
    lv_obj_set_ext_draw_size(fb.table, lv_dpx(4));
    lv_obj_set_width(fb.table, LV_PCT(100));
    lv_obj_set_flex_grow(fb.table, 1);
    /* An icon column, and the name, sized in on_resize. Row 0 is always Up. */
    lv_table_set_column_count(fb.table, 2);
    lv_table_set_column_width(fb.table, 0, lv_dpx(36));
    lv_table_set_cell_value(fb.table, 0, 0, LV_SYMBOL_UP);
    lv_table_set_cell_value(fb.table, 0, 1, "Up");
    lv_obj_add_event_cb(fb.table, on_table_key, LV_EVENT_KEY | LV_EVENT_PREPROCESS, NULL);
    lv_obj_add_event_cb(fb.table, on_escape, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(fb.table, on_row, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(fb.table, on_row_double_clicked, LV_EVENT_DOUBLE_CLICKED, NULL);
    lv_obj_set_send_draw_task_events(fb.table, true);
    lv_obj_add_event_cb(fb.table, on_draw_task, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(fb.table, on_resize, LV_EVENT_SIZE_CHANGED, NULL);

    keys = box(plate);
    fb.message = lv_label_create(keys);
    lv_obj_add_style(fb.message, &fb.st.muted, 0);
    lv_label_set_text(fb.message, "");
    lv_label_set_long_mode(fb.message, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(fb.message, 0);
    lv_obj_set_flex_grow(fb.message, 1);
    button(keys, "Cancel", false, on_cancel);
    fb.choose = button(keys, "Choose", true, on_choose);

    lv_group_set_default(default_group);
}

/* The keyboard, if there is one: the first keypad */
static lv_indev_t *keypad(void)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i != NULL; i = lv_indev_get_next(i))
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_KEYPAD) return i;
    return NULL;
}

void file_browser_open(const file_browser_options *options)
{
    char start[PATH_SIZE];
    lv_indev_t *keyboard = keypad();

    if (fb.dialog == NULL) {
        fb.theme = options->theme ? *options->theme : file_browser_default_theme();
        build();
    }
    lv_label_set_text(fb.title, options->title ? options->title : "");
    fb.saving = options->save_as != NULL;
    fb.folders_only = options->extension == NULL && !fb.saving;
    fits(fb.extension, sizeof fb.extension, "%s", options->extension ? options->extension : "");
    lv_obj_set_hidden(fb.name_row, !fb.saving);
    lv_textarea_set_text(fb.name, fb.saving ? options->save_as : "");
    fb.on_choose = options->on_choose;
    fb.user_data = options->user_data;

    /* The start's folder if it is a file (the current one for a bare name), else home */
    if (!fits(start, sizeof start, "%s", options->start ? options->start : "")) start[0] = '\0';
    if (start[0] && !is_folder(start) && !cut_name(start, NULL, 0)) fits(start, sizeof start, ".");

    if (keyboard && !file_browser_is_open()) {
        fb.before = lv_indev_get_group(keyboard);
        lv_indev_set_group(keyboard, fb.group);
    }
    lv_obj_move_foreground(fb.dialog);
    /* Shown before it is filled, so the path field has its width */
    lv_obj_set_hidden(fb.dialog, false);
    if (start[0] == '\0' || !show(start)) show(home());
    lv_group_focus_obj(fb.saving ? fb.name : fb.table);
}

void file_browser_close(void)
{
    lv_indev_t *keyboard = keypad();

    if (!file_browser_is_open()) return;
    if (keyboard) lv_indev_set_group(keyboard, fb.before);
    lv_obj_set_hidden(fb.dialog, true);
    /* The listing isn't kept: the next opening reads its folder again */
    lv_table_set_row_count(fb.table, 1);
}

bool file_browser_is_open(void)
{
    return fb.dialog != NULL && !lv_obj_is_hidden(fb.dialog);
}
