#include "gslt_epoch_slots_v1.h"

#include <limits.h>
#include <stdio.h>

static unsigned checks_run;
static unsigned checks_failed;

static void expect(bool condition, const char *message) {
    checks_run++;
    if (!condition) {
        checks_failed++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

int main(void) {
    CettaGsltEpochSlotsV1 slots = {0};
    uint32_t *value;

    expect(cetta_gslt_epoch_slots_prepare_v1(
               &slots, 3u, sizeof(uint32_t)),
           "three-slot binder-shaped carrier is admitted");
    value = cetta_gslt_epoch_slots_set_v1(&slots, 1u);
    expect(value != NULL, "current epoch accepts a slot write");
    if (value)
        *value = 42u;
    value = cetta_gslt_epoch_slots_get_v1(&slots, 1u);
    expect(value && *value == 42u,
           "current epoch observes the exact payload");
    expect(cetta_gslt_epoch_slots_get_v1(&slots, 0u) == NULL,
           "unwritten current-epoch slot is absent");

    expect(cetta_gslt_epoch_slots_prepare_v1(
               &slots, 2u, sizeof(uint32_t)),
           "parser-shaped narrower transaction reuses storage");
    expect(cetta_gslt_epoch_slots_get_v1(&slots, 1u) == NULL,
           "fresh epoch hides the prior transaction without clearing");
    expect(cetta_gslt_epoch_slots_set_v1(&slots, 2u) == NULL,
           "retained capacity outside the logical width is inaccessible");
    value = cetta_gslt_epoch_slots_set_v1(&slots, 0u);
    expect(value != NULL, "new epoch writes through the reused carrier");
    if (value)
        *value = 7u;
    expect(!cetta_gslt_epoch_slots_prepare_v1(
               &slots, 2u, sizeof(uint64_t)),
           "carrier element type cannot silently change");
    expect(cetta_gslt_epoch_slots_get_v1(&slots, 3u) == NULL,
           "out-of-range reads fail closed");

    slots.epoch = UINT64_MAX;
    slots.epochs[0] = UINT64_MAX;
    expect(cetta_gslt_epoch_slots_prepare_v1(
               &slots, 3u, sizeof(uint32_t)),
           "epoch wraparound performs a physical stamp reset");
    expect(slots.epoch == 1u &&
               cetta_gslt_epoch_slots_get_v1(&slots, 0u) == NULL,
           "wraparound cannot resurrect a stale value");

    cetta_gslt_epoch_slots_free_v1(&slots);
    printf("GsltEpochSlotsV1Summary checks=%u failures=%u\n",
           checks_run, checks_failed);
    return checks_failed == 0u ? 0 : 1;
}
