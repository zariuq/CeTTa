/* he_typing.c — he-prime diagnostics, refinements, checked type-level
 * computation, and typed inhabitation search over the shared HE type engine.
 * See he_typing.h for the frame.  Comments here state constraints only. */

#include "he_typing.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>


#include "eval.h"
#include "grounded.h"
#include "match.h"
#include "space.h"
#include "symbol.h"

/* ── result plumbing (three-valued) ────────────────────────────────────── */

static Atom *he_sym(Arena *a, const char *s) { return atom_symbol(a, s); }

static Atom *he_wrap1(Arena *a, const char *tag, Atom *x) {
    return atom_expr2(a, he_sym(a, tag), x);
}

static Atom *he_accept(Arena *a, Atom *payload) {
    return he_wrap1(a, "he-accept", payload);
}
static Atom *he_reject(Arena *a, Atom *reason) {
    return he_wrap1(a, "he-reject", reason);
}
static Atom *he_unknown(Arena *a, Atom *reason) {
    return he_wrap1(a, "he-unknown", reason);
}
static Atom *he_reason(Arena *a, const char *tag) {
    Atom *e[1]; e[0] = he_sym(a, tag);
    return atom_expr(a, e, 1);
}
static Atom *he_reason2(Arena *a, const char *tag, Atom *d) {
    return atom_expr2(a, he_sym(a, tag), d);
}

/* ── the consistency relation, with a named reason per edge ───────────────
 * HE's match_types returns a bare bool that fuses four reasons.  Naming the
 * reason apart is the whole regrounding: the dynamic-? edge is marked so it
 * cannot be composed, the top edge is subsumption, the staging edge governs
 * evaluation order, and only the exact edge is sound must-typing. */

typedef enum {
    HE_NONE = 0,   /* no consistency rule applies: a proven mismatch */
    HE_EXACT,      /* structural/nominal equality (sound must-typing core) */
    HE_STRUCT,     /* arrow-congruent, all children consistent, some non-exact */
    HE_DYNAMIC,    /* ? on one side — deliberately non-composing */
    HE_TOP,        /* Atom-as-top — one-directional subsumption */
    HE_META,       /* a meta-type — a staging modality edge */
    HE_UNKNOWN     /* resource exhausted before a verdict */
} HeEdge;

static const char *edge_name(HeEdge e) {
    switch (e) {
    case HE_NONE: return "none";
    case HE_EXACT: return "exact";
    case HE_STRUCT: return "structural";
    case HE_DYNAMIC: return "dynamic";
    case HE_TOP: return "top";
    case HE_META: return "meta-staging";
    case HE_UNKNOWN: return "unknown";
    }
    return "none";
}

static CettaHeTypingEdge public_edge(HeEdge edge) {
    switch (edge) {
    case HE_NONE: return CETTA_HE_EDGE_NONE;
    case HE_EXACT: return CETTA_HE_EDGE_EXACT;
    case HE_STRUCT: return CETTA_HE_EDGE_STRUCTURAL;
    case HE_DYNAMIC: return CETTA_HE_EDGE_DYNAMIC;
    case HE_TOP: return CETTA_HE_EDGE_TOP;
    case HE_META: return CETTA_HE_EDGE_META_STAGING;
    case HE_UNKNOWN: return CETTA_HE_EDGE_UNKNOWN;
    }
    return CETTA_HE_EDGE_UNKNOWN;
}

const char *he_typing_edge_name(CettaHeTypingEdge edge) {
    switch (edge) {
    case CETTA_HE_EDGE_NONE: return "none";
    case CETTA_HE_EDGE_EXACT: return "exact";
    case CETTA_HE_EDGE_STRUCTURAL: return "structural";
    case CETTA_HE_EDGE_DYNAMIC: return "dynamic";
    case CETTA_HE_EDGE_TOP: return "top";
    case CETTA_HE_EDGE_META_STAGING: return "meta-staging";
    case CETTA_HE_EDGE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

bool he_typing_edge_is_exact_or_structural(CettaHeTypingEdge edge) {
    return edge == CETTA_HE_EDGE_EXACT ||
           edge == CETTA_HE_EDGE_STRUCTURAL;
}

const char *he_typing_variable_class_name(CettaHeVariableClass var_class) {
    switch (var_class) {
    case CETTA_HE_VAR_RIGID: return "rigid";
    case CETTA_HE_VAR_SCHEME: return "scheme";
    case CETTA_HE_VAR_ELABORATION: return "elaboration";
    }
    return "unknown";
}

/* Resource-aware entry points install one policy for the whole judgment.
   Structural checking is finite over its input and is measured only when the
   caller explicitly requests a bound. That optional bound is consumed solely
   by partial producers such as evaluation and user-defined type computation. */
static __thread CettaHeTypingBudget *g_active_typing_budget = NULL;

static void he_typing_note_work(uint64_t amount) {
    CettaHeTypingBudget *budget = g_active_typing_budget;
    if (!budget || !budget->steps_limited) return;
    if (budget->work_steps_observed > UINT64_MAX - amount)
        budget->work_steps_observed = UINT64_MAX;
    else
        budget->work_steps_observed += amount;
}

static bool he_typing_step(uint64_t *fuel) {
    CettaHeTypingBudget *budget = g_active_typing_budget;
    if (budget) {
        he_typing_note_work(1);
        return true;
    }
    if (!fuel || *fuel == 0) return false;
    (*fuel)--;
    return true;
}

static bool he_typing_has_producer_steps(const uint64_t *fuel,
                                         uint64_t amount) {
    if (g_active_typing_budget)
        return !g_active_typing_budget->steps_limited ||
               g_active_typing_budget->steps_remaining >= amount;
    return fuel && *fuel >= amount;
}

static void he_typing_charge_external(uint64_t *fuel, uint64_t amount) {
    CettaHeTypingBudget *budget = g_active_typing_budget;
    if (budget) {
        if (budget->steps_limited) {
            budget->steps_remaining = amount >= budget->steps_remaining
                ? 0 : budget->steps_remaining - amount;
            if (budget->steps_spent > UINT64_MAX - amount)
                budget->steps_spent = UINT64_MAX;
            else
                budget->steps_spent += amount;
        }
    } else if (fuel) {
        *fuel = amount >= *fuel ? 0 : *fuel - amount;
    }
    he_typing_note_work(amount);
}

/* ── edge composition ────────────────────────────────────────────────────
 * A composite edge is tagged no more strongly than its weakest child.
 * Tag strength (strongest first): exact > structural > top > meta-staging
 * > dynamic.  Dynamic dominates, so a ? child can never be laundered into a
 * structural edge by nesting — non-composition must survive congruence, not
 * just flat pairs.  Top and meta stay distinguishable through composition so
 * callers can still see WHY a composite is consistent.  none/unknown
 * short-circuit in the congruence loops and never combine. */
static int edge_weakness(HeEdge e) {
    switch (e) {
    case HE_EXACT: return 0;
    case HE_STRUCT: return 1;
    case HE_TOP: return 2;
    case HE_META: return 3;
    case HE_DYNAMIC: return 4;
    default: return 5;
    }
}

static HeEdge edge_combine(HeEdge acc, HeEdge child) {
    if (child == HE_EXACT) return acc;
    /* any non-exact child demotes an exact accumulator to structural */
    if (acc == HE_EXACT) acc = HE_STRUCT;
    return edge_weakness(child) > edge_weakness(acc) ? child : acc;
}

/* Only these edges are exact-or-structural: their reason is structural equality all
 * the way down.  A dynamic, top, or meta-staging tag anywhere in the
 * composition is a gradual acceptance, not a proof — the chainer must not
 * treat such a match as evidence. */
static bool edge_is_exact_or_structural(HeEdge e) {
    return e == HE_EXACT || e == HE_STRUCT;
}

static bool is_undefined(Atom *t) {
    return atom_is_symbol_id(t, g_builtin_syms.undefined_type);
}
static bool is_atom_top(Atom *t) {
    return atom_is_symbol_id(t, g_builtin_syms.atom);
}
static bool is_arrow(Atom *t) {
    return t->kind == ATOM_EXPR && t->expr.len >= 2 &&
           atom_is_symbol_id(t->expr.elems[0], g_builtin_syms.arrow);
}

/* Structural equality of type expressions, treating unbound variables as
 * matching only themselves by identity — no bidirectional binding here, which
 * is the deliberate divergence from HE's match_atoms (census entry). */
static bool type_eq(Atom *x, Atom *y) {
    return atom_eq(x, y);
}

/* Directed note: consistency is symmetric for ? and structural, but Atom-top is
 * recorded so callers can see subsumption direction; the relation itself is
 * evaluated on the ordered pair (actual, expected). */
static HeEdge consistency(Atom *actual, Atom *expected, uint64_t *fuel) {
    if (!he_typing_step(fuel)) return HE_UNKNOWN;

    /* ? on either side: dynamic edge, non-composing.  This is the fix for the
     * %Undefined% laundering — the edge is marked dynamic so a
     * chain through ? cannot be collapsed into an exact relation. */
    if (is_undefined(actual) || is_undefined(expected)) return HE_DYNAMIC;

    /* Atom as expected: genuine top, subsumption holds one-directionally. */
    if (is_atom_top(expected)) return HE_TOP;
    /* Atom as actual against a non-top expected is a proven mismatch under
     * success typing unless expected is itself a meta-type. */
    if (is_atom_top(actual)) {
        if (atom_is_meta_type(expected)) return HE_META;
        return HE_NONE;
    }

    /* Meta-type staging: a meta-typed slot governs evaluation order rather than
     * value membership; the formal (expected) must match the actual's category. */
    if (atom_is_meta_type(expected) || atom_is_meta_type(actual)) {
        if (atom_is_meta_type(expected) && atom_is_meta_type(actual))
            return type_eq(expected, actual) ? HE_META : HE_NONE;
        /* one side a value type, one a meta-type: staging boundary, deferred */
        return HE_META;
    }

    if (type_eq(actual, expected)) return HE_EXACT;

    /* arrow congruence: consistent iff domains and codomains are, the edge
     * combining the child tags (edge_combine: a dynamic child keeps the whole
     * edge dynamic; top/meta children keep their reason visible). */
    if (is_arrow(actual) && is_arrow(expected) &&
        actual->expr.len == expected->expr.len) {
        HeEdge worst = HE_EXACT;
        for (uint32_t i = 1; i < actual->expr.len; i++) {
            HeEdge c = consistency(actual->expr.elems[i], expected->expr.elems[i],
                                   fuel);
            if (c == HE_UNKNOWN) return HE_UNKNOWN;
            if (c == HE_NONE) return HE_NONE;
            worst = edge_combine(worst, c);
        }
        return worst;
    }

    /* Same expression head and arity, non-arrow: structural product/tuple. */
    if (actual->kind == ATOM_EXPR && expected->kind == ATOM_EXPR &&
        actual->expr.len == expected->expr.len && actual->expr.len > 0) {
        HeEdge worst = HE_EXACT;
        for (uint32_t i = 0; i < actual->expr.len; i++) {
            HeEdge c = consistency(actual->expr.elems[i], expected->expr.elems[i],
                                   fuel);
            if (c == HE_UNKNOWN) return HE_UNKNOWN;
            if (c == HE_NONE) return HE_NONE;
            worst = edge_combine(worst, c);
        }
        return worst;
    }

    return HE_NONE;
}

CettaHeTypingEdge he_typing_classify_consistency(Atom *actual, Atom *expected,
                                                 uint64_t fuel) {
    return public_edge(consistency(actual, expected, &fuel));
}

/* ── shared type-inference support ─────────────────────────────────────── */

void he_typing_budget_init(CettaHeTypingBudget *budget, uint64_t steps) {
    if (!budget) return;
    *budget = (CettaHeTypingBudget){
        .steps_limited = true,
        .steps_initial = steps,
        .steps_remaining = steps,
        .steps_spent = 0,
        .work_steps_observed = 0,
        .max_depth_observed = 0,
        .type_capacity = 0,
        .type_capacity_exhausted = false,
        .evaluator_stack_exhausted = false,
        .evaluator_capacity_exhausted = false,
        .allow_marked_user_type_functions = true,
    };
}

void he_typing_budget_init_unbounded(CettaHeTypingBudget *budget) {
    he_typing_budget_init(budget, 0);
    if (budget) budget->steps_limited = false;
}

typedef struct {
    Atom **items;
    uint32_t count;
    uint32_t capacity;
    bool overflow;
    Arena *arena;
} HeTypeSet;

static void set_init(HeTypeSet *s, Arena *arena) {
    *s = (HeTypeSet){.arena = arena};
}

static void set_add(HeTypeSet *s, Atom *t) {
    for (uint32_t i = 0; i < s->count; i++)
        if (atom_eq(s->items[i], t)) return;
    if (s->count == UINT32_MAX) {
        s->overflow = true;
        return;
    }
    if (s->count == s->capacity) {
        uint32_t next_capacity = s->capacity ? s->capacity * 2u : 8u;
        if (next_capacity <= s->capacity) next_capacity = UINT32_MAX;
        if ((size_t)next_capacity > SIZE_MAX / sizeof(Atom *)) {
            s->overflow = true;
            return;
        }
        Atom **next = arena_alloc(
            s->arena, sizeof(Atom *) * (size_t)next_capacity);
        if (s->count)
            memcpy(next, s->items, sizeof(Atom *) * (size_t)s->count);
        s->items = next;
        s->capacity = next_capacity;
    }
    s->items[s->count++] = t;
}

/* A term may be handed quoted to type it as written rather than reduced. */
static Atom *unquote(Atom *t) {
    while (t->kind == ATOM_EXPR && t->expr.len == 2 &&
           atom_is_symbol_id(t->expr.elems[0], g_builtin_syms.quote))
        t = t->expr.elems[1];
    return t;
}

/* Resolve a variable through the accumulated substitution. */
static Atom *deref(Bindings *tb, Atom *t) {
    while (t->kind == ATOM_VAR) {
        Atom *b = bindings_lookup_id(tb, t->var_id);
        if (!b || b == t) break;
        t = b;
    }
    return t;
}

/* A dependent binder (: $v T) in a domain names a value variable $v of type T.
 * Peeling it is what lets an argument bind BOTH a value and a type variable. */
static bool split_binder(Atom *domain, Atom **binder, Atom **type) {
    if (domain->kind == ATOM_EXPR && domain->expr.len == 3 &&
        atom_is_symbol_id(domain->expr.elems[0], g_builtin_syms.colon) &&
        domain->expr.elems[1]->kind == ATOM_VAR) {
        *binder = domain->expr.elems[1];
        *type = domain->expr.elems[2];
        return true;
    }
    *binder = NULL;
    *type = domain;
    return false;
}


/* ── checked type-level computation ──────────────────────────────────────
 * Only explicitly marked functions may run inside a type.  Evaluation runs
 * against a snapshot of the source space, accepts one unique successful
 * normal result, and respects an explicit producer bound when supplied. Open
 * expressions remain residual: a dependent codomain can be instantiated later
 * rather than guessed now. */

typedef enum {
    HE_NORM_COMPLETE = 0,
    HE_NORM_RESOURCE,
    HE_NORM_DEPTH,
    HE_NORM_AMBIGUOUS,
    HE_NORM_NO_RESULT,
    HE_NORM_INADMISSIBLE, /* effectful computation inside a type: never run */
    HE_NORM_PROVISIONAL   /* user computation lacks Prime admission */
} HeNormStatus;

static void he_note_depth(uint32_t *max_depth_observed, uint32_t depth) {
    if (max_depth_observed && depth > *max_depth_observed)
        *max_depth_observed = depth;
}

static bool space_has_unary_marker(Space *space, const char *marker,
                                   Atom *payload) {
    uint32_t len = 0;
    if (!space_length_u32_checked(space, &len)) return false;
    for (uint32_t i = 0; i < len; i++) {
        Atom *at = space_get_at(space, i);
        if (!at || at->kind != ATOM_EXPR || at->expr.len != 2) continue;
        if (!atom_is_symbol(at->expr.elems[0], marker)) continue;
        if (atom_eq(at->expr.elems[1], payload)) return true;
    }
    return false;
}

static bool atom_is_true_value(Atom *x) {
    return atom_is_symbol(x, "True") ||
           (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_BOOL &&
            x->ground.bval);
}

static bool atom_is_false_value(Atom *x) {
    return atom_is_symbol(x, "False") ||
           (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_BOOL &&
            !x->ground.bval);
}

typedef struct {
    Atom *input;
    Atom **children;
    uint32_t next_child;
    uint32_t depth;
    bool entered;
    bool changed;
} HeNormalizeFrame;

static bool he_normalize_push(HeNormalizeFrame **stack, size_t *len,
                              size_t *cap, HeNormalizeFrame *inline_stack,
                              Atom *input, uint32_t depth) {
    if (*len == *cap) {
        size_t next_cap = *cap * 2u;
        if (next_cap <= *cap || next_cap > SIZE_MAX / sizeof(**stack))
            return false;
        HeNormalizeFrame *next = cetta_malloc(sizeof(**stack) * next_cap);
        memcpy(next, *stack, sizeof(**stack) * *len);
        if (*stack != inline_stack) free(*stack);
        *stack = next;
        *cap = next_cap;
    }
    (*stack)[(*len)++] = (HeNormalizeFrame){
        .input = input,
        .depth = depth,
    };
    return true;
}

static Atom *normalize_type_checked(Arena *a, Space *space, Atom *ty,
                                    uint64_t *fuel, HeNormStatus *status,
                                    uint32_t depth,
                                    uint32_t *max_depth_observed) {
    HeNormalizeFrame inline_stack[16];
    HeNormalizeFrame *stack = inline_stack;
    size_t len = 0;
    size_t cap = sizeof inline_stack / sizeof inline_stack[0];
    if (!he_normalize_push(&stack, &len, &cap, inline_stack, ty, depth)) {
        *status = HE_NORM_RESOURCE;
        return ty;
    }

    for (;;) {
        HeNormalizeFrame *frame = &stack[len - 1u];
        Atom *completed = NULL;

        if (!frame->entered) {
            he_note_depth(max_depth_observed, frame->depth);
            if (!he_typing_step(fuel)) {
                *status = HE_NORM_RESOURCE;
                goto fail;
            }
            if (!frame->input || frame->input->kind != ATOM_EXPR ||
                frame->input->expr.len == 0) {
                completed = frame->input;
                goto frame_complete;
            }
            frame->children = arena_alloc(
                a, sizeof(Atom *) * (size_t)frame->input->expr.len);
            frame->entered = true;
        }

        if (frame->next_child < frame->input->expr.len) {
            uint32_t child_depth = frame->depth == UINT32_MAX
                ? UINT32_MAX : frame->depth + 1u;
            if (!he_normalize_push(
                    &stack, &len, &cap, inline_stack,
                    frame->input->expr.elems[frame->next_child],
                    child_depth)) {
                *status = HE_NORM_RESOURCE;
                goto fail;
            }
            continue;
        }

        Atom *norm = frame->changed
            ? atom_expr(a, frame->children, frame->input->expr.len)
            : frame->input;

        /* Type-pure grounded computation (arithmetic and friends) is the
         * capability-gated deterministic fragment; normalize_type_expr will
         * not dispatch anything outside it. */
        Atom *grounded = normalize_type_expr_head(a, norm);
        if (grounded != norm) {
            uint32_t next_depth = frame->depth == UINT32_MAX
                ? UINT32_MAX : frame->depth + 1u;
            *frame = (HeNormalizeFrame){
                .input = grounded,
                .depth = next_depth,
            };
            continue;
        }

        Atom *head = norm->expr.elems[0];

        /* A grounded op that is NOT type-pure names a computation with effects
         * or nondeterminism. It must never run inside a type, and a user marker
         * cannot override that. */
        if (head->kind == ATOM_SYMBOL && is_grounded_op(head->sym_id) &&
            !grounded_op_is_type_pure(head->sym_id) &&
            !atom_has_vars(norm)) {
            *status = HE_NORM_INADMISSIBLE;
            goto fail;
        }

        if (!space_has_unary_marker(space, "type-level-function", head)) {
            completed = norm;
            goto frame_complete;
        }

        if (g_active_typing_budget &&
            !g_active_typing_budget->allow_marked_user_type_functions) {
            *status = HE_NORM_PROVISIONAL;
            goto fail;
        }

        /* Open calls are well-formed residual type expressions. */
        if (atom_has_vars(norm)) {
            completed = norm;
            goto frame_complete;
        }

        if (!he_typing_has_producer_steps(fuel, 1)) {
            *status = HE_NORM_RESOURCE;
            goto fail;
        }
        /* Marked user functions run against a snapshot clone, optionally
         * caller-bounded, and only a unique successful normal form is accepted. */
        bool evaluation_limited = !g_active_typing_budget ||
                                  g_active_typing_budget->steps_limited;
        uint64_t eval_budget = evaluation_limited ? *fuel : 0;
        int budget = evaluation_limited
            ? (eval_budget > (uint64_t)INT_MAX ? INT_MAX : (int)eval_budget)
            : -1;
        Space *snapshot = eval_space_snapshot_clone(space, a);
        if (!snapshot) {
            *status = HE_NORM_RESOURCE;
            goto fail;
        }

        EvalOutcome outcome;
        eval_outcome_init(&outcome);
        metta_eval_outcome(snapshot, a, NULL, norm, budget, &outcome);
        he_typing_charge_external(fuel, outcome.steps_spent);

        Atom *unique = NULL;
        bool ambiguous = false;
        for (CettaCount i = 0; i < outcome.results.len; i++) {
            Atom *candidate = outcome.results.items[i];
            if (!candidate || atom_is_error(candidate) ||
                atom_is_empty(candidate))
                continue;
            if (!unique) unique = candidate;
            else if (!atom_eq(unique, candidate)) {
                ambiguous = true;
                break;
            }
        }
        CettaEvalCompletion completion = outcome.completion;
        if (completion == CETTA_EVAL_INCOMPLETE_STACK &&
            g_active_typing_budget) {
            g_active_typing_budget->evaluator_stack_exhausted = true;
        }
        if (completion == CETTA_EVAL_INCOMPLETE_CAPACITY &&
            g_active_typing_budget) {
            g_active_typing_budget->evaluator_capacity_exhausted = true;
        }
        eval_outcome_free(&outcome);

        if (ambiguous) {
            *status = HE_NORM_AMBIGUOUS;
            goto fail;
        }
        if (completion != CETTA_EVAL_COMPLETE) {
            *status = HE_NORM_RESOURCE;
            goto fail;
        }
        if (!unique) {
            *status = HE_NORM_NO_RESULT;
            goto fail;
        }
        if (atom_eq(unique, norm)) {
            completed = norm;
            goto frame_complete;
        }

        uint32_t next_depth = frame->depth == UINT32_MAX
            ? UINT32_MAX : frame->depth + 1u;
        *frame = (HeNormalizeFrame){
            .input = unique,
            .depth = next_depth,
        };
        continue;

frame_complete:
        len--;
        if (len == 0) {
            if (stack != inline_stack) free(stack);
            return completed;
        }
        frame = &stack[len - 1u];
        uint32_t child_index = frame->next_child++;
        frame->children[child_index] = completed;
        if (completed != frame->input->expr.elems[child_index])
            frame->changed = true;
    }

fail:
    /* A failed child invalidates its enclosing normalization, exactly as the
       recursive implementation did. The current root is retained as residual
       syntax; the status carries why normalization did not finish. */
    ty = stack[0].input;
    if (stack != inline_stack) free(stack);
    return ty;
}

Atom *he_typing_normalize_shared_type(Arena *a, Space *space, Atom *type,
                                      uint64_t fuel, bool *complete) {
    HeNormStatus status = HE_NORM_COMPLETE;
    Atom *normalized = normalize_type_checked(a, space, type, &fuel, &status, 0,
                                              NULL);
    if (complete) *complete = status == HE_NORM_COMPLETE;
    return normalized;
}

CettaHeNormalizeStatus he_typing_normalize_type_status(
    Arena *a, Space *space, Atom *type, uint64_t fuel, Atom **normalized_out) {
    HeNormStatus status = HE_NORM_COMPLETE;
    Atom *normalized = normalize_type_checked(a, space, type, &fuel, &status, 0,
                                              NULL);
    if (normalized_out) *normalized_out = normalized;
    switch (status) {
    case HE_NORM_COMPLETE: return CETTA_HE_NORMALIZE_COMPLETE;
    case HE_NORM_RESOURCE: return CETTA_HE_NORMALIZE_RESOURCE;
    case HE_NORM_DEPTH: return CETTA_HE_NORMALIZE_DEPTH;
    case HE_NORM_AMBIGUOUS: return CETTA_HE_NORMALIZE_AMBIGUOUS;
    case HE_NORM_NO_RESULT: return CETTA_HE_NORMALIZE_NO_RESULT;
    case HE_NORM_INADMISSIBLE: return CETTA_HE_NORMALIZE_INADMISSIBLE;
    case HE_NORM_PROVISIONAL: return CETTA_HE_NORMALIZE_PROVISIONAL;
    }
    return CETTA_HE_NORMALIZE_RESOURCE;
}

CettaHeNormalizeStatus he_typing_normalize_type_status_budgeted(
    Arena *a, Space *space, Atom *type, CettaHeTypingBudget *budget,
    Atom **normalized_out) {
    if (!budget) return CETTA_HE_NORMALIZE_RESOURCE;
    CettaHeTypingBudget *parent_budget = g_active_typing_budget;
    g_active_typing_budget = budget;
    HeNormStatus status = HE_NORM_COMPLETE;
    uint32_t *max_depth_observed = budget->steps_limited
        ? &budget->max_depth_observed : NULL;
    Atom *normalized = normalize_type_checked(
        a, space, type, &budget->steps_remaining, &status, 0,
        max_depth_observed);
    g_active_typing_budget = parent_budget;
    if (normalized_out) *normalized_out = normalized;
    switch (status) {
    case HE_NORM_COMPLETE: return CETTA_HE_NORMALIZE_COMPLETE;
    case HE_NORM_RESOURCE: return CETTA_HE_NORMALIZE_RESOURCE;
    case HE_NORM_DEPTH: return CETTA_HE_NORMALIZE_DEPTH;
    case HE_NORM_AMBIGUOUS: return CETTA_HE_NORMALIZE_AMBIGUOUS;
    case HE_NORM_NO_RESULT: return CETTA_HE_NORMALIZE_NO_RESULT;
    case HE_NORM_INADMISSIBLE: return CETTA_HE_NORMALIZE_INADMISSIBLE;
    case HE_NORM_PROVISIONAL: return CETTA_HE_NORMALIZE_PROVISIONAL;
    }
    return CETTA_HE_NORMALIZE_RESOURCE;
}

typedef enum {
    HE_TYPE_VALID = 0,
    HE_TYPE_INVALID,
    HE_TYPE_VALIDATION_UNKNOWN,
    HE_TYPE_VALIDATION_INCOMPLETE
} HeTypeValidity;

static const char *refinement_status_reason(HeNormStatus ns) {
    switch (ns) {
    case HE_NORM_DEPTH: return "type-refinement-depth-exhausted";
    case HE_NORM_AMBIGUOUS: return "type-refinement-ambiguous";
    case HE_NORM_NO_RESULT: return "type-refinement-no-result";
    case HE_NORM_INADMISSIBLE: return "type-computation-inadmissible";
    case HE_NORM_PROVISIONAL: return "type-computation-unadmitted";
    default: return "type-refinement-fuel-exhausted";
    }
}

typedef struct {
    Atom *type;
    uint32_t next_child;
    uint32_t depth;
    bool entered;
} HeRefinementFrame;

static bool he_refinement_push(HeRefinementFrame **stack, size_t *len,
                               size_t *cap, HeRefinementFrame *inline_stack,
                               Atom *type, uint32_t depth) {
    if (*len == *cap) {
        size_t next_cap = *cap * 2u;
        if (next_cap <= *cap || next_cap > SIZE_MAX / sizeof(**stack))
            return false;
        HeRefinementFrame *next = cetta_malloc(sizeof(**stack) * next_cap);
        memcpy(next, *stack, sizeof(**stack) * *len);
        if (*stack != inline_stack) free(*stack);
        *stack = next;
        *cap = next_cap;
    }
    (*stack)[(*len)++] = (HeRefinementFrame){
        .type = type,
        .depth = depth,
    };
    return true;
}

static HeTypeValidity check_type_refinements(Arena *a, Space *space,
                                             Atom *ty, uint64_t *fuel,
                                             Atom **detail, uint32_t depth,
                                             uint32_t *max_depth_observed) {
    HeRefinementFrame inline_stack[16];
    HeRefinementFrame *stack = inline_stack;
    size_t stack_len = 0;
    size_t stack_cap = sizeof inline_stack / sizeof inline_stack[0];
    if (!he_refinement_push(&stack, &stack_len, &stack_cap, inline_stack,
                            ty, depth)) {
        if (detail) *detail = he_reason(a, "type-refinement-worklist-exhausted");
        return HE_TYPE_VALIDATION_INCOMPLETE;
    }

    while (stack_len > 0) {
        HeRefinementFrame *frame = &stack[stack_len - 1u];
        if (!frame->entered) {
            he_note_depth(max_depth_observed, frame->depth);
            if (!he_typing_step(fuel)) {
                if (detail)
                    *detail = he_reason(a, "type-refinement-fuel-exhausted");
                if (stack != inline_stack) free(stack);
                return HE_TYPE_VALIDATION_INCOMPLETE;
            }

            HeNormStatus ns = HE_NORM_COMPLETE;
            frame->type = normalize_type_checked(
                a, space, frame->type, fuel, &ns, frame->depth,
                max_depth_observed);
            if (ns != HE_NORM_COMPLETE) {
                if (detail) *detail = he_reason(a, refinement_status_reason(ns));
                if (stack != inline_stack) free(stack);
                /* An inadmissible computation in a type is a proven defect,
                   not a resource condition. */
                if (ns == HE_NORM_INADMISSIBLE) return HE_TYPE_INVALID;
                if (ns == HE_NORM_RESOURCE || ns == HE_NORM_DEPTH)
                    return HE_TYPE_VALIDATION_INCOMPLETE;
                return HE_TYPE_VALIDATION_UNKNOWN;
            }
            frame->entered = true;
        }

        ty = frame->type;
        if (ty && ty->kind == ATOM_EXPR &&
            frame->next_child < ty->expr.len) {
            Atom *child = ty->expr.elems[frame->next_child++];
            uint32_t child_depth = frame->depth == UINT32_MAX
                ? UINT32_MAX : frame->depth + 1u;
            if (!he_refinement_push(&stack, &stack_len, &stack_cap,
                                    inline_stack, child, child_depth)) {
                if (detail)
                    *detail = he_reason(
                        a, "type-refinement-worklist-exhausted");
                if (stack != inline_stack) free(stack);
                return HE_TYPE_VALIDATION_INCOMPLETE;
            }
            continue;
        }

        if (ty && ty->kind == ATOM_EXPR && ty->expr.len > 0) {
            uint32_t space_len = 0;
            if (!space_length_u32_checked(space, &space_len)) {
                if (detail) *detail = he_reason(a, "space-too-large");
                if (stack != inline_stack) free(stack);
                return HE_TYPE_VALIDATION_UNKNOWN;
            }
            for (uint32_t i = 0; i < space_len; i++) {
                Atom *row = space_get_at(space, i);
                if (!row || row->kind != ATOM_EXPR || row->expr.len != 4)
                    continue;
                if (!atom_is_symbol(row->expr.elems[0],
                                    "type-index-refinement"))
                    continue;
                if (!atom_eq(row->expr.elems[1], ty->expr.elems[0])) continue;
                Atom *idx = row->expr.elems[2];
                if (!(idx->kind == ATOM_GROUNDED &&
                      idx->ground.gkind == GV_INT) ||
                    idx->ground.ival < 0 ||
                    (uint64_t)idx->ground.ival >= ty->expr.len) {
                    if (detail)
                        *detail = he_reason(
                            a, "invalid-type-refinement-index");
                    if (stack != inline_stack) free(stack);
                    return HE_TYPE_INVALID;
                }
                Atom *predicate = row->expr.elems[3];
                if (!space_has_unary_marker(
                        space, "type-level-function", predicate)) {
                    if (detail)
                        *detail = he_reason(a, "untrusted-type-refinement");
                    if (stack != inline_stack) free(stack);
                    return HE_TYPE_INVALID;
                }
                Atom *arg = ty->expr.elems[(uint32_t)idx->ground.ival];
                if (atom_has_vars(arg)) {
                    if (detail)
                        *detail = he_reason(a, "open-type-refinement");
                    if (stack != inline_stack) free(stack);
                    return HE_TYPE_VALIDATION_UNKNOWN;
                }
                Atom *call = atom_expr2(a, predicate, arg);
                HeNormStatus ps = HE_NORM_COMPLETE;
                uint32_t predicate_depth = frame->depth == UINT32_MAX
                    ? UINT32_MAX : frame->depth + 1u;
                Atom *answer = normalize_type_checked(
                    a, space, call, fuel, &ps, predicate_depth,
                    max_depth_observed);
                if (ps != HE_NORM_COMPLETE) {
                    if (detail)
                        *detail = he_reason(a, refinement_status_reason(ps));
                    if (stack != inline_stack) free(stack);
                    if (ps == HE_NORM_INADMISSIBLE) return HE_TYPE_INVALID;
                    if (ps == HE_NORM_RESOURCE || ps == HE_NORM_DEPTH)
                        return HE_TYPE_VALIDATION_INCOMPLETE;
                    return HE_TYPE_VALIDATION_UNKNOWN;
                }
                if (atom_is_false_value(answer)) {
                    if (detail)
                        *detail = he_reason2(
                            a, "type-refinement-failed", arg);
                    if (stack != inline_stack) free(stack);
                    return HE_TYPE_INVALID;
                }
                if (!atom_is_true_value(answer)) {
                    if (detail)
                        *detail = he_reason2(
                            a, "type-refinement-not-boolean", answer);
                    if (stack != inline_stack) free(stack);
                    return HE_TYPE_VALIDATION_UNKNOWN;
                }
            }
        }
        stack_len--;
    }
    if (stack != inline_stack) free(stack);
    return HE_TYPE_VALID;
}

CettaHeRefinementStatus he_typing_check_refinement_status(
    Arena *a, Space *space, Atom *type, uint64_t fuel, Atom **detail_out) {
    Atom *detail = NULL;
    HeTypeValidity status = check_type_refinements(a, space, type, &fuel,
                                                   &detail, 0, NULL);
    if (detail_out) *detail_out = detail;
    switch (status) {
    case HE_TYPE_VALID: return CETTA_HE_REFINEMENT_VALID;
    case HE_TYPE_INVALID: return CETTA_HE_REFINEMENT_INVALID;
    case HE_TYPE_VALIDATION_UNKNOWN: return CETTA_HE_REFINEMENT_UNDETERMINED;
    case HE_TYPE_VALIDATION_INCOMPLETE:
        return CETTA_HE_REFINEMENT_INCOMPLETE;
    }
    return CETTA_HE_REFINEMENT_UNDETERMINED;
}

CettaHeRefinementStatus he_typing_check_refinement_status_budgeted(
    Arena *a, Space *space, Atom *type, CettaHeTypingBudget *budget,
    Atom **detail_out) {
    if (!budget) return CETTA_HE_REFINEMENT_INCOMPLETE;
    CettaHeTypingBudget *parent_budget = g_active_typing_budget;
    g_active_typing_budget = budget;
    Atom *detail = NULL;
    uint32_t *max_depth_observed = budget->steps_limited
        ? &budget->max_depth_observed : NULL;
    HeTypeValidity status = check_type_refinements(
        a, space, type, &budget->steps_remaining, &detail, 0,
        max_depth_observed);
    g_active_typing_budget = parent_budget;
    if (detail_out) *detail_out = detail;
    switch (status) {
    case HE_TYPE_VALID: return CETTA_HE_REFINEMENT_VALID;
    case HE_TYPE_INVALID: return CETTA_HE_REFINEMENT_INVALID;
    case HE_TYPE_VALIDATION_UNKNOWN: return CETTA_HE_REFINEMENT_UNDETERMINED;
    case HE_TYPE_VALIDATION_INCOMPLETE:
        return CETTA_HE_REFINEMENT_INCOMPLETE;
    }
    return CETTA_HE_REFINEMENT_UNDETERMINED;
}

/* consistency, plus variable unification into a substitution.  The symbol
 * reasons (%Undefined% dynamic, Atom-as-actual reject, meta staging) are checked
 * BEFORE any variable binding, so they are unchanged from `consistency` — the
 * census fixes are about those symbols, which are orthogonal to variables.  Used
 * ONLY for dependent-codomain instantiation; the reject gate (`check-type`)
 * and `is-consistent` keep the binding-free `consistency`. */
static HeEdge consistency_bind(Atom *actual, Atom *expected, uint64_t *fuel,
                               Bindings *tb) {
    if (!he_typing_step(fuel)) return HE_UNKNOWN;
    actual = deref(tb, actual);
    expected = deref(tb, expected);

    if (is_undefined(actual) || is_undefined(expected)) return HE_DYNAMIC;
    if (is_atom_top(expected)) return HE_TOP;
    if (is_atom_top(actual)) {
        if (atom_is_meta_type(expected)) return HE_META;
        return HE_NONE;
    }
    if (atom_is_meta_type(expected) || atom_is_meta_type(actual)) {
        if (atom_is_meta_type(expected) && atom_is_meta_type(actual))
            return type_eq(expected, actual) ? HE_META : HE_NONE;
        return HE_META;
    }

    /* variable unification (into tb): a logic-variable step, orthogonal to the
     * symbol wildcards above.  Binds function-fresh and argument variables so
     * the codomain can be instantiated. */
    if (expected->kind == ATOM_VAR) {
        return bindings_add_id_acyclic(
                   tb, expected->var_id, SYMBOL_ID_NONE, actual)
                   ? HE_EXACT : HE_NONE;
    }
    if (actual->kind == ATOM_VAR) {
        return bindings_add_id_acyclic(
                   tb, actual->var_id, SYMBOL_ID_NONE, expected)
                   ? HE_EXACT : HE_NONE;
    }

    if (type_eq(actual, expected)) return HE_EXACT;

    if (actual->kind == ATOM_EXPR && expected->kind == ATOM_EXPR &&
        actual->expr.len == expected->expr.len && actual->expr.len > 0) {
        uint32_t start = (is_arrow(actual) && is_arrow(expected)) ? 1u : 0u;
        HeEdge worst = HE_EXACT;
        for (uint32_t i = start; i < actual->expr.len; i++) {
            HeEdge c = consistency_bind(actual->expr.elems[i],
                                        expected->expr.elems[i], fuel, tb);
            if (c == HE_UNKNOWN) return HE_UNKNOWN;
            if (c == HE_NONE) return HE_NONE;
            worst = edge_combine(worst, c);
        }
        return worst;
    }
    return HE_NONE;
}

/* All term-type inference comes from the native profile-aware HE engine.
 * he-prime keeps only diagnostics, refinements, checked type computation, and
 * proof search over those shared results. */
static bool infer_shared_types_mode(Arena *a, Space *space, Atom *term,
                                    uint64_t *fuel, HeTypeSet *out,
                                    bool structural) {
    if (!he_typing_step(fuel)) return false;
    Atom **types = NULL;
    uint32_t count = structural
        ? eval_get_atom_types_structural_profiled(space, a, unquote(term), &types)
        : eval_get_atom_types_profiled(space, a, unquote(term), &types);
    for (uint32_t i = 0; i < count; i++)
        set_add(out, types[i]);
    free(types);
    return true;
}

static bool infer_shared_types(Arena *a, Space *space, Atom *term,
                               uint64_t *fuel, HeTypeSet *out) {
    return infer_shared_types_mode(a, space, term, fuel, out, false);
}

static bool infer_shared_types_structural(Arena *a, Space *space, Atom *term,
                                          uint64_t *fuel, HeTypeSet *out) {
    return infer_shared_types_mode(a, space, term, fuel, out, true);
}

static bool infer_shared_types_budgeted_mode(
    Arena *a, Space *space, Atom *term, uint64_t *fuel, HeTypeSet *out,
    bool structural, uint32_t *max_depth_observed,
    bool *type_capacity_exhausted) {
    bool steps_limited = !g_active_typing_budget ||
                         g_active_typing_budget->steps_limited;
    CettaTypeInferenceBudget inference = {
        .steps_limited = steps_limited,
        .steps_remaining = steps_limited && fuel ? *fuel : 0,
        .steps_spent = 0,
        .work_steps_observed = 0,
        .type_capacity = g_active_typing_budget
            ? g_active_typing_budget->type_capacity : 0,
        .max_depth_observed = max_depth_observed ? *max_depth_observed : 0,
        .complete = true,
        .type_capacity_exhausted = false,
        .evaluator_stack_exhausted = false,
        .evaluator_capacity_exhausted = false,
        .allow_marked_user_type_functions =
            !g_active_typing_budget ||
            g_active_typing_budget->allow_marked_user_type_functions,
    };
    Atom **types = NULL;
    uint32_t count = structural
        ? eval_get_atom_types_structural_profiled_budgeted(
              space, a, unquote(term), &types, &inference)
        : eval_get_atom_types_profiled_budgeted(
              space, a, unquote(term), &types, &inference);
    if (g_active_typing_budget && steps_limited) {
        if (steps_limited && fuel) *fuel = inference.steps_remaining;
        if (g_active_typing_budget->steps_spent >
            UINT64_MAX - inference.steps_spent) {
            g_active_typing_budget->steps_spent = UINT64_MAX;
        } else {
            g_active_typing_budget->steps_spent += inference.steps_spent;
        }
        he_typing_note_work(inference.work_steps_observed);
    }
    if (max_depth_observed &&
        inference.max_depth_observed > *max_depth_observed) {
        *max_depth_observed = inference.max_depth_observed;
    }
    if (type_capacity_exhausted)
        *type_capacity_exhausted = inference.type_capacity_exhausted;
    if (inference.evaluator_stack_exhausted && g_active_typing_budget)
        g_active_typing_budget->evaluator_stack_exhausted = true;
    if (inference.evaluator_capacity_exhausted && g_active_typing_budget)
        g_active_typing_budget->evaluator_capacity_exhausted = true;
    for (uint32_t i = 0; i < count; i++)
        set_add(out, types[i]);
    if (inference.type_capacity_exhausted)
        out->overflow = true;
    free(types);
    return inference.complete;
}

/* ── success-typing check (three-valued) ───────────────────────────────── */

static Atom *edge_atom(Arena *a, HeEdge e) { return he_sym(a, edge_name(e)); }

/* Closed checking treats variables as rigid.  Scheme variables are freshened
 * into elaboration variables only at an explicitly marked rule-use boundary;
 * that separate path returns its substitution in the search answer. */
static CettaHeCheckStatus classify_type_mode(
    Arena *a, Space *space, Atom *term, Atom *expected, uint64_t *fuel_io,
    bool ledger_mode, bool structural, bool require_exact_or_structural,
    HeEdge *edge_out, Atom **detail_out, uint32_t *max_depth_observed,
    bool *type_capacity_exhausted) {
    uint64_t fuel = fuel_io ? *fuel_io : 0;
#define CLASSIFY_RETURN(_status) do { \
    if (fuel_io) *fuel_io = fuel; \
    return (_status); \
} while (0)
    if (edge_out) *edge_out = HE_NONE;
    if (detail_out) *detail_out = NULL;
    if (type_capacity_exhausted) *type_capacity_exhausted = false;

    HeNormStatus ens = HE_NORM_COMPLETE;
    expected = normalize_type_checked(a, space, expected, &fuel, &ens, 0,
                                      max_depth_observed);
    if (ens != HE_NORM_COMPLETE) {
        if (detail_out)
            *detail_out = he_reason(a, "expected-type-normalization-incomplete");
        CLASSIFY_RETURN(ens == HE_NORM_RESOURCE || ens == HE_NORM_DEPTH
                            ? CETTA_HE_CHECK_INCOMPLETE
                            : CETTA_HE_CHECK_UNDETERMINED);
    }
    Atom *refinement_detail = NULL;
    HeTypeValidity ev = check_type_refinements(a, space, expected, &fuel,
                                               &refinement_detail, 0,
                                               max_depth_observed);
    if (ev == HE_TYPE_INVALID) {
        if (detail_out)
            *detail_out = refinement_detail
                ? refinement_detail : he_reason(a, "invalid-expected-type");
        CLASSIFY_RETURN(CETTA_HE_CHECK_REFUTED);
    }
    if (ev == HE_TYPE_VALIDATION_UNKNOWN) {
        if (detail_out)
            *detail_out = refinement_detail
                ? refinement_detail
                : he_reason(a, "expected-type-refinement-unknown");
        CLASSIFY_RETURN(CETTA_HE_CHECK_UNDETERMINED);
    }
    if (ev == HE_TYPE_VALIDATION_INCOMPLETE) {
        if (detail_out)
            *detail_out = refinement_detail
                ? refinement_detail
                : he_reason(a, "expected-type-refinement-incomplete");
        CLASSIFY_RETURN(CETTA_HE_CHECK_INCOMPLETE);
    }

    HeTypeSet ts;
    set_init(&ts, a);
    bool inference_complete = true;
    bool ok = true;
    if (ledger_mode) {
        inference_complete = infer_shared_types_budgeted_mode(
            a, space, term, &fuel, &ts, structural, max_depth_observed,
            type_capacity_exhausted);
    } else {
        ok = structural
                 ? infer_shared_types_structural(a, space, term, &fuel, &ts)
                 : infer_shared_types(a, space, term, &fuel, &ts);
    }
    if (!ok) {
        if (detail_out) *detail_out = he_reason(a, "type-inference-exhausted");
        CLASSIFY_RETURN(CETTA_HE_CHECK_INCOMPLETE);
    }
    if (ts.overflow) {
        if (type_capacity_exhausted) *type_capacity_exhausted = true;
        if (detail_out) *detail_out = he_reason(a, "type-set-capacity");
        CLASSIFY_RETURN(CETTA_HE_CHECK_INCOMPLETE);
    }
    HeEdge best = HE_NONE;
    bool saw_unknown = false;
    bool saw_incomplete = false;
    for (uint32_t i = 0; i < ts.count; i++) {
        uint64_t branch_fuel = fuel;
        uint64_t *f = ledger_mode ? &fuel : &branch_fuel;
        HeNormStatus ans = HE_NORM_COMPLETE;
        Atom *actual = normalize_type_checked(a, space, ts.items[i], f, &ans,
                                              0, max_depth_observed);
        if (ans != HE_NORM_COMPLETE) {
            if (ans == HE_NORM_RESOURCE || ans == HE_NORM_DEPTH)
                saw_incomplete = true;
            else saw_unknown = true;
            continue;
        }
        Atom *actual_detail = NULL;
        HeTypeValidity av = check_type_refinements(
            a, space, actual, f, &actual_detail, 0, max_depth_observed);
        if (av == HE_TYPE_INVALID) continue;
        if (av == HE_TYPE_VALIDATION_UNKNOWN) { saw_unknown = true; continue; }
        if (av == HE_TYPE_VALIDATION_INCOMPLETE) {
            saw_incomplete = true;
            continue;
        }
        HeEdge e = consistency(actual, expected, f);
        if (e == HE_UNKNOWN) { saw_unknown = true; continue; }
        if (e == HE_NONE) continue;
        if (best == HE_NONE || edge_weakness(e) < edge_weakness(best))
            best = e;
    }
    if (best != HE_NONE) {
        if (edge_out) *edge_out = best;
        Atom *edge_detail = atom_expr2(a, edge_atom(a, best), expected);
        if (require_exact_or_structural &&
            !edge_is_exact_or_structural(best)) {
            if (detail_out)
                *detail_out = he_reason2(
                    a, "non-exact-or-structural-consistency", edge_detail);
            CLASSIFY_RETURN(CETTA_HE_CHECK_UNDETERMINED);
        }
        if (detail_out) *detail_out = edge_detail;
        CLASSIFY_RETURN(CETTA_HE_CHECK_ESTABLISHED);
    }
    if (saw_incomplete) {
        if (detail_out) *detail_out = he_reason(a, "typing-resource-incomplete");
        CLASSIFY_RETURN(CETTA_HE_CHECK_INCOMPLETE);
    }
    if (!inference_complete) {
        if (detail_out)
            *detail_out = he_reason(a, "type-inference-resource-incomplete");
        CLASSIFY_RETURN(CETTA_HE_CHECK_INCOMPLETE);
    }
    if (saw_unknown) {
        if (detail_out) *detail_out = he_reason(a, "consistency-exhausted");
        CLASSIFY_RETURN(CETTA_HE_CHECK_UNDETERMINED);
    }
    Atom **reasons = arena_alloc(a, sizeof(Atom *) * (ts.count + 1));
    reasons[0] = he_sym(a, "no-consistent-type");
    for (uint32_t i = 0; i < ts.count; i++) reasons[i + 1] = ts.items[i];
    if (detail_out) *detail_out = atom_expr(a, reasons, ts.count + 1);
    CLASSIFY_RETURN(CETTA_HE_CHECK_REFUTED);
}
#undef CLASSIFY_RETURN

static Atom *check_type_mode(Arena *a, Space *space, Atom *term,
                             Atom *expected, uint64_t fuel, bool structural) {
    HeEdge edge = HE_NONE;
    Atom *detail = NULL;
    uint64_t remaining = fuel;
    CettaHeCheckStatus status = classify_type_mode(
        a, space, term, expected, &remaining, false, structural, false, &edge,
        &detail, NULL, NULL);
    (void)edge;
    if (status == CETTA_HE_CHECK_ESTABLISHED)
        return he_accept(a, detail ? detail : he_reason(a, "established"));
    if (status == CETTA_HE_CHECK_REFUTED)
        return he_reject(a, detail ? detail : he_reason(a, "refuted"));
    return he_unknown(a, detail ? detail : he_reason(a, "undetermined"));
}

static Atom *check_type(Arena *a, Space *space, Atom *term, Atom *expected,
                        uint64_t fuel) {
    return check_type_mode(a, space, term, expected, fuel, false);
}

CettaHeCheckStatus he_typing_check_term_status(
    Arena *a, Space *space, Atom *term, Atom *expected, uint64_t fuel,
    bool require_exact_or_structural, CettaHeTypingEdge *edge_out,
    Atom **detail_out) {
    HeEdge edge = HE_NONE;
    uint64_t remaining = fuel;
    CettaHeCheckStatus status = classify_type_mode(
        a, space, term, expected, &remaining, false, false,
        require_exact_or_structural, &edge, detail_out, NULL, NULL);
    if (edge_out) *edge_out = public_edge(edge);
    return status;
}

CettaHeCheckStatus he_typing_check_term_status_budgeted(
    Arena *a, Space *space, Atom *term, Atom *expected,
    CettaHeTypingBudget *budget, bool require_exact_or_structural,
    CettaHeTypingEdge *edge_out, Atom **detail_out) {
    if (!budget) return CETTA_HE_CHECK_INCOMPLETE;
    CettaHeTypingBudget *parent_budget = g_active_typing_budget;
    g_active_typing_budget = budget;
    HeEdge edge = HE_NONE;
    uint32_t *max_depth_observed = budget->steps_limited
        ? &budget->max_depth_observed : NULL;
    CettaHeCheckStatus status = classify_type_mode(
        a, space, term, expected, &budget->steps_remaining, true, false,
        require_exact_or_structural, &edge, detail_out,
        max_depth_observed, &budget->type_capacity_exhausted);
    g_active_typing_budget = parent_budget;
    if (edge_out) *edge_out = public_edge(edge);
    return status;
}

/* ── DTT-native chaining: proof search is type inhabitation ────────────────
 * A declaration (: proof P) is an inhabitant of proposition/type P.  A
 * declaration (: rule (-> P1 ... Pn Q)) marked by (chaining-rule rule) is a
 * proof constructor.  Backward chaining searches for a checked inhabitant of
 * the goal; forward chaining applies the same constructors to existing typed
 * inhabitants.  There is no second MeTTa-level unifier: matching, dependent
 * substitution, type-level computation, and the final proof check all reuse
 * the typing machinery above. */

#define CHAIN_INITIAL_CAP 32u

typedef struct {
    Atom **items;
    size_t len;
    size_t cap;
    Atom *inline_items[32];
} ChainAtomStack;

static void chain_atom_stack_init(ChainAtomStack *stack) {
    stack->items = stack->inline_items;
    stack->len = 0;
    stack->cap = sizeof stack->inline_items / sizeof stack->inline_items[0];
}

static void chain_atom_stack_free(ChainAtomStack *stack) {
    if (stack->items != stack->inline_items) free(stack->items);
}

static bool chain_atom_stack_push(ChainAtomStack *stack, Atom *atom) {
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap * 2u;
        if (next_cap <= stack->cap ||
            next_cap > SIZE_MAX / sizeof(*stack->items))
            return false;
        Atom **next = cetta_malloc(sizeof(*stack->items) * next_cap);
        memcpy(next, stack->items, sizeof(*stack->items) * stack->len);
        if (stack->items != stack->inline_items) free(stack->items);
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = atom;
    return true;
}

typedef struct {
    Atom *term;
    Atom *type;
    Atom *conclusion;
    SymbolId conclusion_head;
    uint32_t arity;
    uint32_t specificity;
    bool is_rule;
    bool is_scheme;
} ChainDecl;

typedef struct {
    SymbolId head;
    uint32_t index;
    uint32_t specificity;
    bool is_rule;
} ChainRef;

typedef struct {
    ChainDecl *decls;
    uint32_t count;
    uint32_t cap;
    ChainRef *refs;
    uint32_t *rigid_fact_indices;
    uint32_t rigid_fact_count;
} ChainIndex;

/* AnswerIdentityV2 is the lossless identity of a typed-search answer.  The
 * proof and checked type are fully substituted, variables are canonicalized
 * jointly across every field, and substitution records query variables,
 * elaboration variables introduced by explicit schemes, and residual equality
 * constraints.  It deliberately does not quotient distinct derivations,
 * theorem-equal proofs, or PLN evidence. */
typedef struct {
    Atom *proof;
    Atom *substitution;
    Atom *type;
    Atom *key;
} AnswerIdentityV2;

typedef struct {
    AnswerIdentityV2 identity;
    Bindings env;
} ChainProof;

typedef struct {
    ChainProof *items;
    uint32_t count;
    uint32_t cap;
} ChainProofVec;

typedef struct {
    uint64_t calls;
    uint64_t fact_candidates;
    uint64_t rule_candidates;
    uint64_t head_pruned;
    uint64_t ancestor_pruned;
    uint64_t proof_checks;
    uint64_t work_items;
    uint64_t continuations;
    uint64_t agenda_weight_pops;
    uint64_t agenda_age_pops;
} ChainStats;

typedef struct {
    VarId var_id;
    SymbolId spelling;
} ChainQueryVar;

typedef enum {
    CHAIN_POLICY_NATIVE = 0,
    CHAIN_POLICY_ATP_GUIDED_INHABITATION,
} ChainPolicy;

typedef struct {
    Arena *arena;
    Space *space;
    ChainIndex index;
    ChainPolicy policy;
    uint64_t fuel;
    bool fuel_limited;
    bool incomplete;
    bool more_possible;
    const char *incomplete_reason;
    ChainStats stats;
    Atom *root_goal;
    ChainQueryVar *query_vars;
    uint32_t query_var_count;
    uint32_t query_var_cap;
    VarId *scheme_vars;
    uint32_t scheme_var_count;
    uint32_t scheme_var_cap;
} ChainContext;

static bool chain_fuel_exhausted(const ChainContext *ctx) {
    return ctx->fuel_limited && ctx->fuel == 0;
}

static void chain_mark_incomplete(ChainContext *ctx, const char *reason) {
    if (ctx->incomplete) return;
    ctx->incomplete = true;
    ctx->incomplete_reason = reason;
}

static void chain_mark_normalization_incomplete(ChainContext *ctx,
                                                HeNormStatus status,
                                                const char *fallback) {
    switch (status) {
    case HE_NORM_RESOURCE:
        chain_mark_incomplete(
            ctx, chain_fuel_exhausted(ctx)
                     ? "fuel-exhausted"
                     : "type-computation-resource-exhausted");
        return;
    case HE_NORM_DEPTH:
        chain_mark_incomplete(ctx, "type-computation-depth-exhausted");
        return;
    case HE_NORM_AMBIGUOUS:
        chain_mark_incomplete(ctx, "type-computation-ambiguous");
        return;
    case HE_NORM_NO_RESULT:
        chain_mark_incomplete(ctx, "type-computation-no-result");
        return;
    case HE_NORM_INADMISSIBLE:
        chain_mark_incomplete(ctx, "type-computation-inadmissible");
        return;
    case HE_NORM_PROVISIONAL:
        chain_mark_incomplete(ctx, "type-computation-unadmitted");
        return;
    case HE_NORM_COMPLETE:
        break;
    }
    chain_mark_incomplete(ctx, fallback);
}

static bool chain_take_fuel(ChainContext *ctx, uint64_t amount) {
    if (!ctx->fuel_limited) return true;
    if (ctx->fuel < amount) {
        ctx->fuel = 0;
        chain_mark_incomplete(ctx, "fuel-exhausted");
        return false;
    }
    ctx->fuel -= amount;
    return true;
}

static void chain_index_init(ChainIndex *idx) {
    idx->decls = NULL;
    idx->refs = NULL;
    idx->rigid_fact_indices = NULL;
    idx->count = idx->cap = 0;
    idx->rigid_fact_count = 0;
}

static void chain_index_free(ChainIndex *idx) {
    free(idx->decls);
    free(idx->refs);
    free(idx->rigid_fact_indices);
    chain_index_init(idx);
}

static uint32_t chain_specificity(Atom *a) {
    if (!a) return 0;
    ChainAtomStack stack;
    chain_atom_stack_init(&stack);
    if (!chain_atom_stack_push(&stack, a)) return 0;
    uint32_t score = 0;
    while (stack.len > 0) {
        Atom *current = stack.items[--stack.len];
        if (!current || current->kind == ATOM_VAR) continue;
        if (score != UINT32_MAX) score++;
        if (current->kind != ATOM_EXPR) continue;
        for (CettaExprIndex i = current->expr.len; i > 0; i--)
            if (!chain_atom_stack_push(&stack, current->expr.elems[i - 1u])) {
                chain_atom_stack_free(&stack);
                return UINT32_MAX;
            }
    }
    chain_atom_stack_free(&stack);
    return score;
}

/* Symbol-count weight and structural distance are search guidance only.  They
 * never participate in unification, proof construction, or proof checking.
 * The weighting is deliberately small and deterministic: it is the typed-Horn
 * analogue of an ATP age/weight queue, not a claim to implement KBO/WPO. */
static uint32_t chain_atom_weight(Atom *a) {
    if (!a) return 0;
    ChainAtomStack stack;
    chain_atom_stack_init(&stack);
    if (!chain_atom_stack_push(&stack, a)) return UINT32_MAX;
    uint32_t weight = 0;
    while (stack.len > 0) {
        Atom *current = stack.items[--stack.len];
        if (weight != UINT32_MAX) weight++;
        if (!current || current->kind != ATOM_EXPR) continue;
        for (CettaExprIndex i = current->expr.len; i > 0; i--)
            if (!chain_atom_stack_push(&stack, current->expr.elems[i - 1u])) {
                chain_atom_stack_free(&stack);
                return UINT32_MAX;
            }
    }
    chain_atom_stack_free(&stack);
    return weight;
}

typedef struct {
    Atom *pattern;
    Atom *goal;
} ChainShapePair;

static uint64_t chain_u64_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint64_t chain_u64_mul_sat(uint64_t left, uint64_t right) {
    if (left == 0 || right == 0) return 0;
    return left > UINT64_MAX / right ? UINT64_MAX : left * right;
}

static uint64_t chain_shape_distance(Atom *pattern, Atom *goal) {
    ChainShapePair inline_items[32];
    ChainShapePair *items = inline_items;
    size_t len = 0, cap = sizeof inline_items / sizeof inline_items[0];
    items[len++] = (ChainShapePair){pattern, goal};
    uint64_t distance = 0;

    while (len > 0) {
        ChainShapePair pair = items[--len];
        pattern = pair.pattern;
        goal = pair.goal;
        if (!pattern || !goal) {
            distance = chain_u64_add_sat(distance, 16);
            continue;
        }
        if (pattern->kind == ATOM_VAR) {
            uint32_t goal_weight = chain_atom_weight(goal);
            distance = chain_u64_add_sat(
                distance, goal_weight == UINT32_MAX ? UINT64_MAX : goal_weight);
            continue;
        }
        if (goal->kind == ATOM_VAR) {
            distance = chain_u64_add_sat(distance, 1);
            continue;
        }
        if (pattern->kind != goal->kind) {
            distance = chain_u64_add_sat(distance, 16);
            continue;
        }
        if (pattern->kind != ATOM_EXPR) {
            if (!atom_eq(pattern, goal))
                distance = chain_u64_add_sat(distance, 8);
            continue;
        }
        CettaExprLen plen = pattern->expr.len;
        CettaExprLen glen = goal->expr.len;
        CettaExprLen shared = plen < glen ? plen : glen;
        uint64_t delta = plen > glen ? (uint64_t)(plen - glen)
                                     : (uint64_t)(glen - plen);
        distance = chain_u64_add_sat(distance,
                                     chain_u64_mul_sat(delta, 8));
        if (len + shared > cap) {
            size_t next_cap = cap;
            while (next_cap < len + shared) {
                if (next_cap > SIZE_MAX / 2u) {
                    distance = UINT64_MAX;
                    goto done;
                }
                next_cap *= 2u;
            }
            ChainShapePair *next = malloc(sizeof(*next) * next_cap);
            if (!next) {
                distance = UINT64_MAX;
                goto done;
            }
            memcpy(next, items, sizeof(*next) * len);
            if (items != inline_items) free(items);
            items = next;
            cap = next_cap;
        }
        for (CettaExprIndex i = 0; i < shared; i++)
            items[len++] = (ChainShapePair){pattern->expr.elems[i],
                                            goal->expr.elems[i]};
    }

done:
    if (items != inline_items) free(items);
    return distance;
}

static bool chain_index_push(ChainIndex *idx, ChainDecl d) {
    if (idx->count == idx->cap) {
        uint32_t ncap = idx->cap ? idx->cap * 2 : CHAIN_INITIAL_CAP;
        ChainDecl *next = realloc(idx->decls, sizeof(ChainDecl) * ncap);
        if (!next) return false;
        idx->decls = next;
        idx->cap = ncap;
    }
    idx->decls[idx->count++] = d;
    return true;
}

static int chain_ref_cmp(const void *pa, const void *pb) {
    const ChainRef *a = pa, *b = pb;
    if (a->head != b->head) return a->head < b->head ? -1 : 1;
    if (a->is_rule != b->is_rule) return a->is_rule ? 1 : -1;
    if (a->specificity != b->specificity)
        return a->specificity > b->specificity ? -1 : 1;
    return a->index < b->index ? -1 : a->index > b->index;
}

static bool chain_index_build(ChainContext *ctx) {
    ChainIndex *idx = &ctx->index;
    chain_index_init(idx);
    uint32_t len = 0;
    if (!space_length_u32_checked(ctx->space, &len)) return false;
    for (uint32_t i = 0; i < len; i++) {
        Atom *row = space_get_at(ctx->space, i);
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 3) continue;
        if (!atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.colon)) continue;
        Atom *term = row->expr.elems[1];
        Atom *type = row->expr.elems[2];
        bool rule = is_arrow(type) &&
                    space_has_unary_marker(ctx->space, "chaining-rule", term);
        if (is_arrow(type) && !rule) continue;
        bool scheme = rule ||
                      space_has_unary_marker(ctx->space, "type-scheme", term);
        /* Open declarations are rigid space facts unless explicitly promoted
           to a type scheme.  Keeping them out of the instantiable index avoids
           treating a free variable as an implicit forall. */
        if (!scheme && (atom_has_vars(term) || atom_has_vars(type))) continue;
        Atom *conclusion = rule ? type->expr.elems[type->expr.len - 1] : type;
        ChainDecl d = {
            .term = term,
            .type = type,
            .conclusion = conclusion,
            .conclusion_head = atom_head_symbol_id(conclusion),
            .arity = rule ? type->expr.len - 2 : 0,
            .specificity = chain_specificity(conclusion),
            .is_rule = rule,
            .is_scheme = scheme,
        };
        if (!chain_index_push(idx, d)) return false;
    }
    if (idx->count == 0) return true;
    for (uint32_t i = 0; i < idx->count; i++) {
        const ChainDecl *decl = &idx->decls[i];
        if (!decl->is_rule && !decl->is_scheme &&
            !atom_is_symbol(decl->type, "Type"))
            idx->rigid_fact_count++;
    }
    if (idx->rigid_fact_count > 0) {
        idx->rigid_fact_indices = malloc(
            sizeof(*idx->rigid_fact_indices) * idx->rigid_fact_count);
        if (!idx->rigid_fact_indices) return false;
        uint32_t fact_position = 0;
        for (uint32_t i = 0; i < idx->count; i++) {
            const ChainDecl *decl = &idx->decls[i];
            if (!decl->is_rule && !decl->is_scheme &&
                !atom_is_symbol(decl->type, "Type"))
                idx->rigid_fact_indices[fact_position++] = i;
        }
    }
    idx->refs = malloc(sizeof(ChainRef) * idx->count);
    if (!idx->refs) return false;
    for (uint32_t i = 0; i < idx->count; i++) {
        idx->refs[i] = (ChainRef){idx->decls[i].conclusion_head, i,
                                  idx->decls[i].specificity,
                                  idx->decls[i].is_rule};
    }
    qsort(idx->refs, idx->count, sizeof(ChainRef), chain_ref_cmp);
    return true;
}

static bool chain_query_var_add(ChainContext *ctx, Atom *var) {
    for (uint32_t i = 0; i < ctx->query_var_count; i++)
        if (ctx->query_vars[i].var_id == var->var_id) return true;
    if (ctx->query_var_count == ctx->query_var_cap) {
        uint32_t ncap = ctx->query_var_cap ? ctx->query_var_cap * 2u : 8u;
        ChainQueryVar *next = realloc(ctx->query_vars,
                                      sizeof(ChainQueryVar) * ncap);
        if (!next) {
            chain_mark_incomplete(ctx, "answer-identity-allocation");
            return false;
        }
        ctx->query_vars = next;
        ctx->query_var_cap = ncap;
    }
    ctx->query_vars[ctx->query_var_count++] =
        (ChainQueryVar){var->var_id, var->sym_id};
    return true;
}

static bool chain_collect_query_vars(ChainContext *ctx, Atom *atom) {
    if (!atom) return false;
    ChainAtomStack stack;
    chain_atom_stack_init(&stack);
    if (!chain_atom_stack_push(&stack, atom)) {
        chain_mark_incomplete(ctx, "answer-identity-allocation");
        return false;
    }
    while (stack.len > 0) {
        Atom *current = stack.items[--stack.len];
        if (current->kind == ATOM_VAR) {
            if (!chain_query_var_add(ctx, current)) {
                chain_atom_stack_free(&stack);
                return false;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR) continue;
        for (CettaExprIndex i = current->expr.len; i > 0; i--)
            if (!chain_atom_stack_push(&stack, current->expr.elems[i - 1u])) {
                chain_mark_incomplete(ctx, "answer-identity-allocation");
                chain_atom_stack_free(&stack);
                return false;
            }
    }
    chain_atom_stack_free(&stack);
    return true;
}

static bool chain_scheme_var_add(ChainContext *ctx, Atom *var,
                                 uint32_t epoch) {
    VarId fresh_id = var_epoch_id(var->var_id, epoch);
    for (uint32_t i = 0; i < ctx->scheme_var_count; i++)
        if (ctx->scheme_vars[i] == fresh_id) return true;
    if (ctx->scheme_var_count == ctx->scheme_var_cap) {
        uint32_t ncap = ctx->scheme_var_cap ? ctx->scheme_var_cap * 2u : 8u;
        VarId *next = realloc(ctx->scheme_vars, sizeof(VarId) * ncap);
        if (!next) {
            chain_mark_incomplete(ctx, "answer-identity-allocation");
            return false;
        }
        ctx->scheme_vars = next;
        ctx->scheme_var_cap = ncap;
    }
    ctx->scheme_vars[ctx->scheme_var_count++] = fresh_id;
    return true;
}

static bool chain_collect_scheme_vars(ChainContext *ctx, Atom *atom,
                                      uint32_t epoch) {
    if (!atom) return false;
    ChainAtomStack stack;
    chain_atom_stack_init(&stack);
    if (!chain_atom_stack_push(&stack, atom)) {
        chain_mark_incomplete(ctx, "answer-identity-allocation");
        return false;
    }
    while (stack.len > 0) {
        Atom *current = stack.items[--stack.len];
        if (current->kind == ATOM_VAR) {
            if (!chain_scheme_var_add(ctx, current, epoch)) {
                chain_atom_stack_free(&stack);
                return false;
            }
            continue;
        }
        if (current->kind != ATOM_EXPR) continue;
        for (CettaExprIndex i = current->expr.len; i > 0; i--)
            if (!chain_atom_stack_push(&stack, current->expr.elems[i - 1u])) {
                chain_mark_incomplete(ctx, "answer-identity-allocation");
                chain_atom_stack_free(&stack);
                return false;
            }
    }
    chain_atom_stack_free(&stack);
    return true;
}

typedef struct {
    VarId source;
    Atom *canonical;
} AnswerCanonicalVar;

typedef struct {
    AnswerCanonicalVar *items;
    uint32_t count;
    uint32_t cap;
} AnswerCanonicalMap;

static Atom *answer_canonical_var(ChainContext *ctx, AnswerCanonicalMap *map,
                                  Atom *var) {
    for (uint32_t i = 0; i < map->count; i++)
        if (map->items[i].source == var->var_id)
            return map->items[i].canonical;
    if (map->count == map->cap) {
        uint32_t ncap = map->cap ? map->cap * 2u : 8u;
        AnswerCanonicalVar *next = realloc(map->items,
                                            sizeof(AnswerCanonicalVar) * ncap);
        if (!next) {
            chain_mark_incomplete(ctx, "answer-identity-allocation");
            return NULL;
        }
        map->items = next;
        map->cap = ncap;
    }
    char name[40];
    snprintf(name, sizeof name, "answer-v%u", map->count);
    Atom *canonical = atom_var(ctx->arena, name);
    map->items[map->count++] =
        (AnswerCanonicalVar){var->var_id, canonical};
    return canonical;
}

typedef struct {
    Atom *input;
    Atom **children;
    uint32_t next_child;
    bool entered;
} AnswerCanonicalFrame;

typedef struct {
    AnswerCanonicalFrame *items;
    size_t len;
    size_t cap;
    AnswerCanonicalFrame inline_items[16];
} AnswerCanonicalStack;

static void answer_canonical_stack_init(AnswerCanonicalStack *stack) {
    stack->items = stack->inline_items;
    stack->len = 0;
    stack->cap = sizeof stack->inline_items / sizeof stack->inline_items[0];
}

static void answer_canonical_stack_free(AnswerCanonicalStack *stack) {
    if (stack->items != stack->inline_items) free(stack->items);
}

static bool answer_canonical_stack_push(AnswerCanonicalStack *stack,
                                        Atom *input) {
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap * 2u;
        if (next_cap <= stack->cap ||
            next_cap > SIZE_MAX / sizeof(*stack->items))
            return false;
        AnswerCanonicalFrame *next = cetta_malloc(
            sizeof(*stack->items) * next_cap);
        memcpy(next, stack->items, sizeof(*stack->items) * stack->len);
        if (stack->items != stack->inline_items) free(stack->items);
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = (AnswerCanonicalFrame){.input = input};
    return true;
}

static Atom *answer_canonicalize(ChainContext *ctx,
                                 AnswerCanonicalMap *map, Atom *atom) {
    if (!atom) return NULL;
    AnswerCanonicalStack stack;
    answer_canonical_stack_init(&stack);
    if (!answer_canonical_stack_push(&stack, atom)) {
        chain_mark_incomplete(ctx, "answer-identity-allocation");
        return NULL;
    }

    for (;;) {
        AnswerCanonicalFrame *frame = &stack.items[stack.len - 1u];
        Atom *completed = NULL;
        if (!frame->entered) {
            if (frame->input->kind == ATOM_VAR) {
                completed = answer_canonical_var(ctx, map, frame->input);
                if (!completed) goto fail;
                goto complete_frame;
            }
            if (frame->input->kind != ATOM_EXPR ||
                !atom_has_vars(frame->input)) {
                completed = frame->input;
                goto complete_frame;
            }
            frame->children = arena_alloc(
                ctx->arena,
                sizeof(Atom *) * (size_t)frame->input->expr.len);
            frame->entered = true;
        }
        if (frame->next_child < frame->input->expr.len) {
            if (!answer_canonical_stack_push(
                    &stack,
                    frame->input->expr.elems[frame->next_child])) {
                chain_mark_incomplete(ctx, "answer-identity-allocation");
                goto fail;
            }
            continue;
        }
        completed = atom_expr(ctx->arena, frame->children,
                              frame->input->expr.len);

complete_frame:
        stack.len--;
        if (stack.len == 0) {
            answer_canonical_stack_free(&stack);
            return completed;
        }
        frame = &stack.items[stack.len - 1u];
        frame->children[frame->next_child++] = completed;
    }

fail:
    answer_canonical_stack_free(&stack);
    return NULL;
}

static bool chain_is_scheme_var(const ChainContext *ctx, VarId id) {
    for (uint32_t i = 0; i < ctx->scheme_var_count; i++)
        if (ctx->scheme_vars[i] == id) return true;
    return false;
}

static Atom *answer_substitution_v2(ChainContext *ctx, const Bindings *env) {
    Atom **query = arena_alloc(
        ctx->arena, sizeof(Atom *) * (ctx->query_var_count + 1u));
    query[0] = he_sym(ctx->arena, "query-substitution-v1");
    for (uint32_t i = 0; i < ctx->query_var_count; i++) {
        Atom *var = atom_var_with_spelling(ctx->arena,
                                           ctx->query_vars[i].spelling,
                                           ctx->query_vars[i].var_id);
        Atom *value = bindings_apply_if_vars(env, ctx->arena, var);
        if (!value) {
            chain_mark_incomplete(ctx, "answer-substitution-failed");
            return NULL;
        }
        query[i + 1u] = atom_expr3(
            ctx->arena, he_sym(ctx->arena, "answer-binding-v1"), var, value);
    }

    uint32_t elaboration_count = 0;
    for (uint32_t i = 0; i < env->len; i++)
        if (chain_is_scheme_var(ctx, env->entries[i].var_id))
            elaboration_count++;
    Atom **elaboration = arena_alloc(
        ctx->arena, sizeof(Atom *) * (elaboration_count + 1u));
    elaboration[0] = he_sym(ctx->arena, "elaboration-substitution-v1");
    uint32_t out = 1;
    for (uint32_t i = 0; i < env->len; i++) {
        const Binding *entry = &env->entries[i];
        if (!chain_is_scheme_var(ctx, entry->var_id)) continue;
        Atom *var = atom_var_with_spelling(
            ctx->arena, entry->spelling, entry->var_id);
        Atom *value = bindings_apply_if_vars(env, ctx->arena, var);
        if (!value) {
            chain_mark_incomplete(ctx, "elaboration-substitution-failed");
            return NULL;
        }
        elaboration[out++] = atom_expr3(
            ctx->arena, he_sym(ctx->arena, "elaboration-binding-v1"), var,
            value);
    }

    Atom **constraints = arena_alloc(ctx->arena,
                                     sizeof(Atom *) * (env->eq_len + 1u));
    constraints[0] = he_sym(ctx->arena, "answer-constraints-v1");
    for (uint32_t i = 0; i < env->eq_len; i++) {
        Atom *left = bindings_apply_if_vars(env, ctx->arena,
                                            env->constraints[i].lhs);
        Atom *right = bindings_apply_if_vars(env, ctx->arena,
                                             env->constraints[i].rhs);
        if (!left || !right) {
            chain_mark_incomplete(ctx, "answer-substitution-failed");
            return NULL;
        }
        constraints[i + 1u] = atom_expr2(ctx->arena, left, right);
    }
    Atom *parts[4] = {
        he_sym(ctx->arena, "answer-substitution-v2"),
        atom_expr(ctx->arena, query, ctx->query_var_count + 1u),
        atom_expr(ctx->arena, elaboration, elaboration_count + 1u),
        atom_expr(ctx->arena, constraints, env->eq_len + 1u)};
    return atom_expr(ctx->arena, parts, 4);
}

static bool answer_identity_v2_make(ChainContext *ctx, Atom *proof,
                                    Atom *type, const Bindings *env,
                                    AnswerIdentityV2 *out) {
    if (bindings_has_loop(env)) return false;
    Atom *substituted_proof = bindings_apply_if_vars(env, ctx->arena, proof);
    Atom *substituted_type = bindings_apply_if_vars(env, ctx->arena, type);
    if (!substituted_proof || !substituted_type) {
        chain_mark_incomplete(ctx, "answer-substitution-failed");
        return false;
    }
    HeNormStatus status = HE_NORM_COMPLETE;
    substituted_type = normalize_type_checked(ctx->arena, ctx->space,
                                               substituted_type, &ctx->fuel,
                                               &status, 0, NULL);
    if (status != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, status, "answer-type-normalization-incomplete");
        return false;
    }
    Atom *substitution = answer_substitution_v2(ctx, env);
    if (!substitution) return false;
    Atom *raw_fields[4] = {he_sym(ctx->arena, "answer-identity-v2"),
                           substituted_proof, substitution, substituted_type};
    Atom *raw = atom_expr(ctx->arena, raw_fields, 4);
    AnswerCanonicalMap map = {0};
    Atom *key = answer_canonicalize(ctx, &map, raw);
    free(map.items);
    if (!key || key->kind != ATOM_EXPR || key->expr.len != 4u) return false;
    *out = (AnswerIdentityV2){
        .proof = key->expr.elems[1],
        .substitution = key->expr.elems[2],
        .type = key->expr.elems[3],
        .key = key,
    };
    return true;
}

static void chain_vec_init(ChainProofVec *v) {
    v->items = NULL; v->count = v->cap = 0;
}

static void chain_vec_free(ChainProofVec *v) {
    for (uint32_t i = 0; i < v->count; i++) bindings_free(&v->items[i].env);
    free(v->items);
    chain_vec_init(v);
}

static bool chain_vec_has_answer(const ChainProofVec *v,
                                 const AnswerIdentityV2 *identity) {
    for (uint32_t i = 0; i < v->count; i++)
        if (atom_alpha_eq(v->items[i].identity.key, identity->key)) return true;
    return false;
}

static bool chain_vec_push_move(ChainContext *ctx, ChainProofVec *v,
                                const AnswerIdentityV2 *identity,
                                Bindings *env) {
    if (chain_vec_has_answer(v, identity)) {
        bindings_free(env);
        bindings_init(env);
        return true;
    }
    if (v->count == v->cap) {
        uint32_t ncap = v->cap ? v->cap * 2 : CHAIN_INITIAL_CAP;
        ChainProof *next = realloc(v->items, sizeof(ChainProof) * ncap);
        if (!next) {
            chain_mark_incomplete(ctx, "allocation-failed");
            bindings_free(env);
            bindings_init(env);
            return false;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count++] = (ChainProof){*identity, *env};
    bindings_init(env);
    return true;
}

static bool chain_type_level_open_call(ChainContext *ctx, Atom *a) {
    return a && a->kind == ATOM_EXPR && a->expr.len > 0 && atom_has_vars(a) &&
           space_has_unary_marker(ctx->space, "type-level-function",
                                  a->expr.elems[0]);
}

typedef struct {
    Atom *left;
    Atom *right;
} ChainUnifyPair;

typedef struct {
    ChainUnifyPair *items;
    size_t len;
    size_t cap;
    ChainUnifyPair inline_items[16];
} ChainUnifyStack;

static void chain_unify_stack_init(ChainUnifyStack *stack) {
    stack->items = stack->inline_items;
    stack->len = 0;
    stack->cap = sizeof stack->inline_items / sizeof stack->inline_items[0];
}

static void chain_unify_stack_free(ChainUnifyStack *stack) {
    if (stack->items != stack->inline_items) free(stack->items);
}

static bool chain_unify_stack_push(ChainUnifyStack *stack,
                                   Atom *left, Atom *right) {
    if (stack->len == stack->cap) {
        size_t next_cap = stack->cap * 2u;
        if (next_cap <= stack->cap ||
            next_cap > SIZE_MAX / sizeof(*stack->items))
            return false;
        ChainUnifyPair *next = cetta_malloc(
            sizeof(*stack->items) * next_cap);
        memcpy(next, stack->items, sizeof(*stack->items) * stack->len);
        if (stack->items != stack->inline_items) free(stack->items);
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = (ChainUnifyPair){left, right};
    return true;
}

static bool chain_unify_shape(ChainContext *ctx, Atom *left, Atom *right,
                              Bindings *env) {
    ChainUnifyStack stack;
    chain_unify_stack_init(&stack);
    if (!chain_unify_stack_push(&stack, left, right)) {
        chain_mark_incomplete(ctx, "unification-worklist-allocation");
        return false;
    }

    while (stack.len > 0) {
        ChainUnifyPair pair = stack.items[--stack.len];
        if (!chain_take_fuel(ctx, 1)) {
            chain_unify_stack_free(&stack);
            return false;
        }
        left = deref(env, pair.left);
        right = deref(env, pair.right);
        if (atom_eq(left, right)) continue;
        if (left->kind == ATOM_VAR) {
            if (!bindings_add_id_acyclic(
                    env, left->var_id, left->sym_id, right)) {
                chain_unify_stack_free(&stack);
                return false;
            }
            continue;
        }
        if (right->kind == ATOM_VAR) {
            if (!bindings_add_id_acyclic(
                    env, right->var_id, right->sym_id, left)) {
                chain_unify_stack_free(&stack);
                return false;
            }
            continue;
        }
        if (chain_type_level_open_call(ctx, left) ||
            chain_type_level_open_call(ctx, right))
            continue; /* checked after premise instantiation */
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len) {
            chain_unify_stack_free(&stack);
            return false;
        }
        for (CettaExprIndex i = left->expr.len; i > 0; i--)
            if (!chain_unify_stack_push(
                    &stack, left->expr.elems[i - 1u],
                    right->expr.elems[i - 1u])) {
                chain_mark_incomplete(ctx, "unification-worklist-allocation");
                chain_unify_stack_free(&stack);
                return false;
            }
    }
    chain_unify_stack_free(&stack);
    return true;
}

static bool chain_unify_deferred(ChainContext *ctx, Atom *left, Atom *right,
                                 Bindings *env) {
    left = bindings_apply_if_vars(env, ctx->arena, left);
    right = bindings_apply_if_vars(env, ctx->arena, right);
    HeNormStatus ls = HE_NORM_COMPLETE, rs = HE_NORM_COMPLETE;
    left = normalize_type_checked(ctx->arena, ctx->space, left, &ctx->fuel, &ls,
                                  0, NULL);
    right = normalize_type_checked(ctx->arena, ctx->space, right, &ctx->fuel,
                                   &rs, 0, NULL);
    if (ls != HE_NORM_COMPLETE || rs != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, ls != HE_NORM_COMPLETE ? ls : rs,
            "type-normalization-incomplete");
        return false;
    }
    return chain_unify_shape(ctx, left, right, env);
}

static bool chain_unify_must(ChainContext *ctx, Atom *left, Atom *right,
                             Bindings *env, Atom **resolved_left) {
    left = bindings_apply_if_vars(env, ctx->arena, left);
    right = bindings_apply_if_vars(env, ctx->arena, right);
    HeNormStatus ls = HE_NORM_COMPLETE, rs = HE_NORM_COMPLETE;
    left = normalize_type_checked(ctx->arena, ctx->space, left, &ctx->fuel, &ls,
                                  0, NULL);
    right = normalize_type_checked(ctx->arena, ctx->space, right, &ctx->fuel,
                                   &rs, 0, NULL);
    if (ls != HE_NORM_COMPLETE || rs != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, ls != HE_NORM_COMPLETE ? ls : rs,
            "type-normalization-incomplete");
        return false;
    }
    Atom *detail = NULL;
    HeTypeValidity lv = check_type_refinements(ctx->arena, ctx->space, left,
                                               &ctx->fuel, &detail, 0, NULL);
    HeTypeValidity rv = check_type_refinements(ctx->arena, ctx->space, right,
                                               &ctx->fuel, &detail, 0, NULL);
    if (lv == HE_TYPE_VALIDATION_INCOMPLETE ||
        rv == HE_TYPE_VALIDATION_INCOMPLETE) {
        chain_mark_incomplete(ctx, chain_fuel_exhausted(ctx)
                                  ? "fuel-exhausted"
                                  : "type-refinement-incomplete");
        return false;
    }
    if (lv == HE_TYPE_VALIDATION_UNKNOWN || rv == HE_TYPE_VALIDATION_UNKNOWN) {
        chain_mark_incomplete(
            ctx, chain_fuel_exhausted(ctx) ? "fuel-exhausted"
                                           : "type-refinement-unknown");
        return false;
    }
    if (lv == HE_TYPE_INVALID || rv == HE_TYPE_INVALID) return false;
    HeEdge e = consistency_bind(left, right, &ctx->fuel, env);
    if (e == HE_UNKNOWN) {
        chain_mark_incomplete(
            ctx, chain_fuel_exhausted(ctx) ? "fuel-exhausted"
                                           : "type-unification-exhausted");
        return false;
    }
    /* Gradual acceptances (dynamic/top/meta anywhere in the composition) are
     * fine for checking but are NOT proofs; only structural equality all the
     * way down lets a candidate stand as an inhabitant. */
    if (!edge_is_exact_or_structural(e)) return false;
    if (resolved_left) {
        Atom *r = bindings_apply_if_vars(env, ctx->arena, left);
        HeNormStatus ns = HE_NORM_COMPLETE;
        r = normalize_type_checked(ctx->arena, ctx->space, r, &ctx->fuel, &ns,
                                   0, NULL);
        if (ns != HE_NORM_COMPLETE) {
            chain_mark_normalization_incomplete(
                ctx, ns, "resolved-type-normalization-incomplete");
            return false;
        }
        *resolved_left = r;
    }
    return true;
}

static bool chain_proof_checked(ChainContext *ctx, Atom *proof, Atom *goal,
                                Bindings *env, Atom **checked_type) {
    ctx->stats.proof_checks++;
    if (bindings_has_loop(env)) return false;
    HeTypeSet ts;
    set_init(&ts, ctx->arena);
    if (!infer_shared_types(ctx->arena, ctx->space, proof, &ctx->fuel, &ts)) {
        chain_mark_incomplete(
            ctx, chain_fuel_exhausted(ctx) ? "fuel-exhausted"
                                           : "proof-type-inference-exhausted");
        return false;
    }
    if (ts.overflow) {
        chain_mark_incomplete(ctx, "proof-type-set-capacity");
        return false;
    }
    for (uint32_t i = 0; i < ts.count; i++) {
        Bindings trial;
        if (!bindings_clone(&trial, env)) {
            chain_mark_incomplete(ctx, "proof-check-binding-allocation");
            return false;
        }
        Atom *resolved = NULL;
        if (chain_unify_must(ctx, ts.items[i], goal, &trial, &resolved) &&
            !bindings_has_loop(&trial)) {
            bindings_replace(env, &trial);
            if (checked_type) *checked_type = resolved;
            return true;
        }
        bindings_free(&trial);
    }
    return false;
}

static bool chain_guidance_premise_supported(ChainContext *ctx,
                                             Atom *premise,
                                             const Bindings *env);

static int chain_premise_score(ChainContext *ctx, Atom *premise,
                               const Bindings *env) {
    premise = bindings_apply_if_vars(env, ctx->arena, premise);
    int score = atom_has_vars(premise) ? 0 : 10000;
    if (ctx->policy == CHAIN_POLICY_ATP_GUIDED_INHABITATION) {
        /* Fail first: a premise already discharged by a rigid fact is less
         * constraining than an unsupported sibling.  Testing the latter first
         * prevents an open easy premise from multiplying continuations before
         * a contradictory premise is examined. */
        if (chain_guidance_premise_supported(ctx, premise, env))
            score -= 1000000;
        uint32_t weight = chain_atom_weight(premise);
        if (weight < 10000u) score += 10000 - (int)weight;
    }
    uint32_t specificity = chain_specificity(premise);
    if (specificity > (uint32_t)(INT_MAX / 8) ||
        score > INT_MAX - (int)specificity * 8)
        score = INT_MAX;
    else
        score += (int)specificity * 8;
    SymbolId h = atom_head_symbol_id(premise);
    if (h != SYMBOL_ID_NONE) {
        uint32_t matches = 0;
        for (uint32_t i = 0; i < ctx->index.count; i++)
            if (ctx->index.decls[i].conclusion_head == h) matches++;
        if (matches < 1000) score += 1000 - (int)matches;
    }
    return score;
}

static uint32_t chain_ref_lower_bound(const ChainIndex *idx, SymbolId head) {
    uint32_t lo = 0, hi = idx->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (idx->refs[mid].head < head) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static uint32_t chain_ref_upper_bound(const ChainIndex *idx, SymbolId head) {
    uint32_t lo = 0, hi = idx->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (idx->refs[mid].head <= head) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

typedef struct { uint32_t lo, hi; } ChainRefRange;

static uint32_t chain_candidate_ranges(ChainContext *ctx, SymbolId head,
                                       ChainRefRange ranges[2]) {
    if (head == SYMBOL_ID_NONE) {
        ranges[0] = (ChainRefRange){0, ctx->index.count};
        return 1;
    }
    uint32_t n = 0;
    uint32_t lo = chain_ref_lower_bound(&ctx->index, head);
    uint32_t hi = chain_ref_upper_bound(&ctx->index, head);
    if (lo < hi) ranges[n++] = (ChainRefRange){lo, hi};
    if (head != SYMBOL_ID_NONE) {
        lo = chain_ref_lower_bound(&ctx->index, SYMBOL_ID_NONE);
        hi = chain_ref_upper_bound(&ctx->index, SYMBOL_ID_NONE);
        if (lo < hi) ranges[n++] = (ChainRefRange){lo, hi};
    }
    return n;
}

typedef struct ChainContinuation ChainContinuation;

struct ChainContinuation {
    Atom *rule_term;
    Atom *goal;
    Atom **premises;
    Atom **premise_binders;
    Atom **args;
    bool *done;
    uint32_t arity;
    uint32_t solved;
    uint32_t depth;
    uint32_t rank;
    uint64_t guidance;
    uint32_t pick;
    ChainContinuation *parent;
};

typedef enum {
    CHAIN_WORK_CANDIDATE = 0,
    CHAIN_WORK_RESULT,
    CHAIN_WORK_CHOICE,
} ChainWorkKind;

typedef struct {
    uint32_t decl_index;
    uint64_t cost;
} ChainChoiceCandidate;

typedef struct {
    ChainChoiceCandidate *items;
    uint32_t count;
    uint32_t position;
    uint64_t base_guidance;
} ChainChoice;

typedef struct {
    ChainWorkKind kind;
    uint32_t decl_index;
    Atom *goal;
    uint32_t depth;
    Atom *proof;
    Atom *type;
    Bindings env;
    ChainContinuation *continuation;
    ChainChoice *choice;
    uint32_t rank;
    uint64_t guidance;
    bool guided;
    uint8_t priority;
    uint64_t serial;
} ChainWorkItem;

typedef struct {
    ChainWorkItem *items;
    uint32_t len;
    uint32_t cap;
    uint64_t next_serial;
    uint64_t guided_pops;
} ChainWorkQueue;

static void chain_queue_init(ChainWorkQueue *queue) {
    queue->items = NULL;
    queue->len = queue->cap = 0;
    queue->next_serial = 0;
    queue->guided_pops = 0;
}

static void chain_queue_free(ChainWorkQueue *queue) {
    for (uint32_t i = 0; i < queue->len; i++)
        bindings_free(&queue->items[i].env);
    free(queue->items);
    chain_queue_init(queue);
}

static bool chain_queue_grow(ChainContext *ctx, ChainWorkQueue *queue) {
    uint32_t ncap = queue->cap ? queue->cap * 2u : CHAIN_INITIAL_CAP;
    ChainWorkItem *next = malloc(sizeof(ChainWorkItem) * ncap);
    if (!next) {
        chain_mark_incomplete(ctx, "search-queue-allocation");
        return false;
    }
    for (uint32_t i = 0; i < queue->len; i++) next[i] = queue->items[i];
    free(queue->items);
    queue->items = next;
    queue->cap = ncap;
    return true;
}

static bool chain_work_before(const ChainWorkItem *left,
                              const ChainWorkItem *right) {
    if (left->guided || right->guided) {
        if (left->guided != right->guided) return left->guided;
        /* A checked subgoal result is administrative continuation work, not a
         * new search alternative.  Propagate it eagerly before comparing the
         * heuristic costs of genuine candidates; otherwise a solved premise
         * can sit behind thousands of cheaper sibling choices. */
        bool left_result = left->kind == CHAIN_WORK_RESULT;
        bool right_result = right->kind == CHAIN_WORK_RESULT;
        if (left_result != right_result) return left_result;
        if (left->guidance != right->guidance)
            return left->guidance < right->guidance;
        if (left->priority != right->priority)
            return left->priority < right->priority;
        if (left->rank != right->rank) return left->rank < right->rank;
        return left->serial < right->serial;
    }
    if (left->rank != right->rank) return left->rank < right->rank;
    if (left->priority != right->priority)
        return left->priority < right->priority;
    return left->serial < right->serial;
}

static int chain_choice_candidate_cmp(const void *left_raw,
                                      const void *right_raw) {
    const ChainChoiceCandidate *left = left_raw;
    const ChainChoiceCandidate *right = right_raw;
    if (left->cost != right->cost) return left->cost < right->cost ? -1 : 1;
    if (left->decl_index != right->decl_index)
        return left->decl_index > right->decl_index ? -1 : 1;
    return 0;
}

static bool chain_queue_push_move(ChainContext *ctx, ChainWorkQueue *queue,
                                  ChainWorkItem *item) {
    if (queue->len == queue->cap && !chain_queue_grow(ctx, queue)) return false;
    item->serial = queue->next_serial++;
    uint32_t pos = queue->len++;
    queue->items[pos] = *item;
    bindings_init(&item->env);
    while (pos > 0) {
        uint32_t parent = (pos - 1u) / 2u;
        if (!chain_work_before(&queue->items[pos], &queue->items[parent]))
            break;
        ChainWorkItem swap = queue->items[parent];
        queue->items[parent] = queue->items[pos];
        queue->items[pos] = swap;
        pos = parent;
    }
    return true;
}

static bool chain_queue_pop(ChainContext *ctx, ChainWorkQueue *queue,
                            ChainWorkItem *out) {
    if (queue->len == 0) return false;
    uint32_t selected = 0;
    if (queue->items[0].guided) {
        /* The global agenda, rather than each rule list, implements the classic
         * age/weight mixture.  Five best-first selections are followed by one
         * oldest-work-item selection; actual structural costs remain intact. */
        bool age_turn = queue->guided_pops % 6u == 5u;
        queue->guided_pops++;
        if (age_turn) {
            uint64_t oldest = queue->items[0].serial;
            for (uint32_t i = 1; i < queue->len; i++) {
                if (queue->items[i].serial < oldest) {
                    oldest = queue->items[i].serial;
                    selected = i;
                }
            }
            ctx->stats.agenda_age_pops++;
        } else {
            ctx->stats.agenda_weight_pops++;
        }
    }
    *out = queue->items[selected];
    queue->len--;
    if (selected < queue->len) {
        queue->items[selected] = queue->items[queue->len];
        uint32_t pos = selected;
        if (pos > 0) {
            uint32_t parent = (pos - 1u) / 2u;
            while (pos > 0 &&
                   chain_work_before(&queue->items[pos],
                                     &queue->items[parent])) {
                ChainWorkItem swap = queue->items[parent];
                queue->items[parent] = queue->items[pos];
                queue->items[pos] = swap;
                pos = parent;
                parent = (pos - 1u) / 2u;
            }
        }
        for (;;) {
            uint32_t left = pos * 2u + 1u;
            uint32_t right = left + 1u;
            if (left >= queue->len) break;
            uint32_t best = left;
            if (right < queue->len &&
                chain_work_before(&queue->items[right],
                                  &queue->items[left]))
                best = right;
            if (!chain_work_before(&queue->items[best],
                                   &queue->items[pos]))
                break;
            ChainWorkItem swap = queue->items[pos];
            queue->items[pos] = queue->items[best];
            queue->items[best] = swap;
            pos = best;
        }
    }
    return true;
}

/* A cheap exact-support probe for the explicit ATP-guided lane.  It is
 * guidance only: declarations used as rigid facts are still checked by the
 * ordinary candidate path, and the completed proof still crosses check-type.
 *
 * Each direction greedily joins instantiated rule premises against closed,
 * non-schematic facts under one shared substitution.  Trying both source
 * directions avoids baking premise order into the observation while keeping
 * the probe bounded by arity * indexed facts. */
static bool chain_guidance_unify_shape(ChainContext *ctx, Atom *left,
                                       Atom *right, Bindings *env) {
    ChainContext probe = *ctx;
    probe.fuel = 0;
    probe.fuel_limited = false;
    probe.incomplete = false;
    probe.incomplete_reason = NULL;
    return chain_unify_shape(&probe, left, right, env);
}

static bool chain_guidance_match_rigid_fact(ChainContext *ctx, Atom *premise,
                                             Bindings *env, bool reverse) {
    for (uint32_t offset = 0; offset < ctx->index.rigid_fact_count; offset++) {
        uint32_t position = reverse
            ? ctx->index.rigid_fact_count - 1u - offset : offset;
        uint32_t i = ctx->index.rigid_fact_indices[position];
        const ChainDecl *fact = &ctx->index.decls[i];
        Bindings trial;
        bindings_init(&trial);
        if (!bindings_clone(&trial, env)) return false;
        if (chain_guidance_unify_shape(ctx, premise, fact->type, &trial)) {
            bindings_replace(env, &trial);
            return true;
        }
        bindings_free(&trial);
    }
    return false;
}

static bool chain_guidance_premise_supported(ChainContext *ctx,
                                             Atom *premise,
                                             const Bindings *env) {
    Bindings trial;
    bindings_init(&trial);
    if (!bindings_clone(&trial, env)) return false;
    bool supported = chain_guidance_match_rigid_fact(
        ctx, premise, &trial, true);
    bindings_free(&trial);
    return supported;
}

static uint32_t chain_candidate_support_direction(ChainContext *ctx,
                                                  const ChainDecl *decl,
                                                  Atom *goal,
                                                  bool reverse) {
    if (!decl->is_rule || decl->arity == 0) return 0;
    Bindings env;
    bindings_init(&env);
    if (!chain_guidance_unify_shape(ctx, decl->conclusion, goal, &env)) {
        bindings_free(&env);
        return 0;
    }
    uint32_t supported = 0;
    for (uint32_t offset = 0; offset < decl->arity; offset++) {
        uint32_t premise_index = reverse
            ? decl->type->expr.len - 2u - offset
            : offset + 1u;
        Atom *binder = NULL, *premise = NULL;
        split_binder(decl->type->expr.elems[premise_index],
                     &binder, &premise);
        (void)binder;
        if (chain_guidance_match_rigid_fact(ctx, premise, &env, reverse))
            supported++;
    }
    bindings_free(&env);
    return supported;
}

static uint32_t chain_candidate_direct_support(ChainContext *ctx,
                                               const ChainDecl *decl,
                                               Atom *goal) {
    uint32_t forward = chain_candidate_support_direction(
        ctx, decl, goal, false);
    uint32_t reverse = chain_candidate_support_direction(
        ctx, decl, goal, true);
    return forward > reverse ? forward : reverse;
}

static ChainContinuation *chain_continuation_clone(ChainContext *ctx,
                                                    ChainContinuation *src) {
    ChainContinuation *dst = arena_alloc(ctx->arena, sizeof *dst);
    *dst = *src;
    if (src->arity == 0) return dst;
    dst->premises = arena_alloc(ctx->arena, sizeof(Atom *) * src->arity);
    dst->premise_binders = arena_alloc(ctx->arena,
                                       sizeof(Atom *) * src->arity);
    dst->args = arena_alloc(ctx->arena, sizeof(Atom *) * src->arity);
    dst->done = arena_alloc(ctx->arena, sizeof(bool) * src->arity);
    memcpy(dst->premises, src->premises, sizeof(Atom *) * src->arity);
    memcpy(dst->premise_binders, src->premise_binders,
           sizeof(Atom *) * src->arity);
    memcpy(dst->args, src->args, sizeof(Atom *) * src->arity);
    memcpy(dst->done, src->done, sizeof(bool) * src->arity);
    return dst;
}

static uint64_t chain_candidate_guidance(ChainContext *ctx,
                                         uint32_t decl_index, Atom *goal) {
    const ChainDecl *decl = &ctx->index.decls[decl_index];
    uint64_t distance = chain_shape_distance(decl->conclusion, goal);
    uint32_t conclusion_weight = chain_atom_weight(decl->conclusion);
    uint32_t goal_weight = chain_atom_weight(goal);
    uint64_t weight_delta = conclusion_weight > goal_weight
        ? (uint64_t)(conclusion_weight - goal_weight)
        : (uint64_t)(goal_weight - conclusion_weight);
    uint64_t premise_weight = 0;
    if (decl->is_rule && decl->type->kind == ATOM_EXPR) {
        for (uint32_t i = 1; i + 1u < decl->type->expr.len; i++) {
            Atom *binder = NULL, *premise = NULL;
            split_binder(decl->type->expr.elems[i], &binder, &premise);
            (void)binder;
            premise_weight = chain_u64_add_sat(
                premise_weight, chain_atom_weight(premise));
        }
    }
    uint64_t age = ctx->index.count - 1u - decl_index;
    uint64_t cost = chain_u64_mul_sat(distance, 4096u);
    cost = chain_u64_add_sat(cost,
                             chain_u64_mul_sat(weight_delta, 128u));
    cost = chain_u64_add_sat(cost,
                             chain_u64_mul_sat(decl->arity, 256u));
    cost = chain_u64_add_sat(cost,
                             chain_u64_mul_sat(premise_weight, 8u));
    cost = chain_u64_add_sat(cost, age);
    if (decl->is_rule) {
        uint32_t support = chain_candidate_direct_support(ctx, decl, goal);
        if (support > 4u) support = 4u;
        /* Four bounded support tiers keep theorem facts competitive while
         * making each additional joined premise a visible 4096-point gain. */
        cost = chain_u64_add_sat(
            cost, chain_u64_mul_sat(4u - support, 4096u));
        cost = chain_u64_add_sat(cost, 128u);
    }
    return cost;
}

static bool chain_schedule_goal_guided(ChainContext *ctx,
                                       ChainWorkQueue *queue, Atom *goal,
                                       uint32_t depth, const Bindings *env,
                                       ChainContinuation *continuation,
                                       uint32_t rank, uint64_t guidance,
                                       bool include_facts,
                                       bool include_rules,
                                       ChainRefRange ranges[2],
                                       uint32_t nranges,
                                       uint32_t *considered_out) {
    ChainChoiceCandidate *candidates = arena_alloc(
        ctx->arena, sizeof(*candidates) * (ctx->index.count ?
                                           ctx->index.count : 1u));
    uint32_t count = 0, considered = 0;
    for (uint32_t pass = 0; pass < 2u; pass++) {
        bool want_rule = pass == 1u;
        if ((want_rule && (!include_rules || depth == 0)) ||
            (!want_rule && !include_facts))
            continue;
        for (uint32_t rg = 0; rg < nranges; rg++) {
            for (uint32_t ri = ranges[rg].lo; ri < ranges[rg].hi; ri++) {
                ChainRef *ref = &ctx->index.refs[ri];
                if (ref->is_rule != want_rule) continue;
                considered++;
                candidates[count++] = (ChainChoiceCandidate){
                    .decl_index = ref->index,
                    .cost = chain_candidate_guidance(ctx, ref->index, goal),
                };
            }
        }
    }
    if (considered_out) *considered_out = considered;
    if (count == 0) return true;
    qsort(candidates, count, sizeof(*candidates),
          chain_choice_candidate_cmp);

    ChainChoice *choice = arena_alloc(ctx->arena, sizeof(*choice));
    *choice = (ChainChoice){
        .items = candidates,
        .count = count,
        .position = 0,
        .base_guidance = guidance,
    };
    const ChainDecl *first =
        &ctx->index.decls[candidates[0].decl_index];
    ChainWorkItem item = {0};
    item.kind = CHAIN_WORK_CHOICE;
    item.goal = goal;
    item.depth = depth;
    item.continuation = continuation;
    item.choice = choice;
    item.rank = rank;
    item.guidance = chain_u64_add_sat(guidance, candidates[0].cost);
    item.guided = true;
    item.priority = first->is_rule ? 2u : 1u;
    bindings_init(&item.env);
    if (!bindings_clone(&item.env, env) ||
        !chain_queue_push_move(ctx, queue, &item)) {
        bindings_free(&item.env);
        if (!ctx->incomplete)
            chain_mark_incomplete(ctx, "search-binding-allocation");
        return false;
    }
    return true;
}

static bool chain_schedule_goal(ChainContext *ctx, ChainWorkQueue *queue,
                                Atom *goal, uint32_t depth,
                                const Bindings *env,
                                ChainContinuation *continuation,
                                uint32_t rank, uint64_t guidance,
                                bool include_facts, bool include_rules) {
    ctx->stats.calls++;
    if (!chain_take_fuel(ctx, 1)) return false;
    goal = bindings_apply_if_vars(env, ctx->arena, goal);
    HeNormStatus status = HE_NORM_COMPLETE;
    goal = normalize_type_checked(ctx->arena, ctx->space, goal, &ctx->fuel,
                                  &status, 0, NULL);
    if (status != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, status, "goal-normalization-incomplete");
        return false;
    }

    /* Variant ancestor regularity is producer-side pruning, not authority.
     * A branch that asks for the same instantiated goal already active on
     * that branch can only repeat the intervening derivation.  Restrict the
     * check to alpha variants: mere unifiability or subsumption would be a
     * stronger—and potentially incomplete—cut. */
    if (ctx->policy == CHAIN_POLICY_ATP_GUIDED_INHABITATION) {
        for (ChainContinuation *ancestor = continuation;
             ancestor; ancestor = ancestor->parent) {
            Atom *ancestor_goal = bindings_apply_if_vars(
                env, ctx->arena, ancestor->goal);
            if (atom_alpha_eq(goal, ancestor_goal)) {
                ctx->stats.ancestor_pruned++;
                return true;
            }
        }
    }

    ChainRefRange ranges[2];
    uint32_t nranges = chain_candidate_ranges(ctx, atom_head_symbol_id(goal),
                                               ranges);
    uint32_t considered = 0;
    if (ctx->policy == CHAIN_POLICY_ATP_GUIDED_INHABITATION) {
        bool ok = chain_schedule_goal_guided(
            ctx, queue, goal, depth, env, continuation, rank, guidance,
            include_facts, include_rules, ranges, nranges, &considered);
        if (considered < ctx->index.count)
            ctx->stats.head_pruned += ctx->index.count - considered;
        return ok;
    }
    for (uint32_t pass = 0; pass < 2u; pass++) {
        bool want_rule = pass == 1u;
        if ((want_rule && (!include_rules || depth == 0)) ||
            (!want_rule && !include_facts))
            continue;
        for (uint32_t rg = 0; rg < nranges; rg++) {
            for (uint32_t ri = ranges[rg].lo; ri < ranges[rg].hi; ri++) {
                ChainRef *ref = &ctx->index.refs[ri];
                if (ref->is_rule != want_rule) continue;
                considered++;
                ChainWorkItem item = {0};
                item.kind = CHAIN_WORK_CANDIDATE;
                item.decl_index = ref->index;
                item.goal = goal;
                item.depth = depth;
                item.continuation = continuation;
                item.rank = rank;
                item.guidance = guidance;
                item.priority = want_rule ? 2u : 1u;
                bindings_init(&item.env);
                if (!bindings_clone(&item.env, env) ||
                    !chain_queue_push_move(ctx, queue, &item)) {
                    bindings_free(&item.env);
                    if (!ctx->incomplete) {
                        chain_mark_incomplete(ctx, "search-binding-allocation");
                    }
                    return false;
                }
                /* Every matching candidate is a retained continuation. */
            }
        }
    }
    if (considered < ctx->index.count)
        ctx->stats.head_pruned += ctx->index.count - considered;
    return true;
}

static bool chain_schedule_result(ChainContext *ctx, ChainWorkQueue *queue,
                                  Atom *proof, Atom *type, Bindings *env,
                                  ChainContinuation *continuation,
                                  uint32_t rank, uint64_t guidance) {
    ChainWorkItem item = {0};
    item.kind = CHAIN_WORK_RESULT;
    item.proof = proof;
    item.type = type;
    item.continuation = continuation;
    item.rank = rank;
    item.guidance = guidance;
    item.guided = ctx->policy == CHAIN_POLICY_ATP_GUIDED_INHABITATION;
    item.priority = 0u;
    item.env = *env;
    bindings_init(env);
    if (chain_queue_push_move(ctx, queue, &item)) return true;
    bindings_free(&item.env);
    return false;
}

static uint32_t chain_pick_premise(ChainContext *ctx,
                                   ChainContinuation *continuation,
                                   const Bindings *env) {
    uint32_t pick = UINT32_MAX;
    int best = -2147483647;
    for (uint32_t i = 0; i < continuation->arity; i++) {
        if (continuation->done[i]) continue;
        int score = chain_premise_score(ctx, continuation->premises[i], env);
        if (score > best) {
            best = score;
            pick = i;
        }
    }
    return pick;
}

static bool chain_schedule_next_premise(ChainContext *ctx,
                                        ChainWorkQueue *queue,
                                        ChainContinuation *continuation,
                                        const Bindings *env) {
    uint32_t pick = chain_pick_premise(ctx, continuation, env);
    if (pick == UINT32_MAX) return false;
    continuation->pick = pick;
    return chain_schedule_goal(ctx, queue, continuation->premises[pick],
                               continuation->depth, env, continuation,
                               continuation->rank, continuation->guidance,
                               true, true);
}

static bool chain_finish_continuation(ChainContext *ctx,
                                      ChainWorkQueue *queue,
                                      ChainContinuation *continuation,
                                      Bindings *env) {
    Atom **elems = arena_alloc(ctx->arena,
                               sizeof(Atom *) * (continuation->arity + 1u));
    elems[0] = continuation->rule_term;
    for (uint32_t i = 0; i < continuation->arity; i++)
        elems[i + 1u] = continuation->args[i];
    Atom *proof = atom_expr(ctx->arena, elems, continuation->arity + 1u);
    proof = bindings_apply_if_vars(env, ctx->arena, proof);
    Atom *checked_type = NULL;
    if (!chain_proof_checked(ctx, proof, continuation->goal, env,
                             &checked_type))
        return false;
    proof = bindings_apply_if_vars(env, ctx->arena, proof);
    ctx->stats.continuations++;
    return chain_schedule_result(ctx, queue, proof, checked_type, env,
                                 continuation->parent, continuation->rank,
                                 continuation->guidance);
}

static void chain_process_candidate(ChainContext *ctx, ChainWorkQueue *queue,
                                    ChainWorkItem *item) {
    const ChainDecl *decl = &ctx->index.decls[item->decl_index];
    uint32_t epoch = fresh_var_suffix();
    if (!decl->is_rule) {
        ctx->stats.fact_candidates++;
        if (decl->is_scheme &&
            (!chain_collect_scheme_vars(ctx, decl->term, epoch) ||
             !chain_collect_scheme_vars(ctx, decl->type, epoch)))
            return;
        Atom *term = decl->is_scheme
            ? atom_freshen_epoch(ctx->arena, decl->term, epoch)
            : decl->term;
        Atom *type = decl->is_scheme
            ? atom_freshen_epoch(ctx->arena, decl->type, epoch)
            : decl->type;
        Atom *resolved = NULL;
        if (!chain_unify_must(ctx, type, item->goal, &item->env, &resolved))
            return;
        term = bindings_apply_if_vars(&item->env, ctx->arena, term);
        (void)chain_schedule_result(ctx, queue, term, resolved, &item->env,
                                    item->continuation, item->rank,
                                    item->guidance);
        return;
    }

    if (item->depth == 0 || !chain_take_fuel(ctx, 1)) return;
    ctx->stats.rule_candidates++;
    if (!chain_collect_scheme_vars(ctx, decl->term, epoch) ||
        !chain_collect_scheme_vars(ctx, decl->type, epoch))
        return;
    Atom *rule_term = atom_freshen_epoch(ctx->arena, decl->term, epoch);
    Atom *rule_type = atom_freshen_epoch(ctx->arena, decl->type, epoch);
    uint32_t arity = rule_type->expr.len - 2u;
    Atom *conclusion = rule_type->expr.elems[rule_type->expr.len - 1u];
    if (!chain_unify_deferred(ctx, conclusion, item->goal, &item->env)) return;

    ChainContinuation *continuation =
        arena_alloc(ctx->arena, sizeof *continuation);
    *continuation = (ChainContinuation){
        .rule_term = rule_term,
        .goal = item->goal,
        .arity = arity,
        .solved = 0,
        .depth = item->depth - 1u,
        .rank = item->rank == UINT32_MAX ? UINT32_MAX : item->rank + 1u,
        .guidance = item->guidance,
        .pick = UINT32_MAX,
        .parent = item->continuation,
    };
    if (arity == 0) {
        (void)chain_finish_continuation(ctx, queue, continuation, &item->env);
        return;
    }
    continuation->premises = arena_alloc(ctx->arena,
                                         sizeof(Atom *) * arity);
    continuation->premise_binders = arena_alloc(ctx->arena,
                                                sizeof(Atom *) * arity);
    continuation->args = arena_alloc(ctx->arena, sizeof(Atom *) * arity);
    continuation->done = arena_alloc(ctx->arena, sizeof(bool) * arity);
    for (uint32_t i = 0; i < arity; i++) {
        Atom *binder = NULL, *premise = NULL;
        split_binder(rule_type->expr.elems[i + 1u], &binder, &premise);
        continuation->premises[i] = premise;
        continuation->premise_binders[i] = binder;
        continuation->args[i] = NULL;
        continuation->done[i] = false;
    }
    (void)chain_schedule_next_premise(ctx, queue, continuation, &item->env);
}

static void chain_process_choice(ChainContext *ctx, ChainWorkQueue *queue,
                                 ChainWorkItem *item) {
    ChainChoice *choice = item->choice;
    if (!choice || choice->position >= choice->count) return;
    ChainChoiceCandidate current = choice->items[choice->position];

    Bindings candidate_env;
    bindings_init(&candidate_env);
    if (!bindings_clone(&candidate_env, &item->env)) {
        chain_mark_incomplete(ctx, "search-binding-allocation");
        return;
    }

    if (choice->position + 1u < choice->count) {
        choice->position++;
        ChainChoiceCandidate next = choice->items[choice->position];
        const ChainDecl *next_decl = &ctx->index.decls[next.decl_index];
        item->guidance = chain_u64_add_sat(choice->base_guidance, next.cost);
        item->priority = next_decl->is_rule ? 2u : 1u;
        if (!chain_queue_push_move(ctx, queue, item)) {
            bindings_free(&candidate_env);
            return;
        }
    }

    const ChainDecl *decl = &ctx->index.decls[current.decl_index];
    ChainWorkItem candidate = {0};
    candidate.kind = CHAIN_WORK_CANDIDATE;
    candidate.decl_index = current.decl_index;
    candidate.goal = item->goal;
    candidate.depth = item->depth;
    candidate.continuation = item->continuation;
    candidate.rank = item->rank;
    candidate.guidance = chain_u64_add_sat(choice->base_guidance,
                                           current.cost);
    candidate.guided = true;
    candidate.priority = decl->is_rule ? 2u : 1u;
    candidate.env = candidate_env;
    chain_process_candidate(ctx, queue, &candidate);
    bindings_free(&candidate.env);
}

static void chain_process_result(ChainContext *ctx, ChainWorkQueue *queue,
                                 ChainWorkItem *item, ChainProofVec *answers) {
    if (!item->continuation) {
        Atom *checked_type = NULL;
        if (!chain_proof_checked(ctx, item->proof, ctx->root_goal, &item->env,
                                 &checked_type))
            return;
        AnswerIdentityV2 identity;
        if (!answer_identity_v2_make(ctx, item->proof, checked_type,
                                     &item->env, &identity))
            return;
        (void)chain_vec_push_move(ctx, answers, &identity, &item->env);
        return;
    }

    ChainContinuation *continuation =
        chain_continuation_clone(ctx, item->continuation);
    uint32_t pick = continuation->pick;
    if (pick >= continuation->arity || continuation->done[pick]) return;
    continuation->done[pick] = true;
    continuation->args[pick] = item->proof;
    continuation->solved++;
    Atom *binder = continuation->premise_binders[pick];
    if (binder && !bindings_add_id_acyclic(
                      &item->env, binder->var_id,
                      binder->sym_id, item->proof))
        return;
    if (continuation->solved == continuation->arity) {
        (void)chain_finish_continuation(ctx, queue, continuation, &item->env);
        return;
    }
    (void)chain_schedule_next_premise(ctx, queue, continuation, &item->env);
}

static void chain_run_search(ChainContext *ctx, Atom *goal, uint32_t depth,
                             uint32_t limit, bool root_rules_only,
                             ChainProofVec *answers) {
    ctx->root_goal = goal;
    if (!chain_collect_query_vars(ctx, goal)) return;
    ChainWorkQueue queue;
    chain_queue_init(&queue);
    Bindings initial;
    bindings_init(&initial);
    (void)chain_schedule_goal(ctx, &queue, goal, depth, &initial, NULL,
                              0, 0,
                              !root_rules_only, true);
    bindings_free(&initial);

    while (queue.len > 0 && !ctx->incomplete && answers->count < limit) {
        ChainWorkItem item;
        if (!chain_queue_pop(ctx, &queue, &item)) break;
        ctx->stats.work_items++;
        if (item.kind == CHAIN_WORK_CANDIDATE)
            chain_process_candidate(ctx, &queue, &item);
        else if (item.kind == CHAIN_WORK_RESULT)
            chain_process_result(ctx, &queue, &item, answers);
        else
            chain_process_choice(ctx, &queue, &item);
        bindings_free(&item.env);
    }
    if (!ctx->incomplete && answers->count >= limit && queue.len > 0) {
        ctx->more_possible = true;
        ctx->incomplete_reason = "answer-limit";
    }
    chain_queue_free(&queue);
}

static Atom *chain_stats_atom(ChainContext *ctx) {
    if (ctx->policy == CHAIN_POLICY_ATP_GUIDED_INHABITATION) {
        Atom *e[12];
        e[0] = he_sym(ctx->arena, "typing-search-stats-v2");
        e[1] = atom_expr2(ctx->arena, he_sym(ctx->arena, "goals-scheduled"),
                          atom_int(ctx->arena, (int64_t)ctx->stats.calls));
        e[2] = atom_expr2(ctx->arena, he_sym(ctx->arena, "fact-candidates"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.fact_candidates));
        e[3] = atom_expr2(ctx->arena, he_sym(ctx->arena, "rule-candidates"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.rule_candidates));
        e[4] = atom_expr2(ctx->arena, he_sym(ctx->arena, "head-pruned"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.head_pruned));
        e[5] = atom_expr2(ctx->arena, he_sym(ctx->arena, "ancestor-pruned"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.ancestor_pruned));
        e[6] = atom_expr2(ctx->arena, he_sym(ctx->arena, "proof-checks"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.proof_checks));
        e[7] = atom_expr2(ctx->arena, he_sym(ctx->arena, "work-items"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.work_items));
        e[8] = atom_expr2(ctx->arena, he_sym(ctx->arena, "continuations"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.continuations));
        e[9] = atom_expr2(ctx->arena, he_sym(ctx->arena, "agenda-weight-pops"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.agenda_weight_pops));
        e[10] = atom_expr2(ctx->arena, he_sym(ctx->arena, "agenda-age-pops"),
                          atom_int(ctx->arena,
                                   (int64_t)ctx->stats.agenda_age_pops));
        e[11] = atom_expr2(ctx->arena, he_sym(ctx->arena, "fuel-remaining"),
                           atom_int(ctx->arena, (int64_t)ctx->fuel));
        return atom_expr(ctx->arena, e, 12);
    }
    Atom *e[9];
    e[0] = he_sym(ctx->arena, "typing-search-stats-v1");
    e[1] = atom_expr2(ctx->arena, he_sym(ctx->arena, "goals-scheduled"),
                      atom_int(ctx->arena, (int64_t)ctx->stats.calls));
    e[2] = atom_expr2(ctx->arena, he_sym(ctx->arena, "fact-candidates"),
                      atom_int(ctx->arena,
                               (int64_t)ctx->stats.fact_candidates));
    e[3] = atom_expr2(ctx->arena, he_sym(ctx->arena, "rule-candidates"),
                      atom_int(ctx->arena,
                               (int64_t)ctx->stats.rule_candidates));
    e[4] = atom_expr2(ctx->arena, he_sym(ctx->arena, "head-pruned"),
                      atom_int(ctx->arena, (int64_t)ctx->stats.head_pruned));
    e[5] = atom_expr2(ctx->arena, he_sym(ctx->arena, "proof-checks"),
                      atom_int(ctx->arena, (int64_t)ctx->stats.proof_checks));
    e[6] = atom_expr2(ctx->arena, he_sym(ctx->arena, "work-items"),
                      atom_int(ctx->arena, (int64_t)ctx->stats.work_items));
    e[7] = atom_expr2(ctx->arena, he_sym(ctx->arena, "continuations"),
                      atom_int(ctx->arena,
                               (int64_t)ctx->stats.continuations));
    e[8] = atom_expr2(ctx->arena, he_sym(ctx->arena, "fuel-remaining"),
                      atom_int(ctx->arena, (int64_t)ctx->fuel));
    return atom_expr(ctx->arena, e, 9);
}

static Atom *chain_report(ChainContext *ctx, uint32_t depth,
                          ChainProofVec *proofs) {
    Atom **p = arena_alloc(ctx->arena, sizeof(Atom *) * (proofs->count + 1));
    p[0] = he_sym(ctx->arena, "proofs");
    for (uint32_t i = 0; i < proofs->count; i++) {
        Atom *decl[3] = {atom_symbol(ctx->arena, ":"),
                         proofs->items[i].identity.proof,
                         proofs->items[i].identity.type};
        p[i + 1] = atom_expr3(ctx->arena,
                              he_sym(ctx->arena, "typed-answer-v2"),
                              atom_expr(ctx->arena, decl, 3),
                              proofs->items[i].identity.substitution);
    }
    Atom *proof_list = atom_expr(ctx->arena, p, proofs->count + 1);
    Atom *e[6];
    e[0] = he_sym(ctx->arena, "typing-search");
    e[1] = he_sym(ctx->arena, ctx->incomplete ? "resource-incomplete"
                              : ctx->more_possible ? "more-possible"
                                                   : "depth-complete");
    e[2] = atom_int(ctx->arena, depth);
    e[3] = he_sym(ctx->arena, ctx->incomplete_reason
                                  ? ctx->incomplete_reason : "complete");
    e[4] = proof_list;
    e[5] = chain_stats_atom(ctx);
    return atom_expr(ctx->arena, e, 6);
}

static bool chain_arg_nat(Arena *a, Atom *x, uint32_t *out,
                          const char *reason, Atom **bad) {
    if (!(x->kind == ATOM_GROUNDED && x->ground.gkind == GV_INT) ||
        x->ground.ival < 0 || (uint64_t)x->ground.ival > UINT32_MAX) {
        *bad = he_reject(a, he_reason(a, reason));
        return false;
    }
    *out = (uint32_t)x->ground.ival;
    return true;
}

static Atom *chain_inhabit_dispatch(Arena *a, Space *space, Atom *goal,
                                    uint32_t depth, uint64_t fuel,
                                    bool fuel_limited,
                                    uint32_t limit, bool first_only,
                                    ChainPolicy policy) {
    ChainContext ctx = {0};
    ctx.arena = a;
    ctx.space = space;
    ctx.fuel = fuel;
    ctx.fuel_limited = fuel_limited;
    ctx.policy = policy;

    CettaHeTypingBudget unbounded_typing;
    CettaHeTypingBudget *parent_budget = g_active_typing_budget;
    if (!fuel_limited) {
        he_typing_budget_init_unbounded(&unbounded_typing);
        g_active_typing_budget = &unbounded_typing;
    }
    if (!chain_index_build(&ctx)) {
        chain_index_free(&ctx.index);
        g_active_typing_budget = parent_budget;
        return he_unknown(a, he_reason(a, "chaining-index-failed"));
    }
    ChainProofVec proofs;
    chain_vec_init(&proofs);
    chain_run_search(&ctx, unquote(goal), depth,
                     first_only ? 1u : (limit ? limit : 1u), false, &proofs);
    Atom *result;
    if (first_only) {
        if (proofs.count > 0) {
            Atom *decl[3] = {atom_symbol(a, ":"),
                             proofs.items[0].identity.proof,
                             proofs.items[0].identity.type};
            result = he_accept(
                a, atom_expr3(a, he_sym(a, "typed-answer-v2"),
                              atom_expr(a, decl, 3),
                              proofs.items[0].identity.substitution));
        } else if (ctx.incomplete) {
            result = he_unknown(a, he_reason(a, ctx.incomplete_reason
                                                ? ctx.incomplete_reason
                                                : "search-incomplete"));
        } else {
            result = he_reject(a, he_reason(a, "no-inhabitant-at-depth"));
        }
    } else {
        result = chain_report(&ctx, depth, &proofs);
    }
    chain_vec_free(&proofs);
    chain_index_free(&ctx.index);
    free(ctx.query_vars);
    free(ctx.scheme_vars);
    g_active_typing_budget = parent_budget;
    return result;
}

static bool chain_parse_search_policy(Atom *atom, ChainPolicy *policy_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        !atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.search_policy) ||
        !atom_is_symbol_id(atom->expr.elems[1],
                           g_builtin_syms.atp_guided_inhabitation))
        return false;
    *policy_out = CHAIN_POLICY_ATP_GUIDED_INHABITATION;
    return true;
}

/* One forward round.  fuel is an aggregate in/out budget shared with the
 * caller; incomplete_out reports honestly when the round could not finish
 * (fuel exhaustion or a failed index build), so a caller must never read
 * "nothing added" as a reached fixpoint on an incomplete round. */
static Atom *chain_forward_step(Arena *a, Space *space, uint64_t *fuel,
                                uint32_t limit, uint32_t *added_out,
                                bool *incomplete_out) {
    ChainContext ctx = {0};
    ctx.arena = a;
    ctx.space = space;
    ctx.fuel = *fuel;
    ctx.fuel_limited = true;
    if (!chain_index_build(&ctx)) {
        if (incomplete_out) *incomplete_out = true;
        chain_index_free(&ctx.index);
        return he_unknown(a, he_reason(a, "chaining-index-failed"));
    }
    ChainProofVec all;
    chain_vec_init(&all);
    Atom *goal = atom_var(a, "forward-goal");
    chain_run_search(&ctx, goal, 1, limit ? limit : 1u, true, &all);
    uint32_t added = 0;
    for (uint32_t i = 0; i < all.count; i++) {
        Atom *decl[3] = {atom_symbol(a, ":"), all.items[i].identity.proof,
                         all.items[i].identity.type};
        Atom *row = atom_expr(a, decl, 3);
        if (!space_contains_exact(space, row)) {
            space_add(space, row);
            added++;
        }
    }
    if (added_out) *added_out = added;
    if (incomplete_out) *incomplete_out = ctx.incomplete || ctx.more_possible;
    *fuel = ctx.fuel;
    Atom *e[5] = {he_sym(a, "forward-step"), atom_int(a, added),
                  atom_int(a, all.count),
                  he_sym(a, ctx.incomplete ? "resource-incomplete"
                              : ctx.more_possible ? "more-possible"
                                                  : "complete"),
                  chain_stats_atom(&ctx)};
    Atom *report = he_accept(a, atom_expr(a, e, 5));
    chain_vec_free(&all);
    chain_index_free(&ctx.index);
    free(ctx.query_vars);
    free(ctx.scheme_vars);
    return report;
}


/* ── argument helpers ──────────────────────────────────────────────────── */

static bool arg_space(Atom *at, Space **out) {
    if (at->kind == ATOM_GROUNDED && at->ground.gkind == GV_SPACE) {
        *out = (Space *)at->ground.ptr;
        return *out != NULL;
    }
    return false;
}

static Atom *arg_fuel(Arena *a, Atom *at, uint64_t *out) {
    if (!(at->kind == ATOM_GROUNDED && at->ground.gkind == GV_INT))
        return he_reject(a, he_reason(a, "fuel-not-an-integer"));
    if (at->ground.ival <= 0)
        return he_reject(a, he_reason(a, "fuel-not-positive"));
    *out = (uint64_t)at->ground.ival;
    return NULL;
}

/* ── dispatch ──────────────────────────────────────────────────────────── */

static const char *const HE_OP_NAMES[] = {
    "normalize-type", "check-type-refinements",
    "is-consistent", "check-type",
    "search-inhabitants", "search-first-inhabitant",
    "type-forward-step", "type-forward-closure",
};

bool he_typing_is_op(const char *name) {
    if (!name) return false;
    for (size_t i = 0; i < sizeof HE_OP_NAMES / sizeof HE_OP_NAMES[0]; i++)
        if (strcmp(name, HE_OP_NAMES[i]) == 0) return true;
    return false;
}

/* Which argument positions arrive as DATA (unevaluated) for each typing op.
 * Kept beside HE_OP_NAMES so the op surface and its argument policy cannot
 * drift apart; the evaluator consults this instead of naming ops itself. */
bool he_typing_op_data_arg(const char *name, uint32_t arg_index) {
    if (!name) return false;
    if (strcmp(name, "normalize-type") == 0 ||
        strcmp(name, "check-type-refinements") == 0)
        return arg_index == 1;
    if (strcmp(name, "is-consistent") == 0)
        return arg_index == 0 || arg_index == 1;
    if (strcmp(name, "check-type") == 0)
        return arg_index == 1 || arg_index == 2;
    if (strcmp(name, "search-inhabitants") == 0 ||
        strcmp(name, "search-first-inhabitant") == 0)
        return arg_index == 1;
    return false;
}

Atom *he_typing_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    const char *name = symbol_bytes(g_symbols, head->sym_id);
    if (!he_typing_is_op(name)) return NULL;
    if (!eval_current_profile_enables_dependent_telescope()) return NULL;

    if (strcmp(name, "normalize-type") == 0) {
        if (nargs != 3)
            return he_reject(a, he_reason(a, "normalize-type-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[2], &fuel);
        if (bad) return bad;
        HeNormStatus ns = HE_NORM_COMPLETE;
        Atom *n = normalize_type_checked(a, sp, unquote(args[1]), &fuel, &ns,
                                         0, NULL);
        if (ns == HE_NORM_AMBIGUOUS)
            return he_unknown(a, he_reason(a, "type-computation-ambiguous"));
        if (ns == HE_NORM_NO_RESULT)
            return he_unknown(a, he_reason(a, "type-computation-no-result"));
        if (ns == HE_NORM_RESOURCE)
            return he_unknown(a, he_reason(a, "type-computation-exhausted"));
        if (ns == HE_NORM_INADMISSIBLE)
            return he_reject(a, he_reason(a, "type-computation-inadmissible"));
        return he_accept(a, n);
    }

    /* Checks the declared type-index-refinement predicates of a type (and
     * runs its admissible computations).  This is NOT a well-formedness
     * judgment: no universes, no binder-domain formation, no positivity. */
    if (strcmp(name, "check-type-refinements") == 0) {
        if (nargs != 3)
            return he_reject(a, he_reason(a, "check-type-refinements-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[2], &fuel);
        if (bad) return bad;
        Atom *detail = NULL;
        HeTypeValidity v = check_type_refinements(a, sp, unquote(args[1]),
                                                  &fuel, &detail, 0, NULL);
        if (v == HE_TYPE_VALID) return he_accept(a, unquote(args[1]));
        if (v == HE_TYPE_INVALID)
            return he_reject(a, detail ? detail : he_reason(a, "invalid-type"));
        return he_unknown(a, detail ? detail
                                    : he_reason(a, "type-refinement-unknown"));
    }


    if (strcmp(name, "is-consistent") == 0) {
        if (nargs != 2)
            return he_reject(a, he_reason(a, "is-consistent-needs-two-types"));
        CettaHeTypingBudget typing;
        he_typing_budget_init_unbounded(&typing);
        CettaHeTypingBudget *parent_budget = g_active_typing_budget;
        g_active_typing_budget = &typing;
        uint64_t unused = 0;
        HeEdge e = consistency(args[0], args[1], &unused);
        g_active_typing_budget = parent_budget;
        if (e == HE_UNKNOWN)
            return he_unknown(a, he_reason(a, "consistency-exhausted"));
        if (e == HE_NONE)
            return he_reject(a, he_reason2(a, "inconsistent",
                                           atom_expr2(a, args[0], args[1])));
        return he_accept(a, edge_atom(a, e));
    }

    if (strcmp(name, "check-type") == 0) {
        if (nargs != 4)
            return he_reject(a, he_reason(a, "check-type-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[3], &fuel);
        if (bad) return bad;
        return check_type(a, sp, args[1], args[2], fuel);
    }


    if (strcmp(name, "search-inhabitants") == 0) {
        if (nargs != 5 && nargs != 6)
            return he_reject(a, he_reason(a, "search-inhabitants-arity"));
        ChainPolicy policy = CHAIN_POLICY_NATIVE;
        if (nargs == 6 && !chain_parse_search_policy(args[5], &policy))
            return he_reject(a, he_reason(a, "unsupported-typing-search-policy"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint32_t depth = 0, limit = 0;
        Atom *bad = NULL;
        if (!chain_arg_nat(a, args[2], &depth, "depth-not-natural", &bad))
            return bad;
        uint64_t fuel = 0;
        bad = arg_fuel(a, args[3], &fuel);
        if (bad) return bad;
        if (!chain_arg_nat(a, args[4], &limit, "limit-not-natural", &bad) ||
            limit == 0)
            return bad ? bad : he_reject(a, he_reason(a, "limit-not-positive"));
        return chain_inhabit_dispatch(a, sp, args[1], depth, fuel, true, limit,
                                      false, policy);
    }

    if (strcmp(name, "search-first-inhabitant") == 0) {
        if (nargs != 3 && nargs != 4 && nargs != 5)
            return he_reject(a, he_reason(a, "search-first-inhabitant-arity"));
        ChainPolicy policy = CHAIN_POLICY_NATIVE;
        bool fuel_limited = false;
        uint64_t fuel = 0;
        if (nargs == 4) {
            Atom *bad = arg_fuel(a, args[3], &fuel);
            if (bad) return bad;
            fuel_limited = true;
        } else if (nargs == 5) {
            Atom *bad = arg_fuel(a, args[3], &fuel);
            if (bad) return bad;
            fuel_limited = true;
            if (!chain_parse_search_policy(args[4], &policy))
                return he_reject(
                    a, he_reason(a, "unsupported-typing-search-policy"));
        }
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint32_t depth = 0;
        Atom *bad = NULL;
        if (!chain_arg_nat(a, args[2], &depth, "depth-not-natural", &bad))
            return bad;
        return chain_inhabit_dispatch(a, sp, args[1], depth, fuel,
                                      fuel_limited, 1,
                                      true, policy);
    }

    if (strcmp(name, "type-forward-step") == 0) {
        if (nargs != 3)
            return he_reject(a, he_reason(a, "type-forward-step-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[1], &fuel);
        if (bad) return bad;
        uint32_t limit = 0;
        if (!chain_arg_nat(a, args[2], &limit, "limit-not-natural", &bad) ||
            limit == 0)
            return bad ? bad : he_reject(a, he_reason(a, "limit-not-positive"));
        return chain_forward_step(a, sp, &fuel, limit, NULL, NULL);
    }

    if (strcmp(name, "type-forward-closure") == 0) {
        if (nargs != 4)
            return he_reject(a, he_reason(a, "type-forward-closure-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint32_t rounds = 0, limit = 0;
        Atom *bad = NULL;
        if (!chain_arg_nat(a, args[1], &rounds, "rounds-not-natural", &bad))
            return bad;
        uint64_t fuel = 0;
        bad = arg_fuel(a, args[2], &fuel);
        if (bad) return bad;
        if (!chain_arg_nat(a, args[3], &limit, "limit-not-natural", &bad) ||
            limit == 0)
            return bad ? bad : he_reject(a, he_reason(a, "limit-not-positive"));
        /* One aggregate budget for the whole closure; a round that ran out of
         * fuel (or failed to index) must not read as a reached fixpoint, and
         * running out of rounds while still adding is not completion either. */
        uint64_t total = 0;
        uint32_t attempted = 0;
        bool fixpoint = false, starved = false;
        for (uint32_t r = 0; r < rounds; r++) {
            if (fuel == 0) { starved = true; break; }
            uint32_t added = 0;
            bool step_incomplete = false;
            (void)chain_forward_step(a, sp, &fuel, limit, &added,
                                     &step_incomplete);
            attempted++;
            total += added;
            if (step_incomplete) { starved = true; break; }
            if (added == 0) { fixpoint = true; break; }
        }
        if (total > (uint64_t)INT64_MAX) total = (uint64_t)INT64_MAX;
        const char *closure_status = fixpoint ? "fixpoint"
                                   : starved  ? "resource-incomplete"
                                              : "rounds-exhausted";
        Atom *e[4] = {he_sym(a, "forward-closure"), atom_int(a, attempted),
                      atom_int(a, (int64_t)total), he_sym(a, closure_status)};
        return he_accept(a, atom_expr(a, e, 4));
    }

    return NULL;
}
