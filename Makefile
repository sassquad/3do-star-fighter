# Star Fighter - SDL2 port.  Classic GNU Make.
#
#   make                 build ./starfighter against SDL2 (needs sdl2-config or pkg-config)
#   make HEADLESS=1      build ./starfighter-headless (no SDL: frame dumps + scripted input, for testing)
#   make clean
#
# Windows (MinGW-w64), either natively under MSYS2/MinGW or cross-compiled from Linux:
#   make CC=x86_64-w64-mingw32-gcc SDL2_PREFIX=/path/to/SDL2/x86_64-w64-mingw32
#   (SDL2_PREFIX is the SDL2 development package's mingw prefix; it is optional when sdl2-config/pkg-config work,
#    as in an MSYS2 shell.)  Produces starfighter.exe.  WINGUI=1 links as a GUI app (no console window).
#
CC      ?= cc
CFLAGS  ?= -O2 -g
# armcc's plain 'char' is unsigned; the game relies on that.
# These flags are required for correctness and are applied even if CFLAGS is given on the command line.
override CFLAGS += -funsigned-char -fwrapv -fno-strict-aliasing -Wall \
           -Werror=implicit-function-declaration -Wno-unused-function -Isrc
LDLIBS  += -lm
# MinGW detection: compiler triplet contains "mingw" (cross or native)
MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
ifneq (,$(or $(findstring mingw,$(MACHINE)),$(filter Windows_NT,$(OS))))
  SDL2_PREFIX := C:\MinGW
  WIN32 := 1
  EXE   := .exe
  LDFLAGS += -static-libgcc
  ifeq ($(WINGUI),1)
    LDFLAGS += -mwindows
  endif
else ifeq ($(HEADLESS),1)
  LDFLAGS += -rdynamic
endif

SRCDIR  := src
OBJDIR  := obj

COMMON_SRCS := $(filter-out $(SRCDIR)/plat_sdl.c $(SRCDIR)/plat_null.c, $(wildcard $(SRCDIR)/*.c))

ifeq ($(HEADLESS),1)
  PLAT_SRC := $(SRCDIR)/plat_null.c
  TARGET   := starfighter-headless$(EXE)
else
  PLAT_SRC := $(SRCDIR)/plat_sdl.c
  TARGET   := starfighter$(EXE)
  ifneq ($(SDL2_PREFIX),)
    SDL_CFLAGS := -I$(SDL2_PREFIX)/include/SDL2
    SDL_LIBS   := -L$(SDL2_PREFIX)/lib -lSDL2
  else
    SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
    SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)
  endif
  ifeq ($(WIN32),1)
    # we use SDL_MAIN_HANDLED (own main()), so drop SDL2main and the GUI-subsystem flag that sdl2-config adds
    SDL_LIBS := $(filter-out -lSDL2main -mwindows,$(SDL_LIBS))
  endif
  ifeq ($(strip $(SDL_LIBS)),)
    $(warning SDL2 development files not found (sdl2-config / pkg-config sdl2). Install libsdl2-dev or use HEADLESS=1)
    SDL_LIBS := -lSDL2
  endif
  override CFLAGS += $(SDL_CFLAGS)
  LDLIBS += $(SDL_LIBS)
endif

SRCS := $(COMMON_SRCS) $(PLAT_SRC)
OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) starfighter starfighter-headless starfighter.exe starfighter-headless.exe

-include $(DEPS)
.PHONY: all clean
