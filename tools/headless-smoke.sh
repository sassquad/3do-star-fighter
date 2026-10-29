#!/bin/sh
# Smoke test: build the headless binary, boot to the main menu, start a new game, and dump frames.
#   tools/headless-smoke.sh /path/to/SF_Resources [outdir]
# View the resulting PPM files with any image viewer (or convert with ImageMagick / Pillow).
set -e
RES=${1:?usage: $0 /path/to/SF_Resources [outdir]}
OUT=${2:-/tmp/sf-smoke}
cd "$(dirname "$0")/.."
make HEADLESS=1
mkdir -p "$OUT"; rm -f "$OUT"/*.ppm
SCRIPT=$(mktemp)
# DOWN selects NEW GAME on the first menu, A presses the highlighted entry; later presses step through screens
printf '300 304 DOWN\n330 336 A\n480 486 A\n600 606 A\n760 766 A\n900 906 A\n1100 1106 A\n' > "$SCRIPT"
SF_SCRIPT=$SCRIPT SF_DUMP=$OUT SF_DUMP_EVERY=40 SF_MAXFRAMES=2400 SF_WATCHDOG=120 \
SF_SAVEDIR=$OUT/save ./starfighter-headless "$RES"
echo "frames written to $OUT"
