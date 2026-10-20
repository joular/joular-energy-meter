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

/*
 * File browser dialog for LVGL 9.6, the same on Linux, macOS, FreeBSD and Windows: a path field
 * over a list of folders, then files, with Home, Up, Cancel and Choose. It opens a file or a
 * folder, or saves a file with a Name field. It works as a desktop one: a click selects, a double
 * click opens a folder or picks a file, and from the keyboard the arrows, Home and End select,
 * Enter opens, Backspace goes up, letters jump to a name, / and ~ start a path, Esc cancels.
 * Paths are UTF-8.
 *
 * Copy file_browser.c and file_browser.h into a project. They need:
 * - LVGL 9.6 or newer with LV_USE_TABLE, LV_USE_TEXTAREA, LV_USE_LABEL and LV_USE_BUTTON;
 * - a font with LVGL's LV_SYMBOL_* glyphs (Montserrat, or a font falling back to it);
 * - for the keyboard, a keypad input device with a group (the dialog takes the group's place
 *   while it is open and gives it back);
 * - for the mouse wheel, an input layer that scrolls the object under the pointer, looking in
 *   lv_layer_top() first, where the dialog is.
 * Sizes are in lv_dpx() units, so they follow the display's DPI.
 */

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stdbool.h>

#include "lvgl/lvgl.h"

/* Gets the chosen path */
typedef void (*file_browser_cb)(const char *path, void *user_data);

typedef struct file_browser_theme {
    lv_color_t backdrop;        /* dims the window behind the dialog */
    lv_color_t plate;           /* dialog, list, path field and plain buttons */
    lv_color_t pressed;         /* plain buttons hovered and pressed, rows pressed */
    lv_color_t edge;            /* borders */
    lv_color_t text;
    lv_color_t muted;           /* placeholder and messages */
    lv_color_t accent;          /* Choose */
    lv_color_t accent_text;
    lv_color_t selection;       /* the selected row */
    lv_color_t selection_text;
    lv_color_t focus;           /* keyboard focus ring */
    const lv_font_t *font;      /* NULL: LVGL's default font */
} file_browser_theme;

/* Joular's look: white plates, teal Choose, amber focus ring */
file_browser_theme file_browser_default_theme(void);

typedef struct file_browser_options {
    const char *title;
    const char *start;          /* a file or folder to start in, which may not exist yet (a bare
                                 * name is in the current folder); NULL or "": the home folder */
    const char *extension;      /* files ending with it, ignoring case ("" for every file);
                                 * NULL to choose a folder */
    const char *save_as;        /* to save a file: the name proposed, and Choose gives it in the
                                 * folder shown; NULL to open one */
    file_browser_cb on_choose;
    void *user_data;
    const file_browser_theme *theme;   /* NULL: the default; read on the first opening only */
} file_browser_options;

/* Opens the dialog over the active screen; the options are copied */
void file_browser_open(const file_browser_options *options);

/* Closes it without choosing */
void file_browser_close(void);

bool file_browser_is_open(void);

#endif /* FILE_BROWSER_H */
