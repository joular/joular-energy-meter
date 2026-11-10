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

/* Choices kept between launches, as key=value lines. Read once at start, written at each
 * Start and on quit. */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef PJ_WINDOWS
#include <windows.h>
#else
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "app.h"

#define MAX_KEPT 24

#ifdef PJ_WINDOWS
#define SEPARATOR '\\'
#else
#define SEPARATOR '/'
#endif

static struct {
    char key[32];
    char value[4096];
} kept[MAX_KEPT];
static int count;

/* The settings file, "" when no folder was found */
static char file[4096];

#ifdef PJ_WINDOWS
/* %APPDATA%\Joular Energy Meter, UTF-8 */
static bool find_folder(char *out, size_t n)
{
    const wchar_t *appdata = _wgetenv(L"APPDATA");
    char folder[MAX_PATH * 3];

    if (appdata == NULL
        || WideCharToMultiByte(CP_UTF8, 0, appdata, -1, folder, (int)sizeof folder, NULL, NULL) == 0)
        return false;
    return snprintf(out, n, "%s\\" JOULARENERGYMETER_NAME, folder) < (int)n;
}

static bool make_folder(const char *folder)
{
    wchar_t w[MAX_PATH];

    return wide(folder, w, MAX_PATH)
           && (CreateDirectoryW(w, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
}

static FILE *open_to_read(const char *name)
{
    wchar_t w[MAX_PATH];

    return wide(name, w, MAX_PATH) ? _wfopen(w, L"r") : NULL;
}
#else
/* Under sudo, root's home would hold settings the user never sees again */
static const char *home_folder(void)
{
    const char *home = getenv("HOME");
    struct passwd *pw;
    uid_t uid;
    gid_t gid;
    bool sudo = sudo_user(&uid, &gid);

    if (!sudo && home != NULL && home[0] == '/') return home;
    pw = getpwuid(sudo ? uid : getuid());
    return pw != NULL ? pw->pw_dir : NULL;
}

/* Application Support on macOS; $XDG_CONFIG_HOME or ~/.config elsewhere */
static bool find_folder(char *out, size_t n)
{
    const char *home = home_folder();
#ifdef PJ_MACOS
    if (home == NULL) return false;
    return snprintf(out, n, "%s/Library/Application Support/" JOULARENERGYMETER_NAME, home) < (int)n;
#else
    const char *config = getenv("XDG_CONFIG_HOME");
    uid_t uid;
    gid_t gid;

    if (!sudo_user(&uid, &gid) && config != NULL && config[0] == '/')
        return snprintf(out, n, "%s/joularenergymeter", config) < (int)n;
    if (home == NULL) return false;
    return snprintf(out, n, "%s/.config/joularenergymeter", home) < (int)n;
#endif
}

/* Made with its parent if needed (~/.config may not exist yet), owned by the user under sudo */
static bool make_folder(const char *folder)
{
    char parent[sizeof file];
    char *slash;
    uid_t uid;
    gid_t gid;

    if (mkdir(folder, 0755) != 0) {
        if (errno == EEXIST) return true;
        if (errno != ENOENT) return false;
        snprintf(parent, sizeof parent, "%s", folder);
        slash = strrchr(parent, '/');
        if (slash == NULL || slash == parent) return false;
        *slash = '\0';
        if (!make_folder(parent) || mkdir(folder, 0755) != 0) return false;
    }
    if (sudo_user(&uid, &gid)) (void)!chown(folder, uid, gid);
    return true;
}

static FILE *open_to_read(const char *name)
{
    return fopen(name, "r");
}
#endif

void settings_set(const char *key, const char *value)
{
    int i;

    for (i = 0; i < count && strcmp(kept[i].key, key) != 0; i++)
        ;
    if (i == MAX_KEPT || strlen(key) >= sizeof kept[i].key || strlen(value) >= sizeof kept[i].value
        || strchr(value, '\n') != NULL)
        return;
    snprintf(kept[i].key, sizeof kept[i].key, "%s", key);
    snprintf(kept[i].value, sizeof kept[i].value, "%s", value);
    if (i == count) count++;
}

const char *settings_get(const char *key)
{
    for (int i = 0; i < count; i++)
        if (strcmp(kept[i].key, key) == 0) return kept[i].value;
    return NULL;
}

int settings_index(const char *key, const char *const names[], int count, int otherwise)
{
    const char *value = settings_get(key);

    for (int i = 0; value != NULL && i < count; i++)
        if (strcmp(value, names[i]) == 0) return i;
    return otherwise;
}

void settings_load(void)
{
    char folder[sizeof file - 16], line[sizeof kept[0].key + sizeof kept[0].value + 2];
    FILE *f;

    if (!find_folder(folder, sizeof folder)) return;
    snprintf(file, sizeof file, "%s%csettings", folder, SEPARATOR);
    if ((f = open_to_read(file)) == NULL) return;

    /* Lines that don't read as key=value, comments included, are skipped */
    while (fgets(line, sizeof line, f) != NULL) {
        char *equal = strchr(line, '=');

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '#' || equal == NULL || equal == line) continue;
        *equal = '\0';
        settings_set(line, equal + 1);
    }
    fclose(f);
}

void settings_save(void)
{
    char folder[sizeof file];
    FILE *f;
    bool written;

    if (file[0] == '\0') return;
    controls_keep();
    display_keep();

    snprintf(folder, sizeof folder, "%s", file);
    *strrchr(folder, SEPARATOR) = '\0';
    if (!make_folder(folder) || (f = open_user_file(file, false)) == NULL) {
        fprintf(stderr, JOULARENERGYMETER_NAME ": can't save the settings in %s: %s\n", file, strerror(errno));
        return;
    }
    written = fputs("# " JOULARENERGYMETER_NAME ", rewritten at each Start and on quit\n", f) >= 0;
    for (int i = 0; i < count && written; i++)
        written = fprintf(f, "%s=%s\n", kept[i].key, kept[i].value) > 0;
    if (fclose(f) != 0 || !written)
        fprintf(stderr, JOULARENERGYMETER_NAME ": can't save the settings in %s\n", file);
}
