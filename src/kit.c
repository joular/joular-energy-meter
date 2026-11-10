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
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "theme_private.h"

/* Size in logical px, colour, font: m(edium), b(old), f(igures) */
static const struct {
    int32_t size;
    text_tone tone;
    char font;
} roles[] = {
    [TEXT_MUTED] = {13, TONE_MUTED, 'm'},        [TEXT_CAPTION] = {12, TONE_MUTED, 'm'},
    [TEXT_LINK] = {12, TONE_ACCENT, 'm'},        [TEXT_WORD] = {15, TONE_MUTED, 'm'},
    [TEXT_TITLE] = {16, TONE_TEXT, 'b'},
    [TEXT_READING] = {52, TONE_TEXT, 'f'},        [TEXT_READING_UNIT] = {20, TONE_MUTED, 'm'},
    [TEXT_READING_SECOND] = {38, TONE_TEXT, 'f'}, [TEXT_SECOND_UNIT] = {18, TONE_MUTED, 'm'},
    [TEXT_GROUP_FIGURE] = {28, TONE_TEXT, 'f'},   [TEXT_GROUP_UNIT] = {16, TONE_MUTED, 'm'},
    [TEXT_ROW_FIGURE] = {18, TONE_TEXT, 'f'},     [TEXT_FIGURES] = {15, TONE_MUTED, 'f'},
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
    lv_obj_set_style_text_font(label, roles[role].font == 'f'   ? ui_figures(roles[role].size)
                                      : roles[role].font == 'b' ? ui_bold(roles[role].size)
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

lv_obj_t *kit_panel(lv_obj_t *parent, const char *symbol, theme_token tile, const char *title, int32_t gap)
{
    lv_obj_t *panel = bare(parent);
    lv_obj_t *head, *body;

    lv_obj_add_style(panel, &styles.card, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    head = kit_box(panel, false, 10);
    lv_obj_add_style(head, &styles.head, 0);
    lv_obj_set_size(head, LV_PCT(100), ui_px(40));
    kit_let_out(head, 4);
    kit_tile(head, symbol, tile, 26);
    if (title) kit_lines(kit_label(head, title, TEXT_TITLE), 1);

    /* Padding leaves room for focus rings */
    body = kit_box(panel, true, gap);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_pad_hor(body, ui_px(16), 0);
    lv_obj_set_style_pad_top(body, ui_px(2), 0);
    lv_obj_set_style_pad_bottom(body, ui_px(12), 0);
    kit_let_out(body, 4);
    return panel;
}

static void on_chart_icon(lv_event_t *e)
{
    lv_obj_t *icon = lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a, bar;
    lv_draw_rect_dsc_t d;
    int32_t w, h, thick;

    lv_obj_get_coords(icon, &a);
    w = lv_area_get_width(&a);
    h = lv_area_get_height(&a);
    thick = LV_MAX(2, w / 6);
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_obj_get_style_text_color(icon, 0);
    d.radius = thick / 3;
    /* Three bars on a base line */
    for (int i = 0; i < 3; i++) {
        static const int tall[] = { 50, 90, 68 };
        bar.x1 = a.x1 + w / 6 + i * (w / 4 + thick / 2) - thick / 2;
        bar.x2 = bar.x1 + thick - 1;
        bar.y2 = a.y2 - thick;
        bar.y1 = bar.y2 - (h - thick) * tall[i] / 100;
        lv_draw_rect(layer, &d, &bar);
    }
    bar = (lv_area_t){ a.x1, a.y2 - LV_MAX(1, thick / 2) + 1, a.x2, a.y2 };
    lv_draw_rect(layer, &d, &bar);
}

/* Sign about half the tile, white, dark on amber; NULL draws three bars */
lv_obj_t *kit_tile(lv_obj_t *parent, const char *symbol, theme_token color, int32_t size)
{
    lv_obj_t *tile = kit_box(parent, false, 0), *sign;
    theme_token ink = color == TOKEN_ENERGY ? TOKEN_ON_ENERGY : TOKEN_ON_COLOUR;

    lv_obj_add_style(tile, &styles.tile, 0);
    lv_obj_set_style_bg_color(tile, theme_color(color), 0);
    lv_obj_set_style_text_color(tile, theme_color(ink), 0);
    lv_obj_set_style_radius(tile, ui_px(size / 4 + 1), 0);
    lv_obj_set_size(tile, ui_px(size), ui_px(size));
    lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (symbol == NULL) {
        sign = bare(tile);
        lv_obj_set_size(sign, ui_px(size / 2), ui_px(size / 2));
        lv_obj_set_style_text_color(sign, theme_color(ink), 0);
        lv_obj_add_event_cb(sign, on_chart_icon, LV_EVENT_DRAW_MAIN_END, NULL);
        return tile;
    }
    sign = lv_label_create(tile);
    lv_label_set_text(sign, symbol);
    lv_obj_set_style_text_font(sign, ui_font(size * 9 / 20 + 1), 0);
    return tile;
}

/* Children: left (name, explanation), controls. The card's body gives its padding to the rows,
 * which have a rule on top. */
lv_obj_t *kit_row(lv_obj_t *panel, const char *name, const char *explanation)
{
    lv_obj_t *body = kit_panel_body(panel), *row = kit_box(body, false, 12), *left, *note, *controls;

    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_bottom(body, ui_px(4), 0);
    lv_obj_add_style(row, &styles.rule_top, 0);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_style_min_height(row, ui_px(48), 0);
    lv_obj_set_style_pad_ver(row, ui_px(6), 0);
    lv_obj_set_style_pad_left(row, ui_px(16), 0);
    lv_obj_set_style_pad_right(row, ui_px(14), 0);
    kit_let_out(row, 4);

    left = kit_box(row, true, 1);
    lv_obj_set_width(left, ui_px(210));
    lv_obj_set_style_text_font(kit_lines(kit_label(left, name, TEXT_TITLE), 1), ui_bold(14), 0);
    note = kit_lines(kit_label(left, explanation, TEXT_CAPTION), 2);
    lv_obj_set_width(note, LV_PCT(100));

    controls = kit_box(row, false, 8);
    lv_obj_set_width(controls, 0);
    lv_obj_set_flex_grow(controls, 1);
    kit_let_out(controls, 4);
    return row;
}

lv_obj_t *kit_row_controls(lv_obj_t *row)
{
    return lv_obj_get_child(row, 1);
}

lv_obj_t *kit_row_name(lv_obj_t *row)
{
    return lv_obj_get_child(lv_obj_get_child(row, 0), 0);
}

lv_obj_t *kit_row_note(lv_obj_t *row)
{
    return lv_obj_get_child(lv_obj_get_child(row, 0), 1);
}

/* A small tag, teal on a light tint, such as "Optional" */
lv_obj_t *kit_chip(lv_obj_t *parent, const char *text)
{
    lv_obj_t *chip = kit_label(parent, text, TEXT_CAPTION);

    lv_obj_set_style_text_font(chip, ui_bold(12), 0);
    lv_obj_set_style_text_color(chip, theme_color(TOKEN_ACCENT), 0);
    lv_obj_set_style_bg_color(chip, theme_color(TOKEN_ACCENT_SOFT), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, ui_px(6), 0);
    lv_obj_set_style_pad_hor(chip, ui_px(7), 0);
    lv_obj_set_style_pad_ver(chip, ui_px(2), 0);
    return chip;
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

lv_obj_t *kit_choices(lv_obj_t *parent)
{
    lv_obj_t *track = kit_box(parent, false, 2);

    lv_obj_add_style(track, &styles.group, 0);
    kit_let_out(track, 4);
    return track;
}

lv_obj_t *kit_choice(lv_obj_t *parent, theme_token color, const char *text, lv_event_cb_t on_click,
                     void *user_data)
{
    lv_obj_t *choice = lv_button_create(parent), *dot, *name;

    lv_obj_remove_style_all(choice);
    lv_obj_add_style(choice, &styles.motion, 0);
    lv_obj_add_style(choice, &styles.choice, 0);
    lv_obj_add_style(choice, &styles.choice_hover, LV_STATE_HOVERED);
    lv_obj_add_style(choice, &styles.chosen, LV_STATE_CHECKED);
    lv_obj_add_style(choice, &styles.chosen_hover, LV_STATE_CHECKED | LV_STATE_HOVERED);
    lv_obj_add_style(choice, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    ring_room(choice);
    lv_obj_set_height(choice, ui_px(30));
    lv_obj_add_event_cb(choice, on_click, LV_EVENT_CLICKED, user_data);
    lv_obj_set_flex_flow(choice, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(choice, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* A plate ring keeps the dot clear of the accent fill; on a plate it doesn't show */
    if (color != TOKEN_COUNT) {
        dot = kit_dot(choice, color);
        lv_obj_set_style_outline_color(dot, theme_color(TOKEN_PLATE), 0);
        lv_obj_set_style_outline_width(dot, ui_px(2), 0);
    }
    /* A long program name is cut */
    name = kit_lines(lv_label_create(choice), 1);
    lv_obj_set_style_max_width(name, ui_px(160), 0);
    kit_choice_set(choice, text);
    return choice;
}

/* As wide as in the chosen style's bold: LVGL doesn't measure a label again when its parent's
 * state changes its font, and the others don't move */
void kit_choice_set(lv_obj_t *choice, const char *text)
{
    lv_obj_t *name = lv_obj_get_child(choice, -1);
    lv_point_t size;

    kit_set_text(name, text);
    lv_text_get_size(&size, text, ui_bold(14), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_style_min_width(name, LV_MIN(size.x, ui_px(160)), 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
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

/* Clicking the label toggles the switch, like a desktop checkbox label */
static void on_switch_label(lv_event_t *e)
{
    lv_obj_t *control = lv_event_get_user_data(e);

    if (lv_obj_has_state(control, LV_STATE_DISABLED)) return;
    lv_obj_set_state(control, LV_STATE_CHECKED, !lv_obj_has_state(control, LV_STATE_CHECKED));
    lv_obj_send_event(control, LV_EVENT_VALUE_CHANGED, NULL);
}

void kit_switch_label(lv_obj_t *label, lv_obj_t *control)
{
    lv_obj_set_clickable(label, true);
    lv_obj_add_event_cb(label, on_switch_label, LV_EVENT_CLICKED, control);
}

/* One key at a time says "Copied" for two seconds, then what it said */
static struct {
    lv_obj_t *key;
    char symbol[8], text[64];
    lv_timer_t *timer;
} copied;

static void copied_back(void)
{
    if (copied.key != NULL && lv_obj_is_valid(copied.key))
        kit_button_set(copied.key, copied.symbol[0] ? copied.symbol : NULL, copied.text, BUTTON_NORMAL);
    copied.key = NULL;
}

static void on_copied_aged(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    copied_back();
}

void kit_copy(lv_obj_t *key, const char *text)
{
    platform_set_clipboard(text);
    if (copied.key != key) {
        copied_back();
        copied.key = key;
        snprintf(copied.symbol, sizeof copied.symbol, "%s", lv_label_get_text(lv_obj_get_child(key, 0)));
        snprintf(copied.text, sizeof copied.text, "%s", lv_label_get_text(lv_obj_get_child(key, 1)));
    }
    kit_button_set(key, LV_SYMBOL_OK, "Copied", BUTTON_NORMAL);
    if (copied.timer == NULL) copied.timer = lv_timer_create(on_copied_aged, 2000, NULL);
    lv_timer_reset(copied.timer);
    lv_timer_resume(copied.timer);
}

/* A card of the Setup page, as wide as the others */
lv_obj_t *kit_setup_card(lv_obj_t *page, const char *symbol, theme_token tile, const char *title)
{
    lv_obj_t *card = kit_panel(page, symbol, tile, title, 0);

    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_style_max_width(card, ui_px(SETUP_WIDTH), 0);
    return card;
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

void kit_open_csv(const char *title, const char *start, file_browser_cb on_choose)
{
    kit_save_csv(title, start, NULL, on_choose);
}

/* name NULL: a file to open */
void kit_save_csv(const char *title, const char *start, const char *name, file_browser_cb on_choose)
{
    file_browser_options options = {
        .title = title,
        .start = start,
        .extension = ".csv",
        .save_as = name,
        .appends = name != NULL,
        .on_choose = on_choose,
        .theme = theme_browser(),
    };

    file_browser_open(&options);
}

/* -------------------------------------------------------------------------------------- */
/* Hover help                                                                             */
/* -------------------------------------------------------------------------------------- */

#define TIP_DELAY_MS 500
#define TIP_MAX 48

static struct {
    lv_obj_t *obj;
    char text[320];
} tips[TIP_MAX];
static int tip_count;
static lv_obj_t *tip_plate, *tip_over;   /* the help shown, the object under the pointer */
static lv_timer_t *tip_timer;

static int tip_index(lv_obj_t *obj)
{
    for (int i = 0; i < tip_count; i++)
        if (tips[i].obj == obj) return i;
    return -1;
}

static void hide_tip(void)
{
    if (tip_timer != NULL) lv_timer_pause(tip_timer);
    if (tip_plate != NULL) kit_show(tip_plate, false);
    tip_over = NULL;
}

/* Under the object and inside the window, above it when there is no room below. Wrapped past
 * a width that reads in one glance. */
static void on_tip_due(lv_timer_t *timer)
{
    int i = tip_over != NULL ? tip_index(tip_over) : -1;
    const lv_font_t *font = ui_font(13);
    int32_t pad = ui_px(10), widest = ui_px(340), width, height, x, y;
    int32_t screen_w, screen_h;
    lv_display_t *display;
    lv_point_t size;
    lv_area_t at;

    lv_timer_pause(timer);
    if (i < 0 || tips[i].text[0] == '\0') return;
    display = lv_obj_get_display(tip_over);
    screen_w = lv_display_get_horizontal_resolution(display);
    screen_h = lv_display_get_vertical_resolution(display);
    if (tip_plate == NULL) {
        tip_plate = lv_label_create(lv_layer_top());
        lv_obj_add_style(tip_plate, &styles.menu, 0);
        lv_label_set_long_mode(tip_plate, LV_LABEL_LONG_WRAP);
    }
    /* At each showing, as the zoom may have changed */
    lv_obj_set_style_text_font(tip_plate, font, 0);
    lv_obj_set_style_text_line_space(tip_plate, ui_px(2), 0);
    lv_obj_set_style_pad_ver(tip_plate, ui_px(6), 0);
    lv_obj_set_style_pad_hor(tip_plate, pad, 0);
    lv_label_set_text(tip_plate, tips[i].text);
    lv_text_get_size(&size, tips[i].text, font, 0, ui_px(2), LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(tip_plate, LV_MIN(size.x, widest) + 2 * pad + 2 * lv_obj_get_style_border_width(tip_plate, 0));
    kit_show(tip_plate, true);
    lv_obj_update_layout(tip_plate);

    lv_obj_get_coords(tip_over, &at);
    width = lv_obj_get_width(tip_plate);
    height = lv_obj_get_height(tip_plate);
    x = LV_MAX(ui_px(8), LV_MIN(at.x1, screen_w - width - ui_px(8)));
    y = at.y2 + ui_px(6);
    if (y + height > screen_h - ui_px(8)) y = at.y1 - height - ui_px(6);
    lv_obj_set_pos(tip_plate, x, y);
}

static void on_tip_event(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    int i;

    switch (lv_event_get_code(e)) {
    case LV_EVENT_HOVER_OVER:
        hide_tip();
        tip_over = obj;
        if (tip_timer == NULL) tip_timer = lv_timer_create(on_tip_due, TIP_DELAY_MS, NULL);
        lv_timer_reset(tip_timer);
        lv_timer_resume(tip_timer);
        break;
    case LV_EVENT_DELETE:
        if (obj == tip_over) hide_tip();
        if ((i = tip_index(obj)) >= 0) tips[i] = tips[--tip_count];
        break;
    default:
        /* Left or pressed */
        if (obj == tip_over) hide_tip();
        break;
    }
}

void kit_tip(lv_obj_t *obj, const char *text)
{
    int i = tip_index(obj);

    if (i < 0) {
        if (tip_count == TIP_MAX) return;
        i = tip_count++;
        tips[i].obj = obj;
        /* Hover only reaches what can be clicked; a label still doesn't join the keys */
        lv_obj_set_clickable(obj, true);
        lv_obj_add_event_cb(obj, on_tip_event, LV_EVENT_HOVER_OVER, NULL);
        lv_obj_add_event_cb(obj, on_tip_event, LV_EVENT_HOVER_LEAVE, NULL);
        lv_obj_add_event_cb(obj, on_tip_event, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(obj, on_tip_event, LV_EVENT_DELETE, NULL);
    }
    snprintf(tips[i].text, sizeof tips[i].text, "%s", text);
}

/* -------------------------------------------------------------------------------------- */
/* Menus                                                                                  */
/* -------------------------------------------------------------------------------------- */

typedef struct {
    lv_obj_t *button;
    kit_menu_cb on_pick;
    lv_group_t *group, *before;
    int rows;
} menu_state;

static lv_indev_t *keypad(void)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i != NULL; i = lv_indev_get_next(i))
        if (lv_indev_get_type(i) == LV_INDEV_TYPE_KEYPAD) return i;
    return NULL;
}

static void on_menu_deleted(lv_event_t *e)
{
    menu_state *m = lv_event_get_user_data(e);

    lv_group_delete(m->group);
    free(m);
}

/* By the keyboard, the focus goes back to its button, and Enter's release must not press it */
static void menu_close(lv_obj_t *menu)
{
    menu_state *m = lv_obj_get_user_data(menu);
    lv_indev_t *keys = keypad();

    if (lv_obj_is_hidden(menu)) return;
    kit_show(menu, false);
    if (keys != NULL) {
        lv_indev_set_group(keys, m->before);
        if (lv_indev_active() == keys) {
            lv_indev_wait_release(keys);
            lv_group_focus_obj(m->button);
        }
    }
    lv_obj_delete_async(menu);
}

static void on_menu_outside(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) menu_close(lv_event_get_current_target(e));
}

static void on_menu_row(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target(e), *menu = lv_obj_get_parent(lv_obj_get_parent(row));
    menu_state *m = lv_obj_get_user_data(menu);
    kit_menu_cb on_pick = m->on_pick;
    int index = (int)(intptr_t)lv_obj_get_user_data(row);

    menu_close(menu);
    on_pick(index);
}

static void on_menu_key(lv_event_t *e)
{
    lv_obj_t *menu = lv_event_get_user_data(e);
    menu_state *m = lv_obj_get_user_data(menu);
    uint32_t key = lv_event_get_key(e);

    if (key == LV_KEY_UP) lv_group_focus_prev(m->group);
    if (key == LV_KEY_DOWN) lv_group_focus_next(m->group);
    if (key == LV_KEY_ESC) menu_close(menu);
}

/* Children: the plate, with the rows */
lv_obj_t *kit_menu(lv_obj_t *button, kit_menu_cb on_pick)
{
    lv_obj_t *menu = bare(lv_layer_top()), *plate;
    menu_state *m = calloc(1, sizeof *m);

    LV_ASSERT_MALLOC(m);
    m->button = button;
    m->on_pick = on_pick;
    m->group = lv_group_create();
    lv_group_set_wrap(m->group, false);
    lv_obj_set_size(menu, LV_PCT(100), LV_PCT(100));
    lv_obj_set_user_data(menu, m);
    lv_obj_set_clickable(menu, true);
    lv_obj_add_event_cb(menu, on_menu_outside, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(menu, on_menu_deleted, LV_EVENT_DELETE, m);
    kit_show(menu, false);

    plate = kit_box(menu, true, 2);
    lv_obj_add_style(plate, &styles.popup, 0);
    kit_let_out(plate, 4);
    return menu;
}

/* Children: check, title */
void kit_menu_add(lv_obj_t *menu, const char *title, bool chosen)
{
    menu_state *m = lv_obj_get_user_data(menu);
    lv_obj_t *row = lv_button_create(lv_obj_get_child(menu, 0)), *check;

    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &styles.popup_row, 0);
    lv_obj_add_style(row, &styles.popup_row_hover, LV_STATE_HOVERED);
    if (chosen) lv_obj_add_style(row, &styles.popup_row_chosen, 0);
    lv_obj_add_style(row, &styles.focus_ring, LV_STATE_FOCUS_KEY);
    ring_room(row);
    /* Made as wide as the widest when the menu opens */
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_user_data(row, (void *)(intptr_t)m->rows++);

    check = lv_label_create(row);
    lv_label_set_text(check, chosen ? LV_SYMBOL_OK : "");
    lv_obj_set_style_text_color(check, theme_color(TOKEN_ACCENT), 0);
    lv_obj_set_width(check, ui_px(16));
    lv_obj_set_style_text_font(lv_label_create(row), ui_bold(14), 0);
    lv_label_set_text(lv_obj_get_child(row, 1), title);

    /* Out of the window's group, into the menu's */
    lv_group_remove_obj(row);
    lv_group_add_obj(m->group, row);
    lv_obj_add_event_cb(row, on_menu_row, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(row, on_menu_key, LV_EVENT_KEY, menu);
    if (chosen) lv_group_focus_obj(row);
}

/* Under its button, or over it when there is no room below */
void kit_menu_open(lv_obj_t *menu)
{
    menu_state *m = lv_obj_get_user_data(menu);
    lv_obj_t *plate = lv_obj_get_child(menu, 0);
    lv_display_t *display = lv_obj_get_display(menu);
    int32_t screen_w = lv_display_get_horizontal_resolution(display), screen_h = lv_display_get_vertical_resolution(display);
    lv_indev_t *keys = keypad();
    int32_t y, widest = 0;
    lv_area_t at;

    lv_obj_get_coords(m->button, &at);
    lv_obj_update_layout(plate);
    for (uint32_t i = 0; i < lv_obj_get_child_count(plate); i++)
        widest = LV_MAX(widest, lv_obj_get_width(lv_obj_get_child(plate, (int32_t)i)));
    for (uint32_t i = 0; i < lv_obj_get_child_count(plate); i++) lv_obj_set_width(lv_obj_get_child(plate, (int32_t)i), widest);
    lv_obj_update_layout(plate);
    y = at.y2 + ui_px(4);
    if (y + lv_obj_get_height(plate) > screen_h - ui_px(8)) y = LV_MAX(ui_px(8), at.y1 - ui_px(4) - lv_obj_get_height(plate));
    lv_obj_set_pos(plate, LV_MAX(ui_px(8), LV_MIN(at.x1, screen_w - lv_obj_get_width(plate) - ui_px(8))), y);
    if (keys != NULL) {
        m->before = lv_indev_get_group(keys);
        lv_indev_set_group(keys, m->group);
    }
    if (lv_group_get_focused(m->group) == NULL) lv_group_focus_next(m->group);
    lv_obj_move_foreground(menu);
    kit_show(menu, true);
}

/* -------------------------------------------------------------------------------------- */
/* Dialogs                                                                                */
/* -------------------------------------------------------------------------------------- */

/* Children: the plate. A click outside does nothing, the keyboard stays in the dialog. */
lv_obj_t *kit_dialog(int32_t width)
{
    lv_obj_t *backdrop = bare(lv_layer_top()), *plate;
    menu_state *m = calloc(1, sizeof *m);
    lv_indev_t *keys = keypad();

    LV_ASSERT_MALLOC(m);
    m->group = lv_group_create();
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, theme_color(TOKEN_SHADE), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_40, 0);
    lv_obj_set_clickable(backdrop, true);
    lv_obj_set_user_data(backdrop, m);
    lv_obj_add_event_cb(backdrop, on_menu_deleted, LV_EVENT_DELETE, m);
    lv_obj_set_flex_flow(backdrop, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(backdrop, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    plate = kit_box(backdrop, true, 12);
    lv_obj_add_style(plate, &styles.popup, 0);
    lv_obj_set_width(plate, ui_px(width));
    lv_obj_set_style_pad_all(plate, ui_px(22), 0);
    kit_let_out(plate, 4);
    if (keys != NULL) {
        m->before = lv_indev_get_group(keys);
        lv_indev_set_group(keys, m->group);
    }
    return plate;
}

/* Out of the window's keyboard group, into the dialog's */
void kit_dialog_take(lv_obj_t *plate, lv_obj_t *obj)
{
    menu_state *m = lv_obj_get_user_data(lv_obj_get_parent(plate));

    lv_group_remove_obj(obj);
    lv_group_add_obj(m->group, obj);
}

/* The keyboard back to the window; Enter's release must not press what it lands on */
void kit_dialog_close(lv_obj_t *plate)
{
    lv_obj_t *backdrop = lv_obj_get_parent(plate);
    menu_state *m = lv_obj_get_user_data(backdrop);
    lv_indev_t *keys = keypad();

    if (keys != NULL) {
        lv_indev_set_group(keys, m->before);
        if (lv_indev_active() == keys) lv_indev_wait_release(keys);
    }
    lv_obj_delete_async(backdrop);
}
