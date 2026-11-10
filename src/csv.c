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

#ifdef PJ_WINDOWS
#include <windows.h>
#define strcasecmp _stricmp
#else
#include <fcntl.h>
#include <strings.h>
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
static FILE *system_out, *target_out;   /* open for the session, target_out when following */

static char trouble[sizeof target_path + 64];

/* Call right after the failure, while errno is still set. Reason first, so a long path is
 * what gets truncated. */
static bool cannot_write(const char *name)
{
    snprintf(trouble, sizeof trouble, "%s: cannot write to %s",
             errno == ELOOP || errno == ENXIO ? "A link, or not a plain file" : strerror(errno), name);
    return false;
}

#ifdef PJ_WINDOWS
/* Paths are UTF-8, the ANSI calls would read them in the local code page */
bool wide(const char *path, wchar_t *out, int n)
{
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, out, n) > 0;
}
#else
bool sudo_user(uid_t *uid, gid_t *gid)
{
    const char *u = getenv("SUDO_UID"), *g = getenv("SUDO_GID");

    if (geteuid() != 0 || u == NULL || g == NULL || atol(u) <= 0) return false;
    *uid = (uid_t)atol(u);
    *gid = (gid_t)atol(g);
    return true;
}
#endif

FILE *open_user_file(const char *name, bool append)
{
#ifdef PJ_WINDOWS
    wchar_t w[PATH_MAX];

    if (!wide(name, w, PATH_MAX)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    return _wfopen(w, append ? L"a" : L"w");
#else
    /* No O_TRUNC: it would empty a hard-linked file before the checks below */
    int flags = O_WRONLY | (append ? O_APPEND : 0) | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;
    int fd = open(name, flags | O_CREAT | O_EXCL, 0644);
    bool created = fd >= 0;
    uid_t uid;
    gid_t gid;
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

    if (created && sudo_user(&uid, &gid)) (void)!fchown(fd, uid, gid);
    return f;
#endif
}

/* Goes into a file name, so replace path separators and characters Windows rejects */
static void file_safe(const char *name, size_t len, char *out, size_t n)
{
    size_t i;

    for (i = 0; i < n - 1 && i < len && name[i] != '\0'; i++) {
        unsigned char c = (unsigned char)name[i];

        out[i] = (c < 32 || strchr("/\\:*?\"<>|", c) != NULL) ? '_' : (char)c;
    }
    out[i] = '\0';
}

const char *csv_target_file(const char *system_file, const target *t, char *out, size_t n)
{
    char name[sizeof t->app];
    const char *first, *end;
    size_t stem = strlen(system_file);

    /* power.csv gives power-144.csv: the name goes before a final .csv, as in PowerJoular */
    if (stem > 4 && strcasecmp(system_file + stem - 4, ".csv") == 0)
        stem -= 4;

    switch (t->kind) {
    case TARGET_PROCESS:
        snprintf(out, n, "%.*s-%u.csv", (int)stem, system_file, t->pid);
        break;

    case TARGET_APPLICATION:
        file_safe(t->app, sizeof name, name, sizeof name);
        snprintf(out, n, "%.*s-%s.csv", (int)stem, system_file, name);
        break;

    case TARGET_COMMAND:
        /* The program's name: its first word, without its folder */
        first = t->command + strspn(t->command, " ");
        end = first + strcspn(first, " ");
        for (const char *c = first; c < end; c++)
            if (*c == '/' || *c == '\\') first = c + 1;
        file_safe(first, (size_t)(end - first), name, sizeof name);
        snprintf(out, n, "%.*s-%s.csv", (int)stem, system_file, name);
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

/* A line, flushed at once so rows survive a crash and the file can be read meanwhile */
static bool write_line(FILE *f, const char *line)
{
    return fprintf(f, "%s\n", line) > 0 && fflush(f) == 0;
}

/* Opens the file for the session's rows, with the header first if it is new or empty */
static FILE *open_rows(const char *name, const char *header)
{
    FILE *f = open_user_file(name, true);

    if (f != NULL && fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0 && !write_line(f, header)) {
        int reason = errno;

        fclose(f);
        errno = reason;
        return NULL;
    }
    return f;
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

    system_out = open_rows(system_path, SYSTEM_HEADER);
    if (system_out == NULL)
        return cannot_write(system_path);

    if (t->kind != TARGET_WHOLE_SYSTEM && (target_out = open_rows(target_path, TARGET_HEADER)) == NULL) {
        cannot_write(target_path);
        csv_stop_recording();
        return false;
    }

    recorded = *t;
    recording = true;
    return true;
}

bool csv_record_point(const history_point *p)
{
    char row[256];

    if (!recording)
        return true;

    if (!write_line(system_out, system_row(p, row, sizeof row)))
        return cannot_write(system_path);

    if (recorded.kind != TARGET_WHOLE_SYSTEM && !write_line(target_out, target_row(p, row, sizeof row)))
        return cannot_write(target_path);

    return true;
}

void csv_stop_recording(void)
{
    recording = false;
    if (system_out != NULL) fclose(system_out);
    if (target_out != NULL) fclose(target_out);
    system_out = target_out = NULL;
}

bool csv_is_recording(void)
{
    return recording;
}

const char *csv_recording_file(void)
{
    return recording ? system_path : "";
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

