#!/bin/bash
# build-sdl3-libusb.sh - build SDL3 with libusb support for the macOS app (see BUILDING-macOS.md).
#
#   tools/build-sdl3-libusb.sh            (or: make sdl3-libusb)
#
# Wired Xbox One / Series controllers (Microsoft's and third-party ones such as PowerA) do not use
# the standard USB game-controller (HID) protocol, and macOS has no driver for them over USB.
# SDL3 can drive them itself through libusb, but Homebrew's SDL3 is built without libusb.
# This script downloads the SDL3 source release, checks its SHA-256, and builds it with libusb
# linked in, installing to build/deps/sdl3-install. tools/make-macos-app.sh then bundles that SDL3
# and libusb into the app instead of Homebrew's SDL3.
#
# Needs: brew install cmake libusb
set -euo pipefail

SDL3_VERSION=3.4.20
SDL3_SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f

cd "$(dirname "$0")/.."
DEPS=$(pwd)/build/deps
TARBALL="$DEPS/SDL3-$SDL3_VERSION.tar.gz"
SRC="$DEPS/src/SDL3-$SDL3_VERSION"
PREFIX="$DEPS/sdl3-install"

BREW=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
CMAKE="$BREW/bin/cmake"
[ -x "$CMAKE" ] || CMAKE=$(command -v cmake || true)
[ -n "$CMAKE" ] || { echo "error: cmake not found (brew install cmake)" >&2; exit 1; }
[ -f "$BREW/opt/libusb/lib/libusb-1.0.0.dylib" ] || { echo "error: libusb not found (brew install libusb)" >&2; exit 1; }
ARCH=$(uname -m)

mkdir -p "$DEPS/src"
if [ ! -f "$TARBALL" ]; then
  echo "== downloading SDL3 $SDL3_VERSION"
  curl -sSLf -o "$TARBALL.part" \
    "https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz"
  mv "$TARBALL.part" "$TARBALL"
fi
echo "$SDL3_SHA256  $TARBALL" | shasum -a 256 -c - >/dev/null \
  || { echo "error: checksum mismatch for $TARBALL (deleted; run again)" >&2; rm -f "$TARBALL"; exit 1; }
[ -d "$SRC" ] || tar -xzf "$TARBALL" -C "$DEPS/src"

echo "== configuring SDL3 ($ARCH, libusb linked in)"
"$CMAKE" -S "$SRC" -B "$DEPS/sdl3-build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$BREW" \
  -DSDL_HIDAPI_LIBUSB=ON -DSDL_HIDAPI_LIBUSB_SHARED=OFF \
  -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >"$DEPS/sdl3-configure.log"
grep -q 'SDL_HIDAPI_LIBUSB *(Wanted: ON): ON' "$DEPS/sdl3-configure.log" \
  || { echo "error: SDL3 configured without libusb (see $DEPS/sdl3-configure.log)" >&2; exit 1; }

echo "== building SDL3"
"$CMAKE" --build "$DEPS/sdl3-build" -j"$(sysctl -n hw.ncpu)" >"$DEPS/sdl3-build.log"
"$CMAKE" --install "$DEPS/sdl3-build" >/dev/null
otool -L "$PREFIX/lib/libSDL3.0.dylib" | grep -q libusb \
  || { echo "error: the built libSDL3 does not link libusb" >&2; exit 1; }
echo "== done: $PREFIX/lib/libSDL3.0.dylib (SDL3 $SDL3_VERSION with libusb)"
