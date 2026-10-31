/* Laser_Plot.c - C translation of Laser_Plot.s : draws a laser bolt as a thin
 * quad */
#include "Laser_Control.h"
#include "Laser_Struct.h"
#include "Plot_Graphic.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"
#include "Setup_Tables.h"
#include "Maths_Stuff.h"
#include "arm_ops.h"

#define POLY_CLIP_DIST 1280
#define CLIP_DISTANCE 2048
#define LASER_SHADE 25

extern int32_t camera_x_position, camera_y_position, camera_z_position;
void clip_3d_line(int32_t *a, int32_t *b);

static void
cam(int32_t wx, int32_t wy, int32_t wz, int32_t *p)
{
  p[0] = (wx - camera_x_position) >> 12;
  p[1] = (wy - camera_y_position) >> 12;
  p[2] = (camera_z_position - wz) >> 12;
  rotate_node_x_y_z_camera(&p[0], &p[1], &p[2]);
}

static int32_t
iabs(int32_t v)
{
  return v < 0 ? -v : v;
}

void
plot_laser(void *laserp)
{
  laser_stack *l = (laser_stack *) laserp;
  int32_t p1[3], p2[3], lim, f1, f2, s1x, s1y, s2x, s2y, dx, dy, ax, ay, r5, r6;
  int32_t k0, k1, k2, k3, x[4], y[4];

  cam(l->x_pos, l->y_pos, l->z_pos, p1);
  lim = p1[1] + CLIP_DISTANCE;
  if (iabs(p1[0]) > lim) {
    return;
  }
  if (iabs(p1[2]) > lim) {
    return;
  }
  cam(l->x_pos2, l->y_pos2, l->z_pos2, p2);

  if (p2[1] < POLY_CLIP_DIST) {
    if (p1[1] < POLY_CLIP_DIST) {
      return; /* both behind the camera */
    }
    clip_3d_line(p2, p1);
  } else if (p1[1] < POLY_CLIP_DIST) {
    clip_3d_line(p1, p2);
  }

  f2 = pex_table[(p2[1] & ~63) >> 6]; /* scale for end 2 (asm r11) */
  f1 = pex_table[(p1[1] & ~63) >> 6]; /* scale for end 1 (asm r12) */
  s2x = MULS(p2[0], f2) >> 16;
  s2y = MULS(p2[2], f2) >> 16;
  s1x = MULS(p1[0], f1) >> 16;
  s1y = MULS(p1[2], f1) >> 16;
  dx = s2x - s1x;
  dy = s2y - s1y;
  ax = iabs(dx);
  ay = iabs(dy);
  r5 = ~127;
  r6 = 128;
  if (ax > (ay << 1)) {
    r5 = 0;
  }
  if (ay > (ax << 1)) {
    r6 = 0;
  }
  if (dx < 0) {
    r6 = -r6;
  }
  if (dy < 0) {
    r5 = -r5;
  }
  k0 = MULS(r5, f2) >> 17;
  k1 = MULS(r6, f2) >> 17;
  k2 = MULS(r5, f1) >> 17;
  k3 = MULS(r6, f1) >> 17;
  if (k0 == 0) {
    k0 = 1;
  }
  if (k1 == 0) {
    k1 = 1;
  }
  if (k2 == 0) {
    k2 = 1;
  }
  if (k3 == 0) {
    k3 = 1;
  }
  x[0] = s2x + k0;
  y[0] = s2y + k1;
  x[1] = s1x + k2;
  y[1] = s1y + k3;
  x[2] = s1x - k2;
  y[2] = s1y - k3;
  x[3] = s2x - k0;
  y[3] = s2y - k1;
  if (y[0] > 120 && y[1] > 120 && y[2] > 120 && y[3] > 120) {
    return;
  }
  if (y[0] < -120 && y[1] < -120 && y[2] < -120 && y[3] < -120) {
    return;
  }
  if (x[0] < -160 && x[1] < -160 && x[2] < -160 && x[3] < -160) {
    return;
  }
  if (x[0] > 160 && x[1] > 160 && x[2] > 160 && x[3] > 160) {
    return;
  }
  cel_quad.x_pos0 = x[0];
  cel_quad.y_pos0 = y[0];
  cel_quad.x_pos1 = x[1];
  cel_quad.y_pos1 = y[1];
  cel_quad.x_pos2 = x[2];
  cel_quad.y_pos2 = y[2];
  cel_quad.x_pos3 = x[3];
  cel_quad.y_pos3 = y[3];
  cel_quad.shade = LASER_SHADE;
  arm_addpolycel32(&cel_quad, 159 - (l->type & 7));
}
