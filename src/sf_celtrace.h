/* sf_celtrace.h - record the cel lists drawn by the cel engine (test hook).
 *
 *   SF_CELTRACE=file          write every DrawCels list of the selected frames
 *   SF_CELTRACE_FRAMES=a-b    presented-frame range to record (default 1000-1009)
 *
 * The file can be replayed by tests/tools/celbench (timing, and a check that
 * the renderer still produces the recorded framebuffers).  Format (host byte
 * order, all fields uint32 unless noted):
 *
 *   "SFCELTR1"                         8-byte magic
 *   per list:
 *     'LIST' frame bitmap(0/1)
 *     framebuffers 0 and 1 before     2 x 320*240 uint16 (a cel may use the other one as source)
 *     count
 *     per CCB: flags xpos ypos hdx hdy vdx vdy hddx hddy pixc pre0 pre1 width height
 *              srckind (0 none, 1 data, 2 framebuffer 0, 3 framebuffer 1)
 *              nbytes, data[nbytes] (padded to a multiple of 4)
 *              hasplut, plut[16] if hasplut
 *     'DONE' framebuffer after        320*240 uint16
 */
#ifndef SF_CELTRACE_H
#define SF_CELTRACE_H
#include "sf_compat.h"

#define CELTRACE_MAGIC "SFCELTR1"
#define CELTRACE_LIST 0x5453494cu /* 'LIST' */
#define CELTRACE_DONE 0x454e4f44u /* 'DONE' */

/* non-zero while the current frame is being recorded */
int celtrace_active(void);
/* called by DrawCels before / after drawing a list into framebuffer 'fbidx' */
void celtrace_begin(int fbidx, const uint16_t *fb, const CCB *list,
                    const uint16_t *fb0, const uint16_t *fb1);
void celtrace_end(const uint16_t *fb);
/* number of bytes of cel source data a CCB refers to (preamble included),
   following the same rules as the renderer; 0 for framebuffer sources */
size_t celtrace_source_bytes(const CCB *c, const uint32_t *src);
#endif
