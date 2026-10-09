#!/bin/sh
# Builds Joular Energy Meter on Linux (x86_64 or aarch64) and makes, in dist/:
#   - a tar.gz with the program, the README and the licences
#   - a .deb (Debian, Ubuntu), an .rpm (Fedora, openSUSE) and an Arch Linux package
#   - an AppImage, with SDL2 inside, for any other distribution
#
# Needs Alire, SDL2's development files and the packaging tools:
#   Debian, Ubuntu: sudo apt install libsdl2-dev rpm libarchive-tools zstd curl file
#   Fedora:         sudo dnf install SDL2-devel rpm-build dpkg bsdtar zstd curl file
#   Arch Linux:     sudo pacman -S sdl2 rpm-tools dpkg libarchive zstd curl file
# The packages need the glibc of the build machine or newer: build on an old distribution
# (the CI uses Ubuntu 22.04, glibc 2.35).
#
# Run from anywhere: packaging/linux.sh. SKIP_BUILD=1 packages bin/joularenergymeter as it is.

. "$(dirname "$0")/common.sh"
need dpkg-deb rpmbuild bsdtar zstd curl objdump

case $(uname -m) in
    x86_64) ARCH=x86_64; DEB_ARCH=amd64 ;;
    aarch64 | arm64) ARCH=aarch64; DEB_ARCH=arm64 ;;
    *) echo "no package for $(uname -m)" >&2; exit 1 ;;
esac

if [ "${SKIP_BUILD:-}" != 1 ]; then
    need alr
    alr --non-interactive build
fi
check "bin/$NAME"

# Newest glibc symbol the program uses
GLIBC=$(objdump -T "bin/$NAME" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -V | tail -1)
echo "needs glibc $GLIBC or newer"

# Files as installed, shared by the .deb, .rpm and Arch package
STAGE="$WORK/root"
install -D -m 755 "bin/$NAME" "$STAGE/usr/bin/$NAME"
install -D -m 644 "$HERE/$NAME.desktop" "$STAGE/usr/share/applications/$NAME.desktop"
install -D -m 644 assets/icon.png "$STAGE/usr/share/icons/hicolor/512x512/apps/$NAME.png"
docs "$STAGE/usr/share/doc/$NAME"
SIZE_KB=$(du -sk "$STAGE" | cut -f1)

# tar.gz (keeps the executable bit, which zip files lose)
TARBALL="$NAME-$VERSION-linux-$ARCH-glibc$GLIBC"
mkdir -p "$WORK/$TARBALL"
cp "bin/$NAME" "$WORK/$TARBALL/"
docs "$WORK/$TARBALL"
tar -czf "$DIST/$TARBALL.tar.gz" -C "$WORK" "$TARBALL"

# .deb: libsdl2-2.0-0 is SDL2 (or sdl2-compat) on Debian and Ubuntu
mkdir -p "$WORK/deb"
cp -R "$STAGE/." "$WORK/deb/"
mkdir -p "$WORK/deb/DEBIAN"
cat > "$WORK/deb/DEBIAN/control" << EOF
Package: $NAME
Version: $VERSION-1
Architecture: $DEB_ARCH
Maintainer: $MAINTAINER
Installed-Size: $SIZE_KB
Depends: libc6 (>= $GLIBC), libsdl2-2.0-0 (>= 2.0.18)
Section: utils
Priority: optional
Homepage: $WEBSITE
Description: $SUMMARY
 $DESCRIPTION
EOF
dpkg-deb --root-owner-group --build "$WORK/deb" "$DIST/${NAME}_$VERSION-1_$DEB_ARCH.deb"

# .rpm: rpmbuild finds the libraries the program needs (libSDL2-2.0.so.0, glibc) by itself
mkdir -p "$WORK/rpm"
cat > "$WORK/rpm/$NAME.spec" << EOF
Name: $NAME
Version: $VERSION
Release: 1
Summary: $SUMMARY
License: GPL-3.0-only
URL: $WEBSITE

# Already built: no debug package, no stripping
%global debug_package %{nil}
%global __os_install_post %{nil}
%global _build_id_links none

%description
$DESCRIPTION

%install
cp -R "$STAGE/." %{buildroot}/

%files
/usr/bin/$NAME
/usr/share/applications/$NAME.desktop
/usr/share/icons/hicolor/512x512/apps/$NAME.png
/usr/share/doc/$NAME
EOF
rpmbuild -bb --quiet \
    --define "_topdir $WORK/rpm" \
    --define "_rpmdir $DIST" \
    --define "_build_name_fmt %%{NAME}-%%{VERSION}-%%{RELEASE}.%%{ARCH}.rpm" \
    "$WORK/rpm/$NAME.spec"
RPM="$DIST/$NAME-$VERSION-1.$ARCH.rpm"
rpm -qpR "$RPM"
if ! rpm -qpR "$RPM" | grep -q libSDL2; then
    echo "the .rpm doesn't ask for SDL2" >&2
    exit 1
fi

# Arch Linux package, made as makepkg does (.PKGINFO, .MTREE, then the files), so it builds on
# any distribution. sdl2 is SDL2 or sdl2-compat.
mkdir -p "$WORK/arch"
cp -R "$STAGE/." "$WORK/arch/"
cat > "$WORK/arch/.PKGINFO" << EOF
pkgname = $NAME
pkgbase = $NAME
xdata = pkgtype=pkg
pkgver = $VERSION-1
pkgdesc = $SUMMARY
url = $WEBSITE
builddate = $(date +%s)
packager = $MAINTAINER
size = $((SIZE_KB * 1024))
arch = $ARCH
license = GPL-3.0-only
depend = glibc
depend = sdl2
EOF
(
    cd "$WORK/arch"
    bsdtar -czf .MTREE --format=mtree --uid 0 --gid 0 --uname root --gname root \
        --options='!all,use-set,type,uid,gid,mode,time,size,md5,sha256,link' .PKGINFO usr
    bsdtar -cf - --uid 0 --gid 0 --uname root --gname root .MTREE .PKGINFO usr \
        | zstd -q -19 -T0 -o "$DIST/$NAME-$VERSION-1-$ARCH.pkg.tar.zst"
)
chmod 644 "$DIST/$NAME-$VERSION-1-$ARCH.pkg.tar.zst"

# AppImage with linuxdeploy, which copies SDL2 and the libraries it needs but leaves out those
# every desktop has (glibc, X11, sound...). Runs without FUSE, as in containers.
LINUXDEPLOY="$ROOT/obj/linuxdeploy-$ARCH.AppImage"
if [ ! -x "$LINUXDEPLOY" ]; then
    curl -fsSL -o "$LINUXDEPLOY" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-$ARCH.AppImage"
    chmod +x "$LINUXDEPLOY"
fi
cp assets/icon.png "$WORK/$NAME.png"
(
    cd "$WORK"
    APPIMAGE_EXTRACT_AND_RUN=1 LINUXDEPLOY_OUTPUT_VERSION="$VERSION" "$LINUXDEPLOY" \
        --appdir AppDir --executable "$ROOT/bin/$NAME" \
        --desktop-file "$HERE/$NAME.desktop" --icon-file "$NAME.png" --output appimage
)
mv "$WORK"/*.AppImage "$DIST/$NAME-$VERSION-$ARCH.AppImage"
APPIMAGE_EXTRACT_AND_RUN=1 "$DIST/$NAME-$VERSION-$ARCH.AppImage" --version

ls -l "$DIST"
