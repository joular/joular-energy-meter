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

/* Display: meter, trend, register and the session's reading rows. Reports from the
 * measuring thread arrive here on the main thread. */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef PJ_WINDOWS
#include <unistd.h>
#endif

#include "app.h"

/* Series colours, in series enum order */
static const theme_token pen[] = { TOKEN_TOTAL, TOKEN_CPU, TOKEN_GPU, TOKEN_TARGET };

/* Register wheels: six for whole units, the red one for tenths */
#define WHEELS 7

/* Widths of the two value columns in the rows, logical pixels */
#define NOW_WIDTH 76
#define SINCE_WIDTH 88

/* Reading rows: name, value now, energy since start */
enum { ROW_CPU, ROW_GPU, ROW_LOAD, ROW_TARGET, ROW_SHARE, ROW_TOTAL_SHARE, ROW_COUNT };

typedef struct row {
    lv_obj_t *box, *name, *now, *since;
} row;

static row rows[ROW_COUNT];

/* CPU/GPU split of the machine's power, a bar and a key with each share. Only shown with
 * a GPU. */
enum { SPLIT_CPU, SPLIT_GPU, SPLIT_COUNT };
static lv_obj_t *split_box, *split_part[SPLIT_COUNT], *split_key[SPLIT_COUNT], *split_share[SPLIT_COUNT];

/* Session stats, each under its name */
enum { STAT_AVERAGE, STAT_SPREAD, STAT_HIGHEST, STAT_LOWEST, STAT_YEAR, STAT_SAMPLES, STAT_COUNT };
static const char *const stat_names[STAT_COUNT] = {
    "Average", "Standard deviation", "Highest", "Lowest", "A year at this average", "Measurements",
};
static lv_obj_t *session_panel, *stat_name[STAT_COUNT], *stat_value[STAT_COUNT];

/* Machine power over the session: mean and Welford's running sum of squares, highest and
 * lowest with their times, and the count of dropped or failed measurements */
static double mean, squares, weight, measured, peak, low;
static int64_t peak_taken, low_taken;
static int missed;

/* Meter: machine power, followed program's power beside it, register above the disc that
 * turns with the machine's energy */
static lv_obj_t *reading, *second_box, *second_name, *second, *disc;
static lv_obj_t *target_panel;
static double joules_per_turn;   /* 0 until the first measurement sets it */
static double turns, counted;    /* disc turns so far, and the energy they represent */

/* Register and its labels */
static lv_obj_t *wheels, *wheels_unit, *clock_label, *watt_hours;
static int register_unit = -1;

static lv_obj_t *alarm_strip, *alarm_title, *alarm_text, *copy_button;
static char alarm_command[1024];

static lv_obj_t *span_label, *export_key, *cpu_key, *gpu_key, *target_key, *target_key_name;
static lv_obj_t *scale, *chart, *chart_note, *ago_far, *ago_mid;
static lv_chart_series_t *lines[4];
static int32_t points[4][HISTORY_CAPACITY];

/* Band labels, static as lv_scale keeps the pointers */
static char band_text[5][16];
static const char *band_labels[6];

static target session_target;
static char heading[160];   /* "Whole machine", "Process 2814", "Application firefox" */
static int samples;
static run_state run = RUN_IDLE;
static bool has_cpu, has_gpu, load_reads;
static bool target_lost;   /* followed program unreadable at the last cycle */

/* -------------------------------------------------------------------------------------- */
/* Text                                                                                   */
/* -------------------------------------------------------------------------------------- */

static bool follows(void)
{
    return session_target.kind != TARGET_WHOLE_SYSTEM;
}

/* Assume the CPU is readable until the hardware is open */
static bool cpu_expected(void)
{
    return has_cpu || run == RUN_STARTING;
}

/* "90 s ago", "2 min ago" or "now", for back seconds */
static char *ago(char *buf, size_t n, double back)
{
    int seconds = (int)back;

    if (seconds == 0)
        snprintf(buf, n, "now");
    else if (seconds >= 60 && seconds % 60 == 0)
        snprintf(buf, n, "%d min ago", seconds / 60);
    else
        snprintf(buf, n, "%d s ago", seconds);
    return buf;
}

static void say(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void warn(const char *format, ...) __attribute__((format(printf, 1, 2)));

static void say(const char *format, ...)
{
    char text[1200];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    ui_message(text);
}

/* Error, the notice stays lit */
static void warn(const char *format, ...)
{
    char text[1200];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    ui_trouble(text);
}

/* Smallest 1, 2, 2.5 or 5 times a power of ten that is >= above. Rounded steps keep the
 * axis steady while the peak moves a little. */
static double nice_step(double above)
{
    static const double multiples[] = { 1.0, 2.0, 2.5, 5.0 };
    double power = 0.001;

    for (int decade = 0; decade < 12; decade++) {
        for (size_t m = 0; m < 4; m++)
            if (multiples[m] * power >= above) return multiples[m] * power;
        power *= 10.0;
    }
    return above;
}

/* Decimals needed to write a step exactly (2.5 needs one) */
static int decimals_of(double step)
{
    for (int decimals = 0; decimals <= 2; decimals++) {
        if (fabs(step - round(step)) < 1e-6) return decimals;
        step *= 10.0;
    }
    return 3;
}

/* -------------------------------------------------------------------------------------- */
/* Showing things                                                                         */
/* -------------------------------------------------------------------------------------- */

/* A row of two fonts aligns bottoms; this lifts the smaller onto the larger's baseline */
static void on_baseline(lv_obj_t *label, const lv_font_t *large, const lv_font_t *small)
{
    lv_obj_set_style_pad_bottom(label, large->base_line - small->base_line, 0);
}

/* Fresh values in ink; held ones (Pause, Stop) or none yet in the muted colour */
static void show_fresh(void)
{
    static int shown = -1;
    bool live = run == RUN_LIVE;

    if (shown == (int)live) return;
    shown = live;
    lv_obj_set_style_text_color(reading, theme_color(live ? TOKEN_TEXT : TOKEN_MUTED), 0);
    lv_obj_set_style_text_color(second, theme_color(live ? TOKEN_TARGET : TOKEN_MUTED), 0);
    for (int i = 0; i < ROW_COUNT; i++)
        lv_obj_set_style_text_color(rows[i].now, theme_color(live ? TOKEN_TEXT : TOKEN_MUTED), 0);
}

/* Number with its unit in the muted colour; NULL number shows nothing */
static void show_figure(lv_obj_t *label, const char *number, const char *unit)
{
    static char muted[8];
    char text[64];

    if (number == NULL) {
        kit_set_text(label, NOTHING);
        return;
    }
    if (muted[0] == '\0') {
        lv_color_t c = theme_color(TOKEN_MUTED);
        snprintf(muted, sizeof muted, "%02x%02x%02x", c.red, c.green, c.blue);
    }
    snprintf(text, sizeof text, "%s #%s %s#", number, muted, unit);
    kit_set_text(label, text);
}

static void show_watts(lv_obj_t *label, double watts, bool available)
{
    char text[32];

    show_figure(label, available && watts >= 0.0 ? fmt_number(text, sizeof text, watts, 1) : NULL, "W");
}

/* Share of the whole machine; negative means unreadable */
static void show_load(lv_obj_t *label, double load)
{
    char text[32];

    show_figure(label, load >= 0.0 ? fmt_number(text, sizeof text, 100.0 * load, 1) : NULL, "%");
}

/* At most three digits before the point and four in all, so a long session's total fits its
 * column and keeps the same precision */
static void show_joules(lv_obj_t *label, double value, bool available)
{
    static const char *const units[] = { "J", "kJ", "MJ", "GJ" };
    char number[32];
    int unit = 0;

    /* 999.95 and up would round to 1000.0 */
    while (value >= 999.95 && unit < 3) {
        value /= 1000.0;
        unit++;
    }
    show_figure(label, available ? fmt_number(number, sizeof number, value, value < 9.9995 ? 3 : value < 99.995 ? 2 : 1)
                                 : NULL, units[unit]);
}

/* Joules to the tenth on six wheels plus the red one, then kJ and so on. Wheels roll only
 * while the unit stays the same */
static void show_register(double joules, bool roll)
{
    static const char *const units[] = { "J", "kJ", "MJ", "GJ" };
    char digits[32], text[32];
    int unit = 0;
    double wh = joules / 3600.0;

    while (joules >= 999999.95 && unit < 3) {
        joules /= 1000.0;
        unit++;
    }
    snprintf(digits, sizeof digits, "%08.1f", fmin(joules, 999999.9));
    /* Drop the point, the red wheel is the tenths */
    memmove(digits + 6, digits + 7, 2);
    kit_register_set(wheels, digits, roll && unit == register_unit);
    if (unit != register_unit) {
        register_unit = unit;
        kit_set_text(wheels_unit, units[unit]);
    }
    /* Same ranging as the row energies: four figures, then kWh and MWh */
    static const char *const wh_units[] = { "Wh", "kWh", "MWh" };
    int wh_unit = 0;
    while (wh >= 999.95 && wh_unit < 2) {
        wh /= 1000.0;
        wh_unit++;
    }
    show_figure(watt_hours, fmt_number(text, sizeof text, wh, wh < 9.9995 ? 3 : wh < 99.995 ? 2 : 1), wh_units[wh_unit]);
}

/* Turn the disc by each sample's energy */
static void turn_disc(double joules, bool roll)
{
    double step = joules - counted;

    if (step <= 0.0) return;
    /* A turn is a round number of joules, about 8 samples per turn. Picked again if the power
     * changes so much that a sample would spin it or barely move it */
    if (joules_per_turn == 0.0 || step > joules_per_turn / 2.0 || step < joules_per_turn / 64.0)
        joules_per_turn = nice_step(8.0 * step);
    turns += step / joules_per_turn;
    counted = joules;
    kit_disc_turn(disc, turns, roll);
}

static void show_span(void)
{
    char text[32];
    double span = history_span();

    lv_label_set_text_fmt(span_label, "Trend, last %s", fmt_span(text, sizeof text, span));
    lv_label_set_text(ago_far, ago(text, sizeof text, span));
    lv_label_set_text(ago_mid, ago(text, sizeof text, span / 2.0));
}

/* Measurement count, and how many were missed */
static void show_samples(void)
{
    char text[48];

    lv_label_set_text_fmt(stat_value[STAT_SAMPLES], "%d", samples);
    if (missed > 0)
        snprintf(text, sizeof text, "%s, %d missed", stat_names[STAT_SAMPLES], missed);
    else
        snprintf(text, sizeof text, "%s%s", stat_names[STAT_SAMPLES], samples > 0 ? ", none missed" : "");
    kit_set_text(stat_name[STAT_SAMPLES], text);
}

/* Lamp, buttons and readings follow the run state */
static void set_run(run_state now)
{
    run = now;
    show_fresh();
    /* Keep the machine from sleeping during a run */
    platform_keep_awake(run == RUN_STARTING || run == RUN_LIVE);
    kit_enable(export_key, history_count() > 0);
    controls_show_run(run);
}

/* -------------------------------------------------------------------------------------- */

static void show_alarm(const char *title, const char *text, const char *command)
{
    lv_label_set_text(alarm_title, title);
    /* Command on its own line, readable before copying */
    if (command[0])
        lv_label_set_text_fmt(alarm_text, "%s\n%s", text, command);
    else
        lv_label_set_text(alarm_text, text);
    snprintf(alarm_command, sizeof alarm_command, "%s", command);
    kit_show(copy_button, command[0] != '\0');
    kit_show(alarm_strip, true);
}

/* Per platform hint to make power readable, and the command for it if there is one */
#define RAPL_FOLDER "/sys/class/powercap/intel-rapl"
#define CPUCTL_DEVICE "/dev/cpuctl0"

/* FreeBSD: RAPL on Intel and AMD only */
#if defined(PJ_FREEBSD) && (defined(__x86_64__) || defined(__i386__))
#define CPUCTL_RAPL
#endif

#if defined(PJ_MACOS) || defined(CPUCTL_RAPL)
/* first, then sudo on this program */
static const char *again_as_root(const char *first)
{
    static char command[sizeof alarm_command];
    /* Quoted so a path with spaces or shell characters pastes as one */
    snprintf(command, sizeof command, "%ssudo '%s'", first, program_path);
    return command;
}
#endif

static const char *power_access_hint(void)
{
#ifdef PJ_MACOS
    return "powermetrics only runs as the superuser. Quit and start again with";
#elif defined(PJ_WINDOWS)
    return "Windows' Energy Meter Interface was not found or does not cover the processor."
           " Install the PawnIO driver (https://pawnio.eu) and run " JOULARENERGYMETER_NAME " as Administrator,"
           " or install Hubblo's RAPL driver.";
#elif defined(PJ_FREEBSD)
#ifdef CPUCTL_RAPL
    if (access(CPUCTL_DEVICE, F_OK) != 0)
        return "RAPL is read through the cpuctl driver, which is not loaded (cpuctl_load=\"YES\" in"
               " /boot/loader.conf loads it at boot). Quit, then load it and start again as the superuser:";
    if (geteuid() != 0)
        return "Reading RAPL through cpuctl needs the superuser. Quit and start again with";
#endif
    return "This processor shows no RAPL counters: a virtual machine, or a processor without them.";
#else
    if (access(RAPL_FOLDER, F_OK) != 0)
        return "This machine shows no RAPL counters: a virtual machine, or a processor without them.";
    return "Reading RAPL needs read access to the powercap files. Granting it lets every local"
           " user read the energy counters (CVE-2020-8694) until the next reboot; on a shared"
           " machine, start " JOULARENERGYMETER_NAME " with sudo under X11 instead. Otherwise run after each boot:";
#endif
}

static const char *power_access_command(void)
{
#ifdef PJ_MACOS
    return again_as_root("");
#elif defined(PJ_WINDOWS)
    return "";
#elif defined(PJ_FREEBSD)
#ifdef CPUCTL_RAPL
    if (access(CPUCTL_DEVICE, F_OK) != 0) return again_as_root("sudo kldload cpuctl && ");
    if (geteuid() != 0) return again_as_root("");
#endif
    return "";
#else
    return access(RAPL_FOLDER, F_OK) == 0 ? "sudo chmod -R a+r " RAPL_FOLDER : "";
#endif
}

/* GPU row, split and legend key only when there is a GPU */
static void show_gpu(bool shown)
{
    kit_show(rows[ROW_GPU].box, shown);
    kit_show(split_box, shown);
    kit_show(gpu_key, shown);
    /* Without a GPU the CPU line would just cover the total */
    kit_show(cpu_key, shown);
    /* CPU row's dot hidden too, the row keeps its place */
    lv_obj_set_style_bg_opa(lv_obj_get_child(rows[ROW_CPU].box, 0), shown ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

/* What the hardware offers, known once opened */
static void show_hardware(void)
{
    show_gpu(has_gpu);
    /* No power, no session stats */
    kit_show(session_panel, has_cpu || has_gpu);

    if (!load_reads)
        show_alarm("CPU load cannot be read on this machine.",
                   "It was built for another operating system: rebuild with"
                   " alr build -- -XPJ_OS=linux, windows, macos or freebsd.", "");
#ifdef PJ_MACOS
    /* Already root, so powermetrics was just slow with its first sample */
    else if (!has_cpu && geteuid() == 0)
        show_alarm("powermetrics gave no CPU power reading in time.",
                   "Press Stop, then Start, to try again.", "");
#endif
    else if (!has_cpu && !has_gpu)
        show_alarm("No power source could be read. CPU load is still measured.",
                   power_access_hint(), power_access_command());
    else if (!has_cpu)
        show_alarm("Only the graphics card's power can be read.",
                   power_access_hint(), power_access_command());
    else
        kit_show(alarm_strip, false);
}

/* -------------------------------------------------------------------------------------- */

/* Each part's share of the bar in 255ths (max flex grow), and in per cent */
static void show_split(const cycle *data, bool available)
{
    double total = data->total_power;
    double share[SPLIT_COUNT] = { data->cpu_power, data->gpu_power };
    long cpu = available && total > 0.0 ? lround(100.0 * share[SPLIT_CPU] / total) : 0;
    /* Shares add up to 100; a part drawing anything never shows 0 */
    long percent[SPLIT_COUNT] = { cpu, 100 - cpu };
    char text[16];

    available = available && total > 0.0;
    for (int i = 0; i < SPLIT_COUNT; i++) {
        lv_obj_set_flex_grow(split_part[i], available ? (uint8_t)lround(255.0 * share[i] / total) : 0);
        if (!available)
            show_figure(split_share[i], NULL, "%");
        else if (share[i] > 0.0 && percent[i] == 0)
            show_figure(split_share[i], "<1", "%");
        else if (share[i] < total && percent[i] == 100)
            show_figure(split_share[i], ">99", "%");
        else {
            snprintf(text, sizeof text, "%ld", percent[i]);
            show_figure(split_share[i], text, "%");
        }
    }
}

/* Time of day of a measurement (Unix time in the CSV) */
static char *time_of(char *buf, size_t n, int64_t taken)
{
    time_t t = (time_t)taken;
    const struct tm *local = localtime(&t);

    if (local == NULL || strftime(buf, n, "%H:%M:%S", local) == 0) snprintf(buf, n, "an unknown time");
    return buf;
}

/* Session stats after a measurement of watts at taken. Average is energy over time, as on
 * the register; standard deviation is over the measurements. */
static void show_session(double watts, int64_t taken, const totals *sum)
{
    char number[32], text[64];
    double delta = watts - mean, cycle = sum->elapsed - measured;
    double average = sum->elapsed > 0.0 ? sum->total_energy / sum->elapsed : watts;
    /* Hours in a 365.25 day year, in thousands, for kWh */
    double year = average * 8.766;

    /* Each measurement weighted by the time it covers, so the spread is around the shown
     * average (energy over time). West's weighted Welford. */
    measured = sum->elapsed;
    if (cycle > 0.0) {
        weight += cycle;
        mean += delta * cycle / weight;
        squares += cycle * delta * (watts - mean);
    }
    if (samples == 1 || watts > peak) {
        peak = watts;
        peak_taken = taken;
    }
    if (samples == 1 || watts < low) {
        low = watts;
        low_taken = taken;
    }
    double spread = weight > 0.0 ? sqrt(squares / weight) : 0.0;

    /* Watts to one decimal like the readings, a spread under 1 W to two */
    show_figure(stat_value[STAT_AVERAGE], fmt_number(number, sizeof number, average, 1), "W");
    show_figure(stat_value[STAT_SPREAD],
                samples > 1 ? fmt_number(number, sizeof number, spread, spread < 0.995 ? 2 : 1) : NULL, "W");
    show_watts(stat_value[STAT_HIGHEST], peak, true);
    snprintf(text, sizeof text, "%s, at %s", stat_names[STAT_HIGHEST], time_of(number, sizeof number, peak_taken));
    kit_set_text(stat_name[STAT_HIGHEST], text);
    show_watts(stat_value[STAT_LOWEST], low, true);
    snprintf(text, sizeof text, "%s, at %s", stat_names[STAT_LOWEST], time_of(number, sizeof number, low_taken));
    kit_set_text(stat_name[STAT_LOWEST], text);
    if (year >= 9999.5)
        show_figure(stat_value[STAT_YEAR], fmt_number(number, sizeof number, year / 1000.0, year < 99995.0 ? 2 : 1), "MWh");
    else
        show_figure(stat_value[STAT_YEAR], fmt_number(number, sizeof number, year, year < 9.995 ? 2 : year < 99.95 ? 1 : 0),
                    "kWh");
}

static void show_cycle(const cycle *data, const totals *sum, int64_t taken)
{
    bool has_power = has_cpu || has_gpu;
    char text[32];

    kit_set_text(reading, has_power ? fmt_number(text, sizeof text, data->total_power, 1) : NOTHING);
    kit_set_text(second, has_cpu && data->target_power >= 0.0 ? fmt_number(text, sizeof text, data->target_power, 1)
                                                              : NOTHING);
    show_watts(rows[ROW_CPU].now, data->cpu_power, has_cpu);
    show_watts(rows[ROW_GPU].now, data->gpu_power, has_gpu);
    show_watts(rows[ROW_TARGET].now, data->target_power, has_cpu);
    show_load(rows[ROW_LOAD].now, data->cpu_usage);
    show_load(rows[ROW_SHARE].now, data->target_usage);

    show_joules(rows[ROW_CPU].since, sum->cpu_energy, has_cpu);
    show_joules(rows[ROW_GPU].since, sum->gpu_energy, has_gpu);
    /* Target never read: no energy rather than zero */
    show_joules(rows[ROW_TARGET].since, sum->target_energy, has_cpu && sum->target_read);
    /* Program's share of the machine, power now and energy since start */
    show_figure(rows[ROW_TOTAL_SHARE].now,
                has_cpu && data->target_power >= 0.0 && data->total_power > 0.0
                    ? fmt_number(text, sizeof text, 100.0 * data->target_power / data->total_power, 1) : NULL, "%");
    show_figure(rows[ROW_TOTAL_SHARE].since,
                has_cpu && sum->target_read && sum->total_energy > 0.0
                    ? fmt_number(text, sizeof text, 100.0 * sum->target_energy / sum->total_energy, 1) : NULL, "%");

    kit_set_text(clock_label, fmt_clock(text, sizeof text, sum->elapsed));
    show_split(data, has_power);
    if (has_power) {
        show_session(data->total_power, taken, sum);
        /* Under a 1 s interval the wheels never settle, so jump instead of rolling */
        bool roll = interval_seconds(chosen_interval()) >= 1.0;

        show_register(sum->total_energy, roll);
        turn_disc(sum->total_energy, roll);
    }
}

/* Placeholders shaped like numbers until the first reading */
static void clear_readings(void)
{
    char text[16];

    kit_set_text(reading, "--.-");
    kit_set_text(second, "--.-");
    for (int i = 0; i < ROW_COUNT; i++) {
        kit_set_text(rows[i].now, NOTHING);
        kit_set_text(rows[i].since, "");
    }
    kit_set_text(rows[ROW_CPU].since, NOTHING);
    kit_set_text(rows[ROW_GPU].since, NOTHING);
    kit_set_text(rows[ROW_TARGET].since, NOTHING);
    kit_set_text(rows[ROW_TOTAL_SHARE].since, NOTHING);
    for (int i = 0; i < STAT_COUNT; i++) {
        kit_set_text(stat_name[i], stat_names[i]);
        kit_set_text(stat_value[i], NOTHING);
    }
    mean = squares = weight = measured = 0.0;
    missed = 0;
    show_samples();
    show_split(&(cycle){ 0 }, false);
    show_register(0.0, false);
    kit_set_text(clock_label, fmt_clock(text, sizeof text, 0.0));
    joules_per_turn = turns = counted = 0.0;
    kit_disc_turn(disc, 0.0, false);
}

/* -------------------------------------------------------------------------------------- */
/* The trend                                                                              */
/* -------------------------------------------------------------------------------------- */

static void refresh_chart(void)
{
    /* At most four bands up to the peak, 1 W scale when idle or empty */
    double highest = fmax(1.0, history_peak(SERIES_TOTAL));
    double step = nice_step(highest / 4.0);
    int bands = (int)ceil(highest / step - 1e-9);
    int decimals = decimals_of(step);
    int count = history_count();
    bool cpu = cpu_expected();
    /* Hundredths of a watt, or whole watts above 10 kW, LVGL chart points are 32 bit */
    double per_watt = highest > 10000.0 ? 1.0 : 100.0;

    /* Newest measurement at the right edge */
    for (int s = 0; s < 4; s++) {
        for (int i = 0; i < HISTORY_CAPACITY; i++) points[s][i] = LV_CHART_POINT_NONE;
        for (int i = 1; i <= count; i++) {
            double value = history_value(history_get(i), (series)s);
            if (value >= 0.0)
                points[s][HISTORY_CAPACITY - count + i - 1] = (int32_t)lround(value * per_watt);
        }
    }
    static double shown_step, shown_per_watt;
    static int shown_bands = -1;
    bool empty = count == 0 || !(cpu || has_gpu);

    /* Only reset the scale when it changes, which is rare */
    if (bands != shown_bands || step != shown_step || per_watt != shown_per_watt) {
        shown_bands = bands;
        shown_step = step;
        shown_per_watt = per_watt;
        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, (int32_t)lround(step * bands * per_watt));
        lv_chart_set_div_line_count(chart, (uint32_t)bands + 1, 0);

        /* Unit on the top label only */
        for (int band = 0; band <= bands; band++) {
            char number[16];
            snprintf(band_text[band], sizeof band_text[band], "%s%s",
                     fmt_number(number, sizeof number, step * band, decimals), band == bands ? " W" : "");
            band_labels[band] = band_text[band];
        }
        band_labels[bands + 1] = NULL;
        lv_scale_set_range(scale, 0, bands);
        lv_scale_set_total_tick_count(scale, (uint32_t)bands + 1);
        lv_scale_set_text_src(scale, band_labels);
    }
    /* No numbers before the first reading */
    if (lv_scale_get_label_show(scale) == empty) lv_scale_set_label_show(scale, !empty);

    lv_chart_hide_series(chart, lines[SERIES_TOTAL], !(cpu || has_gpu));
    lv_chart_hide_series(chart, lines[SERIES_CPU], !cpu || !has_gpu);
    lv_chart_hide_series(chart, lines[SERIES_GPU], !has_gpu);
    lv_chart_hide_series(chart, lines[SERIES_TARGET], !(cpu && follows()));

    if (run == RUN_IDLE && count == 0)
        kit_set_text(chart_note, "Press Start to measure.");
    else if (!cpu && !has_gpu)
        kit_set_text(chart_note, "No power reading on this machine.");
    else if (count == 0)
        kit_set_text(chart_note, "Waiting for the first measurement…");
    kit_show(chart_note, empty);

    lv_chart_refresh(chart);
}

/* -------------------------------------------------------------------------------------- */
/* Building it                                                                            */
/* -------------------------------------------------------------------------------------- */

/* One timer, paused when idle; a second click restarts the two seconds of "Copied" */
static lv_timer_t *copy_timer;

static void on_copy_aged(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    kit_button_set(copy_button, LV_SYMBOL_COPY, "Copy command", BUTTON_NORMAL);
}

static void on_export(lv_event_t *e)
{
    LV_UNUSED(e);
    display_export();
}

/* Show "Copied" on the button for a moment */
static void on_copy(lv_event_t *e)
{
    LV_UNUSED(e);
    platform_set_clipboard(alarm_command);
    kit_button_set(copy_button, LV_SYMBOL_OK, "Copied", BUTTON_NORMAL);
    if (copy_timer == NULL) copy_timer = lv_timer_create(on_copy_aged, 2000, NULL);
    lv_timer_reset(copy_timer);
    lv_timer_resume(copy_timer);
    say("Copied to the clipboard: %s", alarm_command);
}

/* Legend key for a series */
static lv_obj_t *new_key(lv_obj_t *strip, series s, const char *name)
{
    lv_obj_t *key = kit_box(strip, false, 6);

    kit_dot(key, pen[s], true);
    kit_label(key, name, TEXT_CAPTION);
    return key;
}

static void build_alarm(lv_obj_t *column)
{
    lv_obj_t *icon, *words;

    alarm_strip = kit_alarm(column);
    lv_obj_set_flex_align(alarm_strip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    icon = lv_label_create(alarm_strip);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_font(icon, ui_font(16), 0);

    words = kit_box(alarm_strip, true, 2);
    kit_let_out(words, 4);
    lv_obj_set_width(words, 0);
    lv_obj_set_flex_grow(words, 1);
    alarm_title = lv_label_create(words);
    lv_obj_set_style_text_font(alarm_title, ui_font(15), 0);
    lv_obj_set_width(alarm_title, LV_PCT(100));
    alarm_text = lv_label_create(words);
    lv_obj_set_style_text_font(alarm_text, ui_font(13), 0);
    lv_obj_set_width(alarm_text, LV_PCT(100));

    /* Below the text, the column is narrow */
    copy_button = kit_button(words, LV_SYMBOL_COPY, "Copy command", BUTTON_NORMAL, on_copy, NULL);
    lv_obj_set_style_margin_top(copy_button, ui_px(6), 0);
    kit_show(alarm_strip, false);
}

/* Meter reading: name above figures and unit */
static lv_obj_t *new_reading(lv_obj_t *row, const char *name, text_role role, lv_obj_t **value, lv_obj_t **label)
{
    lv_obj_t *box = kit_box(row, true, 0), *line, *unit;
    const lv_font_t *font = ui_figures(role == TEXT_READING ? 64 : 40);

    *label = kit_lines(kit_label(box, name, TEXT_TITLE), 1);
    lv_obj_set_style_max_width(*label, ui_px(110), 0);
    line = kit_box(box, false, 6);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    *value = kit_label(line, "--.-", role);
    /* Line height leaves room for accents, figures don't need it */
    lv_obj_set_style_margin_top(*value, -font->line_height / 5, 0);
    unit = kit_label(line, "W", role == TEXT_READING ? TEXT_READING_UNIT : TEXT_WORD);
    on_baseline(unit, font, lv_obj_get_style_text_font(unit, 0));
    return box;
}

/* Register with its unit, elapsed time above it and the energy in Wh below */
static void build_register(lv_obj_t *row)
{
    lv_obj_t *box = kit_box(row, true, 6), *line;

    line = kit_box(box, false, 0);
    lv_obj_set_width(line, LV_PCT(100));
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    kit_label(line, "Energy since start", TEXT_TITLE);
    clock_label = kit_label(line, "", TEXT_FIGURES);

    line = kit_box(box, false, 10);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    wheels = kit_register(line, WHEELS);
    wheels_unit = kit_label(line, "J", TEXT_READING_UNIT);
    lv_obj_set_style_min_width(wheels_unit, ui_px(26), 0);

    watt_hours = kit_label(box, "", TEXT_FIGURES);
    lv_label_set_recolor(watt_hours, true);
}

/* Machine power, followed program's power beside it, register on the right, disc below */
static void build_meter(lv_obj_t *column)
{
    lv_obj_t *panel = kit_panel(column, NULL, 14);
    lv_obj_t *body = kit_panel_body(panel), *line, *spacer, *name;

    lv_obj_set_width(panel, LV_PCT(100));
    /* Titles aligned at the top, both readings' figures on the same baseline */
    line = kit_box(body, false, 24);
    lv_obj_set_width(line, LV_PCT(100));
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    new_reading(line, "Whole machine", TEXT_READING, &reading, &name);
    lv_obj_update_layout(line);
    /* Followed program's power, separated by a rule, in its series colour */
    second_box = new_reading(line, "", TEXT_READING_SECOND, &second, &second_name);
    lv_obj_set_style_pad_left(second_box, ui_px(20), 0);
    lv_obj_set_style_border_side(second_box, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(second_box, ui_px(1), 0);
    lv_obj_set_style_border_color(second_box, theme_color(TOKEN_ENAMEL_DEEP), 0);
    /* Same height as the machine's box, so names align at the top and figures at the bottom */
    lv_obj_set_height(second_box, lv_obj_get_height(lv_obj_get_child(line, 0)));
    lv_obj_set_flex_align(second_box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    on_baseline(lv_obj_get_child(second_box, 1), ui_figures(64), ui_figures(40));
    spacer = kit_box(line, false, 0);
    lv_obj_set_flex_grow(spacer, 1);
    build_register(line);

    disc = kit_disc(body);
    lv_obj_set_width(disc, LV_PCT(100));
}

/* Row: series dot (-1 for none), name, leader, value now, energy since start */
static void new_row(lv_obj_t *body, int which, int series, const char *name)
{
    row *r = &rows[which];
    lv_obj_t *dot;

    r->box = kit_box(body, false, 8);
    lv_obj_set_size(r->box, LV_PCT(100), ui_px(28));
    dot = kit_dot(r->box, series >= 0 ? pen[series] : TOKEN_PLATE, false);
    if (series < 0) lv_obj_set_style_bg_opa(dot, LV_OPA_TRANSP, 0);
    r->name = kit_lines(kit_label(r->box, name, TEXT_TITLE), 1);
    lv_obj_set_style_max_width(r->name, ui_px(110), 0);
    kit_leader(r->box);
    r->now = kit_label(r->box, NOTHING, TEXT_ROW_FIGURE);
    r->since = kit_label(r->box, "", TEXT_ROW_FIGURE);
    lv_obj_set_style_text_color(r->since, theme_color(TOKEN_MUTED), 0);
    for (lv_obj_t *figure = r->now; figure; figure = figure == r->now ? r->since : NULL) {
        lv_label_set_recolor(figure, true);
        lv_obj_set_style_text_align(figure, LV_TEXT_ALIGN_RIGHT, 0);
    }
    lv_obj_set_width(r->now, ui_px(NOW_WIDTH));
    lv_obj_set_width(r->since, ui_px(SINCE_WIDTH));
}

/* Panel of rows, column headings in its title strip */
static lv_obj_t *rows_panel(lv_obj_t *column, const char *title, const char *now, const char *since)
{
    lv_obj_t *panel = kit_panel(column, title, 0), *strip = kit_panel_strip(panel), *head;

    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_style_pad_column(strip, ui_px(8), 0);
    head = kit_label(strip, now, TEXT_CAPTION);
    lv_obj_set_width(head, ui_px(NOW_WIDTH));
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_RIGHT, 0);
    head = kit_label(strip, since, TEXT_CAPTION);
    lv_obj_set_width(head, ui_px(SINCE_WIDTH));
    lv_obj_set_style_text_align(head, LV_TEXT_ALIGN_RIGHT, 0);
    return kit_panel_body(panel);
}

/* Split bar on an enamel groove, and its key */
static void build_split(lv_obj_t *body)
{
    static const series pens[SPLIT_COUNT] = { SERIES_CPU, SERIES_GPU };
    static const char *const names[SPLIT_COUNT] = { "Processor", "Graphics" };
    lv_obj_t *bar, *keys;

    split_box = kit_box(body, true, 8);
    lv_obj_set_width(split_box, LV_PCT(100));
    lv_obj_set_style_pad_top(split_box, ui_px(10), 0);

    bar = kit_box(split_box, false, 0);
    lv_obj_set_size(bar, LV_PCT(100), ui_px(10));
    lv_obj_set_style_bg_color(bar, theme_color(TOKEN_ENAMEL), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, ui_px(2), 0);
    lv_obj_set_style_clip_corner(bar, true, 0);

    keys = kit_box(split_box, false, 18);
    for (int i = 0; i < SPLIT_COUNT; i++) {
        split_part[i] = kit_box(bar, false, 0);
        lv_obj_set_size(split_part[i], 0, LV_PCT(100));
        lv_obj_set_style_bg_color(split_part[i], theme_color(pen[pens[i]]), 0);
        lv_obj_set_style_bg_opa(split_part[i], LV_OPA_COVER, 0);

        split_key[i] = kit_box(keys, false, 6);
        kit_dot(split_key[i], pen[pens[i]], false);
        kit_label(split_key[i], names[i], TEXT_CAPTION);
        split_share[i] = kit_label(split_key[i], NOTHING, TEXT_FIGURES);
        lv_label_set_recolor(split_share[i], true);
    }
}

/* Machine rows with the power split, and a separate panel for the followed program */
static void build_rows(lv_obj_t *column)
{
    lv_obj_t *body = rows_panel(column, "Whole machine", "Now", "Since start");

    new_row(body, ROW_CPU, SERIES_CPU, "Processor");
    new_row(body, ROW_GPU, SERIES_GPU, "Graphics");
    new_row(body, ROW_LOAD, -1, "CPU load");
    build_split(body);

    body = rows_panel(column, "", "Now", "Since start");
    target_panel = lv_obj_get_parent(body);
    new_row(body, ROW_TARGET, SERIES_TARGET, "Power");
    new_row(body, ROW_SHARE, -1, "Share of CPU");
    new_row(body, ROW_TOTAL_SHARE, -1, "Share of total");
}

/* Session stats in two columns, each under its name */
static void build_session(lv_obj_t *column)
{
    lv_obj_t *body;

    /* Title says whose stats these are, it sits under the followed program's panel */
    session_panel = kit_panel(column, "Session, whole machine", 0);
    body = kit_panel_body(session_panel);
    lv_obj_set_width(session_panel, LV_PCT(100));
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(body, 0, 0);
    lv_obj_set_style_pad_row(body, ui_px(10), 0);
    for (int i = 0; i < STAT_COUNT; i++) {
        lv_obj_t *cell = kit_box(body, true, 0);

        lv_obj_set_width(cell, LV_PCT(50));
        stat_name[i] = kit_lines(kit_label(cell, stat_names[i], TEXT_CAPTION), 1);
        lv_obj_set_width(stat_name[i], LV_PCT(100));
        stat_value[i] = kit_label(cell, NOTHING, TEXT_ROW_FIGURE);
        lv_label_set_recolor(stat_value[i], true);
    }
}

/* Three vertical lines at the quarters, inside the outer horizontal lines. LVGL's own would
 * run through the chart padding */
static void draw_time_lines(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_draw_line_dsc_t line;
    lv_area_t area;

    lv_obj_get_content_coords(obj, &area);
    lv_draw_line_dsc_init(&line);
    lv_obj_init_draw_line_dsc(obj, LV_PART_MAIN, &line);
    for (int quarter = 1; quarter < 4; quarter++) {
        line.p1.x = line.p2.x = area.x1 + lv_area_get_width(&area) * quarter / 4;
        line.p1.y = area.y1;
        line.p2.y = area.y2;
        lv_draw_line(lv_event_get_layer(e), &line);
    }
}

static void build_trend(lv_obj_t *column)
{
    /* Drawn in this order, followed program on top */
    static const series order[] = { SERIES_TOTAL, SERIES_GPU, SERIES_CPU, SERIES_TARGET };
    lv_obj_t *panel = kit_panel(column, "", 6);
    lv_obj_t *strip = kit_panel_strip(panel), *body = kit_panel_body(panel);
    lv_obj_t *plot, *times, *spacer;
    const lv_font_t *small = ui_font(12);
    /* Pad half a label above and below, as the top and bottom band labels are centred on the
     * outer grid lines. Same padding on scale and chart */
    int32_t half = (lv_font_get_line_height(small) + 1) / 2;

    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_grow(body, 1);

    /* Title and legend keys, Export at the end */
    span_label = lv_obj_get_child(strip, 0);
    lv_obj_set_flex_grow(span_label, 0);
    lv_obj_set_style_margin_right(span_label, ui_px(8), 0);
    new_key(strip, SERIES_TOTAL, "Total");
    cpu_key = new_key(strip, SERIES_CPU, "Processor");
    gpu_key = new_key(strip, SERIES_GPU, "Graphics");
    target_key = new_key(strip, SERIES_TARGET, "");
    target_key_name = kit_lines(lv_obj_get_child(target_key, 1), 1);
    lv_obj_set_style_max_width(target_key_name, ui_px(120), 0);
    spacer = kit_box(strip, false, 0);
    lv_obj_set_flex_grow(spacer, 1);
    export_key = kit_button(strip, LV_SYMBOL_DOWNLOAD, "Export", BUTTON_NORMAL, on_export, NULL);
    lv_obj_set_style_pad_hor(export_key, ui_px(10), 0);
    lv_obj_set_style_text_font(export_key, ui_font(13), 0);
    kit_let_out(strip, 4);

    plot = kit_box(body, false, 8);
    lv_obj_set_width(plot, LV_PCT(100));
    lv_obj_set_flex_grow(plot, 1);
    lv_obj_set_style_min_height(plot, ui_px(130), 0);

    /* Labels only, no axis line or ticks */
    scale = lv_scale_create(plot);
    lv_scale_set_mode(scale, LV_SCALE_MODE_VERTICAL_LEFT);
    lv_scale_set_label_show(scale, true);
    lv_scale_set_major_tick_every(scale, 1);
    lv_obj_set_size(scale, ui_px(50), LV_PCT(100));
    lv_obj_set_style_pad_all(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(scale, half, LV_PART_MAIN);
    lv_obj_set_style_border_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_line_width(scale, 0, LV_PART_INDICATOR);
    lv_obj_set_style_length(scale, 0, LV_PART_INDICATOR);
    lv_obj_set_style_pad_left(scale, 0, LV_PART_INDICATOR);
    lv_obj_set_style_text_font(scale, small, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(scale, theme_color(TOKEN_MUTED), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 0, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 0, LV_PART_ITEMS);

    /* Thin grid, series lines 3 px wide on Retina */
    chart = lv_chart_create(plot);
    lv_obj_set_height(chart, LV_PCT(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_scrollable(chart, false);
    lv_obj_set_clickable(chart, false);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, HISTORY_CAPACITY);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(chart, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(chart, half, LV_PART_MAIN);
    lv_obj_set_style_line_color(chart, theme_color(TOKEN_GRID), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, ui_px(1), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, (ui_px(3) + 1) / 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_add_event_cb(chart, draw_time_lines, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    for (size_t i = 0; i < 4; i++) {
        series s = order[i];
        lines[s] = lv_chart_add_series(chart, theme_color(pen[s]), LV_CHART_AXIS_PRIMARY_Y);
        lv_chart_set_series_ext_y_array(chart, lines[s], points[s]);
    }
    /* On a plate-coloured patch hiding the grid */
    chart_note = kit_label(chart, "", TEXT_MUTED);
    lv_obj_set_style_bg_color(chart_note, theme_color(TOKEN_PLATE), 0);
    lv_obj_set_style_bg_opa(chart_note, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(chart_note, ui_px(16), 0);
    lv_obj_set_style_pad_ver(chart_note, ui_px(8), 0);
    lv_obj_center(chart_note);

    /* Time labels under the plot */
    times = kit_box(body, false, 0);
    lv_obj_set_width(times, LV_PCT(100));
    lv_obj_set_style_pad_left(times, ui_px(58), 0);
    lv_obj_set_flex_align(times, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ago_far = kit_label(times, "", TEXT_CAPTION);
    ago_mid = kit_label(times, "", TEXT_CAPTION);
    kit_label(times, "now", TEXT_CAPTION);
    /* Centred under the middle line whatever the widths of the side labels */
    lv_obj_set_ignore_layout(ago_mid, true);
    lv_obj_align(ago_mid, LV_ALIGN_CENTER, 0, 0);
}

void display_create(lv_obj_t *left, lv_obj_t *right)
{
    build_meter(left);
    build_trend(left);
    build_alarm(right);
    build_rows(right);
    build_session(right);

    kit_show(target_panel, false);
    kit_show(second_box, false);
    kit_show(target_key, false);
    show_gpu(false);
    clear_readings();
    show_span();
    set_run(RUN_IDLE);
    refresh_chart();
}

/* -------------------------------------------------------------------------------------- */
/* Sessions                                                                               */
/* -------------------------------------------------------------------------------------- */

void display_start(const target *t)
{
    char name[160];

    session_target = *t;
    samples = 0;
    history_clear();
    has_cpu = has_gpu = false;
    load_reads = true;
    target_lost = false;

    target_name(t, name, sizeof name);
    if (t->kind == TARGET_APPLICATION)
        snprintf(heading, sizeof heading, "Application %s", t->app);
    else if (t->kind == TARGET_PROCESS)
        snprintf(heading, sizeof heading, "Process %u", t->pid);
    else
        snprintf(heading, sizeof heading, "Whole machine");

    /* Followed program's panel, reading and legend key get its name */
    lv_label_set_text(lv_obj_get_child(kit_panel_strip(target_panel), 0), name);
    lv_label_set_text(second_name, name);
    lv_label_set_text(target_key_name, name);
    kit_show(target_panel, follows());
    kit_show(session_panel, true);
    kit_show(second_box, follows());
    kit_show(target_key, follows());

    /* Unknown until the hardware is opened */
    show_gpu(false);
    kit_show(alarm_strip, false);
    clear_readings();
    show_span();

    sampler_start(t, interval_seconds(chosen_interval()));
    set_run(RUN_STARTING);
    refresh_chart();
    say("Started: %s, every %s.", heading, interval_label(chosen_interval()));
}

void display_stop(void)
{
    sampler_stop();
    csv_stop_recording();
    set_run(RUN_IDLE);
    refresh_chart();
    say("Stopped after %d %s.", samples, samples == 1 ? "measurement" : "measurements");
}

/* Gap in the trend so the line isn't drawn across unmeasured time */
static void break_trend(void)
{
    static const history_point gap = {
        .taken = 0,
        .data = { UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE },
    };

    if (history_count() > 0 && history_get(history_count())->taken != 0)
        history_append(&gap);
}

void display_pause(bool paused)
{
    if (run != RUN_LIVE && run != RUN_PAUSED) return;
    sampler_pause(paused);
    if (!paused) break_trend();
    set_run(paused ? RUN_PAUSED : RUN_LIVE);
    ui_message(paused ? "Paused: the time and the energy stop counting." : "Resumed.");
}

/* No file dialog. Export goes to its own file in the current folder, never the default
 * recording file */
void display_export(void)
{
    char second[4200], folder[4096] = "", in[4200] = "";
    int saved = csv_save_history(CSV_EXPORT_FILE, &session_target);
    const char *word = saved == 1 ? "measurement" : "measurements";

    /* Say the folder, the start folder isn't always obvious */
    if (saved >= 0 && csv_folder_of(CSV_EXPORT_FILE, folder, sizeof folder))
        snprintf(in, sizeof in, " in %s", folder);
    if (saved < 0)
        warn("%s.", csv_trouble());
    else if (follows())
        say("Saved %d %s to %s and %s%s.", saved, word, CSV_EXPORT_FILE,
            csv_target_file(CSV_EXPORT_FILE, &session_target, second, sizeof second), in);
    else
        say("Saved %d %s to %s%s.", saved, word, CSV_EXPORT_FILE, in);
}

/* Trend points are one interval apart, as the time labels assume. A running session's trend
 * restarts at the new interval, a stopped one keeps its labels until the next Start */
void display_interval_changed(void)
{
    if (run == RUN_IDLE) {
        if (history_count() == 0) show_span();
        return;
    }

    sampler_set_interval(interval_seconds(chosen_interval()));
    show_span();
    if (history_count() > 0) {
        history_clear();
        set_run(run);
        say("Every %s from now: the trend starts again.", interval_label(chosen_interval()));
    }
    refresh_chart();
}

run_state display_run(void)
{
    return run;
}

void display_report(const report *r)
{
    switch (r->kind) {
    case REPORT_STARTED:
        has_cpu = r->has_cpu;
        has_gpu = r->has_gpu;
        load_reads = r->load_readable;
        show_hardware();
        set_run(RUN_LIVE);
        refresh_chart();
        break;
    case REPORT_MEASURED: {
        history_point point = { .taken = r->taken, .data = r->data };
        bool written;

        samples++;
        history_append(&point);
        written = csv_record_point(&point);
        show_cycle(&r->data, &r->sum, r->taken);
        refresh_chart();
        show_samples();
        /* Export enabled from the first point */
        kit_enable(export_key, true);

        /* Warn once when the followed program becomes unreadable */
        if (follows() && (r->data.target_usage < 0.0) != target_lost) {
            char name[160];

            target_lost = !target_lost;
            if (target_lost)
                warn("%s could not be read: %s.", target_name(&session_target, name, sizeof name),
                    session_target.kind == TARGET_PROCESS ? "it is not running, or access is denied"
                                                          : "access is denied");
        }
        if (!written) warn("%s. Measuring goes on.", csv_trouble());
        break;
    }
    case REPORT_SKIPPED:
        missed++;
        show_samples();
        break_trend();
        ui_message("The machine was asleep: that measurement was dropped.");
        break;
    case REPORT_FAILED:
        missed++;
        show_samples();
        warn("A measurement failed (%s). Measuring goes on.", r->trouble);
        break;
    }
}
