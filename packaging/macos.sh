#!/bin/sh
# Builds Joular Energy Meter on macOS and makes, in dist/:
#   - Joular Energy Meter.app in a zip, with SDL2.framework inside, so users need no Homebrew
#   - a disk image (.dmg) to drag the app to Applications
#
#   packaging/macos.sh                   builds for this Mac's chip, then packages
#   packaging/macos.sh build             only builds bin/app/joularenergymeter
#   packaging/macos.sh package BIN...    packages these binaries, two of them (arm64 and
#                                        x86_64) make a universal app
#
# Needs Alire and Homebrew's sdl2 (only for Alire's dependency check, the program links SDL's own
# SDL2.framework, downloaded here). Minimum macOS 15, as GNAT's Ada runtime is built for it.
# Signed ad hoc only: without an Apple Developer ID, users right-click > Open the first time.

. "$(dirname "$0")/common.sh"
need curl hdiutil lipo codesign ditto

export MACOSX_DEPLOYMENT_TARGET=15.0

# SDL2.framework from SDL's release, kept in obj/ between runs
FRAMEWORKS="$ROOT/obj/SDL2-$SDL2_VERSION"
if [ ! -d "$FRAMEWORKS/SDL2.framework" ]; then
    curl -fsSL -o "$WORK/SDL2.dmg" \
        "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.dmg"
    hdiutil attach -quiet -nobrowse -readonly -mountpoint "$WORK/SDL2" "$WORK/SDL2.dmg"
    mkdir -p "$FRAMEWORKS"
    ditto "$WORK/SDL2/SDL2.framework" "$FRAMEWORKS/SDL2.framework"
    hdiutil detach -quiet "$WORK/SDL2"
fi

build() {
    need alr
    # In bin/app, obj/app and lib/app, so bin/joularenergymeter stays the one that runs from
    # here. gprbuild splits these on spaces.
    alr --non-interactive build -- --subdirs=app -XSDL2_CFLAGS="-F$FRAMEWORKS" \
        -XSDL2_LIBS="-F$FRAMEWORKS -framework SDL2 -Wl,-rpath,@executable_path/../Frameworks"

    # The framework isn't beside the program yet
    DYLD_FRAMEWORK_PATH="$FRAMEWORKS"
    export DYLD_FRAMEWORK_PATH
    check "bin/app/$NAME"
    unset DYLD_FRAMEWORK_PATH

    # Only SDL2.framework and system frameworks, nothing from Homebrew or the toolchain
    otool -L "bin/app/$NAME"
    needed=$(otool -L "bin/app/$NAME" | tail -n +2)
    if echo "$needed" | grep -Eq "gnat|gnarl|homebrew|/usr/local"; then
        echo "the program needs a library from Homebrew or the toolchain" >&2
        exit 1
    fi
    if ! echo "$needed" | grep -q "@rpath/SDL2.framework"; then
        echo "the program isn't linked against SDL2.framework" >&2
        exit 1
    fi
}

package() {
    APP="$WORK/Joular Energy Meter.app"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
    lipo -create "$@" -output "$APP/Contents/MacOS/$NAME"
    chmod 755 "$APP/Contents/MacOS/$NAME"
    ditto "$FRAMEWORKS/SDL2.framework" "$APP/Contents/Frameworks/SDL2.framework"
    sed "s/@VERSION@/$VERSION/g" "$HERE/Info.plist" > "$APP/Contents/Info.plist"
    plutil -lint "$APP/Contents/Info.plist"
    cp assets/icon.icns "$APP/Contents/Resources/"
    docs "$APP/Contents/Resources"

    # SDL2.framework keeps SDL's own signature
    codesign --force --sign - "$APP"
    codesign --verify --strict "$APP"
    "$APP/Contents/MacOS/$NAME" --version

    ARCHS=$(lipo -archs "$APP/Contents/MacOS/$NAME")
    case $ARCHS in
        *" "*) SUFFIX=universal ;;
        *) SUFFIX=$ARCHS ;;
    esac

    # zip: the app with the README and licences beside it
    mkdir -p "$WORK/zip/Joular Energy Meter"
    ditto "$APP" "$WORK/zip/Joular Energy Meter/Joular Energy Meter.app"
    docs "$WORK/zip/Joular Energy Meter"
    ditto -c -k --norsrc --noextattr --noqtn --keepParent "$WORK/zip/Joular Energy Meter" \
        "$DIST/$NAME-$VERSION-macos-$SUFFIX.zip"

    # dmg: the app and a link to Applications. hdiutil is sometimes busy on CI runners: retry.
    mkdir -p "$WORK/dmg"
    ditto "$APP" "$WORK/dmg/Joular Energy Meter.app"
    ln -s /Applications "$WORK/dmg/Applications"
    DMG="$DIST/$NAME-$VERSION-macos-$SUFFIX.dmg"
    for attempt in 1 2 3; do
        hdiutil create -quiet -ov -volname "Joular Energy Meter" -srcfolder "$WORK/dmg" \
            -format UDZO "$DMG" && break
        [ "$attempt" = 3 ] && exit 1
        sleep 5
    done
    hdiutil verify -quiet "$DMG"

    ls -l "$DIST"
}

case ${1:-} in
    build) build ;;
    package) shift; package "$@" ;;
    "") build; package "bin/app/$NAME" ;;
    *) echo "usage: $0 [build | package BINARY...]" >&2; exit 2 ;;
esac
