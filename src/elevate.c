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

/* Power readings without a terminal. The system's own dialog asks for the administrator, never
 * a password field of ours. Linux: pkexec makes the RAPL package counters readable until the
 * next boot, and this program stays the user's. macOS: the admin dialog starts this program
 * again as root, as powermetrics needs. Windows: UAC does the same. */

#include <stdio.h>
#include <string.h>

#ifdef PJ_WINDOWS
#include <windows.h>
#include <shellapi.h>
#else
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef PJ_MACOS
#include <mach-o/dyld.h>
#endif

#include "app.h"

#if defined(PJ_LINUX) || defined(PJ_MACOS)
extern char **environ;

/* The dialog's process, polled from a timer so the window keeps drawing meanwhile */
static struct {
    pid_t pid;
    int out;              /* its stdout and stderr */
    char said[512];       /* their end */
    lv_timer_t *timer;
    void (*on_done)(bool granted, const char *why);
} asked;

/* argv[0] found in PATH; stdin from /dev/null, stdout and stderr into a pipe. In a session of its
 * own: with no terminal, pkexec asks through the desktop's dialog. Returns errno or 0. */
static int spawn(const char *const argv[], pid_t *pid, int *out)
{
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    sigset_t none;
    short flags = POSIX_SPAWN_SETSIGMASK;
    int fds[2], rc;

    if (pipe(fds) != 0) return errno;
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);

    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
    posix_spawnattr_init(&attr);
    sigemptyset(&none);
    posix_spawnattr_setsigmask(&attr, &none);
#ifdef POSIX_SPAWN_SETSID
    flags |= POSIX_SPAWN_SETSID;
#endif
#ifdef PJ_MACOS
    /* Nothing else of ours leaks into the root process */
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attr, flags);

    rc = posix_spawnp(pid, argv[0], &actions, &attr, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return rc;
    }
    *out = fds[0];
    return 0;
}

/* Keeps the end of what it said, without its last line break */
static void drain(void)
{
    char chunk[256];
    ssize_t got;

    while ((got = read(asked.out, chunk, sizeof chunk - 1)) > 0) {
        size_t kept = strlen(asked.said);

        chunk[got] = '\0';
        if (kept + (size_t)got >= sizeof asked.said) {
            memmove(asked.said, asked.said + kept / 2, kept - kept / 2 + 1);
            kept = strlen(asked.said);
        }
        snprintf(asked.said + kept, sizeof asked.said - kept, "%s", chunk);
    }
    asked.said[strcspn(asked.said, "\n")] = '\0';
}

static void on_poll(lv_timer_t *timer)
{
    int status;
    bool granted;
    const char *why;

    drain();
    if (waitpid(asked.pid, &status, WNOHANG) != asked.pid) return;
    drain();
    close(asked.out);
    lv_timer_delete(timer);
    asked.timer = NULL;

    granted = WIFEXITED(status) && WEXITSTATUS(status) == 0;
#ifdef PJ_MACOS
    /* "execution error: User canceled. (-128)" */
    why = strstr(asked.said, "(-128)") != NULL ? "" : asked.said;
#else
    /* 126: the dialog dismissed or not allowed */
    why = WIFEXITED(status) && WEXITSTATUS(status) == 126 ? "" : asked.said[0] ? asked.said : "pkexec failed";
#endif
    asked.on_done(granted, granted ? "" : why);
}

static bool start(const char *const argv[], void (*on_done)(bool, const char *), char *error, size_t n)
{
    int rc;

    if (asked.timer != NULL) {
        snprintf(error, n, "The administrator is already being asked.");
        return false;
    }
    rc = spawn(argv, &asked.pid, &asked.out);
    if (rc != 0) {
        snprintf(error, n, "%s: %s", argv[0], strerror(rc));
        return false;
    }
    asked.said[0] = '\0';
    asked.on_done = on_done;
    asked.timer = lv_timer_create(on_poll, 250, NULL);
    return true;
}
#endif

#ifdef PJ_LINUX
#define POWERCAP "/sys/class/powercap"
#define MAX_PACKAGES 8

/* Each package's counter, intel-rapl:N/energy_uj but not the subzones intel-rapl:N:M, also on
 * AMD. Returns how many. */
static int rapl_files(char files[MAX_PACKAGES][PATH_MAX])
{
    DIR *d = opendir(POWERCAP);
    struct dirent *e;
    int count = 0;

    if (d == NULL) return 0;
    while ((e = readdir(d)) != NULL && count < MAX_PACKAGES)
        if (strncmp(e->d_name, "intel-rapl:", 11) == 0 && strchr(e->d_name + 11, ':') == NULL
            && snprintf(files[count], PATH_MAX, POWERCAP "/%s/energy_uj", e->d_name) < PATH_MAX)
            count++;
    closedir(d);
    return count;
}

elevate_way elevate_offer(void)
{
    char files[MAX_PACKAGES][PATH_MAX];

    return !platform_privileged() && rapl_files(files) > 0 && platform_in_path("pkexec") ? ELEVATE_ALLOW : ELEVATE_NONE;
}

bool elevate_start(const char *option, void (*on_done)(bool granted, const char *why), char *error, size_t n)
{
    static char files[MAX_PACKAGES][PATH_MAX];
    const char *argv[MAX_PACKAGES + 4] = { "pkexec", "chmod", "a+r" };
    int count = rapl_files(files);

    (void)option;
    for (int i = 0; i < count; i++) argv[3 + i] = files[i];
    argv[3 + count] = NULL;
    return start(argv, on_done, error, n);
}

#elif defined(PJ_MACOS)

/* For sh: 'text', with ' written as '\'' */
static bool quoted(char *out, size_t n, const char *text)
{
    size_t used = 0;

    out[used++] = '\'';
    for (; *text && used + 5 < n; text++) {
        if (*text == '\'') {
            memcpy(out + used, "'\\''", 4);
            used += 4;
        } else
            out[used++] = *text;
    }
    if (*text || used + 2 > n) return false;
    out[used++] = '\'';
    out[used] = '\0';
    return true;
}

elevate_way elevate_offer(void)
{
    return platform_privileged() ? ELEVATE_NONE : ELEVATE_RESTART;
}

/* Granted: the program started again took over */
static void (*refused)(bool granted, const char *why);

static void on_restart(bool granted, const char *why)
{
    if (granted) platform_quit();
    else refused(false, why);
}

/* do shell script starts it again in the background as root, so the dialog's answer comes back
 * at once. SUDO_UID and SUDO_GID give its files and settings to the user, as under sudo. */
bool elevate_start(const char *option, void (*on_done)(bool granted, const char *why), char *error, size_t n)
{
    char self[PATH_MAX], found[PATH_MAX], here[PATH_MAX];
    char q_self[3 * PATH_MAX], q_here[3 * PATH_MAX], q_home[3 * PATH_MAX], command[10 * PATH_MAX];
    uint32_t size = sizeof found;
    const char *home = getenv("HOME");
    const char *argv[] = { "/usr/bin/osascript", "-e", "on run argv", "-e",
                           "do shell script (item 1 of argv) with prompt (item 2 of argv) "
                           "with administrator privileges",
                           "-e", "end run", command,
                           JOULARENERGYMETER_NAME " reads the power with powermetrics, which only the administrator can run.",
                           NULL };

    if (_NSGetExecutablePath(found, &size) != 0 || realpath(found, self) == NULL || getcwd(here, sizeof here) == NULL
        || !quoted(q_self, sizeof q_self, self) || !quoted(q_here, sizeof q_here, here)
        || !quoted(q_home, sizeof q_home, home != NULL ? home : here)
        || snprintf(command, sizeof command,
                    /* Only the program in the background, all its ends closed, or the dialog's
                     * answer waits for it to quit */
                    "cd %s || exit 1; SUDO_UID=%u SUDO_GID=%u HOME=%s %s %s </dev/null >/dev/null 2>&1 &",
                    q_here, (unsigned)getuid(), (unsigned)getgid(), q_home, q_self,
                    strcmp(option, "--idle") == 0 ? "--idle" : "--start") >= (int)sizeof command) {
        snprintf(error, n, "This program's path or folder could not be passed on.");
        return false;
    }
    /* The program started again reads them */
    settings_save();
    refused = on_done;
    return start(argv, on_restart, error, n);
}

#elif defined(PJ_WINDOWS)

elevate_way elevate_offer(void)
{
    return platform_privileged() ? ELEVATE_NONE : ELEVATE_RESTART;
}

/* UAC answers before ShellExecuteExW returns, so on_done runs right away */
bool elevate_start(const char *option, void (*on_done)(bool granted, const char *why), char *error, size_t n)
{
    wchar_t self[MAX_PATH], here[MAX_PATH];
    SHELLEXECUTEINFOW info;
    DWORD size = GetModuleFileNameW(NULL, self, MAX_PATH);

    if (size == 0 || size == MAX_PATH || GetCurrentDirectoryW(MAX_PATH, here) == 0) {
        snprintf(error, n, "This program's path or folder could not be passed on.");
        return false;
    }
    memset(&info, 0, sizeof info);
    info.cbSize = sizeof info;
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = self;
    info.lpParameters = strcmp(option, "--idle") == 0 ? L"--idle" : L"--start";
    info.lpDirectory = here;
    info.nShow = SW_SHOWNORMAL;
    /* The program started again reads them */
    settings_save();
    if (ShellExecuteExW(&info))
        platform_quit();
    else if (GetLastError() == ERROR_CANCELLED)
        on_done(false, "");
    else
        on_done(false, "Windows could not start it as administrator");
    return true;
}

#else

elevate_way elevate_offer(void)
{
    return ELEVATE_NONE;
}

bool elevate_start(const char *option, void (*on_done)(bool granted, const char *why), char *error, size_t n)
{
    (void)option;
    (void)on_done;
    snprintf(error, n, "Not on this system.");
    return false;
}

#endif
