/* Collision.c - C translation of Collision.s
 *
 * Ground height, point-v-landscape collision (using the poly map and per-object
 * collision boxes held in graphics_data) and big-ship box collision. Collision
 * box data (host words, as swapped at load): word 0 : number of boxes - 1 ;
 * words 1,2 : unused  ; then boxes of 7 words: [0] bytes:
 * type,colour,group,nowt   [1..3] min x,y,z   [4..6] max x,y,z
 */
#include "Collision.h"
#include "Graphic_Struct.h"
#include "Maths_Stuff.h"
#include "Plot_Graphic.h"
#include "Setup_Tables.h"
#include "SF_Access.h"
#include "arm_ops.h"

#define COLL_POINTER 248
#define COLL_UP_RIGHT 248
#define COLL_UP 249
#define COLL_UP_LEFT 250
#define COLL_LEFT 251
#define COLL_DOWN_LEFT 252
#define COLL_DOWN 253
#define COLL_DOWN_RIGHT 254
#define COLL_RIGHT 255

int32_t test_coll_count;
int32_t bonus_collision_ref;

static int32_t space_mission_or_wot;
static graphics_details *static_object_start, *ship_start;

void
setup_collision_constants(int32_t space_mission)
{
  space_mission_or_wot = space_mission;
  static_object_start =
      (graphics_details *) ((char *) graphics_data + graphics_data[0]);
  ship_start = (graphics_details *) ((char *) graphics_data + graphics_data[1]);
}

/* Bilinearly interpolated ground height at world (x,y): result is height << 21
 * scale */
static int32_t
find_height(int32_t x, int32_t y)
{
  const uint8_t *hm = (const uint8_t *) height_map;
  uint32_t gx = (uint32_t) x >> 24, gy = (uint32_t) y >> 24;
  uint32_t gx1 = (gx + 1) & 255, gy1 = (gy + 1) & 255;
  int32_t h00 = hm[gx + (gy << 8)] - 17, h10 = hm[gx1 + (gy << 8)] - 17;
  int32_t h11 = hm[gx1 + (gy1 << 8)] - 17, h01 = hm[gx + (gy1 << 8)] - 17;
  int32_t fx = (int32_t) (((uint32_t) x << 8) >> 22),
          fy = (int32_t) (((uint32_t) y << 8) >> 22), acc, r;
  if (h00 < 0) {
    h00 = 0;
  }
  if (h10 < 0) {
    h10 = 0;
  }
  if (h11 < 0) {
    h11 = 0;
  }
  if (h01 < 0) {
    h01 = 0;
  }
  acc = MULS(MULS(h01 - h00, fy), 1024 - fx);
  acc = ADD(acc, MULS(MULS(h11 - h10, fy), fx));
  acc = ADD(acc, MULS(MULS(h10 - h00, fx), 1024 - fy));
  acc = ADD(acc, MULS(MULS(h11 - h01, fx), fy));
  r = ADD((int32_t) ((uint32_t) acc << 1), (int32_t) ((uint32_t) h00 << 21));
  return r < 0 ? 0 : r;
}

int32_t
find_ground_height(int32_t x, int32_t y)
{
  return find_height(x, y);
}

/* returns type+1 of the box hit (and sets bonus_collision_ref), or 0 */
static int32_t
check_collision_boxes(int32_t x, int32_t y, int32_t z, const int32_t *data)
{
  int32_t n = data[0];
  const int32_t *b = data + 3;
  do {
    int32_t minx = b[1], miny = b[2], minz = b[3], maxx = b[4], maxy = b[5],
            maxz = b[6];
    int pass;
    /* cmp x,min; cmpgt y; cmpgt z; blt fail : the first compare that is not
       'greater' decides; 'equal' counts as a pass */
    if (x > minx) {
      if (y > miny) {
        pass = !(z < minz);
      } else {
        pass = (y == miny);
      }
    } else {
      pass = (x == minx);
    }
    if (pass && x < maxx && y < maxy && z < maxz) {
      bonus_collision_ref =
          (int32_t) ((uint32_t) b[0] >> 24); /* first byte = type */
      return bonus_collision_ref + 1;
    }
    b += 7;
  } while (--n >= 0);
  bonus_collision_ref = 0;
  return 0;
}

int32_t
check_collision(int32_t x, int32_t y, int32_t z)
{
  const uint8_t *pm = (const uint8_t *) poly_map;
  int32_t r3, r4, r5, obj;

  if (z > (1 << 29)) {
    return 0;
  }
  r3 = (int32_t) (((uint32_t) x + (16384u * 1024u)) >> 25);
  r4 = (int32_t) (((uint32_t) y + (16384u * 1024u)) >> 25);
  r5 = r3 + (r4 << 7);
  obj = pm[r5];
  while (obj >= COLL_POINTER) { /* follow pointer cells to the real object */
    switch (obj) {
      case COLL_UP_RIGHT:
        r3++;
        r4++;
        break;
      case COLL_UP:
        r4++;
        break;
      case COLL_UP_LEFT:
        r3--;
        r4++;
        break;
      case COLL_LEFT:
        r3--;
        break;
      case COLL_DOWN_LEFT:
        r3--;
        r4--;
        break;
      case COLL_DOWN:
        r4--;
        break;
      case COLL_DOWN_RIGHT:
        r3++;
        r4--;
        break;
      default:
        r3++;
        break; /* COLL_RIGHT */
    }
    r3 &= 127;
    r4 &= 127;
    r5 = r3 + (r4 << 7);
    obj = pm[r5];
  }
  if (obj < 64 && obj != 0) {
    int32_t hidx = (r3 << 1) + (r4 << 9),
            h = ((const uint8_t *) height_map)[hidx] - 17, res;
    const int32_t *boxes =
        (const int32_t *) GPTR(static_object_start[obj].collision_adr);
    h = h < 0 ? 0 : (int32_t) ((uint32_t) h << 21);
    res = check_collision_boxes(SUB(x, (int32_t) ((uint32_t) r3 << 25)),
                                SUB(y, (int32_t) ((uint32_t) r4 << 25)),
                                SUB(z, h), boxes);
    if (res != 0) {
      return r5;
    }
  }
  { /* hills */
    int32_t h = find_height(x, y), r0;
    r0 = (h > z) ? ~h : 0;
    if (space_mission_or_wot == 1) {
      r0 = 0;
    }
    return r0;
  }
}

int32_t
big_ship_collision_check(ship_stack *a, ship_stack *b)
{
  rotate_node node;
  const int32_t *boxes;
  node.x_pos = a->x_pos - b->x_pos;
  node.y_pos = a->y_pos - b->y_pos;
  node.z_pos = b->z_pos - a->z_pos;
  node.x_rot = a->x_rot;
  node.y_rot = 1024 * 1024 - a->y_rot;
  node.z_rot = a->z_rot;
  oppo_rotate_node_from_c(&node);
  boxes = (const int32_t *) GPTR(ship_start[a->type].collision_adr);
  return check_collision_boxes(node.x_pos, node.y_pos, node.z_pos, boxes);
}
