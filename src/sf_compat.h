/*
 * sf_compat.h - 3DO / Portfolio OS compatibility layer for the SDL2 port.
 *
 * The original game was written for the 3DO (32-bit big-endian ARM60, armcc).
 * Differences that this port has to deal with:
 *
 *   - 'long' is 32 bits on 3DO but 64 bits on x86-64 Linux.  All uses of
 *     'long' in the game sources were rewritten to int32_t / uint32_t.
 *   - plain 'char' is unsigned under armcc; the Makefile uses -funsigned-char.
 *   - the ARM ran big-endian.  Binary resource files are big-endian, and the
 *     rendering code was written around 32-bit words.  Resource blobs that are
 *     accessed as words are therefore byte-swapped into native uint32_t words
 *     when loaded ("word-swapped" memory model, see sf_load.c).  A byte at
 *     file offset N of such a blob lives at memory offset (N ^ 3) on a
 *     little-endian host; use SF_BYTE() for that.
 *   - pointers are 8 bytes.  Anywhere the original stored a pointer in a
 *     32-bit word (graphics relocation, ship ownership, mission data...) the
 *     field has been widened or turned into an offset.
 */
#ifndef SF_COMPAT_H
#define SF_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *) 0)
#endif

typedef unsigned char ubyte;
typedef unsigned char uint8;
typedef int32_t Item;
typedef int32_t Boolean;

/* ------------------------------------------------------------------ */
/* Endianness helpers                                                  */
/* ------------------------------------------------------------------ */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SF_BIG_ENDIAN 1
#define SF_BYTE(base, off) (((uint8_t *) (base))[(off)])
#else
#define SF_BIG_ENDIAN 0
/* byte at big-endian file offset 'off' of a word-swapped blob */
#define SF_BYTE(base, off) (((uint8_t *) (base))[(off) ^ 3])
#endif

static inline uint32_t
sf_bswap32(uint32_t v)
{
  return (v >> 24) | ((v >> 8) & 0xff00u) | ((v << 8) & 0xff0000u) | (v << 24);
}

static inline uint16_t
sf_bswap16(uint16_t v)
{
  return (uint16_t) ((v >> 8) | (v << 8));
}

/* Convert 'bytes' bytes (multiple of 4) of big-endian words to native words in
 * place */
void sf_swap_words(void *buf, size_t bytes);

/* ------------------------------------------------------------------ */
/* Cel Control Block (native layout - the hardware layout used 32-bit  */
/* pointers, so this is NOT binary compatible with 3DO CCBs)           */
/* ------------------------------------------------------------------ */
#define CCB_SKIP 0x80000000u
#define CCB_LAST 0x40000000u
#define CCB_NPABS 0x20000000u
#define CCB_SPABS 0x10000000u
#define CCB_PPABS 0x08000000u
#define CCB_LDSIZE 0x04000000u
#define CCB_LDPRS 0x02000000u
#define CCB_LDPPMP 0x01000000u
#define CCB_LDPLUT 0x00800000u
#define CCB_CCBPRE 0x00400000u
#define CCB_YOXY 0x00200000u
#define CCB_ACSC 0x00100000u
#define CCB_ALSC 0x00080000u
#define CCB_ACW 0x00040000u
#define CCB_ACCW 0x00020000u
#define CCB_TWD 0x00010000u
#define CCB_LCE 0x00008000u
#define CCB_ACE 0x00004000u
#define CCB_MARIA 0x00001000u
#define CCB_PXOR 0x00000800u
#define CCB_USEAV 0x00000400u
#define CCB_PACKED 0x00000200u
#define CCB_POVER_MASK 0x00000180u
#define CCB_PLUTPOS 0x00000040u
#define CCB_BGND 0x00000020u
#define CCB_NOBLK 0x00000010u

typedef uint32_t CelData;

typedef struct CCB {
  uint32_t ccb_Flags;
  struct CCB *ccb_NextPtr;
  CelData *ccb_SourcePtr;
  void *ccb_PLUTPtr;
  int32_t ccb_XPos, ccb_YPos;
  int32_t ccb_HDX, ccb_HDY;
  int32_t ccb_VDX, ccb_VDY;
  int32_t ccb_HDDX, ccb_HDDY;
  uint32_t ccb_PIXC;
  uint32_t ccb_PRE0, ccb_PRE1;
  int32_t ccb_Width, ccb_Height;
} CCB;

/* ------------------------------------------------------------------ */
/* Screens                                                             */
/* ------------------------------------------------------------------ */
#define SF_SCREEN_W 320
#define SF_SCREEN_H 240

typedef struct Bitmap {
  int32_t bm_Width, bm_Height;
  uint16_t *bm_Buffer; /* linear 320x240, 0RRRRRGGGGGBBBBB, host order */
} Bitmap;

typedef struct ScreenContext {
  int32_t sc_nScreens;
  int32_t sc_curScreen;
  int32_t sc_nFrameBufferPages;
  Item sc_Screens[2];
  Item sc_BitmapItems[2];
  Bitmap *sc_Bitmaps[2];
} ScreenContext;

typedef struct Rect {
  int32_t rect_XLeft, rect_YTop, rect_XRight, rect_YBottom;
} Rect;

typedef struct GrafCon {
  int32_t gc_PenX, gc_PenY;
  int32_t gc_FGPen, gc_BGPen;
} GrafCon;

#define MakeRGB15(r, g, b) ((((r) & 31) << 10) | (((g) & 31) << 5) | ((b) & 31))

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */
typedef struct FontDescriptor FontDescriptor;

typedef struct TextCel {
  CCB *tc_CCB;
  struct FontDescriptor *tc_fontDesc;
  uint32_t tc_formatFlags;
  uint32_t tc_bgColor, tc_fgColor[2];
  int32_t tc_leftMargin, tc_topMargin;
  int32_t tc_XPosInCel, tc_YPosInCel;
  int32_t tc_fontAdjustSpacing, tc_fontAdjustLeading;
  int32_t tc_celRowBytes;
  uint8_t *tc_bytes;  /* cel pixels as plain bytes while text is drawn */
  uint32_t *tc_words; /* same, as the big-endian words the cel engine reads */
  size_t tc_size;
  void *tc_userData;
  uint32_t *tc_plut; /* 16 words = 32 PLUT entries */
} TextCel;

/* ------------------------------------------------------------------ */
/* Control pad                                                         */
/* ------------------------------------------------------------------ */
#define ControlDown 0x80000000u
#define ControlUp 0x40000000u
#define ControlRight 0x20000000u
#define ControlLeft 0x10000000u
#define ControlA 0x08000000u
#define ControlB 0x04000000u
#define ControlC 0x02000000u
#define ControlStart 0x01000000u
#define ControlX 0x00800000u
#define ControlRightShift 0x00400000u
#define ControlLeftShift 0x00200000u

typedef struct ControlPadEventData {
  uint32_t cped_ButtonBits;
} ControlPadEventData;

/* ------------------------------------------------------------------ */
/* Shim API (implemented in sf_platform.c / sf_cel.c / sf_load.c ...) */
/* ------------------------------------------------------------------ */
int OpenGraphics(ScreenContext *sc, int nScreens);
void CloseGraphics(ScreenContext *sc);
int DisplayScreen(Item screenItem, Item unused);
void DrawCels(Item bitmapItem, CCB *ccb);
void FillRect(Item bitmapItem, GrafCon *gc, Rect *r);
void FadeToBlack(ScreenContext *sc, int frames);
void FadeFromBlack(ScreenContext *sc, int frames);
#define EnableVAVG(x) ((void) 0)
#define EnableHAVG(x) ((void) 0)
#define SetCEControl(a, b, c) ((void) 0)
Item GetVRAMIOReq(void);
Item GetVBLIOReq(void);
Item GetTimerIOReq(void);
#define DeleteIOReq(x) ((void) (x))
void WaitVBL(Item req, int frames);
void WaitVBLDefer(Item req, int frames);
void WaitIO(Item req);
void CopyVRAMPages(Item req, void *dst, void *src, int pages, int mask);
void SetVRAMPages(Item req, void *dst, int value, int pages, int mask);
void SetScreenColor(Item screen, uint32_t entry);
#define MakeCLUTColorEntry(i, r, g, b)                                         \
  (((uint32_t) (i) << 24) | ((r) << 16) | ((g) << 8) | (b))

/* 3DO mem */
#define MEMTYPE_ANY 0
#define MEMTYPE_CEL 0
#define MEMTYPE_FILL 0
#define AllocMem(n, t) calloc(1, (n))
#define ALLOCMEM(n, t) calloc(1, (n))
#define FreeMem(p, n) free(p)
/* OS items that no longer exist */

/* controller + misc */
uint32_t sf_pad_read(void); /* current button bits */
int GetControlPad(int n, int wait, ControlPadEventData *d);

/* text */
FontDescriptor *LoadFont(const char *path, int memtype);
void UnloadFont(FontDescriptor *f);
TextCel *CreateTextCel(FontDescriptor *f, int a, int b, int c);
void DeleteTextCel(TextCel *t);
void SetTextCelCoords(TextCel *t, int32_t x, int32_t y);
void SetTextCelColor(TextCel *t, uint32_t bg, uint32_t fg);
void SetTextCelSize(TextCel *t, int32_t w, int32_t h);
void GetTextCelSize(TextCel *t, int32_t *w, int32_t *h);
void UpdateTextInCel(TextCel *t, int fUpdate, const char *text);
#define SetFGPen(gc, c) ((gc)->gc_FGPen = (int32_t) (c))
#define SetBGPen(gc, c) ((gc)->gc_BGPen = (int32_t) (c))
int DrawTextString(FontDescriptor *f, GrafCon *gc, Item bitmap, const char *s);

#endif
