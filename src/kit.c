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

/* Widgets built on theme.c styles */

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "theme_private.h"

/* Size in logical px, colour, figures font or not */
static const struct {
    int32_t size;
    text_tone tone;
    bool figures;
} roles[] = {
    [TEXT_MUTED] = {13, TONE_MUTED, false},        [TEXT_CAPTION] = {12, TONE_MUTED, false},
    [TEXT_LINK] = {12, TONE_ACCENT, false},        [TEXT_WORD] = {15, TONE_MUTED, false},
    [TEXT_TITLE] = {16, TONE_TEXT, false},
    [TEXT_READING] = {52, TONE_TEXT, true},        [TEXT_READING_UNIT] = {20, TONE_MUTED, false},
    [TEXT_READING_SECOND] = {38, TONE_TEXT, true}, [TEXT_SECOND_UNIT] = {18, TONE_MUTED, false},
    [TEXT_GROUP_FIGURE] = {28, TONE_TEXT, true},   [TEXT_GROUP_UNIT] = {16, TONE_MUTED, false},
    [TEXT_ROW_FIGURE] = {18, TONE_TEXT, true},     [TEXT_FIGURES] = {15, TONE_MUTED, true},
};

/* Default theme styles plain objects as cards, so strip everything */
static lv_obj_t *bare(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);
    return obj;
}

/* -------------------------------------------------------------------------------------- */
/* Text                                                                                   */
/* -------------------------------------------------------------------------------------- */

lv_obj_t *kit_label(lv_obj_t *parent, const char *text, text_role role)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    kit_set_role(label, role);
    return label;
}

/* LVGL redraws on set_text even if the text is the same */
void kit_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

lv_obj_t *kit_lines(lv_obj_t *label, int32_t count)
{
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_max_lines(label, count);
    return label;
}

void kit_set_role(lv_obj_t *label, text_role role)
{
    for (int t = 0; t < TONE_COUNT; t++) lv_obj_remove_style(label, &styles.tone[t], 0);
    lv_obj_add_style(label, &styles.tone[roles[role].tone], 0);
    lv_obj_set_style_text_font(label, roles[role].figures ? ui_figures(roles[role].size)
                                                          : ui_font(roles[role].size), 0);
}

/* -------------------------------------------------------------------------------------- */
/* Containers                                                                             */
/* -------------------------------------------------------------------------------------- */

lv_obj_t *kit_box(lv_obj_t *parent, bool vertical, int32_t gap)
{
    lv_obj_t *box = bare(parent);
    lv_obj_set_width(box, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    /* Rows: centre children vertically, and the track too */
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START,
                          vertical ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          vertical ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(box, ui_px(gap), 0);
    return box;
}

/* overflow_visible only reaches as far as the box's own ext draw size */
void kit_let_out(lv_obj_t *box, int32_t reach)
{
    lv_obj_set_overflow_visible(box, true);
    lv_obj_set_ext_draw_size(box, ui_px(reach));
    lv_obj_refresh_ext_draw_size(box);
}

lv_obj_t *kit_panel(lv_obj_t *parent, const char *title, int32_t gap)
{
    lv_obj_t *panel = bare(parent);
    lv_obj_t *strip, *name, *body;

    lv_obj_add_style(panel, &styles.plate, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    strip = kit_box(panel, false, 10);
    lv_obj_add_style(strip, &styles.strip, 0);
    lv_obj_set_width(strip, LV_PCT(100));
    name = kit_lines(kit_label(strip, title ? title : "", TEXT_TITLE), 1);
    lv_obj_set_flex_grow(name, 1);
    kit_show(strip, title != NULL);

    /* Padding leaves room for focus rings */
    body = kit_box(panel, true, gap);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_pad_hor(body, ui_px(16), 0);
    lv_obj_set_style_pad_top(body, ui_px(title ? 8 : 14), 0);
    lv_obj_set_style_pad_bottom(body, ui_px(14), 0);
    kit_let_out(body, 4);
    return panel;
}

lv_obj_t *kit_panel_strip(lv_obj_t *panel)
{
    return lv_obj_get_child(panel, 0);
}

lv_obj_t *kit_panel_body(lv_obj_t *panel)
{
    return lv_obj_get_child(panel, 1);
}

lv_obj_t *kit_alarm(lv_obj_t *parent)
{
    lv_obj_t *alarm = kit_box(parent, false, 12);
    lv_obj_add_style(alarm, &styles.alarm, 0);
    lv_obj_set_width(alarm, LV_PCT(100));
    lv_obj_set_style_pad_ver(alarm, ui_px(10), 0);
    lv_obj_set_style_pad_hor(alarm, ui_px(14), 0);
    kit_let_out(alarm, 4);
    return alarm;
}

/* -------------------------------------------------------------------------------------- */
/* Marks                                                                                  */
/* -------------------------------------------------------------------------------------- */

lv_obj_t *kit_dot(lv_obj_t *parent, theme_token color)
{
    lv_obj_t *dot = bare(parent);
    lv_obj_set_size(dot, ui_px(8), ui_px(8));
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, theme_color(color), 0);
    return dot;
}

/* -------------------------------------------------------------------------------------- */
/* Lamps                                                                                  */
/* -------------------------------------------------------------------------------------- */

lv_obj_t *kit_lamp(lv_obj_t *parent)
{
    lv_obj_t *lamp = bare(parent);
    lv_obj_add_style(lamp, &styles.lamp, 0);
    lv_obj_set_size(lamp, ui_px(10), ui_px(10));
    kit_lamp_set(lamp, TOKEN_LAMP_OFF, false);
    return lamp;
}

void kit_lamp_set(lv_obj_t *lamp, theme_token color, bool lit)
{
    lv_obj_set_style_bg_color(lamp, theme_color(color), 0);
    lv_obj_set_style_shadow_color(lamp, theme_color(color), 0);
    lv_obj_set_style_shadow_width(lamp, lit ? ui_px(10) : 0, 0);
}

/* -------------------------------------------------------------------------------------- */
/* Controls                                                                               */
/* -------------------------------------------------------------------------------------- */

/* Focus ring is drawn outside the control. LVGL only redraws the ext draw area on state
 * change, so reserve the ring's room permanently. */
static void ring_room(lv_obj_t *obj)
{
    lv_obj_set_ext_draw_size(obj, ui_px(4));
    lv_obj_refresh_ext_draw_size(obj);
}

/* Plain button look, its content in a centred row */
static lv_obj_t *key(lv_obj_t *parent, lv_event_cb_t on_click, void *user_data)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_add_style(button, &styles.motion, 0);
    lv_obj_add_style(button, &styles.key, 0);
    lv_obj_add_style(button, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    ring_room(button);
    /* Same height as fields and pickers; Start and Pause are made taller */
    lv_obj_set_height(button, ui_px(32));
    lv_obj_add_event_cb(button, on_click, LV_EVENT_CLICKED, user_data);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return button;
}

lv_obj_t *kit_button(lv_obj_t *parent, const char *symbol, const char *text, button_kind kind,
                     lv_event_cb_t on_click, void *user_data)
{
    lv_obj_t *button = key(parent, on_click, user_data);

    /* Symbol in its own label, one size smaller to match the cap height */
    lv_obj_set_style_text_font(lv_label_create(button), ui_font(12), 0);
    lv_label_create(button);

    /* Start/Stop is one button, Stop = CHECKED, so the transition fades green to red */
    if (kind == BUTTON_NORMAL) {
        lv_obj_add_style(button, &styles.key_hover, LV_STATE_HOVERED);
        lv_obj_add_style(button, &styles.key_pressed, LV_STATE_PRESSED);
    } else {
        lv_obj_add_style(button, &styles.go, 0);
        lv_obj_add_style(button, &styles.go_hover, LV_STATE_HOVERED);
        lv_obj_add_style(button, &styles.go_pressed, LV_STATE_PRESSED);
        lv_obj_add_style(button, &styles.halt, LV_STATE_CHECKED);
        lv_obj_add_style(button, &styles.halt_hover, LV_STATE_CHECKED | LV_STATE_HOVERED);
        lv_obj_add_style(button, &styles.halt_pressed, LV_STATE_CHECKED | LV_STATE_PRESSED);
    }
    lv_obj_add_style(button, &styles.off, LV_STATE_DISABLED);
    kit_button_set(button, symbol, text, kind);
    return button;
}

void kit_button_set(lv_obj_t *button, const char *symbol, const char *text, button_kind kind)
{
    lv_obj_t *glyph = lv_obj_get_child(button, 0), *words = lv_obj_get_child(button, 1);

    lv_obj_set_state(button, LV_STATE_CHECKED, kind == BUTTON_HALT);
    kit_set_text(glyph, symbol ? symbol : "");
    kit_set_text(words, text ? text : "");
    kit_show(glyph, symbol != NULL);
    kit_show(words, text != NULL);
}

lv_obj_t *kit_choice(lv_obj_t *parent, theme_token color, const char *text, lv_event_cb_t on_click,
                     void *user_data)
{
    lv_obj_t *choice = key(parent, on_click, user_data), *dot, *name;

    lv_obj_add_style(choice, &styles.key_hover, LV_STATE_HOVERED);
    lv_obj_add_style(choice, &styles.key_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(choice, &styles.chosen, LV_STATE_CHECKED);
    lv_obj_add_style(choice, &styles.chosen_hover, LV_STATE_CHECKED | LV_STATE_HOVERED);
    lv_obj_add_style(choice, &styles.chosen_pressed, LV_STATE_CHECKED | LV_STATE_PRESSED);

    /* A plate ring keeps the dot clear of the accent fill; on a plate it doesn't show */
    dot = kit_dot(choice, color);
    lv_obj_set_style_outline_color(dot, theme_color(TOKEN_PLATE), 0);
    lv_obj_set_style_outline_width(dot, ui_px(2), 0);
    /* A long program name is cut */
    name = kit_lines(lv_label_create(choice), 1);
    lv_obj_set_style_max_width(name, ui_px(160), 0);
    kit_choice_set(choice, text);
    return choice;
}

void kit_choice_set(lv_obj_t *choice, const char *text)
{
    kit_set_text(lv_obj_get_child(choice, 1), text);
}

lv_obj_t *kit_entry(lv_obj_t *parent, const char *placeholder, bool digits_only)
{
    lv_obj_t *entry = lv_textarea_create(parent);
    lv_obj_remove_style_all(entry);
    lv_obj_add_style(entry, &styles.motion, 0);
    lv_obj_add_style(entry, &styles.entry, 0);
    lv_obj_add_style(entry, &styles.entry_focused, LV_STATE_FOCUSED);
    lv_obj_add_style(entry, &styles.entry_invalid, LV_STATE_USER_1);
    lv_obj_add_style(entry, &styles.disabled, LV_STATE_DISABLED);
    lv_obj_add_style(entry, &styles.placeholder, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_add_style(entry, &styles.cursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_textarea_set_one_line(entry, true);
    lv_textarea_set_placeholder_text(entry, placeholder);
    if (digits_only) lv_textarea_set_accepted_chars(entry, "0123456789");

    /* Centre the text vertically */
    int32_t line = lv_font_get_line_height(lv_obj_get_style_text_font(entry, LV_PART_MAIN));
    lv_obj_set_style_pad_ver(entry, (ui_px(32) - line) / 2, 0);
    lv_obj_set_size(entry, LV_PCT(100), ui_px(32));
    return entry;
}

void kit_entry_set_invalid(lv_obj_t *entry, bool invalid)
{
    lv_obj_set_state(entry, LV_STATE_USER_1, invalid);
}

lv_obj_t *kit_switch(lv_obj_t *parent, lv_event_cb_t on_change, void *user_data)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_remove_style_all(sw);
    lv_obj_add_style(sw, &styles.motion, 0);
    lv_obj_add_style(sw, &styles.motion, LV_PART_INDICATOR);
    lv_obj_add_style(sw, &styles.switch_track, 0);
    lv_obj_add_style(sw, &styles.disabled, LV_STATE_DISABLED);
    lv_obj_add_style(sw, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    ring_room(sw);
    lv_obj_add_style(sw, &styles.switch_on, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_style(sw, &styles.knob, LV_PART_KNOB);
    lv_obj_set_size(sw, ui_px(40), ui_px(22));
    lv_obj_add_event_cb(sw, on_change, LV_EVENT_VALUE_CHANGED, user_data);
    return sw;
}

/* Arrow up while the list is open */
static void on_picker_toggled(lv_event_t *e)
{
    lv_obj_t *picker = lv_event_get_current_target(e);

    lv_dropdown_set_symbol(picker, lv_event_get_code(e) == LV_EVENT_READY ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    lv_obj_invalidate(picker);
}

lv_obj_t *kit_picker(lv_obj_t *parent, const char *options, uint32_t chosen, lv_event_cb_t on_change,
                     void *user_data)
{
    lv_obj_t *picker = lv_dropdown_create(parent);
    lv_obj_remove_style_all(picker);
    lv_obj_add_style(picker, &styles.motion, 0);
    lv_obj_add_style(picker, &styles.picker, 0);
    lv_obj_add_style(picker, &styles.picker_hover, LV_STATE_HOVERED);
    lv_obj_add_style(picker, &styles.picker_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(picker, &styles.disabled, LV_STATE_DISABLED);
    lv_obj_add_style(picker, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    ring_room(picker);
    /* Smaller, muted arrow */
    lv_obj_set_style_text_font(picker, ui_font(11), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(picker, theme_color(TOKEN_MUTED), LV_PART_INDICATOR);
    lv_dropdown_set_options(picker, options);

    /* Dropdown width only accounts for the arrow, add the widest option */
    lv_point_t widest, arrow;
    lv_text_get_size(&widest, options, lv_obj_get_style_text_font(picker, LV_PART_MAIN), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&arrow, LV_SYMBOL_DOWN, lv_obj_get_style_text_font(picker, LV_PART_INDICATOR), 0, 0,
                     LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_size(picker, widest.x + arrow.x + ui_px(12) + lv_obj_get_style_pad_left(picker, LV_PART_MAIN)
                            + lv_obj_get_style_pad_right(picker, LV_PART_MAIN), ui_px(32));

    /* List is a separate object. Hovered option is SELECTED|PRESSED, current one
     * SELECTED|CHECKED. */
    lv_obj_t *list = lv_dropdown_get_list(picker);
    lv_obj_remove_style_all(list);
    lv_obj_add_style(list, &styles.menu, 0);
    lv_obj_add_style(list, &styles.menu_hover, LV_PART_SELECTED | LV_STATE_PRESSED);
    lv_obj_add_style(list, &styles.menu_checked, LV_PART_SELECTED | LV_STATE_CHECKED);

    lv_dropdown_set_selected(picker, chosen);
    lv_obj_add_event_cb(picker, on_change, LV_EVENT_VALUE_CHANGED, user_data);
    lv_obj_add_event_cb(picker, on_picker_toggled, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(picker, on_picker_toggled, LV_EVENT_CANCEL, NULL);
    return picker;
}

lv_obj_t *kit_interval_picker(lv_obj_t *parent, lv_event_cb_t on_change, void *user_data)
{
    char options[128];
    int used = 0;

    for (int c = 0; c < INTERVAL_COUNT && used < (int)sizeof options; c++)
        used += snprintf(options + used, sizeof options - (size_t)used, "%s%s",
                         c ? "\n" : "", interval_label((interval_choice)c));
    return kit_picker(parent, options, (uint32_t)chosen_interval(), on_change, user_data);
}

interval_choice kit_interval_selected(lv_obj_t *picker)
{
    uint32_t picked = lv_dropdown_get_selected(picker);
    return picked < INTERVAL_COUNT ? (interval_choice)picked : chosen_interval();
}

void kit_show(lv_obj_t *obj, bool visible)
{
    lv_obj_set_hidden(obj, !visible);
}

void kit_enable(lv_obj_t *obj, bool enabled)
{
    if (lv_obj_has_state(obj, LV_STATE_DISABLED) != !enabled) lv_obj_set_state(obj, LV_STATE_DISABLED, !enabled);
}

void kit_save_csv(const char *title, const char *start, const char *name, file_browser_cb on_choose)
{
    file_browser_options options = {
        .title = title,
        .start = start,
        .extension = ".csv",
        .save_as = name,
        .appends = true,
        .on_choose = on_choose,
        .theme = theme_browser(),
    };

    file_browser_open(&options);
}
