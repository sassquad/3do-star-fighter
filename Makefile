# Star Fighter - SDL2 port.  Classic GNU Make.
#
#   make                 build ./starfighter against SDL2 (needs sdl2-config or pkg-config)
#   make HEADLESS=1      build ./starfighter-headless (no SDL: frame dumps + scripted input, for testing)
#   make clean
#
# Tests (see tests/README.md; need Python 3 and SF_Resources):
#   make test            build checks, unit tests, scenarios vs goldens, determinism, SDL smoke  (~2 min)
#   make test-asan       unit tests and quick scenarios built with AddressSanitizer + UBSan
#   make test-full       everything, including the long scenarios and the ASan run
#   make rebless [SCENARIOS="a b"]   re-record goldens after an intentional change
#   make celtrace        record the cel lists of a few in-game frames into build/celtraces/
#   make celbench [REPEAT=n]   replay them: same framebuffers as recorded? and time the cel engine
#   make restamp [SCENARIOS="a b"]   after a lossless SF_Resources conversion: record the new resources in the
#                        goldens, but only for scenarios whose frames and audio are still identical
#   make test-performance [REPEAT=n] [ONLY=text] [SCALE=n]   frames per second of the game engine (scenarios
#                              marked performance: yes), optionally at internal resolution scale n
#   RES=/path/to/SF_Resources selects the resources (default: $$SF_RESOURCES, else ./SF_Resources)
#
# Build variables: OBJDIR=dir BINDIR=dir (where objects / binaries go), SAN=address,undefined (sanitizers),
#   WERROR=1 (warnings are errors), COVERAGE=1 (gcov instrumentation), SSE41=0 (x86 CPUs without SSE4.1).
#
# Windows (MinGW-w64), either natively under MSYS2/MinGW or cross-compiled from Linux:
#   make CC=x86_64-w64-mingw32-gcc SDL2_PREFIX=/path/to/SDL2/x86_64-w64-mingw32
#   (SDL2_PREFIX is the SDL2 development package's mingw prefix; it is optional when sdl2-config/pkg-config work,
#    as in an MSYS2 shell.)  Produces starfighter.exe.  WINGUI=1 links as a GUI app (no console window).
#
CC      ?= cc
CFLAGS  ?= -O3 -g
# armcc's plain 'char' is unsigned; the game relies on that.
# These flags are required for correctness and are applied even if CFLAGS is given on the command line.
# -ffp-contract=off: the cel engine places pixels with float arithmetic; fused multiply-adds (allowed
# by default on FMA targets such as aarch64 or -march=native) would move pixels and break the goldens.
override CFLAGS += -funsigned-char -fwrapv -fno-strict-aliasing -ffp-contract=off -Wall \
           -Werror=implicit-function-declaration -Wno-unused-function -Isrc
LDLIBS  += -lm
MACHINE := $(shell $(CC) -dumpmachine 2>/dev/null)
# x86: SSE4.1 is the minimum CPU (2008 and later).  It gives the cel engine single-instruction float
# rounding (roundss), about a third of the game's speed.  SSE41=0 builds for older CPUs.
SSE41 ?= 1
ifneq (,$(and $(filter 1,$(SSE41)),$(or $(findstring x86_64,$(MACHINE)),$(findstring i686,$(MACHINE)),$(findstring i386,$(MACHINE)),$(findstring i586,$(MACHINE)))))
  override CFLAGS += -msse4.1
  # 32-bit x86 would otherwise do float maths on the x87 (80-bit), unlike every 64-bit build
  ifeq (,$(findstring x86_64,$(MACHINE)))
    override CFLAGS += -mfpmath=sse
  endif
endif
# MinGW detection: compiler triplet contains "mingw" (cross or native)
ifneq (,$(or $(findstring mingw,$(MACHINE)),$(filter Windows_NT,$(OS))))
#  SDL2_PREFIX := C:\MinGW
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
OBJDIR  ?= obj
BINDIR  ?= .
# Keep OBJDIR / BINDIR relative and spelt the same way between builds: the dependency files (-MMD)
# name targets as given, and recipes must also work in cmd.exe (mingw32-make), whose mkdir has no
# -p option and does not accept '/' in paths.

ifneq ($(SAN),)
  override CFLAGS  += -fsanitize=$(SAN) -fno-omit-frame-pointer
  override LDFLAGS += -fsanitize=$(SAN)
endif
ifeq ($(WERROR),1)
  override CFLAGS += -Werror
endif
ifeq ($(COVERAGE),1)
  override CFLAGS  += --coverage
  override LDFLAGS += --coverage
endif

COMMON_SRCS := $(filter-out $(SRCDIR)/plat_sdl.c $(SRCDIR)/plat_null.c, $(wildcard $(SRCDIR)/*.c))

ifeq ($(HEADLESS),1)
  PLAT_SRC := $(SRCDIR)/plat_null.c
  TARGET   := $(BINDIR)/starfighter-headless$(EXE)
else
  PLAT_SRC := $(SRCDIR)/plat_sdl.c
  TARGET   := $(BINDIR)/starfighter$(EXE)
  ifneq ($(SDL2_PREFIX),)
    SDL_CFLAGS := -I$(SDL2_PREFIX)/include/SDL2
    SDL_LIBS   := -L$(SDL2_PREFIX)/lib -lSDL2
  else
    SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null)
    SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null)
  endif
  ifeq ($(WIN32),1)
    # we use SDL_MAIN_HANDLED (own main()), so drop SDL2main and the GUI-subsystem flag that sdl2-config adds
#    SDL_LIBS := $(filter-out -lSDL2main -mwindows,$(SDL_LIBS))
    # PJH hack, sdl2-config is not present on my mingw32 setup
    SDL_LIBS := -lSDL2 -lsetupapi -lole32 -loleaut32 -limm32 -lversion -lwinmm -lgdi32 -luser32 -lshell32 -ladvapi32 -luuid -ldinput8 -ldxguid
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

# a separate output directory (test builds) is created like OBJDIR; the default '.' never is
ifneq ($(BINDIR),.)
$(TARGET): | $(BINDIR)
$(BINDIR):
	mkdir -p $(BINDIR)
endif

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

# ---- unit tests (HEADLESS=1 only) ----------------------------------------------------------------
# libsfcore.a holds the whole game linked against plat_null, with main() renamed to sf_game_main so
# that each tests/unit/test_*.c can provide its own main().
UNIT_SRCS := $(wildcard tests/unit/test_*.c)
UNITDIR   := $(BINDIR)/unit
UNIT_BINS := $(patsubst tests/unit/%.c,$(UNITDIR)/%$(EXE),$(UNIT_SRCS))
LIBCORE   := $(OBJDIR)/libsfcore.a

$(OBJDIR)/Star3000_lib.o: $(SRCDIR)/Star3000.c | $(OBJDIR)
	$(CC) $(CFLAGS) -Dmain=sf_game_main -MMD -MP -c -o $@ $<

$(LIBCORE): $(filter-out $(OBJDIR)/Star3000.o,$(OBJS)) $(OBJDIR)/Star3000_lib.o
	rm -f $@
	$(AR) rcs $@ $^

$(UNITDIR)/%$(EXE): tests/unit/%.c tests/unit/sft.h $(LIBCORE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests/unit -MMD -MP -c -o $(UNITDIR)/$*.o $<
	$(CC) $(LDFLAGS) -o $@ $(UNITDIR)/$*.o $(LIBCORE) $(LDLIBS)

# save-file fixture generator (tests/tools/sf_mksave.c): uses only the game's headers
$(BINDIR)/tools/sf_mksave$(EXE): tests/tools/sf_mksave.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# cel-list replay / benchmark (tests/tools/celbench.c, see src/sf_celtrace.h)
$(BINDIR)/tools/celbench$(EXE): tests/tools/celbench.c tests/ref/sf_cel_ref.c $(LIBCORE)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Itests -c -o $(BINDIR)/tools/celbench.o $<
	$(CC) $(LDFLAGS) -o $@ $(BINDIR)/tools/celbench.o $(LIBCORE) $(LDLIBS)

unit-tests: $(BINDIR)/tools/sf_mksave$(EXE) $(BINDIR)/tools/celbench$(EXE)
unit-tests:
ifneq ($(HEADLESS),1)
	$(error unit-tests needs HEADLESS=1)
endif
unit-tests: $(UNIT_BINS)

-include $(UNIT_BINS:$(EXE)=.d) $(OBJDIR)/Star3000_lib.d

# ---- test entry points (tests/run.py builds its own variants under build/) ----------------------
PYTHON ?= python3
RUNTEST = $(PYTHON) tests/run.py $(if $(RES),--resources "$(RES)")
test:
	$(RUNTEST) quick
test-asan:
	$(RUNTEST) asan
test-full:
	$(RUNTEST) full
rebless:
	$(RUNTEST) rebless $(SCENARIOS)
restamp:
	$(RUNTEST) restamp $(SCENARIOS)
celtrace:
	$(RUNTEST) celtrace $(if $(ONLY),--only $(ONLY))
celbench:
	$(RUNTEST) celbench $(if $(REPEAT),--repeat $(REPEAT)) $(if $(ONLY),--only $(ONLY))
test-performance:
	$(RUNTEST) performance $(if $(REPEAT),--repeat $(REPEAT)) $(if $(ONLY),--only $(ONLY)) $(if $(SCALE),--scale $(SCALE))

# macOS: build "build/Star Fighter.app" with SDL and the game data inside (see tools/make-macos-app.sh)
app:
	RES="$(RES)" tools/make-macos-app.sh
# macOS: SDL3 with libusb, which 'make app' then bundles, for wired Xbox controllers (needs cmake, libusb)
sdl3-libusb:
	tools/build-sdl3-libusb.sh

clean:
	rm -rf $(OBJDIR) build starfighter starfighter-headless starfighter.exe starfighter-headless.exe

-include $(DEPS)
.PHONY: all app sdl3-libusb clean unit-tests test test-asan test-full rebless restamp celtrace celbench test-performance
