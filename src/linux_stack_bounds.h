#ifndef CETTA_LINUX_STACK_BOUNDS_H
#define CETTA_LINUX_STACK_BOUNDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#if defined(__linux__)
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Existing pages remain usable after a limit is lowered. Further downward
 * growth is bounded by the limit, the caller's cap and the preceding VMA. */
static inline uintptr_t cetta_stack_growth_low(uintptr_t mapped_low,
        uintptr_t high, uintptr_t preceding_high, uint64_t limit, uint64_t cap) {
    uint64_t size = limit < cap ? limit : cap;
    uintptr_t low = size < high ? high - (uintptr_t)size : 0u;
    if (low < preceding_high) low = preceding_high;
    return low < mapped_low ? low : mapped_low;
}

static inline bool cetta_linux_initial_stack_bounds(uintptr_t here,
        uint64_t cap, uintptr_t *low_out, uintptr_t *high_out) {
#if defined(__linux__)
    /* Only the kernel's initial stack grows. pthread stacks have separately
     * allocated fixed bounds; RLIMIT_STACK must not be applied to them. */
    if ((pid_t)syscall(SYS_gettid) != getpid()) return false;
    struct rlimit limit;
    if (getrlimit(RLIMIT_STACK, &limit)) return false;
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) return false;
    char *line = NULL;
    size_t capacity = 0u;
    uintptr_t preceding_high = 0u;
    bool found = false;
    while (getline(&line, &capacity, maps) >= 0) {
        uintptr_t low, high;
        if (sscanf(line, "%" SCNxPTR "-%" SCNxPTR, &low, &high) != 2 || low >= high) break;
        if (low <= here && here < high) {
            /* A host can switch the initial thread to a different stack. Do
             * not mistake such a mapping for the kernel's growable stack. */
            if (strstr(line, "[stack]")) {
                uint64_t maximum = limit.rlim_cur == RLIM_INFINITY
                    ? cap : (uint64_t)limit.rlim_cur;
                *low_out = cetta_stack_growth_low(low, high, preceding_high, maximum, cap);
                *high_out = high;
                found = true;
            }
            break;
        }
        preceding_high = high;
    }
    free(line);
    fclose(maps);
    return found;
#else
    (void)here; (void)cap; (void)low_out; (void)high_out;
    return false;
#endif
}

#endif
