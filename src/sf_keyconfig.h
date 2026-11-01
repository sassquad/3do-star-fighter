/* sf_keyconfig.h - keyboard layout from a text file (config.txt).
 *
 * File format, one control per line:
 *
 *     # comment
 *     START = Return, Space
 *     A     = Z, Left Ctrl
 *     X     =                  (no keys: control unbound)
 *
 * The left side is a 3DO pad control: UP DOWN LEFT RIGHT A B C START X LS RS
 * (any case).  The right side is a comma-separated list of key names as SDL
 * names them ("Up", "Z", "Left Ctrl", "Return", "Keypad 8", "F1" ...; any
 * case); "Comma" stands for the ',' key.  A control listed in the file loses
 * its built-in keys; controls not listed keep them.  Each key may drive only
 * one control (sf_keyconfig_check).  The parser reports every mistake with its
 * line number; the game refuses to start if there are any.
 *
 * Nothing here depends on SDL: key names are turned into key codes by the
 * caller's lookup function, so the parser can be tested without a display. */
#ifndef SF_KEYCONFIG_H
#define SF_KEYCONFIG_H
#include <stdint.h>
#include <stdio.h>

#define KEYCFG_MAX 64 /* key bindings in total */

typedef struct {
  int32_t key;   /* platform key code */
  uint32_t mask; /* ControlXxx bit it drives */
  int line;      /* line of config.txt, 0 for a built-in key */
} sf_keybind;

/* key name -> key code, or -1 if the name is not a key */
typedef int32_t (*sf_keylookup)(const char *name);
/* key code -> display name (for writing the default file) */
typedef const char *(*sf_keyname)(int32_t key);
/* called for each problem in the file */
typedef void (*sf_keywarn)(int line, const char *problem, const char *text);
/* called for each key that drives two controls (a and b: the bindings) */
typedef void (*sf_keyclash)(const sf_keybind *a, const sf_keybind *b);

/* The pad controls in file order: name and ControlXxx mask. */
int sf_keyconfig_controls(void);
const char *sf_keyconfig_control_name(int i);
uint32_t sf_keyconfig_control_mask(int i);
/* ControlXxx mask for a control name (any case), 0 if unknown. */
uint32_t sf_keyconfig_mask_for(const char *name);

/* Control name for a ControlXxx mask, "?" if none. */
const char *sf_keyconfig_name_for(uint32_t mask);

/* Apply 'text' (the whole file) on top of the n bindings in binds (capacity
   KEYCFG_MAX).  Every problem (bad line, unknown control or key, too many
   keys) is passed to warn and that item is left out.  A key listed twice for
   the same control is kept once.  Returns the new number of bindings. */
int sf_keyconfig_parse(const char *text, sf_keybind *binds, int n,
                       sf_keylookup lookup, sf_keywarn warn);

/* Report every key that drives more than one control (once per pair).
   Returns the number of clashes. */
int sf_keyconfig_check(const sf_keybind *binds, int n, sf_keyclash clash);

/* Write a commented file describing the bindings. */
void sf_keyconfig_write(FILE *f, const sf_keybind *binds, int n,
                        sf_keyname keyname);

/* Pad bits driven by a key. */
uint32_t sf_keyconfig_mask(const sf_keybind *binds, int n, int32_t key);
#endif
