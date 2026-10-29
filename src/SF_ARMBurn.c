/* SF_ARMBurn.c - C translation of SF_ARMBurn.s : ground explosions that burn the landscape
 *
 * Each ongoing explosion is an 8-byte record: delay/timer, stage, original tile, type (0 ground,
 * 8 sea) and the 16-bit map position.  Every two frames an explosion advances one stage, writes
 * the tile into sprite_map and re-draws the tile's pixels in the 6-bit 512x512 map.
 */
#include "SF_ARMBurn.h"
#include "SF_Celutils.h"
#include "SF_Access.h"
#include "SF_Status.h"
#include "SF_Mission.h"
#include "arm_ops.h"

#define MAX_EXPLOSIONS 508

typedef struct { uint8_t timer, stage, original, type; int32_t pos; } burn_explosion;

static const int32_t spx1 [] = { 0, 0, 65536, 65536 };
static const int32_t spx2 [] = { 256, 0, -1, 0, 1, 1, -256, 0, 0, 1, 65536, 65536 };
static const int32_t spx3 [] = { 512, 3, -2, 3, 2, 3, -512, 3, 255, 2, 257, 2, -257, 2, -255, 2,
                                 256, 1, -1, 1, 1, 1, -256, 1, 0, 0, 65536, 65536 };
static const int32_t spx4 [] = { 511, 4, 513, 4, 254, 4, 258, 4, -258, 4, -254, 4, -513, 4, -511, 4,
                                 512, 3, -2, 3, 2, 3, -512, 3, 255, 2, 257, 2, -257, 2, -255, 2,
                                 256, 1, -1, 1, 1, 1, -256, 1, 0, 0, 65536, 65536 };
static const int32_t *const armburn_explosions [4] = { spx1, spx2, spx3, spx4 };

static uint8_t armburn_sprites [16];			/* 0-7 ground sprites, 8-15 sea sprites */
static uint8_t armburn_doheightcheck;
static int32_t armburn_currentexplosions;
static burn_explosion armburn_data [512];

void armburn_resetexplosions (void) { armburn_currentexplosions = 0; }

/* planet_info holds the four sprite words and the height-check flag; words are big-endian
   byte groups, so unpack them most significant byte first */
void armburn_initialise (void *planet)
{
	const planet_data *p = (const planet_data *) planet;
	const int32_t w [4] = { p->explosion1_data1, p->explosion1_data2, p->explosion2_data1, p->explosion2_data2 };
	int i;
	for (i = 0; i < 4; i++) {
		armburn_sprites [i * 4 + 0] = (uint8_t)((uint32_t) w [i] >> 24);
		armburn_sprites [i * 4 + 1] = (uint8_t)((uint32_t) w [i] >> 16);
		armburn_sprites [i * 4 + 2] = (uint8_t)((uint32_t) w [i] >> 8);
		armburn_sprites [i * 4 + 3] = (uint8_t) w [i];
	}
	armburn_doheightcheck = (uint8_t) p->explosion_heightcheck;
}

void armburn_addexplosion (int32_t x, int32_t y, int32_t shape)
{
	const uint8_t *smap = (const uint8_t *) sprite_map, *hmap = (const uint8_t *) height_map;
	const int32_t *s = armburn_explosions [shape & 3];
	int32_t base = x + (y << 8), count = armburn_currentexplosions;

	for (;;) {
		int32_t offs, delay, pos, type;
		burn_explosion *e;
		uint8_t tile;
		if (count >= MAX_EXPLOSIONS) { armburn_currentexplosions = count; return; }
		offs = s [0]; delay = s [1]; s += 2;
		if (offs == 65536) { armburn_currentexplosions = count; return; }
		pos = (offs + base) & 0xffff;
		e = &armburn_data [count++];
		tile = smap [pos];
		e->timer = (uint8_t) delay; e->stage = 0; e->original = tile;
		if (armburn_doheightcheck) {
			type = 0;
			if (hmap [pos] < 17 || hmap [(pos + 1) & 0xffff] < 17 ||
			    hmap [(pos + 256) & 0xffff] < 17 || hmap [(pos + 257) & 0xffff] < 17) type = 8;
		} else type = (smap [base] & 1) ? 8 : 0;
		e->type = (uint8_t) type;
		e->pos = pos;
		/* an explosion is already burning here if the tile shows one of its animation frames */
		{
			const uint8_t *sp = armburn_sprites + type;
			if (tile == sp [0] || tile == sp [1] || tile == sp [2] || tile == sp [3]) count--;
		}
	}
}

/* write 'nbits' (MSB first) of 'value' into the word-swapped 6-bit map at bit offset 'bit' */
static void put_bits (uint32_t *words, int32_t bit, uint32_t value, int nbits)
{
	int i;
	for (i = nbits - 1; i >= 0; i--, bit++) {
		uint32_t mask = 0x80000000u >> (bit & 31);
		if ((value >> i) & 1) words [bit >> 5] |= mask; else words [bit >> 5] &= ~mask;
	}
}

void armburn_updateexplosions (void *spritemap, void *celquad)
{
	uint8_t *smap = (uint8_t *) spritemap;
	cel_celdata *cq = (cel_celdata *) celquad;
	const uint32_t *tiles = (const uint32_t *) cq->cels4x4;
	uint32_t *map512 = (uint32_t *) cq->cel_map512;
	uint8_t *lookup = (uint8_t *) cq->cache_lookup;
	int32_t count = armburn_currentexplosions, idx, active = 0;

	if (count == 0) return;
	idx = count;
	for (;;) {
		burn_explosion *e;
		int32_t x, ybits, stage, variant_or_stage, bitpos;
		uint32_t sprite;
		const uint32_t *t;
		uint8_t code;

		idx--;
		if (idx < 0) { return; }
		e = &armburn_data [idx];
		if (e->timer == 255) { if (!active) armburn_currentexplosions = idx; continue; }
		active = 1;
		if (e->timer != 0) { e->timer--; continue; }
		e->timer = 1;

		x = e->pos & 255;
		ybits = (int32_t)(((uint32_t) e->pos >> 8) << 10);
		bitpos = ((ybits + x * 2) * 48) >> 3;				/* *6 bits per map pixel */
		stage = e->stage;
		if (stage >= 4) {									/* finished: pick a burnt-ground variant */
			variant_or_stage = (int32_t)(((uint32_t) x ^ (uint32_t) idx ^ ((uint32_t) e->pos >> 8)) & 3) + 4;
			e->timer = 255;
			stage = variant_or_stage;
			code = armburn_sprites [stage + e->type];
		} else {
			code = armburn_sprites [stage + e->type];
			e->stage = (uint8_t)(stage + 1);
		}
		if (code == 255) code = e->original;
		smap [e->pos] = code;

		t = tiles + code * 4;
		put_bits (map512, bitpos, t [0] >> 26, 6);
		put_bits (map512, bitpos + 6, (t [0] >> 14) & 63, 6);
		put_bits (map512, bitpos + 384 * 8, t [2] >> 26, 6);
		put_bits (map512, bitpos + 384 * 8 + 6, (t [2] >> 14) & 63, 6);

		sprite = (uint32_t)(((e->pos >> 1) & 0x7e) + ((e->pos >> 10) << 7));
		if (lookup [sprite] > 0) lookup [sprite] = 1;		/* make the cache rebuild this cel */
	}
}
