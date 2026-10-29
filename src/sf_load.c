/* sf_load.c - resource file access for the SDL port */
#include "sf_load.h"
#include <stdarg.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

char sf_resource_root[512] = "SF_Resources/";

void sf_swap_words(void *buf, size_t bytes)
{
#if !SF_BIG_ENDIAN
	uint32_t *w = (uint32_t *)buf;
	size_t n = bytes / 4, i;
	for (i = 0; i < n; i++) w[i] = sf_bswap32(w[i]);
#else
	(void)buf; (void)bytes;
#endif
}

/* Resolve 'rel' under root one path component at a time, ignoring case. */
static int resolve(const char *rel, char *out, size_t outsz)
{
	char cur[1024];
	char comp[256];
	const char *p = rel;
	size_t rl = strlen(sf_resource_root);
	if (rl >= sizeof cur) return 0;
	memcpy(cur, sf_resource_root, rl + 1);
	while (*p) {
		size_t n = 0;
		struct stat st;
		char trial[1024];
		while (*p == '/') p++;
		if (!*p) break;
		while (*p && *p != '/' && n < sizeof comp - 1) comp[n++] = *p++;
		comp[n] = 0;
		if (snprintf(trial, sizeof trial, "%s%s", cur, comp) >= (int)sizeof trial) return 0;
		if (stat(trial, &st) != 0) {
			DIR *d = opendir(cur[0] ? cur : ".");
			struct dirent *e;
			int found = 0;
			if (!d) return 0;
			while ((e = readdir(d)) != NULL) {
				if (strcasecmp(e->d_name, comp) == 0) {
					snprintf(trial, sizeof trial, "%s%s", cur, e->d_name);
					found = 1; break;
				}
			}
			closedir(d);
			if (!found) return 0;
		}
		if (strlen(trial) + 2 >= sizeof cur) return 0;
		strcpy(cur, trial);
		if (*p == '/') strcat(cur, "/");
	}
	if (strlen(cur) >= outsz) return 0;
	strcpy(out, cur);
	return 1;
}

FILE *sf_fopen_resource(const char *relpath, const char *mode)
{
	char full[1024];
	if (!resolve(relpath, full, sizeof full)) return NULL;
	return fopen(full, mode);
}
int sf_resource_exists(const char *relpath)
{
	char full[1024];
	return resolve(relpath, full, sizeof full);
}

/* The game builds paths as RESOURCES_ROOT + name; strip the root if present. */
const char *sf_rel(const char *p);
static const char *strip_root(const char *p)
{
	size_t rl = strlen(sf_resource_root);
	if (strncmp(p, sf_resource_root, rl) == 0) return p + rl;
	return p;
}

static void *load_v(void *buffer, int words, const char *fmt, va_list ap)
{
	char name[512];
	FILE *f;
	long sz;
	vsnprintf(name, sizeof name, fmt, ap);
	f = sf_fopen_resource(strip_root(name), "rb");
	if (!f) { fprintf(stderr, "LOAD ERROR : Can't load '%s'\n", name); return NULL; }
	fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
	if (fread(buffer, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return NULL; }
	fclose(f);
	if (words) sf_swap_words(buffer, (size_t)sz & ~(size_t)3);
	return buffer;
}
void *load_fileat(void *buffer, const char *fmt, ...)
{ va_list ap; void *r; va_start(ap, fmt); r = load_v(buffer, 0, fmt, ap); va_end(ap); return r; }
void *load_fileat_words(void *buffer, const char *fmt, ...)
{ va_list ap; void *r; va_start(ap, fmt); r = load_v(buffer, 1, fmt, ap); va_end(ap); return r; }

/* Decode a 16-bit 320x240 image stored in the 3DO's "LRFORM" (left/right) layout.
 *
 * LRFORM is the frame buffer's native arrangement.  For each pair of display rows (2j, 2j+1):
 *     memory row 2j   holds the LEFT  halves (x = 0..159)   of both rows, interleaved pixel by pixel
 *     memory row 2j+1 holds the RIGHT halves (x = 160..319) of both rows, interleaved likewise
 * i.e.  display (y, x)  <->  memory row 2*(y>>1) + (x>=160),  memory column 2*(x%160) + (y&1).
 * (Worked out by matching the 'FedNet' image against a screenshot from a 3DO emulator.)
 * Image files either start with an 'IMAG' chunk followed by a 'PDAT' chunk containing the pixels,
 * or are bare 153600-byte frame-buffer dumps; both use this layout. */
void *load_image_raw(void *buffer, const char *relpath)
{
	static uint8_t file [320 * 240 * 2 + 4096];
	uint16_t *dst = (uint16_t *)buffer;
	FILE *f = sf_fopen_resource(strip_root(relpath), "rb");
	size_t n, off = 0;
	int x, y;
	if (!f) { fprintf(stderr, "LOAD ERROR : Can't load image '%s'\n", relpath); return NULL; }
	n = fread(file, 1, sizeof file, f);
	fclose(f);
	if (n >= 16 && memcmp(file, "IMAG", 4) == 0) {			/* walk chunks to the pixel data */
		size_t p = 0; off = 0;
		while (p + 8 <= n) {
			uint32_t sz = ((uint32_t)file[p+4] << 24) | (file[p+5] << 16) | (file[p+6] << 8) | file[p+7];
			if (memcmp(file + p, "PDAT", 4) == 0) { off = p + 8; break; }
			if (sz < 8) break;
			p += sz;
		}
		if (off == 0) { fprintf(stderr, "LOAD ERROR : no pixel data in '%s'\n", relpath); return NULL; }
	}
	if (n < off + 320 * 240 * 2) { fprintf(stderr, "LOAD ERROR : image '%s' too short\n", relpath); return NULL; }
	for (y = 0; y < 240; y++) {
		for (x = 0; x < 320; x++) {
			int half = x >= 160, k = x % 160;
			const uint8_t *px = file + off + ((size_t)(2 * (y >> 1) + half) * 320 + (size_t)(2 * k + (y & 1))) * 2;
			dst[y * 320 + x] = (uint16_t)((px[0] << 8) | px[1]);
		}
	}
	return buffer;
}

const char *sf_rel(const char *p) { return strip_root(p); }
