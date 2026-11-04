/* sf_keyconfig.c - keyboard layout from config.txt (see sf_keyconfig.h). */
#include "sf_keyconfig.h"
#include "sf_compat.h"
#include <ctype.h>
#include <string.h>

static const struct {
  const char *name;
  uint32_t mask;
  const char *what; /* comment written to the default file */
} controls[] = {
    {"UP", ControlUp, "pad up: menus up; in flight, nose down"},
    {"DOWN", ControlDown, "pad down: menus down; in flight, nose up"},
    {"LEFT", ControlLeft, "pad left: roll left"},
    {"RIGHT", ControlRight, "pad right: roll right"},
    {"A", ControlA, "select; in flight (default setup) thrust"},
    {"B", ControlB, "in flight (default setup) ECM"},
    {"C", ControlC, "in flight (default setup) change camera"},
    {"START", ControlStart, "start; in flight (default setup) map"},
    {"X", ControlX, "stop / back; in flight (default setup) pause menu"},
    {"LS", ControlLeftShift, "left shift; in flight (default setup) select weapon"},
    {"RS", ControlRightShift, "right shift; in flight (default setup) fire"},
};
#define NCONTROLS ((int) (sizeof controls / sizeof *controls))

int
sf_keyconfig_controls(void)
{
  return NCONTROLS;
}

const char *
sf_keyconfig_control_name(int i)
{
  return (i >= 0 && i < NCONTROLS) ? controls[i].name : NULL;
}

uint32_t
sf_keyconfig_control_mask(int i)
{
  return (i >= 0 && i < NCONTROLS) ? controls[i].mask : 0;
}

static int
same_ci(const char *a, const char *b)
{
  while (*a && *b) {
    if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
      return 0;
    }
    a++;
    b++;
  }
  return *a == *b;
}

const char *
sf_keyconfig_name_for(uint32_t mask)
{
  int i;
  for (i = 0; i < NCONTROLS; i++) {
    if (controls[i].mask == mask) {
      return controls[i].name;
    }
  }
  return "?";
}

uint32_t
sf_keyconfig_mask_for(const char *name)
{
  int i;
  for (i = 0; i < NCONTROLS; i++) {
    if (same_ci(name, controls[i].name)) {
      return controls[i].mask;
    }
  }
  return 0;
}

/* copy s[0..len) without surrounding blanks into out */
static void
trimmed(char *out, size_t outsz, const char *s, size_t len)
{
  while (len && isspace((unsigned char) *s)) {
    s++;
    len--;
  }
  while (len && isspace((unsigned char) s[len - 1])) {
    len--;
  }
  if (len >= outsz) {
    len = outsz - 1;
  }
  memcpy(out, s, len);
  out[len] = 0;
}

static void
warn_(sf_keywarn warn, int line, const char *problem, const char *text)
{
  if (warn) {
    warn(line, problem, text);
  }
}

/* names of the settings lines (not controls) */
static int
is_setting(const char *name)
{
  return same_ci(name, "SCALE");
}

int
sf_config_scale(const char *text, sf_keywarn warn)
{
  int lineno = 0, scale = 0;
  const char *p = text;
  while (p && *p) {
    const char *end = strchr(p, '\n'), *eq;
    size_t len = end ? (size_t) (end - p) : strlen(p);
    char line[512], name[64], value[512];
    lineno++;
    trimmed(line, sizeof line, p, len);
    p = end ? end + 1 : NULL;
    if (!line[0] || line[0] == '#' || !(eq = strchr(line, '='))) {
      continue; /* sf_keyconfig_parse reports malformed lines */
    }
    trimmed(name, sizeof name, line, (size_t) (eq - line));
    trimmed(value, sizeof value, eq + 1, strlen(eq + 1));
    if (same_ci(name, "SCALE")) {
      char *e;
      long n = strtol(value, &e, 10);
      if (!value[0] || *e || n < 1 || n > SF_MAX_SCALE) {
        warn_(warn, lineno, "SCALE must be a whole number from 1 to 4", value);
      } else {
        scale = (int) n;
      }
    }
  }
  return scale;
}

void
sf_config_write_settings(FILE *f, int scale)
{
  fprintf(f,
          "# Display scale: the game is drawn at 320x240 times this number "
          "(1 to 4;\n"
          "# 2 = 640x480).  The 3D view gets sharper; the HUD, menus, text "
          "and videos\n"
          "# keep their size and look.  Higher scales need a faster "
          "computer.\n"
          "SCALE = %d\n",
          scale);
}

int
sf_keyconfig_parse(const char *text, sf_keybind *binds, int n,
                   sf_keylookup lookup, sf_keywarn warn)
{
  uint32_t replaced = 0; /* controls whose built-in keys are already dropped */
  int lineno = 0;
  const char *p = text;
  while (p && *p) {
    const char *end = strchr(p, '\n'), *eq;
    size_t len = end ? (size_t) (end - p) : strlen(p);
    char line[512], name[64], value[512];
    uint32_t mask;
    char *item;
    lineno++;
    trimmed(line, sizeof line, p, len);
    p = end ? end + 1 : NULL;
    if (!line[0] || line[0] == '#') {
      continue;
    }
    eq = strchr(line, '=');
    if (!eq) {
      warn_(warn, lineno, "expected CONTROL = key, key ...", line);
      continue;
    }
    trimmed(name, sizeof name, line, (size_t) (eq - line));
    trimmed(value, sizeof value, eq + 1, strlen(eq + 1));
    if (is_setting(name)) {
      continue; /* read by sf_config_scale etc. */
    }
    mask = sf_keyconfig_mask_for(name);
    if (!mask) {
      warn_(warn, lineno,
            "unknown control (use UP DOWN LEFT RIGHT A B C START X LS RS)",
            name);
      continue;
    }
    if (!(replaced & mask)) { /* first mention: drop the built-in keys */
      int i, k = 0;
      for (i = 0; i < n; i++) {
        if (binds[i].mask != mask) {
          binds[k++] = binds[i];
        }
      }
      n = k;
      replaced |= mask;
    }
    for (item = value; *item;) {
      char *comma = strchr(item, ',');
      char keyname[128];
      int32_t key;
      size_t ilen = comma ? (size_t) (comma - item) : strlen(item);
      trimmed(keyname, sizeof keyname, item, ilen);
      item = comma ? comma + 1 : item + ilen;
      if (!keyname[0]) {
        continue;
      }
      key = lookup(same_ci(keyname, "Comma") ? "," : keyname);
      if (key < 0) {
        warn_(warn, lineno, "unknown key name", keyname);
        continue;
      }
      {
        int i, dup = 0;
        for (i = 0; i < n; i++) {
          dup |= binds[i].key == key && binds[i].mask == mask;
        }
        if (dup) { /* the same key twice for one control: harmless */
          continue;
        }
      }
      if (n >= KEYCFG_MAX) {
        warn_(warn, lineno, "too many keys (the limit is 64)", keyname);
        continue;
      }
      binds[n].key = key;
      binds[n].mask = mask;
      binds[n].line = lineno;
      n++;
    }
  }
  return n;
}

void
sf_keyconfig_write(FILE *f, const sf_keybind *binds, int n,
                   sf_keyname keyname)
{
  int c, i;
  fprintf(f,
          "# Star Fighter keyboard controls and settings, read from this file "
          "when the\n# game starts.\n"
          "#\n"
          "#   CONTROL = key, key ...\n"
          "#\n"
          "# CONTROL is one of the 3DO pad controls below.  Keys are named as "
          "SDL names\n"
          "# them, in any case: Up, Down, Left, Right, A..Z, 0..9, Return, "
          "Space, Escape,\n"
          "# Backspace, Tab, Left Ctrl, Right Ctrl, Left Shift, Right Shift, "
          "Left Alt,\n"
          "# Right Alt, Keypad 0..9, Keypad Enter, F1..F12, Comma (for ','), "
          ". / ; ' [ ] - =\n"
          "# A control with nothing after '=' has no keys.  A control left "
          "out of this\n"
          "# file keeps its built-in keys.  Each key may be used for one "
          "control only.\n"
          "# If this file has a mistake the game lists it and does not "
          "start; delete\n"
          "# the file to get the defaults back.\n"
          "#\n"
          "# What A, B, C, START, X, LS and RS do in flight is set in the "
          "game, under\n"
          "# Configure > Setup Controls; the notes below give the default "
          "setup.\n"
          "# A game controller works as well and is not affected by this "
          "file.\n\n");
  for (c = 0; c < NCONTROLS; c++) {
    int first = 1;
    fprintf(f, "# %s\n%-5s = ", controls[c].what, controls[c].name);
    for (i = 0; i < n; i++) {
      if (binds[i].mask == controls[c].mask) {
        const char *kn = keyname(binds[i].key);
        if (kn && strcmp(kn, ",") == 0) {
          kn = "Comma";
        }
        fprintf(f, "%s%s", first ? "" : ", ", kn ? kn : "?");
        first = 0;
      }
    }
    fprintf(f, "\n\n");
  }
}

int
sf_keyconfig_check(const sf_keybind *binds, int n, sf_keyclash clash)
{
  int i, j, count = 0;
  for (i = 0; i < n; i++) {
    for (j = i + 1; j < n; j++) {
      if (binds[i].key == binds[j].key && binds[i].mask != binds[j].mask) {
        int k, seen = 0; /* report each pair of controls once per key */
        for (k = 0; k < i && !seen; k++) {
          seen = binds[k].key == binds[i].key &&
                 (binds[k].mask == binds[i].mask ||
                  binds[k].mask == binds[j].mask);
        }
        if (!seen) {
          count++;
          if (clash) {
            clash(&binds[i], &binds[j]);
          }
        }
      }
    }
  }
  return count;
}

uint32_t
sf_keyconfig_mask(const sf_keybind *binds, int n, int32_t key)
{
  uint32_t m = 0;
  int i;
  for (i = 0; i < n; i++) {
    if (binds[i].key == key) {
      m |= binds[i].mask;
    }
  }
  return m;
}
