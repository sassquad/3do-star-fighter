# Regression tests

Everything runs on the build machine without a display or sound card. You need GNU make, a C
compiler, Python 3 (standard library only) and `SF_Resources`. clang and the SDL2 development files
are optional; the checks that need them are skipped without them.

    make test            # build checks, unit tests, quick scenarios, determinism, SDL smoke    (~2 min)
    make test-asan       # unit tests + quick scenarios built with AddressSanitizer and UBSan  (~2 min)
    make test-full       # everything above, plus every scenario, also under ASan                (~6 min)
    make rebless         # re-record all goldens after an intentional change
    make rebless SCENARIOS="crash-and-game-over weapons-and-views"
    make test-performance # frames per second of the game engine, per scenario         (~4 min)

`RES=/path/to/SF_Resources` picks the resources (default `$SF_RESOURCES`, else `./SF_Resources`).
The same commands are available as `tests/run.py quick|asan|full|rebless|make-fixtures|list`, with
`--only TEXT` (run matching checks only), `-j N` and `--keep` (keep audio captures).

The run prints one line per check and ends with a list of failures. A full report, with images, is
written to `build/test-report/index.html` (`summary.txt` alongside). Run directories with the logs,
state, frame hashes and checkpoint frames of every scenario are in `build/runs/<variant>/<scenario>/`.
The exit status is non-zero if anything failed.

## What is checked

| Group | What |
|---|---|
| `lint` | the flags the game needs (`-funsigned-char -fwrapv -fno-strict-aliasing`) are on every compile even with `CFLAGS=` on the command line; no 32-bit casts into the owner pointer fields; no implicit 64->32 bit narrowing (clang `-Wshorten-64-to-32`) apart from four known-safe grid-reference sites |
| `build` | headless and SDL builds with `-Werror`; a clang build whose warnings are compared with `tests/clang-warnings.txt` (fails on new ones); the ASan/UBSan build |
| `unit/...` | C unit tests in `tests/unit/` (below), each case reported separately |
| `scenario/...` | scripted runs of the headless game (below): runs to the end, no sanitizer report, behavioural expectations, frames and audio identical to the goldens |
| `determinism` | two scenarios re-run twice more with address randomisation on: frames, state log and audio must be identical (this is how the uninitialised explosion smoke was found) |
| `sdl` | the real SDL build with `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`: boots, takes scripted input, starts a mission |

## Unit tests (`tests/unit/test_*.c`)

Each file is one program built against `libsfcore.a` (the whole game with `main` renamed to
`sf_game_main`, platform `plat_null.c`) and prints TAP. `sft.h` is the harness: `TEST(name)`,
`CHECK`, `CHECK_EQ`, `CHECK_EQX`, `CHECK_STR`, `REQUIRE`, `RUN(name)`, `sft_resources()`,
`sft_tempdir()`. New files are picked up automatically. To build and run one by hand:

    make HEADLESS=1 OBJDIR=build/t/obj BINDIR=build/t unit-tests
    SF_RESOURCES=$PWD/SF_Resources build/t/unit/test_cel

| File | Covers |
|---|---|
| `test_arm_ops.c` | ARM shift/rotate/multiply semantics; the random generator against an independent reference |
| `test_maths.c` | trig tables, `divide`, `find_rotation` vs `atan2`, distances, rotations incl. out-of-range angles, `scan_poly_map` window (regression) |
| `test_armlink.c` | linked lists under 20,000 random operations, resort, suspend/release, z-sort buckets and capacity |
| `test_collision.c` | collision boxes (incl. the reproduced compare-chain quirk), ground height, `GPTR(0) == NULL`, pointer-sized owner fields (regressions) |
| `test_load.c` | word swapping, case-insensitive lookup, LRFORM images (synthetic and every real image) |
| `test_missions.c` | all 62 mission files, planet files and the default configuration |
| `test_nvram.c` | save files: round trip, short files, name sanitising, delete |
| `test_cel.c` | the cel engine pixel by pixel: 16 bpp, coded 1–8 bpp with PLUT, scale, flips, SKIPX, pixel processor, packed cels (regression), cel lists, clipping, last-word overread (regression) |
| `test_armcell.c` | 16x16 ground cels from 4x4 tiles (tile *n* regression), cel cache, 6-bit map packing |
| `test_text.c` | fonts, text widths, empty text (regression), typewriter reveal overrun (regression) |
| `test_cinepak.c` | every frame of every video, a checksum of `Krisalis`, 3,000 corrupted/truncated packets |
| `test_audio.c` | every sample, voice length and pitch, panning and volume, music stream, channel guards (regression); uses a stub platform |
| `test_weapons.c` | SAM owners keep 64-bit pointers (regression: attract-mode crash) |
| `test_bonus.c` | ship damage for every rating (regression: negative shift) |
| `test_testhooks.c` | script parsing and errors, unlimited events, recording and replay, frame limit, hashes, state log, WAV capture |

Several tests only show a regression as a memory error, so run `make test-asan` after touching the
cel engine, text, Cinepak or audio code.

## Scenarios (`tests/scenarios/*.scn`)

A scenario is a scripted headless run. Example:

    # comment
    description: main menu -> mission select -> briefing -> first mission, thrust and fly
    suite: quick                  # quick (make test) or full (make test-full only)
    frames: 2400                  # SF_MAXFRAMES
    fixture: T 8                  # optional: start from a save pointing at Missions/T/MISS_8
    resources: no-video           # optional: run with the Video directory left out
    checkpoint: 300               # frames between checkpoint images (default 300)
    audio: yes                    # capture audio and compare it with the golden (default yes)
    golden: yes                   # compare frames with the golden (default yes)
    performance: yes              # measured by make test-performance (default no)
    expect: max(col('speed', 1300, 1520)) > 1500
    script:
    300 304 DOWN
    330 336 A

`script:` lines are `SF_SCRIPT` events (`first last BUTTON[+BUTTON]`; buttons UP DOWN LEFT RIGHT A B C
START X LS RS). With the default configuration in flight: A = thrust, RS = fire, LS = select weapon,
C = camera, START = map, B = ECM, X = pause menu, arrows = pitch/roll. In the name-entry screen the
cursor starts on "A"; START finishes. Useful timings: the first video is skipped by any button, the
main menu appears ~120 ticks later.

`expect:` lines are Python expressions over the state log (`SF_STATE`, one row per frame with the
columns `frame level menu status score lives weapon air_kills ground_kills hits camera ships lasers
bits explosions smokes thrust x y z xrot yrot zrot speed shields damage`). Available names:
`S` (all rows), `last`, `at(frame)`, `col(name, first=1, last=None)`, `first(name, value, from=1)`,
`ever(name, value, from=1, to=None)`, `seq(name)` (values with repeats collapsed),
`in_order(values, wanted)`, `saved(file)`, `size(file)` (in the save directory), `audio['peak']`,
`audio['seconds']`, and `max min any all len sum abs`. Menu numbers are in `src/SF_Menu.h` (0 main,
2 pause, 5 mission select, 7 sound, 9 load, 11 configure, 12 controls, 15 high scores; -1 = in
game); status values in `src/SF_Status.h` (1 playing, 3 crashed, 4/5 game over).

To work out a script, run the headless game with frame dumps and look at the pictures:

    SF_SCRIPT=my.script SF_DUMP=/tmp/f SF_DUMP_EVERY=20 SF_STATE=/tmp/f/state.tsv \
    SF_MAXFRAMES=2000 SF_SAVEDIR=/tmp/f/save ./starfighter-headless SF_Resources

or play it on the SDL build with `SF_RECORD=my.script` and replay the recording headless.

| Scenario | Suite | Covers |
|---|---|---|
| `boot-to-menu` | quick | three intro videos, logo, main menu, high scores |
| `new-game-takeoff` | quick | menu → mission select → briefing → first mission, thrust |
| `weapons-and-views` | quick | lasers, weapon switch, missiles, debris, camera, map, pause menu |
| `crash-and-game-over` | quick | three crashes, explosions, game over, back to the menu |
| `save-and-load` | quick | name entry, save, load, mission select |
| `mission-space` | quick | space mission (T/8) from a save fixture |
| `mission-{plastic,death,chemical,warrior,ice,earth2,hard-ice}` | full | one mission per planet type (and the hard pyramid) |
| `menus-tour` | full | configure, sound, controls, video toggle, config saved |
| `no-videos` | full | `Video/` missing: videos skipped |
| `boot-attract` | full | 44,000 idle frames: credits and the self-playing demo |

## Goldens (`tests/golden/<scenario>/`)

`frames.txt` holds a hash of every frame, `audio.txt` the hash of the captured audio,
`checkpoints/` a PNG every `checkpoint` frames, `meta.txt` the compiler and a manifest hash of
`SF_Resources`. The headless build is deterministic (virtual clock, fixed random seed, no threads),
so any change in a picture or sound is reported with the first differing frame, and the report shows
expected / actual / difference images for the checkpoints after it.

When a change is intended: run `make rebless`, look at the checkpoint images (`git diff`/`hg diff`
of `tests/golden`, or the contact sheets in the report), then commit the new goldens with the code.
If the goldens were recorded from a different `SF_Resources`, the comparison is skipped with a
warning. A failure after a compiler upgrade with nothing else changed is reported with both
compiler names; check the images and rebless.

## Fixtures

`tests/fixtures/base.save` is a save game written by the game itself (`save-and-load`). Scenarios
with `fixture: L N` get a copy made by `tests/tools/sf_mksave` with the level set to L (T, E, M, H)
and that pyramid's cursor on mission N, so loading it and pressing A starts that mission. The save
format is the game's in-memory `gamefile` struct: if it changes (`GAMEFILE_VERSION`), run
`tests/run.py make-fixtures` to record a new base save.

## Test hooks (both builds)

`src/sf_testhooks.c` reads these environment variables in the headless and the SDL build:

| Variable | Effect |
|---|---|
| `SF_SCRIPT=file` | scripted buttons (ORed with the real input in the SDL build); unknown buttons or bad lines are fatal |
| `SF_MAXFRAMES=n` | exit 0 after n presented frames |
| `SF_DUMP=dir`, `SF_DUMP_EVERY=n` | write frames as PPM |
| `SF_HASH=file` | one hash per frame |
| `SF_STATE=file` | game state per frame (columns above) |
| `SF_AUDIO_DUMP=file` | the mixed audio as a 44.1 kHz stereo WAV (the headless build mixes in step with its virtual clock) |
| `SF_RECORD=file` | record the real input as an `SF_SCRIPT` file |
| `SF_WATCHDOG=sec` | print a backtrace and exit 3 if the game hangs |
| `SF_TRACE_PAD=1` | log scripted buttons |

The SDL build runs in real time, so its runs are not frame-exact; `make test` only checks that it
boots and that scripted input reaches a mission.

## Performance (`make test-performance`)

Runs the scenarios that fly in the 3D game engine (those marked `performance: yes`: the missions,
`new-game-takeoff`, `weapons-and-views`, `crash-and-game-over`) on the optimised headless build, one
at a time, with no frame dumps, hashes, state log or audio capture, and prints frames per second:

    scenario                frames   fps(med)  fps(best)    cpu fps  spread
    new-game-takeoff          2400        220        225        222      5%
    mission-earth2            1800        195        198        200      3%
    ...
    TOTAL                    20200        269        272        271

* `fps(med)` / `fps(best)`: frames / wall-clock time, median and best of the runs.
* `cpu fps`: frames / CPU time used (median), less affected by other load on the machine.
* `spread`: slowest run against the fastest.

The headless build never sleeps (its clock is virtual), so this is how fast the engine produces
frames, not the game's 60 Hz pacing. Each figure covers the whole scenario, including the menus and
briefing before the mission starts (roughly the first 800-1,100 frames); the rest is in flight.
`REPEAT=n` sets the runs per scenario (default 3), `ONLY=text` picks scenarios
(`make test-performance ONLY=mission REPEAT=5`). Results go to `build/test-report/performance.csv` and
are appended, with a timestamp and the compiler, to `build/performance-history.csv` for comparing
over time. The command reports figures only; it fails only if a scenario does not run. Timings are
noisy on shared or virtual machines (identical runs varied by up to 2x on one), so compare figures from
the same machine, preferably idle.

## Known issues and baselines

* `tests/clang-warnings.txt`: warnings clang reports in the existing code (calls through `()`
  function pointers, one unused variable). New ones fail the clang check.
* `tests/ubsan.supp`: UBSan suppressions for known, not yet fixed issues, each with a `# KNOWN:`
  line that the runner lists as a warning. It is empty now.
* The suite does not yet include the long randomised soak runs, fuzzers, coverage report or MinGW
  build of phase 4 of the plan.
