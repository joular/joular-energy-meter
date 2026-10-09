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
 * Joular Energy Meter: live power of the machine and of one process or application.
 * Uses Joular Core and CPU Load, like PowerJoular, so the numbers match. LVGL on SDL.
 *
 * Shared header for all modules, one .c file each.
 *
 * LVGL's built-in Montserrat only has ASCII, the degree sign, the bullet (U+2022) and the
 * LV_SYMBOL_* glyphs. No ellipsis, no dashes other than "-", no narrow space, no accents.
 */

#ifndef JOULAR_ENERGY_METER_APP_H
#define JOULAR_ENERGY_METER_APP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl/lvgl.h"

/* Keep in sync with alire.toml */
#define JOULARENERGYMETER_NAME "Joular Energy Meter"
#define JOULARENERGYMETER_VERSION "0.0.1"
#define JOULARENERGYMETER_AUTHOR "Adel Noureddine"
#define JOULARENERGYMETER_YEAR "2026"
#define JOULARENERGYMETER_LICENCE "GPL-3.0-only"
/* Linked from the bottom of the window */
#define JOULAR_HOME "https://github.com/joular/joular-energy-meter"

/* Shown for a value not measured yet */
#define NOTHING "–"

/* argv[0], for the command line suggested on screen (main.c) */
extern const char *program_path;

/* -------------------------------------------------------------------------------------- */
/* What is measured                                                          (app.c)      */
/* -------------------------------------------------------------------------------------- */

typedef enum { TARGET_WHOLE_SYSTEM, TARGET_PROCESS, TARGET_APPLICATION } target_kind;

/* Whole machine is always measured, plus optionally one process or app */
typedef struct target {
    target_kind kind;
    unsigned int pid;   /* TARGET_PROCESS */
    char app[128];      /* TARGET_APPLICATION, program name */
} target;

/* "Process 2814" or "firefox"; "" for the whole system. Returns buf. */
const char *target_name(const target *t, char *buf, size_t n);

/* Sampling interval stops.
 * Under 250 ms the energy counters barely move; over 10 s RAPL can wrap more than once
 * under load. */
typedef enum {
    MS_250, MS_500, MS_750, SEC_1, SEC_2, SEC_3, SEC_4, SEC_5, SEC_10,
    INTERVAL_COUNT
} interval_choice;

double interval_seconds(interval_choice c);
const char *interval_label(interval_choice c);   /* "250 ms", "1 s" */

/* Current interval. Main thread only.
 * Safe to change mid-run: each cycle is divided by its real duration. */
interval_choice chosen_interval(void);
void set_interval(interval_choice c);

/* A cycle lasting >= SLEEP_FACTOR intervals and >= SLEEP_SECONDS means the machine slept.
 * Counters have probably wrapped, so the cycle is dropped. Shorter stalls (e.g. powermetrics
 * restarting) are kept. */
#define SLEEP_FACTOR 5.0
#define SLEEP_SECONDS 10.0

/* Load or power of a process that couldn't be read (gone, never ran, access denied), or of
 * an app none of whose processes could be read. An app with no process running reads 0, as
 * in PowerJoular. Written as -1 in CSV like PowerJoular; never shown or summed as 0. */
#define UNREADABLE (-1.0)

/* One cycle, same fields as PowerJoular */
typedef struct cycle {
    double cpu_usage;     /* whole machine, 0.0 to 1.0 */
    double target_usage;  /* 0 for the whole system, UNREADABLE when not read */
    double cpu_power;     /* watts */
    double gpu_power;
    double total_power;
    double target_power;  /* target's share of CPU power; 0 for whole system, UNREADABLE when not read */
} cycle;

/* -------------------------------------------------------------------------------------- */
/* The hardware                                                              (monitor.c)  */
/* -------------------------------------------------------------------------------------- */
/* Opens Joular Core, samples CPU load, computes watts and session energy. No LVGL here.
 * Joular Core isn't thread safe: only call from the sampler.c thread. interval is in seconds. */

/* Opens the hardware and takes the first baseline. Still starts with no readable power
 * source, see monitor_cpu_available. */
void monitor_start(const target *t, double interval);

/* On macOS this stops the powermetrics child. Safe when nothing runs. */
void monitor_stop(void);

/* Pause keeps the session. Resume takes a new baseline so the paused energy isn't one huge
 * cycle; paused time is excluded from the duration. */
void monitor_pause(void);
void monitor_resume(double interval);

typedef enum {
    STEP_MEASURED,  /* *out holds a cycle */
    STEP_SKIPPED,   /* cycle too long, machine slept */
    STEP_FAILED     /* reading failed, can carry on */
} step_result;

/* *out only valid on STEP_MEASURED */
step_result monitor_step(double interval, cycle *out);

/* What was available at monitor_start */
bool monitor_cpu_available(void);
bool monitor_gpu_available(void);

/* False when CPU Load gets a machine time of zero */
bool monitor_load_readable(void);

/* Energy since start, in joules, and measured time. Pauses and dropped cycles excluded. */
typedef struct totals {
    double cpu_energy;
    double gpu_energy;
    double total_energy;
    double target_energy;  /* only cycles where the target was read */
    bool target_read;      /* target read at least once */
    double elapsed;        /* seconds */
} totals;

totals monitor_session(void);

/* Why the last start or step failed, "" if none */
const char *monitor_trouble(void);

/* -------------------------------------------------------------------------------------- */
/* The measuring thread                                                      (sampler.c)  */
/* -------------------------------------------------------------------------------------- */
/* Runs the monitor on its own thread so slow readings don't block the UI (powermetrics
 * takes ~1 s to start on macOS). Results come back as reports, delivered on the main
 * thread by sampler_deliver. */

typedef enum {
    REPORT_STARTED,   /* hardware opened */
    REPORT_MEASURED,  /* a cycle, with totals so far */
    REPORT_SKIPPED,   /* machine slept, cycle dropped */
    REPORT_FAILED     /* reading failed, carries on */
} report_kind;

typedef struct report {
    report_kind kind;
    bool has_cpu, has_gpu, load_readable;           /* REPORT_STARTED */
    int64_t taken;                                  /* REPORT_MEASURED, as fmt_now */
    cycle data;                                     /* REPORT_MEASURED */
    totals sum;                                     /* REPORT_MEASURED */
    char trouble[128];                              /* why open or reading failed */
} report;

/* on_report gets every report of the current session. False (reason on stderr) if the
 * thread can't start. Call platform_init first. */
bool sampler_init(void (*on_report)(const report *r));

/* Stops the thread, which releases the hardware */
void sampler_quit(void);

/* Ends any running session (its pending reports are dropped) and measures t */
void sampler_start(const target *t, double interval);
void sampler_stop(void);

void sampler_pause(bool paused);
void sampler_set_interval(double interval);

/* Passes pending reports to on_report. Called by the main loop when woken. */
void sampler_deliver(void);

/* -------------------------------------------------------------------------------------- */
/* The last measurements                                                     (history.c)  */
/* -------------------------------------------------------------------------------------- */
/* Ring buffer, oldest point overwritten when full */

#define HISTORY_CAPACITY 120

/* taken == 0 and all values UNREADABLE marks a gap */
typedef struct history_point {
    int64_t taken;   /* Unix time, seconds */
    cycle data;
} history_point;

void history_clear(void);
void history_append(const history_point *p);
int history_count(void);

/* 1 is the oldest, history_count() the newest. NULL out of range. */
const history_point *history_get(int i);

/* Seconds covered by a full history at the current interval */
double history_span(void);

typedef enum { SERIES_TOTAL, SERIES_CPU, SERIES_GPU, SERIES_TARGET } series;

double history_value(const history_point *p, series s);

/* Max value kept, 0 if empty */
double history_peak(series s);

/* -------------------------------------------------------------------------------------- */
/* CSV files in PowerJoular's format                                         (csv.c)      */
/* -------------------------------------------------------------------------------------- */
/* Same columns, 4 decimals, Unix timestamps. A followed process or app gets a second file
 * with its usage and power, -1 when unreadable. */

#define CSV_DEFAULT_FILE "joularenergymeter-power.csv"

/* For EXPORT, kept apart from the default recording file */
#define CSV_EXPORT_FILE "joularenergymeter-trend.csv"

/* "<system_file>-<pid>.csv" or "<system_file>-<app>.csv" like PowerJoular; system_file
 * for the whole system. Returns out. */
const char *csv_target_file(const char *system_file, const target *t, char *out, size_t n);

/* Files are created or appended to right away, so an unwritable path fails here */
bool csv_start_recording(const char *system_file, const target *t);

/* True when not recording */
bool csv_record_point(const history_point *p);

void csv_stop_recording(void);
bool csv_is_recording(void);

/* "" when not recording */
const char *csv_recording_file(void);

/* Overwrites the files with the whole history. Returns rows written, -1 on error. */
int csv_save_history(const char *system_file, const target *t);

/* Last write error, with strerror */
const char *csv_trouble(void);

/* Basename */
const char *csv_file_name(const char *path);

/* Absolute folder of an existing file; false if it can't be resolved */
bool csv_folder_of(const char *file, char *out, size_t n);

/* -------------------------------------------------------------------------------------- */
/* Numbers and times as text                                                 (format.c)   */
/* -------------------------------------------------------------------------------------- */
/* All write into buf, NUL-terminated and truncated, and return buf */

/* Rounded to decimals ("12.35" for 12.345 and 2) */
char *fmt_number(char *buf, size_t n, double value, int decimals);

/* Unix time in seconds, as in PowerJoular's CSV */
int64_t fmt_now(void);

/* "01:02:03" */
char *fmt_clock(char *buf, size_t n, double seconds);

/* "30 seconds", "2 minutes" */
char *fmt_span(char *buf, size_t n, double seconds);

/* -------------------------------------------------------------------------------------- */
/* The look                                                                  (theme.c)    */
/* -------------------------------------------------------------------------------------- */
/* Electricity meter look: pale teal housing, white dial plates, dark register with rolling
 * digits (tenths in red), disc edge in a slot. Teal/red for Start/Stop, amber for focus.
 * Colours are named by use. */

typedef enum {
    TOKEN_ENAMEL, TOKEN_ENAMEL_DEEP,                  /* housing; folds and rules */
    TOKEN_PLATE, TOKEN_PLATE_HOVER, TOKEN_PLATE_PRESSED,   /* plate, field, plain button */
    TOKEN_EDGE, TOKEN_GRID,                           /* control borders; trend grid */
    TOKEN_TEXT, TOKEN_MUTED, TOKEN_FAINT, TOKEN_ERROR,
    TOKEN_ACCENT,                                     /* links */
    TOKEN_FOCUS,                                      /* focus ring */
    TOKEN_OFF,                                        /* disabled button */
    TOKEN_GO, TOKEN_GO_HOVER, TOKEN_GO_PRESSED,       /* Start */
    TOKEN_HALT, TOKEN_HALT_HOVER, TOKEN_HALT_PRESSED, /* Stop, and the disc mark */
    TOKEN_REGISTER, TOKEN_WHEEL, TOKEN_WHEEL_LEAD,    /* register; digits; leading zeros */
    TOKEN_DISC, TOKEN_DISC_TICK,                      /* disc edge and ticks */
    TOKEN_LAMP_OFF, TOKEN_LAMP_RUN, TOKEN_LAMP_WAIT,  /* state lamp */
    TOKEN_ALARM, TOKEN_ALARM_EDGE, TOKEN_ALARM_TEXT,  /* warning */
    TOKEN_TOTAL, TOKEN_CPU, TOKEN_GPU, TOKEN_TARGET,  /* series colours */
    TOKEN_COUNT
} theme_token;

lv_color_t theme_color(theme_token t);

/* Once, after the display exists and before any widget */
void theme_init(lv_display_t *disp);

/* Logical pixels to screen pixels (x2 on Retina) */
int32_t ui_px(int32_t logical);

/* Embedded Barlow at a logical size. Falls back to Montserrat for LVGL symbols. */
const lv_font_t *ui_font(int32_t logical);

/* Barlow Semi Condensed, tabular digits so numbers don't jitter */
const lv_font_t *ui_figures(int32_t logical);

/* -------------------------------------------------------------------------------------- */
/* The widgets                                                               (kit.c)      */
/* -------------------------------------------------------------------------------------- */
/* All return the LVGL object, using theme.c's shared styles */

/* Text styles. All text is sentence case. */
typedef enum {
    TEXT_BODY,            /* 13 px */
    TEXT_MUTED,           /* 13 px, muted */
    TEXT_CAPTION,         /* 12 px, muted */
    TEXT_ERROR,           /* 13 px, error colour */
    TEXT_LINK,            /* 12 px, accent */
    TEXT_WORD,            /* 15 px, muted, words between the top-bar controls */
    TEXT_TITLE,           /* 15 px, plate title, row name */
    TEXT_READING,         /* 64 px figures, machine power */
    TEXT_READING_UNIT,    /* 22 px, muted */
    TEXT_READING_SECOND,  /* 40 px figures, target colour, target power */
    TEXT_ROW_FIGURE,      /* 20 px figures, row value */
    TEXT_FIGURES,         /* 15 px figures, muted, time and totals by the register */
    TEXT_WHEEL            /* 30 px figures, register digit */
} text_role;

lv_obj_t *kit_label(lv_obj_t *parent, const char *text, text_role role);

/* Only sets when the text changed, to avoid a redraw */
void kit_set_text(lv_obj_t *label, const char *text);

void kit_set_role(lv_obj_t *label, text_role role);

/* Max count lines, ellipsis-style dots when cut; returns label */
lv_obj_t *kit_lines(lv_obj_t *label, int32_t count);

/* Transparent flex row or column, gap in logical pixels.
 * Rows centre their children vertically. */
lv_obj_t *kit_box(lv_obj_t *parent, bool vertical, int32_t gap);

/* Lets children's outer drawing (focus rings etc.) spill reach logical pixels out of box */
void kit_let_out(lv_obj_t *box, int32_t reach);

/* Plate with a title row (none if title is NULL) and a body stacked gap logical pixels
 * apart. The title row has room for extra widgets beside the title. */
lv_obj_t *kit_panel(lv_obj_t *parent, const char *title, int32_t gap);
lv_obj_t *kit_panel_strip(lv_obj_t *panel);
lv_obj_t *kit_panel_body(lv_obj_t *panel);

/* Dotted leader between a name and its value, fills the rest of the row */
lv_obj_t *kit_leader(lv_obj_t *parent);

/* Amber warning row; plain labels inside get its text colour */
lv_obj_t *kit_alarm(lv_obj_t *parent);

/* Series marker: dot next to a total, or short line (wide) in a legend */
lv_obj_t *kit_dot(lv_obj_t *parent, theme_token color, bool wide);


/* Register of count digit wheels, last one red (tenths).
 * kit_register_set takes count digits; changed wheels roll (or jump if !roll), zeros left
 * of the units wheel are dimmed. */
lv_obj_t *kit_register(lv_obj_t *parent, int count);
void kit_register_set(lv_obj_t *reg, const char *digits, bool roll);

/* Disc edge in its slot with a red mark. Single instance.
 * kit_disc_turn sets total turns since start: eases forward when roll, otherwise jumps. */
lv_obj_t *kit_disc(lv_obj_t *parent);
void kit_disc_turn(lv_obj_t *disc, double turns, bool roll);

/* Glows when lit */
lv_obj_t *kit_lamp(lv_obj_t *parent);
void kit_lamp_set(lv_obj_t *lamp, theme_token color, bool lit);

typedef enum { BUTTON_NORMAL, BUTTON_GO, BUTTON_HALT } button_kind;

/* symbol is an LV_SYMBOL_* string or NULL, text may be NULL, or both set.
 * on_click gets LV_EVENT_CLICKED with user_data. kit_button_set changes the content and,
 * only between GO and HALT, the kind (Start -> Stop). */
lv_obj_t *kit_button(lv_obj_t *parent, const char *symbol, const char *text, button_kind kind,
                     lv_event_cb_t on_click, void *user_data);
void kit_button_set(lv_obj_t *button, const char *symbol, const char *text, button_kind kind);

/* One-line field. digits_only allows 0-9 only. Enter sends LV_EVENT_READY. */
lv_obj_t *kit_entry(lv_obj_t *parent, const char *placeholder, bool digits_only);
void kit_entry_set_invalid(lv_obj_t *entry, bool invalid);

/* on_change gets LV_EVENT_VALUE_CHANGED; read it with lv_obj_has_state(sw, LV_STATE_CHECKED) */
lv_obj_t *kit_switch(lv_obj_t *parent, lv_event_cb_t on_change, void *user_data);

/* Dropdown, options one per line, sized to the widest.
 * on_change gets LV_EVENT_VALUE_CHANGED; read it with lv_dropdown_get_selected. */
lv_obj_t *kit_picker(lv_obj_t *parent, const char *options, uint32_t chosen, lv_event_cb_t on_change,
                     void *user_data);

/* One option per interval_choice */
lv_obj_t *kit_interval_picker(lv_obj_t *parent, lv_event_cb_t on_change, void *user_data);
interval_choice kit_interval_selected(lv_obj_t *picker);

void kit_show(lv_obj_t *obj, bool visible);

/* Disabled: greyed, ignores mouse and keyboard */
void kit_enable(lv_obj_t *obj, bool enabled);

/* -------------------------------------------------------------------------------------- */
/* The window                                                                (ui.c)       */
/* -------------------------------------------------------------------------------------- */
/* Single screen built once: controls along the top, meter and trend next to the register
 * and readings, status bar at the bottom. */

/* Session state, drives the lamp, buttons and display */
typedef enum { RUN_IDLE, RUN_STARTING, RUN_LIVE, RUN_PAUSED } run_state;

/* Margin around the plates, logical pixels */
#define UI_GUTTER 16

void ui_build(void);

/* Status bar message, prefixed with the time; stays until the next one.
 * ui_trouble is for errors and stays highlighted. */
void ui_message(const char *text);
void ui_trouble(const char *text);

/* -------------------------------------------------------------------------------------- */
/* The controls and the display                           (ui_controls.c, ui_display.c)   */
/* -------------------------------------------------------------------------------------- */

/* Top bar: target, interval, CSV recording switch, state lamp and run buttons.
 * Start validates the fields, shows errors next to the file field, and starts CSV
 * recording if asked. */
lv_obj_t *controls_create(lv_obj_t *parent);

/* Updates lamp and buttons; locks the choices while running */
void controls_show_run(run_state run);

/* Meter, trend, register and readings. Reports arrive via display_report on the main
 * thread. Values stay on screen after Stop until the next Start. Builds into the left and
 * right columns. */
void display_create(lv_obj_t *left, lv_obj_t *right);

void display_start(const target *t);
void display_stop(void);
void display_pause(bool paused);

/* Writes the trend to CSV_EXPORT_FILE */
void display_export(void);

/* Call after set_interval: updates the sampler and the trend span */
void display_interval_changed(void);

run_state display_run(void);

/* Report callback passed to sampler_init */
void display_report(const report *r);

/* -------------------------------------------------------------------------------------- */
/* The window system                                                         (platform_sdl.c) */
/* -------------------------------------------------------------------------------------- */
/* SDL window feeding LVGL: display at native pixels (sharp on HiDPI), mouse, keyboard for
 * text fields, tick. All controls join LVGL's default group for keyboard nav. The main loop
 * sleeps until an event or the next LVGL timer, so idle costs nothing. */

/* w x h logical pixels, resizable down to min_w x min_h (smaller on small screens), hidden
 * until platform_run. Calls lv_init() and sets up display and inputs. False (reason on
 * stderr) if no window. */
bool platform_init(const char *title, int32_t w, int32_t h, int32_t min_w, int32_t min_h);

/* Shows the window and runs until closed, then hides it. on_wake runs on this thread after
 * each platform_wake. Returns the exit status. */
int platform_run(void (*on_wake)(void));

/* Call once no thread can call platform_wake any more */
void platform_close(void);

/* Makes the main loop run on_wake. The only function here safe from any thread. */
void platform_wake(void);

/* Physical per logical pixel: 2.0 on Retina, usually 1.0 */
float platform_scale(void);

void platform_set_clipboard(const char *text);

/* Opens url in the default browser; false on failure */
bool platform_open_url(const char *url);

/* Hand cursor over links, arrow otherwise */
void platform_set_hand_cursor(bool hand);

/* Blocks idle sleep while on (macOS and Windows). Screen may still sleep, lid close still
 * sleeps. */
void platform_keep_awake(bool on);

#endif /* JOULAR_ENERGY_METER_APP_H */
