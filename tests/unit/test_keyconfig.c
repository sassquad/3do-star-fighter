/* sf_keyconfig.c: the config.txt keyboard layout parser, clash check and
 * writer, with a
 * stand-in for SDL's key names (the real names are exercised by the SDL smoke
 * test, which writes and reads the default file). */
#include "sft.h"
#include "sf_compat.h"
#include "sf_keyconfig.h"
#include <ctype.h>
#include <strings.h>

/* fake key names: "Up" "Down" "Left" "Right" "Return" "Space" "Left Ctrl"
   "Keypad Enter" ",", or one printable character (code = the character) */
static const struct {
  const char *name;
  int32_t key;
} names[] = {{"Up", 1001},     {"Down", 1002},      {"Left", 1003},
             {"Right", 1004},  {"Return", 13},      {"Space", 32},
             {"Left Ctrl", 1005}, {"Keypad Enter", 1006}, {",", 0x2c},
             {"Escape", 27}};

static int32_t
lookup(const char *n)
{
  size_t i;
  for (i = 0; i < sizeof names / sizeof *names; i++) {
    if (strcasecmp(n, names[i].name) == 0) {
      return names[i].key;
    }
  }
  if (n[0] && !n[1] && isprint((unsigned char) n[0])) {
    return toupper((unsigned char) n[0]);
  }
  return -1;
}

static const char *
keyname(int32_t key)
{
  static char one[2];
  size_t i;
  for (i = 0; i < sizeof names / sizeof *names; i++) {
    if (names[i].key == key) {
      return names[i].name;
    }
  }
  one[0] = (char) key;
  return one;
}

static int nwarn, warn_lines[16];
static void
warn(int line, const char *problem, const char *text)
{
  (void) problem;
  (void) text;
  if (nwarn < 16) {
    warn_lines[nwarn] = line;
  }
  nwarn++;
}

/* the built-in layout of plat_sdl.c, with fake key codes */
static const sf_keybind defaults[] = {
    {1001, ControlUp},     {1002, ControlDown},     {1003, ControlLeft},
    {1004, ControlRight},  {'Z', ControlA},         {1005, ControlA},
    {'X', ControlB},       {'C', ControlC},         {13, ControlStart},
    {32, ControlStart},    {27, ControlX},          {'A', ControlLeftShift},
    {'S', ControlRightShift}};
#define NDEF ((int) (sizeof defaults / sizeof *defaults))

static sf_keybind k[KEYCFG_MAX];

static int
load(const char *text)
{
  memcpy(k, defaults, sizeof defaults);
  nwarn = 0;
  return sf_keyconfig_parse(text, k, NDEF, lookup, warn);
}

TEST(test_empty_file_keeps_defaults)
{
  int n = load("");
  CHECK_EQ(n, NDEF);
  n = load("# only comments\n\n   \n# more\n");
  CHECK_EQ(n, NDEF);
  CHECK_EQ(nwarn, 0);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'Z'), ControlA);
}

TEST(test_listed_control_replaces_its_keys)
{
  int n = load("A = Space\n");
  CHECK_EQX(sf_keyconfig_mask(k, n, 32), ControlA | ControlStart);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'Z'), 0);  /* old A keys gone */
  CHECK_EQX(sf_keyconfig_mask(k, n, 1005), 0); /* (both of them) */
  CHECK_EQX(sf_keyconfig_mask(k, n, 'X'), ControlB); /* others unchanged */
  CHECK_EQ(n, NDEF - 2 + 1);
}

TEST(test_several_keys_and_repeated_lines)
{
  int n = load("START = Return, Keypad Enter\nSTART = F\n");
  CHECK_EQX(sf_keyconfig_mask(k, n, 13), ControlStart);
  CHECK_EQX(sf_keyconfig_mask(k, n, 1006), ControlStart);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'F'), ControlStart); /* added, not reset */
  CHECK_EQX(sf_keyconfig_mask(k, n, 32), 0);
}

TEST(test_empty_value_unbinds)
{
  int n = load("X =\n");
  CHECK_EQX(sf_keyconfig_mask(k, n, 27), 0);
  CHECK_EQ(n, NDEF - 1);
  CHECK_EQ(nwarn, 0);
}

TEST(test_case_spaces_and_crlf)
{
  int n = load("  rs   =   left ctrl ,  q  \r\n\tup=w\r\n");
  CHECK_EQ(nwarn, 0);
  CHECK_EQX(sf_keyconfig_mask(k, n, 1005), ControlRightShift | ControlA);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'Q'), ControlRightShift);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'W'), ControlUp);
  CHECK_EQX(sf_keyconfig_mask(k, n, 1001), 0);
}

TEST(test_comma_key)
{
  int n = load("LS = Comma, .\n");
  CHECK_EQX(sf_keyconfig_mask(k, n, ','), ControlLeftShift);
  CHECK_EQX(sf_keyconfig_mask(k, n, '.'), ControlLeftShift);
}

TEST(test_mistakes_are_reported_and_skipped)
{
  int n = load("fire = F\n"         /* 1: unknown control */
               "B = NoSuchKey, G\n" /* 2: one bad key, G still bound */
               "just some words\n"  /* 3: no '=' */
               "C = H\n");          /* 4: fine */
  CHECK_EQ(nwarn, 3);
  CHECK_EQ(warn_lines[0], 1);
  CHECK_EQ(warn_lines[1], 2);
  CHECK_EQ(warn_lines[2], 3);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'G'), ControlB);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'H'), ControlC);
  CHECK_EQX(sf_keyconfig_mask(k, n, 'F'), 0);
}

TEST(test_binding_limit)
{
  char text[4000];
  int i, n;
  strcpy(text, "A = ");
  for (i = 0; i < 80; i++) { /* 80 keys: more than the 64 allowed */
    char item[8];
    snprintf(item, sizeof item, "%c, ", 33 + i);
    strcat(text, item);
  }
  n = load(text);
  CHECK_EQ(n, KEYCFG_MAX);
  CHECK(nwarn > 0);
}

TEST(test_written_file_reads_back)
{
  const char *dir = sft_tempdir("keys");
  char path[600], *text;
  FILE *f;
  long size;
  int n, i;
  REQUIRE(dir);
  snprintf(path, sizeof path, "%s/config.txt", dir);
  memcpy(k, defaults, sizeof defaults);
  k[NDEF].key = ',';
  k[NDEF].mask = ControlB; /* a ',' binding is written as "Comma" */
  f = fopen(path, "w");
  REQUIRE(f);
  sf_keyconfig_write(f, k, NDEF + 1, keyname);
  fclose(f);
  f = fopen(path, "rb");
  REQUIRE(f);
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  text = (char *) calloc(1, (size_t) size + 1);
  REQUIRE(text && fread(text, 1, (size_t) size, f) == (size_t) size);
  fclose(f);
  for (i = 0; i < sf_keyconfig_controls(); i++) {
    CHECK(strstr(text, sf_keyconfig_control_name(i)) != NULL);
  }
  nwarn = 0;
  n = sf_keyconfig_parse(text, k, 0, lookup, warn); /* from nothing */
  CHECK_EQ(nwarn, 0);
  CHECK_EQ(n, NDEF + 1);
  for (i = 0; i < NDEF; i++) {
    CHECK_EQX(sf_keyconfig_mask(k, n, defaults[i].key) & defaults[i].mask,
              defaults[i].mask);
  }
  CHECK_EQX(sf_keyconfig_mask(k, n, ','), ControlB);
  free(text);
}

/* ---- settings: SCALE ---- */
TEST(test_scale_setting)
{
  int n;
  nwarn = 0;
  CHECK_EQ(sf_config_scale("", warn), 0); /* not set */
  CHECK_EQ(sf_config_scale("# SCALE = 3\nA = Z\n", warn), 0);
  CHECK_EQ(sf_config_scale("SCALE = 2\n", warn), 2);
  CHECK_EQ(sf_config_scale("  scale=4  \r\n", warn), 4);
  CHECK_EQ(sf_config_scale("SCALE = 1\nSCALE = 3\n", warn), 3); /* the last one counts */
  CHECK_EQ(nwarn, 0);
  CHECK_EQ(sf_config_scale("SCALE = 0\nSCALE = 5\nSCALE = two\nSCALE =\nSCALE = 2x\n", warn), 0);
  CHECK_EQ(nwarn, 5);
  CHECK_EQ(warn_lines[0], 1);
  CHECK_EQ(warn_lines[4], 5);
  nwarn = 0;
  CHECK_EQ(sf_config_scale("SCALE = 9\nSCALE = 2\n", warn), 2); /* bad line ignored */
  CHECK_EQ(nwarn, 1);
  /* the key parser leaves the setting alone: no "unknown control" */
  n = load("SCALE = 2\nSTART = Return\n");
  CHECK_EQ(nwarn, 0);
  CHECK_EQ(n, NDEF - 1);
}

TEST(test_scale_written_and_read_back)
{
  const char *dir = sft_tempdir("keys2");
  char path[600], text[8192];
  FILE *f;
  size_t len;
  REQUIRE(dir);
  snprintf(path, sizeof path, "%s/config.txt", dir);
  f = fopen(path, "w");
  REQUIRE(f);
  sf_keyconfig_write(f, defaults, NDEF, keyname);
  sf_config_write_settings(f, 3);
  fclose(f);
  f = fopen(path, "rb");
  REQUIRE(f);
  len = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  text[len] = 0;
  nwarn = 0;
  CHECK_EQ(sf_config_scale(text, warn), 3);
  CHECK_EQ(load(text), NDEF);
  CHECK_EQ(nwarn, 0);
}

/* ---- keys used for more than one control ---- */
static const sf_keybind *clash_a[8], *clash_b[8];
static int nclash;
static void
clash(const sf_keybind *a, const sf_keybind *b)
{
  if (nclash < 8) {
    clash_a[nclash] = a;
    clash_b[nclash] = b;
  }
  nclash++;
}

static int
check(int n)
{
  nclash = 0;
  return sf_keyconfig_check(k, n, clash);
}

TEST(test_defaults_have_no_clash)
{
  int n = load("");
  CHECK_EQ(check(n), 0);
}

TEST(test_clash_with_builtin_key)
{
  int n = load("START = A\n"); /* A is LS's built-in key */
  REQUIRE(check(n) == 1);
  CHECK_EQ(clash_a[0]->key, 'A');
  CHECK_EQX(clash_a[0]->mask | clash_b[0]->mask, ControlStart | ControlLeftShift);
  CHECK_EQ(clash_a[0]->line + clash_b[0]->line, 1); /* line 1 and built-in */
  n = load("START = A\nLS = S\nRS = D\n"); /* moved out of the way: fine */
  CHECK_EQ(check(n), 0);
}

TEST(test_clash_between_lines)
{
  int n = load("RS = F\n# comment\nB = Q, F\n");
  REQUIRE(check(n) == 1);
  CHECK_EQ(clash_a[0]->key, 'F');
  CHECK_EQ(clash_a[0]->line, 1);
  CHECK_EQ(clash_b[0]->line, 3);
}

TEST(test_one_key_three_controls)
{
  int n = load("A = F\nB = F\nC = F\n");
  CHECK_EQ(check(n), 3); /* A-B, A-C, B-C */
}

TEST(test_same_key_twice_for_one_control_is_fine)
{
  int n = load("UP = W, w\nUP = W\n");
  CHECK_EQ(nwarn, 0);
  CHECK_EQ(check(n), 0);
  CHECK_EQ(n, NDEF); /* old Up key gone, W added once */
}

TEST(test_control_names)
{
  CHECK_EQ(sf_keyconfig_controls(), 11);
  CHECK_EQX(sf_keyconfig_mask_for("start"), ControlStart);
  CHECK_EQX(sf_keyconfig_mask_for("Ls"), ControlLeftShift);
  CHECK_EQX(sf_keyconfig_mask_for("STARTX"), 0);
  CHECK_EQX(sf_keyconfig_mask_for(""), 0);
  CHECK_STR(sf_keyconfig_name_for(ControlRightShift), "RS");
  CHECK_STR(sf_keyconfig_name_for(0), "?");
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_empty_file_keeps_defaults);
  RUN(test_listed_control_replaces_its_keys);
  RUN(test_several_keys_and_repeated_lines);
  RUN(test_empty_value_unbinds);
  RUN(test_case_spaces_and_crlf);
  RUN(test_comma_key);
  RUN(test_mistakes_are_reported_and_skipped);
  RUN(test_binding_limit);
  RUN(test_written_file_reads_back);
  RUN(test_scale_setting);
  RUN(test_scale_written_and_read_back);
  RUN(test_defaults_have_no_clash);
  RUN(test_clash_with_builtin_key);
  RUN(test_clash_between_lines);
  RUN(test_one_key_three_controls);
  RUN(test_same_key_twice_for_one_control_is_fine);
  RUN(test_control_names);
  return SFT_END();
}
