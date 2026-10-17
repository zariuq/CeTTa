#include "hol_cli.h"
#include "eval.h"

#include <inttypes.h>
#include <stdlib.h>

static bool interrupted(void *unused) {
    (void)unused;
    return eval_native_interrupted();
}

HolReplay *cetta_hol_read_stream(HolKernel *kernel, FILE *file,
                                HolAxiomPolicy policy, HolStatus *status) {
    *status = HOL_OK;
    HolReplay *replay = hol_replay_new(kernel, NULL, policy);
    if (!replay) { *status = HOL_NOMEM; return NULL; }
    HolReplayState state = HOL_REPLAY_NEED_INPUT;
    unsigned char bytes[65536];
    while (state == HOL_REPLAY_NEED_INPUT || state == HOL_REPLAY_RUNNING) {
        if (interrupted(NULL)) { *status = HOL_INTERRUPTED; break; }
        if (state == HOL_REPLAY_NEED_INPUT) {
            size_t n = fread(bytes, 1, sizeof bytes, file);
            if (ferror(file)) { *status = HOL_MALFORMED; break; }
            state = hol_replay_feed(replay, bytes, n, feof(file) != 0);
        }
        if (state == HOL_REPLAY_RUNNING || state == HOL_REPLAY_NEED_INPUT)
            state = hol_replay_resume(replay, 256);
    }
    return replay;
}

int cetta_hol_run_file(const char *path, HolAxiomPolicy policy, uint64_t fuel,
                       bool count_only, bool no_results, bool statistics) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return 4; }
    HolKernel *kernel = hol_kernel_new(NULL);
    if (!kernel) { fclose(file); fputs("hol:error allocation\n", stderr); return 3; }
    hol_set_fuel(kernel, fuel);
    hol_set_interrupt_poll(kernel, interrupted, NULL);
    HolStatus status;
    HolReplay *replay = cetta_hol_read_stream(kernel, file, policy, &status);
    fclose(file);
    int result = 0;
    HolReplayInfo info = {0};
    if (replay) hol_replay_info(replay, &info);
    if (status != HOL_OK || !replay || hol_replay_state(replay) != HOL_REPLAY_ACCEPTED) {
        if (status == HOL_OK) status = hol_replay_status(replay);
        fprintf(stderr, "(hol:%s \"%s\" \"%s\" %" PRIu64 ")\n",
                status == HOL_REFUSED || status == HOL_POLICY ? "refused" : "error",
                hol_status_name(status), replay ? hol_reason_name(hol_replay_reason(replay)) : "allocation",
                info.records);
        result = status == HOL_REFUSED || status == HOL_POLICY ? 1 :
            status == HOL_NOMEM || status == HOL_LIMIT || status == HOL_INTERRUPTED ? 3 : 2;
    } else if (info.failed) {
        fputs("(hol:refused \"probe stream contains refused records\")\n", stderr);
        result = 1;
    } else if (count_only) {
        if (!no_results) printf("%" PRIu64 "\n", info.emitted);
    } else if (!no_results) {
        for (uint64_t i = 0; i < info.emitted; ++i) {
            HolRef theorem = HOL_NULL;
            uint64_t ordinal;
            size_t length;
            status = hol_replay_next(replay, &theorem, &ordinal);
            if (status == HOL_OK) status = hol_print(kernel, theorem, NULL, 0, &length);
            char *text = status == HOL_OK && length < SIZE_MAX ? malloc(length + 1) : NULL;
            if (status == HOL_OK && !text) status = HOL_NOMEM;
            if (status == HOL_OK) status = hol_print(kernel, theorem, text, length + 1, &length);
            if (status == HOL_OK) printf("%s\n", text);
            free(text);
            if (theorem) hol_ref_release(kernel, theorem);
            if (status != HOL_OK) { fprintf(stderr, "hol:error output %s\n", hol_status_name(status)); result = 3; break; }
        }
        if (!result) printf("(hol:accepted %" PRIu64 " %" PRIu64 ")\n", info.records, info.emitted);
    }
    if (statistics) {
        HolStats stats;
        if (replay) hol_replay_info(replay, &info);
        hol_stats(kernel, &stats);
        fprintf(stderr, "(HolKernelStats records=%" PRIu64 " delivered=%" PRIu64
                " live-bytes=%zu peak-bytes=%zu journal-bytes=%zu journal-events=%zu)\n",
                info.records, info.delivered, stats.bytes_live, stats.bytes_peak,
                stats.journal_bytes, stats.journal_events);
    }
    if (fflush(stdout) || ferror(stdout)) result = 4;
    hol_replay_free(replay);
    hol_kernel_free(kernel);
    return result;
}
