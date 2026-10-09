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

/* Styles from theme.c used by kit.c and ui.c */

#ifndef JOULAR_ENERGY_METER_THEME_PRIVATE_H
#define JOULAR_ENERGY_METER_THEME_PRIVATE_H

#include "app.h"

/* Style transition time, ms */
#define THEME_EASE_MS 120

/* Text colours */
typedef enum {
    TONE_TEXT, TONE_MUTED, TONE_FAINT, TONE_ERROR, TONE_ACCENT, TONE_WHEEL,
    TONE_COUNT
} text_tone;

/* Only lv_style_t members, theme.c inits it as an array */
typedef struct theme_styles {
    lv_style_t window, bar, status_bar;
    lv_style_t plate, strip;
    lv_style_t tone[TONE_COUNT];
    lv_style_t motion;
    lv_style_t key, key_hover, key_pressed, go, go_hover, go_pressed, halt, halt_hover, halt_pressed;
    lv_style_t disabled, off, focus_ring;
    lv_style_t picker, picker_hover, picker_pressed;
    lv_style_t entry, entry_focused, entry_invalid, placeholder, cursor;
    lv_style_t switch_track, switch_on, knob;
    lv_style_t menu, menu_hover, menu_checked;
    lv_style_t wheel, wheel_red, slot;
    lv_style_t lamp, alarm, message_new, message_trouble, link_hover;
    lv_style_t scrollbar;
} theme_styles;

extern theme_styles styles;

#endif /* JOULAR_ENERGY_METER_THEME_PRIVATE_H */
