/*
 * Differential test: lazy conversion against the reference algorithm.
 *
 *   mg_diff PROFILE EXPORT [MUTANTS-PER-THEOREM] [REFERENCE-WORK-LIMIT] [SEED]
 *           [MAX-SECONDS]
 *
 * Imports the export (lazy mode), then for every checked theorem checks its
 * original proof and seeded mutants of it -- a cited known replaced by
 * another, the two premises of an implication elimination swapped, a
 * hypothesis index moved, a term argument replaced by another term of the
 * same proof, a type argument changed -- against the stated proposition,
 * once with lazy conversion and once with full normal forms (the reference
 * MathdataKernel.normalize).  Every decided pair must agree.  Reference
 * checks that exceed the work limit are counted as undecided, never as a
 * verdict.  One line per theorem is printed as it is done; MAX-SECONDS
 * stops the run after the theorem in progress, and the summary then says
 * how many theorems were covered.
 */
#include "mg_core.h"
#include "mg_library.h"
#include "mg_profiles.h"
#include "mg_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t g_rng = 0x9e3779b97f4a7c15u;
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

typedef struct {
    MgPf **nodes;
    size_t len, cap;
} PfList;

static void collect(PfList *l, MgPf *root) {
    /* every distinct node of the proof DAG */
    MgPf **stack = NULL;
    size_t sl = 0, sc = 0;
    MgPf **seen = NULL;
    size_t seen_cap = 1024, seen_len = 0;
    seen = calloc(seen_cap, sizeof(MgPf *));
#define SPUSH(x) do { if (sl == sc) { sc = sc ? sc * 2 : 64; stack = realloc(stack, sc * sizeof *stack); } stack[sl++] = (x); } while (0)
    SPUSH(root);
    while (sl) {
        MgPf *p = stack[--sl];
        size_t h = ((uintptr_t)p >> 4) & (seen_cap - 1);
        bool dup = false;
        while (seen[h]) {
            if (seen[h] == p) {
                dup = true;
                break;
            }
            h = (h + 1) & (seen_cap - 1);
        }
        if (dup)
            continue;
        seen[h] = p;
        if (++seen_len * 2 > seen_cap) {
            size_t nc = seen_cap * 2;
            MgPf **ns = calloc(nc, sizeof(MgPf *));
            for (size_t i = 0; i < seen_cap; ++i)
                if (seen[i]) {
                    size_t j = ((uintptr_t)seen[i] >> 4) & (nc - 1);
                    while (ns[j])
                        j = (j + 1) & (nc - 1);
                    ns[j] = seen[i];
                }
            free(seen);
            seen = ns;
            seen_cap = nc;
        }
        if (l->len == l->cap) {
            l->cap = l->cap ? l->cap * 2 : 64;
            l->nodes = realloc(l->nodes, l->cap * sizeof(MgPf *));
        }
        l->nodes[l->len++] = p;
        if (p->p)
            SPUSH(p->p);
        if (p->q)
            SPUSH(p->q);
    }
#undef SPUSH
    free(stack);
    free(seen);
}

/* Rebuild `root` with node `target` replaced by `with`. */
static MgPf *replace(MgStore *s, MgPf *root, MgPf *target, MgPf *with, int depth) {
    if (root == target)
        return with;
    if (depth > 4000)
        return NULL;
    MgPf *p = root->p ? replace(s, root->p, target, with, depth + 1) : NULL;
    MgPf *q = root->q ? replace(s, root->q, target, with, depth + 1) : NULL;
    if (root->p && !p)
        return NULL;
    if (root->q && !q)
        return NULL;
    if (p == root->p && q == root->q)
        return root;
    switch (root->tag) {
    case MG_PF_TMAP: return mg_pf_tmap(s, p, root->t);
    case MG_PF_PFAP: return mg_pf_pfap(s, p, q);
    case MG_PF_PLAM: return mg_pf_plam(s, root->t, p);
    case MG_PF_TLAM: return mg_pf_tlam(s, root->tp, p);
    case MG_PF_TPAP: return mg_pf_tpap(s, p, root->tp);
    case MG_PF_TPLAM: return mg_pf_tplam(s, p);
    default: return root;
    }
}

static MgPf *mutate(MgTheory *t, MgPf *proof, const PfList *nodes, const uint32_t *knowns,
                    size_t known_count, int *kind) {
    MgStore *s = &t->store;
    for (int attempt = 0; attempt < 16; ++attempt) {
        MgPf *n = nodes->nodes[rnd() % nodes->len];
        MgPf *m = NULL;
        int k = (int)(rnd() % 5);
        switch (k) {
        case 0:
            if (n->tag == MG_PF_KNOWN && known_count)
                m = mg_pf_known(s, knowns[rnd() % known_count]);
            break;
        case 1:
            if (n->tag == MG_PF_PFAP)
                m = mg_pf_pfap(s, n->q, n->p);
            break;
        case 2:
            if (n->tag == MG_PF_HYP)
                m = mg_pf_hyp(s, n->n + 1u + (uint32_t)(rnd() % 2));
            break;
        case 3:
            if (n->tag == MG_PF_TMAP) {
                MgPf *other = nodes->nodes[rnd() % nodes->len];
                if (other->tag == MG_PF_TMAP && other->t != n->t)
                    m = mg_pf_tmap(s, n->p, other->t);
            }
            break;
        case 4:
            if (n->tag == MG_PF_TPAP)
                m = mg_pf_tpap(s, n->p, mg_tp_arr(s, n->tp, mg_tp_prop(s)));
            break;
        }
        if (!m || m == n)
            continue;
        MgPf *r = replace(s, proof, n, m, 0);
        if (r && r != proof) {
            *kind = k;
            return r;
        }
    }
    return NULL;
}

static MgStatus check(MgTheory *t, MgTm *claim, MgPf *proof, bool reference,
                      uint64_t limit) {
    t->reference_conversion = reference;
    MgBudget b;
    mg_budget_init(&b, limit);
    MgCheck c;
    mg_check_init(&c, mg_theory_snapshot(t), &b, NULL);
    if (!proof->payload_protected)
        return MG_MALFORMED;
    MgStatus st = mg_check_proposition(&c, 0, NULL, claim)
        ? mg_check_theorem_proof(&c, claim, proof) : c.status;
    t->reference_conversion = false;
    return st;
}

typedef struct {
    MgDeepCall call;
    int argc;
    char **argv;
    int code;
} Main;

static void run(MgDeepCall *call) {
    Main *m = (Main *)call;
    MgProfileTag tag;
    if (!mg_profile_from_name(m->argv[1], &tag)) {
        m->code = 2;
        return;
    }
    int per = m->argc > 3 ? atoi(m->argv[3]) : 4;
    uint64_t limit = m->argc > 4 ? strtoull(m->argv[4], NULL, 10) : 20000000u;
    if (m->argc > 5)
        g_rng = strtoull(m->argv[5], NULL, 10) | 1u;
    double max_seconds = m->argc > 6 ? atof(m->argv[6]) : 0.0;
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    uint64_t covered = 0, total = 0;
    bool stopped_early = false;
    MgStatus st;
    MgTheory *t = mg_theory_new(tag, "diff", &st);
    MgImportPolicy policy = {.all_axioms = true};
    MgImportReport r;
    MgBudget b;
    mg_budget_init(&b, 0);
    st = mg_import_sexprinfo_file(t, m->argv[2], &policy, &b, &r);
    printf("import %s theorems=%llu\n", mg_status_name(st), (unsigned long long)r.theorems);
    if (r.stopped)
        printf("  stopped: %s\n", r.message);
    uint32_t *knowns = malloc(sizeof(uint32_t) * (t->decl_len + 1));
    size_t known_count = 0;
    for (uint64_t i = 0; i < t->decl_len; ++i)
        if (t->decls[i]->ns == MG_NS_KNOWN)
            knowns[known_count++] = t->decls[i]->name;
    uint64_t agree_ok = 0, agree_refused = 0, disagree = 0, undecided = 0, originals = 0;
    uint64_t by_kind[5] = {0};
    double lazy_s = 0, ref_s = 0;
    for (uint64_t i = 0; i < t->decl_len; ++i)
        if (t->decls[i]->kind == MG_DECL_THEOREM && t->decls[i]->proof)
            ++total;
    for (uint64_t i = 0; i < t->decl_len; ++i) {
        MgDecl *d = t->decls[i];
        if (d->kind != MG_DECL_THEOREM || !d->proof)
            continue;
        if (max_seconds > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((double)(now.tv_sec - started.tv_sec) +
                    (double)(now.tv_nsec - started.tv_nsec) * 1e-9 > max_seconds) {
                stopped_early = true;
                break;
            }
        }
        uint64_t before_agree = agree_ok + agree_refused, before_dis = disagree,
                 before_und = undecided;
        PfList nodes = {0};
        collect(&nodes, d->proof);
        for (int k = -1; k < per; ++k) {
            int kind = -1;
            MgPf *proof = k < 0 ? d->proof : mutate(t, d->proof, &nodes, knowns, known_count, &kind);
            if (!proof)
                continue;
            struct timespec a0, a1, a2;
            clock_gettime(CLOCK_MONOTONIC, &a0);
            MgStatus lz = check(t, d->prop, proof, false, 0);
            clock_gettime(CLOCK_MONOTONIC, &a1);
            MgStatus rf = check(t, d->prop, proof, true, limit);
            clock_gettime(CLOCK_MONOTONIC, &a2);
            lazy_s += (double)(a1.tv_sec - a0.tv_sec) + (double)(a1.tv_nsec - a0.tv_nsec) * 1e-9;
            ref_s += (double)(a2.tv_sec - a1.tv_sec) + (double)(a2.tv_nsec - a1.tv_nsec) * 1e-9;
            if (k < 0)
                ++originals;
            if (mg_status_undecided(rf) || mg_status_undecided(lz)) {
                ++undecided;
                continue;
            }
            if (lz == rf) {
                if (lz == MG_OK)
                    ++agree_ok;
                else
                    ++agree_refused;
                if (kind >= 0)
                    ++by_kind[kind];
            } else {
                ++disagree;
                const MgName *n = mg_name_get(&t->store, d->name);
                printf("DISAGREE %s %s mutation=%d lazy=%s reference=%s\n",
                       d->source_name ? d->source_name : "", n ? n->text : "", kind,
                       mg_status_name(lz), mg_status_name(rf));
            }
        }
        free(nodes.nodes);
        ++covered;
        const MgName *n = mg_name_get(&t->store, d->name);
        printf("theorem %llu/%llu %s agree=%llu disagree=%llu undecided=%llu\n",
               (unsigned long long)covered, (unsigned long long)total,
               d->source_name ? d->source_name : (n ? n->text : ""),
               (unsigned long long)(agree_ok + agree_refused - before_agree),
               (unsigned long long)(disagree - before_dis),
               (unsigned long long)(undecided - before_und));
    }
    if (stopped_early)
        printf("stopped after %.0fs: %llu of %llu theorems covered\n", max_seconds,
               (unsigned long long)covered, (unsigned long long)total);
    printf("(MgDifferential theorems=%llu/%llu originals=%llu agree-accepted=%llu agree-refused=%llu "
           "disagree=%llu reference-undecided=%llu mutation-kinds=%llu/%llu/%llu/%llu/%llu "
           "lazy=%.2fs reference=%.2fs)\n",
           (unsigned long long)covered, (unsigned long long)total,
           (unsigned long long)originals, (unsigned long long)agree_ok,
           (unsigned long long)agree_refused, (unsigned long long)disagree,
           (unsigned long long)undecided, (unsigned long long)by_kind[0],
           (unsigned long long)by_kind[1], (unsigned long long)by_kind[2],
           (unsigned long long)by_kind[3], (unsigned long long)by_kind[4], lazy_s, ref_s);
    free(knowns);
    mg_theory_release(t);
    m->code = disagree ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s PROFILE EXPORT [MUTANTS] [REFERENCE-WORK-LIMIT] [SEED] [MAX-SECONDS]\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    Main m = {.argc = argc, .argv = argv};
    m.call.run = run;
    if (!mg_deep_call(&m.call, NULL, NULL))
        return 2;
    mg_deep_call_shutdown();
    return m.code;
}
