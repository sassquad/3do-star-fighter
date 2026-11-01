/* SF_ARMLink.c: linked lists of fixed-size items (add, sorted add, delete,
 * resort, suspend/release) and the z-sort buckets.  Random operation
 * sequences are checked against a simple model after every step. */
#include "sft.h"
#include "SF_ARMLink.h"

typedef struct item {
  link_header h;
  int32_t value; /* sort word (offset 0 after the header) */
  int32_t id;
} item;

#define MAXI 40 /* items in the block, including the end sentinel */

static struct {
  linked_list list;
  item items[MAXI];
} blk;

static uint32_t rng = 12345;
static uint32_t
rnd(void)
{
  rng = rng * 1103515245u + 12345u;
  return rng >> 8;
}

/* Walk the whole chain and check its structure.  Returns the number of
   active items, or -1 (after reporting) on a broken list. */
static int
check_list(int want_sorted)
{
  item *it = (item *) blk.list.start_address, *prev = NULL, *first_free = NULL;
  int n = 0, active = 0, phase = 1;
  while (it) {
    if (n++ > MAXI) {
      SFT_FAIL_("chain longer than the block (loop?)");
      return -1;
    }
    if ((item *) it->h.prev_address != prev) {
      SFT_FAIL_("item %d: prev pointer broken", n);
      return -1;
    }
    if (it->h.status == 1) {
      if (phase != 1) {
        SFT_FAIL_("active item after free items");
        return -1;
      }
      if (want_sorted && prev && prev->h.status == 1 &&
          prev->value > it->value) {
        SFT_FAIL_("not sorted: %d before %d", prev->value, it->value);
        return -1;
      }
      active++;
    } else if (it->h.status == 0) {
      if (!first_free) {
        first_free = it;
      }
      phase = 0;
    } else if (it->h.status == 2) {
      if (it->h.next_address != NULL) {
        SFT_FAIL_("sentinel is not last");
        return -1;
      }
      if (!first_free) {
        first_free = it;
      }
    } else {
      SFT_FAIL_("bad status %d", it->h.status);
      return -1;
    }
    prev = it;
    it = (item *) it->h.next_address;
  }
  if (n != MAXI) {
    SFT_FAIL_("chain holds %d items, block has %d", n, MAXI);
    return -1;
  }
  if (prev->h.status != 2) {
    SFT_FAIL_("chain does not end in the sentinel");
    return -1;
  }
  if ((item *) blk.list.next_freeaddress != first_free) {
    SFT_FAIL_("next_freeaddress is not the first free item");
    return -1;
  }
  return active;
}

static item *
nth_active(int k)
{
  item *it = (item *) blk.list.start_address;
  while (k-- > 0) {
    it = (item *) it->h.next_address;
  }
  return it;
}

static void
init_list(void)
{
  memset(&blk, 0xAA, sizeof blk); /* garbage, as fresh memory would be */
  armlink_initialise(MAXI, &blk, sizeof(item), 0);
}

TEST(test_initialise)
{
  init_list();
  CHECK_EQ(check_list(1), 0);
  CHECK_EQ(blk.list.sortword, (int32_t) sizeof(link_header));
}

TEST(test_add_until_full)
{
  int i;
  init_list();
  for (i = 0; i < MAXI - 1; i++) {
    item *it = (item *) armlink_addtolist(&blk);
    REQUIRE(it != NULL);
    it->value = i;
  }
  CHECK(armlink_addtolist(&blk) == NULL);
  CHECK(armlink_sorttolist(&blk, 5) == NULL);
  CHECK_EQ(check_list(0), MAXI - 1);
}

/* Deleting the only active item relies on aliased pointer updates in the
   original; it must leave an empty, valid list. */
TEST(test_delete_only_item)
{
  item *it;
  init_list();
  it = (item *) armlink_addtolist(&blk);
  REQUIRE(it);
  armlink_deleteitem(it, &blk);
  CHECK_EQ(check_list(1), 0);
  it = (item *) armlink_addtolist(&blk);
  CHECK(it != NULL);
  CHECK_EQ(check_list(1), 1);
}

TEST(test_random_sorted_operations)
{
  int step, model = 0;
  init_list();
  for (step = 0; step < 20000; step++) {
    int n = check_list(1);
    if (n < 0) {
      SFT_FAIL_("at step %d", step);
      return;
    }
    if (n != model) {
      SFT_FAIL_("step %d: %d active, expected %d", step, n, model);
      return;
    }
    if (model < MAXI - 1 && (model == 0 || (rnd() % 100) < 55)) {
      int32_t v = (int32_t) (rnd() % 50) - 25;
      item *it = (item *) armlink_sorttolist(&blk, v);
      if (!it) {
        SFT_FAIL_("step %d: sorttolist failed with %d active", step, model);
        return;
      }
      it->value = v;
      model++;
    } else {
      armlink_deleteitem(nth_active((int) (rnd() % (uint32_t) model)), &blk);
      model--;
    }
  }
}

TEST(test_resort_after_changes)
{
  int round, i;
  init_list();
  for (i = 0; i < 30; i++) {
    item *it = (item *) armlink_addtolist(&blk);
    REQUIRE(it);
    it->value = (int32_t) (rnd() % 1000);
    it->id = i;
  }
  for (round = 0; round < 500; round++) {
    long sum_before = 0, sum_after = 0;
    item *it;
    int k = (int) (rnd() % 5);
    while (k-- >= 0) { /* move some values, as ships move each frame */
      nth_active((int) (rnd() % 30))->value = (int32_t) (rnd() % 1000) - 200;
    }
    for (it = (item *) blk.list.start_address; it->h.status == 1;
         it = (item *) it->h.next_address) {
      sum_before += it->id;
    }
    armlink_resortlist(&blk);
    if (check_list(1) != 30) {
      SFT_FAIL_("round %d", round);
      return;
    }
    for (it = (item *) blk.list.start_address; it->h.status == 1;
         it = (item *) it->h.next_address) {
      sum_after += it->id;
    }
    CHECK_EQ(sum_after, sum_before);
  }
}

TEST(test_suspend_release)
{
  item *a, *b, *c;
  init_list();
  a = (item *) armlink_sorttolist(&blk, 1);
  a->value = 1;
  b = (item *) armlink_sorttolist(&blk, 2);
  b->value = 2;
  c = (item *) armlink_sorttolist(&blk, 3);
  c->value = 3;
  REQUIRE(check_list(1) == 3);
  armlink_suspenditem(b, &blk); /* taken out of the chain, still allocated */
  CHECK((item *) a->h.next_address == c);
  CHECK((item *) c->h.prev_address == a);
  armlink_releaseitem(b, &blk); /* back in at the start */
  CHECK((item *) blk.list.start_address == b);
  CHECK_EQ(check_list(0), 3);
}

/* ---- z-sort: 128 buckets by distance >> 24, far items first in a bucket ---- */
static zsort_list zl;

TEST(test_zsort_buckets_ordered)
{
  int i, b, total = 0;
  const int n = 500;
  armzsort_create(&zl, MAX_GRAPHICPLOT);
  armzsort_initialise();
  for (i = 0; i < n; i++) {
    int32_t d = (int32_t) ((rnd() << 8) & 0x7fffffff);
    armzsort_add(d, &zl, i);
  }
  for (b = 0; b < 128; b++) {
    zsort_item *z = zl.offset[b], *prev = NULL;
    for (; z; prev = z, z = z->next_address) {
      total++;
      if ((z->distance >> 24) != b) {
        SFT_FAIL_("distance 0x%x in bucket %d", z->distance, b);
        return;
      }
      if (prev && prev->distance < z->distance) {
        SFT_FAIL_("bucket %d not in descending order", b);
        return;
      }
    }
  }
  CHECK_EQ(total, n);
  armzsort_clear();
  for (b = 0; b < 128; b++) {
    CHECK(zl.offset[b] == NULL);
  }
}

TEST(test_zsort_capacity)
{
  int i, b, total = 0;
  armzsort_create(&zl, 10);
  armzsort_initialise();
  for (i = 0; i < 25; i++) {
    armzsort_add(i << 20, &zl, i);
  }
  for (b = 0; b < 128; b++) {
    zsort_item *z;
    for (z = zl.offset[b]; z; z = z->next_address) {
      total++;
    }
  }
  CHECK_EQ(total, 10);
}

int
main(void)
{
  SFT_BEGIN();
  RUN(test_initialise);
  RUN(test_add_until_full);
  RUN(test_delete_only_item);
  RUN(test_random_sorted_operations);
  RUN(test_resort_after_changes);
  RUN(test_suspend_release);
  RUN(test_zsort_buckets_ordered);
  RUN(test_zsort_capacity);
  return SFT_END();
}
