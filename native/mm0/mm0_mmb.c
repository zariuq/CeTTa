/* mm0_mmb.c - the MMB command machine (mm0-c/mmb.md "Proof Checking").
 *
 * Equality of expressions is allocation identity, as the format requires:
 * `Term` allocates a fresh node, `Save`/`Ref` share one, and `Refl`, `URef`
 * and the conversion endpoints compare pointers.  Nodes live in an arena
 * released after each declaration; nothing is interned, and no structural
 * comparison ever replaces a pointer comparison.
 *
 * The rules follow mm0-c/verifier.c with two deliberate differences, both
 * toward the specification:
 *
 *  1. In theorem proofs a term's variable set is the union over all its
 *     arguments, bound ones included (mm0.md "Verification": Vars(foo e1 ...
 *     en) = U i, Si; mmb.md "Proof Checking": V(t e1 ... en)).  mm0-c leaves
 *     bound arguments out, so a definition whose value depends on a bound
 *     argument (`def isz {x: nat}: wff x`) escapes the disjointness check
 *     of `Thm` and the freshness check of `Unfold`; tests/mm0 has a file that
 *     mm0-c accepts and that proves a false statement that way.  mm0-rs
 *     computes the same union as this file.
 *  2. Reserved bit 55 of a type word must be zero, as mmb.md says.
 *
 * Statement checking against the .mm0 file is mm0_spec.c, called at the
 * points where mm0-c calls parse_until. */
#include "mm0_internal.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct MbExpr MbExpr;
struct MbExpr {
    uint64_t type;       /* deps | sort << 56 | bound << 63, mmb.md's arg layout */
    uint32_t id;         /* application: term index; variable: binder index */
    uint16_t nargs;
    uint8_t app;
    MbExpr *args[];
};

/* Stack and heap elements are tagged node pointers (mmb.md "Proof
 * Checking": expr, proof, e1 = e2, e1 =?= e2).  As in mm0-c, `e1 = e2` and
 * `e1 =?= e2` take two stack slots, e2 (EXPR) below e1 (CONV/CO_CONV); on
 * the heap a conversion is one slot pointing at an MbConv cell. */
typedef uintptr_t MbSlot;
enum { SLOT_EXPR = 0, SLOT_PROOF = 1, SLOT_CONV = 2, SLOT_CO_CONV = 3 };
#define SLOT_TAG(s) ((unsigned)((s) & 3u))
#define SLOT_PTR(s) ((void *)((s) & ~(uintptr_t)3))
typedef struct { MbExpr *e1, *e2; } MbConv;

/* Unify heap entries: a node, with bit 0 set for subterms saved by
 * UTermSave (mmb.md: UDummy checks freshness against the substitution and
 * earlier dummies, not against saved subterms of its own target). */
#define UHEAP_SAVED ((uintptr_t)1)

typedef enum { MODE_DEF, MODE_THM } ProofMode;
typedef enum { U_DEF, U_THM, U_THM_END } UnifyMode;

/* The decoder of retained statements (see decode_expr). */
typedef struct { Mm0Expr *node; uint32_t next; } DecodeFrame;

typedef struct {
    Mm0Expr **heap; size_t n, cap;
    DecodeFrame *frames; size_t nf, cap_frames;
    uint32_t *dummy_sorts; size_t ndummies, cap_dummies;
} Decoder;

typedef struct {
    const uint8_t *file, *end;
    size_t len;
    uint32_t num_sorts, num_terms, num_thms;
    const uint8_t *sorts, *term_table, *thm_table;
    uint32_t nsorts, nterms, nthms;           /* checked so far */

    Mm0Arena store;                            /* nodes of the current declaration */
    MbSlot *stack; size_t sp, stack_cap;
    MbSlot *heap; size_t hp, heap_cap;
    MbExpr **hyps; size_t hsp, hyps_cap;
    MbExpr **ustack; size_t usp, ustack_cap;
    uintptr_t *uheap; size_t uhp, uheap_cap;
    uint64_t *bound_deps; size_t bound_deps_cap;
    uint32_t next_bv;                          /* bound variables in use */

    const uint8_t *stmt, *cmd;                 /* for error locations */
    Mm0Error *error;
    Mm0Steps *steps;
    Mm0Theory *theory;                         /* NULL in check-only mode */
    Mm0Spec *spec;                             /* NULL without a specification */
    Mm0SpecNames names;
    Decoder dec;                               /* reused by each retained statement */
    Mm0Expr **rhyps; size_t rhyps_cap;         /* a retained theorem's hypotheses */
} Mmb;

static bool fail(Mmb *v, Mm0Status status, const char *reason) {
    Mm0Error *e = v->error;
    if (e && e->status == MM0_OK) {
        e->status = status;
        e->reason = reason;
        const uint8_t *at = v->cmd ? v->cmd : v->stmt;
        e->offset = at ? (int64_t)(at - v->file) : -1;
    }
    return false;
}

/* REFUSED: the proof is wrong.  MALFORMED: the file is not an MMB file. */
#define ENSURE(cond, reason) \
    do { if (__builtin_expect(!(cond), 0)) return fail(v, MM0_REFUSED, reason); } while (0)
#define ENSURE_FORMAT(cond, reason) \
    do { if (__builtin_expect(!(cond), 0)) return fail(v, MM0_MALFORMED, reason); } while (0)
#define CHECK(expr) do { if (!(expr)) return false; } while (0)

#if defined(__GNUC__)
#define HOT static inline __attribute__((always_inline))
#else
#define HOT static inline
#endif

static bool step_stopped(Mmb *v, Mm0Status status) {
    return fail(v, status, status == MM0_LIMIT ? "step budget exhausted" : "interrupted");
}

HOT bool step(Mmb *v) {
    Mm0Status status = mm0_step(v->steps);
    if (__builtin_expect(status == MM0_OK, 1)) return true;
    return step_stopped(v, status);
}

/* Every command is read with five bytes available (mmb.md "To permit
 * read-ahead"); `cmd_at` checks that before decoding.  Positions only ever
 * advance from inside the file. */
HOT bool cmd_at(Mmb *v, const uint8_t *p, MmbCmd *out) {
    ENSURE_FORMAT((size_t)(v->end - p) >= 5, "command out of range");
    *out = mmb_cmd(p);
    return true;
}

/* ---- growable stacks --------------------------------------------------- */

#define GROW(vec, cap, need, what) \
    do { if (!MM0_RESERVE(vec, cap, need)) return fail(v, MM0_NOMEM, what); } while (0)

static inline bool push(Mmb *v, MbSlot s) {
    if (__builtin_expect(v->sp == v->stack_cap, 0)) GROW(v->stack, v->stack_cap, v->sp + 1, "stack allocation");
    v->stack[v->sp++] = s;
    return true;
}
static inline bool push_heap(Mmb *v, MbSlot s) {
    if (__builtin_expect(v->hp == v->heap_cap, 0)) GROW(v->heap, v->heap_cap, v->hp + 1, "heap allocation");
    v->heap[v->hp++] = s;
    return true;
}
static inline bool push_ustack(Mmb *v, MbExpr *e) {
    if (__builtin_expect(v->usp == v->ustack_cap, 0)) GROW(v->ustack, v->ustack_cap, v->usp + 1, "unify stack allocation");
    v->ustack[v->usp++] = e;
    return true;
}
static inline bool push_uheap(Mmb *v, uintptr_t e) {
    if (__builtin_expect(v->uhp == v->uheap_cap, 0)) GROW(v->uheap, v->uheap_cap, v->uhp + 1, "unify heap allocation");
    v->uheap[v->uhp++] = e;
    return true;
}
static inline bool pop_as(Mmb *v, unsigned tag, MbExpr **out) {
    ENSURE(v->sp > 0, "stack underflow");
    MbSlot s = v->stack[--v->sp];
    ENSURE(SLOT_TAG(s) == tag, "bad stack slot");
    *out = SLOT_PTR(s);
    return true;
}
static inline MbSlot slot(MbExpr *e, unsigned tag) { return (MbSlot)e | tag; }

static MbExpr *alloc_expr(Mmb *v, size_t nargs) {
    MbExpr *e = mm0_arena_alloc(&v->store, sizeof *e + nargs * sizeof(MbExpr *));
    if (!e) fail(v, MM0_NOMEM, "expression store allocation");
    return e;
}

/* mm0-c sorts_compatible: the same sort, and a bound target needs a bound
 * value (a bound value may stand where a regular one is expected). */
static inline bool sorts_compatible(uint64_t from, uint64_t to) {
    uint64_t upper = (from ^ to) >> 56;
    return upper == 0 || (upper == 0x80 && (from & MMB_TYPE_BOUND_BIT));
}

/* ---- table access ------------------------------------------------------ */

typedef struct { uint16_t nargs; uint8_t sort; uint32_t p_args; } TermEntry;
typedef struct { uint16_t nargs; uint32_t p_args; } ThmEntry;

static TermEntry term_entry(const Mmb *v, uint32_t t) {
    const uint8_t *p = v->term_table + 8 * (size_t)t;
    return (TermEntry){mm0_le16(p), p[2], mm0_le32(p + 4)};
}
static ThmEntry thm_entry(const Mmb *v, uint32_t t) {
    const uint8_t *p = v->thm_table + 8 * (size_t)t;
    return (ThmEntry){mm0_le16(p), mm0_le32(p + 4)};
}
static inline uint64_t arg_at(const Mmb *v, uint32_t p_args, size_t i) {
    return mm0_le64(v->file + p_args + 8 * i);
}

/* mm0-c load_args: allocate the declaration's variables on the heap and
 * check its binders (mmb.md "Term Table": the k-th bound binder has
 * deps = 1 << k; a regular binder depends only on earlier bound ones). */
static bool load_args(Mmb *v, uint32_t p_args, size_t n) {
    ENSURE_FORMAT(p_args <= v->len && n <= (v->len - p_args) / 8, "bad args pointer");
    v->hp = 0;
    v->next_bv = 0;
    GROW(v->heap, v->heap_cap, n, "heap allocation");
    for (size_t i = 0; i < n; ++i) {
        uint64_t ty = arg_at(v, p_args, i);
        uint64_t deps = ty & MMB_TYPE_DEPS_MASK;
        uint32_t sort = MMB_TYPE_SORT(ty);
        ENSURE_FORMAT(!(ty & MMB_TYPE_RESERVED_BIT), "reserved type bit set");
        ENSURE(sort < v->nsorts, "bad binder sort");
        if (ty & MMB_TYPE_BOUND_BIT) {
            ENSURE(!(v->sorts[sort] & MM0_SORT_STRICT), "bound variable in strict sort");
            ENSURE(v->next_bv < MMB_MAX_BOUND && deps == UINT64_C(1) << v->next_bv,
                   "bad binder deps");
            ++v->next_bv;
        } else {
            ENSURE((deps >> v->next_bv) == 0, "bad binder deps");
        }
        MbExpr *x = alloc_expr(v, 0);
        CHECK(x);
        *x = (MbExpr){.type = ty, .id = (uint32_t)i, .nargs = 0, .app = 0};
        v->heap[v->hp++] = slot(x, SLOT_EXPR);
    }
    return true;
}

/* ---- unification (mmb.md "Unification") -------------------------------- */

static bool run_unify(Mmb *v, UnifyMode mode, const uint8_t *cmd, MbExpr *target) {
    v->usp = 0;
    CHECK(push_ustack(v, target));
    for (;;) {
        CHECK(step(v));
        v->cmd = cmd;
        MmbCmd c = {0};
        CHECK(cmd_at(v, cmd, &c));
        switch (c.op) {
        case MMB_CMD_END:
            if (mode == U_THM_END) ENSURE(v->hsp == 0, "unfinished hypothesis stack");
            ENSURE(v->usp == 0, "unfinished unify stack");
            return true;

        /* URef i: H; S, H[i] --> H; S  (pointer equality) */
        case MMB_UNIFY_REF: {
            ENSURE(c.data < v->uhp, "bad ref step");
            ENSURE(v->usp > 0, "unify stack underflow");
            MbExpr *e = v->ustack[--v->usp];
            ENSURE((MbExpr *)(v->uheap[c.data] & ~UHEAP_SAVED) == e, "unify failure at ref");
            break;
        }

        /* UTerm t: S, (t e1 ... en) --> S, en, ..., e1
         * UTermSave t: H; S, (t e1 ... en) --> H, (t e1 ... en); S, en, ..., e1 */
        case MMB_UNIFY_TERM:
        case MMB_UNIFY_TERM_SAVE: {
            ENSURE(v->usp > 0, "unify stack underflow");
            MbExpr *e = v->ustack[--v->usp];
            ENSURE(e->app, "unify failure at term");
            ENSURE(e->id == c.data, "unify failure at term");
            GROW(v->ustack, v->ustack_cap, v->usp + e->nargs, "unify stack allocation");
            for (size_t i = e->nargs; i-- > 0;) v->ustack[v->usp++] = e->args[i];
            if (c.op == MMB_UNIFY_TERM_SAVE) CHECK(push_uheap(v, (uintptr_t)e | UHEAP_SAVED));
            break;
        }

        /* UDummy s: H; S, x --> H, x; S  where x is a bound variable of sort s
         * with V(e) ∩ V(x) = {} for every e of the substitution and every
         * earlier dummy. */
        case MMB_UNIFY_DUMMY: {
            ENSURE(mode == U_DEF, "Dummy command not allowed in theorem statements");
            ENSURE(v->usp > 0, "unify stack underflow");
            MbExpr *x = v->ustack[--v->usp];
            ENSURE(!x->app, "unify failure at dummy");
            ENSURE((x->type >> 56) == (0x80u | c.data), "unify failure at dummy");
            uint64_t deps = x->type & MMB_TYPE_DEPS_MASK;
            for (size_t i = 0; i < v->uhp; ++i) {
                if (v->uheap[i] & UHEAP_SAVED) continue;
                ENSURE((((MbExpr *)v->uheap[i])->type & deps) == 0,
                       "dummy disjoint variable violation");
            }
            CHECK(push_uheap(v, (uintptr_t)x));
            break;
        }

        /* UHyp (UThm): MS, |- e; S --> MS; S, e
         * UHyp (UThmEnd): HS, e; S --> HS; S, e  (S empty) */
        case MMB_UNIFY_HYP:
            if (mode == U_THM) {
                MbExpr *e = NULL;
                CHECK(pop_as(v, SLOT_PROOF, &e));
                CHECK(push_ustack(v, e));
            } else if (mode == U_THM_END) {
                ENSURE(v->usp == 0, "unfinished unify stack");
                ENSURE(v->hsp > 0, "hypothesis stack underflow");
                CHECK(push_ustack(v, v->hyps[--v->hsp]));
            } else {
                ENSURE(false, "Hyp command not allowed in definition statements");
            }
            break;

        default:
            return fail(v, MM0_MALFORMED, mode == U_DEF ? "unknown opcode in def statement"
                                                       : "unknown opcode in theorem statement");
        }
        cmd += c.size;
    }
}

/* ---- proofs (mmb.md "Proof Checking") ---------------------------------- */

/* Term t: H; S, e1, ..., en --> H; S, (t e1 ... en)   alloc(t e1 ... en)
 * TermSave t = Term t, Save.
 * The new node's variable set is FV in definitions (mmb.md "FV") and the
 * union of all arguments' V in theorems (mmb.md "V"; see the file comment). */
static bool proof_term(Mmb *v, ProofMode mode, uint32_t t, bool save) {
    ENSURE(t < v->nterms, "term out of range");
    TermEntry te = term_entry(v, t);
    size_t n = te.nargs;
    ENSURE(v->sp >= n, "stack underflow");
    v->sp -= n;
    MbExpr *e = alloc_expr(v, n);
    CHECK(e);
    uint64_t accum = (uint64_t)(te.sort & 0x7F) << 56;
    if (mode == MODE_DEF) {
        uint32_t bound = 0;
        GROW(v->bound_deps, v->bound_deps_cap, n, "dependency scratch allocation");
        for (size_t i = 0; i < n; ++i) {
            MbSlot s = v->stack[v->sp + i];
            ENSURE(SLOT_TAG(s) == SLOT_EXPR, "bad stack slot");
            MbExpr *arg = SLOT_PTR(s);
            e->args[i] = arg;
            uint64_t target = arg_at(v, te.p_args, i);
            ENSURE(sorts_compatible(arg->type, target), "type mismatch");
            uint64_t deps = arg->type & MMB_TYPE_DEPS_MASK;
            if (target & MMB_TYPE_BOUND_BIT) {
                v->bound_deps[bound++] = deps;
            } else {
                for (uint32_t j = 0; j < bound; ++j)
                    if (target & (UINT64_C(1) << j)) deps &= ~v->bound_deps[j];
                accum |= deps;
            }
        }
        uint64_t ret = arg_at(v, te.p_args, n) & MMB_TYPE_DEPS_MASK;
        for (uint32_t j = 0; ret && j < bound; ++j)
            if (ret & (UINT64_C(1) << j)) accum |= v->bound_deps[j];
    } else {
        for (size_t i = 0; i < n; ++i) {
            MbSlot s = v->stack[v->sp + i];
            ENSURE(SLOT_TAG(s) == SLOT_EXPR, "bad stack slot");
            MbExpr *arg = SLOT_PTR(s);
            e->args[i] = arg;
            ENSURE(sorts_compatible(arg->type, arg_at(v, te.p_args, i)), "type mismatch");
            accum |= arg->type & MMB_TYPE_DEPS_MASK;
        }
    }
    e->type = accum;
    e->id = t;
    e->nargs = te.nargs;
    e->app = 1;
    CHECK(push(v, slot(e, SLOT_EXPR)));
    if (save) CHECK(push_heap(v, slot(e, SLOT_EXPR)));
    return true;
}

/* Thm T: H; S, e1, ..., en, e --> H; S', |- e
 *   where Unify(T): S; e1, ..., en; e --> S'; H'; .
 * mm0.md "Verification": bound arguments receive bound variables, and if vi
 * is bound and vj does not depend on vi then ei is not in Vars(ej). */
static bool proof_thm(Mmb *v, ProofMode mode, uint32_t t, bool save) {
    ENSURE(mode != MODE_DEF, "invalid opcode in def");
    ENSURE(t < v->nthms, "theorem out of range");
    ThmEntry th = thm_entry(v, t);
    MbExpr *e = NULL;
    CHECK(pop_as(v, SLOT_EXPR, &e));
    size_t n = th.nargs;
    ENSURE(v->sp >= n, "stack underflow");
    v->sp -= n;
    v->uhp = 0;
    GROW(v->uheap, v->uheap_cap, n, "unify heap allocation");
    GROW(v->bound_deps, v->bound_deps_cap, n, "dependency scratch allocation");
    uint32_t bound = 0;
    for (size_t i = 0; i < n; ++i) {
        MbSlot s = v->stack[v->sp + i];
        ENSURE(SLOT_TAG(s) == SLOT_EXPR, "bad stack slot");
        MbExpr *arg = SLOT_PTR(s);
        v->uheap[i] = (uintptr_t)arg;
        uint64_t target = arg_at(v, th.p_args, i);
        ENSURE(sorts_compatible(arg->type, target), "type mismatch");
        uint64_t deps = arg->type & MMB_TYPE_DEPS_MASK;
        if (target & MMB_TYPE_BOUND_BIT) {
            v->bound_deps[bound++] = deps;
            for (size_t j = 0; j < i; ++j)
                ENSURE((((MbExpr *)v->uheap[j])->type & deps) == 0,
                       "disjoint variable violation");
        } else {
            for (uint32_t j = 0; j < bound; ++j)
                ENSURE((target & (UINT64_C(1) << j)) || (v->bound_deps[j] & deps) == 0,
                       "disjoint variable violation");
        }
    }
    v->uhp = n;
    CHECK(run_unify(v, U_THM, v->file + th.p_args + 8 * n, e));
    CHECK(push(v, slot(e, SLOT_PROOF)));
    if (save) CHECK(push_heap(v, slot(e, SLOT_PROOF)));
    return true;
}

static MbConv *alloc_conv(Mmb *v, MbExpr *e1, MbExpr *e2) {
    MbConv *c = mm0_arena_alloc(&v->store, sizeof *c);
    if (!c) { fail(v, MM0_NOMEM, "expression store allocation"); return NULL; }
    *c = (MbConv){e1, e2};
    return c;
}

/* Returns the position just after the stream's END, or NULL. */
static const uint8_t *run_proof(Mmb *v, ProofMode mode, const uint8_t *cmd) {
    for (;;) {
        if (!step(v)) return NULL;
        v->cmd = cmd;
        MmbCmd c = {0};
        if (!cmd_at(v, cmd, &c)) return NULL;
        MbExpr *e = NULL, *e1 = NULL, *e2 = NULL;
        bool ok = true;
        switch (c.op) {
        case MMB_CMD_END:
            return cmd + c.size;

        /* Ref i: H; S --> H; S, H[i]
         * Ref i: H; S, e1 =?= e2 --> H; S  (when H[i] is e1 = e2) */
        case MMB_PROOF_REF: {
            if (c.data >= v->hp) { fail(v, MM0_REFUSED, "bad ref step"); return NULL; }
            MbSlot s = v->heap[c.data];
            if (SLOT_TAG(s) == SLOT_CONV) {
                MbConv *cell = SLOT_PTR(s);
                ok = pop_as(v, SLOT_CO_CONV, &e1) && pop_as(v, SLOT_EXPR, &e2);
                if (ok && !(cell->e1 == e1 && cell->e2 == e2))
                    ok = fail(v, MM0_REFUSED, "ConvRef unify error");
            } else {
                ok = push(v, s);
            }
            break;
        }

        /* Dummy s: H; S --> H, x; S, x   alloc(x: s), a new bound variable */
        case MMB_PROOF_DUMMY: {
            if (c.data >= v->nsorts) { fail(v, MM0_REFUSED, "bad dummy sort"); return NULL; }
            if (v->sorts[c.data] & (MM0_SORT_STRICT | MM0_SORT_FREE)) {
                fail(v, MM0_REFUSED, "dummy variable in strict or free sort");
                return NULL;
            }
            /* mmb.md: next_bv may never exceed 55 (bit 55 is reserved). */
            if (v->next_bv >= MMB_MAX_BOUND) {
                fail(v, MM0_REFUSED, "too many bound variables (MMB limit 55)");
                return NULL;
            }
            MbExpr *x = alloc_expr(v, 0);
            if (!x) return NULL;
            *x = (MbExpr){.type = MMB_TYPE_BOUND_BIT | (uint64_t)c.data << 56 |
                                  UINT64_C(1) << v->next_bv,
                          .id = (uint32_t)v->hp, .nargs = 0, .app = 0};
            ++v->next_bv;
            ok = push(v, slot(x, SLOT_EXPR)) && push_heap(v, slot(x, SLOT_EXPR));
            break;
        }

        case MMB_PROOF_TERM:
        case MMB_PROOF_TERM_SAVE:
            ok = proof_term(v, mode, c.data, c.op == MMB_PROOF_TERM_SAVE);
            break;

        case MMB_PROOF_THM:
        case MMB_PROOF_THM_SAVE:
            ok = proof_thm(v, mode, c.data, c.op == MMB_PROOF_THM_SAVE);
            break;

        /* Hyp: HS; H; S, e --> HS, e; H, |- e; S   (e of provable sort) */
        case MMB_PROOF_HYP:
            if (mode == MODE_DEF) { fail(v, MM0_REFUSED, "invalid opcode in def"); return NULL; }
            ok = pop_as(v, SLOT_EXPR, &e);
            if (ok && !(v->sorts[MMB_TYPE_SORT(e->type)] & MM0_SORT_PROVABLE))
                ok = fail(v, MM0_REFUSED, "hypothesis should have provable sort");
            if (ok && v->hsp == v->hyps_cap &&
                !MM0_RESERVE(v->hyps, v->hyps_cap, v->hsp + 1))
                ok = fail(v, MM0_NOMEM, "hypothesis stack allocation");
            if (ok) {
                v->hyps[v->hsp++] = e;
                ok = push_heap(v, slot(e, SLOT_PROOF));
            }
            break;

        /* Conv: S, e1, |- e2 --> S, |- e1, e1 =?= e2 */
        case MMB_PROOF_CONV:
            ok = pop_as(v, SLOT_PROOF, &e2) && pop_as(v, SLOT_EXPR, &e1) &&
                 push(v, slot(e1, SLOT_PROOF)) && push(v, slot(e2, SLOT_EXPR)) &&
                 push(v, slot(e1, SLOT_CO_CONV));
            break;

        /* Refl: S, e =?= e --> S   (pointer equality) */
        case MMB_PROOF_REFL:
            ok = pop_as(v, SLOT_CO_CONV, &e1) && pop_as(v, SLOT_EXPR, &e2);
            if (ok && e1 != e2) ok = fail(v, MM0_REFUSED, "Refl unify failure");
            break;

        /* Symm: S, e1 =?= e2 --> S, e2 =?= e1 */
        case MMB_PROOF_SYMM:
            ok = pop_as(v, SLOT_CO_CONV, &e1) && pop_as(v, SLOT_EXPR, &e2) &&
                 push(v, slot(e1, SLOT_EXPR)) && push(v, slot(e2, SLOT_CO_CONV));
            break;

        /* Cong: S, (t e1 ... en) =?= (t e1' ... en') --> S, en =?= en', ..., e1 =?= e1' */
        case MMB_PROOF_CONG:
            ok = pop_as(v, SLOT_CO_CONV, &e1) && pop_as(v, SLOT_EXPR, &e2);
            if (ok && !(e1->app && e2->app && e1->id == e2->id))
                ok = fail(v, MM0_REFUSED, "Cong unify error");
            if (ok) {
                if (v->sp + 2 * (size_t)e1->nargs > v->stack_cap &&
                    !MM0_RESERVE(v->stack, v->stack_cap, v->sp + 2 * (size_t)e1->nargs))
                    ok = fail(v, MM0_NOMEM, "stack allocation");
                for (size_t i = e1->nargs; ok && i-- > 0;) {
                    v->stack[v->sp++] = slot(e2->args[i], SLOT_EXPR);
                    v->stack[v->sp++] = slot(e1->args[i], SLOT_CO_CONV);
                }
            }
            break;

        /* Unfold: S, (t e1 ... en) =?= e', e --> S, e =?= e'
         *   where Unify(t): e1, ..., en; e --> H'; . */
        case MMB_PROOF_UNFOLD: {
            ok = pop_as(v, SLOT_EXPR, &e) && pop_as(v, SLOT_CO_CONV, &e1);
            if (!ok) break;
            if (!e1->app) { ok = fail(v, MM0_REFUSED, "Unfold: not a definition"); break; }
            TermEntry te = term_entry(v, e1->id);
            if (!(te.sort & 0x80)) { ok = fail(v, MM0_REFUSED, "Unfold: not a definition"); break; }
            v->uhp = 0;
            if (!MM0_RESERVE(v->uheap, v->uheap_cap, e1->nargs)) {
                ok = fail(v, MM0_NOMEM, "unify heap allocation");
                break;
            }
            for (size_t i = 0; i < e1->nargs; ++i) v->uheap[i] = (uintptr_t)e1->args[i];
            v->uhp = e1->nargs;
            const uint8_t *cmd_saved = cmd;
            ok = run_unify(v, U_DEF, v->file + te.p_args + 8 * ((size_t)te.nargs + 1), e);
            v->cmd = cmd_saved;
            ok = ok && push(v, slot(e, SLOT_CO_CONV));
            break;
        }

        /* ConvCut: S, e1 =?= e2 --> S, e1 = e2, e1 =?= e2 */
        case MMB_PROOF_CONV_CUT:
            ok = pop_as(v, SLOT_CO_CONV, &e1) && pop_as(v, SLOT_EXPR, &e2) &&
                 push(v, slot(e2, SLOT_EXPR)) && push(v, slot(e1, SLOT_CONV)) &&
                 push(v, slot(e2, SLOT_EXPR)) && push(v, slot(e1, SLOT_CO_CONV));
            break;

        /* ConvSave: H; S, e1 = e2 --> H, e1 = e2; S */
        case MMB_PROOF_CONV_SAVE: {
            ok = pop_as(v, SLOT_CONV, &e1) && pop_as(v, SLOT_EXPR, &e2);
            MbConv *cell = ok ? alloc_conv(v, e1, e2) : NULL;
            ok = cell && push_heap(v, (MbSlot)cell | SLOT_CONV);
            break;
        }

        /* Save: H; S, s --> H, s; S, s   (s not an obligation) */
        case MMB_PROOF_SAVE: {
            if (!v->sp) { fail(v, MM0_REFUSED, "stack underflow"); return NULL; }
            MbSlot s = v->stack[v->sp - 1];
            if (SLOT_TAG(s) == SLOT_CO_CONV) {
                ok = fail(v, MM0_REFUSED, "Can't save proof obligation");
            } else if (SLOT_TAG(s) == SLOT_CONV) {
                if (v->sp < 2 || SLOT_TAG(v->stack[v->sp - 2]) != SLOT_EXPR) {
                    ok = fail(v, MM0_REFUSED, "bad stack slot");
                } else {
                    MbConv *cell = alloc_conv(v, SLOT_PTR(s), SLOT_PTR(v->stack[v->sp - 2]));
                    ok = cell && push_heap(v, (MbSlot)cell | SLOT_CONV);
                }
            } else {
                ok = push_heap(v, s);
            }
            break;
        }

        /* mmb.md: "Verifiers that signal successful verification using exit
         * codes are required to report failure for proofs using Sorry". */
        case MMB_PROOF_SORRY:
            fail(v, MM0_REFUSED, "proof uses Sorry");
            return NULL;

        default:
            fail(v, MM0_MALFORMED, mode == MODE_DEF ? "unknown opcode in def"
                                                    : "unknown opcode in theorem");
            return NULL;
        }
        if (!ok) return NULL;
        cmd += c.size;
    }
}

/* ---- retaining checked declarations ------------------------------------ */

/* Decode a checked unify stream into statement nodes of the theory.  The
 * stream was just matched against an acyclic store expression, so it is
 * well-formed; the checks here only guard the decoder itself. */

static void decoder_free(Decoder *d) {
    free(d->heap);
    free(d->frames);
    free(d->dummy_sorts);
}

static bool decode_push_heap(Mmb *v, Decoder *d, Mm0Expr *e) {
    GROW(d->heap, d->cap, d->n + 1, "statement allocation");
    d->heap[d->n++] = e;
    return true;
}

/* Reads one expression starting at *pcmd; on success *pcmd follows it. */
static bool decode_expr(Mmb *v, Decoder *d, uint32_t nargs, const uint8_t **pcmd,
                        Mm0Expr **out) {
    Mm0Arena *arena = &v->theory->arena;
    const uint8_t *cmd = *pcmd;
    Mm0Expr *result = NULL;
    d->nf = 0;
    for (;;) {
        MmbCmd c = {0};
        CHECK(cmd_at(v, cmd, &c));
        cmd += c.size;
        Mm0Expr *e = NULL;
        bool opened = false;
        switch (c.op) {
        case MMB_UNIFY_TERM:
        case MMB_UNIFY_TERM_SAVE: {
            ENSURE_FORMAT(c.data < v->nterms, "statement decoding");
            TermEntry te = term_entry(v, c.data);
            e = mm0_expr_app(arena, c.data, te.nargs);
            if (!e) return fail(v, MM0_NOMEM, "statement allocation");
            if (c.op == MMB_UNIFY_TERM_SAVE) CHECK(decode_push_heap(v, d, e));
            if (te.nargs) {
                GROW(d->frames, d->cap_frames, d->nf + 1, "statement allocation");
                d->frames[d->nf++] = (DecodeFrame){e, 0};
                opened = true;
            }
            break;
        }
        case MMB_UNIFY_REF:
            ENSURE_FORMAT(c.data < d->n, "statement decoding");
            e = d->heap[c.data];
            break;
        case MMB_UNIFY_DUMMY:
            GROW(d->dummy_sorts, d->cap_dummies, d->ndummies + 1, "statement allocation");
            d->dummy_sorts[d->ndummies] = c.data;
            e = mm0_expr_var(arena, nargs + (uint32_t)d->ndummies++);
            if (!e) return fail(v, MM0_NOMEM, "statement allocation");
            CHECK(decode_push_heap(v, d, e));
            break;
        default:
            return fail(v, MM0_MALFORMED, "statement decoding");
        }
        if (opened) continue;
        /* Fill e into its parent, closing every parent it completes. */
        for (;;) {
            if (!d->nf) { result = e; break; }
            DecodeFrame *f = &d->frames[d->nf - 1];
            f->node->args[f->next++] = e;
            if (f->next < f->node->nargs) break;
            e = f->node;
            --d->nf;
        }
        if (result) break;
    }
    *pcmd = cmd;
    *out = result;
    return true;
}

static bool copy_binders(Mmb *v, uint32_t p_args, uint32_t n, Mm0Binder **out,
                         uint32_t *nbound) {
    Mm0Arena *arena = &v->theory->arena;
    Mm0Binder *b = n ? mm0_arena_alloc(arena, n * sizeof *b) : NULL;
    uint64_t *words = n ? mm0_arena_alloc(arena, n * sizeof *words) : NULL;
    if (n && (!b || !words)) return fail(v, MM0_NOMEM, "statement allocation");
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint64_t ty = arg_at(v, p_args, i);
        words[i] = ty & MMB_TYPE_DEPS_MASK;
        bool is_bound = (ty & MMB_TYPE_BOUND_BIT) != 0;
        b[i] = (Mm0Binder){MMB_TYPE_SORT(ty), is_bound, is_bound ? k : 0, &words[i]};
        if (is_bound) ++k;
    }
    *out = b;
    *nbound = k;
    return true;
}

static const char **copy_var_names(Mmb *v, size_t count) {
    if (!count) return NULL;
    const char **names = mm0_arena_alloc(&v->theory->arena, count * sizeof *names);
    if (!names) { fail(v, MM0_NOMEM, "statement allocation"); return NULL; }
    for (size_t i = 0; i < count; ++i) {
        const char *src = i < v->names.var_count ? v->names.var_names[i] : NULL;
        names[i] = NULL;
        if (src && !(names[i] = mm0_arena_strndup(&v->theory->arena, src, strlen(src)))) {
            fail(v, MM0_NOMEM, "statement allocation");
            return NULL;
        }
    }
    return names;
}

static const char *copy_name(Mmb *v) {
    if (!v->names.name) return NULL;
    const char *name = mm0_arena_strndup(&v->theory->arena, v->names.name, v->names.name_len);
    if (!name) fail(v, MM0_NOMEM, "statement allocation");
    return name;
}

static bool retain_sort(Mmb *v, uint32_t s) {
    const char *name = NULL;
    if (v->names.name && !(name = copy_name(v))) return false;
    if (!mm0_theory_push_sort(v->theory, name, v->sorts[s]))
        return fail(v, MM0_NOMEM, "theory allocation");
    return true;
}

static bool retain_term(Mmb *v, uint32_t t, bool local) {
    TermEntry te = term_entry(v, t);
    Mm0TermDecl d = {.nargs = te.nargs, .depw = 1, .ret_sort = te.sort & 0x7F};
    CHECK(copy_binders(v, te.p_args, te.nargs, &d.args, &d.nbound));
    d.ret_deps = mm0_arena_alloc(&v->theory->arena, sizeof(uint64_t));
    if (!d.ret_deps) return fail(v, MM0_NOMEM, "statement allocation");
    *d.ret_deps = arg_at(v, te.p_args, te.nargs) & MMB_TYPE_DEPS_MASK;
    if (te.sort & 0x80) {
        d.flags |= MM0_DECL_DEF;
        Decoder *dec = &v->dec;
        dec->n = dec->ndummies = 0;
        for (uint32_t i = 0; i < te.nargs; ++i) {
            Mm0Expr *x = mm0_expr_var(&v->theory->arena, i);
            if (!x) return fail(v, MM0_NOMEM, "statement allocation");
            CHECK(decode_push_heap(v, dec, x));
        }
        const uint8_t *cmd = v->file + te.p_args + 8 * ((size_t)te.nargs + 1);
        CHECK(decode_expr(v, dec, te.nargs, &cmd, &d.body));
        if (dec->ndummies) {
            d.dummy_sorts = mm0_arena_alloc(&v->theory->arena, dec->ndummies * sizeof(uint32_t));
            if (!d.dummy_sorts) return fail(v, MM0_NOMEM, "statement allocation");
            memcpy(d.dummy_sorts, dec->dummy_sorts, dec->ndummies * sizeof(uint32_t));
        }
        d.ndummies = (uint32_t)dec->ndummies;
    }
    if (local) d.flags |= MM0_DECL_LOCAL;
    else if (v->names.name) CHECK(d.name = copy_name(v));
    if (!local && v->spec) {
        size_t count = (size_t)d.nargs + d.ndummies;
        if (count && !(d.var_names = copy_var_names(v, count))) return false;
    }
    if (!mm0_theory_push_term(v->theory, &d)) return fail(v, MM0_NOMEM, "theory allocation");
    return true;
}

static bool retain_thm(Mmb *v, uint32_t t, bool axiom, bool local) {
    ThmEntry th = thm_entry(v, t);
    Mm0ThmDecl d = {.nargs = th.nargs, .depw = 1};
    CHECK(copy_binders(v, th.p_args, th.nargs, &d.args, &d.nbound));
    Decoder *dec = &v->dec;
    dec->n = dec->ndummies = 0;
    for (uint32_t i = 0; i < th.nargs; ++i) {
        Mm0Expr *x = mm0_expr_var(&v->theory->arena, i);
        if (!x) return fail(v, MM0_NOMEM, "statement allocation");
        CHECK(decode_push_heap(v, dec, x));
    }
    /* mmb.md "Unify Stream": <concl>, UHyp, <hn>, ..., UHyp, <h1>, END */
    const uint8_t *cmd = v->file + th.p_args + 8 * (size_t)th.nargs;
    CHECK(decode_expr(v, dec, th.nargs, &cmd, &d.concl));
    size_t nhyps = 0;
    for (;;) {
        MmbCmd c = {0};
        CHECK(cmd_at(v, cmd, &c));
        if (c.op == MMB_CMD_END) break;
        ENSURE_FORMAT(c.op == MMB_UNIFY_HYP, "statement decoding");
        cmd += c.size;
        GROW(v->rhyps, v->rhyps_cap, nhyps + 1, "statement allocation");
        CHECK(decode_expr(v, dec, th.nargs, &cmd, &v->rhyps[nhyps]));
        ++nhyps;
    }
    if (nhyps) {
        d.hyps = mm0_arena_alloc(&v->theory->arena, nhyps * sizeof *d.hyps);
        if (!d.hyps) return fail(v, MM0_NOMEM, "statement allocation");
        for (size_t i = 0; i < nhyps; ++i) d.hyps[i] = v->rhyps[nhyps - 1 - i];
    }
    d.nhyps = (uint32_t)nhyps;
    if (axiom) d.flags |= MM0_DECL_AXIOM;
    if (local) d.flags |= MM0_DECL_LOCAL;
    else if (v->names.name) CHECK(d.name = copy_name(v));
    if (!local && v->spec && d.nargs && !(d.var_names = copy_var_names(v, d.nargs))) return false;
    if (!mm0_theory_push_thm(v->theory, &d)) return fail(v, MM0_NOMEM, "theory allocation");
    return true;
}

/* ---- statements (mm0-c verify) ----------------------------------------- */

static bool spec_status(Mmb *v, Mm0Status status) {
    v->cmd = NULL;
    if (status == MM0_OK) return true;
    if (v->error && v->error->status == MM0_OK) mm0_error_set(v->error, status, "specification");
    if (v->error && v->error->offset < 0) v->error->offset = v->stmt - v->file;
    return false;
}

static bool check_sort(Mmb *v, size_t size, uint32_t data) {
    ENSURE_FORMAT(data == size, "Next statement incorrect");
    ENSURE_FORMAT(v->nsorts < v->num_sorts, "Step sort overflow");
    v->names = (Mm0SpecNames){0};
    if (v->spec) {
        MmbTables tables = {v->file, v->end, v->num_sorts, v->sorts, v->nterms,
                            v->term_table, v->nthms, v->thm_table};
        CHECK(spec_status(v, mm0_spec_sort(v->spec, &tables, v->nsorts, &v->names, v->error)));
    }
    if (v->theory) CHECK(retain_sort(v, v->nsorts));
    if (v->spec) mm0_spec_release(v->spec);
    ++v->nsorts;
    return true;
}

static bool check_term(Mmb *v, uint8_t op, const uint8_t *body, size_t next_offset) {
    ENSURE_FORMAT(v->nterms < v->num_terms, "Step term overflow");
    TermEntry te = term_entry(v, v->nterms);
    uint32_t sort = te.sort & 0x7F;
    ENSURE(sort < v->nsorts, "bad sort");
    ENSURE(!(v->sorts[sort] & MM0_SORT_PURE), "term in pure sort");
    CHECK(load_args(v, te.p_args, (size_t)te.nargs + 1));
    uint64_t ret = arg_at(v, te.p_args, te.nargs);
    ENSURE((ret >> 56) == sort, "bad return type");
    --v->hp;
    v->sp = 0;
    bool local = (op & 0x08) != 0;
    if (te.sort & 0x80) {
        const uint8_t *end = run_proof(v, MODE_DEF, body);
        CHECK(end);
        v->cmd = NULL;
        ENSURE_FORMAT((size_t)(end - v->file) == next_offset, "Next statement incorrect");
        ENSURE(v->sp == 1, "stack has != one element");
        ENSURE(SLOT_TAG(v->stack[0]) == SLOT_EXPR, "bad stack slot");
        MbExpr *val = SLOT_PTR(v->stack[0]);
        ENSURE(sorts_compatible(val->type, ret), "type mismatch");
        ENSURE((val->type & MMB_TYPE_DEPS_MASK & ~ret) == 0, "type has unaccounted dependencies");
        v->uhp = 0;
        GROW(v->uheap, v->uheap_cap, te.nargs, "unify heap allocation");
        for (size_t i = 0; i < te.nargs; ++i) v->uheap[v->uhp++] = (uintptr_t)SLOT_PTR(v->heap[i]);
        CHECK(run_unify(v, U_DEF, v->file + te.p_args + 8 * ((size_t)te.nargs + 1), val));
        v->cmd = NULL;
    } else {
        ENSURE_FORMAT(body == v->file + next_offset, "Next statement incorrect");
        ENSURE_FORMAT(!local, "local term");
    }
    v->names = (Mm0SpecNames){0};
    if (v->spec && !local) {
        MmbTables tables = {v->file, v->end, v->num_sorts, v->sorts, v->nterms,
                            v->term_table, v->nthms, v->thm_table};
        CHECK(spec_status(v, mm0_spec_term(v->spec, &tables, v->nterms, &v->names, v->error)));
    }
    if (v->theory) CHECK(retain_term(v, v->nterms, local));
    if (v->spec) mm0_spec_release(v->spec);
    ++v->nterms;
    return true;
}

static bool check_thm(Mmb *v, uint8_t op, const uint8_t *body, size_t next_offset) {
    ENSURE_FORMAT(v->nthms < v->num_thms, "Step theorem overflow");
    ThmEntry th = thm_entry(v, v->nthms);
    v->sp = 0;
    v->hsp = 0;
    CHECK(load_args(v, th.p_args, th.nargs));
    const uint8_t *end = run_proof(v, MODE_THM, body);
    CHECK(end);
    v->cmd = NULL;
    ENSURE_FORMAT((size_t)(end - v->file) == next_offset, "Next statement incorrect");
    ENSURE(v->sp == 1, "stack has != one element");
    bool is_thm = (op & 0x37) == MMB_STMT_THM;
    ENSURE(SLOT_TAG(v->stack[0]) == (is_thm ? SLOT_PROOF : SLOT_EXPR), "bad stack slot");
    MbExpr *val = SLOT_PTR(v->stack[0]);
    ENSURE(v->sorts[MMB_TYPE_SORT(val->type)] & MM0_SORT_PROVABLE,
           "conclusion should have provable sort");
    v->uhp = 0;
    GROW(v->uheap, v->uheap_cap, th.nargs, "unify heap allocation");
    for (size_t i = 0; i < th.nargs; ++i) v->uheap[v->uhp++] = (uintptr_t)SLOT_PTR(v->heap[i]);
    CHECK(run_unify(v, U_THM_END, v->file + th.p_args + 8 * (size_t)th.nargs, val));
    v->cmd = NULL;
    bool local = (op & 0x08) != 0;
    bool axiom = (op & 0x3F) == MMB_STMT_AXIOM;
    v->names = (Mm0SpecNames){0};
    if (v->spec && !local) {
        MmbTables tables = {v->file, v->end, v->num_sorts, v->sorts, v->nterms,
                            v->term_table, v->nthms, v->thm_table};
        CHECK(spec_status(v, mm0_spec_thm(v->spec, &tables, v->nthms, axiom, &v->names, v->error)));
    }
    if (v->theory) CHECK(retain_thm(v, v->nthms, axiom, local));
    if (v->spec) mm0_spec_release(v->spec);
    ++v->nthms;
    return true;
}

static bool check_header(Mmb *v) {
    ENSURE_FORMAT(v->len >= 40, "header not long enough");
    const uint8_t *h = v->file;
    ENSURE_FORMAT(mm0_le32(h) == UINT32_C(0x42304D4D), "Not a MM0B file");
    ENSURE_FORMAT(h[4] == 1, "Wrong version");
    v->num_sorts = h[5];
    v->num_terms = mm0_le32(h + 8);
    v->num_thms = mm0_le32(h + 12);
    uint64_t p_terms = mm0_le32(h + 16), p_thms = mm0_le32(h + 20), p_proof = mm0_le32(h + 24);
    ENSURE_FORMAT(v->num_sorts <= 128, "Too many sorts");
    ENSURE_FORMAT(v->len >= 40 + (size_t)v->num_sorts, "header not long enough");
    ENSURE_FORMAT(v->len >= p_terms + (uint64_t)v->num_terms * 8, "Term table out of range");
    ENSURE_FORMAT(v->len >= p_thms + (uint64_t)v->num_thms * 8, "Theorem table out of range");
    ENSURE_FORMAT(v->len > p_proof, "Proof section out of range");
    v->sorts = h + 40;
    v->term_table = v->file + p_terms;
    v->thm_table = v->file + p_thms;
    v->stmt = v->file + p_proof;
    return true;
}

static bool check_file(Mmb *v) {
    CHECK(check_header(v));
    const uint8_t *stmt = v->stmt;
    for (;;) {
        v->stmt = stmt;
        v->cmd = NULL;
        if (*stmt == MMB_CMD_END) break;
        MmbCmd c = {0};
        CHECK(cmd_at(v, stmt, &c));
        size_t offset = (size_t)(stmt - v->file);
        ENSURE_FORMAT(c.data <= v->len - offset && v->len - offset - c.data >= 5,
                      "proof command out of range");
        size_t next = offset + c.data;
        Mm0ArenaMark mark = mm0_arena_mark(&v->store);
        bool ok;
        switch (c.op) {
        case MMB_STMT_SORT:
            ok = check_sort(v, c.size, c.data);
            break;
        case MMB_STMT_DEF:
        case MMB_STMT_LOCAL_DEF:
            ok = check_term(v, c.op, stmt + c.size, next);
            break;
        case MMB_STMT_AXIOM:
        case MMB_STMT_THM:
        case MMB_STMT_LOCAL_THM:
            ok = check_thm(v, *stmt, stmt + c.size, next);
            break;
        default:
            ok = fail(v, MM0_MALFORMED, "bad statement command");
        }
        mm0_arena_reset(&v->store, mark);
        CHECK(ok);
        stmt = v->file + next;
    }
    ENSURE_FORMAT(v->nsorts == v->num_sorts, "not all sorts proved");
    ENSURE_FORMAT(v->nterms == v->num_terms, "not all terms proved");
    ENSURE_FORMAT(v->nthms == v->num_thms, "not all theorems proved");
    if (v->spec) {
        MmbTables tables = {v->file, v->end, v->num_sorts, v->sorts, v->nterms,
                            v->term_table, v->nthms, v->thm_table};
        v->stmt = NULL;
        CHECK(spec_status(v, mm0_spec_finish(v->spec, &tables, v->error)));
    }
    return true;
}

Mm0Status mm0_load_mmb(Mm0Theory *theory, const uint8_t *mmb, size_t mmb_len,
                       const char *spec, size_t spec_len,
                       const Mm0Options *options, Mm0Error *error) {
    Mm0Options defaults;
    if (!options) { mm0_options_init(&defaults); options = &defaults; }
    Mm0Error local_error;
    if (!error) error = &local_error;
    *error = (Mm0Error){.status = MM0_OK, .offset = -1};
    if (!theory || !mmb) {
        mm0_error_set(error, MM0_MALFORMED, "missing theory or file");
        return error->status;
    }
    if (theory->norder) {
        mm0_error_set(error, MM0_REFUSED, "an MMB file loads into an empty theory");
        return error->status;
    }
    if (!spec && !options->without_spec) {
        mm0_error_set(error, MM0_MALFORMED, "missing specification");
        return error->status;
    }
    Mm0Steps steps;
    mm0_steps_init(&steps, options);
    Mmb v = {.file = mmb, .end = mmb + mmb_len, .len = mmb_len, .error = error,
             .steps = &steps, .theory = options->check_only ? NULL : theory};
    mm0_arena_init(&v.store);
    Mm0ArenaMark start = mm0_arena_mark(&theory->arena);
    if (!options->without_spec) {
        v.spec = mm0_spec_new(spec, spec_len, &steps);
        if (!v.spec) mm0_error_set(error, MM0_NOMEM, "specification allocation");
    }
    bool ok = error->status == MM0_OK && check_file(&v);
    if (!ok && error->status == MM0_OK) mm0_error_set(error, MM0_REFUSED, "verification failed");
    if (!ok) {
        mm0_theory_rollback(theory, 0, start);
        if (error->decl[0] == 0 && v.stmt) {
            const char *kind = v.nthms || v.nterms || v.nsorts ? "statement" : "header";
            snprintf(error->decl, sizeof error->decl, "%s at %lld (sorts %u, terms %u, theorems %u)",
                     kind, (long long)(v.stmt - v.file), v.nsorts, v.nterms, v.nthms);
        }
    }
    mm0_spec_free(v.spec);
    mm0_arena_free(&v.store);
    free(v.stack);
    free(v.heap);
    free(v.hyps);
    free(v.ustack);
    free(v.uheap);
    free(v.bound_deps);
    decoder_free(&v.dec);
    free(v.rhyps);
    return error->status;
}
