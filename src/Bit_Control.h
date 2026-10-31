#include "sf_compat.h"
#include "Bit_Struct.h"

extern int32_t cosine_table[2048];
extern int32_t *sine_table;

extern bit_list bits;
#define ROT_LIMIT ((1024 * 1024) - 1)

extern int32_t which_graphics_set;

void bit_update(void);

void add_bit(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
             int32_t, int32_t, int32_t, intptr_t, int32_t);
