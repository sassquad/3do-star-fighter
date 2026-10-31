/* Plot_Land.c - C translation of Plot_Land.s
 *
 * 1. fast_rotation():  rotates the landscape grid nodes into camera space at up
 * to four resolutions and projects them to the screen.
 * 2. machine_code_land_plot() / machine_code_flat_land_plot():
 *                      paint the landscape in concentric rings from far to near
 * and, between rings, plot the z-sorted objects (ships, smoke, lasers, bits,
 * static objects).
 *
 * rotated_coords [node][3] and screen_coords [node][2] are indexed by node
 * number: 0 ..1088  33x33 near grid      1089..1377  17x17 mid grid 1378..1666
 * 17x17 far grid 1667 + 289*k  : flat rings k = 0..4 Where a resolution is not
 * needed its slots are simply not written (as in the original).
 */
#include "Plot_Land.h"
#include "Plot_Graphic.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"
#include "SF_ARMLink.h"
#include "Setup_Tables.h"
#include "SF_Access.h"
#include "arm_ops.h"

#define NODES_NEAR (33 * 33)
#define NODES_RING (17 * 17)
#define FLAT_BASE (NODES_NEAR + 2 * NODES_RING)

extern int32_t test_mode;
extern zsort_list plot_list;
extern void plot_ship_graphic(void *);
extern void plot_bit_graphic(void *);
extern void plot_static_from_grid(void *);

int32_t wave_counter, wave_counter2, wave_counter3;
int32_t land_sort_offset;

static int32_t land_resolution, land_x_pos, land_y_pos;
static int32_t land_x_pos_outer, land_y_pos_outer, land_x_pos_outer_2,
    land_y_pos_outer_2;
static int32_t flat_x[5], flat_y[5];
static int32_t space_mission_or_wot;
static int32_t plot_res_skip_middle, hill_shade_offset;
static int32_t land_size, land_size_flat, land_res_flat, flat_shade;
static int32_t plot_list_pointer, search_plot_to_here;
static int32_t rd_step[3],
    rd_row[3]; /* per-node and per-row rotation vectors */

#define HM(x, y) (((uint8_t *) height_map)[((x) & 255) + (((y) & 255) << 8)])

void
plot_land_constants(void *memptr)
{
  /* The tables are ordinary C globals here; only the space-mission flag is
   * passed (by value). */
  space_mission_or_wot = (int32_t) (intptr_t) ((void **) memptr)[9];
}

/* ------------------------------------------------------------------ */
/* Rotation of the grid nodes                                          */
/* ------------------------------------------------------------------ */
static void
put_node(int32_t *dst, const int32_t *pos, int32_t h, int sh)
{
  const int32_t *e = &quick_height_table[h & 255][0];
  dst[0] = pos[0] - (e[0] >> sh);
  dst[1] = pos[1] - (e[1] >> sh);
  dst[2] = pos[2] - (e[2] >> sh);
}

static int32_t
vert_fix(int32_t x, int32_t y)
{
  return (y & 4) ? (HM(x, y - 4) + HM(x, y + 4)) >> 1 : HM(x, y);
}

static int32_t
hoz_fix(int32_t x, int32_t y)
{
  return (HM(x - 4, y) + HM(x + 4, y)) >> 1;
}

enum { ROW_PLAIN, ROW_FIX, ROW_SKIPMID };

enum { GRID_MID, GRID_LOW, GRID_FLAT };

/* One 17x17 grid.  blk = {x, y, posx, posy, posz}.  xstep = map units per node
   (4 or 8). Rows are visited in the original order; in 'skip middle' rows the
   five middle nodes (6..10) are left unwritten. */
static void
grid17(int32_t *out, const int32_t *blk, int kind, int xstep, int skipmid)
{
  int32_t y0 = blk[1], x0 = blk[0], p0[3] = {blk[2], blk[3], blk[4]};
  int sh = (kind == GRID_MID) ? 6 : 7;
  int row, c;

  for (row = 0; row <= 16; row++) {
    int32_t rr = 16 - row,
            y = (y0 + row * xstep) & 255; /* rr = the original's y_loop_count */
    int mode;
    if (row == 0) {
      mode = (kind == GRID_MID) ? ROW_FIX : ROW_PLAIN;
    } else if (kind == GRID_MID && rr == 0) {
      mode = ROW_FIX;
    } else if (skipmid) {
      mode = ROW_PLAIN;
    } else if (rr >= 11 || rr <= 5) {
      mode = ROW_PLAIN;
    } else {
      mode = ROW_SKIPMID;
    }

    for (c = 0; c <= 16; c++) {
      int32_t x = (x0 + c * xstep) & 255, h, pos[3];
      int32_t *dst = &out[(row * 17 + c) * 3];
      if (mode == ROW_SKIPMID && c >= 6 && c <= 10) {
        continue;
      }
      pos[0] = p0[0] + (rd_step[0] << 2) * c;
      pos[1] = p0[1] + (rd_step[1] << 2) * c;
      pos[2] = p0[2] + (rd_step[2] << 2) * c;
      if (kind == GRID_FLAT) {
        dst[0] = pos[0];
        dst[1] = pos[1];
        dst[2] = pos[2];
        continue;
      }
      if (kind == GRID_MID) {
        if (mode == ROW_FIX && (x & 4)) {
          h = hoz_fix(x, y);
        } else if (c == 0 || c == 16) {
          h = vert_fix(x, y);
        } else {
          h = HM(x, y);
        }
      } else {
        h = HM(x, y);
      }
      put_node(dst, pos, h, sh);
    }
    p0[0] += rd_row[0] << 2;
    p0[1] += rd_row[1] << 2;
    p0[2] += rd_row[2] << 2;
  }
}

void
fast_rotation(void *rotdata, int32_t res)
{
  const int32_t *rd = (const int32_t *) rotdata;
  int32_t *out = &rotated_coords[0][0];
  int32_t *qht = &quick_height_table[0][0];
  int32_t v[3], a[3], b[3], e, i, k;
  static const int32_t node_offs[4] = {0, NODES_NEAR, NODES_NEAR + NODES_RING,
                                       NODES_NEAR + 2 * NODES_RING};
  static const int32_t counters[4] = {NODES_NEAR + 4 * NODES_RING,
                                      5 * NODES_RING, 5 * NODES_RING,
                                      5 * NODES_RING};

  land_resolution = res;
  /* quick height table: entry h = h * (rotated unit height vector); [17..]
   * land, [0..16] sea surface */
  v[0] = qht[0];
  v[1] = qht[1];
  v[2] = qht[2];
  a[0] = a[1] = a[2] = 0;
  e = 17;
  for (i = 0; i < 119; i++) {
    for (k = 0; k < 3; k++) {
      qht[e * 4 + k] = a[k];
      b[k] = a[k] + v[k];
    }
    e++;
    for (k = 0; k < 3; k++) {
      qht[e * 4 + k] = b[k];
      a[k] = b[k] + v[k];
    }
    e++;
  }
  for (k = 0; k < 3; k++) {
    qht[e * 4 + k] = a[k];
  }
  e = 0;
  for (k = 0; k < 3; k++) {
    a[k] = ~(v[k] << 2);
  }
  for (i = 0; i < 8; i++) {
    for (k = 0; k < 3; k++) {
      qht[e * 4 + k] = a[k];
      b[k] = a[k] + (v[k] >> 1);
    }
    e++;
    for (k = 0; k < 3; k++) {
      qht[e * 4 + k] = b[k];
      a[k] = b[k] + (v[k] >> 1);
    }
    e++;
  }
  for (k = 0; k < 3; k++) {
    qht[e * 4 + k] = a[k];
  }

  for (k = 0; k < 3; k++) {
    rd_step[k] = rd[5 + k];
    rd_row[k] = rd[8 + k];
  }
  land_x_pos = rd[0];
  land_y_pos = rd[1];

  if (res <= 0) { /* near grid, 33x33 */
    int32_t row, c, p0[3] = {rd[2], rd[3], rd[4]};
    for (row = 0; row <= 32; row++) {
      for (c = 0; c <= 32; c++) {
        int32_t pos[3] = {p0[0] + rd_step[0] * c, p0[1] + rd_step[1] * c,
                          p0[2] + rd_step[2] * c};
        put_node(out + (row * 33 + c) * 3, pos,
                 HM(land_x_pos + c, land_y_pos + row), 6);
      }
      p0[0] += rd_row[0];
      p0[1] += rd_row[1];
      p0[2] += rd_row[2];
    }
  }
  land_x_pos_outer = rd[11];
  land_y_pos_outer = rd[12];
  if (res <= 1) {
    grid17(out + NODES_NEAR * 3, rd + 11, GRID_MID, 4, res);
  }
  land_x_pos_outer_2 = rd[16];
  land_y_pos_outer_2 = rd[17];
  if (res <= 2) {
    grid17(out + (NODES_NEAR + NODES_RING) * 3, rd + 16, GRID_LOW, 8, res == 2);
  }
  for (k = 0; k < 5; k++) { /* flat rings */
    if (k == 2 && res <= 0) {
      break;
    }
    if (k == 3 && res <= 1) {
      break;
    }
    if (k == 4 && res <= 2) {
      break;
    }
    flat_x[k] = rd[21 + 5 * k];
    flat_y[k] = rd[22 + 5 * k];
    grid17(out + (FLAT_BASE + k * NODES_RING) * 3, rd + 21 + 5 * k, GRID_FLAT,
           4, (k == 0) ? (res == 3) : 0);
  }

  /* perspective: nodes in front of the camera are scaled; others keep
   * camera-space x,z */
  {
    const int32_t *src = &rotated_coords[0][0] + node_offs[res] * 3;
    int32_t *dst = &screen_coords[0][0] + node_offs[res] * 2;
    int32_t n =
        (counters[res] / 3 + 1) * 3; /* the original works in groups of three */
    for (i = 0; i < n; i++, src += 3, dst += 2) {
      int32_t x = src[0], d = src[1], z = src[2];
      if (d > 0) {
        int32_t idx = (d & ~63) >> 6, s;
        if (idx > 16383) {
          idx = 16383;
        }
        s = pex_table[idx];
        x = MULS(x, s) >> 16;
        z = MULS(z, s) >> 16;
      }
      dst[0] = x;
      dst[1] = z;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Plotting land sections                                              */
/* ------------------------------------------------------------------ */
static int32_t
iabs(int32_t v)
{
  return v < 0 ? -v : v;
}

#define RC(n) (&rotated_coords[(n)][0])
#define SC(n) (&screen_coords[(n)][0])

static void
update_waves(int32_t x, int32_t y)
{
  HM(x, y) =
      (uint8_t) ((cosine_table[(wave_counter + (y << 7) + (x << 6)) & 1023] >>
                  9) +
                 8);
}

static int
off_screen(const int32_t *v) /* v = x0,y0,x1,y1,x2,y2,x3,y3 */
{
  if (v[1] > 120 && v[3] > 120 && v[5] > 120 && v[7] > 120) {
    return 1;
  }
  if (v[1] < -120 && v[3] < -120 && v[5] < -120 && v[7] < -120) {
    return 1;
  }
  if (v[0] < -160 && v[2] < -160 && v[4] < -160 && v[6] < -160) {
    return 1;
  }
  if (v[0] > 160 && v[2] > 160 && v[4] > 160 && v[6] > 160) {
    return 1;
  }
  return 0;
}

static void
set_quad(const int32_t *v)
{
  cel_quad.x_pos0 = v[0];
  cel_quad.y_pos0 = v[1];
  cel_quad.x_pos1 = v[2];
  cel_quad.y_pos1 = v[3];
  cel_quad.x_pos2 = v[4];
  cel_quad.y_pos2 = v[5];
  cel_quad.x_pos3 = v[6];
  cel_quad.y_pos3 = v[7];
}

/* signed areas of the two triangles of the quad */
static int32_t
cross1(const int32_t *v)
{
  return MULS(v[2] - v[0], v[5] - v[3]) - MULS(v[4] - v[2], v[3] - v[1]);
}

static int32_t
cross2(const int32_t *v)
{
  return MULS(v[6] - v[4], v[1] - v[7]) - MULS(v[0] - v[6], v[7] - v[5]);
}

static void
load_quad(int32_t *v, int32_t n0, int32_t n1, int32_t n2, int32_t n3)
{
  const int32_t *s;
  s = SC(n0);
  v[0] = s[0];
  v[1] = s[1];
  s = SC(n1);
  v[2] = s[0];
  v[3] = s[1];
  s = SC(n2);
  v[4] = s[0];
  v[5] = s[1];
  s = SC(n3);
  v[6] = s[0];
  v[7] = s[1];
}

/* near grid cell (1 map unit) -> 16x16 texture cel */
static void
plot_land_section(int32_t node, int32_t x, int32_t y)
{
  const int32_t *rc = RC(node);
  int32_t d = rc[1], d2, h, idx, v[8], s6;
  if (d < -8192) {
    return;
  }
  d2 = d + 10240;
  if (d2 < iabs(rc[0])) {
    return;
  }
  if (d2 < iabs(rc[2])) {
    return;
  }
  if (!(d2 > 0)) {
    if (RC(node + 1)[1] < -8192) {
      return;
    }
    if (RC(node + 33)[1] < -8192) {
      return;
    }
    if (RC(node + 34)[1] < -8192) {
      return;
    }
  }
  x &= 255;
  y &= 255;
  idx = x + (y << 8);
  h = HM(x, y);
  if (h <= 16) {
    update_waves(x, y);
  }
  load_quad(v, node + 33, node + 34, node + 1, node);
  if (cross1(v) < 0 && cross2(v) < 0) {
    return; /* both triangles face away */
  }
  if (off_screen(v)) {
    return;
  }
  set_quad(v);
  s6 = (h - HM(x + 1, y)) + (h - HM(x, y + 1));
  if (h <= 16) {
    s6 -= s6 >> 1;
  }
  cel_quad.shade = (s6 >> 1) + hill_shade_offset;
  arm_addpolycel16(&cel_quad, ((uint8_t *) sprite_map)[idx]);
}

/* mid grid cell (4 map units) -> cached 16x16 cel built from the sprite map */
static void
plot_land_section_2(int32_t node, int32_t x, int32_t y)
{
  const int32_t *rc = RC(node);
  int32_t d = rc[1], d2, v[8], h;
  if (d < 8192 * 4) {
    return;
  }
  d2 = d + 10240 * 4;
  if (d2 < iabs(rc[0])) {
    return;
  }
  if (d2 < iabs(rc[2])) {
    return;
  }
  x &= 252;
  y &= 252;
  load_quad(v, node, node + 1, node + 18, node + 17);
  if (cross1(v) >= 0 && cross2(v) >= 0) {
    return; /* this grid uses the opposite sense */
  }
  if (off_screen(v)) {
    return;
  }
  set_quad(v);
  h = HM(x, y);
  cel_quad.shade =
      (((h - HM(x + 4, y)) + (h - HM(x, y + 4))) >> 3) + hill_shade_offset;
  arm_add4cel4(&cel_quad, sprite_map, x, y);
}

/* far grid cell (8 map units) -> cel cut from the 512 map */
static void
plot_land_section_3(int32_t node, int32_t x, int32_t y)
{
  const int32_t *rc = RC(node);
  int32_t d = rc[1], d2, v[8], h;
  if (d < 8192) {
    return;
  }
  d2 = d + 10240 * 4;
  if (d2 < iabs(rc[0])) {
    return;
  }
  if (d2 < iabs(rc[2])) {
    return;
  }
  x &= 252;
  y &= 252;
  load_quad(v, node, node + 1, node + 18, node + 17);
  if (cross1(v) >= 0 && cross2(v) >= 0) {
    return;
  }
  if (off_screen(v)) {
    return;
  }
  set_quad(v);
  h = HM(x, y);
  cel_quad.shade =
      (((h - HM(x + 8, y)) + (h - HM(x, y + 8))) >> 4) + hill_shade_offset;
  arm_addcelfrom512map(&cel_quad, x, y, land_size);
}

/* flat distant ring cell: fixed shade, cel from the pre-built maps */
static void
plot_land_section_flat(int32_t node, int32_t x, int32_t y)
{
  const int32_t *rc = RC(node);
  int32_t d = rc[1], d2, v[8];
  if (d < 8192) {
    return;
  }
  d2 = d + 10240 * 4;
  if (d2 < iabs(rc[0])) {
    return;
  }
  if (d2 < iabs(rc[2])) {
    return;
  }
  x &= 255;
  y &= 255;
  load_quad(v, node, node + 1, node + 18, node + 17);
  if (off_screen(v)) {
    return;
  }
  set_quad(v);
  cel_quad.shade = flat_shade;
  if (land_res_flat == 1) {
    arm_addcelfrom128map(&cel_quad, x, y, land_size_flat);
  } else if (land_res_flat < 1) {
    arm_addcelfrom32map(&cel_quad, x, y, land_size_flat);
  } else {
    arm_addcelfrom512map(&cel_quad, x, y, land_size_flat);
  }
}

/* ------------------------------------------------------------------ */
/* Object plotting between rings                                       */
/* ------------------------------------------------------------------ */
static void
plot_sorted_graphics(void)
{
  int32_t r11, r12 = plot_list_pointer;
  if (r12 < 0) {
    return;
  }
  r11 = search_plot_to_here + land_sort_offset;
  if (r11 < 0) {
    r11 = 0;
  }
  if (r11 > 127) {
    r11 = 127;
  }
  search_plot_to_here = r11;
  for (;;) {
    zsort_item *item = plot_list.offset[r12];
    plot_list_pointer = r12 - 1;
    if (item != NULL) {
      plot_list.offset[r12] = NULL;
      do {
        zsort_item *next = item->next_address;
        void *g = item->graphic_address;
        switch (item->ref) {
          case 0:
            plot_ship_graphic(g);
            break;
          case 1:
            plot_smoke(g);
            break;
          case 2:
            plot_laser(g);
            break;
          case 3:
            plot_bit_graphic(g);
            break;
          case 4:
            plot_static_from_grid(g);
            break;
        }
        item = next;
      } while (item != NULL);
    }
    r12 = plot_list_pointer;
    if (!(search_plot_to_here <= r12)) {
      return;
    }
  }
}

/* ------------------------------------------------------------------ */
/* Ring plotters                                                       */
/* ------------------------------------------------------------------ */
typedef void (*section_fn)(int32_t node, int32_t x, int32_t y);

/* Plot square rings of cells working inwards.  n = ring size, c0..c3 = node
   numbers of the ring's corners (top-left, top-right, bottom-left,
   bottom-right), W = grid width, sh = log2 map units/node. After each ring the
   objects up to that distance are plotted.  search_mode picks how n maps to a
   z-sort bucket (n>>1, n<<1, n<<2) and how the loop ends. */
static void
ring_plotter(section_fn fn, int W, int sh, int32_t x, int32_t y, int32_t c0,
             int32_t c1, int32_t c2, int32_t c3, int32_t n, int search_mode)
{
  for (;;) {
    int32_t r5, half = n >> 1, i, e = n << sh;
    fn(c0, x, y);
    fn(c1, x + e, y);
    fn(c2, x, y + e);
    fn(c3, x + e, y + e);
    for (i = 0, r5 = 1; i < half; i++, r5++) {
      int32_t r6 = r5 * W, s = r5 << sh;
      fn(c0 + r5, x + s, y);
      fn(c0 + r6, x, y + s);
      fn(c1 - r5, x + e - s, y);
      fn(c1 + r6, x + e, y + s);
      fn(c2 + r5, x + s, y + e);
      fn(c2 - r6, x, y + e - s);
      fn(c3 - r5, x + e - s, y + e);
      fn(c3 - r6, x + e, y + e - s);
    }
    search_plot_to_here = (search_mode == 0)
                              ? (n >> 1)
                              : (search_mode == 1 ? (n << 1) : (n << 2));
    plot_sorted_graphics();
    n -= 2;
    if (search_mode == 0) {
      if (n < 0) {
        return;
      }
      if (n == 17) {
        hill_shade_offset = n;
      }
    } else {
      if (n == 9) {
        hill_shade_offset = (search_mode == 1) ? 13 : 9;
      }
      if (plot_res_skip_middle != 0) {
        if (!(n >= 1)) {
          return;
        }
      } else if (n < 6) {
        return;
      }
    }
    c0 += W + 1;
    c1 += W - 1;
    c2 -= W - 1;
    c3 -= W + 1;
    x += 1 << sh;
    y += 1 << sh;
  }
}

static void
sorted_land_plotter(void)
{
  hill_shade_offset = 15;
  ring_plotter(plot_land_section, 33, 0, land_x_pos, land_y_pos, 0, 31, 1023,
               1054, 31, 0);
}

static void
mid_res_sorted_land_plotter(void)
{
  hill_shade_offset = 11;
  ring_plotter(plot_land_section_2, 17, 2, land_x_pos_outer, land_y_pos_outer,
               1089, 1104, 1344, 1359, 15, 1);
}

static void
low_res_sorted_land_plotter(void)
{
  land_size = 0;
  hill_shade_offset = 7;
  ring_plotter(plot_land_section_3, 17, 3, land_x_pos_outer_2,
               land_y_pos_outer_2, 1378, 1393, 1633, 1648, 15, 2);
}

/* 16x16 block of flat cells starting at node 'node'; step = map units per cell
 */
static void
flat_low_res_plotter(int32_t node, int32_t x, int32_t y, int32_t step)
{
  int32_t row, col;
  for (row = 15; row >= 0; row--) {
    int skip = (plot_res_skip_middle == 0 && row > 4 && row < 11);
    int32_t n = node + (15 - row) * 17, yy = (y + (15 - row) * step) & 255;
    for (col = 0; col <= 15; col++) {
      if (skip && col >= 5 && col <= 10) {
        continue;
      }
      plot_land_section_flat(n + col, (x + col * step) & 255, yy);
    }
  }
}

static void
flat_low_res_sorted_land_plotter(void)
{
  plot_res_skip_middle = (land_resolution == 3) ? 1 : 0;
  land_size_flat = 1;
  land_res_flat = 2;
  flat_shade = 5;
  flat_low_res_plotter(FLAT_BASE, flat_x[0], flat_y[0], 16);
  plot_res_skip_middle = 0;
  land_size_flat = 2;
  land_res_flat = 1;
  flat_shade = 3;
  flat_low_res_plotter(FLAT_BASE + NODES_RING, flat_x[1], flat_y[1], 32);
  if (land_resolution == 0) {
    return;
  }
  land_size_flat = 3;
  flat_shade = 2;
  flat_low_res_plotter(FLAT_BASE + 2 * NODES_RING, flat_x[2], flat_y[2], 64);
  if (land_resolution <= 1) {
    return;
  }
  land_size_flat = 4;
  flat_shade = 1;
  flat_low_res_plotter(FLAT_BASE + 3 * NODES_RING, flat_x[3], flat_y[3], 128);
  land_res_flat = 0;
  if (land_resolution <= 2) {
    return;
  }
  land_size_flat = 5;
  flat_shade = 0;
  flat_low_res_plotter(FLAT_BASE + 4 * NODES_RING, flat_x[4], flat_y[4], 256);
}

void
machine_code_flat_land_plot(void)
{
  if (space_mission_or_wot != 1) {
    flat_low_res_sorted_land_plotter();
  }
}

void
machine_code_land_plot(void)
{
  plot_list_pointer = 127;
  if (test_mode == 1) {
    space_mission_or_wot = 1;
  }
  search_plot_to_here = (space_mission_or_wot != 1) ? 64 : ~256;
  plot_sorted_graphics();
  plot_res_skip_middle = 0;
  if (space_mission_or_wot == 1) {
    return;
  }
  switch (land_resolution) {
    case 0:
      low_res_sorted_land_plotter();
      mid_res_sorted_land_plotter();
      sorted_land_plotter();
      break;
    case 1:
      low_res_sorted_land_plotter();
      plot_res_skip_middle = 1;
      mid_res_sorted_land_plotter();
      break;
    case 2:
      plot_res_skip_middle = 1;
      low_res_sorted_land_plotter();
      break;
    default:
      break;
  }
  search_plot_to_here = ~256;
  plot_sorted_graphics();
}
