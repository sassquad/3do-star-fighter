/* Smoke_Plot.c - C translation of Smoke_Plot.s : smoke / explosion puffs as scaled sprites */
#include "Smoke_Control.h"
#include "Smoke_Struct.h"
#include "Plot_Graphic.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"
#include "Setup_Tables.h"
#include "Maths_Stuff.h"
#include "arm_ops.h"

#define BASE_THRUSTER_COLOUR 78
extern int32_t camera_x_position, camera_y_position, camera_z_position;

/* per smoke type: minimum depth before it is drawn */
static const int32_t smoke_clip_table [19] = {
	1024, 8192, 12 * 1024, 8192, 8192, 1024, 8192, 8192, 1024, 2048,
	8192, 8192, 6 * 1024, 4096, 1024, 4096, 4096, 8192, 8192 };

/* handler ids, indexed by smoke type (the jump table of the original) */
enum { H_SMALL_THRUSTER, H_EXPLOSION, H_MUSHROOM, H_MISSILE_THRUSTER, H_BIG_THRUSTER, H_BIG_DAMAGE, H_LASER_HIT,
       H_BLACK_EXPLOSION, H_SMALL_EXPLOSION, H_BIG_EXPLOSION, H_SMALL_DAMAGE, H_BIG_SECTION_THRUST, H_BIG_SECTION_SMOKE };
static const uint8_t smoke_jump_table [19] = {
	H_SMALL_THRUSTER, H_EXPLOSION, H_MUSHROOM, H_EXPLOSION, H_EXPLOSION, H_MISSILE_THRUSTER, H_BIG_THRUSTER,
	H_BIG_DAMAGE, H_MISSILE_THRUSTER, H_LASER_HIT, H_EXPLOSION, H_BLACK_EXPLOSION, H_SMALL_EXPLOSION,
	H_BIG_EXPLOSION, H_SMALL_DAMAGE, H_SMALL_EXPLOSION, H_SMALL_DAMAGE, H_BIG_SECTION_THRUST, H_BIG_SECTION_SMOKE };

static int32_t iabs (int32_t v) { return v < 0 ? -v : v; }

void plot_smoke (void *smokep)
{
	smoke_stack *s = (smoke_stack *) smokep;
	int32_t x = (s->x_pos - camera_x_position) >> 12, y = (s->y_pos - camera_y_position) >> 12, z = (camera_z_position - s->z_pos) >> 12;
	int32_t type = s->type, scale, r6 = 128, r7, colour, shade, c = s->counter, r4, sx, sy;

	rotate_node_x_y_z_camera (&x, &y, &z);
	if (type < 0 || type > 18) return;
	if (y < smoke_clip_table [type]) return;
	if (iabs (x) > y) return;
	if (iabs (z) > y) return;
	scale = pex_table [(y & ~63) >> 6];
	r7 = scale >> 1;
	colour = s->colour;
	shade = c;
	switch (smoke_jump_table [type]) {
	case H_LASER_HIT:      colour = s->colour + (7 - c); shade = 12; r6 <<= 5; r7 <<= 3; break;
	case H_SMALL_THRUSTER: shade = c + ((colour - BASE_THRUSTER_COLOUR) >> 1); break;
	case H_MISSILE_THRUSTER: shade = c + 4; break;
	case H_BIG_THRUSTER:   r4 = c * 2 + 8; r6 = MULS (r6, r4) >> 1; r7 = MULS (r7, r4) >> 1; break;
	case H_BIG_DAMAGE:     shade = 12; r4 = c * 2 + 8; r6 = MULS (r6, r4) >> 1; r7 = MULS (r7, r4) >> 1; break;
	case H_BIG_SECTION_SMOKE: shade = 12; r4 = c * 2 + 8; r6 = MULS (r6, r4); r7 = MULS (r7, r4); break;
	case H_SMALL_DAMAGE:   shade = 12; r4 = c * 2 + 8; r6 = MULS (r6, r4) >> 3; r7 = MULS (r7, r4) >> 3; break;
	case H_EXPLOSION:      r4 = c * 2 + 16; r6 = MULS (r6, r4) >> 1; r7 = MULS (r7, r4) >> 1; break;
	case H_MUSHROOM:       { int32_t a, b; r4 = c * 2 + 16; a = MULS (r6, r4); b = MULS (r7, r4); r6 = a - (a >> 2); r7 = b - (b >> 2); } break;
	case H_SMALL_EXPLOSION: r4 = c * 2 + 16; r6 = MULS (r6, r4) >> 2; r7 = MULS (r7, r4) >> 2; break;
	case H_BIG_EXPLOSION:
	case H_BIG_SECTION_THRUST: r4 = c * 2 + 16; r6 = MULS (r6, r4); r7 = MULS (r7, r4); break;
	case H_BLACK_EXPLOSION: shade = 12; r4 = ((16 - c) << 2) + 32; r6 = MULS (r6, r4) >> 1; r7 = MULS (r7, r4) >> 1; break;
	}
	x -= r6; z -= r6;
	sx = MULS (x, scale) >> 16;
	sy = MULS (z, scale) >> 16;
	cel_quad.x_pos0 = sx; cel_quad.y_pos0 = sy;
	cel_quad.shade = shade;
	arm_addgamecel (&cel_quad, colour, r7, r7);
}
