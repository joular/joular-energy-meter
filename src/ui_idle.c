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

/* The idle power dialog: the whole machine measured for 30 s on its own, nothing recorded and
 * the session on screen left as it is; its average offered as the idle power. When no power
 * can be read, it says why and offers what the system allows, as the warning banner does. */

#include <math.h>
#include <stdio.h>

#include "app.h"

#define IDLE_SECONDS 30.0

typedef enum { IDLE_READY, IDLE_MEASURING, IDLE_DONE, IDLE_FAILED } step;

static struct {
    lv_obj_t *plate, *big, *bar, *done, *detail, *command, *copy_key, *again_key, *main_key;
    step at;
    double cpu, gpu;   /* averages, once done */
    void (*on_use)(double cpu, double gpu);
    /* With no power: the key's words for what the system offers, the command to copy */
    const char *key, *copy;
    char whole[16];    /* "30 s" */
} d;

/* "6.1 W", "420 mW", as the readings write them */
static char *watts_text(char *buf, size_t n, double watts)
{
    char number[24];
    const char *unit = "";

    fmt_power(number, sizeof number, watts, &unit);
    snprintf(buf, n, "%s %s", number, unit);
    return buf;
}

static void show(step at, const char *big, const char *detail)
{
    char label[48], number[32];
    bool moved = at != d.at, failed = at == IDLE_FAILED;

    d.at = at;
    kit_set_text(d.big, big);
    kit_show(d.big, !failed);
    kit_set_text(d.detail, detail);
    kit_show(d.bar, at == IDLE_MEASURING);
    kit_show(d.command, failed && d.copy[0]);
    kit_show(d.copy_key, failed && d.copy[0]);
    /* Again after a result, or after a failure nothing can fix here */
    kit_show(d.again_key, at == IDLE_DONE || (failed && d.key == NULL));
    kit_show(d.main_key, !failed || d.key != NULL);
    if (at == IDLE_DONE) {
        snprintf(label, sizeof label, "Use %s", watts_text(number, sizeof number, d.cpu + d.gpu));
        kit_button_set(d.main_key, NULL, label, BUTTON_GO);
    } else if (failed) {
        if (d.key != NULL) kit_button_set(d.main_key, NULL, d.key, BUTTON_GO);
    } else {
        kit_button_set(d.main_key, at == IDLE_MEASURING ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY,
                       at == IDLE_MEASURING ? "Stop" : "Start", at == IDLE_MEASURING ? BUTTON_HALT : BUTTON_GO);
    }
    kit_enable(d.main_key, true);
    if (moved) lv_group_focus_obj(lv_obj_is_hidden(d.main_key) ? d.again_key : d.main_key);
}

static void measure(void)
{
    target machine = { .kind = TARGET_WHOLE_SYSTEM };

    lv_obj_set_width(d.done, 0);
    show(IDLE_MEASURING, d.whole, "Starting");
    platform_keep_awake(true);
    sampler_start(&machine, 1.0);
}

static void halt(void)
{
    sampler_stop();
    platform_keep_awake(false);
}

/* Why, and the key for what the system offers: the administrator's password on macOS and
 * Windows, polkit's on Linux */
static void cannot_read(void)
{
    const char *hint;
    char text[1200];

    display_power_help(&hint, &d.copy, &d.key);
    snprintf(text, sizeof text, "No power source can be read yet, so there is no idle power to measure. %s", hint);
    kit_set_text(d.command, d.copy);
    kit_button_set(d.copy_key, LV_SYMBOL_COPY, "Copy command", BUTTON_NORMAL);
    show(IDLE_FAILED, "", text);
}

void idle_dialog_close(void)
{
    if (d.plate == NULL) return;
    if (d.at == IDLE_MEASURING) halt();
    kit_dialog_close(d.plate);
    d.plate = NULL;
}

/* The key offered again, and why */
static void offer(const char *why)
{
    kit_enable(d.main_key, true);
    kit_button_set(d.main_key, NULL, d.key, BUTTON_GO);
    kit_set_text(d.detail, why);
}

/* A program started again as the administrator opens this window there (--idle), and this one
 * quits: only a refusal, or Linux's Allow, comes back */
static void on_answer(bool granted, const char *why)
{
    if (d.plate == NULL || d.at != IDLE_FAILED) return;
    if (granted) measure();
    else offer(why[0] ? why : "Cancelled: power still can't be read.");
}

static void ask(void)
{
    char error[256];

    kit_button_set(d.main_key, NULL, "Waiting for the administrator", BUTTON_GO);
    kit_enable(d.main_key, false);
    if (!elevate_start("--idle", on_answer, error, sizeof error)) offer(error);
}

static void on_cancel(lv_event_t *e)
{
    LV_UNUSED(e);
    idle_dialog_close();
}

static void on_again(lv_event_t *e)
{
    LV_UNUSED(e);
    measure();
}

static void on_copy(lv_event_t *e)
{
    LV_UNUSED(e);
    platform_set_clipboard(d.copy);
    kit_button_set(d.copy_key, LV_SYMBOL_OK, "Copied", BUTTON_NORMAL);
}

static void on_main(lv_event_t *e)
{
    LV_UNUSED(e);
    if (d.at == IDLE_READY) {
        measure();
    } else if (d.at == IDLE_MEASURING) {
        halt();
        show(IDLE_READY, d.whole, "Stopped: nothing was measured.");
    } else if (d.at == IDLE_FAILED) {
        ask();
    } else {
        idle_dialog_close();
        d.on_use(d.cpu, d.gpu);
    }
}

static void on_key(lv_event_t *e)
{
    if (lv_event_get_key(e) == LV_KEY_ESC) idle_dialog_close();
}

static lv_obj_t *dialog_key(lv_obj_t *row, const char *text, button_kind kind, lv_event_cb_t on_click)
{
    lv_obj_t *key = kit_button(row, NULL, text, kind, on_click, NULL);

    lv_obj_set_height(key, ui_px(36));
    kit_dialog_take(d.plate, key);
    lv_obj_add_event_cb(key, on_key, LV_EVENT_KEY, NULL);
    return key;
}

void idle_dialog_open(void (*on_use)(double cpu, double gpu))
{
    lv_obj_t *head, *text, *row;

    if (d.plate != NULL || display_run() != RUN_IDLE) return;
    d.on_use = on_use;
    d.copy = "";
    d.key = NULL;
    d.plate = kit_dialog(500);

    head = kit_box(d.plate, false, 10);
    kit_tile(head, LV_SYMBOL_BARS, TOKEN_TILE_GREY, 30);
    lv_obj_set_style_text_font(kit_label(head, "Measure the idle power", TEXT_TITLE), ui_bold(18), 0);
    text = kit_label(d.plate, "Close other programs and leave the computer alone, without the mouse or the keyboard."
                              " The whole machine is measured for 30 s; nothing is recorded, and the readings on"
                              " screen stay as they are.", TEXT_MUTED);
    lv_obj_set_width(text, LV_PCT(100));

    d.big = kit_label(d.plate, "", TEXT_READING_SECOND);
    lv_obj_set_style_margin_top(d.big, ui_px(4), 0);
    /* The time gone, filled on a track */
    d.bar = kit_box(d.plate, false, 0);
    lv_obj_set_size(d.bar, LV_PCT(100), ui_px(6));
    lv_obj_set_style_radius(d.bar, ui_px(3), 0);
    lv_obj_set_style_clip_corner(d.bar, true, 0);
    lv_obj_set_style_bg_color(d.bar, theme_color(TOKEN_OFF), 0);
    lv_obj_set_style_bg_opa(d.bar, LV_OPA_COVER, 0);
    d.done = kit_box(d.bar, false, 0);
    lv_obj_set_size(d.done, 0, LV_PCT(100));
    lv_obj_set_style_bg_color(d.done, theme_color(TOKEN_ACCENT), 0);
    lv_obj_set_style_bg_opa(d.done, LV_OPA_COVER, 0);
    d.detail = kit_label(d.plate, "", TEXT_MUTED);
    lv_obj_set_width(d.detail, LV_PCT(100));
    lv_obj_set_style_text_color(d.detail, theme_color(TOKEN_TEXT), 0);
    /* The command on its own, readable before copying */
    d.command = kit_label(d.plate, "", TEXT_CAPTION);
    lv_obj_set_width(d.command, LV_PCT(100));
    lv_obj_set_style_text_color(d.command, theme_color(TOKEN_TEXT), 0);
    lv_obj_set_style_bg_color(d.command, theme_color(TOKEN_ENAMEL), 0);
    lv_obj_set_style_bg_opa(d.command, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(d.command, ui_px(6), 0);
    lv_obj_set_style_pad_hor(d.command, ui_px(8), 0);
    lv_obj_set_style_pad_ver(d.command, ui_px(6), 0);

    row = kit_box(d.plate, false, 8);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_margin_top(row, ui_px(6), 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    kit_let_out(row, 4);
    dialog_key(row, "Cancel", BUTTON_NORMAL, on_cancel);
    d.copy_key = dialog_key(row, "", BUTTON_NORMAL, on_copy);
    d.again_key = dialog_key(row, "Measure again", BUTTON_NORMAL, on_again);
    d.main_key = dialog_key(row, "", BUTTON_GO, on_main);
    lv_obj_set_style_min_width(d.main_key, ui_px(110), 0);

    snprintf(d.whole, sizeof d.whole, "%.0f s", IDLE_SECONDS);
    d.at = IDLE_READY;
    show(IDLE_READY, d.whole, "Then use its average as the idle power, or keep yours.");
    lv_group_focus_obj(d.main_key);
}

bool idle_dialog_report(const report *r)
{
    char big[32], now[32], average[32], text[160];
    double elapsed = r->sum.elapsed, energy = r->sum.cpu_energy + r->sum.gpu_energy;

    if (d.plate == NULL || d.at != IDLE_MEASURING) return false;
    if (r->kind == REPORT_STARTED && !r->has_cpu && !r->has_gpu) {
        halt();
        cannot_read();
    } else if (r->kind == REPORT_MEASURED && elapsed > 0.0) {
        snprintf(big, sizeof big, "%.0f s", fmax(0.0, ceil(IDLE_SECONDS - elapsed)));
        snprintf(text, sizeof text, "Now %s, average %s",
                 watts_text(now, sizeof now, fmax(0.0, r->data.cpu_power) + fmax(0.0, r->data.gpu_power)),
                 watts_text(average, sizeof average, energy / elapsed));
        lv_obj_set_width(d.done, LV_PCT((int32_t)lround(fmin(100.0, elapsed * 100.0 / IDLE_SECONDS))));
        show(IDLE_MEASURING, big, text);
        /* Within half a cycle of the end */
        if (elapsed + 0.5 >= IDLE_SECONDS) {
            halt();
            d.cpu = r->sum.cpu_energy / elapsed;
            d.gpu = r->sum.gpu_energy / elapsed;
            snprintf(text, sizeof text, "The average over %.0f s: processor %s, graphics %s.", elapsed,
                     watts_text(now, sizeof now, d.cpu), watts_text(average, sizeof average, d.gpu));
            show(IDLE_DONE, watts_text(big, sizeof big, d.cpu + d.gpu), text);
        }
    }
    return true;
}
