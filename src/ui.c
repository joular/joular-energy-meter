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

/* Main screen layout and status bar */

#include <stdio.h>
#include <time.h>

#include "app.h"
#include "theme_private.h"

/* Readings column width, logical px */
#define RIGHT_WIDTH 344

/* Highlight time of a new message, ms */
#define NEW_MESSAGE_MS 5000

static lv_obj_t *message, *link;
static lv_timer_t *message_timer;

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
/* The window                                                                             */
/* -------------------------------------------------------------------------------------- */

void ui_build(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *body, *left, *right;

    lv_obj_add_style(screen, &styles.window, 0);
    lv_obj_set_scrollable(screen, false);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);

    /* Status bar must exist before display_create, which writes to it */
    controls_create(screen);
    body = kit_box(screen, false, UI_GUTTER);
    build_status_bar(screen);

    /* Left column takes what the readings column leaves */
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_pad_all(body, ui_px(UI_GUTTER), 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    kit_let_out(body, 4);

    left = kit_box(body, true, UI_GUTTER);
    lv_obj_set_size(left, 0, LV_PCT(100));
    lv_obj_set_flex_grow(left, 1);
    kit_let_out(left, 4);
    /* Readings scroll in a short window, scrollbar in the gutter */
    lv_obj_set_style_pad_right(body, ui_px(UI_GUTTER - 8), 0);
    right = kit_box(body, true, UI_GUTTER);
    lv_obj_set_size(right, ui_px(RIGHT_WIDTH + 8), LV_PCT(100));
    lv_obj_set_style_pad_right(right, ui_px(8), 0);
    lv_obj_set_scrollable(right, true);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);
    lv_obj_add_style(right, &styles.scrollbar, LV_PART_SCROLLBAR);
    display_create(left, right);

    /* Link last in the focus order */
    lv_group_add_obj(lv_group_get_default(), link);
}
