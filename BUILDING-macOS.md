# Building and playing Star Fighter on a Mac

This guide takes you from the downloaded source code to a **Star Fighter** app you can double-click,
then explains the controls. You don't need to know how to program. You'll type a few commands into
Terminal, and the whole process takes about 15 minutes, most of it waiting for downloads.

Works on Apple Silicon (M1 and later) and Intel Macs.

> **Were you sent the ready-made app?** If you already have **Star Fighter.app**, you don't need to
> build anything: skip to [4. First launch](#4-first-launch). If macOS says the app can't be opened,
> see [Troubleshooting](#7-troubleshooting).

---

## 1. Install the tools (one time only)

### 1a. Apple's command line tools

These provide the compiler that turns the source code into a program.

1. Open **Terminal**: press ⌘ Space, type `Terminal` and press Return.
2. Paste this line and press Return:

   ```sh
   xcode-select --install
   ```

3. A window asks whether to install the tools. Click **Install** and wait for it to finish.
   If it says the tools are *already installed*, that's fine: go on to the next step.

### 1b. Homebrew and SDL2

[Homebrew](https://brew.sh) is a free package manager for macOS. The game uses it to get **SDL2**,
the library that handles the window, sound, keyboard and game controllers.

1. If you don't have Homebrew yet, open <https://brew.sh>, copy the install command shown on the
   page, paste it into Terminal and press Return. Follow the instructions it prints at the end.
   They include two or three lines to copy and run so that the `brew` command works.
2. Install SDL2:

   ```sh
   brew install sdl2
   ```

   (Homebrew now installs "sdl2-compat", which runs SDL2 programs on the newer SDL3. That's
   expected: the game works with it, and the app built below includes both libraries.)

3. **Optional, for a wired Xbox controller** (Xbox One / Series, including third-party pads such
   as PowerA): also install

   ```sh
   brew install cmake libusb
   ```

   macOS has no driver for these controllers over USB, so the game needs its own copy of SDL3
   that can talk to them directly. Step 3 builds it. You don't need this for the keyboard, or for
   controllers that macOS supports itself (an Xbox Wireless Controller paired over Bluetooth, or a
   PlayStation or Switch Pro controller).

---

## 2. Get the game folder ready

You need the downloaded **star-fighter-sdl** folder. It contains `Makefile`, `src` and
**`SF_Resources`**, which holds the game's data: graphics, missions, music, voices and,
if included, the videos. Without `SF_Resources` the game can't run.

In Terminal, move into the folder. Type `cd ` (with a space after it), drag the folder from
Finder onto the Terminal window, then press Return. It will look something like this:

```sh
cd ~/Downloads/star-fighter-sdl
```

---

## 3. Build the app

**If you'll play with a wired Xbox controller,** first run this once. It downloads SDL3, checks
the download, and builds it with USB controller support. It takes a few minutes:

```sh
make sdl3-libusb
```

Then, still in Terminal, run:

```sh
make app
```

The build prints `SDL3 with libusb: wired Xbox controllers supported` if it found that SDL3.
Otherwise it prints a note saying wired Xbox controllers won't work.

This takes a minute or two. Lines of compiler output scroll past. One warning is normal and can be
ignored: `ld: warning: reducing alignment of section __DATA,__common`.

When it finishes, the last line looks like this:

```
== done: .../build/Star Fighter.app (427M, macOS 27.2+, arm64)
```

Your app is **`build/Star Fighter.app`**. It's self-contained: SDL and all of the game data
are inside it.

To open the folder in Finder:

```sh
open build
```

You can drag **Star Fighter.app** into your **Applications** folder and start it from there,
from Launchpad, or from Spotlight like any other app.

> **Keeping the data somewhere else?** If your `SF_Resources` folder isn't inside the game folder,
> tell the build where it is: `make app RES=/path/to/SF_Resources`.

---

## 4. First launch

Double-click **Star Fighter**. The intro plays (if the video files are present), followed by
the main menu.

The game starts in a small 320×240 window, the original 3DO resolution. The window can be resized
and maximised. To render at a higher resolution, see [Settings](#6-settings) below.

---

## 5. Controls

The game was designed for the 3DO joypad, so each key stands in for a pad button.

> **Important:** to select a menu item, press **Z**, not Return.
> Return and Space are the pad's *Start* button, which goes **back** in menus.

### In the menus

| Key | Does |
|---|---|
| ↑ ↓ ← → | Move the cursor |
| **Z** | **Select** |
| Return / Space, or Esc / Backspace | Back to the previous menu |

### In flight

| Key | Does |
|---|---|
| ↑ ↓ | Pitch (nose down / up) |
| ← → | Turn |
| **S** | **Fire** the selected weapon |
| **A** | Switch weapon (laser, missiles, mines…) |
| **Z** | Thrust |
| **X** | Fire ECM (countermeasures) |
| **C** | Change camera view |
| Return / Space | Map |
| Esc / Backspace | In-flight menu |

These are the game's defaults. The in-game **flight controls** menu lets you reassign the actions
(for example, put Fire on a different button) or swap up and down for pitch.

### With a game controller

Xbox, PlayStation and Switch Pro controllers work. You can connect or reconnect one at any time,
including while the game is running. On an Xbox controller:

| Xbox button | Menus | Flight |
|---|---|---|
| **A** | **Select** | Thrust |
| B | — | Fire ECM |
| X | — | Change camera view |
| **RB** or **RT** | — | **Fire** |
| LB or LT | — | Switch weapon |
| Menu (☰, "Start") | Back | Map |
| View (⧉, "Back") | Back | In-flight menu |
| D-pad or left stick | Move | Steer |

(PlayStation pads use the same positions: Cross = A, Circle = B, Square = X, R1/R2 = fire.)

> **Quit Steam before playing with a controller.** While Steam is running, it takes over Xbox
> controllers for itself and the game can't see them. Use **Steam ▸ Quit Steam** in the menu bar;
> closing the window isn't enough.

---

## 6. Settings

The game keeps its settings and saves in your user Library, not inside the app:

```
~/Library/Application Support/Star Fighter/
    config.txt          keyboard layout, resolution, draw distances
    Saves/              saved games
    starfighter.log     messages from the game (useful if something goes wrong)
```

To get there, open Finder, choose **Go ▸ Go to Folder…** (⇧⌘G) and paste
`~/Library/Application Support/Star Fighter`.

**`config.txt`** is created the first time you run the game. Open it in TextEdit, change it,
save, and restart the game. It explains each setting. The most useful ones:

- **Change keys.** Each line lists the keys for one pad button. For example, to select menu items
  with Return as well as Z (Space then remains the Start button):

  ```
  A     = Z, Return
  START = Space
  ```

  A key can belong to only one button. If the file has a mistake, the game shows a message
  listing the problem and quits. To go back to the defaults, delete `config.txt`.

- **Sharper graphics.** `SCALE = 2` (640×480), `3` or `4` (1280×960) draws the 3D view at a higher
  resolution. The menus and cockpit keep their original look. Higher values use more CPU.

- **Draw distances.** `LAND_NEAR`, `LAND_FAR`, `OBJECT_DISTANCE` and the related settings make the
  landscape and objects visible further away.

---

## 7. Troubleshooting

**Nothing happens when I press Return in the menu.**
Use **Z** to select (see [Controls](#5-controls)).

**Left Ctrl + arrow keys switch desktops or open Mission Control.**
Left Ctrl is also a pad button (A) in the default layout, but macOS uses Ctrl + arrows for its own
shortcuts. Use **Z** instead, or turn those shortcuts off in
**System Settings ▸ Keyboard ▸ Keyboard Shortcuts ▸ Mission Control**.

**The app won't open on another Mac ("can't be opened" / "damaged").**
The app is signed only for the Mac it was built on. On the other Mac, either build it there
using this guide, or right-click the app, choose **Open**, then click **Open** again.
If that doesn't work, run this in Terminal once and try again:
`xattr -dr com.apple.quarantine "/Applications/Star Fighter.app"`.
The other Mac also needs the same type of processor (Apple Silicon or Intel) and a macOS version
at least as new as the one the app was built on.

**My controller doesn't do anything.**
1. Quit Steam completely (**Steam ▸ Quit Steam**). It takes Xbox controllers for itself, even when
   it's only running in the background.
2. For a *wired* Xbox One / Series controller, the app must be built with USB controller support:
   run `make sdl3-libusb` and then `make app` again (see [step 3](#3-build-the-app)).
3. Look in `starfighter.log` (see [Settings](#6-settings)). When the game finds a controller it
   writes a line like `controller: using 'PowerA Xbox Series X Wired Controller Black'`. If that
   line is missing, the game didn't see the controller. Unplug it, plug it back in, and restart the
   game.

**The game quits immediately or shows an error.**
Look at the end of `starfighter.log` (see [Settings](#6-settings)). A common cause is a mistake in
`config.txt`: delete the file and the game creates a fresh one.

**`make: sdl2-config: command not found` or `SDL.h not found`.**
SDL2 isn't installed, or Homebrew isn't set up in your Terminal. Run `brew install sdl2`. If
`brew` itself isn't found, re-run the setup lines Homebrew printed when it was installed, then
open a new Terminal window.

**`make app` says `no game data at …`.**
The build can't find `SF_Resources`. Check that it's inside the game folder, or give its location
with `make app RES=/path/to/SF_Resources`.

**No videos play.**
Videos are optional and are skipped when their files aren't in `SF_Resources/Video`.

---

## For developers

- `make` builds a plain `./starfighter` binary that uses Homebrew's SDL; run it with
  `./starfighter SF_Resources`. It reads and writes `config.txt` in the current directory and
  saves to `~/.local/share/starfighter` (or `$SF_SAVEDIR`).
- `make app` runs [`tools/make-macos-app.sh`](tools/make-macos-app.sh). The script builds into
  `build/macapp/`, copies the SDL2 dylib (and, for sdl2-compat, `libSDL3.dylib`, which it loads from
  its own directory) into `Contents/Frameworks`, rewrites the library paths with
  `install_name_tool`, copies `SF_Resources` into `Contents/Resources`, makes an icon from
  `Images/Menu0.png`, and ad-hoc signs the bundle. `tools/make-macos-app.sh --no-resources` leaves
  the data out; the app then needs `SF_RESOURCES` set.
- `make sdl3-libusb` runs [`tools/build-sdl3-libusb.sh`](tools/build-sdl3-libusb.sh): SDL3 3.4.20
  (checked against a pinned SHA-256), built with `SDL_HIDAPI_LIBUSB=ON` and libusb linked in,
  installed to `build/deps/sdl3-install`. `make app` bundles that SDL3 and libusb when present
  (`SDL3_LIB=/path/to/libSDL3.0.dylib` picks another). Wired Xbox One / Series pads use
  Microsoft's GIP protocol on a vendor-specific USB interface (class `ff/47/d0`), not HID, so
  macOS doesn't expose them. SDL's HIDAPI Xbox One driver drives them through libusb on macOS.
  If another process holds the device (`ioreg -l -r -n '<device name>' | grep UsbExclusiveOwner`;
  usually Steam), libusb's claim fails with `LIBUSB_ERROR_ACCESS` and SDL sees no controller.
- The SDL front end (`src/plat_sdl.c`) uses the first game controller connected, picks up another
  one or the same one again after a disconnect, treats the triggers as the shoulder buttons, and
  logs the controller's name. `SF_RECORD=file` records the pad buttons the game sees; it's useful
  for checking a controller.
- `make test` passes on macOS (Python 3 from the command line tools is enough). The linker's
  alignment warning appears as a *warn* on the build checks and is listed in
  `tests/clang-warnings.txt`. The 3DO golden-frame comparisons are skipped, with a warning, when your
  `SF_Resources` differs from the copy the goldens were recorded with.
- To give the app to other people without the warnings in
  [Troubleshooting](#7-troubleshooting), sign it with a Developer ID and notarize it with Apple.
