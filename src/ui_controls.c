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

/* The What to measure card: target, interval, length, CSV recording; and the bar's lamp, state,
 * Start/Stop and Pause. Start validates the fields and passes the target to the display. While
 * measuring, the choices that can't change are locked. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "theme_private.h"

/* The card's rows; the target's row names the field the kind needs */
static lv_obj_t *kind_choices[4], *target_row, *pid_entry, *app_entry, *command_entry, *interval_picker;
static lv_obj_t *length_picker, *record_switch, *path_entry, *browse_key;
static target_kind kind = TARGET_WHOLE_SYSTEM;
/* The bar: state, time, what is or will be measured, count */
static lv_obj_t *lamp, *run_word, *run_time, *what_text, *run_count, *run_key, *pause_key;
static bool ended;   /* idle with the last session's values shown */

/* The banner shows a field error of ours, cleared by editing */
static bool alarm_ours;

/* The process ID typed, held for a zoom's rebuild only: never saved, it names another program
 * at the next launch. What the sentence says while measuring, for the same. */
static char held_pid[16];
/* The last session's, from its Start */
static target measured;

/* Kept names of the target kinds, in target_kind order */
static const char *const kinds[] = { "machine", "process", "application", "command" };

/* Session lengths in seconds, as controls_length gives them; "until it ends" only for what can
 * end. An application with no process left reads 0, as when idle, so it never ends. */
static const int lengths[] = { 0, -1, 60, 300, 600, 1800, 3600 };
static const char *const length_names[] = { "Until stopped", "Until it ends", "For 1 min", "For 5 min",
                                             "For 10 min", "For 30 min", "For 1 h" };
#define LENGTH_COUNT (sizeof lengths / sizeof *lengths)
static int length;

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

static void clear_problems(void)
{
    if (alarm_ours) ui_alarm_clear();
    alarm_ours = false;
    kit_entry_set_invalid(pid_entry, false);
    kit_entry_set_invalid(app_entry, false);
    kit_entry_set_invalid(command_entry, false);
    kit_entry_set_invalid(path_entry, false);
}

/* In the banner, which has a warning sign so it doesn't rely on colour alone; marks and
 * focuses the field */
static void show_problem(const char *text, lv_obj_t *field)
{
    clear_problems();
    ui_alarm(text, "", "");
    alarm_ours = true;
    kit_entry_set_invalid(field, true);
    ui_show_page(PAGE_SETUP);
    lv_group_focus_obj(field);
}

static bool can_end(void)
{
    return kind == TARGET_PROCESS || kind == TARGET_COMMAND;
}

/* The options the kind allows, the chosen length kept if it is one of them */
static void show_lengths(void)
{
    char options[128] = "";
    uint32_t shown = 0, chosen = 0;

    if (length < 0 && !can_end()) length = 0;
    for (size_t i = 0; i < LENGTH_COUNT; i++) {
        if (lengths[i] < 0 && !can_end()) continue;
        if (lengths[i] == length) chosen = shown;
        snprintf(options + strlen(options), sizeof options - strlen(options), "%s%s", shown ? "\n" : "",
                 length_names[i]);
        shown++;
    }
    lv_dropdown_set_options(length_picker, options);
    lv_dropdown_set_selected(length_picker, chosen);
}

/* "application firefox, every 1 s, until stopped": what Start will measure, in the bar */
static void show_summary(void)
{
    char what[200], every[16];
    const char *name;
    size_t n = sizeof what;

    if (display_run() != RUN_IDLE || ended || what_text == NULL) return;
    switch (kind) {
    case TARGET_PROCESS:
        name = lv_textarea_get_text(pid_entry);
        snprintf(what, n, name[0] ? "to measure process %s" : "to measure a process: type its ID", name);
        break;
    case TARGET_APPLICATION:
        name = lv_textarea_get_text(app_entry);
        snprintf(what, n, name[0] ? "to measure application %.120s" : "to measure an application: type its name", name);
        break;
    case TARGET_COMMAND:
        name = lv_textarea_get_text(command_entry);
        snprintf(what, n, name[0] ? "to measure command %.120s" : "to measure a command: type it", name);
        break;
    default:
        snprintf(what, n, "to measure the whole machine");
        break;
    }
    lv_dropdown_get_selected_str(length_picker, every, sizeof every);
    /* "Until stopped" -> "until stopped" */
    every[0] = (char)(every[0] - 'A' + 'a');
    snprintf(what + strlen(what), n - strlen(what), ", every %g s, %s%s", interval_seconds(chosen_interval()), every,
             lv_obj_has_state(record_switch, LV_STATE_CHECKED) ? ", recorded" : "");
    kit_set_text(what_text, what);
}

static void show_target_kind(void)
{
    static const char *const names[] = { "", "Process ID", "Application", "Command" };
    static const char *const notes[] = {
        "", "As the process list shows it", "Program name, such as firefox",
        "Run at Start, children not counted",
    };

    for (int i = 0; i < 4; i++) lv_obj_set_state(kind_choices[i], LV_STATE_CHECKED, i == (int)kind);
    kit_show(target_row, kind != TARGET_WHOLE_SYSTEM);
    kit_set_text(kit_row_name(target_row), names[kind]);
    kit_set_text(kit_row_note(target_row), notes[kind]);
    kit_show(pid_entry, kind == TARGET_PROCESS);
    kit_show(app_entry, kind == TARGET_APPLICATION);
    kit_show(command_entry, kind == TARGET_COMMAND);
    show_lengths();
    clear_problems();
    show_summary();
}

/* Editable only when recording is on, and not while measuring */
static void show_path_enabled(void)
{
    bool editable = lv_obj_has_state(record_switch, LV_STATE_CHECKED) && display_run() == RUN_IDLE;

    kit_enable(path_entry, editable);
    kit_enable(browse_key, editable);
}

/* "application firefox, recording to power.csv" */
static void show_what(void)
{
    char text[1200];
    size_t n = sizeof text;

    target_heading(&measured, text, n);
    text[0] = (char)(text[0] - 'A' + 'a');
    if (csv_is_recording())
        snprintf(text + strlen(text), n - strlen(text), ", recording to %.300s", csv_file_name(csv_recording_file()));
    kit_set_text(what_text, text);
}

/* -------------------------------------------------------------------------------------- */

/* False after showing the error */
static bool read_target(target *followed)
{
    memset(followed, 0, sizeof *followed);
    followed->kind = kind;

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
    case TARGET_COMMAND:
        if (!trimmed(lv_textarea_get_text(command_entry), followed->command, sizeof followed->command)
            || followed->command[0] == '\0') {
            show_problem("Enter the command to run, such as python3 bench.py.", command_entry);
            return false;
        }
#ifdef PJ_WINDOWS
        /* No way back to the user's rights from here */
        if (platform_privileged()) {
            show_problem("As administrator, the command would run as administrator too. Start "
                         JOULARENERGYMETER_NAME " without it to measure a command.", command_entry);
            return false;
        }
#endif
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
        if (!csv_start_recording(path[0] ? path : CSV_DEFAULT_FILE, &followed)) {
            char text[sizeof path + 64];
            snprintf(text, sizeof text, "%s. Choose another file.", csv_trouble());
            show_problem(text, path_entry);
            return;
        }
    }

    clear_problems();
    measured = followed;
    display_start(&followed);
    ui_show_page(PAGE_READINGS);
    settings_save();
}

/* -------------------------------------------------------------------------------------- */

/* Focus the matching field */
static void on_kind(lv_event_t *e)
{
    kind = (target_kind)(intptr_t)lv_event_get_user_data(e);
    /* A command is mostly measured to its end */
    if (kind == TARGET_COMMAND && length == 0) length = -1;
    show_target_kind();
    if (kind != TARGET_WHOLE_SYSTEM)
        lv_group_focus_obj(kind == TARGET_PROCESS ? pid_entry : kind == TARGET_APPLICATION ? app_entry : command_entry);
}

/* Option back to its length, skipping "until it ends" when it isn't shown */
static void on_length_changed(lv_event_t *e)
{
    uint32_t chosen = lv_dropdown_get_selected(length_picker), shown = 0;

    LV_UNUSED(e);
    for (size_t i = 0; i < LENGTH_COUNT; i++) {
        if (lengths[i] < 0 && !can_end()) continue;
        if (shown++ == chosen) length = lengths[i];
    }
    show_summary();
}

double controls_length(void)
{
    return length;
}

/* Clear the error as soon as the user types */
static void on_edited(lv_event_t *e)
{
    LV_UNUSED(e);
    clear_problems();
    show_summary();
}

static void on_interval_changed(lv_event_t *e)
{
    interval_choice chosen = kit_interval_selected(interval_picker);

    LV_UNUSED(e);
    /* Also fires when the same option is picked again */
    if (chosen == chosen_interval()) return;
    set_interval(chosen);
    display_interval_changed();
    show_summary();
}

/* Sends VALUE_CHANGED, which clears the problems */
static void chosen_record_file(const char *path, void *user_data)
{
    LV_UNUSED(user_data);
    lv_textarea_set_text(path_entry, path);
}

/* Starts where the file typed is, in the current folder for a bare name. Rows are appended to a
 * file that exists, so no "Replace?" */
static void on_browse(lv_event_t *e)
{
    char path[4096];

    LV_UNUSED(e);
    if (!trimmed(lv_textarea_get_text(path_entry), path, sizeof path) || path[0] == '\0')
        snprintf(path, sizeof path, "%s", CSV_DEFAULT_FILE);
    kit_save_csv("Choose the file to record to", path, csv_file_name(path), chosen_record_file);
}

static void on_record_switched(lv_event_t *e)
{
    LV_UNUSED(e);
    show_path_enabled();
    clear_problems();
    show_summary();
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
        controls_start();
    else
        display_stop();
}

void controls_start(void)
{
    if (display_run() == RUN_IDLE) start_session();
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

    /* 0: what the row leaves */
    lv_obj_set_width(made, ui_px(width));
    if (width == 0) lv_obj_set_flex_grow(made, 1);
    lv_obj_add_event_cb(made, on_run, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(made, on_edited, LV_EVENT_VALUE_CHANGED, NULL);
    return made;
}

/* Kind in the head, then a row each: the target's field, interval, length, recording */
static void build_card(lv_obj_t *page)
{
    static const char *const names[] = { "Whole machine", "Process", "Application", "Command" };
    lv_obj_t *card = kit_setup_card(page, LV_SYMBOL_EYE_OPEN, TOKEN_TILE_BLUE, "What to measure");
    lv_obj_t *strip = kit_panel_strip(card), *track, *row, *controls;

    lv_obj_set_flex_grow(kit_box(strip, false, 0), 1);
    track = kit_choices(strip);
    for (int i = 0; i < 4; i++)
        kind_choices[i] = kit_choice(track, TOKEN_COUNT, names[i], on_kind, (void *)(intptr_t)i);

    target_row = kit_row(card, "", "");
    controls = kit_row_controls(target_row);
    pid_entry = field(controls, "Process ID", true, 140);
    lv_textarea_set_max_length(pid_entry, 10);
    app_entry = field(controls, "Application name, such as firefox", false, 320);
    command_entry = field(controls, "Command to run, such as python3 bench.py", false, 0);
    lv_textarea_set_max_length(command_entry, sizeof ((target *)0)->command - 1);

    row = kit_row(card, "Every", "Can change while measuring");
    interval_picker = kit_interval_picker(kit_row_controls(row), on_interval_changed, NULL);

    row = kit_row(card, "Length", "Pauses don't count");
    /* Sized to the widest option; options set by show_lengths */
    length_picker = kit_picker(kit_row_controls(row), "Until stopped\nUntil it ends", 0, on_length_changed, NULL);
    kit_tip(length_picker, "How long to measure: until you press Stop, until the process or command ends, or for a"
                           " set time. Pauses don't count.");

    row = kit_row(card, "Record to a CSV file", "PowerJoular's columns");
    controls = kit_row_controls(row);
    record_switch = kit_switch(controls, on_record_switched, NULL);
    kit_switch_label(kit_row_name(row), record_switch);
    path_entry = field(controls, CSV_DEFAULT_FILE, false, 0);
    browse_key = kit_button(controls, LV_SYMBOL_DIRECTORY, NULL, BUTTON_NORMAL, on_browse, NULL);
    kit_tip(browse_key, "Choose the file to record to");
}

/* The interval kept, before its picker is made from chosen_interval */
static void restore_interval(void)
{
    const char *every = settings_get("every");

    for (int c = 0; every != NULL && c < INTERVAL_COUNT; c++)
        if (interval_seconds((interval_choice)c) == atof(every)) set_interval((interval_choice)c);
}

/* The rest, once every field exists, as setting a text clears the fields' problems. Not the
 * process ID: it names another program at the next launch. */
static void restore_choices(void)
{
    const char *app = settings_get("application");
    const char *command = settings_get("command"), *kept_length = settings_get("for");
    const char *record = settings_get("record"), *path = settings_get("record_to");

    kind = (target_kind)settings_index("measure", kinds, 4, kind);
    for (size_t i = 0; kept_length != NULL && i < LENGTH_COUNT; i++)
        if (atoi(kept_length) == lengths[i]) length = lengths[i];
    if (app != NULL) lv_textarea_set_text(app_entry, app);
    if (command != NULL) lv_textarea_set_text(command_entry, command);
    if (held_pid[0]) lv_textarea_set_text(pid_entry, held_pid);
    if (path != NULL) lv_textarea_set_text(path_entry, path);
    if (record != NULL) lv_obj_set_state(record_switch, LV_STATE_CHECKED, strcmp(record, "yes") == 0);
}

void controls_keep(void)
{
    char number[16];

    snprintf(held_pid, sizeof held_pid, "%s", lv_textarea_get_text(pid_entry));
    settings_set("measure", kinds[kind]);
    settings_set("application", lv_textarea_get_text(app_entry));
    settings_set("command", lv_textarea_get_text(command_entry));
    snprintf(number, sizeof number, "%d", length);
    settings_set("for", number);
    snprintf(number, sizeof number, "%g", interval_seconds(chosen_interval()));
    settings_set("every", number);
    settings_set("record", lv_obj_has_state(record_switch, LV_STATE_CHECKED) ? "yes" : "no");
    settings_set("record_to", lv_textarea_get_text(path_entry));
}

void controls_create(lv_obj_t *page)
{
    /* Not the bar's yet */
    what_text = NULL;
    restore_interval();
    build_card(page);
    restore_choices();
    show_target_kind();
}

lv_obj_t *controls_bar(lv_obj_t *bar)
{
    lv_obj_t *tile = kit_box(bar, false, 0), *words, *line;

    /* The lamp on a tinted tile */
    lv_obj_set_size(tile, ui_px(40), ui_px(40));
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(tile, ui_px(10), 0);
    lv_obj_set_style_bg_color(tile, theme_color(TOKEN_ACCENT_SOFT), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lamp = kit_lamp(tile);

    /* State, time and what is measured; under it the count and the messages */
    words = kit_box(bar, true, 3);
    lv_obj_set_width(words, 0);
    lv_obj_set_flex_grow(words, 1);
    line = kit_box(words, false, 10);
    lv_obj_set_width(line, LV_PCT(100));
    run_word = kit_label(line, "", TEXT_TITLE);
    run_time = kit_label(line, "", TEXT_FIGURES);
    lv_obj_set_style_text_color(run_time, theme_color(TOKEN_TEXT), 0);
    what_text = kit_lines(kit_label(line, "", TEXT_WORD), 1);
    lv_obj_set_width(what_text, 0);
    lv_obj_set_flex_grow(what_text, 1);
    line = kit_box(words, false, 10);
    lv_obj_set_width(line, LV_PCT(100));
    run_count = kit_lines(kit_label(line, "", TEXT_CAPTION), 1);

    pause_key = kit_button(bar, LV_SYMBOL_PAUSE, "Pause", BUTTON_NORMAL, on_pause, NULL);
    lv_obj_set_height(pause_key, ui_px(44));
    lv_obj_set_style_radius(pause_key, ui_px(12), 0);
    run_key = kit_button(bar, LV_SYMBOL_PLAY, "Start", BUTTON_GO, on_run, NULL);
    lv_obj_set_size(run_key, ui_px(124), ui_px(44));
    lv_obj_set_style_text_font(run_key, ui_bold(16), 0);

    /* Its state comes with the display's (display_create) */
    /* So Enter starts right away; no ring until the keyboard moves focus */
    lv_group_focus_obj(run_key);
    return line;
}

void controls_show_run(run_state run, bool stopped)
{
    static const struct { const char *word; theme_token color; bool lit; } looks[] = {
        [RUN_IDLE] = { "Ready", TOKEN_LAMP_OFF, false },
        [RUN_STARTING] = { "Starting", TOKEN_LAMP_WAIT, true },
        [RUN_LIVE] = { "Live", TOKEN_LAMP_RUN, true },
        [RUN_PAUSED] = { "Paused", TOKEN_LAMP_WAIT, false },
    };
    bool idle = run == RUN_IDLE, paused = run == RUN_PAUSED;
    bool counted = stopped || run == RUN_LIVE || paused;

    ended = stopped;
    kit_lamp_set(lamp, looks[run].color, looks[run].lit);
    kit_set_text(run_word, stopped ? "Stopped" : looks[run].word);
    kit_show(run_time, counted);
    kit_show(run_count, counted);

    kit_button_set(run_key, idle ? LV_SYMBOL_PLAY : LV_SYMBOL_STOP, idle ? "Start" : "Stop",
                   idle ? BUTTON_GO : BUTTON_HALT);
    kit_button_set(pause_key, paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE, paused ? "Resume" : "Pause",
                   BUTTON_NORMAL);
    kit_show(pause_key, run == RUN_LIVE || paused);

    /* What is measured is locked while a session runs; the interval and length are not */
    for (int i = 0; i < 4; i++) kit_enable(kind_choices[i], idle);
    kit_enable(pid_entry, idle);
    kit_enable(app_entry, idle);
    kit_enable(command_entry, idle);
    kit_enable(record_switch, idle);
    show_path_enabled();
    if (idle && !stopped) show_summary();
    else show_what();
}

void controls_show_progress(double elapsed, int samples, int missed)
{
    char text[48];
    const char *word = samples == 1 ? "measurement" : "measurements";

    kit_set_text(run_time, fmt_clock(text, sizeof text, elapsed));
    if (missed > 0)
        snprintf(text, sizeof text, "%d %s, %d missed", samples, word, missed);
    else
        snprintf(text, sizeof text, "%d %s%s", samples, word, samples > 0 ? ", none missed" : "");
    kit_set_text(run_count, text);
}
