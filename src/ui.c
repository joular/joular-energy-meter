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

/* Main screen layout, warning banner and status bar */

#include <stdio.h>
#include <time.h>

#include "app.h"
#include "theme_private.h"

/* Highlight time of a new message, ms */
#define NEW_MESSAGE_MS 5000

static lv_obj_t *message, *link;
static lv_timer_t *message_timer;

static lv_obj_t *alarm_strip, *alarm_title, *alarm_text, *copy_button;
static char alarm_command[1024];
/* One timer, paused when idle; a second click restarts the two seconds of "Copied" */
static lv_timer_t *copy_timer;

/* -------------------------------------------------------------------------------------- */
/* The status bar                                                                         */
/* -------------------------------------------------------------------------------------- */

static void on_message_aged(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    lv_timer_pause(message_timer);
    lv_obj_remove_state(message, LV_STATE_USER_1);
}

/* Errors get a warning sign, not just colour, and stay highlighted until the next message */
static void show_message(const char *text, bool trouble)
{
    char stamp[16] = "";
    time_t now = time(NULL);
    const struct tm *local = localtime(&now);

    if (local != NULL) strftime(stamp, sizeof stamp, "%H:%M:%S", local);
    lv_label_set_text_fmt(message, "%s  %s%s", stamp, trouble ? LV_SYMBOL_WARNING " " : "", text);
    lv_obj_set_state(message, LV_STATE_USER_2, trouble);
    lv_obj_set_state(message, LV_STATE_USER_1, !trouble);
    if (trouble) {
        lv_timer_pause(message_timer);
    } else {
        lv_timer_reset(message_timer);
        lv_timer_resume(message_timer);
    }
}

static void on_link_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!platform_open_url(JOULAR_HOME)) ui_message("Open " JOULAR_HOME " in a web browser.");
}

static void on_link_hovered(lv_event_t *e)
{
    platform_set_hand_cursor(lv_event_get_code(e) == LV_EVENT_HOVER_OVER);
}

static void build_status_bar(lv_obj_t *screen)
{
    lv_obj_t *bar = kit_box(screen, false, 14);
    lv_obj_t *credits;

    /* Text aligned with the plates; the highlight padding sits in the gutter */
    lv_obj_add_style(bar, &styles.status_bar, 0);
    lv_obj_set_size(bar, LV_PCT(100), ui_px(30));
    lv_obj_set_style_pad_left(bar, ui_px(UI_GUTTER - 8), 0);
    lv_obj_set_style_pad_right(bar, ui_px(UI_GUTTER), 0);

    message = kit_lines(kit_label(bar, "Ready. Choose what to measure, then press Start.", TEXT_CAPTION), 1);
    lv_obj_set_style_max_width(message, LV_PCT(65), 0);
    lv_obj_set_style_pad_hor(message, ui_px(8), 0);
    lv_obj_set_style_pad_ver(message, ui_px(3), 0);
    lv_obj_add_style(message, &styles.message_new, LV_STATE_USER_1);
    lv_obj_add_style(message, &styles.message_trouble, LV_STATE_USER_2);
    message_timer = lv_timer_create(on_message_aged, NEW_MESSAGE_MS, NULL);
    lv_timer_pause(message_timer);

    /* Credits shrink for a long message, link always visible */
    credits = kit_lines(kit_label(bar, JOULARENERGYMETER_NAME " " JOULARENERGYMETER_VERSION " © " JOULARENERGYMETER_YEAR " "
                                       JOULARENERGYMETER_AUTHOR ", " JOULARENERGYMETER_LICENCE, TEXT_CAPTION), 1);
    lv_obj_set_flex_grow(credits, 1);
    lv_obj_set_style_text_align(credits, LV_TEXT_ALIGN_RIGHT, 0);
    link = kit_label(bar, "https://github.com/joular/joular-energy-meter", TEXT_LINK);
    lv_obj_set_clickable(link, true);
    lv_obj_add_style(link, &styles.link_hover, LV_STATE_HOVERED);
    lv_obj_add_style(link, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    lv_obj_set_ext_draw_size(link, ui_px(4));
    lv_obj_add_event_cb(link, on_link_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(link, on_link_hovered, LV_EVENT_HOVER_OVER, NULL);
    lv_obj_add_event_cb(link, on_link_hovered, LV_EVENT_HOVER_LEAVE, NULL);
}

void ui_message(const char *text)
{
    show_message(text, false);
}

void ui_trouble(const char *text)
{
    show_message(text, true);
}

/* -------------------------------------------------------------------------------------- */
/* The warning banner                                                                     */
/* -------------------------------------------------------------------------------------- */

static void on_copy_aged(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    kit_button_set(copy_button, LV_SYMBOL_COPY, "Copy command", BUTTON_NORMAL);
}

/* Show "Copied" on the button for a moment */
static void on_copy(lv_event_t *e)
{
    char text[sizeof alarm_command + 32];

    LV_UNUSED(e);
    platform_set_clipboard(alarm_command);
    kit_button_set(copy_button, LV_SYMBOL_OK, "Copied", BUTTON_NORMAL);
    if (copy_timer == NULL) copy_timer = lv_timer_create(on_copy_aged, 2000, NULL);
    lv_timer_reset(copy_timer);
    lv_timer_resume(copy_timer);
    snprintf(text, sizeof text, "Copied to the clipboard: %s", alarm_command);
    ui_message(text);
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
    lv_obj_set_width(words, 0);
    lv_obj_set_flex_grow(words, 1);
    alarm_title = lv_label_create(words);
    lv_obj_set_style_text_font(alarm_title, ui_font(16), 0);
    lv_obj_set_width(alarm_title, LV_PCT(100));
    alarm_text = lv_label_create(words);
    lv_obj_set_style_text_font(alarm_text, ui_font(13), 0);
    lv_obj_set_width(alarm_text, LV_PCT(100));

    copy_button = kit_button(alarm_strip, LV_SYMBOL_COPY, "Copy command", BUTTON_NORMAL, on_copy, NULL);
    kit_show(alarm_strip, false);
}

void ui_alarm(const char *title, const char *text, const char *command)
{
    lv_label_set_text(alarm_title, title);
    /* Command on its own line, readable before copying */
    if (command[0])
        lv_label_set_text_fmt(alarm_text, "%s\n%s", text, command);
    else
        lv_label_set_text(alarm_text, text);
    kit_show(alarm_text, text[0] != '\0' || command[0] != '\0');
    snprintf(alarm_command, sizeof alarm_command, "%s", command);
    kit_show(copy_button, command[0] != '\0');
    kit_show(alarm_strip, true);
}

void ui_alarm_clear(void)
{
    kit_show(alarm_strip, false);
}

/* -------------------------------------------------------------------------------------- */
/* The window                                                                             */
/* -------------------------------------------------------------------------------------- */

void ui_build(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *body;

    lv_obj_add_style(screen, &styles.window, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    /* Status bar must exist before display_create, which writes to it */
    controls_create(screen);
    body = kit_box(screen, true, 10);
    build_status_bar(screen);

    /* Scrolls only when a banner leaves the chart too little room, scrollbar in the gutter */
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_ver(body, ui_px(12), 0);
    lv_obj_set_style_pad_hor(body, ui_px(UI_GUTTER), 0);
    lv_obj_set_scrollable(body, true);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_add_style(body, &styles.scrollbar, LV_PART_SCROLLBAR);
    build_alarm(body);
    display_create(body);

    /* Link last in the focus order */
    lv_group_add_obj(lv_group_get_default(), link);
}
