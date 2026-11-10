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

/* The window: header with the tabs, warning banner, the Setup and Readings pages, and the bar */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "theme_private.h"

/* Highlight time of a new message, ms */
#define NEW_MESSAGE_MS 5000

LV_IMAGE_DECLARE(joular_logo_1x);
LV_IMAGE_DECLARE(joular_logo_2x);

static lv_obj_t *message, *link, *look_key;
static lv_obj_t *tabs[PAGE_COUNT], *pages[PAGE_COUNT];
static ui_page page_shown = PAGE_SETUP;

static void on_look(lv_event_t *e);
static lv_timer_t *message_timer;

static lv_obj_t *alarm_box, *alarm_strip, *alarm_title, *alarm_text, *act_button, *copy_button;

/* What the banner and the bar show, to show again when the screen is built anew */
typedef struct {
    bool shown, action_shown;
    char title[256], text[1200], command[1024], action[64];
    void (*click)(void);
} banner_state;
static banner_state banner;
static struct { char text[1300]; bool trouble; } said;

/* Zooms, per cent */
static const int zooms[] = { 80, 90, 100, 110, 120, 130, 140, 150 };
#define ZOOM_COUNT ((int)(sizeof zooms / sizeof *zooms))

/* -------------------------------------------------------------------------------------- */
/* The header and the bar                                                                 */
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
    snprintf(said.text, sizeof said.text, "%s  %s%s", stamp, trouble ? LV_SYMBOL_WARNING " " : "", text);
    said.trouble = trouble;
    lv_label_set_text(message, said.text);
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

/* The keyboard leaves what was just hidden for to: Enter on a hidden key would still press it */
static void leave_hidden(lv_obj_t *to)
{
    lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());

    for (lv_obj_t *o = focused; o != NULL; o = lv_obj_get_parent(o))
        if (lv_obj_is_hidden(o)) {
            lv_group_focus_obj(to);
            return;
        }
}

void ui_show_page(ui_page page)
{
    page_shown = page;
    for (int i = 0; i < PAGE_COUNT; i++) {
        kit_show(pages[i], i == (int)page);
        lv_obj_set_state(tabs[i], LV_STATE_CHECKED, i == (int)page);
    }
    leave_hidden(tabs[page]);
}

static void on_tab(lv_event_t *e)
{
    ui_show_page((ui_page)(uintptr_t)lv_event_get_user_data(e));
}

/* The chosen one a raised plate on the track */
static void make_tab(lv_obj_t *track, const char *symbol, const char *name, ui_page page)
{
    lv_obj_t *tab = lv_button_create(track);

    lv_obj_remove_style_all(tab);
    lv_obj_add_style(tab, &styles.motion, 0);
    lv_obj_add_style(tab, &styles.tab, 0);
    lv_obj_add_style(tab, &styles.tab_chosen, LV_STATE_CHECKED);
    lv_obj_add_style(tab, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    lv_obj_set_size(tab, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_text_font(kit_label(tab, symbol, TEXT_CAPTION), ui_font(13), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(tab, 0), theme_color(TOKEN_MUTED), 0);
    lv_label_set_text(lv_label_create(tab), name);
    lv_obj_add_event_cb(tab, on_tab, LV_EVENT_CLICKED, (void *)(uintptr_t)page);
    tabs[page] = tab;
}

/* Logo and name, the tabs, then the credits, the link and the look's key at the right */
static void build_header(lv_obj_t *screen)
{
    lv_obj_t *header = kit_box(screen, false, 14), *logo, *rule, *track, *credits;

    lv_obj_set_size(header, LV_PCT(100), ui_px(56));
    lv_obj_set_style_pad_left(header, ui_px(UI_GUTTER + 4), 0);
    lv_obj_set_style_pad_right(header, ui_px(UI_GUTTER), 0);
    kit_let_out(header, 4);

    logo = lv_image_create(header);
    lv_image_set_src(logo, theme_logo(platform_scale() * platform_zoom() > 1.25f ? &joular_logo_2x : &joular_logo_1x));
    /* Stretch the closest bitmap to the exact size */
    lv_obj_set_size(logo, ui_px(joular_logo_1x.header.w), ui_px(joular_logo_1x.header.h));
    lv_image_set_inner_align(logo, LV_IMAGE_ALIGN_STRETCH);
    rule = kit_box(header, false, 0);
    lv_obj_set_size(rule, ui_px(1), ui_px(24));
    lv_obj_set_style_bg_color(rule, theme_color(TOKEN_ENAMEL_DEEP), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(kit_label(header, "Energy Meter", TEXT_WORD), ui_font(16), 0);

    track = kit_box(header, false, 2);
    lv_obj_add_style(track, &styles.track, 0);
    lv_obj_set_height(track, ui_px(38));
    lv_obj_set_style_margin_left(track, ui_px(6), 0);
    kit_let_out(track, 4);
    make_tab(track, LV_SYMBOL_SETTINGS, "Setup", PAGE_SETUP);
    make_tab(track, LV_SYMBOL_CHARGE, "Readings", PAGE_READINGS);

    credits = kit_lines(kit_label(header, JOULARENERGYMETER_VERSION " © " JOULARENERGYMETER_YEAR " "
                                          JOULARENERGYMETER_AUTHOR ", " JOULARENERGYMETER_LICENCE, TEXT_CAPTION), 1);
    lv_obj_set_width(credits, 0);
    lv_obj_set_flex_grow(credits, 1);
    lv_obj_set_style_text_align(credits, LV_TEXT_ALIGN_RIGHT, 0);
    link = kit_label(header, "GitHub", TEXT_LINK);
    lv_obj_set_clickable(link, true);
    lv_obj_add_style(link, &styles.link_hover, LV_STATE_HOVERED);
    lv_obj_add_style(link, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    lv_obj_set_ext_draw_size(link, ui_px(4));
    lv_obj_add_event_cb(link, on_link_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(link, on_link_hovered, LV_EVENT_HOVER_OVER, NULL);
    lv_obj_add_event_cb(link, on_link_hovered, LV_EVENT_HOVER_LEAVE, NULL);
    kit_tip(link, JOULAR_HOME);

    /* The look's sign, flat, last */
    look_key = kit_button(header, theme_is_dark() ? SYMBOL_MOON : SYMBOL_SUN, NULL, BUTTON_NORMAL, on_look, NULL);
    lv_obj_set_size(look_key, ui_px(32), ui_px(30));
    lv_obj_set_style_pad_hor(look_key, 0, 0);
    lv_obj_set_style_bg_opa(look_key, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(look_key, 0, 0);
    lv_obj_set_style_shadow_width(look_key, 0, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(look_key, 0), ui_font(17), 0);
    lv_obj_set_style_text_color(look_key, theme_color(TOKEN_MUTED), 0);
    kit_tip(look_key, "Light or dark, or like the system");
}

/* A frosted plate floating at the bottom: the session's state and messages, Pause and Start */
static void build_bar(lv_obj_t *screen)
{
    lv_obj_t *dock = kit_box(screen, false, 0), *bar = kit_box(dock, false, 12), *line;

    lv_obj_set_width(dock, LV_PCT(100));
    lv_obj_set_style_pad_hor(dock, ui_px(UI_GUTTER), 0);
    lv_obj_set_style_pad_top(dock, ui_px(6), 0);
    lv_obj_set_style_pad_bottom(dock, ui_px(12), 0);
    kit_let_out(dock, 40);
    lv_obj_add_style(bar, &styles.bar, 0);
    lv_obj_set_size(bar, LV_PCT(100), ui_px(64));
    lv_obj_set_style_pad_left(bar, ui_px(12), 0);
    lv_obj_set_style_pad_right(bar, ui_px(10), 0);
    kit_let_out(bar, 4);

    line = controls_bar(bar);
    message = kit_lines(kit_label(line, "Choose what to measure, then press Start.", TEXT_CAPTION), 1);
    lv_obj_set_style_max_width(message, LV_PCT(75), 0);
    lv_obj_set_style_pad_hor(message, ui_px(6), 0);
    lv_obj_set_style_pad_ver(message, ui_px(1), 0);
    lv_obj_add_style(message, &styles.message_new, LV_STATE_USER_1);
    lv_obj_add_style(message, &styles.message_trouble, LV_STATE_USER_2);
    /* Once: the bar is built again at each zoom */
    if (message_timer == NULL) message_timer = lv_timer_create(on_message_aged, NEW_MESSAGE_MS, NULL);
    lv_timer_pause(message_timer);
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

static void on_copy(lv_event_t *e)
{
    char text[sizeof banner.command + 32];

    LV_UNUSED(e);
    kit_copy(copy_button, banner.command);
    snprintf(text, sizeof text, "Copied to the clipboard: %s", banner.command);
    ui_message(text);
}

static void on_act(lv_event_t *e)
{
    LV_UNUSED(e);
    if (banner.click != NULL) banner.click();
}

/* Under the header, over both pages */
static void build_alarm(lv_obj_t *column)
{
    lv_obj_t *icon, *words;

    alarm_box = kit_box(column, true, 0);
    lv_obj_set_width(alarm_box, LV_PCT(100));
    lv_obj_set_style_pad_hor(alarm_box, ui_px(UI_GUTTER), 0);
    lv_obj_set_style_pad_bottom(alarm_box, ui_px(8), 0);
    kit_let_out(alarm_box, 4);
    alarm_strip = kit_alarm(alarm_box);
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

    act_button = kit_button(alarm_strip, NULL, "", BUTTON_GO, on_act, NULL);
    kit_show(act_button, false);
    copy_button = kit_button(alarm_strip, LV_SYMBOL_COPY, "Copy command", BUTTON_NORMAL, on_copy, NULL);
    kit_show(alarm_box, false);
}

void ui_alarm_action(const char *label, void (*on_click)(void))
{
    banner.action_shown = label != NULL;
    snprintf(banner.action, sizeof banner.action, "%s", label ? label : "");
    banner.click = on_click;
    if (label != NULL) kit_button_set(act_button, NULL, label, BUTTON_GO);
    kit_enable(act_button, on_click != NULL);
    kit_show(act_button, label != NULL);
}

void ui_alarm(const char *title, const char *text, const char *command)
{
    banner.shown = true;
    snprintf(banner.title, sizeof banner.title, "%s", title);
    snprintf(banner.text, sizeof banner.text, "%s", text);
    snprintf(banner.command, sizeof banner.command, "%s", command);
    lv_label_set_text(alarm_title, title);
    /* Command on its own line, readable before copying */
    if (command[0])
        lv_label_set_text_fmt(alarm_text, "%s\n%s", text, command);
    else
        lv_label_set_text(alarm_text, text);
    kit_show(alarm_text, text[0] != '\0' || command[0] != '\0');
    kit_show(copy_button, command[0] != '\0');
    ui_alarm_action(NULL, NULL);
    kit_show(alarm_box, true);
}

void ui_alarm_clear(void)
{
    banner.shown = false;
    kit_show(alarm_box, false);
}

void ui_zoom_init(void)
{
    const char *kept = settings_get("zoom");

    /* Not if the screen is smaller than when it was chosen */
    for (int i = 0; kept != NULL && i < ZOOM_COUNT; i++)
        if (zooms[i] == atoi(kept) && platform_zoom_fits((float)zooms[i] / 100.0f))
            platform_set_zoom((float)zooms[i] / 100.0f);
}

/* The choices go through the settings and the session's figures live outside the widgets, so
 * the screen is simply built again in the new size or look, as at launch */
static void rebuild(float zoom)
{
    /* Building clears our field errors from the banner */
    banner_state shown = banner;

    file_browser_close();
    idle_dialog_close();
    controls_keep();
    display_keep();
    /* Emptied first, so the new styles don't restyle what goes */
    lv_obj_clean(lv_screen_active());
    lv_obj_remove_style_all(lv_screen_active());
    platform_set_zoom(zoom);
    theme_init(lv_display_get_default());
    ui_build();
    /* What lives outside the screen, a hover help, takes the new styles too */
    lv_obj_refresh_style(lv_layer_top(), LV_PART_ANY, LV_STYLE_PROP_ANY);

    if (shown.shown) {
        ui_alarm(shown.title, shown.text, shown.command);
        if (shown.action_shown) ui_alarm_action(shown.action, shown.click);
    } else {
        ui_alarm_clear();
    }
    if (said.text[0]) {
        lv_label_set_text(message, said.text);
        lv_obj_set_state(message, LV_STATE_USER_2, said.trouble);
    }
}

void ui_zoom(int step)
{
    int now = (int)lroundf(platform_zoom() * 100.0f), next = 100, i;
    char kept[8];

    for (i = 0; i < ZOOM_COUNT && zooms[i] != now; i++)
        ;
    if (step != 0 && i < ZOOM_COUNT) next = zooms[LV_CLAMP(0, i + step, ZOOM_COUNT - 1)];
    if (next == now) return;
    /* The layout would be cut */
    if (next > now && !platform_zoom_fits((float)next / 100.0f)) {
        ui_message("The screen is too small for a larger zoom.");
        return;
    }
    rebuild((float)next / 100.0f);
    snprintf(kept, sizeof kept, "%d", next);
    settings_set("zoom", kept);
}

/* Rows of the look menu, as kept */
static const char *const looks[] = { "system", "light", "dark" };

/* After the menu's click is over: its key goes with the screen */
static void on_look_rebuild(void *unused)
{
    (void)unused;
    rebuild(platform_zoom());
}

static void on_look_picked(int row)
{
    if (settings_index("look", looks, 3, 0) == row) return;
    settings_set("look", looks[row]);
    lv_async_call(on_look_rebuild, NULL);
}

static void on_look(lv_event_t *e)
{
    lv_obj_t *menu = kit_menu(lv_event_get_current_target(e), on_look_picked);
    static const char *const names[] = { "Like the system", "Light", "Dark" };
    int kept = settings_index("look", looks, 3, 0);

    for (int i = 0; i < 3; i++) kit_menu_add(menu, names[i], i == kept);
    kit_menu_open(menu);
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

    build_header(screen);
    body = kit_box(screen, true, 0);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    build_alarm(body);

    /* Each page scrolls when the window is too short for it, scrollbar in the gutter */
    for (int i = 0; i < PAGE_COUNT; i++) {
        pages[i] = kit_box(body, true, 8);
        lv_obj_set_width(pages[i], LV_PCT(100));
        lv_obj_set_flex_grow(pages[i], 1);
        lv_obj_set_style_pad_hor(pages[i], ui_px(UI_GUTTER), 0);
        lv_obj_set_style_pad_top(pages[i], ui_px(2), 0);
        lv_obj_set_style_pad_bottom(pages[i], ui_px(4), 0);
        lv_obj_set_scrollable(pages[i], true);
        lv_obj_set_scroll_dir(pages[i], LV_DIR_VER);
        lv_obj_add_style(pages[i], &styles.scrollbar, LV_PART_SCROLLBAR);
        kit_let_out(pages[i], 4);
    }
    /* The settings in a column of cards in the middle */
    lv_obj_set_flex_align(pages[PAGE_SETUP], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* The bar before the readings, which write to it */
    controls_create(pages[PAGE_SETUP]);
    display_settings(pages[PAGE_SETUP]);
    build_bar(screen);
    display_create(pages[PAGE_READINGS]);
    ui_show_page(page_shown);

    /* Link and look's key last in the focus order, after Start */
    lv_group_add_obj(lv_group_get_default(), link);
    lv_group_remove_obj(look_key);
    lv_group_add_obj(lv_group_get_default(), look_key);
}
