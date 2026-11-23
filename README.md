# Star Fighter (3DO) – SDL2 port

A port of the 3DO game *Star Fighter* (Krisalis / Fednet, 1995) to portable C and SDL2. It builds
on Linux, Windows and macOS.

The original is 32-bit big-endian ARM C plus about 13,000 lines of ARM assembler, and it draws
everything through the 3DO's hardware cel engine. This port translates all of it to C, with a
software renderer for the cel engine.

> [!NOTE]
> **Work in progress.** All the assembly is translated, but the game is untested beyond the menus
> and the first moments of a mission. Menus, briefing, HUD, sky, textured landscape, your ship and
> the airfield buildings render; a tester confirmed audio and timing on real SDL2. Flying, combat
> and later missions have not been exercised. See [PORTING_NOTES.md](PORTING_NOTES.md).

**On a Mac?** Follow [BUILDING-macOS.md](BUILDING-macOS.md), a step-by-step guide for first-time
users that ends with a double-clickable app.

## Contents

- [Build](#build)
- [Run](#run)
- [Settings (`config.txt`)](#settings-configtxt)
- [Resources](#resources)
- [Video](#video)
- [Testing](#testing)
- [Credits](#credits)
- [Licence](#licence)

## Build

Plain GNU Make; no CMake. Every platform needs the SDL2 development files.

### Linux

```sh
make                 # needs libsdl2-dev
make HEADLESS=1      # no SDL: frame dumps and scripted input, used for testing
```

### Windows (MinGW-w64)

In an MSYS2 MinGW shell, run `make`. This produces `starfighter.exe`; copy `SDL2.dll` next to it.

To cross-compile from Linux, unpack the SDL2 "mingw" development package and run:

```sh
make CC=x86_64-w64-mingw32-gcc SDL2_PREFIX=/path/to/SDL2-x.y.z/x86_64-w64-mingw32
```

- `WINGUI=1` hides the console window.
- Saves go to `%APPDATA%\starfighter`.
- `make test` checks the Windows build when a MinGW cross compiler is available. It cross-compiles
  for 32- and 64-bit Windows and runs the recipes under `cmd.exe` rules, as `mingw32-make` does
  (see [tests/README.md](tests/README.md)). The porter has not run the Windows build on Windows.

### macOS

```sh
brew install sdl2
make app             # builds "build/Star Fighter.app", with SDL and the game data inside
```

For a wired Xbox One / Series controller, run `make sdl3-libusb` once before `make app`
(needs `brew install cmake libusb`). [BUILDING-macOS.md](BUILDING-macOS.md) covers everything
step by step. [BUILDING-macOS.html](BUILDING-macOS.html) is the same guide as a web page:
download it and open it in a browser.

### Build options

| Option | Effect |
| --- | --- |
| `HEADLESS=1` | Build without SDL, for testing (see [Testing](#testing)) |
| `SSE41=0` | On x86 the build needs a CPU with SSE4.1 (2008 or later); this builds for older ones, about a third slower |

## Run

```sh
./starfighter /path/to/SF_Resources
```

Instead of giving the path, you can set `SF_RESOURCES`, or run the game from a directory that
contains `SF_Resources/`.

### Default keys

Each key stands in for a button on the 3DO joypad.

| Key | 3DO pad | In menus | In flight |
| --- | --- | --- | --- |
| Arrow keys | D-pad | Move | Steer |
| **Z** or Ctrl | A | **Select** | Thrust |
| X or Alt | B | | Fire ECM |
| C | C | | Change camera view |
| Return or Space | Start | Back | Map |
| Esc or Backspace | X (stop) | Back | In-flight menu |
| A | Left shift | | Switch weapon |
| **S** | Right shift | | **Fire** |

The in-flight actions are the game's defaults; its flight controls menu can reassign them.

A game controller (Xbox, PlayStation, Switch Pro) works too, and `config.txt` doesn't affect it.

### Saves

Saves go to `$SF_SAVEDIR`, else `$XDG_DATA_HOME/starfighter`, else `~/.local/share/starfighter`.
On Windows they go to `%APPDATA%\starfighter`, and the macOS app keeps them in
`~/Library/Application Support/Star Fighter/Saves`.

### If audio crashes

Set `SDL_AUDIODRIVER`. The Windows build defaults to `directsound`.

## Settings (`config.txt`)

The game reads `config.txt` from the directory it is started from. If there is none, it writes one
with the defaults and comments, ready to edit. Delete it to get the defaults back.

### Keyboard layout

One line per pad control, for example `START = Return, Space`.

- Each key may be used for one control only.
- If the file names an unknown key or control, or uses a key for two controls (including a built-in
  key of a control the file doesn't list), the game lists every problem, on the console and in a
  message box, and exits.

### Resolution

`SCALE = n` (1 to 4, default 1) draws the game at 320×240 times *n*: 2 is 640×480, 4 is 1280×960.
The `SF_SCALE=n` environment variable overrides it.

- The 3D view is projected at the higher resolution with sub-pixel precision. Polygon edges,
  landscape, lasers and lock-on boxes get sharper, and distant land shows more texture detail, at
  the same size on screen.
- The HUD, cockpit, menus, map, text, backdrops and videos are the original art and keep their size
  and look: each pixel becomes *n*×*n*.
- The game plays exactly the same at every scale.
- Higher scales cost CPU time. See [PORTING_NOTES.md](PORTING_NOTES.md) §12 for measured frame rates.

### Draw distances

Distances are in map squares; the map is 256×256 squares and wraps. The defaults are the game's own
and are written into a new `config.txt`, which explains the allowed ranges.

| Setting | Default | Controls |
| --- | --- | --- |
| `LAND_NEAR` | 16 | How far the landscape is drawn in full detail |
| `LAND_MEDIUM` | 32 | How far it is drawn in medium detail |
| `LAND_FAR` | 64 | How far hills and mountains are drawn (flat beyond) |
| `LAND_NEAR_ALTITUDE` | 32 | Camera altitude above which full detail is dropped |
| `LAND_MEDIUM_ALTITUDE` | 48 | Camera altitude above which medium detail is dropped |
| `LAND_FAR_ALTITUDE` | 64 | Camera altitude above which far detail is dropped |
| `OBJECT_DISTANCE` | 64 | How far 3D objects are drawn (fighters, ground vehicles and buildings within half of it) |
| `BUILDING_ALTITUDE` | 40 | Camera altitude above which buildings and other ground objects are not drawn |

Only drawing changes: lock-on range, which ships count as visible, and the game itself are
unaffected. Larger distances cost CPU time.

## Resources

`SF_Resources` is the game's data.

- The backdrops (`Images/*.png`) and fonts (`Fonts/*.png` and `.txt`) are converted from the
  original 3DO formats (frame-buffer dumps and FontLib files).
- Everything else is used as shipped.
- PNG decoding uses the bundled public-domain [`src/stb_image.h`](src/stb_image.h).

## Video

Videos are the game's 3DO stream files (Cinepak video and SDX2 audio) in `SF_Resources/Video/`.
Playback is implemented in [`src/SF_Video.c`](src/SF_Video.c) and
[`src/sf_cinepak.c`](src/sf_cinepak.c).

- Videos were not part of the original source release, so each video is **skipped** if its file is
  missing.
- File names are matched case-insensitively and otherwise exactly. There are no aliases, so a
  video only plays if a file with the name the game asks for exists.
- It was tested with two of the game's videos (one of them `into1`, which the game plays elsewhere)
  and `Krisalis`.
- A video ends when it finishes or when you *newly* press a button. A button already held when the
  video starts is ignored until it is released.

## Testing

```sh
make test              # build checks, unit tests, scripted scenarios vs goldens, SDL smoke (~2 min)
make test-asan         # the same under AddressSanitizer + UBSan
make test-full         # everything, including the long scenarios (~6 min)
make test-performance  # frames per second of the game engine, per scenario (SCALE=n: at scale n)
make celtrace          # record in-game cel lists
make celbench          # replay them (exact output and timing)
```

The tests need Python 3 and `SF_Resources` (`RES=/path/to/SF_Resources`); see
[tests/README.md](tests/README.md).

The headless build (`make HEADLESS=1`) runs on a virtual clock and is fully deterministic.
`tools/headless-smoke.sh /path/to/SF_Resources /tmp/out` dumps frames of a short session.

Both builds read these test settings from the environment (see
[`src/sf_testhooks.h`](src/sf_testhooks.h)):

| Variable | Effect |
| --- | --- |
| `SF_SCRIPT=file` | Scripted input, one line per event: `first_tick last_tick BUTTON[+BUTTON]` |
| `SF_MAXFRAMES=n` | Exit after *n* frames |
| `SF_DUMP=dir` | Write each frame as an image in *dir* |
| `SF_DUMP_EVERY=n` | With `SF_DUMP`, write only every *n*th frame |
| `SF_HASH=file` | Write a hash of each frame |
| `SF_STATE=file` | Write key game state for each frame |
| `SF_AUDIO_DUMP=file.wav` | Write the mixed audio output as a WAV file |
| `SF_RECORD=file` | Record your own play (keyboard or controller) as an `SF_SCRIPT` file |
| `SF_WATCHDOG=seconds` | Print a backtrace if the game is still running after this long (finds hangs) |
| `SF_TRACE_PAD=1` | Log scripted button presses |
| `SF_SCALE=n` | Render at scale *n* (overrides `SCALE` in `config.txt`) |

## Credits

- **Star Fighter** was created by Fednet Software and released for the Acorn Archimedes in 1994.
  The 3DO version (1995) was developed by Tim Parry and Andrew Hutchings.
- **The 3DO source code** was made available by Nathan Atkinson, with permission from Andrew
  Hutchings, on 24 April 2024, after 3DO community member `shaun_3dohd` asked about releasing it.
  Antonio SJ Musumeci ([trapexit](https://github.com/trapexit)) published it at
  [trapexit/3do-star-fighter](https://github.com/trapexit/3do-star-fighter).
- **The SDL2 port** is by Peter Howkins ([marutan](https://github.com/marutan)). Its home is
  marutan's Mercurial repository, <https://home.marutan.net/hg/star-fighter-sdl>; early work is on
  the `bodging` branch of [marutan/3do-star-fighter](https://github.com/marutan/3do-star-fighter).
- **The macOS app build**, game controller additions and the macOS guide are by Stephen Scott
  ([sassquad](https://github.com/sassquad)).
- **Libraries:** [SDL](https://www.libsdl.org) (zlib licence), and
  [stb_image](https://github.com/nothings/stb) by Sean Barrett (public domain).

### This repository

| Branch | Contents |
| --- | --- |
| `macos` (default) | The SDL2 port plus the macOS additions |
| `sdl-port` | The SDL2 port's history, converted from marutan's Mercurial repository, up to release `r0.0.1` |
| `master` | The original 3DO source release, as published by trapexit |
| `bodging` | marutan's early port work |

## Licence

No licence has been declared for the game, the original source release or the port. The game's
code and data remain the property of their copyright holders, and the source was released with
their permission. Credit the people above if you use this work.
