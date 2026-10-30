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

/* Display: readings, breakdown, carbon intensity and the chart. Reports from the measuring
 * thread arrive here on the main thread. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef PJ_WINDOWS
#include <unistd.h>
#endif

#include "app.h"

/* Series colours, in series enum order */
static const theme_token pen[] = { TOKEN_TOTAL, TOKEN_CPU, TOKEN_GPU, TOKEN_TARGET };

/* Least plot height, logical px: below it the column scrolls */
#define PLOT_MIN 96

/* Breakdown columns, logical px */
#define NOW_WIDTH 90
#define ENERGY_WIDTH 120
#define SHARE_WIDTH 110

/* A reading: name and note over its figures, each a caption over a number and its unit */
enum { FIG_POWER, FIG_ENERGY, FIG_CARBON, FIG_COUNT };

typedef struct reading {
    lv_obj_t *box, *dot, *name, *note;
    lv_obj_t *caption[FIG_COUNT], *number[FIG_COUNT], *unit[FIG_COUNT];
} reading;

/* The followed program, or the machine when none is; then the machine beside it */
static reading first, second;

/* Processor and graphics card */
enum { PART_CPU, PART_GPU, PART_COUNT };
static const series part_series[PART_COUNT] = { SERIES_CPU, SERIES_GPU };
static lv_obj_t *parts_row, *breakdown, *head_now, *head_energy;
static lv_obj_t *part_now[PART_COUNT], *part_energy[PART_COUNT], *part_share[PART_COUNT];

/* Electricity the emissions are counted for: a country's average, one energy source, or a
 * typed value. Emissions are worked out again from the last measurement when it changes. */
enum { MIX_COUNTRY, MIX_SOURCE, MIX_OWN };
static lv_obj_t *carbon_panel, *mix_kind, *mix_country, *mix_source, *mix_own, *mix_own_unit, *mix_note;
static double grams_per_kwh;   /* negative when no value is typed */
static cycle last_cycle;
static totals last_sum;

/* Power of each series over the session: mean and Welford's running sum of squares */
static struct { double mean, squares, weight; } spread[4];
static int missed;

/* Chart of one series, chosen above it, and the caption under it */
static lv_obj_t *choices[4], *range_label, *scale, *chart, *chart_note, *ago_far, *ago_mid, *ago_end;
static lv_obj_t *items[3];
static lv_chart_series_t *lines[4];
static series shown = SERIES_TOTAL;

/* Band labels, static as lv_scale keeps the pointers */
static char band_text[5][16];
static const char *band_labels[6];

static target session_target;
static char heading[160];      /* "Whole machine", "Process 2814", "Application firefox" */
static char target_trouble[128];   /* why the followed program wasn't read at the last cycle */
static int samples;
static run_state run = RUN_IDLE;
static bool has_cpu, has_gpu, load_reads;
static bool target_lost;   /* followed program unreadable at the last cycle */
static bool csv_failed;    /* the last CSV row couldn't be written, said once */

static void refresh_chart(void);

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

/* The last session's values are still shown */
static bool stopped(void)
{
    return run == RUN_IDLE && samples > 0;
}

/* "90 s ago", "2 min before" or "now", for back seconds */
static char *ago(char *buf, size_t n, double back, const char *word)
{
    int seconds = (int)back;

    if (seconds == 0)
        snprintf(buf, n, "now");
    else if (seconds >= 60 && seconds % 60 == 0)
        snprintf(buf, n, "%d min %s", seconds / 60, word);
    else
        snprintf(buf, n, "%d s %s", seconds, word);
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

/* Watts as a number and its unit: mW under 1 W, so a small program never reads 0.0 */
static char *power_text(char *buf, size_t n, double watts, const char **unit)
{
    double milli = watts * 1000.0;

    *unit = watts > 0.0 && watts < 0.9995 ? "mW" : "W";
    if (watts <= 0.0)
        snprintf(buf, n, "0");
    else if (milli < 0.05)
        snprintf(buf, n, "<0.1");
    else if (milli < 9.95)
        fmt_number(buf, n, milli, 1);
    else if (watts < 0.9995)
        fmt_number(buf, n, milli, 0);
    else
        fmt_number(buf, n, watts, 1);
    return buf;
}

static const char *const joule_units[] = { "J", "kJ", "MJ", "GJ" };
static const char *const gram_units[] = { "mg", "g", "kg", "t" };

/* At most three digits before the point and four in all, so a long session's total keeps the
 * same precision. Each unit is 1000 of the one before. */
static char *scaled_text(char *buf, size_t n, double value, const char *const units[4], const char **unit)
{
    int u = 0;

    /* 999.95 and up would round to 1000.0 */
    while (value >= 999.95 && u < 3) {
        value /= 1000.0;
        u++;
    }
    *unit = units[u];
    return fmt_number(buf, n, value, value < 9.9995 ? 3 : value < 99.995 ? 2 : 1);
}

/* A year at watts, in kWh, then MWh */
static char *year_text(char *buf, size_t n, double watts, const char **unit)
{
    /* Hours in a 365.25 day year, in thousands, for kWh */
    double year = watts * 8.766;

    if (year >= 9999.5) {
        *unit = "MWh";
        return fmt_number(buf, n, year / 1000.0, year < 99995.0 ? 2 : 1);
    }
    *unit = "kWh";
    return fmt_number(buf, n, year, year < 9.995 ? 2 : year < 99.95 ? 1 : 0);
}

/* Part of whole in per cent, one decimal, never 0 or 100 when it isn't */
static char *share_text(char *buf, size_t n, double part, double whole)
{
    double percent = 100.0 * part / whole;

    if (part > 0.0 && percent < 0.05)
        snprintf(buf, n, "<0.1");
    else if (part < whole && percent >= 99.95)
        snprintf(buf, n, ">99.9");
    else
        fmt_number(buf, n, percent, 1);
    return buf;
}

/* Time of day of a measurement (Unix time in the CSV) */
static char *time_of(char *buf, size_t n, int64_t taken)
{
    time_t t = (time_t)taken;
    const struct tm *local = localtime(&t);

    if (local == NULL || strftime(buf, n, "%H:%M:%S", local) == 0) snprintf(buf, n, "an unknown time");
    return buf;
}

/* -------------------------------------------------------------------------------------- */
/* Values                                                                                 */
/* -------------------------------------------------------------------------------------- */

/* Whether the hardware gives the series */
static bool readable(series s)
{
    switch (s) {
    case SERIES_CPU:
        return has_cpu;
    case SERIES_GPU:
        return has_gpu;
    case SERIES_TARGET:
        return has_cpu && follows();
    default:
        return has_cpu || has_gpu;
    }
}

static double power_of(const cycle *data, series s)
{
    history_point p = { .data = *data };

    return history_value(&p, s);
}

static double energy_of(const totals *sum, series s)
{
    switch (s) {
    case SERIES_CPU:
        return sum->cpu_energy;
    case SERIES_GPU:
        return sum->gpu_energy;
    case SERIES_TARGET:
        return sum->target_energy;
    default:
        return sum->total_energy;
    }
}

/* Measured, and for the followed program read at least once */
static bool counted(series s)
{
    return samples > 0 && readable(s) && (s != SERIES_TARGET || last_sum.target_read);
}

/* Energy over the measured time, as the readings show after Stop. Negative when there is none
 * yet. The followed program's counts the time it wasn't running, so energy = average x time. */
static double average_of(series s)
{
    return counted(s) && last_sum.elapsed > 0.0 ? energy_of(&last_sum, s) / last_sum.elapsed : -1.0;
}

/* Power now, or the average after Stop */
static double reading_of(series s)
{
    return stopped() ? average_of(s) : power_of(&last_cycle, s);
}

/* Each measurement weighted by the time it covers, so the spread is around the average
 * (energy over time). West's weighted Welford. Before last_sum takes sum. */
static void add_to_spread(const cycle *data, const totals *sum)
{
    double span = sum->elapsed - last_sum.elapsed;

    if (span <= 0.0) return;
    for (int s = 0; s < 4; s++) {
        double watts = power_of(data, (series)s), delta;

        if (watts < 0.0 || !readable((series)s)) continue;
        delta = watts - spread[s].mean;
        spread[s].weight += span;
        spread[s].mean += delta * span / spread[s].weight;
        spread[s].squares += span * delta * (watts - spread[s].mean);
    }
}

/* -------------------------------------------------------------------------------------- */
/* Showing things                                                                         */
/* -------------------------------------------------------------------------------------- */

/* A row of two fonts aligns bottoms; this lifts the smaller onto the larger's baseline */
static void on_baseline(lv_obj_t *label, const lv_font_t *large, const lv_font_t *small)
{
    lv_obj_set_style_pad_bottom(label, large->base_line - small->base_line, 0);
}

/* Colour for a label's recolour marks, "#rrggbb text#" */
static char *hex_of(char *buf, size_t n, theme_token t)
{
    lv_color_t c = theme_color(t);

    snprintf(buf, n, "%02x%02x%02x", c.red, c.green, c.blue);
    return buf;
}

/* Number with its unit in the muted colour; NULL number shows a dash */
static void show_figure(lv_obj_t *label, const char *number, const char *unit)
{
    char text[64], muted[8];

    if (number == NULL) {
        kit_set_text(label, NOTHING);
        return;
    }
    snprintf(text, sizeof text, "%s #%s %s#", number, hex_of(muted, sizeof muted, TOKEN_MUTED), unit);
    kit_set_text(label, text);
}

/* A reading's number and unit, in their own labels; NULL number shows a dash */
static void show_pair(reading *r, int which, const char *number, const char *unit)
{
    kit_set_text(r->number[which], number ? number : NOTHING);
    kit_set_text(r->unit[which], number ? unit : "");
}

/* Power now, or the average after Stop, then energy and emissions since start. Grams per kWh
 * over 3600 are milligrams per joule. */
static void show_reading(reading *r, series s)
{
    char text[32];
    const char *unit = "";
    double watts = reading_of(s), joules = energy_of(&last_sum, s);
    bool has = counted(s);

    show_pair(r, FIG_POWER, has && watts >= 0.0 ? power_text(text, sizeof text, watts, &unit) : NULL, unit);
    show_pair(r, FIG_ENERGY, has ? scaled_text(text, sizeof text, joules, joule_units, &unit) : NULL, unit);
    show_pair(r, FIG_CARBON, has && grams_per_kwh >= 0.0
                                 ? scaled_text(text, sizeof text, joules * grams_per_kwh / 3600.0, gram_units, &unit)
                                 : NULL, unit);
}

/* The followed program's share of the machine, or why it wasn't read */
static void show_note(void)
{
    char share[16], text[200] = "";

    if (!follows() || samples == 0 || !has_cpu)
        ;
    else if (stopped()) {
        if (last_sum.target_read && last_sum.total_energy > 0.0)
            snprintf(text, sizeof text, "%s %% of the machine's energy",
                     share_text(share, sizeof share, last_sum.target_energy, last_sum.total_energy));
    } else if (last_cycle.target_power < 0.0) {
        if (target_trouble[0])
            snprintf(text, sizeof text, "Not read: %s", target_trouble);
        else
            snprintf(text, sizeof text, "Not running");
    } else if (last_cycle.total_power > 0.0) {
        snprintf(text, sizeof text, "%s %% of the machine's power",
                 share_text(share, sizeof share, last_cycle.target_power, last_cycle.total_power));
    }
    kit_set_text(first.note, text);
}

/* Each part's share of the machine in whole per cent, adding up to 100; a part drawing
 * anything never shows 0 */
static void show_shares(double cpu, double gpu, double total)
{
    double part[PART_COUNT] = { cpu, gpu };
    long cpu_percent = total > 0.0 ? lround(100.0 * cpu / total) : 0;
    long percent[PART_COUNT] = { cpu_percent, 100 - cpu_percent };
    char text[16];

    for (int i = 0; i < PART_COUNT; i++) {
        if (total <= 0.0 || !readable(part_series[i]))
            show_figure(part_share[i], NULL, "%");
        else if (part[i] > 0.0 && percent[i] == 0)
            show_figure(part_share[i], "<1", "%");
        else if (part[i] < total && percent[i] == 100)
            show_figure(part_share[i], ">99", "%");
        else {
            snprintf(text, sizeof text, "%ld", percent[i]);
            show_figure(part_share[i], text, "%");
        }
    }
}

/* Processor and graphics card: power now (average after Stop), energy, share of the
 * machine's power (of its energy after Stop) */
static void show_breakdown(void)
{
    char text[32];
    const char *unit = "";

    for (int i = 0; i < PART_COUNT; i++) {
        series s = part_series[i];
        bool has = counted(s);
        double watts = reading_of(s);

        show_figure(part_now[i], has && watts >= 0.0 ? power_text(text, sizeof text, watts, &unit) : NULL, unit);
        show_figure(part_energy[i], has ? scaled_text(text, sizeof text, energy_of(&last_sum, s), joule_units, &unit)
                                        : NULL, unit);
    }
    if (stopped())
        show_shares(last_sum.cpu_energy, last_sum.gpu_energy, last_sum.total_energy);
    else
        show_shares(last_cycle.cpu_power, last_cycle.gpu_power, last_cycle.total_power);
}

/* Caption item: name in the muted colour, value in ink; NULL name hides it */
static void show_item(int i, const char *name, const char *value)
{
    char text[128], ink[8];

    if (name == NULL) {
        kit_show(items[i], false);
        return;
    }
    snprintf(text, sizeof text, "%s  #%s %s#", name, hex_of(ink, sizeof ink, TOKEN_TEXT), value);
    kit_set_text(items[i], text);
    kit_show(items[i], true);
}

/* Under the chart, for its series: CPU load (the program's share of CPU), spread and a year
 * at the average */
static void show_items(void)
{
    char number[32], value[48];
    const char *unit = "";
    bool held = run == RUN_PAUSED || stopped();
    double load = shown == SERIES_TARGET ? last_cycle.target_usage : last_cycle.cpu_usage;
    double average = average_of(shown);
    int n = 0;

    if (samples > 0 && load_reads && shown != SERIES_GPU && load >= 0.0) {
        snprintf(value, sizeof value, "%s %%", fmt_number(number, sizeof number, 100.0 * load, 1));
        if (shown == SERIES_TARGET)
            show_item(n++, held ? "Share of CPU, last reading" : "Share of CPU now", value);
        else
            show_item(n++, held ? "CPU load, last reading" : "CPU load now", value);
    }
    if (samples > 1 && counted(shown) && spread[shown].weight > 0.0) {
        power_text(number, sizeof number, sqrt(spread[shown].squares / spread[shown].weight), &unit);
        snprintf(value, sizeof value, "%s %s", number, unit);
        show_item(n++, "Spread since start", value);
    }
    if (average >= 0.0) {
        year_text(number, sizeof number, average, &unit);
        snprintf(value, sizeof value, "%s %s", number, unit);
        show_item(n++, "A year at this average", value);
    }
    while (n < 3) show_item(n++, NULL, NULL);
}

static void show_readings(void)
{
    show_reading(&first, follows() ? SERIES_TARGET : SERIES_TOTAL);
    if (follows()) show_reading(&second, SERIES_TOTAL);
    show_note();
    show_breakdown();
    show_items();
}

/* Captions follow the run: now, the last reading while paused, the average after Stop */
static void show_captions(void)
{
    const char *power = stopped() ? "Average" : run == RUN_PAUSED ? "Last reading" : "Now";
    const char *energy = stopped() ? "Energy" : "Energy since start";
    const char *carbon = stopped() ? "Carbon" : "Carbon since start";
    reading *both[] = { &first, &second };

    for (int i = 0; i < 2; i++) {
        kit_set_text(both[i]->caption[FIG_POWER], power);
        kit_set_text(both[i]->caption[FIG_ENERGY], energy);
        kit_set_text(both[i]->caption[FIG_CARBON], carbon);
    }
    kit_set_text(head_now, power);
    kit_set_text(head_energy, energy);
}

/* Held values in the muted colour while paused */
static void show_fresh(void)
{
    lv_color_t ink = theme_color(run == RUN_PAUSED ? TOKEN_MUTED : TOKEN_TEXT);

    lv_obj_set_style_text_color(first.number[FIG_POWER], ink, 0);
    lv_obj_set_style_text_color(second.number[FIG_POWER], ink, 0);
    for (int i = 0; i < PART_COUNT; i++) lv_obj_set_style_text_color(part_now[i], ink, 0);
}

/* Chart span over the choices, time labels under it */
static void show_span(void)
{
    char text[32], words[64];
    double span = history_span();
    const char *word = run == RUN_PAUSED || stopped() ? "before" : "ago";

    fmt_span(text, sizeof text, span);
    if (run == RUN_PAUSED)
        snprintf(words, sizeof words, "The %s before the pause", text);
    else if (stopped())
        snprintf(words, sizeof words, "The %s before Stop", text);
    else
        snprintf(words, sizeof words, "Last %s", text);
    kit_set_text(range_label, words);
    kit_set_text(ago_far, ago(text, sizeof text, span, word));
    kit_set_text(ago_mid, ago(text, sizeof text, span / 2.0, word));
    kit_set_text(ago_end, run == RUN_PAUSED ? "Pause" : stopped() ? "Stop" : "now");
}

/* Lamp, buttons, readings and chart follow the run state */
static void set_run(run_state now)
{
    run = now;
    /* Keep the machine from sleeping during a run */
    platform_keep_awake(run == RUN_STARTING || run == RUN_LIVE);
    controls_show_run(run, stopped());
    show_fresh();
    show_captions();
    show_span();
    show_readings();
    refresh_chart();
}

/* -------------------------------------------------------------------------------------- */

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
    static char command[1024];
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

/* What the hardware offers, known once opened */
static void show_hardware(void)
{
    bool has_power = has_cpu || has_gpu;

    /* Without a GPU the processor is the whole machine */
    kit_show(breakdown, has_gpu);
    kit_show(choices[SERIES_CPU], has_gpu);
    kit_show(choices[SERIES_GPU], has_gpu);
    /* No power, no emissions */
    kit_show(carbon_panel, has_power);
    kit_show(parts_row, has_power);

    if (!load_reads)
        ui_alarm("CPU load cannot be read on this machine.",
                 "It was built for another operating system: rebuild with"
                 " alr build -- -XPJ_OS=linux, windows, macos or freebsd.", "");
#ifdef PJ_MACOS
    /* Already root, so powermetrics was just slow with its first sample */
    else if (!has_cpu && geteuid() == 0)
        ui_alarm("powermetrics gave no CPU power reading in time.", "Press Stop, then Start, to try again.", "");
#endif
    else if (!has_power)
        ui_alarm("No power source could be read. CPU load is still measured.",
                 power_access_hint(), power_access_command());
    else if (!has_cpu)
        ui_alarm("Only the graphics card's power can be read.", power_access_hint(), power_access_command());
    else
        ui_alarm_clear();
}

/* -------------------------------------------------------------------------------------- */
/* The chart                                                                              */
/* -------------------------------------------------------------------------------------- */

/* Chart unit: 10 µW, or whole watts above 10 kW, LVGL chart points are 32 bit */
static double per_watt = 100000.0;
static int32_t axis_top;   /* top of the scale, chart units */

/* Appends a measurement, or a gap, to each series. The chart shifts its points along and
 * redraws itself, nothing else is rewritten. */
static void plot(const history_point *p)
{
    for (int s = 0; s < 4; s++) {
        double value = history_value(p, (series)s);

        lv_chart_set_next_value(chart, lines[s], value >= 0.0 ? (int32_t)lround(value * per_watt) : LV_CHART_POINT_NONE);
    }
}

/* Every point again, newest at the right edge: after the history was cleared, or in a new unit */
static void replot(void)
{
    for (int s = 0; s < 4; s++) lv_chart_set_all_values(chart, lines[s], LV_CHART_POINT_NONE);
    for (int i = 1; i <= history_count(); i++) plot(history_get(i));
}

/* The chosen series has points to draw */
static bool plotted(void)
{
    return history_count() > 0 && counted(shown);
}

/* History indexes of the highest, lowest and last point of the chosen series, 0 for none */
static int mark_high, mark_low, mark_last;

/* Gaps and unread points left out. Returns the highest value, 0 for none. */
static double find_marks(void)
{
    double high = 0.0, low = 0.0;

    mark_high = mark_low = mark_last = 0;
    for (int i = 1; i <= history_count(); i++) {
        const history_point *p = history_get(i);
        double v = history_value(p, shown);

        if (p->taken == 0 || v < 0.0) continue;
        if (mark_last == 0 || v > high) {
            mark_high = i;
            high = v;
        }
        if (mark_last == 0 || v < low) {
            mark_low = i;
            low = v;
        }
        mark_last = i;
    }
    return high;
}

static void refresh_chart(void)
{
    /* At most four bands up to the highest point or the average, 1 mW at least */
    double peak = find_marks();
    double highest = fmax(0.001, fmax(peak, average_of(shown)));
    double step = nice_step(highest / 4.0);
    int bands = (int)ceil(highest / step - 1e-9);
    /* Labels in mW under 1 W */
    double factor = step * bands < 1.0 ? 1000.0 : 1.0;
    int decimals = decimals_of(step * factor);
    double unit = history_peak(SERIES_TOTAL) > 10000.0 ? 1.0 : 100000.0;
    static double shown_step, shown_factor;
    static int shown_bands = -1;
    int count = history_count();
    bool drawn = plotted();
    char note[200];

    if (unit != per_watt) {
        per_watt = unit;
        replot();
        shown_bands = -1;   /* the range is in the new unit */
    }
    /* Only reset the scale when it changes, which is rare */
    if (bands != shown_bands || step != shown_step || factor != shown_factor) {
        shown_bands = bands;
        shown_step = step;
        shown_factor = factor;
        axis_top = (int32_t)lround(step * bands * per_watt);
        lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, axis_top);
        lv_chart_set_div_line_count(chart, (uint32_t)bands + 1, 0);

        /* Unit on the top label only */
        for (int band = 0; band <= bands; band++) {
            char number[16];
            snprintf(band_text[band], sizeof band_text[band], "%s%s",
                     fmt_number(number, sizeof number, step * band * factor, decimals),
                     band < bands ? "" : factor > 1.0 ? " mW" : " W");
            band_labels[band] = band_text[band];
        }
        band_labels[bands + 1] = NULL;
        lv_scale_set_range(scale, 0, bands);
        lv_scale_set_total_tick_count(scale, (uint32_t)bands + 1);
        lv_scale_set_text_src(scale, band_labels);
    }
    /* No numbers without a line */
    if (lv_scale_get_label_show(scale) != drawn) lv_scale_set_label_show(scale, drawn);

    for (int s = 0; s < 4; s++) lv_chart_hide_series(chart, lines[s], s != (int)shown || !drawn);

    if (run == RUN_IDLE && count == 0)
        snprintf(note, sizeof note, "Press Start to measure.");
    else if (!cpu_expected() && !has_gpu)
        snprintf(note, sizeof note, "No power reading on this machine.");
    else if (count == 0)
        snprintf(note, sizeof note, "Waiting for the first measurement…");
    else if (shown == SERIES_TARGET && has_cpu)
        snprintf(note, sizeof note, "%s hasn't been read yet.", heading);
    else
        snprintf(note, sizeof note, "No %s power reading on this machine.", shown == SERIES_GPU ? "graphics" : "processor");
    kit_set_text(chart_note, note);
    kit_show(chart_note, !drawn);
}

/* The chart and its caption follow the chosen series */
static void choose(series s)
{
    shown = s;
    for (int i = 0; i < 4; i++) lv_obj_set_state(choices[i], LV_STATE_CHECKED, i == (int)s);
    refresh_chart();
    show_items();
}

static void on_choice(lv_event_t *e)
{
    choose((series)(intptr_t)lv_event_get_user_data(e));
}

/* Where LVGL draws history point i (1 is the oldest) of value v, in chart units: the newest
 * is at the right edge */
static lv_point_t point_at(const lv_area_t *area, int i, int32_t v)
{
    int32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
    int32_t position = HISTORY_CAPACITY - history_count() + i - 1;

    return (lv_point_t){ area->x1 + w * position / (HISTORY_CAPACITY - 1),
                         lv_map(v, 0, axis_top, area->y1 + h, area->y1) };
}

/* Text on a plate-coloured patch so the line doesn't run through it; before puts it left of x */
static void draw_tag(lv_layer_t *layer, const char *text, int32_t x, int32_t y, bool before, bool strong)
{
    const lv_font_t *font = ui_font(12);
    lv_draw_rect_dsc_t patch;
    lv_draw_label_dsc_t words;
    lv_point_t size;
    lv_area_t a, back;

    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    a.x1 = before ? x - size.x - ui_px(2) : x;
    a.x2 = a.x1 + size.x + ui_px(2);
    a.y1 = y - size.y / 2;
    a.y2 = a.y1 + size.y;
    back = a;
    lv_area_increase(&back, ui_px(3), 0);
    lv_draw_rect_dsc_init(&patch);
    patch.bg_color = theme_color(TOKEN_PLATE);
    patch.radius = ui_px(2);
    lv_draw_rect(layer, &patch, &back);

    lv_draw_label_dsc_init(&words);
    words.font = font;
    words.color = theme_color(strong ? TOKEN_TEXT : TOKEN_MUTED);
    words.text = text;
    words.text_local = 1;
    lv_draw_label(layer, &words, &a);
}

static void draw_dot(lv_layer_t *layer, lv_point_t at, int32_t radius)
{
    lv_draw_rect_dsc_t d;
    lv_area_t a = { at.x - radius, at.y - radius, at.x + radius, at.y + radius };

    lv_draw_rect_dsc_init(&d);
    d.radius = LV_RADIUS_CIRCLE;
    d.bg_color = theme_color(pen[shown]);
    d.border_color = theme_color(TOKEN_PLATE);
    d.border_width = ui_px(2);
    lv_draw_rect(layer, &d, &a);
}

/* A mark's dot and "highest 1.3 W at 18:18:16"; in the last quarter the text goes left */
static void draw_mark(lv_layer_t *layer, const lv_area_t *area, int i, const char *word, bool below)
{
    const history_point *p = history_get(i);
    double watts = history_value(p, shown);
    lv_point_t at = point_at(area, i, (int32_t)lround(watts * per_watt));
    bool before = at.x > area->x1 + lv_area_get_width(area) * 3 / 4;
    int32_t y = at.y, step = ui_px(14);
    char number[32], clock[32], text[96];
    const char *unit = "";

    if (below) y = at.y + step < area->y2 ? at.y + step : at.y - step;
    else y = LV_MAX(y, area->y1 + step / 2);
    snprintf(text, sizeof text, "%s %s %s at %s", word, power_text(number, sizeof number, watts, &unit), unit,
             time_of(clock, sizeof clock, p->taken));
    draw_dot(layer, at, ui_px(4));
    draw_tag(layer, text, before ? at.x - ui_px(10) : at.x + ui_px(10), y, before, true);
}

/* Average line with its label, highest and lowest of the points shown, and the last point */
static void draw_marks(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_draw_line_dsc_t line;
    lv_area_t area;
    double average = average_of(shown);
    char number[32], text[64];
    const char *unit = "";

    if (!plotted() || mark_last == 0) return;
    lv_obj_get_content_coords(chart, &area);

    if (average >= 0.0) {
        int32_t y = point_at(&area, 1, (int32_t)lround(average * per_watt)).y;

        lv_draw_line_dsc_init(&line);
        line.color = theme_color(TOKEN_FAINT);
        line.width = ui_px(1);
        line.dash_width = ui_px(5);
        line.dash_gap = ui_px(4);
        line.p1.x = area.x1;
        line.p2.x = area.x2;
        line.p1.y = line.p2.y = y;
        lv_draw_line(layer, &line);
        snprintf(text, sizeof text, "average since start %s %s", power_text(number, sizeof number, average, &unit), unit);
        draw_tag(layer, text, area.x1 + ui_px(6), y - ui_px(10) > area.y1 ? y - ui_px(10) : y + ui_px(10), false, false);
    }
    /* Different points only when the values differ */
    if (mark_high != mark_low) {
        draw_mark(layer, &area, mark_high, "highest", false);
        draw_mark(layer, &area, mark_low, "lowest", true);
    }
    draw_dot(layer, point_at(&area, mark_last, (int32_t)lround(history_value(history_get(mark_last), shown) * per_watt)),
             ui_px(4));
}

/* Gap in the trend so the line isn't drawn across unmeasured time */
static void break_trend(void)
{
    static const history_point gap = {
        .taken = 0,
        .data = { UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE },
    };

    if (history_count() > 0 && history_get(history_count())->taken != 0) {
        history_append(&gap);
        plot(&gap);
    }
}

/* -------------------------------------------------------------------------------------- */
/* Building it                                                                            */
/* -------------------------------------------------------------------------------------- */

/* Caption over a number and its unit, the unit on the number's baseline */
static void new_figure(lv_obj_t *row, reading *r, int which, text_role role, text_role unit_role, int32_t width)
{
    lv_obj_t *box = kit_box(row, true, 0), *line;
    const lv_font_t *font;

    lv_obj_set_style_min_width(box, ui_px(width), 0);
    r->caption[which] = kit_label(box, "", TEXT_MUTED);
    line = kit_box(box, false, 5);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    r->number[which] = kit_label(line, NOTHING, role);
    r->unit[which] = kit_label(line, "", unit_role);
    font = lv_obj_get_style_text_font(r->number[which], 0);
    /* Line height leaves room for accents, figures don't need it */
    lv_obj_set_style_margin_top(r->number[which], -font->line_height / 5, 0);
    on_baseline(r->unit[which], font, lv_obj_get_style_text_font(r->unit[which], 0));
}

/* Name and note over power, energy and carbon */
static void new_reading(lv_obj_t *row, reading *r, bool large)
{
    lv_obj_t *head, *figures;

    r->box = kit_box(row, true, 8);
    head = kit_box(r->box, false, 8);
    r->dot = kit_dot(head, TOKEN_TOTAL);
    r->name = kit_lines(kit_label(head, "Whole machine", TEXT_TITLE), 1);
    lv_obj_set_style_max_width(r->name, ui_px(220), 0);
    r->note = kit_lines(kit_label(head, "", TEXT_MUTED), 1);
    lv_obj_set_style_max_width(r->note, ui_px(260), 0);
    figures = kit_box(r->box, false, 28);
    lv_obj_set_flex_align(figures, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    new_figure(figures, r, FIG_POWER, large ? TEXT_READING : TEXT_READING_SECOND,
               large ? TEXT_READING_UNIT : TEXT_SECOND_UNIT, large ? 150 : 110);
    new_figure(figures, r, FIG_ENERGY, TEXT_GROUP_FIGURE, TEXT_GROUP_UNIT, 110);
    new_figure(figures, r, FIG_CARBON, TEXT_GROUP_FIGURE, TEXT_GROUP_UNIT, 120);
}

static void build_readings(lv_obj_t *column)
{
    lv_obj_t *panel = kit_panel(column, NULL, 0), *row;

    lv_obj_set_width(panel, LV_PCT(100));
    row = kit_box(kit_panel_body(panel), false, 28);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    new_reading(row, &first, true);
    new_reading(row, &second, false);
    /* Rule between the two; names at the top, figures on the same bottom line */
    lv_obj_set_style_pad_left(second.box, ui_px(28), 0);
    lv_obj_set_style_border_side(second.box, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(second.box, ui_px(1), 0);
    lv_obj_set_style_border_color(second.box, theme_color(TOKEN_ENAMEL_DEEP), 0);
    lv_obj_update_layout(row);
    lv_obj_set_height(second.box, lv_obj_get_height(first.box));
    lv_obj_set_flex_align(second.box, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

/* Right-aligned column heading or cell */
static lv_obj_t *cell(lv_obj_t *row, const char *text, text_role role, int32_t width)
{
    lv_obj_t *label = kit_label(row, text, role);

    lv_obj_set_width(label, ui_px(width));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_recolor(label, true);
    return label;
}

/* Processor and graphics card, column headings in the title strip */
static void build_breakdown(lv_obj_t *row)
{
    static const char *const names[PART_COUNT] = { "Processor", "Graphics" };
    lv_obj_t *strip, *body, *line, *name;

    breakdown = kit_panel(row, "Machine breakdown", 0);
    lv_obj_set_width(breakdown, 0);
    lv_obj_set_flex_grow(breakdown, 3);
    strip = kit_panel_strip(breakdown);
    lv_obj_set_style_pad_column(strip, ui_px(8), 0);
    head_now = cell(strip, "", TEXT_CAPTION, NOW_WIDTH);
    head_energy = cell(strip, "", TEXT_CAPTION, ENERGY_WIDTH);
    cell(strip, "Share of machine", TEXT_CAPTION, SHARE_WIDTH);

    body = kit_panel_body(breakdown);
    for (int i = 0; i < PART_COUNT; i++) {
        line = kit_box(body, false, 8);
        lv_obj_set_size(line, LV_PCT(100), ui_px(32));
        lv_obj_set_style_border_side(line, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(line, ui_px(1), 0);
        lv_obj_set_style_border_color(line, theme_color(TOKEN_GRID), 0);
        kit_dot(line, pen[part_series[i]]);
        name = kit_label(line, names[i], TEXT_TITLE);
        lv_obj_set_flex_grow(name, 1);
        part_now[i] = cell(line, NOTHING, TEXT_ROW_FIGURE, NOW_WIDTH);
        part_energy[i] = cell(line, NOTHING, TEXT_ROW_FIGURE, ENERGY_WIDTH);
        part_share[i] = cell(line, NOTHING, TEXT_ROW_FIGURE, SHARE_WIDTH);
    }
}

static int32_t width_of(const char *text, const lv_font_t *font)
{
    lv_point_t size;

    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

/* The closed picker doesn't cut its text, so a long name is shortened with an ellipsis */
static void fit_name(lv_obj_t *picker)
{
    char name[64];
    const lv_font_t *font = lv_obj_get_style_text_font(picker, LV_PART_MAIN);
    size_t len;

    lv_obj_update_layout(picker);
    int32_t room = lv_obj_get_content_width(picker) - ui_px(6)
                   - width_of(LV_SYMBOL_DOWN, lv_obj_get_style_text_font(picker, LV_PART_INDICATOR));

    lv_dropdown_get_selected_str(picker, name, sizeof name - 4);
    if (width_of(name, font) <= room) {
        lv_dropdown_set_text(picker, NULL);
        return;
    }
    /* Back a whole UTF-8 letter at a time (continuation bytes are 10xxxxxx), then drop a
     * trailing space */
    room -= width_of("…", font);
    len = strlen(name);
    while (len > 0 && width_of(name, font) > room) {
        do len--;
        while (len > 0 && ((unsigned char)name[len] & 0xC0) == 0x80);
        name[len] = '\0';
    }
    while (len > 0 && name[len - 1] == ' ') len--;
    strcpy(name + len, "…");
    lv_dropdown_set_text(picker, name);
}

/* The chosen electricity's grams per kWh, its control and its note */
static void show_mix(void)
{
    uint32_t kind = lv_dropdown_get_selected(mix_kind);
    const electricity *e = kind == MIX_COUNTRY  ? carbon_country((int)lv_dropdown_get_selected(mix_country))
                           : kind == MIX_SOURCE ? carbon_source((int)lv_dropdown_get_selected(mix_source))
                                                : NULL;
    const char *typed = lv_textarea_get_text(mix_own);
    char number[16];

    kit_show(mix_country, kind == MIX_COUNTRY);
    kit_show(mix_source, kind == MIX_SOURCE);
    kit_show(mix_own, kind == MIX_OWN);
    kit_show(mix_own_unit, kind == MIX_OWN);
    if (e == NULL) {
        grams_per_kwh = typed[0] ? atof(typed) : -1.0;
        kit_set_text(mix_note, "Your own value, in g CO2e per kWh");
        return;
    }
    grams_per_kwh = e->grams;
    fmt_number(number, sizeof number, e->grams, 0);
    if (e->year > 0)
        lv_label_set_text_fmt(mix_note, "%s g CO2e per kWh, the %d average (Ember)", number, e->year);
    else
        lv_label_set_text_fmt(mix_note, "%s g CO2e per kWh over the life cycle (IPCC)", number);
    fit_name(kind == MIX_COUNTRY ? mix_country : mix_source);
}

static void on_mix_changed(lv_event_t *e)
{
    /* A value to type starts from the one shown */
    if (lv_event_get_target(e) == mix_kind && lv_dropdown_get_selected(mix_kind) == MIX_OWN
        && lv_textarea_get_text(mix_own)[0] == '\0' && grams_per_kwh >= 0.0) {
        char number[16];
        lv_textarea_set_text(mix_own, fmt_number(number, sizeof number, grams_per_kwh, 0));
    }
    show_mix();
    show_readings();
}

/* Names, one per line */
static const char *names_of(const electricity *(*get)(int), int count, char *buf, size_t n)
{
    size_t used = 0;

    buf[0] = '\0';
    for (int i = 0; i < count && used < n; i++)
        used += (size_t)snprintf(buf + used, n - used, "%s%s", i ? "\n" : "", get(i)->name);
    return buf;
}

/* The electricity the emissions are counted for */
static void build_carbon(lv_obj_t *row)
{
    /* Up to 300 names of 47 bytes */
    static char names[16 * 1024];
    lv_obj_t *body, *line;

    carbon_panel = kit_panel(row, "Carbon intensity", 8);
    lv_obj_set_width(carbon_panel, 0);
    lv_obj_set_flex_grow(carbon_panel, 2);
    body = kit_panel_body(carbon_panel);

    line = kit_box(body, false, 8);
    lv_obj_set_width(line, LV_PCT(100));
    kit_let_out(line, 4);
    mix_kind = kit_picker(line, "Country\nEnergy source\nOwn value", MIX_COUNTRY, on_mix_changed, NULL);
    mix_country = kit_picker(line, names_of(carbon_country, carbon_country_count(), names, sizeof names),
                             (uint32_t)carbon_local_country(), on_mix_changed, NULL);
    mix_source = kit_picker(line, names_of(carbon_source, carbon_source_count(), names, sizeof names), 0,
                            on_mix_changed, NULL);
    /* Names take what the kind leaves */
    lv_obj_set_width(mix_country, 0);
    lv_obj_set_flex_grow(mix_country, 1);
    lv_obj_set_width(mix_source, 0);
    lv_obj_set_flex_grow(mix_source, 1);
    mix_own = kit_entry(line, "", true);
    lv_obj_set_width(mix_own, ui_px(64));
    lv_textarea_set_max_length(mix_own, 4);
    lv_obj_add_event_cb(mix_own, on_mix_changed, LV_EVENT_VALUE_CHANGED, NULL);
    mix_own_unit = kit_label(line, "g/kWh", TEXT_WORD);

    mix_note = kit_lines(kit_label(body, "", TEXT_CAPTION), 1);
    lv_obj_set_width(mix_note, LV_PCT(100));
    show_mix();
}

/* Breakdown and carbon intensity side by side, the same height */
static void build_parts(lv_obj_t *column)
{
    parts_row = kit_box(column, false, 10);
    lv_obj_set_width(parts_row, LV_PCT(100));
    lv_obj_set_flex_align(parts_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    kit_let_out(parts_row, 4);
    build_breakdown(parts_row);
    build_carbon(parts_row);
    lv_obj_update_layout(parts_row);
    int32_t height = LV_MAX(lv_obj_get_height(breakdown), lv_obj_get_height(carbon_panel));
    lv_obj_set_height(breakdown, height);
    lv_obj_set_height(carbon_panel, height);
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

static void build_chart(lv_obj_t *column)
{
    static const char *const names[4] = { "Whole machine", "Processor", "Graphics", "" };
    /* Choices in this order, the followed program first */
    static const series order[] = { SERIES_TARGET, SERIES_TOTAL, SERIES_CPU, SERIES_GPU };
    lv_obj_t *panel = kit_panel(column, NULL, 6);
    lv_obj_t *body = kit_panel_body(panel), *top, *spacer, *plot, *times, *caption;
    const lv_font_t *small = ui_font(12);
    /* Pad half a label above and below, as the top and bottom band labels are centred on the
     * outer grid lines. Same padding on scale and chart */
    int32_t half = (lv_font_get_line_height(small) + 1) / 2;

    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_grow(body, 1);

    /* Choices, span at the end */
    top = kit_box(body, false, 6);
    lv_obj_set_width(top, LV_PCT(100));
    kit_let_out(top, 4);
    for (size_t i = 0; i < 4; i++) {
        series s = order[i];
        choices[s] = kit_choice(top, pen[s], names[s], on_choice, (void *)(intptr_t)s);
    }
    spacer = kit_box(top, false, 0);
    lv_obj_set_flex_grow(spacer, 1);
    range_label = kit_label(top, "", TEXT_CAPTION);

    plot = kit_box(body, false, 8);
    lv_obj_set_width(plot, LV_PCT(100));
    lv_obj_set_flex_grow(plot, 1);
    lv_obj_set_style_min_height(plot, ui_px(PLOT_MIN), 0);
    kit_let_out(plot, 8);

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

    /* Thin grid, the line 3 px wide on Retina */
    chart = lv_chart_create(plot);
    lv_obj_set_height(chart, LV_PCT(100));
    lv_obj_set_flex_grow(chart, 1);
    lv_obj_set_scrollable(chart, false);
    lv_obj_set_clickable(chart, false);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    /* A report appends one point per series, the oldest drops off the left */
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
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
    /* Marks reach past the plot by a dot and its ring */
    lv_obj_set_ext_draw_size(chart, ui_px(8));
    lv_obj_refresh_ext_draw_size(chart);
    lv_obj_add_event_cb(chart, draw_time_lines, LV_EVENT_DRAW_MAIN_BEGIN, NULL);
    lv_obj_add_event_cb(chart, draw_marks, LV_EVENT_DRAW_MAIN_END, NULL);
    for (int s = 0; s < 4; s++) lines[s] = lv_chart_add_series(chart, theme_color(pen[s]), LV_CHART_AXIS_PRIMARY_Y);
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
    ago_end = kit_label(times, "", TEXT_CAPTION);
    /* Centred under the middle line whatever the widths of the side labels */
    lv_obj_set_ignore_layout(ago_mid, true);
    lv_obj_align(ago_mid, LV_ALIGN_CENTER, 0, 0);

    caption = kit_box(body, false, 28);
    lv_obj_set_style_pad_top(caption, ui_px(4), 0);
    for (int i = 0; i < 3; i++) {
        items[i] = kit_label(caption, "", TEXT_CAPTION);
        lv_label_set_recolor(items[i], true);
    }
}

void display_create(lv_obj_t *column)
{
    build_readings(column);
    build_parts(column);
    build_chart(column);

    kit_show(second.box, false);
    kit_show(choices[SERIES_TARGET], false);
    choose(SERIES_TOTAL);
    set_run(RUN_IDLE);
}

/* -------------------------------------------------------------------------------------- */
/* Sessions                                                                               */
/* -------------------------------------------------------------------------------------- */

/* Placeholders until the first reading */
static void clear_readings(void)
{
    memset(spread, 0, sizeof spread);
    missed = 0;
    last_cycle = (cycle){ 0 };
    last_sum = (totals){ 0 };
    target_trouble[0] = '\0';
    controls_show_progress(0.0, 0, 0);
}

void display_start(const target *t)
{
    char name[160];

    session_target = *t;
    samples = 0;
    history_clear();
    replot();
    has_cpu = has_gpu = false;
    load_reads = true;
    target_lost = csv_failed = false;

    target_name(t, name, sizeof name);
    if (t->kind == TARGET_APPLICATION)
        snprintf(heading, sizeof heading, "Application %s", t->app);
    else if (t->kind == TARGET_PROCESS)
        snprintf(heading, sizeof heading, "Process %u", t->pid);
    else
        snprintf(heading, sizeof heading, "Whole machine");

    /* The followed program comes first, the machine beside it */
    kit_set_text(first.name, follows() ? name : "Whole machine");
    lv_obj_set_style_bg_color(first.dot, theme_color(follows() ? TOKEN_TARGET : TOKEN_TOTAL), 0);
    kit_show(second.box, follows());
    kit_choice_set(choices[SERIES_TARGET], name);
    kit_show(choices[SERIES_TARGET], follows());
    /* Unknown until the hardware is opened */
    kit_show(carbon_panel, true);
    kit_show(parts_row, true);
    ui_alarm_clear();
    clear_readings();

    choose(follows() ? SERIES_TARGET : SERIES_TOTAL);
    sampler_start(t, interval_seconds(chosen_interval()));
    set_run(RUN_STARTING);
    say("Started: %s, every %s.", heading, interval_label(chosen_interval()));
}

void display_stop(void)
{
    sampler_stop();
    csv_stop_recording();
    set_run(RUN_IDLE);
    say("Stopped after %d %s.", samples, samples == 1 ? "measurement" : "measurements");
}

void display_pause(bool paused)
{
    if (run != RUN_LIVE && run != RUN_PAUSED) return;
    sampler_pause(paused);
    if (!paused) break_trend();
    set_run(paused ? RUN_PAUSED : RUN_LIVE);
    ui_message(paused ? "Paused: the time and the energy stop counting." : "Resumed.");
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
        replot();
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
        /* The chosen series may have gone with the GPU, or never be read without the CPU */
        if (lv_obj_is_hidden(choices[shown]) || !readable(shown))
            choose(readable(SERIES_TARGET) ? SERIES_TARGET : SERIES_TOTAL);
        break;
    case REPORT_MEASURED: {
        history_point point = { .taken = r->taken, .data = r->data };
        bool written;

        samples++;
        history_append(&point);
        plot(&point);
        written = csv_record_point(&point);

        /* Warn once when the followed program becomes unreadable */
        if (follows() && r->data.target_usage < 0.0)
            snprintf(target_trouble, sizeof target_trouble, "%s", r->trouble);
        if (follows() && (r->data.target_usage < 0.0) != target_lost) {
            char name[160];

            target_lost = !target_lost;
            if (target_lost)
                warn("%s could not be read: %s.", target_name(&session_target, name, sizeof name),
                     r->trouble[0] ? r->trouble : "it is not running");
        }

        add_to_spread(&r->data, &r->sum);
        last_cycle = r->data;
        last_sum = r->sum;
        show_readings();
        refresh_chart();
        controls_show_progress(r->sum.elapsed, samples, missed);

        /* Once per failure, again only after a row was written */
        if (!written && !csv_failed) warn("%s. Measuring goes on.", csv_trouble());
        csv_failed = !written;
        break;
    }
    case REPORT_SKIPPED:
        missed++;
        controls_show_progress(last_sum.elapsed, samples, missed);
        break_trend();
        ui_message("The machine was asleep: that measurement was dropped.");
        break;
    case REPORT_FAILED:
        missed++;
        controls_show_progress(last_sum.elapsed, samples, missed);
        break_trend();
        warn("A measurement failed (%s). Measuring goes on.", r->trouble);
        break;
    }
}
