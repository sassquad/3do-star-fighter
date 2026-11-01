/* SF_NVRam.c: save files under $SF_SAVEDIR. */
#include "sft.h"
#include "SF_NVRam.h"
#include <sys/stat.h>

static char dir[600];

static int
exists(const char *name)
{
  char p[1300];
  struct stat st;
  snprintf(p, sizeof p, "%s/%s", dir, name);
  return stat(p, &st) == 0;
}

static long
fsize(const char *name)
{
  char p[1300];
  struct stat st;
  snprintf(p, sizeof p, "%s/%s", dir, name);
  return stat(p, &st) == 0 ? (long) st.st_size : -1;
}

TEST(test_round_trip)
{
  char data[300], back[300];
  int i;
  for (i = 0; i < 300; i++) {
    data[i] = (char) (i * 7);
  }
  CHECK_EQ(nvram_save("Pilot One", "SF#", data, 300), 1);
  CHECK(exists("SF#Pilot One"));
  CHECK_EQ(fsize("SF#Pilot One"), 300);
  memset(back, 0, sizeof back);
  CHECK_EQ(nvram_load("Pilot One", "SF#", back, 300), 1);
  CHECK(memcmp(data, back, 300) == 0);
}

TEST(test_missing_and_short_files)
{
  char buf[64];
  CHECK_EQ(nvram_load("nobody", "SF#", buf, 64), 0);
  CHECK_EQ(nvram_save("small", "SF#", buf, 10), 1);
  CHECK_EQ(nvram_load("small", "SF#", buf, 64), 0); /* too short */
}

TEST(test_prefix_is_sanitised)
{
  /* the 3DO prefixes contain ':' ("SFC:"); '/' would leave the directory */
  char cfg[16] = "config", back[16];
  CHECK_EQ(nvram_save("StarFighter.Config", "SFC:", cfg, 16), 1);
  CHECK(exists("SFC_StarFighter.Config"));
  CHECK_EQ(nvram_save("a/b", "SF#", cfg, 16), 1);
  CHECK(exists("SF#a_b"));
  CHECK_EQ(nvram_load("a/b", "SF#", back, 16), 1);
}

TEST(test_delete)
{
  char buf[8] = {0};
  CHECK_EQ(nvram_save("gone", "SF#", buf, 8), 1);
  CHECK_EQ(nvram_delete("gone", "SF#"), 1);
  CHECK(!exists("SF#gone"));
  CHECK_EQ(nvram_delete("gone", "SF#"), 0);
}

int
main(void)
{
  const char *d = sft_tempdir("nvram");
  SFT_BEGIN();
  /* a nested path that does not exist yet: savedir() creates it */
  snprintf(dir, sizeof dir, "%s/a/b", d);
  setenv("SF_SAVEDIR", dir, 1);
  RUN(test_round_trip);
  RUN(test_missing_and_short_files);
  RUN(test_prefix_is_sanitised);
  RUN(test_delete);
  return SFT_END();
}
