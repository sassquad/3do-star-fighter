/* arm_ops.h - helpers that give C the exact semantics of ARM data-processing
 * shifts. All values are 32-bit; shifts by a register use the bottom byte of
 * the register. */
#ifndef ARM_OPS_H
#define ARM_OPS_H
#include <stdint.h>

static inline int32_t
ASR(int32_t x, uint32_t n)
{
  n &= 255;
  return n >= 32 ? (x < 0 ? -1 : 0) : (x >> n);
}

static inline uint32_t
LSR(uint32_t x, uint32_t n)
{
  n &= 255;
  return n >= 32 ? 0 : (x >> n);
}

static inline uint32_t
LSL(uint32_t x, uint32_t n)
{
  n &= 255;
  return n >= 32 ? 0 : (x << n);
}

static inline uint32_t
ROR(uint32_t x, uint32_t n)
{
  n &= 31;
  return n ? (x >> n) | (x << (32 - n)) : x;
}

static inline int32_t
MULS(int32_t a, int32_t b)
{
  return (int32_t) ((uint32_t) a * (uint32_t) b);
} /* wrapping 32-bit multiply */

static inline int32_t
ADD(int32_t a, int32_t b)
{
  return (int32_t) ((uint32_t) a + (uint32_t) b);
}

static inline int32_t
SUB(int32_t a, int32_t b)
{
  return (int32_t) ((uint32_t) a - (uint32_t) b);
}

/* signed 32x32 -> 64 multiply high/low (smull) */
static inline int32_t
SMULL_HI(int32_t a, int32_t b)
{
  return (int32_t) (((int64_t) a * b) >> 32);
}

static inline uint32_t
SMULL_LO(int32_t a, int32_t b)
{
  return (uint32_t) ((int64_t) a * b);
}
#endif
