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

/* --version and --help, otherwise open the window */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows: SDL2main provides WinMain and renames main */
#include <SDL2/SDL.h>

#include "cpuload.h"
#include "joularcore.h"

#include "app.h"

#ifdef PJ_WINDOWS
#include <windows.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

/* Ada runtime of the two libraries (bound by gprbuild), init before any call */
extern void adainit(void);
extern void adafinal(void);

const char *program_path = "joularenergymeter";

#ifdef PJ_WINDOWS
/* GUI subsystem app has no console. Print to the parent's, unless stdout is redirected. */
static void use_parent_console(void)
{
    if (GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_UNKNOWN
        && AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
    }
}
#else
static void use_parent_console(void)
{
}
#endif

#ifdef PJ_WINDOWS
static void leave_root_folder(void)
{
}
#else
/* Finder and some desktops start apps in /, where the CSV files can't go. Use the home folder. */
static void leave_root_folder(void)
{
    char here[PATH_MAX];
    const char *home = getenv("HOME");

    if (getcwd(here, sizeof here) == NULL || strcmp(here, "/") != 0
        || home == NULL || home[0] != '/') return;
    if (chdir(home) != 0) fprintf(stderr, JOULARENERGYMETER_NAME ": can't open %s, files go to /\n", home);
}
#endif

int main(int argc, char *argv[])
{
    const char *option = argc > 1 ? argv[1] : "";
    int status = 0;

    if (argc > 0) program_path = argv[0];
    adainit();
    leave_root_folder();

    if (strcmp(option, "-v") == 0 || strcmp(option, "--version") == 0) {
        use_parent_console();
        printf(JOULARENERGYMETER_NAME " %s\n", JOULARENERGYMETER_VERSION);
        printf("Joular Core %s, CPU Load %s\n", joularcore_version(), cpuload_version());
    } else if (strcmp(option, "-h") == 0 || strcmp(option, "--help") == 0) {
        use_parent_console();
        printf(JOULARENERGYMETER_NAME " %s monitors the power consumption of multiple platforms, processes and applications\n", JOULARENERGYMETER_VERSION);
        puts("What to monitor is chosen in the GUI window. The only options are:");
        puts("  -v, --version   print the versions and stop");
        puts("  -h, --help      print this and stop");
    } else if (option[0] == '-') {
        /* A file dropped on the exe in Explorer isn't an option and still opens the window */
        use_parent_console();
        fprintf(stderr, JOULARENERGYMETER_NAME ": unknown option %s (try --help)\n", option);
        status = 2;
    } else if (!platform_init(JOULARENERGYMETER_NAME, 1120, 720, 1040, 620)) {
        fputs(JOULARENERGYMETER_NAME ": no window could be opened\n", stderr);
        status = 1;
    } else if (!sampler_init(display_report)) {
        fputs(JOULARENERGYMETER_NAME ": measuring could not start\n", stderr);
        status = 1;
    } else {
        theme_init(lv_display_get_default());
        ui_build();
        status = platform_run(sampler_deliver);

        /* Window may close mid-session: release the hardware (and powermetrics) before
         * adafinal */
        display_stop();
        sampler_quit();
        platform_close();
    }

    adafinal();
    return status;
}
