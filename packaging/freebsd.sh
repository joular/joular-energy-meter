#!/bin/sh
# Builds Joular Energy Meter on FreeBSD and makes, in dist/:
#   - a tar.gz with the program, the README and the licences
#   - a package for pkg (installed with: sudo pkg add joularenergymeter-<version>-freebsd<n>-<arch>.pkg)
#
# Builds with gprbuild, as Alire has no FreeBSD SDL2. Needs GNAT 15 or newer and SDL2:
#   sudo pkg install gprbuild gnat15 sdl2
#   export PATH=/usr/local/gnat15/bin:$PATH
# and CPU Load and Joular Core checked out beside this folder, or in LIBS:
#   git clone --depth 1 --branch 0.0.5 https://github.com/joular/cpuload.git
#   git clone --depth 1 --branch 0.0.5 https://github.com/joular/joularcore.git
#
# Run from anywhere: packaging/freebsd.sh, or LIBS=<folder> packaging/freebsd.sh.
# The package is for the FreeBSD version it's built on (pkg checks it).

. "$(dirname "$0")/common.sh"
need gprbuild pkg

LIBS=${LIBS:-..}
ARCH=$(uname -m)
RELEASE=$(uname -r | cut -d. -f1)

if [ "${SKIP_BUILD:-}" != 1 ]; then
    gprbuild -P joularenergymeter.gpr -aP"$LIBS/joularcore" -aP"$LIBS/cpuload" -p
fi
check "bin/$NAME"

# The Ada runtime is linked in: SDL2 is the only library from outside the base system
readelf -d "bin/$NAME" | grep NEEDED
if readelf -d "bin/$NAME" | grep NEEDED | grep -Eq "gnat|gnarl|stdc"; then
    echo "the program needs a library installed with GNAT" >&2
    exit 1
fi

# tar.gz
TARBALL="$NAME-$VERSION-freebsd$RELEASE-$ARCH"
mkdir -p "$WORK/$TARBALL"
cp "bin/$NAME" "$WORK/$TARBALL/"
docs "$WORK/$TARBALL"
tar -czf "$DIST/$TARBALL.tar.gz" -C "$WORK" "$TARBALL"

# Package: files under /usr/local, SDL2 as a dependency
STAGE="$WORK/root"
install -d "$STAGE/usr/local/bin" "$STAGE/usr/local/share/applications" \
    "$STAGE/usr/local/share/icons/hicolor/512x512/apps"
install -m 755 "bin/$NAME" "$STAGE/usr/local/bin/"
install -m 644 "$HERE/$NAME.desktop" "$STAGE/usr/local/share/applications/"
install -m 644 assets/icon.png "$STAGE/usr/local/share/icons/hicolor/512x512/apps/$NAME.png"
docs "$STAGE/usr/local/share/doc/$NAME"

cat > "$WORK/+MANIFEST" << EOF
name: $NAME
version: "$VERSION"
origin: sysutils/$NAME
comment: "$SUMMARY"
desc: "$DESCRIPTION"
maintainer: "adel.noureddine@outlook.com"
www: "$WEBSITE"
prefix: /usr/local
categories: [sysutils]
licenselogic: single
licenses: [GPLv3]
deps: {
    sdl2: { origin: "$(pkg query %o sdl2)", version: "$(pkg query %v sdl2)" }
}
EOF

{
    echo "@owner root"
    echo "@group wheel"
    (cd "$STAGE" && find . -type f | sed 's|^\.||' | sort)
} > "$WORK/plist"

mkdir -p "$WORK/pkg"
pkg create -M "$WORK/+MANIFEST" -p "$WORK/plist" -r "$STAGE" -o "$WORK/pkg"
mv "$WORK"/pkg/* "$DIST/$NAME-$VERSION-freebsd$RELEASE-$ARCH.pkg"
pkg info -F "$DIST/$NAME-$VERSION-freebsd$RELEASE-$ARCH.pkg"

ls -l "$DIST"
