/* plat_sdl.c - SDL2 back end: window, keyboard/gamepad, audio device, timing */
#include "sf_platform.h"
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
static uint32_t keymask;
static uint32_t argb[320 * 240];

int
plat_init(void)
{
  SDL_SetMainReady();
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
  win =
      SDL_CreateWindow("Star Fighter", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE);
  if (!win) {
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
  SDL_RenderSetLogicalSize(ren, 320, 240);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_STREAMING, 320, 240);
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
  for (i = 0; i < 320 * 240; i++) {
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
  SDL_UpdateTexture(tex, NULL, argb, 320 * 4);
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

static uint32_t
keymap(SDL_Keycode k)
{
  switch (k) {
    case SDLK_UP:
      return ControlUp;
    case SDLK_DOWN:
      return ControlDown;
    case SDLK_LEFT:
      return ControlLeft;
    case SDLK_RIGHT:
      return ControlRight;
    case SDLK_z:
    case SDLK_LCTRL:
      return ControlA;
    case SDLK_x:
    case SDLK_LALT:
      return ControlB;
    case SDLK_c:
      return ControlC;
    case SDLK_RETURN:
    case SDLK_SPACE:
      return ControlStart;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
      return ControlX;
    case SDLK_a:
      return ControlLeftShift;
    case SDLK_s:
      return ControlRightShift;
  }
  return 0;
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
      keymask |= keymap(e.key.keysym.sym);
    } else if (e.type == SDL_KEYUP) {
      keymask &= ~keymap(e.key.keysym.sym);
    } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad_dev) {
      pad_dev = SDL_GameControllerOpen(e.cdevice.which);
    }
  }
  m = keymask;
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
  return m;
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
