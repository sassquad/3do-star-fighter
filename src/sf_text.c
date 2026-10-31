/*
 * sf_text.c - the 3DO font library (FontLib + TextLib), using the game's own
 * font files.
 *
 * This replaces the 3DO SDK's FontLib.c / FontBlit5To8_.s / TextLib.c.  The
 * file format and the rendering rules below were taken from the SDK sources
 * (3DO Toolkit 1.5, Lib3DO.lib/TextLib).
 *
 * FONT FILE (all fields big-endian 32-bit words)
 *   0 'FONT'   1 chunk size   2 version     3 font flags   4 char height   5
 * widest char 6 bits per pixel (5 for the game's fonts)   7 first char   8 last
 * char   9 char extra spacing 10 ascent  11 descent     12 leading    13 offset
 * of char info table   14 size of that table 15 offset of char data    16 size
 * of char data    17..20 reserved Char info table: one word per character,
 * (offset into char data << 10) | width in pixels. Font flags: 0x100 =
 * two-colour font (pixels carry an outline/shadow colour as well).
 *
 * GLYPH DATA (5 bpp, compressed).  Each glyph is a stream of 16-bit packets,
 * two per big-endian word, unpacked into an 8-bit coded cel buffer by
 * blit_char5(): 1ppppppppppppppp   output 3 pixels (three 5-bit values), then
 * check for end of line 0100xxxxcccccccc   skip cccccccc blank lines
 *      0010xxcccccppppp   repeat pixel ppppp ccccc times
 *      0001xxxxcccccnnn   go up nnn+1 lines, copy ccccc pixels
 *      0000xxpppppppppp   output up to 2 pixels, check for end of line after
 * each 0000000000000000   early end of line Pixel values:  00aaa  foreground,
 * aaa/8 coverage, against what is already on screen 01aaa  outline/shadow
 * colour, aaa/8 coverage, against what is on screen 10aaa  foreground blended
 * against the outline colour A destination byte is  (p | p<<5) ^ 0xE0:  low 5
 * bits = PLUT index, top 3 bits = "AMV" (7 - aaa) which the cel engine uses to
 * scale the pixel already on screen (see sf_cel.c).
 *
 * The cel buffers here are kept as plain bytes while text is rendered, then
 * converted to the "word-swapped" model (32-bit words equal to the big-endian
 * words) that sf_cel.c reads.
 */
#include "sf_compat.h"
#include "sf_load.h"
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
  uint32_t flags, charHeight, charWidth, bitsPerPixel, firstChar, lastChar;
  uint32_t charExtra, ascent, descent, leading;
  uint8_t *file; /* the whole font file, as stored (big-endian) */
  size_t size;
  const uint8_t *charInfo;
  const uint8_t *charData;
};

static uint32_t
be32(const uint8_t *p)
{
  return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
         ((uint32_t) p[2] << 8) | p[3];
}

FontDescriptor *
LoadFont(const char *path, int memtype)
{
  FontDescriptor *fd;
  FILE *f;
  uint8_t *data;
  long size;
  uint32_t h[17];
  int i;
  (void) memtype;

  f = sf_fopen_resource(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "font: cannot open '%s'\n", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 84) {
    fclose(f);
    fprintf(stderr, "font: '%s' is too small\n", path);
    return NULL;
  }
  data = (uint8_t *) malloc((size_t) size);
  if (data == NULL || fread(data, 1, (size_t) size, f) != (size_t) size) {
    fclose(f);
    free(data);
    fprintf(stderr, "font: cannot read '%s'\n", path);
    return NULL;
  }
  fclose(f);

  for (i = 0; i < 17; i++) {
    h[i] = be32(data + 4 * i);
  }
  if (h[0] != 0x464F4E54u /* 'FONT' */) {
    fprintf(stderr, "font: '%s' is not a font file\n", path);
    free(data);
    return NULL;
  }
  if (h[6] != 5) {
    fprintf(stderr,
            "font: '%s' is %u bits per pixel; only the 5 bpp format used by "
            "Star Fighter is supported\n",
            path, h[6]);
    free(data);
    return NULL;
  }
  if ((uint64_t) h[13] + h[14] > (uint64_t) size ||
      (uint64_t) h[15] + h[16] > (uint64_t) size || h[8] < h[7] ||
      (uint64_t) (h[8] - h[7] + 1) * 4 > h[14]) {
    fprintf(stderr, "font: '%s' has an invalid layout\n", path);
    free(data);
    return NULL;
  }

  fd = (FontDescriptor *) calloc(1, sizeof *fd);
  fd->flags = h[3];
  fd->charHeight = h[4];
  fd->charWidth = h[5];
  fd->bitsPerPixel = h[6];
  fd->firstChar = h[7];
  fd->lastChar = h[8];
  fd->charExtra = h[9];
  fd->ascent = h[10];
  fd->descent = h[11];
  fd->leading = h[12];
  fd->file = data;
  fd->size = (size_t) size;
  fd->charInfo = data + h[13];
  fd->charData = data + h[15];
  return fd;
}

void
UnloadFont(FontDescriptor *fd)
{
  if (fd == NULL) {
    return;
  }
  free(fd->file);
  free(fd);
}

/* width of a character (0 if the font has no such character); *offset = glyph
 * offset in char data */
static int32_t
char_info(const FontDescriptor *fd, uint32_t ch, uint32_t *offset)
{
  uint32_t v;
  if (ch < fd->firstChar || ch > fd->lastChar) {
    return 0;
  }
  v = be32(fd->charInfo + 4 * (ch - fd->firstChar));
  if (offset) {
    *offset = v >> 10;
  }
  return (int32_t) (v & 0xFF);
}

/* ------------------------------------------------------------------ */
/* Glyph unpacker: C version of FontBlit5To8_.s                        */
/* ------------------------------------------------------------------ */
static void
blit_char5(const FontDescriptor *fd, uint32_t glyph_offset, int32_t src_w,
           int32_t src_h, uint8_t *dst_base, size_t dst_size, int32_t bpr,
           int32_t dst_x, int32_t dst_y)
{
  const uint8_t *s = fd->charData + glyph_offset;
  const uint8_t *s_end = fd->file + fd->size;
  uint8_t *dst = dst_base + (size_t) dst_y * bpr + dst_x; /* r3 */
  uint8_t *dst_end = dst_base + dst_size;
  int32_t xcount = src_w; /* r6 */
  int32_t rows = src_h;   /* r2 */

#define PIXBYTE(p) ((uint8_t) ((((p) | ((p) << 5)) ^ 0xE0) & 0xFF))
#define PUT(p)                                                                 \
  do {                                                                         \
    uint32_t p_ = (p);                                                         \
    if (dst >= dst_base && dst < dst_end && p_ != 0)                           \
      *dst = PIXBYTE(p_);                                                      \
    dst++;                                                                     \
  } while (0)

  for (;;) {
    uint32_t pair;
    int half;
    if (s + 4 > s_end) {
      return;
    }
    pair = be32(s);
    s += 4;
    for (half = 0; half < 2; half++) {
      uint32_t r10 =
          half ? (pair << 16)
               : pair; /* packet being handled is in the top 16 bits */
      int next_row = 0;

      if (r10 & 0x80000000u) { /* write3 */
        PUT((r10 >> 26) & 0x1F);
        PUT((r10 >> 21) & 0x1F);
        PUT((r10 >> 16) & 0x1F);
        xcount -= 3;
        if (xcount == 0) {
          next_row = 1;
        }
      } else if (r10 & 0x40000000u) { /* blankrows */
        int32_t n = (int32_t) ((r10 >> 16) & 0xFF);
        rows -= n;
        if (rows == 0) {
          return;
        }
        dst += (ptrdiff_t) n * bpr;
        xcount = src_w;
      } else if (r10 & 0x20000000u) { /* litrun */
        int32_t count = (int32_t) ((r10 >> 21) & 0x1F);
        uint32_t p = (r10 >> 16) & 0x1F;
        xcount -= count;
        if (p == 0) {
          dst += count;
        } else {
          int32_t k;
          for (k = 0; k < count; k++) {
            PUT(p);
          }
        }
        if (xcount == 0) {
          next_row = 1;
        }
      } else if (r10 & 0x10000000u) { /* copyrun */
        int32_t lines = (int32_t) ((r10 >> 16) & 7) + 1;
        int32_t count = (int32_t) ((r10 >> 19) & 0x1F), k;
        const uint8_t *from = dst - (ptrdiff_t) lines * bpr;
        xcount -= count;
        for (k = 0; k < count; k++, from++) {
          if (from >= dst_base && from < dst_end && dst >= dst_base &&
              dst < dst_end && (*from & 0x1F)) {
            *dst = *from;
          }
          dst++;
        }
        if (xcount == 0) {
          next_row = 1;
        }
      } else { /* write2 */
        if ((r10 >> 16) == 0) {
          next_row = 1; /* early end of line */
        } else {
          PUT((r10 >> 21) & 0x1F);
          if (--xcount == 0) {
            next_row = 1;
          } else {
            PUT((r10 >> 16) & 0x1F);
            if (--xcount == 0) {
              next_row = 1;
            }
          }
        }
      }

      if (next_row) {
        rows -= 1;
        if (rows == 0) {
          return;
        }
        dst += (bpr - src_w) + xcount;
        xcount = src_w;
      }
    }
  }
#undef PUT
#undef PIXBYTE
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
  bytes = (uint8_t *) calloc(1, n);
  words = (uint32_t *) calloc(1, n);
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
          blit_char5(fd, off, cw, char_h, t->tc_bytes, t->tc_size,
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
DrawTextString(FontDescriptor *fd, GrafCon *gc, Item bitmap, const char *text)
{
  TextCel *t = CreateTextCel(fd, 0, 0, 0);
  if (t == NULL) {
    return -1;
  }
  UpdateTextInCel(t, 1, text);
  SetTextCelColor(t, (uint32_t) gc->gc_BGPen, (uint32_t) gc->gc_FGPen);
  SetTextCelCoords(t, gc->gc_PenX, gc->gc_PenY);
  DrawCels(bitmap, t->tc_CCB);
  gc->gc_PenX += t->tc_XPosInCel;
  gc->gc_PenY += t->tc_YPosInCel;
  DeleteTextCel(t);
  return 0;
}
