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

#include <math.h>
#include <stdint.h>
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
    [TEXT_BODY] = {13, TONE_TEXT, false},          [TEXT_MUTED] = {13, TONE_MUTED, false},
    [TEXT_CAPTION] = {12, TONE_MUTED, false},      [TEXT_ERROR] = {13, TONE_ERROR, false},
    [TEXT_LINK] = {12, TONE_ACCENT, false},        [TEXT_WORD] = {15, TONE_MUTED, false},
    [TEXT_TITLE] = {15, TONE_TEXT, false},
    [TEXT_READING] = {64, TONE_TEXT, true},        [TEXT_READING_UNIT] = {22, TONE_MUTED, false},
    [TEXT_READING_SECOND] = {40, TONE_TEXT, true},
    [TEXT_ROW_FIGURE] = {20, TONE_TEXT, true},     [TEXT_FIGURES] = {15, TONE_MUTED, true},
    [TEXT_WHEEL] = {30, TONE_WHEEL, true},
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

/* Dotted line along the bottom edge, roughly on the text baseline */
static void draw_leader(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_draw_line_dsc_t d;
    lv_area_t a;

    lv_obj_get_coords(obj, &a);
    lv_draw_line_dsc_init(&d);
    d.color = theme_color(TOKEN_OFF);
    d.width = ui_px(1);
    d.dash_width = ui_px(1);
    d.dash_gap = ui_px(3);
    d.p1.x = a.x1;
    d.p2.x = a.x2;
    d.p1.y = d.p2.y = a.y2;
    lv_draw_line(lv_event_get_layer(e), &d);
}

lv_obj_t *kit_leader(lv_obj_t *parent)
{
    lv_obj_t *leader = bare(parent);

    lv_obj_set_size(leader, 0, ui_px(12));
    lv_obj_set_flex_grow(leader, 1);
    lv_obj_add_event_cb(leader, draw_leader, LV_EVENT_DRAW_MAIN, NULL);
    return leader;
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

lv_obj_t *kit_dot(lv_obj_t *parent, theme_token color, bool wide)
{
    lv_obj_t *dot = bare(parent);
    lv_obj_set_size(dot, ui_px(wide ? 14 : 8), ui_px(wide ? 3 : 8));
    lv_obj_set_style_radius(dot, wide ? 0 : LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, theme_color(color), 0);
    return dot;
}

/* -------------------------------------------------------------------------------------- */
/* The register                                                                           */
/* -------------------------------------------------------------------------------------- */
/* Each wheel clips a label "0..9 0" that slides up. Current digit in the wheel's user data. */

#define WHEEL_STRIP "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n0"
#define WHEEL_ROLL_MS 250

static int32_t wheel_pitch(void)
{
    return lv_font_get_line_height(ui_figures(roles[TEXT_WHEEL].size));
}

static void on_wheel_rolled(lv_anim_t *a)
{
    lv_obj_t *strip = a->var;
    int digit = (int)(intptr_t)lv_obj_get_user_data(lv_obj_get_parent(strip));

    /* After 9 -> 0 we end on the last 0, snap back to the first */
    lv_obj_set_style_translate_y(strip, -digit * wheel_pitch(), 0);
}

static void roll_wheel(void *var, int32_t y)
{
    lv_obj_set_style_translate_y(var, y, 0);
}

lv_obj_t *kit_register(lv_obj_t *parent, int count)
{
    lv_obj_t *reg = kit_box(parent, false, 3);
    int32_t pitch = wheel_pitch();
    lv_font_glyph_dsc_t zero;

    lv_font_get_glyph_dsc(ui_figures(roles[TEXT_WHEEL].size), &zero, '0', 0);
    for (int i = 0; i < count; i++) {
        lv_obj_t *wheel = bare(reg), *strip;

        lv_obj_add_style(wheel, &styles.wheel, 0);
        if (i == count - 1) lv_obj_add_style(wheel, &styles.wheel_red, 0);
        lv_obj_set_size(wheel, zero.adv_w + ui_px(12), pitch + ui_px(6));
        strip = kit_label(wheel, WHEEL_STRIP, TEXT_WHEEL);
        lv_obj_set_style_text_line_space(strip, 0, 0);
        lv_obj_align(strip, LV_ALIGN_TOP_MID, 0, ui_px(3));
        lv_obj_set_user_data(wheel, (void *)(intptr_t)0);
    }
    char zeros[16] = "";
    memset(zeros, '0', (size_t)LV_MIN(count, 15));
    kit_register_set(reg, zeros, false);
    return reg;
}

void kit_register_set(lv_obj_t *reg, const char *digits, bool roll)
{
    int count = (int)lv_obj_get_child_count(reg);
    int32_t pitch = wheel_pitch();
    bool leading = true;

    for (int i = 0; i < count && digits[i]; i++) {
        lv_obj_t *wheel = lv_obj_get_child(reg, i), *strip = lv_obj_get_child(wheel, 0);
        int was = (int)(intptr_t)lv_obj_get_user_data(wheel), now = digits[i] - '0';
        lv_anim_t a;

        /* Never dim the units and tenths */
        leading = leading && now == 0 && i < count - 2;
        /* Setting a style always redraws, so check first */
        lv_color_t ink = theme_color(leading ? TOKEN_WHEEL_LEAD : TOKEN_WHEEL);
        if (!lv_color_eq(lv_obj_get_style_text_color(strip, 0), ink)) lv_obj_set_style_text_color(strip, ink, 0);
        if (now < 0 || now > 9 || now == was) continue;
        lv_obj_set_user_data(wheel, (void *)(intptr_t)now);
        lv_anim_delete(strip, roll_wheel);
        if (!roll) {
            lv_obj_set_style_translate_y(strip, -now * pitch, 0);
            continue;
        }

        /* Always roll forward: 0 goes to the last 0, a lower digit starts one step before it */
        lv_anim_init(&a);
        lv_anim_set_var(&a, strip);
        lv_anim_set_exec_cb(&a, roll_wheel);
        lv_anim_set_values(&a, -(now > was ? was : now == 0 ? 9 : now - 1) * pitch,
                           -(now == 0 ? 10 : now) * pitch);
        lv_anim_set_duration(&a, WHEEL_ROLL_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_completed_cb(&a, on_wheel_rolled);
        lv_anim_start(&a);
    }
}

/* -------------------------------------------------------------------------------------- */
/* The disc                                                                               */
/* -------------------------------------------------------------------------------------- */
/* Slot width = one turn. Ticks, every fifth long, and a red mark that wraps around. */

#define DISC_ROLL_MS 400

static struct {
    double shown, from, to;   /* turns */
} disc;

/* rect clipped to the slot horizontally */
static void disc_rect(lv_layer_t *layer, const lv_area_t *slot, lv_area_t rect, lv_color_t color)
{
    lv_draw_rect_dsc_t d;

    rect.x1 = LV_MAX(rect.x1, slot->x1);
    rect.x2 = LV_MIN(rect.x2, slot->x2);
    if (rect.x1 > rect.x2) return;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    lv_draw_rect(layer, &d, &rect);
}

static void draw_disc(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_draw_rect_dsc_t edge;
    lv_draw_line_dsc_t tick;

    lv_obj_get_content_coords(obj, &a);
    int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    int ticks = LV_MAX(10, w / ui_px(8) / 5 * 5);
    double phase = disc.shown - floor(disc.shown), at;

    /* Vertical gradient, lighter on top */
    lv_draw_rect_dsc_init(&edge);
    edge.radius = ui_px(2);
    edge.bg_color = theme_color(TOKEN_DISC);
    edge.bg_grad.dir = LV_GRAD_DIR_VER;
    edge.bg_grad.stops_count = 2;
    edge.bg_grad.stops[0].color = lv_color_lighten(theme_color(TOKEN_DISC), LV_OPA_40);
    edge.bg_grad.stops[0].opa = LV_OPA_COVER;
    edge.bg_grad.stops[0].frac = 0;
    edge.bg_grad.stops[1].color = lv_color_darken(theme_color(TOKEN_DISC), LV_OPA_10);
    edge.bg_grad.stops[1].opa = LV_OPA_COVER;
    edge.bg_grad.stops[1].frac = 255;
    lv_draw_rect(layer, &edge, &a);

    lv_draw_line_dsc_init(&tick);
    tick.color = theme_color(TOKEN_DISC_TICK);
    tick.width = ui_px(1);
    for (int k = 0; k < ticks; k++) {
        at = fmod(phase + (double)k / ticks, 1.0);
        tick.p1.x = tick.p2.x = a.x1 + (int32_t)lround(at * (w - 1));
        tick.p1.y = k % 5 ? a.y1 + h / 2 : a.y1 + h / 5;
        tick.p2.y = a.y2;
        lv_draw_line(layer, &tick);
    }

    /* Red mark starts mid-slot; drawn at -w, 0, +w to wrap */
    int32_t half = ui_px(9), x = a.x1 + (int32_t)lround(fmod(phase + 0.5, 1.0) * w);
    for (int32_t shift = -w; shift <= w; shift += w)
        disc_rect(layer, &a, (lv_area_t){ x + shift - half, a.y1, x + shift + half - 1, a.y2 },
                  theme_color(TOKEN_HALT));
}

static void turn_disc(void *var, int32_t v)
{
    disc.shown = disc.from + (disc.to - disc.from) * v / 1000.0;
    lv_obj_invalidate(var);
}

lv_obj_t *kit_disc(lv_obj_t *parent)
{
    lv_obj_t *slot = bare(parent);

    lv_obj_add_style(slot, &styles.slot, 0);
    lv_obj_set_height(slot, ui_px(30));
    lv_obj_add_event_cb(slot, draw_disc, LV_EVENT_DRAW_MAIN_END, NULL);
    return slot;
}

void kit_disc_turn(lv_obj_t *slot, double turns, bool roll)
{
    lv_anim_t a;

    lv_anim_delete(slot, turn_disc);
    if (!roll || turns < disc.shown) {
        disc.shown = turns;
        lv_obj_invalidate(slot);
        return;
    }
    disc.from = disc.shown;
    disc.to = turns;
    lv_anim_init(&a);
    lv_anim_set_var(&a, slot);
    lv_anim_set_exec_cb(&a, turn_disc);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, DISC_ROLL_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
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

lv_obj_t *kit_button(lv_obj_t *parent, const char *symbol, const char *text, button_kind kind,
                     lv_event_cb_t on_click, void *user_data)
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

    /* Symbol in its own label, one size smaller to match the cap height */
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
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
