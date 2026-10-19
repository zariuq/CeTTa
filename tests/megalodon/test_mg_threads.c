/*
 * The kernel's concurrency contract, as the CeTTa module uses it: every
 * kernel call and every session release runs under the theory's lock;
 * theorem records count their holders atomically and are released on any
 * thread without the lock (capsule release); a theory lives while any
 * theorem, session or caller holds it.
 *
 * THREADS threads share one theory.  Each repeatedly, under the lock,
 * checks a proof tree, makes LCF steps over theorems other threads made,
 * and runs a session; theorems pass between threads through a shared pool
 * and are released by whichever thread drops them last.  Every thread races
 * to publish the same name (exactly one wins; every other attempt is
 * refused or conflicts and changes nothing) and publishes names of its own
 * (all win).  Run plain, under ASan/UBSan and under TSan (gcc).
 *
 *   test_mg_threads [THREADS [ROUNDS]]
 */
#include "mg_core.h"
#include "mg_session.h"
#include "mg_sexpr.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MgTheory *T;
static uint32_t g_A, g_AA;
static int g_threads = 8, g_rounds = 400;

#define POOL 32
static _Atomic(MgThm *) g_pool[POOL];
static atomic_int g_race_won, g_race_lost, g_own_won, g_own_lost, g_steps, g_sessions, g_errors;

static void fail(const char *what) {
    atomic_fetch_add(&g_errors, 1);
    printf("FAIL %s\n", what);
}

/* Two rows, [A] |- A by hyp 0 and |- A -> A by proofLam, as a row source
 * that owns its arena (released exactly once, at the session's end). */
typedef struct {
    MgSxArena *arena;
    uint64_t *parents;
    uint32_t cap;
    atomic_int *released;
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
    if (address < 1 || address > 2) {
        *reason = "no row";
        return MG_MALFORMED;
    }
    mg_sx_arena_reset(r->arena);
    MgSxReader rd;
    mg_sx_reader_init(&rd, g_rows[address - 1], strlen(g_rows[address - 1]));
    MgSx *x = mg_sx_read(&rd, r->arena);
    bool nomem = false;
    if (!x || !mg_sx_wire_row(&theory->store, x, out, &r->parents, &r->cap, &nomem)) {
        *reason = "undecodable";
        return nomem ? MG_NOMEM : MG_MALFORMED;
    }
    return MG_OK;
}
static atomic_int g_released;
static void rows_release(void *context) {
    Rows *r = context;
    atomic_fetch_add(r->released, 1);
    mg_sx_arena_free(r->arena);
    free(r->parents);
    free(r);
}

static void drop(MgThm *t) { mg_thm_release(t); }   /* no lock: as a capsule release */

static void *worker(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t seed = 2654435761u * (uint32_t)(id + 1);
    for (int round = 0; round < g_rounds; ++round) {
        seed = seed * 1664525u + 1013904223u;
        MgThm *made = NULL, *mine = NULL;
        const char *why = NULL;
        /* a tree: \h : AA. h (lazy: the hypothesis stays AA) */
        pthread_mutex_lock(&T->lock);
        MgStore *s = &T->store;
        MgPf *proof = mg_pf_plam(s, mg_tm_name(s, g_AA), mg_pf_hyp(s, 0));
        if (mg_thm_check_tree(T, s->ctx_empty, proof, NULL, &why, &made) != MG_OK)
            fail("tree");
        /* LCF over a theorem some thread made: (\h:AA. h) applied to the
         * hypothesis AA, then discharged again */
        MgThm *other = atomic_exchange(&g_pool[seed % POOL], NULL);
        if (other) {
            MgCtx *ctx = mg_ctx(s, 0, NULL, mg_tmlist_cons(s, mg_tm_name(s, g_AA), NULL));
            MgWitness hyp = {.rule = MG_RULE_HYP, .index = 0};
            MgWitness papp = {.rule = MG_RULE_PROOF_APP};
            MgWitness plam = {.rule = MG_RULE_PROOF_LAM};
            MgThm *h = NULL, *app = NULL;
            /* other: |- AA -> AA, closed; h: [AA] |- AA.  Contexts differ:
             * weaken nothing, just check the step is refused or accepted
             * consistently, then make a closed theorem from h */
            if (mg_thm_step(T, &hyp, ctx, NULL, 0, NULL, &why, &h) != MG_OK)
                fail("hyp");
            MgThm *two[2] = {other, h};
            MgStatus st = mg_thm_step(T, &papp, NULL, two, 2, NULL, &why, &app);
            if (st != MG_REFUSED || app)
                fail("proofApp across different contexts must be refused");
            /* accepted across threads: other : |- AA -> AA applied to
             * |- A -> A, which is AA up to conversion */
            MgThm *aa = NULL, *res = NULL;
            MgPf *id = mg_pf_plam(s, mg_tm_name(s, g_A), mg_pf_hyp(s, 0));
            if (mg_thm_check_tree(T, s->ctx_empty, id, NULL, &why, &aa) != MG_OK)
                fail("tree A -> A");
            MgThm *ok2[2] = {other, aa};
            if (aa && (mg_thm_step(T, &papp, NULL, ok2, 2, NULL, &why, &res) != MG_OK ||
                       res->judgment.prop != mg_tm_name(s, g_AA)))
                fail("proofApp over another thread's theorem, up to conversion");
            drop(res);
            drop(aa);
            if (h && mg_thm_step(T, &plam, NULL, &h, 1, NULL, &why, &mine) != MG_OK)
                fail("proofLam");
            atomic_fetch_add(&g_steps, 4);
            pthread_mutex_unlock(&T->lock);
            drop(h);
            drop(other);
        } else {
            pthread_mutex_unlock(&T->lock);
        }
        /* hand the tree theorem to the pool; drop what it replaces */
        drop(atomic_exchange(&g_pool[(seed >> 8) % POOL], made));
        /* a session, released under the lock */
        if (round % 4 == 0) {
            Rows *r = calloc(1, sizeof(*r));
            r->arena = mg_sx_arena_new();
            r->released = &g_released;
            pthread_mutex_lock(&T->lock);
            MgStatus st;
            MgSession *sess = mg_session_begin(T, (MgRowSource){rows_revision, rows_read, rows_release, r}, &st);
            const uint64_t schedule[2] = {1, 2};
            MgSessionReport rep;
            MgThm *fin = NULL;
            if (!sess || mg_session_resume(sess, schedule, 2, 0, NULL, &rep) != MG_OK ||
                rep.status != MG_SESSION_FINISHED ||
                mg_session_finish(sess, 2, mg_tm_imp(s, mg_tm_name(s, g_A), mg_tm_name(s, g_A)), NULL,
                                  &why, &fin) != MG_OK)
                fail("session");
            mg_session_release(sess);
            pthread_mutex_unlock(&T->lock);
            atomic_fetch_add(&g_sessions, 1);
            drop(fin);   /* the theorem outlives its session */
        }
        /* publication races */
        if (mine && round % 10 == 0) {
            char name[64];
            snprintf(name, sizeof name, "own_%d_%d", id, round);
            pthread_mutex_lock(&T->lock);
            uint64_t rev = T->rev;
            const MgDecl *d = NULL;
            MgStatus st = mg_thm_publish(T, mine, mg_name_intern(&T->store, "race", 4), NULL, NULL, &why, &d);
            if (st == MG_OK) {
                atomic_fetch_add(&g_race_won, 1);
                if (T->rev != rev + 1u)
                    fail("a won race advances the revision by one");
            } else {
                atomic_fetch_add(&g_race_lost, 1);
                if ((st != MG_REFUSED && st != MG_CONFLICT) || T->rev != rev)
                    fail("a lost race changes nothing");
            }
            rev = T->rev;
            st = mg_thm_publish(T, mine, mg_name_intern(&T->store, name, strlen(name)), NULL, NULL, &why, &d);
            if (st == MG_OK && T->rev == rev + 1u)
                atomic_fetch_add(&g_own_won, 1);
            else
                atomic_fetch_add(&g_own_lost, 1);
            pthread_mutex_unlock(&T->lock);
        }
        drop(mine);
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc > 1)
        g_threads = atoi(argv[1]);
    if (argc > 2)
        g_rounds = atoi(argv[2]);
    MgStatus st;
    T = mg_theory_new(MG_PROFILE_CUSTOM, "threads", &st);
    MgStore *s = &T->store;
    g_A = mg_name_intern(s, "A", 1);
    g_AA = mg_name_intern(s, "AA", 2);
    MgBudget b;
    mg_budget_init(&b, 0);
    MgItem pa = {.kind = MG_ITEM_PARAMETER, .name = g_A, .type = mg_tp_prop(s)};
    MgItem da = {.kind = MG_ITEM_DEFINITION, .name = g_AA, .type = mg_tp_prop(s),
                 .body = mg_tm_imp(s, mg_tm_name(s, g_A), mg_tm_name(s, g_A))};
    if (mg_theory_associate_parameter(T, g_A, pa.type) != MG_OK ||
        mg_theory_admit(T, &b, &pa, 0, NULL) != MG_OK || mg_theory_admit(T, &b, &da, 0, NULL) != MG_OK) {
        printf("setup failed\n");
        return 2;
    }
    uint64_t decls0 = T->decl_len;
    pthread_t *th = calloc((size_t)g_threads, sizeof(pthread_t));
    for (int i = 0; i < g_threads; ++i)
        pthread_create(&th[i], NULL, worker, (void *)(intptr_t)i);
    for (int i = 0; i < g_threads; ++i)
        pthread_join(th[i], NULL);
    free(th);
    for (int i = 0; i < POOL; ++i)
        drop(atomic_exchange(&g_pool[i], NULL));
    int won = atomic_load(&g_race_won), own = atomic_load(&g_own_won);
    int sessions = atomic_load(&g_sessions), released = atomic_load(&g_released);
    if (won != 1)
        fail("exactly one thread publishes the shared name");
    if (atomic_load(&g_own_lost) != 0)
        fail("every distinct name publishes");
    if (T->decl_len != decls0 + 1u + (uint64_t)own)
        fail("the theory holds exactly the published declarations");
    if (released != sessions)
        fail("every session released its source exactly once");
    printf("(threads %d rounds %d: steps %d sessions %d shared-name wins %d losses %d own names %d)\n",
           g_threads, g_rounds, atomic_load(&g_steps), sessions, won, atomic_load(&g_race_lost), own);
    mg_theory_release(T);
    printf("(MgThreadsSummary %d)\n", atomic_load(&g_errors));
    return atomic_load(&g_errors) ? 1 : 0;
}
