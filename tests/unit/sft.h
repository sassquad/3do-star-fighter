/* sft.h - minimal single-header unit test harness producing TAP output.
 *
 *   #include "sft.h"
 *   TEST(name) { CHECK(x == 1); CHECK_EQ(a, b); CHECK_EQX(word, 0x1234); }
 *   int main(void) { SFT_BEGIN(); RUN(name); return SFT_END(); }
 *
 * Each test binary is linked against libsfcore.a (the whole game, main renamed
 * to sf_game_main, platform = plat_null).  Global game state is shared within
 * one binary, so each test sets up what it needs itself.
 *
 * sft_resources() returns the SF_Resources directory ($SF_RESOURCES, else
 * ./SF_Resources) and points the game's loader at it.
 */
#ifndef SFT_H
#define SFT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

static int sft_tests, sft_failed_tests, sft_cur_failed;
static const char *sft_cur_name;

#define TEST(name) static void name(void)

#define SFT_FAIL_(...)                                                         \
  do {                                                                         \
    printf("#   %s:%d: ", __FILE__, __LINE__);                                 \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
    sft_cur_failed++;                                                          \
  } while (0)

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      SFT_FAIL_("CHECK(%s) failed", #c);                                       \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    long long sft_a_ = (long long) (a), sft_b_ = (long long) (b);              \
    if (sft_a_ != sft_b_) {                                                    \
      SFT_FAIL_("%s == %s failed: %lld != %lld", #a, #b, sft_a_, sft_b_);      \
    }                                                                          \
  } while (0)

#define CHECK_EQX(a, b)                                                        \
  do {                                                                         \
    unsigned long long sft_a_ = (unsigned long long) (a),                      \
                       sft_b_ = (unsigned long long) (b);                      \
    if (sft_a_ != sft_b_) {                                                    \
      SFT_FAIL_("%s == %s failed: 0x%llx != 0x%llx", #a, #b, sft_a_, sft_b_);  \
    }                                                                          \
  } while (0)

#define CHECK_STR(a, b)                                                        \
  do {                                                                         \
    const char *sft_a_ = (a), *sft_b_ = (b);                                   \
    if (strcmp(sft_a_, sft_b_) != 0) {                                         \
      SFT_FAIL_("%s == %s failed: \"%s\" != \"%s\"", #a, #b, sft_a_, sft_b_);  \
    }                                                                          \
  } while (0)

/* Stop the current test (after a failed precondition). */
#define REQUIRE(c)                                                             \
  do {                                                                         \
    if (!(c)) {                                                                \
      SFT_FAIL_("REQUIRE(%s) failed", #c);                                     \
      return;                                                                  \
    }                                                                          \
  } while (0)

static void
sft_run(const char *name, void (*fn)(void))
{
  sft_cur_name = name;
  sft_cur_failed = 0;
  fflush(stdout);
  fn();
  sft_tests++;
  if (sft_cur_failed) {
    sft_failed_tests++;
  }
  printf("%s %d - %s\n", sft_cur_failed ? "not ok" : "ok", sft_tests, name);
  fflush(stdout);
}

#define RUN(name) sft_run(#name, name)
#define SFT_BEGIN() setvbuf(stdout, NULL, _IOLBF, 0)
#define SFT_END()                                                              \
  (printf("1..%d\n", sft_tests), sft_failed_tests ? 1 : 0)

/* ---- helpers shared by several tests ---- */
extern char sf_resource_root[512];

static const char *
sft_resources(void)
{
  static char root[512];
  const char *r = getenv("SF_RESOURCES");
  if (!r || !*r) {
    r = "SF_Resources";
  }
  snprintf(root, sizeof root, "%s%s", r, r[strlen(r) - 1] == '/' ? "" : "/");
  snprintf(sf_resource_root, sizeof sf_resource_root, "%s", root);
  return root;
}

/* Make a fresh empty temporary directory (under $TMPDIR or /tmp). */
static const char *
sft_tempdir(const char *tag)
{
  static char dir[512];
  const char *t = getenv("TMPDIR");
  char cmd[1200];
  snprintf(dir, sizeof dir, "%s/sft-%s-%ld", t && *t ? t : "/tmp", tag,
           (long) getpid());
  snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", dir, dir);
  if (system(cmd) != 0) {
    return NULL;
  }
  return dir;
}

#endif
