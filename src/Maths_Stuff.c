/* Maths_Stuff.c - C translation of Maths_Stuff.s
 *
 * Angles come in two scales: "table" angles 0..1023 index cosine_table directly
 * (sine = cosine_table[angle + 768]); node rotations are 20-bit (1024*1024 =
 * 360 degrees) and are shifted right by 10 before use.
 */
#include "Maths_Stuff.h"
#include "Plot_Graphic.h"
#include "Misc_Struct.h"
#include "Smoke_Struct.h"
#include "Draw_Frame.h"
#include "Setup_Tables.h"
#include "SF_Access.h"
#include "arm_ops.h"

#define SINOFF 768 /* 1024*3 bytes / 4 */

static int32_t camera_rotations[9];

/* rotate (a,b) by angle: a' = a*c - b*s, b' = a*s + b*c  (each product shifted
 * separately) */
static void
rot_pair(int32_t *a, int32_t *b, int32_t ang)
{
  int32_t c, s;
  /* Angles are 0..1023 (a full turn); wrap anything outside, e.g. the -1 a
     demo ship's negative y rotation gives, which read cosine_table[-1]
     (found by ASan in the attract-mode demo). */
  ang &= 1023;
  c = cosine_table[ang];
  s = cosine_table[ang + SINOFF];
  int32_t na = ASR(MULS(*a, c), 12) - ASR(MULS(*b, s), 12);
  int32_t nb = ASR(MULS(*a, s), 12) + ASR(MULS(*b, c), 12);
  *a = na;
  *b = nb;
}

void
rotate_coords_x(int32_t *x, int32_t *y, int32_t *z, int32_t ang)
{
  (void) z;
  rot_pair(x, y, ang);
}

void
rotate_coords_y(int32_t *x, int32_t *y, int32_t *z, int32_t ang)
{
  (void) x;
  rot_pair(y, z, ang);
}

void
rotate_coords_z(int32_t *x, int32_t *y, int32_t *z, int32_t ang)
{
  (void) y;
  rot_pair(x, z, ang);
}

void
rotate_2d_node(void *p)
{
  int32_t *n = (int32_t *) p, z = 0;
  rot_pair(&n[0], &n[1], n[3]);
  (void) z;
}

void
setup_camera_rotations(void)
{
  int32_t v[3][3] = {{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}}, i;
  for (i = 0; i < 3; i++) {
    rotate_coords_x(&v[i][0], &v[i][1], &v[i][2], camera_x_rotation);
    rotate_coords_y(&v[i][0], &v[i][1], &v[i][2], camera_y_rotation);
    rotate_coords_z(&v[i][0], &v[i][1], &v[i][2], camera_z_rotation);
    camera_rotations[i * 3] = v[i][0];
    camera_rotations[i * 3 + 1] = v[i][1];
    camera_rotations[i * 3 + 2] = v[i][2];
  }
}

/* Transform a point by the camera matrix (result scaled back by 12 bits) */
void
rotate_node_x_y_z_camera(int32_t *px, int32_t *py, int32_t *pz)
{
  int32_t x = *px, y = *py, z = *pz;
  const int32_t *r = camera_rotations;
  int32_t a = ADD(ADD(MULS(x, r[0]), MULS(y, r[3])), MULS(z, r[6]));
  int32_t b = ADD(ADD(MULS(x, r[1]), MULS(y, r[4])), MULS(z, r[7]));
  int32_t c = ADD(ADD(MULS(x, r[2]), MULS(y, r[5])), MULS(z, r[8]));
  *px = ASR(a, 12);
  *py = ASR(b, 12);
  *pz = ASR(c, 12);
}

void
rotate_node_from_c(void *p)
{
  rotate_node *n = (rotate_node *) p;
  int32_t x = n->x_pos >> 12, y = n->y_pos >> 12, z = n->z_pos >> 12;
  rotate_coords_z(&x, &y, &z, 1024 - (n->z_rot >> 10));
  rotate_coords_y(&x, &y, &z, n->y_rot >> 10);
  rotate_coords_x(&x, &y, &z, n->x_rot >> 10);
  n->x_pos = (int32_t) LSL((uint32_t) x, 12);
  n->y_pos = -(int32_t) LSL((uint32_t) y, 12);
  n->z_pos = (int32_t) LSL((uint32_t) z, 12);
}

void
oppo_rotate_node_from_c(void *p)
{
  rotate_node *n = (rotate_node *) p;
  int32_t x = n->x_pos >> 12, y = n->y_pos >> 12, z = n->z_pos >> 12;
  rotate_coords_x(&x, &y, &z, n->x_rot >> 10);
  rotate_coords_y(&x, &y, &z, n->y_rot >> 10);
  rotate_coords_z(&x, &y, &z, 1024 - (n->z_rot >> 10));
  n->x_pos = (int32_t) LSL((uint32_t) x, 12);
  n->y_pos = -(int32_t) LSL((uint32_t) y, 12);
  n->z_pos = (int32_t) LSL((uint32_t) z, 12);
}

void
rotate_land_node_from_c(void *p)
{
  rotate_node *n = (rotate_node *) p;
  int32_t x = n->x_pos, y = n->y_pos, z = n->z_pos;
  rotate_coords_x(&x, &y, &z, n->x_rot >> 10);
  rotate_coords_y(&x, &y, &z, n->y_rot >> 10);
  rotate_coords_z(&x, &y, &z, n->z_rot >> 10);
  n->x_pos = x;
  n->y_pos = y;
  n->z_pos = z;
}

/* Signed divide, truncating toward zero; x/0 gives +-0x7fffffff (as the
 * original) */
int32_t
divide(int32_t a, int32_t b)
{
  uint32_t ua = a < 0 ? 0u - (uint32_t) a : (uint32_t) a;
  uint32_t ub = b < 0 ? 0u - (uint32_t) b : (uint32_t) b;
  uint32_t q = ub ? ua / ub : 0x7fffffffu;
  return (((uint32_t) a ^ (uint32_t) b) & 0x80000000u) ? -(int32_t) q
                                                       : (int32_t) q;
}

/* Angle of the vector (x,y) as a 20-bit fraction of a circle (tangent table
 * lookup) */
int32_t
find_rotation(int32_t x, int32_t y)
{
  int32_t ax, ay, r3, r6;
  if (x == 0 && y == 0) {
    return 0;
  }
  ax = x < 0 ? -x : x;
  ay = y < 0 ? -y : y;
  if (ax > ay) {
    r3 = divide((int32_t) LSL((uint32_t) ay, 10), ax);
    r6 = tangent_table[r3];
    if (x < 0) {
      return (y < 0) ? r6 + 256 * 1024 : 256 * 1024 - r6;
    }
    return (y >= 0) ? r6 + 256 * 3 * 1024 : 256 * 3 * 1024 - r6;
  }
  r3 = divide((int32_t) LSL((uint32_t) ax, 10), ay);
  r6 = tangent_table[r3];
  if (y > 0) {
    int32_t r0 = (x < 0) ? r6 : -r6;
    if (r0 < 0) {
      r0 += 1024 * 1024;
    }
    return r0;
  }
  return (x >= 0) ? r6 + 512 * 1024 : 512 * 1024 - r6;
}

static int32_t
iabs(int32_t v)
{
  return v < 0 ? -v : v;
}

int32_t
find_2d_distance(int32_t x, int32_t y)
{
  int32_t ang = find_rotation(x, y) >> 10;
  int32_t c = iabs(cosine_table[ang]), s = iabs(cosine_table[ang + SINOFF]);
  return ASR(ADD(MULS(iabs(y), c), MULS(iabs(x), s)), 12);
}

void
target_finder(void *p)
{
  target_struct *t = (target_struct *) p;
  int32_t d[3], ang, xy, c, s;
  d[0] = (t->x_aim - t->x_pos) >> 14;
  d[1] = (t->y_pos - t->y_aim) >> 14;
  d[2] = (t->z_pos - t->z_aim) >> 14;
  ang = find_rotation(d[0], d[1]);
  t->x_rot = ang;
  c = iabs(cosine_table[ang >> 10]);
  s = iabs(cosine_table[(ang >> 10) + SINOFF]);
  xy = ASR(ADD(MULS(iabs(d[1]), c), MULS(iabs(d[0]), s)), 12);
  ang = find_rotation(d[2], xy);
  t->y_rot = ang;
  c = iabs(cosine_table[ang >> 10]);
  s = iabs(cosine_table[(ang >> 10) + SINOFF]);
  t->distance =
      (int32_t) LSL((uint32_t) ADD(MULS(c, xy), MULS(s, iabs(d[2]))), 2);
}

void
mc_smoke_mover(void *p)
{
  smoke_stack *s = (smoke_stack *) p;
  s->x_vel -= s->x_vel >> 6;
  s->y_vel -= s->y_vel >> 6;
  s->z_vel -= s->z_vel >> 6;
  s->x_pos += s->x_vel;
  s->y_pos += s->y_vel;
  s->z_pos += s->z_vel;
  s->counter -= 1;
}

/* Scan a 32x32 window of the 128x128 poly map from (x,y) (wrapping) and record
   the grid references (x + y*128) of non-empty cells.  'limit_lt' selects:
   cells <= 8 (variant 1) or cells < 248 (variant 2).  Results are terminated
   with -1. */
static void
scan_map_n(int32_t x, int32_t y, intptr_t *found, int variant2, int cells)
{
  const uint8_t *map = (const uint8_t *) poly_map;
  int row, col, b;
  int32_t x_start =
      x & ~3; /* quad-align x: the window starts here on every row */
  for (row = 0; row < cells; row++) {
    x = x_start; /* the assembler steps x back to the row start (subs r0,r0,#28)
                  */
    for (col = 0; col < cells / 4; col++) {
      int32_t idx = x + (y << 7);
      for (b = 0; b < 4; b++) {
        int v = map[idx + b];
        if (v != 0 && (variant2 ? v < 248 : v <= 8)) {
          *found++ = idx + b;
        }
      }
      x = (x + 4) & 127;
    }
    y = (y + 1) & 127;
  }
  *found = -1;
}

static void
scan_map(int32_t x, int32_t y, intptr_t *found, int variant2)
{
  scan_map_n(x, y, found, variant2, 32); /* the game's 32 x 32 cell window */
}

void
scan_poly_map(int32_t x, int32_t y, void *found)
{
  scan_map(x, y, (intptr_t *) found, 0);
}

void
scan_poly_map_2(int32_t x, int32_t y, void *found)
{
  scan_map(x, y, (intptr_t *) found, 1);
}

/* scan_poly_map_2 over a window of 'cells' x 'cells' (a multiple of 4, at most
   64): found needs room for cells * cells + 1 entries */
void
scan_poly_map_window(int32_t x, int32_t y, void *found, int32_t cells)
{
  scan_map_n(x, y, (intptr_t *) found, 1, cells);
}
