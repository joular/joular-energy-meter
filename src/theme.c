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

/* Palette, shared styles, fonts and scale */

#include <math.h>

#include "app.h"
#include "theme_private.h"

/* WCAG AA: text, muted text and links >= 4.5:1 on enamel and plates, plate colour >= 4.5:1
 * on the green and red buttons. Series, lamps, control borders and focus ring >= 3:1.
 * Series colours checked for the three types of colour blindness. */
static const uint32_t palette[TOKEN_COUNT] = {
    [TOKEN_ENAMEL] = 0xe1edee, [TOKEN_ENAMEL_DEEP] = 0xc2d8da,
    [TOKEN_PLATE] = 0xfbfdfd, [TOKEN_PLATE_HOVER] = 0xeef5f6, [TOKEN_PLATE_PRESSED] = 0xe0ecee,
    [TOKEN_EDGE] = 0x5f7b80, [TOKEN_GRID] = 0xe2ecee,
    [TOKEN_TEXT] = 0x12292e, [TOKEN_MUTED] = 0x44595e, [TOKEN_FAINT] = 0x506569, [TOKEN_ERROR] = 0xa82e17,
    [TOKEN_ACCENT] = 0x1a6670,
    [TOKEN_FOCUS] = 0xa86400, [TOKEN_OFF] = 0x758a8d,
    [TOKEN_GO] = 0x1f7568, [TOKEN_GO_HOVER] = 0x1a675b, [TOKEN_GO_PRESSED] = 0x15584e,
    [TOKEN_HALT] = 0xc4381f, [TOKEN_HALT_HOVER] = 0xad301a, [TOKEN_HALT_PRESSED] = 0x982a17,
    [TOKEN_REGISTER] = 0x10262b, [TOKEN_WHEEL] = 0xeef5f5, [TOKEN_WHEEL_LEAD] = 0x81979a,
    [TOKEN_DISC] = 0xcbdcde, [TOKEN_DISC_TICK] = 0x5f7b80,
    [TOKEN_LAMP_OFF] = 0x758a8d, [TOKEN_LAMP_RUN] = 0x1e7d43, [TOKEN_LAMP_WAIT] = 0x9a6a00,
    [TOKEN_ALARM] = 0xf6ebc8, [TOKEN_ALARM_EDGE] = 0xd9b55a, [TOKEN_ALARM_TEXT] = 0x4a3500,
    [TOKEN_TOTAL] = 0x12292e, [TOKEN_CPU] = 0x2d6a8e, [TOKEN_GPU] = 0x8a6420, [TOKEN_TARGET] = 0x9a2450,
};

theme_styles styles;

lv_color_t theme_color(theme_token t)
{
    return lv_color_hex(palette[t]);
}

/* platform_sdl.c sets DPI to 160 x scale, so lv_dpx(n) = n x scale */
int32_t ui_px(int32_t logical)
{
    return lv_dpx(logical);
}

/* ------------------------------------------------------------------------------------------ */
/* The fonts                                                                                  */
/* ------------------------------------------------------------------------------------------ */

/* TTF data in src/fonts */
extern const unsigned char barlow_medium[], barlow_figures[];
extern const size_t barlow_medium_size, barlow_figures_size;

/* Built-in Montserrat sizes, for LVGL symbols */
static const lv_font_t *symbols(int32_t px)
{
    static const lv_font_t *const sizes[] = {
        &lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_16, &lv_font_montserrat_18,
        &lv_font_montserrat_20, &lv_font_montserrat_22, &lv_font_montserrat_24, &lv_font_montserrat_26,
        &lv_font_montserrat_28, &lv_font_montserrat_30, &lv_font_montserrat_32,
    };
    return sizes[LV_CLAMP(0, (px - 11) / 2, (int32_t)(sizeof sizes / sizeof *sizes) - 1)];
}

/* Barlow digits are proportional, so numbers jitter. Give each the width of a 0, centred.
 * A 1 only gets half the extra, full width looks like a space. */
static bool even_digit(const lv_font_t *font, lv_font_glyph_dsc_t *dsc, uint32_t letter, uint32_t next)
{
    const lv_font_t *plain = font->user_data;
    lv_font_glyph_dsc_t zero;

    if (!plain->get_glyph_dsc(font, dsc, letter, next)) return false;
    if (letter >= '0' && letter <= '9' && plain->get_glyph_dsc(font, &zero, '0', 0)) {
        uint16_t width = letter == '1' ? (uint16_t)((zero.adv_w + dsc->adv_w + 1) / 2) : zero.adv_w;
        dsc->ofs_x = (int16_t)(dsc->ofs_x + (width - dsc->adv_w) / 2);
        dsc->adv_w = width;
    }
    return true;
}

/* Created on first use and cached, never freed */
static const lv_font_t *barlow(bool figures, int32_t logical)
{
    static struct { bool figures; int32_t px; lv_font_t *plain; lv_font_t even; } made[40];
    static size_t count;
    int32_t px = LV_MAX(8, (int32_t)lroundf((float)logical * platform_scale()));

    for (size_t i = 0; i < count; i++)
        if (made[i].figures == figures && made[i].px == px) return figures ? &made[i].even : made[i].plain;

    LV_ASSERT(count < sizeof made / sizeof *made);
    lv_font_t *plain = figures ? lv_tiny_ttf_create_data_ex(barlow_figures, barlow_figures_size, px,
                                                            LV_FONT_KERNING_NONE, LV_TINY_TTF_CACHE_GLYPH_CNT)
                               : lv_tiny_ttf_create_data_ex(barlow_medium, barlow_medium_size, px,
                                                            LV_FONT_KERNING_NONE, LV_TINY_TTF_CACHE_GLYPH_CNT);
    LV_ASSERT_NULL(plain);
    plain->fallback = symbols(px);
    size_t i = count++;
    made[i].figures = figures;
    made[i].px = px;
    made[i].plain = plain;
    made[i].even = *plain;
    made[i].even.user_data = plain;
    made[i].even.get_glyph_dsc = even_digit;
    return figures ? &made[i].even : plain;
}

const lv_font_t *ui_font(int32_t logical)
{
    return barlow(false, logical);
}

const lv_font_t *ui_figures(int32_t logical)
{
    return barlow(true, logical);
}

/* ------------------------------------------------------------------------------------------ */
/* The styles                                                                                 */
/* ------------------------------------------------------------------------------------------ */

static void fill(lv_style_t *s, theme_token bg)
{
    lv_style_set_bg_color(s, theme_color(bg));
    lv_style_set_bg_opa(s, LV_OPA_COVER);
}

static void edge(lv_style_t *s, theme_token color, int32_t radius)
{
    lv_style_set_border_color(s, theme_color(color));
    lv_style_set_border_width(s, ui_px(1));
    lv_style_set_radius(s, ui_px(radius));
}

static void text(lv_style_t *s, theme_token color)
{
    lv_style_set_text_color(s, theme_color(color));
}

void theme_init(lv_display_t *disp)
{
    /* No outline here: LVGL computes the redraw area from the state change, so a growing
     * ring gets clipped */
    static const lv_style_prop_t changing[] = {
        LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_WIDTH,
        LV_STYLE_TEXT_COLOR, LV_STYLE_OPA, LV_STYLE_PROP_INV,
    };
    static lv_style_transition_dsc_t eased;
    lv_style_t *all = (lv_style_t *)&styles;

    for (size_t i = 0; i < sizeof styles / sizeof *all; i++) lv_style_init(&all[i]);

    /* Default theme covers what kit.c doesn't restyle, e.g. text selection */
    lv_theme_default_init(disp, theme_color(TOKEN_ACCENT), theme_color(TOKEN_HALT), false, ui_font(13));

    fill(&styles.window, TOKEN_ENAMEL);
    text(&styles.window, TOKEN_TEXT);
    lv_style_set_text_font(&styles.window, ui_font(13));
    lv_style_set_text_line_space(&styles.window, ui_px(2));
    lv_style_set_pad_all(&styles.window, 0);
    lv_style_set_pad_gap(&styles.window, 0);

    /* Top bar and status bar sit on the enamel, separated by a darker line */
    lv_style_set_border_color(&styles.bar, theme_color(TOKEN_ENAMEL_DEEP));
    lv_style_set_border_width(&styles.bar, ui_px(2));
    lv_style_set_border_side(&styles.bar, LV_BORDER_SIDE_BOTTOM);
    lv_style_set_border_color(&styles.status_bar, theme_color(TOKEN_ENAMEL_DEEP));
    lv_style_set_border_width(&styles.status_bar, ui_px(1));
    lv_style_set_border_side(&styles.status_bar, LV_BORDER_SIDE_TOP);

    fill(&styles.plate, TOKEN_PLATE);
    edge(&styles.plate, TOKEN_ENAMEL_DEEP, 8);
    lv_style_set_pad_hor(&styles.strip, ui_px(16));
    lv_style_set_pad_top(&styles.strip, ui_px(12));

    text(&styles.tone[TONE_TEXT], TOKEN_TEXT);
    text(&styles.tone[TONE_MUTED], TOKEN_MUTED);
    text(&styles.tone[TONE_FAINT], TOKEN_FAINT);
    text(&styles.tone[TONE_ERROR], TOKEN_ERROR);
    text(&styles.tone[TONE_ACCENT], TOKEN_ACCENT);
    text(&styles.tone[TONE_WHEEL], TOKEN_WHEEL);

    /* Eased state changes (hover, press, focus, disabled) */
    lv_style_transition_dsc_init(&eased, changing, lv_anim_path_ease_out, THEME_EASE_MS, 0, NULL);
    lv_style_set_transition(&styles.motion, &eased);

    /* Buttons: plain ones are plate coloured, Start green, Stop red, text in plate colour */
    fill(&styles.key, TOKEN_PLATE);
    edge(&styles.key, TOKEN_EDGE, 6);
    text(&styles.key, TOKEN_TEXT);
    lv_style_set_text_font(&styles.key, ui_font(14));
    lv_style_set_pad_hor(&styles.key, ui_px(14));
    lv_style_set_pad_column(&styles.key, ui_px(8));
    fill(&styles.key_hover, TOKEN_PLATE_HOVER);
    fill(&styles.key_pressed, TOKEN_PLATE_PRESSED);
    fill(&styles.go, TOKEN_GO);
    lv_style_set_border_color(&styles.go, theme_color(TOKEN_GO));
    text(&styles.go, TOKEN_PLATE);
    fill(&styles.go_hover, TOKEN_GO_HOVER);
    fill(&styles.go_pressed, TOKEN_GO_PRESSED);
    fill(&styles.halt, TOKEN_HALT);
    lv_style_set_border_color(&styles.halt, theme_color(TOKEN_HALT));
    fill(&styles.halt_hover, TOKEN_HALT_HOVER);
    fill(&styles.halt_pressed, TOKEN_HALT_PRESSED);
    lv_style_set_opa(&styles.disabled, LV_OPA_40);
    /* Disabled button: outline only */
    lv_style_set_bg_opa(&styles.off, LV_OPA_TRANSP);
    lv_style_set_border_color(&styles.off, theme_color(TOKEN_OFF));
    text(&styles.off, TOKEN_OFF);
    /* Keyboard focus only, not shown for mouse clicks */
    lv_style_set_outline_color(&styles.focus_ring, theme_color(TOKEN_FOCUS));
    lv_style_set_outline_width(&styles.focus_ring, ui_px(2));
    lv_style_set_outline_pad(&styles.focus_ring, ui_px(2));

    fill(&styles.picker, TOKEN_PLATE);
    edge(&styles.picker, TOKEN_EDGE, 6);
    text(&styles.picker, TOKEN_TEXT);
    lv_style_set_text_font(&styles.picker, ui_font(15));
    lv_style_set_pad_hor(&styles.picker, ui_px(10));
    fill(&styles.picker_hover, TOKEN_PLATE_HOVER);
    fill(&styles.picker_pressed, TOKEN_PLATE_PRESSED);

    fill(&styles.entry, TOKEN_PLATE);
    edge(&styles.entry, TOKEN_EDGE, 6);
    text(&styles.entry, TOKEN_TEXT);
    lv_style_set_text_font(&styles.entry, ui_font(15));
    lv_style_set_pad_hor(&styles.entry, ui_px(10));
    /* Extra pixel as outline, so the text doesn't shift */
    lv_style_set_border_color(&styles.entry_focused, theme_color(TOKEN_TEXT));
    lv_style_set_outline_color(&styles.entry_focused, theme_color(TOKEN_TEXT));
    lv_style_set_outline_width(&styles.entry_focused, ui_px(1));
    lv_style_set_border_color(&styles.entry_invalid, theme_color(TOKEN_ERROR));
    lv_style_set_outline_color(&styles.entry_invalid, theme_color(TOKEN_ERROR));
    text(&styles.placeholder, TOKEN_FAINT);
    /* No blinking caret, it would wake the loop 60 times a second */
    lv_style_set_border_color(&styles.cursor, theme_color(TOKEN_TEXT));
    lv_style_set_border_width(&styles.cursor, ui_px(1));
    lv_style_set_border_side(&styles.cursor, LV_BORDER_SIDE_LEFT);

    fill(&styles.switch_track, TOKEN_PLATE);
    edge(&styles.switch_track, TOKEN_EDGE, 0);
    lv_style_set_radius(&styles.switch_track, LV_RADIUS_CIRCLE);
    /* Knob slide time */
    lv_style_set_anim_duration(&styles.switch_track, THEME_EASE_MS);
    fill(&styles.switch_on, TOKEN_TEXT);
    lv_style_set_radius(&styles.switch_on, LV_RADIUS_CIRCLE);
    fill(&styles.knob, TOKEN_PLATE);
    edge(&styles.knob, TOKEN_EDGE, 0);
    lv_style_set_radius(&styles.knob, LV_RADIUS_CIRCLE);
    lv_style_set_pad_all(&styles.knob, -ui_px(3));

    /* Dropdown list */
    fill(&styles.menu, TOKEN_PLATE);
    edge(&styles.menu, TOKEN_EDGE, 6);
    lv_style_set_shadow_color(&styles.menu, theme_color(TOKEN_TEXT));
    lv_style_set_shadow_width(&styles.menu, ui_px(12));
    lv_style_set_shadow_offset_y(&styles.menu, ui_px(4));
    lv_style_set_shadow_opa(&styles.menu, LV_OPA_20);
    text(&styles.menu, TOKEN_TEXT);
    lv_style_set_text_font(&styles.menu, ui_font(15));
    lv_style_set_pad_ver(&styles.menu, ui_px(4));
    /* Same padding as the closed picker so options line up */
    lv_style_set_pad_hor(&styles.menu, ui_px(10));
    lv_style_set_text_line_space(&styles.menu, ui_px(10));
    fill(&styles.menu_hover, TOKEN_PLATE_PRESSED);
    text(&styles.menu_hover, TOKEN_TEXT);
    fill(&styles.menu_checked, TOKEN_TEXT);
    text(&styles.menu_checked, TOKEN_PLATE);

    /* Register wheels, red for tenths, and the disc slot */
    fill(&styles.wheel, TOKEN_REGISTER);
    lv_style_set_radius(&styles.wheel, ui_px(2));
    lv_style_set_clip_corner(&styles.wheel, false);
    fill(&styles.wheel_red, TOKEN_HALT);
    fill(&styles.slot, TOKEN_REGISTER);
    lv_style_set_radius(&styles.slot, ui_px(4));
    lv_style_set_pad_all(&styles.slot, ui_px(4));

    lv_style_set_radius(&styles.lamp, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&styles.lamp, LV_OPA_COVER);
    lv_style_set_shadow_opa(&styles.lamp, LV_OPA_60);

    fill(&styles.alarm, TOKEN_ALARM);
    edge(&styles.alarm, TOKEN_ALARM_EDGE, 8);
    text(&styles.alarm, TOKEN_ALARM_TEXT);
    /* New message highlighted for a few seconds; errors stay highlighted in amber */
    fill(&styles.message_new, TOKEN_PLATE);
    text(&styles.message_new, TOKEN_TEXT);
    lv_style_set_radius(&styles.message_new, ui_px(4));
    fill(&styles.message_trouble, TOKEN_ALARM);
    text(&styles.message_trouble, TOKEN_ALARM_TEXT);
    lv_style_set_radius(&styles.message_trouble, ui_px(4));
    lv_style_set_text_decor(&styles.link_hover, LV_TEXT_DECOR_UNDERLINE);

    fill(&styles.scrollbar, TOKEN_EDGE);
    lv_style_set_width(&styles.scrollbar, ui_px(4));
    lv_style_set_radius(&styles.scrollbar, LV_RADIUS_CIRCLE);
    lv_style_set_pad_right(&styles.scrollbar, ui_px(3));
}
