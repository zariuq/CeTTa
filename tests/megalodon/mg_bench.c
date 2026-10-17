/*
 * Measurements of the native kernel that are not replays (see mg_replay for
 * source import and library reload):
 *
 *   mg_bench sharing            proof DAGs whose trees double at every level
 *   mg_bench retained LIBRARY   later use of a reloaded library's theorems
 *   mg_bench sessions COUNT     long-lived session churn: memory stays flat
 *
 * Each prints one line per measurement with wall time and peak RSS.
 */
#include "mg_core.h"
#include "mg_library.h"
#include "mg_profiles.h"
#include "mg_session.h"
#include "mg_sexpr.h"
#include "mg_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static long rss_kb(void) {
    /* current resident set, from /proc (peak from getrusage) */
    FILE *f = fopen("/proc/self/statm", "r");
    long pages = 0, resident = 0;
    if (f) {
        if (fscanf(f, "%ld %ld", &pages, &resident) != 2)
            resident = 0;
        fclose(f);
    }
    return resident * 4;
}

static long peak_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

static uint32_t name_of(MgTheory *t, const char *n) {
    return mg_name_intern(&t->store, n, strlen(n));
}

/* d_0 = proofLam A (hyp 0) : A -> A
 * d_{k+1} = proofApp (proofApp K d_k) d_k : A -> A,
 * K = proofLam (A -> A) (proofLam (A -> A) (hyp 0)).
 * The tree of d_k has about 2^k nodes; the DAG has O(k). */
static int bench_sharing(void) {
    static const uint32_t levels[] = {8, 16, 32, 64, 128, 256, 512, 1024};
    for (size_t li = 0; li < sizeof levels / sizeof levels[0]; ++li) {
        MgStatus st;
        MgTheory *t = mg_theory_new(MG_PROFILE_CUSTOM, "bench", &st);
        MgStore *s = &t->store;
        MgItem param = {.kind = MG_ITEM_PARAMETER, .name = name_of(t, "A"), .type = mg_tp_prop(s)};
        MgBudget b;
        mg_budget_init(&b, 0);
        /* the authority selects A, then admits it */
        if (mg_theory_associate_parameter(t, param.name, param.type) != MG_OK ||
            mg_theory_admit(t, &b, &param, 0, NULL) != MG_OK) return 1;
        MgTm *A = mg_tm_name(s, name_of(t, "A"));
        MgTm *AA = mg_tm_imp(s, A, A);
        MgPf *K = mg_pf_plam(s, AA, mg_pf_plam(s, AA, mg_pf_hyp(s, 0)));
        MgPf *d = mg_pf_plam(s, A, mg_pf_hyp(s, 0));
        for (uint32_t k = 0; k < levels[li]; ++k)
            d = mg_pf_pfap(s, mg_pf_pfap(s, K, d), d);
        double t0 = now();
        MgThm *thm = NULL;
        const char *reason = NULL;
        mg_budget_init(&b, 0);
        st = mg_thm_check_tree(t, s->ctx_empty, d, &b, &reason, &thm);
        double t1 = now();
        printf("sharing levels=%u tree-nodes~2^%u dag-nodes=%u status=%s work=%llu time=%.6fs\n",
               levels[li], levels[li] + 1, 2 * levels[li] + 4, mg_status_name(st),
               (unsigned long long)b.work_used, t1 - t0);
        if (thm) mg_thm_release(thm);
        mg_theory_release(t);
        if (st != MG_OK) return 1;
    }
    return 0;
}

/* A reloaded library: citing its theorems costs a lookup, not a recheck. */
static int bench_retained(const char *path) {
    MgStatus st;
    MgTheory *t = mg_theory_new(MG_PROFILE_EGAL, "bench", &st);
    MgImportPolicy policy = {.all_axioms = true};
    MgImportReport r;
    MgBudget b;
    mg_budget_init(&b, 0);
    double t0 = now();
    st = mg_library_load_file(t, path, &policy, &b, &r);
    double t1 = now();
    printf("reload %s status=%s theorems=%llu time=%.3fs peak_rss_mb=%.1f\n", path,
           mg_status_name(st), (unsigned long long)r.theorems, t1 - t0, (double)peak_kb() / 1024);
    if (st != MG_OK) return 1;
    /* every theorem of the library, cited by a fresh proof tree: the first
     * pass computes each statement's normal form (the known rule's
     * conclusion), the second finds it cached */
    for (int pass = 1; pass <= 2; ++pass) {
    uint64_t cited = 0, work = 0, failed = 0;
    double t2 = now();
    for (uint64_t i = 0; i < t->decl_len; ++i) {
        MgDecl *d = t->decls[i];
        if (d->kind != MG_DECL_THEOREM) continue;
        MgThm *thm = NULL;
        const char *reason = NULL;
        mg_budget_init(&b, 0);
        st = mg_thm_check_tree(t, t->store.ctx_empty, mg_pf_known(&t->store, d->name), &b, &reason, &thm);
        if (st != MG_OK) {
            /* a statement whose normal form cannot be materialized (the
             * reference judgment form): a resource outcome, not a refusal */
            if (pass == 1 && failed < 3)
                printf("cite of %s: %s %s\n", d->source_name ? d->source_name : "?",
                       mg_status_name(st), reason ? reason : "");
            ++failed;
            continue;
        }
        work += b.work_used;
        ++cited;
        mg_thm_release(thm);
    }
    double t3 = now();
    printf("retained pass=%d cites=%llu not-materializable=%llu time=%.6fs per-cite=%.2fus work=%llu\n",
           pass, (unsigned long long)cited, (unsigned long long)failed, t3 - t2,
           cited ? (t3 - t2) * 1e6 / (double)cited : 0.0, (unsigned long long)work);
    }
    /* LCF use: the known as a theorem value, 100000 times */
    double t4 = now();
    uint64_t n = 0;
    for (int round = 0; round < 100000; ++round) {
        for (uint64_t i = t->decl_len; i-- > 0;) {
            MgDecl *d = t->decls[i];
            if (d->kind != MG_DECL_THEOREM) continue;
            MgThm *thm = NULL;
            const char *reason = NULL;
            if (mg_thm_from_known(t, d->name, &reason, &thm) != MG_OK) return 1;
            mg_thm_release(thm);
            ++n;
            break;
        }
    }
    double t5 = now();
    printf("retained known-values=%llu per-value=%.3fus\n", (unsigned long long)n,
           (t5 - t4) * 1e6 / (double)n);
    mg_theory_release(t);
    return 0;
}

/* Two rows: [A] |- A by hyp 0, and |- A -> A by proofLam from it. */
typedef struct {
    MgSxArena *arena;
    uint64_t *parents;
    uint32_t parents_cap;
    unsigned *releases;
} Rows;

static const char *g_rows[] = {
    "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses (Tm.named \"A\"))) "
    "(Tm.named \"A\")) (Megalodon.hyp 0) (parents) 0)",
    "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) (hypotheses)) "
    "(Tm.imp (Tm.named \"A\") (Tm.named \"A\"))) (Megalodon.proofLam (Tm.named \"A\")) (parents 1) 0)",
};

static uint64_t rows_revision(void *c) { (void)c; return 1; }

static MgStatus rows_read(void *context, MgTheory *theory, uint64_t address, MgRow *out,
                          const char **reason) {
    Rows *r = context;
    if (address < 1 || address > 2) { *reason = "no row"; return MG_MALFORMED; }
    mg_sx_arena_reset(r->arena);
    MgSxReader rd;
    const char *text = g_rows[address - 1];
    mg_sx_reader_init(&rd, text, strlen(text));
    MgSx *x = mg_sx_read(&rd, r->arena);
    bool nomem = false;
    if (!x || !mg_sx_wire_row(&theory->store, x, out, &r->parents, &r->parents_cap, &nomem)) {
        *reason = "undecodable";
        return nomem ? MG_NOMEM : MG_MALFORMED;
    }
    return MG_OK;
}

static void rows_release(void *context) {
    Rows *r = context;
    ++*r->releases;
    mg_sx_arena_free(r->arena);
    free(r->parents);
    free(r);
}

static int bench_sessions(unsigned count) {
    MgStatus st;
    MgTheory *t = mg_theory_new(MG_PROFILE_CUSTOM, "bench", &st);
    MgItem param = {.kind = MG_ITEM_PARAMETER, .name = name_of(t, "A"), .type = mg_tp_prop(&t->store)};
    MgBudget b;
    mg_budget_init(&b, 0);
    if (mg_theory_associate_parameter(t, param.name, param.type) != MG_OK ||
        mg_theory_admit(t, &b, &param, 0, NULL) != MG_OK) return 1;
    MgTm *claim = mg_tm_imp(&t->store, mg_tm_name(&t->store, name_of(t, "A")),
                            mg_tm_name(&t->store, name_of(t, "A")));
    unsigned releases = 0;
    const uint64_t schedule[2] = {1, 2};
    long rss0 = 0;
    double t0 = now();
    for (unsigned i = 0; i < count; ++i) {
        if (i == count / 10) rss0 = rss_kb();   /* after warm-up */
        Rows *r = calloc(1, sizeof *r);
        r->arena = mg_sx_arena_new();
        r->releases = &releases;
        MgSession *s = mg_session_begin(t, (MgRowSource){rows_revision, rows_read, rows_release, r}, &st);
        if (!s) return 1;
        MgSessionReport rep;
        mg_budget_init(&b, 0);
        if (mg_session_resume(s, schedule, 2, 0, &b, &rep) != MG_OK || rep.status != MG_SESSION_FINISHED)
            return 1;
        MgThm *thm = NULL;
        const char *reason = NULL;
        if (mg_session_finish(s, 2, claim, &b, &reason, &thm) != MG_OK) return 1;
        if (i % 2) mg_session_cancel(s);   /* explicit cancel, then release */
        mg_session_release(s);
        mg_thm_release(thm);               /* the theorem outlives its session */
    }
    double t1 = now();
    long rss1 = rss_kb();
    printf("sessions count=%u releases=%u time=%.3fs per-session=%.2fus rss_kb after-warmup=%ld end=%ld\n",
           count, releases, t1 - t0, (t1 - t0) * 1e6 / count, rss0, rss1);
    mg_theory_release(t);
    return releases == count ? 0 : 1;
}

typedef struct {
    MgDeepCall call;
    int argc;
    char **argv;
    int code;
} Main;

static void run(MgDeepCall *call) {
    Main *m = (Main *)call;
    const char *what = m->argv[1];
    if (strcmp(what, "sharing") == 0) m->code = bench_sharing();
    else if (strcmp(what, "retained") == 0 && m->argc > 2) m->code = bench_retained(m->argv[2]);
    else if (strcmp(what, "sessions") == 0 && m->argc > 2) m->code = bench_sessions((unsigned)atoi(m->argv[2]));
    else m->code = 2;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s sharing | retained LIBRARY | sessions COUNT\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    Main m = {.argc = argc, .argv = argv};
    m.call.run = run;
    if (!mg_deep_call(&m.call, NULL, NULL)) return 2;
    mg_deep_call_shutdown();
    return m.code;
}
