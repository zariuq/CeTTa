#include "transform_core.h"

#include "match.h"
#include "native_query.h"
#include "parser.h"
#include "petta_semantics.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CETTA_TRANSFORM_TRACE=1 reports each native step on stderr. */
static bool transform_trace(void) {
    static int trace = -1;
    if (trace < 0) {
        const char *setting = getenv("CETTA_TRANSFORM_TRACE");
        trace = setting && strcmp(setting, "1") == 0;
    }
    return trace != 0;
}

static bool transform_cursor_reference(void) {
    const char *setting = getenv("CETTA_TRANSFORM_CURSOR_REFERENCE");
    return setting && strcmp(setting, "1") == 0;
}

/* ── The plan ──────────────────────────────────────────────────────────── */

enum {
    TF_HEAD_PUBLIC_2 = 1u << 0,
    TF_HEAD_PUBLIC_3 = 1u << 1,
    TF_HEAD_PUBLIC_4 = 1u << 2,
    TF_HEAD_NATIVE_2 = 1u << 3,
    TF_HEAD_NATIVE_3 = 1u << 4,
    TF_HEAD_NATIVE_4 = 1u << 5,
    TF_HEAD_RUN_4 = 1u << 6,
    TF_HEAD_NAME = 1u << 7,
};

typedef struct {
    SymbolId head;
    uint32_t mask;
} TransformHeadMemo;

typedef struct {
    const SymbolTable *owner;
    uint64_t owner_id;
    SymbolId ids[7];
    TransformHeadMemo recent[16];
} TransformNameCache;

static void transform_name_cache_reset(TransformNameCache *cache) {
    memset(cache, 0, sizeof(*cache));
    cache->ids[CETTA_TRANSFORM_NAME_TRANSFORM] = atom_symbol_literal_id("transform");
    cache->ids[CETTA_TRANSFORM_NAME_SPLIT] = atom_symbol_literal_id("tf:split");
    cache->ids[CETTA_TRANSFORM_NAME_PROPOSE] = atom_symbol_literal_id("tf:propose");
    cache->ids[CETTA_TRANSFORM_NAME_INST] = atom_symbol_literal_id("tf:inst");
    cache->ids[CETTA_TRANSFORM_NAME_FIRE] = atom_symbol_literal_id("tf:fire");
    cache->ids[CETTA_TRANSFORM_NAME_FIRE_INTO] = atom_symbol_literal_id("tf:fire-into");
    cache->ids[CETTA_TRANSFORM_NAME_RUN] = atom_symbol_literal_id("tf:run");
    cache->owner = g_symbols;
    cache->owner_id = g_symbols->instance_id;
}

static inline __attribute__((always_inline)) TransformNameCache *
transform_name_cache(void) {
    /* Ids and their classifications belong to one table incarnation. */
    static _Thread_local TransformNameCache cache;
    if (!g_symbols)
        return NULL;
    if (cache.owner != g_symbols || cache.owner_id != g_symbols->instance_id)
        transform_name_cache_reset(&cache);
    return &cache;
}

static uint32_t transform_head_classify_slow(TransformNameCache *cache,
                                             SymbolId head) {
    /* Every name of the family begins with `t`: any other symbol is
     * classified by its first byte. */
    const char *spelling = symbol_bytes(g_symbols, head);
    if (!spelling || spelling[0] != 't')
        return 0u;
    uint32_t mask = 0u;
    for (size_t i = 0u; i < 7u && mask == 0u; i++) {
        if (head != cache->ids[i])
            continue;
        mask = TF_HEAD_NAME;
        switch ((CettaTransformName)i) {
        case CETTA_TRANSFORM_NAME_TRANSFORM:
            mask |= TF_HEAD_PUBLIC_3 | TF_HEAD_PUBLIC_4;
            break;
        case CETTA_TRANSFORM_NAME_SPLIT:
            mask |= TF_HEAD_PUBLIC_2 | TF_HEAD_NATIVE_2;
            break;
        case CETTA_TRANSFORM_NAME_PROPOSE:
            mask |= TF_HEAD_PUBLIC_3 | TF_HEAD_NATIVE_3;
            break;
        case CETTA_TRANSFORM_NAME_INST:
            mask |= TF_HEAD_PUBLIC_2 | TF_HEAD_NATIVE_2;
            break;
        case CETTA_TRANSFORM_NAME_FIRE:
            mask |= TF_HEAD_PUBLIC_3 | TF_HEAD_NATIVE_3;
            break;
        case CETTA_TRANSFORM_NAME_FIRE_INTO:
            mask |= TF_HEAD_PUBLIC_4 | TF_HEAD_NATIVE_4;
            break;
        case CETTA_TRANSFORM_NAME_RUN:
            mask |= TF_HEAD_PUBLIC_4 | TF_HEAD_NATIVE_4 | TF_HEAD_RUN_4;
            break;
        }
    }
    return mask;
}

static inline __attribute__((always_inline)) uint32_t
transform_head_mask(SymbolId head) {
    TransformNameCache *cache = transform_name_cache();
    if (!cache)
        return 0u;
    TransformHeadMemo *memo = &cache->recent[head & 15u];
    if (memo->head != head) {
        *memo = (TransformHeadMemo){
            head, transform_head_classify_slow(cache, head)};
        if (memo->mask == 0u)
            symbol_table_add_flags(g_symbols, head,
                                   CETTA_SYMBOL_FLAG_NOT_TRANSFORM_NAME);
    }
    return memo->mask;
}

SymbolId cetta_transform_name_id(CettaTransformName name) {
    TransformNameCache *cache = transform_name_cache();
    return cache && name >= CETTA_TRANSFORM_NAME_TRANSFORM &&
           name <= CETTA_TRANSFORM_NAME_RUN
        ? cache->ids[name] : SYMBOL_ID_NONE;
}

bool cetta_transform_call_head_slow(SymbolId head, CettaExprLen arity) {
    if (arity < 2u || arity > 4u)
        return false;
    uint32_t mask = transform_head_mask(head);
    return (mask & (TF_HEAD_PUBLIC_2 << (arity - 2u))) != 0u;
}

bool cetta_transform_native_call_head_slow(SymbolId head, CettaExprLen arity) {
    if (arity < 2u || arity > 4u)
        return false;
    uint32_t mask = transform_head_mask(head);
    return (mask & (TF_HEAD_NATIVE_2 << (arity - 2u))) != 0u;
}

bool cetta_transform_run_call_head_slow(SymbolId head, CettaExprLen arity) {
    return arity == 4u &&
        (transform_head_mask(head) & TF_HEAD_RUN_4) != 0u;
}

bool cetta_transform_name_head_slow(SymbolId head) {
    return (transform_head_mask(head) & TF_HEAD_NAME) != 0u;
}

typedef enum {
    TF_READ = 0,
    TF_TAKE,
    TF_ABSENT,
    TF_UNIQUE,
    TF_COLLECT,
    TF_LET,
    TF_AS,             /* a branch's pattern unified with the atom its proposal matched */
} TfKind;

typedef struct {
    TfKind kind;
    TfKind access;     /* TF_READ or TF_TAKE: how a matched atom is used */
    Atom *space_ref;   /* as the conjunct names it */
    Space *space;
    Atom *pattern;     /* the matched pattern, or a let's pattern */
    bool joint;        /* absent, unique, collect of (, p1 ... pn): joint matches */
    bool exact;        /* a branch's read or take of the atom its proposal
                        * matched: it meets the stored atoms that are that
                        * atom, not those the atom unifies with */
    Atom *collection;  /* collect: the collection pattern */
    Atom *expression;  /* let: the code */
} TfConjunct;

typedef struct {
    TfConjunct *items;
    size_t len, cap;
    size_t kernel_len; /* conjuncts before the first `all` */
    size_t propose_len;/* conjuncts before the first `all` or `once` */
    bool has_all;
    bool has_bound;    /* an `all` or a `once` met: propose_len is set */
    bool has_let;
    Atom *default_space; /* the space a conjunct that names none reads */
} TfPlan;

typedef enum {
    TF_MODE_PLAIN = 0,
    TF_MODE_READ,
    TF_MODE_TAKE,
    TF_MODE_ABSENT,
    TF_MODE_UNIQUE,
    TF_MODE_COLLECT,
    TF_MODE_ALL,
    TF_MODE_ONCE,
    TF_MODE_AS,
    TF_MODE_LET,
} TfMode;

/* tf:mode: an expression headed by a mode word; anything else is a pattern. */
static TfMode tf_mode(Atom *c) {
    if (!c || c->kind != ATOM_EXPR || c->expr.len == 0u)
        return TF_MODE_PLAIN;
    Atom *head = c->expr.elems[0];
    if (head->kind != ATOM_SYMBOL)
        return TF_MODE_PLAIN;
    static const struct { const char *name; TfMode mode; } words[] = {
        {"read", TF_MODE_READ}, {"take", TF_MODE_TAKE},
        {"absent", TF_MODE_ABSENT}, {"unique", TF_MODE_UNIQUE},
        {"collect", TF_MODE_COLLECT}, {"all", TF_MODE_ALL}, {"once", TF_MODE_ONCE},
        {"let", TF_MODE_LET}, {"tf:as", TF_MODE_AS},
    };
    for (size_t i = 0u; i < sizeof(words) / sizeof(words[0]); i++)
        if (atom_is_symbol(head, words[i].name))
            return words[i].mode;
    return TF_MODE_PLAIN;
}

static bool tf_is_comma(Atom *x) {
    return x && x->kind == ATOM_EXPR && x->expr.len > 0u &&
           atom_is_symbol(x->expr.elems[0], ",");
}

typedef struct {
    const CettaTransformHost *host;
    Atom *default_space;
} TfPlanner;

static bool tf_plan_push(TfPlan *plan, TfConjunct conjunct) {
    if (plan->len == plan->cap) {
        size_t cap = plan->cap ? plan->cap * 2u : 8u;
        TfConjunct *items = cetta_realloc(plan->items, cap * sizeof(*items));
        if (!items)
            return false;
        plan->items = items;
        plan->cap = cap;
    }
    plan->items[plan->len++] = conjunct;
    return true;
}

/* tf:locate for a read, take or plain conjunct: its access, space and pattern. */
/* A proposal's guide names the transform's own space `tf:here`
 * (tf_proposal_atom); a guided conjunct that says so reads that space. */
static bool tf_is_here(Atom *space_ref) {
    return atom_is_symbol_id(space_ref, atom_symbol_literal_id("tf:here"));
}

static bool tf_locate(const TfPlanner *planner, Atom *c, TfKind *access,
                      Atom **space_ref, Atom **pattern) {
    TfMode mode = tf_mode(c);
    if (mode == TF_MODE_PLAIN) {
        *access = TF_READ;
        *space_ref = planner->default_space;
        *pattern = c;
        return true;
    }
    if (mode != TF_MODE_READ && mode != TF_MODE_TAKE && mode != TF_MODE_ABSENT)
        return false;
    *access = mode == TF_MODE_TAKE ? TF_TAKE : TF_READ;
    if (c->expr.len == 3u) {
        *space_ref = tf_is_here(c->expr.elems[1]) ? planner->default_space
                                                  : c->expr.elems[1];
        *pattern = c->expr.elems[2];
        return true;
    }
    if (c->expr.len == 2u) {
        *space_ref = planner->default_space;
        *pattern = c->expr.elems[1];
        return true;
    }
    return false;
}

/* A condition's pattern: a pattern, or (, p1 ... pn) with n > 0 parts none of
 * which is itself headed by `,`, whose joint matches the condition ranges
 * over. */
static bool tf_joint_readable(TfConjunct *conjunct) {
    Atom *p = conjunct->pattern;
    conjunct->joint = tf_is_comma(p);
    if (!conjunct->joint)
        return true;
    if (p->expr.len < 2u)
        return false;
    for (CettaExprLen i = 1u; i < p->expr.len; i++)
        if (tf_is_comma(p->expr.elems[i]))
            return false;
    return true;
}

static bool tf_resolve(const TfPlanner *planner, TfConjunct *conjunct) {
    conjunct->space = NULL;
    /* Cache a pure lookup only. A space may be supplied or rebound by an
     * earlier let; errors belong at that conjunct, after earlier effects. */
    (void)planner->host->resolve_space(planner->host->context,
                                      conjunct->space_ref, &conjunct->space);
    return true;
}

/* The library's walk, flattened: an `all` or a `once` splices its inner
 * conjuncts, and the first `all` ends the kernel.  The library has read the
 * sources already, so a read or take of a pattern headed by `,` is an atom a
 * branch accesses exactly, matched structurally here; an absent, unique or
 * collect of one ranges over its joint matches (tf_each_joint). */
static bool tf_plan_list(const TfPlanner *planner, TfPlan *plan,
                         Atom **conjuncts, size_t len) {
    plan->default_space = planner->default_space;
    for (size_t i = 0u; i < len; i++) {
        Atom *c = conjuncts[i];
        TfConjunct conjunct = {0};
        switch (tf_mode(c)) {
        case TF_MODE_ONCE:
        case TF_MODE_ALL: {
            if (c->expr.len != 2u)
                return false;
            /* `once` splices its inner conjuncts into the firing's own: the
             * firing commits the first candidate that validates.  `all`
             * also ends the kernel. */
            if (tf_mode(c) == TF_MODE_ALL && !plan->has_all) {
                plan->has_all = true;
                plan->kernel_len = plan->len;
            }
            /* A proposal is the kernel's match up to the first `all` or
             * `once` (tf:once and tf:all in the propose phase). */
            if (!plan->has_bound) {
                plan->has_bound = true;
                plan->propose_len = plan->len;
            }
            /* tf:conjuncts of the inner sources */
            Atom *inner = c->expr.elems[1];
            bool spliced;
            if (inner->kind == ATOM_EXPR && inner->expr.len == 0u)
                spliced = true;
            else if (tf_is_comma(inner))
                spliced = tf_plan_list(planner, plan, inner->expr.elems + 1,
                                       inner->expr.len - 1u);
            else
                spliced = tf_plan_list(planner, plan, &inner, 1u);
            if (!spliced)
                return false;
            continue;
        }
        case TF_MODE_AS:
            /* (tf:as P A): written by the library, not by programs */
            if (c->expr.len != 3u)
                return false;
            conjunct.kind = TF_AS;
            conjunct.pattern = c->expr.elems[1];
            conjunct.expression = c->expr.elems[2];
            /* The library writes it after the read or take of the atom
             * itself, which is then an access of exactly that atom. */
            if (plan->len != 0u) {
                TfConjunct *before = &plan->items[plan->len - 1u];
                if ((before->kind == TF_READ || before->kind == TF_TAKE) &&
                    !before->joint &&
                    atom_eq(before->pattern, conjunct.expression))
                    before->exact = true;
            }
            break;
        case TF_MODE_LET:
            if (c->expr.len != 3u)
                return false;
            conjunct.kind = TF_LET;
            conjunct.pattern = c->expr.elems[1];
            conjunct.expression = c->expr.elems[2];
            plan->has_let = true;
            break;
        case TF_MODE_UNIQUE:
        case TF_MODE_COLLECT: {
            bool collect = tf_mode(c) == TF_MODE_COLLECT;
            if (c->expr.len != (collect ? 3u : 2u))
                return false;
            Atom *inner = c->expr.elems[1];
            TfMode inner_mode = tf_mode(inner);
            if (inner_mode != TF_MODE_PLAIN && inner_mode != TF_MODE_READ &&
                inner_mode != TF_MODE_TAKE)
                return false;
            conjunct.kind = collect ? TF_COLLECT : TF_UNIQUE;
            conjunct.collection = collect ? c->expr.elems[2] : NULL;
            if (!tf_locate(planner, inner, &conjunct.access,
                           &conjunct.space_ref, &conjunct.pattern) ||
                !tf_joint_readable(&conjunct) ||
                !tf_resolve(planner, &conjunct))
                return false;
            break;
        }
        case TF_MODE_ABSENT:
            conjunct.kind = TF_ABSENT;
            if (!tf_locate(planner, c, &conjunct.access, &conjunct.space_ref,
                           &conjunct.pattern) ||
                !tf_joint_readable(&conjunct) ||
                !tf_resolve(planner, &conjunct))
                return false;
            conjunct.access = TF_READ;
            break;
        case TF_MODE_PLAIN:
        case TF_MODE_READ:
        case TF_MODE_TAKE:
            if (!tf_locate(planner, c, &conjunct.access, &conjunct.space_ref,
                           &conjunct.pattern) ||
                !tf_resolve(planner, &conjunct))
                return false;
            conjunct.kind = conjunct.access;
            break;
        }
        if (!tf_plan_push(plan, conjunct))
            return false;
    }
    return true;
}

/* ── Proposal ──────────────────────────────────────────────────────────── */

typedef struct {
    TfKind access;
    Atom *space_ref;
    Space *space;
    Atom *atom;
} TfEntry;

typedef struct {
    TfEntry *items;
    size_t len, cap;
} TfEntries;

typedef struct {
    TfEntries entries;
    Atom *instance;
} TfRow;

typedef struct {
    TfEntries kernel;
    TfRow *rows;
    size_t row_len;
} TfCandidate;

typedef enum {
    TF_CONTINUE = 0,
    TF_STOP,      /* a candidate validated, or committed */
    TF_FAIL,      /* capacity refusal before anything ran: decline */
    TF_ERROR,     /* an error after something ran: answered */
    TF_INTERRUPTED, /* the host records fuel/cancellation separately */
} TfStatus;

typedef struct {
    const CettaTransformHost *host;
    Arena *scratch;
    Arena *answer_arena;
    TfPlan plan;
    Atom *template_atom;
    BindingsBuilder builder;
    CettaFrameIdentityScope *scope;
    TfEntries entries;      /* the footprint of the row being proposed */
    Atom **met_parts;       /* the stored atoms a joint match's parts met,
                             * during its visit: NULL for a ground one */
    size_t met_len;
    Atom *stored;           /* the stored atom the match being visited met,
                             * when it has variables: what a read or take of
                             * it reads or takes, whatever the pattern made
                             * of its variables */
    bool extension;         /* proposing the rows of an `all` */
    size_t extension_start; /* entries before the extension */
    TfRow *rows;            /* the extension rows of the current kernel match */
    size_t row_len, row_cap;
    TfCandidate *candidates;/* kept when a let may run code: validated after;
                             * otherwise the first that validates, committed
                             * after the proposal has released its readers */
    size_t candidate_len, candidate_cap;
    bool effects;           /* code ran: a decline is no longer possible */
    bool interrupted;
    bool committing;        /* the atomic transition was paid before its writes */
    bool propose;           /* the propose phase: each kernel match is kept */
    struct TfAnswers *reads_answers; /* reads only: each kernel match's instance */
    bool bag;               /* the consumer takes every answer as a bag: the
                             * order of the matches is free */
    Atom **proposals;       /* (tf:cand GUIDE), in the answer arena */
    size_t proposal_len, proposal_cap;
    Atom *answer;
    Atom *error;
} TfFire;

/* Natural-number spending is monotone and composes by addition. Refusing
 * a step leaves the pending operation untouched; see CostBoundedReachability
 * (prefix_within_budget) and LibTransform (firing_atomic) in Mettapedia. */
static bool tf_spend(TfFire *f) {
    if (f->interrupted)
        return false;
    if (!f->committing && f->host->spend &&
        !f->host->spend(f->host->context, 1u)) {
        f->interrupted = true;
        return false;
    }
    return true;
}

static bool tf_entries_push(TfEntries *entries, TfEntry entry) {
    if (entries->len == entries->cap) {
        size_t cap = entries->cap ? entries->cap * 2u : 8u;
        TfEntry *items = cetta_realloc(entries->items, cap * sizeof(*items));
        if (!items)
            return false;
        entries->items = items;
        entries->cap = cap;
    }
    entries->items[entries->len++] = entry;
    return true;
}

static Bindings *tf_bindings(TfFire *f) {
    return (Bindings *)bindings_builder_bindings(&f->builder);
}

static Atom *tf_apply(TfFire *f, Atom *atom) {
    return bindings_apply(tf_bindings(f), f->scratch, atom);
}

typedef TfStatus (*TfVisit)(TfFire *f, void *data);

/* ── A leg's rows tested before they are matched ───────────────────────── */

/* The lets that follow a read or take and are tests of integers (the host's
 * guard_filter) can be put to a row of the leg as the row is stored: the
 * integers they compare are known before the leg or stand in the row's own
 * elements.  A row a test certainly refuses is passed over with nothing
 * decoded, matched or bound; every other row (one the tests pass, or one
 * they cannot read: a row with variables, a value that is no integer, a
 * result outside the 64-bit integers, a division the dialect rounds or
 * refuses in its own way) is matched and its lets evaluated as always, so
 * the tests only ever skip what the lets would refuse. */
typedef struct {
    bool known;      /* fixed before the leg */
    int64_t value;
    uint32_t element;/* else the row's element at this place */
} TfPretestValue;

typedef struct {
    CettaNativeFilterOp op;
    CettaNativeIntOperand as[2];
    TfPretestValue value[2][2];
} TfPretestTest;

typedef struct {
    CettaExprLen arity; /* elements of the leg's pattern */
    TfPretestTest tests[CETTA_NATIVE_QUERY_FILTERS];
    size_t len;
} TfPretest;

static bool tf_pretest_read(const TfPretestValue *value,
                            const TermUniverse *universe, AtomId row,
                            int64_t *out) {
    if (value->known) {
        *out = value->value;
        return true;
    }
    AtomId id = tu_child(universe, row, value->element);
    if (id == CETTA_ATOM_ID_NONE || tu_kind(universe, id) != ATOM_GROUNDED ||
        tu_ground_kind(universe, id) != GV_INT)
        return false;
    *out = tu_int(universe, id);
    return true;
}

static bool tf_pretest_side(const TfPretestTest *test, unsigned side,
                            const TermUniverse *universe, AtomId row,
                            int64_t *out) {
    int64_t a = 0, b = 0;
    if (!tf_pretest_read(&test->value[side][0], universe, row, &a))
        return false;
    if (test->as[side] == CETTA_NATIVE_INT_VALUE) {
        *out = a;
        return true;
    }
    if (!tf_pretest_read(&test->value[side][1], universe, row, &b))
        return false;
    switch (test->as[side]) {
    case CETTA_NATIVE_INT_SUM:
        return !__builtin_add_overflow(a, b, out);
    case CETTA_NATIVE_INT_DIFFERENCE:
        return !__builtin_sub_overflow(a, b, out);
    case CETTA_NATIVE_INT_PRODUCT:
        return !__builtin_mul_overflow(a, b, out);
    case CETTA_NATIVE_INT_QUOTIENT:
    case CETTA_NATIVE_INT_REMAINDER:
        if (a < 0 || b <= 0)
            return false;
        *out = test->as[side] == CETTA_NATIVE_INT_QUOTIENT ? a / b : a % b;
        return true;
    case CETTA_NATIVE_INT_VALUE:
        break;
    }
    return false;
}

/* Whether a test certainly refuses the stored row. */
static bool tf_pretest_refuses(const TfPretest *pretest,
                               const TermUniverse *universe, AtomId row) {
    if (!universe || row == CETTA_ATOM_ID_NONE || !tu_hdr(universe, row) ||
        tu_kind(universe, row) != ATOM_EXPR || tu_has_vars(universe, row) ||
        tu_petta_value_representation(universe, row) != PETTA_VALUE_ORDINARY ||
        tu_arity(universe, row) != pretest->arity)
        return false;
    for (size_t i = 0u; i < pretest->len; i++) {
        const TfPretestTest *test = &pretest->tests[i];
        int64_t left = 0, right = 0;
        /* A test that cannot be read decides nothing, and neither does any
         * after it: the lets are evaluated in their order. */
        if (!tf_pretest_side(test, 0u, universe, row, &left) ||
            !tf_pretest_side(test, 1u, universe, row, &right))
            return false;
        bool met = test->op == CETTA_NATIVE_FILTER_LESS ? left < right
            : test->op == CETTA_NATIVE_FILTER_NOT_LESS ? !(left < right)
            : test->op == CETTA_NATIVE_FILTER_INT_ALIKE ? left == right
            : test->op == CETTA_NATIVE_FILTER_INT_APART ? left != right
            : true;
        if (test->op != CETTA_NATIVE_FILTER_LESS &&
            test->op != CETTA_NATIVE_FILTER_NOT_LESS &&
            test->op != CETTA_NATIVE_FILTER_INT_ALIKE &&
            test->op != CETTA_NATIVE_FILTER_INT_APART)
            return false;
        if (!met)
            return true;
    }
    return false;
}

/* Each stored atom of `space` matching `pattern` under the current bindings,
 * in declaration order, as `match` reads them; `visit` sees the bindings the
 * match adds, which are undone after it.  EXACT: only the stored atoms that
 * are the pattern, the same atom or the same but for the names of its
 * variables.  PRETEST, when given: the rows it certainly refuses are passed
 * over unmatched (tf_pretest_refuses). */
static TfStatus tf_each_match(TfFire *f, Space *space, Atom *pattern,
                              bool exact, const TfPretest *pretest,
                              TfVisit visit, void *data) {
    if (!space)
        return TF_CONTINUE; /* an empty location */
    Atom *resolved = atom_has_vars(pattern) ? tf_apply(f, pattern) : pattern;
    Atom *index = resolved
        ? petta_semantics_match_index_pattern(f->scratch, resolved, NULL)
        : NULL;
    if (!index)
        return f->effects ? TF_ERROR : TF_FAIL;
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!space_occurrence_cursor_init(space, index, &cursor))
        return f->effects ? TF_ERROR : TF_FAIL;
    TfStatus status = TF_CONTINUE;
    for (;;) {
        if (!tf_spend(f)) { status = TF_INTERRUPTED; break; }
        Atom *candidate = NULL;
        SpaceOccurrenceCursorStep step;
        if (pretest) {
            /* The row as stored first: a row the tests refuse is not lent. */
            AtomId row = CETTA_ATOM_ID_NONE;
            CettaIndex index = 0u;
            step = space_occurrence_cursor_next_id(&cursor, &index, &row);
            if (step == SPACE_OCCURRENCE_CURSOR_ITEM) {
                if (tf_pretest_refuses(
                        pretest, space_occurrence_cursor_universe(&cursor),
                        row))
                    continue;
                candidate = space_occurrence_cursor_atom(&cursor, index);
            }
        } else {
            step = space_occurrence_cursor_next(&cursor, NULL, &candidate);
        }
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        if (step == SPACE_OCCURRENCE_CURSOR_INVALIDATED) {
            status = f->effects ? TF_ERROR : TF_FAIL;
            break;
        }
        if (!candidate || (exact && !atom_alpha_eq(candidate, resolved)))
            continue;
        Atom *open = NULL;
        if (atom_has_vars(candidate)) {
            CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
            if (!cetta_frame_identity_scope_try(f->scope, &epoch)) {
                status = f->effects ? TF_ERROR : TF_FAIL;
                break;
            }
            candidate = atom_freshen_epoch(f->scratch, candidate, epoch);
            if (!candidate) {
                status = f->effects ? TF_ERROR : TF_FAIL;
                break;
            }
            open = candidate;
        }
        uint32_t mark = bindings_builder_save(&f->builder);
        if (match_atoms_builder(pattern, candidate, &f->builder, f->scratch)) {
            Atom *outer = f->stored;
            f->stored = open;
            status = visit(f, data);
            f->stored = outer;
        }
        bindings_builder_rollback(&f->builder, mark);
        if (status != TF_CONTINUE)
            break;
    }
    space_occurrence_cursor_release(&cursor);
    return status;
}

typedef struct {
    Space *space;
    Atom **parts;
    size_t nparts, k;
    TfVisit visit;
    void *data;
    /* The stored atom each part met, where it holds variables. */
    Atom **met;
} TfJoint;

static TfStatus tf_joint_next(TfFire *f, void *data) {
    TfJoint *j = data;
    if (j->k != 0u)
        j->met[j->k - 1u] = f->stored;
    if (j->k == j->nparts) {
        Atom **outer = f->met_parts;
        size_t outer_len = f->met_len;
        f->met_parts = j->met;
        f->met_len = j->nparts;
        TfStatus status = j->visit(f, j->data);
        f->met_parts = outer;
        f->met_len = outer_len;
        return status;
    }
    TfJoint deeper = *j;
    deeper.k++;
    return tf_each_match(f, j->space, j->parts[j->k], false, NULL,
                         tf_joint_next, &deeper);
}

/* Each joint match of (, p1 ... pn), as `match` reads the conjunction: a match
 * of p1, then a match of p2 under its bindings, and so on; `visit` sees the
 * bindings of all n.  A read may meet one occurrence at several parts. */
static TfStatus tf_each_joint(TfFire *f, Space *space, Atom *pattern,
                              TfVisit visit, void *data) {
    if (!space)
        return TF_CONTINUE; /* an empty location */
    size_t nparts = (size_t)pattern->expr.len - 1u;
    Atom **met = arena_alloc(f->scratch, (nparts ? nparts : 1u) * sizeof(*met));
    if (!met)
        return f->effects ? TF_ERROR : TF_FAIL;
    TfJoint j = {space, pattern->expr.elems + 1u, nparts, 0u, visit, data,
                 met};
    return tf_joint_next(f, &j);
}

/* A condition's matches: of its pattern, or the joint ones. */
static TfStatus tf_each_query(TfFire *f, const TfConjunct *c, TfVisit visit,
                              void *data) {
    return c->joint ? tf_each_joint(f, c->space, c->pattern, visit, data)
                    : tf_each_match(f, c->space, c->pattern, c->exact, NULL,
                                    visit, data);
}

static TfStatus tf_walk(TfFire *f, size_t at);
static bool tf_pretest_build(TfFire *f, size_t at, TfPretest *pretest);
static TfStatus tf_candidate(TfFire *f, TfEntries *kernel, TfRow *rows,
                             size_t row_len);

/* ── Reads by the native query engine ──────────────────────────────────────
 * The step's reads are the native query engine's joins (native_query.h): a
 * run of read and take conjuncts is one conjunction over their spaces, and an
 * absence or a unique is one query.  The engine answers a bag, so it reads
 * where the order of the matches is unobservable: a walk whose consumer takes
 * every answer as a bag, with no first match selected, and the conditions
 * whose verdict no order changes.  Proposals, firings and collections read in
 * the occurrence order, which decides which branch takes an atom first and
 * what a collection holds.  The engine declines rows it cannot read by ids,
 * which the occurrence cursors then read. */

enum { TF_NATIVE_LEGS = 16u };

/* The last conjunct the walk reads in this phase, plus one. */
static size_t tf_phase_end(const TfFire *f) {
    if (f->propose)
        return f->plan.propose_len;
    if (f->plan.has_all && !f->extension)
        return f->plan.kernel_len;
    return f->plan.len;
}

/* One integer of a test, as the rows of the leg at PATTERN give it: known
 * already (an integer written in the test, or a variable an earlier
 * conjunct bound to one), or the row's element where PATTERN has the
 * variable.  False for anything else. */
static bool tf_pretest_value(TfFire *f, Atom *pattern, Atom *operand,
                             TfPretestValue *value) {
    Atom *now = operand && atom_has_vars(operand) ? tf_apply(f, operand)
                                                  : operand;
    if (!now)
        return false;
    if (now->kind == ATOM_GROUNDED && now->ground.gkind == GV_INT) {
        *value = (TfPretestValue){.known = true, .value = now->ground.ival};
        return true;
    }
    if (now->kind != ATOM_VAR)
        return false;
    for (CettaExprLen i = 0u; i < pattern->expr.len; i++) {
        Atom *element = pattern->expr.elems[i];
        if (element->kind == ATOM_VAR &&
            var_id_eq(element->var_id, now->var_id)) {
            *value = (TfPretestValue){.element = (uint32_t)i};
            return true;
        }
    }
    return false;
}

static bool tf_pretest_operand(TfFire *f, Atom *pattern, Atom *operand,
                               CettaNativeIntOperand as,
                               TfPretestValue value[2]) {
    if (as == CETTA_NATIVE_INT_VALUE)
        return tf_pretest_value(f, pattern, operand, &value[0]);
    return operand && operand->kind == ATOM_EXPR && operand->expr.len == 3u &&
        tf_pretest_value(f, pattern, operand->expr.elems[1], &value[0]) &&
        tf_pretest_value(f, pattern, operand->expr.elems[2], &value[1]);
}

/* The tests the rows of the leg at AT can be put to: the lets right after
 * it, in order, up to the first that is no test of integers the rows give.
 * False when there is none. */
static bool tf_pretest_build(TfFire *f, size_t at, TfPretest *pretest) {
    const TfConjunct *c = &f->plan.items[at];
    pretest->len = 0u;
    if (!f->host->guard_filter || c->joint || c->exact || !c->space ||
        at + 1u >= f->plan.len || f->plan.items[at + 1u].kind != TF_LET)
        return false;
    Atom *pattern = atom_has_vars(c->pattern) ? tf_apply(f, c->pattern)
                                              : c->pattern;
    if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len == 0u ||
        tf_is_comma(pattern) || atom_is_list(pattern) ||
        atom_is_list_rest(pattern) ||
        atom_petta_value_representation(pattern) != PETTA_VALUE_ORDINARY)
        return false;
    pretest->arity = pattern->expr.len;
    size_t end = tf_phase_end(f);
    for (size_t j = at + 1u;
         j < end && pretest->len < CETTA_NATIVE_QUERY_FILTERS &&
         f->plan.items[j].kind == TF_LET;
         j++) {
        const TfConjunct *let = &f->plan.items[j];
        CettaNativeQueryFilter filter;
        TfPretestTest *test = &pretest->tests[pretest->len];
        if (!f->host->guard_filter(f->host->context, f->scratch, let->pattern,
                                   let->expression, &filter) ||
            !tf_pretest_operand(f, pattern, filter.left, filter.left_as,
                                test->value[0]) ||
            !tf_pretest_operand(f, pattern, filter.right, filter.right_as,
                                test->value[1]))
            break;
        test->op = filter.op;
        test->as[0] = filter.left_as;
        test->as[1] = filter.right_as;
        pretest->len++;
    }
    return pretest->len != 0u;
}

/* A pattern the engine reads as a leg: an expression, not a query over
 * parts. */
static bool tf_native_leg_pattern(const Atom *p) {
    return p && p->kind == ATOM_EXPR && p->expr.len > 0u &&
           !match_pattern_is_connective(p);
}

/* Run the engine's collection of TEMPLATE over PATTERNS, each read in its
 * space; true with *items the ground instances, or false, having changed
 * nothing, when it declines or an instance holds a variable. */
static bool tf_native_collect(TfFire *f, Space **spaces, Atom **patterns,
                              size_t count, Atom *template_atom,
                              const CettaNativeQueryFilter *filters,
                              size_t filter_count,
                              Atom **collection, bool *failed) {
    *collection = NULL;
    *failed = false;
    if (f->host->spend || !cetta_native_query_enabled() ||
        count == 0u || !spaces[0] || !spaces[0]->native.universe)
        return false;
    bool one_space = true;
    for (size_t i = 1u; i < count; i++) {
        if (!spaces[i] || spaces[i]->native.universe != spaces[0]->native.universe)
            return false;
        one_space = one_space && spaces[i] == spaces[0];
    }
    CettaNativeQuery query = {
        .source = spaces[0],
        .patterns = patterns,
        .pattern_count = count,
        .sources = one_space ? NULL : spaces,
        .template_atom = template_atom,
        .consumer = CETTA_NATIVE_QUERY_COLLECT,
        /* A joint match a guard refuses is no match. */
        .filters = filter_count ? filters : NULL,
        .filter_count = filter_count,
        .else_empty = filter_count != 0u,
    };
    CettaNativeQueryResult result = {0};
    CettaNativeQueryStatus status =
        cetta_native_query_run(&query, f->scratch, &result);
    if (status == CETTA_NATIVE_QUERY_DECLINED)
        return false;
    if (status != CETTA_NATIVE_QUERY_DONE || !result.collection ||
        result.collection->kind != ATOM_EXPR) {
        *failed = true;
        return true;
    }
    for (CettaExprLen r = 0u; r < result.collection->expr.len; r++)
        if (atom_has_vars(result.collection->expr.elems[r]))
            return false;
    *collection = result.collection;
    return true;
}

/* The run of read and take conjuncts from AT, joined in one query: each joint
 * match of their patterns under the bindings so far, as `match` reads the
 * conjunction.  Lets that follow the run and are tests of integers
 * (the host's guard_filter) are made by the join as it joins, so a joint
 * match they refuse is never built; the plan may hold no other let.
 * *HANDLED false, having read nothing, when it declines. */
static TfStatus tf_native_legs(TfFire *f, size_t at, bool *handled) {
    *handled = false;
    if (!f->bag || (f->plan.has_let && !f->host->guard_filter))
        return TF_CONTINUE;
    size_t end = tf_phase_end(f);
    Space *spaces[TF_NATIVE_LEGS];
    Atom *parts[TF_NATIVE_LEGS + 1u];
    Atom **patterns = parts + 1u;
    size_t k = 0u;
    bool open = false;
    for (size_t j = at; j < end && k < TF_NATIVE_LEGS; j++) {
        const TfConjunct *c = &f->plan.items[j];
        if ((c->kind != TF_READ && c->kind != TF_TAKE) || !c->space ||
            c->exact || atom_has_vars(c->space_ref))
            break;
        Atom *p = atom_has_vars(c->pattern) ? tf_apply(f, c->pattern) : c->pattern;
        if (!tf_native_leg_pattern(p))
            break;
        spaces[k] = c->space;
        patterns[k] = p;
        open = open || atom_has_vars(p);
        k++;
    }
    /* A run of ground patterns is a few exact lookups: the cursors' work. */
    if (k == 0u || !open)
        return TF_CONTINUE;
    /* The lets right after the run that the join can test.  Every let of
     * the plan must be among them: code that is evaluated runs in the walk,
     * in the conjuncts' order, which a join made first would not keep. */
    CettaNativeQueryFilter filters[CETTA_NATIVE_QUERY_FILTERS];
    size_t tested = 0u;
    size_t after = at + k;
    if (f->plan.has_let) {
        size_t lets = 0u;
        for (size_t j = 0u; j < f->plan.len; j++)
            lets += f->plan.items[j].kind == TF_LET ? 1u : 0u;
        while (after < end && tested < CETTA_NATIVE_QUERY_FILTERS &&
               f->plan.items[after].kind == TF_LET) {
            const TfConjunct *c = &f->plan.items[after];
            CettaNativeQueryFilter *filter = &filters[tested];
            if (!f->host->guard_filter(f->host->context, f->scratch,
                                       c->pattern, c->expression, filter))
                break;
            /* Variables bound before the run are their values. */
            filter->left = atom_has_vars(filter->left)
                ? tf_apply(f, filter->left) : filter->left;
            filter->right = atom_has_vars(filter->right)
                ? tf_apply(f, filter->right) : filter->right;
            if (!filter->left || !filter->right)
                break;
            tested++;
            after++;
        }
        if (tested != lets)
            return TF_CONTINUE;
    }
    parts[0] = atom_symbol(f->scratch, "tf:legs");
    Atom *legs = atom_expr(f->scratch, parts, (CettaExprLen)(k + 1u));
    Atom *found = NULL;
    bool failed = false;
    if (!legs || !tf_native_collect(f, spaces, patterns, k, legs, filters,
                                    tested, &found, &failed))
        return TF_CONTINUE;
    *handled = true;
    if (failed)
        return f->effects ? TF_ERROR : TF_FAIL;
    TfStatus status = TF_CONTINUE;
    size_t entries_before = f->entries.len;
    for (CettaExprLen r = 0u; r < found->expr.len && status == TF_CONTINUE; r++) {
        Atom *tuple = found->expr.elems[r];
        uint32_t mark = bindings_builder_save(&f->builder);
        bool bound = tuple->kind == ATOM_EXPR && tuple->expr.len == k + 1u;
        for (size_t i = 0u; bound && i < k; i++) {
            const TfConjunct *c = &f->plan.items[at + i];
            Atom *atom = tuple->expr.elems[i + 1u];
            bound = match_atoms_builder(c->pattern, atom, &f->builder, f->scratch) &&
                tf_entries_push(&f->entries, (TfEntry){
                    c->access, c->space_ref, c->space, atom});
        }
        /* The engine and match agree on ground rows; a disagreement is a
         * failure of the realization, which declines while nothing ran. */
        status = bound ? tf_walk(f, after) : (f->effects ? TF_ERROR : TF_FAIL);
        f->entries.len = entries_before;
        bindings_builder_rollback(&f->builder, mark);
    }
    return status;
}

/* A condition's matches.  ITEMS are the pattern's instances, which are the
 * condition's value.  MET, where it is kept, holds for each the stored
 * atoms the match met, in the instance's shape: the instance itself where
 * they are ground, and the stored atoms where they hold variables, which
 * are what a take of the match removes (tf_after_match). */
typedef struct {
    Atom **items;
    Atom **met;
    size_t len, cap;
    size_t limit;
    const TfConjunct *conjunct;
} TfFound;

static void tf_found_free(TfFound *found) {
    free(found->items);
    free(found->met);
}

/* What the match of instance I met. */
static Atom *tf_found_met(const TfFound *found, size_t i) {
    return found->met ? found->met[i] : found->items[i];
}

static TfStatus tf_each_copy(TfFire *f, Space *space, Atom *atom,
                             size_t limit, size_t *count);

/* A condition's instances in one query: its pattern's, or the tuples
 * (, a1 ... an) of a joint one, LIMIT at most when LIMIT is set.  *HANDLED
 * false, having read nothing, when the engine declines. */
static TfStatus tf_native_condition(TfFire *f, const TfConjunct *c,
                                    TfFound *found, bool *handled) {
    *handled = false;
    /* absence and uniqueness only: a collection's order is its value */
    if (!found->limit || !c->space || !atom_has_vars(c->pattern))
        return TF_CONTINUE;
    Atom *resolved = tf_apply(f, c->pattern);
    if (!resolved || !atom_has_vars(resolved))
        return TF_CONTINUE;
    Space *spaces[TF_NATIVE_LEGS];
    Atom **patterns;
    size_t count;
    if (c->joint) {
        count = (size_t)resolved->expr.len - 1u;
        patterns = resolved->expr.elems + 1u;
    } else {
        count = 1u;
        patterns = &resolved;
    }
    if (count > TF_NATIVE_LEGS)
        return TF_CONTINUE;
    for (size_t i = 0u; i < count; i++) {
        if (!tf_native_leg_pattern(patterns[i]))
            return TF_CONTINUE;
        spaces[i] = c->space;
    }
    Atom *instances = NULL;
    bool failed = false;
    if (!tf_native_collect(f, spaces, patterns, count, resolved, NULL, 0u,
                           &instances, &failed))
        return TF_CONTINUE;
    if (failed) {
        *handled = true;
        return f->effects ? TF_ERROR : TF_FAIL;
    }
    size_t len = instances->expr.len;
    if (found->limit && len > found->limit)
        len = found->limit;
    /* The engine answers instances.  A take's entry is the stored atom its
     * match met, which is the instance where the instance is itself stored:
     * the one match of a unique is then with that copy.  Where it is not,
     * the match met an atom with variables, which the cursors read. */
    for (size_t i = 0u; c->access == TF_TAKE && len == 1u && i < count; i++) {
        Atom *part = c->joint ? instances->expr.elems[0]->expr.elems[i + 1u]
                              : instances->expr.elems[0];
        size_t copies = 0u;
        TfStatus status = tf_each_copy(f, c->space, part, 1u, &copies);
        if (status != TF_CONTINUE)
            return status;
        if (copies == 0u)
            return TF_CONTINUE;
    }
    *handled = true;
    if (failed)
        return f->effects ? TF_ERROR : TF_FAIL;
    if (len) {
        Atom **items = cetta_realloc(found->items, len * sizeof(*items));
        if (!items)
            return f->effects ? TF_ERROR : TF_FAIL;
        found->items = items;
        found->cap = len;
        memcpy(items, instances->expr.elems, len * sizeof(*items));
    }
    found->len = len;
    return TF_CONTINUE;
}
static bool tf_stored(TfFire *f, const TfEntry *entry, size_t limit,
                      size_t *out);

typedef struct {
    size_t at;
    const TfConjunct *conjunct;
} TfStep;

/* The entry of a read or take is the stored atom its match met: the pattern
 * under its bindings for a ground atom, and the atom itself for one with
 * variables, which the pattern may have bound. */
static TfStatus tf_after_match(TfFire *f, void *data) {
    TfStep *step = data;
    Atom *matched = !step->conjunct->joint && f->stored
        ? f->stored : tf_apply(f, step->conjunct->pattern);
    if (!matched ||
        !tf_entries_push(&f->entries, (TfEntry){
            step->conjunct->access, step->conjunct->space_ref,
            step->conjunct->space, matched}))
        return f->effects ? TF_ERROR : TF_FAIL;
    TfStatus status = tf_walk(f, step->at + 1u);
    f->entries.len--;
    return status;
}

static TfStatus tf_collect(TfFire *f, void *data) {
    TfFound *found = data;
    if (found->len == found->cap) {
        size_t cap = found->cap ? found->cap * 2u : 8u;
        Atom **items = cetta_realloc(found->items, cap * sizeof(*items));
        if (!items)
            return f->effects ? TF_ERROR : TF_FAIL;
        found->items = items;
        Atom **met = cetta_realloc(found->met, cap * sizeof(*met));
        if (!met)
            return f->effects ? TF_ERROR : TF_FAIL;
        found->met = met;
        found->cap = cap;
    }
    const TfConjunct *c = found->conjunct;
    Atom *instance = tf_apply(f, c->pattern);
    if (!instance)
        return f->effects ? TF_ERROR : TF_FAIL;
    Atom *met = instance;
    if (!c->joint) {
        if (f->stored)
            met = f->stored;
    } else {
        bool open = false;
        for (size_t i = 0u; i < f->met_len; i++)
            open = open || f->met_parts[i] != NULL;
        if (open) {
            /* The tuple of what the parts met, under the instance's head. */
            Atom **parts = arena_alloc(f->scratch,
                                       instance->expr.len * sizeof(*parts));
            if (!parts)
                return f->effects ? TF_ERROR : TF_FAIL;
            parts[0] = instance->expr.elems[0];
            for (CettaExprLen i = 1u; i < instance->expr.len; i++)
                parts[i] = (size_t)(i - 1u) < f->met_len &&
                           f->met_parts[i - 1u]
                    ? f->met_parts[i - 1u] : instance->expr.elems[i];
            met = atom_expr(f->scratch, parts, instance->expr.len);
            if (!met)
                return f->effects ? TF_ERROR : TF_FAIL;
        }
    }
    found->items[found->len] = instance;
    found->met[found->len++] = met;
    return found->limit && found->len >= found->limit ? TF_STOP : TF_CONTINUE;
}

static TfStatus tf_found_status(TfStatus status) {
    return status == TF_STOP ? TF_CONTINUE : status;
}

/* A condition's instances, LIMIT at most when LIMIT is set: by the native
 * query engine, or by the occurrence cursors where it declines. */
static TfStatus tf_condition(TfFire *f, const TfConjunct *c, TfFound *found) {
    bool handled = false;
    TfStatus status = tf_native_condition(f, c, found, &handled);
    if (handled)
        return status;
    return tf_found_status(tf_each_query(f, c, tf_collect, found));
}

static TfStatus tf_record_proposal(TfFire *f, Atom *value) {
    if (!value)
        return f->effects ? TF_ERROR : TF_FAIL;
    if (f->proposal_len == f->proposal_cap) {
        size_t cap = f->proposal_cap ? f->proposal_cap * 2u : 16u;
        Atom **items = cetta_realloc(f->proposals, cap * sizeof(*items));
        if (!items)
            return f->effects ? TF_ERROR : TF_FAIL;
        f->proposals = items;
        f->proposal_cap = cap;
    }
    f->proposals[f->proposal_len++] = value;
    return TF_CONTINUE;
}

/* Entry errors participate in HE's ordinary let/superpose selection. In a
 * firing, its first error stops that attempt, preserving earlier effects. */
static TfStatus tf_semantic_error(TfFire *f, Atom *error) {
    if (f->host->dialect == CETTA_TRANSFORM_DIALECT_HE && f->propose)
        return tf_record_proposal(f, atom_deep_copy(f->answer_arena, error));
    f->error = error;
    return TF_ERROR;
}

static Atom *tf_proposal_atom(TfFire *f) {
    Arena *a = f->answer_arena;
    size_t n = f->entries.len;
    Atom **guide = arena_alloc(a, sizeof(Atom *) * (n ? n : 1u));
    if (!guide)
        return NULL;
    Atom *read = atom_symbol(a, "read"), *take = atom_symbol(a, "take");
    /* The transform's own space is named, not held: the branch that fires
     * a guide is given the space again (tf:fire), and a guide of names and
     * data is one stored term however many branches carry it, where a guide
     * holding a space's value is copied for each of them.  The HE library
     * may read a guide itself, so it keeps the space written out. */
    Atom *own = f->host->dialect == CETTA_TRANSFORM_DIALECT_PETTA
        ? f->plan.default_space : NULL;
    Atom *here = NULL;
    for (size_t i = 0u; i < n; i++) {
        const TfEntry *e = &f->entries.items[i];
        Atom *space_ref = NULL;
        if (own && (e->space_ref == own || atom_eq(e->space_ref, own))) {
            if (!here)
                here = atom_symbol(a, "tf:here");
            space_ref = here;
        } else {
            space_ref = atom_deep_copy(a, e->space_ref);
        }
        Atom *triple[3] = {e->access == TF_TAKE ? take : read, space_ref,
                           atom_deep_copy(a, e->atom)};
        guide[i] = atom_expr(a, triple, 3u);
        if (!guide[i])
            return NULL;
    }
    Atom *candidate[2] = {atom_symbol(a, "tf:cand"),
                       atom_expr(a, guide, (CettaExprLen)n)};
    return candidate[1] ? atom_expr(a, candidate, 2u) : NULL;
}

/* A kernel match in the propose phase: (tf:cand GUIDE), GUIDE the atoms its
 * reads, takes and uniques matched as (MODE SPACE ATOM), in conjunct order,
 * SPACE being `tf:here` for the transform's own space in the PeTTa
 * dialect. */
static TfStatus tf_propose(TfFire *f) {
    return tf_record_proposal(f, tf_proposal_atom(f));
}

/* ── Demand-driven entry proposals ─────────────────────────────────────── */

typedef struct {
    SpaceOccurrenceCursor unstarted;
    SpaceOccurrenceCursor active;
    ArenaMark arena_origin;
    ArenaMark candidate_origin;
    uint32_t binding_origin;
    uint32_t candidate_binding;
    size_t entries_origin;
    bool active_owned;
    bool entered;
} TfCursorLevel;

struct CettaTransformCandidateCursor {
    TfFire fire;
    Arena scratch;
    CettaFrameIdentityScope scope;
    TfCursorLevel *levels;
    size_t depth;
    bool finished;
    bool empty_pending;
    bool opened;
};

static void tf_cursor_note_bytes(CettaTransformCandidateCursor *cursor) {
    size_t count = cursor->fire.plan.len;
    uint64_t owned = (uint64_t)(sizeof(*cursor) +
        count * sizeof(*cursor->levels) +
        cursor->fire.plan.cap * sizeof(*cursor->fire.plan.items) +
        cursor->fire.entries.cap * sizeof(*cursor->fire.entries.items) +
        bindings_builder_owned_vector_bytes(&cursor->fire.builder) +
        (cursor->scope.identities != cursor->scope.inline_identities
            ? cursor->scope.cap * sizeof(*cursor->scope.identities) : 0u) +
        arena_accounted_live_bytes(&cursor->scratch));
    uint64_t retained = 0u;
    for (size_t i = 0u; i < count; i++) {
        TfCursorLevel *level = &cursor->levels[i];
        size_t unstarted = space_occurrence_cursor_accounted_bytes(
            &level->unstarted, false);
        owned += unstarted;
        if (level->active_owned)
            owned += space_occurrence_cursor_accounted_bytes(&level->active, false);
        bool first_view = true;
        for (size_t j = 0u; j < i; j++)
            if (cursor->levels[j].unstarted.occurrences ==
                level->unstarted.occurrences)
                first_view = false;
        if (first_view && level->unstarted.occurrences)
            retained += space_occurrence_cursor_accounted_bytes(
                &level->unstarted, true) - unstarted;
    }
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_OWNED_BYTES_PEAK, owned);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_ACCOUNTED_BYTES_PEAK,
        owned + retained);
}

static void tf_cursor_release_level(TfCursorLevel *level) {
    if (level->active_owned)
        space_occurrence_cursor_release(&level->active);
    level->active_owned = false;
    level->entered = false;
}

bool cetta_transform_candidate_cursor_open(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    CettaTransformCandidateCursor **cursor_out) {
    (void)program;
    if (cursor_out)
        *cursor_out = NULL;
    if (!cursor_out || !a || !call || call->kind != ATOM_EXPR ||
        call->expr.len != 4u || !host || !host->resolve_space ||
        !host->remove_atom || !host->add_atom || !host->evaluate)
        return false;
    CettaTransformCandidateCursor *cursor = cetta_malloc(sizeof(*cursor));
    if (!cursor)
        return false;
    *cursor = (CettaTransformCandidateCursor){0};
    arena_init(&cursor->scratch);
    cursor->fire = (TfFire){
        .host = host,
        .scratch = &cursor->scratch,
        .answer_arena = a,
        .template_atom = call->expr.elems[3],
        .scope = &cursor->scope,
        .propose = true,
    };
    TfPlanner planner = {host, call->expr.elems[1]};
    Atom *conjuncts = call->expr.elems[2];
    if (host->dialect == CETTA_TRANSFORM_DIALECT_HE &&
        conjuncts->kind == ATOM_EXPR && conjuncts->expr.len == 2u &&
        atom_is_symbol_id(conjuncts->expr.elems[0],
                          atom_symbol_literal_id("tf:cs")))
        conjuncts = conjuncts->expr.elems[1];
    bool ok = conjuncts->kind == ATOM_EXPR &&
        tf_plan_list(&planner, &cursor->fire.plan, conjuncts->expr.elems,
                     conjuncts->expr.len) &&
        !cursor->fire.plan.has_bound && !cursor->fire.plan.has_all &&
        !cursor->fire.plan.has_let &&
        bindings_builder_init(&cursor->fire.builder, NULL);
    size_t count = cursor->fire.plan.len;
    if (ok && count > 0u) {
        cursor->levels = cetta_malloc(count * sizeof(*cursor->levels));
        ok = cursor->levels != NULL;
    }
    if (ok && cursor->levels)
        memset(cursor->levels, 0, count * sizeof(*cursor->levels));
    for (size_t i = 0u; ok && i < count; i++) {
        TfConjunct *c = &cursor->fire.plan.items[i];
        if ((c->kind != TF_READ && c->kind != TF_TAKE) || c->joint ||
            atom_has_vars(c->space_ref)) {
            ok = false;
            break;
        }
        c->space = NULL;
        if (!host->resolve_space(host->context, c->space_ref, &c->space)) {
            ok = false;
            break;
        }
        space_occurrence_cursor_init_empty(&cursor->levels[i].unstarted);
        space_occurrence_cursor_init_empty(&cursor->levels[i].active);
        /* Capture every level's frontier before the first publication.  The
         * unstarted cursor is never advanced; each entry clones its pinned
         * rows and selection state, including after the space detaches. */
        if (c->space && !space_occurrence_cursor_init(
                c->space, c->pattern, &cursor->levels[i].unstarted)) {
            ok = false;
            break;
        }
    }
    if (!ok) {
        cetta_transform_candidate_cursor_close(cursor);
        return false;
    }
    cursor->fire.plan.propose_len = count;
    cursor->empty_pending = count == 0u;
    *cursor_out = cursor;
    cursor->opened = true;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_OPEN, 1u);
    if (transform_trace())
        fprintf(stderr, "[transform] candidate cursor opened (%zu levels)\n", count);
    tf_cursor_note_bytes(cursor);
    return true;
}

static TfStatus tf_cursor_backtrack(CettaTransformCandidateCursor *cursor,
                                    size_t at) {
    TfFire *f = &cursor->fire;
    TfCursorLevel *level = &cursor->levels[at];
    /* Rollback releases scratch syntax and binding history.  Preserve their
     * high-water mark before a failed match or successful yield can erase it. */
    tf_cursor_note_bytes(cursor);
    bindings_builder_rollback(&f->builder, level->candidate_binding);
    arena_reset(&cursor->scratch, level->candidate_origin);
    f->entries.len = level->entries_origin;
    return TF_CONTINUE;
}

CettaTransformOutcome cetta_transform_candidate_cursor_next(
    CettaTransformCandidateCursor *cursor, Atom **proposal_out) {
    if (proposal_out)
        *proposal_out = NULL;
    if (!cursor || !proposal_out)
        return CETTA_TRANSFORM_DECLINED;
    if (cursor->finished)
        return CETTA_TRANSFORM_NONE;
    TfFire *f = &cursor->fire;
    if (f->interrupted)
        return CETTA_TRANSFORM_INTERRUPTED;
    size_t count = f->plan.len;
    if (cursor->empty_pending) {
        cursor->empty_pending = false;
        cursor->finished = true;
        if (!tf_spend(f))
            return CETTA_TRANSFORM_INTERRUPTED;
        *proposal_out = tf_proposal_atom(f);
        if (!*proposal_out)
            return CETTA_TRANSFORM_DECLINED;
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_CANDIDATE, 1u);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_PROPOSAL_ALLOC, 2u);
        return CETTA_TRANSFORM_FIRED;
    }
    TfStatus failure = TF_CONTINUE;
    for (;;) {
        if (cursor->depth == count) {
            if (!tf_spend(f)) {
                failure = TF_INTERRUPTED;
                break;
            }
            *proposal_out = tf_proposal_atom(f);
            if (!*proposal_out) {
                failure = TF_FAIL;
                break;
            }
            size_t n = f->entries.len;
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_CANDIDATE, 1u);
            cetta_runtime_stats_add(
                CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_PROPOSAL_ALLOC,
                (uint64_t)(2u + 4u * n));
            if (transform_trace()) {
                fprintf(stderr, "[transform] candidate cursor proposal ");
                atom_print(*proposal_out, stderr);
                fprintf(stderr, "\n");
            }
            tf_cursor_backtrack(cursor, count - 1u);
            cursor->depth--;
            return CETTA_TRANSFORM_FIRED;
        }
        size_t at = cursor->depth;
        TfCursorLevel *level = &cursor->levels[at];
        TfConjunct *c = &f->plan.items[at];
        if (!level->entered) {
            if (!tf_spend(f)) {
                failure = TF_INTERRUPTED;
                break;
            }
            level->arena_origin = arena_mark(&cursor->scratch);
            level->binding_origin = bindings_builder_save(&f->builder);
            level->entries_origin = f->entries.len;
            level->candidate_origin = level->arena_origin;
            level->candidate_binding = level->binding_origin;
            level->entered = true;
            if (c->space) {
                if (!space_occurrence_cursor_clone(&level->unstarted,
                                                   &level->active)) {
                    failure = TF_FAIL;
                    break;
                }
                level->active_owned = true;
            }
        }
        if (!tf_spend(f)) {
            failure = TF_INTERRUPTED;
            break;
        }
        Atom *candidate = NULL;
        SpaceOccurrenceCursorStep step = c->space
            ? space_occurrence_cursor_next(&level->active, NULL, &candidate)
            : SPACE_OCCURRENCE_CURSOR_END;
        if (step == SPACE_OCCURRENCE_CURSOR_INVALIDATED) {
            failure = TF_FAIL;
            break;
        }
        if (step == SPACE_OCCURRENCE_CURSOR_END) {
            /* Active selection buffers and this level's scratch high-water mark
             * disappear here even when the whole traversal yields no proposal. */
            tf_cursor_note_bytes(cursor);
            bindings_builder_rollback(&f->builder, level->binding_origin);
            arena_reset(&cursor->scratch, level->arena_origin);
            f->entries.len = level->entries_origin;
            tf_cursor_release_level(level);
            if (at == 0u) {
                cursor->finished = true;
                break;
            }
            cursor->depth--;
            tf_cursor_backtrack(cursor, cursor->depth);
            continue;
        }
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_SCAN_ROW, 1u);
        if (transform_trace()) {
            fprintf(stderr, "[transform] candidate cursor row %zu ", at);
            atom_print(candidate, stderr);
            fprintf(stderr, "\n");
        }
        if (!candidate)
            continue;
        level->candidate_origin = arena_mark(&cursor->scratch);
        level->candidate_binding = bindings_builder_save(&f->builder);
        if (atom_has_vars(candidate)) {
            CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
            if (!cetta_frame_identity_scope_try(f->scope, &epoch)) {
                failure = TF_FAIL;
                break;
            }
            candidate = atom_freshen_epoch(&cursor->scratch, candidate, epoch);
            if (!candidate) {
                failure = TF_FAIL;
                break;
            }
        }
        if (!match_atoms_builder(c->pattern, candidate, &f->builder,
                                 &cursor->scratch)) {
            tf_cursor_backtrack(cursor, at);
            continue;
        }
        if (!tf_entries_push(&f->entries, (TfEntry){
                c->access, c->space_ref, c->space, candidate})) {
            failure = TF_FAIL;
            break;
        }
        cursor->depth++;
    }
    tf_cursor_note_bytes(cursor);
    cursor->finished = true;
    if (transform_trace())
        fprintf(stderr, "[transform] candidate cursor stopped: %d\n", (int)failure);
    if (failure == TF_INTERRUPTED || f->interrupted)
        return CETTA_TRANSFORM_INTERRUPTED;
    if (failure == TF_ERROR || f->effects)
        return CETTA_TRANSFORM_ERROR;
    return failure == TF_FAIL ? CETTA_TRANSFORM_DECLINED
                              : CETTA_TRANSFORM_NONE;
}

void cetta_transform_candidate_cursor_close(
    CettaTransformCandidateCursor *cursor) {
    if (!cursor)
        return;
    if (cursor->opened)
        tf_cursor_note_bytes(cursor);
    if (cursor->levels) {
        for (size_t i = 0u; i < cursor->fire.plan.len; i++) {
            tf_cursor_release_level(&cursor->levels[i]);
            space_occurrence_cursor_release(&cursor->levels[i].unstarted);
        }
    }
    free(cursor->levels);
    free(cursor->fire.entries.items);
    free(cursor->fire.plan.items);
    bindings_builder_free(&cursor->fire.builder);
    cetta_frame_identity_scope_clear(&cursor->scope);
    arena_free(&cursor->scratch);
    bool opened = cursor->opened;
    free(cursor);
    if (opened)
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_TRANSFORM_CURSOR_CLOSE, 1u);
}

typedef struct TfAnswers {
    Atom **items;
    size_t len, cap;
} TfAnswers;

static TfStatus tf_finish(TfFire *f) {
    Atom *instance = tf_apply(f, f->template_atom);
    if (!instance)
        return f->effects ? TF_ERROR : TF_FAIL;
    /* The step with reads only: no branch changes the store, so each kernel
     * match fires, answering its instance. */
    if (f->reads_answers) {
        TfAnswers *answers = f->reads_answers;
        if (answers->len == answers->cap) {
            size_t cap = answers->cap ? answers->cap * 2u : 64u;
            Atom **items = cetta_realloc(answers->items, cap * sizeof(*items));
            if (!items)
                return TF_FAIL;
            answers->items = items;
            answers->cap = cap;
        }
        Atom *kept = atom_deep_copy(f->answer_arena, instance);
        if (!kept)
            return TF_FAIL;
        /* tf:split: a comma template gives its parts */
        if (!tf_is_comma(f->template_atom)) {
            answers->items[answers->len++] = kept;
            return TF_CONTINUE;
        }
        for (CettaExprLen k = 1u; k < kept->expr.len; k++) {
            if (answers->len == answers->cap) {
                size_t cap = answers->cap * 2u;
                Atom **items = cetta_realloc(answers->items, cap * sizeof(*items));
                if (!items)
                    return TF_FAIL;
                answers->items = items;
                answers->cap = cap;
            }
            answers->items[answers->len++] = kept->expr.elems[k];
        }
        return TF_CONTINUE;
    }
    if (!f->extension) {
        TfEntries kernel = f->entries;
        TfRow row = {{NULL, 0u, 0u}, instance};
        return tf_candidate(f, &kernel, &row, 1u);
    }
    if (f->row_len == f->row_cap) {
        size_t cap = f->row_cap ? f->row_cap * 2u : 8u;
        TfRow *rows = cetta_realloc(f->rows, cap * sizeof(*rows));
        if (!rows)
            return f->effects ? TF_ERROR : TF_FAIL;
        f->rows = rows;
        f->row_cap = cap;
    }
    size_t own = f->entries.len - f->extension_start;
    TfRow row = {{NULL, 0u, 0u}, instance};
    for (size_t i = 0u; i < own; i++)
        if (!tf_entries_push(&row.entries,
                             f->entries.items[f->extension_start + i]))
            return f->effects ? TF_ERROR : TF_FAIL;
    f->rows[f->row_len++] = row;
    return TF_CONTINUE;
}

static void tf_rows_free(TfRow *rows, size_t len) {
    for (size_t i = 0u; i < len; i++)
        free(rows[i].entries.items);
}

/* The kernel match is complete: every extension of it forms one candidate. */
static TfStatus tf_extend(TfFire *f, size_t at) {
    TfRow *saved_rows = f->rows;
    size_t saved_len = f->row_len, saved_cap = f->row_cap;
    f->rows = NULL;
    f->row_len = f->row_cap = 0u;
    f->extension = true;
    f->extension_start = f->entries.len;
    TfStatus status = tf_walk(f, at);
    f->extension = false;
    if (status == TF_CONTINUE) {
        TfEntries kernel = f->entries;
        status = tf_candidate(f, &kernel, f->rows, f->row_len);
    }
    tf_rows_free(f->rows, f->row_len);
    free(f->rows);
    f->rows = saved_rows;
    f->row_len = saved_len;
    f->row_cap = saved_cap;
    return status;
}

/* How many stored atoms `match` finds for `atom` in the condition's space. */
static TfStatus tf_stored_in(TfFire *f, const TfConjunct *c, Atom *atom,
                             size_t *out) {
    TfEntry entry = {TF_TAKE, c->space_ref, c->space, atom};
    return tf_stored(f, &entry, 0u, out) ? TF_CONTINUE
                                     : (f->effects ? TF_ERROR : TF_FAIL);
}

/* tf:distinct-tuples: the joint matches whose take positions are distinct
 * occurrences, no atom more often in the tuple than it is stored; in order. */
static TfStatus tf_distinct_tuples(TfFire *f, const TfConjunct *c,
                                   TfFound *found) {
    size_t kept = 0u;
    for (size_t t = 0u; t < found->len; t++) {
        Atom *tuple = tf_found_met(found, t);
        bool fits = true;
        for (CettaExprLen i = 1u; fits && i < tuple->expr.len; i++) {
            size_t occurrences = 0u, stored = 0u;
            for (CettaExprLen k = 1u; k < tuple->expr.len; k++)
                occurrences += atom_alpha_eq(tuple->expr.elems[k],
                                             tuple->expr.elems[i]) ? 1u : 0u;
            TfStatus status = tf_stored_in(f, c, tuple->expr.elems[i], &stored);
            if (status != TF_CONTINUE)
                return status;
            fits = occurrences <= stored;
        }
        if (fits) {
            if (found->met)
                found->met[kept] = found->met[t];
            found->items[kept++] = found->items[t];
        }
    }
    found->len = kept;
    return TF_CONTINUE;
}

/* A take-mode joint collect's footprint: the atoms that take part in some
 * joint match (tf:participants, in order of first appearance), each with
 * every stored copy (tf:every-copy-onto). */
static TfStatus tf_participant_copies(TfFire *f, const TfConjunct *c,
                                      const TfFound *found) {
    size_t first = f->entries.len;
    for (size_t t = 0u; t < found->len; t++) {
        Atom *tuple = tf_found_met(found, t);
        for (CettaExprLen i = 1u; i < tuple->expr.len; i++) {
            Atom *a = tuple->expr.elems[i];
            bool seen = false;
            for (size_t e = first; !seen && e < f->entries.len; e++)
                seen = atom_alpha_eq(f->entries.items[e].atom, a);
            if (seen)
                continue;
            size_t stored = 0u;
            TfStatus status = tf_stored_in(f, c, a, &stored);
            if (status != TF_CONTINUE)
                return status;
            for (size_t k = 0u; k < stored; k++)
                if (!tf_entries_push(&f->entries, (TfEntry){
                        TF_TAKE, c->space_ref, c->space, a}))
                    return f->effects ? TF_ERROR : TF_FAIL;
        }
    }
    return TF_CONTINUE;
}

/* The distinct variables of E, in order of first occurrence, pushed onto
 * *VARS (a cetta_malloc'd array). */
static bool tf_collect_vars(Atom *e, Atom ***vars, size_t *len, size_t *cap) {
    if (e->kind == ATOM_VAR) {
        for (size_t i = 0u; i < *len; i++)
            if (var_id_eq((*vars)[i]->var_id, e->var_id))
                return true;
        if (*len == *cap) {
            size_t grown = *cap ? *cap * 2u : 8u;
            Atom **items = cetta_realloc(*vars, grown * sizeof(*items));
            if (!items)
                return false;
            *vars = items;
            *cap = grown;
        }
        (*vars)[(*len)++] = e;
        return true;
    }
    if (e->kind == ATOM_EXPR && atom_has_vars(e))
        for (CettaExprLen i = 0u; i < e->expr.len; i++)
            if (!tf_collect_vars(e->expr.elems[i], vars, len, cap))
                return false;
    return true;
}

/* The tuple of the variables RESOLVED, a let's code under the firing's
 * bindings, still holds: the ones its evaluation may bind. */
static Atom *tf_unbound_tuple(TfFire *f, Atom *resolved) {
    Atom **vars = NULL;
    size_t len = 0u, cap = 0u;
    Atom *tuple = tf_collect_vars(resolved, &vars, &len, &cap)
        ? atom_expr(f->scratch, vars, (CettaExprLen)len) : NULL;
    free(vars);
    return tuple;
}

static TfStatus tf_walk(TfFire *f, size_t at) {
    if (!tf_spend(f))
        return TF_INTERRUPTED;
    if (f->propose && at == f->plan.propose_len)
        return tf_propose(f);
    if (f->plan.has_all && !f->extension && at == f->plan.kernel_len)
        return tf_extend(f, at);
    if (at == f->plan.len)
        return tf_finish(f);
    TfConjunct current = f->plan.items[at];
    const TfConjunct *c = &current;
    if (c->space_ref) {
        current.space_ref = atom_has_vars(c->space_ref)
            ? tf_apply(f, c->space_ref) : c->space_ref;
        if (!current.space_ref)
            return f->effects ? TF_ERROR : TF_FAIL;
        current.space = NULL;
        bool resolved = f->host->resolve_space(f->host->context,
            current.space_ref, &current.space);
        if (!resolved && f->host->dialect == CETTA_TRANSFORM_DIALECT_HE) {
            Atom *parts[4] = {atom_symbol(f->scratch, "match"), current.space_ref,
                c->pattern, f->template_atom};
            return tf_semantic_error(f, atom_error(f->scratch,
                atom_expr(f->scratch, parts, 4u),
                atom_symbol(f->scratch, "match expects a space as the first argument")));
        }
    }
    TfStep step = {at, c};
    switch (c->kind) {
    case TF_READ:
    case TF_TAKE: {
        bool handled = false;
        TfStatus status = tf_native_legs(f, at, &handled);
        if (handled)
            return status;
        TfPretest pretest;
        return tf_each_match(f, c->space, c->pattern, c->exact,
                             tf_pretest_build(f, at, &pretest) ? &pretest
                                                               : NULL,
                             tf_after_match, &step);
    }
    case TF_ABSENT: {
        TfFound found = {.limit = 1u, .conjunct = c};
        TfStatus status = tf_condition(f, c, &found);
        bool any = found.len != 0u;
        tf_found_free(&found);
        if (status != TF_CONTINUE || any)
            return status;
        return tf_walk(f, at + 1u);
    }
    case TF_UNIQUE: {
        /* A joint unique counts tuples; its one tuple's parts are its
         * footprint entries, in order. */
        TfFound found = {.limit = 2u, .conjunct = c};
        TfStatus status = tf_condition(f, c, &found);
        if (status == TF_CONTINUE && found.len == 1u) {
            Atom *one = found.items[0];
            /* Its entries are the stored atoms the one match met. */
            Atom *met = tf_found_met(&found, 0u);
            size_t entries_before = f->entries.len;
            uint32_t mark = bindings_builder_save(&f->builder);
            if (match_atoms_builder(c->pattern, one, &f->builder, f->scratch)) {
                bool pushed = true;
                if (c->joint) {
                    for (CettaExprLen i = 1u; pushed && i < met->expr.len; i++)
                        pushed = tf_entries_push(&f->entries, (TfEntry){
                            c->access, c->space_ref, c->space,
                            met->expr.elems[i]});
                } else {
                    pushed = tf_entries_push(&f->entries, (TfEntry){
                        c->access, c->space_ref, c->space, met});
                }
                status = pushed ? tf_walk(f, at + 1u)
                                : (f->effects ? TF_ERROR : TF_FAIL);
            }
            bindings_builder_rollback(&f->builder, mark);
            f->entries.len = entries_before;
        }
        tf_found_free(&found);
        return status;
    }
    case TF_COLLECT: {
        TfFound found = {.limit = 0u, .conjunct = c};
        TfStatus status = tf_condition(f, c, &found);
        bool joint_take = c->joint && c->access == TF_TAKE;
        if (status == TF_CONTINUE && joint_take)
            status = tf_distinct_tuples(f, c, &found);
        if (status != TF_CONTINUE) {
            tf_found_free(&found);
            return status;
        }
        Atom *collection = atom_expr(f->scratch, found.items,
                                     (CettaExprLen)found.len);
        size_t entries_before = f->entries.len;
        uint32_t mark = bindings_builder_save(&f->builder);
        if (collection &&
            match_atoms_builder(c->collection, collection, &f->builder,
                                f->scratch)) {
            /* A proposal keeps no collected take (tf:collect in the propose
             * phase): the branch takes them when it fires. */
            if (joint_take && !f->propose)
                status = tf_participant_copies(f, c, &found);
            for (size_t i = 0u; c->access == TF_TAKE && !joint_take &&
                                !f->propose &&
                                i < found.len && status == TF_CONTINUE; i++)
                if (!tf_entries_push(&f->entries, (TfEntry){
                        TF_TAKE, c->space_ref, c->space,
                        tf_found_met(&found, i)}))
                    status = f->effects ? TF_ERROR : TF_FAIL;
            if (status == TF_CONTINUE)
                status = tf_walk(f, at + 1u);
        }
        bindings_builder_rollback(&f->builder, mark);
        f->entries.len = entries_before;
        tf_found_free(&found);
        return status;
    }
    case TF_AS: {
        Atom *value = tf_apply(f, c->expression);
        if (!value)
            return f->effects ? TF_ERROR : TF_FAIL;
        uint32_t mark = bindings_builder_save(&f->builder);
        TfStatus status = TF_CONTINUE;
        if (match_atoms_builder(c->pattern, value, &f->builder, f->scratch))
            status = tf_walk(f, at + 1u);
        bindings_builder_rollback(&f->builder, mark);
        return status;
    }
    case TF_LET: {
        Atom **results = NULL;
        size_t result_len = 0u;
        Atom *error = NULL;
        bool evaluated;
        /* PeTTa code holding a variable the firing has not bound may bind
         * it, as the library's (eval $expression) does: each answer then
         * comes back with the values its evaluation gave those variables. */
        Atom *unbound = NULL;
        Atom **givens = NULL;
        if (f->host->dialect == CETTA_TRANSFORM_DIALECT_PETTA &&
            f->host->evaluate_bound && atom_has_vars(c->expression)) {
            Atom *resolved = tf_apply(f, c->expression);
            if (!resolved)
                return f->effects ? TF_ERROR : TF_FAIL;
            if (atom_has_vars(resolved) &&
                !(unbound = tf_unbound_tuple(f, resolved)))
                return f->effects ? TF_ERROR : TF_FAIL;
        }
        if (f->host->evaluate_bound) {
            f->effects = true;
            evaluated = f->host->evaluate_bound(
                f->host->context, f->scratch, c->expression, tf_bindings(f),
                unbound, &results, unbound ? &givens : NULL, &result_len,
                &error);
        } else {
            Atom *code = tf_apply(f, c->expression);
            if (!code)
                return f->effects ? TF_ERROR : TF_FAIL;
            f->effects = true;
            evaluated = f->host->evaluate(f->host->context, f->scratch, code,
                                          &results, &result_len, &error);
        }
        if (!evaluated) {
            free(results);
            free(givens);
            f->error = error;
            return TF_ERROR;
        }
        TfStatus status = TF_CONTINUE;
        bool he = f->host->dialect == CETTA_TRANSFORM_DIALECT_HE;
        bool has_value = false;
        for (size_t i = 0u; he && i < result_len; i++)
            has_value = has_value || !atom_is_error(results[i]);
        for (size_t i = 0u; i < result_len && status == TF_CONTINUE; i++) {
            if (he && atom_is_error(results[i])) {
                if (!has_value)
                    status = tf_semantic_error(f, results[i]);
                continue;
            }
            uint32_t mark = bindings_builder_save(&f->builder);
            if (match_atoms_builder(c->pattern, results[i], &f->builder,
                                    f->scratch) &&
                (!unbound || !givens || !givens[i] ||
                 match_atoms_builder(unbound, givens[i], &f->builder,
                                     f->scratch)))
                status = tf_walk(f, at + 1u);
            bindings_builder_rollback(&f->builder, mark);
        }
        free(results);
        free(givens);
        return status;
    }
    }
    return f->effects ? TF_ERROR : TF_FAIL;
}

/* ── Validation and the commit ─────────────────────────────────────────── */

static bool tf_entry_eq(const TfEntry *left, TfKind access, const TfEntry *right) {
    return left->access == access && atom_alpha_eq(left->space_ref, right->space_ref) &&
           atom_alpha_eq(left->atom, right->atom);
}

typedef struct {
    const TfEntries *kernel;
    const TfRow *rows;
    size_t row_len;
} TfFootprint;

static size_t tf_footprint_len(const TfFootprint *fp) {
    size_t len = fp->kernel->len;
    for (size_t r = 0u; r < fp->row_len; r++)
        len += fp->rows[r].entries.len;
    return len;
}

/* A hash of an atom that alpha-equivalent atoms share: variables hash alike. */
static uint64_t tf_hash(const Atom *a) {
    uint64_t h;
    switch (a->kind) {
    case ATOM_SYMBOL:
        return 0x9e3779b97f4a7c15ull ^ ((uint64_t)a->sym_id * 0xff51afd7ed558ccdull);
    case ATOM_VAR:
        return 0xc4ceb9fe1a85ec53ull;
    case ATOM_EXPR:
        h = 0x2545f4914f6cdd1dull ^ a->expr.len;
        for (CettaExprLen i = 0u; i < a->expr.len; i++)
            h = (h ^ tf_hash(a->expr.elems[i])) * 0x100000001b3ull;
        return h;
    case ATOM_GROUNDED:
        switch (a->ground.gkind) {
        case GV_INT:
            return 0x6a09e667f3bcc909ull ^ ((uint64_t)a->ground.ival * 0x9e3779b97f4a7c15ull);
        case GV_BOOL:
            return 0xbb67ae8584caa73bull ^ (uint64_t)a->ground.bval;
        case GV_STRING: {
            h = 0x3c6ef372fe94f82bull;
            for (const char *c = a->ground.sval; c && *c; c++)
                h = (h ^ (unsigned char)*c) * 0x100000001b3ull;
            return h;
        }
        default:
            return 0xa54ff53a5f1d36f1ull ^ (uint64_t)a->ground.gkind;
        }
    default:
        return 0x510e527fade682d1ull;
    }
}

/* The footprint grouped by equal entries, in the order each first occurs:
 * tf:count is a group's size, and tf:apply-takes visits the take groups in
 * this order. */
typedef struct {
    const TfEntry *entry; /* the first occurrence */
    uint64_t hash;
    size_t count;
    size_t reads;         /* take groups: read entries of the same atom */
    size_t stored;
    void *prepared;       /* first take only: retained live selection */
} TfGroup;

typedef struct {
    TfGroup *groups;
    size_t len;
    size_t *slots;        /* open addressing over group indices, +1 */
    size_t mask;
} TfGroups;

static uint64_t tf_entry_hash(TfKind access, const TfEntry *e) {
    return (tf_hash(e->space_ref) * 31u + tf_hash(e->atom)) * 2u + (uint64_t)access;
}

static TfGroup *tf_group_find(TfGroups *g, TfKind access, const TfEntry *e,
                              uint64_t hash, size_t **slot_out) {
    size_t at = (size_t)hash & g->mask;
    for (;;) {
        size_t index = g->slots[at];
        if (index == 0u) {
            *slot_out = &g->slots[at];
            return NULL;
        }
        TfGroup *group = &g->groups[index - 1u];
        if (group->hash == hash && tf_entry_eq(group->entry, access, e))
            return group;
        at = (at + 1u) & g->mask;
    }
}

static bool tf_groups_build(const TfFootprint *fp, TfGroups *g) {
    size_t len = tf_footprint_len(fp);
    size_t cap = 16u;
    while (cap < 2u * len + 2u)
        cap <<= 1u;
    *g = (TfGroups){0};
    g->groups = cetta_malloc(sizeof(TfGroup) * (len ? len : 1u));
    g->slots = calloc(cap, sizeof(size_t));
    g->mask = cap - 1u;
    if (!g->groups || !g->slots)
        return false;
    const TfEntries *parts_kernel = fp->kernel;
    for (size_t part = 0u; part <= fp->row_len; part++) {
        const TfEntries *entries = part == 0u ? parts_kernel : &fp->rows[part - 1u].entries;
        for (size_t i = 0u; i < entries->len; i++) {
            const TfEntry *e = &entries->items[i];
            uint64_t hash = tf_entry_hash(e->access, e);
            size_t *slot = NULL;
            TfGroup *group = tf_group_find(g, e->access, e, hash, &slot);
            if (group) {
                group->count++;
                continue;
            }
            g->groups[g->len] = (TfGroup){
                .entry = e, .hash = hash, .count = 1u,
            };
            *slot = ++g->len;
        }
    }
    for (size_t i = 0u; i < g->len; i++) {
        TfGroup *take = &g->groups[i];
        if (take->entry->access != TF_TAKE)
            continue;
        size_t *slot = NULL;
        TfGroup *read = tf_group_find(g, TF_READ, take->entry,
                                      tf_entry_hash(TF_READ, take->entry), &slot);
        take->reads = read ? read->count : 0u;
    }
    return true;
}

static void tf_groups_free(TfFire *f, TfGroups *g) {
    if (f->host->discard_take) {
        for (size_t i = 0u; i < g->len; i++) {
            if (g->groups[i].prepared) {
                f->host->discard_take(f->host->context, g->groups[i].prepared);
                g->groups[i].prepared = NULL;
            }
        }
    }
    free(g->groups);
    free(g->slots);
}

static TfStatus tf_count_one(TfFire *f, void *data) {
    (void)f;
    (*(size_t *)data)++;
    return TF_CONTINUE;
}

/* Each stored atom of `space` that is `atom`, counted: the same atom or,
 * for an atom with variables, the same but for their names, as a footprint
 * groups its entries (tf_entry_eq).  A stored atom that `atom` only unifies
 * with is another atom.  LIMIT nonzero: no more are counted than LIMIT. */
static TfStatus tf_each_copy(TfFire *f, Space *space, Atom *atom,
                             size_t limit, size_t *count) {
    if (!space)
        return TF_CONTINUE; /* an empty location */
    /* The atom as stored: its variables are its own, whatever the firing's
     * bindings made of them. */
    Atom *resolved = atom;
    Atom *index = resolved
        ? petta_semantics_match_index_pattern(f->scratch, resolved, NULL)
        : NULL;
    if (!index)
        return f->effects ? TF_ERROR : TF_FAIL;
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!space_occurrence_cursor_init(space, index, &cursor))
        return f->effects ? TF_ERROR : TF_FAIL;
    TfStatus status = TF_CONTINUE;
    for (;;) {
        if (!tf_spend(f)) { status = TF_INTERRUPTED; break; }
        Atom *candidate = NULL;
        SpaceOccurrenceCursorStep step =
            space_occurrence_cursor_next(&cursor, NULL, &candidate);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        if (step == SPACE_OCCURRENCE_CURSOR_INVALIDATED) {
            status = f->effects ? TF_ERROR : TF_FAIL;
            break;
        }
        if (candidate && atom_alpha_eq(candidate, resolved) &&
            ++(*count) == limit)
            break;
    }
    space_occurrence_cursor_release(&cursor);
    return status;
}

/* tf:stored: how many copies of the entry's atom are stored.  The PeTTa
 * library counts the stored atoms that are it (tf_each_copy), which is what
 * its takes remove; the HE library counts the atoms `match` finds for it.
 * LIMIT nonzero: a caller that asks only whether LIMIT are stored is
 * answered LIMIT at most, and the space is read no further. */
static bool tf_stored(TfFire *f, const TfEntry *entry, size_t limit,
                      size_t *out) {
    *out = 0u;
    if (f->host->dialect == CETTA_TRANSFORM_DIALECT_HE)
        return tf_each_match(f, entry->space, entry->atom, false, NULL,
                             tf_count_one, out) == TF_CONTINUE;
    return tf_each_copy(f, entry->space, entry->atom, limit, out) ==
           TF_CONTINUE;
}

/* tf:valid: each taken atom is stored with the multiplicity the firing takes,
 * plus one more copy when the firing also reads it. */
static TfStatus tf_valid(TfFire *f, TfGroups *g, bool *valid) {
    *valid = true;
    bool first_take = true;
    for (size_t i = 0u; i < g->len && *valid; i++) {
        TfGroup *take = &g->groups[i];
        if (take->entry->access != TF_TAKE)
            continue;
        if (first_take && f->host->prepare_take && f->host->commit_take &&
            f->host->discard_take) {
            bool prepared = f->host->prepare_take(
                f->host->context, take->entry->space_ref, take->entry->atom,
                take->count, &take->prepared, &take->stored);
            if (!prepared)
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_TRANSFORM_TAKE_PREPARATION_DECLINED);
        }
        first_take = false;
        size_t needed = take->count + (take->reads > 0u ? 1u : 0u);
        if (!take->prepared &&
            !tf_stored(f, take->entry, needed, &take->stored))
            return f->effects ? TF_ERROR : TF_FAIL;
        *valid = take->stored >= needed;
    }
    return TF_CONTINUE;
}

/* tf:apply-takes: each distinct taken atom, in footprint order.  The PeTTa
 * library takes the copies the firing takes, stored atoms that are the taken
 * atom, and no atom it only unifies with (the host's take_atom); the HE
 * library removes the copies it takes, one by one, and goes on from an error
 * a removal answers, which it binds as a value. */
static TfStatus tf_commit(TfFire *f, const TfFootprint *fp, TfGroups *g) {
    if (!tf_spend(f))
        return TF_INTERRUPTED;
    f->committing = true;
    Atom **instances = arena_alloc(f->answer_arena,
                                   sizeof(Atom *) * (fp->row_len ? fp->row_len : 1u));
    if (!instances)
        return f->effects ? TF_ERROR : TF_FAIL;
    /* Everything the commit answers is copied before any removal can
     * release the stored syntax it borrows. */
    for (size_t r = 0u; r < fp->row_len; r++)
        instances[r] = atom_deep_copy(f->answer_arena, fp->rows[r].instance);
    Atom **taken = arena_alloc(f->answer_arena, sizeof(Atom *) * (g->len ? g->len : 1u));
    if (!taken)
        return f->effects ? TF_ERROR : TF_FAIL;
    for (size_t i = 0u; i < g->len; i++) {
        if (g->groups[i].entry->access != TF_TAKE)
            continue;
        taken[i] = atom_deep_copy(f->answer_arena, g->groups[i].entry->atom);
    }
    f->effects = true;
    for (size_t i = 0u; i < g->len; i++) {
        TfGroup *take = &g->groups[i];
        if (take->entry->access != TF_TAKE)
            continue;
        TfEntry now = *take->entry;
        now.atom = taken[i];
        if (f->host->dialect == CETTA_TRANSFORM_DIALECT_HE) {
            for (size_t k = 0u; k < take->count; k++) {
                Atom *error = f->host->remove_atom(f->host->context, now.space_ref,
                                                  now.atom);
                if (error) { f->error = error; return TF_ERROR; }
            }
            continue;
        }
        Atom *error = NULL;
        if (take->prepared) {
            void *prepared = take->prepared;
            take->prepared = NULL; /* commit consumes it, including on error */
            error = f->host->commit_take(f->host->context, prepared);
        } else if (f->host->take_atom) {
            error = f->host->take_atom(f->host->context, now.space_ref,
                                       now.atom, take->count);
        } else {
            return TF_ERROR; /* a host whose programs take supplies take_atom */
        }
        if (error) {
            f->error = error;
            return TF_ERROR;
        }
    }
    Atom *fired = atom_expr(f->answer_arena, instances, (CettaExprLen)fp->row_len);
    Atom *parts[2] = {atom_symbol(f->answer_arena, "tf:fired"), fired};
    f->answer = atom_expr(f->answer_arena, parts, 2u);
    return TF_STOP;
}

/* Validate a candidate and, when it validates, commit it. */
static TfStatus tf_try_commit(TfFire *f, const TfFootprint *fp) {
    TfGroups g;
    if (!tf_groups_build(fp, &g)) {
        tf_groups_free(f, &g);
        return f->effects ? TF_ERROR : TF_FAIL;
    }
    bool valid = false;
    TfStatus status = tf_valid(f, &g, &valid);
    if (status == TF_CONTINUE && valid)
        status = tf_commit(f, fp, &g);
    tf_groups_free(f, &g);
    return status;
}

static bool tf_entries_copy(Arena *arena, const TfEntries *src, TfEntries *dst) {
    *dst = (TfEntries){NULL, 0u, 0u};
    for (size_t i = 0u; i < src->len; i++) {
        TfEntry e = src->items[i];
        e.atom = atom_deep_copy(arena, e.atom);
        if (!tf_entries_push(dst, e))
            return false;
    }
    return true;
}

/* Keep a candidate past its proposal, its syntax copied out of the rows the
 * proposal reads. */
static TfStatus tf_keep_candidate(TfFire *f, TfEntries *kernel, TfRow *rows,
                                  size_t row_len) {
    if (f->candidate_len == f->candidate_cap) {
        size_t cap = f->candidate_cap ? f->candidate_cap * 2u : 8u;
        TfCandidate *items = cetta_realloc(f->candidates, cap * sizeof(*items));
        if (!items)
            return TF_ERROR;
        f->candidates = items;
        f->candidate_cap = cap;
    }
    TfCandidate kept = {0};
    if (!tf_entries_copy(f->scratch, kernel, &kept.kernel))
        return TF_ERROR;
    kept.rows = cetta_malloc(sizeof(TfRow) * (row_len ? row_len : 1u));
    if (!kept.rows)
        return TF_ERROR;
    for (size_t r = 0u; r < row_len; r++) {
        kept.rows[r].instance = atom_deep_copy(f->scratch, rows[r].instance);
        if (!tf_entries_copy(f->scratch, &rows[r].entries, &kept.rows[r].entries))
            return TF_ERROR;
    }
    kept.row_len = row_len;
    f->candidates[f->candidate_len++] = kept;
    return TF_CONTINUE;
}

static TfStatus tf_candidate(TfFire *f, TfEntries *kernel, TfRow *rows,
                             size_t row_len) {
    TfFootprint fp = {kernel, rows, row_len};
    /* Code may have run between proposals: keep the candidate and validate
     * every one after the proposal, as the library does. */
    if (f->plan.has_let)
        return tf_keep_candidate(f, kernel, rows, row_len);
    /* The first candidate that validates stops the proposal; it commits
     * once the proposal has unwound, as the library commits after its
     * search, so no removal meets a reader the proposal still holds. */
    TfGroups g;
    if (!tf_groups_build(&fp, &g)) {
        tf_groups_free(f, &g);
        return f->effects ? TF_ERROR : TF_FAIL;
    }
    bool valid = false;
    TfStatus status = tf_valid(f, &g, &valid);
    tf_groups_free(f, &g);
    if (status != TF_CONTINUE || !valid)
        return status;
    status = tf_keep_candidate(f, kernel, rows, row_len);
    return status == TF_CONTINUE ? TF_STOP : status;
}

/* ── The firing ────────────────────────────────────────────────────────── */

CettaTransformOutcome cetta_transform_fire(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out) {
    (void)program;
    *answer_out = NULL;
    /* (tf:fire-into SPACE CONJUNCTS TEMPLATE DESTINATION) adds the instances
     * to DESTINATION after the commit and answers how many. */
    bool into = call && call->kind == ATOM_EXPR && call->expr.len == 5u;
    if (!call || call->kind != ATOM_EXPR ||
        (call->expr.len != 4u && !into) || !host ||
        !host->resolve_space || !host->remove_atom || !host->add_atom ||
        !host->evaluate)
        return CETTA_TRANSFORM_DECLINED;
    Arena scratch;
    arena_init(&scratch);
    CETTA_FRAME_IDENTITY_SCOPE(scope);
    TfFire f = {
        .host = host,
        .scratch = &scratch,
        .answer_arena = a,
        .template_atom = call->expr.elems[3],
        .scope = &scope,
    };
    TfPlanner planner = {host, call->expr.elems[1]};
    CettaTransformOutcome outcome = CETTA_TRANSFORM_DECLINED;
    Atom *conjuncts = call->expr.elems[2];
    if (host->dialect == CETTA_TRANSFORM_DIALECT_HE &&
        conjuncts->kind == ATOM_EXPR && conjuncts->expr.len == 2u &&
        atom_is_symbol_id(conjuncts->expr.elems[0],
                          atom_symbol_literal_id("tf:cs")))
        conjuncts = conjuncts->expr.elems[1];
    if (conjuncts->kind != ATOM_EXPR ||
        !tf_plan_list(&planner, &f.plan, conjuncts->expr.elems,
                      conjuncts->expr.len) ||
        !bindings_builder_init(&f.builder, NULL)) {
        if (transform_trace()) {
            fprintf(stderr, "[transform] declined ");
            atom_print(call, stderr);
            fprintf(stderr, "\n");
        }
        free(f.plan.items);
        arena_free(&scratch);
        return outcome;
    }
    TfStatus status = tf_walk(&f, 0u);
    /* With a let, every kept candidate in turn; without one, the candidate
     * that stopped the proposal, which validates again over the same store. */
    if (f.plan.has_let ? status == TF_CONTINUE : status == TF_STOP) {
        status = TF_CONTINUE;
        for (size_t i = 0u; i < f.candidate_len && status == TF_CONTINUE; i++) {
            TfFootprint fp = {&f.candidates[i].kernel, f.candidates[i].rows,
                              f.candidates[i].row_len};
            status = tf_try_commit(&f, &fp);
        }
    }
    if (status == TF_STOP && into) {
        /* tf:split, then tf:add-all: a comma template gives its parts; the
         * template's own syntax decides, never an instantiated value. */
        Atom *fired = f.answer->expr.elems[1];
        Atom *destination = call->expr.elems[4];
        bool comma = tf_is_comma(f.template_atom);
        int64_t added = 0;
        for (CettaExprLen r = 0u; r < fired->expr.len && status == TF_STOP; r++) {
            Atom *instance = fired->expr.elems[r];
            CettaExprLen first = comma ? 1u : 0u;
            CettaExprLen last = comma ? instance->expr.len : 1u;
            for (CettaExprLen k = first; k < last && status == TF_STOP; k++) {
                Atom *part = comma ? instance->expr.elems[k] : instance;
                Atom *error = host->add_atom(host->context, destination, part);
                if (error) {
                    f.error = error;
                    status = TF_ERROR;
                } else {
                    added++;
                }
            }
        }
        if (status == TF_STOP)
            f.answer = atom_int(a, added);
    }
    if (f.interrupted)
        status = TF_INTERRUPTED;
    switch (status) {
    case TF_INTERRUPTED:
        outcome = CETTA_TRANSFORM_INTERRUPTED;
        break;
    case TF_STOP:
        *answer_out = f.answer;
        outcome = CETTA_TRANSFORM_FIRED;
        break;
    case TF_CONTINUE:
        outcome = CETTA_TRANSFORM_NONE;
        break;
    case TF_FAIL:
        outcome = f.effects ? CETTA_TRANSFORM_ERROR : CETTA_TRANSFORM_DECLINED;
        break;
    case TF_ERROR:
        outcome = CETTA_TRANSFORM_ERROR;
        break;
    }
    if (outcome == CETTA_TRANSFORM_ERROR) {
        *answer_out = f.error ? atom_deep_copy(a, f.error)
            : atom_symbol(a, "TransformRealizationFailed");
    }
    for (size_t i = 0u; i < f.candidate_len; i++) {
        free(f.candidates[i].kernel.items);
        tf_rows_free(f.candidates[i].rows, f.candidates[i].row_len);
        free(f.candidates[i].rows);
    }
    free(f.candidates);
    free(f.entries.items);
    free(f.plan.items);
    bindings_builder_free(&f.builder);
    arena_free(&scratch);
    if (transform_trace()) {
        fprintf(stderr, "[transform] %s ",
                outcome == CETTA_TRANSFORM_FIRED ? "fired" :
                outcome == CETTA_TRANSFORM_NONE ? "none" :
                outcome == CETTA_TRANSFORM_INTERRUPTED ? "interrupted" :
                outcome == CETTA_TRANSFORM_ERROR ? "error" : "declined");
        atom_print(call, stderr);
        fprintf(stderr, "\n");
    }
    return outcome;
}

/* ── Proposals and guided sources ──────────────────────────────────────── */

static CettaTransformOutcome cetta_transform_propose_cursor(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out);

static bool tf_public_cursor_worthwhile(const TfFire *f) {
    const size_t threshold = 64u;
    if (f->plan.has_bound || f->plan.has_all || f->plan.has_let)
        return false;
    size_t product = 1u;
    for (size_t i = 0u; i < f->plan.len; i++) {
        const TfConjunct *c = &f->plan.items[i];
        if ((c->kind != TF_READ && c->kind != TF_TAKE) || c->joint ||
            atom_has_vars(c->space_ref) || !c->space)
            return false;
        size_t frontier = (size_t)space_length64(c->space);
        if (product < threshold) {
            if (frontier == 0u)
                return false;
            if (frontier >= (threshold + product - 1u) / product)
                product = threshold;
            else
                product *= frontier;
        }
    }
    return product >= threshold;
}

static CettaTransformOutcome cetta_transform_propose_impl(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out, bool allow_cursor) {
    (void)program;
    *answer_out = NULL;
    if (!call || call->kind != ATOM_EXPR ||
        call->expr.len != 4u || !host || !host->resolve_space ||
        !host->remove_atom || !host->add_atom || !host->evaluate)
        return CETTA_TRANSFORM_DECLINED;
    Arena scratch;
    arena_init(&scratch);
    CETTA_FRAME_IDENTITY_SCOPE(scope);
    TfFire f = {
        .host = host,
        .scratch = &scratch,
        .answer_arena = a,
        .template_atom = call->expr.elems[3],
        .scope = &scope,
        .propose = true,
    };
    TfPlanner planner = {host, call->expr.elems[1]};
    Atom *conjuncts = call->expr.elems[2];
    if (host->dialect == CETTA_TRANSFORM_DIALECT_HE &&
        conjuncts->kind == ATOM_EXPR && conjuncts->expr.len == 2u &&
        atom_is_symbol_id(conjuncts->expr.elems[0],
                          atom_symbol_literal_id("tf:cs")))
        conjuncts = conjuncts->expr.elems[1];
    CettaTransformOutcome outcome = CETTA_TRANSFORM_DECLINED;
    bool use_cursor = false;
    if (conjuncts->kind == ATOM_EXPR &&
        tf_plan_list(&planner, &f.plan, conjuncts->expr.elems,
                     conjuncts->expr.len) &&
        bindings_builder_init(&f.builder, NULL)) {
        if (!f.plan.has_bound)
            f.plan.propose_len = f.plan.len;
        use_cursor = allow_cursor && tf_public_cursor_worthwhile(&f);
        if (!use_cursor) {
            TfStatus status = tf_walk(&f, 0u);
            if (f.interrupted) {
                outcome = CETTA_TRANSFORM_INTERRUPTED;
            } else if (status == TF_CONTINUE) {
                *answer_out = atom_expr(a, f.proposals, (CettaExprLen)f.proposal_len);
                outcome = *answer_out ? CETTA_TRANSFORM_FIRED
                                      : (f.effects ? CETTA_TRANSFORM_ERROR
                                                   : CETTA_TRANSFORM_DECLINED);
            } else if (status == TF_ERROR || f.effects) {
                outcome = CETTA_TRANSFORM_ERROR;
            }
            if (outcome == CETTA_TRANSFORM_ERROR && !*answer_out)
                *answer_out = f.error ? atom_deep_copy(a, f.error)
                    : atom_symbol(a, "TransformRealizationFailed");
        }
        bindings_builder_free(&f.builder);
    }
    free(f.proposals);
    free(f.entries.items);
    free(f.plan.items);
    arena_free(&scratch);
    if (use_cursor)
        return cetta_transform_propose_cursor(program, a, call, host, answer_out);
    if (transform_trace()) {
        fprintf(stderr, "[transform] proposals %s ",
                outcome == CETTA_TRANSFORM_FIRED ? "made" :
                outcome == CETTA_TRANSFORM_INTERRUPTED ? "interrupted" :
                outcome == CETTA_TRANSFORM_ERROR ? "error" : "declined");
        atom_print(call, stderr);
        fprintf(stderr, "\n");
    }
    return outcome;
}

CettaTransformOutcome cetta_transform_propose_eager(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out) {
    return cetta_transform_propose_impl(program, a, call, host, answer_out, false);
}

/* Public proposals are still a materialized bag.  Large eligible frontiers
 * use the same resumable producer as first demand, with this collector as
 * their all-demand consumer.  Tiny products keep the cheaper eager collector;
 * every other source keeps the established walker. */
static CettaTransformOutcome cetta_transform_propose_cursor(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out) {
    if (answer_out)
        *answer_out = NULL;
    CettaTransformCandidateCursor *cursor = NULL;
    if (!answer_out ||
        !cetta_transform_candidate_cursor_open(program, a, call, host, &cursor))
        return cetta_transform_propose_eager(program, a, call, host, answer_out);
    Atom **items = NULL;
    size_t len = 0u, cap = 0u;
    CettaTransformOutcome outcome = CETTA_TRANSFORM_NONE;
    for (;;) {
        Atom *proposal = NULL;
        outcome = cetta_transform_candidate_cursor_next(cursor, &proposal);
        if (outcome != CETTA_TRANSFORM_FIRED)
            break;
        if (len == cap) {
            size_t next = cap ? cap * 2u : 8u;
            Atom **grown = cetta_realloc(items, next * sizeof(*grown));
            if (!grown) {
                outcome = CETTA_TRANSFORM_DECLINED;
                break;
            }
            items = grown;
            cap = next;
        }
        items[len++] = proposal;
    }
    if (outcome == CETTA_TRANSFORM_DECLINED) {
        /* Proposal production has no logical effect.  An allocation or route
         * refusal may therefore use the general eager walker exactly once. */
        free(items);
        cetta_transform_candidate_cursor_close(cursor);
        return cetta_transform_propose_eager(program, a, call, host, answer_out);
    }
    if (outcome == CETTA_TRANSFORM_NONE) {
        *answer_out = atom_expr(a, items, (CettaExprLen)len);
        outcome = *answer_out ? CETTA_TRANSFORM_FIRED : CETTA_TRANSFORM_DECLINED;
    } else if (outcome == CETTA_TRANSFORM_ERROR) {
        *answer_out = atom_symbol(a, "TransformRealizationFailed");
    }
    free(items);
    cetta_transform_candidate_cursor_close(cursor);
    if (transform_trace()) {
        fprintf(stderr, "[transform] proposals %s ",
                outcome == CETTA_TRANSFORM_FIRED ? "made" :
                outcome == CETTA_TRANSFORM_INTERRUPTED ? "interrupted" :
                outcome == CETTA_TRANSFORM_ERROR ? "error" : "declined");
        atom_print(call, stderr);
        fprintf(stderr, " ; cursor collected %zu\n", len);
    }
    return outcome;
}

CettaTransformOutcome cetta_transform_propose(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out) {
    return cetta_transform_propose_impl(
        program, a, call, host, answer_out, !transform_cursor_reference());
}

/* tf:locate-in: the pattern of a read, take or unique conjunct. */
static Atom *tf_locate_in(Atom *c) {
    TfMode mode = tf_mode(c);
    if (mode == TF_MODE_PLAIN)
        return c;
    if (mode == TF_MODE_UNIQUE)
        return c->expr.len >= 2u ? tf_locate_in(c->expr.elems[1]) : NULL;
    if (c->expr.len == 3u)
        return c->expr.elems[2];
    return c->expr.len >= 2u ? c->expr.elems[1] : NULL;
}

bool cetta_transform_inst(Arena *a, Atom *call, Atom **answer_out) {
    *answer_out = NULL;
    if (!call || call->kind != ATOM_EXPR ||
        call->expr.len != 3u)
        return false;
    Atom *cs = call->expr.elems[1];
    Atom *guide = call->expr.elems[2];
    bool wrapped = cs && cs->kind == ATOM_EXPR && cs->expr.len == 2u &&
        atom_is_symbol_id(cs->expr.elems[0], atom_symbol_literal_id("tf:cs"));
    if (wrapped)
        cs = cs->expr.elems[1];
    if (cs->kind != ATOM_EXPR || guide->kind != ATOM_EXPR)
        return false;
    size_t cap = 2u * (size_t)cs->expr.len + 1u;
    Atom **out = cetta_malloc(cap * sizeof(*out));
    if (!out)
        return false;
    Atom *as = atom_symbol(a, "tf:as");
    size_t n = 0u, g = 0u;
    bool ok = true;
    for (CettaExprLen i = 0u; ok && i < cs->expr.len; i++) {
        Atom *c = cs->expr.elems[i];
        TfMode mode = tf_mode(c);
        if (mode == TF_MODE_ALL || mode == TF_MODE_ONCE) {
            for (CettaExprLen k = i; k < cs->expr.len; k++)
                out[n++] = cs->expr.elems[k];
            break;
        }
        if (mode == TF_MODE_ABSENT || mode == TF_MODE_COLLECT ||
            mode == TF_MODE_LET) {
            out[n++] = c;
            continue;
        }
        Atom *p = tf_locate_in(c);
        if (!p) {
            ok = false;
            break;
        }
        if (mode == TF_MODE_UNIQUE && tf_is_comma(p)) {
            /* a unique joint match: one guide entry per part, bound as the
             * tuple */
            size_t parts = (size_t)p->expr.len - 1u;
            Atom **atoms = arena_alloc(a, sizeof(Atom *) * (parts + 1u));
            if (!atoms || g + parts > guide->expr.len) {
                ok = false;
                break;
            }
            atoms[0] = p->expr.elems[0];
            for (size_t k = 0u; ok && k < parts; k++) {
                Atom *e = guide->expr.elems[g + k];
                ok = e->kind == ATOM_EXPR && e->expr.len == 3u;
                if (ok)
                    atoms[k + 1u] = e->expr.elems[2];
            }
            g += parts;
            Atom *tuple = ok ? atom_expr(a, atoms, (CettaExprLen)(parts + 1u)) : NULL;
            Atom *pinned[3] = {as, p, tuple};
            out[n++] = c;
            out[n++] = tuple ? atom_expr(a, pinned, 3u) : NULL;
            ok = ok && out[n - 1u];
            continue;
        }
        if (g >= guide->expr.len) {
            ok = false;
            break;
        }
        Atom *e = guide->expr.elems[g++];
        if (e->kind != ATOM_EXPR || e->expr.len != 3u) {
            ok = false;
            break;
        }
        Atom *pinned[3] = {as, p, e->expr.elems[2]};
        /* a read or take of the atom itself, then its pattern bound to it; a
         * unique keeps its count, then binds */
        out[n++] = mode == TF_MODE_UNIQUE ? c : e;
        out[n++] = atom_expr(a, pinned, 3u);
        ok = out[n - 1u] != NULL;
    }
    if (ok) {
        *answer_out = atom_expr(a, out, (CettaExprLen)n);
        if (wrapped && *answer_out) {
            Atom *parts[2] = {atom_symbol(a, "tf:cs"), *answer_out};
            *answer_out = atom_expr(a, parts, 2u);
        }
    }
    free(out);
    return *answer_out != NULL;
}

/* ── A whole transform, collected ──────────────────────────────────────── */

typedef struct {
    Atom **items;
    size_t len, cap;
    Atom *invalid_source;
} TfAtoms;

static bool tf_atoms_push(TfAtoms *xs, Atom *x) {
    if (!x)
        return false;
    if (xs->len == xs->cap) {
        size_t cap = xs->cap ? xs->cap * 2u : 16u;
        Atom **items = cetta_realloc(xs->items, cap * sizeof(*items));
        if (!items)
            return false;
        xs->items = items;
        xs->cap = cap;
    }
    xs->items[xs->len++] = x;
    return true;
}

/* tf:conjuncts */
static void tf_conjuncts(Atom *sources, Atom ***items, size_t *len) {
    if (sources->kind == ATOM_EXPR && sources->expr.len == 0u) {
        *items = NULL;
        *len = 0u;
    } else if (tf_is_comma(sources)) {
        *items = sources->expr.elems + 1u;
        *len = (size_t)sources->expr.len - 1u;
    } else {
        *items = NULL;
        *len = 1u;
    }
}

static bool tf_normalize(Arena *a, Atom **cs, size_t len, Atom *one, TfAtoms *out);

/* tf:normal-conjunct, appended to OUT. */
static bool tf_normal_conjunct(Arena *a, Atom *c, TfAtoms *out) {
    if (tf_is_comma(c))
        return tf_normalize(a, c->expr.elems + 1u, (size_t)c->expr.len - 1u,
                            NULL, out);
    TfMode mode = tf_mode(c);
    bool valid_arity = mode == TF_MODE_PLAIN ||
        ((mode == TF_MODE_READ || mode == TF_MODE_TAKE || mode == TF_MODE_ABSENT)
            ? c->expr.len == 2u || c->expr.len == 3u
            : (mode == TF_MODE_ALL || mode == TF_MODE_ONCE || mode == TF_MODE_UNIQUE)
                ? c->expr.len == 2u : c->expr.len == 3u);
    if (!valid_arity) {
        out->invalid_source = c;
        return false;
    }
    if (mode == TF_MODE_READ || mode == TF_MODE_TAKE) {
        Atom *p = tf_locate_in(c);
        if (!p)
            return false;
        if (!tf_is_comma(p))
            return tf_atoms_push(out, c);
        /* tf:accesses: the conjunct's access of each part */
        for (CettaExprLen i = 1u; i < p->expr.len; i++) {
            Atom *access;
            if (c->expr.len == 3u) {
                Atom *parts[3] = {c->expr.elems[0], c->expr.elems[1],
                                  p->expr.elems[i]};
                access = atom_expr(a, parts, 3u);
            } else {
                Atom *parts[2] = {c->expr.elems[0], p->expr.elems[i]};
                access = atom_expr(a, parts, 2u);
            }
            if (!access || !tf_normal_conjunct(a, access, out))
                return false;
        }
        return true;
    }
    if (mode == TF_MODE_ALL || mode == TF_MODE_ONCE) {
        if (c->expr.len < 2u)
            return false;
        Atom **inner;
        size_t inner_len;
        tf_conjuncts(c->expr.elems[1], &inner, &inner_len);
        TfAtoms body = {0};
        bool ok = tf_normalize(a, inner, inner_len, c->expr.elems[1], &body);
        Atom *wrapped = NULL;
        if (ok && body.len == 0u) {
            wrapped = atom_expr(a, NULL, 0u);
        } else if (ok) {
            Atom **parts = arena_alloc(a, sizeof(Atom *) * (body.len + 1u));
            if (parts) {
                parts[0] = atom_symbol(a, ",");
                memcpy(parts + 1u, body.items, body.len * sizeof(Atom *));
                wrapped = atom_expr(a, parts, (CettaExprLen)(body.len + 1u));
            }
        }
        if (!ok)
            out->invalid_source = body.invalid_source;
        free(body.items);
        Atom *made[2] = {c->expr.elems[0], wrapped};
        return wrapped && tf_atoms_push(out, atom_expr(a, made, 2u));
    }
    return tf_atoms_push(out, c);
}

/* tf:normalize of CS (or of the one conjunct ONE when CS is NULL). */
static bool tf_normalize(Arena *a, Atom **cs, size_t len, Atom *one, TfAtoms *out) {
    for (size_t i = 0u; i < len; i++)
        if (!tf_normal_conjunct(a, cs ? cs[i] : one, out))
            return false;
    return true;
}

static bool tf_vars_hold(const TfAtoms *vars, const Atom *v) {
    for (size_t i = 0u; i < vars->len; i++)
        if (var_id_eq(vars->items[i]->var_id, v->var_id))
            return true;
    return false;
}

/* tf:vars */
static bool tf_vars(Atom *e, TfAtoms *vars) {
    if (e->kind == ATOM_VAR)
        return tf_vars_hold(vars, e) || tf_atoms_push(vars, e);
    if (e->kind == ATOM_EXPR && atom_has_vars(e))
        for (CettaExprLen i = 0u; i < e->expr.len; i++)
            if (!tf_vars(e->expr.elems[i], vars))
                return false;
    return true;
}

/* tf:source-vars */
static bool tf_source_vars(Atom **cs, size_t len, TfAtoms *vars) {
    for (size_t i = 0u; i < len; i++) {
        Atom *c = cs[i];
        TfMode mode = tf_mode(c);
        if (mode == TF_MODE_LET) {
            if (c->expr.len != 3u || !tf_vars(c->expr.elems[1], vars))
                return false;
        } else if (mode == TF_MODE_ALL || mode == TF_MODE_ONCE) {
            Atom **inner;
            size_t inner_len;
            tf_conjuncts(c->expr.elems[1], &inner, &inner_len);
            if (!tf_source_vars(inner ? inner : &c->expr.elems[1], inner_len, vars))
                return false;
        } else if (!tf_vars(c, vars)) {
            return false;
        }
    }
    return true;
}

Atom *cetta_transform_bind_code(Arena *a, Atom *parameters,
                                 Atom *values, Atom *body) {
    if (!a || !parameters || !values || !body ||
        parameters->kind != ATOM_EXPR || values->kind != ATOM_EXPR ||
        parameters->expr.len != values->expr.len)
        return NULL;
    if (parameters->expr.len == 0u)
        return body;
    Atom *pair = atom_expr2(a, parameters, body);
    Atom *renamed = pair ? rename_vars_only(a, pair, parameters) : NULL;
    if (!renamed || renamed->kind != ATOM_EXPR || renamed->expr.len != 2u)
        return NULL;
    Atom *formals = renamed->expr.elems[0];
    Atom *result = renamed->expr.elems[1];
    if (formals->kind != ATOM_EXPR || formals->expr.len != parameters->expr.len)
        return NULL;
    if (result == body)
        return body;
    for (CettaExprIndex i = parameters->expr.len; i-- > 0u;) {
        if (formals->expr.elems[i]->kind != ATOM_VAR)
            return NULL;
        Atom *quoted = atom_expr2(a, atom_symbol(a, "quote"), values->expr.elems[i]);
        Atom *parts[] = {atom_symbol(a, "let"), formals->expr.elems[i], quoted, result};
        result = quoted ? atom_expr(a, parts, 4u) : NULL;
        if (!result)
            return NULL;
    }
    return result;
}

/* Source values enter through fresh binding positions, leaving raw data
 * arguments and explicit quote forms unchanged inside the code. */
static Atom *tf_protect(Arena *a, const TfAtoms *vars, Atom *e) {
    if (!atom_has_vars(e) || vars->len == 0u)
        return e;
    Atom *parameters = atom_expr(a, vars->items, (CettaExprLen)vars->len);
    return parameters ? cetta_transform_bind_code(a, parameters, parameters, e) : NULL;
}

static Atom *tf_protect_template(Arena *a, const TfAtoms *vars, Atom *e) {
    if (!tf_is_comma(e))
        return tf_protect(a, vars, e);
    Atom **parts = arena_alloc(a, sizeof(*parts) * (size_t)e->expr.len);
    if (!parts)
        return NULL;
    parts[0] = e->expr.elems[0];
    for (CettaExprLen i = 1u; i < e->expr.len; i++)
        if (!(parts[i] = tf_protect(a, vars, e->expr.elems[i])))
            return NULL;
    return atom_expr(a, parts, e->expr.len);
}

/* tf:protect-lets */
static bool tf_protect_lets(Arena *a, const TfAtoms *vars, Atom **cs, size_t len,
                            TfAtoms *out) {
    for (size_t i = 0u; i < len; i++) {
        Atom *c = cs[i];
        TfMode mode = tf_mode(c);
        if (mode == TF_MODE_LET) {
            Atom *expression = tf_protect(a, vars, c->expr.elems[2]);
            Atom *parts[3] = {c->expr.elems[0], c->expr.elems[1], expression};
            if (!expression || !tf_atoms_push(out, atom_expr(a, parts, 3u)))
                return false;
        } else if (mode == TF_MODE_ALL || mode == TF_MODE_ONCE) {
            Atom **inner;
            size_t inner_len;
            tf_conjuncts(c->expr.elems[1], &inner, &inner_len);
            TfAtoms body = {0};
            bool ok = tf_protect_lets(a, vars, inner ? inner : &c->expr.elems[1],
                                      inner_len, &body);
            Atom **parts = ok ? arena_alloc(a, sizeof(Atom *) * (body.len + 1u)) : NULL;
            Atom *wrapped = NULL;
            if (parts) {
                parts[0] = atom_symbol(a, ",");
                memcpy(parts + 1u, body.items, body.len * sizeof(Atom *));
                wrapped = atom_expr(a, parts, (CettaExprLen)(body.len + 1u));
            }
            free(body.items);
            Atom *made[2] = {c->expr.elems[0], wrapped};
            if (!wrapped || !tf_atoms_push(out, atom_expr(a, made, 2u)))
                return false;
        } else if (!tf_atoms_push(out, c)) {
            return false;
        }
    }
    return true;
}

/* tf:has-let */
static bool tf_has_let(Atom *cs) {
    for (CettaExprLen i = 0u; cs->kind == ATOM_EXPR && i < cs->expr.len; i++) {
        Atom *c = cs->expr.elems[i];
        TfMode mode = tf_mode(c);
        if (mode == TF_MODE_LET)
            return true;
        if ((mode == TF_MODE_ALL || mode == TF_MODE_ONCE) && c->expr.len >= 2u) {
            Atom *inner = c->expr.elems[1];
            if (tf_is_comma(inner) ? tf_has_let(inner)
                                   : tf_mode(inner) == TF_MODE_LET)
                return true;
        }
    }
    return false;
}

static Atom *tf_call3(Arena *a, const char *head, Atom *x, Atom *y, Atom *z) {
    Atom *parts[4] = {atom_symbol(a, head), x, y, z};
    return atom_expr(a, parts, 4u);
}

/* A transform with reads and conditions only, no take, `all` or `once`, and
 * no `let` but tests of integers that follow its reads and that a join can
 * make as it joins (the host's guard_filter): no branch changes the store
 * and no code runs, so every kernel match the tests pass fires on the store
 * at the call and answers its instance, which is match's bag of answers.
 * DECLINED, having read nothing, otherwise. */
static CettaTransformOutcome tf_reads_only_collect(
    const CettaTransformHost *host, Arena *a, Atom *space, Atom *conjuncts,
    Atom *template_atom, CettaStepReads mode, Atom **collection_out,
    uint64_t *count_out) {
    bool bag = mode != CETTA_STEP_READS_ORDERED;
    Arena scratch;
    arena_init(&scratch);
    CETTA_FRAME_IDENTITY_SCOPE(scope);
    TfAnswers answers = {0};
    TfFire f = {
        .host = host,
        .scratch = &scratch,
        .answer_arena = a,
        .template_atom = template_atom,
        .scope = &scope,
        .reads_answers = &answers,
        .bag = bag,
    };
    TfPlanner planner = {host, space};
    CettaTransformOutcome outcome = CETTA_TRANSFORM_DECLINED;
    bool planned = tf_plan_list(&planner, &f.plan, conjuncts->expr.elems,
                                conjuncts->expr.len);
    bool reads = planned && !f.plan.has_bound;
    for (size_t i = 0u; reads && i < f.plan.len; i++)
        reads = f.plan.items[i].access != TF_TAKE &&
                f.plan.items[i].kind != TF_TAKE;
    /* The reads first, then the lets, each a test the join makes. */
    size_t read_len = 0u;
    while (reads && read_len < f.plan.len &&
           f.plan.items[read_len].kind != TF_LET)
        read_len++;
    CettaNativeQueryFilter filters[CETTA_NATIVE_QUERY_FILTERS];
    size_t tested = 0u;
    bool guarded = reads && f.plan.has_let;
    if (guarded) {
        bool tests = host->guard_filter != NULL &&
            f.plan.len - read_len <= CETTA_NATIVE_QUERY_FILTERS;
        for (size_t i = read_len; tests && i < f.plan.len; i++) {
            const TfConjunct *c = &f.plan.items[i];
            tests = c->kind == TF_LET &&
                host->guard_filter(host->context, &scratch, c->pattern,
                                   c->expression, &filters[tested]);
            if (tests)
                tested++;
        }
        /* A let that runs code is no read: the caller proposes and fires. */
        if (!tests)
            reads = false;
    }
    /* Reads alone, every one a leg the engine joins: the engine collects
     * the template's instances directly. */
    bool legs = reads && bag && read_len != 0u &&
        read_len <= TF_NATIVE_LEGS && !tf_is_comma(template_atom);
    Space *spaces[TF_NATIVE_LEGS];
    Atom *patterns[TF_NATIVE_LEGS];
    for (size_t i = 0u; legs && i < read_len; i++) {
        legs = f.plan.items[i].kind == TF_READ && f.plan.items[i].space &&
               tf_native_leg_pattern(f.plan.items[i].pattern);
        if (legs) {
            spaces[i] = f.plan.items[i].space;
            patterns[i] = f.plan.items[i].pattern;
        }
    }
    if (legs) {
        Atom *found = NULL;
        bool failed = false;
        /* The engine builds the answers in the caller's arena. */
        f.scratch = a;
        bool collected = tf_native_collect(&f, spaces, patterns, read_len,
                                           template_atom, filters, tested,
                                           &found, &failed);
        f.scratch = &scratch;
        if (collected && !failed) {
            *collection_out = found;
            *count_out = found->expr.len;
            outcome = *collection_out ? CETTA_TRANSFORM_FIRED
                                      : CETTA_TRANSFORM_DECLINED;
            free(f.plan.items);
            arena_free(&scratch);
            return outcome;
        }
    }
    /* Tests the join declined (a value that is no integer, say) are code
     * for the evaluator: the caller's route, not the walk below. */
    if (reads && !guarded && mode != CETTA_STEP_READS_ENGINE &&
        bindings_builder_init(&f.builder, NULL)) {
        TfStatus status = tf_walk(&f, 0u);
        /* A row holding variables is renamed in this step's frame: an answer
         * holding one would outlive its names, so the caller reads it. */
        for (size_t i = 0u; status == TF_CONTINUE && i < answers.len; i++)
            if (atom_has_vars(answers.items[i]))
                status = TF_FAIL;
        if (f.interrupted) {
            outcome = CETTA_TRANSFORM_INTERRUPTED;
        } else if (status == TF_CONTINUE) {
            *collection_out = atom_expr(a, answers.items, (CettaExprLen)answers.len);
            *count_out = answers.len;
            outcome = *collection_out ? CETTA_TRANSFORM_FIRED
                                      : CETTA_TRANSFORM_DECLINED;
        }
        bindings_builder_free(&f.builder);
    }
    free(answers.items);
    free(f.entries.items);
    free(f.plan.items);
    arena_free(&scratch);
    return outcome;
}

CettaTransformOutcome cetta_transform_reads(
    const CettaTransformHost *host, Arena *a, Atom *space, Atom *pattern,
    Atom *template_atom, CettaStepReads mode, Atom **collection_out,
    uint64_t *count_out) {
    *collection_out = NULL;
    *count_out = 0u;
    if (!host || !space || !pattern ||
        !template_atom)
        return CETTA_TRANSFORM_DECLINED;
    /* match reads its pattern, or each part of a conjunction, as data: a
     * read conjunct each, whatever heads it. */
    bool joint = tf_is_comma(pattern);
    size_t parts = joint ? (size_t)pattern->expr.len - 1u : 1u;
    if (parts == 0u || parts > TF_NATIVE_LEGS)
        return CETTA_TRANSFORM_DECLINED;
    Atom *reads[TF_NATIVE_LEGS];
    Atom *read = atom_symbol(a, "read");
    for (size_t i = 0u; i < parts; i++) {
        Atom *part = joint ? pattern->expr.elems[i + 1u] : pattern;
        if (tf_is_comma(part))
            return CETTA_TRANSFORM_DECLINED;
        reads[i] = atom_expr2(a, read, part);
        if (!reads[i])
            return CETTA_TRANSFORM_DECLINED;
    }
    Atom *conjuncts = atom_expr(a, reads, (CettaExprLen)parts);
    CettaTransformOutcome outcome = conjuncts
        ? tf_reads_only_collect(host, a, space, conjuncts, template_atom, mode,
                                collection_out, count_out)
        : CETTA_TRANSFORM_DECLINED;
    if (transform_trace() && outcome == CETTA_TRANSFORM_FIRED)
        fprintf(stderr, "[step] match reads: %llu answers\n",
                (unsigned long long)*count_out);
    return outcome;
}

bool cetta_transform_prepare(Arena *work, Atom *sources, Atom *template_atom,
                             CettaTransformDialect dialect, bool caller,
                             bool bound_code,
                             Atom **conjuncts_out, Atom **template_out,
                             Atom **error_out) {
    *error_out = NULL;
    *conjuncts_out = NULL;
    *template_out = NULL;
    if (!work || !sources || !template_atom)
        return false;
    Atom **cs;
    size_t cs_len;
    tf_conjuncts(sources, &cs, &cs_len);
    TfAtoms plain = {0}, vars = {0}, protected_cs = {0};
    bool ok = tf_normalize(work, cs, cs_len, sources, &plain);
    Atom **items = plain.items;
    size_t len = plain.len;
    Atom *instance = template_atom;
    if (ok && dialect == CETTA_TRANSFORM_DIALECT_PETTA) {
        ok = tf_source_vars(plain.items, plain.len, &vars);
        /* Bound code reads source values through its environment. Quote
         * syntax inserted into a raw argument would become stored data. */
        if (ok && !bound_code) {
            ok = tf_protect_lets(work, &vars, plain.items, plain.len, &protected_cs);
            items = protected_cs.items;
            len = protected_cs.len;
        }
        if (ok && caller)
            instance = tf_protect_template(work, &vars, template_atom);
    }
    Atom *listed = ok ? atom_expr(work, items, (CettaExprLen)len) : NULL;
    if (listed && dialect == CETTA_TRANSFORM_DIALECT_HE) {
        Atom *parts[2] = {atom_symbol(work, "tf:cs"), listed};
        listed = atom_expr(work, parts, 2u);
    }
    if (!ok && plain.invalid_source && dialect == CETTA_TRANSFORM_DIALECT_HE)
        *error_out = atom_error(work, atom_deep_copy(work, plain.invalid_source),
                                atom_symbol(work, "MalformedTransformSource"));
    free(plain.items);
    free(vars.items);
    free(protected_cs.items);
    if (!listed || !instance)
        return false;
    *conjuncts_out = listed;
    *template_out = instance;
    return true;
}

static Atom *tf_lower_let(Arena *a, Atom *pattern, Atom *value, Atom *body) {
    Atom *parts[4] = {atom_symbol(a, "let"), pattern, value, body};
    return atom_expr(a, parts, 4u);
}

static Atom *tf_lower_call2(Arena *a, const char *head, Atom *x, Atom *y) {
    Atom *parts[3] = {atom_symbol(a, head), x, y};
    return atom_expr(a, parts, 3u);
}

Atom *cetta_transform_lower(Arena *a, Atom *call,
                            const CettaTransformHost *host) {
    if (!a || !call || !host || call->kind != ATOM_EXPR ||
        call->expr.len != 5u ||
        !atom_is_symbol_id(call->expr.elems[0],
                           atom_symbol_literal_id("tf:run")))
        return NULL;
    Arena work;
    arena_init(&work);
    Atom *space = call->expr.elems[1];
    Atom *sources = call->expr.elems[2];
    Atom *template_atom = call->expr.elems[3];
    Atom *destination = call->expr.elems[4];
    bool he = host->dialect == CETTA_TRANSFORM_DIALECT_HE;
    bool caller = atom_is_symbol_id(destination,
                                    atom_symbol_literal_id("tf:caller"));
    bool into = !caller &&
        (he ? destination->kind == ATOM_GROUNDED
            : (destination->kind == ATOM_GROUNDED &&
               destination->ground.gkind == GV_SPACE) ||
              (destination->kind == ATOM_SYMBOL &&
               atom_name_cstr(destination)[0] == '&'));
    Atom *listed = NULL, *instance = NULL;
    Atom *lowered = NULL, *error = NULL;
    if (!cetta_transform_prepare(&work, sources, template_atom, host->dialect,
                                 caller && !into, host->evaluate_bound != NULL,
                                 &listed, &instance, &error)) {
        lowered = error;
        goto done;
    }
    Atom *matches = atom_var_with_id(&work, "tf-matches", fresh_var_id());
    Atom *guide = atom_var_with_id(&work, "tf-guide", fresh_var_id());
    Atom *guided = atom_var_with_id(&work, "tf-guided", fresh_var_id());
    Atom *instances = atom_var_with_id(&work, "tf-instances", fresh_var_id());
    Atom *all = atom_var_with_id(&work, "tf-all", fresh_var_id());
    Atom *item = atom_var_with_id(&work, "tf-instance", fresh_var_id());
    if (!matches || !guide || !guided || !instances || !all || !item)
        goto done;
    Atom *proposal = tf_call3(&work, "tf:propose", space, listed, instance);
    Atom *guided_call = tf_lower_call2(&work, "tf:inst", listed, guide);
    Atom *fire = NULL;
    if (into) {
        Atom *parts[5] = {atom_symbol(&work, "tf:fire-into"), space, guided,
                          instance, destination};
        fire = atom_expr(&work, parts, 5u);
    } else {
        fire = tf_call3(&work, "tf:fire", space, guided, instance);
    }
    if (!proposal || !guided_call || !fire)
        goto done;

    Atom *body = NULL;
    if (into) {
        body = fire;
    } else {
        Atom *boolean = atom_symbol(&work, tf_is_comma(instance)
            ? (he ? "True" : "true") : (he ? "False" : "false"));
        Atom *split = tf_lower_call2(&work, "tf:split", boolean, instances);
        Atom *alternatives = atom_expr2(&work, atom_symbol(&work, "superpose"), all);
        Atom *route = NULL;
        if (caller) {
            route = item;
            if (!he) {
                Atom *parts[2] = {atom_symbol(&work, "eval"), item};
                route = atom_expr(&work, parts, 2u);
            }
        } else {
            Atom *parts[3] = {destination, item};
            Atom *application = atom_expr(&work, parts, 2u);
            if (!he) {
                Atom *reduce_parts[2] = {atom_symbol(&work, "reduce"), application};
                application = atom_expr(&work, reduce_parts, 2u);
            }
            route = application;
        }
        Atom *fired_pattern[2] = {atom_symbol(&work, "tf:fired"), instances};
        body = tf_lower_let(&work, atom_expr(&work, fired_pattern, 2u), fire,
            tf_lower_let(&work, all, split,
                tf_lower_let(&work, item, alternatives, route)));
    }
    Atom *guide_pattern[2] = {atom_symbol(&work, "tf:cand"), guide};
    body = tf_lower_let(&work, guided, guided_call, body);
    body = tf_lower_let(&work, atom_expr(&work, guide_pattern, 2u),
                        atom_expr2(&work, atom_symbol(&work, "superpose"), matches),
                        body);
    lowered = tf_lower_let(&work, matches, proposal, body);
    if (lowered)
        lowered = atom_deep_copy(a, lowered);
done:
    arena_free(&work);
    return lowered;
}

Atom *cetta_transform_split(Arena *a, Atom *call,
                            CettaTransformDialect dialect) {
    if (!a || !call || call->kind != ATOM_EXPR || call->expr.len != 3u)
        return NULL;
    Atom *instances = call->expr.elems[2];
    if (!instances || instances->kind != ATOM_EXPR)
        return NULL;
    bool comma = atom_is_symbol_id(call->expr.elems[1],
        atom_symbol_literal_id(dialect == CETTA_TRANSFORM_DIALECT_HE
                               ? "True" : "true"));
    if (!comma)
        return instances;
    TfAtoms split = {0};
    for (CettaExprLen i = 0u; i < instances->expr.len; i++) {
        Atom *instance = instances->expr.elems[i];
        if (!instance || instance->kind != ATOM_EXPR || instance->expr.len == 0u)
            goto fail;
        for (CettaExprLen k = 1u; k < instance->expr.len; k++)
            if (!tf_atoms_push(&split, instance->expr.elems[k]))
                goto fail;
    }
    Atom *answer = atom_expr(a, split.items, (CettaExprLen)split.len);
    free(split.items);
    return answer;
fail:
    free(split.items);
    return NULL;
}

CettaTransformOutcome cetta_transform_collect(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **collection_out, uint64_t *count_out) {
    *collection_out = NULL;
    *count_out = 0u;
    bool into = call && call->kind == ATOM_EXPR && call->expr.len == 5u;
    if (!call || call->kind != ATOM_EXPR ||
        (call->expr.len != 4u && !into) || !host)
        return CETTA_TRANSFORM_DECLINED;
    if (host->dialect == CETTA_TRANSFORM_DIALECT_HE)
        return CETTA_TRANSFORM_DECLINED;
    Atom *space = call->expr.elems[1];
    Atom *sources = call->expr.elems[2];
    Atom *template_atom = call->expr.elems[3];
    Atom *destination = into ? call->expr.elems[4] : NULL;
    if (into) {
        Space *target = NULL;
        if (!host->resolve_space(host->context, destination, &target) || !target)
            return CETTA_TRANSFORM_DECLINED;
    }
    /* The same preparation the streaming runner uses. */
    Arena work;
    arena_init(&work);
    TfAtoms answers = {0};
    CettaTransformOutcome outcome = CETTA_TRANSFORM_DECLINED;
    Atom *answer = NULL;
    bool fired_any = false;
    Atom *listed = NULL, *prepared_template = NULL;
    if (!cetta_transform_prepare(&work, sources, template_atom, host->dialect,
                                 false, host->evaluate_bound != NULL,
                                 &listed, &prepared_template, &answer)) {
        if (answer) outcome = CETTA_TRANSFORM_ERROR;
        goto done;
    }
    template_atom = prepared_template;
    if (!into) {
        CettaTransformOutcome reads =
            tf_reads_only_collect(host, a, space, listed, template_atom,
                                  CETTA_STEP_READS_BAG, collection_out,
                                  count_out);
        if (reads != CETTA_TRANSFORM_DECLINED) {
            outcome = reads;
            if (transform_trace()) {
                fprintf(stderr, "[transform] fired, reads only: %zu answers ",
                        (size_t)*count_out);
                atom_print(call, stderr);
                fprintf(stderr, "\n");
            }
            arena_free(&work);
            return outcome;
        }
    }
    Atom *propose = tf_call3(&work, "tf:propose", space, listed,
                             template_atom);
    Atom *matches = NULL;
    if (!propose)
        goto done;
    outcome = cetta_transform_propose(program, &work, propose, host, &matches);
    if (outcome != CETTA_TRANSFORM_FIRED) {
        answer = matches;
        goto done;
    }
    bool comma = tf_is_comma(template_atom);
    for (CettaExprLen m = 0u; m < matches->expr.len; m++) {
        Atom *candidate = matches->expr.elems[m];
        Atom *guided_call = atom_expr2(&work, atom_symbol(&work, "tf:inst"), listed);
        Atom *kc = NULL;
        if (candidate->kind != ATOM_EXPR || candidate->expr.len != 2u || !guided_call)
            goto failed;
        Atom *gparts[3] = {guided_call->expr.elems[0], listed, candidate->expr.elems[1]};
        if (!cetta_transform_inst(&work, atom_expr(&work, gparts, 3u), &kc))
            goto failed;
        Atom *fire;
        if (into) {
            Atom *parts[5] = {atom_symbol(&work, "tf:fire-into"), space, kc,
                              template_atom, destination};
            fire = atom_expr(&work, parts, 5u);
        } else {
            fire = tf_call3(&work, "tf:fire", space, kc, template_atom);
        }
        Atom *fired = NULL;
        CettaTransformOutcome branch =
            fire ? cetta_transform_fire(program, &work, fire, host, &fired)
                 : CETTA_TRANSFORM_DECLINED;
        if (branch == CETTA_TRANSFORM_NONE)
            continue;
        if (branch == CETTA_TRANSFORM_ERROR || branch == CETTA_TRANSFORM_INTERRUPTED) {
            outcome = branch;
            answer = fired;
            goto done;
        }
        if (branch != CETTA_TRANSFORM_FIRED || !fired)
            goto failed;
        fired_any = true;
        if (into) {
            if (!tf_atoms_push(&answers, fired))
                goto failed;
            continue;
        }
        /* tf:split: a comma template gives its parts */
        Atom *instances = fired->expr.elems[1];
        for (CettaExprLen r = 0u; r < instances->expr.len; r++) {
            Atom *instance = instances->expr.elems[r];
            CettaExprLen first = comma ? 1u : 0u;
            CettaExprLen last = comma ? instance->expr.len : 1u;
            for (CettaExprLen k = first; k < last; k++)
                if (!tf_atoms_push(&answers, comma ? instance->expr.elems[k]
                                                   : instance))
                    goto failed;
        }
    }
    outcome = CETTA_TRANSFORM_FIRED;
    goto done;
failed:
    /* Nothing is undone: a failure after a branch fired, or after the
     * proposals ran code, is an error. */
    outcome = fired_any || tf_has_let(listed) ? CETTA_TRANSFORM_ERROR
                                              : CETTA_TRANSFORM_DECLINED;
    answer = NULL;
done:
    if (outcome == CETTA_TRANSFORM_FIRED) {
        *count_out = answers.len;
        *collection_out = atom_deep_copy(
            a, atom_expr(&work, answers.items, (CettaExprLen)answers.len));
        if (!*collection_out)
            outcome = CETTA_TRANSFORM_ERROR;
    }
    if (outcome == CETTA_TRANSFORM_ERROR)
        *collection_out = answer ? atom_deep_copy(a, answer)
            : atom_symbol(a, "TransformRealizationFailed");
    free(answers.items);
    arena_free(&work);
    return outcome;
}
