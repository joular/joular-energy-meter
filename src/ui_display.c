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

/* Display: the Electricity and Idle power cards of the Setup page; the readings, breakdown,
 * electricity and chart of the Readings page. Reports from the measuring thread arrive here on
 * the main thread. */

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
static const theme_token pen[SERIES_COUNT] = { TOKEN_TOTAL, TOKEN_CPU, TOKEN_GPU, TOKEN_TARGET, TOKEN_PLUG };

/* Least plot height, logical px: below it the column scrolls */
#define PLOT_MIN 96

/* Beside "Whole machine": not the screen, memory, disks or power supply */
#define MACHINE_SCOPE "processor and graphics only"

/* Hover help */
#define MACHINE_TIP "The power of the processor and the graphics card, as the computer reports them. The screen," \
    " memory, disks and the power supply's losses aren't in it: a plug meter shows the whole."
#define SHARE_TIP "The program's power is its share of the processor's power, from its CPU load as PowerJoular" \
    " counts it; the graphics card isn't given to it. This is that power over the machine's."
#define LOAD_TIP "How busy the processor is, all its cores together."
#define TARGET_LOAD_TIP "The part of the machine's processing capacity the program used: all of one core out of" \
    " eight is 12.5 %. Not a part of the processor's power."
#define SPREAD_TIP "How much the power varied around its average since the start (standard deviation)."
#define YEAR_TIP "The energy of a whole year at the average power since the start, day and night."
#define ABOVE_TIP "Shows the readings without the idle power: what the program, or anything you run, adds to" \
    " an idle machine. The breakdown and the cost stay whole."
#define SELF_TIP "The processor capacity this meter uses itself, so it can be told apart from what it measures."

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
enum { PART_CPU, PART_GPU, PART_COUNT, ROW_PLUG = PART_COUNT, ROW_COUNT };
static const series row_series[ROW_COUNT] = { SERIES_CPU, SERIES_GPU, SERIES_PLUG };
static lv_obj_t *parts_row, *breakdown, *head_now, *head_energy;
/* Their rows, then the plug's, which has no share */
static lv_obj_t *row_line[ROW_COUNT], *row_name[ROW_COUNT], *row_now[ROW_COUNT], *row_energy[ROW_COUNT];
static lv_obj_t *part_share[PART_COUNT];

/* Electricity the emissions are counted for: a country's average, one energy source, or a
 * typed value. Emissions are worked out again from the last measurement when it changes. */
enum { MIX_COUNTRY, MIX_MONTH, MIX_SOURCE, MIX_OWN };
static lv_obj_t *mix_kind, *mix_country, *mix_source, *mix_own, *mix_own_unit, *mix_note;
/* The price paid per kWh. On the Readings page, the intensity used, the cost and everyday terms. */
static lv_obj_t *price_entry, *currency_entry;
static lv_obj_t *results, *mix_used, *cost_caption, *cost_figure, *cost_note, *like_note;
static double grams_per_kwh;   /* negative when no value is typed */
static cycle last_cycle;
static totals last_sum;

/* Power of each series over the session: mean and Welford's running sum of squares */
static struct { double mean, squares, weight; } spread[SERIES_COUNT];
static int missed;

/* Chart of one series, chosen above it, and the caption under it */
static lv_obj_t *choices[SERIES_COUNT], *range_label, *scale, *chart, *chart_note, *ago_far, *ago_mid, *ago_end;
static lv_obj_t *items[4];
static lv_chart_series_t *lines[SERIES_COUNT];
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
static bool end_said;      /* the followed process or command ended, said once */
static bool hardware_known;   /* the session's power sources are known (REPORT_STARTED) */

/* The plug meter's file as chosen, used from the next Start; whether this session has one */
static char plug_file[4096];
static bool session_plug, plug_warned;
static lv_obj_t *plug_key, *plug_label;

/* Idle power of the machine, optional: measured in the idle dialog, or typed, and kept between
 * launches. Above idle subtracts it: idle x time from the machine's energy, and for the program
 * the idle power of the processor times its share of the processor over the time. at is when it
 * was measured, 0 when typed. */
static struct { double cpu, gpu; int64_t at; bool known; } idle;
static double share_seconds;   /* the program's share of the processor's power, times the time */
static lv_obj_t *idle_note, *idle_entry, *measure_key, *above_box, *above_switch, *above_name, *copy_key;
static bool filling;           /* the idle field is set by the program, not typed */
static int64_t session_from;   /* when the session started, Unix time */

static void show_idle(void);
static void fit_parts(void);
static void show_session(void);
static void show_money(void);

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
    case SERIES_PLUG:
        return session_plug;
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
    case SERIES_PLUG:
        return sum->plug_energy;
    default:
        return sum->total_energy;
    }
}

/* Measured, and for the followed program and the plug read at least once */
static bool counted(series s)
{
    return samples > 0 && readable(s) && (s != SERIES_TARGET || last_sum.target_read)
           && (s != SERIES_PLUG || last_sum.plug_read);
}

/* Energy over the measured time, as the readings show after Stop. Negative when there is none
 * yet. The followed program's counts the time it wasn't running, so energy = average x time. */
static double average_of(series s)
{
    /* The plug over the time it gave readings: a file gone quiet is unknown, not 0 W */
    double time = s == SERIES_PLUG ? last_sum.plug_time : last_sum.elapsed;

    return counted(s) && time > 0.0 ? energy_of(&last_sum, s) / time : -1.0;
}

/* Power now, or the average after Stop */
static double reading_of(series s)
{
    return stopped() ? average_of(s) : power_of(&last_cycle, s);
}

/* The idle power within a series; none for the program, see shown_energy */
static double idle_of(series s)
{
    switch (s) {
    case SERIES_CPU:
        return idle.cpu;
    case SERIES_GPU:
        return idle.gpu;
    case SERIES_TOTAL:
        return idle.cpu + idle.gpu;
    default:
        return 0.0;
    }
}

static bool above_on(void)
{
    return idle.known && lv_obj_has_state(above_switch, LV_STATE_CHECKED);
}

/* Energy since start, all of it or above idle */
static double above_idle_energy(series s)
{
    double joules = energy_of(&last_sum, s);

    return fmax(0.0, joules - (s == SERIES_TARGET ? idle.cpu * share_seconds : idle_of(s) * last_sum.elapsed));
}

static double shown_energy(series s)
{
    return above_on() ? above_idle_energy(s) : energy_of(&last_sum, s);
}

/* reading_of, all of it or above idle */
static double shown_power(series s)
{
    double watts = reading_of(s);

    if (!above_on() || watts < 0.0) return watts;
    if (stopped()) return last_sum.elapsed > 0.0 ? shown_energy(s) / last_sum.elapsed : -1.0;
    if (s != SERIES_TARGET) return fmax(0.0, watts - idle_of(s));
    /* The program's share of what the processor draws above idle */
    return last_cycle.cpu_power > 0.0 ? fmax(0.0, last_cycle.cpu_power - idle.cpu) * watts / last_cycle.cpu_power : 0.0;
}

/* Each measurement weighted by the time it covers, so the spread is around the average
 * (energy over time). West's weighted Welford. Before last_sum takes sum. */
static void add_to_spread(const cycle *data, const totals *sum)
{
    double span = sum->elapsed - last_sum.elapsed;

    if (span <= 0.0) return;
    for (int s = 0; s < SERIES_COUNT; s++) {
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
    double watts = shown_power(s), joules = shown_energy(s);
    bool has = counted(s);

    show_pair(r, FIG_POWER, has && watts >= 0.0 ? fmt_power(text, sizeof text, watts, &unit) : NULL, unit);
    show_pair(r, FIG_ENERGY, has ? scaled_text(text, sizeof text, joules, joule_units, &unit) : NULL, unit);
    show_pair(r, FIG_CARBON, has && grams_per_kwh >= 0.0
                                 ? scaled_text(text, sizeof text, joules * grams_per_kwh / 3600.0, gram_units, &unit)
                                 : NULL, unit);
}

/* The followed program's share of the machine, or why it wasn't read */
static void show_note(void)
{
    char share[16], text[200] = "";

    if (!follows())
        snprintf(text, sizeof text, "%s", MACHINE_SCOPE);
    else if (samples == 0 || !has_cpu)
        ;
    else if (stopped()) {
        if (last_sum.target_read && shown_energy(SERIES_TOTAL) > 0.0)
            snprintf(text, sizeof text, "%s %% of the machine's energy%s",
                     share_text(share, sizeof share, shown_energy(SERIES_TARGET), shown_energy(SERIES_TOTAL)),
                     above_on() ? " above idle" : "");
    } else if (last_cycle.target_power < 0.0) {
        if (target_trouble[0])
            snprintf(text, sizeof text, "Not read: %s", target_trouble);
        else
            snprintf(text, sizeof text, "Not running");
    } else if (shown_power(SERIES_TOTAL) > 0.0) {
        snprintf(text, sizeof text, "%s %% of the machine's power%s",
                 share_text(share, sizeof share, shown_power(SERIES_TARGET), shown_power(SERIES_TOTAL)),
                 above_on() ? " above idle" : "");
    }
    kit_set_text(first.note, text);
    kit_tip(first.note, !follows() ? MACHINE_TIP : strchr(text, '%') != NULL ? SHARE_TIP : "");
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
        if (total <= 0.0 || !readable(row_series[i]))
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

    for (int i = 0; i < ROW_COUNT; i++) {
        series s = row_series[i];
        bool has = counted(s);
        double watts = reading_of(s);

        show_figure(row_now[i], has && watts >= 0.0 ? fmt_power(text, sizeof text, watts, &unit) : NULL, unit);
        show_figure(row_energy[i], has ? scaled_text(text, sizeof text, energy_of(&last_sum, s), joule_units, &unit)
                                       : NULL, unit);
    }
    if (stopped())
        show_shares(last_sum.cpu_energy, last_sum.gpu_energy, last_sum.total_energy);
    else
        show_shares(last_cycle.cpu_power, last_cycle.gpu_power, last_cycle.total_power);
}

/* Caption item: name in the muted colour, value in ink; NULL name hides it */
static void show_item(int i, const char *name, const char *value, const char *tip)
{
    char text[128], ink[8];

    if (name == NULL) {
        kit_show(items[i], false);
        return;
    }
    snprintf(text, sizeof text, "%s  #%s %s#", name, hex_of(ink, sizeof ink, TOKEN_TEXT), value);
    kit_set_text(items[i], text);
    kit_tip(items[i], tip);
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
            show_item(n++, held ? "Share of CPU, last reading" : "Share of CPU now", value, TARGET_LOAD_TIP);
        else
            show_item(n++, held ? "CPU load, last reading" : "CPU load now", value, LOAD_TIP);
    }
    if (samples > 1 && counted(shown) && spread[shown].weight > 0.0) {
        fmt_power(number, sizeof number, sqrt(spread[shown].squares / spread[shown].weight), &unit);
        snprintf(value, sizeof value, "%s %s", number, unit);
        show_item(n++, "Spread since start", value, SPREAD_TIP);
    }
    if (average >= 0.0) {
        year_text(number, sizeof number, average, &unit);
        snprintf(value, sizeof value, "%s %s", number, unit);
        show_item(n++, "A year at this average", value, YEAR_TIP);
    }
    /* What measuring costs, so it can be told apart from what is measured */
    if (samples > 0 && load_reads && last_cycle.self_usage >= 0.0) {
        snprintf(value, sizeof value, "%s %%", fmt_number(number, sizeof number, 100.0 * last_cycle.self_usage, 1));
        show_item(n++, held ? "This meter's CPU, last reading" : "This meter's CPU now", value, SELF_TIP);
    }
    while (n < 4) show_item(n++, NULL, NULL, NULL);
}

static void show_readings(void)
{
    show_reading(&first, follows() ? SERIES_TARGET : SERIES_TOTAL);
    if (follows()) show_reading(&second, SERIES_TOTAL);
    show_note();
    show_breakdown();
    show_items();
    show_money();
    kit_enable(copy_key, samples > 0);
}

/* Captions follow the run: now, the last reading while paused, the average after Stop */
static void show_captions(void)
{
    const char *power = stopped() ? "Average" : run == RUN_PAUSED ? "Last reading" : "Now";
    const char *energy = stopped() ? "Energy" : "Energy since start";
    const char *carbon = stopped() ? "Carbon" : "Carbon since start";
    reading *both[] = { &first, &second };
    char above_power[48];

    /* The breakdown below stays whole */
    kit_set_text(head_now, power);
    kit_set_text(head_energy, energy);
    if (above_on()) {
        snprintf(above_power, sizeof above_power, "%s above idle", power);
        power = above_power;
        energy = "Energy above idle";
        carbon = "Carbon above idle";
    }
    for (int i = 0; i < 2; i++) {
        kit_set_text(both[i]->caption[FIG_POWER], power);
        kit_set_text(both[i]->caption[FIG_ENERGY], energy);
        kit_set_text(both[i]->caption[FIG_CARBON], carbon);
    }
}

/* Held values in the muted colour while paused */
static void show_fresh(void)
{
    lv_color_t ink = theme_color(run == RUN_PAUSED ? TOKEN_MUTED : TOKEN_TEXT);

    lv_obj_set_style_text_color(first.number[FIG_POWER], ink, 0);
    lv_obj_set_style_text_color(second.number[FIG_POWER], ink, 0);
    for (int i = 0; i < ROW_COUNT; i++) lv_obj_set_style_text_color(row_now[i], ink, 0);
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
    show_idle();
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

/* way: what the banner's key offers */
static const char *power_access_hint(elevate_way way)
{
#ifdef PJ_MACOS
    (void)way;
    return "powermetrics only runs as the administrator. Restart with admin rights, which asks for"
           " your password, or quit and start again with:";
#elif defined(PJ_WINDOWS)
    (void)way;
    return "Windows' Energy Meter Interface was not found or does not cover the processor."
           " Install the PawnIO driver (https://pawnio.eu) and restart as administrator,"
           " or install Hubblo's RAPL driver.";
#elif defined(PJ_FREEBSD)
    (void)way;
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
    if (way == ELEVATE_ALLOW)
        return "Reading RAPL needs read access to the powercap files. Allowing it lets every local"
               " user read the energy counters (CVE-2020-8694) until the next reboot; on a shared"
               " machine, start " JOULARENERGYMETER_NAME " with sudo under X11 instead. Allow reading"
               " power asks for the administrator, or run after each boot:";
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

/* What the banner's key offers, and its words */
static elevate_way way;

static const char *way_label(void)
{
#ifdef PJ_WINDOWS
    return "Restart as administrator";
#else
    return way == ELEVATE_ALLOW ? "Allow reading power" : "Restart with admin rights";
#endif
}

static void show_hardware(void);

static void on_elevated(bool granted, const char *why)
{
    if (granted && run != RUN_IDLE) {
        display_stop();
        controls_start();
        say("Power can be read now: measuring again.");
    } else if (granted) {
        ui_alarm_clear();
        say("Power can be read now: press Start.");
    } else {
        /* The banner again, unless a new one took its place after Stop */
        if (run != RUN_IDLE) show_hardware();
        if (why[0])
            warn("The administrator could not be asked: %s.", why);
        else
            say("Cancelled: power is still not read.");
    }
}

static void elevate(void)
{
    char error[256];

    /* Before: on Windows the answer comes before elevate_start returns */
    ui_alarm_action("Waiting for the administrator", NULL);
    if (!elevate_start("--start", on_elevated, error, sizeof error)) {
        ui_alarm_action(way_label(), elevate);
        warn("%s", error);
    }
}

void display_power_help(const char **hint, const char **command, const char **key)
{
#ifdef PJ_MACOS
    /* Already root, so powermetrics was just slow with its first sample */
    if (geteuid() == 0) {
        *hint = "powermetrics gave no power reading in time. Measure again.";
        *command = "";
        *key = NULL;
        return;
    }
#endif
    way = elevate_offer();
    *hint = power_access_hint(way);
    *command = power_access_command();
    *key = way != ELEVATE_NONE ? way_label() : NULL;
}

/* The power banner, with the key the system allows */
static void power_alarm(const char *title)
{
    const char *hint, *command, *key;

    display_power_help(&hint, &command, &key);
    ui_alarm(title, hint, command);
    if (key != NULL) ui_alarm_action(key, elevate);
}

/* What the hardware offers, known once opened: which plates, rows and choices show */
static void show_sources(void)
{
    bool has_power = has_cpu || has_gpu;

    /* Without a GPU the processor is the whole machine, so no breakdown, unless the plug's row
     * is there */
    kit_show(breakdown, has_gpu || session_plug);
    kit_show(row_line[PART_GPU], has_gpu);
    kit_show(row_line[ROW_PLUG], session_plug);
    kit_show(choices[SERIES_CPU], has_gpu);
    kit_show(choices[SERIES_GPU], has_gpu);
    kit_show(choices[SERIES_PLUG], session_plug);
    if (session_plug) {
        char tip[320];

        snprintf(tip, sizeof tip, "What the whole computer draws at the wall, screen and power supply included, from"
                                  " the plug meter's file %.180s.", csv_file_name(plug_file));
        kit_tip(row_name[ROW_PLUG], tip);
        kit_tip(choices[SERIES_PLUG], tip);
    }
    /* No power, no emissions */
    kit_show(results, has_power);
    kit_show(parts_row, has_power || session_plug);
    fit_parts();
}

/* The same, and a banner when power can't be read */
static void show_hardware(void)
{
    bool has_power = has_cpu || has_gpu;

    show_sources();

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
        power_alarm("No power source could be read. CPU load is still measured.");
    else if (!has_cpu)
        power_alarm("Only the graphics card's power can be read.");
    else
        ui_alarm_clear();
}

/* -------------------------------------------------------------------------------------- */
/* The chart                                                                              */
/* -------------------------------------------------------------------------------------- */

/* Chart unit: 10 µW, or whole watts above 10 kW, LVGL chart points are 32 bit */
static double per_watt = 100000.0;
static int32_t axis_top;   /* top of the scale, chart units */
/* The scale shown, set again only when it changes; -1 bands for a new chart */
static double shown_step, shown_factor;
static int shown_bands = -1;

/* Appends a measurement, or a gap, to each series. The chart shifts its points along and
 * redraws itself, nothing else is rewritten. */
static void plot(const history_point *p)
{
    for (int s = 0; s < SERIES_COUNT; s++) {
        double value = history_value(p, (series)s);

        lv_chart_set_next_value(chart, lines[s], value >= 0.0 ? (int32_t)lround(value * per_watt) : LV_CHART_POINT_NONE);
    }
}

/* Every point again, newest at the right edge: after the history was cleared, or in a new unit */
static void replot(void)
{
    for (int s = 0; s < SERIES_COUNT; s++) lv_chart_set_all_values(chart, lines[s], LV_CHART_POINT_NONE);
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
    double unit = fmax(history_peak(SERIES_TOTAL), history_peak(SERIES_PLUG)) > 10000.0 ? 1.0 : 100000.0;
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

    for (int s = 0; s < SERIES_COUNT; s++) lv_chart_hide_series(chart, lines[s], s != (int)shown || !drawn);

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
    for (int i = 0; i < SERIES_COUNT; i++) lv_obj_set_state(choices[i], LV_STATE_CHECKED, i == (int)s);
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
    snprintf(text, sizeof text, "%s %s %s at %s", word, fmt_power(number, sizeof number, watts, &unit), unit,
             time_of(clock, sizeof clock, p->taken));
    draw_dot(layer, at, ui_px(4));
    draw_tag(layer, text, before ? at.x - ui_px(10) : at.x + ui_px(10), y, before, true);
}

/* Average line with its label, highest and lowest of the points shown, and the last point */
/* A dashed level across the plot with its tag, at the left or the right; nothing outside it */
static void draw_level(lv_layer_t *layer, const lv_area_t *area, double watts, theme_token color, int32_t dash,
                       int32_t gap, const char *word, bool right)
{
    int32_t y = point_at(area, 1, (int32_t)lround(watts * per_watt)).y;
    lv_draw_line_dsc_t line;
    char number[32], text[64];
    const char *unit = "";

    if (y < area->y1 || y > area->y2) return;
    lv_draw_line_dsc_init(&line);
    line.color = theme_color(color);
    line.width = ui_px(1);
    line.dash_width = ui_px(dash);
    line.dash_gap = ui_px(gap);
    line.p1.x = area->x1;
    line.p2.x = area->x2;
    line.p1.y = line.p2.y = y;
    lv_draw_line(layer, &line);
    snprintf(text, sizeof text, "%s %s %s", word, fmt_power(number, sizeof number, watts, &unit), unit);
    draw_tag(layer, text, right ? area->x2 - ui_px(14) : area->x1 + ui_px(6),
             y - ui_px(10) > area->y1 ? y - ui_px(10) : y + ui_px(10), right, false);
}

static void draw_marks(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    double average = average_of(shown);

    if (!plotted() || mark_last == 0) return;
    lv_obj_get_content_coords(chart, &area);

    if (average >= 0.0) draw_level(layer, &area, average, TOKEN_FAINT, 5, 4, "average since start", false);
    /* The idle power within the machine's series, at the right so the average's tag stays clear */
    if (idle.known && shown != SERIES_TARGET && shown != SERIES_PLUG)
        draw_level(layer, &area, idle_of(shown), TOKEN_MUTED, 2, 3, "idle", true);
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
        .data = { UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE, UNREADABLE },
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

/* The plug meter's file and its key */
static void show_plug(void)
{
    kit_button_set(plug_key, NULL, plug_file[0] ? "Remove" : "Choose…", BUTTON_NORMAL);
    kit_tip(plug_key, plug_file[0] ? "Stops reading the plug meter's file, from the next Start."
                                   : "Reads the CSV file a power meter at the wall writes as it measures, such as"
                                     " PowerSpyCli's: what the whole computer draws, screen and power supply"
                                     " included.");
    kit_set_text(plug_label, plug_file[0] ? csv_file_name(plug_file) : "None");
    kit_set_role(plug_label, plug_file[0] ? TEXT_TITLE : TEXT_WORD);
}

/* Where the idle power comes from, beside its field */
static void show_idle(void)
{
    char number[32], when[32], text[64];
    const char *unit = "";
    time_t at = (time_t)idle.at, now = time(NULL);
    struct tm day, today;
    const struct tm *local;

    kit_enable(measure_key, run == RUN_IDLE);
    kit_enable(plug_key, run == RUN_IDLE);
    kit_show(above_box, idle.known);
    snprintf(text, sizeof text, "Above idle (%s %s)", fmt_power(number, sizeof number, idle.cpu + idle.gpu, &unit), unit);
    kit_set_text(above_name, text);

    if (!idle.known) {
        kit_set_text(idle_note, "Type it, or measure it");
        return;
    }
    if (idle.at == 0) {
        kit_set_text(idle_note, "Your own value");
        return;
    }
    if ((local = localtime(&now)) != NULL) today = *local;
    if (local == NULL || (local = localtime(&at)) == NULL) snprintf(when, sizeof when, "earlier");
    else {
        day = *local;
        if (day.tm_year == today.tm_year && day.tm_yday == today.tm_yday)
            strftime(when, sizeof when, "today at %H:%M", &day);
        else
            strftime(when, sizeof when, "on %Y-%m-%d", &day);
    }
    snprintf(text, sizeof text, "Measured %s", when);
    kit_set_text(idle_note, text);
}

/* Everything that shows it, after a change */
static void idle_changed(void)
{
    show_idle();
    show_captions();
    show_readings();
    refresh_chart();
}

/* The field in watts, from the idle power */
static void fill_idle(void)
{
    char number[32] = "";

    if (idle.known) snprintf(number, sizeof number, "%.*f", idle.cpu + idle.gpu < 10.0 ? 2 : 1, idle.cpu + idle.gpu);
    filling = true;
    lv_textarea_set_text(idle_entry, number);
    filling = false;
}

/* A decimal typed with a point or a comma; buf gets it with a point */
static double typed_number(lv_obj_t *entry, char *buf, size_t n)
{
    snprintf(buf, n, "%s", lv_textarea_get_text(entry));
    for (char *c = buf; *c; c++)
        if (*c == ',') *c = '.';
    return atof(buf);
}

/* Typed: split between the processor and the graphics card as the last one was, all to the
 * processor without one. Empty: none. */
static void on_idle_typed(lv_event_t *e)
{
    char typed[16];
    double watts, whole = idle.cpu + idle.gpu, cpu_part = whole > 0.0 ? idle.cpu / whole : 1.0;

    LV_UNUSED(e);
    if (filling) return;
    watts = typed_number(idle_entry, typed, sizeof typed);
    idle.known = typed[0] != '\0' && watts >= 0.0;
    if (idle.known) {
        idle.cpu = watts * cpu_part;
        idle.gpu = watts - idle.cpu;
        idle.at = 0;
    }
    idle_changed();
}

/* The dialog's average, kept */
static void use_idle(double cpu, double gpu)
{
    char number[32];
    const char *unit = "";

    idle.cpu = cpu;
    idle.gpu = gpu;
    idle.at = fmt_now();
    idle.known = true;
    fill_idle();
    idle_changed();
    settings_save();
    say("Idle power: %s %s. Turn on Above idle on the Readings page to see what comes on top of it.",
        fmt_power(number, sizeof number, cpu + gpu, &unit), unit);
}

void display_measure_idle(void)
{
    ui_show_page(PAGE_SETUP);
    idle_dialog_open(use_idle);
}

static void on_measure_idle(lv_event_t *e)
{
    LV_UNUSED(e);
    display_measure_idle();
}

/* Followed from the next Start */
static void chosen_plug(const char *path, void *user_data)
{
    LV_UNUSED(user_data);
    snprintf(plug_file, sizeof plug_file, "%s", path);
    sampler_set_plug(plug_file);
    show_plug();
    say("Plug meter: the readings written to %s, from the next Start.", csv_file_name(plug_file));
}

/* Choose the file, or stop using it */
static void on_plug(lv_event_t *e)
{
    LV_UNUSED(e);
    if (plug_file[0] == '\0') {
        kit_open_csv("Choose the CSV file the plug meter writes", "", chosen_plug);
        return;
    }
    plug_file[0] = '\0';
    sampler_set_plug("");
    show_plug();
    say("No plug meter from the next Start.");
}

static void on_above(lv_event_t *e)
{
    LV_UNUSED(e);
    show_captions();
    show_readings();
}


#if defined(PJ_MACOS)
#define OS_NAME "macOS"
#elif defined(PJ_WINDOWS)
#define OS_NAME "Windows"
#elif defined(PJ_FREEBSD)
#define OS_NAME "FreeBSD"
#else
#define OS_NAME "Linux"
#endif

/* Appends to buf, cut when full */
static void add(char *buf, size_t n, const char *format, ...) __attribute__((format(printf, 3, 4)));

static void add(char *buf, size_t n, const char *format, ...)
{
    size_t used = strlen(buf);
    va_list args;

    va_start(args, format);
    if (used < n) vsnprintf(buf + used, n - used, format, args);
    va_end(args);
}

/* "average 6.1 W, energy 619.2 J, carbon 7.087 mg" of a series that was counted */
static char *figures_of(char *buf, size_t n, series s)
{
    char watts[32], joules[32], grams[32];
    const char *w = "", *j = "", *g = "";
    double energy = energy_of(&last_sum, s);

    fmt_power(watts, sizeof watts, average_of(s), &w);
    scaled_text(joules, sizeof joules, energy, joule_units, &j);
    snprintf(buf, n, "average %s %s, energy %s %s", watts, w, joules, j);
    if (grams_per_kwh >= 0.0) {
        scaled_text(grams, sizeof grams, energy * grams_per_kwh / 3600.0, gram_units, &g);
        add(buf, n, ", carbon %s %s", grams, g);
    }
    return buf;
}

/* The session in plain text, for a report or an email */
static void summary_of(char *buf, size_t n)
{
    char when[32] = "", clock[16], figures[200], name[160], number[32], share[16];
    const char *unit = "";
    time_t from = (time_t)session_from;
    const struct tm *local = localtime(&from);
    uint32_t kind = lv_dropdown_get_selected(mix_kind);

    buf[0] = '\0';
    if (local != NULL) strftime(when, sizeof when, "%Y-%m-%d %H:%M", local);
    add(buf, n, "%s %s on %s\n", JOULARENERGYMETER_NAME, JOULARENERGYMETER_VERSION, OS_NAME);
    add(buf, n, "Measured: %s, every %s, from %s for %s", heading, interval_label(chosen_interval()), when,
        fmt_clock(clock, sizeof clock, last_sum.elapsed));
    if (missed > 0) add(buf, n, " (%d measurements, %d missed)\n", samples, missed);
    else add(buf, n, " (%d measurements, none missed)\n", samples);
    if (counted(SERIES_TOTAL))
        add(buf, n, "Whole machine (processor and graphics only): %s\n", figures_of(figures, sizeof figures, SERIES_TOTAL));
    if (follows() && counted(SERIES_TARGET))
        add(buf, n, "%s: %s, %s %% of the machine's energy\n", target_name(&session_target, name, sizeof name),
            figures_of(figures, sizeof figures, SERIES_TARGET),
            share_text(share, sizeof share, last_sum.target_energy, last_sum.total_energy));
    if (counted(SERIES_PLUG)) add(buf, n, "At the plug: %s\n", figures_of(figures, sizeof figures, SERIES_PLUG));
    if (idle.known && counted(SERIES_TOTAL)) {
        add(buf, n, "Above the idle power (%s %s): machine %s", fmt_power(number, sizeof number, idle.cpu + idle.gpu, &unit),
            unit, scaled_text(figures, sizeof figures, above_idle_energy(SERIES_TOTAL), joule_units, &unit));
        add(buf, n, " %s", unit);
        if (follows() && counted(SERIES_TARGET)) {
            scaled_text(figures, sizeof figures, above_idle_energy(SERIES_TARGET), joule_units, &unit);
            add(buf, n, ", program %s %s", figures, unit);
        }
        add(buf, n, "\n");
    }
    if (kind == MIX_OWN) add(buf, n, "Electricity: your own value, %s g CO2e per kWh\n", lv_textarea_get_text(mix_own));
    else {
        lv_dropdown_get_selected_str(kind == MIX_SOURCE ? mix_source : mix_country, name, sizeof name);
        add(buf, n, "Electricity: %s, %s\n", name, lv_label_get_text(mix_note));
    }
    if (lv_label_get_text(cost_note)[0]) add(buf, n, "%s\n", lv_label_get_text(cost_note));
    if (lv_label_get_text(like_note)[0]) add(buf, n, "%s\n", lv_label_get_text(like_note));
}

static void on_copy_summary(lv_event_t *e)
{
    static char text[2048];

    LV_UNUSED(e);
    summary_of(text, sizeof text);
    kit_copy(copy_key, text);
    say("The session's summary is in the clipboard.");
}

/* The idle power and the plug meter, kept from the last launch */
static void build_idle(lv_obj_t *page)
{
    lv_obj_t *card = kit_setup_card(page, LV_SYMBOL_BARS, TOKEN_TILE_GREY, "Idle power and plug meter"), *row, *controls;
    const char *cpu = settings_get("idle_cpu"), *gpu = settings_get("idle_gpu"), *at = settings_get("idle_at");
    const char *plug = settings_get("plug"), *used = settings_get("idle");
    lv_obj_t *after;

    kit_chip(kit_panel_strip(card), "Optional");
    row = kit_row(card, "Idle power", "");
    idle_note = kit_row_note(row);
    controls = kit_row_controls(row);
    idle_entry = kit_entry(controls, "None", false);
    lv_obj_set_width(idle_entry, ui_px(80));
    lv_textarea_set_accepted_chars(idle_entry, "0123456789.,");
    lv_textarea_set_max_length(idle_entry, 7);
    kit_tip(idle_entry, "What the machine draws doing nothing, in watts: the processor and the graphics card, as"
                        " measured. Empty for none.");
    kit_label(controls, "W", TEXT_WORD);
    measure_key = kit_button(controls, NULL, "Measure…", BUTTON_NORMAL, on_measure_idle, NULL);
    kit_tip(measure_key, "Measures the whole machine for 30 s while you leave it alone, in a window of its own;"
                         " you can then use its average.");
    after = kit_lines(kit_label(controls, "Above idle, on the Readings page, takes it out", TEXT_MUTED), 1);
    lv_obj_set_width(after, 0);
    lv_obj_set_flex_grow(after, 1);

    row = kit_row(card, "Plug meter", "A wall meter's CSV file");
    controls = kit_row_controls(row);
    plug_label = kit_lines(kit_label(controls, "", TEXT_WORD), 1);
    lv_obj_set_width(plug_label, 0);
    lv_obj_set_flex_grow(plug_label, 1);
    plug_key = kit_button(controls, NULL, "", BUTTON_NORMAL, on_plug, NULL);

    if (cpu != NULL && gpu != NULL && at != NULL && atof(cpu) >= 0.0 && atof(gpu) >= 0.0) {
        idle.cpu = atof(cpu);
        idle.gpu = atof(gpu);
        idle.at = (int64_t)atoll(at);
        /* The split stays for the next value typed */
        idle.known = used == NULL || strcmp(used, "no") != 0;
    }
    fill_idle();
    lv_obj_add_event_cb(idle_entry, on_idle_typed, LV_EVENT_VALUE_CHANGED, NULL);
    if (plug != NULL) snprintf(plug_file, sizeof plug_file, "%s", plug);
    sampler_set_plug(plug_file);
}

/* Copy summary and Above idle in the card's head, the program and the machine side by side */
static void build_readings(lv_obj_t *column)
{
    lv_obj_t *panel = kit_panel(column, LV_SYMBOL_CHARGE, TOKEN_ENERGY, "Readings", 0), *row, *strip, *name;
    const char *above = settings_get("above");

    lv_obj_set_width(panel, LV_PCT(100));
    strip = kit_panel_strip(panel);
    lv_obj_set_flex_grow(kit_box(strip, false, 0), 1);
    copy_key = kit_button(strip, LV_SYMBOL_COPY, "Copy summary", BUTTON_NORMAL, on_copy_summary, NULL);
    lv_obj_set_height(copy_key, ui_px(28));
    kit_tip(copy_key, "Copies this session in plain text, what was measured, for how long, its energy, carbon and"
                      " cost, to paste into a report or an email.");
    above_box = kit_box(strip, false, 8);
    kit_let_out(above_box, 4);
    above_switch = kit_switch(above_box, on_above, NULL);
    name = above_name = kit_label(above_box, "Above idle", TEXT_WORD);
    kit_switch_label(name, above_switch);
    kit_tip(name, ABOVE_TIP);
    kit_tip(above_switch, ABOVE_TIP);
    if (above != NULL && strcmp(above, "yes") == 0) lv_obj_add_state(above_switch, LV_STATE_CHECKED);

    row = kit_box(kit_panel_body(panel), false, 28);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    new_reading(row, &first, true);
    new_reading(row, &second, false);
    kit_set_text(first.note, MACHINE_SCOPE);
    kit_set_text(second.note, MACHINE_SCOPE);
    kit_tip(first.note, MACHINE_TIP);
    kit_tip(second.note, MACHINE_TIP);
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
    static const char *const names[ROW_COUNT] = { "Processor", "Graphics", "At the plug" };
    lv_obj_t *strip, *body, *line;

    breakdown = kit_panel(row, SYMBOL_CPU, TOKEN_TILE_BLUE, "Machine breakdown", 0);
    lv_obj_set_width(breakdown, 0);
    lv_obj_set_flex_grow(breakdown, 3);
    strip = kit_panel_strip(breakdown);
    lv_obj_set_style_pad_column(strip, ui_px(8), 0);
    /* Column heads at the right, over their columns */
    lv_obj_set_flex_grow(kit_box(strip, false, 0), 1);
    head_now = cell(strip, "", TEXT_CAPTION, NOW_WIDTH);
    head_energy = cell(strip, "", TEXT_CAPTION, ENERGY_WIDTH);
    cell(strip, "Share of machine", TEXT_CAPTION, SHARE_WIDTH);

    body = kit_panel_body(breakdown);
    for (int i = 0; i < ROW_COUNT; i++) {
        line = row_line[i] = kit_box(body, false, 8);
        lv_obj_set_size(line, LV_PCT(100), ui_px(32));
        lv_obj_set_style_border_side(line, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(line, ui_px(1), 0);
        lv_obj_set_style_border_color(line, theme_color(TOKEN_GRID), 0);
        kit_dot(line, pen[row_series[i]]);
        row_name[i] = kit_label(line, names[i], TEXT_TITLE);
        lv_obj_set_flex_grow(row_name[i], 1);
        row_now[i] = cell(line, NOTHING, TEXT_ROW_FIGURE, NOW_WIDTH);
        row_energy[i] = cell(line, NOTHING, TEXT_ROW_FIGURE, ENERGY_WIDTH);
        /* The plug isn't a part of the machine: no share */
        if (i < PART_COUNT) part_share[i] = cell(line, NOTHING, TEXT_ROW_FIGURE, SHARE_WIDTH);
        else cell(line, "", TEXT_ROW_FIGURE, SHARE_WIDTH);
    }
    kit_show(row_line[ROW_PLUG], false);
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
    static const char *const month_names[12] = { "January", "February", "March", "April", "May", "June", "July",
                                                 "August", "September", "October", "November", "December" };
    uint32_t kind = lv_dropdown_get_selected(mix_kind);
    bool country = kind == MIX_COUNTRY || kind == MIX_MONTH;
    const electricity *e = country              ? carbon_country((int)lv_dropdown_get_selected(mix_country))
                           : kind == MIX_SOURCE ? carbon_source((int)lv_dropdown_get_selected(mix_source))
                                                : NULL;
    const char *typed = lv_textarea_get_text(mix_own);
    char number[16];
    time_t now = time(NULL);
    const struct tm *local = localtime(&now);
    int month = local != NULL ? local->tm_mon + 1 : 1, year;
    double grams;

    kit_show(mix_country, country);
    kit_show(mix_source, kind == MIX_SOURCE);
    kit_show(mix_own, kind == MIX_OWN);
    kit_show(mix_own_unit, kind == MIX_OWN);
    if (e == NULL) {
        grams_per_kwh = typed[0] ? atof(typed) : -1.0;
        kit_set_text(mix_note, "Your own value, in g CO2e per kWh");
        lv_label_set_text_fmt(mix_used, grams_per_kwh >= 0.0 ? "%s g CO2e per kWh, your own value" : "No carbon intensity",
                                  typed);
        return;
    }
    grams_per_kwh = e->grams;
    fmt_number(number, sizeof number, e->grams, 0);
    /* This calendar month, from the latest year Ember has it, else the year's */
    if (kind == MIX_MONTH && carbon_month(e, month, &year, &grams)) {
        grams_per_kwh = grams;
        lv_label_set_text_fmt(mix_note, "%s g CO2e per kWh, the %s %d average (Ember)",
                              fmt_number(number, sizeof number, grams, 0), month_names[month - 1], year);
    } else if (kind == MIX_MONTH)
        lv_label_set_text_fmt(mix_note, "No monthly figures: %s g CO2e per kWh, the %d average (Ember)", number,
                              e->year);
    else if (e->year > 0)
        lv_label_set_text_fmt(mix_note, "%s g CO2e per kWh, the %d average (Ember)", number, e->year);
    else
        lv_label_set_text_fmt(mix_note, "%s g CO2e per kWh over the life cycle (IPCC)", number);
    /* "France, 40 g CO2e per kWh, the November 2025 average (Ember)" */
    lv_label_set_text_fmt(mix_used, "%s, %s", e->name, lv_label_get_text(mix_note));
    fit_name(country ? mix_country : mix_source);
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

/* Kept names of the kinds, in MIX_* order */
static const char *const mix_names[] = { "country", "month", "source", "own" };


/* Index of the kept country (by code, "" for the world) or source (by name) */
static uint32_t kept_choice(const char *key, const electricity *(*get)(int), int count, bool by_code,
                            uint32_t otherwise)
{
    const char *value = settings_get(key);

    for (int i = 0; value != NULL && i < count; i++)
        if (strcmp(by_code ? get(i)->code : get(i)->name, value) == 0) return (uint32_t)i;
    return otherwise;
}

void display_keep(void)
{
    char number[32];

    /* Kept when not used too, for the split of the next value typed */
    settings_set("idle", idle.known ? "yes" : "no");
    snprintf(number, sizeof number, "%.4f", idle.cpu);
    settings_set("idle_cpu", number);
    snprintf(number, sizeof number, "%.4f", idle.gpu);
    settings_set("idle_gpu", number);
    snprintf(number, sizeof number, "%lld", (long long)idle.at);
    settings_set("idle_at", number);
    settings_set("above", lv_obj_has_state(above_switch, LV_STATE_CHECKED) ? "yes" : "no");
    settings_set("plug", plug_file);
    settings_set("carbon", mix_names[lv_dropdown_get_selected(mix_kind)]);
    settings_set("country", carbon_country((int)lv_dropdown_get_selected(mix_country))->code);
    settings_set("source", carbon_source((int)lv_dropdown_get_selected(mix_source))->name);
    settings_set("own_value", lv_textarea_get_text(mix_own));
    settings_set("price", lv_textarea_get_text(price_entry));
    settings_set("currency", lv_textarea_get_text(currency_entry));
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

/* Everyday terms, from the EPA's greenhouse gas equivalencies (US, updated August 2026): a
 * smartphone charge takes 0.019 kWh, an average gasoline car emits 3.93e-4 t CO2e per mile.
 * Below one charge, a 10 W LED bulb, a plain conversion. */
#define PHONE_CHARGE_JOULES (0.019 * 3.6e6)
#define CAR_GRAMS_PER_KM (393.0 / 1.609344)
#define LED_WATTS 10.0

/* Three figures, two decimals at least, never an exponent, no zeros at the end past those:
 * 0.000412, 0.0000088, 1.23 */
static char *money_text(char *buf, size_t n, double value)
{
    int decimals = value > 0.0 ? 2 - (int)floor(log10(value)) : 2;
    char *point, *end;

    fmt_number(buf, n, value, decimals < 2 ? 2 : decimals > 8 ? 8 : decimals);
    if ((point = strchr(buf, '.')) != NULL)
        for (end = buf + strlen(buf) - 1; end > point + 2 && *end == '0'; end--) *end = '\0';
    return buf;
}

/* The machine's whole energy and the program's, as paid for: no above idle here */
static void show_money(void)
{
    char price_text[16], machine[32], program[32], text[160], amount[32];
    const char *currency, *unit = "";
    double price, joules, grams, km;

    currency = lv_textarea_get_text(currency_entry);
    joules = energy_of(&last_sum, SERIES_TOTAL);
    price = typed_number(price_entry, price_text, sizeof price_text);

    kit_set_text(cost_caption, stopped() ? "Cost" : "Cost since start");
    show_figure(cost_figure, counted(SERIES_TOTAL) && price > 0.0 ? money_text(machine, sizeof machine, joules / 3.6e6 * price)
                                                                  : NULL, currency);
    if (price <= 0.0)
        snprintf(text, sizeof text, "No price: set one on the Setup page");
    /* The program's within the machine's */
    else if (follows() && counted(SERIES_TARGET))
        snprintf(text, sizeof text, "%s for the program, at %s %s per kWh",
                 money_text(program, sizeof program, last_sum.target_energy / 3.6e6 * price), price_text, currency);
    else
        snprintf(text, sizeof text, "At %s %s per kWh", price_text, currency);
    kit_set_text(cost_note, text);

    text[0] = '\0';
    if (counted(SERIES_TOTAL) && joules > 0.0) {
        if (joules >= PHONE_CHARGE_JOULES) {
            double charges = joules / PHONE_CHARGE_JOULES;

            fmt_number(amount, sizeof amount, charges, charges < 9.95 ? 1 : 0);
            snprintf(text, sizeof text, "Machine: energy of %s phone charge%s", amount, strcmp(amount, "1.0") ? "s" : "");
        } else {
            double seconds = joules / LED_WATTS;

            if (seconds < 60.0) snprintf(amount, sizeof amount, "%.0f s", seconds);
            else if (seconds < 3600.0) snprintf(amount, sizeof amount, "%.0f min", seconds / 60.0);
            else snprintf(amount, sizeof amount, "%.1f h", seconds / 3600.0);
            snprintf(text, sizeof text, "Machine: energy of a 10 W LED bulb for %s", amount);
        }
        if (grams_per_kwh >= 0.0) {
            grams = joules * grams_per_kwh / 3.6e6;
            km = grams / CAR_GRAMS_PER_KM;
            if (km >= 1.0) unit = "km";
            else if ((km *= 1000.0) >= 1.0) unit = "m";
            if (unit[0])
                add(text, sizeof text, ", CO2e of %s %s by car", fmt_number(amount, sizeof amount, km, km < 9.95 ? 1 : 0),
                    unit);
        }
    }
    kit_set_text(like_note, text);
}

static void on_price_changed(lv_event_t *e)
{
    LV_UNUSED(e);
    show_money();
}

static void kept_text(lv_obj_t *entry, const char *key)
{
    const char *value = settings_get(key);

    if (value != NULL) lv_textarea_set_text(entry, value);
}

/* The electricity the emissions are counted for, and its price */
static void build_electricity(lv_obj_t *page)
{
    /* Up to 300 names of 47 bytes */
    static char names[16 * 1024];
    lv_obj_t *card = kit_setup_card(page, SYMBOL_PLUG, TOKEN_TILE_TEAL, "Electricity"), *row, *line;

    kit_chip(kit_panel_strip(card), "Optional");
    kit_lines(kit_label(kit_panel_strip(card), "Your country's average for the carbon until you choose; a cost only with a price",
                        TEXT_MUTED), 1);
    row = kit_row(card, "Carbon intensity", "");
    mix_note = kit_row_note(row);
    kit_tip(mix_note, "Grams of CO2 equivalent per kWh of electricity, over the life cycle of the power plants.");
    line = kit_row_controls(row);
    mix_kind = kit_picker(line, "Country\nCountry, this month\nEnergy source\nOwn value", (uint32_t)settings_index("carbon", mix_names, 4, MIX_COUNTRY), on_mix_changed,
                          NULL);
    mix_country = kit_picker(line, names_of(carbon_country, carbon_country_count(), names, sizeof names),
                             kept_choice("country", carbon_country, carbon_country_count(), true,
                                         (uint32_t)carbon_local_country()),
                             on_mix_changed, NULL);
    mix_source = kit_picker(line, names_of(carbon_source, carbon_source_count(), names, sizeof names),
                            kept_choice("source", carbon_source, carbon_source_count(), false, 0),
                            on_mix_changed, NULL);
    /* Names take what the kind leaves */
    lv_obj_set_width(mix_country, 0);
    lv_obj_set_flex_grow(mix_country, 1);
    lv_obj_set_width(mix_source, 0);
    lv_obj_set_flex_grow(mix_source, 1);
    mix_own = kit_entry(line, "", true);
    lv_obj_set_width(mix_own, ui_px(64));
    lv_textarea_set_max_length(mix_own, 4);
    /* Before its event: the readings don't exist yet. Digits only and 4 at most, as typed. */
    kept_text(mix_own, "own_value");
    lv_obj_add_event_cb(mix_own, on_mix_changed, LV_EVENT_VALUE_CHANGED, NULL);
    mix_own_unit = kit_label(line, "g/kWh", TEXT_WORD);

    /* "[0.25] [EUR] per kWh". The font has no euro sign: EUR. */
    row = kit_row(card, "Price", "As on your bill; empty for no cost");
    line = kit_row_controls(row);
    price_entry = kit_entry(line, "None", false);
    lv_obj_set_width(price_entry, ui_px(80));
    lv_textarea_set_accepted_chars(price_entry, "0123456789.,");
    lv_textarea_set_max_length(price_entry, 8);
    kit_tip(price_entry, "Your price per kWh, as on your bill; a comma or a point for the decimals.");
    currency_entry = kit_entry(line, "EUR", false);
    lv_obj_set_width(currency_entry, ui_px(64));
    lv_textarea_set_accepted_chars(currency_entry, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz$£¥");
    lv_textarea_set_max_length(currency_entry, 4);
    kit_tip(currency_entry, "A short name: EUR, USD, CHF, $ or £. The font has no euro sign.");
    kit_label(line, "per kWh", TEXT_WORD);
    /* Kept from the last time, before the events: the readings don't exist yet */
    kept_text(price_entry, "price");
    kept_text(currency_entry, "currency");
    lv_obj_add_event_cb(price_entry, on_price_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(currency_entry, on_price_changed, LV_EVENT_VALUE_CHANGED, NULL);
}

/* The intensity the carbon is counted with, the cost at the price set, an everyday equivalent */
static void build_results(lv_obj_t *row)
{
    lv_obj_t *body, *figure;

    results = kit_panel(row, SYMBOL_PLUG, TOKEN_TILE_TEAL, "Electricity", 4);
    lv_obj_set_width(results, 0);
    lv_obj_set_flex_grow(results, 2);
    body = kit_panel_body(results);
    mix_used = kit_lines(kit_label(body, "", TEXT_CAPTION), 1);
    lv_obj_set_width(mix_used, LV_PCT(100));
    kit_tip(mix_used, "Chosen on the Setup page. Grams of CO2 equivalent per kWh of electricity, over the life cycle"
                      " of the power plants.");
    figure = kit_box(body, true, 0);
    lv_obj_set_style_pad_top(figure, ui_px(2), 0);
    cost_caption = kit_label(figure, "", TEXT_MUTED);
    cost_figure = kit_label(figure, NOTHING, TEXT_GROUP_FIGURE);
    lv_label_set_recolor(cost_figure, true);
    cost_note = kit_lines(kit_label(body, "", TEXT_CAPTION), 1);
    lv_obj_set_width(cost_note, LV_PCT(100));
    kit_tip(cost_note, "The machine's energy at your price, and the followed program's part of it. The whole energy,"
                       " also with Above idle on: it is what is paid.");
    like_note = kit_lines(kit_label(body, "", TEXT_CAPTION), 1);
    lv_obj_set_width(like_note, LV_PCT(100));
    kit_tip(like_note, "Orders of magnitude from the US EPA: a phone charge takes 0.019 kWh, an average gasoline car"
                       " emits 244 g CO2e per km. Below one charge, a 10 W LED bulb.");
}

/* Breakdown and electricity the same height, again when the breakdown's rows change */
static void fit_parts(void)
{
    int32_t height;

    lv_obj_set_height(breakdown, LV_SIZE_CONTENT);
    lv_obj_set_height(results, LV_SIZE_CONTENT);
    lv_obj_update_layout(parts_row);
    /* A hidden plate keeps its size: only one shown sets its own */
    height = lv_obj_is_hidden(results)    ? lv_obj_get_height(breakdown)
             : lv_obj_is_hidden(breakdown) ? lv_obj_get_height(results)
                                           : LV_MAX(lv_obj_get_height(breakdown), lv_obj_get_height(results));
    lv_obj_set_height(breakdown, height);
    lv_obj_set_height(results, height);
}

/* Breakdown and electricity side by side, the same height */
static void build_parts(lv_obj_t *column)
{
    parts_row = kit_box(column, false, 10);
    lv_obj_set_width(parts_row, LV_PCT(100));
    lv_obj_set_flex_align(parts_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    kit_let_out(parts_row, 4);
    build_breakdown(parts_row);
    build_results(parts_row);
    fit_parts();
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
    static const char *const names[SERIES_COUNT] = { "Whole machine", "Processor", "Graphics", "", "At the plug" };
    /* Choices in this order, the followed program first */
    static const series order[SERIES_COUNT] = { SERIES_TARGET, SERIES_TOTAL, SERIES_CPU, SERIES_GPU, SERIES_PLUG };
    lv_obj_t *panel = kit_panel(column, NULL, TOKEN_TILE_GREY, NULL, 6);
    lv_obj_t *body = kit_panel_body(panel), *top, *track, *spacer, *plot, *times, *caption;
    const lv_font_t *small = ui_font(12);
    /* Pad half a label above and below, as the top and bottom band labels are centred on the
     * outer grid lines. Same padding on scale and chart */
    int32_t half = (lv_font_get_line_height(small) + 1) / 2;

    lv_obj_set_width(panel, LV_PCT(100));
    lv_obj_set_flex_grow(panel, 1);
    lv_obj_set_flex_grow(body, 1);

    /* Choices joined on a track in the card's head, span at the end */
    top = kit_panel_strip(panel);
    track = kit_choices(top);
    for (size_t i = 0; i < SERIES_COUNT; i++) {
        series s = order[i];
        choices[s] = kit_choice(track, pen[s], names[s], on_choice, (void *)(intptr_t)s);
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
    for (int s = 0; s < SERIES_COUNT; s++) lines[s] = lv_chart_add_series(chart, theme_color(pen[s]), LV_CHART_AXIS_PRIMARY_Y);
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
    for (int i = 0; i < 4; i++) {
        items[i] = kit_label(caption, "", TEXT_CAPTION);
        lv_label_set_recolor(items[i], true);
    }
}

/* Over the cards measuring can do without */
static void build_optional(lv_obj_t *page)
{
    lv_obj_t *line = kit_box(page, false, 10), *note;

    lv_obj_set_width(line, LV_PCT(100));
    lv_obj_set_style_max_width(line, ui_px(SETUP_WIDTH), 0);
    lv_obj_set_style_pad_hor(line, ui_px(6), 0);
    lv_obj_set_style_text_font(kit_label(line, "Optional", TEXT_TITLE), ui_bold(15), 0);
    note = kit_lines(kit_label(line, "Measuring works without these; they add to the readings", TEXT_MUTED), 1);
    lv_obj_set_width(note, 0);
    lv_obj_set_flex_grow(note, 1);
}

void display_settings(lv_obj_t *page)
{
    build_optional(page);
    build_electricity(page);
    build_idle(page);
}

void display_create(lv_obj_t *page)
{
    build_readings(page);
    build_parts(page);
    build_chart(page);
    /* With the Readings page's parts, which show it too */
    show_mix();
    show_plug();

    /* Built again at a zoom, the session as it was; at launch, the empty view */
    show_session();
    kit_show(choices[SERIES_PLUG], false);
    if (hardware_known) show_sources();
    shown_bands = -1;
    replot();
    choose(shown);
    set_run(run);
    if (samples > 0 || run != RUN_IDLE) controls_show_progress(last_sum.elapsed, samples, missed);
}

/* -------------------------------------------------------------------------------------- */
/* Sessions                                                                               */
/* -------------------------------------------------------------------------------------- */

/* Placeholders until the first reading */
static void clear_readings(void)
{
    memset(spread, 0, sizeof spread);
    share_seconds = 0.0;
    missed = 0;
    last_cycle = (cycle){ 0 };
    last_sum = (totals){ 0 };
    target_trouble[0] = '\0';
    controls_show_progress(0.0, 0, 0);
}

/* The followed program first, the machine beside it */
static void show_session(void)
{
    char name[160];

    target_name(&session_target, name, sizeof name);
    kit_set_text(first.name, follows() ? name : "Whole machine");
    lv_obj_set_style_bg_color(first.dot, theme_color(follows() ? TOKEN_TARGET : TOKEN_TOTAL), 0);
    kit_show(second.box, follows());
    kit_choice_set(choices[SERIES_TARGET], name);
    kit_show(choices[SERIES_TARGET], follows());
}

void display_start(const target *t)
{
    char span[16];

    session_target = *t;
    samples = 0;
    history_clear();
    replot();
    has_cpu = has_gpu = false;
    load_reads = true;
    target_lost = csv_failed = end_said = plug_warned = false;
    session_plug = plug_file[0] != '\0';
    session_from = fmt_now();

    /* Cut, it only names the session in messages */
    target_heading(t, heading, sizeof heading);

    show_session();
    /* Unknown until the hardware is opened */
    hardware_known = false;
    kit_show(results, true);
    kit_show(parts_row, true);
    ui_alarm_clear();
    clear_readings();

    choose(follows() ? SERIES_TARGET : SERIES_TOTAL);
    sampler_start(t, interval_seconds(chosen_interval()));
    set_run(RUN_STARTING);
    if (controls_length() > 0)
        say("Started: %s, every %s, for %s.", heading, interval_label(chosen_interval()),
            fmt_clock(span, sizeof span, controls_length()));
    else
        say("Started: %s, every %s%s.", heading, interval_label(chosen_interval()),
            controls_length() < 0 ? ", until it ends" : "");
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

/* Stops at the length chosen, or when the followed program ended if so chosen. Pauses don't
 * count, as for the energy. */
static void check_end(const report *r)
{
    double length = controls_length();
    char span[32], text[300];

    if (r->ended && !end_said) {
        end_said = true;
        fmt_clock(span, sizeof span, r->sum.elapsed);
        if (session_target.kind != TARGET_COMMAND)
            snprintf(text, sizeof text, "%s ended after %s", heading, span);
        else if (r->exit_code >= 0)
            snprintf(text, sizeof text, "The command ended after %s, with exit status %d", span, r->exit_code);
        else
            snprintf(text, sizeof text, "The command ended after %s", span);
        if (length < 0) display_stop();
        say("%s%s", text, length < 0 ? "." : ". Measuring the machine goes on.");
    } else if (length > 0 && r->sum.elapsed + interval_seconds(chosen_interval()) / 2 >= length) {
        display_stop();
        say("Stopped after %s, as chosen.", fmt_clock(span, sizeof span, r->sum.elapsed));
    }
}

void display_report(const report *r)
{
    /* The idle dialog's, not the session's */
    if (idle_dialog_report(r)) return;

    /* The window was blocked for minutes (a menu open, a drag) and the queue overflowed */
    if (r->dropped > 0) {
        missed += r->dropped;
        break_trend();
        warn("%d %s lost while the window was busy%s.", r->dropped,
             r->dropped == 1 ? "measurement was" : "measurements were",
             !csv_is_recording() ? "" : r->dropped == 1 ? ": its CSV row is missing" : ": their CSV rows are missing");
    }

    switch (r->kind) {
    case REPORT_STARTED:
        hardware_known = true;
        if (session_target.kind == TARGET_COMMAND && r->ended) {
            display_stop();
            warn("The command could not start: %s.", r->trouble);
            break;
        }
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
            /* After its end was said, no news */
            if (target_lost && !end_said)
                warn("%s could not be read: %s.", target_name(&session_target, name, sizeof name),
                     r->trouble[0] ? r->trouble : "it is not running");
        }

        /* Once each time the plug's file goes quiet, not in its first seconds */
        if (session_plug && (r->data.plug_power < 0.0) != plug_warned && r->sum.elapsed >= PLUG_STALE_SECONDS) {
            plug_warned = !plug_warned;
            if (plug_warned)
                warn("No new reading from the plug meter for %.0f s, in %s.", PLUG_STALE_SECONDS, csv_file_name(plug_file));
        }

        add_to_spread(&r->data, &r->sum);
        /* As the program's energy counts it: only cycles where it was read */
        if (r->data.target_power > 0.0 && r->data.cpu_power > 0.0)
            share_seconds += (r->sum.elapsed - last_sum.elapsed) * r->data.target_power / r->data.cpu_power;
        last_cycle = r->data;
        last_sum = r->sum;
        show_readings();
        refresh_chart();
        controls_show_progress(r->sum.elapsed, samples, missed);

        /* Once per failure, again only after a row was written */
        if (!written && !csv_failed) warn("%s. Measuring goes on.", csv_trouble());
        csv_failed = !written;
        check_end(r);
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
