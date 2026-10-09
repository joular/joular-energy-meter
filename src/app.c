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

/* Target names and the sampling interval */

#include <stdio.h>

#include "app.h"

static interval_choice interval = SEC_1;

const char *target_name(const target *t, char *buf, size_t n)
{
    if (t->kind == TARGET_PROCESS)
        snprintf(buf, n, "Process %u", t->pid);
    else
        snprintf(buf, n, "%s", t->kind == TARGET_APPLICATION ? t->app : "");

    return buf;
}

double interval_seconds(interval_choice c)
{
    static const double seconds[INTERVAL_COUNT] = {0.25, 0.5, 0.75, 1.0, 2.0, 3.0, 4.0, 5.0, 10.0};

    return seconds[c];
}

const char *interval_label(interval_choice c)
{
    static const char *const labels[INTERVAL_COUNT] = {
        "250 ms", "500 ms", "750 ms", "1 s", "2 s", "3 s", "4 s", "5 s", "10 s"
    };

    return labels[c];
}

interval_choice chosen_interval(void)
{
    return interval;
}

void set_interval(interval_choice c)
{
    if ((unsigned)c < INTERVAL_COUNT)
        interval = c;
}
