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

/* Palette, shared styles, fonts and scale: Joular Code GUI's look, light and dark */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "theme_private.h"

/* WCAG AA in both looks: text, muted text and links >= 4.5:1 on the ground, cards and fields,
 * the text of Start and Stop on them. Field borders, series, lamps, tile signs and the focus
 * ring >= 3:1. Series colours checked for the three types of colour blindness, lighter in the
 * dark look. */
static const uint32_t palettes[2][TOKEN_COUNT] = { {
    [TOKEN_ENAMEL] = 0xe9f1f3, [TOKEN_ENAMEL_DEEP] = 0xd3dee2, [TOKEN_RULE] = 0xe3eaed,
    [TOKEN_GROUND_END] = 0xeceff6, [TOKEN_GLOW_SEA] = 0xc4e3e6, [TOKEN_GLOW_SUN] = 0xfbe0c2,
    [TOKEN_PLATE] = 0xffffff, [TOKEN_PLATE_HOVER] = 0xf0f6f7, [TOKEN_PLATE_PRESSED] = 0xe4eef0,
    [TOKEN_GLASS_EDGE] = 0xffffff, [TOKEN_SHADE] = 0x0e1c22,
    [TOKEN_EDGE] = 0xcdd8dc, [TOKEN_GRID] = 0xe3eaed, [TOKEN_FIELD] = 0xffffff, [TOKEN_FIELD_EDGE] = 0x87929d,
    [TOKEN_TEXT] = 0x14212b, [TOKEN_MUTED] = 0x57636e, [TOKEN_FAINT] = 0x5f6b76, [TOKEN_ERROR] = 0xb3361c,
    [TOKEN_ACCENT] = 0x0a6f7a, [TOKEN_ACCENT_SOFT] = 0xe0f0f1,
    [TOKEN_FOCUS] = 0xa86400, [TOKEN_OFF] = 0xe1e8eb,
    [TOKEN_GO] = 0x0a6f7a, [TOKEN_GO_HOVER] = 0x08626c, [TOKEN_GO_PRESSED] = 0x07545d,
    [TOKEN_HALT] = 0xb33a1f, [TOKEN_HALT_HOVER] = 0x9f3319, [TOKEN_HALT_PRESSED] = 0x8a2c16,
    [TOKEN_LAMP_OFF] = 0x758a8d, [TOKEN_LAMP_RUN] = 0x1e7d43, [TOKEN_LAMP_WAIT] = 0x9a6a00,
    [TOKEN_ALARM] = 0xfbf0d6, [TOKEN_ALARM_EDGE] = 0xe2c26b, [TOKEN_ALARM_TEXT] = 0x6f4a00,
    [TOKEN_ENERGY] = 0xf2a516, [TOKEN_ON_ENERGY] = 0x3b2a00, [TOKEN_ON_COLOUR] = 0xffffff,
    [TOKEN_TILE_BLUE] = 0x2f6bd8, [TOKEN_TILE_TEAL] = 0x0c8577, [TOKEN_TILE_GREY] = 0x64707d,
    [TOKEN_TOTAL] = 0x14212b, [TOKEN_CPU] = 0x2d6a8e, [TOKEN_GPU] = 0x8a6420, [TOKEN_TARGET] = 0x9a2450,
    /* Slate: apart from the machine's ink by lightness and from the processor's blue by hue */
    [TOKEN_PLUG] = 0x6b7c82,
}, {
    [TOKEN_ENAMEL] = 0x0c171d, [TOKEN_ENAMEL_DEEP] = 0x2a3a47, [TOKEN_RULE] = 0x24323e,
    [TOKEN_GROUND_END] = 0x0d1220, [TOKEN_GLOW_SEA] = 0x14404a, [TOKEN_GLOW_SUN] = 0x322a1e,
    [TOKEN_PLATE] = 0x182430, [TOKEN_PLATE_HOVER] = 0x203040, [TOKEN_PLATE_PRESSED] = 0x283a4b,
    [TOKEN_GLASS_EDGE] = 0x2b3b48, [TOKEN_SHADE] = 0x000000,
    [TOKEN_EDGE] = 0x33465a, [TOKEN_GRID] = 0x24323e, [TOKEN_FIELD] = 0x101a22, [TOKEN_FIELD_EDGE] = 0x5f7180,
    [TOKEN_TEXT] = 0xe8eef3, [TOKEN_MUTED] = 0x9db0bf, [TOKEN_FAINT] = 0x97aab9, [TOKEN_ERROR] = 0xf28a73,
    [TOKEN_ACCENT] = 0x45c1cc, [TOKEN_ACCENT_SOFT] = 0x173c43,
    [TOKEN_FOCUS] = 0xf2b44a, [TOKEN_OFF] = 0x22303c,
    /* Light teal: Start's text is the card's dark */
    [TOKEN_GO] = 0x45c1cc, [TOKEN_GO_HOVER] = 0x5ccad4, [TOKEN_GO_PRESSED] = 0x37a9b3,
    [TOKEN_HALT] = 0xc8402a, [TOKEN_HALT_HOVER] = 0xd24d36, [TOKEN_HALT_PRESSED] = 0xb03722,
    [TOKEN_LAMP_OFF] = 0x6f838f, [TOKEN_LAMP_RUN] = 0x4cc276, [TOKEN_LAMP_WAIT] = 0xe0a83a,
    [TOKEN_ALARM] = 0x33280f, [TOKEN_ALARM_EDGE] = 0x6b5320, [TOKEN_ALARM_TEXT] = 0xf7c870,
    [TOKEN_ENERGY] = 0xf7b84a, [TOKEN_ON_ENERGY] = 0x3b2a00, [TOKEN_ON_COLOUR] = 0xffffff,
    [TOKEN_TILE_BLUE] = 0x3d7be6, [TOKEN_TILE_TEAL] = 0x13958a, [TOKEN_TILE_GREY] = 0x5b6a78,
    [TOKEN_TOTAL] = 0xe8eef3, [TOKEN_CPU] = 0x6aaed6, [TOKEN_GPU] = 0xd9a441, [TOKEN_TARGET] = 0xe27aa4,
    [TOKEN_PLUG] = 0x9fb2ba,
} };

/* How much of the ground shows through cards and the bar; how dark shadows are */
static const struct { lv_opa_t card, card_shadow, bar_shadow, lift; } glass[2] = {
    { 204, 20, 40, 30 },
    { 184, 90, 140, 120 },
};

static bool dark, dark_from_system;

theme_styles styles;

lv_color_t theme_color(theme_token t)
{
    return lv_color_hex(palettes[dark][t]);
}

bool theme_is_dark(void)
{
    return dark;
}

/* ------------------------------------------------------------------------------------------ */
/* The ground                                                                                 */
/* ------------------------------------------------------------------------------------------ */

/* Painted once per window size, as CSS would: a 160° gradient, then the sun's glow from the
 * bottom right, then the sea's from the top left. Drawn as the window's background, it costs
 * a copy, where a gradient would be computed again on every redraw. */
static lv_draw_buf_t *ground;

/* Strength of an elliptic glow, 1 at its centre to 0 at its edge; u and v in its radii */
static float glow(float u, float v)
{
    float d = sqrtf(u * u + v * v);
    return d >= 1.0f ? 0.0f : 1.0f - d;
}

/* In the pixels' byte order */
static void bgr(theme_token t, float out[3])
{
    lv_color_t c = theme_color(t);
    out[0] = c.blue;
    out[1] = c.green;
    out[2] = c.red;
}

static void paint_ground(lv_display_t *disp)
{
    /* Ordered dither, so the soft gradients don't band */
    static const uint8_t bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    float w = (float)lv_display_get_horizontal_resolution(disp), h = (float)lv_display_get_vertical_resolution(disp);
    /* 160°: down and a bit to the right */
    float dx = 0.342f, dy = 0.940f, length = w * dx + h * dy;
    float from[3], to[3], sun[3], sea[3];
    lv_draw_buf_t *old = ground;
    static bool painted_dark;

    /* The same size and look: only the styles, made again, need it */
    if (ground != NULL && ground->header.w == (uint32_t)w && ground->header.h == (uint32_t)h && painted_dark == dark) {
        lv_style_set_bg_image_src(&styles.window, ground);
        return;
    }
    painted_dark = dark;
    ground = lv_draw_buf_create((uint32_t)w, (uint32_t)h, LV_COLOR_FORMAT_XRGB8888, LV_STRIDE_AUTO);
    if (ground == NULL) {
        ground = old;
        return;
    }
    bgr(TOKEN_ENAMEL, from);
    bgr(TOKEN_GROUND_END, to);
    bgr(TOKEN_GLOW_SUN, sun);
    bgr(TOKEN_GLOW_SEA, sea);
    for (int32_t y = 0; y < (int32_t)h; y++) {
        uint8_t *px = ground->data + (size_t)y * ground->header.stride;
        /* The sea fades out at 60 % of a 70 % x 90 % ellipse, the sun at 62 % of 60 % x 80 % */
        float sea_v = (float)y / (0.54f * h), sun_v = (h - (float)y) / (0.496f * h);
        for (int32_t x = 0; x < (int32_t)w; x++, px += 4) {
            float t = LV_CLAMP(0.0f, (((float)x - w / 2) * dx + ((float)y - h / 2) * dy) / length + 0.5f, 1.0f);
            float a = glow((float)x / (0.42f * w), sea_v), b = glow((w - (float)x) / (0.372f * w), sun_v);
            float noise = bayer[y & 3][x & 3] / 16.0f - 0.47f;
            for (int i = 0; i < 3; i++) {
                float c = from[i] + (to[i] - from[i]) * t;
                c += (sun[i] - c) * b;
                c += (sea[i] - c) * a;
                px[i] = (uint8_t)LV_CLAMP(0.0f, c + noise + 0.5f, 255.0f);
            }
            px[3] = 0xff;
        }
    }
    /* A new size or look redraws everything anyway */
    lv_style_set_bg_image_src(&styles.window, ground);
    if (old) lv_draw_buf_destroy(old);
}

static void on_resolution(lv_event_t *e)
{
    paint_ground(lv_event_get_target(e));
}

/* platform_sdl.c sets DPI to 160 x scale x zoom, so lv_dpx(n) = n x scale x zoom */
int32_t ui_px(int32_t logical)
{
    return lv_dpx(logical);
}

/* The black lettering in the text's colour in the dark look. Made once for each of the two
 * logos, never freed. */
const void *theme_logo(const lv_image_dsc_t *logo)
{
    static struct { const lv_image_dsc_t *from; lv_draw_buf_t *copy; } made[2];
    lv_color_t ink = theme_color(TOKEN_TEXT);
    lv_draw_buf_t image, *copy;
    int i = 0;

    if (!dark) return logo;
    while (i < 1 && made[i].from != NULL && made[i].from != logo) i++;
    if (made[i].from == logo) return made[i].copy;
    if (lv_draw_buf_from_image(&image, logo) != LV_RESULT_OK || (copy = lv_draw_buf_dup(&image)) == NULL) return logo;
    made[i].from = logo;
    made[i].copy = copy;
    for (uint32_t y = 0; y < logo->header.h; y++) {
        uint8_t *px = copy->data + (size_t)y * copy->header.stride;
        /* ARGB8888, straight alpha: the lettering's edges are black too, only fainter */
        for (uint32_t x = 0; x < logo->header.w; x++, px += 4)
            if (px[0] < 60 && px[1] < 60 && px[2] < 60) {
                px[0] = ink.blue;
                px[1] = ink.green;
                px[2] = ink.red;
            }
    }
    return copy;
}

/* ------------------------------------------------------------------------------------------ */
/* The fonts                                                                                  */
/* ------------------------------------------------------------------------------------------ */

/* TTF data in src/fonts */
extern const unsigned char barlow_medium[], barlow_semibold[], barlow_figures[], lucide_icons[];
extern const size_t barlow_medium_size, barlow_semibold_size, barlow_figures_size, lucide_icons_size;

typedef enum { FONT_MEDIUM, FONT_SEMIBOLD, FONT_FIGURES, FONT_ICONS } font_family;

/* Built-in Montserrat sizes, for LVGL symbols Lucide lacks */
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

/* Each size made on first use and cached, never freed: about twenty per zoom. Text falls back
 * to Lucide for LVGL's symbols, then Montserrat. The built-in font, with a word on stderr, if a
 * TTF can't be made (out of memory) or the cache is full. */
static const lv_font_t *font_at(font_family family, int32_t px)
{
    static const struct { const unsigned char *data; const size_t *size; } ttf[] = {
        [FONT_MEDIUM] = { barlow_medium, &barlow_medium_size },
        [FONT_SEMIBOLD] = { barlow_semibold, &barlow_semibold_size },
        [FONT_FIGURES] = { barlow_figures, &barlow_figures_size },
        [FONT_ICONS] = { lucide_icons, &lucide_icons_size },
    };
    static struct { font_family family; int32_t px; lv_font_t *plain; lv_font_t even; } made[200];
    static size_t count;
    static bool said;
    lv_font_t *plain = NULL;

    for (size_t i = 0; i < count; i++)
        if (made[i].family == family && made[i].px == px) return family == FONT_FIGURES ? &made[i].even : made[i].plain;

    if (count < sizeof made / sizeof *made)
        plain = lv_tiny_ttf_create_data_ex(ttf[family].data, *ttf[family].size, px, LV_FONT_KERNING_NONE,
                                           LV_TINY_TTF_CACHE_GLYPH_CNT);
    if (plain == NULL) {
        if (!said) fprintf(stderr, JOULARENERGYMETER_NAME ": a font can't be made, using the built-in one\n");
        said = true;
        return symbols(px);
    }
    plain->fallback = family == FONT_ICONS ? symbols(px) : font_at(FONT_ICONS, px);
    size_t i = count++;
    made[i].family = family;
    made[i].px = px;
    made[i].plain = plain;
    made[i].even = *plain;
    made[i].even.user_data = plain;
    made[i].even.get_glyph_dsc = even_digit;
    return family == FONT_FIGURES ? &made[i].even : plain;
}

static int32_t px_of(int32_t logical)
{
    return LV_MAX(8, (int32_t)lroundf((float)logical * platform_scale() * platform_zoom()));
}

const lv_font_t *ui_font(int32_t logical)
{
    return font_at(FONT_MEDIUM, px_of(logical));
}

const lv_font_t *ui_bold(int32_t logical)
{
    return font_at(FONT_SEMIBOLD, px_of(logical));
}

const lv_font_t *ui_figures(int32_t logical)
{
    return font_at(FONT_FIGURES, px_of(logical));
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

static void shadow(lv_style_t *s, int32_t width, int32_t below, lv_opa_t opa)
{
    lv_style_set_shadow_color(s, theme_color(TOKEN_SHADE));
    lv_style_set_shadow_width(s, ui_px(width));
    lv_style_set_shadow_offset_y(s, ui_px(below));
    lv_style_set_shadow_opa(s, opa);
}

/* Frosted: the ground shows through, a light edge */
static void glassy(lv_style_t *s, int32_t radius)
{
    fill(s, TOKEN_PLATE);
    lv_style_set_bg_opa(s, glass[dark].card);
    edge(s, TOKEN_GLASS_EDGE, radius);
}

static void field(lv_style_t *s, int32_t font)
{
    fill(s, TOKEN_FIELD);
    edge(s, TOKEN_FIELD_EDGE, 10);
    text(s, TOKEN_TEXT);
    lv_style_set_text_font(s, ui_font(font));
    lv_style_set_pad_hor(s, ui_px(11));
}

static void make_styles(void)
{
    /* No outline here: LVGL computes the redraw area from the state change, so a growing
     * ring gets clipped */
    static const lv_style_prop_t changing[] = {
        LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, LV_STYLE_BORDER_COLOR, LV_STYLE_BORDER_WIDTH,
        LV_STYLE_TEXT_COLOR, LV_STYLE_OPA, LV_STYLE_PROP_INV,
    };
    static lv_style_transition_dsc_t eased;

    fill(&styles.window, TOKEN_ENAMEL);
    text(&styles.window, TOKEN_TEXT);
    lv_style_set_text_font(&styles.window, ui_font(13));
    lv_style_set_text_line_space(&styles.window, ui_px(2));
    lv_style_set_pad_all(&styles.window, 0);
    lv_style_set_pad_gap(&styles.window, 0);

    /* The header lies on the ground; the bar is a frosted plate floating above it */
    glassy(&styles.bar, 18);
    shadow(&styles.bar, 36, 12, glass[dark].bar_shadow);
    /* Header tabs: the chosen one a raised plate on a frosted track */
    fill(&styles.track, TOKEN_PLATE);
    lv_style_set_bg_opa(&styles.track, 140);
    lv_style_set_radius(&styles.track, ui_px(11));
    lv_style_set_pad_all(&styles.track, ui_px(3));
    lv_style_set_pad_gap(&styles.track, ui_px(2));
    text(&styles.tab, TOKEN_MUTED);
    lv_style_set_text_font(&styles.tab, ui_font(15));
    lv_style_set_pad_hor(&styles.tab, ui_px(12));
    lv_style_set_pad_column(&styles.tab, ui_px(8));
    lv_style_set_radius(&styles.tab, ui_px(8));
    fill(&styles.tab_chosen, TOKEN_PLATE);
    text(&styles.tab_chosen, TOKEN_TEXT);
    lv_style_set_text_font(&styles.tab_chosen, ui_bold(15));
    shadow(&styles.tab_chosen, 4, 1, glass[dark].lift);

    /* Cards: frosted, a head with the tile and title, rules between rows */
    glassy(&styles.card, 16);
    shadow(&styles.card, 28, 8, glass[dark].card_shadow);
    lv_style_set_pad_left(&styles.head, ui_px(14));
    lv_style_set_pad_right(&styles.head, ui_px(14));
    lv_style_set_bg_opa(&styles.tile, LV_OPA_COVER);
    lv_style_set_radius(&styles.tile, ui_px(8));
    /* Rule between a card's rows */
    lv_style_set_border_color(&styles.rule_top, theme_color(TOKEN_RULE));
    lv_style_set_border_width(&styles.rule_top, ui_px(1));
    lv_style_set_border_side(&styles.rule_top, LV_BORDER_SIDE_TOP);

    text(&styles.tone[TONE_TEXT], TOKEN_TEXT);
    text(&styles.tone[TONE_MUTED], TOKEN_MUTED);
    text(&styles.tone[TONE_FAINT], TOKEN_FAINT);
    text(&styles.tone[TONE_ACCENT], TOKEN_ACCENT);

    /* Eased state changes (hover, press, focus, disabled) */
    lv_style_transition_dsc_init(&eased, changing, lv_anim_path_ease_out, THEME_EASE_MS, 0, NULL);
    lv_style_set_transition(&styles.motion, &eased);

    /* Buttons: plain ones white and bordered, Start teal, Stop red */
    fill(&styles.key, TOKEN_PLATE);
    edge(&styles.key, TOKEN_EDGE, 9);
    shadow(&styles.key, 3, 1, glass[dark].lift / 2);
    text(&styles.key, TOKEN_TEXT);
    lv_style_set_text_font(&styles.key, ui_font(14));
    lv_style_set_pad_hor(&styles.key, ui_px(13));
    lv_style_set_pad_column(&styles.key, ui_px(7));
    fill(&styles.key_hover, TOKEN_PLATE_HOVER);
    fill(&styles.key_pressed, TOKEN_PLATE_PRESSED);
    fill(&styles.go, TOKEN_GO);
    lv_style_set_border_color(&styles.go, theme_color(TOKEN_GO));
    text(&styles.go, TOKEN_PLATE);
    lv_style_set_radius(&styles.go, ui_px(12));
    shadow(&styles.go, 6, 2, glass[dark].lift);
    fill(&styles.go_hover, TOKEN_GO_HOVER);
    fill(&styles.go_pressed, TOKEN_GO_PRESSED);
    fill(&styles.halt, TOKEN_HALT);
    lv_style_set_border_color(&styles.halt, theme_color(TOKEN_HALT));
    text(&styles.halt, TOKEN_ON_COLOUR);
    fill(&styles.halt_hover, TOKEN_HALT_HOVER);
    fill(&styles.halt_pressed, TOKEN_HALT_PRESSED);
    /* Joined choices on a sunken track, the chosen one filled */
    fill(&styles.group, TOKEN_ENAMEL);
    lv_style_set_radius(&styles.group, ui_px(10));
    lv_style_set_pad_all(&styles.group, ui_px(3));
    lv_style_set_pad_gap(&styles.group, ui_px(2));
    text(&styles.choice, TOKEN_TEXT);
    lv_style_set_text_font(&styles.choice, ui_font(14));
    lv_style_set_pad_hor(&styles.choice, ui_px(11));
    lv_style_set_pad_column(&styles.choice, ui_px(7));
    lv_style_set_radius(&styles.choice, ui_px(7));
    fill(&styles.choice_hover, TOKEN_PLATE_HOVER);
    fill(&styles.chosen, TOKEN_ACCENT);
    text(&styles.chosen, TOKEN_PLATE);
    lv_style_set_text_font(&styles.chosen, ui_bold(14));
    lv_style_set_bg_color(&styles.chosen_hover, theme_color(TOKEN_ACCENT));
    /* Readable: a locked form still says what runs */
    lv_style_set_opa(&styles.disabled, LV_OPA_60);
    /* Disabled button: grey */
    fill(&styles.off, TOKEN_OFF);
    lv_style_set_border_color(&styles.off, theme_color(TOKEN_OFF));
    lv_style_set_shadow_opa(&styles.off, LV_OPA_TRANSP);
    text(&styles.off, TOKEN_FAINT);
    /* Keyboard focus only, not shown for mouse clicks */
    lv_style_set_outline_color(&styles.focus_ring, theme_color(TOKEN_FOCUS));
    lv_style_set_outline_width(&styles.focus_ring, ui_px(2));
    lv_style_set_outline_pad(&styles.focus_ring, ui_px(2));

    field(&styles.picker, 15);
    fill(&styles.picker_hover, TOKEN_PLATE_HOVER);
    fill(&styles.picker_pressed, TOKEN_PLATE_PRESSED);

    field(&styles.entry, 15);
    /* Extra pixel as outline, so the text doesn't shift */
    lv_style_set_border_color(&styles.entry_focused, theme_color(TOKEN_TEXT));
    lv_style_set_outline_color(&styles.entry_focused, theme_color(TOKEN_TEXT));
    lv_style_set_outline_width(&styles.entry_focused, ui_px(1));
    lv_style_set_border_color(&styles.entry_invalid, theme_color(TOKEN_ERROR));
    lv_style_set_outline_color(&styles.entry_invalid, theme_color(TOKEN_ERROR));
    lv_style_set_outline_width(&styles.entry_invalid, ui_px(1));
    text(&styles.placeholder, TOKEN_FAINT);
    /* No blinking caret, it would wake the loop 60 times a second */
    lv_style_set_border_color(&styles.cursor, theme_color(TOKEN_TEXT));
    lv_style_set_border_width(&styles.cursor, ui_px(1));
    lv_style_set_border_side(&styles.cursor, LV_BORDER_SIDE_LEFT);

    /* Squarer than a pill */
    fill(&styles.switch_track, TOKEN_FIELD_EDGE);
    lv_style_set_radius(&styles.switch_track, ui_px(7));
    /* Knob slide time */
    lv_style_set_anim_duration(&styles.switch_track, THEME_EASE_MS);
    fill(&styles.switch_on, TOKEN_ACCENT);
    lv_style_set_radius(&styles.switch_on, ui_px(7));
    fill(&styles.knob, TOKEN_ON_COLOUR);
    lv_style_set_radius(&styles.knob, ui_px(5));
    lv_style_set_pad_all(&styles.knob, -ui_px(3));

    /* Dropdown list, hover help */
    fill(&styles.menu, TOKEN_PLATE);
    edge(&styles.menu, TOKEN_EDGE, 10);
    shadow(&styles.menu, 12, 4, LV_OPA_20);
    text(&styles.menu, TOKEN_TEXT);
    lv_style_set_text_font(&styles.menu, ui_font(15));
    lv_style_set_pad_ver(&styles.menu, ui_px(4));
    /* Same padding as the closed picker so options line up */
    lv_style_set_pad_hor(&styles.menu, ui_px(11));
    lv_style_set_text_line_space(&styles.menu, ui_px(10));
    fill(&styles.menu_hover, TOKEN_PLATE_PRESSED);
    text(&styles.menu_hover, TOKEN_TEXT);
    fill(&styles.menu_checked, TOKEN_ACCENT);
    text(&styles.menu_checked, TOKEN_PLATE);

    lv_style_set_radius(&styles.lamp, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&styles.lamp, LV_OPA_COVER);
    lv_style_set_shadow_opa(&styles.lamp, LV_OPA_40);

    fill(&styles.alarm, TOKEN_ALARM);
    edge(&styles.alarm, TOKEN_ALARM_EDGE, 12);
    text(&styles.alarm, TOKEN_ALARM_TEXT);
    /* New message highlighted for a few seconds; errors stay highlighted in amber */
    fill(&styles.message_new, TOKEN_ACCENT_SOFT);
    text(&styles.message_new, TOKEN_TEXT);
    lv_style_set_radius(&styles.message_new, ui_px(6));
    fill(&styles.message_trouble, TOKEN_ALARM);
    text(&styles.message_trouble, TOKEN_ALARM_TEXT);
    lv_style_set_radius(&styles.message_trouble, ui_px(6));
    lv_style_set_text_decor(&styles.link_hover, LV_TEXT_DECOR_UNDERLINE);

    /* Menus under a key: a plate, rows with the chosen one tinted */
    fill(&styles.popup, TOKEN_PLATE);
    edge(&styles.popup, TOKEN_EDGE, 12);
    lv_style_set_pad_all(&styles.popup, ui_px(6));
    lv_style_set_pad_gap(&styles.popup, ui_px(2));
    shadow(&styles.popup, 24, 8, LV_OPA_20);
    text(&styles.popup_row, TOKEN_TEXT);
    lv_style_set_text_font(&styles.popup_row, ui_font(14));
    lv_style_set_radius(&styles.popup_row, ui_px(7));
    lv_style_set_pad_ver(&styles.popup_row, ui_px(9));
    lv_style_set_pad_left(&styles.popup_row, ui_px(10));
    lv_style_set_pad_right(&styles.popup_row, ui_px(14));
    lv_style_set_pad_column(&styles.popup_row, ui_px(8));
    fill(&styles.popup_row_hover, TOKEN_PLATE_HOVER);
    fill(&styles.popup_row_chosen, TOKEN_ACCENT_SOFT);

    fill(&styles.scrollbar, TOKEN_FIELD_EDGE);
    lv_style_set_width(&styles.scrollbar, ui_px(4));
    lv_style_set_radius(&styles.scrollbar, LV_RADIUS_CIRCLE);
    lv_style_set_pad_right(&styles.scrollbar, ui_px(3));
}

void theme_init(lv_display_t *disp)
{
    static const char *const looks[] = { "system", "light", "dark" };
    /* The system's asked once, again when the look goes back to it: not at each zoom */
    static int system_dark = -1;
    static bool made;
    int look = settings_index("look", looks, 3, 0);
    lv_style_t *all = (lv_style_t *)&styles;

    if (look == 0 && (system_dark < 0 || !dark_from_system)) system_dark = platform_dark_look();
    dark_from_system = look == 0;
    dark = look == 0 ? system_dark : look == 2;
    /* Again at a new zoom or look: the old values freed first */
    for (size_t i = 0; i < sizeof styles / sizeof *all; i++) {
        if (made) lv_style_reset(&all[i]);
        lv_style_init(&all[i]);
    }
    make_styles();
    paint_ground(disp);
    if (!made) lv_display_add_event_cb(disp, on_resolution, LV_EVENT_RESOLUTION_CHANGED, NULL);
    made = true;
    /* The default theme covers what kit.c doesn't restyle, e.g. text selection. Last: when its
     * dark flag changes, it restyles every widget, so with our styles made. */
    lv_theme_default_init(disp, theme_color(TOKEN_ACCENT), theme_color(TOKEN_HALT), dark, ui_font(13));
    platform_dark_title(dark);
}

/* Selected row as a picked menu option; disabled Choose and messages in the placeholder's
 * colour, which stays readable as text */
const file_browser_theme *theme_browser(void)
{
    static file_browser_theme t;

    t.backdrop = theme_color(TOKEN_SHADE);
    t.plate = theme_color(TOKEN_PLATE);
    t.pressed = theme_color(TOKEN_PLATE_PRESSED);
    t.edge = theme_color(TOKEN_FIELD_EDGE);
    t.text = theme_color(TOKEN_TEXT);
    t.muted = theme_color(TOKEN_FAINT);
    t.accent = theme_color(TOKEN_GO);
    t.accent_text = theme_color(TOKEN_PLATE);
    t.selection = theme_color(TOKEN_ACCENT);
    t.selection_text = theme_color(TOKEN_PLATE);
    t.focus = theme_color(TOKEN_FOCUS);
    t.font = ui_font(14);
    return &t;
}
