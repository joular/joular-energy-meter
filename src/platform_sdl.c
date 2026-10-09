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

/*
 * SDL side: window, mouse, keyboard, clock and main loop. The LVGL display uses the pixels of
 * the screen the window opens on, so text is sharp on HiDPI; the mouse comes in window points
 * and is scaled here. Input is read only on SDL events, so an idle window runs no timer and
 * the loop sleeps in SDL until the next event.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#ifdef PJ_MACOS
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(PJ_WINDOWS)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "app.h"

/* LVGL render buffer, about 1/20 of a 15" Retina drawable and no slower than a bigger one */
#define PARTIAL_BUF_BYTES (1u * 1024 * 1024)

static struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Cursor *hand;
    int draw_w, draw_h;   /* the texture and the LVGL display, in pixels */
    float scale;          /* pixels per window point, read once when the window opens */
    lv_display_t *disp;
    void *buf;
    lv_indev_t *mouse, *keyboard;
    struct { int32_t x, y; bool pressed, moved; } ptr;
    struct { uint32_t key; lv_indev_state_t state; } kb;
} sdl;

static bool sdl_error(const char *what)
{
    fprintf(stderr, "%s: %s\n", what, SDL_GetError());
    return false;
}

/* -------------------------------------------------------------------------------------- */
/* Drawing                                                                                */
/* -------------------------------------------------------------------------------------- */

/* No alpha, a window doesn't use it and SDL copies without blending */
static bool create_texture(void)
{
    if (sdl.texture) SDL_DestroyTexture(sdl.texture);
    sdl.texture = SDL_CreateTexture(sdl.renderer, SDL_PIXELFORMAT_XRGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, sdl.draw_w, sdl.draw_h);
    if (!sdl.texture) return sdl_error("texture");
    /* Smooth on a screen of another scale, pixel exact on its own */
    SDL_SetTextureScaleMode(sdl.texture, SDL_ScaleModeLinear);
    return true;
}

static void present(void)
{
    SDL_RenderClear(sdl.renderer);
    SDL_RenderCopy(sdl.renderer, sdl.texture, NULL, NULL);
    SDL_RenderPresent(sdl.renderer);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    SDL_Rect r = { area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area) };
    int pitch = (int)lv_draw_buf_width_to_stride((uint32_t)r.w, LV_COLOR_FORMAT_XRGB8888);
    SDL_UpdateTexture(sdl.texture, &r, px_map, pitch);
    if (lv_display_flush_is_last(disp)) present();
    lv_display_flush_ready(disp);
}

/* lv_dpx(n) == n * scale, so the theme's lengths follow the screen */
static int32_t dpi(void)
{
    return (int32_t)(160.0f * sdl.scale + 0.5f);
}

/* Layout is in pixels at the scale the window opened with. Keep that scale on resize and let
 * SDL scale the picture on a screen of another scale. True if the size changed. */
static bool resize_display(void)
{
    int ww, wh;
    SDL_GetWindowSize(sdl.window, &ww, &wh);
    int w = (int)(ww * sdl.scale + 0.5f), h = (int)(wh * sdl.scale + 0.5f);
    if (w <= 0 || h <= 0 || (w == sdl.draw_w && h == sdl.draw_h)) return false;
    sdl.draw_w = w;
    sdl.draw_h = h;
    create_texture();
    lv_display_set_resolution(sdl.disp, w, h);   /* invalidates everything for the blank texture */
    return true;
}

/* -------------------------------------------------------------------------------------- */
/* Input                                                                                  */
/* -------------------------------------------------------------------------------------- */

static void mouse_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = sdl.ptr.x;
    data->point.y = sdl.ptr.y;
    data->state = sdl.ptr.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Pointer from SDL_GetMouseState, in window points. Not the event x/y: points in SDL2 but
 * renderer pixels in sdl2-compat (Homebrew, recent Linux distros). Clamped to the display, SDL
 * keeps reporting a pointer dragged out of the window. */
static void read_pointer(void)
{
    int x, y;
    SDL_GetMouseState(&x, &y);
    sdl.ptr.x = LV_CLAMP(0, (int32_t)(x * sdl.scale + 0.5f), sdl.draw_w - 1);
    sdl.ptr.y = LV_CLAMP(0, (int32_t)(y * sdl.scale + 0.5f), sdl.draw_h - 1);
}

static void key_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->key = sdl.kb.key;
    data->state = sdl.kb.state;
}

/* LVGL sends a key on press and needs a release before the next */
static void key_send(uint32_t key)
{
    sdl.kb.key = key;
    sdl.kb.state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(sdl.keyboard);
    sdl.kb.state = LV_INDEV_STATE_RELEASED;
    lv_indev_read(sdl.keyboard);
}

/* One key per character. lv_textarea reads the UTF-8 bytes back from the key word, so copy
 * them in memory order, no decoding. */
static void type_text(const char *s)
{
    while (*s) {
        size_t n = 1;
        while (n < 4 && ((unsigned char)s[n] & 0xC0) == 0x80) n++;
        uint32_t key = 0;
        memcpy(&key, s, n);
        key_send(key);
        s += n;
    }
}

/* True when a text field has focus, Space then types a space */
static bool typing(void)
{
    lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
    return focused != NULL && lv_obj_check_type(focused, &lv_textarea_class);
}

/* Keys that type nothing; letters and digits come as SDL_TEXTINPUT */
static uint32_t key_of(const SDL_Keysym *k)
{
    switch (k->sym) {
    /* Space activates the focused control, like any desktop */
    case SDLK_SPACE:     return typing() ? 0 : LV_KEY_ENTER;
    case SDLK_BACKSPACE: return LV_KEY_BACKSPACE;
    case SDLK_DELETE:    return LV_KEY_DEL;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:  return LV_KEY_ENTER;
    case SDLK_ESCAPE:    return LV_KEY_ESC;
    case SDLK_LEFT:      return LV_KEY_LEFT;
    case SDLK_RIGHT:     return LV_KEY_RIGHT;
    case SDLK_UP:        return LV_KEY_UP;
    case SDLK_DOWN:      return LV_KEY_DOWN;
    case SDLK_HOME:      return LV_KEY_HOME;
    case SDLK_END:       return LV_KEY_END;
    case SDLK_TAB:       return (k->mod & KMOD_SHIFT) ? LV_KEY_PREV : LV_KEY_NEXT;
    case SDLK_PAGEUP:    return LV_KEY_PREV;
    case SDLK_PAGEDOWN:  return LV_KEY_NEXT;
    default:             return 0;
    }
}

static void key_down(const SDL_Keysym *k)
{
    /* Chords give no SDL_TEXTINPUT, so paste is done here. First line only, a line break
     * would press Enter and a tab move focus. */
    if ((k->mod & (KMOD_CTRL | KMOD_GUI)) && k->sym == SDLK_v) {
        char *clip = SDL_GetClipboardText();
        if (clip) {
            clip[strcspn(clip, "\t\r\n")] = '\0';
            type_text(clip);
        }
        SDL_free(clip);
        return;
    }
    uint32_t key = key_of(k);
    if (key) key_send(key);
}

/* Scroll the nearest object under the pointer that has something to scroll */
static void scroll_wheel(float steps)
{
    lv_point_t p = { sdl.ptr.x, sdl.ptr.y };

    for (lv_obj_t *obj = lv_indev_search_obj(lv_screen_active(), &p); obj != NULL;
         obj = lv_obj_get_parent(obj)) {
        if (lv_obj_is_scrollable(obj)
            && (lv_obj_get_scroll_top(obj) > 0 || lv_obj_get_scroll_bottom(obj) > 0)) {
            lv_obj_scroll_by_bounded(obj, 0, (int32_t)(steps * 40.0f * sdl.scale), LV_ANIM_OFF);
            return;
        }
    }
}

/* Hidden: stop drawing. Shown again: redraw everything. LVGL counts enable/disable calls and
 * SDL can report a minimised window as hidden too, so only act on a change. */
static void set_in_sight(bool in_sight)
{
    static bool hidden;

    if (hidden == !in_sight) return;
    hidden = !in_sight;
    lv_display_enable_invalidation(sdl.disp, in_sight);
    if (in_sight) lv_obj_invalidate(lv_screen_active());
}

/* True when the window is closing */
static bool handle_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_QUIT:
        return true;
    case SDL_WINDOWEVENT:
        switch (e->window.event) {
        case SDL_WINDOWEVENT_CLOSE:
            return true;
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            /* Redraw during the drag instead of stretching the old frame */
            if (resize_display()) lv_refr_now(sdl.disp);
            break;
        case SDL_WINDOWEVENT_MINIMIZED:
        case SDL_WINDOWEVENT_HIDDEN:
            set_in_sight(false);
            break;
        case SDL_WINDOWEVENT_RESTORED:
        case SDL_WINDOWEVENT_SHOWN:
            set_in_sight(true);
            break;
        case SDL_WINDOWEVENT_EXPOSED:
            present();
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:   /* switched away mid-drag */
            if (!sdl.ptr.pressed) break;
            sdl.ptr.pressed = false;
            lv_indev_read(sdl.mouse);
            break;
        }
        break;
    case SDL_MOUSEMOTION:
        read_pointer();
        sdl.ptr.moved = true;   /* read once per batch, after the loop */
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        if (e->button.button != SDL_BUTTON_LEFT) break;
        read_pointer();
        sdl.ptr.pressed = e->type == SDL_MOUSEBUTTONDOWN;
        lv_indev_read(sdl.mouse);   /* now, so a press and release in one batch both count */
        sdl.ptr.moved = false;
        break;
    case SDL_MOUSEWHEEL:
        scroll_wheel(e->wheel.preciseY);
        break;
    case SDL_TEXTINPUT:
        /* Outside a field, Space already pressed the control in key_of */
        if (strcmp(e->text.text, " ") == 0 && !typing()) break;
        type_text(e->text.text);
        break;
    case SDL_KEYDOWN:
        /* Held Enter or Space fires once, or Start and Stop would alternate. Backspace and arrows
         * repeat */
        if (e->key.repeat && key_of(&e->key.keysym) == LV_KEY_ENTER) break;
        key_down(&e->key.keysym);
        break;
    }
    return false;
}

/* -------------------------------------------------------------------------------------- */
/* Setting up and running                                                                 */
/* -------------------------------------------------------------------------------------- */

static lv_indev_t *create_indev(lv_indev_type_t type, lv_indev_read_cb_t read_cb)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, type);
    lv_indev_set_read_cb(indev, read_cb);
    lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    return indev;
}

bool platform_init(const char *title, int32_t w, int32_t h, int32_t min_w, int32_t min_h)
{
    /* Windows: sizes in points and drawable in pixels, as on macOS. Set before SDL_Init. By
     * name since SDL_HINT_WINDOWS_DPI_SCALING is SDL 2.24+, older SDL ignores it. */
    SDL_SetHint("SDL_WINDOWS_DPI_SCALING", "1");
    /* SDL blocks the screensaver by default, not wanted for a meter left running for hours */
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    /* The click that focuses the window also counts */
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return sdl_error("SDL");

    lv_init();
    lv_tick_set_cb(SDL_GetTicks);
    lv_delay_set_cb(SDL_Delay);

    /* Hidden until platform_run draws into it. Usable bounds exclude menu bar, dock and
     * taskbar. */
    SDL_Rect u;
    if (SDL_GetDisplayUsableBounds(0, &u) != 0) u = (SDL_Rect){ 0, 0, 1280, 720 };
    sdl.window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  LV_MIN(w, u.w), LV_MIN(h, u.h),
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN);
    if (!sdl.window) return sdl_error("window");
    SDL_SetWindowMinimumSize(sdl.window, LV_MIN(min_w, u.w), LV_MIN(min_h, u.h));

    /* No vsync: LVGL paces frames and redraws only changes, and waiting for vblank every
     * frame would delay mouse and keyboard */
    sdl.renderer = SDL_CreateRenderer(sdl.window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl.renderer) sdl.renderer = SDL_CreateRenderer(sdl.window, -1, 0);
    if (!sdl.renderer) return sdl_error("renderer");

    int ww, wh;
    SDL_GetWindowSize(sdl.window, &ww, &wh);
    SDL_GetRendererOutputSize(sdl.renderer, &sdl.draw_w, &sdl.draw_h);
    sdl.scale = (float)sdl.draw_w / (float)ww;
    if (!create_texture()) return false;

    sdl.buf = malloc(PARTIAL_BUF_BYTES);
    if (!sdl.buf) {
        fputs("out of memory\n", stderr);
        return false;
    }

    sdl.disp = lv_display_create(sdl.draw_w, sdl.draw_h);
    /* Set the format before the buffers, the stride depends on it */
    lv_display_set_color_format(sdl.disp, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_dpi(sdl.disp, dpi());
    lv_display_set_buffers(sdl.disp, sdl.buf, NULL, PARTIAL_BUF_BYTES,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(sdl.disp, flush_cb);

    /* Controls created from here on join the default group, driven by the keyboard */
    sdl.mouse = create_indev(LV_INDEV_TYPE_POINTER, mouse_read);
    sdl.keyboard = create_indev(LV_INDEV_TYPE_KEYPAD, key_read);
    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(sdl.keyboard, group);
    SDL_StartTextInput();
    return true;
}

int platform_run(void (*on_wake)(void))
{
    /* Draw before showing, so the window never shows empty */
    lv_refr_now(sdl.disp);
    SDL_ShowWindow(sdl.window);

    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_USEREVENT) {
                on_wake();
            } else if (handle_event(&e)) {
                /* Hide right away while the program shuts down */
                SDL_HideWindow(sdl.window);
                return 0;
            }
        }
        if (sdl.ptr.moved) {
            sdl.ptr.moved = false;
            lv_indev_read(sdl.mouse);
        }
        /* NULL event leaves it queued for the next round. SDL only notices Ctrl+C and kill when
         * it polls events, so wake at least twice a second. */
        uint32_t wait = lv_timer_handler();
        SDL_WaitEventTimeout(NULL, (int)LV_MIN(wait, 500u));
    }
}

void platform_close(void)
{
    if (sdl.hand) SDL_FreeCursor(sdl.hand);
    SDL_DestroyTexture(sdl.texture);
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
    free(sdl.buf);
}

void platform_wake(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_USEREVENT;
    /* Dropped if the queue is full, the next one does the job */
    SDL_PushEvent(&e);
}

float platform_scale(void)
{
    return sdl.scale;
}

/* -------------------------------------------------------------------------------------- */
/* The rest of the system                                                                 */
/* -------------------------------------------------------------------------------------- */

void platform_set_clipboard(const char *text)
{
    if (SDL_SetClipboardText(text) != 0) sdl_error("clipboard");
}

bool platform_open_url(const char *url)
{
#if !defined(PJ_MACOS) && !defined(PJ_WINDOWS)
    /* xdg-open would start the browser as root */
    if (geteuid() == 0) return false;
#endif
    return SDL_OpenURL(url) == 0;
}

void platform_set_hand_cursor(bool hand)
{
    if (hand && !sdl.hand) sdl.hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    SDL_SetCursor(hand && sdl.hand ? sdl.hand : SDL_GetDefaultCursor());
}

/* Only blocks idle system sleep, the screen can still turn off. On Linux that needs
 * logind's inhibitor over D-Bus, not linked; see the README. */
void platform_keep_awake(bool on)
{
#ifdef PJ_MACOS
    static IOPMAssertionID held = kIOPMNullAssertionID;

    if (on && held == kIOPMNullAssertionID) {
        IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleSystemSleep, kIOPMAssertionLevelOn,
                                    CFSTR(JOULARENERGYMETER_NAME " is measuring"), &held);
    } else if (!on && held != kIOPMNullAssertionID) {
        IOPMAssertionRelease(held);
        held = kIOPMNullAssertionID;
    }
#elif defined(PJ_WINDOWS)
    SetThreadExecutionState(on ? ES_CONTINUOUS | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
#else
    (void)on;
#endif
}
