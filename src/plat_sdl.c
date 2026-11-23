/* plat_sdl.c - SDL2 back end: window, keyboard/gamepad, audio device, timing
 * The test hooks in sf_testhooks.c work here too (SF_SCRIPT, SF_MAXFRAMES,
 * SF_DUMP, SF_HASH, SF_STATE, SF_AUDIO_DUMP, SF_RECORD, SF_WATCHDOG): scripted
 * buttons are ORed with the real input.  With SDL_VIDEODRIVER=dummy and
 * SDL_AUDIODRIVER=dummy the game runs without a display or sound card. Timing
 * is real, so unlike the headless build runs are not frame-exact.
 * The keyboard layout and the display SCALE are read from config.txt in the
 * current directory (see sf_keyconfig.h); if it is missing, one with the
 * defaults is written.  If it has invalid keys, a key used for two controls
 * or a bad setting, the game lists the problems and exits.  SF_SCALE, if
 * set, overrides SCALE. */
#include "sf_platform.h"
#include "sf_testhooks.h"
#include "sf_keyconfig.h"
#include "sf_videosys.h"
/* We keep our own main() (Star3000.c); on Windows tell SDL not to rename it to
 * SDL_main. */
#define SDL_MAIN_HANDLED
#include <SDL.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID adev;
static plat_audio_cb acb;
static SDL_GameController *pad_dev;
static int quit;
static uint32_t *argb; /* sf_fb_w x sf_fb_h */
static void load_keys(void);

int
plat_init(void)
{
  SDL_SetMainReady();
  th_init();
  th_set_exit_hook(plat_audio_close);
#ifdef _WIN32
  /* The default WASAPI backend crashes inside SDL2.dll on some 32-bit MinGW
     setups; DirectSound is reliable. A user-set SDL_AUDIODRIVER still wins. */
  SDL_setenv("SDL_AUDIODRIVER", "directsound", 0);
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER |
               SDL_INIT_TIMER) != 0) {
    fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 0;
  }
  load_keys(); /* also chooses the scale (SCALE in config.txt) */
  if (th_scale()) {
    video_set_scale(th_scale());
  }
  {
    /* 960x720 as before, or the frame-buffer size if larger, but no larger
       than the desktop's usable area (keeping 4:3) */
    int w = sf_fb_w > 960 ? sf_fb_w : 960, h = sf_fb_h > 720 ? sf_fb_h : 720;
    SDL_Rect r;
    if (SDL_GetDisplayUsableBounds(0, &r) == 0 && r.w > 0 && r.h > 0 &&
        (w > r.w || h > r.h)) {
      if (r.w * 3 < r.h * 4) {
        w = r.w;
        h = r.w * 3 / 4;
      } else {
        h = r.h;
        w = r.h * 4 / 3;
      }
    }
    win = SDL_CreateWindow("Star Fighter", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE);
  }
  if (!win) {
    return 0;
  }
  fprintf(stderr, "display: scale %d (%dx%d)\n", sf_scale, sf_fb_w, sf_fb_h);
  argb = (uint32_t *) malloc((size_t) sf_fb_w * sf_fb_h * 4);
  if (!argb) {
    return 0;
  }
  ren = SDL_CreateRenderer(
      win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) {
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!ren) {
    return 0;
  }
  SDL_RenderSetLogicalSize(ren, sf_fb_w, sf_fb_h);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, sf_fb_w, sf_fb_h);
  return tex != NULL;
}

void
plat_shutdown(void)
{
  plat_audio_close();
  if (pad_dev) {
    SDL_GameControllerClose(pad_dev);
  }
  if (tex) {
    SDL_DestroyTexture(tex);
  }
  if (ren) {
    SDL_DestroyRenderer(ren);
  }
  if (win) {
    SDL_DestroyWindow(win);
  }
  SDL_Quit();
}

void
plat_present(const uint16_t *fb, int br)
{
  int i;
  th_frame(fb, br, SDL_GetTicks());
  for (i = 0; i < sf_fb_w * sf_fb_h; i++) {
    uint32_t v = fb[i], r = ((v >> 10) & 31) << 3, g = ((v >> 5) & 31) << 3,
             b = (v & 31) << 3;
    r |= r >> 5;
    g |= g >> 5;
    b |= b >> 5;
    r = r * br >> 8;
    g = g * br >> 8;
    b = b * br >> 8;
    argb[i] = 0xff000000u | (r << 16) | (g << 8) | b;
  }
  SDL_UpdateTexture(tex, NULL, argb, sf_fb_w * 4);
  SDL_RenderClear(ren);
  SDL_RenderCopy(ren, tex, NULL, NULL);
  SDL_RenderPresent(ren);
}

uint32_t
plat_ticks_ms(void)
{
  return SDL_GetTicks();
}

void
plat_sleep_ms(uint32_t ms)
{
  uint32_t end = SDL_GetTicks() + ms;
  while ((int32_t) (end - SDL_GetTicks()) > 0) {
    plat_pad();
    SDL_Delay(1);
  }
}

/* ---- keyboard layout: built-in defaults, replaced from config.txt ---- */
#define KEYS_FILE "config.txt" /* in the directory the game is started from */

static const sf_keybind default_keys[] = {
    {SDLK_UP, ControlUp},          {SDLK_DOWN, ControlDown},
    {SDLK_LEFT, ControlLeft},      {SDLK_RIGHT, ControlRight},
    {SDLK_z, ControlA},            {SDLK_LCTRL, ControlA},
    {SDLK_x, ControlB},            {SDLK_LALT, ControlB},
    {SDLK_c, ControlC},            {SDLK_RETURN, ControlStart},
    {SDLK_SPACE, ControlStart},    {SDLK_ESCAPE, ControlX},
    {SDLK_BACKSPACE, ControlX},    {SDLK_a, ControlLeftShift},
    {SDLK_s, ControlRightShift},
};
static sf_keybind keys[KEYCFG_MAX];
static int nkeys;
static SDL_Keycode held[32]; /* keys currently down */
static int nheld;

static int32_t
key_lookup(const char *name)
{
  SDL_Keycode k = SDL_GetKeyFromName(name);
  return k == SDLK_UNKNOWN ? -1 : (int32_t) k;
}

static const char *
key_name(int32_t key)
{
  return SDL_GetKeyName((SDL_Keycode) key);
}

/* problems found in config.txt, printed as they are found and collected for
   the message box */
static char key_problems[2048];
static int nkey_problems;

static void
key_problem(const char *msg)
{
  size_t len = strlen(key_problems);
  fprintf(stderr, "%s\n", msg);
  nkey_problems++;
  if (len + strlen(msg) + 2 < sizeof key_problems) {
    snprintf(key_problems + len, sizeof key_problems - len, "%s\n", msg);
  }
}

static void
key_warning(int line, const char *problem, const char *text)
{
  char msg[300];
  snprintf(msg, sizeof msg, "%s:%d: %s: '%s'", KEYS_FILE, line, problem, text);
  key_problem(msg);
}

static void
describe(char *out, size_t n, const sf_keybind *b)
{
  if (b->line) {
    snprintf(out, n, "%s (line %d)", sf_keyconfig_name_for(b->mask), b->line);
  } else {
    snprintf(out, n, "%s (its built-in key; list %s in the file to change it)",
             sf_keyconfig_name_for(b->mask), sf_keyconfig_name_for(b->mask));
  }
}

static void
key_clash(const sf_keybind *a, const sf_keybind *b)
{
  char msg[400], da[120], db[120];
  const sf_keybind *first = (a->line && (!b->line || a->line <= b->line)) ? a : b;
  const sf_keybind *second = first == a ? b : a;
  describe(da, sizeof da, first);
  describe(db, sizeof db, second);
  snprintf(msg, sizeof msg, "%s: key '%s' is used for both %s and %s", KEYS_FILE,
           SDL_GetKeyName((SDL_Keycode) a->key), da, db);
  key_problem(msg);
}

/* Load the keyboard layout from config.txt; if there is none, write one
   describing the defaults so it can be edited.  If the file has mistakes
   (bad lines, unknown controls or keys, a key used for more than one
   control) they are all listed and the game exits. */
static void
load_keys(void)
{
  FILE *f;
  long size;
  char *text;
  nkeys = (int) (sizeof default_keys / sizeof *default_keys);
  memcpy(keys, default_keys, sizeof default_keys);
  f = fopen(KEYS_FILE, "rb");
  if (!f) {
    f = fopen(KEYS_FILE, "w");
    if (f) {
      sf_keyconfig_write(f, keys, nkeys, key_name);
      sf_config_write_settings(f, sf_scale);
      fclose(f);
      fprintf(stderr, "keys: wrote the default layout to %s\n", KEYS_FILE);
    } else {
      fprintf(stderr, "keys: no %s and cannot create one; using defaults\n",
              KEYS_FILE);
    }
    return;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  text = (char *) malloc((size_t) (size > 0 ? size : 0) + 1);
  if (!text || size < 0 || fread(text, 1, (size_t) size, f) != (size_t) size) {
    key_problem(KEYS_FILE ": cannot be read");
  } else {
    int scale;
    text[size] = 0;
    nkeys = sf_keyconfig_parse(text, keys, nkeys, key_lookup, key_warning);
    sf_keyconfig_check(keys, nkeys, key_clash);
    scale = sf_config_scale(text, key_warning);
    if (scale) {
      video_set_scale(scale);
    }
    sf_config_settings(text, key_warning); /* draw distances */
  }
  free(text);
  fclose(f);
  if (nkey_problems) {
    char title[64];
    fprintf(stderr,
            "%s has %d problem%s; fix %s or delete the file to use the "
            "defaults\n",
            KEYS_FILE, nkey_problems, nkey_problems == 1 ? "" : "s",
            nkey_problems == 1 ? "it" : "them");
    snprintf(title, sizeof title, "Star Fighter: problems in %s", KEYS_FILE);
    if (strlen(key_problems) + 80 < sizeof key_problems) {
      strcat(key_problems, "\nFix the file, or delete it to use the "
                           "defaults.");
    }
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, key_problems, NULL);
    SDL_Quit();
    exit(1);
  }
  fprintf(stderr, "keys: loaded %s\n", KEYS_FILE);
}

/* pad bits from the keys held down (several keys may drive one control) */
static uint32_t
held_mask(void)
{
  uint32_t m = 0;
  int i;
  for (i = 0; i < nheld; i++) {
    m |= sf_keyconfig_mask(keys, nkeys, (int32_t) held[i]);
  }
  return m;
}

static void
key_down(SDL_Keycode k)
{
  int i;
  for (i = 0; i < nheld; i++) {
    if (held[i] == k) {
      return;
    }
  }
  if (nheld < (int) (sizeof held / sizeof *held)) {
    held[nheld++] = k;
  }
}

static void
key_up(SDL_Keycode k)
{
  int i;
  for (i = 0; i < nheld; i++) {
    if (held[i] == k) {
      held[i] = held[--nheld];
      return;
    }
  }
}

/* open game controller number i (an SDL device index) as our pad */
static void
pad_open(int i)
{
  pad_dev = SDL_GameControllerOpen(i);
  if (pad_dev) {
    const char *name = SDL_GameControllerName(pad_dev);
    fprintf(stderr, "controller: using '%s'\n", name ? name : "?");
  } else {
    fprintf(stderr, "controller: cannot open device %d: %s\n", i,
            SDL_GetError());
  }
}

uint32_t
plat_pad(void)
{
  SDL_Event e;
  uint32_t m;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) {
      quit = 1;
    } else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
      key_down(e.key.keysym.sym);
    } else if (e.type == SDL_KEYUP) {
      key_up(e.key.keysym.sym);
    } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad_dev) {
      pad_open(e.cdevice.which);
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad_dev &&
               e.cdevice.which ==
                   SDL_JoystickInstanceID(
                       SDL_GameControllerGetJoystick(pad_dev))) {
      /* our controller was unplugged: drop it and use another one if any
         (when it is plugged in again, SDL sends CONTROLLERDEVICEADDED) */
      fprintf(stderr, "controller: disconnected\n");
      SDL_GameControllerClose(pad_dev);
      pad_dev = NULL;
      {
        int i;
        for (i = 0; i < SDL_NumJoysticks() && !pad_dev; i++) {
          if (SDL_IsGameController(i)) {
            pad_open(i);
          }
        }
      }
    }
  }
  m = held_mask();
  if (pad_dev) {
#define B(b) SDL_GameControllerGetButton(pad_dev, b)
    if (B(SDL_CONTROLLER_BUTTON_DPAD_UP)) {
      m |= ControlUp;
    }
    if (B(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
      m |= ControlDown;
    }
    if (B(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
      m |= ControlLeft;
    }
    if (B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
      m |= ControlRight;
    }
    if (B(SDL_CONTROLLER_BUTTON_A)) {
      m |= ControlA;
    }
    if (B(SDL_CONTROLLER_BUTTON_B)) {
      m |= ControlB;
    }
    if (B(SDL_CONTROLLER_BUTTON_X)) {
      m |= ControlC;
    }
    if (B(SDL_CONTROLLER_BUTTON_START)) {
      m |= ControlStart;
    }
    if (B(SDL_CONTROLLER_BUTTON_BACK)) {
      m |= ControlX;
    }
    if (B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) {
      m |= ControlLeftShift;
    }
    if (B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) {
      m |= ControlRightShift;
    }
    /* the triggers do the same as the shoulder buttons (by default the right
       one fires, the left one changes weapon) */
    if (SDL_GameControllerGetAxis(pad_dev, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >
        16000) {
      m |= ControlLeftShift;
    }
    if (SDL_GameControllerGetAxis(pad_dev, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >
        16000) {
      m |= ControlRightShift;
    }
    {
      int ax = SDL_GameControllerGetAxis(pad_dev, SDL_CONTROLLER_AXIS_LEFTX),
          ay = SDL_GameControllerGetAxis(pad_dev, SDL_CONTROLLER_AXIS_LEFTY);
      if (ax < -16000) {
        m |= ControlLeft;
      }
      if (ax > 16000) {
        m |= ControlRight;
      }
      if (ay < -16000) {
        m |= ControlUp;
      }
      if (ay > 16000) {
        m |= ControlDown;
      }
    }
#undef B
  }
  if (quit) {
    exit(0);
  }
  th_record_pad(m, SDL_GetTicks());
  return m | th_script_pad(SDL_GetTicks());
}

void
plat_stick(int32_t *x, int32_t *y, int32_t *b)
{
  *x = 128;
  *y = 128;
  *b = 0;
}

int
plat_quit_requested(void)
{
  return quit;
}

int
plat_audio_open(plat_audio_cb cb)
{
  SDL_AudioSpec want, have;
  acb = cb;
  SDL_zero(want);
  want.freq = 44100;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = NULL;
  want.userdata = NULL;
  {
    extern void sf_sdl_audio_trampoline(void *ud, Uint8 *stream, int len);
    want.callback = sf_sdl_audio_trampoline;
  }
  adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
#ifdef _WIN32
  if (!adev) { /* DirectSound unavailable: fall back to SDL's default driver
                  order */
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_setenv("SDL_AUDIODRIVER", "", 1);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
      adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    }
  }
#endif
  if (!adev) {
    return 0;
  }
  SDL_PauseAudioDevice(adev, 0);
  return 1;
}

void
sf_sdl_audio_trampoline(void *ud, Uint8 *stream, int len)
{
  (void) ud;
  if (acb) {
    acb((int16_t *) stream, len / 4);
    th_audio((const int16_t *) stream, len / 4);
  } else {
    SDL_memset(stream, 0, len);
  }
}

void
plat_audio_close(void)
{
  if (adev) {
    SDL_CloseAudioDevice(adev);
    adev = 0;
  }
}

void
plat_audio_lock(void)
{
  if (adev) {
    SDL_LockAudioDevice(adev);
  }
}

void
plat_audio_unlock(void)
{
  if (adev) {
    SDL_UnlockAudioDevice(adev);
  }
}
