#include "sf_compat.h"
#ifndef __EXPLOSION_SET_H
#define __EXPLOSION_SET_H

#include "SF_ARMLink.h"

#define MAX_EXPLOSIONS 32

typedef struct explosion_stack {
  link_header header;
  int32_t grid_ref;
  int32_t counter;
} explosion_stack;

typedef struct explosion_list {
  linked_list info;
  explosion_stack laser_item[MAX_EXPLOSIONS];
} explosion_list;

#endif
