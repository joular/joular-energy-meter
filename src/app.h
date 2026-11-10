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
#include <stdio.h>

#include "lvgl/lvgl.h"

#include "file_browser.h"

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

typedef enum { TARGET_WHOLE_SYSTEM, TARGET_PROCESS, TARGET_APPLICATION, TARGET_COMMAND } target_kind;

/* Whole machine is always measured, plus optionally one process, app or command */
typedef struct target {
    target_kind kind;
    unsigned int pid;     /* TARGET_PROCESS, and TARGET_COMMAND once started */
    char app[128];        /* TARGET_APPLICATION, program name */
    char command[1024];   /* TARGET_COMMAND, as typed */
} target;

/* "Process 2814", "firefox" or the command; "" for the whole system. Returns buf. */
const char *target_name(const target *t, char *buf, size_t n);

/* "Application firefox", "Process 2814", "Command python3 bench.py", "Whole machine" */
const char *target_heading(const target *t, char *buf, size_t n);

/* Sampling interval stops.
 * Under 1 s the CPU counters are too coarse (CPU Load reads a process in 10 ms units on
 * Linux, ~15 ms on Windows) and the energy counters barely move. 10 s at most, well before
 * RAPL's counters wrap (after minutes under load, see joularcore.h). */
typedef enum {
    SEC_1, SEC_2, SEC_3, SEC_4, SEC_5, SEC_10,
    INTERVAL_COUNT
} interval_choice;

double interval_seconds(interval_choice c);
const char *interval_label(interval_choice c);   /* "1 s", "10 s" */

/* Current interval. Main thread only.
 * Safe to change mid-run: each cycle is divided by its real duration. */
interval_choice chosen_interval(void);
void set_interval(interval_choice c);

/* The wall clock gets this far ahead of the monotonic clock across a cycle only when the
 * machine slept. Counters have probably wrapped, so the cycle is dropped. A clock set forward
 * drops one cycle too. */
#define RESUME_SECONDS 2.0

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
    double self_usage;    /* this meter's share of the machine, UNREADABLE when not read */
    double plug_power;    /* watts at the plug meter, UNREADABLE without one or a fresh row */
} cycle;

/* -------------------------------------------------------------------------------------- */
/* The hardware                                                              (monitor.c)  */
/* -------------------------------------------------------------------------------------- */
/* Opens Joular Core, samples CPU load, computes watts and session energy. No LVGL here.
 * Joular Core isn't thread safe: only call from the sampler.c thread. */

/* A plug meter is followed through the CSV file it writes, "time;watts" or "time,watts" per
 * row as PowerSpyCli does: a cycle gets the average of the rows added during it, or the last one,
 * and none after this long without a new row */
#define PLUG_STALE_SECONDS 5.0

/* Opens the hardware and takes the first baseline. Still starts with no readable power
 * source, see monitor_cpu_available. plug: the plug meter's file, "" for none. */
void monitor_start(const target *t, const char *plug);

/* On macOS this stops the powermetrics child. Safe when nothing runs. */
void monitor_stop(void);

/* Pause keeps the session. Resume takes a new baseline so the paused energy isn't one huge
 * cycle; paused time is excluded from the duration. */
void monitor_pause(void);
void monitor_resume(void);

typedef enum {
    STEP_MEASURED,  /* *out holds a cycle */
    STEP_SKIPPED,   /* machine slept during the cycle */
    STEP_FAILED     /* reading failed, can carry on */
} step_result;

/* *out only valid on STEP_MEASURED */
step_result monitor_step(cycle *out);

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
    double plug_energy;    /* only cycles with a plug reading, over plug_time */
    double plug_time;
    bool plug_read;
} totals;

totals monitor_session(void);

/* Why the last start or step failed, or why its target couldn't be read; "" if nothing */
const char *monitor_trouble(void);

/* The followed process or command has ended; exit_code is the command's, else -1 */
bool monitor_ended(int *exit_code);

/* -------------------------------------------------------------------------------------- */
/* A command to measure                                                      (command.c)  */
/* -------------------------------------------------------------------------------------- */
/* Run in the current folder through /bin/sh on POSIX, as a program and its arguments on
 * Windows. As the user: under sudo, the one who ran sudo. In a session of its own, so it goes
 * on after Stop. One at a time, measuring thread only. */

/* False, with the reason in trouble, if it couldn't start */
bool command_start(const char *line, unsigned int *pid, char *trouble, size_t n);

/* True once it has ended, with its exit status (128 + the signal's number when killed, -1 if
 * unknown) and its own CPU time at the end in microseconds (-1 if unknown), which CPU Load
 * can't read from an ended process */
bool command_ended(int *exit_code, int64_t *used);

/* Stops following it, it goes on */
void command_forget(void);

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
    char trouble[128];                              /* why open or reading failed, or why the
                                                     * target couldn't be read */
    int dropped;                                    /* reports lost just before this one, the
                                                     * queue being full */
    bool ended;                                     /* the followed process or command has
                                                     * ended (at REPORT_STARTED: the command
                                                     * couldn't start) */
    int exit_code;                                  /* the command's once ended, -1 if unknown */
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

/* The plug meter's file for the next sessions, "" for none */
void sampler_set_plug(const char *file);

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

typedef enum { SERIES_TOTAL, SERIES_CPU, SERIES_GPU, SERIES_TARGET, SERIES_PLUG, SERIES_COUNT } series;

double history_value(const history_point *p, series s);

/* Max value kept, 0 if empty */
double history_peak(series s);

/* -------------------------------------------------------------------------------------- */
/* CSV files in PowerJoular's format                                         (csv.c)      */
/* -------------------------------------------------------------------------------------- */
/* Same columns, 4 decimals, Unix timestamps. A followed process or app gets a second file
 * with its usage and power, -1 when unreadable. */

#define CSV_DEFAULT_FILE "joularenergymeter-power.csv"

/* power.csv gives power-<pid>.csv or power-<app>.csv like PowerJoular; system_file
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

/* Last write error, with strerror */
const char *csv_trouble(void);

/* Basename */
const char *csv_file_name(const char *path);

/* Opens name (UTF-8) to append to it, or to replace what it holds. Runs as root, so only a
 * regular file with a single link, never through a symlink that could point anywhere. Files
 * created under sudo are chowned to SUDO_UID/SUDO_GID. NULL with errno set on failure. */
FILE *open_user_file(const char *name, bool append);

#ifdef PJ_WINDOWS
/* A UTF-8 path for the wide calls */
bool wide(const char *path, wchar_t *out, int n);
#else
#include <sys/types.h>
/* Run as root by sudo, or by Restart with admin rights: the user who asked, from SUDO_UID and
 * SUDO_GID */
bool sudo_user(uid_t *uid, gid_t *gid);
#endif

/* -------------------------------------------------------------------------------------- */
/* Choices kept between launches                                             (settings.c) */
/* -------------------------------------------------------------------------------------- */
/* key=value lines in the settings folder of whoever started the app (under sudo, of the user
 * who ran sudo): Application Support on macOS, ~/.config on Linux and FreeBSD, %APPDATA% on
 * Windows. A missing or unreadable file gives the defaults. */

/* Once, before ui_build */
void settings_load(void);

/* NULL when not kept */
const char *settings_get(const char *key);

/* Where the kept value is among names, otherwise when it isn't */
int settings_index(const char *key, const char *const names[], int count, int otherwise);

void settings_set(const char *key, const char *value);

/* Asks the controls and the display for their choices, then writes the file. A failure only
 * goes to stderr. */
void settings_save(void);

/* -------------------------------------------------------------------------------------- */
/* Carbon intensity of electricity                                           (carbon.c)   */
/* -------------------------------------------------------------------------------------- */
/* Grams of CO2 equivalent per kWh, over the life cycle. Countries' yearly averages come from
 * Ember, in carbon-intensity.csv beside the program (in the app's Resources on macOS) or in
 * ../share/joularenergymeter, else from the built-in copy. Energy sources are the IPCC's
 * medians. */

#define CARBON_FILE "carbon-intensity.csv"
#define CARBON_MONTHLY_FILE "carbon-intensity-monthly.csv"

typedef struct electricity {
    char code[4];     /* ISO 3166-1 alpha-3 of a country, "" for the world or a source */
    char name[48];
    int year;         /* of a country's average, 0 for a source */
    double grams;     /* per kWh */
} electricity;

/* Once, before the others */
void carbon_load(void);

/* World first, then the countries by name */
int carbon_country_count(void);
const electricity *carbon_country(int i);

/* Country of the time zone (on Windows, of the region setting), else the world */
int carbon_local_country(void);

/* A country's (or the world's) intensity in a calendar month, 1 to 12, from the latest year
 * Ember has that month; false where Ember has no monthly figures (fewer countries) */
bool carbon_month(const electricity *e, int month, int *year, double *grams);

/* From the lowest to the highest */
int carbon_source_count(void);
const electricity *carbon_source(int i);

/* -------------------------------------------------------------------------------------- */
/* Numbers and times as text                                                 (format.c)   */
/* -------------------------------------------------------------------------------------- */
/* All write into buf, NUL-terminated and truncated, and return buf */

/* Rounded to decimals ("12.35" for 12.345 and 2) */
char *fmt_number(char *buf, size_t n, double value, int decimals);

/* Watts as a number, and its unit in unit: mW under 1 W, so a small figure never reads 0.0 */
char *fmt_power(char *buf, size_t n, double watts, const char **unit);

/* Unix time in seconds, as in PowerJoular's CSV */
int64_t fmt_now(void);

/* "01:02:03" */
char *fmt_clock(char *buf, size_t n, double seconds);

/* "30 seconds", "2 minutes" */
char *fmt_span(char *buf, size_t n, double seconds);

/* -------------------------------------------------------------------------------------- */
/* The look                                                                  (theme.c)    */
/* -------------------------------------------------------------------------------------- */
/* Joular Code GUI's look, light or dark: a painted ground with two glows, frosted cards with a
 * coloured tile, teal/red for Start/Stop, amber for focus and warnings. Colours are named by
 * use. */

typedef enum {
    TOKEN_ENAMEL, TOKEN_ENAMEL_DEEP, TOKEN_RULE,      /* ground; folds; rules in cards */
    TOKEN_GROUND_END, TOKEN_GLOW_SEA, TOKEN_GLOW_SUN, /* the ground's gradient and glows */
    TOKEN_PLATE, TOKEN_PLATE_HOVER, TOKEN_PLATE_PRESSED,   /* card, plain button */
    TOKEN_GLASS_EDGE, TOKEN_SHADE,                    /* cards' edge; shadows and dimming */
    TOKEN_EDGE, TOKEN_GRID,                           /* button borders; chart grid */
    TOKEN_FIELD, TOKEN_FIELD_EDGE,                    /* fields and pickers */
    TOKEN_TEXT, TOKEN_MUTED, TOKEN_FAINT, TOKEN_ERROR,
    TOKEN_ACCENT, TOKEN_ACCENT_SOFT,                  /* links, chosen; tinted */
    TOKEN_FOCUS,                                      /* focus ring */
    TOKEN_OFF,                                        /* disabled button */
    TOKEN_GO, TOKEN_GO_HOVER, TOKEN_GO_PRESSED,       /* Start */
    TOKEN_HALT, TOKEN_HALT_HOVER, TOKEN_HALT_PRESSED, /* Stop */
    TOKEN_LAMP_OFF, TOKEN_LAMP_RUN, TOKEN_LAMP_WAIT,  /* state lamp */
    TOKEN_ALARM, TOKEN_ALARM_EDGE, TOKEN_ALARM_TEXT,  /* warning */
    TOKEN_ENERGY, TOKEN_ON_ENERGY, TOKEN_ON_COLOUR,   /* amber tile; signs on tiles */
    TOKEN_TILE_BLUE, TOKEN_TILE_TEAL, TOKEN_TILE_GREY,
    TOKEN_TOTAL, TOKEN_CPU, TOKEN_GPU, TOKEN_TARGET, TOKEN_PLUG,  /* series colours */
    TOKEN_COUNT
} theme_token;

/* In the look in use */
lv_color_t theme_color(theme_token t);

/* After the display exists and the settings are loaded, before any widget; again at a new zoom
 * or look. The look comes from the setting "look": light, dark, or the system's. */
void theme_init(lv_display_t *disp);

bool theme_is_dark(void);

/* The logo, with its black lettering in the text's colour in the dark look */
const void *theme_logo(const lv_image_dsc_t *logo);

/* Logical pixels to screen pixels (x2 on Retina) */
int32_t ui_px(int32_t logical);

/* Embedded Barlow Medium at a logical size. Falls back to Lucide for LVGL symbols, then
 * Montserrat. */
const lv_font_t *ui_font(int32_t logical);

/* Barlow SemiBold: titles and the chosen choice */
const lv_font_t *ui_bold(int32_t logical);

/* Lucide signs at Font Awesome codepoints LVGL has no symbol for */
#define SYMBOL_SUN "\xEF\x86\x85"
#define SYMBOL_MOON "\xEF\x86\x86"
#define SYMBOL_CPU "\xEF\x8B\x9B"
#define SYMBOL_PLUG "\xEF\x87\xA6"

/* Barlow Semi Condensed, tabular digits so numbers don't jitter */
const lv_font_t *ui_figures(int32_t logical);

/* -------------------------------------------------------------------------------------- */
/* The widgets                                                               (kit.c)      */
/* -------------------------------------------------------------------------------------- */
/* All return the LVGL object, using theme.c's shared styles */

/* Text styles. All text is sentence case. */
typedef enum {
    TEXT_MUTED,           /* 13 px, muted */
    TEXT_CAPTION,         /* 12 px, muted */
    TEXT_LINK,            /* 12 px, accent */
    TEXT_WORD,            /* 15 px, muted, words between the top-bar controls */
    TEXT_TITLE,           /* 16 px, plate title, reading name, run state */
    TEXT_READING,         /* 52 px figures, first reading's power */
    TEXT_READING_UNIT,    /* 20 px, muted */
    TEXT_READING_SECOND,  /* 38 px figures, the machine's power beside the program's */
    TEXT_SECOND_UNIT,     /* 18 px, muted */
    TEXT_GROUP_FIGURE,    /* 28 px figures, energy and carbon */
    TEXT_GROUP_UNIT,      /* 16 px, muted */
    TEXT_ROW_FIGURE,      /* 18 px figures, breakdown value */
    TEXT_FIGURES          /* 15 px figures, muted, elapsed time */
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

/* Frosted card: a head with a coloured tile (symbol, or three bars for NULL) and the title
 * (none for NULL), then the caller's widgets (kit_panel_strip gives the head); and a body
 * stacked gap logical pixels apart */
lv_obj_t *kit_panel(lv_obj_t *parent, const char *symbol, theme_token tile, const char *title, int32_t gap);
lv_obj_t *kit_panel_strip(lv_obj_t *panel);
lv_obj_t *kit_panel_body(lv_obj_t *panel);

/* A small tag, such as "Optional" */
lv_obj_t *kit_chip(lv_obj_t *parent, const char *text);

/* A card of the Setup page */
lv_obj_t *kit_setup_card(lv_obj_t *page, const char *symbol, theme_token tile, const char *title);

/* A setting in a card: its name and a short explanation at the left, its controls at the right,
 * added to kit_row_controls. The name and explanation can change. */
lv_obj_t *kit_row(lv_obj_t *panel, const char *name, const char *explanation);
lv_obj_t *kit_row_controls(lv_obj_t *row);
lv_obj_t *kit_row_name(lv_obj_t *row);
lv_obj_t *kit_row_note(lv_obj_t *row);

/* Square of colour with a sign half its size, logical px; three bars for symbol NULL */
lv_obj_t *kit_tile(lv_obj_t *parent, const char *symbol, theme_token color, int32_t size);

/* Amber warning row; plain labels inside get its text colour */
lv_obj_t *kit_alarm(lv_obj_t *parent);

/* Series marker, a dot */
lv_obj_t *kit_dot(lv_obj_t *parent, theme_token color);

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

/* The sunken track that joins kit_choice choices */
lv_obj_t *kit_choices(lv_obj_t *parent);

/* One of a row of choices, with the series dot (none for TOKEN_COUNT). Chosen = LV_STATE_CHECKED, set by the caller.
 * on_click gets LV_EVENT_CLICKED with user_data. kit_choice_set changes the name. */
lv_obj_t *kit_choice(lv_obj_t *parent, theme_token color, const char *text, lv_event_cb_t on_click,
                     void *user_data);
void kit_choice_set(lv_obj_t *choice, const char *text);

/* on_change gets LV_EVENT_VALUE_CHANGED; read it with lv_obj_has_state(sw, LV_STATE_CHECKED) */
lv_obj_t *kit_switch(lv_obj_t *parent, lv_event_cb_t on_change, void *user_data);

/* Clicking label toggles control, sending it LV_EVENT_VALUE_CHANGED */
void kit_switch_label(lv_obj_t *label, lv_obj_t *control);

/* Copies text to the clipboard; key, a kit_button, says "Copied" for two seconds */
void kit_copy(lv_obj_t *key, const char *text);

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

/* The file browser in this look, to choose the CSV file to record to: start as in
 * file_browser_options, name proposed. Rows are appended to a file that exists, so it never
 * asks to replace one. on_choose gets the full path. */
void kit_save_csv(const char *title, const char *start, const char *name, file_browser_cb on_choose);

/* The same, to choose a CSV file that exists */
void kit_open_csv(const char *title, const char *start, file_browser_cb on_choose);

/* A menu under button (over it when there is no room below): rows added with kit_menu_add,
 * then kit_menu_open. on_pick gets the row's index, the menu closes itself. Esc or a click
 * outside closes it too. */
typedef void (*kit_menu_cb)(int row);
lv_obj_t *kit_menu(lv_obj_t *button, kit_menu_cb on_pick);

/* A dialog in the middle of the window, over a dimmed backdrop: its plate, width in logical px.
 * Its keys join its keyboard group with kit_dialog_take; kit_dialog_close deletes it. */
lv_obj_t *kit_dialog(int32_t width);
void kit_dialog_take(lv_obj_t *plate, lv_obj_t *obj);
void kit_dialog_close(lv_obj_t *plate);
void kit_menu_add(lv_obj_t *menu, const char *title, bool chosen);
void kit_menu_open(lv_obj_t *menu);

/* Hover help: half a second over obj, and text shows in a small plate by it. obj becomes
 * clickable, as hover only reaches that; a label doesn't join the keys for it. Called again,
 * the text changes; "" shows none. Mouse only: say it in the window too when it matters. */
void kit_tip(lv_obj_t *obj, const char *text);

/* -------------------------------------------------------------------------------------- */
/* The window                                                                (ui.c)       */
/* -------------------------------------------------------------------------------------- */
/* The header with the Setup and Readings tabs, the warning banner, the two pages, and the bar at
 * the bottom with the session's state, messages and the main keys. */

/* Session state, drives the lamp, buttons and display */
typedef enum { RUN_IDLE, RUN_STARTING, RUN_LIVE, RUN_PAUSED } run_state;

/* Margin around the plates, logical pixels */
#define UI_GUTTER 16

/* Most a Setup card widens to, logical pixels */
#define SETUP_WIDTH 780

void ui_build(void);

/* The pages, chosen by the header's tabs */
typedef enum { PAGE_SETUP, PAGE_READINGS, PAGE_COUNT } ui_page;
void ui_show_page(ui_page page);

/* The zoom kept from the last time, before theme_init */
void ui_zoom_init(void);

/* Zoom from 80 to 150 %: step 1 or -1 to the next, 0 back to 100 %. Builds the screen again,
 * keeping the choices, the session and the banner. */
void ui_zoom(int step);

/* The bar's message, prefixed with the time; stays until the next one.
 * ui_trouble is for errors and stays highlighted. */
void ui_message(const char *text);
void ui_trouble(const char *text);

/* Warning banner under the header: title, text under it (may be ""), and a command to copy
 * on its own line (may be ""). Replaces the one shown. */
void ui_alarm(const char *title, const char *text, const char *command);
void ui_alarm_clear(void);

/* A key in the banner shown, before Copy command: on_click NULL shows it disabled, label NULL
 * hides it. ui_alarm hides it. */
void ui_alarm_action(const char *label, void (*on_click)(void));

/* -------------------------------------------------------------------------------------- */
/* The controls and the display                           (ui_controls.c, ui_display.c)   */
/* -------------------------------------------------------------------------------------- */

/* The idle power dialog (ui_idle.c): measures the whole machine for a while on its own, apart
 * from the session on screen. on_use gets the processor's and the graphics card's average
 * power when the user keeps them. idle_dialog_report takes the sampler's reports while it
 * measures; idle_dialog_close stops it and closes it. */
void idle_dialog_open(void (*on_use)(double cpu, double gpu));
/* Opens it as the Measure key does (--idle) */
void display_measure_idle(void);
bool idle_dialog_report(const report *r);
void idle_dialog_close(void);

/* The What to measure card on the Setup page: target, interval, length, CSV recording.
 * Start validates the fields, shows errors in the banner, and starts CSV recording if
 * asked. */
void controls_create(lv_obj_t *page);

/* The bar's lamp, state, time and count, Pause and Start/Stop, after controls_create. Returns
 * the box under the state, where the messages go. */
lv_obj_t *controls_bar(lv_obj_t *bar);

/* Updates lamp and keys; the choices lock while running. stopped: idle, the last session's
 * values still shown */
void controls_show_run(run_state run, bool stopped);

/* Measured time and count in the bar */
void controls_show_progress(double elapsed, int samples, int missed);


/* Pass their choices to settings_set; both are built from settings_get */
void controls_keep(void);
void display_keep(void);

/* Start pressed: checks the fields and starts, unless a session runs */
void controls_start(void);

/* Session length chosen, in seconds: 0 until stopped, -1 until the followed process or
 * command ends */
double controls_length(void);

/* The Electricity and Idle power cards of the Setup page, before display_create */
void display_settings(lv_obj_t *page);

/* Readings, breakdown, electricity and chart, built into the Readings page. Reports arrive via
 * display_report on the main thread. Values stay on screen after Stop until the next
 * Start. */
void display_create(lv_obj_t *page);

void display_start(const target *t);
void display_stop(void);
void display_pause(bool paused);

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

/* The layout's size, 1 by default: ui_px and ui_font follow it, and the smallest window too
 * (within the screen). The screen must be built again after a change. */
void platform_set_zoom(float zoom);
float platform_zoom(void);

/* The smallest window at this zoom fits the screen the window is on */
bool platform_zoom_fits(float zoom);

/* Ctrl or Cmd with +, - or 0 calls on_zoom with 1, -1 or 0, outside LVGL's own handling */
void platform_on_zoom(void (*on_zoom)(int step));

/* Folder of the program with a trailing separator, the app's Resources on macOS; "" if
 * unknown */
const char *platform_program_folder(void);

void platform_set_clipboard(const char *text);

/* Opens url in the default browser; false on failure */
bool platform_open_url(const char *url);

/* Hand cursor over links, arrow otherwise */
void platform_set_hand_cursor(bool hand);

/* Blocks idle sleep while on (macOS and Windows). Screen may still sleep, lid close still
 * sleeps. */
void platform_keep_awake(bool on);

/* Root, or an elevated administrator on Windows */
bool platform_privileged(void);

/* Ends platform_run as closing the window does */
void platform_quit(void);

/* The system's look is dark: macOS's appearance (the sudo user's under sudo), GNOME's colour
 * scheme, Windows' app mode. False when unknown. */
bool platform_dark_look(void);

#ifndef PJ_WINDOWS
/* An executable of that name in PATH */
bool platform_in_path(const char *name);
#endif

/* The window's title bar in the look, where the system allows it (macOS, Windows) */
void platform_dark_title(bool dark);

/* -------------------------------------------------------------------------------------- */
/* Power readings without a terminal                                         (elevate.c)  */
/* -------------------------------------------------------------------------------------- */
/* The system's own dialog asks for the administrator, never a password field of ours */

typedef enum {
    ELEVATE_NONE,     /* nothing to offer */
    ELEVATE_ALLOW,    /* Linux: pkexec makes the RAPL counters readable until the next boot */
    ELEVATE_RESTART   /* macOS, Windows: start again as the administrator, with an option */
} elevate_way;

/* What can be offered now for the power readings */
elevate_way elevate_offer(void);

/* Opens the dialog. on_done runs later on the main thread: granted, or why not ("" when
 * cancelled). A restart saves the settings first, passes option (--start or --idle) and quits
 * once the new program started, so on_done only hears of a refusal. False, with the reason in
 * error, if no dialog could open. */
bool elevate_start(const char *option, void (*on_done)(bool granted, const char *why), char *error, size_t n);

/* Why no power can be read here, the command that makes it readable ("" for none) and the
 * key's words for what the system offers (NULL for none), as the warning banner says them
 * (ui_display.c) */
void display_power_help(const char **hint, const char **command, const char **key);

#endif /* JOULAR_ENERGY_METER_APP_H */
