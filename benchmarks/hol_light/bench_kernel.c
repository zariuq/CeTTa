/* bench_kernel.c - native-only measurements through the public API, as
 * JSON lines with raw times, iteration counts and retained bytes.  These
 * are not cross-implementation comparisons (bench/run_bench.sh does those
 * on identical streams).  Phases:
 *   cold          kernel creation and bootstrap, then free
 *   first/repeat  REFL of a fresh large term (interning) vs the same again
 *   guard         dependency validation: hol_env_admits on a deep history
 *   export        certificate export of N roots with shared payloads
 *   decode+check  replay of that certificate: one-shot, and appended in
 *                 4 KiB chunks with record budget 7 (append/resume)
 *   gc            a collection after releasing an owner of temporaries
 *   retained      bytes per live object class after the run */
#include "hol_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

static double now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static void must(HolStatus s, const char *what) {
    if (s != HOL_OK) { fprintf(stderr, "bench: %s: %s\n", what, hol_status_name(s)); exit(1); }
}

static void row(const char *phase, unsigned long n, double secs, HolKernel *k) {
    HolStats s;
    memset(&s, 0, sizeof s);
    if (k) hol_stats(k, &s);
    printf("{\"phase\":\"%s\",\"iterations\":%lu,\"seconds\":%.6f,\"ns_per_iteration\":%.1f,"
           "\"bytes_live\":%zu,\"terms\":%zu,\"thms\":%zu,\"refs\":%zu}\n",
           phase, n, secs, n ? secs * 1e9 / (double)n : 0.0, s.bytes_live, s.terms, s.thms, s.refs);
}

int main(void) {
    double t0 = now();
    for (int i = 0; i < 1000; i++) {
        HolKernel *k = hol_kernel_new(NULL);
        HolRef env;
        must(hol_env_bare(k, &env), "bare");
        hol_kernel_free(k);
    }
    row("cold-kernel-create-bootstrap-free", 1000, now() - t0, NULL);

    /* the 20000-node chain below is deeper than the default limit (8192);
     * 20000 levels need about 5 MiB of stack, within the usual 8 MiB */
    HolConfig cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.max_depth = 1u << 15;
    HolKernel *k = hol_kernel_new(&cfg);
    hol_journal_enable(k, true);
    HolRef env, A, AA, x, y, f;
    must(hol_env_bare(k, &env), "bare");
    must(hol_mk_vartype(k, "A", &A), "A");
    HolRef args[2] = {A, A};
    must(hol_mk_type(k, env, "fun", args, 2, &AA), "fun");
    must(hol_mk_var(k, "x", A, &x), "x");
    must(hol_mk_var(k, "y", A, &y), "y");
    must(hol_mk_var(k, "f", AA, &f), "f");

    /* first vs repeated: a 20000-node term built and checked twice */
    HolOwner tmp = hol_owner_new(k);
    HolOwner prev = hol_owner_set_current(k, tmp);
    t0 = now();
    HolRef t = x;
    for (int i = 0; i < 20000; i++) must(hol_mk_comb(k, f, t, &t), "comb");
    HolRef th;
    must(hol_REFL(k, t, &th), "refl");
    row("first-construct-and-refl-20000-nodes", 20000, now() - t0, k);
    t0 = now();
    HolRef t2 = x;
    for (int i = 0; i < 20000; i++) must(hol_mk_comb(k, f, t2, &t2), "comb");
    HolRef th2;
    must(hol_REFL(k, t2, &th2), "refl");
    row("repeat-construct-and-refl-20000-nodes-interned", 20000, now() - t0, k);
    HolRef pr[2] = {y, x}, th3;
    t0 = now();
    must(hol_INST(k, pr, 1, th, &th3), "inst");
    row("inst-20000-node-chain", 20000, now() - t0, k);
    hol_owner_set_current(k, prev);
    t0 = now();
    must(hol_owner_release(k, tmp), "release");
    hol_gc(k);
    row("release-owner-and-collect", 1, now() - t0, k);

    /* guard: dependency validation against a 10000-event history */
    HolRef e = env, p, q;
    HolRef Bo;
    must(hol_bool_ty(k, &Bo), "bool");
    for (int i = 0; i < 10000; i++) {
        char nm[32];
        snprintf(nm, sizeof nm, "c%d", i);
        HolRef e2;
        must(hol_new_constant(k, e, nm, Bo, &e2), "const");
        e = e2;
    }
    HolRef c0, rc;
    must(hol_mk_const(k, e, "c0", NULL, 0, &c0), "c0");
    must(hol_REFL(k, c0, &rc), "refl c0");
    bool ok = false;
    t0 = now();
    for (int i = 0; i < 1000000; i++) must(hol_env_admits(k, e, rc, &ok), "admits");
    row("guard-env-admits-depth-10000", 1000000, now() - t0, k);
    if (!ok) return 1;

    /* export and replay: 2000 roots, shared payloads, distinct events */
    must(hol_mk_var(k, "p", Bo, &p), "p");
    must(hol_ASSUME(k, p, &q), "assume");
    HolRef roots[2000];
    for (int i = 0; i < 2000; i++) must(hol_DEDUCT_ANTISYM_RULE(k, q, q, &roots[i]), "deduct");
    uint8_t *bytes = NULL;
    size_t len = 0;
    t0 = now();
    must(hol_export(k, roots, 2000, &bytes, &len), "export");
    row("export-2000-roots", 2000, now() - t0, k);
    printf("{\"certificate_bytes\":%zu}\n", len);
    for (int mode = 0; mode < 2; mode++) {
        HolKernel *k2 = hol_kernel_new(NULL);
        HolReplay *r = hol_replay_new(k2, NULL, HOL_AXIOMS_OPEN);
        t0 = now();
        HolReplayState st = HOL_REPLAY_NEED_INPUT;
        if (mode == 0) {
            hol_replay_feed(r, bytes, len, true);
            st = hol_replay_resume(r, 0);
        } else {
            for (size_t at = 0; at < len;) {
                size_t n = len - at > 4096 ? 4096 : len - at;
                hol_replay_feed(r, bytes + at, n, at + n == len);
                at += n;
                do st = hol_replay_resume(r, 7); while (st == HOL_REPLAY_RUNNING);
            }
        }
        HolReplayInfo info;
        hol_replay_info(r, &info);
        if (st != HOL_REPLAY_ACCEPTED || info.emitted != 2000) return 1;
        row(mode ? "decode-check-appended-4KiB-budget-7" : "decode-check-one-shot", info.records, now() - t0, k2);
        hol_replay_free(r);
        hol_kernel_free(k2);
    }
    free(bytes);
    HolStats s;
    hol_stats(k, &s);
    printf("{\"retained_bytes_live\":%zu,\"bytes_peak\":%zu,\"types\":%zu,\"terms\":%zu,\"thms\":%zu,"
           "\"decls\":%zu,\"refs\":%zu,\"gc_runs\":%llu}\n",
           s.bytes_live, s.bytes_peak, s.types, s.terms, s.thms, s.decls, s.refs, (unsigned long long)s.gc_runs);
    hol_kernel_free(k);
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("{\"maxrss_kib\":%ld}\n", ru.ru_maxrss);
    return 0;
}
