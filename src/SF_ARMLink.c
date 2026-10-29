/* SF_ARMLink.c - C translation of SF_ARMLink.s
 *
 * Doubly linked lists of fixed-size items held in one block of memory:
 *     [ linked_list header ][ item 0 ][ item 1 ] ... [ item max-1 ]
 * Every item starts with a link_header.  Active items (status 1) come first in the chain,
 * sorted if the list is sorted; free items (status 0) follow; the last item of the block
 * is a sentinel with status 2.  'next_freeaddress' points at the first free item.
 *
 * The statement order of the original is kept deliberately: some cases (e.g. deleting the
 * only active item) rely on temporarily aliased pointers being repaired by later stores.
 */
#include "SF_ARMLink.h"
#include "Ship_Struct.h"
#include "Laser_Struct.h"

#define MIN_I32 ((int32_t) 0x80000000)

static link_header *H (void *p) { return (link_header *) p; }
static int32_t sortval (void *item, int32_t sortword) { return *(int32_t *)((char *) item + sortword); }

void armlink_initialise (int32_t max, void *listp, int32_t itemsize, int32_t sortoffset)
{
	linked_list *list = (linked_list *) listp;
	char *item, *next;
	int32_t n = max - 1;
	if (n < 0) return;
	item = (char *) listp + sizeof (linked_list);
	list->start_address = item;
	list->next_freeaddress = item;
	list->sortword = (int32_t) sizeof (link_header) + sortoffset;
	H (item)->prev_address = NULL;
	H (item)->status = 0;
	next = item + itemsize;
	H (item)->next_address = next;
	for (;;) {
		if (--n < 0) break;
		H (next)->prev_address = item;
		item += itemsize;
		H (item)->status = 0;
		next = item + itemsize;
		H (item)->next_address = next;
	}
	H (item)->next_address = NULL;
	H (item)->status = 2;
}

void *armlink_addtolist (void *listp)
{
	linked_list *list = (linked_list *) listp;
	link_header *item = H (list->next_freeaddress);
	if (item->status == 2) return NULL;
	item->status = 1;
	list->next_freeaddress = item->next_address;
	return item;
}

void *armlink_sorttolist (void *listp, int32_t value)
{
	linked_list *list = (linked_list *) listp;
	int32_t sw = list->sortword, v;
	link_header *r3 = H (list->next_freeaddress), *r0, *r1, *r5, *r6;

	if (r3->status == 2) return NULL;				/* no room */
	r3->status = 1;
	r5 = H (r3->prev_address);
	r6 = H (r3->next_address);
	list->next_freeaddress = r6;

	r0 = H (r3->prev_address);
	v = r0 ? sortval (r0, sw) : MIN_I32;
	if (v <= value) return r3;						/* already in the right place */

	do {											/* search back for an item <= value */
		r0 = H (r0->prev_address);
		v = r0 ? sortval (r0, sw) : MIN_I32;
	} while (v > value);

	if (r0 == NULL) {								/* becomes the new start of the list */
		link_header *p = H (r3->prev_address), *n = H (r3->next_address);
		n->prev_address = p;						/* unlink from current position */
		p->next_address = n;
		r3->prev_address = NULL;
		r1 = H (list->start_address);
		list->start_address = r3;
		r1->prev_address = r3;
		r3->next_address = r1;
		return r3;
	}
	r3->prev_address = r0;							/* insert after r0 */
	r1 = H (r0->next_address);
	r0->next_address = r3;
	r3->next_address = r1;
	r1->prev_address = r3;
	r5->next_address = r6;							/* close the gap where r3 used to be */
	r6->prev_address = r5;
	return r3;
}

void armlink_resortlist (void *listp)
{
	linked_list *list = (linked_list *) listp;
	int32_t sw = list->sortword, r3 = MIN_I32, r2, r8;
	link_header *r0 = H (list->start_address), *r4, *r5, *r6, *r8p;

	for (;;) {
		if (r0->status != 1) return;
		r2 = sortval (r0, sw);
		if (r2 >= r3) {								/* in order: move on */
			if (r2 > r3) r3 = r2;
			r0 = H (r0->next_address);
			continue;
		}
		r5 = H (r0->prev_address);					/* out of order: bubble back */
		r4 = H (r5->prev_address);
		r8 = r4 ? sortval (r4, sw) : MIN_I32;
		if (r2 >= r8) {								/* swap with the single previous item */
			r8p = H (r0->next_address);
			r0->prev_address = r4; r0->next_address = r5;
			r5->prev_address = r0; r5->next_address = r8p;
			if (r4) r4->next_address = r0; else list->start_address = r0;
			r8p->prev_address = r5;
			r3 = r2;
			continue;
		}
		do {										/* find the insertion point further back */
			r5 = r4;
			r4 = H (r5->prev_address);
			r8 = r4 ? sortval (r4, sw) : MIN_I32;
		} while (r2 < r8);
		r6 = H (r0->prev_address);
		r8p = H (r0->next_address);
		r0->prev_address = r4; r0->next_address = r5;
		r5->prev_address = r0;
		r6->next_address = r8p;
		if (r4) r4->next_address = r0; else list->start_address = r0;
		r8p->prev_address = r6;
		r3 = r2;
	}
}

void armlink_deleteitem (void *itemp, void *listp)
{
	link_header *r0 = H (itemp), *r2, *r3, *r4;
	linked_list *list = (linked_list *) listp;

	r3 = H (list->start_address);
	r2 = H (r0->prev_address);
	r4 = H (r0->next_address);
	if (r0 != r3) {
		r2->next_address = r4;
		r4->prev_address = r2;
	} else {
		r0->prev_address = r2;
		if (r4->status == 1) {
			r4->prev_address = r2;
			list->start_address = r4;
		}
	}
	r3 = H (list->next_freeaddress);
	list->next_freeaddress = r0;
	r2 = H (r3->prev_address);
	r2->next_address = r0;
	r0->prev_address = r2;
	r0->next_address = r3;
	r3->prev_address = r0;
	r0->status = 0;
	H (list->start_address)->prev_address = NULL;
}

void armlink_suspenditem (void *itemp, void *listp)
{
	link_header *r0 = H (itemp), *r2 = H (r0->prev_address), *r3 = H (r0->next_address);
	linked_list *list = (linked_list *) listp;
	r3->prev_address = r2;
	if (r2) r2->next_address = r3; else list->start_address = r3;
}

void armlink_releaseitem (void *itemp, void *listp)
{
	link_header *r0 = H (itemp), *r3;
	linked_list *list = (linked_list *) listp;
	r3 = H (list->start_address);
	list->start_address = r0;
	r0->prev_address = NULL;
	r0->next_address = r3;
	r3->prev_address = r0;
}

/* ------------------------------------------------------------------ */
/* Z sort                                                              */
/* ------------------------------------------------------------------ */
static zsort_list *zs_list;
static zsort_item *zs_next;
static int32_t     zs_maxreset, zs_count;

void armzsort_create (void *listp, int32_t maxitems)
{
	int i;
	zs_list = (zsort_list *) listp;
	zs_maxreset = maxitems;
	for (i = 0; i < 128; i++) zs_list->offset [i] = NULL;
}

void armzsort_initialise (void)
{
	zs_next = &zs_list->graphic [0];
	zs_count = zs_maxreset;
}

void armzsort_add (int32_t distance, void *graphic, int32_t ref)
{
	zsort_item *r4, *r6, *entry;
	int32_t idx;

	if (--zs_count < 0) { zs_count++; return; }		/* subs/movmi: note count is only stored on success */
	idx = distance >> 24;
	r4 = zs_list->offset [idx];
	entry = zs_next;
	if (r4 == NULL) {								/* empty group */
		zs_list->offset [idx] = entry;
		entry->distance = distance; entry->graphic_address = graphic; entry->ref = ref; entry->next_address = NULL;
		zs_next++;
		return;
	}
	if (distance >= r4->distance) {					/* goes at the head of the group */
		zs_list->offset [idx] = entry;
		entry->distance = distance; entry->graphic_address = graphic; entry->ref = ref; entry->next_address = r4;
		zs_next++;
		return;
	}
	for (;;) {
		r6 = r4;
		r4 = r4->next_address;
		if (r4 == NULL) {							/* goes at the end */
			r6->next_address = entry;
			entry->distance = distance; entry->graphic_address = graphic; entry->ref = ref; entry->next_address = NULL;
			zs_next++;
			return;
		}
		if (distance < r4->distance) continue;
		entry->distance = distance; entry->graphic_address = graphic; entry->ref = ref; entry->next_address = r6->next_address;
		r6->next_address = entry;
		zs_next++;
		return;
	}
}

/* ------------------------------------------------------------------ */
/* Collision check: lasers v ships, ships v ships                      */
/* ------------------------------------------------------------------ */
static int32_t absdiff (int32_t a, int32_t b) { int32_t d = (int32_t)((uint32_t) a - (uint32_t) b); return d < 0 ? -d : d; }

intptr_t *armcol_collisioncheck (intptr_t *stack, void *laserlist, void *shiplist)
{
	/* Register names follow the assembler:  r1 laser, r2 ship, r11 other ship / laser X,
	   r10 ship being tested against a laser, r3-r5 ship X,Y,Z, r6 catchment, r7-r9/r14 saved copies */
	laser_stack *r1 = (laser_stack *) ((linked_list *) laserlist)->start_address;
	ship_stack  *r2 = (ship_stack *)  ((linked_list *) shiplist)->start_address;
	ship_stack  *r10, *r11s;
	int32_t r3, r4, r5, r6, r7, r8, r9, r14, r11, r12, lim;

armcol_newship:
	if (r2->header.status != 1) return stack;
	r11s = (ship_stack *) r2->header.next_address;
	r3 = r2->x_pos; r4 = r2->y_pos; r5 = r2->z_pos;
	r6 = r2->collision_size;

armcol_newshipcheck:
	if (r11s->header.status != 1) goto armcol_newlaser;
	lim = r11s->collision_size + r6;
	if (absdiff (r11s->x_pos, r3) > lim) goto armcol_newlaser;
	if (absdiff (r11s->y_pos, r4) > lim) { r11s = (ship_stack *) r11s->header.next_address; goto armcol_newshipcheck; }
	if (absdiff (r11s->z_pos, r5) > lim) { r11s = (ship_stack *) r11s->header.next_address; goto armcol_newshipcheck; }
	{
		intptr_t o1 = r11s->who_owns_me, o2 = r2->who_owns_me;
		if (o1 != o2 && o1 != (intptr_t) r2 && o2 != (intptr_t) r11s) {
			r11s->what_hit_me = (intptr_t) r2;
			r2->what_hit_me = (intptr_t) r11s;
		}
	}
	r11s = (ship_stack *) r11s->header.next_address;
	goto armcol_newshipcheck;

armcol_newlaserrestorenext:
	r3 = r7; r4 = r8; r5 = r9; r6 = r14;
	r1 = (laser_stack *) r1->header.next_address;

armcol_newlaser:
	if (r1->header.status != 1) { r2 = (ship_stack *) r2->header.next_address; goto armcol_newship; }
	r11 = r1->x_pos;
	if (absdiff (r3, r11) <= r6) goto armcol_laserwithin;
	if (r11 > r3) { r2 = (ship_stack *) r2->header.next_address; goto armcol_newship; }
	r1 = (laser_stack *) r1->header.next_address;
	goto armcol_newlaser;

armcol_laserwithin:
	r10 = r2;
	r7 = r3; r8 = r4; r9 = r5; r14 = r6;

armcol_checknext:
	if (absdiff (r11, r3) > r6) goto armcol_newlaserrestorenext;
	if (absdiff (r1->y_pos, r4) > r6) goto armcol_next;
	if (absdiff (r1->z_pos, r5) > r6) goto armcol_next;
	r12 = (int32_t) 0;
	{
		intptr_t owner = r1->who_owns_me;
		if (owner <= 16384) goto armcol_registercollision;			/* ground object */
		if ((intptr_t) r10 == owner) goto armcol_next;				/* laser owner is the ship hit */
		if (r10->who_owns_me == owner) goto armcol_next;			/* e.g. wing pod hit by its owner */
		if ((intptr_t) r10 == ((ship_stack *) owner)->who_owns_me) goto armcol_next;
	}
armcol_registercollision:
	*stack++ = (intptr_t) r1;
	*stack++ = (intptr_t) r10;

armcol_next:
	r10 = (ship_stack *) r10->header.next_address;
	if (r10->header.status != 1) goto armcol_newlaserrestorenext;
	r6 = r10->collision_size;
	r3 = r10->x_pos; r4 = r10->y_pos; r5 = r10->z_pos;
	goto armcol_checknext;
}

/* Empty every bucket of the z-sort list.  (Plot_Land.s does this implicitly as it walks and
   plots the buckets; used by the placeholder land plotter until that is translated.) */
void armzsort_clear (void)
{
	int i;
	for (i = 0; i < 128; i++) zs_list->offset [i] = NULL;
}
