/* he_typing.c — HE-style typing, formally regrounded, for the he-prime profile.
 * See he_typing.h for the frame.  Comments here state constraints only. */

#include "he_typing.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>


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

/* ── the consistency relation, with a named licensing reason per edge ──────
 * HE's match_types returns a bare bool that fuses four reasons.  Naming the
 * reason apart is the whole regrounding: the dynamic-? edge is marked so it
 * cannot be composed, the top edge is subsumption, the staging edge governs
 * evaluation order, and only the exact edge is sound must-typing. */

typedef enum {
    HE_NONE = 0,   /* no rule licenses consistency: a proven mismatch */
    HE_EXACT,      /* structural/nominal equality (sound must-typing core) */
    HE_STRUCT,     /* arrow-congruent, all children consistent, some non-exact */
    HE_DYNAMIC,    /* licensed by ? on one side — deliberately non-composing */
    HE_TOP,        /* licensed by Atom-as-top — one-directional subsumption */
    HE_META,       /* licensed by a meta-type — a staging modality edge */
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

/* ── edge composition ────────────────────────────────────────────────────
 * A composite edge is licensed no more strongly than its weakest child.
 * License strength (strongest first): exact > structural > top > meta-staging
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

/* Only these edges are proof grade: the licensing is structural equality all
 * the way down.  A dynamic, top, or meta-staging license anywhere in the
 * composition is a gradual acceptance, not a proof — the chainer must not
 * treat such a match as evidence. */
static bool edge_is_proof_grade(HeEdge e) {
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
    if (*fuel == 0) return HE_UNKNOWN;
    (*fuel)--;

    /* ? on either side: dynamic edge, non-composing.  This is the fix for the
     * %Undefined% laundering — the edge is licensed but marked dynamic so a
     * chain through ? cannot be collapsed into an exact relation. */
    if (is_undefined(actual) || is_undefined(expected)) return HE_DYNAMIC;

    /* Atom as expected: genuine top, subsumption holds one-directionally. */
    if (is_atom_top(expected)) return HE_TOP;
    /* Atom as actual against a non-top expected: this is the unsound direction
     * HE also accepts; a proven mismatch under success typing unless expected
     * is itself a meta-type. */
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
     * combining the child licenses (edge_combine: a dynamic child keeps the
     * whole edge dynamic; top/meta children keep their license visible). */
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

/* ── type-of: the intersection of declared types (a set), read from a space ──
 * Mirrors HE get_atom_types in shape (a type SET, %Undefined% for none) but
 * reads ? honestly and tracks reasons.  Fuel-bounded; grounded self-types are a
 * trust edge (census entry) preserved for fidelity. */

#define HE_MAX_TYPES 64u

typedef struct {
    Atom *items[HE_MAX_TYPES];
    uint32_t count;
    bool overflow;
} HeTypeSet;

static void set_init(HeTypeSet *s) { s->count = 0; s->overflow = false; }

static void set_add(HeTypeSet *s, Atom *t) {
    for (uint32_t i = 0; i < s->count; i++)
        if (atom_eq(s->items[i], t)) return;
    if (s->count >= HE_MAX_TYPES) { s->overflow = true; return; }
    s->items[s->count++] = t;
}

/* Collect (: <term> <T>) declarations for a symbol/expression head. */
static void collect_annotations(Arena *a, Space *space, Atom *term,
                                HeTypeSet *out) {
    uint32_t len = 0;
    if (!space_length_u32_checked(space, &len)) return;
    for (uint32_t i = 0; i < len; i++) {
        Atom *at = space_get_at(space, i);
        if (!at || at->kind != ATOM_EXPR || at->expr.len != 3) continue;
        if (!atom_is_symbol_id(at->expr.elems[0], g_builtin_syms.colon)) continue;
        if (atom_eq(at->expr.elems[1], term))
            set_add(out, at->expr.elems[2]);
    }
}

/* A term may be handed quoted to type it as written rather than reduced. */
static Atom *unquote(Atom *t) {
    while (t->kind == ATOM_EXPR && t->expr.len == 2 &&
           atom_is_symbol_id(t->expr.elems[0], g_builtin_syms.quote))
        t = t->expr.elems[1];
    return t;
}

static bool type_of(Arena *a, Space *space, Atom *term, uint64_t *fuel,
                    HeTypeSet *out);

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
 * normal result, and is fuel-bounded.  Open expressions remain residual: a
 * dependent codomain can be instantiated later rather than guessed now. */

typedef enum {
    HE_NORM_COMPLETE = 0,
    HE_NORM_RESOURCE,
    HE_NORM_AMBIGUOUS,
    HE_NORM_NO_RESULT,
    HE_NORM_INADMISSIBLE  /* effectful computation inside a type: never run */
} HeNormStatus;

/* Bounds the mutual normalize/validate recursion: structural descent plus the
 * rewrite iteration on computed results.  Fuel bounds total work but not stack
 * depth — a type-level function that keeps growing its result would otherwise
 * deepen the C stack once per rewrite until overflow. */
#define HE_TYPE_DEPTH_LIMIT 512u

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

static Atom *normalize_type_checked(Arena *a, Space *space, Atom *ty,
                                    uint64_t *fuel, HeNormStatus *status,
                                    uint32_t depth) {
    if (*fuel == 0 || depth > HE_TYPE_DEPTH_LIMIT) {
        *status = HE_NORM_RESOURCE;
        return ty;
    }
    (*fuel)--;

    if (ty->kind != ATOM_EXPR || ty->expr.len == 0)
        return ty;

    Atom **children = arena_alloc(a, sizeof(Atom *) * ty->expr.len);
    bool changed = false;
    for (uint32_t i = 0; i < ty->expr.len; i++) {
        HeNormStatus child_status = HE_NORM_COMPLETE;
        children[i] = normalize_type_checked(a, space, ty->expr.elems[i],
                                             fuel, &child_status, depth + 1);
        if (child_status != HE_NORM_COMPLETE) {
            *status = child_status;
            return ty;
        }
        changed |= children[i] != ty->expr.elems[i];
    }
    Atom *norm = changed ? atom_expr(a, children, ty->expr.len) : ty;

    /* Type-pure grounded computation (arithmetic and friends) is the
     * capability-gated deterministic fragment; normalize_type_expr will not
     * dispatch anything outside it. */
    Atom *grounded = normalize_type_expr(a, norm);
    if (grounded != norm) {
        HeNormStatus next = HE_NORM_COMPLETE;
        Atom *r = normalize_type_checked(a, space, grounded, fuel, &next,
                                         depth + 1);
        if (next != HE_NORM_COMPLETE) *status = next;
        return r;
    }

    Atom *head = norm->expr.elems[0];

    /* A grounded op that is NOT type-pure names a computation with effects or
     * nondeterminism.  It must never run inside a type, and a user marker
     * cannot override that: the verdict is an honest inadmissible, not a
     * silent normal form and not an execution. */
    if (head->kind == ATOM_SYMBOL && !atom_has_vars(norm) &&
        is_grounded_op(head->sym_id) &&
        !grounded_op_is_type_pure(head->sym_id)) {
        *status = HE_NORM_INADMISSIBLE;
        return norm;
    }

    if (!space_has_unary_marker(space, "type-level-function", head))
        return norm;

    /* Open calls are well-formed residual type expressions. */
    if (atom_has_vars(norm)) return norm;

    if (*fuel == 0) {
        *status = HE_NORM_RESOURCE;
        return norm;
    }
    /* Marked user functions run under a scoped experimental policy, NOT a
     * proven purity judgment: evaluation happens against a snapshot clone
     * (space effects are contained and discarded), fuel-bounded, and only a
     * unique successful normal form is accepted.  What this does not contain:
     * a marked function may still perform I/O through the evaluator.  The
     * marker is user trust, and the containment boundary is the snapshot. */
    uint64_t budget = *fuel < 512 ? *fuel : 512;
    *fuel -= budget;
    Space *snapshot = eval_space_snapshot_clone(space, a);
    if (!snapshot) {
        *status = HE_NORM_RESOURCE;
        return norm;
    }

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(snapshot, a, NULL, norm, (int)budget, &rs);

    Atom *unique = NULL;
    bool ambiguous = false;
    for (CettaCount i = 0; i < rs.len; i++) {
        Atom *candidate = rs.items[i];
        if (!candidate || atom_is_error(candidate) || atom_is_empty(candidate))
            continue;
        if (!unique) unique = candidate;
        else if (!atom_eq(unique, candidate)) {
            ambiguous = true;
            break;
        }
    }
    result_set_free(&rs);

    if (ambiguous) {
        *status = HE_NORM_AMBIGUOUS;
        return norm;
    }
    if (!unique) {
        *status = HE_NORM_NO_RESULT;
        return norm;
    }
    if (atom_eq(unique, norm)) return norm;

    HeNormStatus next = HE_NORM_COMPLETE;
    Atom *r = normalize_type_checked(a, space, unique, fuel, &next, depth + 1);
    if (next != HE_NORM_COMPLETE) *status = next;
    return r;
}

typedef enum {
    HE_TYPE_VALID = 0,
    HE_TYPE_INVALID,
    HE_TYPE_VALIDATION_UNKNOWN
} HeTypeValidity;

static const char *norm_status_reason(HeNormStatus ns) {
    switch (ns) {
    case HE_NORM_AMBIGUOUS: return "type-property-ambiguous";
    case HE_NORM_NO_RESULT: return "type-property-no-result";
    case HE_NORM_INADMISSIBLE: return "type-computation-inadmissible";
    default: return "type-property-fuel-exhausted";
    }
}

static HeTypeValidity validate_type_properties(Arena *a, Space *space,
                                               Atom *ty, uint64_t *fuel,
                                               Atom **detail, uint32_t depth) {
    if (*fuel == 0 || depth > HE_TYPE_DEPTH_LIMIT) {
        if (detail) *detail = he_reason(a, "type-property-fuel-exhausted");
        return HE_TYPE_VALIDATION_UNKNOWN;
    }
    (*fuel)--;

    HeNormStatus ns = HE_NORM_COMPLETE;
    ty = normalize_type_checked(a, space, ty, fuel, &ns, depth);
    if (ns != HE_NORM_COMPLETE) {
        if (detail) *detail = he_reason(a, norm_status_reason(ns));
        /* an inadmissible computation in a type is a proven defect, not a
         * resource condition */
        return ns == HE_NORM_INADMISSIBLE ? HE_TYPE_INVALID
                                          : HE_TYPE_VALIDATION_UNKNOWN;
    }

    if (ty->kind != ATOM_EXPR || ty->expr.len == 0) return HE_TYPE_VALID;
    for (uint32_t i = 0; i < ty->expr.len; i++) {
        HeTypeValidity child = validate_type_properties(a, space,
                                                        ty->expr.elems[i],
                                                        fuel, detail,
                                                        depth + 1);
        if (child != HE_TYPE_VALID) return child;
    }

    uint32_t len = 0;
    if (!space_length_u32_checked(space, &len)) {
        if (detail) *detail = he_reason(a, "space-too-large");
        return HE_TYPE_VALIDATION_UNKNOWN;
    }
    for (uint32_t i = 0; i < len; i++) {
        Atom *row = space_get_at(space, i);
        if (!row || row->kind != ATOM_EXPR || row->expr.len != 4) continue;
        if (!atom_is_symbol(row->expr.elems[0], "type-index-property")) continue;
        if (!atom_eq(row->expr.elems[1], ty->expr.elems[0])) continue;
        Atom *idx = row->expr.elems[2];
        if (!(idx->kind == ATOM_GROUNDED && idx->ground.gkind == GV_INT) ||
            idx->ground.ival < 0 || (uint64_t)idx->ground.ival >= ty->expr.len) {
            if (detail) *detail = he_reason(a, "invalid-type-property-index");
            return HE_TYPE_INVALID;
        }
        Atom *predicate = row->expr.elems[3];
        if (!space_has_unary_marker(space, "type-level-function", predicate)) {
            if (detail) *detail = he_reason(a, "untrusted-type-property");
            return HE_TYPE_INVALID;
        }
        Atom *arg = ty->expr.elems[(uint32_t)idx->ground.ival];
        if (atom_has_vars(arg)) {
            if (detail) *detail = he_reason(a, "open-type-property");
            return HE_TYPE_VALIDATION_UNKNOWN;
        }
        Atom *call = atom_expr2(a, predicate, arg);
        HeNormStatus ps = HE_NORM_COMPLETE;
        Atom *answer = normalize_type_checked(a, space, call, fuel, &ps,
                                              depth + 1);
        if (ps != HE_NORM_COMPLETE) {
            if (detail) *detail = he_reason(a, norm_status_reason(ps));
            return ps == HE_NORM_INADMISSIBLE ? HE_TYPE_INVALID
                                              : HE_TYPE_VALIDATION_UNKNOWN;
        }
        if (atom_is_false_value(answer)) {
            if (detail) *detail = he_reason2(a, "type-property-failed", arg);
            return HE_TYPE_INVALID;
        }
        if (!atom_is_true_value(answer)) {
            if (detail) *detail = he_reason2(a, "type-property-not-boolean", answer);
            return HE_TYPE_VALIDATION_UNKNOWN;
        }
    }
    return HE_TYPE_VALID;
}

/* consistency, plus variable unification into a substitution.  The symbol
 * reasons (%Undefined% dynamic, Atom-as-actual reject, meta staging) are checked
 * BEFORE any variable binding, so they are unchanged from `consistency` — the
 * census fixes are about those symbols, which are orthogonal to variables.  Used
 * ONLY for dependent-codomain instantiation; the reject gate (`check-typing`)
 * and `is-consistent` keep the binding-free `consistency`. */
static HeEdge consistency_bind(Atom *actual, Atom *expected, uint64_t *fuel,
                               Bindings *tb) {
    if (*fuel == 0) return HE_UNKNOWN;
    (*fuel)--;
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
        return bindings_add_id(tb, expected->var_id, SYMBOL_ID_NONE, actual)
                   ? HE_EXACT : HE_NONE;
    }
    if (actual->kind == ATOM_VAR) {
        return bindings_add_id(tb, actual->var_id, SYMBOL_ID_NONE, expected)
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

/* Application typing with dependent-codomain instantiation.  For each function
 * type of the head with matching arity: freshen it, unify each argument's type
 * against the corresponding domain (peeling dependent binders and binding their
 * value variable), then substitute the accumulated bindings into the codomain
 * and reduce grounded operators in it.  The result set is the intersection-
 * elimination over the head's function types.  type-of stays TOTAL: when no
 * function type applies, the caller falls back to the structural product. */
static void type_of_application(Arena *a, Space *space, Atom *term,
                                uint64_t *fuel, HeTypeSet *out) {
    HeTypeSet head_types;
    set_init(&head_types);
    if (!type_of(a, space, term->expr.elems[0], fuel, &head_types)) return;
    uint32_t argc = term->expr.len - 1;
    for (uint32_t h = 0; h < head_types.count; h++) {
        Atom *ft = head_types.items[h];
        if (!is_arrow(ft)) continue;
        uint32_t arity = ft->expr.len - 2;
        if (argc > arity) continue;

        uint32_t suf = fresh_var_suffix();
        Atom *fresh_ft = atom_freshen_epoch(a, ft, suf);
        Atom *cod = fresh_ft->expr.elems[fresh_ft->expr.len - 1];
        Bindings tb;
        bindings_init(&tb);
        bool all_ok = true;

        for (uint32_t i = 0; i < argc && all_ok; i++) {
            Atom *domain = fresh_ft->expr.elems[i + 1];
            Atom *arg_term = term->expr.elems[i + 1];
            Atom *binder = NULL, *dtype = domain;
            split_binder(domain, &binder, &dtype);
            if (binder) {
                Atom *arg_val = bindings_apply_if_vars(&tb, a, arg_term);
                bindings_add_id(&tb, binder->var_id, SYMBOL_ID_NONE, arg_val);
            }
            Atom *dom = bindings_apply_if_vars(&tb, a, dtype);
            if (atom_is_meta_type(dom)) {
                if (!atom_meta_type_accepts(a, dom, arg_term)) all_ok = false;
                continue;
            }
            HeTypeSet at;
            set_init(&at);
            if (!type_of(a, space, arg_term, fuel, &at)) { all_ok = false; break; }
            bool found = false;
            for (uint32_t k = 0; k < at.count; k++) {
                Bindings trial;
                if (!bindings_clone(&trial, &tb)) { all_ok = false; break; }
                uint64_t f = *fuel;
                HeEdge e = consistency_bind(at.items[k], dom, &f, &trial);
                if (e != HE_NONE && e != HE_UNKNOWN) {
                    bindings_free(&tb);
                    tb = trial; /* move: tb takes over trial's storage */
                    found = true;
                    break;
                }
                bindings_free(&trial);
            }
            if (!found) all_ok = false;
        }
        if (all_ok) {
            uint32_t remaining = arity - argc;
            Atom *concrete_cod = bindings_apply_if_vars(&tb, a, cod);
            HeNormStatus ns = HE_NORM_COMPLETE;
            concrete_cod = normalize_type_checked(a, space, concrete_cod,
                                                  fuel, &ns, 0);
            if (ns == HE_NORM_COMPLETE && remaining == 0) {
                set_add(out, concrete_cod);
            } else if (ns == HE_NORM_COMPLETE) {
                Atom **residual = arena_alloc(a,
                    sizeof(Atom *) * (remaining + 2));
                residual[0] = atom_symbol(a, "->");
                for (uint32_t j = 0; j < remaining; j++) {
                    Atom *d = fresh_ft->expr.elems[argc + 1 + j];
                    residual[j + 1] = bindings_apply_if_vars(&tb, a, d);
                }
                residual[remaining + 1] = concrete_cod;
                set_add(out, atom_expr(a, residual, remaining + 2));
            }
        }
        bindings_free(&tb);
    }
}

static bool type_of(Arena *a, Space *space, Atom *term, uint64_t *fuel,
                    HeTypeSet *out) {
    if (*fuel == 0) return false;
    (*fuel)--;
    term = unquote(term);

    if (term->kind == ATOM_GROUNDED) {
        Atom *gt = get_grounded_type(a, term);
        if (!is_undefined(gt)) set_add(out, gt);
        return true;
    }
    if (term->kind == ATOM_VAR) {
        /* a variable carries no value type: its type is ? */
        set_add(out, atom_undefined_type(a));
        return true;
    }
    if (term->kind == ATOM_SYMBOL) {
        collect_annotations(a, space, term, out);
        if (out->count == 0) set_add(out, atom_undefined_type(a));
        return true;
    }
    /* expression */
    collect_annotations(a, space, term, out);
    if (out->count == 0 && term->expr.len >= 2)
        type_of_application(a, space, term, fuel, out);
    if (out->count == 0 && term->expr.len > 0) {
        /* tuple: the product of element types (census: application-vs-tuple is
         * decided by whether the head has a function type). */
        Atom **elems = arena_alloc(a, sizeof(Atom *) * term->expr.len);
        bool ok = true;
        for (uint32_t i = 0; i < term->expr.len; i++) {
            HeTypeSet et;
            set_init(&et);
            if (!type_of(a, space, term->expr.elems[i], fuel, &et) ||
                et.count == 0) {
                ok = false;
                break;
            }
            elems[i] = et.items[0];
        }
        if (ok) set_add(out, atom_expr(a, elems, term->expr.len));
    }
    if (out->count == 0) set_add(out, atom_undefined_type(a));
    return true;
}

/* Derive a subject's type from its structure, ignoring its own top-level
 * annotation — used to validate a declared type against the derivation. */
static bool type_of_structural(Arena *a, Space *space, Atom *term,
                               uint64_t *fuel, HeTypeSet *out) {
    if (*fuel == 0) return false;
    (*fuel)--;
    term = unquote(term);
    if (term->kind != ATOM_EXPR || term->expr.len == 0)
        return type_of(a, space, term, fuel, out);
    if (term->expr.len >= 2)
        type_of_application(a, space, term, fuel, out);
    if (out->count == 0) {
        Atom **elems = arena_alloc(a, sizeof(Atom *) * term->expr.len);
        bool ok = true;
        for (uint32_t i = 0; i < term->expr.len; i++) {
            HeTypeSet et;
            set_init(&et);
            if (!type_of(a, space, term->expr.elems[i], fuel, &et) ||
                et.count == 0) { ok = false; break; }
            elems[i] = et.items[0];
        }
        if (ok) set_add(out, atom_expr(a, elems, term->expr.len));
    }
    if (out->count == 0) set_add(out, atom_undefined_type(a));
    return true;
}

/* ── success-typing check (three-valued) ───────────────────────────────── */

static Atom *edge_atom(Arena *a, HeEdge e) { return he_sym(a, edge_name(e)); }

/* accept if any declared type is consistent with expected (success typing);
 * reject only if every type is a proven mismatch; unknown on exhaustion. */
static Atom *check_typing_mode(Arena *a, Space *space, Atom *term,
                               Atom *expected, uint64_t fuel, bool structural) {
    HeNormStatus ens = HE_NORM_COMPLETE;
    expected = normalize_type_checked(a, space, expected, &fuel, &ens, 0);
    if (ens != HE_NORM_COMPLETE)
        return he_unknown(a, he_reason(a, "expected-type-normalization-incomplete"));
    Atom *property_detail = NULL;
    HeTypeValidity ev = validate_type_properties(a, space, expected, &fuel,
                                                 &property_detail, 0);
    if (ev == HE_TYPE_INVALID)
        return he_reject(a, property_detail ? property_detail
                                            : he_reason(a, "invalid-expected-type"));
    if (ev == HE_TYPE_VALIDATION_UNKNOWN)
        return he_unknown(a, property_detail ? property_detail
                                             : he_reason(a, "expected-type-validation-unknown"));

    HeTypeSet ts;
    set_init(&ts);
    bool ok = structural ? type_of_structural(a, space, term, &fuel, &ts)
                         : type_of(a, space, term, &fuel, &ts);
    if (!ok)
        return he_unknown(a, he_reason(a, "type-of-exhausted"));
    if (ts.overflow)
        return he_unknown(a, he_reason(a, "type-set-capacity"));
    HeEdge best = HE_NONE;
    bool saw_unknown = false;
    for (uint32_t i = 0; i < ts.count; i++) {
        uint64_t f = fuel;
        HeNormStatus ans = HE_NORM_COMPLETE;
        Atom *actual = normalize_type_checked(a, space, ts.items[i], &f, &ans,
                                              0);
        if (ans != HE_NORM_COMPLETE) { saw_unknown = true; continue; }
        Atom *actual_detail = NULL;
        HeTypeValidity av = validate_type_properties(a, space, actual, &f,
                                                     &actual_detail, 0);
        if (av == HE_TYPE_INVALID) continue;
        if (av == HE_TYPE_VALIDATION_UNKNOWN) { saw_unknown = true; continue; }
        uint32_t epoch = fresh_var_suffix();
        Atom *fresh_actual = atom_freshen_epoch(a, actual, epoch);
        Atom *fresh_expected = atom_freshen_epoch(a, expected, epoch + 1);
        Bindings tb;
        bindings_init(&tb);
        HeEdge e = consistency_bind(fresh_actual, fresh_expected, &f, &tb);
        bindings_free(&tb);
        if (e == HE_UNKNOWN) { saw_unknown = true; continue; }
        if (e == HE_EXACT) best = HE_EXACT;
        else if (e != HE_NONE && best != HE_EXACT) best = e;
    }
    if (best == HE_EXACT)
        return he_accept(a, atom_expr2(a, edge_atom(a, HE_EXACT), expected));
    if (best != HE_NONE)
        return he_accept(a, atom_expr2(a, edge_atom(a, best), expected));
    if (saw_unknown)
        return he_unknown(a, he_reason(a, "consistency-exhausted"));
    Atom **rs = arena_alloc(a, sizeof(Atom *) * (ts.count + 1));
    rs[0] = he_sym(a, "no-consistent-type");
    for (uint32_t i = 0; i < ts.count; i++) rs[i + 1] = ts.items[i];
    return he_reject(a, atom_expr(a, rs, ts.count + 1));
}

static Atom *check_typing(Arena *a, Space *space, Atom *term, Atom *expected,
                          uint64_t fuel) {
    return check_typing_mode(a, space, term, expected, fuel, false);
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
#define CHAIN_ANSWER_DEPTH_LIMIT 512u

typedef struct {
    Atom *term;
    Atom *type;
    Atom *conclusion;
    SymbolId conclusion_head;
    uint32_t arity;
    uint32_t specificity;
    bool is_rule;
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
} ChainIndex;

/* AnswerIdentityV1 is the lossless identity of a typed-search answer.  The
 * proof and checked type are fully substituted, variables are canonicalized
 * jointly across every field, and substitution records only the original
 * query variables plus residual equality constraints.  It deliberately does
 * not quotient distinct derivations, theorem-equal proofs, or PLN evidence. */
typedef struct {
    Atom *proof;
    Atom *substitution;
    Atom *type;
    Atom *key;
} AnswerIdentityV1;

typedef struct {
    AnswerIdentityV1 identity;
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
    uint64_t proof_checks;
    uint64_t work_items;
    uint64_t continuations;
} ChainStats;

typedef struct {
    VarId var_id;
    SymbolId spelling;
} ChainQueryVar;

typedef struct {
    Arena *arena;
    Space *space;
    ChainIndex index;
    uint64_t fuel;
    bool incomplete;
    bool more_possible;
    const char *incomplete_reason;
    ChainStats stats;
    Atom *root_goal;
    ChainQueryVar *query_vars;
    uint32_t query_var_count;
    uint32_t query_var_cap;
} ChainContext;

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
        chain_mark_incomplete(ctx, "fuel-exhausted");
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
    case HE_NORM_COMPLETE:
        break;
    }
    chain_mark_incomplete(ctx, fallback);
}

static bool chain_take_fuel(ChainContext *ctx, uint64_t amount) {
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
    idx->count = idx->cap = 0;
}

static void chain_index_free(ChainIndex *idx) {
    free(idx->decls);
    free(idx->refs);
    chain_index_init(idx);
}

static uint32_t chain_specificity(Atom *a, uint32_t depth) {
    if (!a || depth > 64) return 0;
    if (a->kind == ATOM_VAR) return 0;
    if (a->kind != ATOM_EXPR) return 1;
    uint32_t score = 1;
    for (uint32_t i = 0; i < a->expr.len; i++)
        score += chain_specificity(a->expr.elems[i], depth + 1);
    return score;
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
        Atom *conclusion = rule ? type->expr.elems[type->expr.len - 1] : type;
        ChainDecl d = {
            .term = term,
            .type = type,
            .conclusion = conclusion,
            .conclusion_head = atom_head_symbol_id(conclusion),
            .arity = rule ? type->expr.len - 2 : 0,
            .specificity = chain_specificity(conclusion, 0),
            .is_rule = rule,
        };
        if (!chain_index_push(idx, d)) return false;
    }
    if (idx->count == 0) return true;
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

static bool chain_collect_query_vars(ChainContext *ctx, Atom *atom,
                                     uint32_t depth) {
    if (!atom || depth > CHAIN_ANSWER_DEPTH_LIMIT) {
        chain_mark_incomplete(ctx, "answer-identity-depth");
        return false;
    }
    if (atom->kind == ATOM_VAR) return chain_query_var_add(ctx, atom);
    if (atom->kind != ATOM_EXPR) return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++)
        if (!chain_collect_query_vars(ctx, atom->expr.elems[i], depth + 1u))
            return false;
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

static Atom *answer_canonicalize_rec(ChainContext *ctx,
                                     AnswerCanonicalMap *map, Atom *atom,
                                     uint32_t depth) {
    if (!atom || depth > CHAIN_ANSWER_DEPTH_LIMIT) {
        chain_mark_incomplete(ctx, "answer-identity-depth");
        return NULL;
    }
    if (atom->kind == ATOM_VAR) return answer_canonical_var(ctx, map, atom);
    if (atom->kind != ATOM_EXPR || !atom_has_vars(atom)) return atom;
    Atom **children = arena_alloc(ctx->arena, sizeof(Atom *) * atom->expr.len);
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        children[i] = answer_canonicalize_rec(ctx, map, atom->expr.elems[i],
                                              depth + 1u);
        if (!children[i]) return NULL;
    }
    return atom_expr(ctx->arena, children, atom->expr.len);
}

static Atom *answer_substitution_v1(ChainContext *ctx, const Bindings *env) {
    Atom **parts = arena_alloc(ctx->arena,
        sizeof(Atom *) * (ctx->query_var_count + 2u));
    parts[0] = he_sym(ctx->arena, "answer-substitution-v1");
    for (uint32_t i = 0; i < ctx->query_var_count; i++) {
        Atom *var = atom_var_with_spelling(ctx->arena,
                                           ctx->query_vars[i].spelling,
                                           ctx->query_vars[i].var_id);
        Atom *value = bindings_apply_if_vars(env, ctx->arena, var);
        if (!value) {
            chain_mark_incomplete(ctx, "answer-substitution-failed");
            return NULL;
        }
        parts[i + 1u] = atom_expr3(ctx->arena,
                                  he_sym(ctx->arena, "answer-binding-v1"),
                                  var, value);
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
    parts[ctx->query_var_count + 1u] =
        atom_expr(ctx->arena, constraints, env->eq_len + 1u);
    return atom_expr(ctx->arena, parts, ctx->query_var_count + 2u);
}

static bool answer_identity_v1_make(ChainContext *ctx, Atom *proof,
                                    Atom *type, const Bindings *env,
                                    AnswerIdentityV1 *out) {
    Atom *substituted_proof = bindings_apply_if_vars(env, ctx->arena, proof);
    Atom *substituted_type = bindings_apply_if_vars(env, ctx->arena, type);
    if (!substituted_proof || !substituted_type) {
        chain_mark_incomplete(ctx, "answer-substitution-failed");
        return false;
    }
    HeNormStatus status = HE_NORM_COMPLETE;
    substituted_type = normalize_type_checked(ctx->arena, ctx->space,
                                               substituted_type, &ctx->fuel,
                                               &status, 0);
    if (status != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, status, "answer-type-normalization-incomplete");
        return false;
    }
    Atom *substitution = answer_substitution_v1(ctx, env);
    if (!substitution) return false;
    Atom *raw_fields[4] = {he_sym(ctx->arena, "answer-identity-v1"),
                           substituted_proof, substitution, substituted_type};
    Atom *raw = atom_expr(ctx->arena, raw_fields, 4);
    AnswerCanonicalMap map = {0};
    Atom *key = answer_canonicalize_rec(ctx, &map, raw, 0);
    free(map.items);
    if (!key || key->kind != ATOM_EXPR || key->expr.len != 4u) return false;
    *out = (AnswerIdentityV1){
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
                                 const AnswerIdentityV1 *identity) {
    for (uint32_t i = 0; i < v->count; i++)
        if (atom_alpha_eq(v->items[i].identity.key, identity->key)) return true;
    return false;
}

static bool chain_vec_push_move(ChainContext *ctx, ChainProofVec *v,
                                const AnswerIdentityV1 *identity,
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

static bool chain_unify_shape_rec(ChainContext *ctx, Atom *left, Atom *right,
                                  Bindings *env, uint32_t depth) {
    if (depth > CETTA_MATCH_DEPTH_LIMIT || !chain_take_fuel(ctx, 1)) return false;
    left = deref(env, left);
    right = deref(env, right);
    if (atom_eq(left, right)) return true;
    if (left->kind == ATOM_VAR)
        return bindings_add_id(env, left->var_id, left->sym_id, right);
    if (right->kind == ATOM_VAR)
        return bindings_add_id(env, right->var_id, right->sym_id, left);
    if (chain_type_level_open_call(ctx, left) ||
        chain_type_level_open_call(ctx, right))
        return true; /* checked after premise instantiation */
    if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
        left->expr.len != right->expr.len)
        return false;
    for (uint32_t i = 0; i < left->expr.len; i++)
        if (!chain_unify_shape_rec(ctx, left->expr.elems[i],
                                   right->expr.elems[i], env, depth + 1))
            return false;
    return true;
}

static bool chain_unify_deferred(ChainContext *ctx, Atom *left, Atom *right,
                                 Bindings *env) {
    left = bindings_apply_if_vars(env, ctx->arena, left);
    right = bindings_apply_if_vars(env, ctx->arena, right);
    HeNormStatus ls = HE_NORM_COMPLETE, rs = HE_NORM_COMPLETE;
    left = normalize_type_checked(ctx->arena, ctx->space, left, &ctx->fuel, &ls,
                                  0);
    right = normalize_type_checked(ctx->arena, ctx->space, right, &ctx->fuel,
                                   &rs, 0);
    if (ls != HE_NORM_COMPLETE || rs != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, ls != HE_NORM_COMPLETE ? ls : rs,
            "type-normalization-incomplete");
        return false;
    }
    return chain_unify_shape_rec(ctx, left, right, env, 0);
}

static bool chain_unify_must(ChainContext *ctx, Atom *left, Atom *right,
                             Bindings *env, Atom **resolved_left) {
    left = bindings_apply_if_vars(env, ctx->arena, left);
    right = bindings_apply_if_vars(env, ctx->arena, right);
    HeNormStatus ls = HE_NORM_COMPLETE, rs = HE_NORM_COMPLETE;
    left = normalize_type_checked(ctx->arena, ctx->space, left, &ctx->fuel, &ls,
                                  0);
    right = normalize_type_checked(ctx->arena, ctx->space, right, &ctx->fuel,
                                   &rs, 0);
    if (ls != HE_NORM_COMPLETE || rs != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, ls != HE_NORM_COMPLETE ? ls : rs,
            "type-normalization-incomplete");
        return false;
    }
    Atom *detail = NULL;
    HeTypeValidity lv = validate_type_properties(ctx->arena, ctx->space, left,
                                                 &ctx->fuel, &detail, 0);
    HeTypeValidity rv = validate_type_properties(ctx->arena, ctx->space, right,
                                                 &ctx->fuel, &detail, 0);
    if (lv == HE_TYPE_VALIDATION_UNKNOWN || rv == HE_TYPE_VALIDATION_UNKNOWN) {
        chain_mark_incomplete(ctx, ctx->fuel == 0 ? "fuel-exhausted"
                                                  : "type-property-unknown");
        return false;
    }
    if (lv == HE_TYPE_INVALID || rv == HE_TYPE_INVALID) return false;
    HeEdge e = consistency_bind(left, right, &ctx->fuel, env);
    if (e == HE_UNKNOWN) {
        chain_mark_incomplete(ctx, ctx->fuel == 0 ? "fuel-exhausted"
                                              : "type-unification-exhausted");
        return false;
    }
    /* Gradual acceptances (dynamic/top/meta anywhere in the composition) are
     * fine for checking but are NOT proofs; only structural equality all the
     * way down lets a candidate stand as an inhabitant. */
    if (!edge_is_proof_grade(e)) return false;
    if (resolved_left) {
        Atom *r = bindings_apply_if_vars(env, ctx->arena, left);
        HeNormStatus ns = HE_NORM_COMPLETE;
        r = normalize_type_checked(ctx->arena, ctx->space, r, &ctx->fuel, &ns,
                                   0);
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
    HeTypeSet ts;
    set_init(&ts);
    if (!type_of(ctx->arena, ctx->space, proof, &ctx->fuel, &ts)) {
        chain_mark_incomplete(ctx, ctx->fuel == 0 ? "fuel-exhausted"
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
        if (chain_unify_must(ctx, ts.items[i], goal, &trial, &resolved)) {
            bindings_replace(env, &trial);
            if (checked_type) *checked_type = resolved;
            return true;
        }
        bindings_free(&trial);
    }
    return false;
}

static int chain_premise_score(ChainContext *ctx, Atom *premise,
                               const Bindings *env) {
    premise = bindings_apply_if_vars(env, ctx->arena, premise);
    int score = atom_has_vars(premise) ? 0 : 10000;
    score += (int)chain_specificity(premise, 0) * 8;
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
    uint32_t pick;
    ChainContinuation *parent;
};

typedef enum {
    CHAIN_WORK_CANDIDATE = 0,
    CHAIN_WORK_RESULT,
} ChainWorkKind;

typedef struct {
    ChainWorkKind kind;
    uint32_t decl_index;
    Atom *goal;
    uint32_t depth;
    Atom *proof;
    Atom *type;
    Bindings env;
    ChainContinuation *continuation;
    uint32_t rank;
    uint8_t priority;
    uint64_t serial;
} ChainWorkItem;

typedef struct {
    ChainWorkItem *items;
    uint32_t len;
    uint32_t cap;
    uint64_t next_serial;
} ChainWorkQueue;

static void chain_queue_init(ChainWorkQueue *queue) {
    queue->items = NULL;
    queue->len = queue->cap = 0;
    queue->next_serial = 0;
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
    if (left->rank != right->rank) return left->rank < right->rank;
    if (left->priority != right->priority)
        return left->priority < right->priority;
    return left->serial < right->serial;
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

static bool chain_queue_pop(ChainWorkQueue *queue, ChainWorkItem *out) {
    if (queue->len == 0) return false;
    *out = queue->items[0];
    queue->len--;
    if (queue->len > 0) {
        queue->items[0] = queue->items[queue->len];
        uint32_t pos = 0;
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

static bool chain_schedule_goal(ChainContext *ctx, ChainWorkQueue *queue,
                                Atom *goal, uint32_t depth,
                                const Bindings *env,
                                ChainContinuation *continuation,
                                uint32_t rank,
                                bool include_facts, bool include_rules) {
    ctx->stats.calls++;
    if (!chain_take_fuel(ctx, 1)) return false;
    goal = bindings_apply_if_vars(env, ctx->arena, goal);
    HeNormStatus status = HE_NORM_COMPLETE;
    goal = normalize_type_checked(ctx->arena, ctx->space, goal, &ctx->fuel,
                                  &status, 0);
    if (status != HE_NORM_COMPLETE) {
        chain_mark_normalization_incomplete(
            ctx, status, "goal-normalization-incomplete");
        return false;
    }

    ChainRefRange ranges[2];
    uint32_t nranges = chain_candidate_ranges(ctx, atom_head_symbol_id(goal),
                                               ranges);
    uint32_t considered = 0;
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
                                  uint32_t rank) {
    ChainWorkItem item = {0};
    item.kind = CHAIN_WORK_RESULT;
    item.proof = proof;
    item.type = type;
    item.continuation = continuation;
    item.rank = rank;
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
                               continuation->rank,
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
                                 continuation->parent, continuation->rank);
}

static void chain_process_candidate(ChainContext *ctx, ChainWorkQueue *queue,
                                    ChainWorkItem *item) {
    const ChainDecl *decl = &ctx->index.decls[item->decl_index];
    uint32_t epoch = fresh_var_suffix();
    if (!decl->is_rule) {
        ctx->stats.fact_candidates++;
        Atom *term = atom_freshen_epoch(ctx->arena, decl->term, epoch);
        Atom *type = atom_freshen_epoch(ctx->arena, decl->type, epoch);
        Atom *resolved = NULL;
        if (!chain_unify_must(ctx, type, item->goal, &item->env, &resolved))
            return;
        term = bindings_apply_if_vars(&item->env, ctx->arena, term);
        (void)chain_schedule_result(ctx, queue, term, resolved, &item->env,
                                    item->continuation, item->rank);
        return;
    }

    if (item->depth == 0 || !chain_take_fuel(ctx, 1)) return;
    ctx->stats.rule_candidates++;
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

static void chain_process_result(ChainContext *ctx, ChainWorkQueue *queue,
                                 ChainWorkItem *item, ChainProofVec *answers) {
    if (!item->continuation) {
        Atom *checked_type = NULL;
        if (!chain_proof_checked(ctx, item->proof, ctx->root_goal, &item->env,
                                 &checked_type))
            return;
        AnswerIdentityV1 identity;
        if (!answer_identity_v1_make(ctx, item->proof, checked_type,
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
    if (binder && !bindings_add_id(&item->env, binder->var_id,
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
    if (!chain_collect_query_vars(ctx, goal, 0)) return;
    ChainWorkQueue queue;
    chain_queue_init(&queue);
    Bindings initial;
    bindings_init(&initial);
    (void)chain_schedule_goal(ctx, &queue, goal, depth, &initial, NULL,
                              0,
                              !root_rules_only, true);
    bindings_free(&initial);

    while (queue.len > 0 && !ctx->incomplete && answers->count < limit) {
        ChainWorkItem item;
        if (!chain_queue_pop(&queue, &item)) break;
        ctx->stats.work_items++;
        if (item.kind == CHAIN_WORK_CANDIDATE)
            chain_process_candidate(ctx, &queue, &item);
        else
            chain_process_result(ctx, &queue, &item, answers);
        bindings_free(&item.env);
    }
    if (!ctx->incomplete && answers->count >= limit && queue.len > 0) {
        ctx->more_possible = true;
        ctx->incomplete_reason = "answer-limit";
    }
    chain_queue_free(&queue);
}

static Atom *chain_stats_atom(ChainContext *ctx) {
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
                              he_sym(ctx->arena, "typed-answer-v1"),
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
                                    uint32_t limit, bool first_only) {
    ChainContext ctx = {0};
    ctx.arena = a;
    ctx.space = space;
    ctx.fuel = fuel;
    if (!chain_index_build(&ctx)) {
        chain_index_free(&ctx.index);
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
            result = he_accept(a, atom_expr(a, decl, 3));
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
    return result;
}

/* One forward round.  fuel is an aggregate in/out budget shared with the
 * caller; incomplete_out reports honestly when the round could not finish
 * (fuel exhaustion or a failed index build), so a caller must never read
 * "nothing added" as a reached fixpoint on an incomplete round. */
static Atom *chain_forward_step(Arena *a, Space *space, uint64_t *fuel,
                                uint32_t limit, uint32_t *added_out,
                                bool *incomplete_out) {
    ChainContext ctx = {0};
    ctx.arena = a; ctx.space = space; ctx.fuel = *fuel;
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
    "type-of", "normalize-typing", "validate-type-properties",
    "is-consistent", "is-consistent-in", "type-consistency-kind",
    "check-typing", "validate-typing",
    "type-inhabit", "type-inhabit-first",
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
    if (strcmp(name, "type-of") == 0 ||
        strcmp(name, "normalize-typing") == 0 ||
        strcmp(name, "validate-type-properties") == 0)
        return arg_index == 1;
    if (strcmp(name, "is-consistent") == 0 ||
        strcmp(name, "type-consistency-kind") == 0)
        return arg_index == 0 || arg_index == 1;
    if (strcmp(name, "is-consistent-in") == 0)
        return arg_index == 1 || arg_index == 2;
    if (strcmp(name, "check-typing") == 0)
        return arg_index == 1 || arg_index == 2;
    if (strcmp(name, "type-inhabit") == 0 ||
        strcmp(name, "type-inhabit-first") == 0)
        return arg_index == 1;
    return false;
}

Atom *he_typing_dispatch(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head->kind != ATOM_SYMBOL) return NULL;
    const char *name = symbol_bytes(g_symbols, head->sym_id);
    if (!he_typing_is_op(name)) return NULL;
    if (!eval_current_profile_enables_dependent_telescope()) return NULL;

    if (strcmp(name, "type-of") == 0) {
        if (nargs != 2)
            return he_reject(a, he_reason(a, "type-of-needs-space-and-term"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 100000;
        HeTypeSet ts;
        set_init(&ts);
        if (!type_of(a, sp, unquote(args[1]), &fuel, &ts))
            return he_unknown(a, he_reason(a, "type-of-exhausted"));
        if (ts.overflow)
            return he_unknown(a, he_reason(a, "type-set-capacity"));
        Atom **e = arena_alloc(a, sizeof(Atom *) * (ts.count + 1));
        e[0] = he_sym(a, "type-set");
        for (uint32_t i = 0; i < ts.count; i++) e[i + 1] = ts.items[i];
        return atom_expr(a, e, (CettaExprLen)(ts.count + 1));
    }


    if (strcmp(name, "normalize-typing") == 0) {
        if (nargs != 3)
            return he_reject(a, he_reason(a, "normalize-typing-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[2], &fuel);
        if (bad) return bad;
        HeNormStatus ns = HE_NORM_COMPLETE;
        Atom *n = normalize_type_checked(a, sp, unquote(args[1]), &fuel, &ns,
                                         0);
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

    /* Validates the declared type-index-property refinements of a type (and
     * runs its admissible computations).  This is NOT a well-formedness
     * judgment: no universes, no binder-domain formation, no positivity. */
    if (strcmp(name, "validate-type-properties") == 0) {
        if (nargs != 3)
            return he_reject(a, he_reason(a, "validate-type-properties-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[2], &fuel);
        if (bad) return bad;
        Atom *detail = NULL;
        HeTypeValidity v = validate_type_properties(a, sp, unquote(args[1]),
                                                    &fuel, &detail, 0);
        if (v == HE_TYPE_VALID) return he_accept(a, unquote(args[1]));
        if (v == HE_TYPE_INVALID)
            return he_reject(a, detail ? detail : he_reason(a, "invalid-type"));
        return he_unknown(a, detail ? detail
                                    : he_reason(a, "type-validation-unknown"));
    }

    if (strcmp(name, "is-consistent-in") == 0) {
        if (nargs != 4)
            return he_reject(a, he_reason(a, "is-consistent-in-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[3], &fuel);
        if (bad) return bad;
        HeNormStatus as = HE_NORM_COMPLETE, bs = HE_NORM_COMPLETE;
        Atom *left = normalize_type_checked(a, sp, unquote(args[1]), &fuel, &as,
                                            0);
        Atom *right = normalize_type_checked(a, sp, unquote(args[2]), &fuel,
                                             &bs, 0);
        if (as != HE_NORM_COMPLETE || bs != HE_NORM_COMPLETE)
            return he_unknown(a, he_reason(a, "consistency-normalization-incomplete"));
        Bindings tb;
        bindings_init(&tb);
        HeEdge e = consistency_bind(left, right, &fuel, &tb);
        bindings_free(&tb);
        if (e == HE_UNKNOWN)
            return he_unknown(a, he_reason(a, "consistency-exhausted"));
        if (e == HE_NONE)
            return he_reject(a, he_reason2(a, "inconsistent",
                                           atom_expr2(a, left, right)));
        return he_accept(a, edge_atom(a, e));
    }


    if (strcmp(name, "is-consistent") == 0) {
        if (nargs != 2)
            return he_reject(a, he_reason(a, "is-consistent-needs-two-types"));
        uint64_t fuel = 100000;
        HeEdge e = consistency(args[0], args[1], &fuel);
        if (e == HE_UNKNOWN)
            return he_unknown(a, he_reason(a, "consistency-exhausted"));
        if (e == HE_NONE)
            return he_reject(a, he_reason2(a, "inconsistent",
                                           atom_expr2(a, args[0], args[1])));
        return he_accept(a, edge_atom(a, e));
    }

    if (strcmp(name, "type-consistency-kind") == 0) {
        if (nargs != 2)
            return he_reject(a, he_reason(a, "type-consistency-kind-needs-two-types"));
        uint64_t fuel = 100000;
        HeEdge e = consistency(args[0], args[1], &fuel);
        return he_wrap1(a, "consistency-kind", edge_atom(a, e));
    }

    if (strcmp(name, "check-typing") == 0) {
        if (nargs != 4)
            return he_reject(a, he_reason(a, "check-typing-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[3], &fuel);
        if (bad) return bad;
        return check_typing(a, sp, args[1], args[2], fuel);
    }


    if (strcmp(name, "type-inhabit") == 0) {
        if (nargs != 5)
            return he_reject(a, he_reason(a, "type-inhabit-arity"));
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
        return chain_inhabit_dispatch(a, sp, args[1], depth, fuel, limit,
                                      false);
    }

    if (strcmp(name, "type-inhabit-first") == 0) {
        if (nargs != 4)
            return he_reject(a, he_reason(a, "type-inhabit-first-arity"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint32_t depth = 0;
        Atom *bad = NULL;
        if (!chain_arg_nat(a, args[2], &depth, "depth-not-natural", &bad))
            return bad;
        uint64_t fuel = 0;
        bad = arg_fuel(a, args[3], &fuel);
        if (bad) return bad;
        return chain_inhabit_dispatch(a, sp, args[1], depth, fuel, 1,
                                      true);
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
        const char *closure_status = fixpoint ? "complete"
                                   : starved  ? "resource-incomplete"
                                              : "rounds-exhausted";
        Atom *e[4] = {he_sym(a, "forward-closure"), atom_int(a, attempted),
                      atom_int(a, (int64_t)total), he_sym(a, closure_status)};
        return he_accept(a, atom_expr(a, e, 4));
    }

    if (strcmp(name, "validate-typing") == 0) {
        if (nargs != 2)
            return he_reject(a, he_reason(a, "validate-typing-needs-space-and-fuel"));
        Space *sp = NULL;
        if (!arg_space(args[0], &sp))
            return he_reject(a, he_reason(a, "first-argument-not-a-space"));
        uint64_t fuel = 0;
        Atom *bad = arg_fuel(a, args[1], &fuel);
        if (bad) return bad;
        uint32_t len = 0;
        if (!space_length_u32_checked(sp, &len))
            return he_reject(a, he_reason(a, "space-too-large"));
        uint32_t total = 0, accept = 0, reject = 0, unknown = 0;
        for (uint32_t i = 0; i < len; i++) {
            Atom *at = space_get_at(sp, i);
            /* validate each (: term T) claim: is term consistent with T? */
            if (!at || at->kind != ATOM_EXPR || at->expr.len != 3) continue;
            if (!atom_is_symbol_id(at->expr.elems[0], g_builtin_syms.colon))
                continue;
            /* a declaration of a symbol's own type is definitional, not a
             * claim to re-derive; only check compound subjects */
            if (at->expr.elems[1]->kind != ATOM_EXPR) continue;
            total++;
            uint64_t row = fuel;
            Atom *v = check_typing_mode(a, sp, at->expr.elems[1],
                                        at->expr.elems[2], row, true);
            if (v->kind == ATOM_EXPR && v->expr.len >= 1 &&
                v->expr.elems[0]->kind == ATOM_SYMBOL) {
                const char *tag = symbol_bytes(g_symbols, v->expr.elems[0]->sym_id);
                if (strcmp(tag, "he-accept") == 0) accept++;
                else if (strcmp(tag, "he-unknown") == 0) unknown++;
                else {
                    reject++;
                    printf("HE-VALIDATE-REJECT %u ", i);
                    atom_print(at->expr.elems[1], stdout);
                    printf(" : ");
                    atom_print(v->expr.elems[1], stdout);
                    printf("\n");
                }
            }
        }
        fflush(stdout);
        printf("HE-VALIDATE total=%u accept=%u reject=%u unknown=%u\n",
               total, accept, reject, unknown);
        fflush(stdout);
        Atom *e[5];
        e[0] = he_sym(a, "he-validate-report");
        e[1] = atom_int(a, (int64_t)total);
        e[2] = atom_int(a, (int64_t)accept);
        e[3] = atom_int(a, (int64_t)reject);
        e[4] = atom_int(a, (int64_t)unknown);
        return atom_expr(a, e, 5);
    }

    return NULL;
}
