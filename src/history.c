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

/* Ring buffer of the last measurements */

#include <math.h>

#include "app.h"

static history_point points[HISTORY_CAPACITY];
static int oldest;   /* index of the oldest point */
static int kept;

void history_clear(void)
{
    kept = 0;
    oldest = 0;
}

void history_append(const history_point *p)
{
    points[(oldest + kept) % HISTORY_CAPACITY] = *p;

    if (kept < HISTORY_CAPACITY)
        kept++;
    else
        oldest = (oldest + 1) % HISTORY_CAPACITY;   /* overwrote the oldest */
}

int history_count(void)
{
    return kept;
}

const history_point *history_get(int i)
{
    if (i < 1 || i > kept)
        return NULL;

    return &points[(oldest + i - 1) % HISTORY_CAPACITY];
}

double history_span(void)
{
    return HISTORY_CAPACITY * interval_seconds(chosen_interval());
}

double history_value(const history_point *p, series s)
{
    switch (s) {
    case SERIES_CPU:
        return p->data.cpu_power;
    case SERIES_GPU:
        return p->data.gpu_power;
    case SERIES_TARGET:
        return p->data.target_power;
    default:
        return p->data.total_power;
    }
}

double history_peak(series s)
{
    double highest = 0.0;

    for (int i = 1; i <= kept; i++)
        highest = fmax(highest, history_value(history_get(i), s));

    return highest;
}
