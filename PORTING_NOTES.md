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
| Text font | `Fonts/*.png` glyph strips + `*.txt` metrics, converted from the original FontLib files (see §6, §11) |
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
* The framebuffer is a linear 320×240 host `uint16_t` buffer. Backdrop images are now `Images/*.png`
  (8-bit RGB, loaded by `load_image_png`; see §11). The original files were stored in the 3DO's
  **LRFORM** (left/right) layout, which the one-off converter decoded:
  for each pair of display rows (2j, 2j+1), memory row 2j holds the *left* halves (x 0..159) of both rows
  interleaved pixel by pixel, and memory row 2j+1 holds the *right* halves; i.e. display (y,x) is at memory
  row `2*(y>>1) + (x>=160)`, column `2*(x%160) + (y&1)`. Some files start with an `IMAG` chunk followed by a
  `PDAT` chunk holding the pixels (`FedNet`); others are bare 153600-byte dumps (`Menu*`, `Mission_Select`);
  the layout is the same. (Derived by matching `FedNet` against a screenshot from a 3DO emulator, supplied by
  the user; the earlier "two-bank row interleave" I first used was wrong and scrambled most backdrops.)

## 5. The cel engine (`sf_cel.c`)

Implements the CCB subset the game uses: PRE0/PRE1 (from CCB or data), coded 1–8 bpp and uncoded 16 bpp,
packed (RLE) cels, PLUT loading, `BGND`, `SKIPX`, forward-mapped quads (`P = P0 + v·V + u·(H + v·HDD)`),
and the pixel processor (PIXC) for P-modes 0/1 with MF/DV, second-source (frame buffer or source) and
halving.

**Inferred, not documented, behaviour** – I did not have 3DO hardware docs, so these were derived and
cross-checked against the source:
* PIXC field decode: derived from the shade tables in `SF_ARMCell.s`, whose comments give the intended
  percentages (e.g. "P0 150%", "P1 75%"); my decode reproduces those figures.
* PRE0 bits (now checked against the 3DO Portfolio cel format, `3docel.h`): 31 `LITERAL`, 30 `BGND`
  (pixels whose value is 000 are drawn rather than transparent), 27:24 `SKIPX`, 15:6 `VCNT`, 4 `LINEAR`,
  3 `REP8`, 2:0 `BPP`. The preamble has **no flip bits**; the 3DO flips a cel with negative `HDX`/`VDY`,
  which the quad mapping handles. Bits 31 and 30 were once guessed to be horizontal/vertical flips – see §9.
* Coded-pixel P-bit: for 6/8 bpp coded cels with PLUTPOS=0 it is the top bit of the pixel code, else
  bit 15 of the PLUT entry. `ACW/ACCW` culling, `USEAV`, `PXOR`, `MARIA` are ignored.
* Quad filling uses pixel-centre sampling; hardware rounding may differ by a pixel.

Performance: see §11. The engine is still forward-mapped in floating point (exactly as before), but
skips work that provably cannot change a pixel.

## 6. Known gaps

* **Font.** `Fonts/{Message,Menu,Title}` were 3DO FontLib `FONT` files (5 bpp glyphs in FontLib's
  16-bit packet compression). They are now a PNG glyph strip and a metrics text file each (§11), drawn
  by replaying per-pixel ops that reproduce FontLib's `FontBlit5To8` exactly. `sf_text.c` keeps
  TextLib's text cels (8bpp coded cels, PLUT blends, AMV anti-aliasing against the screen). The cel engine honours
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
2. Performance: done on the headless build (§11); worth a look on the SDL build on a slow machine.
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
| **Far ground (3rd level of detail) out by a row against the near and mid levels: roads appear to move as they cross the boundary** | `sf_cel.c` read PRE0 bit 30 as "vertical flip". It is really `BGND`, which `arm_addcelfrom512map` sets on every far-ring and flat-ring cel, so each 8x8-unit far cell was drawn upside down: features running along map x were mirrored within the cell (a road at the cell's row k drawn at row 7-k), while features along map y were right, hence "one dimension only". Bit 31 (`LITERAL`) was likewise read as a horizontal flip, mirroring the packed 32x32 polygon textures (`arm_addpolycel32`) left to right | bits 30/31 no longer flip; bit 30 / `CCB_BGND` draw 000 pixels. New unit tests draw a road through a mid cell and a far cell at every row and column and require the same rows (`test_armcell.c`, fail on the old engine). Also fixes the map-screen streaks (§10). Goldens reblessed (frames only; game state and audio unchanged). **Not yet confirmed on the SDL build** |
| **Exploding ships and buildings: every flying piece was a whole copy of the object** | `plot_ship` (`Plot_Graphic.c`) took the "groups to leave out" mask from `ship->counter`. The game moved that mask to `group_mask` (byte 40; `Ship_Struct.h` notes counter is "NOT ANYMORE" the mask) and `add_ship_explosion_section` / `add_explosion_section` write only `group_mask`. A new section is zeroed, so counter was 0 and every piece drew all its groups | read `ship->group_mask`. Compared before/after frames of the player crash and a space kill: pieces are now single wings/nose sections. Pinned by the `crash-and-game-over` and `mission-space` goldens (reblessed; frames only). **Not yet confirmed on the SDL build** |
| **Faded screens (Fednet logo, menu background, mission pyramid...) flashed up at full brightness for a frame, went black, then faded in** | On the 3DO `FadeToBlack` / `FadeFromBlack` change the screens' colour tables, which keep their value, so a screen displayed after a fade out is still black until it is faded in. `sf_cel.c` passed the fade only as a per-present brightness, and `DisplayScreen` always presented at full brightness | the display brightness is kept between calls: `FadeToBlack` leaves it at 0, `FadeFromBlack` at full, `DisplayScreen` uses it. Every `FadeToBlack` in the game is followed by a `FadeFromBlack` (`screen_swap`/`screen_postswap`, the Fednet intro). Unit test `test_fade_state_persists_across_display`; goldens reblessed (2-6 frames per scenario, the flash frames). **Not yet confirmed on the SDL build** |
| Audio read before the start of a sample (ASan, attract demo) | the engine-noise Doppler in `Sound_Control.c` can give a negative pitch bend (`9*1024` + speed terms - up to `12*1024`); the mixer then stepped backwards off the start of the sample | `sf_mix_voice_bend` holds a negative bend at 0 (`sf_audio.c`). Unit test `test_negative_bend_stays_in_sample`. Exposed when the fade fix above shifted the demo's timing |
| Enemy laser type depended on the build (ASan attract demo drifted from the normal build at frame 35,833: different laser count) | ships with "random" lasers (`weapons[WEAPON_LASER] == 255`) took `(ship address >> 5) & 7` (`Laser_Control.c`). The address depends on where the `ships` pool lands, which differed between the sanitizer, normal and 32-bit Windows builds | the ship's slot in the pool times the 32-bit (3DO) `ship_stack` size, 228 bytes, so every build makes the same choice. Changes which laser some enemies use (attract demo golden reblessed). The ASan scenario runs, which must match the normal build's goldens, guard against this class of problem |

Testing aids: the regression suite in `tests/` (`make test`, see `tests/README.md`). Scripted input
(`sf_testhooks.c`, used by both builds) is clocked by presented frames plus time since the last
present (screens that never present no longer stall a script); `SF_WATCHDOG` prints a backtrace on a
hang. The headless build mixes audio in step with its virtual clock, so `SF_AUDIO_DUMP` captures it.
A 12,000-frame randomised-button session (menus, mission, pause menu, camera views, map screen, death,
save) ran without a crash on an optimised build. An ASAN run of the same session is slow (~10 fps) and was
only completed for the shorter scripted sessions.

## 10. Map-screen streaks (believed fixed)

> Update 2: the far-ground PRE0 fix in §9 (bit 30 is `BGND`, not a vertical flip) removes the horizontal streaks on the map overlay in headless frames, and the header label is readable again. Please confirm on the SDL build.
>
> Update: the packed-cel preamble fix in §9 draws many game cels that were previously dropped or shifted, and the map overlay uses the same cels. The streaks described below may be (partly) gone; re-check before investigating further.

On the in-mission **map / briefing overlay screen** (reached in my randomised run while the briefing text
was typing out), the rendering looked wrong: horizontal bright streaks across the tilted terrain map and
vertical green/red bars where text should be. I traced that the map cels (16x16 texels shrunk to ~6 px with
height skew, `arm_addcelfrom512map`) and the text cels (sane size/stride/width fields) have plausible
parameters, so the cause is not yet found. It may be a cel-engine limitation (e.g. how very small or
heavily-minified forward-mapped cels, or the vertical-flip cels, are filled) or a state carried between cels.
Please check whether you see it in the real build; if so, a screenshot and what you pressed would help.

## 11. Simplification and performance work (November 2026)

Plan and measurements: the project doc "improvement plan". Every step kept the goldens (frames and
audio) bit-identical; each was checked with `make test-full`, the cel-engine differential test and
replays of cel lists recorded before the work.

**Measured result** (`make test-performance`, 2-core x86-64, median fps): TOTAL 268 → 519 (1.94×);
per scenario from 1.6× (`mission-plastic`) to 2.2× (`new-game-takeoff`, `weapons-and-views`). The
totals include the menus before each mission, so in-flight frames gain a little more.

| Step | What | Effect |
|---|---|---|
| Build flags | `-O3`; `-msse4.1` on x86 (`SSE41=0` to opt out; 32-bit x86 also gets `-mfpmath=sse`); `-ffp-contract=off` is now mandatory in every build | ~+30 %. `ceilf` becomes one `roundss`. `-ffp-contract=off` keeps FMA targets (aarch64, `-march=native`) from moving pixels |
| Pixel processor | per-PIXC lookup tables instead of three divisions per channel | 3-10 % |
| Rasteriser | whole texel rows skipped when they cannot reach a pixel centre (corner positions are monotonic along a row); texels decoded only once they cover a pixel; edge-test terms hoisted per pixel row; axis-aligned texels filled as rectangles; corner positions and their pixel-centre indices shared between neighbouring texels | 18-39 % on in-game cel lists |
| Screens | `sf_videosys.c` (`video_open/flip/show/fill/copy_in/fade_out/fade_in`, `frame_wait/defer/sync`) replaces OpenGraphics, ScreenContext, Item/IO requests, DisplayScreen, CopyVRAMPages, SetVRAMPages, FillRect, FadeTo/FromBlack, WaitVBL/WaitIO, GetControlPad; `sf_sys.c` removed | simpler; same calls in the same order |
| Cel engine interface | `draw_cels(buffer, list)`; inside, each CCB is decoded once into a typed `CelJob` (`cel_prepare`) and rasterised (`cel_render`). Game code no longer pokes text-cel CCB bits: `textcel_hide/hidden/last/link/revealed/set_revealed/width` | clearer; the typewriter reveal (31 Oct overrun) has one implementation |
| Resources | `Images/*` → `Images/*.png` (stb_image, PNG only); `Fonts/*` → `Fonts/*.png` + `.txt` | LRFORM and FontLib decoders removed |

Kept on purpose: CCBs remain the format the game builds and the renderer reads (much of it is
translated assembly that follows the original line by line, and the reference renderer, cel traces and
differential test use it); Cinepak video and AIFF/AIFC audio are unchanged (re-encoding video would
change pixels; the AIFF parser is needed for the music streams anyway); 3D model, map, mission and
palette data have no generic equivalent. A byte-per-texel texture format was not done: after the
rasteriser work, texel decoding is 7-9 % of renderer time.

**Font conversion detail.** FontLib's unpacker writes non-zero pixels and has "copy run" packets that
copy the pixel k rows above in the destination. Each pixel of a glyph box is visited at most once, in
row-major order, and copies only read earlier rows of the same box (checked for every glyph), so a
glyph is stored as one op per pixel: 0 leave, 1-31 write that value, 128+k copy from k rows above if
non-zero. Replaying the ops is identical to unpacking, also where glyphs overlap (the Menu font's
spacing is -2): checked for every pair of glyphs of every font, and for 10,534 strings (all game text
plus every two-character string) rendered with the old and new code.

**Tools added for this kind of work:** `make celtrace` / `make celbench` (record in-game cel lists,
replay them for exact-output checks and quiet timing), `tests/unit/test_cel_diff.c` (thousands of
random cel lists against `tests/ref/sf_cel_ref.c`, a frozen copy of the renderer before the work), and
`tests/run.py full --compare-across-resources` / `make restamp` for lossless resource conversions.


## 12. Internal resolution scale (November 2026)

Plan: the project doc "upscale plan". `SCALE = n` in `config.txt` (or `SF_SCALE=n`) renders at
320x240 times n, n = 1 to 4. At n = 1 every golden is unchanged.

**How it works.** The game still computes every position on the 320x240 logical screen.

* *Frame buffers and renderer.* The two frame buffers are `sf_fb_w x sf_fb_h` (`sf_videosys.c`).
  `cel_prepare` multiplies each cel's geometry by n after the CCB has been applied to the engine
  state (state carried between cels stays logical). Axis-aligned cels at whole logical pixels come
  out exactly as the 1x picture blown up, so the HUD, menus, map, text and sprites keep their size
  and look. Frame-buffer-source cels (blur and zoom transitions) use one texel per physical pixel
  (`X' = nX, H' = H, V' = V, D' = D/n`): their 3DO preamble could not describe a 1280-pixel row.
  Backdrops (`video_copy_in`) and video frames are blown up n x n.
* *Sub-pixel 3D view.* While `draw_frame()` runs at n > 1, `cel_subpix` = 3: the projection keeps
  three fractional bits, `MULS(x, pex) >> 13` instead of `>> 16`. The 32-bit product is the original
  one, so it wraps exactly where the ARM code did. The cel builders shift by 3 less (`SHP` in
  `SF_ARMCell.c`, `arm_plotsky`), so every CCB field has the same magnitude as at n = 1 and no new
  overflow is possible. The 3D code's screen-space constants (clip limits +-160/+-120, sprite
  offsets, star and planet bounds) go through `SUBPIX()`.
* *Kept apparent sizes.* Lasers and lock-on box lines keep a minimum of one logical pixel (laser
  half widths at least one logical pixel either way, matching the whole-pixel rounding); the sky and
  the black under-horizon polygon are laid out in whole logical pixels as before and moved by the
  horizon point's sub-pixel fraction.

**Overflow review** (measured over the flight scenarios before implementation, see the plan):

| Quantity | At n = 1 | What was done for n > 1 |
|---|---|---|
| projection `x * pex` (int32) | wraps for 0.17 % of land nodes (authentic, in the goldens) | product unchanged; only the final shift changes |
| CCB fields (16.16 / 12.20) | max 2^28.6 | unchanged magnitudes (sub-pixel bits shifted in, not added) |
| land facing tests `cross1/cross2` | 10 wraps in 15 M | 64-bit products when `cel_subpix`; 32-bit (with wraps) kept at n = 1 |
| object facing test `cross3` | max 2^25 | 64-bit when `cel_subpix` |
| frame-buffer cel preamble (10-bit word offset) | 320 px rows | not encoded: scaled inside the renderer |

**Found while implementing.**
* `rotate_land()` projects the land nodes before `draw_land()`, so sub-pixel mode has to cover all of
  `draw_frame()` (first try only covered `draw_land()`: the land came out at 1/8 size).
* `pex_table` (steps of 64 depth units) and `pex_table_near` (steps of 32) round differently, and
  each object picks one by its own distance. Neighbouring objects (the pipe sections by the launch
  pad) were up to half a logical pixel apart; whole pixels hid it, sub-pixel positions showed a seam.
  In sub-pixel mode the factor is now exact, `12582912 / depth` (`pex_fix`); the same faint seam at
  1x is the original behaviour and stays.
* Rendering does not feed back into the game: water heights are updated before a land section's
  facing test, and lock-on and collision work in camera space. The scaled test runs check this: the
  state log is identical at 1x, 2x and 4x in every scenario.
* The mixed audio can differ by about a frame for one sound. When a frame draws more than 1024 cels,
  `screen_intercept()` draws the list part-way through the frame and waits for the frame delay there;
  sounds started after that point in the frame are mixed one frame later. Sub-pixel culling drops
  some zero-area land quads that whole pixels kept (in `mission-chemical` 968 instead of 1027 cels in
  four frames), so the mid-frame wait can happen at one scale and not the other. Inaudible; the
  scaled tests allow such differences in up to 2 % of the audio (0.7 % there, 0 elsewhere).

**Tests.** `tests/README.md`, "Scaled runs": every scenario at 2x and four at 4x (two under ASan)
must play exactly as at 1x, draw their 2D screens as the 1x screens blown up, and match their own
frame-hash goldens; `tests/unit/test_scale.c` checks the renderer scale against the frozen reference
renderer and that sub-pixel quads encode to the same CCB fields as whole-pixel ones.

**Cost.** `make test-performance SCALE=n` on one 2-core x86-64 machine (headless, median fps; the
original code measured 711 TOTAL on the same machine, so scale 1 is unchanged):

| Scenario | 1x | 2x | 4x |
|---|---|---|---|
| crash-and-game-over | 1,132 | 509 | 189 |
| mission-chemical | 831 | 369 | 125 |
| mission-death | 722 | 340 | 117 |
| mission-earth2 | 557 | 265 | 100 |
| mission-hard-ice | 510 | 264 | 101 |
| mission-ice | 512 | 271 | 103 |
| mission-plastic | 596 | 265 | 91 |
| mission-space | 3,260 | 1,766 | 741 |
| mission-warrior | 829 | 351 | 121 |
| new-game-takeoff | 655 | 270 | 97 |
| weapons-and-views | 718 | 310 | 111 |
| **TOTAL** | **712** | **329** | **119** |

2x costs 2.2x the time and 4x 6x: the per-texel work stays the same (the scene has as many texels)
while the per-pixel work (filling, pixel processor, clears, fades, colour conversion) grows with n^2.
The game itself paces flight at 20 frames per second (15 in space), so even the slowest mission at 4x
(91 fps) has about 4.5x headroom on this machine; a single-core machine of the SSE4.1 minimum's era
may want 2x or 3x.

## 13. Rotated view: the sky as spans (November 2026)

Flying with the view rolled was much slower than level flight, at every scale (renderer time per
frame, 40 frames of each, `celbench`): 1x 3.0 ms level / 14.1 ms rolled, 2x 7.8 / 47.7, 4x 22.5 / 181.
Nothing in the rotation itself is expensive (the camera matrices are set up once per frame); the time
went into the sky. `arm_plotsky` draws it as a cel one texel wide with one texel per screen line, each
texel a strip up to 400 pixels long. Level, each strip is an axis-aligned rectangle and is filled
directly. Tilted even slightly, each strip is a long thin diagonal texel, and the rasteriser tested
every pixel centre of its bounding box: 93 % of all edge tests in rolled frames at 2x (434 M of 467 M
over 40 frames), about 27 tests per pixel drawn.

`fill_quad_box` now finds the covered pixels of each pixel row as spans when the box is at least
8 pixels wide. A pixel is drawn iff its four edge values are all >= 0 or all <= 0, and each edge value
`a - ey * ((x + 0.5f) - px)` is monotonic in x because every float operation in it rounds
monotonically; so "e >= 0" and "e <= 0" each hold on a prefix or a suffix of the row, and the drawn
pixels are the union of two intervals whose ends are found with the same float expression (start at
the real crossing, step to the exact integer end). The pixels are bit-identical: every recorded cel
trace replays identically, the differential test gained 600 cel lists of long thin texels at random
angles (`test_long_thin_texels_match_reference`), and all goldens match. (The two intervals are never
both non-empty in practice: along a row the four edge values sum to a constant, so that needs a
degenerate quad. The code handles it anyway.)

| Renderer, ms per frame | level before | level after | rolled before | rolled after |
|---|---|---|---|---|
| 1x | 3.0 | 3.2 | 14.1 | 4.7 |
| 2x | 7.8 | 7.4 | 47.7 | 9.7 |
| 4x | 22.5 | 17.6 | 181 | 22.4 |

Whole game, `make test-performance ONLY=roll SCALE=n` (the scenario includes ~1,000 menu and
briefing frames before the roll): 1x 219 -> 375 fps, 2x 76 -> 183, 4x 22 -> 79.

New scenario `roll-and-turn` (full suite, `performance: yes`, goldens at 1x, 2x and 4x) keeps this
covered.
