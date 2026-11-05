#include "sf_compat.h"
//	File : SF_ARMLink (Header file for linked list routines)

#ifndef __SF_ARMLINK
#define __SF_ARMLINK

// Includes

#include "SF_Allocation.h"

// Structure definitions for general linked list routine

/* NOTE: the 3DO layout was 3 x 32-bit words.  With 64-bit pointers these grow
   to 24 bytes; nothing outside SF_ARMLink.c depends on the numeric layout any
   more. */
typedef struct linked_list {
  void *start_address;    // First item (start of the chain, active items first)
  void *next_freeaddress; // First free item
  int32_t sortword; // Byte offset (from start of item) of the 32-bit sort value
} linked_list;

typedef struct link_header {
  void *prev_address;
  void *next_address;
  int32_t status; // 0 = free, 1 = active, 2 = end-of-list sentinel
} link_header;

typedef struct zsort_item {
  int32_t distance;
  int32_t ref;
  void *graphic_address;
  struct zsort_item *next_address;
} zsort_item;

typedef struct zsort_list {
  zsort_item *offset[128];
  zsort_item graphic[MAX_GRAPHICPLOT_ALL]; /* MAX_GRAPHICPLOT used at the defaults */
} zsort_list;

extern void armlink_initialise(int32_t, void *, int32_t,
                               int32_t); // Setup an empty linked list
extern void *armlink_addtolist(void *);  // Add an item to the list
extern void *armlink_sorttolist(void *,
                                int32_t); // Add an item to the list, sorted
extern void armlink_resortlist(void *);   // Resort a list into ascending order
extern void armlink_deleteitem(void *, void *); // Delete an item from a list
extern void armlink_suspenditem(void *,
                                void *); // Suspend an item from the linked list
extern void armlink_releaseitem(void *,
                                void *);      // Release an item from suspension
extern void armzsort_create(void *, int32_t); // Create a list
extern void armzsort_initialise(void);
extern void armzsort_clear(void); // Initialise zsort list a frame start
extern void armzsort_add(int32_t, void *, int32_t); // Add a graphic to the list
extern intptr_t *armcol_collisioncheck(intptr_t *, void *,
                                       void *); // Collision Check

#endif
