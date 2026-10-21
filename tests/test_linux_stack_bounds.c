#define _GNU_SOURCE
#include <stdlib.h>
#include "linux_stack_bounds.h"
#include <pthread.h>

static unsigned checks, failures;
#define CHECK(test, label) do { checks++; if (!(test)) { failures++; \
    fprintf(stderr, "FAIL: %s\n", label); } } while (0)

static void *worker(void *unused) {
    (void)unused;
    uintptr_t low = 0u, high = 0u;
    return (void *)(uintptr_t)cetta_linux_initial_stack_bounds(
        (uintptr_t)__builtin_frame_address(0), 1024u * 1024u, &low, &high);
}

int main(void) {
    CHECK(cetta_stack_growth_low(900, 1000, 200, 0, 600, 800) == 400, "growable pages are available");
    CHECK(cetta_stack_growth_low(900, 1000, 800, 0, 600, 800) == 800, "growth stops at preceding mapping");
    CHECK(cetta_stack_growth_low(900, 1000, 500, 150, 600, 800) == 650, "growth stops a guard gap above it");
    CHECK(cetta_stack_growth_low(900, 1000, 200, 0, 50, 800) == 900, "lowered limit does not unmap pages");
    CHECK(cetta_stack_growth_low(900, 1000, 200, 0, 600, 200) == 800, "caller cap bounds growth");
    CHECK(cetta_stack_growth_low(900, 1000, 200, 0, UINT64_MAX, UINT64_MAX) == 200, "large limits do not underflow");
    CHECK(cetta_stack_growth_low(900, 1000, UINTPTR_MAX - 10, 100, 600, 800) == 900, "a gap past the top saturates");
    uintptr_t low = 0u, high = 0u, here = (uintptr_t)__builtin_frame_address(0);
    bool known = cetta_linux_initial_stack_bounds(here, 16u * 1024u * 1024u, &low, &high);
    CHECK(known && low <= here && here < high, "initial stack bounds contain the current frame");
    struct rlimit limit;
    if (!getrlimit(RLIMIT_STACK, &limit) && limit.rlim_cur >= 1024u * 1024u)
        CHECK(known && here - low > 256u * 1024u, "main stack has room beyond its initial VMA");
    pthread_t thread;
    int error = pthread_create(&thread, NULL, worker, NULL);
    CHECK(!error, "fixed-stack worker starts");
    if (!error) {
        void *claimed = NULL;
        CHECK(!pthread_join(thread, &claimed) && !claimed, "worker never inherits main-thread growth limits");
    }
    printf("linux stack bounds: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
