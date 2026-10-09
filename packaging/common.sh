# Shared by linux.sh, freebsd.sh and macos.sh. Packages go to dist/, work files to obj/packaging.

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
HERE="$ROOT/packaging"
DIST="$ROOT/dist"
WORK="$ROOT/obj/packaging"
cd "$ROOT"

NAME=joularenergymeter
VERSION=$(sed -n 's/^version *= *"\(.*\)"/\1/p' alire.toml | head -1)
SUMMARY="GUI for monitoring the power of hardware, processes and applications"
DESCRIPTION="Joular Energy Meter monitors, in real time, the power consumption of hardware components (CPU, GPU), processes and applications, on Linux, macOS, Windows and FreeBSD. It also follows one process or one whole application, and exports power values to CSV files."
MAINTAINER="Adel Noureddine <adel.noureddine@outlook.com>"
WEBSITE="https://www.noureddine.org/research/joular"

# SDL's own build, shipped inside the macOS app (and the Windows packages, see windows.ps1)
SDL2_VERSION=2.32.10

rm -rf "$WORK"
mkdir -p "$WORK" "$DIST"

# Fails with a hint when a tool is missing
need() {
    for tool in "$@"; do
        command -v "$tool" > /dev/null || { echo "$tool is missing, see the top of $0" >&2; exit 1; }
    done
}

# README and the licences: GPL, LVGL's MIT and Barlow's OFL
docs() {
    mkdir -p "$1"
    cp README.md LICENSE "$1/"
    cp lvgl/LICENCE.txt "$1/LICENCE-LVGL.txt"
    cp src/fonts/OFL.txt "$1/LICENCE-Barlow.txt"
}

# --version and --help print before any window opens. The version must be alire.toml's.
check() {
    "$1" --help > /dev/null
    "$1" --version
    built=$("$1" --version | head -1 | awk '{print $NF}')
    if [ "$built" != "$VERSION" ]; then
        echo "alire.toml says $VERSION but $1 says $built" >&2
        exit 1
    fi
}
