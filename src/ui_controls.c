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

/* Top bar: target, interval, CSV recording, state lamp, Start/Stop and Pause.
 * Start validates the fields and passes the target to the display. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "theme_private.h"

LV_IMAGE_DECLARE(joular_logo_1x);
LV_IMAGE_DECLARE(joular_logo_2x);

static lv_obj_t *kind_picker, *pid_entry, *app_entry, *interval_picker;
static lv_obj_t *record_switch, *record_name, *path_entry, *recording_note, *problem;
static lv_obj_t *lamp, *run_word, *run_key, *pause_key;

/* -------------------------------------------------------------------------------------- */

/* Strips surrounding spaces. False, buf untouched, if too long: truncating would point to
 * another file or program. */
static bool trimmed(const char *text, char *buf, size_t n)
{
    while (*text == ' ') text++;
    size_t len = strlen(text);
    while (len > 0 && text[len - 1] == ' ') len--;
    if (len >= n) return false;
    memcpy(buf, text, len);
    buf[len] = '\0';
    return true;
}

/* Digits only, > 0, fits in unsigned int */
static bool pid_of(const char *text, unsigned int *pid)
{
    unsigned long long value = 0;

    if (*text == '\0') return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        value = value * 10 + (unsigned)(*text - '0');
        if (value > UINT_MAX) return false;
    }
    *pid = (unsigned int)value;
    return value > 0;
}

/* -------------------------------------------------------------------------------------- */

/* Error and recording note share the same spot, one hides the other */
static void clear_problems(void)
{
    kit_show(problem, false);
    kit_show(recording_note, true);
    kit_entry_set_invalid(pid_entry, false);
    kit_entry_set_invalid(app_entry, false);
    kit_entry_set_invalid(path_entry, false);
}

/* Warning sign so it doesn't rely on colour alone; marks and focuses the field */
static void show_problem(const char *text, lv_obj_t *field)
{
    clear_problems();
    lv_label_set_text_fmt(problem, LV_SYMBOL_WARNING " %s", text);
    kit_show(problem, true);
    kit_show(recording_note, false);
    kit_entry_set_invalid(field, true);
    lv_group_focus_obj(field);
}

static void show_target_kind(void)
{
    uint32_t kind = lv_dropdown_get_selected(kind_picker);

    kit_show(pid_entry, kind == TARGET_PROCESS);
    kit_show(app_entry, kind == TARGET_APPLICATION);
    clear_problems();
}

/* Editable only when idle and recording is on */
static void show_path_enabled(void)
{
    kit_enable(path_entry, display_run() == RUN_IDLE
                           && lv_obj_has_state(record_switch, LV_STATE_CHECKED));
}

static void show_recording_note(void)
{
    const char *file = csv_recording_file();
    char folder[PATH_MAX];

    if (!csv_is_recording())
        kit_set_text(recording_note, "Export saves the trend to " CSV_EXPORT_FILE ".");
    /* Can't resolve the folder, show the path as typed */
    else if (!csv_folder_of(file, folder, sizeof folder))
        lv_label_set_text_fmt(recording_note, "Writing %s", file);
    else
        lv_label_set_text_fmt(recording_note, "Writing %s in %s", csv_file_name(file), folder);
}

/* -------------------------------------------------------------------------------------- */

/* False after showing the error */
static bool read_target(target *followed)
{
    memset(followed, 0, sizeof *followed);
    followed->kind = (target_kind)lv_dropdown_get_selected(kind_picker);

    switch (followed->kind) {
    case TARGET_PROCESS:
        if (!pid_of(lv_textarea_get_text(pid_entry), &followed->pid)) {
            show_problem("Enter a process ID, a whole number such as 2814.", pid_entry);
            return false;
        }
        break;
    case TARGET_APPLICATION:
        if (!trimmed(lv_textarea_get_text(app_entry), followed->app, sizeof followed->app)
            || followed->app[0] == '\0') {
            show_problem("Enter the application's name, such as firefox.", app_entry);
            return false;
        }
        break;
    case TARGET_WHOLE_SYSTEM:
        break;
    }
    return true;
}

static void start_session(void)
{
    target followed;
    char path[4096];

    if (!read_target(&followed)) return;

    if (lv_obj_has_state(record_switch, LV_STATE_CHECKED)) {
        if (!trimmed(lv_textarea_get_text(path_entry), path, sizeof path)) {
            show_problem("That file name is too long. Choose another file.", path_entry);
            return;
        }
        /* Export overwrites that file, it would wipe the recording */
        if (strcasecmp(csv_file_name(path), CSV_EXPORT_FILE) == 0) {
            show_problem("Export writes the trend to " CSV_EXPORT_FILE ". Choose another file.", path_entry);
            return;
        }
        if (!csv_start_recording(path[0] ? path : CSV_DEFAULT_FILE, &followed)) {
            char text[sizeof path + 64];
            snprintf(text, sizeof text, "%s. Choose another file.", csv_trouble());
            show_problem(text, path_entry);
            return;
        }
    }

    clear_problems();
    display_start(&followed);
}

/* -------------------------------------------------------------------------------------- */

/* Focus the matching field */
static void on_kind_changed(lv_event_t *e)
{
    uint32_t kind = lv_dropdown_get_selected(kind_picker);

    LV_UNUSED(e);
    show_target_kind();
    if (kind != TARGET_WHOLE_SYSTEM) lv_group_focus_obj(kind == TARGET_PROCESS ? pid_entry : app_entry);
}

/* Clear the error as soon as the user types */
static void on_edited(lv_event_t *e)
{
    LV_UNUSED(e);
    clear_problems();
}

static void on_interval_changed(lv_event_t *e)
{
    interval_choice chosen = kit_interval_selected(interval_picker);

    LV_UNUSED(e);
    /* Also fires when the same option is picked again */
    if (chosen == chosen_interval()) return;
    set_interval(chosen);
    display_interval_changed();
}

static void on_record_switched(lv_event_t *e)
{
    LV_UNUSED(e);
    show_path_enabled();
    clear_problems();
}

/* Start/Stop button, and Enter in a field */
static void on_run(lv_event_t *e)
{
    static uint32_t pressed;   /* tick of the last start or stop */

    /* Ignore the second click of a double click */
    if (lv_event_get_code(e) == LV_EVENT_CLICKED && lv_tick_elaps(pressed) < LV_INDEV_DEF_DOUBLE_CLICK_TIME)
        return;
    pressed = lv_tick_get();

    if (display_run() == RUN_IDLE)
        start_session();
    else
        display_stop();
}

static void on_pause(lv_event_t *e)
{
    LV_UNUSED(e);
    display_pause(display_run() != RUN_PAUSED);
}

/* -------------------------------------------------------------------------------------- */

/* Enter in the field starts a session */
static lv_obj_t *field(lv_obj_t *parent, const char *placeholder, bool digits_only, int32_t width)
{
    lv_obj_t *made = kit_entry(parent, placeholder, digits_only);

    lv_obj_set_width(made, ui_px(width));
    lv_obj_add_event_cb(made, on_run, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(made, on_edited, LV_EVENT_VALUE_CHANGED, NULL);
    return made;
}

/* Clicking the label toggles the switch, like a desktop checkbox label */
static void on_name_clicked(lv_event_t *e)
{
    lv_obj_t *control = lv_event_get_user_data(e);

    if (lv_obj_has_state(control, LV_STATE_DISABLED)) return;
    lv_obj_set_state(control, LV_STATE_CHECKED, !lv_obj_has_state(control, LV_STATE_CHECKED));
    lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
}

/* "Measure [Whole machine] [2814] every [1 s]" */
static void what_line(lv_obj_t *sentence)
{
    lv_obj_t *line = kit_box(sentence, false, 8);

    kit_let_out(line, 4);
    kit_label(line, "Measure", TEXT_WORD);
    kind_picker = kit_picker(line, "Whole machine\nProcess\nApplication", TARGET_WHOLE_SYSTEM,
                             on_kind_changed, NULL);
    pid_entry = field(line, "Process ID", true, 120);
    lv_textarea_set_max_length(pid_entry, 10);
    app_entry = field(line, "Application name", false, 170);
    kit_label(line, "every", TEXT_WORD);
    interval_picker = kit_interval_picker(line, on_interval_changed, NULL);
}

/* "(o) Record to [joularenergymeter-power.csv]  Writing ..." */
static void record_line(lv_obj_t *sentence)
{
    lv_obj_t *line = kit_box(sentence, false, 8);

    lv_obj_set_width(line, LV_PCT(100));
    kit_let_out(line, 4);
    record_switch = kit_switch(line, on_record_switched, NULL);
    record_name = kit_label(line, "Record to", TEXT_WORD);
    lv_obj_set_clickable(record_name, true);
    lv_obj_add_event_cb(record_name, on_name_clicked, LV_EVENT_CLICKED, record_switch);
    path_entry = field(line, CSV_DEFAULT_FILE, false, 236);

    /* Errors for all fields are shown here, near Start */
    recording_note = kit_lines(kit_label(line, "", TEXT_CAPTION), 2);
    lv_obj_set_flex_grow(recording_note, 1);
    problem = kit_lines(kit_label(line, "", TEXT_ERROR), 2);
    lv_obj_set_flex_grow(problem, 1);
}

lv_obj_t *controls_create(lv_obj_t *parent)
{
    lv_obj_t *bar = kit_box(parent, false, 20);
    lv_obj_t *brand, *logo, *name, *sentence, *keys;

    lv_obj_add_style(bar, &styles.bar, 0);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_style_pad_hor(bar, ui_px(UI_GUTTER + 4), 0);
    lv_obj_set_style_pad_ver(bar, ui_px(12), 0);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    kit_let_out(bar, 4);

    /* Logo on top, "Energy Meter" below */
    brand = kit_box(bar, true, 8);
    logo = lv_image_create(brand);
    lv_image_set_src(logo, platform_scale() > 1.25f ? &joular_logo_2x : &joular_logo_1x);
    /* Stretch the closest bitmap to the exact size */
    lv_obj_set_size(logo, ui_px(joular_logo_1x.header.w), ui_px(joular_logo_1x.header.h));
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_margin_ver(logo, ui_px(3), 0);
    name = kit_box(brand, false, 0);
    lv_obj_set_height(name, ui_px(32));
    kit_label(name, "Energy Meter", TEXT_TITLE);

    sentence = kit_box(bar, true, 8);
    lv_obj_set_width(sentence, 0);
    lv_obj_set_flex_grow(sentence, 1);
    kit_let_out(sentence, 4);
    what_line(sentence);
    record_line(sentence);

    /* Lamp and state, Pause (only while running), Start/Stop */
    keys = kit_box(bar, false, 10);
    lv_obj_set_height(keys, ui_px(32 + 8 + 32));
    kit_let_out(keys, 4);
    lamp = kit_lamp(keys);
    run_word = kit_label(keys, "", TEXT_BODY);
    lv_obj_set_style_margin_right(run_word, ui_px(8), 0);
    pause_key = kit_button(keys, LV_SYMBOL_PAUSE, "Pause", BUTTON_NORMAL, on_pause, NULL);
    lv_obj_set_height(pause_key, ui_px(40));
    run_key = kit_button(keys, LV_SYMBOL_PLAY, "Start", BUTTON_GO, on_run, NULL);
    lv_obj_set_size(run_key, ui_px(116), ui_px(40));
    lv_obj_set_style_text_font(run_key, ui_font(16), 0);

    show_target_kind();
    controls_show_run(RUN_IDLE);
    /* So Enter starts right away; no ring until the keyboard moves focus */
    lv_group_focus_obj(run_key);
    return bar;
}

void controls_show_run(run_state run)
{
    static const struct { const char *word; theme_token color; bool lit; } looks[] = {
        [RUN_IDLE] = { "Ready", TOKEN_LAMP_OFF, false },
        [RUN_STARTING] = { "Starting", TOKEN_LAMP_WAIT, true },
        [RUN_LIVE] = { "Live", TOKEN_LAMP_RUN, true },
        [RUN_PAUSED] = { "Paused", TOKEN_LAMP_WAIT, false },
    };
    bool idle = run == RUN_IDLE, paused = run == RUN_PAUSED;

    kit_lamp_set(lamp, looks[run].color, looks[run].lit);
    /* "Stopped" when the last session's values are still shown */
    kit_set_text(run_word, idle && history_count() > 0 ? "Stopped" : looks[run].word);

    kit_button_set(run_key, idle ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP, idle ? "Start" : "Stop",
                   idle ? BUTTON_GO : BUTTON_HALT);
    kit_button_set(pause_key, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE, paused ? "Resume" : "Pause",
                   BUTTON_NORMAL);
    kit_show(pause_key, run == RUN_LIVE || paused);

    /* Locked while a session runs */
    kit_enable(kind_picker, idle);
    kit_enable(pid_entry, idle);
    kit_enable(app_entry, idle);
    kit_enable(record_switch, idle);
    show_path_enabled();
    show_recording_note();
}
