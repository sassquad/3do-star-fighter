# Porting notes

## 1. Status at a glance

| Area | State |
|---|---|
| C game logic (`Star3000.c`, `SFlib/*.c`) | Converted, compiles clean for x86-64 (pointer/int issues audited, see §3) |
| Cel engine (3DO CCB renderer) | Written (`sf_cel.c`); verified visually on menus, briefing, HUD, sky |
| File / resource loading, endianness | Done (`sf_load.c`, struct parsers in `SF_Access.c`); mission parser validated against the real 4924-byte file size |
| Audio (SFX, SDX2 music/voice streaming) | Written (`sf_audio.c`, `SF_Sound.c`, `SF_Music.c`); **compiled but never listened to** (headless sandbox) |
| Input / window / timing | `plat_sdl.c` written; **never compiled against real SDL2 headers** (none in my sandbox; syntax-checked against a hand-written shim only) |
| Save games | Done (`SF_NVRam.c`, plain files) |
| Video | **Implemented** (Cinepak + SDX2 audio, `SF_Video.c` / `sf_cinepak.c`). Tested headless with `into1` and `Krisalis` (frames decode cleanly and look right; audio decode checked numerically, not heard). Missing files are skipped; the other videos are untested |
| Text font | Original `Fonts/*` files, decoded by `sf_text.c` (see §6) |
| ARM assembly → C | **Partly done** – see §2 |
| Playable game | **Untested** – all assembly is translated; ships, structures and the landscape draw in headless runs; flying, combat, weapons, AI, saving etc. have not been exercised |

What I actually ran: the headless build boots, plays the logo sequence, shows the main menu,
accepts scripted input, creates a new game, loads a mission, shows the briefing and flies the
in-game loop with HUD, sky and **textured landscape** (see `screenshots/ingame-landscape.png`).
The user also confirmed on real SDL2: menus, timing and audio (samples + music) work, and supplied the reference image that exposed the LRFORM mistake.
Everything not mentioned here is untested.

## 2. Assembly translation

| Original `.s` | Lines | Status | C file |
|---|---|---|---|
| `SF_ARMUtils.s` | 252 | done | `SF_ARMUtils.c` |
| `SF_ARMLink.s` (lists, z-sort, collision sweep) | 954 | done | `SF_ARMLink.c` |
| `SF_ARMCell.s` (CCB builders, 6-bit maps, 4x4 cache) | 1921 | done | `SF_ARMCell.c`, `sf_shade_tables.h` (generated from the asm `DCD`s) |
| `SF_ARMSky.s` | 284 | done | `SF_ARMSky.c` |
| `SF_ARMAnim.s` | 434 | done | `SF_ARMAnim.c` |
| `SF_ARMBurn.s` | 674 | done | `SF_ARMBurn.c` |
| `Maths_Stuff.s` | 976 | done | `Maths_Stuff.c` |
| `Collision.s` | 616 | done | `Collision.c` |
| `Laser_Plot.s` | 289 | done (untested in-game) | `Laser_Plot.c` |
| `Smoke_Plot.s` | 515 | done (untested in-game) | `Smoke_Plot.c` |
| `Plot_Graphic.s` | 2166 stripped (3540 raw) | **done**: rotations, stars, planets, distant ships, `rotate_graphic`, perspective, polygon link walker with near-plane clipping, the 8 style routines, ships/bits/static objects, lock-on boxes. Ships, buildings seen in headless frames; bits (explosions), trees/clouds, lock-on boxes **not yet seen** | `Plot_Graphic.c` |
| `Plot_Land.s` | 1679 stripped (3230 raw) | **done**, verified visually | `Plot_Land.c` |

`Plot_Land.c` contains the z-sort bucket dispatcher (`ref` 0 ship, 1 smoke, 2 laser, 3 bit, 4 static-from-grid);
all five targets are now translated.

### Translation conventions
* Each C routine follows the assembler's statement order. Where the asm relies on aliasing or odd
  flag behaviour (e.g. `armlink_deleteitem` on the only active item; the collision-box compare chain
  in `Collision.c`, where an *equal* coordinate counts as a pass) it is reproduced deliberately.
* `arm_ops.h` gives C the exact semantics of ARM shifts by register (≥32), wrapping multiply, etc.
* Original register names are kept in comments/variables in the more intricate routines.

## 3. 64-bit / pointer issues found and how they were handled

1. **`long` was 32-bit.** All `long`/`unsigned long` → `int32_t`/`uint32_t` (script-converted). armcc's
   unsigned `char` → `-funsigned-char`. `-fwrapv` keeps signed overflow wrapping as on ARM.
2. **Linked-list headers** (`link_header`, `linked_list`, `zsort_item`, `zsort_list`) held 32-bit
   pointers; now real pointers (24-byte headers). Sort-value offsets are computed from `sizeof`.
3. **Ownership fields hold either a small integer (ground object ≤ 16384) or a ship pointer**
   (`who_owns_me`, `what_hit_me`, `who_hit_me`, laser/bit owners, `owner` parameters of
   `add_laser`, `aim_laser`, `add_bit`, `explode_*`, `add_explosion_line`): widened to `intptr_t`.
   The collision stack and `temp_store` (scratch aliased over `graphic_rotated_coords` on 3DO) are now
   `intptr_t` arrays in their own storage. `air_to_*_scan` likewise.
4. **Graphics data relocation** (`Setup_Tables.c`) added absolute addresses into 32-bit fields of
   `graphics_details`, the section group tables etc. These fields are now **byte offsets from
   `graphics_data`**; use `GPTR(off)` / `GOFF(ptr)` (`Graphic_Struct.h`). Every C use site was found via
   `-Wint-to-pointer-cast` and wrapped. **The remaining untranslated assembly must use `GPTR()` whenever
   it loads one of those fields as an address.**
5. **`mission_data` contains `ship_sdb` with two pointers**, so it can no longer be read raw from the
   file; `load_mission_file` parses it field by field (checked to consume exactly 4924 bytes).
   `game_configuration` and `planet_data` get the same treatment.
6. **Texture animation data** (`SF_ARMAnim`) stored absolute addresses *inside* the file image after
   init; it now keeps the slot index and remembers the table base instead.
7. **Machine-code constant tables** (`machine_code_constants`, `plot_land_constants`, `memptr[]`) were
   pointer blocks for the asm; the translated code uses the C globals directly.
8. `arm_burn` used a heap *address* (`r4>>3`) as a pseudo-random seed for burnt-ground variants; the
   port uses the explosion index (deterministic).
9. Implicit function declarations are an error in the Makefile (`-Werror=implicit-function-declaration`)
   because a truncated pointer return value is silent on x86-64.

## 4. Endianness model

Resource data is big-endian. Blobs the original accessed as 32-bit words (graphics data, cel lists,
cel palettes, cosine/tangent tables, sky, animations, 4x4 sprites) are byte-swapped into native words at
load ("word-swapped" model). Consequences:
* All word-oriented asm logic (6-bit pixel packing with `lsr #24` etc.) translates verbatim.
* A *byte* at file offset N of such a blob is at memory offset `N ^ 3`: use `SF_BYTE()` (`sf_compat.h`).
  `collision_details` (four `char`s in a word) has its field order reversed on little-endian hosts.
* Pure byte maps (`poly_map`, `sprite_map`, `height_map`) are not swapped.
* The framebuffer is a linear 320×240 host `uint16_t` buffer. Image files (`Images/*`, `LoadImage` in the
  original) are stored in the 3DO's **LRFORM** (left/right) layout, decoded in `load_image_raw`:
  for each pair of display rows (2j, 2j+1), memory row 2j holds the *left* halves (x 0..159) of both rows
  interleaved pixel by pixel, and memory row 2j+1 holds the *right* halves; i.e. display (y,x) is at memory
  row `2*(y>>1) + (x>=160)`, column `2*(x%160) + (y&1)`. Some files start with an `IMAG` chunk followed by a
  `PDAT` chunk holding the pixels (`FedNet`); others are bare 153600-byte dumps (`Menu*`, `Mission_Select`);
  the layout is the same. (Derived by matching `FedNet` against a screenshot from a 3DO emulator, supplied by
  the user; the earlier "two-bank row interleave" I first used was wrong and scrambled most backdrops.)

## 5. The cel engine (`sf_cel.c`)

Implements the CCB subset the game uses: PRE0/PRE1 (from CCB or data), coded 1–8 bpp and uncoded 16 bpp,
packed (RLE) cels, PLUT loading, flips, `SKIPX`, forward-mapped quads (`P = P0 + v·V + u·(H + v·HDD)`),
and the pixel processor (PIXC) for P-modes 0/1 with MF/DV, second-source (frame buffer or source) and
halving.

**Inferred, not documented, behaviour** – I did not have 3DO hardware docs, so these were derived and
cross-checked against the source:
* PIXC field decode: derived from the shade tables in `SF_ARMCell.s`, whose comments give the intended
  percentages (e.g. "P0 150%", "P1 75%"); my decode reproduces those figures.
* PRE0 bit 30 = vertical flip (used by the 6-bit map cels). If the landscape comes out upside-down once
  `Plot_Land` is ported, flip this assumption first.
* Coded-pixel P-bit: for 6/8 bpp coded cels with PLUTPOS=0 it is the top bit of the pixel code, else
  bit 15 of the PLUT entry. `ACW/ACCW` culling, `USEAV`, `PXOR`, `MARIA` are ignored.
* Quad filling uses pixel-centre sampling; hardware rounding may differ by a pixel.

Performance is untested on realistic scenes (float per-texel quads).

## 6. Known gaps

* **Font.** `Fonts/{Message,Menu,Title}` are 3DO FontLib `FONT` files: 21-word header, one
  `(offset<<10 | width)` word per character, and 5 bpp glyphs in FontLib's 16-bit packet compression.
  `sf_text.c` loads them as-is and ports FontLib's `FontBlit5To8` unpacker and TextLib's text cels
  (8bpp coded cels, PLUT blends, AMV anti-aliasing against the screen). The cel engine honours
  `CCB_USEAV` for 8bpp coded pixels and recognises TextLib's PIXC word `0x1F00BFC0`
  (screen * AMV/8 + pixel colour). Format notes are at the top of `sf_text.c`.
* **Video**: see §8. Files absent => skipped, as required.
* **Audio** is unheard: gain/pitch mapping (`pitch 60` = unity, gain `(16384±pos)*vol>>7`) was inferred from
  `SF_Sound.c`; AIFF `INST` sustain loops are honoured.
* Several asm quirks were reproduced rather than "fixed", e.g. `SF_ARMBurn` uses sub-row word 0 for the even
  map row whereas `arm_generatemaps` uses word 8.
* Text resources shipped with LF line endings; the parser accepts both LF and CR.
* The 3DO controller broker/ProStick thread was removed; a keyboard/gamepad drives the pad bits. The
  keyboard layout comes from `config.txt` in the current directory (`sf_keyconfig.c`), written with
  the defaults if missing; invalid keys or a key used for two controls stop the game with a list of
  the problems.
  `plat_stick` returns a centred stick (no analogue support yet).

## 7. Suggested next steps
1. Fly it: take off, shoot, get shot, explode, land, save/load, play several missions, and report anything wrong.
   Untested areas that most likely hide bugs: weapons/explosions (bits, smoke, lasers), the AI (`test_prog.c`),
   enemy ships, space missions (stars/planets), lock-on boxes, pause/menu-in-game, configure screen.
2. Performance check on a real machine (forward-mapped cel quads in floating point).
3. Compare shading/colours with a real 3DO or an emulator: the PIXC decode and a few shade offsets were inferred.

## 8. Video playback

* Container: 3DO stream chunks `tag size time channel ...`. `FILM`/`FRME` carries one Cinepak packet
  (`flags<<24|length, width, height, strips`, then 6 extra bytes, then strips); frame `time` is in 1/240 s.
  `SNDS`/`SSMP` carries SDX2 stereo audio (22050 Hz per the `SHDR`). `SHDR`/`FILL` are ignored.
* `sf_cinepak.c` implements codebook (full/partial, colour/greyscale), intra/inter/V1-only vector chunks,
  per-strip codebook inheritance (frame flag bit 0) and keeps the previous frame for skipped blocks.
  Colour: R=Y+2V, G=Y-U/2-V, B=Y+2U (signed U,V). Strip height is `y2-y1`; strips stack vertically.
* Audio is decoded up-front into a PCM buffer and mixed (linear resample to 44.1 kHz) by `sf_pcm_*` in
  `sf_audio.c`; video frames are scheduled against the wall clock from the same start instant. There is no
  adaptive A/V resync: if the machine stalls, frames are dropped (all are still decoded, so inter frames stay correct).
* The picture is centred on the 320x240 screen (letterboxed for 320x200 movies).
* As on the 3DO, music is paused and samples shut down while a video runs.
* Deviation: a button held at start does not cancel the video (the original cancelled on any non-zero pad).

## 9. Bugs found by testing (and how)

Found by a user report plus AddressSanitizer / UBSan runs and randomised-input runs of the headless build:

| Bug | Cause | Fix |
|---|---|---|
| **Segfault when the player's ship was destroyed** (ground impact) in `ship_explosion_section_control` | `GPTR(0)` returned the base of `graphics_data` instead of NULL, so "no polygon group" fields passed `!= NULL` checks | `GPTR(0)` is now NULL; the uninitialised `section_data_header` locals the original also had are initialised. Reproduced with the identical stack by forcing the player's shields to 0, and confirmed fixed |
| Heap overrun drawing short/empty text | one-word-wide text bitmap made the row-stride field `(words-2)` negative | bitmap is at least two words wide (`sf_cel.c`) |
| Heap overrun on far-landscape cels near the map edge | cels cut from the bottom of the 6-bit maps read past the end (harmless on the 3DO) | map buffers are padded and zeroed (`SF_Celutils.c`) |
| Out-of-range sound channel (-1) in `sound_alter` etc. | `sound_playsample` returns -1 when nothing starts; callers pass it back | channel guards in `SF_Sound.c` |
| Test builds silently lost `-funsigned-char` | a command-line `CFLAGS=` overrides Makefile `+=` | required flags use `override` |
| **Segfault when the player collides with another ship** in `war_update_attacked_friend` (`ship_firee = 0x5563e978`) | `Collision_Update.c` stored `ship->who_hit_me = (int32_t) ...` in two places (laser hit, ship/ship collision). `who_hit_me` is `intptr_t` here, but the cast cut the 64-bit ship pointer to 32 bits; `SF_War.c` later dereferenced it | casts removed (`who_hit_me = laser->who_owns_me;` / `= ship->what_hit_me;`). Found by code reading only; confirmed fixed by the tester on the SDL build |
| **Mission-select screen: planets invisible, red mission numbers garbled** (also missing: sun, missile icons, grid label, header arrows in other screens) | Packed game cels whose flags have `CCB_CCBPRE` clear keep only **one** preamble word (`PRE0`) in their data; there is no `PRE1`, and row data starts straight after `PRE0` (checked by walking the rows of every packed cel in every `Game.*` file: all 1,500+ fit the one-word layout, none fits two words). `draw_one` in `sf_cel.c` always consumed two words, so the first row word was read as `PRE1` (giving a width of ~1800 and the cel was dropped, or a shifted decode) | packed cels without `CCB_CCBPRE` take `PRE0` only, and each row's width comes from its own packed data (`decode_row_packed` natural-width mode). Checked in headless frames against the emulator screenshot; **not yet confirmed on the SDL build** |
| **Ground objects (buildings, domes, trees, guns) popping in and out while flying** | `scan_map` in `Maths_Stuff.c` (C version of `scan_poly_map` / `scan_poly_map_2`) did not return x to the start of the window after each row, as the assembler does (`subs r0,r0,#28`). Each of the 32 rows was therefore scanned 32 cells further along the 128-wide map, so the "32x32 window" was really a set of skewed stripes and an object was only found when its row happened to line up | x is reset to the quad-aligned start on every row. Compared frame sequences before and after in the headless build: objects now stay put. Also affects ground-gun / SAM scans (`scan_poly_map`). **Not yet confirmed on the SDL build** |
| **Mid-distance ground texture (2nd level of detail) does not line up with the near and far levels** | `arm_add4cel4` (`SF_ARMCell.c`) skipped 4 words ("16 byte header") at the start of the 4x4 tile data. There is no header: the `.4` file is tiles from byte 0, and the assembler's `add r7,r7,#16` only offsets its pre-decrementing `ldr r2,[r9,#-4]!` reads onto the tile's own last word. Every mid-grid cell therefore used tile `n+1` where the near cels, the 128/512 maps (`arm_generatemaps`) and explosions use tile `n` | tile `n` is now at word `n*4`. Compared before/after frames (roads and markings in the mid ring now continue from the near ring). **Not yet confirmed on the SDL build** |

| **Heap overrun in the typewriter text reveal** (mission briefing, in-game messages) | The reveal raises the PRE1 pixel count up to `ccb_Width`, one more than a row holds; for 8 bpp text cels whose width is a multiple of 4 the last row's extra pixel lay past the buffer. `get_bits` also always read the word after the one it needed | text cel buffers padded with 8 zero bytes (`sf_text.c`); `get_bits` reads the next word only when the field crosses into it (`sf_cel.c`). Found by ASan on the smoke script; frames unchanged |
| **Explosion smoke placed from uninitialised memory** (crashes rendered differently in ~1 run in 6) | `explode_ship_from_collision_box` uses `node_data`'s position for smoke and bonus crystals under its own random test; on the first pass it may not have been set yet | `node_data` zero-initialised (`Explosion.c`). Found by the determinism check, located with valgrind |
| **Segfault in the attract-mode demo** in `war_update_attacked_friend` (about 1 in 2 runs) | `add_sam` took its owner as `int32_t`; big ships pass their own pointer, which was cut to 32 bits and later dereferenced (same class as the `who_hit_me` bug above) | parameter is `intptr_t` (`Weapons.c`, `Weapons.h`). A clang `-Wshorten-64-to-32` check now guards against new narrowings |
| `cosine_table[-1]` read in the demo | a ship's negative y rotation gave angle index -1 in `rot_pair` | angles wrapped with `& 1023` (`Maths_Stuff.c`). Found by ASan |
| Negative shift in `bonus_downgrade_ship` | damage ratings above 3 made `>> (3 - damage_rating)` negative (undefined in C; armcc emitted an ARM register shift, giving 0) | `ASR()` from `arm_ops.h` (`SF_Bonus.c`). Found by UBSan |
| `messages[8]` written in the demo (`messages[]` had 8 entries) | `MESSAGE_DEMO` is 8; `message_add` indexes `messages[]` with it before `message_text` redirects drawing to slot 7. The demo's captions now appear | `FONT_MAXMESSAGE` is 9 (`SF_Allocation.h`). Found by UBSan |

Testing aids: the regression suite in `tests/` (`make test`, see `tests/README.md`). Scripted input
(`sf_testhooks.c`, used by both builds) is clocked by presented frames plus time since the last
present (screens that never present no longer stall a script); `SF_WATCHDOG` prints a backtrace on a
hang. The headless build mixes audio in step with its virtual clock, so `SF_AUDIO_DUMP` captures it.
A 12,000-frame randomised-button session (menus, mission, pause menu, camera views, map screen, death,
save) ran without a crash on an optimised build. An ASAN run of the same session is slow (~10 fps) and was
only completed for the shorter scripted sessions.

## 10. Known visual problem (not fixed)

> Update: the packed-cel preamble fix in §9 draws many game cels that were previously dropped or shifted, and the map overlay uses the same cels. The streaks described below may be (partly) gone; re-check before investigating further.

On the in-mission **map / briefing overlay screen** (reached in my randomised run while the briefing text
was typing out), the rendering looked wrong: horizontal bright streaks across the tilted terrain map and
vertical green/red bars where text should be. I traced that the map cels (16x16 texels shrunk to ~6 px with
height skew, `arm_addcelfrom512map`) and the text cels (sane size/stride/width fields) have plausible
parameters, so the cause is not yet found. It may be a cel-engine limitation (e.g. how very small or
heavily-minified forward-mapped cels, or the vertical-flip cels, are filled) or a state carried between cels.
Please check whether you see it in the real build; if so, a screenshot and what you pressed would help.
