/* Plot_Graphic.c - C translation of Plot_Graphic.s
 *
 * Camera / rotation set-up, star fields, planets, distant ships, sky rotation,
 * clip_3d_line, and the 3D object plotter: rotate_graphic (walks a graphic's
 * node list), perspective, the polygon link walker with near-plane clipping,
 * the eight polygon-group style routines, ships, explosion bits, static objects
 * and the target lock-on boxes.
 *
 * Graphic data layout (host words, see sf_compat.h for the byte rule), relative
 * to its header: words 0..6  byte offsets of polygon "link" groups 0..6 (0 =
 * none);  word 7  offset of the back-face test vector list;  byte 32 style;
 * byte 33/34 node counts;  node bytes from +40. Link group:  word count-1, then
 * 5-word records: four node numbers, then bytes [colour/sprite,
 * shade-or-light-angle, light-angle2, pad].
 */
#include "Plot_Graphic.h"
#include "Ship_Struct.h"
#include "Bit_Struct.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"
#include "SF_Access.h"
#include "Setup_Tables.h"
#include "Maths_Stuff.h"
#include "Graphic_Struct.h"
#include "arm_ops.h"

#define PLANET_SIZE 48
#define POLY_CLIP_DIST 1280
#define WEAPON 6
#define PARACHUTE 4

/* ---- variables the C game code reads / writes ---- */
int32_t camera_x_position, camera_y_position, camera_z_position = (1 << 28);
int32_t camera_x_rotation, camera_y_rotation, camera_z_rotation;
int32_t camera_x_velocity, camera_y_velocity, camera_z_velocity;
int32_t silly_x, silly_y;
int32_t radar_rotation;
int32_t atg_selected, ata_selected;
intptr_t air_to_ground_scan_temp, air_to_ground_scan, air_to_air_scan_temp,
    air_to_air_scan;
int32_t air_to_ground_x, air_to_ground_y, air_to_ground_z, air_to_air_x,
    air_to_air_y, air_to_air_z;
int32_t collision_box_colour_adder;
int32_t are_we_in_space_or_wot;
int32_t planet_1_x_pos, planet_1_y_pos, planet_1_z_pos, planet_1_ref = 14;
int32_t planet_2_x_pos, planet_2_y_pos, planet_2_z_pos, planet_2_ref = 15;

/* ---- tables set up by machine_code_constants ---- */
graphics_details *static_graphics_data, *ships_data, *explosion_bits_data;
int32_t ship_graphic_rotation[9], static_graphic_rotation[9],
    static_graphic_radar_rotation[9];

void
machine_code_constants(void *memptr)
{
  (void) memptr; /* all tables are ordinary C globals in this port */
  static_graphics_data =
      (graphics_details *) ((char *) graphics_data + graphics_data[0]);
  ships_data = (graphics_details *) ((char *) graphics_data + graphics_data[1]);
  explosion_bits_data =
      (graphics_details *) ((char *) graphics_data + graphics_data[2]);
}

static int32_t
perspective(int32_t depth)
{
  return pex_fix(depth, pex_table[(depth & ~63) >> 6]);
}

/* Camera-space coordinate times a perspective factor -> screen coordinate.
   The product is the original one (so it wraps exactly where the ARM code
   did); at scales above 1 the result keeps cel_subpix fractional bits. */
static inline int32_t
project(int32_t v, int32_t sc)
{
  return MULS(v, sc) >> (16 - cel_subpix);
}

/* Transform a world position (relative to the camera) into camera space */
static void
to_camera_space(int32_t wx, int32_t wy, int32_t wz, int32_t *x, int32_t *y,
                int32_t *z)
{
  *x = (wx - camera_x_position) >> 12;
  *y = (wy - camera_y_position) >> 12;
  *z = (camera_z_position - wz) >> 12;
  rotate_node_x_y_z_camera(x, y, z);
}

/* ------------------------------------------------------------------ */
/* Rotation set-up (once per frame)                                    */
/* ------------------------------------------------------------------ */
static void
rotate_plus_radar(int32_t *x, int32_t *y, int32_t *z)
{
  int32_t a = camera_x_rotation - radar_rotation;
  if (a < 0) {
    a += 1024;
  }
  rotate_coords_x(x, y, z, a);
  rotate_coords_y(x, y, z, camera_y_rotation);
  rotate_coords_z(x, y, z, camera_z_rotation);
}

void
setup_rotations(void)
{
  static const int32_t unit[3][3] = {{1024, 0, 0}, {0, 1024, 0}, {0, 0, 1024}};
  int32_t i, v[3];
  setup_camera_rotations();
  for (i = 0; i < 3; i++) {
    v[0] = unit[i][0];
    v[1] = unit[i][1];
    v[2] = unit[i][2];
    if (i == 2) {
      v[0] = silly_x;
      v[1] = silly_y;
    }
    rotate_node_x_y_z_camera(&v[0], &v[1], &v[2]);
    static_graphic_rotation[i * 3] = v[0];
    static_graphic_rotation[i * 3 + 1] = v[1];
    static_graphic_rotation[i * 3 + 2] = v[2];
  }
  for (i = 0; i < 3; i++) {
    v[0] = unit[i][0];
    v[1] = unit[i][1];
    v[2] = unit[i][2];
    if (i == 2) {
      v[0] = silly_x;
      v[1] = silly_y;
    }
    rotate_plus_radar(&v[0], &v[1], &v[2]);
    static_graphic_radar_rotation[i * 3] = v[0];
    static_graphic_radar_rotation[i * 3 + 1] = v[1];
    static_graphic_radar_rotation[i * 3 + 2] = v[2];
  }
  radar_rotation -= 32;
  if (radar_rotation < 0) {
    radar_rotation += 1024;
  }
}

void
rotate_sky(void *out)
{
  int32_t *o = (int32_t *) out, x = 0, y = 16384, z = 0;
  rotate_coords_y(&x, &y, &z, camera_y_rotation);
  (void) z;
  o[1] = 240;
  o[0] = camera_z_position >> 17;
}

void
rotate_sky_node(void *p)
{
  int32_t *n = (int32_t *) p;
  rotate_coords_y(&n[0], &n[1], &n[2], camera_y_rotation);
  rotate_coords_z(&n[0], &n[1], &n[2], camera_z_rotation);
}

/* ------------------------------------------------------------------ */
/* Planets, far ships, stars                                           */
/* ------------------------------------------------------------------ */
static void
plot_this_planet(int32_t *planet)
{
  int32_t x = planet[0], y = planet[1], z = planet[2], sx, sy, sc;
  rotate_node_x_y_z_camera(&x, &y, &z);
  if (y < 8192) {
    return;
  }
  sc = perspective(y);
  sx = project(x, sc);
  sy = project(z, sc);
  if (sx > SUBPIX(160 + PLANET_SIZE / 2) || sx < -SUBPIX(160 + PLANET_SIZE / 2)) {
    return;
  }
  if (sy > SUBPIX(120 + PLANET_SIZE / 2) || sy < -SUBPIX(120 + PLANET_SIZE / 2)) {
    return;
  }
  cel_quad.x_pos0 = sx - SUBPIX(PLANET_SIZE / 2);
  cel_quad.y_pos0 = sy - SUBPIX(PLANET_SIZE / 2);
  cel_quad.shade = 12;
  arm_addgamecel(&cel_quad, planet[3], 1024, 1024);
}

void
plot_planets(void)
{
  int32_t p1[4] = {planet_1_x_pos, planet_1_y_pos, planet_1_z_pos,
                   planet_1_ref};
  int32_t p2[4] = {planet_2_x_pos, planet_2_y_pos, planet_2_z_pos,
                   planet_2_ref};
  plot_this_planet(p1);
  plot_this_planet(p2);
}

void
plot_this_distant_ship(ship_stack *ship)
{
  int32_t x, y, z, sx, sy, sc, sprite, t;
  to_camera_space(ship->x_pos, ship->y_pos, ship->z_pos, &x, &y, &z);
  if (y < 8192) {
    return;
  }
  sc = perspective(y);
  sx = project(x, sc);
  sy = project(z, sc);
  if (sx > SUBPIX(168) || sx < -SUBPIX(168) || sy > SUBPIX(128) ||
      sy < -SUBPIX(128)) {
    return;
  }
  cel_quad.x_pos0 = sx - SUBPIX(4);
  cel_quad.y_pos0 = sy - SUBPIX(4);
  cel_quad.shade = 12;
  t = ship->type >> 4;
  if (t == WEAPON) {
    sprite = 42;
  } else if (t == PARACHUTE) {
    sprite = 43;
  } else {
    sprite = (((uint8_t *) ship->special_data)[2] == 0) ? 41 : 40;
  }
  arm_addgamecel(&cel_quad, sprite, 1024, 1024);
}

typedef struct {
  int32_t x, y, w;
} star_plot;

static void
flush_stars(const star_plot *s, int n, int32_t star_shade, int space)
{
  int i;
  for (i = 0; i < n; i++) {
    int32_t sprite = (int32_t) s[i].w >> 24, shade;
    shade = space ? 12 : star_shade - ((s[i].w >> 16) & 255);
    cel_quad.shade = shade;
    cel_quad.x_pos0 = s[i].x;
    cel_quad.y_pos0 = s[i].y;
    arm_addgamecel(&cel_quad, sprite, 1024, 1024);
  }
}

void
plot_stars(void)
{
  int32_t sky1 = ((int32_t *) skyfile)[1],
          d = camera_z_position - (int32_t) ((uint32_t) sky1 << 17), shade, i;
  star_plot plot[128];
  int n = 0;
  if (d < 0) {
    return;
  }
  shade = d >> 24;
  for (i = 0; i < 128; i++) {
    int32_t *st = &star_coords[i][0], x = st[0], y = st[1], z = st[2], sx, sy;
    rotate_node_x_y_z_camera(&x, &y, &z);
    if (y < 1024) {
      continue;
    }
    sx = project(x, perspective(y));
    sy = project(z, perspective(y));
    if (sx > SUBPIX(180) || sx < -SUBPIX(180) || sy > SUBPIX(140) ||
        sy < -SUBPIX(140)) {
      continue;
    }
    plot[n].x = sx;
    plot[n].y = sy;
    plot[n].w = st[3];
    n++;
  }
  flush_stars(plot, n, shade, 0);
}

void
plot_space_stars(void)
{
  star_plot plot[128];
  int n = 0, i;
  for (i = 0; i < 128; i++) {
    int32_t *st = &star_coords[i][0], x, y, z, sx, sy;
    st[0] = (st[0] + (camera_x_velocity >> 14)) & 0xffff;
    st[1] = (st[1] + (camera_y_velocity >> 14)) & 0xffff;
    st[2] = (st[2] - (camera_z_velocity >> 14)) & 0xffff;
    x = st[0] - (1 << 15);
    y = st[1] - (1 << 15);
    z = st[2] - (1 << 15);
    rotate_node_x_y_z_camera(&x, &y, &z);
    if (y < 1024) {
      continue;
    }
    sx = project(x, perspective(y));
    sy = project(z, perspective(y));
    if (sx > SUBPIX(180) || sx < -SUBPIX(180) || sy > SUBPIX(140) ||
        sy < -SUBPIX(140)) {
      continue;
    }
    plot[n].x = sx;
    plot[n].y = sy;
    plot[n].w = st[3];
    n++;
  }
  flush_stars(plot, n, 0, 1);
}

/* Clip a 3D line to the near plane: a = (x,y,z) of one end, b of the other.
   On return a is the clipped end (the nearer one) and b is unchanged. */
void
clip_3d_line(int32_t *a, int32_t *b)
{
  int32_t t, ax, az;
  if (!(a[1] < b[1])) {
    int32_t tmp[3];
    memcpy(tmp, a, sizeof tmp);
    memcpy(a, b, sizeof tmp);
    memcpy(b, tmp, sizeof tmp);
  }
  t = divide((int32_t) LSL((uint32_t) (b[1] - POLY_CLIP_DIST), 10),
             b[1] - a[1]);
  ax = b[0] - (MULS(b[0] - a[0], t) >> 10);
  az = b[2] - (MULS(b[2] - a[2], t) >> 10);
  a[0] = ax;
  a[1] = POLY_CLIP_DIST;
  a[2] = az;
}

/* ================================================================== */
/* 3D object plotter                                                   */
/* ================================================================== */
#define CAR 7
#define BONUS 3
#define WEAPON 6
#define FLUFFY_CLOUD 58
#define cyan 40
#define green 8

extern void clip_3d_line(int32_t *a, int32_t *b);

static const uint32_t *graphic_start; /* graphic_start_adr */
static int32_t plot_graphic_pos[3];   /* object position in camera space */
static int32_t plot_graphic_y_rot, plot_graphic_z_rot;
static int32_t shade_style, plot_group_mask, plot_bit_palette;
static int32_t i_am_a_ship, i_am_a_object,
    yes_i_am_a_atg_lock_on_a_truck_or_tank;
static const int32_t *ship_to_plot; /* -> x_pos of the object being plotted */
static ship_stack *ship_to_plot_ship;
static int32_t static_object_details[4], static_object_grid_ref;
static int32_t high_res_pex;

#define GWORDS(off) ((const uint32_t *) ((const char *) graphic_start + (off)))

/* rotate a vector by an object's rotation then the camera (asm
 * rotate_node_by_ship) */
static void
rotate_by_object(int32_t *x, int32_t *y, int32_t *z, const int32_t *obj)
{
  int32_t a;
  rotate_coords_z(x, y, z, obj[5] >> 10);
  rotate_coords_y(x, y, z, obj[4] >> 10);
  a = camera_x_rotation - (obj[3] >> 10);
  if (a < 0) {
    a += 1024;
  }
  rotate_coords_x(x, y, z, a);
  rotate_coords_y(x, y, z, camera_y_rotation);
  rotate_coords_z(x, y, z, camera_z_rotation);
}

static void
store_vec(int32_t *dst, int32_t x, int32_t y, int32_t z)
{
  dst[0] = x;
  dst[1] = y;
  dst[2] = z;
}

static void
setup_ship_rotations(const int32_t *obj)
{
  int32_t v[3], i;
  static const int32_t u[2][3] = {{512, 0, 0}, {0, 512, 0}};
  for (i = 0; i < 3; i++) {
    if (i < 2) {
      v[0] = u[i][0];
      v[1] = u[i][1];
      v[2] = u[i][2];
    } else {
      v[0] = silly_y;
      v[1] = silly_x;
      v[2] = 512;
    }
    rotate_by_object(&v[0], &v[1], &v[2], obj);
    store_vec(&ship_graphic_rotation[i * 3], v[0], v[1], v[2]);
  }
}

static void
setup_static_graphic_ship_rotations(const int32_t *obj)
{
  int32_t v[3], i;
  for (i = 0; i < 3; i++) {
    v[0] = (i == 0) ? 1024 : 0;
    v[1] = (i == 1) ? 1024 : 0;
    v[2] = (i == 2) ? 1024 : 0;
    rotate_by_object(&v[0], &v[1], &v[2], obj);
    store_vec(&ship_graphic_rotation[i * 3], v[0], v[1], v[2]);
  }
}

static void
do_perspective(int32_t count_minus1, int high)
{
  int32_t i;
  for (i = 0; i <= count_minus1; i++) {
    int32_t x = graphic_rotated_coords[i][0], d = graphic_rotated_coords[i][1],
            z = graphic_rotated_coords[i][2];
    if (d > 0) {
      int32_t idx, sc;
      if (high) {
        idx = (d & ~31) >> 5;
        if (idx > 2047) {
          idx = 2047;
        }
        sc = pex_table_near[idx];
      } else {
        idx = (d & ~63) >> 6;
        if (idx > 16383) {
          idx = 16383;
        }
        sc = pex_table[idx];
      }
      sc = pex_fix(d, sc);
      x = project(x, sc);
      z = project(z, sc);
    } else { /* behind the camera: camera-space x, z stand in, as before */
      x = SUBPIX(x);
      z = SUBPIX(z);
    }
    graphic_screen_coords[i][0] = x;
    graphic_screen_coords[i][1] = z;
  }
}

/* Walk the graphic's node list: each node is three movement bytes (along the
   rotated x, y, z axes) relative to the previous node.  byte: 0 none; bit7
   negative; bit6 shift right (else left); low 5 bits shift. */
static void
rotate_graphic(const int32_t *m, const int32_t *pos)
{
  int32_t px = pos[0], py = pos[1], pz = pos[2], a, b, nodes_a, radar,
          k, node = 0;
  const int32_t *mat = m;
  int32_t counter, offs = 40;

  high_res_pex = (py < 4096);
  nodes_a = SF_BYTE(graphic_start, 33);
  b = SF_BYTE(graphic_start, 34) - 1;
  if (b < 0) {
    b = nodes_a;
  }
  radar = nodes_a - b;
  counter = nodes_a - radar;
  a = counter;
  for (;;) {
    for (k = 0; k < 3; k++) {
      int32_t cmd = SF_BYTE(graphic_start, offs++), n;
      const int32_t *v = mat + k * 3;
      if (cmd == 0) {
        continue;
      }
      n = cmd & 31;
      if (cmd & 128) {
        if (cmd & 64) {
          px = SUB(px, ASR(v[0], n));
          py = SUB(py, ASR(v[1], n));
          pz = SUB(pz, ASR(v[2], n));
        } else {
          px = SUB(px, (int32_t) LSL(v[0], n));
          py = SUB(py, (int32_t) LSL(v[1], n));
          pz = SUB(pz, (int32_t) LSL(v[2], n));
        }
      } else {
        if (cmd & 64) {
          px = ADD(px, ASR(v[0], n));
          py = ADD(py, ASR(v[1], n));
          pz = ADD(pz, ASR(v[2], n));
        } else {
          px = ADD(px, (int32_t) LSL(v[0], n));
          py = ADD(py, (int32_t) LSL(v[1], n));
          pz = ADD(pz, (int32_t) LSL(v[2], n));
        }
      }
    }
    store_vec(&graphic_rotated_coords[node][0], px, py, pz);
    node++;
    if (--counter >= 0) {
      continue;
    }
    if (radar > 0) {
      counter = radar;
      radar = 0;
      mat = static_graphic_radar_rotation;
      continue;
    }
    break;
  }
  (void) a;
  do_perspective(nodes_a, high_res_pex);
}

/* ------------------------------------------------------------------ */
/* Polygon link walker                                                 */
/* ------------------------------------------------------------------ */
/* Facing test on screen coordinates; only the sign is used.  With
   sub-pixel coordinates the products grow by 4^cel_subpix, so they are
   formed in 64 bits; at scale 1 the original 32-bit arithmetic is kept. */
static int32_t
cross3(const int32_t *s0, const int32_t *s1, const int32_t *s2)
{
  if (cel_subpix) {
    int64_t c = (int64_t) (s1[0] - s0[0]) * (s2[1] - s1[1]) -
                (int64_t) (s2[0] - s1[0]) * (s1[1] - s0[1]);
    return (c > 0) - (c < 0);
  }
  return MULS(s1[0] - s0[0], s2[1] - s1[1]) -
         MULS(s2[0] - s1[0], s1[1] - s0[1]);
}

static int32_t
iabs32(int32_t v)
{
  return v < 0 ? -v : v;
}

static void
add_poly(const int32_t *v /* x0,y0,..x3,y3 */, uint32_t rec4)
{
  int32_t sprite = (int32_t) (rec4 >> 24), r17 = (int32_t) ((rec4 >> 16) & 255),
          r18 = (int32_t) ((rec4 >> 8) & 255);
  int32_t shade;
  const int32_t xl = SUBPIX(160), yl = SUBPIX(120);
  if (v[1] > yl && v[3] > yl && v[5] > yl && v[7] > yl) {
    return;
  }
  if (v[1] < -yl && v[3] < -yl && v[5] < -yl && v[7] < -yl) {
    return;
  }
  if (v[0] < -xl && v[2] < -xl && v[4] < -xl && v[6] < -xl) {
    return;
  }
  if (v[0] > xl && v[2] > xl && v[4] > xl && v[6] > xl) {
    return;
  }
  cel_quad.x_pos0 = v[0];
  cel_quad.y_pos0 = v[1];
  cel_quad.x_pos1 = v[2];
  cel_quad.y_pos1 = v[3];
  cel_quad.x_pos2 = v[4];
  cel_quad.y_pos2 = v[5];
  cel_quad.x_pos3 = v[6];
  cel_quad.y_pos3 = v[7];
  if (shade_style < 1) {
    shade = ((r17 - 17) << 1) + 17;
    cel_quad.shade = shade;
    arm_addpolycel32(&cel_quad, sprite);
    return;
  }
  { /* lit by a fixed light direction: depends on the object's rotation and the
       polygon's two angle bytes */
    int32_t c2 = cosine_table[((plot_graphic_z_rot - r17) & 255) * 4];
    int32_t c4 = cosine_table[r18 * 4], c;
    if (c4 < 0) {
      c4 = -c4;
    }
    c2 = MULS(c2, c4) >> 12;
    c = cosine_table[plot_graphic_y_rot * 4];
    c2 = MULS(c2, c) >> 12;
    c2 >>= 8;
    shade = c2 + 17;
    if (sprite == 31) {
      shade = 4;
    }
    cel_quad.shade = shade;
    arm_addpolycel32(&cel_quad, sprite);
    if (shade_style == 2) {
      arm_setpolycel32palette(&cel_quad, plot_bit_palette);
    }
  }
}

static void
plot_fluffy(const uint32_t *rec, int32_t type)
{
  int32_t x = graphic_rotated_coords[rec[0]][0],
          y = graphic_rotated_coords[rec[0]][1],
          z = graphic_rotated_coords[rec[0]][2];
  int32_t sc, off, sz;
  if (y < 8192) {
    return;
  }
  sc = pex_fix(y, pex_table[(y & ~63) >> 6]);
  sz = sc + (sc >> 1);
  if (type >= 252) {
    off = 3 * 512;
    sz = (int32_t) LSL((uint32_t) sz, 2);
  } else {
    off = 3 * 1024;
    sz = (int32_t) LSL((uint32_t) sz, 3);
  }
  x -= off;
  z -= off;
  cel_quad.x_pos0 = project(x, sc);
  cel_quad.y_pos0 = project(z, sc);
  cel_quad.shade = (type < 252) ? 4 : 17;
  arm_addgamecel(&cel_quad, type - (248 - FLUFFY_CLOUD), sz, sz);
}

/* clip a polygon that has vertices closer than the near plane, then project it
 * with the high-res table */
static void
clip_this_polygon(const uint32_t *rec, int32_t *v_out_ok, int32_t *screen_out)
{
  int32_t c[4][3], i, s[4][2];
  for (i = 0; i < 4; i++) {
    store_vec(c[i], graphic_rotated_coords[rec[i]][0],
              graphic_rotated_coords[rec[i]][1],
              graphic_rotated_coords[rec[i]][2]);
  }
  if (!(c[0][1] > POLY_CLIP_DIST)) {
    clip_3d_line(c[0], (c[1][1] > c[3][1]) ? c[1] : c[3]);
  }
  if (!(c[1][1] > POLY_CLIP_DIST)) {
    clip_3d_line(c[1], (c[0][1] <= c[2][1]) ? c[2] : c[0]);
  }
  if (!(c[2][1] > POLY_CLIP_DIST)) {
    clip_3d_line(c[2], (c[1][1] > c[3][1]) ? c[1] : c[3]);
  }
  if (!(c[3][1] > POLY_CLIP_DIST)) {
    clip_3d_line(c[3], (c[2][1] > c[0][1]) ? c[2] : c[0]);
  }
  for (i = 0; i < 4; i++) {
    int32_t idx = (c[i][1] & ~31) >> 5, sc;
    if (idx > 2047) {
      idx = 2047;
    }
    if (idx < 0) {
      idx = 0;
    }
    sc = pex_fix(c[i][1], pex_table_near[idx]);
    s[i][0] = project(c[i][0], sc);
    s[i][1] = project(c[i][2], sc);
  }
  for (i = 0; i < 4; i++) {
    screen_out[i * 2] = s[i][0];
    screen_out[i * 2 + 1] = s[i][1];
  }
  *v_out_ok = (cross3(s[0], s[1], s[2]) >= 0);
}

static void
plot_graphic_links(const uint32_t *p)
{
  int32_t count = (int32_t) p[0], i;
  const uint32_t *rec = p + 1;
  for (i = 0; i <= count; i++, rec += 5) {
    int32_t type = (int32_t) (rec[4] >> 24);
    const int32_t *d[4];
    int32_t v[8], k, behind = 0, any_front = 0, ok;
    if (type >= 248) {
      plot_fluffy(rec, type);
      continue;
    }
    for (k = 0; k < 4; k++) {
      d[k] = &graphic_rotated_coords[rec[k]][0];
      if (d[k][1] < POLY_CLIP_DIST) {
        behind = 1;
      }
    }
    if (behind) {
      int32_t sv[8];
      for (k = 0; k < 4; k++) {
        if (d[k][1] > POLY_CLIP_DIST) {
          any_front = 1;
        }
      }
      if (!any_front) {
        continue;
      }
      clip_this_polygon(rec, &ok, sv);
      if (!ok) {
        continue;
      }
      add_poly(sv, rec[4]);
      continue;
    }
    for (k = 0; k < 4; k++) {
      v[k * 2] = graphic_screen_coords[rec[k]][0];
      v[k * 2 + 1] = graphic_screen_coords[rec[k]][1];
    }
    if (cross3(&v[0], &v[2], &v[4]) < 0) {
      continue; /* facing away */
    }
    add_poly(v, rec[4]);
  }
}

/* group n: skipped if its bit is set in plot_group_mask or its offset is zero
 */
static void
plot_poly_group(int n)
{
  uint32_t off;
  if (plot_group_mask & (1 << n)) {
    return;
  }
  off = graphic_start[n];
  if (off == 0) {
    return;
  }
  plot_graphic_links(GWORDS(off));
}

/* bit n of the result is set where the n-th test polygon faces away from the
 * camera */
static int32_t
test_these_vectors(void)
{
  const uint32_t *p = GWORDS(graphic_start[7]);
  int32_t count = (int32_t) p[0], mask = 0, bit = 1, i;
  p++;
  for (i = 0; i <= count; i++, p += 3, bit <<= 1) {
    const int32_t *a = graphic_rotated_coords[p[0]],
                  *b = graphic_rotated_coords[p[1]],
                  *c = graphic_rotated_coords[p[2]];
    if (a[1] < 1280 || b[1] < 1280 || c[1] < 1280) {
      continue;
    }
    if (cross3(graphic_screen_coords[p[0]], graphic_screen_coords[p[1]],
               graphic_screen_coords[p[2]]) < 0) {
      mask |= bit;
    }
  }
  return mask;
}

#define G(n) plot_poly_group(n)

static void
plot_graphic_style(int style)
{
  int32_t m = 0;
  if (style != 2) {
    m = test_these_vectors();
  }
  switch (style) {
    case 0:
      if (m & 1) {
        G(1);
      }
      if (m & 2) {
        G(2);
      }
      G(0);
      if (!(m & 1)) {
        G(1);
      }
      if (!(m & 2)) {
        G(2);
      }
      break;
    case 1:
      if (m & 8) {
        G(4);
      }
      if (m & 2) {
        G(2);
      }
      if (m & 16) {
        G(5);
      }
      if (m & 4) {
        G(3);
      }
      if (m & 1) {
        G(1);
      }
      G(0);
      if (!(m & 1)) {
        G(1);
      }
      if (!(m & 4)) {
        G(3);
      }
      if (!(m & 2)) {
        G(2);
      }
      if (!(m & 8)) {
        G(4);
      }
      if (!(m & 16)) {
        G(5);
      }
      break;
    case 2:
      G(0);
      G(1);
      break;
    case 3:
      if (m & 1) {
        G(1);
      }
      G(0);
      if (!(m & 1)) {
        G(1);
      }
      break;
    case 4:
      if (m & 8) {
        G(4);
      }
      if (m & 1) {
        G(1);
      }
      if (m & 2) {
        G(2);
      }
      if (m & 4) {
        G(3);
      }
      G(0);
      if (!(m & 1)) {
        G(1);
      }
      if (!(m & 2)) {
        G(2);
      }
      if (!(m & 4)) {
        G(3);
      }
      if (!(m & 8)) {
        G(4);
      }
      break;
    case 5:
      G(0);
      if (m & 4) {
        G(5);
      }
      if (m & 8) {
        G(6);
      }
      G(4);
      if (!(m & 4)) {
        G(5);
      }
      if (!(m & 8)) {
        G(6);
      }
      if (!(m & 1)) {
        G(1);
      }
      if (!(m & 2)) {
        G(2);
      }
      G(3);
      break;
    case 6:
      if (!(m & 1)) {
        G(0);
      }
      if (!(m & 2)) {
        G(1);
      }
      if (m & 4) {
        G(3);
      }
      G(2);
      if (m & 2) {
        G(1);
      }
      if (!(m & 4)) {
        G(3);
      }
      if (m & 1) {
        G(0);
      }
      break;
    case 7:
      if (m & 8) {
        G(4);
      }
      if (m & 4) {
        G(3);
      }
      if (m & 2) {
        G(2);
      }
      if (m & 1) {
        G(1);
      }
      G(0);
      if (!(m & 1)) {
        G(1);
      }
      if (!(m & 2)) {
        G(2);
      }
      if (!(m & 4)) {
        G(3);
      }
      if (!(m & 8)) {
        G(4);
      }
      break;
  }
}

#undef G

static void
begin_graphic(const graphics_details *table, int32_t index,
              const int32_t *rotation, int dummy)
{
  (void) dummy;
  graphic_start = (const uint32_t *) GPTR(table[index].graphic_adr);
  rotate_graphic(rotation, plot_graphic_pos);
  plot_graphic_style((int) SF_BYTE(graphic_start, 32));
}

/* ------------------------------------------------------------------ */
/* Ships, bits, static objects                                         */
/* ------------------------------------------------------------------ */
static void draw_collision_boxes(int type);

static void
plot_ship(ship_stack *ship)
{
  int32_t type = ship->type, clip, x, y, z, lim;
  const int32_t *obj = &ship->x_pos;
  ship_to_plot = obj;
  ship_to_plot_ship = ship;
  i_am_a_ship = (type < 255);
  clip = (type >= 255) ? (8 << 24) : ships_data[type].clip_size;
  plot_graphic_y_rot = ship->y_rot >> 12;
  plot_graphic_z_rot = (ship->z_rot >> 12) + 40;
  to_camera_space(ship->x_pos, ship->y_pos, ship->z_pos, &x, &y, &z);
  plot_graphic_pos[0] = x;
  plot_graphic_pos[1] = y;
  plot_graphic_pos[2] = z;
  lim = y + (clip >> 12);
  if (lim < 0) {
    return;
  }
  if (iabs32(x) > lim) {
    return;
  }
  if (iabs32(z) > lim) {
    return;
  }
  if (type <= 255) {
    setup_ship_rotations(obj);
    shade_style = 1;
    plot_group_mask = 0;
    begin_graphic(ships_data, type, ship_graphic_rotation, 0);
    return;
  }
  if (type >= 512) {
    setup_ship_rotations(obj);
  } else {
    setup_static_graphic_ship_rotations(obj);
  }
  shade_style = 1;
  /* explosion pieces (ship and building sections): groups to leave out.
     The mask lives in group_mask (byte 40); counter (byte 44) once held it,
     and reading counter - always 0 for a section - drew every piece as the
     whole object (see Ship_Struct.h) */
  plot_group_mask = ship->group_mask;
  begin_graphic((type >= 512) ? ships_data : static_graphics_data,
                (type >= 512) ? type - 512 : type - 256, ship_graphic_rotation,
                0);
}

int32_t plot_ship_sprite, plot_lockon_ok = 1; /* see Plot_Graphic.h */

void
plot_ship_graphic(void *p)
{
  ship_stack *ship = (ship_stack *) p;
  int32_t t, x, y, z;
  i_am_a_ship = 0;
  i_am_a_object = 0;
  /* sprite or 3D was decided by draw_land (the original read can_see, which
     draw_land had just set the same way; with OBJECT_DISTANCE in config.txt
     the drawing range and the game's visibility range can differ) */
  if (plot_ship_sprite) {
    plot_this_distant_ship(ship);
  } else {
    plot_ship(ship);
  }
  yes_i_am_a_atg_lock_on_a_truck_or_tank = 0;
  if (atg_selected != 0 && ship_to_plot_ship &&
      (ship_to_plot_ship->type >> 4) == CAR) {
    yes_i_am_a_atg_lock_on_a_truck_or_tank = 1;
  } else {
    if (ata_selected == 0) {
      return;
    }
    if (ship_to_plot_ship && (ship_to_plot_ship->type >> 4) == CAR) {
      return;
    }
  }
  x = iabs32(plot_graphic_pos[0]);
  y = plot_graphic_pos[1];
  z = iabs32(plot_graphic_pos[2]);
  if (plot_lockon_ok && !(x > air_to_air_x) && !(y < (x << 2)) &&
      !(y < (z << 2)) && ship_to_plot_ship) {
    t = ship_to_plot_ship->type;
    if (t < 256) {
      int32_t g = t >> 4;
      if (g != 0 && g != BONUS && g != WEAPON) {
        air_to_air_scan_temp = (intptr_t) ship_to_plot_ship;
        air_to_air_x = x;
        air_to_air_y = y;
        air_to_air_z = z;
      }
    }
  }
  if (ship_to_plot_ship != (ship_stack *) air_to_air_scan) {
    return;
  }
  draw_collision_boxes(1);
}

static void
plot_bit_obj(bit_stack *bit)
{
  int32_t x, y, z;
  const int32_t *obj = &bit->x_pos;
  ship_to_plot = obj;
  ship_to_plot_ship = NULL;
  plot_graphic_y_rot = bit->y_rot >> 12;
  plot_graphic_z_rot = (bit->z_rot >> 12) + 40;
  to_camera_space(bit->x_pos, bit->y_pos, bit->z_pos, &x, &y, &z);
  if (y < 0) {
    return;
  }
  if (iabs32(x) > y) {
    return;
  }
  if (iabs32(z) > y) {
    return;
  }
  plot_graphic_pos[0] = x;
  plot_graphic_pos[1] = y;
  plot_graphic_pos[2] = z;
  setup_ship_rotations(obj);
  plot_bit_palette = bit->colour1;
  shade_style = 2;
  plot_group_mask = 0;
  begin_graphic(explosion_bits_data, bit->type, ship_graphic_rotation, 0);
}

void
plot_bit_graphic(void *p)
{
  plot_bit_obj((bit_stack *) p);
}

static void
plot_static_graphic(int32_t x, int32_t y, int32_t z, int32_t type)
{
  plot_graphic_pos[0] = x;
  plot_graphic_pos[1] = y;
  plot_graphic_pos[2] = z;
  i_am_a_object = (type >= 60) ? 0 : 1;
  shade_style = 0;
  plot_group_mask = 0;
  begin_graphic(static_graphics_data, type, static_graphic_rotation, 0);
}

void
plot_static_from_grid(void *gridref)
{
  const uint8_t *pm = (const uint8_t *) poly_map,
                *hm = (const uint8_t *) height_map;
  int32_t ref = (int32_t) (intptr_t) gridref, gx = ref & 127, gy = ref >> 7,
          type = pm[ref];
  int32_t clip = static_graphics_data[type].clip_size, h, x, y, z, lim,
          details_x, details_y, details_z;
  int32_t wx, wy, wz;
  static_object_grid_ref = ref;
  i_am_a_ship = 0;
  i_am_a_object = 0;
  h = hm[(gx << 1) + (gy << 9)] - 17;
  if (h < 0) {
    h = 0;
  }
  if (type == 60) {
    h += 64;
  }
  h = (int32_t) LSL((uint32_t) h, 21);
  wx = (int32_t) LSL((uint32_t) gx, 25) - camera_x_position;
  wy = (int32_t) LSL((uint32_t) gy, 25) - camera_y_position;
  wz = camera_z_position - h;
  details_x = wx;
  details_y = wy;
  details_z = wz;
  static_object_details[0] = wx;
  static_object_details[1] = wy;
  static_object_details[2] = wz;
  static_object_details[3] = type;
  x = wx >> 12;
  y = wy >> 12;
  z = wz >> 12;
  rotate_node_x_y_z_camera(&x, &y, &z);
  lim = y + (clip >> 12);
  if (lim < 0) {
    return;
  }
  if (iabs32(x) > lim) {
    return;
  }
  if (iabs32(z) > lim) {
    return;
  }
  if (!plot_lockon_ok) { /* beyond the game's own range: drawn only */
    plot_static_graphic(x, y, z, type);
    return;
  }
  if (are_we_in_space_or_wot != 0) {
    if (ata_selected == 0) {
      plot_static_graphic(x, y, z, type);
      return;
    }
  } else if (atg_selected == 0) {
    plot_static_graphic(x, y, z, type);
    return;
  }
  { /* air-to-ground lock-on candidate */
    int32_t ax = iabs32(x), az = iabs32(z);
    if (y < (ax << 2) || y < (az << 1) || ax > air_to_ground_x) {
      plot_static_graphic(x, y, z, type);
      return;
    }
    air_to_ground_x = ax;
    air_to_ground_scan_temp = ref;
    plot_static_graphic(x, y, z, type);
    if (ref == air_to_ground_scan) {
      draw_collision_boxes(0);
    }
  }
  (void) details_x;
  (void) details_y;
  (void) details_z;
}

/* ------------------------------------------------------------------ */
/* Lock-on boxes                                                       */
/* ------------------------------------------------------------------ */
static void
draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t colour,
          int32_t shader)
{
  int32_t dx, dy, ox = 0, oy = 0;
  const int32_t xl = SUBPIX(160), yl = SUBPIX(120);
  if (y0 > yl && y1 > yl) {
    return;
  }
  if (y0 < -yl && y1 < -yl) {
    return;
  }
  if (x0 < -xl && x1 < -xl) {
    return;
  }
  if (x0 > xl && x1 > xl) {
    return;
  }
  dx = iabs32(x0 - x1);
  dy = iabs32(y0 - y1);
  /* one logical pixel thick at every scale */
  if (dx > dy) {
    oy = SUBPIX(1);
  } else {
    ox = SUBPIX(1);
  }
  cel_quad.x_pos0 = x0;
  cel_quad.y_pos0 = y0;
  cel_quad.x_pos1 = x1;
  cel_quad.y_pos1 = y1;
  cel_quad.x_pos2 = x1 + ox;
  cel_quad.y_pos2 = y1 + oy;
  cel_quad.x_pos3 = x0 + ox;
  cel_quad.y_pos3 = y0 + oy;
  cel_quad.shade = shader;
  arm_addmonocel(&cel_quad, 0, colour, 2);
}

static void
draw_collision_boxes(int type)
{
  static const int32_t link[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0},
                                      {0, 6}, {1, 7}, {2, 4}, {3, 5},
                                      {4, 5}, {5, 6}, {6, 7}, {7, 4}};
  const graphics_details *table;
  int32_t idx, nboxes;
  const int32_t *box;
  int32_t saved_rot[9];
  if (type == 0) {
    if (i_am_a_object == 0) {
      return;
    }
    table = static_graphics_data;
    idx = static_object_details[3];
  } else {
    if (i_am_a_ship == 0 || ship_to_plot_ship == NULL) {
      return;
    }
    idx = ship_to_plot_ship->type;
    if (idx >= 255) {
      return;
    }
    table = ships_data;
  }
  box = (const int32_t *) GPTR(table[idx].collision_adr);
  nboxes = box[0];
  box += 4;
  memcpy(saved_rot, ship_graphic_rotation, sizeof saved_rot);
  for (; nboxes >= 0; nboxes--, box += 7) {
    int32_t x0 = box[0], y0 = box[1], z0 = box[2], x1 = box[3], y1 = box[4],
            z1 = box[5];
    int32_t P[8][3] = {{x0, y0, -z0}, {x1, y0, -z0}, {x1, y1, -z0},
                       {x0, y1, -z0}, {x1, y1, -z1}, {x0, y1, -z1},
                       {x0, y0, -z1}, {x1, y0, -z1}};
    int32_t k, colour;
    for (k = 0; k < 8; k++) {
      int32_t x = P[k][0] >> 12, y = P[k][1] >> 12, z = P[k][2] >> 12,
              sc = collision_box_colour_adder + 16;
      x = MULS(x, sc) >> 4;
      y = MULS(y, sc) >> 4;
      z = MULS(z, sc) >> 4;
      if (type == 0) {
        rotate_node_x_y_z_camera(&x, &y, &z);
      } else {
        rotate_by_object(&x, &y, &z, ship_to_plot);
      }
      store_vec(&graphic_rotated_coords[k][0], x + plot_graphic_pos[0],
                y + plot_graphic_pos[1], z + plot_graphic_pos[2]);
    }
    do_perspective(7, 0);
    colour = (type == 0) ? cyan : green;
    if (are_we_in_space_or_wot == 1) {
      colour = green;
    }
    if (yes_i_am_a_atg_lock_on_a_truck_or_tank) {
      colour = cyan;
    }
    for (k = 0; k < 12; k++) {
      int32_t a = link[k][0], b = link[k][1];
      if (graphic_rotated_coords[a][1] < 2048 ||
          graphic_rotated_coords[b][1] < 2048) {
        continue;
      }
      draw_line(graphic_screen_coords[a][0], graphic_screen_coords[a][1],
                graphic_screen_coords[b][0], graphic_screen_coords[b][1],
                colour, (collision_box_colour_adder >> 1) + 1);
    }
  }
  memcpy(ship_graphic_rotation, saved_rot, sizeof saved_rot);
}
