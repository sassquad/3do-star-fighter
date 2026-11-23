#!/bin/bash
# make-macos-app.sh - build "Star Fighter.app", a self-contained macOS application bundle.
#
#   tools/make-macos-app.sh [--no-resources]        (or: make app [RES=/path/to/SF_Resources])
#
# Builds the SDL2 game (objects in build/macapp/obj) and assembles build/Star Fighter.app:
#
#   Contents/MacOS/launch             launcher script (the bundle's executable, see below)
#   Contents/MacOS/starfighter        the game
#   Contents/Frameworks/              the SDL2 dylib the game links against, plus libSDL3.dylib when
#                                     that SDL2 is sdl2-compat (Homebrew's "sdl2" today), which loads
#                                     SDL3 at run time from its own directory. The SDL3 is the one
#                                     built by tools/build-sdl3-libusb.sh if present (with libusb,
#                                     bundled too, for wired Xbox controllers), else Homebrew's
#   Contents/Resources/SF_Resources   the game data (copied from $RES, $SF_RESOURCES or ./SF_Resources;
#                                     --no-resources leaves it out: the app then needs $SF_RESOURCES)
#   Contents/Resources/StarFighter.icns  made from SF_Resources/Images/Menu0.png
#
# The launcher runs the game in ~/Library/Application Support/Star Fighter, so config.txt (keys,
# SCALE, draw distances) is written and read there, saves go to its Saves/ folder, and console output
# goes to starfighter.log there when the app is started from the Finder.
#
# The bundle is ad-hoc signed, which is enough to run it on this Mac. Other Macs will need it signed
# with a Developer ID and notarized, or the user must allow it in System Settings > Privacy & Security.
# It runs on the same CPU type (arm64 / x86_64) and macOS version as the SDL libraries it copies.
set -euo pipefail

cd "$(dirname "$0")/.."
ROOT=$(pwd)
APP_NAME="Star Fighter"
APP="build/$APP_NAME.app"
WITH_RES=1
[ "${1:-}" = "--no-resources" ] && WITH_RES=0

RES=${RES:-${SF_RESOURCES:-$ROOT/SF_Resources}}
RES=${RES%/}
if [ $WITH_RES = 1 ] && [ ! -d "$RES/Data" ]; then
  echo "error: no game data at '$RES' (set RES=/path/to/SF_Resources or pass --no-resources)" >&2
  exit 1
fi

echo "== building the game"
make -j"$(sysctl -n hw.ncpu)" OBJDIR=build/macapp/obj BINDIR=build/macapp
BIN=build/macapp/starfighter

# ---- find the SDL2 library the game was linked against ------------------------------------------
SDL2_REF=$(otool -L "$BIN" | awk '/libSDL2/ {print $1; exit}')
[ -n "$SDL2_REF" ] || { echo "error: $BIN does not link libSDL2" >&2; exit 1; }
SDL2_SRC=$SDL2_REF
case "$SDL2_SRC" in
  @rpath/*)  # resolve against the library directory sdl2-config reports
    SDL2_SRC="$(sdl2-config --prefix)/lib/${SDL2_SRC#@rpath/}" ;;
esac
[ -f "$SDL2_SRC" ] || { echo "error: cannot find $SDL2_SRC" >&2; exit 1; }
SDL2_NAME=$(basename "$SDL2_SRC")

SDL3_SRC=
LIBUSB_REF=
if strings "$SDL2_SRC" | grep -q '@loader_path/libSDL3.dylib'; then
  # sdl2-compat: it dlopen()s SDL3, looking first at @loader_path/libSDL3.dylib.
  # Prefer the SDL3 built with libusb by tools/build-sdl3-libusb.sh: it can drive wired Xbox
  # controllers, which macOS has no driver for. SDL3_LIB=/path/to/libSDL3.0.dylib overrides.
  SDL3_SRC=${SDL3_LIB:-}
  if [ -z "$SDL3_SRC" ] && [ -f build/deps/sdl3-install/lib/libSDL3.0.dylib ]; then
    SDL3_SRC=build/deps/sdl3-install/lib/libSDL3.0.dylib
  fi
  if [ -z "$SDL3_SRC" ]; then
    SDL3_DIR=$(pkg-config --variable=libdir sdl3 2>/dev/null || true)
    [ -n "$SDL3_DIR" ] || SDL3_DIR="$(brew --prefix sdl3 2>/dev/null)/lib"
    SDL3_SRC="$SDL3_DIR/libSDL3.0.dylib"
  fi
  [ -f "$SDL3_SRC" ] || { echo "error: $SDL2_SRC is sdl2-compat but SDL3 was not found ($SDL3_SRC)" >&2; exit 1; }
  LIBUSB_REF=$(otool -L "$SDL3_SRC" | awk '/libusb/ {print $1; exit}')
  if [ -n "$LIBUSB_REF" ]; then
    echo "   SDL3 with libusb: wired Xbox controllers supported ($SDL3_SRC)"
  else
    echo "   note: this SDL3 has no libusb, so wired Xbox controllers will not work;"
    echo "         run tools/build-sdl3-libusb.sh (make sdl3-libusb) first to add it"
  fi
fi

# ---- assemble the bundle ------------------------------------------------------------------------
echo "== assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"
C="$APP/Contents"

cp "$BIN" "$C/MacOS/starfighter"
cp "$SDL2_SRC" "$C/Frameworks/$SDL2_NAME"
[ -n "$SDL3_SRC" ] && cp "$SDL3_SRC" "$C/Frameworks/libSDL3.dylib"
[ -n "$LIBUSB_REF" ] && cp "$LIBUSB_REF" "$C/Frameworks/$(basename "$LIBUSB_REF")"
chmod u+w "$C/Frameworks/"*

# point the game at the bundled SDL2 and give the bundled libraries bundle-relative ids
install_name_tool -change "$SDL2_REF" "@executable_path/../Frameworks/$SDL2_NAME" "$C/MacOS/starfighter" 2>/dev/null
install_name_tool -id "@executable_path/../Frameworks/$SDL2_NAME" "$C/Frameworks/$SDL2_NAME" 2>/dev/null
[ -n "$SDL3_SRC" ] && install_name_tool -id "@executable_path/../Frameworks/libSDL3.dylib" "$C/Frameworks/libSDL3.dylib" 2>/dev/null
if [ -n "$LIBUSB_REF" ]; then
  LIBUSB_NAME=$(basename "$LIBUSB_REF")
  install_name_tool -change "$LIBUSB_REF" "@loader_path/$LIBUSB_NAME" "$C/Frameworks/libSDL3.dylib" 2>/dev/null
  install_name_tool -id "@loader_path/$LIBUSB_NAME" "$C/Frameworks/$LIBUSB_NAME" 2>/dev/null
fi

# nothing may still refer to Homebrew / /usr/local
if otool -L "$C/MacOS/starfighter" "$C/Frameworks/"*.dylib | grep -E '^\s+/(opt|usr/local)/' ; then
  echo "error: the bundle still depends on libraries outside it (above)" >&2
  exit 1
fi

cat > "$C/MacOS/launch" <<'EOF'
#!/bin/sh
# Star Fighter launcher: run the game with its settings and saves in Application Support.
HERE=$(cd "$(dirname "$0")" && pwd)
SUPPORT="$HOME/Library/Application Support/Star Fighter"
mkdir -p "$SUPPORT" && cd "$SUPPORT" || exit 1
SF_SAVEDIR=${SF_SAVEDIR:-$SUPPORT/Saves}
export SF_SAVEDIR
RES=${SF_RESOURCES:-$HERE/../Resources/SF_Resources}
# started from the Finder: keep the console output in a log
[ -t 2 ] || exec >>"$SUPPORT/starfighter.log" 2>&1
[ -t 2 ] || echo "---- $(date)"
exec "$HERE/starfighter" "$RES"
EOF
chmod +x "$C/MacOS/launch"

if [ $WITH_RES = 1 ]; then
  echo "== copying game data from $RES ($(du -sh "$RES" | cut -f1))"
  rsync -a --exclude .DS_Store "$RES/" "$C/Resources/SF_Resources/"
fi

# icon: the centre square of the first menu backdrop
ICON_SRC="$RES/Images/Menu0.png"
HAVE_ICON=0
if [ -f "$ICON_SRC" ]; then
  ICONSET=build/macapp/StarFighter.iconset
  rm -rf "$ICONSET" && mkdir -p "$ICONSET"
  sips -c 240 240 "$ICON_SRC" --out build/macapp/icon-square.png >/dev/null
  for s in 16 32 128 256 512; do
    sips -z $s $s build/macapp/icon-square.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) build/macapp/icon-square.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
  done
  iconutil -c icns "$ICONSET" -o "$C/Resources/StarFighter.icns" && HAVE_ICON=1
fi

# the oldest macOS the copied binaries allow
MINOS=$(otool -l "$C/MacOS/starfighter" "$C/Frameworks/"*.dylib | awk '$1 == "minos" {print $2}' | sort -t. -k1,1n -k2,2n | tail -1)
VERSION=$(date +%Y.%m.%d)

cat > "$C/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key>                 <string>$APP_NAME</string>
  <key>CFBundleDisplayName</key>          <string>$APP_NAME</string>
  <key>CFBundleIdentifier</key>           <string>local.starfighter.sdl</string>
  <key>CFBundleExecutable</key>           <string>launch</string>
  <key>CFBundlePackageType</key>          <string>APPL</string>
  <key>CFBundleShortVersionString</key>   <string>$VERSION</string>
  <key>CFBundleVersion</key>              <string>$VERSION</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  $( [ $HAVE_ICON = 1 ] && echo '<key>CFBundleIconFile</key>             <string>StarFighter</string>' )
  <key>LSMinimumSystemVersion</key>       <string>${MINOS:-11.0}</string>
  <key>LSApplicationCategoryType</key>    <string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key>      <true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
EOF
plutil -lint "$C/Info.plist" >/dev/null

echo "== signing (ad hoc)"
codesign --force --sign - "$C/Frameworks/"*.dylib
codesign --force --sign - "$C/MacOS/starfighter"
codesign --force --sign - "$APP"
codesign --verify --deep --strict "$APP"

echo "== done: $ROOT/$APP ($(du -sh "$APP" | cut -f1), macOS ${MINOS:-?}+, $(lipo -archs "$C/MacOS/starfighter"))"
