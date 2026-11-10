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

/* Numbers and times as text */

#include <math.h>
#include <stdio.h>
#include <time.h>

#include "app.h"

char *fmt_number(char *buf, size_t n, double value, int decimals)
{
    if (!isfinite(value)) {
        snprintf(buf, n, "?");
        return buf;
    }

    /* %.0f rounds half to even (12.5 -> 12), we want half away from zero */
    if (decimals == 0)
        value = round(value);

    snprintf(buf, n, "%.*f", decimals, value);
    return buf;
}

int64_t fmt_now(void)
{
    return (int64_t)time(NULL);
}

char *fmt_clock(char *buf, size_t n, double seconds)
{
    long long total = seconds > 0.0 ? (long long)seconds : 0;

    snprintf(buf, n, "%02lld:%02lld:%02lld", total / 3600, (total / 60) % 60, total % 60);
    return buf;
}

char *fmt_span(char *buf, size_t n, double seconds)
{
    long long whole = seconds > 0.0 ? (long long)seconds : 0;

    if (whole >= 120)
        snprintf(buf, n, "%lld minutes", whole / 60);
    else
        snprintf(buf, n, "%lld seconds", whole);

    return buf;
}

/* Watts as a number and its unit: mW under 1 W, so a small figure never reads 0.0 */
char *fmt_power(char *buf, size_t n, double watts, const char **unit)
{
    double milli = watts * 1000.0;

    *unit = watts > 0.0 && watts < 0.9995 ? "mW" : "W";
    if (watts <= 0.0)
        snprintf(buf, n, "0");
    else if (milli < 0.05)
        snprintf(buf, n, "<0.1");
    else if (milli < 9.95)
        fmt_number(buf, n, milli, 1);
    else if (watts < 0.9995)
        fmt_number(buf, n, milli, 0);
    else
        fmt_number(buf, n, watts, 1);
    return buf;
}
