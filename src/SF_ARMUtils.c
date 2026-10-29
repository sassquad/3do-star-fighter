/* SF_ARMUtils.c - C translation of SF_ARMUtils.s (random numbers, map target drawing) */
#include "SF_ARMUtils.h"
#include "SF_Celutils.h"
#include "SF_ARMCell.h"
#include "SF_Access.h"
#include "arm_ops.h"

#define MAP_OBJECTSPRITE 145      /* Sprite for ground object */

static uint32_t arm_randomseed1 = 1023, arm_randomseed2 = 1023;
static int32_t  arm_crossshade;

void arm_randominit (void) { arm_randomseed1 = 1023; arm_randomseed2 = 1023; }

int32_t arm_random (void)
{
	uint32_t r0 = arm_randomseed1, r1 = arm_randomseed2;
	r0 ^= ROR (r1, r1);          /* eor r0,r0,r1,ror r1 */
	r1 += r0;
	arm_randomseed1 = r0; arm_randomseed2 = r1;
	return (int32_t)(r0 ^ r1);
}

int32_t arm_randomvalue (int32_t limit)
{
	int32_t r0 = 0;
	uint32_t r2 = (uint32_t) arm_random (), r1 = 1u << 16;
	for (;;) {
		r1 >>= 1;
		if (r1 == 0) return r0;
		if (!(r2 & r1)) continue;
		r0 += (int32_t) r1;
		if (r0 > limit) r0 -= (int32_t) r1;
	}
}

void arm_drawmaptargets (void *polymap, int32_t zoom, void *celquad, int32_t offset)
{
	const uint8_t *map = (const uint8_t *) polymap;
	const uint8_t *hmap = (const uint8_t *) height_map;
	cel_celdata *cq = (cel_celdata *) celquad;
	int32_t xoff = offset & 255, yoff = (int32_t)((uint32_t) offset >> 8);
	int32_t scale = zoom + 20;
	int32_t i;

	arm_crossshade = (arm_crossshade + 1) & 255;
	cq->shade = (arm_crossshade & 4) ? 8 : 11;

	for (i = 16383; i >= 0; i--) {
		int32_t obj = map [i], x = i & 127, y = i >> 7, hoff, r0, r1;
		if (obj == 0 || obj >= 60) continue;
		hoff = (y << 9) + (x << 1);
		r0 = (int32_t)((uint32_t) x << 25) - (int32_t)((uint32_t) xoff << 24);
		r1 = (int32_t)((uint32_t) y << 25) - (int32_t)((uint32_t) yoff << 24);
		r0 = ASR (r0, (uint32_t) scale);
		r1 = ASR (r1, (uint32_t) scale);
		if (r0 <= -128 || r1 <= -128) continue;
		if (r0 >= 128 || r1 >= 128) continue;
		r1 = -r1;
		r0 -= r1 >> 1;
		r0 >>= 1;  r0 += r0 >> 1;
		r1 >>= 1;  r1 += r1 >> 1;
		r1 -= ASR ((int32_t) hmap [hoff], (uint32_t) zoom);
		r1 -= 10 - 3;
		cq->x_pos0 = r0;
		cq->y_pos0 = r1;
		arm_addgamecel (cq, MAP_OBJECTSPRITE, 1024, 1024);
	}
}
