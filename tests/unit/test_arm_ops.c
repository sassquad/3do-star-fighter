/* ARM data-processing semantics (arm_ops.h) and the game's random generator
 * (SF_ARMUtils.c). */
#include "sft.h"
#include "arm_ops.h"
#include "SF_ARMUtils.h"

/* Register-specified shifts use the bottom byte; >= 32 shifts everything out
   (ASR fills with the sign). */
TEST(test_asr_semantics)
{
  CHECK_EQ(ASR(-8, 0), -8);
  CHECK_EQ(ASR(-8, 1), -4);
  CHECK_EQ(ASR(-8, 31), -1);
  CHECK_EQ(ASR(-8, 32), -1);
  CHECK_EQ(ASR(8, 32), 0);
  CHECK_EQ(ASR(8, 255), 0);
  CHECK_EQ(ASR(-1, 255), -1);
  CHECK_EQ(ASR(0x40000000, 256), 0x40000000); /* 256 & 255 == 0 */
  CHECK_EQ(ASR(INT32_MIN, 31), -1);
}

TEST(test_lsr_lsl_semantics)
{
  CHECK_EQX(LSR(0x80000000u, 31), 1);
  CHECK_EQX(LSR(0x80000000u, 32), 0);
  CHECK_EQX(LSR(0xffffffffu, 33), 0);
  CHECK_EQX(LSR(0x12345678u, 256), 0x12345678u);
  CHECK_EQX(LSL(1, 31), 0x80000000u);
  CHECK_EQX(LSL(1, 32), 0);
  CHECK_EQX(LSL(0xffffffffu, 200), 0);
  CHECK_EQX(LSL(3, 0), 3);
  CHECK_EQX(LSL(3, 257), 6);
}

TEST(test_ror_semantics)
{
  CHECK_EQX(ROR(0x12345678u, 0), 0x12345678u);
  CHECK_EQX(ROR(0x12345678u, 32), 0x12345678u);
  CHECK_EQX(ROR(0x12345678u, 4), 0x81234567u);
  CHECK_EQX(ROR(0x12345678u, 36), 0x81234567u);
  CHECK_EQX(ROR(1, 1), 0x80000000u);
}

TEST(test_wrapping_arithmetic)
{
  CHECK_EQ(MULS(0x10000, 0x10000), 0);
  CHECK_EQ(MULS(-1, INT32_MIN), INT32_MIN);
  CHECK_EQ(MULS(46341, 46341), (int32_t) 0x80001219u);
  CHECK_EQ(ADD(INT32_MAX, 1), INT32_MIN);
  CHECK_EQ(SUB(INT32_MIN, 1), INT32_MAX);
}

/* The generator is seeded with 1023/1023 at start-up; its sequence decides
   explosion debris, bonuses, backdrops etc., so the headless goldens depend on
   it.  Reference: eor r0,r0,r1,ror r1 ; add r1,r1,r0 ; result r0^r1. */
static uint32_t ref_s1 = 1023, ref_s2 = 1023;
static uint32_t
ref_random(void)
{
  uint32_t r0 = ref_s1, r1 = ref_s2, n = r1 & 31;
  r0 ^= n ? (r1 >> n) | (r1 << (32 - n)) : r1;
  r1 += r0;
  ref_s1 = r0;
  ref_s2 = r1;
  return r0 ^ r1;
}

TEST(test_random_sequence_matches_reference)
{
  int i, bad = 0;
  arm_randominit();
  ref_s1 = ref_s2 = 1023;
  for (i = 0; i < 100000; i++) {
    uint32_t a = (uint32_t) arm_random(), b = ref_random();
    if (a != b && bad++ < 3) {
      SFT_FAIL_("value %d: 0x%08x != reference 0x%08x", i, a, b);
    }
  }
}

TEST(test_random_init_restarts_sequence)
{
  int32_t first[8], i;
  arm_randominit();
  for (i = 0; i < 8; i++) {
    first[i] = arm_random();
  }
  arm_randominit();
  for (i = 0; i < 8; i++) {
    CHECK_EQ(arm_random(), first[i]);
  }
}

/* arm_randomvalue(limit) returns 0..limit inclusive (bitwise build-up from
   bit 15 down), and 0 for a negative limit. */
TEST(test_randomvalue_in_range)
{
  int i, limit;
  arm_randominit();
  for (limit = 1; limit <= 40000; limit = limit * 3 + 1) {
    int seen_nonzero = 0, seen_limit = 0;
    for (i = 0; i < 20000; i++) {
      int32_t v = arm_randomvalue(limit);
      if (v < 0 || v > limit) {
        SFT_FAIL_("arm_randomvalue(%d) returned %d", limit, v);
        return;
      }
      seen_nonzero |= v != 0;
      seen_limit |= v == limit;
    }
    CHECK(seen_nonzero);
    if (limit < 64) {
      CHECK(seen_limit);
    }
  }
  for (i = 0; i < 100; i++) {
    CHECK_EQ(arm_randomvalue(-1), 0);
  }
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_asr_semantics);
  RUN(test_lsr_lsl_semantics);
  RUN(test_ror_semantics);
  RUN(test_wrapping_arithmetic);
  RUN(test_random_sequence_matches_reference);
  RUN(test_random_init_restarts_sequence);
  RUN(test_randomvalue_in_range);
  return SFT_END();
}
