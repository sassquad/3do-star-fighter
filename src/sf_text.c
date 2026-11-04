/*
 * sf_text.c - the 3DO font library (FontLib + TextLib), using the game's own
 * font files.
 *
 * This replaces the 3DO SDK's FontLib.c / FontBlit5To8_.s / TextLib.c.  The
 * file format and the rendering rules below were taken from the SDK sources
 * (3DO Toolkit 1.5, Lib3DO.lib/TextLib).
 *
 * FONTS (converted from the 3DO FontLib files by tools/convert_resources.py)
 *   Fonts/<name>.txt  metrics: "key value" lines (flags, height, widest, first,
 *                     last, spacing, ascent, descent, leading), then one
 *                     "char x width" line per character; '#' starts a comment.
 *                     Font flags: 0x100 = two-colour font (pixels carry an
 *                     outline/shadow colour as well).
 *   Fonts/<name>.png  8-bit greyscale strip, 'height' pixels high, with each
 *                     character's glyph at its x.  Each pixel is an op, applied
 *                     to the text cel in row-major order:
 *                        0       leave the pixel
 *                        1..31   write pixel value p (as the byte below)
 *                        128+k   copy the pixel k rows above if its low 5 bits
 *                                are non-zero
 *                     This replays FontLib's 5 bpp packet unpacker (FontBlit5To8)
 *                     pixel for pixel, including its copy packets, which matter
 *                     where glyphs overlap (the Menu font's spacing is -2).
 *   Pixel values:  00aaa  foreground, aaa/8 coverage, against what is already
 *                         on screen
 *                  01aaa  outline/shadow colour, aaa/8 coverage, against what
 *                         is on screen
 *                  10aaa  foreground blended against the outline colour
 *   A cel byte is (p | p<<5) ^ 0xE0: low 5 bits = PLUT index, top 3 bits =
 *   "AMV" (7 - aaa), which the cel engine uses to scale the pixel already on
 *   screen (see sf_cel.c).
 *
 * The cel buffers here are kept as plain bytes while text is rendered, then
 * converted to the "word-swapped" model (32-bit words equal to the big-endian
 * words) that sf_cel.c reads.
 */
#include "sf_compat.h"
#include "sf_load.h"
#include "sf_video.h"
#include "sf_png.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FFLAG_TWOCOLOR 0x00000100u

#define TC_INTERNAL_AUTOSIZE_WIDTH 0x02000000u
#define TC_INTERNAL_AUTOSIZE_HEIGHT 0x04000000u
#define TC_INTERNAL_TWOCOLOR 0x08000000u

#define PIXC_OPAQUE 0x1F001F00u
#define PIXC_8BPP_BLEND                                                        \
  0x1F00bfc0u /* sf_cel.c recognises this word (text anti-aliasing) */

#define PRE0_VCNT_SHIFT 6
#define PRE0_BPP_8 0x00000005u
#define PRE1_TLHPCNT_SHIFT 0
#define PRE1_WOFFSET10_SHIFT 16
#define PRE1_TLLSB_PDC0 0x00001000u

#define PLUT_ENTRIES 32

/* ------------------------------------------------------------------ */
/* Font files                                                          */
/* ------------------------------------------------------------------ */
struct FontDescriptor {
  uint32_t flags, charHeight, charWidth, firstChar, lastChar;
  int32_t charExtra; /* spacing between characters (may be negative) */
  uint32_t ascent, descent, leading;
  uint8_t *ops; /* glyph strip, strip_w x charHeight */
  int32_t strip_w;
  int32_t x[256], width[256];
};

static uint32_t
be32(const uint8_t *p)
{
  return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
         ((uint32_t) p[2] << 8) | p[3];
}

static int
read_metrics(FontDescriptor *fd, const char *path)
{
  char line[256];
  int seen = 0;
  FILE *f = sf_fopen_resource(path, "rb");
  if (!f) {
    fprintf(stderr, "font: cannot open '%s'\n", path);
    return 0;
  }
  while (fgets(line, sizeof line, f)) {
    char key[32];
    long a, b, c;
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
      continue;
    }
    if (sscanf(line, "%ld %ld %ld", &a, &b, &c) == 3) {
      if (a < 0 || a > 255 || b < 0 || c < 0 || c > 255) {
        break;
      }
      fd->x[a] = (int32_t) b;
      fd->width[a] = (int32_t) c;
      continue;
    }
    if (sscanf(line, "%31s %ld", key, &a) != 2) {
      break;
    }
    seen++;
    if (!strcmp(key, "flags")) {
      fd->flags = (uint32_t) a;
    } else if (!strcmp(key, "height")) {
      fd->charHeight = (uint32_t) a;
    } else if (!strcmp(key, "widest")) {
      fd->charWidth = (uint32_t) a;
    } else if (!strcmp(key, "first")) {
      fd->firstChar = (uint32_t) a;
    } else if (!strcmp(key, "last")) {
      fd->lastChar = (uint32_t) a;
    } else if (!strcmp(key, "spacing")) {
      fd->charExtra = (int32_t) a;
    } else if (!strcmp(key, "ascent")) {
      fd->ascent = (uint32_t) a;
    } else if (!strcmp(key, "descent")) {
      fd->descent = (uint32_t) a;
    } else if (!strcmp(key, "leading")) {
      fd->leading = (uint32_t) a;
    } else {
      seen--;
    }
  }
  fclose(f);
  if (seen != 9 || fd->lastChar > 255 || fd->firstChar > fd->lastChar) {
    fprintf(stderr, "font: '%s' is not a valid metrics file\n", path);
    return 0;
  }
  return 1;
}

FontDescriptor *
LoadFont(const char *path, int memtype)
{
  char name[512];
  FontDescriptor *fd = (FontDescriptor *) calloc(1, sizeof *fd);
  int w = 0, h = 0;
  uint32_t c;
  (void) memtype;
  snprintf(name, sizeof name, "%s.txt", sf_rel(path));
  if (!read_metrics(fd, name)) {
    free(fd);
    return NULL;
  }
  snprintf(name, sizeof name, "%s.png", sf_rel(path));
  fd->ops = sf_png_load(name, 1, &w, &h);
  if (!fd->ops || h != (int) fd->charHeight) {
    fprintf(stderr, "font: cannot load glyphs '%s'\n", name);
    sf_png_free(fd->ops);
    free(fd);
    return NULL;
  }
  fd->strip_w = w;
  for (c = fd->firstChar; c <= fd->lastChar; c++) {
    if (fd->x[c] + fd->width[c] > w) {
      fprintf(stderr, "font: char %u lies outside '%s'\n", c, name);
      sf_png_free(fd->ops);
      free(fd);
      return NULL;
    }
  }
  return fd;
}

void
UnloadFont(FontDescriptor *fd)
{
  if (fd == NULL) {
    return;
  }
  sf_png_free(fd->ops);
  free(fd);
}

/* width of a character (0 if the font has no such character); *x = where its
 * glyph is in the strip */
static int32_t
char_info(const FontDescriptor *fd, uint32_t ch, uint32_t *x)
{
  if (ch < fd->firstChar || ch > fd->lastChar) {
    return 0;
  }
  if (x) {
    *x = (uint32_t) fd->x[ch];
  }
  return fd->width[ch];
}

/* Draw a glyph (strip column glyph_x, src_w x src_h) into an 8-bit coded cel
   buffer at (dst_x, dst_y) by replaying its pixel ops (see the top of the file). */
static void
draw_glyph(const FontDescriptor *fd, uint32_t glyph_x, int32_t src_w, int32_t src_h,
           uint8_t *dst_base, size_t dst_size, int32_t bpr, int32_t dst_x, int32_t dst_y)
{
  int32_t r, c;
  for (r = 0; r < src_h; r++) {
    const uint8_t *op = fd->ops + (size_t) r * (size_t) fd->strip_w + glyph_x;
    for (c = 0; c < src_w; c++) {
      ptrdiff_t d = (ptrdiff_t) (dst_y + r) * bpr + dst_x + c;
      uint32_t o = op[c];
      if (o == 0 || d < 0 || (size_t) d >= dst_size) {
        continue;
      }
      if (o < 32) {
        dst_base[d] = (uint8_t) (((o | (o << 5)) ^ 0xE0) & 0xFF);
      } else {
        ptrdiff_t from = d - (ptrdiff_t) (o - 128) * bpr;
        if (from >= 0 && (dst_base[from] & 0x1F)) {
          dst_base[d] = dst_base[from];
        }
      }
    }
  }
}

/* ------------------------------------------------------------------ */
/* Text cels                                                           */
/* ------------------------------------------------------------------ */

/* (Re)allocate the pixel buffers for a w x h 8-bit coded cel and set the CCB
   size / preamble fields. Returns the row size in bytes, or -1. */
static int32_t
alloc_text_celdata(TextCel *t, int32_t w, int32_t h)
{
  CCB *ccb = t->tc_CCB;
  int32_t row_bytes, row_woffset;
  size_t n;
  uint8_t *bytes;
  uint32_t *words;

  if (w < 1) {
    w = 1;
  }
  if (h < 1) {
    h = 1;
  }
  if (w > 2047) {
    w = 2047;
  }
  row_bytes = ((w * 8 + 31) / 32) * 4;
  if (row_bytes < 8) {
    row_bytes = 8; /* the cel engine's minimum of two words per row */
  }
  row_woffset = row_bytes / 4 - 2;

  n = (size_t) h * row_bytes;
  /* The pixel buffers are padded with TEXTCEL_PAD zero bytes beyond the cel.
     The game's typewriter reveal (SF_Control.c, SF_Message.c) sets the PRE1
     pixel count up to ccb_Width, i.e. one pixel more than the row holds; on
     the last row the cel engine then reads past the cel data (harmless on the
     3DO, a heap overrun here; found by ASan).  tc_size stays the cel size. */
#define TEXTCEL_PAD 8
  bytes = (uint8_t *) calloc(1, n + TEXTCEL_PAD);
  words = (uint32_t *) calloc(1, n + TEXTCEL_PAD);
  if (bytes == NULL || words == NULL) {
    free(bytes);
    free(words);
    return -1;
  }
  free(t->tc_bytes);
  free(t->tc_words);
  t->tc_bytes = bytes;
  t->tc_words = words;
  t->tc_size = n;
  ccb->ccb_SourcePtr = (CelData *) words;

  ccb->ccb_Width = w;
  ccb->ccb_Height = h;
  ccb->ccb_PRE0 = ((uint32_t) (h - 1) << PRE0_VCNT_SHIFT) | PRE0_BPP_8;
  ccb->ccb_PRE1 = ((uint32_t) (w - 1) << PRE1_TLHPCNT_SHIFT) | PRE1_TLLSB_PDC0 |
                  ((uint32_t) row_woffset << PRE1_WOFFSET10_SHIFT);
  return row_bytes;
}

static void
sync_words(TextCel *t)
{
  size_t i, nw = t->tc_size / 4;
  for (i = 0; i < nw; i++) {
    t->tc_words[i] = be32(t->tc_bytes + 4 * i);
  }
}

/* PLUT entries for anti-aliasing.  Group g (entries 8g..8g+7) holds the blends
 * for colour g. */
static void
recalc_colors(TextCel *t, int n_colors)
{
  uint16_t plut[PLUT_ENTRIES];
  int32_t ci, i;
  memset(plut, 0, sizeof plut);
  for (ci = 0; ci < n_colors; ci++) {
    uint32_t fg, bg, fr, fgn, fb;
    uint16_t *p = plut + ci * 8;
    if (ci == 2 && (t->tc_formatFlags & TC_INTERNAL_TWOCOLOR)) {
      fg = t->tc_fgColor[0];
      bg = t->tc_fgColor[1];
    } else {
      fg = t->tc_fgColor[ci];
      bg = t->tc_bgColor;
    }
    if (bg == 0 && fg == 0) {
      fg = MakeRGB15(0, 0, 1); /* never a transparent foreground */
    }
    fr = (fg >> 10) & 0x1F;
    fgn = (fg >> 5) & 0x1F;
    fb = fg & 0x1F;
    if (bg == 0) {
      for (i = 1; i < 8; i++) {
        uint32_t c =
            (((fr * i) >> 3) << 10) | (((fgn * i) >> 3) << 5) | ((fb * i) >> 3);
        p[i - 1] = (uint16_t) (c == 0 ? MakeRGB15(0, 0, 1) : c);
      }
    } else {
      uint32_t br = (bg >> 10) & 0x1F, bgn = (bg >> 5) & 0x1F, bb = bg & 0x1F;
      for (i = 1; i < 8; i++) {
        uint32_t inv = 8 - (uint32_t) i;
        p[i - 1] = (uint16_t) (0x8000u |
                               ((((fr * i) >> 3) + ((br * inv) >> 3)) << 10) |
                               ((((fgn * i) >> 3) + ((bgn * inv) >> 3)) << 5) |
                               (((fb * i) >> 3) + ((bb * inv) >> 3)));
      }
    }
    p[0] = (uint16_t) (0x8000u | bg);
    p[7] = (uint16_t) (0x8000u | fg);
  }
  for (i = 0; i < PLUT_ENTRIES / 2; i++) {
    t->tc_plut[i] = ((uint32_t) plut[2 * i] << 16) | plut[2 * i + 1];
  }
  if (t->tc_bgColor == 0) {
    t->tc_CCB->ccb_Flags &= ~CCB_BGND;
    t->tc_CCB->ccb_PIXC = PIXC_8BPP_BLEND;
  } else {
    t->tc_CCB->ccb_Flags |= CCB_BGND;
    t->tc_CCB->ccb_PIXC = PIXC_OPAQUE;
  }
}

TextCel *
CreateTextCel(FontDescriptor *fd, int formatFlags, int width, int height)
{
  TextCel *t;
  CCB *ccb;
  uint32_t flags = (uint32_t) formatFlags & 0xFFFFu;
  int32_t row_bytes;

  if (fd == NULL) {
    return NULL;
  }
  if (width == 0) {
    width = (int) fd->charWidth;
    flags |= TC_INTERNAL_AUTOSIZE_WIDTH;
  }
  if (height == 0) {
    height = (int) fd->charHeight;
    flags |= TC_INTERNAL_AUTOSIZE_HEIGHT;
  }
  if (fd->flags & FFLAG_TWOCOLOR) {
    flags |= TC_INTERNAL_TWOCOLOR;
  }

  t = (TextCel *) calloc(1, sizeof *t);
  ccb = (CCB *) calloc(1, sizeof *ccb);
  t->tc_plut = (uint32_t *) calloc(PLUT_ENTRIES / 2, sizeof(uint32_t));
  if (t == NULL || ccb == NULL || t->tc_plut == NULL) {
    free(t ? t->tc_plut : NULL);
    free(ccb);
    free(t);
    return NULL;
  }
  t->tc_CCB = ccb;
  ccb->ccb_PLUTPtr = t->tc_plut;
  ccb->ccb_HDX = 1 << 20;
  ccb->ccb_VDY = 1 << 16;
  ccb->ccb_PIXC = PIXC_OPAQUE;
  ccb->ccb_Flags = CCB_LAST | CCB_NPABS | CCB_SPABS | CCB_PPABS | CCB_LDSIZE |
                   CCB_LDPRS | CCB_LDPPMP | CCB_LDPLUT | CCB_CCBPRE | CCB_YOXY |
                   CCB_USEAV | CCB_NOBLK | CCB_ACE | CCB_ACW | CCB_ACCW;
  t->tc_fontDesc = fd;
  t->tc_formatFlags = flags;
  if ((row_bytes = alloc_text_celdata(t, width, height)) < 0) {
    free(t->tc_plut);
    free(ccb);
    free(t);
    return NULL;
  }
  t->tc_celRowBytes = row_bytes;
  t->tc_fgColor[0] = MakeRGB15(31, 31, 31);
  if (flags & TC_INTERNAL_TWOCOLOR) {
    t->tc_fgColor[1] = MakeRGB15(0, 0, 1);
  }
  recalc_colors(t, (flags & TC_INTERNAL_TWOCOLOR) ? 3 : 1);
  return t;
}

void
DeleteTextCel(TextCel *t)
{
  if (t == NULL) {
    return;
  }
  free(t->tc_bytes);
  free(t->tc_words);
  free(t->tc_plut);
  free(t->tc_CCB);
  free(t);
}

void
SetTextCelCoords(TextCel *t, int32_t x, int32_t y)
{
  if (x < 1024 && x > -1024 && y < 1024 && y > -1024) {
    t->tc_CCB->ccb_XPos = x << 16;
    t->tc_CCB->ccb_YPos = y << 16;
  } else {
    t->tc_CCB->ccb_XPos = x;
    t->tc_CCB->ccb_YPos = y;
  }
}

void
SetTextCelColor(TextCel *t, uint32_t bg, uint32_t fg)
{
  if ((int32_t) bg >= 0) {
    t->tc_bgColor = bg;
  }
  if ((int32_t) fg >= 0) {
    t->tc_fgColor[0] = fg;
  }
  recalc_colors(t, (t->tc_formatFlags & TC_INTERNAL_TWOCOLOR) ? 3 : 1);
}

void
GetTextCelSize(TextCel *t, int32_t *w, int32_t *h)
{
  if (w) {
    *w = t->tc_CCB->ccb_Width;
  }
  if (h) {
    *h = t->tc_CCB->ccb_Height;
  }
}

static void
erase_text(TextCel *t)
{
  memset(t->tc_bytes, 0, t->tc_size);
  memset(t->tc_words, 0, t->tc_size);
  t->tc_XPosInCel = t->tc_leftMargin;
  t->tc_YPosInCel = t->tc_topMargin;
}

/* width/height 0 = size to fit the text; negative = keep current */
void
SetTextCelSize(TextCel *t, int32_t w, int32_t h)
{
  int32_t row_bytes;
  const FontDescriptor *fd = t->tc_fontDesc;
  if (w < 0) {
    w = t->tc_CCB->ccb_Width;
  } else if (w == 0) {
    w = (int32_t) fd->charWidth;
    t->tc_formatFlags |= TC_INTERNAL_AUTOSIZE_WIDTH;
  } else {
    t->tc_formatFlags &= ~TC_INTERNAL_AUTOSIZE_WIDTH;
  }
  if (h < 0) {
    h = t->tc_CCB->ccb_Height;
  } else if (h == 0) {
    h = (int32_t) fd->charHeight;
    t->tc_formatFlags |= TC_INTERNAL_AUTOSIZE_HEIGHT;
  } else {
    t->tc_formatFlags &= ~TC_INTERNAL_AUTOSIZE_HEIGHT;
  }
  row_bytes = alloc_text_celdata(t, w, h);
  if (row_bytes > 0) {
    t->tc_celRowBytes = row_bytes;
    erase_text(t);
  }
}

/* Lay the text out (and, if render is set, draw it into the cel).
   Single-line-or-newline text with no word wrapping, which is all the game
   uses.  Returns the extent of the text. */
static void
layout_text(TextCel *t, const char *text, int render, int32_t *max_w,
            int32_t *max_h)
{
  const FontDescriptor *fd = t->tc_fontDesc;
  int32_t char_h = (int32_t) fd->charHeight;
  int32_t spacing = (int32_t) fd->charExtra + t->tc_fontAdjustSpacing;
  int32_t line_spacing = (int32_t) fd->leading + t->tc_fontAdjustLeading;
  int32_t right_edge =
      render ? t->tc_CCB->ccb_Width - t->tc_leftMargin : 0xffff;
  int32_t bottom_edge = render ? t->tc_CCB->ccb_Height : 0xffff;
  int32_t dst_x = render ? t->tc_XPosInCel : t->tc_leftMargin;
  int32_t dst_y = render ? t->tc_YPosInCel : t->tc_topMargin;
  int32_t line_count = 0, widest = 0;
  const char *line = text, *end = text + strlen(text);

  do {
    const char *c = line;
    int32_t line_chars = 0, line_w = 0;
    while (c < end && *c != '\n' && *c != '\r') {
      if (*c != '\t') {
        line_w += char_info(fd, (uint8_t) *c, NULL) + spacing;
      }
      c++;
      line_chars++;
    }
    if (line_w > 0) {
      line_w -= spacing;
    }
    if (line_chars != 0 || *c == '\n' || *c == '\r') {
      line_count++;
    }

    for (c = line; c < line + line_chars; c++) {
      if (*c == '\t') {
        continue;
      }
      {
        uint32_t off = 0;
        int32_t cw = char_info(fd, (uint8_t) *c, &off);
        if (render && cw > 0 && dst_x + cw <= right_edge &&
            dst_x >= t->tc_leftMargin && dst_y + char_h <= bottom_edge) {
          draw_glyph(fd, off, cw, char_h, t->tc_bytes, t->tc_size,
                     t->tc_celRowBytes, dst_x, dst_y);
        }
        dst_x += cw + spacing;
      }
    }
    if (line_w > widest) {
      widest = line_w;
    }

    line += line_chars;
    if (line < end && (*line == '\n' || *line == '\r')) {
      line++;
      dst_x = t->tc_leftMargin;
      dst_y += char_h + line_spacing;
    }
  } while (line < end && dst_y <= bottom_edge);

  if (render) {
    t->tc_XPosInCel = dst_x;
    t->tc_YPosInCel = dst_y;
  }
  if (max_w) {
    *max_w = (widest >= spacing) ? widest : 0;
  }
  if (max_h) {
    *max_h = (line_count > 0)
                 ? (char_h + line_spacing) * line_count - line_spacing
                 : 0;
  }
}

void
UpdateTextInCel(TextCel *t, int replace, const char *text)
{
  int autow = (t->tc_formatFlags & TC_INTERNAL_AUTOSIZE_WIDTH) != 0;
  int autoh = (t->tc_formatFlags & TC_INTERNAL_AUTOSIZE_HEIGHT) != 0;

  if (replace && (autow || autoh)) {
    int32_t cw, ch;
    layout_text(t, text, 0, &cw, &ch);
    cw += 2 * t->tc_leftMargin;
    ch += 2 * t->tc_topMargin;
    SetTextCelSize(t, autow ? cw : t->tc_CCB->ccb_Width,
                   autoh ? ch : t->tc_CCB->ccb_Height);
    if (autow) {
      t->tc_formatFlags |= TC_INTERNAL_AUTOSIZE_WIDTH;
    }
    if (autoh) {
      t->tc_formatFlags |= TC_INTERNAL_AUTOSIZE_HEIGHT;
    }
  }
  if (replace) {
    erase_text(t);
  }
  layout_text(t, text, 1, NULL, NULL);
  sync_words(t);
}

int
DrawTextString(FontDescriptor *fd, GrafCon *gc, int buffer, const char *text)
{
  TextCel *t = CreateTextCel(fd, 0, 0, 0);
  if (t == NULL) {
    return -1;
  }
  UpdateTextInCel(t, 1, text);
  SetTextCelColor(t, (uint32_t) gc->gc_BGPen, (uint32_t) gc->gc_FGPen);
  SetTextCelCoords(t, gc->gc_PenX, gc->gc_PenY);
  draw_cels(buffer, t->tc_CCB);
  gc->gc_PenX += t->tc_XPosInCel;
  gc->gc_PenY += t->tc_YPosInCel;
  DeleteTextCel(t);
  return 0;
}

/* ------------------------------------------------------------------ */
/* Port helpers (see sf_compat.h)                                      */
/* ------------------------------------------------------------------ */
void
textcel_hide(TextCel *t, int hidden)
{
  if (hidden) {
    t->tc_CCB->ccb_Flags |= CCB_SKIP;
  } else {
    t->tc_CCB->ccb_Flags &= ~CCB_SKIP;
  }
}

int
textcel_hidden(const TextCel *t)
{
  return (t->tc_CCB->ccb_Flags & CCB_SKIP) != 0;
}

void
textcel_last(TextCel *t, int last)
{
  if (last) {
    t->tc_CCB->ccb_Flags |= CCB_LAST;
  } else {
    t->tc_CCB->ccb_Flags &= ~CCB_LAST;
  }
}

int32_t
textcel_revealed(const TextCel *t)
{
  return (int32_t) (t->tc_CCB->ccb_PRE1 & 1023);
}

void
textcel_set_revealed(TextCel *t, int32_t pixels)
{
  t->tc_CCB->ccb_PRE1 = (t->tc_CCB->ccb_PRE1 & ~1023u) | (uint32_t) pixels;
}

int32_t
textcel_width(const TextCel *t)
{
  return t->tc_CCB->ccb_Width;
}

void
textcel_link(TextCel *a, TextCel *b)
{
  a->tc_CCB->ccb_NextPtr = b->tc_CCB;
}
