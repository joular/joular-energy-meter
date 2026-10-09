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

/* Measuring thread. Only it calls the monitor, so a slow reading never blocks the window.
 * The window sets requests under the lock, the thread acts on them with the lock released
 * and queues reports for the main loop to hand to the window. */

#include <stdio.h>

#include <SDL2/SDL.h>

#include "app.h"

/* Pending reports, a minute at 250 ms while the main loop is blocked (window dragged on
 * Windows, menu open on macOS). Oldest dropped when full */
#define QUEUE_SIZE 256

typedef struct queued {
    unsigned session;
    report r;
} queued;

static struct {
    SDL_mutex *lock;
    SDL_cond *asked;   /* signalled on every request from the window */
    SDL_Thread *thread;
    void (*on_report)(const report *r);

    /* Requests from the window. Each start and stop bumps the session, so stale reports
     * are dropped. */
    unsigned session;
    bool measuring, paused, quit;
    target followed;
    double interval;
    double longest;   /* longest interval asked for since the last cycle */

    queued queue[QUEUE_SIZE];
    int first, count;
} s;

/* Under the lock */
static void post(unsigned session, const report *r)
{
    if (s.count == QUEUE_SIZE) {
        s.first = (s.first + 1) % QUEUE_SIZE;
        s.count--;
    }

    s.queue[(s.first + s.count) % QUEUE_SIZE] = (queued){ session, *r };
    s.count++;
    platform_wake();
}

/* One action per turn, lock released while the monitor runs, then check the requests
 * again as they may have changed. */
static int measure(void *unused)
{
    unsigned started = 0;   /* last session started here, they count from 1 */
    bool open = false;      /* hardware open for it */
    bool paused = false;
    uint64_t last = 0;      /* start or last cycle, in SDL ticks */

    (void)unused;
    SDL_LockMutex(s.lock);

    for (;;) {
        uint64_t now = SDL_GetTicks64();
        uint64_t due = last + (uint64_t)(s.interval * 1000.0);

        if (open && (!s.measuring || s.quit || s.session != started)) {
            SDL_UnlockMutex(s.lock);
            monitor_stop();
            SDL_LockMutex(s.lock);
            open = false;
        } else if (s.quit) {
            break;
        } else if (s.measuring && s.session != started) {
            report r = { .kind = REPORT_STARTED };
            target t = s.followed;
            double interval = s.interval;

            started = s.session;
            SDL_UnlockMutex(s.lock);
            monitor_start(&t, interval);
            r.has_cpu = monitor_cpu_available();
            r.has_gpu = monitor_gpu_available();
            r.load_readable = monitor_load_readable();
            snprintf(r.trouble, sizeof r.trouble, "%s", monitor_trouble());
            SDL_LockMutex(s.lock);

            open = true;
            paused = false;
            last = SDL_GetTicks64();
            post(started, &r);
        } else if (open && s.paused != paused) {
            double interval = s.interval;

            paused = s.paused;
            SDL_UnlockMutex(s.lock);
            if (paused)
                monitor_pause();
            else
                monitor_resume(interval);
            SDL_LockMutex(s.lock);

            /* Wait a full interval before the next cycle, as at start */
            last = SDL_GetTicks64();
        } else if (open && !paused && now >= due) {
            report r = { .kind = REPORT_FAILED };
            /* Longest interval set during the cycle, it waited that long */
            double interval = s.interval > s.longest ? s.interval : s.longest;
            step_result result;

            /* Stay on the grid unless half an interval late (long cycle, sleep, new interval) */
            last = 2 * (now - due) < due - last ? due : now;
            s.longest = 0.0;
            SDL_UnlockMutex(s.lock);
            result = monitor_step(interval, &r.data);
            r.taken = fmt_now();
            r.sum = monitor_session();
            snprintf(r.trouble, sizeof r.trouble, "%s", monitor_trouble());
            SDL_LockMutex(s.lock);

            if (result == STEP_MEASURED)
                r.kind = REPORT_MEASURED;
            else if (result == STEP_SKIPPED)
                r.kind = REPORT_SKIPPED;
            post(started, &r);
        } else if (open && !paused) {
            SDL_CondWaitTimeout(s.asked, s.lock, (Uint32)(due - now));
        } else {
            SDL_CondWait(s.asked, s.lock);
        }
    }

    SDL_UnlockMutex(s.lock);
    return 0;
}

bool sampler_init(void (*on_report)(const report *r))
{
    s.on_report = on_report;
    s.lock = SDL_CreateMutex();
    s.asked = SDL_CreateCond();

    /* 8 MB stack like a main thread, the Ada libraries expect it */
    if (s.lock != NULL && s.asked != NULL)
        s.thread = SDL_CreateThreadWithStackSize(measure, "measuring", 8u * 1024 * 1024, NULL);

    if (s.thread != NULL)
        return true;

    fprintf(stderr, "measuring thread: %s\n", SDL_GetError());
    return false;
}

/* Lock held on entry: wake the thread and unlock */
static void ask(void)
{
    SDL_CondSignal(s.asked);
    SDL_UnlockMutex(s.lock);
}

void sampler_quit(void)
{
    SDL_LockMutex(s.lock);
    s.quit = true;
    ask();

    SDL_WaitThread(s.thread, NULL);
    SDL_DestroyCond(s.asked);
    SDL_DestroyMutex(s.lock);
}

void sampler_start(const target *t, double interval)
{
    SDL_LockMutex(s.lock);
    s.session++;
    s.followed = *t;
    s.interval = interval;
    s.measuring = true;
    s.paused = false;
    ask();
}

void sampler_stop(void)
{
    SDL_LockMutex(s.lock);
    s.session++;
    s.measuring = false;
    s.paused = false;
    ask();
}

void sampler_pause(bool paused)
{
    SDL_LockMutex(s.lock);
    s.paused = paused;
    ask();
}

void sampler_set_interval(double interval)
{
    SDL_LockMutex(s.lock);
    s.interval = interval;
    if (interval > s.longest) s.longest = interval;
    ask();
}

void sampler_deliver(void)
{
    for (;;) {
        report r;
        bool found = false;

        SDL_LockMutex(s.lock);

        while (s.count > 0 && !found) {
            const queued *q = &s.queue[s.first];

            s.first = (s.first + 1) % QUEUE_SIZE;
            s.count--;

            if (q->session == s.session) {
                r = q->r;
                found = true;
            }
        }

        SDL_UnlockMutex(s.lock);

        if (!found)
            return;

        /* Outside the lock, the callback may call back into the sampler */
        s.on_report(&r);
    }
}
