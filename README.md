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
`%APPDATA%\starfighter`. This Windows build has not been compiled or run by me: no MinGW toolchain was available here.

Plain GNU Make; no CMake.

## Run

    ./starfighter /path/to/SF_Resources          # or set SF_RESOURCES, or run from a dir containing SF_Resources/

Keys: arrows = D-pad, Z/Ctrl = A, X/Alt = B, C = C, Enter/Space = Start, Esc/Backspace = X (stop),
A/S = left/right shift. A game controller works too. If audio crashes, set `SDL_AUDIODRIVER` (the Windows build defaults to `directsound`). Saves go to `$SF_SAVEDIR`,
else `$XDG_DATA_HOME/starfighter`, else `~/.local/share/starfighter`.

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
    make test-performance  # frames per second of the game engine, per scenario

Needs Python 3 and `SF_Resources` (`RES=/path/to/SF_Resources`); see `tests/README.md`.

Both builds read test settings from the environment (`src/sf_testhooks.h`): `SF_SCRIPT=file`
(lines `first_tick last_tick BUTTON[+BUTTON]`), `SF_MAXFRAMES=n`, `SF_DUMP=dir`, `SF_DUMP_EVERY=n`,
`SF_HASH=file`, `SF_STATE=file`, `SF_AUDIO_DUMP=file.wav`, `SF_RECORD=file` (record your own play as
a script), `SF_WATCHDOG=seconds` (prints a backtrace if the game hangs), `SF_TRACE_PAD=1`. The
headless build (`make HEADLESS=1`) runs on a virtual clock and is fully deterministic;
`tools/headless-smoke.sh /path/to/SF_Resources /tmp/out` dumps frames of a short session.
