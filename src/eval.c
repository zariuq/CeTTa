#define _GNU_SOURCE
#include "eval.h"
#include "match.h"
#include "grounded.h"
#include "library.h"
#include "stats.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Global registry for named spaces/values (set by eval_top_with_registry) */
static Registry *g_registry = NULL;
/* Persistent arena for atoms that outlive a single evaluation (space, states) */
static Arena *g_persistent_arena = NULL;
/* Active importable library set */
static CettaLibraryContext *g_library_context = NULL;
static CettaEvalSession g_fallback_eval_session;
static bool g_fallback_eval_session_ready = false;

typedef struct {
    Space **items;
    uint32_t len, cap;
} TempSpaceSet;

static TempSpaceSet g_temp_spaces = {0};

static void ensure_fallback_eval_session(void) {
    if (g_fallback_eval_session_ready) return;
    cetta_eval_session_init_he_extended(&g_fallback_eval_session);
    g_fallback_eval_session_ready = true;
}

static CettaEvalSession *fallback_eval_session(void) {
    ensure_fallback_eval_session();
    return &g_fallback_eval_session;
}

static CettaEvalSession *active_eval_session(void) {
    if (g_library_context) {
        return &g_library_context->session;
    }
    return fallback_eval_session();
}

static const CettaEvaluatorOptions *active_eval_options_const(void) {
    return &active_eval_session()->options;
}

static CettaEvaluatorOptions *active_eval_options(void) {
    return &active_eval_session()->options;
}

static bool eval_type_check_auto_enabled(void) {
    return active_eval_options_const()->type_check_auto;
}

static bool eval_bare_minimal_enabled(void) {
    return cetta_evaluator_options_is_bare_minimal(active_eval_options_const());
}

static int current_eval_fuel_limit(void) {
    return cetta_evaluator_options_effective_fuel_limit(active_eval_options_const());
}

/* ── Result Set ─────────────────────────────────────────────────────────── */

void result_set_init(ResultSet *rs) {
    rs->items = NULL;
    rs->len = 0;
    rs->cap = 0;
}

void result_set_add(ResultSet *rs, Atom *atom) {
    if (rs->len >= rs->cap) {
        rs->cap = rs->cap ? rs->cap * 2 : 8;
        rs->items = cetta_realloc(rs->items, sizeof(Atom *) * rs->cap);
    }
    rs->items[rs->len++] = atom;
}

void result_set_free(ResultSet *rs) {
    free(rs->items);
    rs->items = NULL;
    rs->len = rs->cap = 0;
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

static bool expr_head_is_id(Atom *a, SymbolId id) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol_id(a->expr.elems[0], id);
}

static uint32_t expr_nargs(Atom *a) {
    return a->kind == ATOM_EXPR ? a->expr.len - 1 : 0;
}

static Atom *expr_arg(Atom *a, uint32_t i) {
    return a->expr.elems[i + 1];
}

static bool is_true_atom(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.true_text) ||
           (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL && a->ground.bval);
}

static bool atom_is_registry_token(Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL &&
           symbol_bytes(g_symbols, atom->sym_id)[0] == '&';
}

static Atom *registry_lookup_atom(Atom *atom) {
    if (!g_registry || !atom_is_registry_token(atom)) return NULL;
    return registry_lookup_id(g_registry, atom->sym_id);
}

/* ── Outcome set (unified result type: atom + bindings) ─────────────────── */

void outcome_set_init(OutcomeSet *os) {
    os->items = NULL;
    os->len = 0;
    os->cap = 0;
}

static void outcome_move(Outcome *dst, Outcome *src) {
    dst->atom = src->atom;
    bindings_move(&dst->env, &src->env);
}

static void bindings_array_free(Bindings *items, uint32_t len) {
    if (!items) return;
    for (uint32_t i = 0; i < len; i++)
        bindings_free(&items[i]);
    free(items);
}

void outcome_set_add(OutcomeSet *os, Atom *atom, const Bindings *env) {
    if (os->len >= os->cap) {
        os->cap = os->cap ? os->cap * 2 : 8;
        os->items = cetta_realloc(os->items, sizeof(Outcome) * os->cap);
    }
    os->items[os->len].atom = atom;
    if (!bindings_clone(&os->items[os->len].env, env))
        return;
    os->len++;
}

void outcome_set_add_move(OutcomeSet *os, Atom *atom, Bindings *env) {
    if (os->len >= os->cap) {
        os->cap = os->cap ? os->cap * 2 : 8;
        os->items = cetta_realloc(os->items, sizeof(Outcome) * os->cap);
    }
    os->items[os->len].atom = atom;
    bindings_move(&os->items[os->len].env, env);
    os->len++;
}

static void outcome_set_filter_errors_if_success(OutcomeSet *os) {
    bool has_success = false;
    for (uint32_t i = 0; i < os->len; i++) {
        if (!atom_is_error(os->items[i].atom)) {
            has_success = true;
            break;
        }
    }
    if (!has_success) return;

    uint32_t out = 0;
    for (uint32_t i = 0; i < os->len; i++) {
        if (atom_is_error(os->items[i].atom))
            continue;
        if (out != i) {
            bindings_free(&os->items[out].env);
            outcome_move(&os->items[out], &os->items[i]);
        }
        out++;
    }
    for (uint32_t i = out; i < os->len; i++)
        bindings_free(&os->items[i].env);
    os->len = out;
}

void outcome_set_free(OutcomeSet *os) {
    for (uint32_t i = 0; i < os->len; i++)
        bindings_free(&os->items[i].env);
    free(os->items);
    os->items = NULL;
    os->len = os->cap = 0;
}

static Atom *make_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs);

static const CettaProfile *active_profile(void) {
    return g_library_context ? g_library_context->session.profile : NULL;
}

static Atom *profile_surface_error(Arena *a, Atom *call, const char *surface_name) {
    const CettaProfile *profile = active_profile();
    char buf[256];
    snprintf(buf, sizeof(buf), "surface %s is unavailable in profile %s",
             surface_name, profile ? profile->name : "unknown");
    return atom_error(a, call, atom_symbol(a, buf));
}

static Atom *bad_arg_type_error(Space *s, Arena *a, Atom *call, int64_t arg_index,
                                Atom *expected_type, Atom *actual) {
    Atom **actual_types = NULL;
    uint32_t nat = get_atom_types(s, a, actual, &actual_types);
    Atom *actual_type = (nat > 0) ? actual_types[0] : atom_undefined_type(a);
    Atom *reason = atom_expr(a, (Atom *[]) {
        atom_symbol(a, "BadArgType"),
        atom_int(a, arg_index),
        expected_type,
        actual_type
    }, 4);
    free(actual_types);
    return atom_error(a, call, reason);
}

static Atom *state_bad_arg_type_error(Space *s, Arena *a, Atom *call,
                                      int64_t arg_index, Atom *actual) {
    char fresh_name[48];
    snprintf(fresh_name, sizeof(fresh_name), "$__state#%u", fresh_var_suffix());
    Atom *expected_type =
        atom_expr2(a, atom_symbol(a, "StateMonad"), atom_var(a, fresh_name));
    return bad_arg_type_error(s, a, call, arg_index, expected_type, actual);
}

typedef enum {
    CETTA_SEARCH_POLICY_LANE_NONE = 0,
    CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF = 1,
    CETTA_SEARCH_POLICY_LANE_ATP_SATURATION = 2,
    CETTA_SEARCH_POLICY_LANE_SOLVER_ORACLE = 3
} CettaSearchPolicyLane;

typedef enum {
    CETTA_SEARCH_POLICY_ORDER_NATIVE = 0,
    CETTA_SEARCH_POLICY_ORDER_REVERSE = 1,
    CETTA_SEARCH_POLICY_ORDER_LEX = 2,
    CETTA_SEARCH_POLICY_ORDER_SHORTLEX = 3
} CettaSearchPolicyOrder;

typedef struct {
    bool present;
    CettaSearchPolicyLane lane;
    CettaSearchPolicyOrder order;
    Atom *policy_atom;
} CettaSearchPolicySpec;

typedef struct {
    Atom *raw_atom;
    Atom *render_atom;
    const Bindings *env;
    char *key;
    uint32_t ordinal;
} SearchEmitCandidate;

typedef enum {
    CETTA_SEARCH_POLICY_PARSE_NOT_POLICY = 0,
    CETTA_SEARCH_POLICY_PARSE_OK = 1,
    CETTA_SEARCH_POLICY_PARSE_ERROR = 2
} CettaSearchPolicyParseStatus;

static const char *search_policy_lane_name(CettaSearchPolicyLane lane) {
    switch (lane) {
    case CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF:
        return "recursive-dependent-proof";
    case CETTA_SEARCH_POLICY_LANE_ATP_SATURATION:
        return "atp-saturation";
    case CETTA_SEARCH_POLICY_LANE_SOLVER_ORACLE:
        return "solver-oracle";
    case CETTA_SEARCH_POLICY_LANE_NONE:
    default:
        return "none";
    }
}

static Atom *search_policy_reason_unknown(Arena *a, Atom *lane_atom) {
    return atom_expr2(a, atom_symbol(a, "UnknownSearchPolicy"), lane_atom);
}

static Atom *search_policy_reason_unavailable(Arena *a, CettaSearchPolicyLane lane) {
    return atom_expr2(a, atom_symbol(a, "SearchPolicyLaneUnavailable"),
                      atom_symbol(a, search_policy_lane_name(lane)));
}

static Atom *search_policy_reason_bad_order(Arena *a, Atom *order_atom) {
    return atom_expr2(a, atom_symbol(a, "UnsupportedSearchPolicyOrder"), order_atom);
}

static int compare_stream_candidates(const void *lhs, const void *rhs) {
    const SearchEmitCandidate *left = lhs;
    const SearchEmitCandidate *right = rhs;
    int cmp = strcmp(left->key, right->key);
    if (cmp != 0) return cmp;
    if (left->ordinal < right->ordinal) return -1;
    if (left->ordinal > right->ordinal) return 1;
    return 0;
}

static int compare_stream_candidates_shortlex(const void *lhs, const void *rhs) {
    const SearchEmitCandidate *left = lhs;
    const SearchEmitCandidate *right = rhs;
    size_t left_len = strlen(left->key);
    size_t right_len = strlen(right->key);
    if (left_len < right_len) return -1;
    if (left_len > right_len) return 1;
    int cmp = strcmp(left->key, right->key);
    if (cmp != 0) return cmp;
    if (left->ordinal < right->ordinal) return -1;
    if (left->ordinal > right->ordinal) return 1;
    return 0;
}

static Atom *search_policy_reason_bad_option(Arena *a, Atom *option_atom) {
    return atom_expr2(a, atom_symbol(a, "UnsupportedSearchPolicyOption"), option_atom);
}

static CettaSearchPolicyParseStatus parse_search_policy_atom(
    Arena *a, Atom *policy_atom, CettaSearchPolicySpec *spec, Atom **reason_out) {
    if (!expr_head_is_id(policy_atom, g_builtin_syms.search_policy))
        return CETTA_SEARCH_POLICY_PARSE_NOT_POLICY;
    if (spec) {
        spec->present = false;
        spec->lane = CETTA_SEARCH_POLICY_LANE_NONE;
        spec->order = CETTA_SEARCH_POLICY_ORDER_NATIVE;
        spec->policy_atom = policy_atom;
    }
    if (reason_out) *reason_out = NULL;

    uint32_t nargs = expr_nargs(policy_atom);
    if (nargs == 0 || nargs > 2) {
        if (reason_out) *reason_out = atom_symbol(a, "IncorrectNumberOfArguments");
        return CETTA_SEARCH_POLICY_PARSE_ERROR;
    }

    Atom *lane_atom = expr_arg(policy_atom, 0);
    if (lane_atom->kind != ATOM_SYMBOL) {
        if (reason_out) *reason_out = atom_symbol(a, "SearchPolicyLaneNameSymbolIsExpected");
        return CETTA_SEARCH_POLICY_PARSE_ERROR;
    }

    CettaSearchPolicyLane lane = CETTA_SEARCH_POLICY_LANE_NONE;
    if (atom_is_symbol_id(lane_atom, g_builtin_syms.recursive_dependent_proof)) {
        lane = CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF;
    } else if (atom_is_symbol_id(lane_atom, g_builtin_syms.atp_saturation)) {
        lane = CETTA_SEARCH_POLICY_LANE_ATP_SATURATION;
    } else if (atom_is_symbol_id(lane_atom, g_builtin_syms.solver_oracle)) {
        lane = CETTA_SEARCH_POLICY_LANE_SOLVER_ORACLE;
    } else {
        if (reason_out) *reason_out = search_policy_reason_unknown(a, lane_atom);
        return CETTA_SEARCH_POLICY_PARSE_ERROR;
    }

    CettaSearchPolicyOrder order = CETTA_SEARCH_POLICY_ORDER_NATIVE;
    if (nargs == 2) {
        Atom *option_atom = expr_arg(policy_atom, 1);
        if (!expr_head_is_id(option_atom, g_builtin_syms.order) || expr_nargs(option_atom) != 1) {
            if (reason_out) *reason_out = search_policy_reason_bad_option(a, option_atom);
            return CETTA_SEARCH_POLICY_PARSE_ERROR;
        }
        Atom *order_atom = expr_arg(option_atom, 0);
        if (order_atom->kind != ATOM_SYMBOL) {
            if (reason_out) *reason_out = atom_symbol(a, "SearchPolicyOrderSymbolIsExpected");
            return CETTA_SEARCH_POLICY_PARSE_ERROR;
        }
        if (atom_is_symbol_id(order_atom, g_builtin_syms.native)) {
            order = CETTA_SEARCH_POLICY_ORDER_NATIVE;
        } else if (atom_is_symbol_id(order_atom, g_builtin_syms.reverse)) {
            order = CETTA_SEARCH_POLICY_ORDER_REVERSE;
        } else if (atom_is_symbol_id(order_atom, g_builtin_syms.lex)) {
            order = CETTA_SEARCH_POLICY_ORDER_LEX;
        } else if (atom_is_symbol_id(order_atom, g_builtin_syms.shortlex)) {
            order = CETTA_SEARCH_POLICY_ORDER_SHORTLEX;
        } else {
            if (reason_out) *reason_out = search_policy_reason_bad_order(a, order_atom);
            return CETTA_SEARCH_POLICY_PARSE_ERROR;
        }
        if (lane != CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF &&
            order != CETTA_SEARCH_POLICY_ORDER_NATIVE) {
            if (reason_out) *reason_out = search_policy_reason_bad_order(a, order_atom);
            return CETTA_SEARCH_POLICY_PARSE_ERROR;
        }
    }

    if (spec) {
        spec->present = true;
        spec->lane = lane;
        spec->order = order;
    }
    return CETTA_SEARCH_POLICY_PARSE_OK;
}

static Atom *dispatch_native_op(Space *s, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *head_name = head ? atom_name_cstr(head) : NULL;
    if (head && head_name && active_profile() &&
        !cetta_profile_allows_surface(active_profile(), head_name)) {
        return profile_surface_error(a, make_call_expr(a, head, args, nargs), head_name);
    }
    Atom *result = grounded_dispatch(a, head, args, nargs);
    if (result) return result;
    if (g_library_context) {
        return cetta_library_dispatch_native(g_library_context, s, a, head, args, nargs);
    }
    return NULL;
}

static Atom *make_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1] = args[i];
    return atom_expr(a, elems, nargs + 1);
}

static bool is_capture_closure(Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_CAPTURE;
}

static Atom *materialize_runtime_token(Space *s, Arena *a, Atom *atom) {
    if (!atom_is_symbol_id(atom, g_builtin_syms.capture))
        return atom;
    Arena *dst = g_persistent_arena ? g_persistent_arena : a;
    CaptureClosure *closure = arena_alloc(dst, sizeof(CaptureClosure));
    closure->space_ptr = s;
    closure->options = *active_eval_options_const();
    return atom_capture(dst, closure);
}

static Atom *result_eval_type_hint(Atom *declared_type, Atom *result_atom) {
    if (result_atom && result_atom->kind == ATOM_EXPR && result_atom->expr.len >= 1 &&
        atom_is_symbol_id(result_atom->expr.elems[0], g_builtin_syms.function)) {
        return NULL;
    }
    return declared_type;
}

static bool bindings_merge_into(Bindings *dst, const Bindings *src) {
    return bindings_try_merge(dst, src);
}

static uint32_t get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types);

/* Forward-declared below with the rest of the evaluator entry points. */
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os);

static void stream_emit(Space *s, Arena *a, Atom *stream_expr, int fuel,
                        bool bounded, int64_t limit, bool preserve_bindings,
                        CettaSearchPolicyOrder order,
                        OutcomeSet *os) {
    Bindings _empty;
    bindings_init(&_empty);

    ResultBindSet inner;
    rb_set_init(&inner);
    metta_eval_bind(s, a, stream_expr, fuel, &inner);

    bool sorted_order = order == CETTA_SEARCH_POLICY_ORDER_LEX ||
                        order == CETTA_SEARCH_POLICY_ORDER_SHORTLEX;

    if (sorted_order) {
        uint32_t candidate_cap = inner.len > 0 ? inner.len : 1;
        SearchEmitCandidate *candidates =
            arena_alloc(a, sizeof(SearchEmitCandidate) * candidate_cap);
        uint32_t candidate_len = 0;
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i].atom;
            if (atom_is_empty(r))
                continue;
            Atom *render_atom = preserve_bindings
                ? bindings_apply(&inner.items[i].env, a, r)
                : r;
            candidates[candidate_len].raw_atom = r;
            candidates[candidate_len].render_atom = render_atom;
            candidates[candidate_len].env = &inner.items[i].env;
            candidates[candidate_len].key = atom_to_string(a, render_atom);
            candidates[candidate_len].ordinal = candidate_len;
            candidate_len++;
        }
        if (candidate_len == 0) {
            if (bounded && limit == 1)
                outcome_set_add(os, atom_empty(a), &_empty);
            else
                outcome_set_add(os, atom_expr(a, NULL, 0), &_empty);
            rb_set_free(&inner);
            return;
        }
        qsort(candidates, candidate_len, sizeof(SearchEmitCandidate),
              order == CETTA_SEARCH_POLICY_ORDER_LEX
                  ? compare_stream_candidates
                  : compare_stream_candidates_shortlex);
        if (bounded && limit == 1) {
            outcome_set_add(os, candidates[0].raw_atom, candidates[0].env);
            rb_set_free(&inner);
            return;
        }
        uint32_t sorted_cap = 0;
        if (bounded && limit > 0 && (uint64_t)limit < UINT32_MAX)
            sorted_cap = (uint32_t)limit;
        uint32_t count = candidate_len;
        if (bounded && sorted_cap > 0 && count > sorted_cap)
            count = sorted_cap;
        Atom **items = arena_alloc(a, sizeof(Atom *) * (count > 0 ? count : 1));
        for (uint32_t i = 0; i < count; i++) {
            items[i] = preserve_bindings
                ? candidates[i].render_atom
                : candidates[i].raw_atom;
        }
        outcome_set_add(os, atom_expr(a, items, count), &_empty);
        rb_set_free(&inner);
        return;
    }

    if (bounded && limit == 1) {
        bool emitted = false;
        if (order == CETTA_SEARCH_POLICY_ORDER_REVERSE) {
            for (uint32_t i = inner.len; i > 0; i--) {
                Atom *r = inner.items[i - 1].atom;
                if (atom_is_empty(r))
                    continue;
                outcome_set_add(os, r, &inner.items[i - 1].env);
                emitted = true;
                break;
            }
        } else {
            for (uint32_t i = 0; i < inner.len; i++) {
                Atom *r = inner.items[i].atom;
                if (atom_is_empty(r))
                    continue;
                outcome_set_add(os, r, &inner.items[i].env);
                emitted = true;
                break;
            }
        }
        if (!emitted)
            outcome_set_add(os, atom_empty(a), &_empty);
        rb_set_free(&inner);
        return;
    }

    uint32_t cap = 0;
    if (bounded && limit > 0 && (uint64_t)limit < UINT32_MAX)
        cap = (uint32_t)limit;
    if (bounded && limit == 0) {
        outcome_set_add(os, atom_unit(a), &_empty);
        rb_set_free(&inner);
        return;
    }
    uint32_t alloc_len = bounded ? (cap > 0 ? cap : 1) : (inner.len > 0 ? inner.len : 1);
    Atom **items = arena_alloc(a, sizeof(Atom *) * alloc_len);
    uint32_t count = 0;
    if (order == CETTA_SEARCH_POLICY_ORDER_REVERSE) {
        for (uint32_t i = inner.len; i > 0; i--) {
            Atom *r = inner.items[i - 1].atom;
            if (atom_is_empty(r))
                continue;
            if (bounded && cap > 0 && count >= cap)
                break;
            items[count++] = preserve_bindings
                ? bindings_apply(&inner.items[i - 1].env, a, r)
                : r;
        }
    } else {
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i].atom;
            if (atom_is_empty(r))
                continue;
            if (bounded && cap > 0 && count >= cap)
                break;
            items[count++] = preserve_bindings
                ? bindings_apply(&inner.items[i].env, a, r)
                : r;
        }
    }
    outcome_set_add(os, atom_expr(a, items, count), &_empty);
    rb_set_free(&inner);
}

static Atom *resolve_registry_refs_impl(Arena *a, Atom *atom, bool *changed_out) {
    Atom *val = registry_lookup_atom(atom);
    if (val) {
        *changed_out = true;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_REGISTRY_RESOLVE_HIT);
        return val;
    }
    if (atom->kind != ATOM_EXPR) {
        *changed_out = false;
        return atom;
    }

    Atom **new_elems = NULL;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        bool child_changed = false;
        Atom *resolved = resolve_registry_refs_impl(a, atom->expr.elems[i], &child_changed);
        if (child_changed && !new_elems) {
            new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            for (uint32_t j = 0; j < i; j++)
                new_elems[j] = atom->expr.elems[j];
        }
        if (new_elems)
            new_elems[i] = resolved;
    }
    if (!new_elems) {
        *changed_out = false;
        return atom;
    }
    *changed_out = true;
    return atom_expr(a, new_elems, atom->expr.len);
}

static Atom *resolve_registry_refs(Arena *a, Atom *atom) {
    bool changed = false;
    Atom *resolved = resolve_registry_refs_impl(a, atom, &changed);
    if (!changed) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_REGISTRY_RESOLVE_NOOP);
    } else if (atom->kind == ATOM_EXPR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_REGISTRY_RESOLVE_REWRITE);
    }
    return resolved;
}

static void temp_space_register(Space *space) {
    if (g_temp_spaces.len >= g_temp_spaces.cap) {
        g_temp_spaces.cap = g_temp_spaces.cap ? g_temp_spaces.cap * 2 : 4;
        g_temp_spaces.items = cetta_realloc(g_temp_spaces.items, sizeof(Space *) * g_temp_spaces.cap);
    }
    g_temp_spaces.items[g_temp_spaces.len++] = space;
}

static bool temp_space_is_registered(Space *space) {
    if (!space) return false;
    for (uint32_t i = 0; i < g_temp_spaces.len; i++) {
        if (g_temp_spaces.items[i] == space)
            return true;
    }
    return false;
}

static Space *space_persistent_clone(Space *src, Arena *dst) {
    Space *clone = arena_alloc(dst, sizeof(Space));
    space_init(clone);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (uint32_t i = 0; i < src->len; i++) {
        space_add(clone, atom_deep_copy_shared(dst, space_get_at(src, i)));
    }
    return clone;
}

void eval_release_temporary_spaces(void) {
    for (uint32_t i = 0; i < g_temp_spaces.len; i++) {
        space_free(g_temp_spaces.items[i]);
        free(g_temp_spaces.items[i]);
    }
    free(g_temp_spaces.items);
    g_temp_spaces.items = NULL;
    g_temp_spaces.len = 0;
    g_temp_spaces.cap = 0;
}

Registry *eval_current_registry(void) {
    return g_registry;
}

Arena *eval_current_persistent_arena(void) {
    return g_persistent_arena;
}

static const char *string_like_atom(Atom *atom) {
    if (!atom) return NULL;
    if (atom->kind == ATOM_SYMBOL) return atom_name_cstr(atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return atom->ground.sval;
    }
    return NULL;
}

static Atom *space_kind_atom(Arena *a, const Space *space) {
    return atom_symbol(a, space_kind_name(space ? space->kind : SPACE_KIND_ATOM));
}

static Atom *space_match_backend_atom(Arena *a, const Space *space) {
    return atom_symbol(a, space_match_backend_name(space));
}

static Atom *space_capabilities_atom(Arena *a, const Space *space) {
    const char *atom_caps[] = {
        "unordered", "match", "add", "remove", "len", "snapshot"
    };
    const char *stack_caps[] = {
        "ordered", "stack", "lifo", "match", "push", "peek", "pop", "get",
        "truncate", "len", "snapshot"
    };
    const char *queue_caps[] = {
        "ordered", "queue", "fifo", "match", "push", "peek", "pop", "get",
        "truncate", "len", "snapshot"
    };
    const char *hash_caps[] = {
        "unordered", "hash", "exact", "match", "add", "remove", "len",
        "snapshot", "nodup"
    };
    const char **caps = atom_caps;
    uint32_t count = sizeof(atom_caps) / sizeof(atom_caps[0]);
    if (space_is_stack(space)) {
        caps = stack_caps;
        count = sizeof(stack_caps) / sizeof(stack_caps[0]);
    } else if (space_is_queue(space)) {
        caps = queue_caps;
        count = sizeof(queue_caps) / sizeof(queue_caps[0]);
    } else if (space_is_hash(space)) {
        caps = hash_caps;
        count = sizeof(hash_caps) / sizeof(hash_caps[0]);
    }
    Atom **elems = arena_alloc(a, sizeof(Atom *) * count);
    for (uint32_t i = 0; i < count; i++) {
        elems[i] = atom_symbol(a, caps[i]);
    }
    return atom_expr(a, elems, count);
}

static const char *ordered_space_empty_error_symbol(const Space *space) {
    return space_is_queue(space) ? "EmptyQueueSpace" : "EmptyStackSpace";
}

typedef struct {
    Space *space;
    SymbolId bind_key;
    bool is_fresh;
} ImportDestination;

static ImportDestination resolve_import_destination(Arena *a, Atom *target, Atom **error_out) {
    ImportDestination dest = {0};
    if (!g_registry || !atom_is_registry_token(target)) {
        *error_out = atom_symbol(a, "import! destination must be &self or a fresh &name");
        return dest;
    }

    if (target->sym_id == g_builtin_syms.self) {
        Space *self = resolve_space(g_registry, target);
        if (!self) {
            *error_out = atom_symbol(a, "import! destination space not found");
        }
        dest.space = self;
        return dest;
    }

    if (registry_lookup_id(g_registry, target->sym_id)) {
        *error_out = atom_symbol(a,
            "import! destination must be a new &name, or &self");
        return dest;
    }

    Space *ns = cetta_malloc(sizeof(Space));
    space_init(ns);
    dest.space = ns;
    dest.bind_key = target->sym_id;
    dest.is_fresh = true;
    return dest;
}

static void collect_resolved_spaces(Space *s, Arena *a, Atom *space_expr,
                                    int fuel, Space ***spaces_out,
                                    uint32_t *len_out) {
    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);

    Space **spaces = NULL;
    uint32_t len = 0;
    if (rs.len > 0) {
        spaces = cetta_malloc(sizeof(Space *) * rs.len);
        for (uint32_t i = 0; i < rs.len; i++) {
            Space *sp = resolve_space(g_registry, rs.items[i]);
            if (sp) spaces[len++] = sp;
        }
    }
    free(rs.items);
    *spaces_out = spaces;
    *len_out = len;
}

static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr, int fuel) {
    if (!g_registry) return NULL;

    Atom *direct = resolve_registry_refs(a, space_expr);
    Space *sp = resolve_space(g_registry, direct);
    if (sp) return sp;

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (uint32_t i = 0; i < rs.len; i++) {
        sp = resolve_space(g_registry, rs.items[i]);
        if (sp) break;
    }
    free(rs.items);
    return sp;
}

static Atom *space_arg_error(Arena *a, Atom *call, const char *message) {
    return atom_error(a, call, atom_symbol(a, message));
}

static Atom *call_signature_error(Arena *a, Atom *call, const char *expected) {
    char buf[1024];
    int pos = snprintf(buf, sizeof(buf), "expected: %s, found: ", expected);
    if (pos < 0) pos = 0;
    if ((size_t)pos < sizeof(buf)) {
        FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
        if (tmp) {
            atom_print(call, tmp);
            pos += (int)ftell(tmp);
            fclose(tmp);
        }
    }
    buf[sizeof(buf) - 1] = '\0';
    return atom_error(a, call, atom_symbol(a, buf));
}

static void result_set_resolve_registry_refs(Arena *a, ResultSet *rs) {
    for (uint32_t i = 0; i < rs->len; i++) {
        rs->items[i] = resolve_registry_refs(a, rs->items[i]);
    }
}

/* Lightweight snapshot: shares atom pointers (no deep copy).
   The snapshot's atoms[] is a frozen copy of the source's pointer array.
   Safe because: atoms in the source live in the persistent arena and are
   never mutated or freed during evaluation.  The snapshot only needs its
   own atoms[] array (malloc'd) and indexes; space_free handles that. */
static Space *space_snapshot_clone(Space *src, Arena *a) {
    (void)a;
    Space *clone = cetta_malloc(sizeof(Space));
    space_init(clone);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    if (src->len > 0) {
        clone->cap = src->len;
        clone->atoms = cetta_malloc(sizeof(Atom *) * clone->cap);
        memcpy(clone->atoms, src->atoms, sizeof(Atom *) * src->len);
        clone->len = src->len;
    }
    /* Backend indexes are rebuilt lazily on first match against the snapshot. */
    temp_space_register(clone);
    return clone;
}

static Atom *runtime_stats_inventory_atom(Arena *a) {
    Space *inventory = cetta_malloc(sizeof(Space));
    CettaRuntimeStats stats;
    space_init(inventory);
    temp_space_register(inventory);
    cetta_runtime_stats_snapshot(&stats);
    cetta_runtime_stats_populate_space(inventory, a, &stats);
    return atom_space(a, inventory);
}

/* ── Function type utilities (Types.lean:260-281) ──────────────────────── */

static bool is_function_type(Atom *a) {
    /* HE uses (-> (->)) for zero-argument functions, so a valid function type
       can have just the arrow head plus a return type. */
    return a->kind == ATOM_EXPR && a->expr.len >= 2 &&
           atom_is_symbol_id(a->expr.elems[0], g_builtin_syms.arrow);
}

/* Extract arg types from (-> t1 t2 ... tN ret). Returns count.
   Writes to out[], up to max entries. Excludes "->" and last (return type). */
static uint32_t get_function_arg_types(Atom *ft, Atom **out, uint32_t max) {
    if (!is_function_type(ft)) return 0;
    uint32_t n = ft->expr.len - 2; /* skip "->" at [0] and ret at [len-1] */
    if (n > max) n = max;
    for (uint32_t i = 0; i < n; i++)
        out[i] = ft->expr.elems[i + 1];
    return n;
}

static Atom *get_function_ret_type(Atom *ft) {
    if (!is_function_type(ft)) return NULL;
    return ft->expr.elems[ft->expr.len - 1];
}

static bool eval_dependent_telescope_enabled(void) {
    return cetta_profile_enables_dependent_telescope(active_eval_session()->profile);
}

static bool split_dependent_domain(Atom *domain, Atom **binder_out, Atom **type_out) {
    if (eval_dependent_telescope_enabled() &&
        domain &&
        domain->kind == ATOM_EXPR &&
        domain->expr.len == 3 &&
        atom_is_symbol_id(domain->expr.elems[0], g_builtin_syms.colon) &&
        domain->expr.elems[1]->kind == ATOM_VAR) {
        *binder_out = domain->expr.elems[1];
        *type_out = domain->expr.elems[2];
        return true;
    }
    *binder_out = NULL;
    *type_out = domain;
    return false;
}

static Atom *function_domain_type(Bindings *env, Arena *a, Atom *domain, Atom **binder_out) {
    Atom *binder = NULL;
    Atom *body = domain;
    split_dependent_domain(domain, &binder, &body);
    if (binder_out) *binder_out = binder;
    return bindings_apply(env, a, body);
}

static bool bind_domain_binder(Bindings *env, Atom *domain, Atom *term) {
    Atom *binder = NULL;
    Atom *body = domain;
    if (!split_dependent_domain(domain, &binder, &body) || !binder) {
        return true;
    }
    return bindings_add_var(env, binder, term);
}

static Atom *normalize_type_expr_profiled(Arena *a, Atom *ty) {
    if (ty->kind != ATOM_EXPR || ty->expr.len < 2) return ty;
    Atom **new_elems = arena_alloc(a, sizeof(Atom *) * ty->expr.len);
    bool changed = false;
    for (uint32_t i = 0; i < ty->expr.len; i++) {
        new_elems[i] = normalize_type_expr_profiled(a, ty->expr.elems[i]);
        if (new_elems[i] != ty->expr.elems[i]) changed = true;
    }
    Atom *norm = changed ? atom_expr(a, new_elems, ty->expr.len) : ty;
    SymbolId op_id = SYMBOL_ID_NONE;
    if (norm->expr.elems[0]->kind == ATOM_SYMBOL)
        op_id = norm->expr.elems[0]->sym_id;
    if (norm->expr.len >= 3 && op_id != SYMBOL_ID_NONE && is_grounded_op(op_id)) {
        Atom *result = grounded_dispatch(a, norm->expr.elems[0],
                                         norm->expr.elems + 1, norm->expr.len - 1);
        if (result) return result;
    }
    return norm;
}

static uint32_t infer_dependent_application_types(Space *s, Arena *a, Atom *atom,
                                                  Atom ***out_types) {
    Atom *op = atom->expr.elems[0];
    Atom **op_types = NULL;
    uint32_t nop = get_atom_types_profiled(s, a, op, &op_types);
    Atom **types = NULL;
    uint32_t count = 0;
    bool tried_func_type = false;

    for (uint32_t oi = 0; oi < nop; oi++) {
        Atom *ft = op_types[oi];
        if (!is_function_type(ft)) {
            continue;
        }
        tried_func_type = true;
        if (ft->expr.len - 2 != atom->expr.len - 1) {
            continue;
        }

        Atom *fresh_ft = rename_vars(a, ft, fresh_var_suffix());
        Atom *fresh_ret = get_function_ret_type(fresh_ft);
        Bindings tb;
        bindings_init(&tb);
        bool all_ok = true;

        for (uint32_t ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
            Atom *binder = NULL;
            Atom *decl = function_domain_type(&tb, a, fresh_ft->expr.elems[ai + 1], &binder);
            Atom **atypes = NULL;
            uint32_t nat = get_atom_types_profiled(s, a, atom->expr.elems[ai + 1], &atypes);
            bool found = false;

            for (uint32_t ti = 0; ti < nat; ti++) {
                Bindings trial;
                if (!bindings_clone(&trial, &tb)) {
                    continue;
                }
                if (match_types(decl, atypes[ti], &trial)) {
                    Atom *arg_term = bindings_apply(&trial, a, atom->expr.elems[ai + 1]);
                    if (bind_domain_binder(&trial, fresh_ft->expr.elems[ai + 1], arg_term)) {
                        bindings_replace(&tb, &trial);
                        found = true;
                        bindings_free(&trial);
                        break;
                    }
                }
                bindings_free(&trial);
            }
            free(atypes);
            if (!found) {
                all_ok = false;
            }
        }

        if (all_ok) {
            Atom *concrete_ret =
                normalize_type_expr_profiled(a, bindings_apply(&tb, a, fresh_ret));
            types = types ? cetta_realloc(types, sizeof(Atom *) * (count + 1))
                          : cetta_malloc(sizeof(Atom *));
            types[count++] = concrete_ret;
        }
        bindings_free(&tb);
    }

    free(op_types);
    if (tried_func_type && count == 0) {
        *out_types = NULL;
        return 0;
    }
    *out_types = types;
    return count;
}

static uint32_t get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types) {
    uint32_t count = get_atom_types(s, a, atom, out_types);
    if (!eval_dependent_telescope_enabled() || atom->kind != ATOM_EXPR || count != 0) {
        return count;
    }
    return infer_dependent_application_types(s, a, atom, out_types);
}

/* ── Type cast (TypeCheck.lean:126-148) ────────────────────────────────── */

static void type_cast_fn(Space *s, Arena *a, Atom *atom, Atom *expectedType,
                         int fuel, ResultSet *rs) {
    Atom **types;
    uint32_t ntypes = get_atom_types_profiled(s, a, atom, &types);
    /* Try each type — return on FIRST match (early return per spec) */
    for (uint32_t i = 0; i < ntypes; i++) {
        Bindings mb;
        bindings_init(&mb);
        if (match_types(types[i], expectedType, &mb)) {
            bindings_free(&mb);
            result_set_add(rs, atom);
            free(types);
            return;
        }
        bindings_free(&mb);
    }
    /* No match — return errors for all types */
    for (uint32_t i = 0; i < ntypes; i++) {
        Atom *reason = atom_expr3(a, atom_symbol(a, "BadType"),
                                  expectedType, types[i]);
        result_set_add(rs, atom_error(a, atom, reason));
    }
    free(types);
}

/* ── Check if function type is applicable (TypeCheck.lean:55-116) ──────── */

/* Returns true if applicable, filling success_bindings[0..n_success-1].
   Returns false if not applicable, filling errors[0..n_errors-1]. */
static bool check_function_applicable(
    Atom *expr, Atom *funcType, Atom *expectedType,
    Space *s, Arena *a, int fuel,
    /* out */ Atom **errors, uint32_t *n_errors,
    Bindings *success_bindings, uint32_t *n_success) {

    *n_errors = 0;
    *n_success = 0;

    Atom *arg_types[32];
    uint32_t nargs = get_function_arg_types(funcType, arg_types, 32);
    uint32_t expr_narg = (expr->kind == ATOM_EXPR && expr->expr.len > 0)
                         ? expr->expr.len - 1 : 0;

    /* Step 1: arity check */
    if (expr_narg != nargs) {
        errors[(*n_errors)++] = atom_error(a, expr,
            atom_symbol(a, "IncorrectNumberOfArguments"));
        return false;
    }

    Atom *retType = get_function_ret_type(funcType);
    if (!retType) retType = atom_undefined_type(a);

    /* Step 2: check each argument type, threading bindings */
    Bindings results[64];
    for (uint32_t i = 0; i < 64; i++) bindings_init(&results[i]);
    uint32_t nresults = 1;
    bindings_init(&results[0]);

    for (uint32_t i = 0; i < nargs && nresults > 0; i++) {
        Atom *arg = expr->expr.elems[i + 1];
        Bindings next[64];
        for (uint32_t ni = 0; ni < 64; ni++) bindings_init(&next[ni]);
        uint32_t nnext = 0;

        for (uint32_t r = 0; r < nresults; r++) {
            bool found = false;
            /* Apply accumulated bindings to expected arg type
               (resolves type variables bound by previous args) */
            Atom *expected =
                function_domain_type(&results[r], a, arg_types[i], NULL);
            if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
                atom_is_symbol_id(expected, g_builtin_syms.undefined_type)) {
                if (nnext < 64 && bindings_clone(&next[nnext], &results[r]))
                    nnext++;
                continue;
            }

            Atom **atypes;
            uint32_t natypes = get_atom_types_profiled(s, a, arg, &atypes);
            if (natypes == 0) {
                if (nnext < 64 && bindings_clone(&next[nnext], &results[r]))
                    nnext++;
                free(atypes);
                continue;
            }
            for (uint32_t t = 0; t < natypes; t++) {
                Bindings mb;
                if (!bindings_clone(&mb, &results[r]))
                    continue;
                if (match_types(expected, atypes[t], &mb)) {
                    if (!bind_domain_binder(&mb, arg_types[i], arg)) {
                        bindings_free(&mb);
                        continue;
                    }
                    if (nnext < 64) {
                        bindings_move(&next[nnext], &mb);
                        nnext++;
                    }
                    found = true;
                }
                bindings_free(&mb);
            }
            if (!found && natypes > 0) {
                /* Report first mismatching type */
                Atom *reason = atom_expr(a, (Atom*[]){
                    atom_symbol(a, "BadArgType"),
                    atom_int(a, (int64_t)(i + 1)),
                    expected,
                    atypes[0]
                }, 4);
                if (*n_errors < 64)
                    errors[(*n_errors)++] = atom_error(a, expr, reason);
            }
            free(atypes);
        }
        for (uint32_t r = 0; r < nresults; r++)
            bindings_free(&results[r]);
        for (uint32_t ni = 0; ni < nnext; ni++)
            bindings_move(&results[ni], &next[ni]);
        for (uint32_t ni = nnext; ni < 64; ni++)
            bindings_free(&next[ni]);
        nresults = nnext;
    }

    if (nresults == 0) {
        for (uint32_t i = 0; i < 64; i++)
            bindings_free(&results[i]);
        return false;
    }

    /* Step 3: check return type */
    uint32_t ret_ok = 0;
    for (uint32_t r = 0; r < nresults; r++) {
        Bindings mb;
        if (!bindings_clone(&mb, &results[r]))
            continue;
        Atom *inst_ret =
            eval_dependent_telescope_enabled() ? bindings_apply(&results[r], a, retType) : retType;
        if (match_types(expectedType, inst_ret, &mb)) {
            if (ret_ok < 64) {
                bindings_move(&success_bindings[ret_ok], &mb);
                ret_ok++;
            }
        } else {
            Atom *reason = atom_expr3(a, atom_symbol(a, "BadType"),
                                      expectedType, inst_ret);
            if (*n_errors < 64)
                errors[(*n_errors)++] = atom_error(a, expr, reason);
        }
        bindings_free(&mb);
    }

    for (uint32_t r = 0; r < nresults; r++)
        bindings_free(&results[r]);
    *n_success = ret_ok;
    return ret_ok > 0;
}

/* ── Forward declarations ───────────────────────────────────────────────── */

static void metta_call(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                       bool preserve_bindings, OutcomeSet *os);
/* Like metta_eval but also returns bindings produced by equation queries */
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os);
/* Like metta_eval but preserves bindings in an OutcomeSet result. */
static void metta_eval_bind_typed(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, OutcomeSet *os);

/* ── metta_eval: full recursive evaluation (metta.md lines 240-272) ────── */

void metta_eval(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, ResultSet *rs) {
    if (fuel == 0) {
        /* Fuel exhausted — return empty result set (matches HE behavior:
           infinite recursion produces no output, not an error atom) */
        return;
    }

    Atom *etype = type ? type : atom_undefined_type(a);

    /* Resolve registry tokens (&name → bound value) */
    Atom *bound = registry_lookup_atom(atom);
    if (bound) {
        result_set_add(rs, bound);
        return;
    }
    atom = materialize_runtime_token(s, a, atom);

    /* Empty/Error: return as-is (spec line 253) */
    if (atom_is_empty(atom) || atom_is_error(atom)) {
        result_set_add(rs, atom);
        return;
    }

    /* Type == Atom or matches meta-type, or meta-type is Variable:
       return as-is (spec line 255) — THIS is the laziness control */
    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol_id(etype, g_builtin_syms.atom) || atom_eq(etype, meta) ||
        atom_is_symbol_id(meta, g_builtin_syms.variable)) {
        result_set_add(rs, atom);
        return;
    }

    /* Symbol/Grounded/empty-expr: typeCast (spec line 260) */
    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        type_cast_fn(s, a, atom, etype, fuel, rs);
        return;
    }

    /* Variable: return as-is (already handled by meta==Variable above,
       but be safe) */
    if (atom->kind == ATOM_VAR) {
        result_set_add(rs, atom);
        return;
    }

    /* Expression: interpret_expression → metta_call (spec line 262) */
    {
        OutcomeSet os;
        outcome_set_init(&os);
        metta_call(s, a, atom, etype, fuel > 0 ? fuel - 1 : fuel, false, &os);
        outcome_set_filter_errors_if_success(&os);
        for (uint32_t oi = 0; oi < os.len; oi++)
            result_set_add(rs, os.items[oi].atom);
        outcome_set_free(&os);
    }
}

/* ── metta_eval_bind: like metta_eval but returns bindings too ──────────── */
/* Used by interpret_tuple to thread bindings between sub-expressions.
   For most atoms, bindings are empty. For equation queries, bindings
   contain variable assignments from bidirectional matching. */

static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);

    Atom *bound = registry_lookup_atom(atom);
    if (bound) {
        outcome_set_add(os, bound, &empty);
        return;
    }
    atom = materialize_runtime_token(s, a, atom);

    if (fuel == 0 || atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        atom->kind == ATOM_VAR ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        outcome_set_add(os, atom, &empty);
        return;
    }
    metta_call(s, a, atom, NULL, fuel > 0 ? fuel - 1 : fuel, true, os);
    outcome_set_filter_errors_if_success(os);
}

static void metta_eval_bind_typed(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);

    if (!type) {
        metta_eval_bind(s, a, atom, fuel, os);
        return;
    }

    if (fuel == 0) {
        return;
    }

    Atom *etype = type ? type : atom_undefined_type(a);

    Atom *bound = registry_lookup_atom(atom);
    if (bound) {
        outcome_set_add(os, bound, &empty);
        return;
    }
    atom = materialize_runtime_token(s, a, atom);

    if (atom_is_empty(atom) || atom_is_error(atom)) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol_id(etype, g_builtin_syms.atom) || atom_eq(etype, meta) ||
        atom_is_symbol_id(meta, g_builtin_syms.variable)) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        ResultSet rs;
        result_set_init(&rs);
        type_cast_fn(s, a, atom, etype, fuel, &rs);
        for (uint32_t i = 0; i < rs.len; i++)
            outcome_set_add(os, rs.items[i], &empty);
        result_set_free(&rs);
        return;
    }

    if (atom->kind == ATOM_VAR) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    metta_call(s, a, atom, etype, fuel > 0 ? fuel - 1 : fuel, true, os);
    outcome_set_filter_errors_if_success(os);
}

static void eval_with_prefix_bindings(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                      const Bindings *prefix, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    metta_eval_bind_typed(s, a, type, atom, fuel, &inner);
    for (uint32_t i = 0; i < inner.len; i++) {
        Bindings merged;
        if (!bindings_clone_merge(&merged, prefix, &inner.items[i].env))
            continue;
        outcome_set_add_move(os, bindings_apply(&merged, a, inner.items[i].atom), &merged);
        bindings_free(&merged);
    }
    outcome_set_free(&inner);
}

static void outcome_set_add_prefixed(Arena *a, OutcomeSet *os, Atom *atom,
                                     const Bindings *local_env,
                                     const Bindings *outer_env,
                                     bool preserve_bindings) {
    Bindings empty;
    bindings_init(&empty);
    const Bindings *inner = local_env ? local_env : &empty;

    if (!preserve_bindings) {
        outcome_set_add(os, atom, &empty);
        return;
    }
    if (!outer_env || outer_env->len == 0) {
        Atom *applied = (inner->len == 0) ? atom : bindings_apply((Bindings *)inner, a, atom);
        outcome_set_add(os, applied, inner);
        return;
    }

    Bindings merged;
    if (!bindings_clone_merge(&merged, outer_env, inner))
        return;
    outcome_set_add_move(os, bindings_apply(&merged, a, atom), &merged);
    bindings_free(&merged);
}

static void outcome_set_append_prefixed(Arena *a, OutcomeSet *dst, OutcomeSet *src,
                                        const Bindings *outer_env,
                                        bool preserve_bindings) {
    for (uint32_t i = 0; i < src->len; i++) {
        outcome_set_add_prefixed(a, dst, src->items[i].atom, &src->items[i].env,
                                 outer_env, preserve_bindings);
    }
}

/* When the caller only cares about atoms, apply pending bindings before
   evaluation and continue through metta_eval so recursive branches can keep
   using the atom-only tail-call path. */
static void eval_for_caller(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                            const Bindings *prefix, bool preserve_bindings,
                            OutcomeSet *os) {
    if (preserve_bindings) {
        eval_with_prefix_bindings(s, a, type, atom, fuel, prefix, os);
        return;
    }

    Atom *applied = (!prefix || prefix->len == 0)
        ? atom
        : bindings_apply((Bindings *)prefix, a, atom);
    ResultSet rs;
    result_set_init(&rs);
    Bindings empty;
    bindings_init(&empty);
    metta_eval(s, a, type, applied, fuel, &rs);
    for (uint32_t i = 0; i < rs.len; i++)
        outcome_set_add(os, rs.items[i], &empty);
    result_set_free(&rs);
}

static void eval_direct_outcomes(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                 OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    eval_for_caller(s, a, type, atom, fuel, &empty, false, os);
}

static __attribute__((unused)) void
eval_for_current_caller(Space *s, Arena *a, Atom *type, Atom *atom,
                        int fuel, const Bindings *prefix,
                        const Bindings *outer_env,
                        bool preserve_bindings, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    eval_for_caller(s, a, type, atom, fuel, prefix, preserve_bindings, &inner);
    outcome_set_append_prefixed(a, os, &inner, outer_env, preserve_bindings);
    outcome_set_free(&inner);
}

static __attribute__((unused)) void
eval_direct_for_current(Space *s, Arena *a, Atom *type, Atom *atom,
                        int fuel, const Bindings *outer_env,
                        bool preserve_bindings, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    eval_direct_outcomes(s, a, type, atom, fuel, &inner);
    outcome_set_append_prefixed(a, os, &inner, outer_env, preserve_bindings);
    outcome_set_free(&inner);
}

static void interpret_function_args(Space *s, Arena *a, Atom *op,
                                    Atom **orig_args, Atom **arg_types, uint32_t nargs,
                                    uint32_t idx, Atom **prefix,
                                    const Bindings *env, int fuel, OutcomeSet *os) {
    if (idx == nargs) {
        Atom **call_elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
        call_elems[0] = op;
        for (uint32_t i = 0; i < nargs; i++)
            call_elems[i + 1] = prefix[i];
        outcome_set_add(os, atom_expr(a, call_elems, nargs + 1), env);
        return;
    }

    Atom *orig_arg = orig_args[idx];
    Atom *arg_type = function_domain_type((Bindings *)env, a, arg_types[idx], NULL);
    Atom *bound_arg = bindings_apply((Bindings *)env, a, orig_arg);
    if (atom_is_symbol_id(arg_type, g_builtin_syms.atom) || orig_arg->kind == ATOM_VAR) {
        prefix[idx] = bound_arg;
        if (eval_dependent_telescope_enabled()) {
            Bindings merged;
            if (!bindings_clone(&merged, env))
                return;
            if (!bind_domain_binder(&merged, arg_types[idx], bound_arg)) {
                bindings_free(&merged);
                return;
            }
            interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                    idx + 1, prefix, &merged, fuel, os);
            bindings_free(&merged);
        } else {
            interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                    idx + 1, prefix, env, fuel, os);
        }
        return;
    }

    OutcomeSet arg_os;
    outcome_set_init(&arg_os);
    metta_eval_bind_typed(s, a, arg_type, bound_arg, fuel, &arg_os);

    for (uint32_t i = 0; i < arg_os.len; i++) {
        Bindings merged;
        if (!bindings_clone_merge(&merged, env, &arg_os.items[i].env))
            continue;
        if (atom_is_empty_or_error(arg_os.items[i].atom) &&
            !atom_eq(arg_os.items[i].atom, orig_arg)) {
            outcome_set_add_move(os, arg_os.items[i].atom, &merged);
            bindings_free(&merged);
            continue;
        }
        prefix[idx] = arg_os.items[i].atom;
        if (!bind_domain_binder(&merged, arg_types[idx], arg_os.items[i].atom)) {
            bindings_free(&merged);
            continue;
        }
        interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                idx + 1, prefix, &merged, fuel, os);
        bindings_free(&merged);
    }
    outcome_set_free(&arg_os);
}

static void dispatch_capture_outcomes(Space *s, Arena *a, Atom *head, Atom **args,
                                      uint32_t nargs, int fuel,
                                      const Bindings *prefix,
                                      bool preserve_bindings, OutcomeSet *os) {
    if (!is_capture_closure(head))
        return;

    if (nargs != 1) {
        Atom *err = atom_error(a, make_call_expr(a, head, args, nargs),
                               atom_symbol(a, "IncorrectNumberOfArguments"));
        outcome_set_add(os, err, prefix);
        return;
    }

    CaptureClosure *closure = (CaptureClosure *)head->ground.ptr;
    CettaEvaluatorOptions saved_options = *active_eval_options_const();
    *active_eval_options() = closure->options;
    eval_for_caller((Space *)closure->space_ptr, a, NULL, args[0], fuel, prefix,
                    preserve_bindings, os);
    *active_eval_options() = saved_options;
}

static bool dispatch_foreign_outcomes(Space *s, Arena *a, Atom *head,
                                      Atom **args, uint32_t nargs,
                                      Atom *result_type, int fuel,
                                      const Bindings *prefix,
                                      bool allow_tail,
                                      bool preserve_bindings,
                                      Atom **tail_next, Atom **tail_type,
                                      OutcomeSet *os) {
    if (!g_library_context || !g_library_context->foreign_runtime ||
        !cetta_foreign_is_callable_atom(head)) {
        return false;
    }

    ResultSet rs;
    result_set_init(&rs);
    Atom *error = NULL;
    bool ok = cetta_foreign_call(g_library_context->foreign_runtime, s, a, head,
                                 args, nargs, &rs, &error);
    if (!ok) {
        if (error) {
            eval_for_caller(s, a, result_type, error, fuel, prefix,
                            preserve_bindings, os);
        }
        result_set_free(&rs);
        return true;
    }

    if (allow_tail && rs.len == 1) {
        *tail_next = (prefix->len == 0)
            ? rs.items[0]
            : bindings_apply((Bindings *)prefix, a, rs.items[0]);
        *tail_type = result_type;
        result_set_free(&rs);
        return true;
    }

    for (uint32_t i = 0; i < rs.len; i++)
        eval_for_caller(s, a, result_type, rs.items[i], fuel, prefix,
                        preserve_bindings, os);
    result_set_free(&rs);
    return true;
}

static bool try_dynamic_capture_dispatch(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                                         bool preserve_bindings, OutcomeSet *os) {
    if (atom->kind != ATOM_EXPR || atom->expr.len < 1)
        return false;

    Atom *op = atom->expr.elems[0];
    OutcomeSet heads;
    outcome_set_init(&heads);
    metta_eval_bind(s, a, op, fuel, &heads);

    bool saw_capture = false;
    bool saw_other = false;
    for (uint32_t hi = 0; hi < heads.len; hi++) {
        Atom *head_atom = heads.items[hi].atom;
        if (atom_is_empty_or_error(head_atom))
            continue;
        if (is_capture_closure(head_atom))
            saw_capture = true;
        else
            saw_other = true;
    }

    if (!saw_capture || saw_other) {
        outcome_set_free(&heads);
        return false;
    }

    Atom *exp_type = etype ? etype : atom_undefined_type(a);
    for (uint32_t hi = 0; hi < heads.len; hi++) {
        Atom *head_atom = heads.items[hi].atom;
        Bindings head_env;
        if (!bindings_clone(&head_env, &heads.items[hi].env))
            continue;

        if (atom_is_empty_or_error(head_atom)) {
            outcome_set_add_move(os, head_atom, &head_env);
            bindings_free(&head_env);
            continue;
        }

        Atom *head_type = get_grounded_type(a, head_atom);
        Atom *errors[64];
        uint32_t n_errors = 0;
        Bindings succs[64];
        for (uint32_t si = 0; si < 64; si++) bindings_init(&succs[si]);
        uint32_t n_succs = 0;
        if (!check_function_applicable(atom, head_type, exp_type, s, a, fuel,
                                       errors, &n_errors, succs, &n_succs)) {
            for (uint32_t ei = 0; ei < n_errors; ei++)
                outcome_set_add(os, errors[ei], &head_env);
            for (uint32_t si = 0; si < n_succs; si++)
                bindings_free(&succs[si]);
            bindings_free(&head_env);
            continue;
        }
        for (uint32_t si = 0; si < n_succs; si++)
            bindings_free(&succs[si]);

        Atom *arg_types[32];
        uint32_t expr_narg = atom->expr.len - 1;
        get_function_arg_types(head_type, arg_types, 32);

        OutcomeSet call_terms;
        outcome_set_init(&call_terms);
        Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
        interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                expr_narg, 0, prefix, &head_env, fuel, &call_terms);

        for (uint32_t ci = 0; ci < call_terms.len; ci++) {
            Atom *call_atom = call_terms.items[ci].atom;
            Bindings combo_ctx;
            if (!bindings_clone(&combo_ctx, &call_terms.items[ci].env))
                continue;
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 1 &&
                is_capture_closure(call_atom->expr.elems[0])) {
                dispatch_capture_outcomes(s, a, call_atom->expr.elems[0],
                                          call_atom->expr.elems + 1,
                                          call_atom->expr.len - 1,
                                          fuel, &combo_ctx, preserve_bindings, os);
            } else {
                outcome_set_add(os, call_atom, &combo_ctx);
            }
            bindings_free(&combo_ctx);
        }
        outcome_set_free(&call_terms);
        bindings_free(&head_env);
    }

    outcome_set_free(&heads);
    return true;
}

/* ── interpret_tuple: Cartesian product of sub-expression evaluations ──── */
/* Per HE spec metta.md lines 358-381:
   Evaluate each element of the expression. If an element returns multiple
   results, produce all combinations (Cartesian product).
   The bindings parameter threads variable bindings from earlier elements
   to later ones (spec line 376: interpret_tuple($tail, $space, $hb)). */

static void interpret_tuple(Space *s, Arena *a,
                            Atom **orig_elems, uint32_t len,
                            uint32_t idx, Atom **prefix,
                            Bindings *ctx, int fuel, ResultBindSet *rbs) {
    if (idx == len) {
        rb_set_add(rbs, atom_expr(a, prefix, len), ctx);
        return;
    }
    /* Apply accumulated bindings to this element before evaluating */
    Atom *elem = bindings_apply(ctx, a, orig_elems[idx]);
    ResultBindSet sub;
    rb_set_init(&sub);
    metta_eval_bind(s, a, elem, fuel, &sub);
    if (sub.len == 0) {
        outcome_set_free(&sub);
        return;
    }
    Bindings empty;
    bindings_init(&empty);
    for (uint32_t i = 0; i < sub.len; i++) {
        if (atom_is_empty(sub.items[i].atom) || atom_is_error(sub.items[i].atom)) {
            rb_set_add(rbs, sub.items[i].atom, &empty);
        } else {
            prefix[idx] = sub.items[i].atom;
            Bindings merged;
            if (!bindings_clone_merge(&merged, ctx, &sub.items[i].env))
                continue;
            interpret_tuple(s, a, orig_elems, len, idx + 1, prefix,
                            &merged, fuel, rbs);
            bindings_free(&merged);
        }
    }
    outcome_set_free(&sub);
}

static __attribute__((noinline)) bool
handle_match(Space *s, Arena *a, Atom *atom, int fuel, bool preserve_bindings,
             OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    uint32_t nargs = expr_nargs(atom);
    if (atom_head_symbol_id(atom) != g_builtin_syms.match || nargs != 3) return false;

    Atom *space_ref = expr_arg(atom, 0);
    Atom *pattern = resolve_registry_refs(a, expr_arg(atom, 1));
    Atom *template = resolve_registry_refs(a, expr_arg(atom, 2));
    Space *ms = resolve_single_space_arg(s, a, space_ref, fuel);
    if (g_registry && !ms) {
        outcome_set_add(os, space_arg_error(a, atom,
            "match expects a space as the first argument"), &_empty);
        return true;
    }
    if (!ms) ms = s;

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        uint32_t n_conjuncts = pattern->expr.len - 1;
        BindingSet matches;
        space_query_conjunction(ms, a, pattern->expr.elems + 1, n_conjuncts,
                                NULL, &matches);
        for (uint32_t bi = 0; bi < matches.len; bi++) {
            Atom *result = bindings_apply(&matches.items[bi], a, template);
            eval_for_caller(s, a, NULL, result, fuel, &matches.items[bi],
                            preserve_bindings, os);
        }
        binding_set_free(&matches);
        return true;
    }

    {
        #define MAX_CHAIN 16
        #define MAX_VARS_PER_PAT 32
        typedef struct { Space *space; Atom *pattern; } MatchStep;

        MatchStep steps[MAX_CHAIN];
        uint32_t nsteps = 0;

        steps[nsteps].space = ms;
        steps[nsteps].pattern = pattern;
        nsteps++;

        Atom *body = template;
        while (nsteps < MAX_CHAIN &&
               body->kind == ATOM_EXPR && body->expr.len == 4 &&
               atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.match)) {
            Atom *inner_ref = resolve_registry_refs(a, body->expr.elems[1]);
            Atom *inner_pat = resolve_registry_refs(a, body->expr.elems[2]);
            Space *inner_sp = g_registry ? resolve_space(g_registry, inner_ref) : NULL;
            if (!inner_sp) inner_sp = s;
            steps[nsteps].space = inner_sp;
            steps[nsteps].pattern = inner_pat;
            nsteps++;
            body = body->expr.elems[3];
        }

        if (nsteps >= 3) {
            VarId pat_vars[MAX_CHAIN][MAX_VARS_PER_PAT];
            uint32_t pat_nvars[MAX_CHAIN];
            for (uint32_t i = 0; i < nsteps; i++) {
                pat_nvars[i] = 0;
                Atom *stack[64];
                uint32_t sp = 0;
                stack[sp++] = steps[i].pattern;
                while (sp > 0) {
                    Atom *cur = stack[--sp];
                    if (cur->kind == ATOM_VAR) {
                        bool dup = false;
                        for (uint32_t v = 0; v < pat_nvars[i]; v++) {
                            if (pat_vars[i][v] == cur->var_id) {
                                dup = true;
                                break;
                            }
                        }
                        if (!dup && pat_nvars[i] < MAX_VARS_PER_PAT)
                            pat_vars[i][pat_nvars[i]++] = cur->var_id;
                    }
                    if (cur->kind == ATOM_EXPR) {
                        for (uint32_t j = 0; j < cur->expr.len && sp < 64; j++)
                            stack[sp++] = cur->expr.elems[j];
                    }
                }
            }

            VarId bound[MAX_CHAIN * MAX_VARS_PER_PAT];
            uint32_t nbound = 0;
            for (uint32_t v = 0; v < pat_nvars[0]; v++)
                bound[nbound++] = pat_vars[0][v];

            bool scheduled[MAX_CHAIN];
            memset(scheduled, 0, sizeof(scheduled));
            scheduled[0] = true;

            MatchStep reordered[MAX_CHAIN];
            reordered[0] = steps[0];
            for (uint32_t round = 1; round < nsteps; round++) {
                int best = -1;
                uint32_t best_score = 0;
                for (uint32_t j = 1; j < nsteps; j++) {
                    if (scheduled[j]) continue;
                    uint32_t score = 0;
                    for (uint32_t v = 0; v < pat_nvars[j]; v++) {
                        for (uint32_t b = 0; b < nbound; b++) {
                            if (pat_vars[j][v] == bound[b]) {
                                score++;
                                break;
                            }
                        }
                    }
                    if (best < 0 || score > best_score) {
                        best = (int)j;
                        best_score = score;
                    }
                }
                scheduled[best] = true;
                reordered[round] = steps[best];
                for (uint32_t v = 0; v < pat_nvars[best]; v++) {
                    bool dup = false;
                    for (uint32_t b = 0; b < nbound; b++) {
                        if (bound[b] == pat_vars[best][v]) {
                            dup = true;
                            break;
                        }
                    }
                    if (!dup) bound[nbound++] = pat_vars[best][v];
                }
            }
            memcpy(steps, reordered, sizeof(MatchStep) * nsteps);
        }

        Bindings *cur_binds = cetta_malloc(sizeof(Bindings));
        uint32_t ncur = 1;
        bindings_init(&cur_binds[0]);

        uint32_t accum_steps = (nsteps > 1) ? nsteps - 1 : nsteps;
        for (uint32_t si = 0; si < accum_steps; si++) {
            MatchStep *step = &steps[si];
            Bindings *next_binds = NULL;
            uint32_t nnext = 0, cnext = 0;
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Atom *grounded = bindings_apply(&cur_binds[bi], a, step->pattern);
                SubstMatchSet smr;
                smset_init(&smr);
                space_subst_query(step->space, a, grounded, &smr);
                for (uint32_t ci = 0; ci < smr.len; ci++) {
                    Bindings mb;
                    if (space_subst_match_with_seed(step->space, grounded, &smr.items[ci],
                                                    &cur_binds[bi], a, &mb)) {
                        if (nnext >= cnext) {
                            cnext = cnext ? cnext * 2 : 8;
                            next_binds = cetta_realloc(next_binds, sizeof(Bindings) * cnext);
                        }
                        bindings_move(&next_binds[nnext++], &mb);
                    }
                }
                smset_free(&smr);
            }
            bindings_array_free(cur_binds, ncur);
            cur_binds = next_binds;
            ncur = nnext;
            if (ncur == 0) break;
        }

        if (nsteps > 1 && ncur > 0) {
            MatchStep *last = &steps[nsteps - 1];
            static uint64_t g_chain_progress = 0;
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Atom *grounded = bindings_apply(&cur_binds[bi], a, last->pattern);
                SubstMatchSet smr;
                smset_init(&smr);
                space_subst_query(last->space, a, grounded, &smr);
                for (uint32_t ci = 0; ci < smr.len; ci++) {
                    Bindings mb;
                    if (space_subst_match_with_seed(last->space, grounded, &smr.items[ci],
                                                    &cur_binds[bi], a, &mb)) {
                        Atom *result = bindings_apply(&mb, a, body);
                        eval_for_caller(s, a, NULL, result, fuel, &mb,
                                        preserve_bindings, os);
                        bindings_free(&mb);
                        g_chain_progress++;
                        if ((g_chain_progress % 100000) == 0) {
                            fprintf(stderr, "[chain] %luk results  (step %u/%u, bi=%u/%u)\n",
                                (unsigned long)(g_chain_progress / 1000),
                                nsteps, nsteps, bi, ncur);
                        }
                    }
                }
                smset_free(&smr);
            }
        } else {
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Atom *result = bindings_apply(&cur_binds[bi], a, body);
                eval_for_caller(s, a, NULL, result, fuel, &cur_binds[bi],
                                preserve_bindings, os);
            }
        }
        bindings_array_free(cur_binds, ncur);
    }

    return true;
}

static __attribute__((noinline)) bool
handle_dispatch(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                bool preserve_bindings, Atom **tail_next, Atom **tail_type,
                Bindings *tail_env,
                OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    *tail_next = NULL;
    *tail_type = NULL;
    bindings_init(tail_env);
    if (atom->kind != ATOM_EXPR || atom->expr.len < 1) return false;

    Atom *op = atom->expr.elems[0];
    Atom **op_types;
    uint32_t n_op_types = get_atom_types_profiled(s, a, op, &op_types);
    bool only_function_types = (n_op_types > 0);
    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        if (!is_function_type(op_types[ti])) {
            only_function_types = false;
            break;
        }
    }

    bool has_func_type = false;
    bool has_non_func_type = false;
    OutcomeSet func_results;
    outcome_set_init(&func_results);
    Atom *func_errors[64];
    uint32_t n_func_errors = 0;

    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        if (is_function_type(op_types[ti])) {
            has_func_type = true;
            Atom *errors[64];
            uint32_t n_errors = 0;
            Bindings succs[64];
            for (uint32_t si = 0; si < 64; si++) bindings_init(&succs[si]);
            uint32_t n_succs = 0;
            Atom *exp_type = etype ? etype : atom_undefined_type(a);
            Atom *fresh_ft = rename_vars(a, op_types[ti], fresh_var_suffix());
        if (check_function_applicable(atom, fresh_ft, exp_type,
                                      s, a, fuel,
                                      errors, &n_errors,
                                      succs, &n_succs)) {
                Atom *arg_types[32];
                get_function_arg_types(fresh_ft, arg_types, 32);
                Atom *ret_type = get_function_ret_type(fresh_ft);
                if (atom_is_symbol_id(ret_type, g_builtin_syms.expression))
                    ret_type = atom_undefined_type(a);

                OutcomeSet heads;
                outcome_set_init(&heads);
                metta_eval_bind_typed(s, a, fresh_ft, op, fuel, &heads);

                for (uint32_t hi = 0; hi < heads.len; hi++) {
                    Atom *head_atom = heads.items[hi].atom;
                    Bindings head_env;
                    if (!bindings_clone(&head_env, &heads.items[hi].env))
                        continue;
                    if (atom_is_empty_or_error(head_atom) && !atom_eq(head_atom, op)) {
                        outcome_set_add_move(&func_results, head_atom, &head_env);
                        bindings_free(&head_env);
                        continue;
                    }

                    uint32_t expr_narg = atom->expr.len - 1;
                    OutcomeSet call_terms;
                    outcome_set_init(&call_terms);
                    Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
                    interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                            expr_narg, 0, prefix, &head_env, fuel, &call_terms);

                    for (uint32_t ci = 0; ci < call_terms.len; ci++) {
                        Atom *call_atom = call_terms.items[ci].atom;
                        Bindings combo_ctx;
                        if (!bindings_clone(&combo_ctx, &call_terms.items[ci].env))
                            continue;
                        Atom *inst_ret_type =
                            eval_dependent_telescope_enabled()
                                ? bindings_apply(&combo_ctx, a, ret_type)
                                : bindings_apply(&head_env, a, ret_type);

                        bool dispatched = false;
                        if (!dispatched &&
                            call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                            Atom *h = call_atom->expr.elems[0];
                            if (dispatch_foreign_outcomes(
                                    s, a, h,
                                    call_atom->expr.elems + 1, 0,
                                    inst_ret_type, fuel, &combo_ctx,
                                    only_function_types &&
                                        n_op_types == 1 && heads.len == 1 &&
                                        call_terms.len == 1,
                                    preserve_bindings,
                                    tail_next, tail_type, &func_results)) {
                                if (only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && *tail_next) {
                                    bindings_copy(tail_env, &combo_ctx);
                                    bindings_free(&combo_ctx);
                                    outcome_set_free(&call_terms);
                                    bindings_free(&head_env);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    return true;
                                }
                                dispatched = true;
                            }
                        }
                        if (!dispatched &&
                            call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                            Atom *h = call_atom->expr.elems[0];
                            if (is_capture_closure(h)) {
                                dispatch_capture_outcomes(s, a, h,
                                    call_atom->expr.elems + 1, call_atom->expr.len - 1,
                                    fuel, &combo_ctx, preserve_bindings,
                                    &func_results);
                                dispatched = true;
                            } else if (dispatch_foreign_outcomes(
                                           s, a, h,
                                           call_atom->expr.elems + 1, call_atom->expr.len - 1,
                                           inst_ret_type, fuel, &combo_ctx,
                                           only_function_types &&
                                               n_op_types == 1 && heads.len == 1 &&
                                               call_terms.len == 1,
                                           preserve_bindings,
                                           tail_next, tail_type, &func_results)) {
                                if (only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && *tail_next) {
                                    bindings_copy(tail_env, &combo_ctx);
                                    bindings_free(&combo_ctx);
                                    outcome_set_free(&call_terms);
                                    bindings_free(&head_env);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    return true;
                                }
                                dispatched = true;
                            } else if (h->kind == ATOM_SYMBOL &&
                                       is_grounded_op(h->sym_id)) {
                                Atom *gr = dispatch_native_op(s, a, h,
                                    call_atom->expr.elems + 1, call_atom->expr.len - 1);
                                if (gr) {
                                    if (only_function_types &&
                                        n_op_types == 1 && heads.len == 1 &&
                                        call_terms.len == 1) {
                                        *tail_next = (combo_ctx.len == 0)
                                            ? gr
                                            : bindings_apply(&combo_ctx, a, gr);
                                        *tail_type = inst_ret_type;
                                        bindings_copy(tail_env, &combo_ctx);
                                        bindings_free(&combo_ctx);
                                        outcome_set_free(&call_terms);
                                        bindings_free(&head_env);
                                        outcome_set_free(&heads);
                                        outcome_set_free(&func_results);
                                        for (uint32_t sj = 0; sj < n_succs; sj++)
                                            bindings_free(&succs[sj]);
                                        free(op_types);
                                        return true;
                                    }
                                    eval_for_caller(s, a, inst_ret_type, gr, fuel,
                                                    &combo_ctx, preserve_bindings,
                                                    &func_results);
                                    dispatched = true;
                                }
                            }
                        }
                        if (!dispatched) {
                            QueryResults qr;
                            query_results_init(&qr);
                            query_equations(s, call_atom, a, &qr);
                            if (qr.len > 0) {
                                if (only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && qr.len == 1) {
                                    Bindings merged;
                                    if (!bindings_clone_merge(&merged, &combo_ctx,
                                                              &qr.items[0].bindings)) {
                                        query_results_free(&qr);
                                        bindings_free(&combo_ctx);
                                        continue;
                                    }
                                    Atom *tail_hint = result_eval_type_hint(inst_ret_type,
                                                                            qr.items[0].result);
                                    *tail_next = (merged.len == 0)
                                        ? qr.items[0].result
                                        : bindings_apply(&merged, a, qr.items[0].result);
                                    *tail_type = tail_hint;
                                    bindings_copy(tail_env, &merged);
                                    bindings_free(&merged);
                                    query_results_free(&qr);
                                    bindings_free(&combo_ctx);
                                    outcome_set_free(&call_terms);
                                    bindings_free(&head_env);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    return true;
                                }
                                for (uint32_t qi = 0; qi < qr.len; qi++) {
                                    Bindings merged;
                                    if (!bindings_clone_merge(&merged, &combo_ctx,
                                                              &qr.items[qi].bindings))
                                        continue;
                                    eval_for_caller(s, a,
                                                    result_eval_type_hint(inst_ret_type, qr.items[qi].result),
                                                    qr.items[qi].result, fuel, &merged,
                                                    preserve_bindings, &func_results);
                                    bindings_free(&merged);
                                }
                                dispatched = true;
                            }
                            query_results_free(&qr);
                        }
                        if (!dispatched)
                            outcome_set_add_move(&func_results, call_atom, &combo_ctx);
                        bindings_free(&combo_ctx);
                    }
                    outcome_set_free(&call_terms);
                    bindings_free(&head_env);
                }
                outcome_set_free(&heads);
            } else {
                for (uint32_t ei = 0; ei < n_errors && n_func_errors < 64; ei++)
                    func_errors[n_func_errors++] = errors[ei];
            }
            for (uint32_t si = 0; si < n_succs; si++)
                bindings_free(&succs[si]);
        } else {
            has_non_func_type = true;
        }
    }
    free(op_types);

    if (func_results.len > 0) {
        for (uint32_t i = 0; i < func_results.len; i++)
            outcome_set_add(os, func_results.items[i].atom, &func_results.items[i].env);
        outcome_set_free(&func_results);
        if (!has_non_func_type) return true;
    } else {
        outcome_set_free(&func_results);
    }

    if (has_func_type && n_func_errors > 0 &&
        (!has_non_func_type || eval_type_check_auto_enabled())) {
        for (uint32_t i = 0; i < n_func_errors; i++)
            outcome_set_add(os, func_errors[i], &_empty);
        if (!has_non_func_type) return true;
    }

    if (has_func_type && !has_non_func_type) {
        return true;
    }

    if (!has_func_type &&
        try_dynamic_capture_dispatch(s, a, atom, etype, fuel, preserve_bindings, os)) {
        return true;
    }

    {
        ResultBindSet tuples;
        rb_set_init(&tuples);
        Atom **prefix = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        Bindings empty_ctx;
        bindings_init(&empty_ctx);
        interpret_tuple(s, a, atom->expr.elems, atom->expr.len,
                        0, prefix, &empty_ctx, fuel, &tuples);

        if (tuples.len == 1) {
            Bindings tuple_bindings;
            if (!bindings_clone(&tuple_bindings, &tuples.items[0].env)) {
                outcome_set_free(&tuples);
                return true;
            }
            Atom *call_atom = bindings_apply(&tuple_bindings, a, tuples.items[0].atom);
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add(os, call_atom, &_empty);
                bindings_free(&tuple_bindings);
                outcome_set_free(&tuples);
                return true;
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                Atom *h = call_atom->expr.elems[0];
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, 0,
                        NULL, fuel, &tuple_bindings, true,
                        preserve_bindings,
                        tail_next, tail_type, os)) {
                    if (*tail_next)
                        bindings_copy(tail_env, &tuple_bindings);
                    bindings_free(&tuple_bindings);
                    outcome_set_free(&tuples);
                    return true;
                }
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (is_capture_closure(h)) {
                    dispatch_capture_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        fuel, &tuple_bindings, preserve_bindings, os);
                    bindings_free(&tuple_bindings);
                    outcome_set_free(&tuples);
                    return true;
                }
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        NULL, fuel, &tuple_bindings, true,
                        preserve_bindings,
                        tail_next, tail_type, os)) {
                    if (*tail_next)
                        bindings_copy(tail_env, &tuple_bindings);
                    bindings_free(&tuple_bindings);
                    outcome_set_free(&tuples);
                    return true;
                }
                if (h->kind == ATOM_SYMBOL &&
                    is_grounded_op(h->sym_id)) {
                    Atom *result = dispatch_native_op(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        *tail_next = (tuple_bindings.len == 0)
                            ? result
                            : bindings_apply(&tuple_bindings, a, result);
                        *tail_type = etype;
                        bindings_copy(tail_env, &tuple_bindings);
                        bindings_free(&tuple_bindings);
                        outcome_set_free(&tuples);
                        return true;
                    }
                }
            }

            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len == 1) {
                Bindings merged;
                if (!bindings_clone_merge(&merged, &tuple_bindings,
                                          &qr.items[0].bindings)) {
                    bindings_free(&tuple_bindings);
                    query_results_free(&qr);
                    outcome_set_free(&tuples);
                    return true;
                }
                Atom *tail_hint = result_eval_type_hint(etype, qr.items[0].result);
                *tail_next = (merged.len == 0)
                    ? qr.items[0].result
                    : bindings_apply(&merged, a, qr.items[0].result);
                *tail_type = tail_hint;
                bindings_copy(tail_env, &merged);
                bindings_free(&merged);
                bindings_free(&tuple_bindings);
                query_results_free(&qr);
                outcome_set_free(&tuples);
                return true;
            }
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++) {
                    Bindings merged;
                    if (!bindings_clone_merge(&merged, &tuple_bindings,
                                              &qr.items[i].bindings))
                        continue;
                    eval_for_caller(s, a,
                                    result_eval_type_hint(NULL, qr.items[i].result),
                                    qr.items[i].result, fuel, &merged,
                                    preserve_bindings, os);
                    bindings_free(&merged);
                }
                bindings_free(&tuple_bindings);
                query_results_free(&qr);
                outcome_set_free(&tuples);
                return true;
            }
            query_results_free(&qr);
            outcome_set_add_move(os, call_atom, &tuple_bindings);
            bindings_free(&tuple_bindings);
            outcome_set_free(&tuples);
            return true;
        }

        for (uint32_t ti = 0; ti < tuples.len; ti++) {
            Atom *call_atom = tuples.items[ti].atom;
            Bindings tuple_bindings;
            if (!bindings_clone(&tuple_bindings, &tuples.items[ti].env))
                continue;

            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add(os, call_atom, &_empty);
                bindings_free(&tuple_bindings);
                continue;
            }

            call_atom = bindings_apply(&tuple_bindings, a, call_atom);

            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                Atom *h = call_atom->expr.elems[0];
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, 0,
                        NULL, fuel, &tuple_bindings, false, preserve_bindings,
                        tail_next, tail_type, os)) {
                    bindings_free(&tuple_bindings);
                    continue;
                }
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (is_capture_closure(h)) {
                    dispatch_capture_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        fuel, &tuple_bindings, preserve_bindings, os);
                    bindings_free(&tuple_bindings);
                    continue;
                }
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        NULL, fuel, &tuple_bindings, false, preserve_bindings,
                        tail_next, tail_type, os)) {
                    bindings_free(&tuple_bindings);
                    continue;
                }
                if (h->kind == ATOM_SYMBOL &&
                    is_grounded_op(h->sym_id)) {
                    Atom *result = dispatch_native_op(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        eval_for_caller(s, a, NULL, result, fuel, &tuple_bindings,
                                        preserve_bindings, os);
                        bindings_free(&tuple_bindings);
                        continue;
                    }
                }
            }

            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, call_atom, a, &qr);
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++) {
                    Bindings merged;
                    if (!bindings_clone_merge(&merged, &tuple_bindings,
                                              &qr.items[i].bindings))
                        continue;
                    eval_for_caller(s, a,
                                    result_eval_type_hint(NULL, qr.items[i].result),
                                    qr.items[i].result, fuel, &merged,
                                    preserve_bindings, os);
                    bindings_free(&merged);
                }
                query_results_free(&qr);
                bindings_free(&tuple_bindings);
                continue;
            }
            query_results_free(&qr);

            outcome_set_add_move(os, call_atom, &tuple_bindings);
            bindings_free(&tuple_bindings);
        }
        outcome_set_free(&tuples);
    }

    return true;
}

/* ── metta_call: dispatch expressions ───────────────────────────────────── */

static void metta_call(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                       bool preserve_bindings, OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    __attribute__((cleanup(bindings_free))) Bindings current_env;
    bindings_init(&current_env);
    if (!etype) etype = atom_undefined_type(a);
#define TAIL_REENTER_ENV(next_atom, extra_env) do { \
    if (preserve_bindings && (extra_env) != NULL && \
        !bindings_merge_into(&current_env, (extra_env))) return; \
    atom = resolve_registry_refs(a, (next_atom)); \
    if (fuel == 0) return; \
    if (fuel > 0) fuel--; \
    goto tail_call; \
} while (0)
#define TAIL_REENTER(next_atom) TAIL_REENTER_ENV((next_atom), NULL)
#define outcome_set_add(_os, _atom, _env) \
    outcome_set_add_prefixed(a, (_os), (_atom), (_env), &current_env, preserve_bindings)
tail_call: ;
    atom = materialize_runtime_token(s, a, atom);
    if (atom_is_error(atom) || atom_is_empty(atom)) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol_id(etype, g_builtin_syms.atom) || atom_eq(etype, meta) ||
        atom_is_symbol_id(meta, g_builtin_syms.variable)) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        ResultSet rs;
        result_set_init(&rs);
        type_cast_fn(s, a, atom, etype, fuel, &rs);
        for (uint32_t i = 0; i < rs.len; i++)
            outcome_set_add(os, rs.items[i], &_empty);
        result_set_free(&rs);
        return;
    }

    if (atom->kind == ATOM_VAR) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    uint32_t nargs = expr_nargs(atom);
    const SymbolId head_id = atom_head_symbol_id(atom);

    /* ── Special forms (arguments NOT pre-evaluated) ───────────────────── */

    /* ── superpose ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.superpose) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *list = expr_arg(atom, 0);
        if (list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < list->expr.len; i++) {
                eval_for_current_caller(s, a, etype, list->expr.elems[i], fuel, &_empty,
                                        &current_env, preserve_bindings, os);
            }
        }
        return;
    }

    /* ── collapse ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.collapse && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        Atom *collected = atom_expr(a, inner.items, inner.len);
        free(inner.items);
        outcome_set_add(os, collected, &_empty);
        return;
    }

    /* ── cons-atom ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.cons_atom && nargs == 2) {
        Atom *hd = expr_arg(atom, 0);
        Atom *tl = expr_arg(atom, 1);
        if (tl->kind == ATOM_EXPR) {
            Atom **elems = arena_alloc(a, sizeof(Atom *) * (tl->expr.len + 1));
            elems[0] = hd;
            for (uint32_t i = 0; i < tl->expr.len; i++)
                elems[i + 1] = tl->expr.elems[i];
            outcome_set_add(os, atom_expr(a, elems, tl->expr.len + 1), &_empty);
        } else {
            outcome_set_add(os, atom_expr2(a, hd, tl), &_empty);
        }
        return;
    }

    /* ── union-atom ────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.union_atom && nargs == 2) {
        Atom *lhs = expr_arg(atom, 0);
        Atom *rhs = expr_arg(atom, 1);
        if (lhs->kind == ATOM_EXPR && rhs->kind == ATOM_EXPR) {
            uint32_t len = lhs->expr.len + rhs->expr.len;
            Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
            for (uint32_t i = 0; i < lhs->expr.len; i++)
                elems[i] = lhs->expr.elems[i];
            for (uint32_t i = 0; i < rhs->expr.len; i++)
                elems[lhs->expr.len + i] = rhs->expr.elems[i];
            outcome_set_add(os, atom_expr(a, elems, len), &_empty);
        } else {
            outcome_set_add(os, atom, &_empty);
        }
        return;
    }

    /* ── decons-atom ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.decons_atom) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0) {
            Atom *hd = e->expr.elems[0];
            Atom *tl = atom_expr(a, e->expr.elems + 1, e->expr.len - 1);
            outcome_set_add(os, atom_expr2(a, hd, tl), &_empty);
        } else {
            outcome_set_add(os,
                call_signature_error(a, atom,
                    "(decons-atom (: <expr> Expression))"),
                &_empty);
        }
        return;
    }

    /* ── car-atom / cdr-atom ─────────────────────────────────────────── */
    if (head_id == g_builtin_syms.car_atom && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0)
            outcome_set_add(os, e->expr.elems[0], &_empty);
        else
            outcome_set_add(os,
                atom_error(a, atom,
                    atom_string(a, "car-atom expects a non-empty expression as an argument")),
                &_empty);
        return;
    }
    if (head_id == g_builtin_syms.cdr_atom && nargs == 1) {
        Atom *e = expr_arg(atom, 0);
        if (e->kind == ATOM_EXPR && e->expr.len > 0)
            outcome_set_add(os, atom_expr(a, e->expr.elems + 1, e->expr.len - 1), &_empty);
        else
            outcome_set_add(os,
                atom_error(a, atom,
                    atom_string(a, "cdr-atom expects a non-empty expression as an argument")),
                &_empty);
        return;
    }

    /* ── match (with nested-match fusion + join reordering) ──────────── */
    OutcomeSet match_results;
    outcome_set_init(&match_results);
    if (handle_match(s, a, atom, fuel, preserve_bindings, &match_results)) {
        outcome_set_append_prefixed(a, os, &match_results, &current_env,
                                    preserve_bindings);
        outcome_set_free(&match_results);
        return;
    }
    outcome_set_free(&match_results);

    /* ── unify ─────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.unify) {
        if (nargs != 4) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *target = expr_arg(atom, 0);
        Atom *pattern = expr_arg(atom, 1);
        Atom *then_br = expr_arg(atom, 2);
        Atom *else_br = expr_arg(atom, 3);
        Bindings b;
        bindings_init(&b);
        if (match_atoms(target, pattern, &b) && !bindings_has_loop(&b)) {
            Atom *next_atom = bindings_apply(&b, a, then_br);
            if (preserve_bindings &&
                !bindings_merge_into(&current_env, &b)) {
                bindings_free(&b);
                return;
            }
            bindings_free(&b);
            TAIL_REENTER(next_atom);
        } else {
            bindings_free(&b);
            TAIL_REENTER(else_br);
        }
        return;
    }

    /* ── case ──────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.case_text) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultSet scrut;
        result_set_init(&scrut);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &scrut);
        Atom *branches = expr_arg(atom, 1);
        /* Single-scrutinee deterministic fast path with TCO.
           This avoids linear C-stack growth for tail-recursive
           loops expressed as let -> case recursion. */
        if (scrut.len == 1 && branches->kind == ATOM_EXPR) {
            Atom *sv = scrut.items[0];
            for (uint32_t i = 0; i < branches->expr.len; i++) {
                Atom *branch = branches->expr.elems[i];
                if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                    Bindings b;
                    bindings_init(&b);
                    if (simple_match(branch->expr.elems[0], sv, &b)) {
                        Atom *next_atom = bindings_apply(&b, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_merge_into(&current_env, &b)) {
                            bindings_free(&b);
                            free(scrut.items);
                            return;
                        }
                        bindings_free(&b);
                        free(scrut.items);
                        TAIL_REENTER(next_atom);
                    }
                    bindings_free(&b);
                }
            }
            free(scrut.items);
            return;
        }
        for (uint32_t si = 0; si < scrut.len; si++) {
            Atom *sv = scrut.items[si];
            if (branches->kind == ATOM_EXPR) {
                for (uint32_t i = 0; i < branches->expr.len; i++) {
                    Atom *branch = branches->expr.elems[i];
                    if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                        Bindings b;
                        bindings_init(&b);
                        if (simple_match(branch->expr.elems[0], sv, &b)) {
                            Atom *result = bindings_apply(&b, a, branch->expr.elems[1]);
                            eval_for_current_caller(s, a, NULL, result, fuel, &b,
                                                    &current_env,
                                                    preserve_bindings, os);
                            bindings_free(&b);
                            break;
                        }
                        bindings_free(&b);
                    }
                }
            }
        }
        free(scrut.items);
        return;
    }

    /* ── switch ────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.switch_text ||
        head_id == g_builtin_syms.switch_minimal) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *scrutinee = expr_arg(atom, 0);
        Atom *branches = expr_arg(atom, 1);
        if (branches->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < branches->expr.len; i++) {
                Atom *branch = branches->expr.elems[i];
                if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                    Bindings b;
                    bindings_init(&b);
                    if (simple_match(branch->expr.elems[0], scrutinee, &b)) {
                        Atom *next_atom = bindings_apply(&b, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_merge_into(&current_env, &b)) {
                            bindings_free(&b);
                            return;
                        }
                        bindings_free(&b);
                        TAIL_REENTER(next_atom);
                        return;
                    }
                    bindings_free(&b);
                }
            }
        }
        return;
    }

    /* ── let* (nested let sugar with tail reentry) ────────────────────── */
    if (head_id == g_builtin_syms.let_star && nargs == 2) {
        Atom *blist = expr_arg(atom, 0);
        Atom *body = expr_arg(atom, 1);
        if (blist->kind != ATOM_EXPR || blist->expr.len == 0) {
            TAIL_REENTER(body);
        } else {
            Atom *first = blist->expr.elems[0];
            Atom *rest = atom_expr(a, blist->expr.elems + 1, blist->expr.len - 1);
            if (first->kind == ATOM_EXPR && first->expr.len == 2) {
                Atom *inner = atom_expr3(a, atom_symbol_id(a, g_builtin_syms.let_star), rest, body);
                Atom *elems[4] = { atom_symbol_id(a, g_builtin_syms.let),
                    first->expr.elems[0], first->expr.elems[1], inner };
                Atom *desugared = atom_expr(a, elems, 4);
                TAIL_REENTER(desugared);
            }
        }
        return;
    }

    /* ── let ───────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.let && nargs == 3) {
        Atom *pat = expr_arg(atom, 0);
        Atom *val_expr = expr_arg(atom, 1);
        Atom *body_let = expr_arg(atom, 2);
        ResultSet vals;
        result_set_init(&vals);
        metta_eval(s, a, NULL, val_expr, fuel, &vals);
        bool all_errors = vals.len > 0;
        for (uint32_t i = 0; i < vals.len; i++) {
            if (!atom_is_error(vals.items[i])) {
                all_errors = false;
                break;
            }
        }
        if (all_errors) {
            for (uint32_t i = 0; i < vals.len; i++)
                outcome_set_add(os, vals.items[i], &_empty);
            free(vals.items);
            return;
        }
        if (vals.len == 1) {
            /* Single-result fast path with TCO */
            Bindings b; bindings_init(&b);
            bool ok = false;
            if (pat->kind == ATOM_VAR) {
                bindings_add_var(&b, pat, vals.items[0]);
                ok = true;
            } else {
                ok = simple_match(pat, vals.items[0], &b);
            }
            free(vals.items);
            if (ok) {
                Atom *next_atom = bindings_apply(&b, a, body_let);
                if (preserve_bindings &&
                    !bindings_merge_into(&current_env, &b)) {
                    bindings_free(&b);
                    return;
                }
                bindings_free(&b);
                TAIL_REENTER(next_atom);
            }
            bindings_free(&b);
            return;
        }
        /* Multi-result: no TCO */
        for (uint32_t i = 0; i < vals.len; i++) {
            Bindings b; bindings_init(&b);
            if (pat->kind == ATOM_VAR) {
                bindings_add_var(&b, pat, vals.items[i]);
                eval_for_current_caller(s, a, NULL,
                                        bindings_apply(&b, a, body_let), fuel, &b,
                                        &current_env, preserve_bindings, os);
            } else if (simple_match(pat, vals.items[i], &b)) {
                eval_for_current_caller(s, a, NULL,
                                        bindings_apply(&b, a, body_let), fuel, &b,
                                        &current_env, preserve_bindings, os);
            }
            bindings_free(&b);
        }
        free(vals.items);
        return;
    }

    /* ── chain ─────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.chain) {
        if (nargs != 3) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *to_eval = expr_arg(atom, 0);
        Atom *var = expr_arg(atom, 1);
        Atom *tmpl_chain = expr_arg(atom, 2);
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, to_eval, fuel, &inner);
        if (inner.len == 0) {
            outcome_set_add(os, atom_empty(a), &_empty);
            free(inner.items);
            return;
        }
        if (inner.len == 1 && !atom_is_empty(inner.items[0])) {
            /* Single-result fast path with TCO */
            Atom *next_atom;
            Bindings b;
            bool has_binding = false;
            if (var->kind == ATOM_VAR) {
                bindings_init(&b);
                has_binding = true;
                bindings_add_var(&b, var, inner.items[0]);
                next_atom = bindings_apply(&b, a, tmpl_chain);
            } else {
                next_atom = tmpl_chain;
            }
            free(inner.items);
            if (has_binding && preserve_bindings &&
                !bindings_merge_into(&current_env, &b)) {
                bindings_free(&b);
                return;
            }
            if (has_binding)
                bindings_free(&b);
            TAIL_REENTER(next_atom);
        }
        /* Multi-result: no TCO */
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i];
            if (atom_is_empty(r)) continue;
            if (var->kind == ATOM_VAR) {
                Bindings b; bindings_init(&b);
                bindings_add_var(&b, var, r);
                Atom *subst = bindings_apply(&b, a, tmpl_chain);
                eval_for_current_caller(s, a, NULL, subst, fuel, &b,
                                        &current_env, preserve_bindings, os);
                bindings_free(&b);
            } else {
                outcome_set_add(os, tmpl_chain, &_empty);
            }
        }
        if (inner.len == 0)
            outcome_set_add(os, atom_empty(a), &_empty);
        free(inner.items);
        return;
    }

    /* ── search-policy ─────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.search_policy) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "search-policy")) {
            outcome_set_add(os, profile_surface_error(a, atom, "search-policy"), &_empty);
            return;
        }
        CettaSearchPolicySpec spec = {0};
        Atom *reason = NULL;
        CettaSearchPolicyParseStatus parsed =
            parse_search_policy_atom(a, atom, &spec, &reason);
        if (parsed == CETTA_SEARCH_POLICY_PARSE_OK) {
            outcome_set_add(os, atom, &_empty);
        } else {
            outcome_set_add(os,
                atom_error(a, atom, reason ? reason : atom_symbol(a, "MalformedSearchPolicy")),
                &_empty);
        }
        return;
    }

    /* ── collect / select / once ───────────────────────────────────────── */
    if (head_id == g_builtin_syms.collect ||
        head_id == g_builtin_syms.select ||
        head_id == g_builtin_syms.once) {
        bool is_collect = head_id == g_builtin_syms.collect;
        bool is_once = head_id == g_builtin_syms.once;
        const char *surface = is_collect ? "collect" : (is_once ? "once" : "select");
        CettaSearchPolicySpec policy = {0};
        policy.order = CETTA_SEARCH_POLICY_ORDER_NATIVE;
        if (active_profile() && !cetta_profile_allows_surface(active_profile(), surface)) {
            outcome_set_add(os, profile_surface_error(a, atom, surface), &_empty);
            return;
        }

        int64_t limit = 1;
        Atom *stream_expr = NULL;

        if (is_collect) {
            if (nargs == 1) {
                stream_expr = expr_arg(atom, 0);
            } else if (nargs == 2) {
                Atom *reason = NULL;
                CettaSearchPolicyParseStatus parsed =
                    parse_search_policy_atom(a, expr_arg(atom, 0), &policy, &reason);
                if (parsed == CETTA_SEARCH_POLICY_PARSE_NOT_POLICY) {
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "SearchPolicy"),
                                           expr_arg(atom, 0)),
                        &_empty);
                    return;
                }
                if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                    outcome_set_add(os,
                        atom_error(a, atom,
                                   reason ? reason : atom_symbol(a, "MalformedSearchPolicy")),
                        &_empty);
                    return;
                }
                stream_expr = expr_arg(atom, 1);
            } else {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                    &_empty);
                return;
            }
            if (policy.present &&
                policy.lane != CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF) {
                outcome_set_add(os,
                    atom_error(a, atom, search_policy_reason_unavailable(a, policy.lane)),
                    &_empty);
                return;
            }
            stream_emit(s, a, stream_expr, fuel, false, 0, preserve_bindings,
                        policy.order, os);
            return;
        }

        if (is_once) {
            if (nargs == 1) {
                stream_expr = expr_arg(atom, 0);
            } else if (nargs == 2) {
                Atom *reason = NULL;
                CettaSearchPolicyParseStatus parsed =
                    parse_search_policy_atom(a, expr_arg(atom, 0), &policy, &reason);
                if (parsed == CETTA_SEARCH_POLICY_PARSE_NOT_POLICY) {
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "SearchPolicy"),
                                           expr_arg(atom, 0)),
                        &_empty);
                    return;
                }
                if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                    outcome_set_add(os,
                        atom_error(a, atom,
                                   reason ? reason : atom_symbol(a, "MalformedSearchPolicy")),
                        &_empty);
                    return;
                }
                stream_expr = expr_arg(atom, 1);
            } else {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                    &_empty);
                return;
            }
        } else if (nargs == 1) {
            stream_expr = expr_arg(atom, 0);
        } else if (nargs == 2) {
            Atom *reason = NULL;
            CettaSearchPolicyParseStatus parsed =
                parse_search_policy_atom(a, expr_arg(atom, 0), &policy, &reason);
            if (parsed == CETTA_SEARCH_POLICY_PARSE_OK) {
                stream_expr = expr_arg(atom, 1);
            } else if (parsed == CETTA_SEARCH_POLICY_PARSE_ERROR) {
                outcome_set_add(os,
                    atom_error(a, atom,
                               reason ? reason : atom_symbol(a, "MalformedSearchPolicy")),
                    &_empty);
                return;
            } else {
                Atom *limit_atom = expr_arg(atom, 0);
                if (limit_atom->kind != ATOM_GROUNDED || limit_atom->ground.gkind != GV_INT) {
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "Number"), limit_atom),
                        &_empty);
                    return;
                }
                if (limit_atom->ground.ival < 0) {
                    outcome_set_add(os,
                        atom_error(a, atom, atom_symbol(a, "UnsignedIntegerIsExpected")),
                        &_empty);
                    return;
                }
                limit = limit_atom->ground.ival;
                stream_expr = expr_arg(atom, 1);
            }
        } else if (nargs == 3) {
            Atom *limit_atom = expr_arg(atom, 0);
            Atom *reason = NULL;
            CettaSearchPolicyParseStatus parsed =
                parse_search_policy_atom(a, expr_arg(atom, 1), &policy, &reason);
            if (limit_atom->kind != ATOM_GROUNDED || limit_atom->ground.gkind != GV_INT) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "Number"), limit_atom),
                    &_empty);
                return;
            }
            if (limit_atom->ground.ival < 0) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "UnsignedIntegerIsExpected")),
                    &_empty);
                return;
            }
            if (parsed == CETTA_SEARCH_POLICY_PARSE_NOT_POLICY) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, 2, atom_symbol(a, "SearchPolicy"),
                                       expr_arg(atom, 1)),
                    &_empty);
                return;
            }
            if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                outcome_set_add(os,
                    atom_error(a, atom,
                               reason ? reason : atom_symbol(a, "MalformedSearchPolicy")),
                    &_empty);
                return;
            }
            limit = limit_atom->ground.ival;
            stream_expr = expr_arg(atom, 2);
        } else {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }

        if (policy.present &&
            policy.lane != CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF) {
            outcome_set_add(os,
                atom_error(a, atom, search_policy_reason_unavailable(a, policy.lane)),
                &_empty);
            return;
        }

        stream_emit(s, a, stream_expr, fuel, true, limit, preserve_bindings,
                    policy.order, os);
        return;
    }

    /* ── function / return ─────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.function && nargs == 1) {
        Atom *body = expr_arg(atom, 0);
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,body, fuel, &inner);
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i];
            if (atom_head_symbol_id(r) == g_builtin_syms.return_text && r->expr.len == 2) {
                outcome_set_add(os, r->expr.elems[1], &_empty);
            } else {
                outcome_set_add(os, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "function"), body),
                    atom_symbol(a, "NoReturn")), &_empty);
            }
        }
        if (inner.len == 0)
            outcome_set_add(os, atom_error(a,
                atom_expr2(a, atom_symbol(a, "function"), body),
                atom_symbol(a, "NoReturn")), &_empty);
        free(inner.items);
        return;
    }

    /* ── assert ────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assert_text && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        for (uint32_t i = 0; i < inner.len; i++) {
            if (is_true_atom(inner.items[i])) {
                outcome_set_add(os, atom_unit(a), &_empty);
            } else {
                outcome_set_add(os, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "assert"), expr_arg(atom, 0)),
                    atom_expr3(a, expr_arg(atom, 0),
                        atom_symbol(a, "not"), atom_symbol(a, "True"))), &_empty);
            }
        }
        free(inner.items);
        return;
    }

    /* ── return (data, not evaluated further) ──────────────────────────── */
    if (head_id == g_builtin_syms.return_text) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    /* ── quote ─────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.quote) {
        if (nargs == 1) {
            outcome_set_add(os, atom, &_empty);
        } else {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
        }
        return;
    }

    /* ── eval (minimal instruction) ────────────────────────────────────── */
    if (head_id == g_builtin_syms.eval) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        TAIL_REENTER(expr_arg(atom, 0));
    }

    /* ── foldl-atom-in-space (clean extension surface) ────────────────── */
    if (head_id == g_builtin_syms.foldl_atom_in_space) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "foldl-atom-in-space")) {
            outcome_set_add(os,
                profile_surface_error(a, atom, "foldl-atom-in-space"), &_empty);
            return;
        }
        if (nargs != 6) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom **helper_elems = arena_alloc(a, sizeof(Atom *) * 7);
        helper_elems[0] = atom_symbol(a, "_minimal-foldl-atom");
        for (uint32_t i = 0; i < 6; i++) {
            helper_elems[i + 1] = expr_arg(atom, i);
        }
        Atom *helper_call = atom_expr(a, helper_elems, 7);
        Atom *eval_helper = atom_expr2(a, atom_symbol_id(a, g_builtin_syms.eval), helper_call);
        Atom *rewrite = atom_expr2(a, atom_symbol_id(a, g_builtin_syms.function), eval_helper);
        TAIL_REENTER(rewrite);
    }

    /* ── new-space ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.new_space) {
        if (nargs > 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        SpaceKind kind = SPACE_KIND_ATOM;
        if (nargs == 1) {
            if (active_profile() &&
                !cetta_profile_allows_surface(active_profile(), "new-space-kind")) {
                outcome_set_add(os, profile_surface_error(a, atom, "new-space-kind"), &_empty);
                return;
            }
            const char *kind_name = string_like_atom(expr_arg(atom, 0));
            if (!space_kind_from_name(kind_name, &kind)) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "UnknownSpaceKind")),
                    &_empty);
                return;
            }
        }
        Arena *pa = g_persistent_arena ? g_persistent_arena : a;
        Space *ns = arena_alloc(pa, sizeof(Space));
        space_init(ns);
        (void)space_match_backend_try_set(ns, s->match_backend.kind);
        ns->kind = kind;
        outcome_set_add(os, atom_space(pa, ns), &_empty);
        return;
    }

    /* ── context-space ─────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.context_space) {
        if (nargs == 0) {
            outcome_set_add(os, atom_space(a, s), &_empty);
        } else {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
        }
        return;
    }

    /* ── call-native ──────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.call_native) {
        /* HE documents this as an internal instruction. Direct user-level
           calls surface as an error instead of silently passing through. */
        outcome_set_add(os, call_signature_error(a, atom,
            "(call-native func args)"), &_empty);
        return;
    }

    /* ── register-module! / import! ───────────────────────────────────── */
    if (head_id == g_builtin_syms.git_module_bang && g_library_context) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        const char *url = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (!url) {
            error = atom_symbol(a, "git-module! expects a URL; use quotes if needed");
        }
        if (!error && cetta_library_register_git_module(g_library_context, url, a, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "git-module! failed")), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.register_module_bang && nargs != 1) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

    if (head_id == g_builtin_syms.register_module_bang &&
        nargs == 1 && g_library_context) {
        const char *path = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (path && cetta_library_register_module(g_library_context, path, a, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "register-module! failed")), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.import_bang && nargs != 2) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

    if (head_id == g_builtin_syms.import_bang &&
        nargs == 2 && g_registry && g_library_context) {
        const char *spec = string_like_atom(expr_arg(atom, 1));
        Atom *error = NULL;
        ImportDestination dest = {0};
        if (spec) {
            dest = resolve_import_destination(a, expr_arg(atom, 0), &error);
        }
        if (!spec && !error) {
            error = atom_symbol(a, "import! expects a module name");
        }
        if (!error && dest.space &&
            cetta_library_import_module(g_library_context, spec, dest.space, dest.is_fresh,
                                        a, g_persistent_arena ? g_persistent_arena : a,
                                        g_registry, fuel, &error)) {
            if (dest.is_fresh) {
                Arena *pa = g_persistent_arena ? g_persistent_arena : a;
                registry_bind_id(g_registry, dest.bind_key, atom_space(pa, dest.space));
            }
            outcome_set_add(os, atom_unit(a), &_empty);
        } else if (error) {
            if (dest.is_fresh && dest.space) {
                space_free(dest.space);
                free(dest.space);
            }
            outcome_set_add(os, atom_error(a, atom, error), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.include && g_library_context) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        const char *spec = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (!spec) {
            error = atom_symbol(a, "include expects a module name argument");
        }
        if (!error &&
            cetta_library_include_module(g_library_context, spec, s, a,
                                        g_persistent_arena ? g_persistent_arena : a,
                                        g_registry, fuel, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "include failed")), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.mod_space_bang && g_library_context) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        const char *spec = string_like_atom(expr_arg(atom, 0));
        Atom *error = NULL;
        if (!spec) {
            error = atom_symbol(a, "mod-space! expects a module name argument");
        }
        Atom *space_atom = NULL;
        if (!error) {
            space_atom = cetta_library_mod_space(g_library_context, spec, a,
                                                g_persistent_arena ? g_persistent_arena : a,
                                                g_registry, fuel, &error);
        }
        if (space_atom) {
            outcome_set_add(os, space_atom, &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "mod-space! failed")), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.print_mods_bang && g_library_context) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *error = NULL;
        if (cetta_library_print_loaded_modules(g_library_context, stdout, a, &error)) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "print-mods! failed")), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.module_inventory_bang && g_library_context) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "module-inventory!")) {
            outcome_set_add(os, profile_surface_error(a, atom, "module-inventory!"), &_empty);
            return;
        }
        Atom *error = NULL;
        Atom *inventory = cetta_library_module_inventory_space(
            g_library_context, a, g_persistent_arena ? g_persistent_arena : a, &error);
        if (inventory) {
            outcome_set_add(os, inventory, &_empty);
        } else {
            outcome_set_add(os, atom_error(a, atom,
                error ? error : atom_symbol(a, "module-inventory! failed")), &_empty);
        }
        return;
    }

    if (head_id == g_builtin_syms.reset_runtime_stats_bang) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "reset-runtime-stats!")) {
            outcome_set_add(os,
                profile_surface_error(a, atom, "reset-runtime-stats!"), &_empty);
            return;
        }
        cetta_runtime_stats_reset();
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.runtime_stats_bang) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "runtime-stats!")) {
            outcome_set_add(os,
                profile_surface_error(a, atom, "runtime-stats!"), &_empty);
            return;
        }
        outcome_set_add(os, runtime_stats_inventory_atom(a), &_empty);
        return;
    }

    /* ── with-space-snapshot ───────────────────────────────────────────── */
    if (head_id == g_builtin_syms.with_space_snapshot &&
        nargs == 3 && g_registry) {
        Atom *binder = expr_arg(atom, 0);
        Atom *space_ref = resolve_registry_refs(a, expr_arg(atom, 1));
        Atom *body = expr_arg(atom, 2);
        Space *target = resolve_space(g_registry, space_ref);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        Space *snapshot = space_snapshot_clone(target, a);
        Atom *snapshot_atom = atom_space(a, snapshot);
        Bindings b;
        bindings_init(&b);
        if (binder->kind == ATOM_VAR) {
            bindings_add_var(&b, binder, snapshot_atom);
            Atom *next_atom = bindings_apply(&b, a, body);
            if (preserve_bindings &&
                !bindings_merge_into(&current_env, &b)) {
                bindings_free(&b);
                return;
            }
            bindings_free(&b);
            TAIL_REENTER(next_atom);
        } else if (simple_match(binder, snapshot_atom, &b)) {
            Atom *next_atom = bindings_apply(&b, a, body);
            if (preserve_bindings &&
                !bindings_merge_into(&current_env, &b)) {
                bindings_free(&b);
                return;
            }
            bindings_free(&b);
            TAIL_REENTER(next_atom);
        }
        bindings_free(&b);
        return;
    }

    /* ── structured space introspection / ordered-space ops ───────────── */
    if (head_id == g_builtin_syms.space_kind) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-kind")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-kind"), &_empty);
            return;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-kind expects a space as its argument"), &_empty);
            return;
        }
        outcome_set_add(os, space_kind_atom(a, target), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_match_backend) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-match-backend")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-match-backend"), &_empty);
            return;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-match-backend expects a space as its argument"), &_empty);
            return;
        }
        outcome_set_add(os, space_match_backend_atom(a, target), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_capabilities) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-capabilities")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-capabilities"), &_empty);
            return;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-capabilities expects a space as its argument"), &_empty);
            return;
        }
        outcome_set_add(os, space_capabilities_atom(a, target), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_len) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-len")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-len"), &_empty);
            return;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-len expects a space as its argument"), &_empty);
            return;
        }
        outcome_set_add(os, atom_int(a, (int64_t)space_length(target)), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_push) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-push")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-push"), &_empty);
            return;
        }
        if (nargs != 2 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-push expects an ordered space as the first argument"), &_empty);
            return;
        }
        if (!space_is_ordered(target)) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnsupportedSpaceKind")),
                &_empty);
            return;
        }
        Arena *dst = g_persistent_arena ? g_persistent_arena : a;
        Atom *stored = (dst == g_persistent_arena)
            ? atom_deep_copy_shared(dst, expr_arg(atom, 1))
            : atom_deep_copy(dst, expr_arg(atom, 1));
        space_add(target, stored);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_PUSH);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_peek) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-peek")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-peek"), &_empty);
            return;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-peek expects an ordered space as its argument"), &_empty);
            return;
        }
        if (!space_is_ordered(target)) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnsupportedSpaceKind")),
                &_empty);
            return;
        }
        Atom *top = space_peek(target);
        if (!top) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, ordered_space_empty_error_symbol(target))),
                &_empty);
            return;
        }
        outcome_set_add(os, top, &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_pop) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "space-pop")) {
            outcome_set_add(os, profile_surface_error(a, atom, "space-pop"), &_empty);
            return;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-pop expects an ordered space as its argument"), &_empty);
            return;
        }
        if (!space_is_ordered(target)) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnsupportedSpaceKind")),
                &_empty);
            return;
        }
        Atom *popped = NULL;
        if (!space_pop(target, &popped)) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, ordered_space_empty_error_symbol(target))),
                &_empty);
            return;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_POP);
        outcome_set_add(os, popped, &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_get ||
        head_id == g_builtin_syms.space_truncate) {
        const bool is_get = head_id == g_builtin_syms.space_get;
        const char *surface = is_get ? "space-get" : "space-truncate";
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), surface)) {
            outcome_set_add(os, profile_surface_error(a, atom, surface), &_empty);
            return;
        }
        if (nargs != 2 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                is_get ? "space-get expects an ordered space as the first argument"
                       : "space-truncate expects an ordered space as the first argument"),
                &_empty);
            return;
        }
        if (!space_is_ordered(target)) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnsupportedSpaceKind")),
                &_empty);
            return;
        }
        ResultSet idx_rs;
        result_set_init(&idx_rs);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &idx_rs);
        Atom *idx_atom = (idx_rs.len > 0) ? idx_rs.items[0] : expr_arg(atom, 1);
        bool bad_index = idx_atom->kind != ATOM_GROUNDED ||
                         idx_atom->ground.gkind != GV_INT ||
                         idx_atom->ground.ival < 0;
        if (bad_index) {
            free(idx_rs.items);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "ExpectedNonNegativeIndex")),
                &_empty);
            return;
        }
        uint32_t idx = (uint32_t)idx_atom->ground.ival;
        free(idx_rs.items);
        if (is_get) {
            Atom *item = space_get_at(target, idx);
            if (!item) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "IndexOutOfBounds")),
                    &_empty);
                return;
            }
            outcome_set_add(os, item, &_empty);
        } else {
            if (!space_truncate(target, idx)) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "IndexOutOfBounds")),
                    &_empty);
                return;
            }
            outcome_set_add(os, atom_unit(a), &_empty);
        }
        return;
    }

    /* ── bind! ─────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.bind_bang && nargs != 2) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

    if (head_id == g_builtin_syms.bind_bang && nargs == 2 && g_registry) {
        Atom *name = expr_arg(atom, 0);
        Atom *val_expr = expr_arg(atom, 1);
        ResultSet val_rs;
        result_set_init(&val_rs);
        metta_eval(s, a, NULL, val_expr, fuel, &val_rs);
        Atom *val = (val_rs.len > 0) ? val_rs.items[0] : val_expr;
        if (name->kind == ATOM_SYMBOL) {
            /* Deep-copy to persistent arena so value survives eval_arena reset */
            Arena *dst = g_persistent_arena ? g_persistent_arena : a;
            Atom *stored = NULL;
            if (dst == g_persistent_arena &&
                val->kind == ATOM_GROUNDED &&
                val->ground.gkind == GV_SPACE &&
                temp_space_is_registered((Space *)val->ground.ptr)) {
                stored = atom_space(dst,
                    space_persistent_clone((Space *)val->ground.ptr, dst));
            } else {
                stored = (dst == g_persistent_arena)
                    ? atom_deep_copy_shared(dst, val)
                    : atom_deep_copy(dst, val);
            }
            registry_bind_id(g_registry, name->sym_id, stored);
        }
        free(val_rs.items);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-reduct ────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_reduct && nargs == 2 && g_registry) {
        Space **targets = NULL;
        uint32_t ntargets = 0;
        collect_resolved_spaces(s, a, expr_arg(atom, 0), fuel, &targets, &ntargets);

        OutcomeSet vals;
        outcome_set_init(&vals);
        metta_eval_bind(s, a, expr_arg(atom, 1), fuel, &vals);

        if (ntargets > 0) {
            Arena *dst = g_persistent_arena ? g_persistent_arena : a;
            for (uint32_t ti = 0; ti < ntargets; ti++) {
                for (uint32_t vi = 0; vi < vals.len; vi++) {
                    Atom *stored = (dst == g_persistent_arena)
                        ? atom_deep_copy_shared(dst, vals.items[vi].atom)
                        : atom_deep_copy(dst, vals.items[vi].atom);
                    space_add(targets[ti], stored);
                    outcome_set_add(os, atom_unit(a), &vals.items[vi].env);
                }
            }
        }

        free(targets);
        outcome_set_free(&vals);
        return;
    }

    /* ── add-atom ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_atom && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom expects a space as the first argument"), &_empty);
            return;
        }
        /* Deep-copy to persistent arena so atom survives eval_arena reset */
        Arena *dst = g_persistent_arena ? g_persistent_arena : a;
        Atom *stored = (dst == g_persistent_arena)
            ? atom_deep_copy_shared(dst, atom_to_add)
            : atom_deep_copy(dst, atom_to_add);
        space_add(target, stored);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-atom-nodup (dedup variant for forward chaining) ────────────── */
    if (head_id == g_builtin_syms.add_atom_nodup && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom expects a space as the first argument"), &_empty);
            return;
        }
        bool found = space_is_hash(target) ? space_contains_exact(target, atom_to_add) : false;
        if (!found) {
            for (uint32_t i = 0; i < target->len && !found; i++)
                if (atom_eq(space_get_at(target, i), atom_to_add)) found = true;
        }
        if (!found) {
            Arena *dst = g_persistent_arena ? g_persistent_arena : a;
            Atom *stored = (dst == g_persistent_arena)
                ? atom_deep_copy_shared(dst, atom_to_add)
                : atom_deep_copy(dst, atom_to_add);
            space_add(target, stored);
        }
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── remove-atom ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.remove_atom && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_rm = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "remove-atom expects a space as the first argument"), &_empty);
            return;
        }
        space_remove(target, atom_to_rm);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── get-atoms ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_atoms && nargs == 1 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "get-atoms expects a space as its argument"), &_empty);
            return;
        }
        for (uint32_t i = 0; i < target->len; i++)
            outcome_set_add(os, space_get_at(target, i), &_empty);
        return;
    }

    /* ── count-atoms ──────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.count_atoms && nargs == 1 && g_registry) {
        if (active_profile() && !cetta_profile_allows_surface(active_profile(), "count-atoms")) {
            outcome_set_add(os, profile_surface_error(a, atom, "count-atoms"), &_empty);
            return;
        }
        Atom *space_ref = expr_arg(atom, 0);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        outcome_set_add(os, atom_int(a, (int64_t)target->len), &_empty);
        return;
    }

    /* ── collapse-bind ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.collapse_bind) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultBindSet inner;
        rb_set_init(&inner);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &inner);
        /* Spec shape: a single expression whose elements are
           (<atom> <Bindings.toAtom>) pairs. */
        Atom **pairs = arena_alloc(a, sizeof(Atom *) * inner.len);
        for (uint32_t i = 0; i < inner.len; i++) {
            pairs[i] = atom_expr2(a, inner.items[i].atom,
                                  bindings_to_atom(a, &inner.items[i].env));
        }
        outcome_set_add(os, atom_expr(a, pairs, inner.len), &_empty);
        rb_set_free(&inner);
        return;
    }

    /* ── superpose-bind ────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.superpose_bind) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *list = expr_arg(atom, 0);
        if (list->kind != ATOM_EXPR) {
            outcome_set_add(os,
                bad_arg_type_error(s, a, atom, 1, atom_expression_type(a), list),
                &_empty);
            return;
        }
        if (list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < list->expr.len; i++) {
                Atom *pair = list->expr.elems[i];
                if (pair->kind != ATOM_EXPR || pair->expr.len != 2) continue;
                Bindings restored;
                if (!bindings_from_atom(pair->expr.elems[1], &restored)) continue;
                outcome_set_add_move(os, pair->expr.elems[0], &restored);
                bindings_free(&restored);
            }
        }
        return;
    }

    /* ── metta (self-referential eval with type/space) ─────────────────── */
    if (head_id == g_builtin_syms.metta && nargs == 3 && g_registry) {
        Atom *to_eval = expr_arg(atom, 0);
        Atom *type_arg = expr_arg(atom, 1);
        Atom *space_ref = expr_arg(atom, 2);
        Space *target = resolve_space(g_registry, space_ref);
        if (!target) target = s;
        Atom *etype = atom_is_symbol_id(type_arg, g_builtin_syms.undefined_type) ? NULL : type_arg;
        eval_direct_outcomes(target, a, etype, to_eval, fuel, os);
        return;
    }

    /* ── evalc (eval in context space) ─────────────────────────────────── */
    if (head_id == g_builtin_syms.evalc && g_registry) {
        if (nargs != 2) {
            outcome_set_add(os, call_signature_error(a, atom,
                "(evalc <atom> <space>)"), &_empty);
            return;
        }
        Atom *to_eval = expr_arg(atom, 0);
        Atom *space_ref = expr_arg(atom, 1);
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, call_signature_error(a, atom,
                "(evalc <atom> <space>)"), &_empty);
            return;
        }
        eval_direct_outcomes(target, a, NULL, to_eval, fuel, os);
        return;
    }

    /* ── new-state / get-state / change-state! ───────────────────────────── */
    if (head_id == g_builtin_syms.new_state) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *initial = expr_arg(atom, 0);
        /* Allocate state in persistent arena (survives eval_arena reset) */
        Arena *pa = g_persistent_arena ? g_persistent_arena : a;
        StateCell *cell = arena_alloc(pa, sizeof(StateCell));
        cell->value = atom_deep_copy(pa, initial);
        /* Infer content type from initial value */
        Atom **itypes;
        uint32_t nit = get_atom_types(s, a, initial, &itypes);
        cell->content_type = (nit > 0) ? atom_deep_copy(pa, itypes[0]) : atom_undefined_type(pa);
        free(itypes);
        outcome_set_add(os, atom_state(pa, cell), &_empty);
        return;
    }
    if (head_id == g_builtin_syms.get_state) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultBindSet refs;
        rb_set_init(&refs);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &refs);
        for (uint32_t i = 0; i < refs.len; i++) {
            Atom *state_ref = bindings_apply(&refs.items[i].env, a, refs.items[i].atom);
            state_ref = resolve_registry_refs(a, state_ref);
            if (state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE) {
                StateCell *cell = (StateCell *)state_ref->ground.ptr;
                outcome_set_add(os, cell->value, &refs.items[i].env);
            } else {
                outcome_set_add(os,
                    state_bad_arg_type_error(s, a, atom, 1, state_ref),
                    &refs.items[i].env);
            }
        }
        outcome_set_free(&refs);
        return;
    }
    if (head_id == g_builtin_syms.change_state_bang) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        ResultBindSet refs;
        rb_set_init(&refs);
        metta_eval_bind(s, a, expr_arg(atom, 0), fuel, &refs);
        for (uint32_t i = 0; i < refs.len; i++) {
            Atom *state_ref = bindings_apply(&refs.items[i].env, a, refs.items[i].atom);
            state_ref = resolve_registry_refs(a, state_ref);
            if (!(state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE)) {
                outcome_set_add(os,
                    state_bad_arg_type_error(s, a, atom, 1, state_ref),
                    &refs.items[i].env);
                continue;
            }

            ResultBindSet vals;
            rb_set_init(&vals);
            Atom *bound_val_expr = bindings_apply(&refs.items[i].env, a, expr_arg(atom, 1));
            metta_eval_bind(s, a, bound_val_expr, fuel, &vals);
            for (uint32_t vi = 0; vi < vals.len; vi++) {
                Bindings merged;
                if (!bindings_clone_merge(&merged, &refs.items[i].env,
                                          &vals.items[vi].env))
                    continue;
                StateCell *cell = (StateCell *)state_ref->ground.ptr;
                Atom *new_v = bindings_apply(&vals.items[vi].env, a, vals.items[vi].atom);
                Atom **new_types;
                uint32_t nnt = get_atom_types(s, a, new_v, &new_types);
                bool type_ok = false;
                for (uint32_t ti = 0; ti < nnt; ti++) {
                    Bindings tb;
                    bindings_init(&tb);
                    if (match_types(cell->content_type, new_types[ti], &tb)) {
                        type_ok = true;
                        bindings_free(&tb);
                        break;
                    }
                    bindings_free(&tb);
                }
                free(new_types);
                if (type_ok) {
                    cell->value = g_persistent_arena ? atom_deep_copy(g_persistent_arena, new_v) : new_v;
                    outcome_set_add(os, state_ref, &merged);
                } else {
                    Atom *error_state_arg = expr_arg(atom, 0);
                    const char *error_state_name = atom_name_cstr(error_state_arg);
                    if (error_state_name && error_state_name[0] == '&')
                        error_state_arg = state_ref;
                    Atom **full = arena_alloc(a, sizeof(Atom *) * 3);
                    full[0] = atom_symbol(a, "change-state!");
                    full[1] = error_state_arg;
                    full[2] = expr_arg(atom, 1);
                    Atom **et;
                    uint32_t net = get_atom_types(s, a, new_v, &et);
                    Atom *actual_t = (net > 0) ? et[0] : atom_undefined_type(a);
                    free(et);
                    Atom *reason = atom_expr(a, (Atom*[]){
                        atom_symbol(a, "BadArgType"), atom_int(a, 2),
                        cell->content_type, actual_t
                    }, 4);
                    outcome_set_add(os, atom_error(a, atom_expr(a, full, 3), reason), &merged);
                }
                bindings_free(&merged);
            }
            outcome_set_free(&vals);
        }
        outcome_set_free(&refs);
        return;
    }

    /* ── pragma! ────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.pragma_bang) {
        if (nargs < 2) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "pragma! expects key and value as arguments")),
                &_empty);
            return;
        }

        Atom *key = expr_arg(atom, 0);
        Atom *value = expr_arg(atom, 1);
        CettaEvalSession *session = active_eval_session();
        bool bare_minimal = eval_bare_minimal_enabled();
        bool handled = false;

        if (atom_is_symbol_id(key, g_builtin_syms.type_check) &&
            atom_is_symbol_id(value, g_builtin_syms.auto_text)) {
            handled = cetta_eval_session_set_type_check_auto(session, true);
        } else if (atom_is_symbol_id(key, g_builtin_syms.interpreter) &&
                   atom_is_symbol_id(value, g_builtin_syms.bare_minimal)) {
            handled = cetta_eval_session_set_interpreter_mode(
                session, CETTA_INTERPRETER_BARE_MINIMAL);
        } else if (!bare_minimal &&
                   atom_is_symbol_id(key, g_builtin_syms.max_stack_depth)) {
            if (value->kind == ATOM_GROUNDED &&
                value->ground.gkind == GV_INT &&
                value->ground.ival >= 0) {
                handled = cetta_eval_session_set_max_stack_depth(
                    session, (int)value->ground.ival);
            } else {
                outcome_set_add(os, atom_error(a, atom,
                    atom_symbol(a, "UnsignedIntegerIsExpected")),
                    &_empty);
                return;
            }
        } else if (!bare_minimal && key->kind != ATOM_SYMBOL) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "pragma! expects symbol atom as a key")),
                &_empty);
            return;
        } else if (!bare_minimal && key->kind == ATOM_SYMBOL) {
            CettaEvalOptionValueKind value_kind = CETTA_EVAL_OPTION_VALUE_TEXT;
            const char *value_repr = NULL;
            int64_t int_value = 0;
            char int_buf[32];

            if (value->kind == ATOM_SYMBOL) {
                value_kind = CETTA_EVAL_OPTION_VALUE_SYMBOL;
                value_repr = atom_name_cstr(value);
            } else if (value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT) {
                value_kind = CETTA_EVAL_OPTION_VALUE_INT;
                int_value = value->ground.ival;
                snprintf(int_buf, sizeof(int_buf), "%" PRId64, int_value);
                value_repr = int_buf;
            } else if (value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING) {
                value_kind = CETTA_EVAL_OPTION_VALUE_TEXT;
                value_repr = value->ground.sval;
            } else {
                value_repr = atom_to_string(a, value);
            }
            handled = cetta_eval_session_record_generic_setting(
                session, atom_name_cstr(key), value_kind, value_repr, int_value);
        }

        outcome_set_add(os, handled ? atom_unit(a) : atom, &_empty);
        return;
    }

    /* ── nop (evaluate for side effects, return unit) ────────────────────── */
    if (head_id == g_builtin_syms.nop && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &inner);
        free(inner.items);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── get-metatype ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_metatype) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *target = expr_arg(atom, 0);
        Atom *val = registry_lookup_atom(target);
        if (val) target = val;
        outcome_set_add(os, get_meta_type(a, target), &_empty);
        return;
    }

    /* ── get-type ───────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_type && nargs == 1) {
        Atom *target = expr_arg(atom, 0);
        /* Resolve registry tokens for get-type */
        Atom *val = registry_lookup_atom(target);
        if (val) target = val;
        Atom **types;
        uint32_t n = get_atom_types_profiled(s, a, target, &types);
        /* If only %Undefined% and arg is an expression, try evaluating first */
        if (n == 1 && atom_is_symbol_id(types[0], g_builtin_syms.undefined_type) &&
            target->kind == ATOM_EXPR) {
            free(types);
            ResultSet evr;
            result_set_init(&evr);
            metta_eval(s, a, NULL, target, fuel, &evr);
            if (evr.len > 0) {
                n = get_atom_types_profiled(s, a, evr.items[0], &types);
            } else {
                types = cetta_malloc(sizeof(Atom *));
                types[0] = atom_undefined_type(a);
                n = 1;
            }
            free(evr.items);
        }
        for (uint32_t i = 0; i < n; i++)
            outcome_set_add(os, types[i], &_empty);
        free(types);
        return;
    }

    /* ── get-type-space ────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_type_space) {
        Atom *resolved_space = NULL;
        if (nargs >= 1) {
            resolved_space = expr_arg(atom, 0);
            Atom *val = registry_lookup_atom(resolved_space);
            if (val) resolved_space = val;
        }

        if (nargs != 2) {
            Atom **err_elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
            err_elems[0] = atom_symbol(a, "get-type-space");
            for (uint32_t i = 0; i < nargs; i++) {
                Atom *arg = expr_arg(atom, i);
                if (i == 0 && resolved_space) {
                    if (atom_is_symbol_id(arg, g_builtin_syms.self))
                        arg = atom_symbol(a, "ModuleSpace(GroundingSpace-top)");
                    else
                        arg = resolved_space;
                }
                err_elems[i + 1] = arg;
            }
            outcome_set_add(os, atom_error(a, atom_expr(a, err_elems, nargs + 1),
                atom_symbol(a, "IncorrectNumberOfArguments")), &_empty);
            return;
        }

        if (!(resolved_space &&
              resolved_space->kind == ATOM_GROUNDED &&
              resolved_space->ground.gkind == GV_SPACE)) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "get-type-space expects a space as the first argument")),
                &_empty);
            return;
        }

        Atom *target = expr_arg(atom, 1);
        Atom *val = registry_lookup_atom(target);
        if (val) target = val;

        Atom **types;
        uint32_t n = get_atom_types((Space *)resolved_space->ground.ptr, a, target, &types);
        for (uint32_t i = 0; i < n; i++)
            outcome_set_add(os, types[i], &_empty);
        free(types);
        return;
    }

    /* ── assertEqual ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertEqual && nargs == 2) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        result_set_resolve_registry_refs(a, &actual);
        result_set_resolve_registry_refs(a, &expected);
        bool ok = (actual.len == expected.len);
        if (ok && actual.len > 0) {
            bool *used = calloc(expected.len, sizeof(bool));
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_eq(actual.items[i], expected.items[j])) {
                        used[j] = true; found = true; break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        if (ok) {
            free(actual.items);
            free(expected.items);
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            /* Build HE-compatible error message:
               \nExpected: [e1, e2]\nGot: [a1, a2]\nMissed/Excessive */
            char buf[2048];
            int pos = 0;
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "\nExpected: [");
            for (uint32_t i = 0; i < expected.len; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ", ");
                FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                if (tmp) { atom_print(expected.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
            }
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "]\nGot: [");
            for (uint32_t i = 0; i < actual.len; i++) {
                if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ", ");
                FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                if (tmp) { atom_print(actual.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
            }
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "]");
            /* Missed results (in expected but not actual) */
            bool has_missed = false;
            for (uint32_t i = 0; i < expected.len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < actual.len; j++)
                    if (atom_eq(expected.items[i], actual.items[j])) { found = true; break; }
                if (!found) {
                    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        has_missed ? ", " : "\nMissed results: ");
                    has_missed = true;
                    FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                    if (tmp) { atom_print(expected.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
                }
            }
            /* Excessive results (in actual but not expected) */
            bool has_excess = false;
            for (uint32_t i = 0; i < actual.len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++)
                    if (atom_eq(actual.items[i], expected.items[j])) { found = true; break; }
                if (!found) {
                    pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                        has_excess ? ", " : "\nExcessive results: ");
                    has_excess = true;
                    FILE *tmp = fmemopen(buf + pos, sizeof(buf) - (size_t)pos, "w");
                    if (tmp) { atom_print(actual.items[i], tmp); pos += (int)ftell(tmp); fclose(tmp); }
                }
            }
            free(actual.items);
            free(expected.items);
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqual"),
                    expr_arg(atom, 0), expr_arg(atom, 1)),
                atom_string(a, buf)), &_empty);
        }
        return;
    }

    /* ── assertEqualToResult ───────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertEqualToResult && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &actual);
        result_set_resolve_registry_refs(a, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            if (actual.len == expected_list->expr.len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    Atom *expected_item = resolve_registry_refs(a, expected_list->expr.elems[i]);
                    if (!atom_eq(actual.items[i], expected_item))
                        ok = false;
                }
            }
        }
        /* Empty expected () matches empty result set */
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0) {
            ok = true;
        }
        free(actual.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, "mismatch")), &_empty);
        }
        return;
    }

    /* ── assertEqualMsg ──────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertEqualMsg && nargs == 3) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        result_set_resolve_registry_refs(a, &actual);
        result_set_resolve_registry_refs(a, &expected);
        bool ok = (actual.len == expected.len);
        if (ok && actual.len > 0) {
            bool *used = calloc(expected.len, sizeof(bool));
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_eq(actual.items[i], expected.items[j])) {
                        used[j] = true; found = true; break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        free(actual.items);
        free(expected.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            /* Error message is the 3rd arg (user-provided string) */
            outcome_set_add(os, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertEqualMsg"),
                    expr_arg(atom, 0), expr_arg(atom, 1)}, 3),
                expr_arg(atom, 2)), &_empty);
        }
        return;
    }

    /* ── assertEqualToResultMsg ────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertEqualToResultMsg && nargs == 3) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        result_set_resolve_registry_refs(a, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            if (actual.len == expected_list->expr.len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    Atom *expected_item = resolve_registry_refs(a, expected_list->expr.elems[i]);
                    if (!atom_eq(actual.items[i], expected_item))
                        ok = false;
                }
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertEqualToResultMsg"),
                    expr_arg(atom, 0), expected_list}, 3),
                expr_arg(atom, 2)), &_empty);
        }
        return;
    }

    /* ── assertAlphaEqual ─────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertAlphaEqual && nargs == 2) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        bool ok = (actual.len == expected.len);
        if (ok && actual.len > 0) {
            bool *used = calloc(expected.len, sizeof(bool));
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_alpha_eq(actual.items[i], expected.items[j])) {
                        used[j] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        free(actual.items);
        free(expected.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertAlphaEqual"),
                    expr_arg(atom, 0), expr_arg(atom, 1)),
                atom_string(a, "mismatch")), &_empty);
        }
        return;
    }

    /* ── assertAlphaEqualMsg ──────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertAlphaEqualMsg && nargs == 3) {
        ResultSet actual, expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        bool ok = (actual.len == expected.len);
        if (ok && actual.len > 0) {
            bool *used = calloc(expected.len, sizeof(bool));
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_alpha_eq(actual.items[i], expected.items[j])) {
                        used[j] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        free(actual.items);
        free(expected.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertAlphaEqualMsg"),
                    expr_arg(atom, 0), expr_arg(atom, 1)}, 3),
                expr_arg(atom, 2)), &_empty);
        }
        return;
    }

    /* ── assertAlphaEqualToResult (alpha-equivalence comparison) ─────── */
    if (head_id == g_builtin_syms.assertAlphaEqualToResult && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR && actual.len == expected_list->expr.len) {
            ok = true;
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                if (!atom_alpha_eq(actual.items[i], expected_list->expr.elems[i]))
                    ok = false;
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertAlphaEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, "mismatch")), &_empty);
        }
        return;
    }

    /* ── assertAlphaEqualToResultMsg ──────────────────────────────────── */
    if (head_id == g_builtin_syms.assertAlphaEqualToResultMsg && nargs == 3) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR && actual.len == expected_list->expr.len) {
            ok = true;
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                if (!atom_alpha_eq(actual.items[i], expected_list->expr.elems[i]))
                    ok = false;
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            outcome_set_add(os, atom_error(a,
                atom_expr(a, (Atom*[]){atom_symbol(a, "assertAlphaEqualToResultMsg"),
                    expr_arg(atom, 0), expected_list}, 3),
                expr_arg(atom, 2)), &_empty);
        }
        return;
    }

    /* ── assertIncludes ────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assertIncludes && nargs == 2) {
        ResultSet actual;
        result_set_init(&actual);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        /* Check that every expected item is in the actual results */
        bool ok = true;
        if (expected_list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < expected_list->expr.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < actual.len; j++) {
                    if (atom_eq(expected_list->expr.elems[i], actual.items[j])) {
                        found = true; break;
                    }
                }
                if (!found) ok = false;
            }
        }
        if (ok) {
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            /* Build error: (assertIncludes error: <expected> not included in result: <actual>) */
            Atom *actual_expr = atom_expr(a, actual.items, actual.len);
            Atom *msg = atom_expr(a, (Atom*[]){
                atom_symbol(a, "assertIncludes"), atom_symbol(a, "error:"),
                expected_list, atom_symbol(a, "not"), atom_symbol(a, "included"),
                atom_symbol(a, "in"), atom_symbol(a, "result:"),
                actual_expr}, 8);
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertIncludes"),
                    expr_arg(atom, 0), expected_list),
                msg), &_empty);
        }
        free(actual.items);
        return;
    }

    {
        Atom *tail_next = NULL;
        Atom *tail_type = NULL;
        __attribute__((cleanup(bindings_free))) Bindings tail_env;
        OutcomeSet dispatch_results;
        outcome_set_init(&dispatch_results);
        if (handle_dispatch(s, a, atom, etype, fuel, preserve_bindings,
                            &tail_next, &tail_type, &tail_env,
                            &dispatch_results)) {
            if (tail_next) {
                if (preserve_bindings &&
                    !bindings_merge_into(&current_env, &tail_env)) {
                    outcome_set_free(&dispatch_results);
                    return;
                }
                outcome_set_free(&dispatch_results);
                if (tail_type) etype = tail_type;
                TAIL_REENTER(tail_next);
            }
            outcome_set_append_prefixed(a, os, &dispatch_results, &current_env,
                                        preserve_bindings);
            outcome_set_free(&dispatch_results);
            return;
        }
        outcome_set_free(&dispatch_results);
    }
}

#undef outcome_set_add
#undef TAIL_REENTER
#undef TAIL_REENTER_ENV


/* ── Top-level evaluation ───────────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs) {
    metta_eval(s, a, NULL, expr, current_eval_fuel_limit(), rs);
}

void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs) {
    g_registry = r;
    g_persistent_arena = persistent;
    metta_eval(s, a, NULL, expr, current_eval_fuel_limit(), rs);
}

void eval_set_default_fuel(int fuel) {
    cetta_eval_session_set_fuel_limit(fallback_eval_session(), fuel);
}

int eval_get_default_fuel(void) {
    return fallback_eval_session()->options.fuel_limit;
}

void eval_set_library_context(CettaLibraryContext *ctx) {
    g_library_context = ctx;
    if (!ctx) return;
    ctx->session.options.fuel_limit = fallback_eval_session()->options.fuel_limit;
}
