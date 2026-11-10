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

/* The command a session measures. Its own process is the one followed (exec, so not a shell),
 * and it is reaped here, so its end is seen at once, with its exit status and its own CPU time
 * at the end, which CPU Load can't read from an ended process. */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifdef PJ_WINDOWS
#include <windows.h>
#else
#ifdef PJ_MACOS
#include <libproc.h>
#include <mach/mach_time.h>
#endif
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "app.h"

#ifdef PJ_WINDOWS

static HANDLE process;

void command_forget(void)
{
    if (process != NULL) CloseHandle(process);
    process = NULL;
}

bool command_start(const char *line, unsigned int *pid, char *trouble, size_t n)
{
    /* CreateProcessW may write into the command line */
    wchar_t w[2048];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    command_forget();
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    if (MultiByteToWideChar(CP_UTF8, 0, line, -1, w, 2048) == 0) {
        snprintf(trouble, n, "the command is too long");
        return false;
    }
    if (!CreateProcessW(NULL, w, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        snprintf(trouble, n, "Windows error %lu", (unsigned long)GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    process = pi.hProcess;
    *pid = (unsigned int)pi.dwProcessId;
    return true;
}

bool command_ended(int *exit_code, int64_t *used)
{
    DWORD code;
    FILETIME created, exited, kernel, user;

    if (process == NULL || WaitForSingleObject(process, 0) != WAIT_OBJECT_0) return false;
    *exit_code = GetExitCodeProcess(process, &code) ? (int)code : -1;
    /* 100 ns units */
    *used = GetProcessTimes(process, &created, &exited, &kernel, &user)
            ? (int64_t)((((ULONGLONG)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime)
                         + ((ULONGLONG)user.dwHighDateTime << 32 | user.dwLowDateTime)) / 10)
            : -1;
    command_forget();
    return true;
}

#else

extern char **environ;

static pid_t child;      /* the command followed, 0 when none */
static pid_t leftover;   /* one left running at Stop, reaped once it ends */

void command_forget(void)
{
    if (leftover != 0 && waitpid(leftover, NULL, WNOHANG) != 0) leftover = 0;
    if (child != 0) leftover = child;
    child = 0;
}

/* Everything is made before fork: the child may only call async-signal-safe functions. Under
 * sudo it runs as the user who ran sudo, with their groups and home. */
bool command_start(const char *line, unsigned int *pid, char *trouble, size_t n)
{
    static const int defaults[] = { SIGPIPE, SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGCHLD };
    static char script[sizeof ((target *)0)->command + 8], home[PATH_MAX + 8], user[256], logname[256];
    char *argv[] = { "sh", "-c", script, NULL };
    uid_t uid = 0;
    gid_t gid = 0;
    bool drop = sudo_user(&uid, &gid);
#ifdef PJ_MACOS
    int groups[64];
#else
    gid_t groups[64];
#endif
    int count = 64, entries, kept = 0, last_fd = (int)sysconf(_SC_OPEN_MAX);
    struct passwd *pw = drop ? getpwuid(uid) : NULL;
    char **env;
    sigset_t none;

    command_forget();
    snprintf(script, sizeof script, "exec %s", line);
    if (last_fd < 0 || last_fd > 4096) last_fd = 4096;

    /* Root's HOME, USER and LOGNAME replaced by the user's */
    for (entries = 0; environ[entries] != NULL; entries++)
        ;
    env = malloc((size_t)(entries + 4) * sizeof *env);
    if (env == NULL) {
        snprintf(trouble, n, "%s", strerror(ENOMEM));
        return false;
    }
    for (int i = 0; i < entries; i++)
        if (pw == NULL || (strncmp(environ[i], "HOME=", 5) != 0 && strncmp(environ[i], "USER=", 5) != 0
                           && strncmp(environ[i], "LOGNAME=", 8) != 0))
            env[kept++] = environ[i];
    if (pw != NULL) {
        snprintf(home, sizeof home, "HOME=%s", pw->pw_dir);
        snprintf(user, sizeof user, "USER=%s", pw->pw_name);
        snprintf(logname, sizeof logname, "LOGNAME=%s", pw->pw_name);
        env[kept++] = home;
        env[kept++] = user;
        env[kept++] = logname;
        if (getgrouplist(pw->pw_name, gid, groups, &count) < 0) count = 0;
    }
    env[kept] = NULL;
    if (drop && pw == NULL) {
        free(env);
        snprintf(trouble, n, "no user %u to run it as", (unsigned)uid);
        return false;
    }

    child = fork();
    if (child == 0) {
        int null = open("/dev/null", O_RDONLY);

        /* Its own session: Ctrl+C on this program and Stop leave it running */
        setsid();
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (size_t i = 0; i < sizeof defaults / sizeof *defaults; i++) signal(defaults[i], SIG_DFL);
        if (null > 0) {
            dup2(null, 0);
            close(null);
        }
        /* Our files (CSV, counters) stay ours */
        for (int fd = 3; fd < last_fd; fd++) close(fd);
        if (drop && ((count > 0 ? setgroups(count, (const gid_t *)groups) : setgroups(1, &gid)) != 0
                     || setgid(gid) != 0 || setuid(uid) != 0))
            _exit(126);
        execve("/bin/sh", argv, env);
        _exit(127);
    }
    free(env);
    if (child < 0) {
        child = 0;
        snprintf(trouble, n, "%s", strerror(errno));
        return false;
    }
    *pid = (unsigned int)child;
    return true;
}

/* Its own CPU time in µs, as CPU Load counts it, while it is a zombie; -1 if unknown */
static int64_t own_time(pid_t pid)
{
#ifdef PJ_LINUX
    char path[32], line[1024] = "";
    const char *end;
    unsigned long long utime, stime;
    long tick = sysconf(_SC_CLK_TCK);
    FILE *f;

    snprintf(path, sizeof path, "/proc/%d/stat", (int)pid);
    if (tick <= 0 || (f = fopen(path, "r")) == NULL) return -1;
    if (fgets(line, sizeof line, f) == NULL) line[0] = '\0';
    fclose(f);
    /* utime and stime are fields 14 and 15, counted after the name, which may hold ")" */
    end = strrchr(line, ')');
    if (end == NULL || sscanf(end + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime, &stime) != 2)
        return -1;
    return (int64_t)((utime + stime) * (unsigned long long)(1000000 / tick));
#elif defined(PJ_MACOS)
    struct rusage_info_v2 info;
    mach_timebase_info_data_t base;

    if (proc_pid_rusage(pid, RUSAGE_INFO_V2, (rusage_info_t *)&info) != 0 || mach_timebase_info(&base) != KERN_SUCCESS)
        return -1;
    /* Its own, not its children's (ri_child_*) */
    return (int64_t)((info.ri_user_time + info.ri_system_time) * base.numer / base.denom / 1000);
#else
    (void)pid;
    return -1;
#endif
}

bool command_ended(int *exit_code, int64_t *used)
{
    siginfo_t info;
    int status;

    if (child == 0) return false;
    /* Seen ended without reaping it, so its time can still be read */
    memset(&info, 0, sizeof info);
    if (waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT) != 0) {
        if (errno == EINTR) return false;
        /* Gone already: nothing more to know */
        *used = -1;
    } else if (info.si_pid == 0) {
        return false;
    } else {
        *used = own_time(child);
    }
    *exit_code = waitpid(child, &status, 0) != child ? -1
                 : WIFEXITED(status)                 ? WEXITSTATUS(status)
                 : WIFSIGNALED(status)               ? 128 + WTERMSIG(status)
                                                     : -1;
    child = 0;
    return true;
}

#endif
