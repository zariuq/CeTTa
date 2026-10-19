/*
 * Native Megalodon kernel: retained sessions, theorem records, LCF steps
 * and publication.
 *
 * Each case states its independently expected verdict.  The controls follow
 * NativeSessionControls.lean, SourceSessionControls.lean,
 * BorrowedProofCursorControls.lean, ObservedCursor.lean (statuses, exact
 * suffixes), CursorEnvironment.lean (revalidation) and NativePublication.lean
 * (failed commits leave the live theory unchanged).
 */
#include "mg_core.h"
#include "mg_session.h"
#include "mg_sexpr.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_fail;
#define EXPECT(cond, label) do { \
        if (cond) { ++g_pass; } \
        else { ++g_fail; printf("FAIL %s (%s:%d)\n", label, __FILE__, __LINE__); } \
    } while (0)

static MgTheory *T;
static MgSxArena *A;

static MgSx *sx(const char *text) {
    MgSxReader r;
    mg_sx_reader_init(&r, text, strlen(text));
    MgSx *x = mg_sx_read(&r, A);
    if (!x) {
        printf("parse error in test data: %s\n", text);
        exit(2);
    }
    return x;
}
static MgTm *TM(const char *w) { return mg_sx_wire_tm(&T->store, sx(w)); }
static MgTp *TP(const char *w) { return mg_sx_wire_tp(&T->store, sx(w)); }
static uint32_t NM(const char *n) { return mg_name_intern(&T->store, n, strlen(n)); }

static MgTheory *new_theory(void) {
    MgStatus st;
    MgTheory *t = mg_theory_new(MG_PROFILE_CUSTOM, "session-test", &st);
    if (!t) {
        printf("theory creation failed: %s\n", mg_status_name(st));
        exit(2);
    }
    return t;
}

static void fresh_theory(void) {
    if (T)
        mg_theory_release(T);
    T = new_theory();
}

static MgStatus admit_item(MgItemKind kind, const char *name, const char *type,
                           const char *prop) {
    MgItem item = {0};
    item.kind = kind;
    item.name = NM(name);
    if (type)
        item.type = TP(type);
    if (prop)
        item.prop = TM(prop);
    MgBudget b;
    mg_budget_init(&b, 0);
    return mg_theory_admit(T, &b, &item, 0, NULL);
}

static void parameter(const char *name, const char *type) {
    EXPECT(mg_theory_associate_parameter(T, NM(name), TP(type)) == MG_OK, name);
    EXPECT(admit_item(MG_ITEM_PARAMETER, name, type, NULL) == MG_OK, name);
}

static void axiom(const char *name, const char *prop) {
    EXPECT(mg_theory_associate_known(T, NM(name), TM(prop)) == MG_OK, name);
    EXPECT(admit_item(MG_ITEM_KNOWN, name, NULL, prop) == MG_OK, name);
}

/* ── a borrowed test row store ────────────────────────────────────────── */

typedef struct {
    uint64_t address;
    char *text;
} StoreRow;

typedef struct {
    StoreRow rows[96];
    uint32_t len;
    uint64_t revision;
    int releases;
    uint64_t reads;
    MgSxArena *arena;
    uint64_t *parents;
    uint32_t parents_cap;
} TestStore;

static uint64_t store_revision(void *context) {
    return ((TestStore *)context)->revision;
}

static MgStatus store_read(void *context, MgTheory *theory, uint64_t address,
                           MgRow *out, const char **reason) {
    TestStore *st = context;
    ++st->reads;
    for (uint32_t i = 0; i < st->len; ++i) {
        if (st->rows[i].address != address)
            continue;
        mg_sx_arena_reset(st->arena);
        MgSxReader r;
        mg_sx_reader_init(&r, st->rows[i].text, strlen(st->rows[i].text));
        MgSx *x = mg_sx_read(&r, st->arena);
        if (!x) {
            if (r.error && strcmp(r.error, "out of memory") == 0)
                return MG_NOMEM;
            *reason = "unreadable row";
            return MG_MALFORMED;
        }
        uint64_t before = theory->store.resource_failures;
        bool nomem = false;
        if (!mg_sx_wire_row(&theory->store, x, out, &st->parents,
                            &st->parents_cap, &nomem)) {
            if (nomem || theory->store.resource_failures != before)
                return MG_NOMEM;
            *reason = "undecodable row";
            return MG_MALFORMED;
        }
        return MG_OK;
    }
    *reason = "no row at this address";
    return MG_MALFORMED;
}

static void store_release(void *context) {
    ((TestStore *)context)->releases += 1;
}

static void store_init(TestStore *st) {
    memset(st, 0, sizeof(*st));
    st->revision = 1;
    st->arena = mg_sx_arena_new();
}

static void store_free(TestStore *st) {
    for (uint32_t i = 0; i < st->len; ++i)
        free(st->rows[i].text);
    mg_sx_arena_free(st->arena);
    mg_sys_free(st->parents);
}

static void put(TestStore *st, uint64_t address, const char *fmt, ...) {
    char buffer[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, ap);
    va_end(ap);
    for (uint32_t i = 0; i < st->len; ++i)
        if (st->rows[i].address == address) {
            free(st->rows[i].text);
            st->rows[i].text = strdup(buffer);
            return;
        }
    st->rows[st->len].address = address;
    st->rows[st->len].text = strdup(buffer);
    ++st->len;
}

static MgRowSource source_of(TestStore *st) {
    return (MgRowSource){store_revision, store_read, store_release, st};
}

static MgSession *begin(TestStore *st) {
    MgStatus status;
    MgSession *s = mg_session_begin(T, source_of(st), &status);
    if (!s) {
        printf("session begin failed\n");
        exit(2);
    }
    return s;
}

static MgSessionReport run(MgSession *s, const uint64_t *schedule, uint64_t n,
                           uint64_t bound, uint64_t limit) {
    MgBudget b;
    mg_budget_init(&b, limit);
    MgSessionReport r;
    mg_session_resume(s, schedule, n, bound, &b, &r);
    return r;
}

static bool pending_is(const MgSessionReport *r, const uint64_t *expected,
                       uint64_t n) {
    if (r->pending_len != n)
        return false;
    for (uint64_t i = 0; i < n; ++i)
        if (r->pending[i] != expected[i])
            return false;
    return true;
}

/* ── shared data ──────────────────────────────────────────────────────── */

#define CTX0 "(Megalodon.context 0 (types) (hypotheses))"
#define PRED "(Tp.arr (Tp.var 0) (Tp.prop))"
#define QV "(Tm.all (Tp.var 0) (Tm.app (Tm.db 1) (Tm.db 0)))"
#define BODY "(Tm.all " PRED " (Tm.imp " QV " " QV "))"
#define SCHEMA "(Tm.typeAll " BODY ")"
#define Q0 "(Tm.all (Tp.base 0) (Tm.app (Tm.db 1) (Tm.db 0)))"
#define BODY0 "(Tm.all (Tp.arr (Tp.base 0) (Tp.prop)) (Tm.imp " Q0 " " Q0 "))"

/* The polymorphic predicate-identity schema and its base-type instance:
 *   0: <1,[A->prop],[Q]>  |- Q                    hyp 0
 *   1: <1,[A->prop],[]>   |- Q -> Q               proofLam Q        (0)
 *   2: <1,[],[]>          |- all P. Q -> Q        termLam           (1)
 *   3: <0,[],[]>          |- typeAll (...)        typeLam           (2)
 *   4: <0,[],[]>          |- (...)[A := base 0]   typeApp (base 0)  (3) */
static void put_schema(TestStore *st) {
    put(st, 0, "(Megalodon.row (Megalodon.judgment (Megalodon.context 1 (types " PRED
        ") (hypotheses " QV ")) " QV ") (Megalodon.hyp 0) (parents) 0)");
    put(st, 1, "(Megalodon.row (Megalodon.judgment (Megalodon.context 1 (types " PRED
        ") (hypotheses)) (Tm.imp " QV " " QV ")) (Megalodon.proofLam " QV ") (parents 0) 1)");
    put(st, 2, "(Megalodon.row (Megalodon.judgment (Megalodon.context 1 (types) "
        "(hypotheses)) " BODY ") (Megalodon.termLam) (parents 1) 2)");
    put(st, 3, "(Megalodon.row (Megalodon.judgment " CTX0 " " SCHEMA
        ") (Megalodon.typeLam) (parents 2) 3)");
    put(st, 4, "(Megalodon.row (Megalodon.judgment " CTX0 " " BODY0
        ") (Megalodon.typeApp (Tp.base 0) " BODY ") (parents 3) 4)");
}
static const uint64_t SCHEMA_ROWS[] = {0, 1, 2, 3, 4};

#define QP "(Tm.named \"q\")"
#define IMPQ "(Tm.imp " QP " " QP ")"
#define IMPI "(Tm.imp " IMPQ " " IMPQ ")"

/* Implication rows over a parameter q : prop, with a genuinely shared DAG
 * (13 is cited by 14 and 16; 14 is cited by 16). */
static void put_implication(TestStore *st) {
    put(st, 10, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) "
        "(hypotheses " QP ")) " QP ") (Megalodon.hyp 0) (parents) 10)");
    put(st, 11, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofLam " QP ") (parents 10) 11)");
    put(st, 12, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) "
        "(hypotheses " IMPQ ")) " IMPQ ") (Megalodon.hyp 0) (parents) 12)");
    put(st, 13, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPI
        ") (Megalodon.proofLam " IMPQ ") (parents 12) 13)");
    put(st, 14, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 13 11) 14)");
    /* swapped parents */
    put(st, 15, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 11 13) 15)");
    /* shared: 13 again, and 14 */
    put(st, 16, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 13 14) 16)");
    /* a repeated edge stays repeated, and does not match the rule */
    put(st, 17, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 14 14) 17)");
    /* future parent 19 */
    put(st, 18, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 13 19) 18)");
    put(st, 19, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 13 11) 19)");
    /* three checked parents: not the rule's requirements */
    put(st, 20, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 13 11 11) 20)");
    /* malformed witness arity */
    put(st, 21, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp) (parents 13 11) 21)");
    /* an axiom that is not selected */
    put(st, 22, "(Megalodon.row (Megalodon.judgment " CTX0 " " QP
        ") (Megalodon.known \"missing_ax\") (parents) 22)");
    /* an unsafe hidden substitution operand */
    put(st, 23, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types "
        "(Tp.base 0)) (hypotheses)) " QP ") (Megalodon.termApp (Tm.typeLam "
        "(Tm.db 0)) (Tp.base 0) " QP ") (parents 13) 23)");
    /* wrong context: no hypothesis 0 */
    put(st, 24, "(Megalodon.row (Megalodon.judgment " CTX0 " " QP
        ") (Megalodon.hyp 0) (parents) 24)");
    /* unsafe stated proposition */
    put(st, 25, "(Megalodon.row (Megalodon.judgment " CTX0
        " (Tm.typeLam (Tm.db 0))) (Megalodon.hyp 0) (parents) 25)");
    /* a row citing itself */
    put(st, 26, "(Megalodon.row (Megalodon.judgment " CTX0 " " IMPQ
        ") (Megalodon.proofApp " IMPQ ") (parents 13 26) 26)");
    /* type generalization in a nonempty hypothesis context */
    put(st, 27, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) "
        "(hypotheses " QP ")) (Tm.typeAll " QP ")) (Megalodon.typeLam) (parents 10) 27)");
    /* the selected axiom ax : q */
    put(st, 40, "(Megalodon.row (Megalodon.judgment " CTX0 " " QP
        ") (Megalodon.known \"ax\") (parents) 40)");
    put(st, 41, "(Megalodon.row (Megalodon.judgment " CTX0 " " QP
        ") (Megalodon.proofApp " QP ") (parents 11 40) 41)");
}

#define DOM "(Tm.all (Tp.base 0) (Tm.app (Tm.named \"p\") (Tm.db 0)))"
/* MathdataKernel forallIdentityProof as rows: termApp and termLam. */
static void put_forall(TestStore *st) {
    put(st, 32, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types "
        "(Tp.base 0)) (hypotheses " DOM ")) " DOM ") (Megalodon.hyp 0) (parents) 32)");
    put(st, 31, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types "
        "(Tp.base 0)) (hypotheses " DOM ")) (Tm.app (Tm.named \"p\") (Tm.db 0))) "
        "(Megalodon.termApp (Tm.db 0) (Tp.base 0) (Tm.app (Tm.named \"p\") (Tm.db 0))) "
        "(parents 32) 31)");
    put(st, 30, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types) "
        "(hypotheses " DOM ")) " DOM ") (Megalodon.termLam) (parents 31) 30)");
    put(st, 33, "(Megalodon.row (Megalodon.judgment " CTX0 " (Tm.imp " DOM " " DOM
        ")) (Megalodon.proofLam " DOM ") (parents 30) 33)");
    /* termApp at the wrong argument type */
    put(st, 34, "(Megalodon.row (Megalodon.judgment (Megalodon.context 0 (types "
        "(Tp.base 0)) (hypotheses " DOM ")) (Tm.app (Tm.named \"p\") (Tm.named \"p\"))) "
        "(Megalodon.termApp (Tm.named \"p\") (Tp.base 0) (Tm.app (Tm.named \"p\") (Tm.db 0))) "
        "(parents 32) 34)");
}
static const uint64_t FORALL_ROWS[] = {32, 31, 30, 33};

static void implication_theory(void) {
    fresh_theory();
    parameter("q", "(Tp.prop)");
    parameter("p", "(Tp.arr (Tp.base 0) (Tp.prop))");
    axiom("ax", QP);
}

/* ── sessions ─────────────────────────────────────────────────────────── */

static void test_schema_pause_resume(void) {
    fresh_theory();
    TestStore st;
    store_init(&st);
    put_schema(&st);

    MgSession *a = begin(&st);
    MgSessionReport r = run(a, SCHEMA_ROWS, 5, 2, 0);
    EXPECT(r.status == MG_SESSION_PAUSED && r.checked_rows == 2u, "row bound pauses after two rows");
    const uint64_t rest[] = {2, 3, 4};
    EXPECT(pending_is(&r, rest, 3), "pause keeps the exact unfinished suffix");
    MgSessionReport i1, i2;
    mg_session_inspect(a, &i1);
    mg_session_inspect(a, &i2);
    EXPECT(i1.status == MG_SESSION_PAUSED && i2.status == i1.status &&
           i1.checked_rows == i2.checked_rows && pending_is(&i2, rest, 3),
           "inspection is read-only");
    uint64_t reads_before = st.reads;
    mg_session_inspect(a, &i1);
    EXPECT(st.reads == reads_before, "inspection reads no rows");
    uint64_t copy[3];
    memcpy(copy, r.pending, sizeof copy);
    /* a different (finite) budget for the next call */
    r = run(a, copy, 3, 0, 100000);
    EXPECT(r.status == MG_SESSION_FINISHED && r.checked_rows == 5u && r.pending_len == 0u,
           "resume under a different budget finishes");

    /* the same statements as one uninterrupted run */
    MgSession *b = begin(&st);
    r = run(b, SCHEMA_ROWS, 5, 0, 0);
    EXPECT(r.status == MG_SESSION_FINISHED, "one-shot run finishes");
    bool same = true;
    for (uint64_t k = 0; k < 5; ++k) {
        MgJudgment x, y;
        same = same && mg_session_judgment(a, k, &x) && mg_session_judgment(b, k, &y) &&
               mg_judgment_eq(x, y);
    }
    EXPECT(same, "paused and uninterrupted sessions check the same judgments");

    const char *why = NULL;
    MgThm *schema = NULL, *instance = NULL, *bad = NULL;
    EXPECT(mg_session_finish(a, 3, TM(SCHEMA), NULL, &why, &schema) == MG_OK,
           "polymorphic schema publishes from the finished session");
    EXPECT(mg_session_finish(a, 4, TM(BODY0), NULL, &why, &instance) == MG_OK,
           "base-type instance publishes from the same session");
    EXPECT(mg_session_finish(a, 3, TM(BODY0), NULL, &why, &bad) == MG_REFUSED && !bad,
           "a claim that does not normalize to the root is refused");
    EXPECT(mg_session_finish(a, 2, TM(BODY), NULL, &why, &bad) == MG_REFUSED && !bad,
           "an open root (type depth 1) is not publishable");
    EXPECT(mg_session_finish(a, 99, TM(SCHEMA), NULL, &why, &bad) == MG_MALFORMED && !bad,
           "an unchecked root address is malformed");

    uint64_t rev = T->rev;
    const MgDecl *d = NULL;
    EXPECT(mg_thm_publish(T, schema, NM("poly_id"), NULL, NULL, &why, &d) == MG_OK && d &&
           T->rev == rev + 1u, "publish the schema as a known");
    EXPECT(mg_thm_publish(T, instance, NM("base_id"), NULL, NULL, &why, &d) == MG_OK,
           "publish the instance as a known");
    rev = T->rev;
    uint64_t decls = T->decl_len;
    EXPECT(mg_thm_publish(T, schema, NM("poly_id"), NULL, NULL, &why, &d) == MG_REFUSED &&
           T->rev == rev && T->decl_len == decls, "a duplicate commit changes nothing");

    /* later native use of the published schema (LCF typeApp) */
    MgThm *known = NULL, *inst2 = NULL;
    EXPECT(mg_thm_from_known(T, NM("poly_id"), &why, &known) == MG_OK, "published known is a theorem");
    MgWitness w = {.rule = MG_RULE_TYPE_APP, .type = TP("(Tp.base 0)")};
    EXPECT(mg_thm_step(T, &w, NULL, &known, 1, NULL, &why, &inst2) == MG_OK &&
           mg_judgment_eq(inst2->judgment, instance->judgment),
           "type instantiation of the published schema gives the instance");
    mg_thm_release(inst2);
    mg_thm_release(known);
    mg_thm_release(schema);
    mg_thm_release(instance);
    mg_session_release(a);
    mg_session_release(b);
    EXPECT(st.releases == 2, "each session released its source exactly once");
    store_free(&st);
}

typedef struct {
    uint64_t address;
    MgSessionStatus status;
    const char *label;
} RowCase;

static void test_rows_negative(void) {
    implication_theory();
    TestStore st;
    store_init(&st);
    put_implication(&st);
    put_forall(&st);
    const uint64_t base[] = {10, 11, 12, 13, 14};
    const RowCase cases[] = {
        {19, MG_SESSION_FINISHED, "implication elimination with ordered parents"},
        {15, MG_SESSION_REFUSED, "swapped parents are refused"},
        {17, MG_SESSION_REFUSED, "repeated edge does not satisfy the rule"},
        {18, MG_SESSION_MALFORMED, "future parent is malformed"},
        {20, MG_SESSION_REFUSED, "three checked parents are not the requirements"},
        {21, MG_SESSION_MALFORMED, "wrong witness arity is malformed"},
        {22, MG_SESSION_REFUSED, "an unselected axiom is refused"},
        {23, MG_SESSION_MALFORMED, "unsafe hidden operand is rejected at decoding"},
        {24, MG_SESSION_REFUSED, "a wrong context is refused"},
        {25, MG_SESSION_MALFORMED, "unsafe stated proposition is rejected at decoding"},
        {26, MG_SESSION_MALFORMED, "a self parent is malformed"},
        {27, MG_SESSION_REFUSED, "type generalization needs empty contexts"},
        {40, MG_SESSION_FINISHED, "a selected axiom is a known"},
        {77, MG_SESSION_MALFORMED, "an address without a row is malformed"},
    };
    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; ++k) {
        MgSession *s = begin(&st);
        MgSessionReport r = run(s, base, 5, 0, 0);
        bool prefix = r.status == MG_SESSION_FINISHED && r.checked_rows == 5u;
        uint64_t addr = cases[k].address;
        r = run(s, &addr, 1, 0, 0);
        bool ok = prefix && r.status == cases[k].status;
        if (cases[k].status != MG_SESSION_FINISHED)
            ok = ok && r.failed_address == addr && r.checked_rows == 5u &&
                 pending_is(&r, &addr, 1);
        EXPECT(ok, cases[k].label);
        if (!ok)
            printf("  got %s (%s)\n", mg_session_status_name(r.status),
                   r.reason ? r.reason : "");
        mg_session_release(s);
    }
    /* the shared DAG, all at once */
    {
        MgSession *s = begin(&st);
        const uint64_t dag[] = {10, 11, 12, 13, 14, 16, 40, 41};
        MgSessionReport r = run(s, dag, 8, 0, 0);
        EXPECT(r.status == MG_SESSION_FINISHED && r.checked_rows == 8u,
               "a genuinely shared DAG checks with each row once");
        const char *why = NULL;
        MgThm *t = NULL;
        EXPECT(mg_session_finish(s, 16, TM(IMPQ), NULL, &why, &t) == MG_OK,
               "shared-DAG root publishes");
        mg_thm_release(t);
        EXPECT(mg_session_finish(s, 41, TM(QP), NULL, &why, &t) == MG_OK,
               "axiom use publishes as an ordinary checked theorem");
        mg_thm_release(t);
        mg_session_release(s);
    }
    /* duplicate scheduled address */
    {
        MgSession *s = begin(&st);
        const uint64_t dup[] = {10, 11, 10, 12};
        MgSessionReport r = run(s, dup, 4, 0, 0);
        const uint64_t tail[] = {10, 12};
        EXPECT(r.status == MG_SESSION_MALFORMED && r.failed_address == 10u &&
               r.checked_rows == 2u && pending_is(&r, tail, 2),
               "a duplicate scheduled address is malformed and keeps its suffix");
        mg_session_release(s);
    }
    /* a valid early root must not conceal an invalid unused tail */
    {
        MgSession *s = begin(&st);
        const uint64_t sched[] = {10, 11, 12, 13, 14, 15};
        MgSessionReport r = run(s, sched, 6, 0, 0);
        EXPECT(r.status == MG_SESSION_REFUSED && r.failed_address == 15u,
               "the invalid tail row stops the batch");
        uint64_t rev = T->rev;
        const char *why = NULL;
        MgThm *t = NULL;
        EXPECT(mg_session_finish(s, 14, TM(IMPQ), NULL, &why, &t) == MG_UNFINISHED && !t &&
               T->rev == rev, "valid root with an invalid suffix does not publish");
        mg_session_release(s);
    }
    /* termApp / termLam rows */
    {
        MgSession *s = begin(&st);
        MgSessionReport r = run(s, FORALL_ROWS, 4, 0, 0);
        EXPECT(r.status == MG_SESSION_FINISHED, "forall identity rows check");
        const char *why = NULL;
        MgThm *t = NULL;
        char claim[512];
        snprintf(claim, sizeof claim, "(Tm.imp %s %s)", DOM, DOM);
        EXPECT(mg_session_finish(s, 33, TM(claim), NULL, &why, &t) == MG_OK,
               "forall identity publishes");
        mg_thm_release(t);
        uint64_t wrong = 34;
        r = run(s, &wrong, 1, 0, 0);
        EXPECT(r.status == MG_SESSION_REFUSED, "term argument of the wrong type is refused");
        mg_session_release(s);
    }
    store_free(&st);
}

static bool interrupt_flag;
static bool interrupt_now(void *context) {
    (void)context;
    return interrupt_flag;
}

static void test_budgets(void) {
    fresh_theory();
    TestStore st;
    store_init(&st);
    put_schema(&st);
    MgSession *ref = begin(&st);
    MgSessionReport full = run(ref, SCHEMA_ROWS, 5, 0, 0);
    EXPECT(full.status == MG_SESSION_FINISHED, "unlimited default finishes");
    /* Work units count work actually done: node results cached by the
     * first run are not recomputed, so measure the warm cost. */
    MgSession *warm = begin(&st);
    MgSessionReport again = run(warm, SCHEMA_ROWS, 5, 0, 0);
    uint64_t needed = again.work_used;
    EXPECT(again.status == MG_SESSION_FINISHED && needed <= full.work_used && needed > 5u,
           "rows and rule work are both charged");
    mg_session_release(warm);

    bool monotone = true, suffixes = true, resumed = true;
    uint64_t last_checked = 0;
    uint64_t exhausted_runs = 0;
    for (uint64_t limit = 1; limit <= needed; ++limit) {
        MgSession *s = begin(&st);
        MgSessionReport r = run(s, SCHEMA_ROWS, 5, 0, limit);
        if (limit < needed) {
            if (r.status != MG_SESSION_EXHAUSTED)
                resumed = false;
            ++exhausted_runs;
        }
        if (r.checked_rows < last_checked)
            monotone = false;
        last_checked = r.checked_rows;
        if (r.pending_len != 5u - r.checked_rows ||
            (r.pending_len && r.pending[0] != SCHEMA_ROWS[r.checked_rows]))
            suffixes = false;
        if (r.status != MG_SESSION_FINISHED) {
            uint64_t copy[5];
            uint64_t n = r.pending_len;
            memcpy(copy, r.pending, sizeof(uint64_t) * n);
            r = run(s, copy, n, 0, 0);
            if (r.status != MG_SESSION_FINISHED)
                resumed = false;
        }
        for (uint64_t k = 0; k < 5; ++k) {
            MgJudgment x, y;
            if (!mg_session_judgment(s, k, &x) || !mg_session_judgment(ref, k, &y) ||
                !mg_judgment_eq(x, y))
                resumed = false;
        }
        mg_session_release(s);
    }
    EXPECT(exhausted_runs == needed - 1u, "every smaller budget is exhausted, not refused");
    EXPECT(monotone, "more budget never checks fewer rows");
    EXPECT(suffixes, "every stop keeps the exact suffix with the stopping row");
    EXPECT(resumed, "exhaustion then retry checks the same judgments");

    /* exact boundary */
    {
        MgSession *s = begin(&st);
        MgSessionReport r = run(s, SCHEMA_ROWS, 5, 0, needed);
        EXPECT(r.status == MG_SESSION_FINISHED, "exact budget finishes");
        mg_session_release(s);
        s = begin(&st);
        r = run(s, SCHEMA_ROWS, 5, 0, needed - 1u);
        EXPECT(r.status == MG_SESSION_EXHAUSTED, "one unit less is exhausted");
        mg_session_release(s);
    }
    /* interruption is polled on every row */
    {
        MgSession *s = begin(&st);
        MgBudget b;
        mg_budget_init(&b, 0);
        b.interrupted = interrupt_now;
        interrupt_flag = true;
        MgSessionReport r;
        mg_session_resume(s, SCHEMA_ROWS, 5, 0, &b, &r);
        EXPECT(r.status == MG_SESSION_INTERRUPTED && r.checked_rows == 0u &&
               r.pending_len == 5u, "interruption stops before the first row");
        interrupt_flag = false;
        mg_budget_init(&b, 0);
        b.interrupted = interrupt_now;
        uint64_t copy[5];
        memcpy(copy, r.pending, sizeof copy);
        mg_session_resume(s, copy, 5, 0, &b, &r);
        EXPECT(r.status == MG_SESSION_FINISHED, "resume after interruption finishes");
        mg_session_release(s);
    }
    /* hypothesis rows are charged too (the MM0 draft's bypass) */
    {
        TestStore many;
        store_init(&many);
        for (uint64_t k = 0; k < 90; ++k)
            put(&many, 100u + k, "(Megalodon.row (Megalodon.judgment (Megalodon.context 1 "
                "(types " PRED ") (hypotheses " QV ")) " QV ") (Megalodon.hyp 0) (parents) %llu)",
                (unsigned long long)k);
        uint64_t sched[90];
        for (uint64_t k = 0; k < 90; ++k)
            sched[k] = 100u + k;
        MgSession *s = begin(&many);
        MgSessionReport r = run(s, sched, 90, 0, 20);
        EXPECT(r.status == MG_SESSION_EXHAUSTED && r.checked_rows < 20u,
               "a budget of 20 does not accept 90 hypothesis rows");
        mg_session_release(s);
        store_free(&many);
    }
    mg_session_release(ref);
    store_free(&st);
}

static void test_store_revision(void) {
    fresh_theory();
    TestStore st;
    store_init(&st);
    put_schema(&st);
    MgSession *s = begin(&st);
    MgSessionReport r = run(s, SCHEMA_ROWS, 5, 2, 0);
    EXPECT(r.status == MG_SESSION_PAUSED, "paused before the store changes");
    st.revision += 1;
    uint64_t rest[] = {2, 3, 4};
    r = run(s, rest, 3, 0, 0);
    EXPECT(r.status == MG_SESSION_MALFORMED && r.checked_rows == 2u &&
           pending_is(&r, rest, 3), "a changed row store is not read silently");
    EXPECT(mg_session_rebase(s) == MG_OK, "rebase adopts a revision with unchanged checked rows");
    r = run(s, rest, 3, 0, 0);
    EXPECT(r.status == MG_SESSION_FINISHED, "after rebase the walk continues");
    /* change a checked row */
    put(&st, 1, "(Megalodon.row (Megalodon.judgment (Megalodon.context 1 (types " PRED
        ") (hypotheses)) (Tm.imp " QV " " QV ")) (Megalodon.proofLam " QV ") (parents 0) 99)");
    st.revision += 1;
    EXPECT(mg_session_rebase(s) == MG_REFUSED, "rebase refuses a changed checked row");
    uint64_t again = 4;
    r = run(s, &again, 1, 0, 0);
    EXPECT(r.status == MG_SESSION_MALFORMED, "the session stays on its old store revision");
    mg_session_release(s);
    store_free(&st);
}

static void test_cancel(void) {
    fresh_theory();
    TestStore st;
    store_init(&st);
    put_schema(&st);
    MgSession *s = begin(&st);
    MgSessionReport r = run(s, SCHEMA_ROWS, 5, 0, 0);
    const char *why = NULL;
    MgThm *t = NULL;
    EXPECT(r.status == MG_SESSION_FINISHED &&
           mg_session_finish(s, 3, TM(SCHEMA), NULL, &why, &t) == MG_OK,
           "finish before cancellation");
    mg_session_cancel(s);
    EXPECT(st.releases == 1, "cancel releases the borrowed store once");
    mg_session_cancel(s);
    EXPECT(st.releases == 1, "cancel is idempotent");
    r = run(s, SCHEMA_ROWS, 5, 0, 0);
    EXPECT(r.status == MG_SESSION_CANCELLED, "a cancelled session does not walk");
    MgJudgment j;
    EXPECT(!mg_session_judgment(s, 3, &j), "a cancelled checkpoint is gone");
    const MgDecl *d = NULL;
    EXPECT(mg_thm_publish(T, t, NM("kept"), NULL, NULL, &why, &d) == MG_OK,
           "an independently retained theorem survives cancellation");
    mg_session_release(s);
    EXPECT(st.releases == 1, "release after cancel does not release again");
    mg_thm_release(t);
    store_free(&st);
}

static void test_theory_change(void) {
    implication_theory();
    TestStore st;
    store_init(&st);
    put_implication(&st);
    put_schema(&st);
    const char *why = NULL;
    MgSession *s = begin(&st);
    const uint64_t sched[] = {10, 11, 40, 41, 0, 1, 2, 3};
    MgSessionReport r = run(s, sched, 8, 0, 0);
    EXPECT(r.status == MG_SESSION_FINISHED, "mixed rows check");
    MgThm *uses_ax = NULL, *schema = NULL;
    EXPECT(mg_session_finish(s, 41, TM(QP), NULL, &why, &uses_ax) == MG_OK, "finish ax use");
    EXPECT(mg_session_finish(s, 3, TM(SCHEMA), NULL, &why, &schema) == MG_OK, "finish schema");
    /* an unrelated extension preserves both */
    parameter("r", "(Tp.prop)");
    EXPECT(mg_session_revalidate(s) == MG_OK && mg_session_env(s).rev == T->rev,
           "unrelated extension: the checkpoint moves to the live snapshot");
    /* withdrawal of a consulted axiom */
    EXPECT(mg_theory_withdraw(T, NM("ax"), MG_NS_KNOWN) == MG_OK, "withdraw ax");
    uint64_t rev = T->rev;
    const MgDecl *d = NULL;
    EXPECT(mg_thm_publish(T, uses_ax, NM("q_holds"), NULL, NULL, &why, &d) == MG_REFUSED &&
           T->rev == rev, "a theorem depending on a withdrawn axiom does not publish");
    EXPECT(mg_thm_valid_in(uses_ax, (MgEnv){T, uses_ax->rev}),
           "it remains valid against its retained old snapshot");
    EXPECT(mg_session_revalidate(s) == MG_REFUSED, "the session footprint sees the withdrawal");
    EXPECT(mg_thm_publish(T, schema, NM("schema"), NULL, NULL, &why, &d) == MG_OK,
           "an independent theorem still publishes");
    mg_thm_release(uses_ax);
    mg_thm_release(schema);
    mg_session_release(s);
    store_free(&st);
}

/* ── LCF steps ────────────────────────────────────────────────────────── */

static MgCtx *ctx_of(const char *wire) {
    MgCtx *c = mg_sx_wire_context(&T->store, sx(wire));
    if (!c) {
        printf("bad context %s\n", wire);
        exit(2);
    }
    return c;
}

static void test_lcf(void) {
    implication_theory();
    const char *why = NULL;
    MgThm *h = NULL, *k = NULL, *h1 = NULL, *m = NULL, *n = NULL, *bad = NULL;
    MgWitness hyp = {.rule = MG_RULE_HYP, .index = 0};
    EXPECT(mg_thm_step(T, &hyp, ctx_of("(Megalodon.context 0 (types) (hypotheses " QP "))"),
                       NULL, 0, NULL, &why, &h) == MG_OK, "LCF hyp");
    MgWitness plam = {.rule = MG_RULE_PROOF_LAM};
    EXPECT(mg_thm_step(T, &plam, NULL, &h, 1, NULL, &why, &k) == MG_OK &&
           k->judgment.prop == TM(IMPQ) && k->judgment.ctx == T->store.ctx_empty,
           "LCF proofLam discharges");
    EXPECT(mg_thm_step(T, &hyp, ctx_of("(Megalodon.context 0 (types) (hypotheses " IMPQ "))"),
                       NULL, 0, NULL, &why, &h1) == MG_OK, "LCF hyp 2");
    EXPECT(mg_thm_step(T, &plam, NULL, &h1, 1, NULL, &why, &m) == MG_OK, "LCF proofLam 2");
    MgThm *pair[2] = {m, k};
    MgWitness papp = {.rule = MG_RULE_PROOF_APP};
    EXPECT(mg_thm_step(T, &papp, NULL, pair, 2, NULL, &why, &n) == MG_OK &&
           n->judgment.prop == TM(IMPQ), "LCF proofApp");
    MgThm *swapped[2] = {k, m};
    EXPECT(mg_thm_step(T, &papp, NULL, swapped, 2, NULL, &why, &bad) == MG_REFUSED && !bad,
           "LCF proofApp with swapped premises is refused");
    EXPECT(mg_thm_step(T, &papp, NULL, pair, 1, NULL, &why, &bad) == MG_REFUSED && !bad,
           "LCF proofApp with one premise is refused");
    /* known, termLam, termApp */
    MgThm *ax = NULL, *dom_h = NULL, *inst = NULL, *gen = NULL, *done = NULL;
    MgWitness kn = {.rule = MG_RULE_KNOWN, .name = NM("ax")};
    EXPECT(mg_thm_step(T, &kn, T->store.ctx_empty, NULL, 0, NULL, &why, &ax) == MG_OK &&
           ax->judgment.prop == TM(QP), "LCF known");
    MgWitness kn2 = {.rule = MG_RULE_KNOWN, .name = NM("nope")};
    EXPECT(mg_thm_step(T, &kn2, T->store.ctx_empty, NULL, 0, NULL, &why, &bad) == MG_REFUSED,
           "LCF known of an absent axiom is refused");
    EXPECT(mg_thm_step(T, &hyp, ctx_of("(Megalodon.context 0 (types (Tp.base 0)) (hypotheses "
                                       DOM "))"), NULL, 0, NULL, &why, &dom_h) == MG_OK,
           "LCF hyp in an open context");
    MgWitness tapp = {.rule = MG_RULE_TERM_APP, .arg = TM("(Tm.db 0)")};
    EXPECT(mg_thm_step(T, &tapp, NULL, &dom_h, 1, NULL, &why, &inst) == MG_OK &&
           inst->judgment.prop == TM("(Tm.app (Tm.named \"p\") (Tm.db 0))"), "LCF termApp");
    MgWitness tapp_bad = {.rule = MG_RULE_TERM_APP, .arg = TM("(Tm.named \"q\")")};
    EXPECT(mg_thm_step(T, &tapp_bad, NULL, &dom_h, 1, NULL, &why, &bad) == MG_REFUSED,
           "LCF termApp with an ill-typed argument is refused");
    MgWitness tlam = {.rule = MG_RULE_TERM_LAM, .type = TP("(Tp.base 0)")};
    EXPECT(mg_thm_step(T, &tlam, NULL, &inst, 1, NULL, &why, &gen) == MG_OK &&
           gen->judgment.prop == TM(DOM), "LCF termLam generalizes");
    EXPECT(mg_thm_step(T, &plam, NULL, &gen, 1, NULL, &why, &done) == MG_OK &&
           done->judgment.ctx == T->store.ctx_empty, "LCF closes the forall identity");
    MgWitness tlam_bad = {.rule = MG_RULE_TERM_LAM, .type = TP("(Tp.prop)")};
    EXPECT(mg_thm_step(T, &tlam_bad, NULL, &inst, 1, NULL, &why, &bad) == MG_REFUSED,
           "LCF termLam at the wrong binder type is refused");
    /* typeLam / typeApp */
    MgThm *ph = NULL, *pl = NULL, *pg = NULL, *pa = NULL;
    EXPECT(mg_thm_step(T, &hyp, ctx_of("(Megalodon.context 1 (types (Tp.var 0)) (hypotheses "
                                       "(Tm.all (Tp.var 0) (Tm.imp (Tm.named \"q\") (Tm.named \"q\")))))"),
                       NULL, 0, NULL, &why, &ph) == MG_OK, "LCF hyp at type depth 1");
    EXPECT(mg_thm_step(T, &plam, NULL, &ph, 1, NULL, &why, &pl) == MG_OK, "LCF proofLam at depth 1");
    MgWitness tylam = {.rule = MG_RULE_TYPE_LAM};
    EXPECT(mg_thm_step(T, &tylam, NULL, &pl, 1, NULL, &why, &bad) == MG_REFUSED,
           "LCF typeLam over a nonempty term context is refused");
    MgThm *gl = NULL;
    EXPECT(mg_thm_step(T, &tlam, NULL, &pl, 1, NULL, &why, &bad) == MG_REFUSED,
           "LCF termLam at the wrong type is refused");
    MgWitness tl1 = {.rule = MG_RULE_TERM_LAM, .type = TP("(Tp.var 0)")};
    EXPECT(mg_thm_step(T, &tl1, NULL, &pl, 1, NULL, &why, &gl) == MG_OK, "LCF termLam at depth 1");
    EXPECT(mg_thm_step(T, &tylam, NULL, &gl, 1, NULL, &why, &pg) == MG_OK &&
           pg->judgment.prop->tag == MG_TM_TALL, "LCF typeLam");
    MgWitness tyapp = {.rule = MG_RULE_TYPE_APP, .type = TP("(Tp.prop)")};
    EXPECT(mg_thm_step(T, &tyapp, NULL, &pg, 1, NULL, &why, &pa) == MG_OK &&
           pa->judgment.ctx == T->store.ctx_empty, "LCF typeApp instantiates");
    MgWitness tyapp_bad = {.rule = MG_RULE_TYPE_APP, .type = TP("(Tp.var 3)")};
    MgThm *pb = NULL;
    MgStatus st = mg_thm_step(T, &tyapp_bad, NULL, &pg, 1, NULL, &why, &pb);
    EXPECT(st == MG_OK || st == MG_REFUSED, "typeApp with an out-of-scope type is decided");
    mg_thm_release(pb);

    /* foreign premises */
    MgTheory *other = new_theory();
    MgThm *alien = NULL;
    MgWitness hyp2 = {.rule = MG_RULE_HYP, .index = 0};
    /* Lazy judgments are formed: a context's hypotheses are propositions
     * (here primitive 0 is not even a term of the custom theory). */
    MgCtx *ill = mg_ctx(&other->store, 0, NULL,
                        mg_tmlist_cons(&other->store, mg_tm_prim(&other->store, 0), NULL));
    EXPECT(ill && mg_thm_step(other, &hyp2, ill, NULL, 0, NULL, &why, &alien) == MG_REFUSED &&
           !alien, "LCF hyp in a context with an ill-typed hypothesis is refused");
    other->reference_conversion = true;
    EXPECT(mg_thm_step(other, &hyp2, ill, NULL, 0, NULL, &why, &alien) == MG_OK,
           "... while the reference rule states any hypothesis it is given");
    mg_thm_release(alien);
    alien = NULL;
    other->reference_conversion = false;
    MgItem r_item = {.kind = MG_ITEM_PARAMETER, .name = mg_name_intern(&other->store, "r", 1),
                     .type = mg_tp_prop(&other->store)};
    MgBudget ob;
    mg_budget_init(&ob, 0);
    EXPECT(mg_theory_associate_parameter(other, r_item.name, r_item.type) == MG_OK &&
           mg_theory_admit(other, &ob, &r_item, 0, NULL) == MG_OK, "a parameter of another theory");
    MgCtx *octx = mg_ctx(&other->store, 0, NULL,
                         mg_tmlist_cons(&other->store, mg_tm_name(&other->store, r_item.name), NULL));
    EXPECT(octx && mg_thm_step(other, &hyp2, octx, NULL, 0, NULL, &why, &alien) == MG_OK,
           "a theorem of another theory");
    EXPECT(mg_thm_step(T, &plam, NULL, &alien, 1, NULL, &why, &bad) == MG_REFUSED && !bad,
           "a premise of another theory is refused");
    mg_thm_release(alien);
    mg_theory_release(other);

    /* withdrawal invalidates LCF values that consulted the name */
    EXPECT(mg_theory_withdraw(T, NM("ax"), MG_NS_KNOWN) == MG_OK, "withdraw ax");
    EXPECT(mg_thm_step(T, &plam, NULL, &ax, 1, NULL, &why, &bad) == MG_REFUSED && !bad,
           "an LCF premise consulting a withdrawn axiom is refused");
    MgThm *still = NULL;
    MgThm *pair2[2] = {m, k};
    EXPECT(mg_thm_step(T, &papp, NULL, pair2, 2, NULL, &why, &still) == MG_OK,
           "unrelated LCF premises remain usable");
    mg_thm_release(still);

    MgThm *all[] = {h, k, h1, m, n, ax, dom_h, inst, gen, done, ph, pl, gl, pg, pa};
    for (size_t i = 0; i < sizeof all / sizeof all[0]; ++i)
        mg_thm_release(all[i]);
}

/* ── publication boundary ─────────────────────────────────────────────── */

static void test_publication(void) {
    implication_theory();
    TestStore st;
    store_init(&st);
    put_implication(&st);
    MgSession *s = begin(&st);
    const uint64_t sched[] = {10, 11};
    MgSessionReport r = run(s, sched, 2, 0, 0);
    const char *why = NULL;
    MgThm *t = NULL;
    EXPECT(r.status == MG_SESSION_FINISHED &&
           mg_session_finish(s, 11, TM(IMPQ), NULL, &why, &t) == MG_OK, "finish");
    uint64_t rev = T->rev, decls = T->decl_len;
    const MgDecl *d = NULL;
    T->publication_permitted = false;
    EXPECT(mg_thm_publish(T, t, NM("imp_refl"), NULL, NULL, &why, &d) == MG_DENIED && !d &&
           T->rev == rev && T->decl_len == decls, "denied permission changes nothing");
    T->publication_permitted = true;
    EXPECT(mg_theory_publish_known(T, rev - 1u, NM("imp_refl"), TM(IMPQ), &t->footprint,
                                   NULL, NULL, NULL, &d) == MG_CONFLICT &&
           T->rev == rev && T->decl_len == decls, "a stale captured revision conflicts");
    EXPECT(mg_thm_publish(T, t, NM("imp_refl"), TM("(Tm.imp (Tm.named \"q\") (Tm.named \"r0\"))"),
                          NULL, &why, &d) == MG_REFUSED && T->rev == rev,
           "an ill-formed claim does not publish");
    EXPECT(mg_thm_publish(T, t, NM("imp_refl"), NULL, NULL, &why, &d) == MG_OK && d &&
           T->rev == rev + 1u, "publish");
    EXPECT(mg_thm_publish(T, t, NM("imp_refl"), NULL, NULL, &why, &d) == MG_REFUSED &&
           T->rev == rev + 1u, "double commit refused");
    EXPECT(mg_thm_publish(T, t, NM("ax"), NULL, NULL, &why, &d) == MG_REFUSED &&
           T->rev == rev + 1u, "publishing over an existing known is refused");
    MgThm *open = NULL;
    MgWitness hyp = {.rule = MG_RULE_HYP, .index = 0};
    EXPECT(mg_thm_step(T, &hyp, ctx_of("(Megalodon.context 0 (types) (hypotheses " QP "))"),
                       NULL, 0, NULL, &why, &open) == MG_OK &&
           mg_thm_publish(T, open, NM("open"), NULL, NULL, &why, &d) == MG_REFUSED,
           "an open theorem does not publish");
    mg_thm_release(open);
    mg_thm_release(t);
    mg_session_release(s);
    store_free(&st);
}

/* ── allocation failure through check / finish / publish ──────────────── */

static void test_allocation_failure(void) {
#ifdef MG_TEST_HOOKS
    bool unchanged = true, undecided_only = true, finished = false;
    uint64_t attempts = 0;
    for (uint64_t k = 0; !finished && k < 100000u; ++k, ++attempts) {
        implication_theory();
        TestStore st;
        store_init(&st);
        put_implication(&st);
        put_schema(&st);
        MgTm *claim = TM(IMPQ);
        uint32_t name = NM("published");
        uint64_t rev = T->rev, decls = T->decl_len;
        const uint64_t sched[] = {10, 11, 12, 13, 14, 16, 0, 1, 2, 3, 4};
        mg_test_fail_allocation_after(k);
        MgStatus status = MG_OK;
        MgSession *s = mg_session_begin(T, source_of(&st), &status);
        MgSessionReport r = {0};
        MgThm *t = NULL;
        const char *why = NULL;
        MgStatus outcome = status;
        if (s) {
            MgBudget b;
            mg_budget_init(&b, 0);
            outcome = mg_session_resume(s, sched, 11, 0, &b, &r);
            if (outcome == MG_OK && r.status != MG_SESSION_FINISHED)
                outcome = r.status == MG_SESSION_NOMEM ? MG_NOMEM : MG_REFUSED;
            if (outcome == MG_OK)
                outcome = mg_session_finish(s, 16, claim, NULL, &why, &t);
            if (outcome == MG_OK) {
                const MgDecl *d = NULL;
                outcome = mg_thm_publish(T, t, name, NULL, NULL, &why, &d);
            }
        }
        mg_test_fail_allocation_after(UINT64_MAX);
        if (outcome == MG_OK) {
            finished = T->rev == rev + 1u && T->decl_len == decls + 1u;
        } else {
            if (outcome != MG_NOMEM) {
                undecided_only = false;
                printf("  k=%llu outcome %s (%s)\n", (unsigned long long)k,
                       mg_status_name(outcome), why ? why : "");
            }
            if (T->rev != rev || T->decl_len != decls)
                unchanged = false;
        }
        mg_thm_release(t);
        if (s)
            mg_session_release(s);
        else if (st.releases != 1)
            unchanged = false; /* begin must release the source on failure */
        store_free(&st);
    }
    EXPECT(finished, "allocation injection reaches a successful run");
    EXPECT(undecided_only, "an allocation failure is never a refusal or malformation");
    EXPECT(unchanged, "every failed attempt leaves the theory unchanged");
    printf("(allocation points exercised: %llu)\n", (unsigned long long)attempts);
#else
    printf("(allocation injection needs -DMG_TEST_HOOKS)\n");
#endif
}

/* Formation consults the hypotheses even when the proof is just Hyp 0.
 * These dependencies must survive theorem retention and cursor extraction. */
static void test_context_footprint(void) {
    fresh_theory();
    parameter("context-only", "(Tp.prop)");
    MgTm *p = TM("(Tm.named \"context-only\")");
    MgCtx *ctx = mg_ctx(&T->store, 0, NULL,
                         mg_tmlist_cons(&T->store, p, NULL));
    MgWitness hyp = {.rule = MG_RULE_HYP, .index = 0};
    MgThm *lcf = NULL, *tree = NULL, *row = NULL, *retry = NULL;
    const char *why = NULL;
    EXPECT(mg_thm_step(T, &hyp, ctx, NULL, 0, NULL, &why, &lcf) == MG_OK,
           "context-only LCF hypothesis checks");
    MgPf *proof = mg_sx_wire_pf(&T->store, sx("(Pf.hyp 0)"));
    EXPECT(mg_thm_check_tree(T, ctx, proof, NULL, &why, &tree) == MG_OK,
           "context-only tree checks");
    TestStore store;
    store_init(&store);
    put(&store, 0, "(Megalodon.row (Megalodon.judgment "
        "(Megalodon.context 0 (types) (hypotheses (Tm.named \"context-only\"))) "
        "(Tm.named \"context-only\")) (Megalodon.hyp 0) (parents) 0)");
    MgStatus status;
    MgRowSource source = {store_revision, store_read, store_release, &store};
    MgSession *session = mg_session_begin_mode(T, source, true, &status);
    uint64_t schedule[] = {0};
    MgSessionReport report = run(session, schedule, 1, 0, 0);
    EXPECT(report.status == MG_SESSION_FINISHED &&
           mg_session_theorem(session, 0, &why, &row) == MG_OK,
           "context-only lazy row checks and extracts");
    uint64_t before = T->rev;
    parameter("unrelated-context", "(Tp.prop)");
    EXPECT(mg_thm_valid_in(lcf, mg_theory_snapshot(T)) &&
           mg_thm_valid_in(tree, mg_theory_snapshot(T)) &&
           mg_thm_valid_in(row, mg_theory_snapshot(T)) &&
           mg_session_revalidate(session) == MG_OK,
           "unrelated extension preserves all contextual routes");
    EXPECT(mg_theory_withdraw(T, NM("context-only"), MG_NS_TERM) == MG_OK,
           "withdraw context-only declaration");
    EXPECT(mg_thm_valid_in(lcf, (MgEnv){T, before}),
           "retained original context stays valid in its old snapshot");
    EXPECT(mg_thm_step(T, &hyp, ctx, NULL, 0, NULL, &why, &retry) == MG_REFUSED,
           "fresh hypothesis check detects the removed declaration");
    EXPECT(!mg_thm_valid_in(lcf, mg_theory_snapshot(T)),
           "LCF footprint retains context formation reads");
    EXPECT(!mg_thm_valid_in(tree, mg_theory_snapshot(T)),
           "tree footprint retains context formation reads");
    EXPECT(!mg_thm_valid_in(row, mg_theory_snapshot(T)),
           "extracted theorem retains context formation reads");
    EXPECT(mg_session_revalidate(session) == MG_REFUSED,
           "lazy cursor revalidation retains context formation reads");
    mg_thm_release(retry);
    mg_thm_release(row);
    mg_thm_release(tree);
    mg_thm_release(lcf);
    mg_session_release(session);
    store_free(&store);
}

int main(void) {
    A = mg_sx_arena_new();
    test_schema_pause_resume();
    test_rows_negative();
    test_budgets();
    test_store_revision();
    test_cancel();
    test_theory_change();
    test_lcf();
    test_publication();
    test_allocation_failure();
    test_context_footprint();
    if (T)
        mg_theory_release(T);
    mg_sx_arena_free(A);
    printf("(MgSessionSummary %d %d)\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
