/* SF_ARMSky.c - C translation of SF_ARMSky.s : sky gradient rendered as a 1-pixel wide cel */
#include "SF_ARMSky.h"
#include "SF_Celutils.h"
#include "SF_Screenutils.h"
#include "Maths_Stuff.h"
#include "arm_ops.h"

#define cel_maxhold 1024

char skyfile [1024] __attribute__ ((aligned (8)));		/* loaded as host words from Sky/<name> */
static uint32_t armsky_renderdata [400 * 2];			/* one 8-byte line per scanline */
static int32_t  armsky_renderheight;

void arm_rendersky (int32_t pitch, int32_t height)
{
	const uint32_t *sky = (const uint32_t *) skyfile;
	int32_t r0 = pitch, r1 = height, r4, r6, idx;
	if (r1 <= 0) { armsky_renderheight = r1; return; }
	r4 = (int32_t) sky [0];
	sky += 2;
	r6 = r0 >> 7;
	r6 += r6 >> 1;
	r6 += 48;
	if (r6 > 255) r6 = 255;
	r0 += r4;
	r4 = 0;
	while (r1 >= 400) {				/* clip: skip the lines that would be off the top */
		r4 += r0 >> 1;
		r0 -= r0 >> 5;
		r1--;
	}
	armsky_renderheight = r1;
	for (idx = r1; idx > 0; idx--) {
		int32_t r5 = r4 >> 10;
		armsky_renderdata [idx * 2] = (r5 > r6) ? sky [r6] : sky [r5];
		r4 += r0 >> 1;
		if (r0 > 128) r0 -= r0 >> 5;
	}
}

void arm_plotsky (void *celquad)
{
	cel_celdata *cq = (cel_celdata *) celquad;
	int32_t h = armsky_renderheight;
	int32_t x0, y0, x1, y1, x2, y2, x3, y3;
	CCB *c;
	if (h <= 0) return;
	if (cq->temp_cels >= cel_maxhold) screen_intercept ();
	x0 = cq->x_pos0; y0 = cq->y_pos0; x1 = cq->x_pos1; y1 = cq->y_pos1;
	x2 = cq->x_pos2; y2 = cq->y_pos2; x3 = cq->x_pos3; y3 = cq->y_pos3;
	c = cq->plotlist [cq->temp_cels++];
	c->ccb_Flags = 0x3f6e4120u;
	c->ccb_SourcePtr = (CelData *) armsky_renderdata;
	c->ccb_XPos = (160 << 16) + (int32_t) LSL ((uint32_t) x0, 16);
	c->ccb_YPos = (120 << 16) + (int32_t) LSL ((uint32_t) y0, 16);
	c->ccb_HDX = (int32_t) LSL ((uint32_t)(x1 - x0), 20);
	c->ccb_HDY = (int32_t) LSL ((uint32_t)(y1 - y0), 20);
	c->ccb_VDX  = divide ((int32_t) LSL ((uint32_t)(x3 - x0), 16), h);
	c->ccb_VDY  = divide ((int32_t) LSL ((uint32_t)(y3 - y0), 16), h);
	c->ccb_HDDX = divide ((int32_t) LSL ((uint32_t)(x2 - x3 - x1 + x0), 20), h);
	c->ccb_HDDY = divide ((int32_t) LSL ((uint32_t)(y2 - y3 - y1 + y0), 20), h);
	c->ccb_PIXC = 0x1f001f00u;
	c->ccb_PRE0 = ((uint32_t)(h - 1) << 6) | (1 << 4) | 6;
	c->ccb_PRE1 = 1u << 12;
}
