/* Public kernel API lifetime benchmark, used by the integrated service.
 * Runs one mode per process so peak RSS is not inherited from another mode. */
#include "hol_replay.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void require(HolStatus status) {
    if (status != HOL_OK) {
        fprintf(stderr, "HOL lifetime benchmark: %s\n", hol_status_name(status));
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 3 || (strcmp(argv[1], "check") && strcmp(argv[1], "record"))) {
        fputs("usage: bench_lifetimes check|record STEPS\n", stderr);
        return 2;
    }
    char *end;
    errno = 0;
    uint64_t steps = strtoull(argv[2], &end, 10);
    if (errno || end == argv[2] || *end || argv[2][0] == '-' || !steps) return 2;
    bool recording = !strcmp(argv[1], "record");
    HolKernel *kernel = hol_kernel_new(NULL);
    if (!kernel) return 1;
    HolRef a, x, current, base;
    require(hol_mk_vartype(kernel, "A", &a));
    require(hol_mk_var(kernel, "x", a, &x));
    hol_journal_enable(kernel, recording);
    require(hol_REFL(kernel, x, &current));
    require(hol_ref_dup(kernel, current, &base));
    double start = now();
    for (uint64_t i = 0; i < steps; ++i) {
        HolRef next;
        require(hol_TRANS(kernel, current, base, &next));
        require(hol_ref_release(kernel, current));
        current = next;
    }
    double check_s = now() - start;
    start = now();
    hol_gc(kernel);
    double collect_s = now() - start;
    HolStats live;
    hol_stats(kernel, &live);
    bool equal = false;
    require(hol_thm_equal(kernel, current, base, &equal));
    if (!equal || live.journal_events != (recording ? steps + 1 : 0)) return 1;
    require(hol_ref_release(kernel, current));
    require(hol_ref_release(kernel, base));
    hol_gc(kernel);
    HolStats retired;
    hol_stats(kernel, &retired);
    if (retired.journal_events || retired.journal_bytes) return 1;
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    printf("{\"mode\":\"%s\",\"steps\":%" PRIu64
           ",\"check_s\":%.6f,\"collect_s\":%.6f,\"core_bytes\":%zu,"
           "\"journal_events\":%zu,\"journal_bytes\":%zu,\"live_refs\":%zu,"
           "\"retired_core_bytes\":%zu,\"retired_journal_bytes\":%zu,"
           "\"maxrss_kib\":%ld}\n",
           argv[1], steps, check_s, collect_s, live.bytes_live, live.journal_events,
           live.journal_bytes, live.refs, retired.bytes_live, retired.journal_bytes,
           usage.ru_maxrss);
    hol_kernel_free(kernel);
    return 0;
}
