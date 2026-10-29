/* SF_ARMAnim.c - C translation of SF_ARMAnim.s : texture animation
 *
 * Animation data (Animations .anim files, host words):
 *   word 0 : number of animations;  then a table of word offsets (relative to word 1)
 *   each animation block:
 *     [0] active  [1] timer  [2] timer reload  [3] number of slots  [4] current frame
 *     [5] number of frames  [6] restart frame
 *     then for each slot:  slot word, followed by 'nframes' frame words
 * The slot word is an index into the cel offset table (cel_list32).  The 3DO stored the
 * absolute ADDRESS of that table entry in the data after initialisation; here the index is
 * left in place and the table base is kept in armtex_table.  Frame words are converted
 * from sprite numbers to the sprite's offset value at initialisation, as on the 3DO.
 * Animating = writing the frame's offset value into the table entry.
 */
#include "SF_ARMAnim.h"

static uint32_t *armtex_table;

typedef struct { uint32_t *blk; } blkptr;
static uint32_t *block (uint32_t *data, uint32_t n) { return (uint32_t *)((char *)(data + 1) + data [1 + n]); }

void armtex_initialise (void *animdata, void *celtable)
{
	uint32_t *d = (uint32_t *) animdata, n = d [0];
	armtex_table = (uint32_t *) celtable;
	while (n-- > 0) {
		uint32_t *b = block (d, n), *p = b + 7, slots = b [3], frames = b [5], s, f;
		for (s = 0; s < slots; s++) {
			p++;												/* slot index stays an index */
			for (f = 0; f < frames; f++) { *p = armtex_table [*p]; p++; }
		}
	}
}

static void apply_frame (uint32_t *b, uint32_t frame)
{
	uint32_t *p = b + 7, slots = b [3], frames = b [5], s;
	for (s = 0; s < slots; s++) {
		armtex_table [p [0]] = p [1 + frame];
		p += 1 + frames;
	}
}

void armtex_reset (void *animdata)
{
	uint32_t *d = (uint32_t *) animdata, n = d [0];
	while (n-- > 0) {
		uint32_t *b = block (d, n);
		b [1] = b [2];
		b [4] = 0;
		apply_frame (b, 0);
	}
}

void armtex_updateall (void *animdata)
{
	uint32_t *d = (uint32_t *) animdata, n = d [0];
	while (n-- > 0) {
		uint32_t *b = block (d, n);
		int32_t counter;
		if (b [0] == 0) continue;
		counter = (int32_t) b [1] - 1;
		if (counter >= 0) { b [1] = (uint32_t) counter; continue; }
		b [1] = b [2];
		b [4] += 1;
		if ((int32_t) b [4] >= (int32_t) b [5]) b [4] = b [6];
		apply_frame (b, b [4]);
	}
}

void armtex_update (void *animdata, int32_t index, int32_t delta)
{
	uint32_t *b = block ((uint32_t *) animdata, (uint32_t) index);
	int32_t counter = (int32_t) b [1] - 1, frame;
	if (counter >= 0) { b [1] = (uint32_t) counter; return; }
	b [1] = b [2];
	frame = (int32_t) b [4] + delta;
	if (frame < 0) { frame = (int32_t) b [6] - 1; if (frame < 0) frame = 0; }
	if (frame >= (int32_t) b [5]) frame = (int32_t) b [6];
	b [4] = (uint32_t) frame;
	apply_frame (b, (uint32_t) frame);
}
