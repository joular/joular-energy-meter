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

/* Hardware side: opens Joular Core, samples CPU load, converts readings to watts and sums
 * the session energy. No LVGL here, measuring thread only. */

/* QueryUnbiasedInterruptTime is Windows 7 or newer */
#if defined(PJ_WINDOWS) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0601
#endif

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#ifdef PJ_WINDOWS
#include <windows.h>
#else
#include <signal.h>
#include <time.h>
#endif

#include "cpuload.h"
#include "joularcore.h"

#include "app.h"

static target followed;
static bool has_cpu, has_gpu, load_reads;

static bool running, paused;

/* Set when the followed process ended: a reused PID is another program */
static bool gone;

/* Baseline CPU sample for the next cycle, and when it was taken on both clocks */
static cpuload_sample before;
static double taken_before, wall_before;

static totals consumed;
static char trouble[128];

/* Monotonic seconds that stop while the machine sleeps: CLOCK_MONOTONIC on Linux and FreeBSD,
 * CLOCK_UPTIME_RAW on macOS (its CLOCK_MONOTONIC runs on), unbiased interrupt time on Windows */
static double now(void)
{
#ifdef PJ_WINDOWS
    ULONGLONG t;

    QueryUnbiasedInterruptTime(&t);
    return (double)t / 1e7;
#else
    struct timespec t;

#ifdef PJ_MACOS
    clock_gettime(CLOCK_UPTIME_RAW, &t);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
#endif
}

/* Wall-clock seconds, which run on through a sleep */
static double wall(void)
{
#ifdef PJ_WINDOWS
    FILETIME ft;
    ULARGE_INTEGER t;

    GetSystemTimeAsFileTime(&ft);
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;
    return (double)t.QuadPart / 1e7;
#else
    struct timespec t;

    clock_gettime(CLOCK_REALTIME, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
#endif
}

/* No such process any more, so a later reading of the PID would be another program's. One
 * that runs but can't be read (another user's, on macOS and Windows) isn't gone. */
static bool absent(unsigned int pid)
{
#ifdef PJ_WINDOWS
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

    if (h == NULL) return GetLastError() == ERROR_INVALID_PARAMETER;
    CloseHandle(h);
    return false;
#else
    return pid > INT_MAX || (kill((pid_t)pid, 0) != 0 && errno == ESRCH);
#endif
}

/* Some hardware gives joules since the last read, some gives watts */
static double watts(const joularcore_measurement *m, double over)
{
    if (!m->available)
        return 0.0;

    if (m->unit == 1)
        return m->value;

    return over > 0.0 ? m->value / over : 0.0;
}

/* Machine counters read once, target measured against the same sample */
static void take_sample(cpuload_sample *out)
{
    cpuload_sample machine;

    cpuload_take_system(&machine);

    switch (followed.kind) {
    case TARGET_PROCESS:
        cpuload_take_pid_with(followed.pid, &machine, out);
        break;
    case TARGET_APPLICATION:
        cpuload_take_app_with(followed.app, &machine, out);
        break;
    default:
        *out = machine;
        break;
    }
}

/* Same order as monitor_step, so the CPU sample and the power reading cover the same time */
static void take_baseline(joularcore_reading *reading)
{
    take_sample(&before);
    taken_before = now();
    wall_before = wall();
    joularcore_read(reading);
}

void monitor_start(const target *t)
{
    joularcore_reading first;

    if (running)
        return;

    followed = *t;
    gone = false;
    consumed = (totals){0};
    trouble[0] = '\0';

    /* Doesn't fail from C, unreadable sources just show as not available */
    joularcore_open(1, 1);
    take_baseline(&first);

    has_cpu = first.cpu.available != 0;
    has_gpu = first.gpu.available != 0;

    /* CPU Load gives zero machine time when it can't read the counters */
    load_reads = before.total != 0;

    running = true;
    paused = false;
}

void monitor_stop(void)
{
    running = false;
    paused = false;

    /* Safe to call always, Joular Core knows if there's anything to release */
    joularcore_close();
}

void monitor_pause(void)
{
    if (running)
        paused = true;
}

void monitor_resume(void)
{
    /* Energy read across the pause, discarded */
    joularcore_reading discarded;
    cpuload_sample paused_at = before;

    if (!running || !paused)
        return;

    paused = false;
    take_baseline(&discarded);

    /* CPU time only grows, so less means the PID was reused */
    if (followed.kind == TARGET_PROCESS && before.used < paused_at.used)
        gone = true;
}

step_result monitor_step(cycle *out)
{
    cpuload_sample previous = before, after;
    joularcore_reading reading;
    double taken_after, wall_after, elapsed, slept;

    *out = (cycle){0};

    if (!running || paused)
        return STEP_FAILED;

    /* A reason only lasts until the next step */
    trouble[0] = '\0';
    take_sample(&after);
    taken_after = now();
    wall_after = wall();

    /* Nothing raises across C, so the only failure after a good start is a sample with no
     * machine time. Keep the baseline, the next cycle covers this one too */
    if (load_reads && after.total == 0) {
        snprintf(trouble, sizeof trouble, "CPU Load read no machine time");
        return STEP_FAILED;
    }

    joularcore_read(&reading);

    /* Real cycle length, longer than the interval under load. Only the wall clock counts a
     * sleep, so the gap between the two is how long the machine slept */
    elapsed = taken_after - taken_before;
    slept = (wall_after - wall_before) - elapsed;

    /* Next cycle's baseline, even if this one is dropped */
    before = after;
    taken_before = taken_after;
    wall_before = wall_after;

    if (slept > RESUME_SECONDS)
        return STEP_SKIPPED;

    out->cpu_usage = cpuload_system_usage(&previous, &after);
    out->cpu_power = watts(&reading.cpu, elapsed);
    out->gpu_power = watts(&reading.gpu, elapsed);
    out->total_power = out->cpu_power + out->gpu_power;

    if (followed.kind != TARGET_WHOLE_SYSTEM) {
        /* Negative load means the target couldn't be read */
        out->target_usage = cpuload_process_usage(&previous, &after);

        /* Gone for good when the process ended, or its CPU time went backwards (the PID was
         * reused, same check as on resume). CPU Load says -1 for a process it can't read, be
         * it gone or another user's: a read failure alone only costs this cycle */
        if (followed.kind == TARGET_PROCESS
            && (after.used < 0 ? absent(followed.pid) : previous.used >= 0 && after.used < previous.used))
            gone = true;
        if (gone)
            out->target_usage = UNREADABLE;
        else if (out->target_usage < 0.0)
            snprintf(trouble, sizeof trouble, followed.kind == TARGET_PROCESS
                     ? "it is running, but access is denied" : "access is denied");

        if (out->target_usage >= 0.0) consumed.target_read = true;
        if (out->target_usage < 0.0) {
            out->target_usage = UNREADABLE;
            out->target_power = UNREADABLE;
        } else if (out->cpu_usage > 0.0) {
            /* Capped, rounding in the two loads can put the share above the total */
            out->target_power = fmin(out->cpu_power,
                                     out->cpu_power * out->target_usage / out->cpu_usage);
        }
    }

    consumed.cpu_energy += out->cpu_power * elapsed;
    consumed.gpu_energy += out->gpu_power * elapsed;
    consumed.total_energy += out->total_power * elapsed;

    if (out->target_power > 0.0)
        consumed.target_energy += out->target_power * elapsed;

    /* Like the energy, dropped cycles and pauses don't count */
    consumed.elapsed += elapsed;
    return STEP_MEASURED;
}

bool monitor_cpu_available(void)
{
    return has_cpu;
}

bool monitor_gpu_available(void)
{
    return has_gpu;
}

bool monitor_load_readable(void)
{
    return load_reads;
}

totals monitor_session(void)
{
    return consumed;
}

const char *monitor_trouble(void)
{
    return trouble;
}
