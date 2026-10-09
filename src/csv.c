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

/* Same CSV as PowerJoular (columns, four decimals, Unix timestamps) */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PJ_WINDOWS
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "app.h"

#define DECIMALS 4

static const char SYSTEM_HEADER[] = "Timestamp,CPU Usage,Total Power,CPU Power,GPU Power";
static const char TARGET_HEADER[] = "Timestamp,CPU Usage,CPU Power";

static bool recording;
static target recorded;
static char system_path[4096];
static char target_path[sizeof system_path + sizeof recorded.app + 8];   /* "-<app>.csv" appended */

static char trouble[sizeof target_path + 64];

/* Call right after the failure, while errno is still set. Reason first, so a long path is
 * what gets truncated. */
static bool cannot_write(const char *name)
{
    snprintf(trouble, sizeof trouble, "%s: cannot write to %s",
             errno == ELOOP || errno == ENXIO ? "A link, or not a plain file" : strerror(errno), name);
    return false;
}

/* Runs as root, so only write to a regular file with a single link, never through a symlink
 * that could point anywhere. Files created under sudo are chowned to SUDO_UID/SUDO_GID. */
static FILE *open_csv(const char *name, bool append)
{
#ifdef PJ_WINDOWS
    return fopen(name, append ? "a" : "w");
#else
    int flags = O_WRONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC | (append ? O_APPEND : 0);
    int fd = open(name, flags | O_CREAT | O_EXCL, 0644);
    bool created = fd >= 0;
    const char *uid = getenv("SUDO_UID"), *gid = getenv("SUDO_GID");
    struct stat st;
    FILE *f = NULL;

    if (fd < 0 && errno == EEXIST)
        fd = open(name, flags);
    if (fd < 0)
        return NULL;

    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1)
        errno = ELOOP;   /* reported as a link */
    else if (append || ftruncate(fd, 0) == 0)
        f = fdopen(fd, append ? "a" : "w");

    if (f == NULL) {
        int reason = errno;

        close(fd);
        errno = reason;
        return NULL;
    }

    if (created && geteuid() == 0 && uid != NULL && gid != NULL)
        (void)!fchown(fd, (uid_t)atol(uid), (gid_t)atol(gid));
    return f;
#endif
}

const char *csv_target_file(const char *system_file, const target *t, char *out, size_t n)
{
    char app[sizeof t->app];
    size_t i;

    switch (t->kind) {
    case TARGET_PROCESS:
        snprintf(out, n, "%s-%u.csv", system_file, t->pid);
        break;

    case TARGET_APPLICATION:
        /* Goes into a file name, so replace path separators and characters Windows rejects */
        for (i = 0; i < sizeof app - 1 && t->app[i] != '\0'; i++) {
            unsigned char c = (unsigned char)t->app[i];

            app[i] = (c < 32 || strchr("/\\:*?\"<>|", c) != NULL) ? '_' : (char)c;
        }
        app[i] = '\0';
        snprintf(out, n, "%s-%s.csv", system_file, app);
        break;

    default:
        snprintf(out, n, "%s", system_file);
        break;
    }

    return out;
}

static const char *system_row(const history_point *p, char *buf, size_t n)
{
    char usage[32], total[32], cpu[32], gpu[32];

    snprintf(buf, n, "%lld,%s,%s,%s,%s", (long long)p->taken,
             fmt_number(usage, sizeof usage, p->data.cpu_usage, DECIMALS),
             fmt_number(total, sizeof total, p->data.total_power, DECIMALS),
             fmt_number(cpu, sizeof cpu, p->data.cpu_power, DECIMALS),
             fmt_number(gpu, sizeof gpu, p->data.gpu_power, DECIMALS));
    return buf;
}

static const char *target_row(const history_point *p, char *buf, size_t n)
{
    char usage[32], power[32];

    snprintf(buf, n, "%lld,%s,%s", (long long)p->taken,
             fmt_number(usage, sizeof usage, p->data.target_usage, DECIMALS),
             fmt_number(power, sizeof power, p->data.target_power, DECIMALS));
    return buf;
}

/* Append row, writing header first if the file is new or empty. Closed after each row so
 * rows survive a crash and the file can be read meanwhile. NULL row just creates the file. */
static bool append_row(const char *name, const char *header, const char *row)
{
    FILE *f = open_csv(name, true);
    bool ok = true;

    if (f == NULL)
        return false;

    if (fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0)
        ok = fprintf(f, "%s\n", header) > 0;

    if (ok && row != NULL)
        ok = fprintf(f, "%s\n", row) > 0;

    return fclose(f) == 0 && ok;
}

bool csv_start_recording(const char *system_file, const target *t)
{
    csv_stop_recording();
    trouble[0] = '\0';

    /* Don't let a truncated name turn into another file's name */
    if (snprintf(system_path, sizeof system_path, "%s", system_file) >= (int)sizeof system_path) {
        errno = ENAMETOOLONG;
        return cannot_write(system_file);
    }

    csv_target_file(system_file, t, target_path, sizeof target_path);

    if (!append_row(system_path, SYSTEM_HEADER, NULL))
        return cannot_write(system_path);

    if (t->kind != TARGET_WHOLE_SYSTEM && !append_row(target_path, TARGET_HEADER, NULL))
        return cannot_write(target_path);

    recorded = *t;
    recording = true;
    return true;
}

bool csv_record_point(const history_point *p)
{
    char row[256];

    if (!recording)
        return true;

    if (!append_row(system_path, SYSTEM_HEADER, system_row(p, row, sizeof row)))
        return cannot_write(system_path);

    if (recorded.kind != TARGET_WHOLE_SYSTEM
        && !append_row(target_path, TARGET_HEADER, target_row(p, row, sizeof row)))
        return cannot_write(target_path);

    return true;
}

void csv_stop_recording(void)
{
    recording = false;
}

bool csv_is_recording(void)
{
    return recording;
}

const char *csv_recording_file(void)
{
    return recording ? system_path : "";
}

/* New file with header and all kept points, gaps skipped. of_target picks the target
 * columns. Returns rows written, or -1. */
static int write_file(const char *name, bool of_target)
{
    FILE *f = open_csv(name, false);
    char row[256];
    int written = 0;
    bool ok;

    if (f == NULL)
        return -1;

    ok = fprintf(f, "%s\n", of_target ? TARGET_HEADER : SYSTEM_HEADER) > 0;

    for (int i = 1; ok && i <= history_count(); i++) {
        const history_point *p = history_get(i);

        if (p->taken == 0)
            continue;
        ok = fprintf(f, "%s\n", of_target ? target_row(p, row, sizeof row)
                                          : system_row(p, row, sizeof row)) > 0;
        written++;
    }

    return fclose(f) == 0 && ok ? written : -1;
}

int csv_save_history(const char *system_file, const target *t)
{
    char target_file[sizeof target_path];
    int written;

    trouble[0] = '\0';

    written = write_file(system_file, false);
    if (written < 0) {
        cannot_write(system_file);
        return -1;
    }

    if (t->kind != TARGET_WHOLE_SYSTEM
        && write_file(csv_target_file(system_file, t, target_file, sizeof target_file), true) < 0) {
        cannot_write(target_file);
        return -1;
    }

    return written;
}

const char *csv_trouble(void)
{
    return trouble;
}

const char *csv_file_name(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef PJ_WINDOWS
    const char *back = strrchr(path, '\\');
    if (back != NULL && (slash == NULL || back > slash)) slash = back;
#endif
    return slash ? slash + 1 : path;
}

bool csv_folder_of(const char *file, char *out, size_t n)
{
    char full[PATH_MAX];
    size_t len;

#ifdef PJ_WINDOWS
    if (_fullpath(full, file, sizeof full) == NULL) return false;
#else
    if (realpath(file, full) == NULL) return false;
#endif
    len = (size_t)(csv_file_name(full) - full);
    /* Drop the trailing separator unless it's the root */
    if (len > 1) len--;
    snprintf(out, n, "%.*s", (int)len, full);
    return true;
}
