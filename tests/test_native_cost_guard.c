#include "native_cost.h"
#include <stdio.h>

_Thread_local CettaNativeCostScope *cetta_native_cost_current_scope;
static unsigned observed_calls;
static CettaNativeCostKind observed_kind;
static uint64_t observed_units;

/* Returning zero deliberately makes the wrapper's return value insufficient
 * to detect an unwanted observed-service invocation. */
uint64_t cetta_native_cost_charge_observed(CettaNativeCostKind kind, uint64_t units) {
    observed_calls++;
    observed_kind = kind;
    observed_units = units;
    return 0u;
}

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "native cost guard: %s\n", #condition); return 1; \
} } while (0)

int main(void) {
    CHECK(!cetta_native_cost_active());
    CHECK(cetta_native_cost_charge(CETTA_COST_CAPTURE, 7u) == 0u);
    CHECK(cetta_native_cost_charge(CETTA_COST_QUEUE_VISIT, UINT64_MAX) == 0u);
    CHECK(observed_calls == 0u);
    CettaNativeCostScope scope = {0};
    cetta_native_cost_current_scope = &scope;
    CHECK(cetta_native_cost_active());
    CHECK(cetta_native_cost_charge(CETTA_COST_PUBLISH, 3u) == 0u);
    CHECK(observed_calls == 1u);
    CHECK(observed_kind == CETTA_COST_PUBLISH && observed_units == 3u);
    cetta_native_cost_current_scope = NULL;
    CHECK(cetta_native_cost_charge(CETTA_COST_CAPTURE, 9u) == 0u);
    CHECK(observed_calls == 1u);
    puts("PASS: inactive native cost guard never invokes observed service");
    return 0;
}
