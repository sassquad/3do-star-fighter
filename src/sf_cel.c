/*
 * sf_cel.c - software emulation of the 3DO cel engine (the subset Star Fighter uses),
 *            plus screen / text shims.
 *
 * Cel pixel data is expected in the "word-swapped" model: 32-bit words whose numeric
 * value equals the big-endian word that was stored in the file / built by the original
 * ARM code.  Pixels are read MSB-first.
 *
 * Mapping model (matches the hardware):
 *      P(u,v) = P0 + v*V + u*(H + v*HDD)
 *   H,HDD are 12.20 fixed point per source pixel, V is 16.16 per source row,
 *   P0 is 16.16 screen position.  Each source texel is drawn as the quadrilateral
 *   spanned by P(u,v) .. P(u+1,v+1) (forward mapping, later texels overwrite earlier).
 *
 * Pixel processor (PIXC), per P-mode (low half = P-mode 0, high half = P-mode 1):
 *      bits 12-10 MF  -> multiplier (MF+1)
 *      bits  9- 8 DV  -> divisor    {16,2,4,8}
 *      bits  7- 6 2S  -> second source: 0 none, 2 frame buffer, 3 source pixel
 *      bits  5- 4 2DV -> second source divisor {1,2,4,8}
 *      bit      0     -> halve the sum
 *   result = clamp5( (src*(MF+1)/DV + src2/2DV) / (bit0?2:1) )
 *   (derived from the shade tables in SF_ARMCell.s whose comments give the intended
 *    percentages; the tables reproduce those percentages exactly).
 */
#include "sf_compat.h"
#include "sf_platform.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */
static Bitmap  g_bitmaps[2];
static uint16_t *g_fb[2];

int OpenGraphics(ScreenContext *sc, int n)
{
	int i;
	for (i = 0; i < 2; i++) {
		g_fb[i] = (uint16_t *)calloc(SF_SCREEN_W * SF_SCREEN_H, 2);
		g_bitmaps[i].bm_Width = SF_SCREEN_W;
		g_bitmaps[i].bm_Height = SF_SCREEN_H;
		g_bitmaps[i].bm_Buffer = g_fb[i];
		sc->sc_Bitmaps[i] = &g_bitmaps[i];
		sc->sc_Screens[i] = i + 1;
		sc->sc_BitmapItems[i] = i + 1;
	}
	sc->sc_nScreens = n;
	sc->sc_curScreen = 0;
	sc->sc_nFrameBufferPages = (SF_SCREEN_W * SF_SCREEN_H * 2 + 4095) / 4096;
	return plat_init();
}
void CloseGraphics(ScreenContext *sc) { (void)sc; plat_shutdown(); }

static uint16_t *fb_for_bitmap(Item bi) { return g_fb[(bi - 1) & 1]; }
static uint16_t *fb_for_screen(Item si) { return g_fb[(si - 1) & 1]; }

int DisplayScreen(Item screenItem, Item unused)
{
	(void)unused;
	plat_present(fb_for_screen(screenItem), 256);
	return 0;
}

void FillRect(Item bi, GrafCon *gc, Rect *r)
{
	uint16_t *fb = fb_for_bitmap(bi);
	int x, y;
	int x0 = r->rect_XLeft, x1 = r->rect_XRight, y0 = r->rect_YTop, y1 = r->rect_YBottom;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > SF_SCREEN_W) x1 = SF_SCREEN_W;
	if (y1 > SF_SCREEN_H) y1 = SF_SCREEN_H;
	for (y = y0; y < y1; y++)
		for (x = x0; x < x1; x++) fb[y * SF_SCREEN_W + x] = (uint16_t)gc->gc_FGPen;
}

void CopyVRAMPages(Item req, void *dst, void *src, int pages, int mask)
{ (void)req; (void)pages; (void)mask; memcpy(dst, src, SF_SCREEN_W * SF_SCREEN_H * 2); }
void SetVRAMPages(Item req, void *dst, int value, int pages, int mask)
{
	uint16_t *d = (uint16_t *)dst; int i; (void)req; (void)pages; (void)mask;
	for (i = 0; i < SF_SCREEN_W * SF_SCREEN_H; i++) d[i] = (uint16_t)value;
}
void SetScreenColor(Item screen, uint32_t entry) { (void)screen; (void)entry; }

void FadeToBlack(ScreenContext *sc, int frames)
{
	int i; uint16_t *fb = g_fb[1 - sc->sc_curScreen];
	for (i = 1; i <= frames; i++) { plat_present(fb, 256 - 256 * i / frames); WaitVBL(0, 1); }
}
void FadeFromBlack(ScreenContext *sc, int frames)
{
	int i; uint16_t *fb = g_fb[1 - sc->sc_curScreen];
	for (i = 1; i <= frames; i++) { plat_present(fb, 256 * i / frames); WaitVBL(0, 1); }
}

/* ------------------------------------------------------------------ */
/* Cel engine                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
	uint16_t plut[32];
	uint32_t pixc;
	int32_t  xpos, ypos, hdx, hdy, vdx, vdy, hddx, hddy;
} CEState;

#define MAXW 1024
#define MAXH 1024

static inline uint32_t get_bits(const uint32_t *row, uint32_t bitpos, int n)
{
	uint32_t wi = bitpos >> 5, off = bitpos & 31;
	uint64_t win = ((uint64_t)row[wi] << 32) | row[wi + 1];
	return (uint32_t)((win >> (64 - n - off)) & ((1u << n) - 1));
}

static inline int is_fb_source(const CelData *p)
{ return p == (const CelData *)g_fb[0] || p == (const CelData *)g_fb[1]; }

/* CCB_USEAV in force for the cel being drawn: 8bpp coded pixels carry a 3-bit AMV in their top bits. */
static int g_useav;

static const int bpp_tab[8] = {0, 1, 2, 4, 6, 8, 16, 16};

/* Decode one source row into 'out' as 17-bit values: bit16 = transparent, bits15-0 colour (+SSB).
   Returns number of pixels decoded. */
static void decode_row_unpacked(const uint32_t *row, int bpp, int coded, int plutpos, int skipx,
                                int width, const CEState *st, int force_p, uint32_t *out)
{
	int i;
	for (i = 0; i < width; i++) {
		uint32_t px = get_bits(row, (uint32_t)(i + skipx) * bpp, bpp), col, p;
		if (!coded) { col = px; p = (col >> 15) & 1; }
		else {
			uint32_t idx = px & ((bpp > 5) ? 31 : ((1u << bpp) - 1));
			col = st->plut[idx];
			if (!plutpos && bpp >= 6) p = (px >> (bpp - 1)) & 1; else p = (col >> 15) & 1;
		}
		if (force_p >= 0) p = (uint32_t)force_p;
		out[i] = (col & 0x7fff) | (p << 15) | ((uint32_t)((col & 0x7fff) == 0) << 16);
		if (g_useav && coded && bpp == 8) out[i] |= ((px >> 5) & 7) << 17;		/* AMV */
	}
}

/* Packed (RLE) rows. 'row' points at the row header. Returns pointer to next row. */
/* Number of pixels in the row just decoded by decode_row_packed (natural-width mode only). */
static int g_packed_px;

/* width > 0: the cel's PRE1 gave the width; pad/clip the row to it.
   width == 0: packed cel with no PRE1 (see draw_one); the row's own data says how long it is. */
static const uint32_t *decode_row_packed(const uint32_t *row, int bpp, int coded, int plutpos,
                                         int skipx, int width, const CEState *st, int force_p, uint32_t *out)
{
	uint32_t pos, rowwords;
	int natural = (width == 0);
	int x = 0, total = natural ? MAXW - 1 : width + skipx;
	uint32_t tmp[MAXW + 64];
	if (bpp < 8) { rowwords = get_bits(row, 0, 8); pos = 8; }
	else         { rowwords = get_bits(row, 0, 16); pos = 16; }
	while (x < total) {
		uint32_t hdr = get_bits(row, pos, 8), type = hdr >> 6, cnt = (hdr & 63) + 1, i;
		pos += 8;
		if (type == 0) break;
		if (x + (int)cnt > total) cnt = (uint32_t)(total - x);
		for (i = 0; i < cnt; i++) {
			uint32_t px = 0, col = 0, p, tr = 0;
			if (type == 1 || (type == 3 && i == 0)) {
				px = get_bits(row, pos, bpp); pos += bpp;
				if (type == 3) { /* remember for repeat */ tmp[MAXW + 0] = px; }
			} else if (type == 3) px = tmp[MAXW + 0];
			if (type == 2) tr = 1;
			else if (!coded) { col = px; p = (col >> 15) & 1; (void)p; tr = (px & 0x7fff) == 0; }
			else {
				uint32_t idx = px & ((bpp > 5) ? 31 : ((1u << bpp) - 1));
				col = st->plut[idx]; tr = (col & 0x7fff) == 0;
			}
			{
				uint32_t pp;
				if (!coded) pp = (col >> 15) & 1;
				else if (!plutpos && bpp >= 6) pp = (px >> (bpp - 1)) & 1;
				else pp = (col >> 15) & 1;
				if (force_p >= 0) pp = (uint32_t)force_p;
				tmp[x] = (col & 0x7fff) | (pp << 15) | (tr << 16);
			}
			x++;
		}
	}
	if (natural) {
		g_packed_px = (x > skipx) ? x - skipx : 0;
		if (g_packed_px) memcpy(out, tmp + skipx, (size_t)g_packed_px * sizeof(uint32_t));
		return (const uint32_t *)((const uint8_t *)row + (rowwords + 2) * 4);
	}
	for (; x < total; x++) tmp[x] = 1u << 16;
	memcpy(out, tmp + skipx, (size_t)width * sizeof(uint32_t));
	return (const uint32_t *)((const uint8_t *)row + (rowwords + 2) * 4);
}

static inline uint16_t pixproc(uint32_t pix16, uint32_t pixc, uint16_t bg)
{
	uint32_t half = ((pix16 & 0x8000) ? (pixc >> 16) : pixc) & 0xffff;
	if (half == 0xbfc0) {		/* TextLib anti-alias: existing pixel scaled by AMV/8, plus the pixel's colour */
		uint32_t amv = (pix16 >> 17) & 7, o = 0, sh;
		for (sh = 0; sh <= 10; sh += 5) {
			uint32_t v = ((bg >> sh) & 31) * amv / 8 + ((pix16 >> sh) & 31);
			o |= (v > 31 ? 31 : v) << sh;
		}
		return (uint16_t)o;
	}
	uint32_t mf = ((half >> 10) & 7) + 1, dvs = (half >> 8) & 3, s2 = (half >> 6) & 3, d2s = (half >> 4) & 3;
	static const uint32_t dvt[4] = {16, 2, 4, 8};
	uint32_t dv = dvt[dvs], d2 = 1u << d2s, out = 0, sh;
	for (sh = 0; sh <= 10; sh += 5) {
		uint32_t c = (pix16 >> sh) & 31, v = c * mf / dv, t = 0;
		if (s2 == 2) t = ((bg >> sh) & 31) / d2;
		else if (s2 == 3) t = c / d2;
		v += t;
		if (half & 1) v >>= 1;
		if (v > 31) v = 31;
		out |= v << sh;
	}
	return (uint16_t)out;
}

static inline void put_px(uint16_t *fb, int x, int y, uint32_t pix, uint32_t pixc, int bgnd)
{
	uint16_t *d;
	if ((unsigned)x >= SF_SCREEN_W || (unsigned)y >= SF_SCREEN_H) return;
	if (((pix >> 16) & 1) && !bgnd) return;          /* transparent */
	d = &fb[y * SF_SCREEN_W + x];
	*d = pixproc(pix, pixc, *d);
}

static void fill_quad(uint16_t *fb, const float *px, const float *py, uint32_t pix, uint32_t pixc, int bgnd)
{
	float minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
	int i, x0, x1, y0, y1, x, y;
	for (i = 1; i < 4; i++) {
		if (px[i] < minx) minx = px[i]; if (px[i] > maxx) maxx = px[i];
		if (py[i] < miny) miny = py[i]; if (py[i] > maxy) maxy = py[i];
	}
	x0 = (int)ceilf(minx - 0.5f); x1 = (int)ceilf(maxx - 0.5f) - 1;   /* centres in [min,max) */
	y0 = (int)ceilf(miny - 0.5f); y1 = (int)ceilf(maxy - 0.5f) - 1;
	if (x1 < 0 || y1 < 0 || x0 >= SF_SCREEN_W || y0 >= SF_SCREEN_H) return;
	if (x0 == x1 && y0 == y1) { put_px(fb, x0, y0, pix, pixc, bgnd); return; }   /* common minified case */
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
	if (x1 >= SF_SCREEN_W) x1 = SF_SCREEN_W - 1; if (y1 >= SF_SCREEN_H) y1 = SF_SCREEN_H - 1;
	for (y = y0; y <= y1; y++) {
		float cy = y + 0.5f;
		for (x = x0; x <= x1; x++) {
			float cx = x + 0.5f;
			int neg = 0, pos = 0;
			for (i = 0; i < 4; i++) {
				int j = (i + 1) & 3;
				float e = (px[j] - px[i]) * (cy - py[i]) - (py[j] - py[i]) * (cx - px[i]);
				if (e < 0) neg = 1; else if (e > 0) pos = 1;
			}
			if (!(neg && pos)) put_px(fb, x, y, pix, pixc, bgnd);
		}
	}
}

static void draw_one(uint16_t *fb, CCB *c, CEState *st)
{
	uint32_t fl = c->ccb_Flags, pre0, pre1;
	const CelData *src = c->ccb_SourcePtr;
	int bpp, coded, packed, plutpos, force_p = -1, skipx, width, height, hflip, vflip, v, u, stride_words = 0;
	uint32_t rowbuf[MAXW + 8];
	int fbsrc = is_fb_source(src);
	int natural = 0;							/* packed cel with no PRE1: row widths are not known up front */
	float X, Y;

	if (fl & CCB_LDSIZE) { st->hdx = c->ccb_HDX; st->hdy = c->ccb_HDY; st->vdx = c->ccb_VDX; st->vdy = c->ccb_VDY; }
	if (fl & CCB_LDPRS)  { st->hddx = c->ccb_HDDX; st->hddy = c->ccb_HDDY; }
	if (fl & CCB_LDPPMP) st->pixc = c->ccb_PIXC;
	g_useav = (fl & CCB_USEAV) != 0;
	if (fl & CCB_YOXY)   { st->xpos = c->ccb_XPos; st->ypos = c->ccb_YPos; }
	if (fl & CCB_CCBPRE) { pre0 = c->ccb_PRE0; pre1 = c->ccb_PRE1; }
	else if ((fl & CCB_PACKED) && !fbsrc) {
		/* Packed cel with the preamble in its data: the data holds PRE0 only (confirmed against every
		   packed cel in the game's Game.* files).  There is no PRE1, so the width comes from the rows. */
		pre0 = src[0]; pre1 = 0; src += 1; natural = 1;
	}
	else { pre0 = src[0]; pre1 = src[1]; src += 2; }
	if ((fl & CCB_LDPLUT) && c->ccb_PLUTPtr) {
		const uint32_t *pw = (const uint32_t *)c->ccb_PLUTPtr; int i;
		for (i = 0; i < 32; i++) st->plut[i] = (uint16_t)(pw[i >> 1] >> ((i & 1) ? 0 : 16));
	}
	if (fl & CCB_SKIP) return;
	if ((fl & CCB_POVER_MASK) == 0x100) force_p = 0;
	else if ((fl & CCB_POVER_MASK) == 0x180) force_p = 1;
	coded = !(pre0 & 0x10); bpp = bpp_tab[pre0 & 7]; packed = (fl & CCB_PACKED) != 0; plutpos = (fl & CCB_PLUTPOS) != 0;
	skipx = (int)((pre0 >> 24) & 15);
	hflip = (pre0 >> 31) & 1; vflip = (pre0 >> 30) & 1;
	height = (int)((pre0 >> 6) & 0x3ff) + 1;
	width = natural ? 0 : (int)(pre1 & 0x7ff) + 1 - skipx;
	if (bpp == 0 || (!natural && width <= 0) || width > MAXW || height > MAXH) return;
	if (natural) stride_words = 0;
	else if (bpp >= 8) stride_words = (int)((pre1 >> 16) & 0x3ff) + 2; else stride_words = (int)((pre1 >> 24) & 0xff) + 2;
	{
		float H0x = st->hdx / 1048576.0f, H0y = st->hdy / 1048576.0f;
		float Vx = st->vdx / 65536.0f, Vy = st->vdy / 65536.0f;
		float Dx = st->hddx / 1048576.0f, Dy = st->hddy / 1048576.0f;
		const uint32_t *rowp = src;
		X = st->xpos / 65536.0f; Y = st->ypos / 65536.0f;
		for (v = 0; v < height; v++) {
			int vv = vflip ? height - 1 - v : v;
			float Hx = H0x + vv * Dx, Hy = H0y + vv * Dy;
			float rx = X + vv * Vx, ry = Y + vv * Vy;
			float Hx2 = H0x + (vv + 1) * Dx, Hy2 = H0y + (vv + 1) * Dy;
			float rx2 = X + (vv + 1) * Vx, ry2 = Y + (vv + 1) * Vy;
			if (fbsrc) {
				const uint16_t *r = (const uint16_t *)src + (size_t)v * SF_SCREEN_W;
				for (u = 0; u < width; u++) rowbuf[u] = (r[u] & 0xffff) | (((r[u] & 0x7fff) == 0) ? 0 : 0);
			} else if (packed) {
				rowp = decode_row_packed(rowp, bpp, coded, plutpos, skipx, natural ? 0 : width, st, force_p, rowbuf);
				if (natural) width = g_packed_px;			/* this row's pixel count */
			} else {
				decode_row_unpacked(rowp, bpp, coded, plutpos, skipx, width, st, force_p, rowbuf);
				rowp += stride_words;
			}
			for (u = 0; u < width; u++) {
				int uu = hflip ? width - 1 - u : u;
				float px[4], py[4];
				px[0] = rx + uu * Hx;         py[0] = ry + uu * Hy;
				px[1] = rx + (uu + 1) * Hx;   py[1] = ry + (uu + 1) * Hy;
				px[2] = rx2 + (uu + 1) * Hx2; py[2] = ry2 + (uu + 1) * Hy2;
				px[3] = rx2 + uu * Hx2;       py[3] = ry2 + uu * Hy2;
				if (((rowbuf[u] >> 16) & 1) && !(fl & CCB_BGND)) continue;
				fill_quad(fb, px, py, rowbuf[u], st->pixc, (fl & CCB_BGND) != 0);
			}
		}
	}
}

void DrawCels(Item bitmapItem, CCB *ccb)
{
	CEState st;
	uint16_t *fb = fb_for_bitmap(bitmapItem);
	memset(&st, 0, sizeof st);
	st.pixc = 0x1f001f00u;
	while (ccb) {
		uint32_t fl = ccb->ccb_Flags;
		draw_one(fb, ccb, &st);
		if (fl & CCB_LAST) break;
		ccb = ccb->ccb_NextPtr;
	}
}
