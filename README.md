# Joular Energy Meter

Joular Energy Meter is a desktop application that monitors the power consumption of hardware components, processes and applications, on Windows, Linux, macOS and FreeBSD.

Similar to our powerful command-line tool, [PowerJoular](https://github.com/joular/powerjoular), Joular Energy Meter does the energy and CPU usage measuring through two Ada libraries we developed:
- [Joular Core](https://github.com/joular/joularcore): for CPU and GPU energy and power consumption.
- [CPU Load](https://github.com/joular/cpuload): for CPU usage for the whole system, a specific PID, and a specific application (all its PIDs).

## :rocket: Features

- Monitor CPU, GPU and total power, and the CPU load of the machine and of the process you follow
- The energy used since the start, on a meter-style counter, plus average, peak and lowest power
- A chart of the last 120 measurements (two minutes at 1 s)
- CSV files in PowerJoular's format, written live or exported from the chart
- Runs on Linux, Windows, macOS and FreeBSD, and has the same UI and look&feel on all of them

A process that has ended or can't be read shows a dash, not 0 %, and is written as `-1` in the
CSV files, as PowerJoular does.

## :satellite: Supported platforms

| OS | CPU | GPU |
|---|---|---|
| Linux | Intel, AMD, Raspberry Pi | Nvidia, AMD |
| Windows | Intel, AMD | Nvidia, AMD |
| macOS | Apple Silicon, Intel | Apple Silicon |
| FreeBSD | Intel, AMD | Nvidia |

How each hardware component is read is detailed in [Joular Core](https://github.com/joular/joularcore#satellite-supported-platforms).

## :floppy_disk: Building and compilation

You need SDL2 (2.0.18 or newer) and LVGL 9.6 inside this folder:

```bash
git clone https://github.com/joular/joular-energy-meter
cd joular-energy-meter && git clone --depth 1 --branch release/v9.6 https://github.com/lvgl/lvgl.git
```

### With Alire

[Alire](https://alire.ada.dev) brings GNAT, gprbuild, Joular Core and CPU Load:

```bash
alr build
```

Alire checks that SDL2 is installed and offers to install it. To install it yourself:

```bash
brew install sdl2                   # macOS
sudo apt install libsdl2-dev        # Debian, Ubuntu
sudo dnf install SDL2-devel         # Fedora
pacman -S mingw-w64-x86_64-SDL2     # Windows, in Alire's msys2 shell
sudo pkg install sdl2               # FreeBSD
```

If SDL2 is in another place: `alr build -- -XSDL2_CFLAGS="-I<folder>" -XSDL2_LIBS="-L<folder> -lSDL2"`.

### Without Alire

You need GNAT and gprbuild, SDL2 (as above), and the two libraries next to this folder:

```bash
cd ..
git clone --depth 1 --branch 0.0.5 https://github.com/joular/cpuload.git
git clone --depth 1 --branch 0.0.5 https://github.com/joular/joularcore.git
cd joular-energy-meter
gprbuild -P joularenergymeter.gpr -aP../joularcore -aP../cpuload -p
```

The OS is detected from the compiler, which you can force with `-XPJ_OS=linux`, `windows`, `macos` or
`freebsd`. gprbuild doesn't look where Homebrew puts SDL2, so on macOS add
`-XSDL2_CFLAGS="-I$(brew --prefix)/include" -XSDL2_LIBS="-L$(brew --prefix)/lib -lSDL2"`.
The program is `bin/joularenergymeter`.

On FreeBSD, use GNAT 15 or newer (`sudo pkg install gprbuild gnat15`, then put
`/usr/local/gnat15/bin` first in `PATH`). GNAT 12 can't compile Joular Core's FreeBSD code.
Alire has no FreeBSD SDL2 package, so this is the only way to build it on FreeBSD.

### :package: Packages and installers

`packaging/` has one script per system. Each builds the program, checks it and makes the packages in `dist/`. The CI runs the same scripts and keeps what they make as artifacts.
Every package has the program, this README and the licences (GPL, LVGL's MIT, Barlow's OFL).
The top of each script lists what it needs.

| System | Run | Makes |
| --- | --- | --- |
| Linux | `packaging/linux.sh` | `.tar.gz`, `.deb`, `.rpm`, Arch Linux `.pkg.tar.zst`, `.AppImage` |
| macOS | `packaging/macos.sh` | `.zip` with *Joular Energy Meter.app*, `.dmg` |
| Windows | `powershell -ExecutionPolicy Bypass -File packaging\windows.ps1` | `.zip`, `.msi` |
| FreeBSD | `packaging/freebsd.sh` | `.tar.gz`, `.pkg` |

Users need no compiler, Alire or MSYS2 to run Joular Energy Meter:

- **Linux** (x86_64 and aarch64): `sudo apt install ./joularenergymeter_<version>-1_amd64.deb`,
  `sudo dnf install ./joularenergymeter-<version>-1.x86_64.rpm` or
  `sudo pacman -U joularenergymeter-<version>-1-x86_64.pkg.tar.zst`, which also install SDL2. The
  AppImage has SDL2 inside and runs anywhere (`chmod +x`, then start it). The tar.gz needs the
  SDL2 package (`libsdl2-2.0-0` on Debian and Ubuntu, `SDL2` on Fedora). All need glibc 2.35 or
  newer (Debian 12, Ubuntu 22.04, Fedora 36 and later), as built by the CI on Ubuntu 22.04.
- **macOS**: open the `.dmg` and drag the app to Applications. One binary for Apple silicon and
  Intel, with SDL's `SDL2.framework` inside, macOS 15 or newer (GNAT's Ada runtime is built for
  15). Signed ad hoc only, so the first time macOS blocks it: open *System Settings > Privacy &
  Security* and click *Open Anyway*. Started from Finder, it writes its CSV files to the home
  folder. For power readings, start it from Terminal:
  `sudo "/Applications/Joular Energy Meter.app/Contents/MacOS/joularenergymeter"`.
- **Windows**: run the `.msi` (Program Files, Start menu, removed from Settings > Apps), or unzip
  the `.zip` and run `joularenergymeter.exe`. Both ship SDL's own `SDL2.dll`. Unsigned, so
  SmartScreen asks once (More info, Run anyway).
- **FreeBSD**: `sudo pkg install sdl2`, then `sudo pkg add joularenergymeter-<version>-freebsd14-amd64.pkg` (pkg add looks for dependencies only beside the file), or the tar.gz.

On macOS the script builds the app's binary in `bin/app`, so `bin/joularenergymeter` stays the
one that runs from the checkout. A binary built by hand needs SDL2 from where it was built
(Homebrew, MSYS2), so ship the packages instead.

## Reading the power

CPU load always works. Power reading may need some access depending on each OS, and when it's missing, a notice says what to do, with a button to copy the command.

- **Linux**: RAPL files are root-only (CVE-2020-8694). Either run `sudo chmod -R a+r /sys/class/powercap/intel-rapl` after each boot, which lets every user on the machine read them, or start the app with `sudo` under X11 (not Wayland).
- **Windows**: nothing to do on Windows 11. On older Windows versions, install the [PawnIO driver](https://pawnio.eu) and run as Administrator, or [Hubblo's RAPL driver](https://github.com/hubblo-org/windows-rapl-driver).
- **macOS**: `powermetrics` needs root: `sudo ./bin/joularenergymeter`.
- **FreeBSD**: load the cpuctl driver (`sudo kldload cpuctl`, or `cpuctl_load="YES"` in `/boot/loader.conf`) and run with `sudo` under X11. Intel and AMD only. The `kmem` group also works, but it can read all memory through `/dev/mem`.
- Nvidia GPUs, AMD GPUs on Linux and Windows, and Raspberry Pi models need nothing.

## :bulb: Usage

The top bar has options to: *Measure* the whole machine, a process (by ID) or an
application (by name), *every* interval, and optionally *Record to* a CSV file. Press **Start**
(or Enter).
While measuring, the target and the file are locked, while the measurement interval can still be changed, which restarts the chart.
**Pause** stops the clock and the energy count until you resume.

- **Meter**: the machine's power, the followed program's power next to it, the energy counter (joules, then kJ and MJ), and a disc that turns with the energy used.
- **Chart**: the last 120 measurements. **Export** saves them to `joularenergymeter-trend.csv` in the folder the app was started from (the home folder when started from Finder or a menu).
- **Right column**: processor, graphics and CPU load, now and since the start, with the CPU/GPU split; the followed program's power and its share of the CPU and of the machine; and session figures (average, standard deviation, highest and lowest with their time, a year at the current average, measurements taken and missed).
- **Status bar**: the last message. Errors stay highlighted until the next one.

When you follow a process or application, a *second* CSV file is written next to the first (`<file>-<pid>.csv` or `<file>-<name>.csv`), as PowerJoular does. Applications are matched by program name (`firefox`, not *Firefox.app*), and on macOS, all programs inside the app bundle count.

The interval goes from 250 ms to 10 s. Each cycle is divided by the time it really took, so changing it while measuring is fine. A cycle longer than five intervals and 10 s means the machine slept, and it's dropped. CSV timestamps are whole Unix seconds, as in PowerJoular.

As root, the application only writes to plain files (no links), and files it creates under `sudo` belong to you. While measuring, macOS and Windows don't go to sleep on idle. On Linux, use `systemd-inhibit --what=sleep` for long runs.

## :newspaper: License

Joular Energy Meter is licensed under the GNU GPL 3 license only (GPL-3.0-only).

Copyright (c) 2026, Adel Noureddine.
All rights reserved. This program and the accompanying materials are made available under the terms of the GNU General Public License v3.0 only (GPL-3.0-only) which accompanies this distribution, and is available at: https://www.gnu.org/licenses/gpl-3.0.en.html

Third-party work and dependencies:
- Joular Core and CPU Load are licensed LGPL-3.0-only.
- SDL2 is licensed zlib.
- LVGL, MIT licence, Copyright (c) 2025 LVGL Kft (`lvgl/LICENCE.txt`).
- Barlow font, SIL Open Font License 1.1, Copyright 2017 The Barlow Project Authors (`src/fonts/OFL.txt`).
- Montserrat font (built into LVGL), SIL Open Font License 1.1, Copyright 2024 The Montserrat.Git Project Authors.
- Font Awesome Free 5 symbols (built into LVGL), SIL Open Font License 1.1, Fonticons, Inc.

Author : Prof. Adel Noureddine