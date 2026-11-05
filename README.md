# Star Fighter (3DO) – SDL2 port, work in progress

A port of the 3DO game *Star Fighter* (Krisalis / Fednet) to portable C + SDL2, targeting Linux x86-64.
The original is 32-bit big-endian ARM C plus ~13,000 lines of ARM assembler, drawing everything
through the 3DO's hardware cel engine.

**Status: all assembly is translated; the game is untested beyond the menus and the first moments of a
mission.** Menus, briefing, HUD, sky, textured landscape, your ship and the airfield buildings render;
audio and timing were confirmed good on real SDL2 by a tester. Flying, combat and later missions have not
been exercised, and video is skipped when absent. See `PORTING_NOTES.md`.

## Build

    make                 # needs SDL2 development files (libsdl2-dev) – see "SDL status" below
    make HEADLESS=1      # no SDL: frame dumps + scripted input, used for testing

Windows (MinGW-w64): in an MSYS2 MinGW shell just run `make`. To cross-compile from Linux, unpack the SDL2
"mingw" development package and run

    make CC=x86_64-w64-mingw32-gcc SDL2_PREFIX=/path/to/SDL2-x.y.z/x86_64-w64-mingw32

which produces `starfighter.exe` (copy `SDL2.dll` next to it; `WINGUI=1` hides the console window). If audio crashes, set `SDL_AUDIODRIVER` (the Windows build defaults to `directsound`). Saves go to
`%APPDATA%\starfighter`. The Windows build is checked by `make test` when a MinGW cross compiler is available (cross-compiled
for 32- and 64-bit Windows, with recipes run under cmd.exe rules as `mingw32-make` does; see
`tests/README.md`); it has not been run on Windows by me.

Plain GNU Make; no CMake. On x86 the build needs a CPU with SSE4.1 (2008 or later); `make SSE41=0`
builds for older ones (about a third slower).

## Run

    ./starfighter /path/to/SF_Resources          # or set SF_RESOURCES, or run from a dir containing SF_Resources/

Keys (default): arrows = D-pad, Z/Ctrl = A, X/Alt = B, C = C, Enter/Space = Start, Esc/Backspace = X (stop),
A/S = left/right shift. The keyboard layout is read from `config.txt` in the directory the game is
started from; if there is none, the game writes one with the defaults and comments, ready to edit
(one line per pad control, e.g. `START = Return, Space`; delete it to get the defaults back). Each key
may be used for one control only; if the file names an unknown key or control, or uses a key for two
controls (including a built-in key of a control the file does not list), the game lists every problem
(on the console and in a message box) and exits. A game controller works too and is not affected by
`config.txt`. If audio crashes, set `SDL_AUDIODRIVER` (the Windows build defaults to `directsound`). Saves go to `$SF_SAVEDIR`,
else `$XDG_DATA_HOME/starfighter`, else `~/.local/share/starfighter`.

**Resolution.** `SCALE = n` in `config.txt` (1 to 4, default 1) draws the game at 320x240 times n
(2 = 640x480, 4 = 1280x960); `SF_SCALE=n` in the environment overrides it. The 3D view is projected at
the higher resolution with sub-pixel precision: polygon edges, landscape, lasers and lock-on boxes get
sharper and distant land shows more texture detail, at the same size on screen. The HUD, cockpit,
menus, map, text, backdrops and videos are the original art and keep their size and look (each pixel
becomes n x n). The game plays exactly the same at every scale. Higher scales cost CPU time: see
`PORTING_NOTES.md` §12 for measured frame rates.

**Draw distances.** Also in `config.txt`, in map squares (the map is 256 x 256 squares and wraps):
`LAND_NEAR` (16), `LAND_MEDIUM` (32) and `LAND_FAR` (64) set how far the landscape is drawn in full
detail, in medium detail and with hills and mountains (flat beyond); `LAND_NEAR_ALTITUDE` (32),
`LAND_MEDIUM_ALTITUDE` (48) and `LAND_FAR_ALTITUDE` (64) the camera altitude above which each detail
level is dropped; `OBJECT_DISTANCE` (64) how far 3D objects are drawn (fighters, ground vehicles and
buildings within half of it); `BUILDING_ALTITUDE` (40) the camera altitude above which buildings and
other ground objects are not drawn. The values in brackets are the game's own and are written into a new
`config.txt`; the file explains the allowed ranges. Only drawing changes: lock-on range, which
ships count as visible and the game itself are unaffected. Larger distances cost CPU time.

## Resources

`SF_Resources` is the game's data. The backdrops (`Images/*.png`) and fonts (`Fonts/*.png` + `.txt`)
are converted from the original 3DO formats (frame-buffer dumps, FontLib files); everything else is
used as shipped. PNG decoding uses the bundled public-domain `src/stb_image.h`.

## Video

Videos are the game's 3DO stream files (Cinepak video + SDX2 audio) in `SF_Resources/Video/`. They are not
part of the source release, so every video is **skipped** if its file is missing. Playback is implemented
(`SF_Video.c`, `sf_cinepak.c`). It was tested with two of the game's videos (one of them `into1`, which the game
plays elsewhere) and `Krisalis`. File names are matched case-insensitively and exactly otherwise: there are no
aliases, so a video is only played if a file with the name the game asks for exists.
A video ends when it finishes or when you *newly* press a button (a button already held when the video
starts is ignored until released).

## Testing

    make test            # build checks, unit tests, scripted scenarios vs goldens, SDL smoke (~2 min)
    make test-asan       # the same under AddressSanitizer + UBSan
    make test-full       # everything, including the long scenarios (~6 min)
    make test-performance  # frames per second of the game engine, per scenario (SCALE=n: at scale n)
    make celtrace; make celbench   # record in-game cel lists; replay them (exact output + timing)

Needs Python 3 and `SF_Resources` (`RES=/path/to/SF_Resources`); see `tests/README.md`.

Both builds read test settings from the environment (`src/sf_testhooks.h`): `SF_SCRIPT=file`
(lines `first_tick last_tick BUTTON[+BUTTON]`), `SF_MAXFRAMES=n`, `SF_DUMP=dir`, `SF_DUMP_EVERY=n`,
`SF_HASH=file`, `SF_STATE=file`, `SF_AUDIO_DUMP=file.wav`, `SF_RECORD=file` (record your own play as
a script), `SF_WATCHDOG=seconds` (prints a backtrace if the game hangs), `SF_TRACE_PAD=1`,
`SF_SCALE=n` (render at scale n). The
headless build (`make HEADLESS=1`) runs on a virtual clock and is fully deterministic;
`tools/headless-smoke.sh /path/to/SF_Resources /tmp/out` dumps frames of a short session.
