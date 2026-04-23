#define _GNU_SOURCE
#include "eval.h"
#include "lang_adapter.h"
#include "match.h"
#include "search_machine.h"
#include "grounded.h"
#include "library.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "parser.h"
#include "petta_contract.h"
#include "stats.h"
#include "answer_bank.h"
#include "table_store.h"
#include "term_universe.h"
#include "variant_shape.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/resource.h>

typedef struct EvalRetainedTailArena EvalRetainedTailArena;

/* Global registry for named spaces/values (set by eval_top_with_registry) */
static Registry *g_registry = NULL;
/* Current evaluation root space and persistent fallback. */
static Space *g_eval_root_space = NULL;
static TermUniverse g_eval_fallback_universe = {0};
/* Query cache for the current logical evaluation episode. */
static TableStore g_episode_table;
static bool g_episode_table_ready = false;
static AnswerBank g_episode_answer_bank;
static bool g_episode_answer_bank_ready = false;
static VariantBank g_episode_outcome_variant_bank;
static bool g_episode_outcome_variant_bank_ready = false;
static Arena g_episode_survivor_arena;
static bool g_episode_survivor_arena_ready = false;
static EvalRetainedTailArena *g_retained_tail_arenas = NULL;
/* Active importable library set */
static CettaLibraryContext *g_library_context = NULL;
static CettaEvalSession g_fallback_eval_session;
static bool g_fallback_eval_session_ready = false;

typedef enum {
    CETTA_OUTCOME_CONTROL_NONE = 0,
    CETTA_OUTCOME_CONTROL_CUT = 1,
    CETTA_OUTCOME_CONTROL_ERROR_VALUE = 2,
} CettaOutcomeControl;

typedef enum {
    CETTA_TAIL_SCOPE_LOCAL = 0,
    CETTA_TAIL_SCOPE_CONTINUATION = 1,
} CettaTailScope;

typedef struct {
    Space **items;
    uint32_t len, cap;
} TempSpaceSet;

static TempSpaceSet g_temp_spaces = {0};

typedef struct {
    Arena scratch;
    Arena generated;
    Arena *survivor_arena;
    bool active;
} EvalQueryEpisode;

typedef struct {
    Arena slots[2];
    bool ready[2];
    int active_slot;
    int pending_slot;
} EvalLocalTailSurvivor;

typedef struct EvalRetainedTailArena {
    Arena arena;
    struct EvalRetainedTailArena *next;
} EvalRetainedTailArena;

static Atom *resolve_registry_refs(Arena *a, Atom *atom);
static Arena *eval_storage_arena(Arena *fallback);
static Atom *guard_mork_space_surface(Arena *a, Atom *call, Space *space,
                                      const char *surface,
                                      const char *explicit_surface);
static Atom *guard_mork_handle_surface(Space *s, Arena *a, Atom *call,
                                       Atom *space_expr, int fuel,
                                       const char *surface,
                                       const char *explicit_surface);
static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr,
                                       int fuel);
static Atom *space_arg_error(Arena *a, Atom *call, const char *message);
static bool bindings_project_body_visible_env(Arena *a, Atom *body,
                                              const Bindings *full,
                                              Bindings *out);
static bool symbol_id_is_builtin_surface(SymbolId id);

static uint64_t eval_monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

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

const CettaLanguageSpec *eval_current_language(void) {
    return cetta_language_current();
}

static Atom *adapt_runtime_atoms_or_error(Arena *a,
                                          Atom *call_atom,
                                          Space *target,
                                          Atom *surface_atom,
                                          Atom ***out_atoms,
                                          int *out_len) {
    int adapted_len;
    if (!out_atoms || !out_len)
        return atom_error(a, call_atom,
                          atom_symbol(a, "RuntimeLanguageAdaptationFailed"));
    *out_atoms = NULL;
    *out_len = 0;
    adapted_len = cetta_language_adapt_runtime_atoms_for_session(
        active_eval_session(), target, surface_atom, eval_storage_arena(a),
        out_atoms);
    if (adapted_len == CETTA_RUNTIME_ADAPT_ERR_HELPER_MISSING) {
        free(*out_atoms);
        *out_atoms = NULL;
        return atom_error(a, call_atom,
                          atom_symbol(a, "PettaRuntimeHelperMissing"));
    }
    if (adapted_len < 0 || (adapted_len > 0 && !*out_atoms)) {
        free(*out_atoms);
        *out_atoms = NULL;
        return atom_error(a, call_atom,
                          atom_symbol(a, "RuntimeLanguageAdaptationFailed"));
    }
    *out_len = adapted_len;
    return NULL;
}

static Atom *prepare_runtime_mutation_atoms_or_error(
    Space *s,
    Arena *a,
    Atom *call_atom,
    Atom *space_ref,
    Atom *surface_atom,
    int fuel,
    const char *surface_name,
    const char *explicit_surface,
    const char *space_error_message,
    Space **out_target,
    Atom ***out_atoms,
    int *out_len
) {
    Atom *mork_handle_error = NULL;
    Atom *mork_error = NULL;
    Atom *adapt_error = NULL;
    Space *target = NULL;

    if (!out_target || !out_atoms || !out_len) {
        return atom_error(a, call_atom,
                          atom_symbol(a, "RuntimeLanguageAdaptationFailed"));
    }
    *out_target = NULL;
    *out_atoms = NULL;
    *out_len = 0;

    mork_handle_error = guard_mork_handle_surface(
        s, a, call_atom, space_ref, fuel, surface_name, explicit_surface);
    if (mork_handle_error)
        return mork_handle_error;

    target = resolve_single_space_arg(s, a, space_ref, fuel);
    if (!target) {
        return space_arg_error(a, call_atom, space_error_message);
    }

    mork_error = guard_mork_space_surface(
        a, call_atom, target, surface_name, explicit_surface);
    if (mork_error)
        return mork_error;

    if (!space_match_backend_materialize_attached(target, eval_storage_arena(a))) {
        return atom_error(a, call_atom,
                          atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed"));
    }

    adapt_error = adapt_runtime_atoms_or_error(
        a, call_atom, target, surface_atom, out_atoms, out_len);
    if (adapt_error)
        return adapt_error;

    *out_target = target;
    return NULL;
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

#define CETTA_DEFAULT_EVAL_C_STACK_BUDGET_BYTES (1024u * 1024u)
#define CETTA_MIN_EVAL_C_STACK_BUDGET_BYTES (1024u * 1024u)
#define CETTA_MAX_EVAL_C_STACK_BUDGET_BYTES (16u * 1024u * 1024u)

static __thread uint32_t g_eval_c_stack_guard_depth = 0;
static __thread uintptr_t g_eval_c_stack_anchor = 0;
static __thread size_t g_eval_c_stack_budget_bytes = 0;

typedef struct {
    bool active;
} EvalCStackGuard;

static size_t eval_c_stack_budget_bytes(void) {
    if (g_eval_c_stack_budget_bytes != 0)
        return g_eval_c_stack_budget_bytes;

    size_t budget = CETTA_DEFAULT_EVAL_C_STACK_BUDGET_BYTES;
#ifdef RLIMIT_STACK
    struct rlimit rl;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 &&
        rl.rlim_cur != RLIM_INFINITY && rl.rlim_cur > 0) {
        size_t stack_soft_limit = (size_t)rl.rlim_cur;
        size_t reserve = stack_soft_limit / 4;
        if (reserve < (1024u * 1024u))
            reserve = (1024u * 1024u);
        if (stack_soft_limit > reserve)
            budget = stack_soft_limit - reserve;
    }
#endif
    if (budget < CETTA_MIN_EVAL_C_STACK_BUDGET_BYTES)
        budget = CETTA_MIN_EVAL_C_STACK_BUDGET_BYTES;
    if (budget > CETTA_MAX_EVAL_C_STACK_BUDGET_BYTES)
        budget = CETTA_MAX_EVAL_C_STACK_BUDGET_BYTES;
    g_eval_c_stack_budget_bytes = budget;
    cetta_runtime_stats_set(CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_BUDGET_BYTES,
                            (uint64_t)budget);
    return budget;
}

static bool eval_c_stack_guard_enter(int fuel, EvalCStackGuard *guard) {
    char stack_probe = 0;
    uintptr_t here = (uintptr_t)&stack_probe;
    if (guard) guard->active = false;
    /* Fuel limits logical evaluation, but native recursion can still consume
       stack before fuel bottoms out, so guard both finite and infinite runs. */
    if (g_eval_c_stack_guard_depth++ == 0)
        g_eval_c_stack_anchor = here;
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_DEPTH_PEAK,
        (uint64_t)g_eval_c_stack_guard_depth);
    uintptr_t delta = here > g_eval_c_stack_anchor
        ? (here - g_eval_c_stack_anchor)
        : (g_eval_c_stack_anchor - here);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_DELTA_BYTES_PEAK,
        (uint64_t)delta);
    if (delta > eval_c_stack_budget_bytes()) {
        if (g_eval_c_stack_guard_depth > 0)
            g_eval_c_stack_guard_depth--;
        if (g_eval_c_stack_guard_depth == 0)
            g_eval_c_stack_anchor = 0;
        return false;
    }
    if (guard) guard->active = true;
    return true;
}

static void eval_c_stack_guard_leave(EvalCStackGuard *guard) {
    if (!guard || !guard->active)
        return;
    guard->active = false;
    if (g_eval_c_stack_guard_depth > 0)
        g_eval_c_stack_guard_depth--;
    if (g_eval_c_stack_guard_depth == 0)
        g_eval_c_stack_anchor = 0;
}

int eval_current_effective_fuel_limit(void) {
    return current_eval_fuel_limit();
}

static const CettaEvalOptionEntry *active_eval_option(const char *key) {
    return cetta_evaluator_options_find(active_eval_options_const(), key);
}

static bool mork_space_sugar_option_allows(const CettaEvalOptionEntry *option) {
    if (!option) return false;
    if (option->kind == CETTA_EVAL_OPTION_VALUE_INT) {
        return option->int_value != 0;
    }
    return strcmp(option->repr, "allow") == 0 ||
           strcmp(option->repr, "on") == 0 ||
           strcmp(option->repr, "true") == 0;
}

static bool generic_mork_space_sugar_allowed(void) {
    return mork_space_sugar_option_allows(active_eval_option("mork-space-sugar"));
}

static bool language_known_head_query_miss_is_failure(void) {
    const CettaLanguageSpec *lang = eval_current_language();
    return cetta_language_known_head_query_miss_is_failure(lang);
}

static bool language_is_petta(void) {
    const CettaLanguageSpec *lang = eval_current_language();
    return lang && lang->id == CETTA_LANGUAGE_PETTA;
}

static CettaNamedSpacePolicy language_named_space_policy(void) {
    const CettaLanguageSpec *lang = eval_current_language();
    return cetta_language_named_space_policy(lang);
}

static CettaFunctionArgPolicy language_function_arg_policy(Atom *head,
                                                           uint32_t arg_index,
                                                           Atom *domain_type) {
    return cetta_language_function_arg_policy(eval_current_language(),
                                              atom_head_symbol_id(head),
                                              arg_index,
                                              domain_type);
}

static bool atom_is_native_dispatch_head(Atom *head) {
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    return is_grounded_op(head->sym_id) ||
           (language_is_petta() && head->sym_id == g_builtin_syms.equals);
}

static bool petta_grounded_op_is_under_applied(Atom *head, uint32_t nargs) {
    if (!language_is_petta() || !head || head->kind != ATOM_SYMBOL)
        return false;
    SymbolId id = head->sym_id;
    uint32_t expected = 0;
    if (id == g_builtin_syms.op_plus ||
        id == g_builtin_syms.op_minus ||
        id == g_builtin_syms.op_mul ||
        id == g_builtin_syms.op_div ||
        id == g_builtin_syms.op_mod ||
        id == g_builtin_syms.op_lt ||
        id == g_builtin_syms.op_gt ||
        id == g_builtin_syms.op_le ||
        id == g_builtin_syms.op_ge ||
        id == g_builtin_syms.equals ||
        id == g_builtin_syms.alpha_eq ||
        id == g_builtin_syms.op_eq ||
        id == g_builtin_syms.op_ne ||
        id == g_builtin_syms.op_and ||
        id == g_builtin_syms.op_or ||
        id == g_builtin_syms.op_xor) {
        expected = 2;
    } else if (id == g_builtin_syms.if_equal) {
        expected = 4;
    } else {
        return false;
    }
    return nargs < expected;
}

static bool petta_user_callable_is_under_applied(Space *s, Atom *atom) {
    if (!language_is_petta() || !s || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len == 0)
        return false;
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    uint32_t nargs = atom->expr.len > 0 ? atom->expr.len - 1 : 0;
    return space_equations_have_head_with_greater_arity(s, head->sym_id,
                                                        nargs);
}

static bool expression_should_eval_as_superpose_list(Space *s, Atom *atom) {
    if (!s || !atom || atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return false;
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    uint32_t nargs = atom->expr.len - 1;
    return atom_is_native_dispatch_head(head) ||
           space_equations_have_head_with_arity(s, head->sym_id, nargs);
}

static bool parsed_syntax_needs_quote(Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return false;
    Atom *head = atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    if (atom_is_symbol_id(head, g_builtin_syms.quote))
        return false;
    return atom_is_native_dispatch_head(head) ||
           symbol_id_is_builtin_surface(head->sym_id);
}

static bool catch_result_is_stuck_known_call(Atom *body, Atom *result) {
    if (!body || !result || !atom_eq(body, result) ||
        body->kind != ATOM_EXPR || body->expr.len == 0) {
        return false;
    }
    Atom *head = body->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    return atom_is_native_dispatch_head(head) ||
           symbol_id_is_builtin_surface(head->sym_id);
}

static Atom *petta_unwrap_noeval_value(Atom *atom) {
    while (atom && atom->kind == ATOM_EXPR && atom->expr.len == 2 &&
           atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.noeval)) {
        atom = atom->expr.elems[1];
    }
    return atom;
}

static bool known_head_query_miss_should_fail(Space *s, Atom *call_atom) {
    if (!language_known_head_query_miss_is_failure()) return false;
    if (!call_atom || call_atom->kind != ATOM_EXPR || call_atom->expr.len == 0) return false;
    Atom *head = call_atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL) return false;
    return space_equations_may_match_known_head(s, head->sym_id);
}

static bool atom_resolves_to_mork_handle(Space *s, Arena *a, Atom *space_expr,
                                         int fuel) {
    uint64_t id = 0;
    Atom *direct = resolve_registry_refs(a, space_expr);
    if (cetta_native_handle_arg(direct, "mork-space", &id)) {
        return true;
    }

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (uint32_t i = 0; i < rs.len; i++) {
        if (cetta_native_handle_arg(rs.items[i], "mork-space", &id)) {
            free(rs.items);
            return true;
        }
    }
    free(rs.items);
    return false;
}

static bool generic_mork_handle_sugar_allowed(Space *s, Arena *a, Atom *space_expr,
                                              int fuel) {
    return generic_mork_space_sugar_allowed() &&
           atom_resolves_to_mork_handle(s, a, space_expr, fuel);
}

static bool atom_is_mork_space_handle_value(Atom *atom) {
    uint64_t id = 0;
    return cetta_native_handle_arg(atom, "mork-space", &id);
}

static bool space_requires_explicit_mork_namespace(const Space *space) {
    return space &&
           space->match_backend.kind == SPACE_ENGINE_MORK &&
           !generic_mork_space_sugar_allowed();
}

static Atom *mork_space_surface_error(Arena *a, Atom *call,
                                      const char *surface,
                                      const char *explicit_surface) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "generic %s does not operate on MorkSpace unless you enable (pragma! mork-space-sugar allow); use explicit %s",
             surface, explicit_surface);
    return atom_error(a, call, atom_string(a, buf));
}

static Atom *mork_handle_surface_error(Arena *a, Atom *call,
                                       const char *surface,
                                       const char *explicit_surface) {
    char buf[256];
    snprintf(buf, sizeof(buf),
             "MorkSpace requires explicit %s; %s does not operate on MorkSpace",
             explicit_surface, surface);
    return atom_error(a, call, atom_string(a, buf));
}

static Atom *make_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs);
static void eval_for_caller(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                            const Bindings *prefix, bool preserve_bindings,
                            OutcomeSet *os);
static Atom *outcome_atom_materialize(Arena *a, Outcome *out);
static void outcome_set_add_existing(OutcomeSet *os, const Outcome *src);
static void outcome_set_add_prefixed_outcome(Arena *a, OutcomeSet *os,
                                             const Outcome *src,
                                             const Bindings *outer_env,
                                             bool preserve_bindings);
static bool bindings_effective_merge(Bindings *owned,
                                     const Bindings **effective,
                                     const Bindings *outer_env,
                                     const Bindings *inner_env,
                                     bool include_constraints);
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os);
static bool petta_eval_open_callable_relation_values(
    Space *s, Arena *a, Atom *term, int fuel,
    const Bindings *prefix, bool preserve_bindings, OutcomeSet *os);
static bool emit_unquoted_mork_rows(Space *s, Arena *a, SymbolId internal_head_id,
                                    Atom *surface_atom, uint32_t nargs,
                                    Atom **args, bool evaluate_rows, int fuel,
                                    OutcomeSet *os);
static bool emit_direct_mork_match_rows(Space *s, Arena *a, Atom *surface_atom,
                                        Atom **args, int fuel,
                                        OutcomeSet *os);

static Atom *dispatch_named_native(Space *s, Arena *a, SymbolId head_id,
                                   Atom **args, uint32_t nargs) {
    Atom *head = atom_symbol_id(a, head_id);
    Atom *result = grounded_dispatch(a, head, args, nargs);
    if (result) return result;
    if (g_library_context) {
        return cetta_library_dispatch_native(g_library_context, s, a, head, args, nargs);
    }
    return NULL;
}

static Atom *rewrite_error_call(Arena *a, Atom *surface_atom, Atom *result) {
    if (!result || !atom_is_error(result) || result->kind != ATOM_EXPR ||
        result->expr.len < 3) {
        return result;
    }
    return atom_error(a, surface_atom, result->expr.elems[2]);
}

static bool emit_generic_mork_handle_native_surface(
    Space *s, Arena *a, Atom *surface_atom, Atom **args, uint32_t nargs,
    int fuel, SymbolId explicit_head_id, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    if (!generic_mork_handle_sugar_allowed(s, a, args[0], fuel)) {
        return false;
    }
    Atom **resolved_args = arena_alloc(a, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        resolved_args[i] = resolve_registry_refs(a, args[i]);
    }
    Atom *result = dispatch_named_native(s, a, explicit_head_id, resolved_args, nargs);
    if (!result) {
        return false;
    }
    outcome_set_add(os, rewrite_error_call(a, surface_atom, result), &empty);
    return true;
}

static bool emit_generic_mork_handle_atoms_surface(
    Space *s, Arena *a, Atom *surface_atom, Atom *space_arg, int fuel,
    OutcomeSet *os) {
    Atom *args[] = { space_arg };
    if (!generic_mork_handle_sugar_allowed(s, a, space_arg, fuel)) {
        return false;
    }
    return emit_unquoted_mork_rows(s, a, g_builtin_syms.lib_mork_space_atoms,
                                   surface_atom, 1, args, false, fuel, os);
}

static bool emit_generic_mork_handle_match_surface(
    Space *s, Arena *a, Atom *surface_atom, Atom **args, int fuel,
    OutcomeSet *os) {
    if (!generic_mork_handle_sugar_allowed(s, a, args[0], fuel)) {
        return false;
    }
    if (emit_direct_mork_match_rows(s, a, surface_atom, args, fuel, os)) {
        return true;
    }
    return emit_unquoted_mork_rows(s, a, g_builtin_syms.lib_mork_space_match,
                                   surface_atom, 3, args, true, fuel, os);
}

static Atom *guard_mork_space_surface(Arena *a, Atom *call, Space *space,
                                      const char *surface,
                                      const char *explicit_surface) {
    if (!space_requires_explicit_mork_namespace(space)) {
        return NULL;
    }
    return mork_space_surface_error(a, call, surface, explicit_surface);
}

static Atom *guard_mork_handle_surface(Space *s, Arena *a, Atom *call,
                                       Atom *space_expr, int fuel,
                                       const char *surface,
                                       const char *explicit_surface) {
    if (!g_registry) return NULL;

    if (atom_resolves_to_mork_handle(s, a, space_expr, fuel)) {
        return mork_handle_surface_error(a, call, surface, explicit_surface);
    }
    return NULL;
}

static CettaTableMode active_search_table_mode(void) {
    const CettaEvalOptionEntry *table_mode = active_eval_option("search-table-mode");
    if (table_mode && table_mode->kind == CETTA_EVAL_OPTION_VALUE_SYMBOL) {
        if (strcmp(table_mode->repr, "variant") == 0) {
            return CETTA_TABLE_MODE_VARIANT;
        }
    }
    return CETTA_TABLE_MODE_NONE;
}

static AnswerBank *eval_active_episode_answer_bank(void) {
    if (!g_episode_answer_bank_ready) {
        answer_bank_init(&g_episode_answer_bank);
        g_episode_answer_bank_ready = true;
    }
    return &g_episode_answer_bank;
}

static void eval_release_episode_answer_bank(void) {
    if (!g_episode_answer_bank_ready)
        return;
    answer_bank_free(&g_episode_answer_bank);
    g_episode_answer_bank_ready = false;
}

static TableStore *eval_active_episode_table(void) {
    CettaTableMode mode = active_search_table_mode();
    if (mode == CETTA_TABLE_MODE_NONE) {
        if (g_episode_table_ready) {
            table_store_free(&g_episode_table);
            g_episode_table_ready = false;
        }
        eval_release_episode_answer_bank();
        return NULL;
    }
    if (!g_episode_table_ready) {
        table_store_init(&g_episode_table, mode, eval_active_episode_answer_bank());
        g_episode_table_ready = true;
    } else if (g_episode_table.mode != mode) {
        table_store_free(&g_episode_table);
        eval_release_episode_answer_bank();
        table_store_init(&g_episode_table, mode, eval_active_episode_answer_bank());
    }
    return &g_episode_table;
}

static void eval_release_episode_table(void) {
    if (!g_episode_table_ready)
        return;
    table_store_free(&g_episode_table);
    g_episode_table_ready = false;
    eval_release_episode_answer_bank();
}

static bool outcome_variant_sharing_enabled(void) {
    const CettaEvalOptionEntry *option = active_eval_option("outcome-variant-sharing");
    if (option) {
        if (option->kind == CETTA_EVAL_OPTION_VALUE_INT)
            return option->int_value != 0;
        return strcmp(option->repr, "off") != 0 &&
               strcmp(option->repr, "false") != 0 &&
               strcmp(option->repr, "0") != 0;
    }
    /* High-confidence auto lane: variant-table mode already commits to
       canonicalized variant reuse at explicit memo boundaries. */
    return active_search_table_mode() == CETTA_TABLE_MODE_VARIANT;
}

static VariantBank *eval_active_outcome_variant_bank(void) {
    static const CettaVariantShapeOptions kOutcomeVariantOptions = {
        .slot_policy = CETTA_VARIANT_SLOT_FIXED_SPELLING,
        .slot_name = "$_slot",
        .share_immutable = true,
    };
    if (!outcome_variant_sharing_enabled()) {
        if (g_episode_outcome_variant_bank_ready) {
            variant_bank_free(&g_episode_outcome_variant_bank);
            g_episode_outcome_variant_bank_ready = false;
        }
        return NULL;
    }
    if (!g_episode_outcome_variant_bank_ready) {
        variant_bank_init(&g_episode_outcome_variant_bank, kOutcomeVariantOptions);
        g_episode_outcome_variant_bank_ready = true;
    }
    return &g_episode_outcome_variant_bank;
}

static void eval_release_outcome_variant_bank(void) {
    if (!g_episode_outcome_variant_bank_ready)
        return;
    variant_bank_free(&g_episode_outcome_variant_bank);
    g_episode_outcome_variant_bank_ready = false;
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

static bool atom_is_same_var(Atom *lhs, Atom *rhs) {
    return lhs && rhs && lhs->kind == ATOM_VAR && rhs->kind == ATOM_VAR &&
           lhs->var_id == rhs->var_id && lhs->sym_id == rhs->sym_id;
}

static bool is_true_atom(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.true_text) ||
           (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL && a->ground.bval);
}

static bool is_false_atom(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.false_text) ||
           (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL && !a->ground.bval);
}

static bool atom_is_bool_literal(Atom *a, bool *value_out) {
    if (is_true_atom(a)) {
        if (value_out) *value_out = true;
        return true;
    }
    if (is_false_atom(a)) {
        if (value_out) *value_out = false;
        return true;
    }
    if (language_is_petta() && atom_is_symbol_id(a, g_builtin_syms.petta_true_text)) {
        if (value_out) *value_out = true;
        return true;
    }
    if (language_is_petta() && atom_is_symbol_id(a, g_builtin_syms.petta_false_text)) {
        if (value_out) *value_out = false;
        return true;
    }
    return false;
}

static bool atom_petta_surface_eq(Atom *lhs, Atom *rhs) {
    bool lhs_bool = false;
    bool rhs_bool = false;
    if (atom_is_bool_literal(lhs, &lhs_bool) &&
        atom_is_bool_literal(rhs, &rhs_bool)) {
        return lhs_bool == rhs_bool;
    }
    if (lhs && lhs->kind == ATOM_EXPR && lhs->expr.len == 2 &&
        atom_is_symbol_id(lhs->expr.elems[0], g_builtin_syms.quote)) {
        return atom_petta_surface_eq(lhs->expr.elems[1], rhs);
    }
    if (rhs && rhs->kind == ATOM_EXPR && rhs->expr.len == 2 &&
        atom_is_symbol_id(rhs->expr.elems[0], g_builtin_syms.quote)) {
        return atom_petta_surface_eq(lhs, rhs->expr.elems[1]);
    }
    if (!lhs || !rhs || lhs->kind != rhs->kind)
        return false;
    if (lhs->kind == ATOM_EXPR) {
        if (lhs->expr.len != rhs->expr.len)
            return false;
        for (uint32_t i = 0; i < lhs->expr.len; i++) {
            if (!atom_petta_surface_eq(lhs->expr.elems[i], rhs->expr.elems[i]))
                return false;
        }
        return true;
    }
    return atom_eq(lhs, rhs);
}

static bool atom_assert_eq(Atom *lhs, Atom *rhs) {
    if (language_is_petta())
        return atom_petta_surface_eq(lhs, rhs);
    return atom_eq(lhs, rhs);
}

static bool atom_is_unit_literal(Atom *a) {
    return a && a->kind == ATOM_EXPR && a->expr.len == 0;
}

static bool atom_get_int_literal(Atom *a, int64_t *value_out) {
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT)
        return false;
    if (value_out)
        *value_out = a->ground.ival;
    return true;
}

static bool int64_add_checked(int64_t lhs, int64_t rhs, int64_t *out) {
    __int128 sum = (__int128)lhs + (__int128)rhs;
    if (sum < INT64_MIN || sum > INT64_MAX)
        return false;
    if (out)
        *out = (int64_t)sum;
    return true;
}

static bool int64_sub_checked(int64_t lhs, int64_t rhs, int64_t *out) {
    __int128 diff = (__int128)lhs - (__int128)rhs;
    if (diff < INT64_MIN || diff > INT64_MAX)
        return false;
    if (out)
        *out = (int64_t)diff;
    return true;
}

static bool atom_is_registry_token(Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL &&
           symbol_bytes(g_symbols, atom->sym_id)[0] == '&';
}

static Atom *registry_lookup_atom(Atom *atom) {
    if (!g_registry || !atom_is_registry_token(atom)) return NULL;
    return registry_lookup_id(g_registry, atom->sym_id);
}

static TermUniverse *eval_current_term_universe(void) {
    if (g_eval_root_space && g_eval_root_space->native.universe)
        return g_eval_root_space->native.universe;
    if (g_library_context && g_library_context->term_universe.persistent_arena)
        return &g_library_context->term_universe;
    if (g_eval_fallback_universe.persistent_arena)
        return &g_eval_fallback_universe;
    return NULL;
}

static Arena *eval_persistent_arena(void) {
    TermUniverse *universe = eval_current_term_universe();
    return universe ? universe->persistent_arena : NULL;
}

static Arena *eval_storage_arena(Arena *fallback) {
    Arena *persistent = eval_persistent_arena();
    return persistent ? persistent : fallback;
}

static bool eval_storage_is_persistent(const Arena *arena) {
    Arena *persistent = eval_persistent_arena();
    return persistent && persistent == arena;
}

static Arena *eval_active_episode_survivor_arena(void) {
    if (!g_episode_survivor_arena_ready) {
        arena_init(&g_episode_survivor_arena);
        arena_set_hashcons(&g_episode_survivor_arena, NULL);
        arena_set_runtime_kind(&g_episode_survivor_arena,
                               CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
        g_episode_survivor_arena_ready = true;
    }
    return &g_episode_survivor_arena;
}

static void eval_release_retained_tail_arenas(void) {
    EvalRetainedTailArena *node = g_retained_tail_arenas;
    while (node) {
        EvalRetainedTailArena *next = node->next;
        arena_free(&node->arena);
        free(node);
        node = next;
    }
    g_retained_tail_arenas = NULL;
}

static void eval_release_episode_survivor_arena(void) {
    eval_release_retained_tail_arenas();
    if (!g_episode_survivor_arena_ready)
        return;
    arena_free(&g_episode_survivor_arena);
    g_episode_survivor_arena_ready = false;
}

static void eval_local_tail_survivor_init(EvalLocalTailSurvivor *survivor) {
    if (!survivor)
        return;
    memset(survivor, 0, sizeof(*survivor));
    survivor->active_slot = -1;
    survivor->pending_slot = -1;
}

static void eval_retain_local_tail_arena(Arena *arena);

static void eval_tail_note_reclaimed(size_t bytes) {
    if (bytes > 0) {
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_RECLAIMED_BYTES,
            (uint64_t)bytes);
    }
}

static bool arena_ptr_in_reclaimable_region(
    const Arena *arena,
    ArenaMark mark,
    const void *ptr
) {
    if (!arena || !ptr)
        return false;
    const char *p = (const char *)ptr;
    for (ArenaBlock *block = arena->head; block; block = block->next) {
        const char *start = block->data;
        const char *end = block->data + block->used;
        if (p >= start && p < end) {
            if (block != mark.head)
                return true;
            return (size_t)(p - start) >= mark.used;
        }
        if (block == mark.head)
            break;
    }
    return false;
}

static bool atom_references_reclaimable_region(
    const Arena *arena,
    ArenaMark mark,
    Atom *atom
) {
    if (!atom)
        return false;
    if (arena_ptr_in_reclaimable_region(arena, mark, atom))
        return true;
    if (atom->kind == ATOM_EXPR) {
        if (arena_ptr_in_reclaimable_region(arena, mark, atom->expr.elems))
            return true;
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (atom_references_reclaimable_region(
                    arena, mark, atom->expr.elems[i]))
                return true;
        }
    } else if (atom->kind == ATOM_GROUNDED &&
               atom->ground.gkind == GV_STRING) {
        return arena_ptr_in_reclaimable_region(
            arena, mark, atom->ground.sval);
    }
    return false;
}

static void eval_tail_reset_to_mark(Arena *eval_arena, ArenaMark mark) {
    size_t before_reset;
    if (!eval_arena)
        return;
    before_reset = eval_arena->live_bytes;
    arena_reset(eval_arena, mark);
    if (before_reset > eval_arena->live_bytes)
        eval_tail_note_reclaimed(before_reset - eval_arena->live_bytes);
}

static void eval_local_tail_survivor_release_slot(
    EvalLocalTailSurvivor *survivor,
    uint32_t slot,
    bool count_reclaimed
) {
    if (!survivor || slot >= 2 || !survivor->ready[slot])
        return;
    if (count_reclaimed)
        eval_tail_note_reclaimed(survivor->slots[slot].live_bytes);
    arena_free(&survivor->slots[slot]);
    survivor->ready[slot] = false;
    if (survivor->active_slot == (int)slot)
        survivor->active_slot = -1;
    if (survivor->pending_slot == (int)slot)
        survivor->pending_slot = -1;
}

static Arena *eval_local_tail_survivor_prepare(
    EvalLocalTailSurvivor *survivor,
    uint32_t *slot_out
) {
    uint32_t slot;
    if (!survivor)
        return eval_active_episode_survivor_arena();
    slot = survivor->active_slot == 0 ? 1u : 0u;
    if (survivor->ready[slot]) {
        eval_retain_local_tail_arena(&survivor->slots[slot]);
        survivor->ready[slot] = false;
        if (survivor->pending_slot == (int)slot)
            survivor->pending_slot = -1;
    }
    arena_init(&survivor->slots[slot]);
    arena_set_hashcons(&survivor->slots[slot], NULL);
    arena_set_runtime_kind(&survivor->slots[slot],
                           CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
    survivor->ready[slot] = true;
    survivor->pending_slot = (int)slot;
    if (slot_out)
        *slot_out = slot;
    return &survivor->slots[slot];
}

static void eval_local_tail_survivor_commit(
    EvalLocalTailSurvivor *survivor,
    uint32_t slot
) {
    int old_slot;
    if (!survivor || slot >= 2 || !survivor->ready[slot])
        return;
    old_slot = survivor->active_slot;
    if (old_slot >= 0 && old_slot != (int)slot) {
        eval_local_tail_survivor_release_slot(survivor, (uint32_t)old_slot,
                                              true);
    }
    survivor->active_slot = (int)slot;
    if (survivor->pending_slot == (int)slot)
        survivor->pending_slot = -1;
}

static void eval_local_tail_survivor_abort(
    EvalLocalTailSurvivor *survivor,
    uint32_t slot
) {
    if (!survivor || slot >= 2 || survivor->active_slot == (int)slot)
        return;
    eval_local_tail_survivor_release_slot(survivor, slot, true);
}

static bool eval_local_tail_survivor_has_pending(
    EvalLocalTailSurvivor *survivor
) {
    return survivor &&
           survivor->pending_slot >= 0 &&
           survivor->pending_slot < 2 &&
           survivor->ready[survivor->pending_slot];
}

static void eval_local_tail_survivor_abort_pending(
    EvalLocalTailSurvivor *survivor
) {
    if (!eval_local_tail_survivor_has_pending(survivor))
        return;
    eval_local_tail_survivor_release_slot(
        survivor, (uint32_t)survivor->pending_slot, true);
}

static bool eval_local_tail_survivor_commit_pending_reentry(
    EvalLocalTailSurvivor *survivor,
    Arena *eval_arena,
    ArenaMark eval_mark
) {
    int slot;
    size_t before_reset;

    if (!eval_local_tail_survivor_has_pending(survivor) || !eval_arena)
        return false;
    slot = survivor->pending_slot;
    eval_local_tail_survivor_commit(survivor, (uint32_t)slot);

    before_reset = eval_arena->live_bytes;
    arena_reset(eval_arena, eval_mark);
    if (before_reset > eval_arena->live_bytes)
        eval_tail_note_reclaimed(before_reset - eval_arena->live_bytes);
    return true;
}

static bool eval_local_tail_survivor_promote_reentry(
    EvalLocalTailSurvivor *survivor,
    Arena *eval_arena,
    ArenaMark eval_mark,
    Atom **next_atom,
    Atom **next_type
) {
    uint32_t slot = 0;
    Arena *dst;
    Atom *promoted_atom;
    Atom *promoted_type = NULL;
    size_t before_reset;

    if (!survivor || !eval_arena || !next_atom || !*next_atom)
        return false;

    dst = eval_local_tail_survivor_prepare(survivor, &slot);
    if (!dst)
        return false;

    promoted_atom = atom_deep_copy(dst, *next_atom);
    if (!promoted_atom) {
        eval_local_tail_survivor_abort(survivor, slot);
        return false;
    }
    if (next_type && *next_type) {
        promoted_type = atom_deep_copy(dst, *next_type);
        if (!promoted_type) {
            eval_local_tail_survivor_abort(survivor, slot);
            return false;
        }
    }

    *next_atom = promoted_atom;
    if (next_type && *next_type)
        *next_type = promoted_type;
    eval_local_tail_survivor_commit(survivor, slot);

    before_reset = eval_arena->live_bytes;
    arena_reset(eval_arena, eval_mark);
    if (before_reset > eval_arena->live_bytes)
        eval_tail_note_reclaimed(before_reset - eval_arena->live_bytes);
    return true;
}

static void eval_retain_local_tail_arena(Arena *arena) {
    if (!arena || !arena->head)
        return;
    EvalRetainedTailArena *node = cetta_malloc(sizeof(EvalRetainedTailArena));
    node->arena = *arena;
    node->next = g_retained_tail_arenas;
    g_retained_tail_arenas = node;
    memset(arena, 0, sizeof(*arena));
}

static void eval_local_tail_survivor_cleanup(
    EvalLocalTailSurvivor *survivor
) {
    if (!survivor)
        return;
    for (uint32_t i = 0; i < 2; i++) {
        if (!survivor->ready[i])
            continue;
        if (survivor->active_slot == (int)i ||
            survivor->pending_slot == (int)i) {
            eval_retain_local_tail_arena(&survivor->slots[i]);
        } else {
            eval_local_tail_survivor_release_slot(survivor, i, true);
        }
        survivor->ready[i] = false;
    }
    survivor->active_slot = -1;
    survivor->pending_slot = -1;
}

static void eval_query_episode_init(EvalQueryEpisode *episode) {
    if (!episode)
        return;
    arena_init(&episode->scratch);
    arena_set_hashcons(&episode->scratch, NULL);
    arena_set_runtime_kind(&episode->scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_init(&episode->generated);
    arena_set_hashcons(&episode->generated, NULL);
    arena_set_runtime_kind(&episode->generated, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    episode->survivor_arena = eval_active_episode_survivor_arena();
    episode->active = true;
}

static void eval_query_episode_free(EvalQueryEpisode *episode) {
    if (!episode || !episode->active)
        return;
    arena_free(&episode->generated);
    arena_free(&episode->scratch);
    episode->survivor_arena = NULL;
    episode->active = false;
}

static void eval_query_episode_cleanup(EvalQueryEpisode *episode) {
    eval_query_episode_free(episode);
}

static Arena *eval_query_episode_scratch(EvalQueryEpisode *episode) {
    if (!episode || !episode->active)
        return NULL;
    return &episode->scratch;
}

static Arena *eval_query_episode_generated(EvalQueryEpisode *episode) {
    if (!episode || !episode->active)
        return NULL;
    return &episode->generated;
}

static Arena *eval_query_episode_result_arena(EvalQueryEpisode *episode,
                                              Arena *fallback) {
    if (!episode || !episode->active || !episode->survivor_arena)
        return fallback;
    return episode->survivor_arena;
}

static Atom *eval_query_episode_promote_atom(EvalQueryEpisode *episode,
                                             Atom *atom) {
    Arena *dst = eval_query_episode_result_arena(episode, NULL);
    if (!dst || !atom)
        return atom;
    return atom_deep_copy(dst, atom);
}

static size_t eval_query_episode_survivor_live_bytes(EvalQueryEpisode *episode) {
    Arena *survivor = eval_query_episode_result_arena(episode, NULL);
    return survivor ? survivor->live_bytes : 0;
}

static void eval_query_episode_note_answer_promotion(Arena *arena,
                                                     size_t before_bytes) {
    size_t after_bytes = arena ? arena->live_bytes : before_bytes;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_QUERY_EPISODE_PROMOTED_ANSWER_COUNT);
    if (after_bytes > before_bytes) {
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_QUERY_EPISODE_PROMOTED_ANSWER_BYTES,
            (uint64_t)(after_bytes - before_bytes));
    }
}

static bool bindings_promote_atoms_to_arena(Arena *dst, Bindings *bindings) {
    if (!dst || !bindings)
        return true;
    for (uint32_t i = 0; i < bindings->len; i++) {
        Atom *promoted = atom_deep_copy(dst, bindings->entries[i].val);
        if (bindings->entries[i].val && !promoted)
            return false;
        bindings->entries[i].val = promoted;
    }
    for (uint32_t i = 0; i < bindings->eq_len; i++) {
        Atom *lhs = atom_deep_copy(dst, bindings->constraints[i].lhs);
        Atom *rhs = atom_deep_copy(dst, bindings->constraints[i].rhs);
        if ((bindings->constraints[i].lhs && !lhs) ||
            (bindings->constraints[i].rhs && !rhs)) {
            return false;
        }
        bindings->constraints[i].lhs = lhs;
        bindings->constraints[i].rhs = rhs;
    }
    bindings->lookup_cache_count = 0;
    bindings->lookup_cache_next = 0;
    return true;
}

static bool eval_query_episode_promote_bindings(EvalQueryEpisode *episode,
                                                Bindings *bindings) {
    if (!episode || !episode->active || !episode->survivor_arena || !bindings)
        return true;
    return bindings_promote_atoms_to_arena(episode->survivor_arena, bindings);
}

static Atom *eval_store_atom(Arena *dst, Atom *src) {
    if (!eval_storage_is_persistent(dst))
        return atom_deep_copy(dst, src);
    return term_universe_store_atom(eval_current_term_universe(), dst, src);
}

static Atom *space_compare_atom(const Space *space, Arena *dst, Atom *src) {
    if (space && space->native.universe &&
        space->native.universe->persistent_arena)
        return term_universe_canonicalize_atom(dst, src);
    return atom_deep_copy(dst, src);
}

static bool petta_internal_head_is_name(Atom *atom, const char *name) {
    const char *atom_name = NULL;
    if (!atom || atom->kind != ATOM_SYMBOL || !name)
        return false;
    atom_name = atom_name_cstr(atom);
    return atom_name && strcmp(atom_name, name) == 0;
}

static bool petta_partial_value_unpack(Atom *atom, Atom **out_head,
                                       Atom ***out_bound_args,
                                       uint32_t *out_bound_len) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3 ||
        !petta_internal_head_is_name(atom->expr.elems[0],
                                     cetta_petta_partial_head_name()))
        return false;
    Atom *bound = atom->expr.elems[2];
    if (!bound || bound->kind != ATOM_EXPR)
        return false;
    if (out_head)
        *out_head = atom->expr.elems[1];
    if (out_bound_args)
        *out_bound_args = bound->expr.elems;
    if (out_bound_len)
        *out_bound_len = bound->expr.len;
    return true;
}

static Atom *petta_make_partial_value(Arena *a, Atom *head,
                                      Atom **bound_args,
                                      uint32_t bound_len) {
    Atom **bound = bound_len > 0
        ? arena_alloc(a, sizeof(Atom *) * bound_len)
        : NULL;
    for (uint32_t i = 0; i < bound_len; i++)
        bound[i] = bound_args[i];
    return atom_expr3(a,
        atom_symbol(a, cetta_petta_partial_head_name()),
        head,
        atom_expr(a, bound, bound_len));
}

static Atom *petta_make_partial_from_call(Arena *a, Atom *call_atom) {
    if (!call_atom || call_atom->kind != ATOM_EXPR || call_atom->expr.len == 0)
        return call_atom;
    return petta_make_partial_value(a, call_atom->expr.elems[0],
                                    call_atom->expr.elems + 1,
                                    call_atom->expr.len - 1);
}

static bool petta_atom_contains_private_variant_var_eval(const Atom *atom) {
    if (!atom)
        return false;
    switch (atom->kind) {
    case ATOM_VAR:
        return variant_private_var_id(atom->var_id);
    case ATOM_EXPR:
        for (uint32_t i = 0; i < atom->expr.len; i++) {
            if (petta_atom_contains_private_variant_var_eval(atom->expr.elems[i]))
                return true;
        }
        return false;
    default:
        return false;
    }
}

static Atom *petta_lambda_env_pack(Arena *a, const Bindings *b) {
    uint32_t assign_len = 0;
    uint32_t equality_len = 0;
    if (b) {
        for (uint32_t i = 0; i < b->len; i++) {
            if (variant_private_var_id(b->entries[i].var_id) ||
                petta_atom_contains_private_variant_var_eval(b->entries[i].val))
                continue;
            assign_len++;
        }
        for (uint32_t i = 0; i < b->eq_len; i++) {
            if (petta_atom_contains_private_variant_var_eval(b->constraints[i].lhs) ||
                petta_atom_contains_private_variant_var_eval(b->constraints[i].rhs))
                continue;
            equality_len++;
        }
    }
    Atom **assigns = assign_len > 0 ? arena_alloc(a, sizeof(Atom *) * assign_len) : NULL;
    Atom **equalities = equality_len > 0 ? arena_alloc(a, sizeof(Atom *) * equality_len) : NULL;
    uint32_t ai = 0;
    uint32_t ei = 0;
    if (b) {
        for (uint32_t i = 0; i < b->len; i++) {
            if (variant_private_var_id(b->entries[i].var_id) ||
                petta_atom_contains_private_variant_var_eval(b->entries[i].val))
                continue;
            Atom *var_atom = atom_var_with_spelling(a, b->entries[i].spelling,
                                                    b->entries[i].var_id);
            Atom *entry_elems[] = {
                var_atom,
                b->entries[i].val,
                atom_bool(a, b->entries[i].legacy_name_fallback),
            };
            assigns[ai++] = atom_expr(a, entry_elems, 3);
        }
        for (uint32_t i = 0; i < b->eq_len; i++) {
            if (petta_atom_contains_private_variant_var_eval(b->constraints[i].lhs) ||
                petta_atom_contains_private_variant_var_eval(b->constraints[i].rhs))
                continue;
            equalities[ei++] = atom_expr2(a, b->constraints[i].lhs,
                                          b->constraints[i].rhs);
        }
    }
    return atom_expr3(a,
        atom_symbol(a, cetta_petta_lambda_env_head_name()),
        atom_expr(a, assigns, assign_len),
        atom_expr(a, equalities, equality_len));
}

static bool petta_lambda_env_unpack(Atom *atom, Bindings *out) {
    bindings_init(out);
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3 ||
        !petta_internal_head_is_name(atom->expr.elems[0],
                                     cetta_petta_lambda_env_head_name())) {
        return false;
    }
    Atom *assigns = atom->expr.elems[1];
    Atom *equalities = atom->expr.elems[2];
    if (!assigns || assigns->kind != ATOM_EXPR ||
        !equalities || equalities->kind != ATOM_EXPR) {
        return false;
    }
    for (uint32_t i = 0; i < assigns->expr.len; i++) {
        Atom *assign = assigns->expr.elems[i];
        if (!assign || assign->kind != ATOM_EXPR || assign->expr.len != 3 ||
            !assign->expr.elems[0] || assign->expr.elems[0]->kind != ATOM_VAR) {
            bindings_free(out);
            return false;
        }
        Atom *var_atom = assign->expr.elems[0];
        Atom *value_atom = assign->expr.elems[1];
        Atom *fallback_atom = assign->expr.elems[2];
        bool legacy = false;
        if (fallback_atom && fallback_atom->kind == ATOM_GROUNDED &&
            fallback_atom->ground.gkind == GV_BOOL) {
            legacy = fallback_atom->ground.bval;
        }
        if (legacy) {
            if (!bindings_add_id_name_fallback(out, var_atom->var_id,
                                               var_atom->sym_id, value_atom)) {
                bindings_free(out);
                return false;
            }
        } else {
            if (!bindings_add_id(out, var_atom->var_id,
                                 var_atom->sym_id, value_atom)) {
                bindings_free(out);
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < equalities->expr.len; i++) {
        Atom *pair = equalities->expr.elems[i];
        if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2) {
            bindings_free(out);
            return false;
        }
        if (!bindings_add_constraint(out, pair->expr.elems[0], pair->expr.elems[1])) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

typedef struct {
    VarId old_id;
    Atom *new_var;
} PettaLambdaParamMapEntry;

typedef struct {
    PettaLambdaParamMapEntry *items;
    uint32_t len;
} PettaLambdaParamMap;

static Atom *petta_lambda_rewrite_param_var(Arena *a, Atom *var, void *ctx) {
    (void)a;
    if (!var || var->kind != ATOM_VAR || !ctx)
        return var;
    PettaLambdaParamMap *map = (PettaLambdaParamMap *)ctx;
    for (uint32_t i = 0; i < map->len; i++) {
        if (map->items[i].old_id == var->var_id)
            return map->items[i].new_var;
    }
    return var;
}

static bool petta_lambda_prepare_params_and_body(Arena *a,
                                                 Atom *params_src,
                                                 Atom *body_src,
                                                 Atom **out_params,
                                                 Atom **out_body) {
    if (!out_params || !out_body)
        return false;
    *out_params = params_src;
    *out_body = body_src;
    if (!params_src || params_src->kind != ATOM_EXPR)
        return false;
    uint32_t param_count = params_src->expr.len;
    if (param_count == 0) {
        *out_params = params_src;
        *out_body = body_src;
        return true;
    }
    PettaLambdaParamMapEntry *map_items =
        arena_alloc(a, sizeof(PettaLambdaParamMapEntry) * param_count);
    Atom **fresh_params = arena_alloc(a, sizeof(Atom *) * param_count);
    for (uint32_t i = 0; i < param_count; i++) {
        Atom *param = params_src->expr.elems[i];
        if (!param || param->kind != ATOM_VAR)
            return false;
        Atom *fresh = atom_var_with_spelling(a, param->sym_id, fresh_var_id());
        map_items[i].old_id = param->var_id;
        map_items[i].new_var = fresh;
        fresh_params[i] = fresh;
    }
    Bindings empty;
    bindings_init(&empty);
    PettaLambdaParamMap map = { map_items, param_count };
    Atom *rewritten_body =
        bindings_apply_rewrite_vars(&empty, a, body_src,
                                    petta_lambda_rewrite_param_var, &map);
    bindings_free(&empty);
    if (!rewritten_body)
        return false;
    *out_params = atom_expr(a, fresh_params, param_count);
    *out_body = rewritten_body;
    return true;
}


static bool dispatch_petta_lambda_make_outcomes(Space *s, Arena *a, Atom *atom,
                                                const Bindings *prefix,
                                                OutcomeSet *os) {
    (void)s;
    Bindings _empty; bindings_init(&_empty);
    if (!language_is_petta() || atom->kind != ATOM_EXPR || atom->expr.len != 3 ||
        !petta_internal_head_is_name(atom->expr.elems[0], "|->"))
        return false;
    Atom *params_src = atom->expr.elems[1];
    Atom *body_src = atom->expr.elems[2];
    Atom *params = NULL;
    Atom *body = NULL;
    if (!petta_lambda_prepare_params_and_body(a, params_src, body_src,
                                              &params, &body)) {
        outcome_set_add(os,
            atom_error(a, atom,
                       atom_symbol(a, "PettaLambdaInvalidParameters")),
            prefix ? prefix : &_empty);
        return true;
    }
    Atom *env_pack = petta_lambda_env_pack(a, prefix);
    Atom *closure_args[] = {
        params,
        body,
        env_pack,
    };
    outcome_set_add(os,
        make_call_expr(a, atom_symbol(a, cetta_petta_lambda_closure_head_name()),
                       closure_args, 3),
        prefix ? prefix : &_empty);
    bindings_free(&_empty);
    return true;
}

static bool dispatch_petta_lambda_closure_outcomes(Space *s, Arena *a,
                                                   Atom *atom, int fuel,
                                                   const Bindings *prefix,
                                                   bool preserve_bindings,
                                                   OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    if (!language_is_petta() || atom->kind != ATOM_EXPR || atom->expr.len < 4 ||
        !petta_internal_head_is_name(atom->expr.elems[0],
                                     cetta_petta_lambda_closure_head_name()))
        return false;

    Atom *params = atom->expr.elems[1];
    Atom *body = atom->expr.elems[2];
    Atom *env_pack = atom->expr.elems[3];
    if (!params || params->kind != ATOM_EXPR) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "PettaLambdaCorruptClosure")),
            prefix ? prefix : &_empty);
        return true;
    }

    uint32_t param_count = params->expr.len;
    uint32_t supplied_count = atom->expr.len - 4;
    if (supplied_count == 0) {
        outcome_set_add(os, atom, prefix ? prefix : &_empty);
        return true;
    }

    Bindings call_env;
    if (!petta_lambda_env_unpack(env_pack, &call_env)) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "PettaLambdaCorruptEnv")),
            prefix ? prefix : &_empty);
        return true;
    }

    uint32_t apply_count = supplied_count < param_count ? supplied_count : param_count;
    Atom *applied_body = body;
    for (uint32_t i = 0; i < apply_count; i++) {
        Atom *param = params->expr.elems[i];
        if (!param || param->kind != ATOM_VAR) {
            bindings_free(&call_env);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "PettaLambdaApplyFailed")),
                prefix ? prefix : &_empty);
            return true;
        }
    }
    if (apply_count > 0) {
        PettaLambdaParamMapEntry *apply_items =
            arena_alloc(a, sizeof(PettaLambdaParamMapEntry) * apply_count);
        for (uint32_t i = 0; i < apply_count; i++) {
            Atom *param = params->expr.elems[i];
            apply_items[i].old_id = param->var_id;
            apply_items[i].new_var = atom->expr.elems[4 + i];
        }
        Bindings empty_rewrite;
        bindings_init(&empty_rewrite);
        PettaLambdaParamMap apply_map = { apply_items, apply_count };
        applied_body =
            bindings_apply_rewrite_vars(&empty_rewrite, a, body,
                                        petta_lambda_rewrite_param_var,
                                        &apply_map);
        bindings_free(&empty_rewrite);
        if (!applied_body) {
            bindings_free(&call_env);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "PettaLambdaApplyFailed")),
                prefix ? prefix : &_empty);
            return true;
        }
    }

    if (apply_count < param_count) {
        uint32_t remaining = param_count - apply_count;
        Atom **remaining_params = remaining > 0
            ? arena_alloc(a, sizeof(Atom *) * remaining)
            : NULL;
        for (uint32_t i = 0; i < remaining; i++)
            remaining_params[i] = params->expr.elems[apply_count + i];
        Atom *new_env = petta_lambda_env_pack(a, &call_env);
        Atom *closure_args[] = {
            atom_expr(a, remaining_params, remaining),
            applied_body,
            new_env,
        };
        outcome_set_add(os,
            make_call_expr(a, atom_symbol(a, cetta_petta_lambda_closure_head_name()),
                           closure_args, 3),
            prefix ? prefix : &_empty);
        bindings_free(&call_env);
        return true;
    }

    Atom *bound_body = bindings_apply_if_vars(&call_env, a, applied_body);
    ResultSet inner;
    result_set_init(&inner);
    metta_eval(s, a, NULL, bound_body, fuel, &inner);
    bindings_free(&call_env);

    uint32_t extra_count = supplied_count - apply_count;
    bool had_value = false;
    bool had_error = false;
    for (uint32_t i = 0; i < inner.len; i++) {
        Atom *result_atom = inner.items[i];
        if (atom_is_empty(result_atom))
            continue;
        if (atom_is_error(result_atom))
            had_error = true;
        else
            had_value = true;
        if (extra_count > 0) {
            Atom **extra_args = arena_alloc(a, sizeof(Atom *) * extra_count);
            for (uint32_t j = 0; j < extra_count; j++)
                extra_args[j] = atom->expr.elems[4 + apply_count + j];
            Atom *next_call = make_call_expr(a, result_atom, extra_args, extra_count);
            eval_for_caller(s, a, NULL, next_call, fuel, prefix ? prefix : &_empty,
                            preserve_bindings, os);
        } else {
            outcome_set_add(os, result_atom, prefix ? prefix : &_empty);
        }
    }
    result_set_free(&inner);
    if (extra_count == 0 && !had_value && !had_error &&
        petta_eval_open_callable_relation_values(s, a, bound_body, fuel,
                                                 prefix ? prefix : &_empty,
                                                 preserve_bindings, os)) {
        bindings_free(&_empty);
        return true;
    }
    if (!had_value && !had_error)
        outcome_set_add(os, atom_empty(a), prefix ? prefix : &_empty);
    bindings_free(&_empty);
    return true;
}

static Atom *petta_callable_repr_surface_atom(Arena *a, Atom *value) {
    if (!language_is_petta() || !value || value->kind != ATOM_EXPR ||
        value->expr.len != 4 ||
        !petta_internal_head_is_name(value->expr.elems[0],
                                     cetta_petta_lambda_closure_head_name())) {
        return value;
    }

    Atom *params = value->expr.elems[1];
    Atom *body = value->expr.elems[2];
    Atom *env_pack = value->expr.elems[3];
    Atom *surface_body = body;
    Bindings closure_env;
    if (petta_lambda_env_unpack(env_pack, &closure_env)) {
        surface_body = bindings_apply_if_vars(&closure_env, a, body);
        bindings_free(&closure_env);
    }

    Atom *lambda_args[] = { params, surface_body };
    return make_call_expr(a, atom_symbol(a, "|->"), lambda_args, 2);
}

/* ── Outcome set (unified result type: atom + bindings) ─────────────────── */

void outcome_set_init_with_owner(OutcomeSet *os, Arena *owner) {
    os->items = NULL;
    os->len = 0;
    os->cap = 0;
    os->payload_owner = owner;
}

void outcome_set_init(OutcomeSet *os) {
    outcome_set_init_with_owner(os, NULL);
}

void outcome_set_set_owner(OutcomeSet *os, Arena *owner) {
    if (!os)
        return;
    os->payload_owner = owner;
}

static Arena *outcome_set_payload_arena(const OutcomeSet *os, Arena *fallback) {
    if (!os || !os->payload_owner)
        return fallback;
    return os->payload_owner;
}

static void outcome_set_bind_owner_if_missing(OutcomeSet *os, Arena *owner) {
    if (!os || os->payload_owner || !owner)
        return;
    os->payload_owner = owner;
}

static Outcome *outcome_set_push_slot(OutcomeSet *os) {
    if (os->len >= os->cap) {
        os->cap = os->cap ? os->cap * 2 : 8;
        os->items = cetta_realloc(os->items, sizeof(Outcome) * os->cap);
    }
    return &os->items[os->len++];
}

static void outcome_init(Outcome *out) {
    if (!out)
        return;
    out->kind = CETTA_OUTCOME_INLINE;
    out->atom = NULL;
    out->materialized_atom = NULL;
    out->control = CETTA_OUTCOME_CONTROL_NONE;
    bindings_init(&out->env);
    variant_instance_init(&out->variant);
    out->answer_ref.bank = NULL;
    out->answer_ref.ref = CETTA_ANSWER_REF_NONE;
    cetta_var_map_init(&out->answer_ref.goal_instantiation);
}

static void outcome_free_fields(Outcome *out) {
    if (!out)
        return;
    bindings_free(&out->env);
    variant_instance_free(&out->variant);
    cetta_var_map_free(&out->answer_ref.goal_instantiation);
    out->answer_ref.bank = NULL;
    out->answer_ref.ref = CETTA_ANSWER_REF_NONE;
    out->kind = CETTA_OUTCOME_INLINE;
    out->atom = NULL;
    out->materialized_atom = NULL;
    out->control = CETTA_OUTCOME_CONTROL_NONE;
}

static void outcome_move(Outcome *dst, Outcome *src) {
    dst->kind = src->kind;
    dst->atom = src->atom;
    dst->materialized_atom = src->materialized_atom;
    dst->control = src->control;
    bindings_move(&dst->env, &src->env);
    variant_instance_move(&dst->variant, &src->variant);
    dst->answer_ref = src->answer_ref;
    cetta_var_map_init(&src->answer_ref.goal_instantiation);
    src->answer_ref.bank = NULL;
    src->answer_ref.ref = CETTA_ANSWER_REF_NONE;
    src->kind = CETTA_OUTCOME_INLINE;
    src->atom = NULL;
    src->materialized_atom = NULL;
    src->control = CETTA_OUTCOME_CONTROL_NONE;
}

static void outcome_set_mark_control_since(OutcomeSet *os, uint32_t start,
                                           CettaOutcomeControl control) {
    if (!os)
        return;
    for (uint32_t i = start; i < os->len; i++)
        os->items[i].control = (uint8_t)control;
}

static bool outcome_set_consume_control_since(OutcomeSet *os, uint32_t start,
                                              CettaOutcomeControl control) {
    bool seen = false;
    if (!os)
        return false;
    for (uint32_t i = start; i < os->len; i++) {
        if (os->items[i].control == (uint8_t)control)
            seen = true;
        os->items[i].control = CETTA_OUTCOME_CONTROL_NONE;
    }
    return seen;
}

static void outcome_refresh_materialized_fast_path(Outcome *out);
static bool bindings_effective_merge(Bindings *owned,
                                     const Bindings **effective,
                                     const Bindings *outer_env,
                                     const Bindings *inner_env,
                                     bool include_constraints);

static bool outcome_clone_goal_instantiation(Arena *owner,
                                             CettaVarMap *dst,
                                             const CettaVarMap *src) {
    if (owner)
        return cetta_var_map_clone_live(owner, dst, src);
    return cetta_var_map_clone(dst, src);
}

static bool outcome_clone(Outcome *dst, const Outcome *src, Arena *owner) {
    if (!dst || !src)
        return false;
    outcome_init(dst);
    dst->kind = src->kind;
    dst->atom = src->atom;
    dst->materialized_atom = src->materialized_atom;
    dst->control = src->control;
    dst->answer_ref.bank = src->answer_ref.bank;
    dst->answer_ref.ref = src->answer_ref.ref;
    if (!bindings_clone(&dst->env, &src->env) ||
        !variant_instance_clone(&dst->variant, &src->variant) ||
        !outcome_clone_goal_instantiation(owner,
                                          &dst->answer_ref.goal_instantiation,
                                          &src->answer_ref.goal_instantiation)) {
        outcome_free_fields(dst);
        return false;
    }
    if (!dst->materialized_atom)
        outcome_refresh_materialized_fast_path(dst);
    return true;
}

static bool atom_contains_vars(const Atom *atom) {
    return atom_has_vars(atom);
}

static Atom *outcome_preview_atom(const Outcome *out) {
    if (!out)
        return NULL;
    if (out->kind == CETTA_OUTCOME_INLINE)
        return out->atom;
    if (!out->answer_ref.bank || out->answer_ref.ref == CETTA_ANSWER_REF_NONE)
        return NULL;
    const AnswerRecord *record =
        answer_bank_get(out->answer_ref.bank, out->answer_ref.ref);
    return record ? record->result : NULL;
}

static Atom *dispatch_native_op(Space *s, Arena *a, Atom *head, Atom **args, uint32_t nargs);
static Atom *materialize_runtime_token(Space *s, Arena *a, Atom *atom);
static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr, int fuel);
static Atom *space_arg_error(Arena *a, Atom *call, const char *message);

static bool grounded_dispatch_accepts_data_arg(SymbolId head_id, uint32_t arg_index) {
    return arg_index == 1 &&
           (head_id == g_builtin_syms.add_atom ||
            head_id == g_builtin_syms.remove_atom);
}
static bool bindings_project_answer_ref_env(Arena *a,
                                            const CettaVarMap *goal_instantiation,
                                            const Bindings *full,
                                            Bindings *projected);
static void eval_for_caller(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                            const Bindings *prefix, bool preserve_bindings,
                            OutcomeSet *os);

static bool atom_eval_is_immediate_value(Atom *atom, int fuel) {
    return fuel == 0 ||
           atom->kind == ATOM_SYMBOL ||
           atom->kind == ATOM_GROUNDED ||
           atom->kind == ATOM_VAR ||
           (atom->kind == ATOM_EXPR && atom->expr.len == 0);
}

static bool outcome_init_answer_ref(Arena *owner, Outcome *out, const AnswerBank *bank,
                                    AnswerRef ref,
                                    const CettaVarMap *goal_instantiation,
                                    const Bindings *prefix_env) {
    if (!owner || !out || !bank || ref == CETTA_ANSWER_REF_NONE)
        return false;
    outcome_init(out);
    out->kind = CETTA_OUTCOME_ANSWER_REF;
    out->answer_ref.bank = bank;
    out->answer_ref.ref = ref;
    if (!cetta_var_map_clone_live(owner,
                                  &out->answer_ref.goal_instantiation,
                                  goal_instantiation)) {
        outcome_free_fields(out);
        return false;
    }
    if (prefix_env &&
        !bindings_project_answer_ref_env(owner,
                                         &out->answer_ref.goal_instantiation,
                                         prefix_env,
                                         &out->env)) {
        outcome_free_fields(out);
        return false;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ANSWER_REF_EMIT);
    return true;
}

static bool outcome_inflate_answer_ref(Arena *a, Outcome *out) {
    Atom *result = NULL;
    Bindings bank_bindings;
    VariantInstance bank_variant;
    Bindings merged;
    const Bindings *effective = NULL;

    if (!out || out->kind != CETTA_OUTCOME_ANSWER_REF)
        return true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_ANSWER_REF_INFLATE_CALL);
    if (!table_store_materialize_answer_ref(out->answer_ref.bank,
                                            out->answer_ref.ref,
                                            a,
                                            &out->answer_ref.goal_instantiation,
                                            &result,
                                            &bank_bindings,
                                            &bank_variant)) {
        return false;
    }
    if (!bindings_effective_merge(&merged, &effective,
                                  &out->env, &bank_bindings, true)) {
        bindings_free(&bank_bindings);
        variant_instance_free(&bank_variant);
        return false;
    }
    bindings_free(&out->env);
    if (effective == &merged) {
        bindings_move(&out->env, &merged);
    } else if (effective == &bank_bindings) {
        bindings_move(&out->env, &bank_bindings);
    } else if (effective == NULL) {
        bindings_init(&out->env);
    } else {
        if (!bindings_clone(&out->env, effective)) {
            bindings_free(&bank_bindings);
            variant_instance_free(&bank_variant);
            return false;
        }
        bindings_free(&bank_bindings);
        bindings_free(&merged);
    }
    if (effective == &merged) {
        bindings_free(&bank_bindings);
    }
    variant_instance_free(&out->variant);
    variant_instance_move(&out->variant, &bank_variant);
    cetta_var_map_free(&out->answer_ref.goal_instantiation);
    out->answer_ref.bank = NULL;
    out->answer_ref.ref = CETTA_ANSWER_REF_NONE;
    out->kind = CETTA_OUTCOME_INLINE;
    out->atom = result;
    out->materialized_atom = NULL;
    outcome_refresh_materialized_fast_path(out);
    return true;
}

static Atom *eval_direct_grounded_application(Space *s, Arena *a, Atom *atom,
                                              const Bindings *prefix, int fuel) {
    Atom *bound = registry_lookup_atom(atom);
    if (bound)
        atom = bound;
    atom = materialize_runtime_token(s, a, atom);
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return NULL;

    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL || !is_grounded_op(head->sym_id))
        return NULL;

    uint32_t nargs = atom->expr.len - 1;
    Atom **args = arena_alloc(a, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        Atom *arg = atom->expr.elems[i + 1];
        arg = bindings_apply_if_vars(prefix, a, arg);
        Atom *resolved = registry_lookup_atom(arg);
        args[i] = materialize_runtime_token(s, a, resolved ? resolved : arg);
        if (atom_is_empty(args[i]) || atom_is_error(args[i]) ||
            (!atom_eval_is_immediate_value(args[i], fuel) &&
             !grounded_dispatch_accepts_data_arg(head->sym_id, i)))
            return NULL;
    }
    return dispatch_native_op(s, a, head, args, nargs);
}

static void outcome_refresh_materialized_fast_path(Outcome *out) {
    if (!out)
        return;
    if (out->kind == CETTA_OUTCOME_ANSWER_REF) {
        out->materialized_atom = NULL;
        return;
    }
    if (!variant_instance_present(&out->variant) &&
        (out->env.len == 0 || !atom_contains_vars(out->atom))) {
        out->materialized_atom = out->atom;
    } else {
        out->materialized_atom = NULL;
    }
}

static void outcome_try_factor_variant(Outcome *out) {
    if (!out || out->kind != CETTA_OUTCOME_INLINE ||
        variant_instance_present(&out->variant) ||
        !out->atom || !atom_contains_vars(out->atom))
        return;
    VariantBank *bank = eval_active_outcome_variant_bank();
    if (!bank)
        return;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_FACTOR_ATTEMPT);
    VariantShape shape;
    if (!variant_shape_from_bound_atom(bank, &out->env, out->atom, &shape))
        return;
    if (!variant_instance_from_shape(&out->variant, &shape)) {
        variant_shape_free(&shape);
        return;
    }
    out->atom = shape.skeleton;
    variant_shape_free(&shape);
    out->materialized_atom = NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_FACTOR_SUCCESS);
}

static Atom *outcome_atom_materialize(Arena *a, Outcome *out) {
    Atom *materialized;
    VariantInstance compacted;
    if (!out)
        return NULL;
    if (out->kind == CETTA_OUTCOME_ANSWER_REF && !outcome_inflate_answer_ref(a, out))
        return NULL;
    if (out->materialized_atom)
        return out->materialized_atom;
    materialized = out->atom;
    if (variant_instance_present(&out->variant)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_SLOT_MATERIALIZE);
        if (out->env.len > 0 || out->env.eq_len > 0) {
            variant_instance_init(&compacted);
            if (!variant_instance_sink_env(a, &compacted, &out->variant, &out->env))
                return NULL;
            materialized = variant_instance_materialize(a, materialized, &compacted);
            variant_instance_free(&compacted);
        } else {
            materialized = variant_instance_materialize(a, materialized, &out->variant);
        }
    } else if (out->env.len > 0) {
        materialized = bindings_apply_if_vars(&out->env, a, materialized);
    }
    out->materialized_atom = materialized;
    return materialized;
}

static Atom *outcome_atom_materialize_traced(Arena *a, Outcome *out,
                                             CettaRuntimeCounter site_counter) {
    if (out && !out->materialized_atom && variant_instance_present(&out->variant))
        cetta_runtime_stats_inc(site_counter);
    return outcome_atom_materialize(a, out);
}

static Atom *outcome_atom_materialize_variant_only(Arena *a, Outcome *out) {
    if (!out)
        return NULL;
    if (out->kind == CETTA_OUTCOME_ANSWER_REF && !outcome_inflate_answer_ref(a, out))
        return NULL;
    if (!variant_instance_present(&out->variant))
        return out->atom;
    return variant_instance_materialize(a, out->atom, &out->variant);
}

static bool symbol_id_is_builtin_surface(SymbolId id) {
    return id != SYMBOL_ID_NONE && id <= g_builtin_syms.native_handle;
}

static bool outcome_skip_call_observation_fast_path(Space *s,
                                                    const Outcome *out) {
    Atom *preview = outcome_preview_atom(out);
    Atom *head;
    if (!s || !preview || preview->kind != ATOM_EXPR ||
        preview->expr.len == 0) {
        return false;
    }
    head = preview->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    if (atom_is_native_dispatch_head(head))
        return false;
    if (g_library_context && g_library_context->foreign_runtime &&
        cetta_foreign_is_callable_head(g_library_context->foreign_runtime,
                                       head)) {
        return false;
    }
    if (space_equations_may_match_known_head(s, head->sym_id))
        return false;
    return true;
}

static bool outcome_atom_is_error(Arena *a, Outcome *out) {
    Atom *candidate;
    Atom *head;
    if (!out)
        return false;
    if (out->kind == CETTA_OUTCOME_ANSWER_REF && !outcome_inflate_answer_ref(a, out))
        return false;
    if (out->materialized_atom)
        return atom_is_error(out->materialized_atom);
    candidate = out->atom;
    if (!candidate)
        return false;
    if (candidate->kind == ATOM_EXPR && candidate->expr.len > 0) {
        head = candidate->expr.elems[0];
        if (head && head->kind == ATOM_SYMBOL)
            return atom_is_symbol_id(head, g_builtin_syms.error);
    } else if (candidate->kind != ATOM_VAR) {
        return false;
    }
    return atom_is_error(outcome_atom_materialize_traced(
        a, out, CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_ERROR_FILTER));
}

static __attribute__((unused)) bool outcome_atom_is_empty_or_error(Arena *a, Outcome *out) {
    return atom_is_empty_or_error(outcome_atom_materialize(a, out));
}

static void bindings_array_free(Bindings *items, uint32_t len) {
    if (!items) return;
    for (uint32_t i = 0; i < len; i++)
        bindings_free(&items[i]);
    free(items);
}

void outcome_set_add(OutcomeSet *os, Atom *atom, const Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = atom;
    bindings_assert_no_private_variant_slots(env);
    if (!bindings_clone(&slot->env, env)) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
    outcome_try_factor_variant(slot);
    outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_unfactored(OutcomeSet *os, Atom *atom,
                                       const Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = atom;
    bindings_assert_no_private_variant_slots(env);
    if (!bindings_clone(&slot->env, env)) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
    outcome_refresh_materialized_fast_path(slot);
}

void outcome_set_add_move(OutcomeSet *os, Atom *atom, Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = atom;
    bindings_assert_no_private_variant_slots(env);
    bindings_move(&slot->env, env);
    outcome_try_factor_variant(slot);
    outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_existing(OutcomeSet *os, const Outcome *src) {
    Outcome *slot = outcome_set_push_slot(os);
    bindings_assert_no_private_variant_slots(&src->env);
    if (!outcome_clone(slot, src, outcome_set_payload_arena(os, NULL))) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
    if (!slot->materialized_atom)
        outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_existing_move(OutcomeSet *os, Outcome *src) {
    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    outcome_move(slot, src);
    if (!slot->materialized_atom)
        outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_existing_with_env(Arena *a,
                                              OutcomeSet *os,
                                              const Outcome *src,
                                              const Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    Arena *payload_arena = outcome_set_payload_arena(os, a);
    outcome_init(slot);
    slot->kind = src->kind;
    slot->atom = src->atom;
    slot->control = src->control;
    bindings_assert_no_private_variant_slots(&src->env);
    bindings_assert_no_private_variant_slots(env);
    if (!(src->kind == CETTA_OUTCOME_ANSWER_REF
              ? bindings_project_answer_ref_env(payload_arena,
                                               &src->answer_ref.goal_instantiation,
                                               env,
                                               &slot->env)
              : bindings_clone(&slot->env, env)) ||
        !variant_instance_clone(&slot->variant, &src->variant) ||
        !outcome_clone_goal_instantiation(payload_arena,
                                          &slot->answer_ref.goal_instantiation,
                                          &src->answer_ref.goal_instantiation)) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
    slot->answer_ref.bank = src->answer_ref.bank;
    slot->answer_ref.ref = src->answer_ref.ref;
    outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_with_variant(OutcomeSet *os, Atom *atom,
                                         const Bindings *env,
                                         const VariantInstance *variant) {
    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = atom;
    bindings_assert_no_private_variant_slots(env);
    if (!bindings_clone(&slot->env, env) ||
        !variant_instance_clone(&slot->variant, variant)) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
    outcome_refresh_materialized_fast_path(slot);
}

static bool outcome_set_add_promoted_existing(Arena *a, OutcomeSet *os,
                                              const Outcome *src,
                                              bool preserve_bindings) {
    Arena *payload_arena = outcome_set_payload_arena(os, a);
    if (!payload_arena || !os || !src)
        return false;
    (void)preserve_bindings;

    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->control = src->control;
    if (src->kind == CETTA_OUTCOME_ANSWER_REF) {
        slot->kind = CETTA_OUTCOME_ANSWER_REF;
        slot->answer_ref.bank = src->answer_ref.bank;
        slot->answer_ref.ref = src->answer_ref.ref;
        if (!bindings_clone(&slot->env, &src->env) ||
            !cetta_var_map_clone_live(payload_arena,
                                      &slot->answer_ref.goal_instantiation,
                                      &src->answer_ref.goal_instantiation)) {
            outcome_free_fields(slot);
            os->len--;
            return false;
        }
        slot->materialized_atom = NULL;
        return true;
    }
    if (src->atom)
        slot->atom = atom_deep_copy(payload_arena, src->atom);
    if (src->atom && !slot->atom) {
        outcome_free_fields(slot);
        os->len--;
        return false;
    }
    bindings_assert_no_private_variant_slots(&src->env);
    if (!bindings_clone(&slot->env, &src->env) ||
        !bindings_promote_atoms_to_arena(payload_arena, &slot->env) ||
        !variant_instance_clone(&slot->variant, &src->variant) ||
        !variant_instance_promote_atoms_to_arena(payload_arena, &slot->variant)) {
        outcome_free_fields(slot);
        os->len--;
        return false;
    }
    slot->materialized_atom = NULL;
    outcome_refresh_materialized_fast_path(slot);
    return true;
}

static void outcome_set_append_promoted(Arena *a, OutcomeSet *dst,
                                        OutcomeSet *src,
                                        bool preserve_bindings) {
    if (!a || !dst || !src)
        return;
    for (uint32_t i = 0; i < src->len; i++)
        (void)outcome_set_add_promoted_existing(a, dst, &src->items[i],
                                                preserve_bindings);
}

static inline bool bindings_has_value_entries(const Bindings *env) {
    return env && env->len > 0;
}

static inline bool bindings_has_any_entries(const Bindings *env) {
    return env && (env->len > 0 || env->eq_len > 0);
}

static bool bindings_effective_merge(Bindings *owned,
                                     const Bindings **effective,
                                     const Bindings *outer_env,
                                     const Bindings *inner_env,
                                     bool include_constraints) {
    bool outer_nonempty = include_constraints
        ? bindings_has_any_entries(outer_env)
        : bindings_has_value_entries(outer_env);
    bool inner_nonempty = include_constraints
        ? bindings_has_any_entries(inner_env)
        : bindings_has_value_entries(inner_env);

    bindings_init(owned);
    if (!outer_nonempty) {
        *effective = inner_nonempty ? inner_env : NULL;
        return true;
    }
    if (!inner_nonempty) {
        *effective = outer_env;
        return true;
    }
    if (!bindings_try_merge_live(owned, outer_env) ||
        !bindings_try_merge_live(owned, inner_env)) {
        bindings_free(owned);
        *effective = NULL;
        return false;
    }
    *effective = owned;
    return true;
}

static Atom *outcome_materialize_with_outer_env(Arena *a, const Outcome *src,
                                                const Bindings *outer_env) {
    Outcome inflated;
    const Outcome *effective_src = src;
    Atom *materialized;
    Bindings merged;
    const Bindings *effective = NULL;
    outcome_init(&inflated);
    bindings_assert_no_private_variant_slots(&src->env);
    bindings_assert_no_private_variant_slots(outer_env);
    if (src->kind == CETTA_OUTCOME_ANSWER_REF) {
        if (!outcome_clone(&inflated, src, a) ||
            !outcome_inflate_answer_ref(a, &inflated)) {
            outcome_free_fields(&inflated);
            return NULL;
        }
        effective_src = &inflated;
    }
    materialized = effective_src->atom;
    if (variant_instance_present(&effective_src->variant)) {
        outcome_free_fields(&inflated);
        return NULL;
    }
    if (!bindings_effective_merge(&merged, &effective, outer_env, &effective_src->env, false)) {
        outcome_free_fields(&inflated);
        return NULL;
    }
    if (effective)
        materialized = bindings_apply_if_vars(effective, a, materialized);
    if (effective == &merged)
        bindings_free(&merged);
    outcome_free_fields(&inflated);
    return materialized;
}

static bool outcome_set_add_compacted_variant(Arena *a, OutcomeSet *os,
                                              const Outcome *src,
                                              const Bindings *outer_env) {
    Outcome inflated;
    const Outcome *effective_src = src;
    Outcome *slot;
    Bindings merged;
    const Bindings *effective = NULL;
    outcome_init(&inflated);

    if (src->kind == CETTA_OUTCOME_ANSWER_REF) {
        if (!outcome_clone(&inflated, src, a) ||
            !outcome_inflate_answer_ref(a, &inflated)) {
            outcome_free_fields(&inflated);
            return false;
        }
        effective_src = &inflated;
    }

    if (!variant_instance_present(&effective_src->variant)) {
        outcome_free_fields(&inflated);
        return false;
    }

    if (!bindings_effective_merge(&merged, &effective, outer_env, &effective_src->env, true)) {
        outcome_free_fields(&inflated);
        return false;
    }

    slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = effective_src->atom;
    slot->control = effective_src->control;
    if (effective) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_PREFIX_COMPACT);
        if (!variant_instance_sink_env(a, &slot->variant, &effective_src->variant, effective)) {
            if (effective == &merged)
                bindings_free(&merged);
            outcome_free_fields(&inflated);
            outcome_free_fields(slot);
            os->len--;
            return false;
        }
    } else if (!variant_instance_clone(&slot->variant, &effective_src->variant)) {
        if (effective == &merged)
            bindings_free(&merged);
        outcome_free_fields(&inflated);
        outcome_free_fields(slot);
        os->len--;
        return false;
    }
    outcome_refresh_materialized_fast_path(slot);
    if (effective == &merged)
        bindings_free(&merged);
    outcome_free_fields(&inflated);
    return true;
}

static void outcome_set_add_prefixed_outcome(Arena *a, OutcomeSet *os,
                                             const Outcome *src,
                                             const Bindings *outer_env,
                                             bool preserve_bindings) {
    Bindings empty;
    bindings_init(&empty);
    bindings_assert_no_private_variant_slots(&src->env);
    bindings_assert_no_private_variant_slots(outer_env);
    if (!preserve_bindings) {
        if (variant_instance_present(&src->variant)) {
            if (outcome_set_add_compacted_variant(a, os, src, outer_env))
                return;
        }
        Atom *applied = outcome_materialize_with_outer_env(a, src, outer_env);
        if (!applied)
            return;
        uint32_t before = os->len;
        outcome_set_add(os, applied, &empty);
        outcome_set_mark_control_since(os, before, src->control);
        return;
    }
    if (!bindings_has_any_entries(outer_env)) {
        outcome_set_add_existing(os, src);
        return;
    }
    Bindings merged;
    const Bindings *effective = NULL;
    if (!bindings_effective_merge(&merged, &effective, outer_env, &src->env, true) ||
        !effective)
        return;
    outcome_set_add_existing_with_env(a, os, src, effective);
    if (effective == &merged)
        bindings_free(&merged);
}

static void outcome_set_filter_errors_if_success(Arena *a, OutcomeSet *os) {
    bool has_success = false;
    for (uint32_t i = 0; i < os->len; i++) {
        if (!outcome_atom_is_error(a, &os->items[i])) {
            has_success = true;
            break;
        }
    }
    if (!has_success) return;

    uint32_t out = 0;
    for (uint32_t i = 0; i < os->len; i++) {
        if (outcome_atom_is_error(a, &os->items[i]))
            continue;
        if (out != i) {
            outcome_free_fields(&os->items[out]);
            outcome_init(&os->items[out]);
            outcome_move(&os->items[out], &os->items[i]);
        }
        out++;
    }
    for (uint32_t i = out; i < os->len; i++)
        outcome_free_fields(&os->items[i]);
    os->len = out;
}

static bool atom_is_petta_typecheck_error(Atom *atom) {
    if (!atom_is_error(atom) || atom->expr.len < 3)
        return false;
    Atom *reason = atom->expr.elems[2];
    if (atom_is_symbol_id(reason, g_builtin_syms.bad_type) ||
        atom_is_symbol_id(reason, g_builtin_syms.bad_arg_type))
        return true;
    if (reason && reason->kind == ATOM_EXPR && reason->expr.len > 0) {
        Atom *head = reason->expr.elems[0];
        return atom_is_symbol_id(head, g_builtin_syms.bad_type) ||
               atom_is_symbol_id(head, g_builtin_syms.bad_arg_type);
    }
    return false;
}

static bool outcome_set_all_petta_typecheck_errors(Arena *a, OutcomeSet *os) {
    if (!os || os->len == 0)
        return false;
    for (uint32_t i = 0; i < os->len; i++) {
        Atom *atom = outcome_atom_materialize(a, &os->items[i]);
        if (!atom_is_petta_typecheck_error(atom))
            return false;
    }
    return true;
}

void outcome_set_free(OutcomeSet *os) {
    for (uint32_t i = 0; i < os->len; i++)
        outcome_free_fields(&os->items[i]);
    free(os->items);
    os->items = NULL;
    os->len = os->cap = 0;
    os->payload_owner = NULL;
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
        atom_symbol_id(a, g_builtin_syms.bad_arg_type),
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

typedef struct {
    Atom *key_atom;
    Atom *acc_atom;
    uint32_t hash;
} FoldByKeyBucket;

typedef struct {
    bool used;
    uint32_t hash;
    uint32_t bucket_index;
} FoldByKeySlot;

typedef struct {
    FoldByKeyBucket *buckets;
    uint32_t bucket_len;
    uint32_t bucket_cap;
    FoldByKeySlot *slots;
    uint32_t slot_cap;
} FoldByKeyTable;

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

static uint32_t next_pow2_u32(uint32_t n) {
    uint32_t cap = 1;
    while (cap < n && cap < (1u << 31))
        cap <<= 1;
    return cap;
}

static void fold_by_key_table_init(FoldByKeyTable *table) {
    memset(table, 0, sizeof(*table));
}

static void fold_by_key_table_free(FoldByKeyTable *table) {
    free(table->buckets);
    free(table->slots);
    fold_by_key_table_init(table);
}

static void fold_by_key_slots_place_existing(FoldByKeySlot *slots, uint32_t slot_cap,
                                             FoldByKeyBucket *buckets,
                                             uint32_t bucket_index) {
    uint32_t mask = slot_cap - 1;
    uint32_t hash = buckets[bucket_index].hash;
    uint32_t idx = hash & mask;
    for (;;) {
        if (!slots[idx].used) {
            buckets[bucket_index].hash = hash;
            slots[idx].used = true;
            slots[idx].hash = hash;
            slots[idx].bucket_index = bucket_index;
            return;
        }
        idx = (idx + 1) & mask;
    }
}

static void fold_by_key_table_ensure_capacity(FoldByKeyTable *table,
                                              uint32_t min_groups) {
    uint32_t desired_cap = table->bucket_cap ? table->bucket_cap : 8;
    while (desired_cap < min_groups && desired_cap < (1u << 31))
        desired_cap <<= 1;

    if (desired_cap != table->bucket_cap) {
        table->buckets = table->buckets
            ? cetta_realloc(table->buckets, sizeof(FoldByKeyBucket) * desired_cap)
            : cetta_malloc(sizeof(FoldByKeyBucket) * desired_cap);
        table->bucket_cap = desired_cap;
    }

    uint32_t min_slot_cap = desired_cap > (1u << 30) ? (1u << 31) : desired_cap * 2;
    uint32_t desired_slot_cap = next_pow2_u32(min_slot_cap);
    if (desired_slot_cap != table->slot_cap) {
        FoldByKeySlot *slots = cetta_malloc(sizeof(FoldByKeySlot) * desired_slot_cap);
        memset(slots, 0, sizeof(FoldByKeySlot) * desired_slot_cap);
        for (uint32_t i = 0; i < table->bucket_len; i++)
            fold_by_key_slots_place_existing(slots, desired_slot_cap, table->buckets, i);
        free(table->slots);
        table->slots = slots;
        table->slot_cap = desired_slot_cap;
    }
}

static bool fold_by_key_table_lookup_or_insert(FoldByKeyTable *table,
                                               Atom *key_atom, uint32_t hash,
                                               uint32_t *bucket_index_out) {
    fold_by_key_table_ensure_capacity(table, table->bucket_len + 1);
    uint32_t mask = table->slot_cap - 1;
    uint32_t idx = hash & mask;
    for (;;) {
        if (!table->slots[idx].used) {
            uint32_t bucket_index = table->bucket_len++;
            table->buckets[bucket_index].key_atom = key_atom;
            table->buckets[bucket_index].acc_atom = NULL;
            table->buckets[bucket_index].hash = hash;
            table->slots[idx].used = true;
            table->slots[idx].hash = hash;
            table->slots[idx].bucket_index = bucket_index;
            *bucket_index_out = bucket_index;
            return true;
        }
        uint32_t bucket_index = table->slots[idx].bucket_index;
        if (table->slots[idx].hash == hash &&
            atom_eq(table->buckets[bucket_index].key_atom, key_atom)) {
            *bucket_index_out = bucket_index;
            return false;
        }
        idx = (idx + 1) & mask;
    }
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

static Atom *dispatch_native_space_mutation(Space *s, Arena *a, Atom *head,
                                            Atom **args, uint32_t nargs) {
    if (!head || head->kind != ATOM_SYMBOL || nargs != 2 || !g_registry)
        return NULL;

    SymbolId head_id = head->sym_id;
    bool is_add = head_id == g_builtin_syms.add_atom;
    bool is_remove = head_id == g_builtin_syms.remove_atom;
    if (!is_add && !is_remove)
        return NULL;

    Atom *call = make_call_expr(a, head, args, nargs);
    Atom *space_ref = args[0];
    Atom *payload = args[1];
    const int fuel = eval_get_default_fuel();
    const char *surface = is_add ? "add-atom" : "remove-atom";
    const char *explicit_surface = is_add ? "mork:add-atom" : "mork:remove-atom";
    SymbolId explicit_head_id = is_add ? g_builtin_syms.mork_add_atom
                                       : g_builtin_syms.mork_remove_atom;

    if (generic_mork_handle_sugar_allowed(s, a, space_ref, fuel)) {
        Atom **resolved_args = arena_alloc(a, sizeof(Atom *) * nargs);
        for (uint32_t i = 0; i < nargs; i++)
            resolved_args[i] = resolve_registry_refs(a, args[i]);
        Atom *result = dispatch_named_native(s, a, explicit_head_id,
                                             resolved_args, nargs);
        if (result)
            return rewrite_error_call(a, call, result);
    }

    Atom *mork_handle_error = guard_mork_handle_surface(
        s, a, call, space_ref, fuel, surface, explicit_surface);
    if (mork_handle_error)
        return mork_handle_error;

    Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
    if (!target) {
        return space_arg_error(a, call,
                               is_add
                                   ? "add-atom expects a space as the first argument"
                                   : "remove-atom expects a space as the first argument");
    }

    Atom *mork_error = guard_mork_space_surface(
        a, call, target, surface, explicit_surface);
    if (mork_error)
        return mork_error;

    if (!space_match_backend_materialize_attached(
            target, eval_storage_arena(a))) {
        return atom_error(
            a, call,
            atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed"));
    }

    if (is_add) {
        Arena *dst = eval_storage_arena(a);
        (void)space_admit_atom(target, dst, payload);
        return atom_unit(a);
    }

    Atom *compare_atom = space_compare_atom(target, a, payload);
    if (!(target && target->universe &&
          space_remove_atom_id(target,
                               term_universe_lookup_atom_id(target->universe,
                                                            compare_atom)))) {
        space_remove(target, compare_atom);
    }
    return atom_unit(a);
}

static Atom *dispatch_native_op(Space *s, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *head_name = head ? atom_name_cstr(head) : NULL;
    if (head && head_name && active_profile() &&
        !cetta_profile_allows_surface(active_profile(), head_name)) {
        return profile_surface_error(a, make_call_expr(a, head, args, nargs), head_name);
    }
    if (petta_grounded_op_is_under_applied(head, nargs))
        return NULL;
    if (language_is_petta() && head &&
        atom_is_symbol_id(head, g_builtin_syms.equals) && nargs == 2) {
        BindingsBuilder bb;
        if (!bindings_builder_init(&bb, NULL))
            return NULL;
        bool ok = match_atoms_builder(args[0], args[1], &bb);
        bindings_builder_free(&bb);
        return atom_bool(a, ok);
    }
    if (head && atom_is_symbol_id(head, g_builtin_syms.size) &&
        nargs == 1) {
        Atom *call = make_call_expr(a, head, args, nargs);
        if (generic_mork_handle_sugar_allowed(
                s, a, args[0], eval_get_default_fuel())) {
            Atom *resolved = resolve_registry_refs(a, args[0]);
            Atom *mork_args[] = { resolved };
            Atom *result = dispatch_named_native(
                s, a, g_builtin_syms.lib_mork_space_size, mork_args, 1);
            return rewrite_error_call(a, call, result);
        }
        Atom *handle_error = guard_mork_handle_surface(
            s, a, call, args[0], eval_get_default_fuel(), "size", "mork:size");
        if (handle_error) return handle_error;
        if (args[0]->kind == ATOM_GROUNDED &&
            args[0]->ground.gkind == GV_SPACE) {
            Atom *error = guard_mork_space_surface(
                a, call, (Space *)args[0]->ground.ptr, "size", "mork:size");
            if (error) return error;
        }
    }
    Atom *space_mutation = dispatch_native_space_mutation(s, a, head, args, nargs);
    if (space_mutation) return space_mutation;
    Atom *result = grounded_dispatch(a, head, args, nargs);
    if (result) return result;
    if (g_library_context) {
        return cetta_library_dispatch_native(g_library_context, s, a, head, args, nargs);
    }
    return NULL;
}

static bool is_petta_equals_native_call(Atom *head, uint32_t nargs) {
    return language_is_petta() &&
           head &&
           atom_is_symbol_id(head, g_builtin_syms.equals) &&
           nargs == 2;
}

static bool dispatch_petta_equals_native_outcomes(
    Space *s,
    Arena *a,
    Atom *head,
    Atom **args,
    uint32_t nargs,
    Atom *result_type,
    int fuel,
    const Bindings *prefix,
    bool preserve_bindings,
    OutcomeSet *os
) {
    if (!is_petta_equals_native_call(head, nargs))
        return false;

    Bindings empty;
    bindings_init(&empty);
    const Bindings *base = prefix ? prefix : &empty;
    BindingsBuilder bb;
    if (!bindings_builder_init(&bb, base)) {
        eval_for_caller(s, a, result_type, atom_false(a), fuel, base,
                        preserve_bindings, os);
        return true;
    }

    bool ok = match_atoms_builder(args[0], args[1], &bb);
    const Bindings *result_env = ok ? bindings_builder_bindings(&bb) : base;
    eval_for_caller(s, a, result_type, atom_bool(a, ok), fuel, result_env,
                    preserve_bindings, os);
    bindings_builder_free(&bb);
    return true;
}

static bool dispatch_petta_equals_native_tail(
    Arena *a,
    Atom *head,
    Atom **args,
    uint32_t nargs,
    Atom *result_type,
    const Bindings *prefix,
    Atom **tail_next,
    Atom **tail_type,
    Bindings *tail_env
) {
    if (!is_petta_equals_native_call(head, nargs))
        return false;

    Bindings empty;
    bindings_init(&empty);
    const Bindings *base = prefix ? prefix : &empty;
    BindingsBuilder bb;
    if (!bindings_builder_init(&bb, base)) {
        *tail_next = atom_false(a);
        *tail_type = result_type;
        (void)bindings_copy(tail_env, base);
        return true;
    }

    bool ok = match_atoms_builder(args[0], args[1], &bb);
    const Bindings *result_env = ok ? bindings_builder_bindings(&bb) : base;
    *tail_next = atom_bool(a, ok);
    *tail_type = result_type;
    (void)bindings_copy(tail_env, result_env);
    bindings_builder_free(&bb);
    return true;
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
    Arena *dst = eval_storage_arena(a);
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

typedef struct {
    VarId var_id;
    SymbolId spelling;
} VisibleVarRef;

typedef struct {
    uint32_t base_id;
    SymbolId spelling;
    uint32_t path_offset;
    uint32_t path_len;
} VisibleVarShapeRef;

#define BODY_VISIBLE_INLINE_CAP 64
#define BODY_VISIBLE_PATH_UNSET UINT32_MAX

typedef struct {
    VisibleVarRef inline_items[BODY_VISIBLE_INLINE_CAP];
    VisibleVarRef *items;
    uint32_t len;
    uint32_t cap;
} FreeVarSet;

typedef struct {
    VisibleVarShapeRef inline_items[BODY_VISIBLE_INLINE_CAP];
    VisibleVarShapeRef *items;
    uint32_t len;
    uint32_t cap;
    uint32_t inline_path_items[BODY_VISIBLE_INLINE_CAP];
    uint32_t *path_items;
    uint32_t path_len;
    uint32_t path_cap;
} FreeVarShapeSet;

typedef struct {
    uint32_t inline_items[BODY_VISIBLE_INLINE_CAP];
    uint32_t *items;
    uint32_t len;
    uint32_t cap;
} IndexPathStack;

typedef struct {
    VarId inline_ids[BODY_VISIBLE_INLINE_CAP];
    VarId *ids;
    uint32_t len;
    uint32_t cap;
} BoundVarStack;

#define BODY_VISIBLE_FREE_VAR_CACHE_CAP 256

typedef struct {
    bool occupied;
    uint32_t body_shape_hash;
    Atom *body_key;
    FreeVarShapeSet vars;
} BodyVisibleFreeVarCacheEntry;

static BodyVisibleFreeVarCacheEntry
    g_body_visible_free_var_cache[BODY_VISIBLE_FREE_VAR_CACHE_CAP];
static Arena g_body_visible_free_var_cache_arena;
static bool g_body_visible_free_var_cache_arena_ready = false;

static void free_var_set_init(FreeVarSet *set) {
    set->items = set->inline_items;
    set->len = 0;
    set->cap = BODY_VISIBLE_INLINE_CAP;
}

static void free_var_set_free(FreeVarSet *set) {
    if (set->items != set->inline_items)
        free(set->items);
    set->items = set->inline_items;
    set->len = 0;
    set->cap = BODY_VISIBLE_INLINE_CAP;
}

static void free_var_shape_set_init(FreeVarShapeSet *set) {
    set->items = set->inline_items;
    set->len = 0;
    set->cap = BODY_VISIBLE_INLINE_CAP;
    set->path_items = set->inline_path_items;
    set->path_len = 0;
    set->path_cap = BODY_VISIBLE_INLINE_CAP;
}

static void free_var_shape_set_free(FreeVarShapeSet *set) {
    if (set->items != set->inline_items)
        free(set->items);
    if (set->path_items != set->inline_path_items)
        free(set->path_items);
    set->items = set->inline_items;
    set->len = 0;
    set->cap = BODY_VISIBLE_INLINE_CAP;
    set->path_items = set->inline_path_items;
    set->path_len = 0;
    set->path_cap = BODY_VISIBLE_INLINE_CAP;
}

static void free_var_shape_set_move(FreeVarShapeSet *dst, FreeVarShapeSet *src) {
    dst->len = src->len;
    dst->cap = src->cap;
    if (src->items == src->inline_items) {
        dst->items = dst->inline_items;
        if (src->len > 0) {
            memcpy(dst->inline_items, src->inline_items,
                   sizeof(VisibleVarShapeRef) * src->len);
        }
    } else {
        dst->items = src->items;
    }
    dst->path_len = src->path_len;
    dst->path_cap = src->path_cap;
    if (src->path_items == src->inline_path_items) {
        dst->path_items = dst->inline_path_items;
        if (src->path_len > 0) {
            memcpy(dst->inline_path_items, src->inline_path_items,
                   sizeof(uint32_t) * src->path_len);
        }
    } else {
        dst->path_items = src->path_items;
    }
    src->items = src->inline_items;
    src->len = 0;
    src->cap = BODY_VISIBLE_INLINE_CAP;
    src->path_items = src->inline_path_items;
    src->path_len = 0;
    src->path_cap = BODY_VISIBLE_INLINE_CAP;
}

static void index_path_stack_init(IndexPathStack *stack) {
    stack->items = stack->inline_items;
    stack->len = 0;
    stack->cap = BODY_VISIBLE_INLINE_CAP;
}

static void index_path_stack_free(IndexPathStack *stack) {
    if (stack->items != stack->inline_items)
        free(stack->items);
    stack->items = stack->inline_items;
    stack->len = 0;
    stack->cap = BODY_VISIBLE_INLINE_CAP;
}

static bool index_path_stack_reserve(IndexPathStack *stack, uint32_t needed) {
    if (needed <= stack->cap)
        return true;
    uint32_t next_cap = stack->cap ? stack->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    uint32_t *next = stack->items == stack->inline_items
        ? cetta_malloc(sizeof(uint32_t) * next_cap)
        : cetta_realloc(stack->items, sizeof(uint32_t) * next_cap);
    if (stack->items == stack->inline_items && stack->len > 0)
        memcpy(next, stack->items, sizeof(uint32_t) * stack->len);
    stack->items = next;
    stack->cap = next_cap;
    return true;
}

static bool index_path_stack_push(IndexPathStack *stack, uint32_t index) {
    if (!index_path_stack_reserve(stack, stack->len + 1))
        return false;
    stack->items[stack->len++] = index;
    return true;
}

static void index_path_stack_pop(IndexPathStack *stack) {
    if (stack->len > 0)
        stack->len--;
}

static bool free_var_set_reserve(FreeVarSet *set, uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t next_cap = set->cap ? set->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    VisibleVarRef *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(VisibleVarRef) * next_cap)
        : cetta_realloc(set->items, sizeof(VisibleVarRef) * next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(VisibleVarRef) * set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool free_var_set_add(FreeVarSet *set, VarId var_id, SymbolId spelling) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].var_id == var_id)
            return true;
    }
    if (!free_var_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].var_id = var_id;
    set->items[set->len].spelling = spelling;
    set->len++;
    return true;
}

static bool free_var_shape_set_reserve(FreeVarShapeSet *set, uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t next_cap = set->cap ? set->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    VisibleVarShapeRef *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(VisibleVarShapeRef) * next_cap)
        : cetta_realloc(set->items, sizeof(VisibleVarShapeRef) * next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(VisibleVarShapeRef) * set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool free_var_shape_set_reserve_paths(FreeVarShapeSet *set, uint32_t needed) {
    if (needed <= set->path_cap)
        return true;
    uint32_t next_cap = set->path_cap ? set->path_cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    uint32_t *next = set->path_items == set->inline_path_items
        ? cetta_malloc(sizeof(uint32_t) * next_cap)
        : cetta_realloc(set->path_items, sizeof(uint32_t) * next_cap);
    if (set->path_items == set->inline_path_items && set->path_len > 0)
        memcpy(next, set->path_items, sizeof(uint32_t) * set->path_len);
    set->path_items = next;
    set->path_cap = next_cap;
    return true;
}

static bool free_var_shape_set_add(FreeVarShapeSet *set, uint32_t base_id,
                                   SymbolId spelling) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].base_id == base_id && set->items[i].spelling == spelling)
            return true;
    }
    if (!free_var_shape_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].base_id = base_id;
    set->items[set->len].spelling = spelling;
    set->items[set->len].path_offset = BODY_VISIBLE_PATH_UNSET;
    set->items[set->len].path_len = 0;
    set->len++;
    return true;
}

static bool free_var_shape_set_store_path(FreeVarShapeSet *set, uint32_t index,
                                          const IndexPathStack *path) {
    if (index >= set->len)
        return false;
    if (!free_var_shape_set_reserve_paths(set, set->path_len + path->len))
        return false;
    set->items[index].path_offset = set->path_len;
    set->items[index].path_len = path->len;
    if (path->len > 0) {
        memcpy(set->path_items + set->path_len, path->items,
               sizeof(uint32_t) * path->len);
        set->path_len += path->len;
    }
    return true;
}

static bool free_var_shape_set_from_visible(const FreeVarSet *visible,
                                            FreeVarShapeSet *shape) {
    free_var_shape_set_init(shape);
    for (uint32_t i = 0; i < visible->len; i++) {
        if (!free_var_shape_set_add(shape,
                                    var_base_id(visible->items[i].var_id),
                                    visible->items[i].spelling)) {
            free_var_shape_set_free(shape);
            return false;
        }
    }
    return true;
}

static bool free_var_shape_set_capture_paths_rec(Atom *atom,
                                                 FreeVarShapeSet *shape,
                                                 IndexPathStack *path,
                                                 uint32_t *found_count) {
    if (!atom || !atom_has_vars(atom) || *found_count == shape->len)
        return true;
    if (atom->kind == ATOM_VAR) {
        uint32_t base_id = var_base_id(atom->var_id);
        for (uint32_t i = 0; i < shape->len; i++) {
            if (shape->items[i].path_offset != BODY_VISIBLE_PATH_UNSET)
                continue;
            if (shape->items[i].base_id == base_id &&
                shape->items[i].spelling == atom->sym_id) {
                if (!free_var_shape_set_store_path(shape, i, path))
                    return false;
                (*found_count)++;
                break;
            }
        }
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!index_path_stack_push(path, i))
            return false;
        if (!free_var_shape_set_capture_paths_rec(atom->expr.elems[i], shape, path,
                                                  found_count)) {
            index_path_stack_pop(path);
            return false;
        }
        index_path_stack_pop(path);
        if (*found_count == shape->len)
            return true;
    }
    return true;
}

static bool free_var_shape_set_capture_paths(Atom *body, FreeVarShapeSet *shape) {
    IndexPathStack path;
    uint32_t found_count = 0;
    for (uint32_t i = 0; i < shape->len; i++) {
        shape->items[i].path_offset = BODY_VISIBLE_PATH_UNSET;
        shape->items[i].path_len = 0;
    }
    index_path_stack_init(&path);
    bool ok = free_var_shape_set_capture_paths_rec(body, shape, &path, &found_count);
    index_path_stack_free(&path);
    return ok && found_count == shape->len;
}

static uint32_t atom_shape_hash(const Atom *atom) {
    if (!atom)
        return 0;
    if (!atom_has_vars(atom))
        return atom_hash((Atom *)atom);
    uint32_t h = 5381u;
    h = ((h << 5) + h) ^ (uint32_t)atom->kind;
    switch (atom->kind) {
    case ATOM_VAR:
        h = ((h << 5) + h) ^ (uint32_t)var_base_id(atom->var_id);
        h = ((h << 5) + h) ^ atom->sym_id;
        return h;
    case ATOM_EXPR:
        h = ((h << 5) + h) ^ atom->expr.len;
        for (uint32_t i = 0; i < atom->expr.len; i++)
            h = ((h << 5) + h) ^ atom_shape_hash(atom->expr.elems[i]);
        return h;
    default:
        return atom_hash((Atom *)atom);
    }
}

static bool atom_shape_eq(const Atom *lhs, const Atom *rhs) {
    if (lhs == rhs)
        return true;
    if (!lhs || !rhs || lhs->kind != rhs->kind)
        return false;
    if (!atom_has_vars(lhs) && !atom_has_vars(rhs))
        return atom_eq((Atom *)lhs, (Atom *)rhs);
    switch (lhs->kind) {
    case ATOM_VAR:
        return lhs->sym_id == rhs->sym_id &&
               var_base_id(lhs->var_id) == var_base_id(rhs->var_id);
    case ATOM_EXPR:
        if (lhs->expr.len != rhs->expr.len)
            return false;
        for (uint32_t i = 0; i < lhs->expr.len; i++) {
            if (!atom_shape_eq(lhs->expr.elems[i], rhs->expr.elems[i]))
                return false;
        }
        return true;
    default:
        return atom_eq((Atom *)lhs, (Atom *)rhs);
    }
}

static bool body_visible_free_var_cache_init(void) {
    if (g_body_visible_free_var_cache_arena_ready)
        return true;
    arena_init(&g_body_visible_free_var_cache_arena);
    g_body_visible_free_var_cache_arena_ready = true;
    return true;
}

static __attribute__((unused)) const FreeVarShapeSet *
body_visible_free_var_cache_lookup(Atom *body) {
    if (!body || !g_body_visible_free_var_cache_arena_ready)
        return NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BODY_VISIBLE_CACHE_LOOKUP);
    uint32_t hash = atom_shape_hash(body);
    uint32_t slot = hash % BODY_VISIBLE_FREE_VAR_CACHE_CAP;
    for (uint32_t probe = 0; probe < BODY_VISIBLE_FREE_VAR_CACHE_CAP; probe++) {
        BodyVisibleFreeVarCacheEntry *entry =
            &g_body_visible_free_var_cache[(slot + probe) % BODY_VISIBLE_FREE_VAR_CACHE_CAP];
        if (!entry->occupied)
            return NULL;
        if (entry->body_shape_hash == hash && atom_shape_eq(entry->body_key, body)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BODY_VISIBLE_CACHE_HIT);
            return &entry->vars;
        }
    }
    return NULL;
}

static __attribute__((unused)) const FreeVarShapeSet *
body_visible_free_var_cache_store(Atom *body,
                                  const FreeVarSet *computed) {
    FreeVarShapeSet shape;
    if (!body || !body_visible_free_var_cache_init())
        return NULL;
    if (!free_var_shape_set_from_visible(computed, &shape))
        return NULL;
    if (!free_var_shape_set_capture_paths(body, &shape)) {
        free_var_shape_set_free(&shape);
        return NULL;
    }
    uint32_t hash = atom_shape_hash(body);
    uint32_t slot = hash % BODY_VISIBLE_FREE_VAR_CACHE_CAP;
    BodyVisibleFreeVarCacheEntry *target = NULL;
    for (uint32_t probe = 0; probe < BODY_VISIBLE_FREE_VAR_CACHE_CAP; probe++) {
        BodyVisibleFreeVarCacheEntry *entry =
            &g_body_visible_free_var_cache[(slot + probe) % BODY_VISIBLE_FREE_VAR_CACHE_CAP];
        if (!entry->occupied) {
            target = entry;
            break;
        }
        if (entry->body_shape_hash == hash && atom_shape_eq(entry->body_key, body)) {
            free_var_shape_set_free(&entry->vars);
            target = entry;
            break;
        }
    }
    if (!target)
        target = &g_body_visible_free_var_cache[slot];
    if (target->occupied)
        free_var_shape_set_free(&target->vars);
    target->occupied = true;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BODY_VISIBLE_CACHE_STORE);
    target->body_shape_hash = hash;
    target->body_key = atom_deep_copy(&g_body_visible_free_var_cache_arena, body);
    free_var_shape_set_move(&target->vars, &shape);
    return &target->vars;
}

static bool collect_body_visible_refs_for_shape_rec(Atom *atom,
                                                    const FreeVarShapeSet *shape,
                                                    FreeVarSet *out) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (out->len == shape->len)
        return true;
    if (atom->kind == ATOM_VAR) {
        uint32_t base_id = var_base_id(atom->var_id);
        for (uint32_t i = 0; i < shape->len; i++) {
            if (shape->items[i].base_id == base_id &&
                shape->items[i].spelling == atom->sym_id) {
                return free_var_set_add(out, atom->var_id, atom->sym_id);
            }
        }
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_body_visible_refs_for_shape_rec(atom->expr.elems[i], shape, out))
            return false;
        if (out->len == shape->len)
            return true;
    }
    return true;
}

static __attribute__((unused)) bool
collect_body_visible_refs_for_shape(Atom *body,
                                    const FreeVarShapeSet *shape,
                                    FreeVarSet *out) {
    bool have_paths = true;
    for (uint32_t i = 0; i < shape->len; i++) {
        if (shape->items[i].path_offset == BODY_VISIBLE_PATH_UNSET) {
            have_paths = false;
            break;
        }
    }
    if (have_paths) {
        free_var_set_init(out);
        for (uint32_t i = 0; i < shape->len; i++) {
            const VisibleVarShapeRef *ref = &shape->items[i];
            Atom *cursor = body;
            if (ref->path_offset + ref->path_len > shape->path_len) {
                free_var_set_free(out);
                have_paths = false;
                break;
            }
            for (uint32_t j = 0; j < ref->path_len; j++) {
                uint32_t child_index = shape->path_items[ref->path_offset + j];
                if (!cursor || cursor->kind != ATOM_EXPR ||
                    child_index >= cursor->expr.len) {
                    free_var_set_free(out);
                    have_paths = false;
                    break;
                }
                cursor = cursor->expr.elems[child_index];
            }
            if (!have_paths)
                break;
            if (!cursor || cursor->kind != ATOM_VAR ||
                var_base_id(cursor->var_id) != ref->base_id ||
                cursor->sym_id != ref->spelling ||
                !free_var_set_add(out, cursor->var_id, cursor->sym_id)) {
                free_var_set_free(out);
                have_paths = false;
                break;
            }
        }
        if (have_paths && out->len == shape->len)
            return true;
    }

    free_var_set_init(out);
    if (!collect_body_visible_refs_for_shape_rec(body, shape, out)) {
        free_var_set_free(out);
        return false;
    }
    return out->len == shape->len;
}

static __attribute__((unused)) void bound_var_stack_init(BoundVarStack *stack) {
    stack->ids = stack->inline_ids;
    stack->len = 0;
    stack->cap = BODY_VISIBLE_INLINE_CAP;
}

static __attribute__((unused)) void bound_var_stack_free(BoundVarStack *stack) {
    if (stack->ids != stack->inline_ids)
        free(stack->ids);
    stack->ids = stack->inline_ids;
    stack->len = 0;
    stack->cap = BODY_VISIBLE_INLINE_CAP;
}

static bool bound_var_stack_reserve(BoundVarStack *stack, uint32_t needed) {
    if (needed <= stack->cap)
        return true;
    uint32_t next_cap = stack->cap ? stack->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    VarId *next = stack->ids == stack->inline_ids
        ? cetta_malloc(sizeof(VarId) * next_cap)
        : cetta_realloc(stack->ids, sizeof(VarId) * next_cap);
    if (stack->ids == stack->inline_ids && stack->len > 0)
        memcpy(next, stack->ids, sizeof(VarId) * stack->len);
    stack->ids = next;
    stack->cap = next_cap;
    return true;
}

static bool bound_var_stack_push(BoundVarStack *stack, VarId var_id) {
    if (!bound_var_stack_reserve(stack, stack->len + 1))
        return false;
    stack->ids[stack->len++] = var_id;
    return true;
}

static bool bound_var_stack_contains(const BoundVarStack *stack, VarId var_id) {
    for (uint32_t i = stack->len; i > 0; i--) {
        if (stack->ids[i - 1] == var_id)
            return true;
    }
    return false;
}

static bool collect_bound_pattern_vars(Atom *atom, BoundVarStack *bound) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR)
        return bound_var_stack_push(bound, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_bound_pattern_vars(atom->expr.elems[i], bound))
            return false;
    }
    return true;
}

static bool collect_bound_pattern_ref_vars(Atom *atom,
                                           const BoundVarStack *bound,
                                           FreeVarSet *free_vars) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR) {
        if (!bound_var_stack_contains(bound, atom->var_id))
            return true;
        return free_var_set_add(free_vars, atom->var_id, atom->sym_id);
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_bound_pattern_ref_vars(atom->expr.elems[i], bound, free_vars))
            return false;
    }
    return true;
}

static bool collect_structural_vars_rec(Atom *atom, FreeVarSet *free_vars) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR)
        return free_var_set_add(free_vars, atom->var_id, atom->sym_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_structural_vars_rec(atom->expr.elems[i], free_vars))
            return false;
    }
    return true;
}

static bool collect_free_vars_rec(Atom *atom, BoundVarStack *bound, FreeVarSet *free_vars);

static __attribute__((unused)) bool
collect_external_pattern_visible_vars(Atom *atom,
                                      const BoundVarStack *visible,
                                      FreeVarSet *free_vars) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR) {
        if (!bound_var_stack_contains(visible, atom->var_id))
            return true;
        return free_var_set_add(free_vars, atom->var_id, atom->sym_id);
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_external_pattern_visible_vars(atom->expr.elems[i], visible, free_vars))
            return false;
    }
    return true;
}

static __attribute__((unused)) bool collect_external_pattern_refs_case_like_branches(
    Atom *branches, const BoundVarStack *visible, FreeVarSet *free_vars);

static __attribute__((unused)) bool
collect_external_pattern_refs_rec(Atom *atom,
                                  const BoundVarStack *visible,
                                  FreeVarSet *free_vars) {
    if (!atom || atom->kind != ATOM_EXPR)
        return true;

    uint32_t nargs = expr_nargs(atom);
    SymbolId head_id = atom_head_symbol_id(atom);

    if (head_id == g_builtin_syms.quote && nargs == 1)
        return collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars);

    if (head_id == g_builtin_syms.let && nargs == 3) {
        if (!collect_external_pattern_refs_rec(expr_arg(atom, 1), visible, free_vars))
            return false;
        if (!collect_external_pattern_visible_vars(expr_arg(atom, 0), visible, free_vars))
            return false;
        return collect_external_pattern_refs_rec(expr_arg(atom, 2), visible, free_vars);
    }

    if (head_id == g_builtin_syms.let_star && nargs == 2) {
        Atom *bindings_list = expr_arg(atom, 0);
        Atom *body = expr_arg(atom, 1);
        if (bindings_list && bindings_list->kind == ATOM_EXPR) {
            for (uint32_t i = 0; i < bindings_list->expr.len; i++) {
                Atom *binding = bindings_list->expr.elems[i];
                if (binding->kind == ATOM_EXPR && binding->expr.len == 2) {
                    if (!collect_external_pattern_refs_rec(binding->expr.elems[1], visible,
                                                           free_vars)) {
                        return false;
                    }
                    if (!collect_external_pattern_visible_vars(binding->expr.elems[0], visible,
                                                               free_vars)) {
                        return false;
                    }
                    continue;
                }
                if (!collect_external_pattern_refs_rec(binding, visible, free_vars))
                    return false;
            }
        }
        return collect_external_pattern_refs_rec(body, visible, free_vars);
    }

    if (head_id == g_builtin_syms.chain && nargs == 3) {
        if (!collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars))
            return false;
        if (!collect_external_pattern_visible_vars(expr_arg(atom, 1), visible, free_vars))
            return false;
        return collect_external_pattern_refs_rec(expr_arg(atom, 2), visible, free_vars);
    }

    if (head_id == g_builtin_syms.unify && nargs == 4) {
        if (!collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars))
            return false;
        if (!collect_external_pattern_visible_vars(expr_arg(atom, 1), visible, free_vars))
            return false;
        if (!collect_external_pattern_refs_rec(expr_arg(atom, 2), visible, free_vars))
            return false;
        return collect_external_pattern_refs_rec(expr_arg(atom, 3), visible, free_vars);
    }

    if ((head_id == g_builtin_syms.case_text ||
         head_id == g_builtin_syms.switch_text ||
         head_id == g_builtin_syms.switch_minimal) &&
        nargs == 2) {
        if (!collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars))
            return false;
        return collect_external_pattern_refs_case_like_branches(expr_arg(atom, 1), visible,
                                                                free_vars);
    }

    if (head_id == g_builtin_syms.match && nargs == 3) {
        if (!collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars))
            return false;
        if (!collect_external_pattern_visible_vars(expr_arg(atom, 1), visible, free_vars))
            return false;
        return collect_external_pattern_refs_rec(expr_arg(atom, 2), visible, free_vars);
    }

    if ((head_id == g_builtin_syms.fold || head_id == g_builtin_syms.reduce) &&
        (nargs == 5 || nargs == 6)) {
        uint32_t stream_idx = nargs == 6 ? 1 : 0;
        uint32_t init_idx = stream_idx + 1;
        uint32_t acc_idx = stream_idx + 2;
        uint32_t item_idx = stream_idx + 3;
        uint32_t step_idx = stream_idx + 4;
        if (nargs == 6 &&
            !collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars)) {
            return false;
        }
        if (!collect_external_pattern_refs_rec(expr_arg(atom, stream_idx), visible, free_vars) ||
            !collect_external_pattern_refs_rec(expr_arg(atom, init_idx), visible, free_vars) ||
            !collect_external_pattern_visible_vars(expr_arg(atom, acc_idx), visible, free_vars) ||
            !collect_external_pattern_visible_vars(expr_arg(atom, item_idx), visible, free_vars)) {
            return false;
        }
        return collect_external_pattern_refs_rec(expr_arg(atom, step_idx), visible, free_vars);
    }

    if (head_id == g_builtin_syms.fold_by_key && (nargs == 6 || nargs == 7)) {
        uint32_t stream_idx = nargs == 7 ? 1 : 0;
        uint32_t init_idx = stream_idx + 1;
        uint32_t acc_idx = stream_idx + 2;
        uint32_t item_idx = stream_idx + 3;
        uint32_t key_idx = stream_idx + 4;
        uint32_t step_idx = stream_idx + 5;
        if (nargs == 7 &&
            !collect_external_pattern_refs_rec(expr_arg(atom, 0), visible, free_vars)) {
            return false;
        }
        if (!collect_external_pattern_refs_rec(expr_arg(atom, stream_idx), visible, free_vars) ||
            !collect_external_pattern_refs_rec(expr_arg(atom, init_idx), visible, free_vars) ||
            !collect_external_pattern_visible_vars(expr_arg(atom, item_idx), visible, free_vars) ||
            !collect_external_pattern_refs_rec(expr_arg(atom, key_idx), visible, free_vars) ||
            !collect_external_pattern_visible_vars(expr_arg(atom, acc_idx), visible, free_vars) ||
            !collect_external_pattern_visible_vars(expr_arg(atom, item_idx), visible, free_vars)) {
            return false;
        }
        return collect_external_pattern_refs_rec(expr_arg(atom, step_idx), visible, free_vars);
    }

    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_external_pattern_refs_rec(atom->expr.elems[i], visible, free_vars))
            return false;
    }
    return true;
}

static __attribute__((unused)) bool collect_external_pattern_refs_case_like_branches(
    Atom *branches, const BoundVarStack *visible, FreeVarSet *free_vars) {
    if (!branches)
        return true;
    if (branches->kind != ATOM_EXPR)
        return collect_external_pattern_refs_rec(branches, visible, free_vars);
    for (uint32_t i = 0; i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
            if (!collect_external_pattern_visible_vars(branch->expr.elems[0], visible,
                                                       free_vars) ||
                !collect_external_pattern_refs_rec(branch->expr.elems[1], visible,
                                                   free_vars)) {
                return false;
            }
            continue;
        }
        if (!collect_external_pattern_refs_rec(branch, visible, free_vars))
            return false;
    }
    return true;
}

static bool collect_free_vars_case_like_branches(Atom *branches,
                                                 BoundVarStack *bound,
                                                 FreeVarSet *free_vars) {
    if (!branches)
        return true;
    if (branches->kind != ATOM_EXPR)
        return collect_free_vars_rec(branches, bound, free_vars);
    for (uint32_t i = 0; i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
            uint32_t mark = bound->len;
            if (!collect_bound_pattern_ref_vars(branch->expr.elems[0], bound, free_vars))
                return false;
            if (!collect_bound_pattern_vars(branch->expr.elems[0], bound))
                return false;
            if (!collect_free_vars_rec(branch->expr.elems[1], bound, free_vars)) {
                bound->len = mark;
                return false;
            }
            bound->len = mark;
            continue;
        }
        if (!collect_free_vars_rec(branch, bound, free_vars))
            return false;
    }
    return true;
}

static bool *alloc_zeroed_bool_array(uint32_t len) {
    if (len == 0)
        return NULL;
    size_t bytes = sizeof(bool) * (size_t)len;
    bool *used = cetta_malloc(bytes);
    memset(used, 0, bytes);
    return used;
}

static bool collect_free_vars_let_star(Atom *bindings_list, Atom *body,
                                       BoundVarStack *bound,
                                       FreeVarSet *free_vars) {
    uint32_t mark = bound->len;
    if (bindings_list && bindings_list->kind == ATOM_EXPR) {
        for (uint32_t i = 0; i < bindings_list->expr.len; i++) {
            Atom *binding = bindings_list->expr.elems[i];
            if (binding->kind == ATOM_EXPR && binding->expr.len == 2) {
                if (!collect_free_vars_rec(binding->expr.elems[1], bound, free_vars)) {
                    bound->len = mark;
                    return false;
                }
                if (!collect_bound_pattern_ref_vars(binding->expr.elems[0], bound, free_vars)) {
                    bound->len = mark;
                    return false;
                }
                if (!collect_bound_pattern_vars(binding->expr.elems[0], bound)) {
                    bound->len = mark;
                    return false;
                }
                continue;
            }
            if (!collect_free_vars_rec(binding, bound, free_vars)) {
                bound->len = mark;
                return false;
            }
        }
    }
    if (!collect_free_vars_rec(body, bound, free_vars)) {
        bound->len = mark;
        return false;
    }
    bound->len = mark;
    return true;
}

static bool collect_free_vars_fold(Atom *atom, uint32_t nargs,
                                   BoundVarStack *bound,
                                   FreeVarSet *free_vars) {
    uint32_t stream_idx = nargs == 6 ? 1 : 0;
    uint32_t init_idx = stream_idx + 1;
    uint32_t acc_idx = stream_idx + 2;
    uint32_t item_idx = stream_idx + 3;
    uint32_t step_idx = stream_idx + 4;
    if (nargs == 6 && !collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
        return false;
    if (!collect_free_vars_rec(expr_arg(atom, stream_idx), bound, free_vars) ||
        !collect_free_vars_rec(expr_arg(atom, init_idx), bound, free_vars)) {
        return false;
    }
    if (!collect_bound_pattern_ref_vars(expr_arg(atom, acc_idx), bound, free_vars) ||
        !collect_bound_pattern_ref_vars(expr_arg(atom, item_idx), bound, free_vars)) {
        return false;
    }
    uint32_t mark = bound->len;
    if (!collect_bound_pattern_vars(expr_arg(atom, acc_idx), bound) ||
        !collect_bound_pattern_vars(expr_arg(atom, item_idx), bound)) {
        bound->len = mark;
        return false;
    }
    if (!collect_free_vars_rec(expr_arg(atom, step_idx), bound, free_vars)) {
        bound->len = mark;
        return false;
    }
    bound->len = mark;
    return true;
}

static bool collect_free_vars_fold_by_key(Atom *atom, uint32_t nargs,
                                          BoundVarStack *bound,
                                          FreeVarSet *free_vars) {
    uint32_t stream_idx = nargs == 7 ? 1 : 0;
    uint32_t init_idx = stream_idx + 1;
    uint32_t acc_idx = stream_idx + 2;
    uint32_t item_idx = stream_idx + 3;
    uint32_t key_idx = stream_idx + 4;
    uint32_t step_idx = stream_idx + 5;
    if (nargs == 7 && !collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
        return false;
    if (!collect_free_vars_rec(expr_arg(atom, stream_idx), bound, free_vars) ||
        !collect_free_vars_rec(expr_arg(atom, init_idx), bound, free_vars)) {
        return false;
    }

    if (!collect_bound_pattern_ref_vars(expr_arg(atom, item_idx), bound, free_vars))
        return false;
    uint32_t key_mark = bound->len;
    if (!collect_bound_pattern_vars(expr_arg(atom, item_idx), bound)) {
        bound->len = key_mark;
        return false;
    }
    if (!collect_free_vars_rec(expr_arg(atom, key_idx), bound, free_vars)) {
        bound->len = key_mark;
        return false;
    }
    bound->len = key_mark;

    if (!collect_bound_pattern_ref_vars(expr_arg(atom, acc_idx), bound, free_vars) ||
        !collect_bound_pattern_ref_vars(expr_arg(atom, item_idx), bound, free_vars)) {
        return false;
    }
    uint32_t step_mark = bound->len;
    if (!collect_bound_pattern_vars(expr_arg(atom, acc_idx), bound) ||
        !collect_bound_pattern_vars(expr_arg(atom, item_idx), bound)) {
        bound->len = step_mark;
        return false;
    }
    if (!collect_free_vars_rec(expr_arg(atom, step_idx), bound, free_vars)) {
        bound->len = step_mark;
        return false;
    }
    bound->len = step_mark;
    return true;
}

static bool collect_free_vars_rec(Atom *atom, BoundVarStack *bound, FreeVarSet *free_vars) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR) {
        if (bound_var_stack_contains(bound, atom->var_id))
            return true;
        return free_var_set_add(free_vars, atom->var_id, atom->sym_id);
    }
    if (atom->kind != ATOM_EXPR)
        return true;

    uint32_t nargs = expr_nargs(atom);
    SymbolId head_id = atom_head_symbol_id(atom);

    if (head_id == g_builtin_syms.quote && nargs == 1)
        return collect_structural_vars_rec(expr_arg(atom, 0), free_vars);

    if (head_id == g_builtin_syms.let && nargs == 3) {
        if (!collect_free_vars_rec(expr_arg(atom, 1), bound, free_vars))
            return false;
        /* Binder patterns may reference already-bound outer vars, but fresh
           pattern vars remain local binders rather than free references. */
        if (!collect_bound_pattern_ref_vars(expr_arg(atom, 0), bound, free_vars))
            return false;
        uint32_t mark = bound->len;
        if (!collect_bound_pattern_vars(expr_arg(atom, 0), bound)) {
            bound->len = mark;
            return false;
        }
        if (!collect_free_vars_rec(expr_arg(atom, 2), bound, free_vars)) {
            bound->len = mark;
            return false;
        }
        bound->len = mark;
        return true;
    }

    if (head_id == g_builtin_syms.let_star && nargs == 2)
        return collect_free_vars_let_star(expr_arg(atom, 0), expr_arg(atom, 1),
                                          bound, free_vars);

    if (head_id == g_builtin_syms.chain && nargs == 3) {
        if (!collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
            return false;
        if (!collect_bound_pattern_ref_vars(expr_arg(atom, 1), bound, free_vars))
            return false;
        uint32_t mark = bound->len;
        if (!collect_bound_pattern_vars(expr_arg(atom, 1), bound)) {
            bound->len = mark;
            return false;
        }
        if (!collect_free_vars_rec(expr_arg(atom, 2), bound, free_vars)) {
            bound->len = mark;
            return false;
        }
        bound->len = mark;
        return true;
    }

    if (head_id == g_builtin_syms.unify && nargs == 4) {
        if (!collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
            return false;
        if (!collect_bound_pattern_ref_vars(expr_arg(atom, 1), bound, free_vars))
            return false;
        uint32_t mark = bound->len;
        if (!collect_bound_pattern_vars(expr_arg(atom, 1), bound)) {
            bound->len = mark;
            return false;
        }
        if (!collect_free_vars_rec(expr_arg(atom, 2), bound, free_vars)) {
            bound->len = mark;
            return false;
        }
        bound->len = mark;
        return collect_free_vars_rec(expr_arg(atom, 3), bound, free_vars);
    }

    if ((head_id == g_builtin_syms.case_text ||
         head_id == g_builtin_syms.switch_text ||
         head_id == g_builtin_syms.switch_minimal) &&
        nargs == 2) {
        if (!collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
            return false;
        return collect_free_vars_case_like_branches(expr_arg(atom, 1), bound, free_vars);
    }

    if (head_id == g_builtin_syms.match && nargs == 3) {
        if (!collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
            return false;
        if (!collect_bound_pattern_ref_vars(expr_arg(atom, 1), bound, free_vars))
            return false;
        uint32_t mark = bound->len;
        if (!collect_bound_pattern_vars(expr_arg(atom, 1), bound)) {
            bound->len = mark;
            return false;
        }
        if (!collect_free_vars_rec(expr_arg(atom, 2), bound, free_vars)) {
            bound->len = mark;
            return false;
        }
        bound->len = mark;
        return true;
    }

    if ((head_id == g_builtin_syms.fold || head_id == g_builtin_syms.reduce) &&
        (nargs == 5 || nargs == 6))
        return collect_free_vars_fold(atom, nargs, bound, free_vars);

    if (head_id == g_builtin_syms.fold_by_key && (nargs == 6 || nargs == 7))
        return collect_free_vars_fold_by_key(atom, nargs, bound, free_vars);

    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_free_vars_rec(atom->expr.elems[i], bound, free_vars))
            return false;
    }
    return true;
}

static __attribute__((unused)) Atom *
bindings_apply_without_self(Bindings *full, Arena *a,
                            VarId skip_id, Atom *value) {
    if (!value || !atom_contains_vars(value) || !full || full->len == 0)
        return value;
    Bindings reduced;
    if (!bindings_clone(&reduced, full))
        return value;
    bool removed = false;
    for (uint32_t i = 0; i < reduced.len; i++) {
        if (reduced.entries[i].var_id != skip_id)
            continue;
        for (uint32_t j = i + 1; j < reduced.len; j++)
            reduced.entries[j - 1] = reduced.entries[j];
        reduced.len--;
        reduced.lookup_cache_count = 0;
        reduced.lookup_cache_next = 0;
        removed = true;
        break;
    }
    Atom *resolved = removed
        ? bindings_apply_if_vars(&reduced, a, value)
        : bindings_apply_if_vars(full, a, value);
    bindings_free(&reduced);
    return resolved;
}

static __attribute__((unused)) Atom *
bindings_resolve_body_visible_var(Arena *a, const Bindings *full,
                                  const VisibleVarRef *wanted) {
    Atom *exact = bindings_lookup_id((Bindings *)full, wanted->var_id);
    if (exact) {
        if (!atom_contains_vars(exact))
            return exact;
        return bindings_apply_without_self((Bindings *)full, a, wanted->var_id, exact);
    }

    Atom *slot_var =
        atom_var_with_spelling(a, wanted->spelling, wanted->var_id);
    Atom *resolved = bindings_apply_if_vars(full, a, slot_var);
    if (resolved != slot_var)
        return resolved;

    return slot_var;
}

static Atom *bindings_rewrite_body_visible_var(Arena *a, Atom *var,
                                               void *ctx) {
    const Bindings *full = (const Bindings *)ctx;
    if (!var || var->kind != ATOM_VAR || !full)
        return var;
    VisibleVarRef wanted = {
        .var_id = var->var_id,
        .spelling = var->sym_id,
    };
    return bindings_resolve_body_visible_var(a, full, &wanted);
}

static Atom *bindings_apply_body_visible_vars(const Bindings *full, Arena *a,
                                              Atom *body) {
    if (!full || !body || !atom_contains_vars(body))
        return body;
    return bindings_apply_rewrite_vars((Bindings *)full, a, body,
                                       bindings_rewrite_body_visible_var,
                                       (void *)full);
}

static bool collect_pattern_vars_simple_rec(Atom *atom, VarId *ids,
                                            SymbolId *spellings,
                                            uint32_t *len, uint32_t cap) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR) {
        for (uint32_t i = 0; i < *len; i++) {
            if (ids[i] == atom->var_id)
                return true;
        }
        if (*len >= cap)
            return false;
        ids[*len] = atom->var_id;
        spellings[*len] = atom->sym_id;
        (*len)++;
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_pattern_vars_simple_rec(atom->expr.elems[i], ids, spellings,
                                             len, cap)) {
            return false;
        }
    }
    return true;
}

static bool bindings_project_body_visible_env(Arena *a, Atom *body,
                                              const Bindings *full, Bindings *out) {
    VarId body_ids[64];
    SymbolId body_spellings[64];
    uint32_t nbody = 0;

    bindings_init(out);
    if (!full || full->len == 0)
        return true;
    if (!atom_contains_vars(body))
        return true;
    if (!collect_pattern_vars_simple_rec(body, body_ids, body_spellings, &nbody, 64))
        return false;
    if (nbody == 0)
        return true;
    for (uint32_t i = 0; i < nbody; i++) {
        VisibleVarRef wanted = {
            .var_id = body_ids[i],
            .spelling = body_spellings[i],
        };
        Atom *resolved = bindings_resolve_body_visible_var(a, full, &wanted);
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == wanted.var_id &&
            resolved->sym_id == wanted.spelling) {
            continue;
        }
        if (!bindings_add_id(out, wanted.var_id, wanted.spelling, resolved)) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static Atom *bindings_apply_projected_body_visible(const Bindings *visible,
                                                   Arena *a, Atom *body) {
    if (!visible || visible->len == 0 || !body || !atom_has_vars(body))
        return body;
    if (body->kind == ATOM_VAR) {
        Atom *val = bindings_lookup_id((Bindings *)visible, body->var_id);
        return val ? val : body;
    }
    if (body->kind != ATOM_EXPR)
        return body;

    Atom **new_elems = NULL;
    for (uint32_t i = 0; i < body->expr.len; i++) {
        Atom *child = body->expr.elems[i];
        Atom *next = atom_has_vars(child)
            ? bindings_apply_projected_body_visible(visible, a, child)
            : child;
        if (!next)
            return NULL;
        if (!new_elems && next != child) {
            new_elems = arena_alloc(a, sizeof(Atom *) * body->expr.len);
            for (uint32_t j = 0; j < i; j++)
                new_elems[j] = body->expr.elems[j];
        }
        if (new_elems)
            new_elems[i] = next;
    }
    return new_elems ? atom_expr(a, new_elems, body->expr.len) : body;
}

static bool bindings_builder_merge_commit(BindingsBuilder *dst,
                                          const Bindings *src) {
    if (!bindings_builder_try_merge(dst, src))
        return false;
    bindings_builder_commit(dst);
    return true;
}

static bool atom_list_contains_alpha_eq(Atom **items, uint32_t len,
                                        Atom *candidate) {
    for (uint32_t i = 0; i < len; i++) {
        if (atom_alpha_eq(items[i], candidate))
            return true;
    }
    return false;
}

static void outcome_set_add_unique_alpha(OutcomeSet *os, Arena *a, Atom *atom,
                                         const Bindings *env) {
    for (uint32_t i = 0; i < os->len; i++) {
        Atom *existing = outcome_atom_materialize(a, &os->items[i]);
        if (atom_alpha_eq(existing, atom))
            return;
    }
    outcome_set_add(os, atom, env);
}

static bool tail_apply_bindings_to_next(Arena *a, Atom **tail_next,
                                        Atom **tail_type, Bindings *tail_env,
                                        bool *changed_out) {
    bool changed = false;
    if (changed_out)
        *changed_out = false;
    if (!tail_env || (tail_env->len == 0 && tail_env->eq_len == 0))
        return true;
    if (tail_env->eq_len != 0)
        return false;
    if (tail_next && *tail_next) {
        Atom *applied = bindings_apply_if_vars(tail_env, a, *tail_next);
        if (applied != *tail_next)
            changed = true;
        *tail_next = applied;
    }
    if (tail_type && *tail_type) {
        Atom *applied = bindings_apply_if_vars(tail_env, a, *tail_type);
        if (applied != *tail_type)
            changed = true;
        *tail_type = applied;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SAFE_POINT_COUNT);
    cetta_runtime_stats_update_max(
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_ENTRIES_PEAK,
        tail_env->len);
    bindings_free(tail_env);
    bindings_init(tail_env);
    if (changed_out)
        *changed_out = changed;
    return true;
}

static bool petta_var_spelling_is_internal(SymbolId spelling) {
    const char *name = symbol_bytes(g_symbols, spelling);
    return name &&
           (strncmp(name, "__petta_", 8) == 0 ||
            strncmp(name, "$__petta_", 9) == 0);
}

static bool atom_contains_petta_internal_var(Atom *atom) {
    if (!atom)
        return false;
    if (atom->kind == ATOM_VAR)
        return petta_var_spelling_is_internal(atom->sym_id);
    if (atom->kind != ATOM_EXPR)
        return false;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (atom_contains_petta_internal_var(atom->expr.elems[i]))
            return true;
    }
    return false;
}

static bool bindings_project_tail_env(Arena *a, Atom *next_atom,
                                      const Bindings *full,
                                      Bindings *out) {
    bindings_init(out);
    if (!full || (full->len == 0 && full->eq_len == 0))
        return true;
    if (full->eq_len != 0)
        return bindings_copy(out, full);
    uint32_t full_len = full->len;

    Bindings visible;
    if (!bindings_project_body_visible_env(a, next_atom, full, &visible))
        return false;
    for (uint32_t i = 0; i < visible.len; i++) {
        if (!bindings_add_id(out, visible.entries[i].var_id,
                             visible.entries[i].spelling,
                             visible.entries[i].val)) {
            bindings_free(&visible);
            bindings_free(out);
            return false;
        }
    }
    bindings_free(&visible);

    if (!language_is_petta())
        return true;

    for (uint32_t i = 0; i < full->len; i++) {
        if (!petta_var_spelling_is_internal(full->entries[i].spelling) &&
            !atom_contains_petta_internal_var(full->entries[i].val)) {
            continue;
        }
        Atom *projected = full->entries[i].val;
        if (atom_contains_vars(projected)) {
            projected = bindings_apply_without_self((Bindings *)full, a,
                                                    full->entries[i].var_id,
                                                    projected);
        }
        if (!bindings_add_id(out, full->entries[i].var_id,
                             full->entries[i].spelling, projected)) {
            bindings_free(out);
            return false;
        }
    }
    if (out->len < full_len) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_SAFE_POINT_COUNT);
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_ENTRIES_PEAK,
            (uint64_t)(full_len - out->len));
    }
    return true;
}

typedef struct {
    const Bindings *env;
    Bindings owned;
    uint32_t mark;
    bool used_builder;
} BindingsMergeAttempt;

static __attribute__((unused)) bool
bindings_builder_merge_or_clone(BindingsBuilder *builder,
                                const Bindings *base,
                                const Bindings *extra,
                                BindingsMergeAttempt *attempt) {
    attempt->env = NULL;
    bindings_init(&attempt->owned);
    attempt->mark = bindings_builder_save(builder);
    attempt->used_builder = false;
    if (bindings_builder_try_merge(builder, extra)) {
        attempt->env = bindings_builder_bindings(builder);
        attempt->used_builder = true;
        return true;
    }
    bindings_builder_rollback(builder, attempt->mark);
    if (!bindings_try_merge_live(&attempt->owned, base) ||
        !bindings_try_merge_live(&attempt->owned, extra)) {
        bindings_free(&attempt->owned);
        return false;
    }
    attempt->env = &attempt->owned;
    return true;
}

static __attribute__((unused)) void
bindings_merge_attempt_finish(BindingsBuilder *builder,
                              BindingsMergeAttempt *attempt) {
    if (attempt->used_builder) {
        bindings_builder_rollback(builder, attempt->mark);
    } else {
        bindings_free(&attempt->owned);
    }
}

static void eval_for_caller(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                            const Bindings *prefix, bool preserve_bindings,
                            OutcomeSet *os);

typedef struct {
    Space *space;
    Arena *arena;
    Atom *declared_type;
    int fuel;
    const Bindings *base_env;
    bool preserve_bindings;
    SearchContext *context;
    OutcomeSet *outcomes;
    EvalQueryEpisode *episode;
} QueryEvalVisitorCtx;

static void eval_delayed_outcome_for_caller(Space *s, Arena *a,
                                            Atom *declared_type,
                                            Outcome *seed, int fuel,
                                            bool preserve_bindings,
                                            OutcomeSet *outcomes) {
    Atom *head = NULL;
    Atom *preview = outcome_preview_atom(seed);
    if (!seed || !preview)
        return;

    if (atom_is_empty(preview) || outcome_atom_is_error(a, seed) ||
        preview->kind != ATOM_EXPR || preview->expr.len == 0) {
        outcome_set_add_prefixed_outcome(a, outcomes, seed, NULL, preserve_bindings);
        return;
    }
    head = preview->expr.elems[0];
    if (head && head->kind == ATOM_SYMBOL &&
        !symbol_id_is_builtin_surface(head->sym_id) &&
        !is_grounded_op(head->sym_id) &&
        !(g_library_context && g_library_context->foreign_runtime &&
          cetta_foreign_is_callable_head(g_library_context->foreign_runtime,
                                         head)) &&
        !space_equations_may_match_known_head(s, head->sym_id)) {
        outcome_set_add_prefixed_outcome(a, outcomes, seed, NULL, preserve_bindings);
        return;
    }

    Atom *variant_applied = outcome_atom_materialize_variant_only(a, seed);
    if (!variant_applied)
        return;
    eval_for_caller(s, a, declared_type, variant_applied, fuel,
                    &seed->env, preserve_bindings, outcomes);
}

static bool query_visit_eval_for_caller_common(Atom *result,
                                               const Bindings *bindings,
                                               const VariantInstance *variant,
                                               void *ctx) {
    QueryEvalVisitorCtx *query_eval = ctx;
    BindingsMergeAttempt attempt;
    if (!bindings_builder_merge_or_clone(search_context_builder(query_eval->context),
                                         query_eval->base_env,
                                         bindings,
                                         &attempt)) {
        return true;
    }

    Outcome delayed;
    outcome_init(&delayed);
    delayed.atom = result;
    if (!bindings_clone(&delayed.env, attempt.env) ||
        (variant && !variant_instance_clone(&delayed.variant, variant))) {
        outcome_free_fields(&delayed);
        bindings_merge_attempt_finish(search_context_builder(query_eval->context),
                                      &attempt);
        return true;
    }
    if (query_eval->episode) {
        Arena *generated_arena = eval_query_episode_generated(query_eval->episode);
        Arena *payload_arena =
            outcome_set_payload_arena(query_eval->outcomes, query_eval->arena);
        OutcomeSet generated;
        size_t before_bytes = payload_arena ? payload_arena->live_bytes : 0;
        outcome_set_init(&generated);
        if (generated_arena) {
            ArenaMark generated_mark = arena_mark(generated_arena);
            eval_delayed_outcome_for_caller(
                query_eval->space,
                generated_arena,
                result_eval_type_hint(query_eval->declared_type, result),
                &delayed,
                query_eval->fuel,
                query_eval->preserve_bindings,
                &generated);
            outcome_set_append_promoted(query_eval->arena,
                                        query_eval->outcomes,
                                        &generated,
                                        query_eval->preserve_bindings);
            if (generated.len > 0) {
                cetta_runtime_stats_add(
                    CETTA_RUNTIME_COUNTER_QUERY_EPISODE_DELAYED_OUTCOME_SURVIVOR_COUNT,
                    generated.len);
                eval_query_episode_note_answer_promotion(payload_arena,
                                                         before_bytes);
            }
            outcome_set_free(&generated);
            arena_reset(generated_arena, generated_mark);
        } else {
            eval_delayed_outcome_for_caller(
                query_eval->space,
                query_eval->arena,
                result_eval_type_hint(query_eval->declared_type, result),
                &delayed,
                query_eval->fuel,
                query_eval->preserve_bindings,
                query_eval->outcomes);
            outcome_set_free(&generated);
        }
    } else {
        eval_delayed_outcome_for_caller(
            query_eval->space,
            query_eval->arena,
            result_eval_type_hint(query_eval->declared_type, result),
            &delayed,
            query_eval->fuel,
            query_eval->preserve_bindings,
            query_eval->outcomes);
    }
    outcome_free_fields(&delayed);
    bindings_merge_attempt_finish(search_context_builder(query_eval->context),
                                  &attempt);
    return true;
}

static bool query_visit_eval_for_caller(Atom *result, const Bindings *bindings,
                                        void *ctx) {
    return query_visit_eval_for_caller_common(result, bindings, NULL, ctx);
}

static bool query_visit_eval_for_caller_answer_ref_common(
    const AnswerBank *bank,
    AnswerRef ref,
    const CettaVarMap *goal_instantiation,
    void *ctx) {
    QueryEvalVisitorCtx *query_eval = ctx;
    Outcome delayed;
    Arena *generated_arena = NULL;
    Arena *seed_owner = NULL;
    if (!query_eval || !bank || ref == CETTA_ANSWER_REF_NONE)
        return false;
    if (query_eval->episode)
        generated_arena = eval_query_episode_generated(query_eval->episode);
    seed_owner = generated_arena ? generated_arena : query_eval->arena;
    if (!outcome_init_answer_ref(seed_owner,
                                 &delayed,
                                 bank,
                                 ref,
                                 goal_instantiation,
                                 query_eval->base_env)) {
        return true;
    }

    if (query_eval->episode) {
        Arena *payload_arena =
            outcome_set_payload_arena(query_eval->outcomes, query_eval->arena);
        OutcomeSet generated;
        size_t before_bytes = payload_arena ? payload_arena->live_bytes : 0;
        outcome_set_init(&generated);
        if (generated_arena) {
            ArenaMark generated_mark = arena_mark(generated_arena);
            eval_delayed_outcome_for_caller(
                query_eval->space,
                generated_arena,
                query_eval->declared_type,
                &delayed,
                query_eval->fuel,
                query_eval->preserve_bindings,
                &generated);
            outcome_set_append_promoted(query_eval->arena,
                                        query_eval->outcomes,
                                        &generated,
                                        query_eval->preserve_bindings);
            if (generated.len > 0) {
                cetta_runtime_stats_add(
                    CETTA_RUNTIME_COUNTER_QUERY_EPISODE_DELAYED_OUTCOME_SURVIVOR_COUNT,
                    generated.len);
                eval_query_episode_note_answer_promotion(payload_arena,
                                                         before_bytes);
            }
            outcome_set_free(&generated);
            arena_reset(generated_arena, generated_mark);
        } else {
            eval_delayed_outcome_for_caller(
                query_eval->space,
                query_eval->arena,
                query_eval->declared_type,
                &delayed,
                query_eval->fuel,
                query_eval->preserve_bindings,
                query_eval->outcomes);
            outcome_set_free(&generated);
        }
    } else {
        eval_delayed_outcome_for_caller(
            query_eval->space,
            query_eval->arena,
            query_eval->declared_type,
            &delayed,
            query_eval->fuel,
            query_eval->preserve_bindings,
            query_eval->outcomes);
    }
    outcome_free_fields(&delayed);
    return true;
}

typedef struct {
    QueryEvalVisitorCtx *query_eval;
    TableQueryHandle *table_handle;
    bool table_ok;
    CettaVarMap goal_instantiation;
    bool goal_ready;
    Arena *goal_owner;
} QueryCachedVisitStreamCtx;

static bool query_visit_eval_for_caller_stage(Atom *result,
                                              const Bindings *bindings,
                                              void *ctx) {
    QueryCachedVisitStreamCtx *stream = ctx;
    AnswerRef ref = CETTA_ANSWER_REF_NONE;
    if (!stream || !stream->query_eval)
        return false;
    if (stream->table_handle && stream->table_handle->impl && stream->table_ok) {
        if (!table_store_add_answer(stream->table_handle, result, bindings, &ref))
            stream->table_ok = false;
    }
    (void)ref;
    return query_visit_eval_for_caller(result, bindings, stream->query_eval);
}

typedef enum {
    QUERY_TABLE_TAIL_MISS = 0,
    QUERY_TABLE_TAIL_EMPTY,
    QUERY_TABLE_TAIL_SINGLE,
    QUERY_TABLE_TAIL_MULTI,
} QueryTableTailState;

typedef struct {
    SearchContext *context;
    const Bindings *base_env;
    Arena *arena;
    Atom *declared_type;
    Atom **tail_next;
    Atom **tail_type;
    Bindings *tail_env;
    EvalQueryEpisode *episode;
    EvalLocalTailSurvivor *local_survivor;
    bool ok;
} QuerySingleTailDelayedCtx;

static bool query_delayed_result_apply_single_tail(EvalQueryEpisode *episode,
                                                   EvalLocalTailSurvivor *local_survivor,
                                                   SearchContext *context,
                                                   const Bindings *base_env,
                                                   Arena *a,
                                                   Atom *declared_type,
                                                   Atom *result,
                                                   const Bindings *bindings,
                                                   const VariantInstance *variant,
                                                   Atom **tail_next,
                                                   Atom **tail_type,
                                                   Bindings *tail_env) {
    BindingsMergeAttempt attempt;
    Atom *variant_applied;
    uint32_t local_slot = 0;
    bool using_local_survivor = false;
    size_t before_bytes;
    Arena *result_arena = eval_query_episode_result_arena(episode, a);
    if (episode && local_survivor) {
        result_arena = eval_local_tail_survivor_prepare(local_survivor,
                                                        &local_slot);
        episode->survivor_arena = result_arena;
        using_local_survivor = result_arena != NULL;
    }
    before_bytes = eval_query_episode_survivor_live_bytes(episode);
    if (!bindings_builder_merge_or_clone(search_context_builder(context),
                                         base_env,
                                         bindings,
                                         &attempt)) {
        if (using_local_survivor)
            eval_local_tail_survivor_abort(local_survivor, local_slot);
        return false;
    }
    if (variant && variant_instance_present(variant)) {
        variant_applied = variant_instance_materialize(result_arena, result, variant);
    } else if (episode) {
        variant_applied = eval_query_episode_promote_atom(episode, result);
    } else {
        variant_applied = result;
    }
    if (!variant_applied) {
        if (using_local_survivor)
            eval_local_tail_survivor_abort(local_survivor, local_slot);
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    *tail_next = variant_applied;
    *tail_type = result_eval_type_hint(declared_type, result);
    if (using_local_survivor && *tail_type)
        *tail_type = atom_deep_copy(result_arena, *tail_type);
    bindings_copy(tail_env, attempt.env);
    if (episode && !eval_query_episode_promote_bindings(episode, tail_env)) {
        if (using_local_survivor)
            eval_local_tail_survivor_abort(local_survivor, local_slot);
        bindings_free(tail_env);
        bindings_init(tail_env);
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    bindings_merge_attempt_finish(search_context_builder(context), &attempt);
    if (episode)
        eval_query_episode_note_answer_promotion(
            result_arena, before_bytes);
    return true;
}

static bool query_answer_ref_apply_single_tail(EvalQueryEpisode *episode,
                                               EvalLocalTailSurvivor *local_survivor,
                                               SearchContext *context,
                                               const Bindings *base_env,
                                               Arena *a,
                                               Atom *declared_type,
                                               const AnswerBank *bank,
                                               AnswerRef ref,
                                               const CettaVarMap *goal_instantiation,
                                               Atom **tail_next,
                                               Atom **tail_type,
                                               Bindings *tail_env) {
    Atom *result = NULL;
    Bindings materialized;
    VariantInstance replay_variant;
    bool ok = false;

    bindings_init(&materialized);
    variant_instance_init(&replay_variant);
    if (!bank || ref == CETTA_ANSWER_REF_NONE || !goal_instantiation)
        goto done;
    if (!table_store_materialize_answer_ref(bank, ref, a, goal_instantiation,
                                            &result, &materialized,
                                            &replay_variant)) {
        goto done;
    }
    ok = query_delayed_result_apply_single_tail(episode,
                                                local_survivor,
                                                context,
                                                base_env,
                                                a,
                                                declared_type,
                                                result,
                                                &materialized,
                                                &replay_variant,
                                                tail_next,
                                                tail_type,
                                                tail_env);

done:
    variant_instance_free(&replay_variant);
    bindings_free(&materialized);
    return ok;
}

typedef struct {
    Arena *goal_owner;
    const AnswerBank *first_bank;
    AnswerRef first_ref;
    CettaVarMap goal_instantiation;
    bool first_ready;
    bool ok;
} QuerySingleTailAnswerRefCtx;

static bool query_visit_collect_single_tail_answer_ref(
    const AnswerBank *bank,
    AnswerRef ref,
    const CettaVarMap *goal_instantiation,
    void *ctx) {
    QuerySingleTailAnswerRefCtx *collect = ctx;
    if (!collect || !collect->ok)
        return false;
    if (collect->first_ready)
        return false;
    if (!bank || ref == CETTA_ANSWER_REF_NONE || !goal_instantiation) {
        collect->ok = false;
        return false;
    }
    collect->first_bank = bank;
    collect->first_ref = ref;
    if (!cetta_var_map_clone_live(collect->goal_owner,
                                  &collect->goal_instantiation,
                                  goal_instantiation)) {
        collect->ok = false;
        return false;
    }
    collect->first_ready = true;
    return true;
}

static bool query_equations_table_hit_visit(Space *s, Atom *query, Arena *a,
                                            QueryEvalVisitorCtx *query_eval,
                                            uint32_t *visited_out) {
    TableStore *table = eval_active_episode_table();
    if (!table)
        return false;
    return table_store_lookup_visit_delayed(table, s, space_revision(s), query,
                                            a,
                                            query_visit_eval_for_caller_common,
                                            query_eval, visited_out);
}

static QueryTableTailState
query_equations_table_hit_single_tail(Space *s, Atom *query,
                                      EvalQueryEpisode *episode, Arena *a,
                                      QueryEvalVisitorCtx *query_eval,
                                      EvalLocalTailSurvivor *local_survivor,
                                      Atom **tail_next,
                                      Atom **tail_type,
                                      Bindings *tail_env) {
    TableStore *table = eval_active_episode_table();
    uint32_t visited = 0;
    QuerySingleTailAnswerRefCtx collect = {
        .goal_owner = a,
        .first_bank = NULL,
        .first_ref = CETTA_ANSWER_REF_NONE,
        .ok = true,
    };
    cetta_var_map_init(&collect.goal_instantiation);

    if (!table)
        return QUERY_TABLE_TAIL_MISS;
    if (!table_store_lookup_visit_ref(table, s, space_revision(s), query, a,
                                      query_visit_collect_single_tail_answer_ref,
                                      &collect, &visited)) {
        cetta_var_map_free(&collect.goal_instantiation);
        return QUERY_TABLE_TAIL_MISS;
    }
    if (visited == 0) {
        cetta_var_map_free(&collect.goal_instantiation);
        return QUERY_TABLE_TAIL_EMPTY;
    }
    if (visited == 1 && collect.ok && collect.first_ready) {
        /* Answer refs carry a goal-instantiation map whose bindings must be
           replayed through the ordinary visitor path.  The tail shortcut
           cannot safely substitute that delayed environment yet. */
        cetta_var_map_free(&collect.goal_instantiation);
        return QUERY_TABLE_TAIL_MULTI;
    }
    cetta_var_map_free(&collect.goal_instantiation);
    return QUERY_TABLE_TAIL_MULTI;
}

static uint32_t query_equations_cached_visit(Space *s, Atom *query, Arena *a,
                                             QueryEvalVisitorCtx *query_eval) {
    uint32_t visited = 0;
    __attribute__((cleanup(eval_query_episode_cleanup)))
    EvalQueryEpisode episode = {0};
    QueryEvalVisitorCtx episode_eval = *query_eval;
    Arena *query_arena = a;
    TableStore *table = eval_active_episode_table();
    TableQueryHandle cache_handle = {0};
    bool cache_started = false;
    QueryCachedVisitStreamCtx stream_ctx = {
        .query_eval = &episode_eval,
        .table_handle = NULL,
        .table_ok = true,
        .goal_ready = false,
        .goal_owner = NULL,
    };
    cetta_var_map_init(&stream_ctx.goal_instantiation);

    eval_query_episode_init(&episode);
    episode_eval.episode = &episode;
    query_arena = eval_query_episode_scratch(&episode);
    stream_ctx.goal_owner = query_arena;
    outcome_set_bind_owner_if_missing(
        episode_eval.outcomes,
        eval_query_episode_result_arena(&episode, query_eval->arena));

    if (query_equations_table_hit_visit(s, query, query_arena, &episode_eval,
                                        &visited))
        return visited;

    if (table &&
        table_store_begin_query(table, s, space_revision(s), query, &cache_handle)) {
        cache_started = true;
        stream_ctx.table_handle = &cache_handle;
    }

    visited = query_equations_visit(s, query, query_arena,
                                    query_visit_eval_for_caller_stage,
                                    &stream_ctx);
    if (cache_started) {
        if (stream_ctx.table_ok) {
            if (!table_store_commit_query(&cache_handle))
                table_store_abort_query(&cache_handle);
        } else {
            table_store_abort_query(&cache_handle);
        }
    }
    cetta_var_map_free(&stream_ctx.goal_instantiation);
    return visited;
}

static bool query_result_apply_single_tail(EvalQueryEpisode *episode,
                                           EvalLocalTailSurvivor *local_survivor,
                                           SearchContext *context,
                                           const Bindings *base_env,
                                           Arena *a,
                                           Atom *declared_type,
                                           const QueryResult *result,
                                           Atom **tail_next,
                                           Atom **tail_type,
                                           Bindings *tail_env) {
    BindingsMergeAttempt attempt;
    uint32_t local_slot = 0;
    bool using_local_survivor = false;
    size_t before_bytes;
    Arena *result_arena = eval_query_episode_result_arena(episode, a);
    if (episode && local_survivor) {
        result_arena =
            eval_local_tail_survivor_prepare(local_survivor, &local_slot);
        episode->survivor_arena = result_arena;
        using_local_survivor = result_arena != NULL;
    }
    before_bytes = eval_query_episode_survivor_live_bytes(episode);
    if (!bindings_builder_merge_or_clone(search_context_builder(context),
                                         base_env,
                                         &result->bindings,
                                         &attempt)) {
        if (using_local_survivor)
            eval_local_tail_survivor_abort(local_survivor, local_slot);
        return false;
    }
    *tail_next = episode
        ? eval_query_episode_promote_atom(episode, result->result)
        : result->result;
    if (!*tail_next) {
        if (using_local_survivor)
            eval_local_tail_survivor_abort(local_survivor, local_slot);
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    *tail_type = result_eval_type_hint(declared_type, result->result);
    if (using_local_survivor && *tail_type)
        *tail_type = atom_deep_copy(result_arena, *tail_type);
    bindings_copy(tail_env, attempt.env);
    if (episode && !eval_query_episode_promote_bindings(episode, tail_env)) {
        if (using_local_survivor)
            eval_local_tail_survivor_abort(local_survivor, local_slot);
        bindings_free(tail_env);
        bindings_init(tail_env);
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    bindings_merge_attempt_finish(search_context_builder(context), &attempt);
    if (episode)
        eval_query_episode_note_answer_promotion(
            result_arena, before_bytes);
    return true;
}

typedef struct {
    EvalQueryEpisode *episode;
    SearchContext *context;
    const Bindings *base_env;
    Arena *arena;
    Atom *declared_type;
    bool preserve_bindings;
    QueryEvalVisitorCtx *query_eval;
    TableQueryHandle *table_handle;
    const AnswerBank *answer_bank;
    Arena *goal_owner;
    CettaVarMap goal_instantiation;
    bool goal_ready;
    bool table_ok;
    uint32_t count;
    AnswerRef first_ref;
    bool first_is_ref;
    Atom *first_result;
    Bindings first_bindings;
    bool first_ready;
} QueryMissSingleTailStreamCtx;

static void query_miss_single_tail_stream_ctx_init(
    QueryMissSingleTailStreamCtx *ctx,
    EvalQueryEpisode *episode,
    SearchContext *context,
    const Bindings *base_env,
    Arena *arena,
    Atom *declared_type,
    bool preserve_bindings,
    QueryEvalVisitorCtx *query_eval,
    TableQueryHandle *table_handle,
    const AnswerBank *answer_bank,
    Arena *goal_owner) {
    if (!ctx)
        return;
    ctx->episode = episode;
    ctx->context = context;
    ctx->base_env = base_env;
    ctx->arena = arena;
    ctx->declared_type = declared_type;
    ctx->preserve_bindings = preserve_bindings;
    ctx->query_eval = query_eval;
    ctx->table_handle = table_handle;
    ctx->answer_bank = answer_bank;
    ctx->goal_owner = goal_owner;
    cetta_var_map_init(&ctx->goal_instantiation);
    ctx->goal_ready = false;
    ctx->table_ok = true;
    ctx->count = 0;
    ctx->first_ref = CETTA_ANSWER_REF_NONE;
    ctx->first_is_ref = false;
    ctx->first_result = NULL;
    bindings_init(&ctx->first_bindings);
    ctx->first_ready = false;
}

static void query_miss_single_tail_release_first(
    QueryMissSingleTailStreamCtx *ctx) {
    if (!ctx)
        return;
    if (ctx->first_ready && !ctx->first_is_ref)
        bindings_free(&ctx->first_bindings);
    if (!ctx->first_is_ref)
        bindings_init(&ctx->first_bindings);
    ctx->first_ref = CETTA_ANSWER_REF_NONE;
    ctx->first_is_ref = false;
    ctx->first_result = NULL;
    ctx->first_ready = false;
}

static bool query_miss_single_tail_store_first(
    QueryMissSingleTailStreamCtx *ctx,
    Atom *result,
    const Bindings *bindings,
    AnswerRef ref) {
    if (!ctx)
        return false;
    ctx->count = 1;
    ctx->first_ready = true;
    (void)ref;
    ctx->first_result = result;
    if (!bindings_clone(&ctx->first_bindings, bindings)) {
        ctx->first_ready = false;
        ctx->count = 0;
        return false;
    }
    return true;
}

static bool query_miss_single_tail_publish_first(
    QueryMissSingleTailStreamCtx *ctx) {
    bool ok = true;
    if (!ctx || !ctx->first_ready)
        return true;
    if (ctx->first_is_ref) {
        ok = query_visit_eval_for_caller_answer_ref_common(
            ctx->answer_bank,
            ctx->first_ref,
            &ctx->goal_instantiation,
            ctx->query_eval);
    } else {
        ok = query_visit_eval_for_caller(ctx->first_result,
                                         &ctx->first_bindings,
                                         ctx->query_eval);
    }
    query_miss_single_tail_release_first(ctx);
    return ok;
}

static bool query_miss_single_tail_publish_current(
    QueryMissSingleTailStreamCtx *ctx,
    Atom *result,
    const Bindings *bindings,
    AnswerRef ref) {
    if (!ctx)
        return false;
    (void)ref;
    return query_visit_eval_for_caller(result, bindings, ctx->query_eval);
}

static void query_miss_single_tail_stream_ctx_free(
    QueryMissSingleTailStreamCtx *ctx) {
    if (!ctx)
        return;
    query_miss_single_tail_release_first(ctx);
    cetta_var_map_free(&ctx->goal_instantiation);
    ctx->goal_ready = false;
    ctx->count = 0;
}

static bool query_visit_stream_single_tail_miss(Atom *result,
                                                const Bindings *bindings,
                                                void *ctx) {
    QueryMissSingleTailStreamCtx *stream = ctx;
    AnswerRef ref = CETTA_ANSWER_REF_NONE;
    if (!stream || !stream->query_eval)
        return false;
    if (stream->table_handle && stream->table_handle->impl && stream->table_ok) {
        if (!table_store_add_answer(stream->table_handle, result, bindings, &ref))
            stream->table_ok = false;
    }
    if (stream->count == 0)
        return query_miss_single_tail_store_first(stream, result, bindings, ref);
    if (stream->count == 1 && stream->first_ready) {
        if (!query_miss_single_tail_publish_first(stream))
            return false;
    }
    stream->count++;
    return query_miss_single_tail_publish_current(stream, result, bindings, ref);
}

static QueryTableTailState
query_equations_miss_single_tail_stream(Space *s, Atom *query,
                                        EvalQueryEpisode *episode,
                                        Arena *query_arena,
                                        QueryEvalVisitorCtx *query_eval,
                                        EvalLocalTailSurvivor *local_survivor,
                                        bool allow_single_tail,
                                        Atom **tail_next,
                                        Atom **tail_type,
                                        Bindings *tail_env) {
    TableStore *table = eval_active_episode_table();
    TableQueryHandle cache_handle = {0};
    bool cache_started = false;
    QueryMissSingleTailStreamCtx stream;

    query_miss_single_tail_stream_ctx_init(&stream,
                                           episode,
                                           query_eval->context,
                                           query_eval->base_env,
                                           query_eval->arena,
                                           query_eval->declared_type,
                                           query_eval->preserve_bindings,
                                           query_eval,
                                           NULL,
                                           table ? table->answer_bank : NULL,
                                           query_arena);
    if (table &&
        table_store_begin_query(table, s, space_revision(s), query, &cache_handle)) {
        cache_started = true;
        stream.table_handle = &cache_handle;
    }

    (void)query_equations_visit(s, query, query_arena,
                                query_visit_stream_single_tail_miss,
                                &stream);

    if (cache_started) {
        if (stream.table_ok) {
            if (!table_store_commit_query(&cache_handle))
                table_store_abort_query(&cache_handle);
        } else {
            table_store_abort_query(&cache_handle);
        }
    }

    if (stream.count == 0) {
        query_miss_single_tail_stream_ctx_free(&stream);
        return QUERY_TABLE_TAIL_EMPTY;
    }
    if (stream.count == 1 && stream.first_ready) {
        if (!allow_single_tail || stream.first_is_ref) {
            bool keep_going = query_miss_single_tail_publish_first(&stream);
            query_miss_single_tail_stream_ctx_free(&stream);
            return keep_going ? QUERY_TABLE_TAIL_MULTI
                              : QUERY_TABLE_TAIL_EMPTY;
        }
        bool ok;
        if (stream.first_is_ref) {
            ok = query_answer_ref_apply_single_tail(episode,
                                                    local_survivor,
                                                    query_eval->context,
                                                    query_eval->base_env,
                                                    query_eval->arena,
                                                    query_eval->declared_type,
                                                    stream.answer_bank,
                                                    stream.first_ref,
                                                    &stream.goal_instantiation,
                                                    tail_next,
                                                    tail_type,
                                                    tail_env);
        } else {
            QueryResult first = {
                .result = stream.first_result,
                .bindings = stream.first_bindings,
            };
            ok = query_result_apply_single_tail(episode,
                                                local_survivor,
                                                query_eval->context,
                                                query_eval->base_env,
                                                query_eval->arena,
                                                query_eval->declared_type,
                                                &first,
                                                tail_next,
                                                tail_type,
                                                tail_env);
        }
        query_miss_single_tail_stream_ctx_free(&stream);
        if (!ok)
            return QUERY_TABLE_TAIL_EMPTY;
        return QUERY_TABLE_TAIL_SINGLE;
    }
    query_miss_single_tail_stream_ctx_free(&stream);
    return QUERY_TABLE_TAIL_MULTI;
}

static uint32_t get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types);

/* Forward-declared below with the rest of the evaluator entry points. */
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os);

typedef bool (*OrderedOutcomeVisitor)(Arena *a, Atom *atom,
                                      const Bindings *env, void *ctx);
static __attribute__((unused)) void
eval_for_current_caller(Space *s, Arena *a, Atom *type, Atom *atom,
                        int fuel, const Bindings *prefix,
                        const Bindings *outer_env,
                        bool preserve_bindings, OutcomeSet *os);

typedef struct {
    OrderedOutcomeVisitor visitor;
    void *ctx;
    uint32_t *visited;
    bool stopped;
} DirectWalkVisitorCtx;

typedef struct {
    Space *space;
    Arena *arena;
    Atom *templ;
    int fuel;
    DirectWalkVisitorCtx *walk;
} DirectWalkMorkCtx;

static bool direct_outcome_walk_mork_match_supported(Arena *a, Atom *atom) {
    if (!g_library_context || !atom || atom->kind != ATOM_EXPR || atom->expr.len != 4 ||
        !atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.mork_match_surface)) {
        return false;
    }
    Atom *space_arg = resolve_registry_refs(a, atom->expr.elems[1]);
    CettaMorkSpaceHandle *bridge = NULL;
    return cetta_library_lookup_explicit_mork_bridge(g_library_context, space_arg,
                                                     &bridge) && bridge;
}

static bool direct_outcome_walk_visit_inner(DirectWalkVisitorCtx *walk,
                                            Arena *a,
                                            OutcomeSet *inner) {
    for (uint32_t i = 0; i < inner->len; i++) {
        Atom *r = outcome_atom_materialize(a, &inner->items[i]);
        if (atom_is_empty(r))
            continue;
        (*walk->visited)++;
        if (!walk->visitor(a, r, &inner->items[i].env, walk->ctx)) {
            walk->stopped = true;
            return false;
        }
    }
    return true;
}

static bool direct_outcome_walk_mork_row(const Bindings *bindings, void *ctx) {
    DirectWalkMorkCtx *mork = ctx;
    if (mork->walk->stopped)
        return false;

    Bindings empty;
    bindings_init(&empty);
    Atom *row = bindings_apply_if_vars(bindings, mork->arena, mork->templ);
    OutcomeSet inner;
    outcome_set_init(&inner);
    eval_for_caller(mork->space, mork->arena, NULL, row, mork->fuel,
                    &empty, false, &inner);
    bool keep_going = direct_outcome_walk_visit_inner(mork->walk, mork->arena, &inner);
    outcome_set_free(&inner);
    return keep_going;
}

static bool direct_outcome_walk_mork_match(Space *s, Arena *a, Atom *atom, int fuel,
                                           OrderedOutcomeVisitor visitor, void *ctx,
                                           uint32_t *visited) {
    if (!direct_outcome_walk_mork_match_supported(a, atom))
        return false;

    Atom *space_arg = resolve_registry_refs(a, atom->expr.elems[1]);
    Atom *pattern = resolve_registry_refs(a, atom->expr.elems[2]);
    Atom *templ = resolve_registry_refs(a, atom->expr.elems[3]);
    CettaMorkSpaceHandle *bridge = NULL;
    if (!cetta_library_lookup_explicit_mork_bridge(g_library_context, space_arg, &bridge) ||
        !bridge) {
        return false;
    }

    DirectWalkVisitorCtx walk = {
        .visitor = visitor,
        .ctx = ctx,
        .visited = visited,
        .stopped = false,
    };
    DirectWalkMorkCtx mork = {
        .space = s,
        .arena = a,
        .templ = templ,
        .fuel = fuel,
        .walk = &walk,
    };

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        return space_match_backend_mork_visit_conjunction_direct(
            bridge, a, pattern->expr.elems + 1, pattern->expr.len - 1, NULL,
            direct_outcome_walk_mork_row, &mork);
    }
    return space_match_backend_mork_visit_bindings_direct(
        bridge, a, pattern, direct_outcome_walk_mork_row, &mork);
}

static bool direct_outcome_walk_supported(Space *s, Arena *a, Atom *atom, int fuel) {
    Atom *bound = registry_lookup_atom(atom);
    if (bound)
        atom = bound;
    atom = materialize_runtime_token(s, a, atom);
    if (atom_is_empty(atom) || atom_is_error(atom) || atom_eval_is_immediate_value(atom, fuel))
        return true;
    if (direct_outcome_walk_mork_match_supported(a, atom))
        return true;
    if (expr_head_is_id(atom, g_builtin_syms.superpose) && expr_nargs(atom) == 1) {
        Atom *list = expr_arg(atom, 0);
        if (list->kind != ATOM_EXPR)
            return false;
        for (uint32_t i = 0; i < list->expr.len; i++) {
            if (!direct_outcome_walk_supported(s, a, list->expr.elems[i], fuel))
                return false;
        }
        return true;
    }
    return false;
}

static bool direct_outcome_walk(Space *s, Arena *a, Atom *atom, int fuel,
                                OrderedOutcomeVisitor visitor, void *ctx,
                                uint32_t *visited) {
    Bindings empty;
    bindings_init(&empty);
    Atom *bound = registry_lookup_atom(atom);
    if (bound)
        atom = bound;
    atom = materialize_runtime_token(s, a, atom);
    if (atom_is_empty(atom)) {
        /* Match HE's internal sentinel behavior: Empty does not become a
           user-visible stream item on the native fast path. */
        return true;
    }
    if (atom_is_error(atom) || atom_eval_is_immediate_value(atom, fuel)) {
        (*visited)++;
        return visitor(a, atom, &empty, ctx);
    }
    if (direct_outcome_walk_mork_match_supported(a, atom)) {
        return direct_outcome_walk_mork_match(s, a, atom, fuel, visitor, ctx, visited);
    }
    if (expr_head_is_id(atom, g_builtin_syms.superpose) && expr_nargs(atom) == 1) {
        Atom *list = expr_arg(atom, 0);
        if (list->kind != ATOM_EXPR)
            return false;
        for (uint32_t i = 0; i < list->expr.len; i++) {
            if (!direct_outcome_walk(s, a, list->expr.elems[i], fuel, visitor, ctx, visited))
                return false;
        }
        return true;
    }
    return false;
}

static uint32_t outcome_set_visit_ordered(Arena *a, OutcomeSet *inner,
                                          CettaSearchPolicyOrder order,
                                          OrderedOutcomeVisitor visitor,
                                          void *ctx) {
    uint32_t visited = 0;
    bool sorted_order = order == CETTA_SEARCH_POLICY_ORDER_LEX ||
                        order == CETTA_SEARCH_POLICY_ORDER_SHORTLEX;

    if (sorted_order) {
        uint32_t candidate_cap = inner->len > 0 ? inner->len : 1;
        SearchEmitCandidate *candidates =
            arena_alloc(a, sizeof(SearchEmitCandidate) * candidate_cap);
        uint32_t candidate_len = 0;
        for (uint32_t i = 0; i < inner->len; i++) {
            Atom *r = outcome_atom_materialize(a, &inner->items[i]);
            if (atom_is_empty(r))
                continue;
            candidates[candidate_len].raw_atom = r;
            candidates[candidate_len].render_atom = r;
            candidates[candidate_len].env = &inner->items[i].env;
            candidates[candidate_len].key = atom_to_string(a, r);
            candidates[candidate_len].ordinal = candidate_len;
            candidate_len++;
        }
        qsort(candidates, candidate_len, sizeof(SearchEmitCandidate),
              order == CETTA_SEARCH_POLICY_ORDER_LEX
                  ? compare_stream_candidates
                  : compare_stream_candidates_shortlex);
        for (uint32_t i = 0; i < candidate_len; i++) {
            visited++;
            if (!visitor(a, candidates[i].raw_atom, candidates[i].env, ctx))
                break;
        }
        return visited;
    }

    if (order == CETTA_SEARCH_POLICY_ORDER_REVERSE) {
        for (uint32_t i = inner->len; i > 0; i--) {
            Atom *r = outcome_atom_materialize(a, &inner->items[i - 1]);
            if (atom_is_empty(r))
                continue;
            visited++;
            if (!visitor(a, r, &inner->items[i - 1].env, ctx))
                break;
        }
        return visited;
    }

    for (uint32_t i = 0; i < inner->len; i++) {
        Atom *r = outcome_atom_materialize(a, &inner->items[i]);
        if (atom_is_empty(r))
            continue;
        visited++;
        if (!visitor(a, r, &inner->items[i].env, ctx))
            break;
    }
    return visited;
}

static uint32_t metta_eval_bind_visit(Space *s, Arena *a, Atom *atom, int fuel,
                                      CettaSearchPolicyOrder order,
                                      OrderedOutcomeVisitor visitor,
                                      void *ctx) {
    if (order == CETTA_SEARCH_POLICY_ORDER_NATIVE &&
        direct_outcome_walk_supported(s, a, atom, fuel)) {
        uint32_t visited = 0;
        direct_outcome_walk(s, a, atom, fuel, visitor, ctx, &visited);
        return visited;
    }
    OutcomeSet inner;
    outcome_set_init(&inner);
    metta_eval_bind(s, a, atom, fuel, &inner);
    uint32_t visited = outcome_set_visit_ordered(a, &inner, order, visitor, ctx);
    outcome_set_free(&inner);
    return visited;
}

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} StreamItemBuffer;

static bool stream_item_buffer_push(StreamItemBuffer *buffer, Atom *item) {
    if (buffer->len >= buffer->cap) {
        uint32_t next_cap = buffer->cap ? buffer->cap * 2 : 8;
        buffer->items = cetta_realloc(buffer->items, sizeof(Atom *) * next_cap);
        buffer->cap = next_cap;
    }
    buffer->items[buffer->len++] = item;
    return true;
}

static void stream_item_buffer_free(StreamItemBuffer *buffer) {
    free(buffer->items);
    buffer->items = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

typedef struct {
    OutcomeSet *os;
} StreamFirstResultCtx;

static bool stream_visit_emit_first(Arena *a, Atom *atom, const Bindings *env, void *ctx) {
    (void)a;
    StreamFirstResultCtx *first = ctx;
    outcome_set_add(first->os, atom, env);
    return false;
}

typedef struct {
    Space *s;
    Arena *a;
    Atom *pat;
    Atom *body;
    Atom *type;
    int fuel;
    const Bindings *outer_env;
    bool preserve_bindings;
    bool body_closed;
    OutcomeSet *os;
    ResultSet errors;
    bool has_success;
    bool cut_seen;
} LetDirectVisitCtx;

static bool let_direct_branch_visit(Arena *a, Atom *atom,
                                    const Bindings *env, void *ctx) {
    (void)a;
    LetDirectVisitCtx *let_ctx = ctx;
    Bindings empty;
    bindings_init(&empty);
    if (atom_is_error(atom)) {
        result_set_add(&let_ctx->errors, atom);
        return true;
    }
    let_ctx->has_success = true;
    if (let_ctx->pat->kind == ATOM_VAR &&
        !let_ctx->preserve_bindings &&
        let_ctx->body_closed) {
        Atom *branch_type = language_is_petta() ? let_ctx->type : NULL;
        uint32_t before = let_ctx->os->len;
        eval_for_current_caller(let_ctx->s, let_ctx->a, branch_type, let_ctx->body,
                                let_ctx->fuel, &empty, let_ctx->outer_env,
                                false, let_ctx->os);
        if (language_is_petta() &&
            outcome_set_consume_control_since(let_ctx->os, before,
                                              CETTA_OUTCOME_CONTROL_CUT)) {
            let_ctx->cut_seen = true;
            return false;
        }
        return true;
    }

    BindingsBuilder b;
    if (!bindings_builder_init(&b, env))
        return true;

    bool ok = false;
    if (let_ctx->pat->kind == ATOM_VAR)
        ok = bindings_builder_add_var_fresh(&b, let_ctx->pat, atom);
    else
        ok = simple_match_builder(let_ctx->pat, atom, &b);
    if (ok) {
        const Bindings *bb = bindings_builder_bindings(&b);
        Atom *branch_type = language_is_petta() ? let_ctx->type : NULL;
        uint32_t before = let_ctx->os->len;
        eval_for_current_caller(let_ctx->s, let_ctx->a, branch_type, let_ctx->body,
                                let_ctx->fuel, bb, let_ctx->outer_env,
                                let_ctx->preserve_bindings, let_ctx->os);
        if (language_is_petta() &&
            outcome_set_consume_control_since(let_ctx->os, before,
                                              CETTA_OUTCOME_CONTROL_CUT)) {
            let_ctx->cut_seen = true;
            bindings_builder_free(&b);
            return false;
        }
    }
    bindings_builder_free(&b);
    return true;
}

typedef struct {
    StreamItemBuffer buffer;
    bool bounded;
    int64_t limit;
} StreamCollectCtx;

static bool stream_visit_collect(Arena *a, Atom *atom, const Bindings *env, void *ctx) {
    (void)a;
    (void)env;
    StreamCollectCtx *collect = ctx;
    if (!stream_item_buffer_push(&collect->buffer, atom))
        return false;
    if (collect->bounded && collect->limit > 0 &&
        collect->buffer.len >= (uint32_t)collect->limit) {
        return false;
    }
    return true;
}

static void stream_emit(Space *s, Arena *a, Atom *stream_expr, int fuel,
                        bool bounded, int64_t limit, bool preserve_bindings,
                        CettaSearchPolicyOrder order,
                        OutcomeSet *os) {
    Bindings _empty;
    bindings_init(&_empty);

    if (bounded && limit == 1) {
        StreamFirstResultCtx first = { .os = os };
        if (metta_eval_bind_visit(s, a, stream_expr, fuel, order,
                                  stream_visit_emit_first, &first) == 0) {
            outcome_set_add(os, atom_empty(a), &_empty);
        }
        return;
    }

    if (bounded && limit == 0) {
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    StreamCollectCtx collect = {0};
    collect.bounded = bounded;
    collect.limit = limit;
    (void)preserve_bindings;
    metta_eval_bind_visit(s, a, stream_expr, fuel, order, stream_visit_collect, &collect);

    Atom **items = arena_alloc(a, sizeof(Atom *) * (collect.buffer.len > 0 ? collect.buffer.len : 1));
    for (uint32_t i = 0; i < collect.buffer.len; i++)
        items[i] = collect.buffer.items[i];
    outcome_set_add(os, atom_expr(a, items, collect.buffer.len), &_empty);
    stream_item_buffer_free(&collect.buffer);
}

static bool eval_bound_single_with_scratch(Space *s, Arena *a, Arena *scratch,
                                           Atom *call, Atom *expr,
                                           SymbolId acc_spelling, Atom *acc_value,
                                           SymbolId item_spelling, Atom *item_value,
                                           int fuel, const char *no_result_error,
                                           const char *multi_result_error,
                                           Atom **result_out, Atom **error_out) {
    ArenaMark scratch_mark = arena_mark(scratch);
    Atom *bound_atom = cetta_fold_bind_step_atom(scratch, expr,
                                                 acc_spelling, acc_value,
                                                 item_spelling, item_value);
    ResultBindSet results;
    rb_set_init(&results);
    metta_eval_bind(s, scratch, bound_atom, fuel, &results);

    Atom *resolved = NULL;
    for (uint32_t i = 0; i < results.len; i++) {
        Atom *candidate = outcome_atom_materialize(scratch, &results.items[i]);
        if (atom_is_empty(candidate))
            continue;
        if (resolved) {
            *error_out = atom_error(a, call, atom_symbol(a, multi_result_error));
            rb_set_free(&results);
            arena_reset(scratch, scratch_mark);
            return false;
        }
        /* Eval results live in the per-top-level eval arena; do not share
           structure into the global hash-cons table from this ephemeral dst. */
        resolved = atom_deep_copy(a, candidate);
    }

    rb_set_free(&results);
    arena_reset(scratch, scratch_mark);

    if (!resolved) {
        *error_out = atom_error(a, call, atom_symbol(a, no_result_error));
        return false;
    }
    *result_out = resolved;
    return true;
}

typedef struct {
    Space *s;
    Arena *a;
    Arena stream_scratch;
    Atom *call;
    Atom *acc;
    SymbolId acc_spelling;
    SymbolId item_spelling;
    Atom *step_expr;
    int fuel;
    OutcomeSet *os;
    bool ok;
} ReduceStreamCtx;

static bool reduce_stream_visit(Arena *work_a, Atom *item, const Bindings *env, void *ctx) {
    (void)work_a;
    (void)env;
    ReduceStreamCtx *reduce = ctx;
    Bindings empty;
    bindings_init(&empty);
    Atom *next_acc = NULL;
    Atom *error = NULL;
    if (!eval_bound_single_with_scratch(reduce->s, reduce->a, &reduce->stream_scratch,
                                        reduce->call, reduce->step_expr,
                                        reduce->acc_spelling, reduce->acc,
                                        reduce->item_spelling, item,
                                        reduce->fuel,
                                        "ReduceStepNoResult",
                                        "ReduceStepMultipleResults",
                                        &next_acc, &error)) {
        outcome_set_add(reduce->os, error, &empty);
        reduce->ok = false;
        return false;
    }
    reduce->acc = next_acc;
    return true;
}

static bool reduce_stream_results(Space *s, Arena *a, Arena *work_a, Atom *call,
                                  Atom *stream_expr, CettaSearchPolicyOrder order,
                                  Atom *init, SymbolId acc_spelling,
                                  SymbolId item_spelling, Atom *step_expr,
                                  int fuel, OutcomeSet *os) {
    Bindings _empty;
    bindings_init(&_empty);

    ReduceStreamCtx reduce = {
        .s = s,
        .a = a,
        .call = call,
        .acc = init,
        .acc_spelling = acc_spelling,
        .item_spelling = item_spelling,
        .step_expr = step_expr,
        .fuel = fuel,
        .os = os,
        .ok = true,
    };
    arena_init(&reduce.stream_scratch);
    arena_set_runtime_kind(&reduce.stream_scratch,
                           CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    if (a->hashcons)
        arena_set_hashcons(&reduce.stream_scratch, a->hashcons);

    metta_eval_bind_visit(s, work_a, stream_expr, fuel, order, reduce_stream_visit, &reduce);

    arena_free(&reduce.stream_scratch);
    if (!reduce.ok)
        return false;
    outcome_set_add(os, reduce.acc, &_empty);
    return true;
}

typedef struct {
    Space *s;
    Arena *a;
    Arena stream_scratch;
    Atom *call;
    Atom *init;
    SymbolId acc_spelling;
    SymbolId item_spelling;
    Atom *key_expr;
    Atom *step_expr;
    int fuel;
    OutcomeSet *os;
    FoldByKeyTable *table;
    bool ok;
} FoldByKeyVisitCtx;

static bool fold_by_key_stream_visit(Arena *visit_a, Atom *item, const Bindings *env,
                                     void *raw_ctx) {
    (void)visit_a;
    (void)env;
    FoldByKeyVisitCtx *ctx = raw_ctx;
    Bindings empty;
    bindings_init(&empty);
    Atom *key_atom = NULL;
    Atom *error = NULL;
    if (!eval_bound_single_with_scratch(ctx->s, ctx->a, &ctx->stream_scratch,
                                        ctx->call, ctx->key_expr,
                                        SYMBOL_ID_NONE, NULL,
                                        ctx->item_spelling, item,
                                        ctx->fuel,
                                        "FoldByKeyKeyNoResult",
                                        "FoldByKeyKeyMultipleResults",
                                        &key_atom, &error)) {
        outcome_set_add(ctx->os, error, &empty);
        ctx->ok = false;
        return false;
    }
    uint32_t bucket_index = 0;
    bool inserted = fold_by_key_table_lookup_or_insert(ctx->table, key_atom,
                                                       atom_hash(key_atom),
                                                       &bucket_index);
    Atom *current_acc = inserted ? ctx->init : ctx->table->buckets[bucket_index].acc_atom;
    Atom *next_acc = NULL;
    error = NULL;
    if (!eval_bound_single_with_scratch(ctx->s, ctx->a, &ctx->stream_scratch,
                                        ctx->call, ctx->step_expr,
                                        ctx->acc_spelling, current_acc,
                                        ctx->item_spelling, item,
                                        ctx->fuel,
                                        "ReduceStepNoResult",
                                        "ReduceStepMultipleResults",
                                        &next_acc, &error)) {
        outcome_set_add(ctx->os, error, &empty);
        ctx->ok = false;
        return false;
    }
    ctx->table->buckets[bucket_index].acc_atom = next_acc;
    return true;
}

static __attribute__((unused)) bool
fold_by_key_stream_results(Space *s, Arena *a, Arena *work_a,
                           Atom *call, Atom *stream_expr,
                           CettaSearchPolicyOrder order,
                           Atom *init, SymbolId acc_spelling,
                           SymbolId item_spelling, Atom *key_expr,
                           Atom *step_expr, int fuel,
                           OutcomeSet *os) {
    Bindings _empty;
    bindings_init(&_empty);

    FoldByKeyTable table;
    fold_by_key_table_init(&table);

    FoldByKeyVisitCtx ctx = {
        .s = s,
        .a = a,
        .call = call,
        .init = init,
        .acc_spelling = acc_spelling,
        .item_spelling = item_spelling,
        .key_expr = key_expr,
        .step_expr = step_expr,
        .fuel = fuel,
        .os = os,
        .table = &table,
        .ok = true,
    };
    arena_init(&ctx.stream_scratch);
    arena_set_runtime_kind(&ctx.stream_scratch,
                           CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    if (a->hashcons)
        arena_set_hashcons(&ctx.stream_scratch, a->hashcons);

    metta_eval_bind_visit(s, work_a, stream_expr, fuel, order,
                          fold_by_key_stream_visit, &ctx);

    arena_free(&ctx.stream_scratch);
    if (!ctx.ok) {
        fold_by_key_table_free(&table);
        return false;
    }

    for (uint32_t i = 0; i < table.bucket_len; i++) {
        Atom **pair = arena_alloc(a, sizeof(Atom *) * 2);
        pair[0] = table.buckets[i].key_atom;
        pair[1] = table.buckets[i].acc_atom ? table.buckets[i].acc_atom : init;
        outcome_set_add(os, atom_expr(a, pair, 2), &_empty);
    }
    fold_by_key_table_free(&table);
    return true;
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
    if (!g_registry)
        return atom;
    if (!atom_has_registry_refs(atom)) {
        if (!atom || atom->kind != ATOM_SYMBOL)
            return atom;
        const char *bytes = symbol_bytes(g_symbols, atom->sym_id);
        if (!bytes || bytes[0] != '&')
            return atom;
    }
    bool changed = false;
    Atom *resolved = resolve_registry_refs_impl(a, atom, &changed);
    if (!changed) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_REGISTRY_RESOLVE_NOOP);
    } else if (atom->kind == ATOM_EXPR) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_REGISTRY_RESOLVE_REWRITE);
    }
    return resolved;
}

typedef struct {
    CettaMorkSpaceHandle *bridge;
    Arena scratch;
    Atom *error_atom;
    const char *error_message;
    bool emit_stats;
    bool emit_timing;
} MorkAddStreamVisitCtx;

static bool emit_mork_add_atoms_from_stream_source(Space *s, Arena *a,
                                                   Atom *call_atom,
                                                   Atom *space_arg,
                                                   Atom *stream_source,
                                                   const Bindings *current_env,
                                                   int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    if (!g_library_context) {
        return false;
    }

    CettaMorkSpaceHandle *bridge = NULL;
    if (!cetta_library_lookup_explicit_mork_bridge(g_library_context, space_arg, &bridge) ||
        !bridge) {
        return false;
    }

    MorkAddStreamVisitCtx ctx = {
        .bridge = bridge,
        .error_atom = NULL,
        .error_message = NULL,
        .emit_stats = cetta_runtime_stats_is_enabled(),
        .emit_timing = cetta_runtime_timing_is_enabled(),
    };
    ResultSet stream_items;
    result_set_init(&stream_items);
    uint64_t eval_started_ns = ctx.emit_timing ? eval_monotonic_ns() : 0;
    uint64_t insert_started_ns = 0;
    arena_init(&ctx.scratch);
    arena_set_runtime_kind(&ctx.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    if (a->hashcons)
        arena_set_hashcons(&ctx.scratch, a->hashcons);
    Atom *applied_stream = (!current_env || current_env->len == 0)
        ? stream_source
        : bindings_apply_if_vars(current_env, a, stream_source);
    metta_eval(s, a, NULL, applied_stream, fuel, &stream_items);
    uint64_t eval_finished_ns = ctx.emit_timing ? eval_monotonic_ns() : 0;
    if (ctx.emit_timing && eval_finished_ns >= eval_started_ns) {
        cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_STREAM_EVAL_NS,
                                eval_finished_ns - eval_started_ns);
    }
    insert_started_ns = ctx.emit_timing ? eval_finished_ns : 0;
    uint32_t total_items = 0;
    for (uint32_t i = 0; i < stream_items.len; i++) {
        if (atom_is_error(stream_items.items[i])) {
            ctx.error_atom = stream_items.items[i];
            break;
        }
        total_items++;
    }

    if (!ctx.error_atom && total_items > 0) {
        uint8_t *packet = NULL;
        size_t packet_len = 0;
        uint64_t packet_bytes = 0;
        uint64_t pack_ns = 0;
        uint64_t native_started_ns = ctx.emit_timing ? eval_monotonic_ns() : 0;
        const char *pack_error = NULL;

        if (!cetta_library_pack_mork_expr_batch(
                &ctx.scratch, stream_items.items, total_items,
                &packet, &packet_len, &packet_bytes,
                ctx.emit_timing ? &pack_ns : NULL, &pack_error)) {
            ctx.error_message = pack_error ? pack_error
                                           : "MORK expr-byte lowering failed";
        } else {
            if (ctx.emit_stats) {
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_CALL);
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_ITEMS,
                                        total_items);
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_PACKET_BYTES,
                                        packet_bytes);
            }
            if (ctx.emit_timing) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_PACK_NS,
                                        pack_ns);
            }
            uint64_t ffi_started_ns = ctx.emit_timing ? eval_monotonic_ns() : 0;
            bool ok = cetta_mork_bridge_space_add_expr_bytes_batch(
                ctx.bridge, packet, packet_len, NULL);
            if (ctx.emit_timing) {
                uint64_t ffi_finished_ns = eval_monotonic_ns();
                if (ffi_finished_ns >= ffi_started_ns) {
                    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_FFI_NS,
                                            ffi_finished_ns - ffi_started_ns);
                }
                if (ffi_finished_ns >= native_started_ns) {
                    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_NATIVE_NS,
                                            ffi_finished_ns - native_started_ns);
                }
            }
            if (!ok) {
                ctx.error_message = cetta_mork_bridge_last_error();
            }
        }
        free(packet);
    }

    if (ctx.emit_timing) {
        uint64_t insert_finished_ns = eval_monotonic_ns();
        if (insert_finished_ns >= insert_started_ns) {
            cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_STREAM_INSERT_NS,
                                    insert_finished_ns - insert_started_ns);
        }
    }
    result_set_free(&stream_items);
    arena_free(&ctx.scratch);

    if (ctx.error_atom) {
        outcome_set_add(os, ctx.error_atom, &empty);
        return true;
    }
    if (ctx.error_message) {
        outcome_set_add(os, atom_error(a, call_atom,
                                       atom_string(a, ctx.error_message)),
                        &empty);
        return true;
    }
    outcome_set_add(os, atom_unit(a), &empty);
    return true;
}

static bool emit_mork_add_atoms_from_collapse(Space *s, Arena *a, Atom *call_atom,
                                              Atom *space_arg, Atom *stream_arg,
                                              const Bindings *current_env,
                                              int fuel, OutcomeSet *os) {
    if (!expr_head_is_id(stream_arg, g_builtin_syms.collapse) ||
        expr_nargs(stream_arg) != 1) {
        return false;
    }
    return emit_mork_add_atoms_from_stream_source(
        s, a, call_atom, space_arg, expr_arg(stream_arg, 0),
        current_env, fuel, os);
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
    if (!space_match_backend_materialize_attached(src, dst))
        return NULL;
    Space *clone = arena_alloc(dst, sizeof(Space));
    space_init_with_universe(clone, src ? src->native.universe : NULL);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (uint32_t i = 0; i < space_length(src); i++) {
        AtomId atom_id = space_get_atom_id_at(src, i);
        if (atom_id != CETTA_ATOM_ID_NONE) {
            space_add_atom_id(clone, atom_id);
            continue;
        }
        if (!space_admit_atom(clone, dst, space_get_at(src, i))) {
            Atom *stored = space_store_atom(clone, dst, space_get_at(src, i));
            space_add(clone, stored);
        }
    }
    return clone;
}

void eval_release_temporary_spaces(void) {
    eval_release_episode_table();
    eval_release_outcome_variant_bank();
    eval_release_episode_survivor_arena();
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
    return eval_persistent_arena();
}

static const char *string_like_atom(Atom *atom) {
    if (!atom) return NULL;
    if (atom->kind == ATOM_SYMBOL) return atom_name_cstr(atom);
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return atom->ground.sval;
    }
    return NULL;
}

static const char *module_spec_atom(Atom *atom, char *buf, size_t buf_sz) {
    const char *spec = string_like_atom(atom);
    if (spec)
        return spec;
    Atom *head = atom && atom->kind == ATOM_EXPR && atom->expr.len == 2
                     ? atom->expr.elems[0]
                     : NULL;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2 ||
        !head || head->kind != ATOM_SYMBOL ||
        strcmp(atom_name_cstr(head), "library") != 0) {
        return NULL;
    }
    const char *name = string_like_atom(atom->expr.elems[1]);
    if (!name || !*name)
        return NULL;
    if (strncmp(name, "lib_", 4) == 0) {
        int n = snprintf(buf, buf_sz, "lib/%s", name);
        return n > 0 && (size_t)n < buf_sz ? buf : NULL;
    }
    return name;
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
    space_init_with_universe(ns, eval_current_term_universe());
    dest.space = ns;
    dest.bind_key = target->sym_id;
    dest.is_fresh = true;
    return dest;
}

static Space *resolve_include_destination(Arena *a, Atom *target, Atom **error_out) {
    if (!g_registry || !atom_is_registry_token(target)) {
        *error_out = atom_symbol(a, "include destination must be &self or an existing &name");
        return NULL;
    }
    Atom *resolved = registry_lookup_id(g_registry, target->sym_id);
    if (atom_is_mork_space_handle_value(resolved)) {
        *error_out = atom_string(a, "include does not operate on MorkSpace; use mork:include!");
        return NULL;
    }
    Space *space = resolve_space(g_registry, target);
    if (!space) {
        *error_out = atom_symbol(a, "include destination must be &self or an existing &name");
    }
    return space;
}

static Space *resolve_runtime_space_ref(Space *context, Arena *a,
                                        Atom *space_ref);

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
            Space *sp = resolve_runtime_space_ref(s, a, rs.items[i]);
            if (sp) spaces[len++] = sp;
        }
    }
    free(rs.items);
    *spaces_out = spaces;
    *len_out = len;
}

static SpaceEngine default_runtime_space_backend_kind(Space *s) {
    SpaceEngine backend_kind = s ? s->match_backend.kind : SPACE_ENGINE_NATIVE;
    if (backend_kind == SPACE_ENGINE_MORK)
        backend_kind = SPACE_ENGINE_PATHMAP;
    return backend_kind;
}

static Space *alloc_runtime_space(Arena *pa, Space *context, SpaceKind kind,
                                  SpaceEngine backend_kind) {
    if (!pa)
        return NULL;
    Space *ns = arena_alloc(pa, sizeof(Space));
    if (!ns)
        return NULL;
    space_init_with_universe(ns, eval_current_term_universe());
    ns->kind = kind;
    if (!space_match_backend_try_set(ns, backend_kind))
        return NULL;
    return ns;
}

static Space *resolve_runtime_space_ref(Space *context, Arena *a,
                                        Atom *space_ref) {
    if (!g_registry || !space_ref)
        return NULL;

    Space *sp = resolve_space(g_registry, space_ref);
    if (sp)
        return sp;

    if (language_named_space_policy() != CETTA_NAMED_SPACE_POLICY_AUTO_CREATE ||
        !atom_is_registry_token(space_ref) ||
        space_ref->kind != ATOM_SYMBOL) {
        return NULL;
    }

    Atom *bound = registry_lookup_id(g_registry, space_ref->sym_id);
    if (bound)
        return resolve_space(g_registry, space_ref);

    Arena *pa = eval_storage_arena(a);
    Space *fresh = alloc_runtime_space(pa, context, SPACE_KIND_ATOM,
                                       default_runtime_space_backend_kind(context));
    if (!fresh)
        return NULL;
    registry_bind_id(g_registry, space_ref->sym_id, atom_space(pa, fresh));
    return fresh;
}

static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr, int fuel) {
    if (!g_registry) return NULL;

    Atom *direct = resolve_registry_refs(a, space_expr);
    Space *sp = resolve_runtime_space_ref(s, a, direct);
    if (sp) return sp;

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (uint32_t i = 0; i < rs.len; i++) {
        sp = resolve_runtime_space_ref(s, a, rs.items[i]);
        if (sp) break;
    }
    free(rs.items);
    return sp;
}

static Space *resolve_unify_space_target_if_obvious(Space *s, Arena *a,
                                                    Atom *target, int fuel) {
    if (!target)
        return NULL;
    Atom *direct = resolve_registry_refs(a, target);
    if (direct && direct->kind == ATOM_GROUNDED &&
        direct->ground.gkind == GV_SPACE) {
        return (Space *)direct->ground.ptr;
    }
    if (atom_is_registry_token(target)) {
        return resolve_single_space_arg(s, a, target, fuel);
    }
    if (target->kind == ATOM_EXPR && target->expr.len > 0) {
        SymbolId head_id = atom_head_symbol_id(target);
        if (head_id == g_builtin_syms.new_space ||
            head_id == g_builtin_syms.context_space) {
            return resolve_single_space_arg(s, a, target, fuel);
        }
    }
    return NULL;
}

static Atom *space_arg_error(Arena *a, Atom *call, const char *message) {
    return atom_error(a, call, atom_symbol(a, message));
}

typedef struct {
    SymbolId op_id;
    Atom *space_ref;
    Atom *template_atom;
} CettaAppendEffect;

static bool effect_template_vars_are_only(Atom *atom, VarId var_id) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR)
        return atom->var_id == var_id;
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!effect_template_vars_are_only(atom->expr.elems[i], var_id))
            return false;
    }
    return true;
}

static Atom *effect_template_replace_var(Arena *a, Atom *atom, VarId var_id,
                                         Atom *value) {
    if (!atom || !atom_has_vars(atom))
        return atom;
    if (atom->kind == ATOM_VAR)
        return atom->var_id == var_id ? value : atom;
    if (atom->kind != ATOM_EXPR)
        return atom;

    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        elems[i] = effect_template_replace_var(a, atom->expr.elems[i],
                                               var_id, value);
    }
    return atom_expr(a, elems, atom->expr.len);
}

static bool infer_single_append_effect(Atom *body, CettaAppendEffect *effect) {
    if (!body || !effect || body->kind != ATOM_EXPR || body->expr.len != 3)
        return false;
    SymbolId head_id = atom_head_symbol_id(body);
    if (head_id == g_builtin_syms.add_atom ||
        head_id == g_builtin_syms.add_atom_nodup) {
        *effect = (CettaAppendEffect) {
            .op_id = head_id,
            .space_ref = expr_arg(body, 0),
            .template_atom = expr_arg(body, 1),
        };
        return true;
    }
    return false;
}

typedef struct {
    Space *s;
    Arena *a;
    Atom *call_atom;
    Atom *pat;
    const Bindings *outer_env;
    CettaAppendEffect effect;
    Space *generic_target;
    bool direct_template_instantiation;
    OutcomeSet *os;
    uint32_t *unit_count;
    uint32_t emitted_units;
    ResultSet errors;
    bool failed;
} BatchAppendLetCtx;

static bool batch_append_space_contains_atom(Space *target, Atom *compare_atom) {
    bool found = false;
    bool backend_checked =
        space_match_backend_contains_atom_structural_direct(
            target, compare_atom, &found);
    if (!backend_checked)
        found = space_contains_exact(target, compare_atom);
    if (!found && !backend_checked) {
        bool alpha_fallback = atom_has_vars(compare_atom);
        for (uint32_t i = 0; i < space_length(target) && !found; i++) {
            Atom *candidate = space_get_at(target, i);
            if (!candidate)
                continue;
            if (alpha_fallback ? atom_alpha_eq(candidate, compare_atom)
                               : atom_eq(candidate, compare_atom)) {
                found = true;
            }
        }
    }
    return found;
}

static void batch_append_emit_unit(BatchAppendLetCtx *ctx) {
    Bindings empty;
    bindings_init(&empty);
    if (ctx->os) {
        outcome_set_add(ctx->os, atom_unit(ctx->a), &empty);
        ctx->emitted_units++;
    } else if (ctx->unit_count) {
        (*ctx->unit_count)++;
        ctx->emitted_units++;
    }
}

static bool batch_append_let_visit(Arena *a, Atom *atom,
                                   const Bindings *env, void *user_ctx) {
    (void)a;
    BatchAppendLetCtx *ctx = user_ctx;
    Bindings empty;
    bindings_init(&empty);

    if (atom_is_error(atom)) {
        result_set_add(&ctx->errors, atom);
        return true;
    }

    BindingsBuilder branch_builder;
    Bindings merged;
    const Bindings *effective = NULL;
    bool builder_active = false;
    bool merged_active = false;
    Atom *instantiated = NULL;

    if (ctx->direct_template_instantiation &&
        (!env || (env->len == 0 && env->eq_len == 0))) {
        instantiated = effect_template_replace_var(
            ctx->a, ctx->effect.template_atom, ctx->pat->var_id, atom);
    } else {
        if (!bindings_builder_init(&branch_builder, env))
            return true;
        builder_active = true;
        if (!bindings_builder_add_var_fresh(&branch_builder, ctx->pat, atom))
            goto cleanup;
        if (!bindings_effective_merge(&merged, &effective, ctx->outer_env,
                                      bindings_builder_bindings(&branch_builder),
                                      true)) {
            goto cleanup;
        }
        merged_active = effective == &merged;
        instantiated =
            bindings_apply_if_vars(effective, ctx->a, ctx->effect.template_atom);
    }
    bool ok = false;
    if (ctx->effect.op_id == g_builtin_syms.add_atom) {
        ok = space_admit_atom(ctx->generic_target,
                              eval_storage_arena(ctx->a),
                              instantiated);
    } else {
        Atom *compare_atom =
            space_compare_atom(ctx->generic_target, ctx->a, instantiated);
        ok = compare_atom &&
             (batch_append_space_contains_atom(ctx->generic_target,
                                               compare_atom) ||
              space_admit_atom(ctx->generic_target,
                               eval_storage_arena(ctx->a),
                               instantiated));
    }
    if (!ok) {
        Atom *error = atom_error(ctx->a, ctx->call_atom,
                                 atom_symbol(ctx->a, "BatchAppendFailed"));
        if (ctx->os) {
            outcome_set_add(ctx->os, error, &empty);
        } else {
            result_set_add(&ctx->errors, error);
        }
        ctx->failed = true;
        goto cleanup_fail;
    }
    batch_append_emit_unit(ctx);

cleanup:
    if (merged_active)
        bindings_free(&merged);
    if (builder_active)
        bindings_builder_free(&branch_builder);
    return true;

cleanup_fail:
    if (merged_active)
        bindings_free(&merged);
    if (builder_active)
        bindings_builder_free(&branch_builder);
    return false;
}

static bool try_effect_batch_append_let_units(Space *s, Arena *a,
                                              Atom *call_atom, Atom *pat,
                                              Atom *stream_expr, Atom *body,
                                              int fuel,
                                              const Bindings *outer_env,
                                              OutcomeSet *os,
                                              uint32_t *unit_count,
                                              ResultSet *errors_out) {
    if (!os && !unit_count)
        return false;
    if (!pat || pat->kind != ATOM_VAR)
        return false;
    if (!direct_outcome_walk_supported(s, a, stream_expr, fuel))
        return false;

    CettaAppendEffect effect = {0};
    if (!infer_single_append_effect(body, &effect)) {
        return false;
    }

    BatchAppendLetCtx ctx = {
        .s = s,
        .a = a,
        .call_atom = call_atom,
        .pat = pat,
        .outer_env = outer_env,
        .effect = effect,
        .generic_target = NULL,
        .direct_template_instantiation = false,
        .os = os,
        .unit_count = unit_count,
        .emitted_units = 0,
        .failed = false,
    };
    result_set_init(&ctx.errors);

    Atom *space_ref = bindings_apply_if_vars(outer_env, a, effect.space_ref);
    space_ref = resolve_registry_refs(a, space_ref);
    ctx.effect.template_atom =
        bindings_apply_if_vars(outer_env, a, effect.template_atom);
    ctx.direct_template_instantiation =
        effect_template_vars_are_only(ctx.effect.template_atom, pat->var_id);

    Atom *mork_handle_error = guard_mork_handle_surface(
        s, a, body, space_ref, fuel,
        effect.op_id == g_builtin_syms.add_atom_nodup
            ? "add-atom-nodup" : "add-atom",
        "mork:add-atom");
    if (mork_handle_error) {
        result_set_free(&ctx.errors);
        return false;
    }
    Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
    if (!target) {
        result_set_free(&ctx.errors);
        return false;
    }
    Atom *mork_error = guard_mork_space_surface(
        a, body, target,
        effect.op_id == g_builtin_syms.add_atom_nodup
            ? "add-atom-nodup" : "add-atom",
        "mork:add-atom");
    if (mork_error) {
        result_set_free(&ctx.errors);
        return false;
    }
    if (!space_match_backend_materialize_attached(
            target, eval_storage_arena(a))) {
        if (os) {
            Bindings empty;
            bindings_init(&empty);
            outcome_set_add(os,
                atom_error(a, call_atom,
                           atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed")),
                &empty);
        }
        result_set_free(&ctx.errors);
        return true;
    }
    ctx.generic_target = target;

    (void)metta_eval_bind_visit(s, a, stream_expr, fuel,
                                CETTA_SEARCH_POLICY_ORDER_NATIVE,
                                batch_append_let_visit, &ctx);

    if (ctx.errors.len > 0 && errors_out) {
        for (uint32_t i = 0; i < ctx.errors.len; i++)
            result_set_add(errors_out, ctx.errors.items[i]);
    } else if (ctx.errors.len > 0 && os && ctx.emitted_units == 0) {
        Bindings empty;
        bindings_init(&empty);
        for (uint32_t i = 0; i < ctx.errors.len; i++)
            outcome_set_add(os, ctx.errors.items[i], &empty);
    } else if (ctx.errors.len > 0 && !os) {
        result_set_free(&ctx.errors);
        return false;
    }

    result_set_free(&ctx.errors);
    return true;
}

static bool effect_safe_single_feeder_value(Space *s, Arena *a,
                                            Atom *expr, int fuel,
                                            Atom **out) {
    if (!expr || !out)
        return false;
    Atom *bound = registry_lookup_atom(expr);
    if (bound)
        expr = bound;
    expr = materialize_runtime_token(s, a, expr);
    if (atom_eval_is_immediate_value(expr, fuel)) {
        *out = expr;
        return true;
    }
    if (!expr_head_is_id(expr, g_builtin_syms.eval) || expr_nargs(expr) != 1)
        return false;

    Atom *arg = expr_arg(expr, 0);
    SymbolId head_id = atom_head_symbol_id(arg);
    SymbolId list_range_id = symbol_intern_cstr(g_symbols, "list:range");
    if (head_id != g_builtin_syms.range_atom && head_id != list_range_id)
        return false;

    ResultSet vals;
    result_set_init(&vals);
    metta_eval(s, a, NULL, expr, fuel, &vals);
    Atom *single = NULL;
    for (uint32_t i = 0; i < vals.len; i++) {
        if (atom_is_empty(vals.items[i]))
            continue;
        if (atom_is_error(vals.items[i]) || single) {
            free(vals.items);
            return false;
        }
        single = vals.items[i];
    }
    free(vals.items);
    if (!single)
        return false;
    *out = single;
    return true;
}

static bool try_effect_batch_append_collapse_count(Space *s, Arena *a,
                                                   Atom *inner, int fuel,
                                                   const Bindings *outer_env,
                                                   uint32_t depth,
                                                   uint32_t *unit_count,
                                                   ResultSet *errors_out) {
    if (!inner || inner->kind != ATOM_EXPR ||
        !expr_head_is_id(inner, g_builtin_syms.let) ||
        expr_nargs(inner) != 3) {
        return false;
    }

    Atom *stream_expr =
        bindings_apply_if_vars(outer_env, a, expr_arg(inner, 1));
    if (try_effect_batch_append_let_units(
            s, a, inner, expr_arg(inner, 0), stream_expr, expr_arg(inner, 2),
            fuel, outer_env, NULL, unit_count, errors_out)) {
        return true;
    }

    if (depth >= 8)
        return false;

    Atom *single = NULL;
    if (!effect_safe_single_feeder_value(s, a, stream_expr, fuel, &single))
        return false;

    BindingsBuilder b;
    if (!bindings_builder_init(&b, outer_env))
        return false;
    Atom *pat = expr_arg(inner, 0);
    bool ok = pat->kind == ATOM_VAR
        ? bindings_builder_add_var_fresh(&b, pat, single)
        : simple_match_builder(pat, single, &b);
    if (!ok) {
        bindings_builder_free(&b);
        return false;
    }

    bool handled = try_effect_batch_append_collapse_count(
        s, a, expr_arg(inner, 2), fuel, bindings_builder_bindings(&b),
        depth + 1, unit_count, errors_out);
    bindings_builder_free(&b);
    return handled;
}

static bool try_effect_batch_append_collapse(Space *s, Arena *a,
                                             Atom *inner, int fuel,
                                             const Bindings *outer_env,
                                             OutcomeSet *os) {
    uint32_t unit_count = 0;
    ResultSet errors;
    result_set_init(&errors);
    if (!try_effect_batch_append_collapse_count(
            s, a, inner, fuel, outer_env, 0, &unit_count, &errors)) {
        result_set_free(&errors);
        return false;
    }

    Bindings empty;
    bindings_init(&empty);
    uint32_t error_count = unit_count == 0 ? errors.len : 0;
    uint32_t len = unit_count + error_count;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (len ? len : 1));
    Atom *unit = atom_unit(a);
    for (uint32_t i = 0; i < unit_count; i++)
        items[i] = unit;
    for (uint32_t i = 0; i < error_count; i++)
        items[unit_count + i] = errors.items[i];
    outcome_set_add(os, atom_expr(a, items, len), &empty);
    result_set_free(&errors);
    return true;
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

/* Snapshot clone: shares universe-backed term identity but freezes the
   source's logical view. The snapshot owns only its atom-id sequence and
   per-space indexes/backend state. */
static void space_snapshot_copy_logical_view(Space *dst, const Space *src) {
    uint32_t n;

    if (!dst || !src)
        return;
    n = space_length(src);
    dst->revision = space_revision(src);
    if (n == 0)
        return;

    dst->native.atom_ids = cetta_malloc(sizeof(AtomId) * n);
    dst->native.cap = n;
    dst->native.len = n;
    dst->native.start = 0;
    for (uint32_t i = 0; i < n; i++) {
        dst->native.atom_ids[i] = space_get_atom_id_at(src, i);
    }

    /* The clone starts with no rebuilt indexes; they must be derived from the
       frozen logical view on first use. */
    dst->native.eq_idx_dirty = true;
    dst->native.ty_idx_dirty = true;
    dst->native.exact_idx_dirty = true;
}

static Space *space_snapshot_clone(Space *src, Arena *a) {
    SpaceEngine snapshot_backend = SPACE_ENGINE_NATIVE_CANDIDATE_EXACT;
    if (!space_match_backend_materialize_attached(
            src, eval_storage_arena(a)))
        return NULL;
    Space *clone = cetta_malloc(sizeof(Space));
    space_init_with_universe(clone, src ? src->native.universe : NULL);
    clone->kind = src->kind;
    /* Snapshots freeze a logical view for evaluator-side reads/matches.
       Use a native query backend over that frozen atom-id sequence so the
       snapshot remains stable even when the live source space is backed by a
       Rust-primary engine whose bridge state is optimized for mutation. */
    if (src &&
        (src->match_backend.kind == SPACE_ENGINE_NATIVE ||
         src->match_backend.kind == SPACE_ENGINE_NATIVE_CANDIDATE_EXACT)) {
        snapshot_backend = src->match_backend.kind;
    }
    (void)space_match_backend_try_set(clone, snapshot_backend);
    space_snapshot_copy_logical_view(clone, src);
    /* Backend indexes are rebuilt lazily on first match against the snapshot;
       the clone only freezes the logical atom sequence. */
    temp_space_register(clone);
    return clone;
}

static Atom *runtime_stats_inventory_atom(Arena *a) {
    Space *inventory = cetta_malloc(sizeof(Space));
    CettaRuntimeStats stats;
    space_init_with_universe(inventory, eval_current_term_universe());
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
    return bindings_apply_if_vars(env, a, body);
}

static __attribute__((unused)) bool
bind_domain_binder(Bindings *env, Atom *domain, Atom *term) {
    Atom *binder = NULL;
    Atom *body = domain;
    if (!split_dependent_domain(domain, &binder, &body) || !binder) {
        return true;
    }
    return bindings_add_var(env, binder, term);
}

static bool bind_domain_binder_builder(BindingsBuilder *bb, Atom *domain,
                                       Atom *term) {
    Atom *binder = NULL;
    Atom *body = domain;
    if (!split_dependent_domain(domain, &binder, &body) || !binder) {
        return true;
    }
    return bindings_builder_add_var_fresh(bb, binder, term);
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
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&scratch, NULL);

    for (uint32_t oi = 0; oi < nop; oi++) {
        Atom *ft = op_types[oi];
        if (!is_function_type(ft)) {
            continue;
        }
        tried_func_type = true;
        if (ft->expr.len - 2 != atom->expr.len - 1) {
            continue;
        }

        ArenaMark scratch_mark = arena_mark(&scratch);
        Atom *fresh_ft = atom_freshen_epoch(&scratch, ft, fresh_var_suffix());
        Atom *fresh_ret = get_function_ret_type(fresh_ft);
        Bindings tb;
        bindings_init(&tb);
        bool all_ok = true;

        for (uint32_t ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
            Atom *binder = NULL;
            Atom *decl =
                function_domain_type(&tb, &scratch, fresh_ft->expr.elems[ai + 1], &binder);
            Atom **atypes = NULL;
            uint32_t nat = get_atom_types_profiled(s, a, atom->expr.elems[ai + 1], &atypes);
            bool found = false;
            SearchContext trial_context;
            if (!search_context_init(&trial_context, &tb, &scratch)) {
                free(atypes);
                bindings_free(&tb);
                free(types);
                arena_free(&scratch);
                free(op_types);
                *out_types = NULL;
                return 0;
            }

            for (uint32_t ti = 0; ti < nat; ti++) {
                ChoicePoint point = search_context_save(&trial_context);
                if (match_types_builder(decl, atypes[ti],
                                        search_context_builder(&trial_context))) {
                    Atom *arg_term =
                        bindings_apply_if_vars(search_context_bindings(&trial_context),
                                               search_context_scratch(&trial_context),
                                               atom->expr.elems[ai + 1]);
                    if (bind_domain_binder_builder(search_context_builder(&trial_context),
                                                  fresh_ft->expr.elems[ai + 1],
                                                  arg_term)) {
                        Bindings next_tb;
                        bindings_init(&next_tb);
                        search_context_take(&trial_context, &next_tb);
                        bindings_replace(&tb, &next_tb);
                        found = true;
                        break;
                    }
                }
                search_context_rollback(&trial_context, point);
            }
            search_context_free(&trial_context);
            free(atypes);
            if (!found) {
                all_ok = false;
            }
        }

        if (all_ok) {
            Atom *concrete_ret =
                normalize_type_expr_profiled(&scratch,
                                             bindings_apply_if_vars(&tb, &scratch, fresh_ret));
            types = types ? cetta_realloc(types, sizeof(Atom *) * (count + 1))
                          : cetta_malloc(sizeof(Atom *));
            types[count++] = atom_deep_copy(a, concrete_ret);
        }
        bindings_free(&tb);
        arena_reset(&scratch, scratch_mark);
    }

    arena_free(&scratch);
    free(op_types);
    if (tried_func_type && count == 0) {
        *out_types = NULL;
        return 0;
    }
    *out_types = types;
    return count;
}

static bool petta_get_type_is_single_undefined(Atom **types, uint32_t count) {
    return count == 1 && types &&
           atom_is_symbol_id(types[0], g_builtin_syms.undefined_type);
}

static bool petta_type_tuple_extend(Arena *a, Atom ***items,
                                    uint32_t *len, uint32_t *cap,
                                    Atom **prefix, uint32_t prefix_len) {
    Atom *tuple = atom_expr(a, prefix, prefix_len);
    if (atom_list_contains_alpha_eq(*items, *len, tuple))
        return true;
    if (*len >= *cap) {
        uint32_t new_cap = *cap ? *cap * 2 : 4;
        Atom **new_items = cetta_realloc(*items, sizeof(Atom *) * new_cap);
        if (!new_items)
            return false;
        *items = new_items;
        *cap = new_cap;
    }
    (*items)[(*len)++] = tuple;
    return true;
}

static bool petta_infer_tuple_type_rec(Space *s, Arena *a, Atom *const *elems,
                                       uint32_t elem_len, uint32_t index,
                                       Atom **prefix, Atom ***items,
                                       uint32_t *len, uint32_t *cap) {
    if (index == elem_len)
        return petta_type_tuple_extend(a, items, len, cap, prefix, elem_len);

    Atom **elem_types = NULL;
    uint32_t nelem_types = get_atom_types_profiled(s, a, elems[index], &elem_types);
    if (nelem_types == 0) {
        prefix[index] = atom_undefined_type(a);
        return petta_infer_tuple_type_rec(s, a, elems, elem_len, index + 1,
                                          prefix, items, len, cap);
    }
    for (uint32_t i = 0; i < nelem_types; i++) {
        prefix[index] = elem_types[i];
        if (!petta_infer_tuple_type_rec(s, a, elems, elem_len, index + 1,
                                        prefix, items, len, cap)) {
            free(elem_types);
            return false;
        }
    }
    free(elem_types);
    return true;
}

static uint32_t petta_infer_tuple_types(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types) {
    if (!language_is_petta() || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len == 0) {
        *out_types = NULL;
        return 0;
    }
    Atom **items = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    Atom **prefix = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    if (!petta_infer_tuple_type_rec(s, a, atom->expr.elems, atom->expr.len,
                                    0, prefix, &items, &len, &cap)) {
        free(items);
        *out_types = NULL;
        return 0;
    }
    *out_types = items;
    return len;
}

static bool type_array_add_unique_alpha(Atom ***items, uint32_t *count,
                                        Atom *candidate);
static uint32_t petta_collect_user_get_type_rule_types(Space *s, Arena *a,
                                                       Atom *target,
                                                       Atom ***out_types);
static int g_petta_user_get_type_rule_eval_depth = 0;

static uint32_t get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types) {
    uint32_t count = get_atom_types(s, a, atom, out_types);
    if (language_is_petta() && g_petta_user_get_type_rule_eval_depth == 0) {
        Atom **rule_types = NULL;
        uint32_t nrule_types =
            petta_collect_user_get_type_rule_types(s, a, atom, &rule_types);
        if (nrule_types > 0) {
            if (petta_get_type_is_single_undefined(*out_types, count)) {
                free(*out_types);
                *out_types = rule_types;
                count = nrule_types;
            } else {
                for (uint32_t i = 0; i < nrule_types; i++) {
                    (void)type_array_add_unique_alpha(out_types, &count,
                                                      rule_types[i]);
                }
                free(rule_types);
            }
        }
    }
    if (!eval_dependent_telescope_enabled() || atom->kind != ATOM_EXPR || count != 0) {
        return count;
    }
    return infer_dependent_application_types(s, a, atom, out_types);
}

static bool get_type_should_try_value_result(Atom *target,
                                             Atom **types,
                                             uint32_t ntypes) {
    if (!target || target->kind != ATOM_EXPR || ntypes == 0)
        return false;
    if (ntypes == 1 && atom_is_symbol_id(types[0], g_builtin_syms.undefined_type))
        return true;
    for (uint32_t i = 0; i < ntypes; i++) {
        if (!atom_contains_vars(types[i]))
            return false;
    }
    return true;
}

static bool get_type_eval_result_types(Space *s, Arena *a, Atom *target,
                                       int fuel, Atom ***out_types,
                                       uint32_t *out_count) {
    ResultSet evr;
    Atom **collected = NULL;
    uint32_t count = 0;
    result_set_init(&evr);
    metta_eval(s, a, NULL, target, fuel, &evr);
    for (uint32_t i = 0; i < evr.len; i++) {
        Atom **value_types = NULL;
        uint32_t nvalue_types =
            get_atom_types_profiled(s, a, evr.items[i], &value_types);
        for (uint32_t ti = 0; ti < nvalue_types; ti++) {
            if (atom_list_contains_alpha_eq(collected, count, value_types[ti]))
                continue;
            collected = collected
                ? cetta_realloc(collected, sizeof(Atom *) * (count + 1))
                : cetta_malloc(sizeof(Atom *));
            collected[count++] = value_types[ti];
        }
        free(value_types);
    }
    result_set_free(&evr);
    if (count == 0) {
        free(collected);
        return false;
    }
    *out_types = collected;
    *out_count = count;
    return true;
}

static bool type_array_add_unique_alpha(Atom ***items, uint32_t *count,
                                        Atom *candidate) {
    if (!items || !count || !candidate)
        return false;
    if (atom_list_contains_alpha_eq(*items, *count, candidate))
        return true;
    Atom **grown = *items
        ? cetta_realloc(*items, sizeof(Atom *) * (*count + 1))
        : cetta_malloc(sizeof(Atom *));
    if (!grown)
        return false;
    *items = grown;
    (*items)[(*count)++] = candidate;
    return true;
}

static uint32_t petta_collect_user_get_type_rule_types(Space *s, Arena *a,
                                                       Atom *target,
                                                       Atom ***out_types) {
    *out_types = NULL;
    if (!language_is_petta() || !s || !target || target->kind == ATOM_VAR)
        return 0;

    Atom *query =
        atom_expr2(a, atom_symbol_id(a, g_builtin_syms.get_type), target);

    QueryResults qr;
    query_results_init(&qr);
    query_equations(s, query, a, &qr);

    Atom **types = NULL;
    uint32_t count = 0;
    if (qr.len > 0) {
        SearchContext context;
        Bindings empty;
        OutcomeSet out;
        bindings_init(&empty);
        outcome_set_init(&out);
        if (search_context_init(&context, &empty, NULL)) {
            QueryEvalVisitorCtx query_eval = {
                .space = s,
                .arena = a,
                .declared_type = NULL,
                .fuel = -1,
                .base_env = &empty,
                .preserve_bindings = false,
                .context = &context,
                .outcomes = &out,
                .episode = NULL,
            };
            g_petta_user_get_type_rule_eval_depth++;
            query_results_visit(&qr, query_visit_eval_for_caller, &query_eval);
            g_petta_user_get_type_rule_eval_depth--;
            search_context_free(&context);
        }
        bindings_free(&empty);
        for (uint32_t oi = 0; oi < out.len; oi++) {
            Atom *ty = outcome_atom_materialize(a, &out.items[oi]);
            if (atom_is_empty(ty) || atom_is_error(ty))
                continue;
            (void)type_array_add_unique_alpha(&types, &count, ty);
        }
        outcome_set_free(&out);
    }

    query_results_free(&qr);
    *out_types = types;
    return count;
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
        Atom *reason = atom_expr3(a, atom_symbol_id(a, g_builtin_syms.bad_type),
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
            if (language_is_petta() &&
                expected &&
                expected->kind == ATOM_VAR &&
                atom_contains_vars(arg)) {
                if (nnext < 64) {
                    bindings_move(&next[nnext], &results[r]);
                    nnext++;
                }
                continue;
            }
            if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
                atom_is_symbol_id(expected, g_builtin_syms.undefined_type)) {
                if (nnext < 64) {
                    bindings_move(&next[nnext], &results[r]);
                    nnext++;
                }
                continue;
            }

            Atom **atypes;
            uint32_t natypes = get_atom_types_profiled(s, a, arg, &atypes);
            if (natypes == 0) {
                if (nnext < 64) {
                    bindings_move(&next[nnext], &results[r]);
                    nnext++;
                }
                free(atypes);
                continue;
            }
            SearchContext candidate_context;
            search_context_init_owned(&candidate_context, &results[r], NULL);
            for (uint32_t t = 0; t < natypes; t++) {
                ChoicePoint point = search_context_save(&candidate_context);
                if (match_types_builder(expected, atypes[t],
                                        search_context_builder(&candidate_context))) {
                    if (!bind_domain_binder_builder(search_context_builder(&candidate_context),
                                                    arg_types[i], arg)) {
                        search_context_rollback(&candidate_context, point);
                        continue;
                    }
                    if (nnext < 64 &&
                        bindings_clone(&next[nnext],
                                       search_context_bindings(&candidate_context))) {
                        nnext++;
                    }
                    search_context_rollback(&candidate_context, point);
                    found = true;
                }
                else {
                    search_context_rollback(&candidate_context, point);
                }
            }
            search_context_free(&candidate_context);
            if (!found && natypes > 0) {
                /* Report first mismatching type */
                Atom *reason = atom_expr(a, (Atom*[]){
                    atom_symbol_id(a, g_builtin_syms.bad_arg_type),
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
        SearchContext ret_context;
        search_context_init_owned(&ret_context, &results[r], NULL);
        Atom *inst_ret =
            eval_dependent_telescope_enabled()
                ? bindings_apply_if_vars(search_context_bindings(&ret_context), a, retType)
                : retType;
        if (match_types_builder(expectedType, inst_ret,
                                search_context_builder(&ret_context))) {
            if (ret_ok < 64) {
                search_context_take(&ret_context, &success_bindings[ret_ok]);
                ret_ok++;
            }
        } else {
            Atom *reason = atom_expr3(a, atom_symbol_id(a, g_builtin_syms.bad_type),
                                      expectedType, inst_ret);
            if (*n_errors < 64)
                errors[(*n_errors)++] = atom_error(a, expr, reason);
        }
        search_context_free(&ret_context);
    }

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
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard stack_guard = {0};
    if (!eval_c_stack_guard_enter(fuel, &stack_guard)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_TRIP_EVAL);
        result_set_add(rs, atom_error(a, atom, atom_symbol(a, "StackOverflow")));
        return;
    }
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

    /* Empty/Error: return as-is (control forms filter Empty where needed). */
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
        outcome_set_filter_errors_if_success(a, &os);
        for (uint32_t oi = 0; oi < os.len; oi++)
            result_set_add(rs,
                           outcome_atom_materialize_traced(
                               a, &os.items[oi],
                               CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_TOP_LEVEL));
        outcome_set_free(&os);
    }
}

/* ── metta_eval_bind: like metta_eval but returns bindings too ──────────── */
/* Used by interpret_tuple to thread bindings between sub-expressions.
   For most atoms, bindings are empty. For equation queries, bindings
   contain variable assignments from bidirectional matching. */

static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os) {
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard stack_guard = {0};
    if (!eval_c_stack_guard_enter(fuel, &stack_guard)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_TRIP_BIND);
        Bindings overflow_empty;
        bindings_init(&overflow_empty);
        outcome_set_add(os, atom_error(a, atom, atom_symbol(a, "StackOverflow")),
                        &overflow_empty);
        return;
    }
    Bindings empty;
    bindings_init(&empty);

    Atom *bound = registry_lookup_atom(atom);
    if (bound) {
        outcome_set_add(os, bound, &empty);
        return;
    }
    atom = materialize_runtime_token(s, a, atom);

    if (atom_eval_is_immediate_value(atom, fuel)) {
        outcome_set_add(os, atom, &empty);
        return;
    }
    metta_call(s, a, atom, NULL, fuel > 0 ? fuel - 1 : fuel, true, os);
    outcome_set_filter_errors_if_success(a, os);
}

static void metta_eval_bind_typed(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, OutcomeSet *os) {
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard stack_guard = {0};
    if (!eval_c_stack_guard_enter(fuel, &stack_guard)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_TRIP_BIND_TYPED);
        Bindings overflow_empty;
        bindings_init(&overflow_empty);
        outcome_set_add(os, atom_error(a, atom, atom_symbol(a, "StackOverflow")),
                        &overflow_empty);
        return;
    }
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
    outcome_set_filter_errors_if_success(a, os);
}

static void eval_with_prefix_bindings(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                      const Bindings *prefix, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    Atom *eval_atom = atom;
    if (language_is_petta() &&
        bindings_has_any_entries(prefix) &&
        expr_head_is_id(atom, g_builtin_syms.superpose)) {
        eval_atom = bindings_apply_if_vars(prefix, a, atom);
    }
    metta_eval_bind_typed(s, a, type, eval_atom, fuel, &inner);
    if (!prefix || prefix->len == 0) {
        for (uint32_t i = 0; i < inner.len; i++) {
            outcome_set_add_existing_move(os, &inner.items[i]);
        }
        outcome_set_free(&inner);
        return;
    }
    BindingsBuilder merged_builder;
    if (!bindings_builder_init(&merged_builder, prefix)) {
        outcome_set_free(&inner);
        return;
    }
    for (uint32_t i = 0; i < inner.len; i++) {
        BindingsMergeAttempt attempt;
        if (!bindings_builder_merge_or_clone(&merged_builder, prefix,
                                             &inner.items[i].env, &attempt))
            continue;
        outcome_set_add_existing_with_env(a, os, &inner.items[i], attempt.env);
        bindings_merge_attempt_finish(&merged_builder, &attempt);
    }
    bindings_builder_free(&merged_builder);
    outcome_set_free(&inner);
}

typedef struct {
    VarId var_id;
    SymbolId spelling;
} MatchVisibleVarRef;

typedef struct {
    MatchVisibleVarRef *items;
    uint32_t len;
    uint32_t cap;
} MatchVisibleVarSet;

typedef struct {
    VarId hidden_var_id;
    VarId visible_var_id;
    SymbolId spelling;
} MatchVisibleAliasRef;

typedef struct {
    MatchVisibleAliasRef *items;
    uint32_t len;
    uint32_t cap;
} MatchVisibleAliasSet;

static __attribute__((unused)) void match_visible_var_set_init(MatchVisibleVarSet *set) {
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static __attribute__((unused)) void match_visible_var_set_free(MatchVisibleVarSet *set) {
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool match_visible_var_set_reserve(MatchVisibleVarSet *set,
                                          uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t next_cap = set->cap ? set->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    set->items = set->items
        ? cetta_realloc(set->items, sizeof(MatchVisibleVarRef) * next_cap)
        : cetta_malloc(sizeof(MatchVisibleVarRef) * next_cap);
    set->cap = next_cap;
    return true;
}

static bool match_visible_var_set_add(MatchVisibleVarSet *set, VarId var_id,
                                      SymbolId spelling) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].var_id == var_id)
            return true;
    }
    if (!match_visible_var_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].var_id = var_id;
    set->items[set->len].spelling = spelling;
    set->len++;
    return true;
}

static bool match_visible_var_set_contains(const MatchVisibleVarSet *set,
                                           VarId var_id) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].var_id == var_id)
            return true;
    }
    return false;
}

static void match_visible_alias_set_init(MatchVisibleAliasSet *set) {
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static void match_visible_alias_set_free(MatchVisibleAliasSet *set) {
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool match_visible_alias_set_reserve(MatchVisibleAliasSet *set,
                                            uint32_t needed) {
    if (needed <= set->cap)
        return true;
    uint32_t next_cap = set->cap ? set->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    set->items = set->items
        ? cetta_realloc(set->items, sizeof(MatchVisibleAliasRef) * next_cap)
        : cetta_malloc(sizeof(MatchVisibleAliasRef) * next_cap);
    set->cap = next_cap;
    return true;
}

static bool match_visible_alias_set_add(MatchVisibleAliasSet *set,
                                        VarId hidden_var_id,
                                        VarId visible_var_id,
                                        SymbolId spelling) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].hidden_var_id == hidden_var_id) {
            set->items[i].visible_var_id = visible_var_id;
            set->items[i].spelling = spelling;
            return true;
        }
    }
    if (!match_visible_alias_set_reserve(set, set->len + 1))
        return false;
    set->items[set->len].hidden_var_id = hidden_var_id;
    set->items[set->len].visible_var_id = visible_var_id;
    set->items[set->len].spelling = spelling;
    set->len++;
    return true;
}

static const MatchVisibleAliasRef *match_visible_alias_set_lookup(
    const MatchVisibleAliasSet *set, VarId hidden_var_id) {
    for (uint32_t i = 0; i < set->len; i++) {
        if (set->items[i].hidden_var_id == hidden_var_id)
            return &set->items[i];
    }
    return NULL;
}

static bool collect_match_visible_vars_rec(Atom *atom,
                                           MatchVisibleVarSet *set) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR)
        return match_visible_var_set_add(set, atom->var_id, atom->sym_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!collect_match_visible_vars_rec(atom->expr.elems[i], set))
            return false;
    }
    return true;
}

static __attribute__((unused)) bool collect_match_visible_vars_many(Atom **atoms,
                                            uint32_t natoms,
                                            MatchVisibleVarSet *set) {
    for (uint32_t i = 0; i < natoms; i++) {
        if (!collect_match_visible_vars_rec(atoms[i], set))
            return false;
    }
    return true;
}

static bool atom_refs_only_match_visible_vars(Atom *atom,
                                              const MatchVisibleVarSet *visible) {
    if (!atom || !atom_has_vars(atom))
        return true;
    if (atom->kind == ATOM_VAR)
        return match_visible_var_set_contains(visible, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return true;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        if (!atom_refs_only_match_visible_vars(atom->expr.elems[i], visible))
            return false;
    }
    return true;
}

static Atom *rewrite_match_visible_aliases(Arena *a, Atom *atom,
                                           const MatchVisibleAliasSet *aliases) {
    if (!atom || !aliases || aliases->len == 0 || !atom_has_vars(atom))
        return atom;
    if (atom->kind == ATOM_VAR) {
        const MatchVisibleAliasRef *alias =
            match_visible_alias_set_lookup(aliases, atom->var_id);
        if (!alias || alias->spelling != atom->sym_id)
            return atom;
        return atom_var_with_spelling(a, alias->spelling, alias->visible_var_id);
    }
    if (atom->kind != ATOM_EXPR)
        return atom;

    Atom **rewritten = NULL;
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        Atom *child = atom->expr.elems[i];
        Atom *next = rewrite_match_visible_aliases(a, child, aliases);
        if (!rewritten && next != child) {
            rewritten = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            for (uint32_t j = 0; j < i; j++)
                rewritten[j] = atom->expr.elems[j];
        }
        if (rewritten)
            rewritten[i] = next;
    }
    return rewritten ? atom_expr(a, rewritten, atom->expr.len) : atom;
}

static __attribute__((unused)) bool project_match_visible_bindings(Arena *a,
                                           const MatchVisibleVarSet *visible,
                                           const Bindings *full,
                                           Bindings *projected) {
    bindings_init(projected);
    if (!full || visible->len == 0)
        return true;

    Atom **resolved_values = arena_alloc(a, sizeof(Atom *) * visible->len);
    MatchVisibleAliasSet aliases;
    match_visible_alias_set_init(&aliases);

    for (uint32_t i = 0; i < visible->len; i++) {
        VisibleVarRef wanted = {
            .var_id = visible->items[i].var_id,
            .spelling = visible->items[i].spelling,
        };
        Atom *resolved = bindings_resolve_body_visible_var(a, full, &wanted);
        resolved_values[i] = resolved;
        if (resolved->kind == ATOM_VAR &&
            resolved->sym_id == visible->items[i].spelling &&
            resolved->var_id != visible->items[i].var_id &&
            !match_visible_alias_set_add(&aliases, resolved->var_id,
                                         visible->items[i].var_id,
                                         visible->items[i].spelling)) {
            match_visible_alias_set_free(&aliases);
            bindings_free(projected);
            return false;
        }
    }

    for (uint32_t i = 0; i < visible->len; i++) {
        Atom *resolved = rewrite_match_visible_aliases(a, resolved_values[i], &aliases);
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == visible->items[i].var_id &&
            resolved->sym_id == visible->items[i].spelling) {
            continue;
        }
        if (!bindings_add_id(projected, visible->items[i].var_id,
                             visible->items[i].spelling, resolved)) {
            bindings_free(projected);
            return false;
        }
    }

    for (uint32_t i = 0; i < full->eq_len; i++) {
        Atom *lhs = bindings_apply_if_vars(full, a, full->constraints[i].lhs);
        Atom *rhs = bindings_apply_if_vars(full, a, full->constraints[i].rhs);
        lhs = rewrite_match_visible_aliases(a, lhs, &aliases);
        rhs = rewrite_match_visible_aliases(a, rhs, &aliases);
        if (!atom_refs_only_match_visible_vars(lhs, visible) ||
            !atom_refs_only_match_visible_vars(rhs, visible)) {
            continue;
        }
        if (!bindings_add_constraint(projected, lhs, rhs)) {
            match_visible_alias_set_free(&aliases);
            bindings_free(projected);
            return false;
        }
    }
    match_visible_alias_set_free(&aliases);
    return true;
}

static bool collect_goal_instantiation_visible_vars(
    const CettaVarMap *goal_instantiation,
    MatchVisibleVarSet *visible) {
    if (!visible)
        return false;
    if (!goal_instantiation)
        return true;
    for (uint32_t i = 0; i < goal_instantiation->len; i++) {
        Atom *mapped = goal_instantiation->items[i].mapped_var;
        if (!mapped)
            continue;
        if (!collect_match_visible_vars_rec(mapped, visible))
            return false;
    }
    return true;
}

static bool bindings_project_answer_ref_env(Arena *a,
                                            const CettaVarMap *goal_instantiation,
                                            const Bindings *full,
                                            Bindings *projected) {
    MatchVisibleVarSet visible;
    match_visible_var_set_init(&visible);
    bool ok = collect_goal_instantiation_visible_vars(goal_instantiation,
                                                      &visible);
    if (ok)
        ok = project_match_visible_bindings(a, &visible, full, projected);
    match_visible_var_set_free(&visible);
    return ok;
}

static void outcome_set_add_prefixed(Arena *a, OutcomeSet *os, Atom *atom,
                                     const Bindings *local_env,
                                     const Bindings *outer_env,
                                     bool preserve_bindings) {
    Bindings empty;
    bindings_init(&empty);
    const Bindings *inner = local_env ? local_env : &empty;

    if (!preserve_bindings) {
        Atom *applied = atom;
        Bindings merged;
        const Bindings *effective = NULL;
        if (!bindings_effective_merge(&merged, &effective, outer_env, inner, false))
            return;
        if (effective)
            applied = bindings_apply_if_vars(effective, a, atom);
        if (effective == &merged)
            bindings_free(&merged);
        outcome_set_add(os, applied, &empty);
        return;
    }
    if (!bindings_has_any_entries(outer_env)) {
        outcome_set_add(os, atom, inner);
        return;
    }

    Bindings merged;
    const Bindings *effective = NULL;
    if (!bindings_effective_merge(&merged, &effective, outer_env, inner, true) ||
        !effective)
        return;
    if (effective == &merged) {
        outcome_set_add_move(os, atom, &merged);
    } else {
        outcome_set_add(os, atom, effective);
    }
}

static __attribute__((unused)) void
outcome_set_add_prefixed_move(Arena *a, OutcomeSet *os, Atom *atom,
                              Bindings *local_env,
                              const Bindings *outer_env,
                              bool preserve_bindings) {
    Bindings empty;
    bindings_init(&empty);
    Bindings *inner = local_env ? local_env : &empty;

    if (!preserve_bindings) {
        Atom *applied = atom;
        Bindings merged;
        const Bindings *effective = NULL;
        if (!bindings_effective_merge(&merged, &effective, outer_env, inner, false))
            return;
        if (effective)
            applied = bindings_apply_if_vars(effective, a, atom);
        if (effective == &merged)
            bindings_free(&merged);
        outcome_set_add(os, applied, &empty);
        return;
    }
    if (!bindings_has_any_entries(outer_env)) {
        if (local_env) {
            outcome_set_add_move(os, atom, inner);
        } else {
            outcome_set_add(os, atom, &empty);
        }
        return;
    }

    Bindings merged;
    const Bindings *effective = NULL;
    if (!bindings_effective_merge(&merged, &effective, outer_env, inner, true) ||
        !effective)
        return;
    if (effective == &merged) {
        outcome_set_add_move(os, atom, &merged);
    } else if (effective == inner && local_env) {
        outcome_set_add_move(os, atom, inner);
    } else {
        outcome_set_add(os, atom, effective);
    }
}

static void outcome_set_append_prefixed(Arena *a, OutcomeSet *dst, OutcomeSet *src,
                                        const Bindings *outer_env,
                                        bool preserve_bindings) {
    if (preserve_bindings && bindings_has_any_entries(outer_env)) {
        BindingsBuilder merged_builder;
        if (!bindings_builder_init(&merged_builder, outer_env))
            return;
        for (uint32_t i = 0; i < src->len; i++) {
            BindingsMergeAttempt attempt;
            if (!bindings_builder_merge_or_clone(&merged_builder, outer_env,
                                                 &src->items[i].env, &attempt))
                continue;
            outcome_set_add_existing_with_env(a, dst, &src->items[i], attempt.env);
            bindings_merge_attempt_finish(&merged_builder, &attempt);
        }
        bindings_builder_free(&merged_builder);
        return;
    }
    for (uint32_t i = 0; i < src->len; i++) {
        if (preserve_bindings && !bindings_has_any_entries(outer_env)) {
            outcome_set_add_existing(dst, &src->items[i]);
            continue;
        }
        outcome_set_add_prefixed_outcome(a, dst, &src->items[i],
                                         outer_env, preserve_bindings);
    }
}

static void outcome_set_append_prefixed_move(Arena *a, OutcomeSet *dst,
                                             OutcomeSet *src,
                                             const Bindings *outer_env,
                                             bool preserve_bindings) {
    if (preserve_bindings && bindings_has_any_entries(outer_env)) {
        BindingsBuilder merged_builder;
        if (!bindings_builder_init(&merged_builder, outer_env))
            return;
        for (uint32_t i = 0; i < src->len; i++) {
            BindingsMergeAttempt attempt;
            if (!bindings_builder_merge_or_clone(&merged_builder, outer_env,
                                                 &src->items[i].env, &attempt))
                continue;
            outcome_set_add_existing_with_env(a, dst, &src->items[i], attempt.env);
            bindings_merge_attempt_finish(&merged_builder, &attempt);
        }
        bindings_builder_free(&merged_builder);
        return;
    }
    for (uint32_t i = 0; i < src->len; i++) {
        if (preserve_bindings && !bindings_has_any_entries(outer_env)) {
            outcome_set_add_existing_move(dst, &src->items[i]);
            continue;
        }
        outcome_set_add_prefixed_outcome(a, dst, &src->items[i],
                                         outer_env, preserve_bindings);
    }
}

/* When the caller only cares about atoms, apply pending bindings before
   evaluation and drop only semantic envs at the boundary so delayed variants
   can survive until a true observation point. */
static void eval_for_caller(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                            const Bindings *prefix, bool preserve_bindings,
                            OutcomeSet *os) {
    if (preserve_bindings) {
        eval_with_prefix_bindings(s, a, type, atom, fuel, prefix, os);
        return;
    }

    Bindings empty;
    bindings_init(&empty);
    Atom *direct_grounded = eval_direct_grounded_application(s, a, atom, prefix, fuel);
    if (direct_grounded) {
        outcome_set_add(os, direct_grounded, &empty);
        return;
    }
    Atom *applied = (!prefix || prefix->len == 0)
        ? atom
        : bindings_apply_if_vars(prefix, a, atom);
    if (atom_eval_is_immediate_value(applied, fuel)) {
        if (type) {
            OutcomeSet inner;
            outcome_set_init(&inner);
            metta_eval_bind_typed(s, a, type, applied, fuel, &inner);
            outcome_set_filter_errors_if_success(a, &inner);
            outcome_set_append_prefixed_move(a, os, &inner, NULL, false);
            outcome_set_free(&inner);
            return;
        }
        outcome_set_add(os, applied, &empty);
        return;
    }
    Atom *applied_grounded = eval_direct_grounded_application(s, a, applied, &empty, fuel);
    if (applied_grounded) {
        outcome_set_add(os, applied_grounded, &empty);
        return;
    }
    OutcomeSet inner;
    outcome_set_init(&inner);
    metta_eval_bind_typed(s, a, type, applied, fuel, &inner);
    outcome_set_filter_errors_if_success(a, &inner);
    outcome_set_append_prefixed_move(a, os, &inner, NULL, false);
    outcome_set_free(&inner);
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
    if (!preserve_bindings) {
        Bindings merged;
        const Bindings *effective = NULL;
        if (!bindings_effective_merge(&merged, &effective, outer_env, prefix, true))
            return;
        eval_for_caller(s, a, type, atom, fuel, effective, false, os);
        if (effective == &merged)
            bindings_free(&merged);
        return;
    }

    OutcomeSet inner;
    outcome_set_init(&inner);
    eval_for_caller(s, a, type, atom, fuel, prefix, preserve_bindings, &inner);
    outcome_set_append_prefixed_move(a, os, &inner, outer_env, preserve_bindings);
    outcome_set_free(&inner);
}

static bool branch_outer_env_begin(Bindings *owned,
                                   const Bindings **effective_outer,
                                   const Bindings *outer_env,
                                   const Bindings *branch_env) {
    return bindings_effective_merge(owned, effective_outer,
                                    outer_env, branch_env, true);
}

static void branch_outer_env_finish(Bindings *owned,
                                    const Bindings *effective_outer) {
    if (effective_outer == owned)
        bindings_free(owned);
}

static bool petta_rel_goal_outcome_seen(Arena *a, const OutcomeSet *os,
                                        Atom *atom, const Bindings *env) {
    if (!os || !atom || !env)
        return false;
    for (uint32_t i = 0; i < os->len; i++) {
        Atom *existing = outcome_atom_materialize(a, &os->items[i]);
        if (!atom_eq(existing, atom))
            continue;
        if (bindings_eq(&os->items[i].env, (Bindings *)env))
            return true;
    }
    return false;
}

static void petta_rel_goal_add_unique(Arena *a, OutcomeSet *os,
                                      Atom *atom, const Bindings *env) {
    if (!petta_rel_goal_outcome_seen(a, os, atom, env))
        outcome_set_add(os, atom, env);
}

static void petta_rel_goal_add_unique_control(Arena *a, OutcomeSet *os,
                                              Atom *atom, const Bindings *env,
                                              uint8_t control) {
    if (!petta_rel_goal_outcome_seen(a, os, atom, env)) {
        uint32_t before = os->len;
        outcome_set_add(os, atom, env);
        outcome_set_mark_control_since(os, before, control);
    }
}

static void petta_rel_goal_add_existing_unique(Arena *a, OutcomeSet *os,
                                               const Outcome *src) {
    Atom *atom = outcome_atom_materialize(a, (Outcome *)src);
    if (!petta_rel_goal_outcome_seen(a, os, atom, &src->env))
        outcome_set_add_existing(os, src);
}

static bool petta_rel_goal_project_env(Arena *a, Atom *scope,
                                       const Bindings *full,
                                       Bindings *out) {
    VarId body_ids[64];
    SymbolId body_spellings[64];
    uint32_t nbody = 0;

    bindings_init(out);
    if (!full)
        return true;
    if (!atom_contains_vars(scope))
        return true;
    if (!collect_pattern_vars_simple_rec(scope, body_ids, body_spellings, &nbody, 64))
        return false;
    for (uint32_t i = 0; i < nbody; i++) {
        VisibleVarRef wanted = {
            .var_id = body_ids[i],
            .spelling = body_spellings[i],
        };
        Atom *resolved = bindings_resolve_body_visible_var(a, full, &wanted);
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == wanted.var_id &&
            resolved->sym_id == wanted.spelling) {
            for (uint32_t j = 0; j < full->len; j++) {
                Atom *val = full->entries[j].val;
                if (!val || val->kind != ATOM_VAR)
                    continue;
                if (val->var_id != wanted.var_id &&
                    val->sym_id != wanted.spelling) {
                    continue;
                }
                VisibleVarRef producer = {
                    .var_id = full->entries[j].var_id,
                    .spelling = full->entries[j].spelling,
                };
                Atom *producer_resolved =
                    bindings_resolve_body_visible_var(a, full, &producer);
                if (producer_resolved->kind == ATOM_VAR &&
                    producer_resolved->var_id == producer.var_id &&
                    producer_resolved->sym_id == producer.spelling) {
                    continue;
                }
                resolved = producer_resolved;
                break;
            }
        }
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == wanted.var_id &&
            resolved->sym_id == wanted.spelling) {
            Atom *spelling_match = NULL;
            bool ambiguous = false;
            for (uint32_t j = 0; j < full->len; j++) {
                if (full->entries[j].spelling != wanted.spelling)
                    continue;
                VisibleVarRef producer = {
                    .var_id = full->entries[j].var_id,
                    .spelling = full->entries[j].spelling,
                };
                Atom *producer_resolved =
                    bindings_resolve_body_visible_var(a, full, &producer);
                if (producer_resolved->kind == ATOM_VAR &&
                    producer_resolved->var_id == producer.var_id &&
                    producer_resolved->sym_id == producer.spelling) {
                    continue;
                }
                if (spelling_match && !atom_eq(spelling_match, producer_resolved)) {
                    ambiguous = true;
                    break;
                }
                spelling_match = producer_resolved;
            }
            if (!ambiguous && spelling_match)
                resolved = spelling_match;
        }
        if (resolved->kind == ATOM_VAR &&
            resolved->var_id == wanted.var_id &&
            resolved->sym_id == wanted.spelling) {
            bool copied_same_spelling = false;
            for (uint32_t j = 0; j < full->len; j++) {
                if (full->entries[j].spelling != wanted.spelling)
                    continue;
                Atom *projected = full->entries[j].val;
                if (atom_contains_vars(projected)) {
                    projected =
                        bindings_apply_without_self((Bindings *)full, a,
                                                    full->entries[j].var_id,
                                                    projected);
                }
                if (!bindings_add_id_name_fallback(out,
                                                   full->entries[j].var_id,
                                                   full->entries[j].spelling,
                                                   projected)) {
                    bindings_free(out);
                    return false;
                }
                copied_same_spelling = true;
            }
            if (copied_same_spelling)
                continue;
            continue;
        }
        if (!bindings_add_id_name_fallback(out, wanted.var_id,
                                           wanted.spelling, resolved)) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static CettaPettaBuiltinRelationKind
petta_rel_call_builtin_kind(Atom *goal_call) {
    return cetta_petta_builtin_relation_kind_from_call(goal_call);
}

typedef enum {
    PETTA_RUNTIME_INT_CMP_EQ = 0,
    PETTA_RUNTIME_INT_CMP_LT,
    PETTA_RUNTIME_INT_CMP_GT,
} PettaRuntimeIntCmpKind;

static void petta_rel_goal_emit_builtin_binary_candidate(
    Arena *a,
    Atom *projection_scope,
    Atom *applied_output_pat,
    const Bindings *base_env,
    OutcomeSet *os,
    Atom *lhs_atom,
    Atom *rhs_atom,
    Atom *out_atom
) {
    Atom *export_items[] = { lhs_atom, rhs_atom };
    Atom *result_items[] = {
        out_atom,
        atom_expr(a, export_items, 2),
    };
    Atom *result = atom_expr(a, result_items, 2);
    const Bindings *result_env = base_env;
    __attribute__((cleanup(bindings_builder_free)))
    BindingsBuilder match_builder = {0};
    if (applied_output_pat &&
        (applied_output_pat->kind == ATOM_VAR ||
         !atom_eq(applied_output_pat, result))) {
        if (!bindings_builder_init(&match_builder, result_env))
            return;
        if (!match_atoms_builder(applied_output_pat, result, &match_builder))
            return;
        result_env = bindings_builder_bindings(&match_builder);
    }
    const Bindings *visible_result_env = result_env;
    Bindings projected_env;
    Bindings merged_env;
    bool have_projected_env = false;
    bool have_merged_env = false;
    bindings_init(&projected_env);
    bindings_init(&merged_env);
    if (petta_rel_goal_project_env(a, projection_scope, result_env, &projected_env)) {
        have_projected_env = true;
        if (bindings_clone(&merged_env, base_env) &&
            bindings_try_merge_live(&merged_env, &projected_env)) {
            visible_result_env = &merged_env;
            have_merged_env = true;
        } else {
            visible_result_env = result_env;
        }
    }
    petta_rel_goal_add_unique(a, os, result, visible_result_env);
    if (have_merged_env)
        bindings_free(&merged_env);
    if (have_projected_env)
        bindings_free(&projected_env);
}

static void petta_rel_goal_emit_builtin_unary_candidate(
    Arena *a,
    Atom *projection_scope,
    Atom *applied_output_pat,
    const Bindings *base_env,
    OutcomeSet *os,
    Atom *arg_atom,
    Atom *out_atom
) {
    Atom *export_items[] = { arg_atom };
    Atom *result_items[] = {
        out_atom,
        atom_expr(a, export_items, 1),
    };
    Atom *result = atom_expr(a, result_items, 2);
    const Bindings *result_env = base_env;
    __attribute__((cleanup(bindings_builder_free)))
    BindingsBuilder match_builder = {0};
    if (applied_output_pat &&
        (applied_output_pat->kind == ATOM_VAR ||
         !atom_eq(applied_output_pat, result))) {
        if (!bindings_builder_init(&match_builder, result_env))
            return;
        if (!match_atoms_builder(applied_output_pat, result, &match_builder))
            return;
        result_env = bindings_builder_bindings(&match_builder);
    }
    Bindings projected_env;
    Bindings merged_env;
    const Bindings *visible_result_env = result_env;
    bool have_projected_env = false;
    bool have_merged_env = false;
    bindings_init(&projected_env);
    bindings_init(&merged_env);
    if (petta_rel_goal_project_env(a, projection_scope, result_env, &projected_env)) {
        have_projected_env = true;
        if (bindings_clone(&merged_env, base_env) &&
            bindings_try_merge_live(&merged_env, &projected_env)) {
            visible_result_env = &merged_env;
            have_merged_env = true;
        }
    }
    petta_rel_goal_add_unique(a, os, result, visible_result_env);
    if (have_merged_env)
        bindings_free(&merged_env);
    if (have_projected_env)
        bindings_free(&projected_env);
}

static Atom *petta_expr_slice(Arena *a, Atom *tuple,
                              uint32_t start, uint32_t len) {
    Atom **items = len > 0 ? arena_alloc(a, sizeof(Atom *) * len) : NULL;
    for (uint32_t i = 0; i < len; i++)
        items[i] = tuple->expr.elems[start + i];
    return atom_expr(a, items, len);
}

static Atom *petta_expr_concat(Arena *a, Atom *lhs, Atom *rhs) {
    uint32_t lhs_len = lhs->expr.len;
    uint32_t rhs_len = rhs->expr.len;
    uint32_t len = lhs_len + rhs_len;
    Atom **items = len > 0 ? arena_alloc(a, sizeof(Atom *) * len) : NULL;
    for (uint32_t i = 0; i < lhs_len; i++)
        items[i] = lhs->expr.elems[i];
    for (uint32_t i = 0; i < rhs_len; i++)
        items[lhs_len + i] = rhs->expr.elems[i];
    return atom_expr(a, items, len);
}

static void petta_rel_goal_eval_builtin_append(Space *s, Arena *a,
                                               Atom *goal_call,
                                               Atom *output_pat,
                                               int fuel,
                                               const Bindings *base_env,
                                               OutcomeSet *os) {
    (void)s;
    (void)fuel;
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env))
        applied_env = &visible_env;

    Atom *applied_call =
        resolve_registry_refs(a, bindings_apply_if_vars(applied_env, a, goal_call));
    Atom *applied_output_pat = output_pat
        ? resolve_registry_refs(
              a, bindings_apply_if_vars(applied_env, a, output_pat))
        : NULL;
    Atom *lhs_atom = expr_arg(applied_call, 0);
    Atom *rhs_atom = expr_arg(applied_call, 1);
    Atom *out_atom = expr_arg(applied_call, 2);
    bool had_candidate = false;

    if (out_atom && out_atom->kind == ATOM_EXPR) {
        for (uint32_t split = 0; split <= out_atom->expr.len; split++) {
            Atom *lhs_candidate = petta_expr_slice(a, out_atom, 0, split);
            Atom *rhs_candidate =
                petta_expr_slice(a, out_atom, split, out_atom->expr.len - split);
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, applied_env, os,
                lhs_candidate, rhs_candidate, out_atom);
            had_candidate = true;
        }
    } else if (lhs_atom && rhs_atom &&
               lhs_atom->kind == ATOM_EXPR &&
               rhs_atom->kind == ATOM_EXPR) {
        Atom *out_candidate = petta_expr_concat(a, lhs_atom, rhs_atom);
        petta_rel_goal_emit_builtin_binary_candidate(
            a, projection_scope, applied_output_pat, applied_env, os,
            lhs_atom, rhs_atom, out_candidate);
        had_candidate = true;
    }

    if (!had_candidate)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    bindings_free(&visible_env);
}

static void petta_rel_goal_eval_builtin_hash_plus(Space *s, Arena *a,
                                                  Atom *goal_call,
                                                  Atom *output_pat,
                                                  int fuel,
                                                  const Bindings *base_env,
                                                  OutcomeSet *os) {
    (void)s;
    (void)fuel;
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env)) {
        applied_env = &visible_env;
    }

    Atom *applied_call =
        resolve_registry_refs(a, bindings_apply_if_vars(applied_env, a, goal_call));
    Atom *applied_output_pat = output_pat
        ? resolve_registry_refs(
              a, bindings_apply_if_vars(applied_env, a, output_pat))
        : NULL;

    Atom *lhs_atom = expr_arg(applied_call, 0);
    Atom *rhs_atom = expr_arg(applied_call, 1);
    Atom *out_atom = expr_arg(applied_call, 2);

    int64_t lhs_val = 0;
    int64_t rhs_val = 0;
    int64_t out_val = 0;
    bool lhs_known = atom_get_int_literal(lhs_atom, &lhs_val);
    bool rhs_known = atom_get_int_literal(rhs_atom, &rhs_val);
    bool out_known = atom_get_int_literal(out_atom, &out_val);
    bool had_success = false;

    if (lhs_known && rhs_known) {
        int64_t sum = 0;
        if (int64_add_checked(lhs_val, rhs_val, &sum)) {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                lhs_atom, rhs_atom, atom_int(a, sum));
            had_success = true;
        }
    } else if (rhs_known && out_known) {
        int64_t inferred_lhs = 0;
        if (int64_sub_checked(out_val, rhs_val, &inferred_lhs)) {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                atom_int(a, inferred_lhs), rhs_atom, out_atom);
            had_success = true;
        }
    } else if (lhs_known && out_known) {
        int64_t inferred_rhs = 0;
        if (int64_sub_checked(out_val, lhs_val, &inferred_rhs)) {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                lhs_atom, atom_int(a, inferred_rhs), out_atom);
            had_success = true;
        }
    }

    if (!had_success)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    bindings_free(&visible_env);
}

static void petta_rel_goal_eval_builtin_hash_eq(Space *s, Arena *a,
                                                Atom *goal_call,
                                                Atom *output_pat,
                                                int fuel,
                                                const Bindings *base_env,
                                                OutcomeSet *os) {
    (void)s;
    (void)fuel;
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env)) {
        applied_env = &visible_env;
    }

    Atom *applied_call = bindings_apply_if_vars(applied_env, a, goal_call);
    Atom *applied_output_pat = output_pat
        ? bindings_apply_if_vars(applied_env, a, output_pat)
        : NULL;

    Atom *lhs_atom = expr_arg(applied_call, 0);
    Atom *rhs_atom = expr_arg(applied_call, 1);
    Atom *out_atom = expr_arg(applied_call, 2);

    int64_t lhs_val = 0;
    int64_t rhs_val = 0;
    bool out_bool = false;
    bool lhs_known = atom_get_int_literal(lhs_atom, &lhs_val);
    bool rhs_known = atom_get_int_literal(rhs_atom, &rhs_val);
    bool out_known = atom_is_bool_literal(out_atom, &out_bool);
    bool had_success = false;

    if (lhs_known && rhs_known) {
        petta_rel_goal_emit_builtin_binary_candidate(
            a, projection_scope, applied_output_pat, base_env, os,
            lhs_atom, rhs_atom, atom_bool(a, lhs_val == rhs_val));
        had_success = true;
    } else if (out_known && out_bool && lhs_known) {
        petta_rel_goal_emit_builtin_binary_candidate(
            a, projection_scope, applied_output_pat, base_env, os,
            lhs_atom, atom_int(a, lhs_val), atom_true(a));
        had_success = true;
    } else if (out_known && out_bool && rhs_known) {
        petta_rel_goal_emit_builtin_binary_candidate(
            a, projection_scope, applied_output_pat, base_env, os,
            atom_int(a, rhs_val), rhs_atom, atom_true(a));
        had_success = true;
    } else if (!out_known) {
        if (lhs_known && rhs_known) {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                lhs_atom, rhs_atom, atom_bool(a, lhs_val == rhs_val));
            had_success = true;
        } else if (lhs_known) {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                lhs_atom, atom_int(a, lhs_val), atom_true(a));
            had_success = true;
        } else if (rhs_known) {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                atom_int(a, rhs_val), rhs_atom, atom_true(a));
            had_success = true;
        } else {
            petta_rel_goal_emit_builtin_binary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                lhs_atom, rhs_atom, atom_true(a));
            had_success = true;
        }
    }

    if (!had_success)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    bindings_free(&visible_env);
}

static bool petta_int_cmp_eval(PettaRuntimeIntCmpKind kind,
                               int64_t lhs, int64_t rhs) {
    switch (kind) {
    case PETTA_RUNTIME_INT_CMP_EQ:
        return lhs == rhs;
    case PETTA_RUNTIME_INT_CMP_LT:
        return lhs < rhs;
    case PETTA_RUNTIME_INT_CMP_GT:
        return lhs > rhs;
    default:
        return false;
    }
}

static void petta_rel_goal_eval_builtin_hash_cmp(Space *s, Arena *a,
                                                 Atom *goal_call,
                                                 Atom *output_pat,
                                                 int fuel,
                                                 const Bindings *base_env,
                                                 OutcomeSet *os,
                                                 PettaRuntimeIntCmpKind kind) {
    (void)s;
    (void)fuel;
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env)) {
        applied_env = &visible_env;
    }

    Atom *applied_call = bindings_apply_if_vars(applied_env, a, goal_call);
    Atom *applied_output_pat = output_pat
        ? bindings_apply_if_vars(applied_env, a, output_pat)
        : NULL;

    Atom *lhs_atom = expr_arg(applied_call, 0);
    Atom *rhs_atom = expr_arg(applied_call, 1);

    int64_t lhs_val = 0;
    int64_t rhs_val = 0;
    bool lhs_known = atom_get_int_literal(lhs_atom, &lhs_val);
    bool rhs_known = atom_get_int_literal(rhs_atom, &rhs_val);
    bool had_success = false;

    if (lhs_known && rhs_known) {
        petta_rel_goal_emit_builtin_binary_candidate(
            a, projection_scope, applied_output_pat, base_env, os,
            lhs_atom, rhs_atom,
            atom_bool(a, petta_int_cmp_eval(kind, lhs_val, rhs_val)));
        had_success = true;
    }

    if (!had_success)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    bindings_free(&visible_env);
}

static void petta_rel_goal_eval_builtin_member(Space *s, Arena *a,
                                               Atom *goal_call,
                                               Atom *output_pat,
                                               int fuel,
                                               const Bindings *base_env,
                                               OutcomeSet *os) {
    (void)s;
    (void)fuel;
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env))
        applied_env = &visible_env;

    Atom *applied_call = bindings_apply_if_vars(applied_env, a, goal_call);
    Atom *applied_output_pat = output_pat
        ? bindings_apply_if_vars(applied_env, a, output_pat)
        : NULL;
    Atom *needle = expr_arg(applied_call, 0);
    Atom *list = expr_arg(applied_call, 1);
    Atom *out_atom = expr_arg(applied_call, 2);
    bool out_bool = false;
    bool out_known = atom_is_bool_literal(out_atom, &out_bool);
    bool had_success = false;

    if ((!out_known || out_bool) && list && list->kind == ATOM_EXPR) {
        for (uint32_t i = 0; i < list->expr.len; i++) {
            BindingsBuilder b;
            if (!bindings_builder_init(&b, applied_env))
                continue;
            if (match_atoms_builder(needle, list->expr.elems[i], &b)) {
                const Bindings *bb = bindings_builder_bindings(&b);
                if (!bindings_has_loop((Bindings *)bb)) {
                    Atom *bound_needle = bindings_apply_if_vars(bb, a, needle);
                    Atom *bound_list = bindings_apply_if_vars(bb, a, list);
                    petta_rel_goal_emit_builtin_binary_candidate(
                        a, projection_scope, applied_output_pat, bb, os,
                        bound_needle, bound_list, atom_true(a));
                    had_success = true;
                }
            }
            bindings_builder_free(&b);
        }
    }

    if (!had_success)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    bindings_free(&visible_env);
}

static Atom *petta_rel_goal_fresh_tuple(Arena *a, int64_t len) {
    if (len < 0 || len > 16)
        return NULL;
    Atom **items = len > 0 ? arena_alloc(a, sizeof(Atom *) * (uint32_t)len) : NULL;
    for (int64_t i = 0; i < len; i++) {
        char name[96];
        snprintf(name, sizeof(name), "$__petta_size_item_%u_%" PRId64,
                 fresh_var_suffix(), i);
        items[i] = atom_var(a, name);
    }
    return atom_expr(a, items, (uint32_t)len);
}

static void petta_rel_goal_eval_builtin_size_atom(Space *s, Arena *a,
                                                  Atom *goal_call,
                                                  Atom *output_pat,
                                                  int fuel,
                                                  const Bindings *base_env,
                                                  OutcomeSet *os) {
    (void)s;
    (void)fuel;
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env))
        applied_env = &visible_env;

    Atom *applied_call = bindings_apply_if_vars(applied_env, a, goal_call);
    Atom *applied_output_pat = output_pat
        ? bindings_apply_if_vars(applied_env, a, output_pat)
        : NULL;
    Atom *list = expr_arg(applied_call, 0);
    Atom *out_atom = expr_arg(applied_call, 1);
    int64_t out_len = 0;
    bool out_known = atom_get_int_literal(out_atom, &out_len);
    bool had_success = false;

    if (list && list->kind == ATOM_EXPR) {
        petta_rel_goal_emit_builtin_unary_candidate(
            a, projection_scope, applied_output_pat, base_env, os,
            list, atom_int(a, (int64_t)list->expr.len));
        had_success = true;
    } else if (list && list->kind == ATOM_VAR && out_known) {
        Atom *tuple = petta_rel_goal_fresh_tuple(a, out_len);
        if (tuple) {
            petta_rel_goal_emit_builtin_unary_candidate(
                a, projection_scope, applied_output_pat, base_env, os,
                tuple, out_atom);
            had_success = true;
        }
    }

    if (!had_success)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    bindings_free(&visible_env);
}

static Atom *petta_rel_goal_try_single_concrete_eval(Space *s, Arena *a,
                                                     Atom *term,
                                                     int fuel) {
    if (!term || term->kind != ATOM_EXPR)
        return term;

    OutcomeSet outcomes;
    outcome_set_init(&outcomes);
    metta_eval_bind(s, a, term, fuel, &outcomes);

    Atom *single_value = NULL;
    bool ambiguous = false;
    for (uint32_t i = 0; i < outcomes.len; i++) {
        Atom *candidate = outcome_atom_materialize(a, &outcomes.items[i]);
        if (!candidate || atom_is_error(candidate) || atom_is_empty(candidate) ||
            atom_contains_vars(candidate)) {
            continue;
        }
        if (!single_value) {
            single_value = candidate;
            continue;
        }
        if (!atom_eq(single_value, candidate)) {
            ambiguous = true;
            break;
        }
    }

    outcome_set_free(&outcomes);
    if (single_value && !ambiguous)
        return single_value;
    return term;
}

static Atom *petta_rel_goal_normalize_runtime_term(Space *s, Arena *a,
                                                   Atom *term,
                                                   int fuel) {
    if (!term || term->kind != ATOM_EXPR)
        return term;

    Atom *evaluated = petta_rel_goal_try_single_concrete_eval(s, a, term, fuel);
    if (evaluated != term)
        return evaluated;

    Atom **normalized_elems = NULL;
    for (uint32_t i = 0; i < term->expr.len; i++) {
        Atom *child = term->expr.elems[i];
        Atom *normalized = petta_rel_goal_normalize_runtime_term(s, a, child, fuel);
        if (normalized != child) {
            if (!normalized_elems) {
                normalized_elems =
                    arena_alloc(a, sizeof(Atom *) * term->expr.len);
                for (uint32_t j = 0; j < term->expr.len; j++) {
                    normalized_elems[j] = term->expr.elems[j];
                }
            }
            normalized_elems[i] = normalized;
        }
    }

    if (!normalized_elems)
        return term;
    return atom_expr(a, normalized_elems, term->expr.len);
}

static void petta_rel_goal_eval_hidden_relation_call(Space *s, Arena *a,
                                                     Atom *goal_call,
                                                     Atom *output_pat,
                                                     int fuel,
                                                     const Bindings *base_env,
                                                     OutcomeSet *os) {
    switch (petta_rel_call_builtin_kind(goal_call)) {
    case CETTA_PETTA_BUILTIN_REL_APPEND:
        petta_rel_goal_eval_builtin_append(s, a, goal_call, output_pat,
                                           fuel, base_env, os);
        return;
    case CETTA_PETTA_BUILTIN_REL_MEMBER:
        petta_rel_goal_eval_builtin_member(s, a, goal_call, output_pat,
                                           fuel, base_env, os);
        return;
    case CETTA_PETTA_BUILTIN_REL_SIZE_ATOM:
        petta_rel_goal_eval_builtin_size_atom(s, a, goal_call, output_pat,
                                              fuel, base_env, os);
        return;
    case CETTA_PETTA_BUILTIN_REL_HASH_PLUS:
        petta_rel_goal_eval_builtin_hash_plus(s, a, goal_call, output_pat,
                                              fuel, base_env, os);
        return;
    case CETTA_PETTA_BUILTIN_REL_HASH_EQ:
        petta_rel_goal_eval_builtin_hash_eq(s, a, goal_call, output_pat,
                                            fuel, base_env, os);
        return;
    case CETTA_PETTA_BUILTIN_REL_HASH_LT:
        petta_rel_goal_eval_builtin_hash_cmp(s, a, goal_call, output_pat,
                                             fuel, base_env, os,
                                             PETTA_RUNTIME_INT_CMP_LT);
        return;
    case CETTA_PETTA_BUILTIN_REL_HASH_GT:
        petta_rel_goal_eval_builtin_hash_cmp(s, a, goal_call, output_pat,
                                             fuel, base_env, os,
                                             PETTA_RUNTIME_INT_CMP_GT);
        return;
    default:
        break;
    }
    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }

    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env)) {
        applied_env = &visible_env;
    }

    Atom *applied_call = bindings_apply_if_vars(applied_env, a, goal_call);
    if (applied_call && applied_call->kind == ATOM_EXPR) {
        Atom **normalized_elems = NULL;
        for (uint32_t i = 1; i < applied_call->expr.len; i++) {
            Atom *child = applied_call->expr.elems[i];
            Atom *normalized =
                petta_rel_goal_normalize_runtime_term(s, a, child, fuel);
            if (normalized != child) {
                if (!normalized_elems) {
                    normalized_elems =
                        arena_alloc(a, sizeof(Atom *) * applied_call->expr.len);
                    for (uint32_t j = 0; j < applied_call->expr.len; j++) {
                        normalized_elems[j] = applied_call->expr.elems[j];
                    }
                }
                normalized_elems[i] = normalized;
            }
        }
        if (normalized_elems) {
            applied_call = atom_expr(a, normalized_elems, applied_call->expr.len);
        }
    }
    Atom *applied_output_pat = output_pat
        ? bindings_apply_if_vars(applied_env, a, output_pat)
        : NULL;
    if (applied_output_pat) {
        applied_output_pat =
            petta_rel_goal_normalize_runtime_term(s, a, applied_output_pat, fuel);
    }

    Atom *query_call = applied_call;
    if (applied_call && applied_call->kind == ATOM_EXPR &&
        applied_call->expr.len >= 2) {
        Atom **query_elems =
            arena_alloc(a, sizeof(Atom *) * applied_call->expr.len);
        for (uint32_t i = 0; i < applied_call->expr.len; i++) {
            query_elems[i] = applied_call->expr.elems[i];
        }
        query_elems[applied_call->expr.len - 1] =
            atom_var(a, "__petta_rel_query_export");
        query_call = atom_expr(a, query_elems, applied_call->expr.len);
    }

    QueryResults qr;
    query_results_init(&qr);
    query_equations(s, query_call, a, &qr);

    bool had_success = false;
    bool had_error = false;
    bool cut_seen = false;
    for (uint32_t i = 0; i < qr.len; i++) {
        OutcomeSet body_out;
        outcome_set_init(&body_out);
        Atom *bound_body =
            bindings_apply_if_vars(&qr.items[i].bindings, a, qr.items[i].result);
        Bindings empty_prefix;
        bindings_init(&empty_prefix);
        eval_for_current_caller(s, a, NULL,
                                bound_body,
                                fuel,
                                &empty_prefix,
                                base_env,
                                true,
                                &body_out);

        for (uint32_t j = 0; j < body_out.len; j++) {
            Atom *result = outcome_atom_materialize(a, &body_out.items[j]);
            if (atom_is_error(result)) {
                had_error = true;
                petta_rel_goal_add_existing_unique(a, os, &body_out.items[j]);
                if (body_out.items[j].control == CETTA_OUTCOME_CONTROL_CUT)
                    cut_seen = true;
                continue;
            }
            if (atom_is_empty(result))
                continue;
            had_success = true;
            if (body_out.items[j].control == CETTA_OUTCOME_CONTROL_CUT)
                cut_seen = true;
            const Bindings *result_env = &body_out.items[j].env;
            __attribute__((cleanup(bindings_builder_free)))
            BindingsBuilder match_builder = {0};
            if (applied_output_pat &&
                (applied_output_pat->kind == ATOM_VAR ||
                 !atom_eq(applied_output_pat, result))) {
                if (!bindings_builder_init(&match_builder, result_env))
                    continue;
                if (!match_atoms_builder(applied_output_pat, result, &match_builder))
                    continue;
                result_env = bindings_builder_bindings(&match_builder);
            }
            Bindings projected_env;
            const Bindings *visible_result_env = result_env;
            if (petta_rel_goal_project_env(a, projection_scope, result_env, &projected_env)) {
                visible_result_env = &projected_env;
            }
            petta_rel_goal_add_unique_control(a, os, result, visible_result_env,
                                              body_out.items[j].control);
            bindings_free(&projected_env);
        }

        outcome_set_free(&body_out);
        if (cut_seen) {
            outcome_set_consume_control_since(os, 0, CETTA_OUTCOME_CONTROL_CUT);
            break;
        }
    }

    if (!had_success && !had_error)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);

    query_results_free(&qr);
    bindings_free(&visible_env);
}

static void petta_rel_goal_eval_relation_call(Space *s, Arena *a,
                                              Atom *goal_call,
                                              Atom *output_pat,
                                              int fuel,
                                              const Bindings *base_env,
                                              OutcomeSet *os) {
    if (cetta_petta_relation_call_uses_hidden_head(goal_call)) {
        petta_rel_goal_eval_hidden_relation_call(s, a, goal_call, output_pat,
                                                 fuel, base_env, os);
        return;
    }

    Atom *projection_scope = goal_call;
    if (output_pat) {
        Atom **scope_elems = arena_alloc(a, sizeof(Atom *) * 2);
        scope_elems[0] = goal_call;
        scope_elems[1] = output_pat;
        projection_scope = atom_expr(a, scope_elems, 2);
    }
    Bindings visible_env;
    const Bindings *applied_env = base_env;
    if (petta_rel_goal_project_env(a, projection_scope, base_env, &visible_env)) {
        applied_env = &visible_env;
    }
    OutcomeSet inner;
    outcome_set_init(&inner);
    Atom *applied_call = bindings_apply_if_vars(applied_env, a, goal_call);
    Atom *applied_output_pat = output_pat
        ? bindings_apply_if_vars(applied_env, a, output_pat)
        : NULL;
    metta_eval_bind(s, a, applied_call, fuel, &inner);
    OutcomeSet merged;
    outcome_set_init(&merged);
    outcome_set_append_prefixed_move(a, &merged, &inner, base_env, true);
    outcome_set_free(&inner);
    bool had_success = false;
    bool had_error = false;
    for (uint32_t i = 0; i < merged.len; i++) {
        Atom *result = outcome_atom_materialize(a, &merged.items[i]);
        if (atom_is_error(result)) {
            had_error = true;
            petta_rel_goal_add_existing_unique(a, os, &merged.items[i]);
            continue;
        }
        if (atom_is_empty(result))
            continue;
        had_success = true;
        const Bindings *result_env = &merged.items[i].env;
        __attribute__((cleanup(bindings_builder_free)))
        BindingsBuilder match_builder = {0};
        if (applied_output_pat) {
            bool pat_bool = false;
            bool result_bool = false;
            if (atom_is_bool_literal(applied_output_pat, &pat_bool)) {
                if (!atom_is_bool_literal(result, &result_bool) ||
                    pat_bool != result_bool) {
                    continue;
                }
            } else if (applied_output_pat->kind == ATOM_VAR ||
                       !atom_eq(applied_output_pat, result)) {
                if (!bindings_builder_init(&match_builder, NULL))
                    continue;
                if (!match_atoms_builder(applied_output_pat, result, &match_builder))
                    continue;
                if (!bindings_builder_try_merge(&match_builder, result_env))
                    continue;
                result_env = bindings_builder_bindings(&match_builder);
            }
        }
        Bindings projected_env;
        const Bindings *visible_result_env = result_env;
        if (petta_rel_goal_project_env(a, projection_scope, result_env, &projected_env)) {
            visible_result_env = &projected_env;
        }
        petta_rel_goal_add_unique_control(a, os, result, visible_result_env,
                                          merged.items[i].control);
        bindings_free(&projected_env);
    }
    if (!had_success && !had_error)
        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);
    outcome_set_free(&merged);
    bindings_free(&visible_env);
}

static void petta_rel_goal_eval(Space *s, Arena *a, Atom *goal, int fuel,
                                const Bindings *outer_env,
                                OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    const Bindings *base_env = outer_env ? outer_env : &empty;

    if (!goal || atom_is_symbol_id(goal, g_builtin_syms.petta_rel_true)) {
        petta_rel_goal_add_unique(a, os, atom_true(a), base_env);
        return;
    }

    if (goal->kind != ATOM_EXPR || goal->expr.len == 0) {
        petta_rel_goal_add_unique(
            a, os,
            atom_error(a, goal, atom_symbol(a, "BadPettaRelationGoal")),
            base_env);
        return;
    }

    SymbolId head_id = atom_head_symbol_id(goal);
    uint32_t nargs = expr_nargs(goal);

    if (head_id == g_builtin_syms.petta_rel_call && nargs == 3) {
        Atom *forward_call = expr_arg(goal, 0);
        Atom *relation_call = expr_arg(goal, 1);
        Atom *actual = expr_arg(goal, 2);
        bool relation_needs_export = false;
        Atom *relation_result_pat = NULL;

        if (!atom_is_empty(relation_call)) {
            if (relation_call->kind == ATOM_EXPR &&
                relation_call->expr.len >= 2) {
                Atom *relation_export_pat =
                    relation_call->expr.elems[relation_call->expr.len - 1];
                relation_needs_export =
                    relation_export_pat && atom_contains_vars(relation_export_pat);
                Atom *items[] = { actual, relation_export_pat };
                relation_result_pat = atom_expr(a, items, 2);
            }
        }
        if (!atom_is_empty(relation_call) &&
            petta_rel_call_builtin_kind(relation_call) !=
                CETTA_PETTA_BUILTIN_REL_NONE) {
            petta_rel_goal_eval_relation_call(s, a, relation_call,
                                              relation_result_pat, fuel,
                                              base_env, os);
            return;
        }
        OutcomeSet forward;
        outcome_set_init(&forward);
        petta_rel_goal_eval_relation_call(s, a, forward_call, actual, fuel,
                                          base_env, &forward);

        if (relation_needs_export && !atom_is_empty(relation_call)) {
            OutcomeSet refined;
            bool refined_success = false;
            bool refined_error = false;
            outcome_set_init(&refined);
            for (uint32_t i = 0; i < forward.len; i++) {
                Atom *candidate = outcome_atom_materialize(a, &forward.items[i]);
                if (atom_is_error(candidate)) {
                    refined_error = true;
                    petta_rel_goal_add_existing_unique(a, &refined, &forward.items[i]);
                    continue;
                }
                if (atom_is_empty(candidate) || atom_contains_vars(candidate))
                    continue;
                if (relation_call->kind != ATOM_EXPR || relation_call->expr.len < 3)
                    continue;
                Atom **rel_elems = arena_alloc(a, sizeof(Atom *) * relation_call->expr.len);
                for (uint32_t j = 0; j < relation_call->expr.len; j++) {
                    rel_elems[j] = relation_call->expr.elems[j];
                }
                rel_elems[relation_call->expr.len - 2] = candidate;
                Atom *concrete_relation_call = atom_expr(a, rel_elems, relation_call->expr.len);
                OutcomeSet one;
                outcome_set_init(&one);
                petta_rel_goal_eval_relation_call(s, a, concrete_relation_call,
                                                  relation_result_pat, fuel,
                                                  base_env, &one);
                for (uint32_t j = 0; j < one.len; j++) {
                    Atom *refined_atom = outcome_atom_materialize(a, &one.items[j]);
                    if (atom_is_error(refined_atom))
                        refined_error = true;
                    else if (!atom_is_empty(refined_atom))
                        refined_success = true;
                    petta_rel_goal_add_existing_unique(a, &refined, &one.items[j]);
                }
                outcome_set_free(&one);
            }
            if (refined_success || refined_error) {
                for (uint32_t i = 0; i < refined.len; i++) {
                    petta_rel_goal_add_existing_unique(a, os, &refined.items[i]);
                }
                outcome_set_free(&forward);
                outcome_set_free(&refined);
                return;
            }
            outcome_set_free(&refined);
            bool forward_success = false;
            bool forward_error = false;
            for (uint32_t i = 0; i < forward.len; i++) {
                Atom *candidate = outcome_atom_materialize(a, &forward.items[i]);
                if (atom_is_error(candidate)) {
                    forward_error = true;
                    petta_rel_goal_add_existing_unique(a, os, &forward.items[i]);
                    continue;
                }
                if (atom_is_empty(candidate))
                    continue;
                forward_success = true;
                petta_rel_goal_add_existing_unique(a, os, &forward.items[i]);
            }
            if (forward_success || forward_error) {
                outcome_set_free(&forward);
                return;
            }
            outcome_set_free(&forward);
        } else {
            bool forward_success = false;
            for (uint32_t i = 0; i < forward.len; i++) {
                Atom *atom = outcome_atom_materialize(a, &forward.items[i]);
                if (!atom_is_empty(atom) && !atom_is_error(atom)) {
                    if (atom_contains_vars(atom))
                        continue;
                    forward_success = true;
                    break;
                }
            }
            if (forward_success) {
                for (uint32_t i = 0; i < forward.len; i++) {
                    petta_rel_goal_add_existing_unique(a, os, &forward.items[i]);
                }
                outcome_set_free(&forward);
                return;
            }
            outcome_set_free(&forward);
        }

        if (!atom_is_empty(relation_call)) {
            petta_rel_goal_eval_relation_call(s, a, relation_call,
                                              relation_result_pat, fuel,
                                              base_env, os);
            return;
        }

        petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);
        return;
    }

    if (head_id == g_builtin_syms.petta_rel_conj && nargs == 2) {
        OutcomeSet lhs;
        outcome_set_init(&lhs);
        petta_rel_goal_eval(s, a, expr_arg(goal, 0), fuel, base_env, &lhs);
        bool had_success = false;
        bool had_error = false;
        bool cut_seen = false;
        for (uint32_t i = 0; i < lhs.len; i++) {
            Atom *lhs_atom = outcome_atom_materialize(a, &lhs.items[i]);
            if (atom_is_error(lhs_atom)) {
                had_error = true;
                petta_rel_goal_add_existing_unique(a, os, &lhs.items[i]);
                continue;
            }
            if (atom_is_empty(lhs_atom) || is_false_atom(lhs_atom) ||
                atom_is_unit_literal(lhs_atom))
                continue;
            Bindings rhs_projected;
            Bindings rhs_outer;
            bindings_init(&rhs_projected);
            bindings_init(&rhs_outer);
            const Bindings *rhs_outer_env = &lhs.items[i].env;
            if (petta_rel_goal_project_env(a, expr_arg(goal, 1), &lhs.items[i].env,
                                           &rhs_projected)) {
                if (bindings_clone(&rhs_outer, &lhs.items[i].env) &&
                    bindings_try_merge_live(&rhs_outer, &rhs_projected)) {
                    rhs_outer_env = &rhs_outer;
                } else {
                    rhs_outer_env = &lhs.items[i].env;
                }
            }
            OutcomeSet rhs;
            outcome_set_init(&rhs);
            petta_rel_goal_eval(s, a, expr_arg(goal, 1), fuel,
                                rhs_outer_env, &rhs);
            BindingsBuilder merged_builder;
            bool have_merge_builder =
                bindings_builder_init(&merged_builder, &lhs.items[i].env);
            for (uint32_t j = 0; j < rhs.len; j++) {
                Atom *rhs_atom = outcome_atom_materialize(a, &rhs.items[j]);
                const Bindings *combined_rhs_env = &rhs.items[j].env;
                BindingsMergeAttempt merge_attempt = {0};
                bool merged_rhs_env = false;
                if (have_merge_builder &&
                    bindings_has_any_entries(&lhs.items[i].env)) {
                    if (!bindings_builder_merge_or_clone(&merged_builder,
                                                         &lhs.items[i].env,
                                                         &rhs.items[j].env,
                                                         &merge_attempt)) {
                        continue;
                    }
                    combined_rhs_env = merge_attempt.env;
                    merged_rhs_env = true;
                }
                if (atom_is_error(rhs_atom)) {
                    had_error = true;
                    Bindings projected_env;
                    const Bindings *visible_rhs_env = combined_rhs_env;
                    if (petta_rel_goal_project_env(a, goal, combined_rhs_env,
                                                   &projected_env)) {
                        visible_rhs_env = &projected_env;
                    }
                    petta_rel_goal_add_unique_control(a, os, rhs_atom,
                                                      visible_rhs_env,
                                                      rhs.items[j].control);
                    if (rhs.items[j].control == CETTA_OUTCOME_CONTROL_CUT)
                        cut_seen = true;
                    bindings_free(&projected_env);
                    if (merged_rhs_env)
                        bindings_merge_attempt_finish(&merged_builder, &merge_attempt);
                    continue;
                }
                if (atom_is_empty(rhs_atom) || is_false_atom(rhs_atom) ||
                    atom_is_unit_literal(rhs_atom)) {
                    if (merged_rhs_env)
                        bindings_merge_attempt_finish(&merged_builder, &merge_attempt);
                    continue;
                }
                had_success = true;
                Bindings projected_env;
                const Bindings *visible_rhs_env = combined_rhs_env;
                if (petta_rel_goal_project_env(a, goal, combined_rhs_env,
                                               &projected_env)) {
                    visible_rhs_env = &projected_env;
                }
                petta_rel_goal_add_unique_control(a, os, rhs_atom,
                                                  visible_rhs_env,
                                                  rhs.items[j].control);
                if (rhs.items[j].control == CETTA_OUTCOME_CONTROL_CUT)
                    cut_seen = true;
                bindings_free(&projected_env);
                if (merged_rhs_env)
                    bindings_merge_attempt_finish(&merged_builder, &merge_attempt);
                if (cut_seen)
                    break;
            }
            if (have_merge_builder)
                bindings_builder_free(&merged_builder);
            outcome_set_free(&rhs);
            bindings_free(&rhs_projected);
            bindings_free(&rhs_outer);
            if (cut_seen)
                break;
        }
        if (!had_success && !had_error)
            petta_rel_goal_add_unique(a, os, atom_empty(a), base_env);
        outcome_set_free(&lhs);
        return;
    }

    petta_rel_goal_add_unique(
        a, os,
        atom_error(a, goal, atom_symbol(a, "BadPettaRelationGoal")),
        base_env);
}

static SymbolId petta_runtime_relation_head_id(Arena *a, SymbolId head_id,
                                               uint32_t arity) {
    return cetta_petta_relation_head_id(a, head_id, arity);
}

typedef struct {
    Atom **items;
    uint32_t len;
} PettaRelationArgValues;

static void petta_relation_aware_emit_combos(
    Space *s, Arena *a, Atom *head, PettaRelationArgValues *args,
    uint32_t nargs, uint32_t idx, Atom **prefix_args, int fuel,
    const Bindings *prefix, bool preserve_bindings, OutcomeSet *os,
    bool *emitted) {
    if (idx == nargs) {
        Atom *call = make_call_expr(a, head, prefix_args, nargs);
        ResultSet rs;
        result_set_init(&rs);
        metta_eval(s, a, NULL, call, fuel, &rs);
        for (uint32_t i = 0; i < rs.len; i++) {
            if (atom_is_empty(rs.items[i]))
                continue;
            *emitted = true;
            outcome_set_add_prefixed(a, os, rs.items[i], NULL, prefix,
                                     preserve_bindings);
        }
        result_set_free(&rs);
        return;
    }

    for (uint32_t i = 0; i < args[idx].len; i++) {
        prefix_args[idx] = args[idx].items[i];
        petta_relation_aware_emit_combos(
            s, a, head, args, nargs, idx + 1, prefix_args, fuel,
            prefix, preserve_bindings, os, emitted);
    }
}

static bool petta_eval_open_callable_relation_values(
    Space *s, Arena *a, Atom *term, int fuel,
    const Bindings *prefix, bool preserve_bindings, OutcomeSet *os) {
    if (!language_is_petta() || !s || !term ||
        term->kind != ATOM_EXPR || term->expr.len == 0 ||
        !atom_contains_vars(term))
        return false;

    Atom *head = term->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;

    uint32_t nargs = term->expr.len - 1;
    SymbolId relation_head_id =
        petta_runtime_relation_head_id(a, head->sym_id, nargs);
    if (space_equations_may_match_known_head(s, relation_head_id)) {
        char actual_name[96];
        snprintf(actual_name, sizeof(actual_name),
                 "__petta_lambda_rel_actual_%u", fresh_var_suffix());
        Atom *actual = atom_var(a, actual_name);

        Atom **export_items = nargs > 0
            ? arena_alloc(a, sizeof(Atom *) * nargs)
            : NULL;
        for (uint32_t i = 0; i < nargs; i++)
            export_items[i] = term->expr.elems[i + 1];
        Atom *export_pack = atom_expr(a, export_items, nargs);

        Atom **rel_args = arena_alloc(a, sizeof(Atom *) * (nargs + 2));
        for (uint32_t i = 0; i < nargs; i++)
            rel_args[i] = term->expr.elems[i + 1];
        rel_args[nargs] = actual;
        rel_args[nargs + 1] = export_pack;
        Atom *relation_call =
            make_call_expr(a, atom_symbol_id(a, relation_head_id),
                           rel_args, nargs + 2);

        Atom *goal_args[] = { term, relation_call, actual };
        Atom *goal =
            make_call_expr(a, atom_symbol_id(a, g_builtin_syms.petta_rel_call),
                           goal_args, 3);

        OutcomeSet rel;
        outcome_set_init(&rel);
        petta_rel_goal_eval(s, a, goal, fuel, prefix, &rel);

        bool emitted = false;
        for (uint32_t i = 0; i < rel.len; i++) {
            Atom *result = outcome_atom_materialize(a, &rel.items[i]);
            if (atom_is_empty(result))
                continue;
            emitted = true;
            if (atom_is_error(result)) {
                outcome_set_add_prefixed_outcome(a, os, &rel.items[i], prefix,
                                                 preserve_bindings);
                continue;
            }
            Atom *value = result;
            if (result->kind == ATOM_EXPR && result->expr.len == 2)
                value = result->expr.elems[0];
            outcome_set_add_prefixed(a, os, value, &rel.items[i].env, prefix,
                                     preserve_bindings);
        }
        outcome_set_free(&rel);
        if (emitted)
            return true;
    }

    PettaRelationArgValues *arg_values =
        nargs > 0 ? arena_alloc(a, sizeof(PettaRelationArgValues) * nargs) : NULL;
    bool used_relation_arg = false;
    for (uint32_t i = 0; i < nargs; i++) {
        Atom *arg = term->expr.elems[i + 1];
        arg_values[i].items = NULL;
        arg_values[i].len = 0;
        if (arg && arg->kind == ATOM_EXPR && atom_contains_vars(arg)) {
            OutcomeSet arg_os;
            outcome_set_init(&arg_os);
            if (petta_eval_open_callable_relation_values(
                    s, a, arg, fuel, prefix, preserve_bindings, &arg_os)) {
                arg_values[i].items =
                    arena_alloc(a, sizeof(Atom *) * (arg_os.len ? arg_os.len : 1));
                for (uint32_t j = 0; j < arg_os.len; j++) {
                    Atom *value = outcome_atom_materialize(a, &arg_os.items[j]);
                    if (atom_is_empty(value))
                        continue;
                    if (atom_is_error(value)) {
                        outcome_set_add_prefixed_outcome(a, os, &arg_os.items[j],
                                                         prefix, preserve_bindings);
                        outcome_set_free(&arg_os);
                        return true;
                    }
                    arg_values[i].items[arg_values[i].len++] = value;
                }
                outcome_set_free(&arg_os);
                if (arg_values[i].len == 0)
                    return false;
                used_relation_arg = true;
                continue;
            }
            outcome_set_free(&arg_os);
        }

        ResultSet rs;
        result_set_init(&rs);
        metta_eval(s, a, NULL, arg, fuel, &rs);
        arg_values[i].items =
            arena_alloc(a, sizeof(Atom *) * (rs.len ? rs.len : 1));
        for (uint32_t j = 0; j < rs.len; j++) {
            if (!atom_is_empty(rs.items[j]))
                arg_values[i].items[arg_values[i].len++] = rs.items[j];
        }
        result_set_free(&rs);
        if (arg_values[i].len == 0)
            arg_values[i].items[arg_values[i].len++] =
                bindings_apply_if_vars(prefix, a, arg);
    }

    if (!used_relation_arg)
        return false;

    Atom **prefix_args = nargs > 0 ? arena_alloc(a, sizeof(Atom *) * nargs) : NULL;
    bool emitted = false;
    petta_relation_aware_emit_combos(s, a, head, arg_values, nargs, 0,
                                     prefix_args, fuel, prefix,
                                     preserve_bindings, os, &emitted);
    return emitted;
}

static __attribute__((unused)) void
eval_direct_for_current(Space *s, Arena *a, Atom *type, Atom *atom,
                        int fuel, const Bindings *outer_env,
                        bool preserve_bindings, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    eval_direct_outcomes(s, a, type, atom, fuel, &inner);
    outcome_set_append_prefixed_move(a, os, &inner, outer_env, preserve_bindings);
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
    Atom *bound_arg = bindings_apply_if_vars(env, a, orig_arg);
    CettaFunctionArgPolicy policy =
        (language_is_petta() && arg_types[idx] &&
         arg_types[idx]->kind == ATOM_VAR)
            ? CETTA_FUNCTION_ARG_POLICY_RAW
            : language_function_arg_policy(op, idx, arg_type);
    if (policy == CETTA_FUNCTION_ARG_POLICY_RAW || orig_arg->kind == ATOM_VAR) {
        prefix[idx] = bound_arg;
        if (eval_dependent_telescope_enabled()) {
            BindingsBuilder merged;
            if (!bindings_builder_init(&merged, env))
                return;
            if (!bind_domain_binder_builder(&merged, arg_types[idx], bound_arg)) {
                bindings_builder_free(&merged);
                return;
            }
            interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                    idx + 1, prefix,
                                    (const Bindings *)bindings_builder_bindings(&merged),
                                    fuel, os);
            bindings_builder_free(&merged);
        } else {
            interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                    idx + 1, prefix, env, fuel, os);
        }
        return;
    }

    OutcomeSet arg_os;
    outcome_set_init(&arg_os);
    if (policy == CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL)
        metta_eval_bind(s, a, bound_arg, fuel, &arg_os);
    else
        metta_eval_bind_typed(s, a, arg_type, bound_arg, fuel, &arg_os);

    BindingsBuilder merged_builder;
    if (!bindings_builder_init(&merged_builder, env)) {
        outcome_set_free(&arg_os);
        return;
    }
    for (uint32_t i = 0; i < arg_os.len; i++) {
        BindingsMergeAttempt attempt;
        Atom *arg_atom =
            outcome_atom_materialize_traced(
                a, &arg_os.items[i],
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_DISPATCH_CALL_TERM);
        if (!bindings_builder_merge_or_clone(&merged_builder, env,
                                             &arg_os.items[i].env, &attempt))
            continue;
        if (atom_is_empty_or_error(arg_atom) &&
            !atom_eq(arg_atom, orig_arg)) {
            outcome_set_add(os, arg_atom, attempt.env);
            bindings_merge_attempt_finish(&merged_builder, &attempt);
            continue;
        }
        prefix[idx] = arg_atom;
        if (attempt.used_builder) {
            if (bind_domain_binder_builder(&merged_builder, arg_types[idx],
                                           arg_atom)) {
                interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                        idx + 1, prefix,
                                        (const Bindings *)bindings_builder_bindings(&merged_builder),
                                        fuel, os);
            }
        } else {
            BindingsBuilder attempt_builder;
            bindings_builder_init_owned(&attempt_builder, &attempt.owned);
            if (bind_domain_binder_builder(&attempt_builder, arg_types[idx],
                                           arg_atom)) {
                interpret_function_args(s, a, op, orig_args, arg_types, nargs,
                                        idx + 1, prefix,
                                        (const Bindings *)bindings_builder_bindings(&attempt_builder),
                                        fuel, os);
            }
            bindings_builder_free(&attempt_builder);
        }
        bindings_merge_attempt_finish(&merged_builder, &attempt);
    }
    bindings_builder_free(&merged_builder);
    outcome_set_free(&arg_os);
}

static void interpret_surface_args_with_policy(Space *s, Arena *a, Atom *head,
                                               Atom **orig_args, Atom **arg_types,
                                               uint32_t nargs, const Bindings *env,
                                               int fuel, OutcomeSet *os) {
    Atom **prefix = arena_alloc(a, sizeof(Atom *) * nargs);
    interpret_function_args(s, a, head, orig_args, arg_types, nargs, 0, prefix,
                            env, fuel, os);
}

static bool direct_surface_fill_arg_types(Space *s, Arena *a, Atom *op,
                                          uint32_t nargs, Atom **arg_types) {
    if (!arg_types || nargs == 0)
        return true;
    for (uint32_t i = 0; i < nargs; i++)
        arg_types[i] = atom_undefined_type(a);
    if (nargs > 32)
        return false;

    Atom **op_types = NULL;
    uint32_t n_op_types = get_atom_types_profiled(s, a, op, &op_types);
    Atom *selected_ft = NULL;
    CettaFunctionArgPolicy selected_policies[32];
    bool selected_direct_profile = false;

    for (uint32_t ti = 0; ti < n_op_types; ti++) {
        Atom *ft = op_types[ti];
        if (!is_function_type(ft))
            continue;
        if (ft->kind != ATOM_EXPR || ft->expr.len < 2 ||
            ft->expr.len - 2 != nargs)
            continue;

        Atom *fresh_ft = atom_freshen_epoch(a, ft, fresh_var_suffix());
        Atom *candidate_arg_types[32];
        if (get_function_arg_types(fresh_ft, candidate_arg_types, 32) != nargs)
            continue;

        if (!selected_direct_profile) {
            selected_ft = fresh_ft;
            for (uint32_t ai = 0; ai < nargs; ai++) {
                selected_policies[ai] =
                    language_function_arg_policy(op, ai, candidate_arg_types[ai]);
            }
            selected_direct_profile = true;
            continue;
        }

        bool compatible = true;
        for (uint32_t ai = 0; ai < nargs; ai++) {
            CettaFunctionArgPolicy candidate_policy =
                language_function_arg_policy(op, ai, candidate_arg_types[ai]);
            if (candidate_policy != selected_policies[ai]) {
                compatible = false;
                break;
            }
        }
        if (!compatible) {
            selected_direct_profile = false;
            selected_ft = NULL;
            break;
        }
    }

    if (selected_direct_profile && selected_ft)
        (void)get_function_arg_types(selected_ft, arg_types, 32);
    free(op_types);
    return selected_direct_profile;
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
                                      Bindings *tail_env,
                                      OutcomeSet *os) {
    if (!g_library_context || !g_library_context->foreign_runtime ||
        !cetta_foreign_is_callable_head(g_library_context->foreign_runtime,
                                        head)) {
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
        *tail_next = rs.items[0];
        *tail_type = result_type;
        bindings_copy(tail_env, prefix);
        result_set_free(&rs);
        return true;
    }

    for (uint32_t i = 0; i < rs.len; i++)
        eval_for_caller(s, a, result_type, rs.items[i], fuel, prefix,
                        preserve_bindings, os);
    result_set_free(&rs);
    return true;
}

static bool dispatch_direct_surface_no_type(Space *s, Arena *a,
                                            Atom *op, Atom *atom,
                                            int fuel,
                                            const Bindings *current_env,
                                            bool preserve_bindings,
                                            Atom **tail_next,
                                            Atom **tail_type,
                                            Bindings *tail_env,
                                            OutcomeSet *os) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return false;

    Atom *resolved_op = (!current_env || current_env->len == 0)
        ? op
        : bindings_apply_if_vars(current_env, a, op);
    resolved_op = resolve_registry_refs(a, resolved_op);
    resolved_op = materialize_runtime_token(s, a, resolved_op);

    bool native_candidate =
        resolved_op && resolved_op->kind == ATOM_SYMBOL &&
        atom_is_native_dispatch_head(resolved_op);
    bool foreign_candidate =
        g_library_context && g_library_context->foreign_runtime &&
        cetta_foreign_is_callable_head(g_library_context->foreign_runtime,
                                       resolved_op);
    if (!native_candidate && !foreign_candidate && !is_capture_closure(resolved_op))
        return false;

    uint32_t nargs = atom->expr.len - 1;
    Atom **arg_types = nargs ? arena_alloc(a, sizeof(Atom *) * nargs) : NULL;
    if (nargs > 0)
        (void)direct_surface_fill_arg_types(s, a, resolved_op, nargs, arg_types);

    OutcomeSet call_terms;
    outcome_set_init(&call_terms);
    Atom **prefix = nargs ? arena_alloc(a, sizeof(Atom *) * nargs) : NULL;
    interpret_function_args(s, a, resolved_op, atom->expr.elems + 1, arg_types,
                            nargs, 0, prefix, current_env, fuel, &call_terms);

    bool handled = false;
    for (uint32_t ci = 0; ci < call_terms.len; ci++) {
        Atom *call_atom = outcome_atom_materialize(a, &call_terms.items[ci]);
        Bindings *combo_ctx = &call_terms.items[ci].env;
        if (!call_atom || call_atom->kind != ATOM_EXPR || call_atom->expr.len == 0)
            continue;

        Atom *h = call_atom->expr.elems[0];
        if (is_capture_closure(h)) {
            dispatch_capture_outcomes(s, a, h,
                                      call_atom->expr.elems + 1,
                                      call_atom->expr.len - 1,
                                      fuel, combo_ctx, preserve_bindings, os);
            handled = true;
            continue;
        }
        if (dispatch_foreign_outcomes(s, a, h,
                                      call_atom->expr.elems + 1,
                                      call_atom->expr.len - 1,
                                      NULL, fuel, combo_ctx, false,
                                      preserve_bindings,
                                      tail_next, tail_type, tail_env, os)) {
            handled = true;
            continue;
        }
        if (dispatch_petta_equals_native_outcomes(
                s, a, h, call_atom->expr.elems + 1, call_atom->expr.len - 1,
                NULL, fuel, combo_ctx, preserve_bindings, os)) {
            handled = true;
            continue;
        }
        if (atom_is_native_dispatch_head(h)) {
            Atom *result = dispatch_native_op(s, a, h,
                                              call_atom->expr.elems + 1,
                                              call_atom->expr.len - 1);
            if (result) {
                eval_for_caller(s, a, NULL, result, fuel, combo_ctx,
                                preserve_bindings, os);
                handled = true;
            }
        }
    }
    outcome_set_free(&call_terms);
    return handled;
}

static Atom *dispatch_observed_arg(Arena *a, const Bindings *current_env,
                                   Atom *arg) {
    if (!arg || !current_env || !bindings_has_any_entries(current_env))
        return arg;
    return bindings_apply_if_vars(current_env, a, arg);
}

static bool petta_expr_head_is_callable_prefix(Space *s, Atom *head) {
    if (!head || head->kind != ATOM_EXPR || head->expr.len == 0)
        return false;

    if (petta_partial_value_unpack(head, NULL, NULL, NULL))
        return true;

    Atom *op = head->expr.elems[0];
    if (!op)
        return false;
    if (op->kind == ATOM_SYMBOL) {
        if (op->sym_id == g_builtin_syms.arrow)
            return false;
        if (petta_internal_head_is_name(
                op, cetta_petta_lambda_closure_head_name()))
            return true;
        if (is_grounded_op(op->sym_id) ||
            symbol_id_is_builtin_surface(op->sym_id) ||
            space_equations_may_match_known_head(s, op->sym_id)) {
            return true;
        }
        if (g_library_context && g_library_context->foreign_runtime &&
            cetta_foreign_is_callable_head(g_library_context->foreign_runtime,
                                           op)) {
            return true;
        }
        return false;
    }
    if (is_capture_closure(op))
        return true;
    return petta_expr_head_is_callable_prefix(s, op);
}

static bool petta_symbol_is_callable_value(Space *s, Atom *head) {
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    if (head->sym_id == g_builtin_syms.arrow)
        return false;
    if (atom_is_native_dispatch_head(head) ||
        symbol_id_is_builtin_surface(head->sym_id) ||
        space_equations_may_match_known_head(s, head->sym_id)) {
        return true;
    }
    return g_library_context && g_library_context->foreign_runtime &&
           cetta_foreign_is_callable_head(g_library_context->foreign_runtime,
                                          head);
}

static Atom *petta_callable_apply_atom(Space *s, Arena *a, Atom *callable,
                                       Atom **args, uint32_t nargs) {
    Atom *partial_head = NULL;
    Atom **bound_args = NULL;
    uint32_t bound_len = 0;

    if (!language_is_petta() || !callable)
        return NULL;

    if (petta_partial_value_unpack(callable, &partial_head,
                                   &bound_args, &bound_len)) {
        uint32_t total = bound_len + nargs;
        Atom **all_args = total > 0
            ? arena_alloc(a, sizeof(Atom *) * total)
            : NULL;
        for (uint32_t i = 0; i < bound_len; i++)
            all_args[i] = bound_args[i];
        for (uint32_t i = 0; i < nargs; i++)
            all_args[bound_len + i] = args[i];
        Atom *nested = petta_callable_apply_atom(s, a, partial_head,
                                                 all_args, total);
        return nested ? nested : make_call_expr(a, partial_head, all_args, total);
    }

    if (callable->kind == ATOM_EXPR &&
        callable->expr.len >= 1 &&
        petta_internal_head_is_name(callable->expr.elems[0],
                                    cetta_petta_lambda_closure_head_name())) {
        uint32_t prefix_len = callable->expr.len;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * (prefix_len + nargs));
        for (uint32_t i = 0; i < prefix_len; i++)
            elems[i] = callable->expr.elems[i];
        for (uint32_t i = 0; i < nargs; i++)
            elems[prefix_len + i] = args[i];
        return atom_expr(a, elems, prefix_len + nargs);
    }

    if (callable->kind == ATOM_EXPR &&
        petta_expr_head_is_callable_prefix(s, callable)) {
        uint32_t prefix_len = callable->expr.len;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * (prefix_len + nargs));
        for (uint32_t i = 0; i < prefix_len; i++)
            elems[i] = callable->expr.elems[i];
        for (uint32_t i = 0; i < nargs; i++)
            elems[prefix_len + i] = args[i];
        return atom_expr(a, elems, prefix_len + nargs);
    }

    if (petta_symbol_is_callable_value(s, callable) || is_capture_closure(callable))
        return make_call_expr(a, callable, args, nargs);

    return NULL;
}

static Atom *petta_flatten_higher_order_call(Space *s, Arena *a, Atom *call_atom) {
    if (!language_is_petta() ||
        !call_atom ||
        call_atom->kind != ATOM_EXPR ||
        call_atom->expr.len < 2) {
        return NULL;
    }

    Atom *head = call_atom->expr.elems[0];
    Atom *applied = petta_callable_apply_atom(s, a, head,
                                              call_atom->expr.elems + 1,
                                              call_atom->expr.len - 1);
    if (!applied || atom_eq(applied, call_atom))
        return NULL;
    return applied;
}

static bool petta_eval_callable_single(Space *s, Arena *a, Atom *callable,
                                       Atom **args, uint32_t nargs, int fuel,
                                       Atom *error_call, Atom **out,
                                       Atom **error_out) {
    Atom *call = petta_callable_apply_atom(s, a, callable, args, nargs);
    if (!call) {
        if (error_out)
            *error_out = atom_error(a, error_call ? error_call : callable,
                                    atom_symbol(a, "PettaCallableExpected"));
        return false;
    }

    OutcomeSet results;
    outcome_set_init(&results);
    metta_eval_bind(s, a, call, fuel, &results);

    Atom *result = NULL;
    for (uint32_t i = 0; i < results.len; i++) {
        Atom *candidate = outcome_atom_materialize(a, &results.items[i]);
        if (atom_is_empty(candidate))
            continue;
        if (atom_is_error(candidate)) {
            if (error_out)
                *error_out = candidate;
            outcome_set_free(&results);
            return false;
        }
        if (result) {
            if (error_out)
                *error_out = atom_error(a, error_call ? error_call : call,
                                        atom_symbol(a, "PettaCallableMultipleResults"));
            outcome_set_free(&results);
            return false;
        }
        result = atom_deep_copy(a, candidate);
    }
    outcome_set_free(&results);

    if (!result) {
        if (error_out)
            *error_out = atom_error(a, error_call ? error_call : call,
                                    atom_symbol(a, "PettaCallableNoResult"));
        return false;
    }
    if (out)
        *out = result;
    return true;
}

static bool dispatch_petta_repr_outcomes(Space *s, Arena *a, Atom *atom,
                                         int fuel, const Bindings *prefix,
                                         bool preserve_bindings,
                                         OutcomeSet *os) {
    if (!language_is_petta() || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 2)
        return false;
    const char *head_name = atom_name_cstr(atom->expr.elems[0]);
    if (!head_name ||
        (strcmp(head_name, "repr") != 0 && strcmp(head_name, "repra") != 0))
        return false;

    OutcomeSet values;
    outcome_set_init(&values);
    eval_for_caller(s, a, NULL, expr_arg(atom, 0), fuel, prefix,
                    preserve_bindings, &values);

    Bindings empty;
    bindings_init(&empty);
    for (uint32_t i = 0; i < values.len; i++) {
        Atom *value = outcome_atom_materialize(a, &values.items[i]);
        if (atom_is_empty(value))
            continue;
        if (atom_is_error(value)) {
            outcome_set_add_existing(os, &values.items[i]);
            continue;
        }
        value = petta_callable_repr_surface_atom(a, value);
        char *rendered = atom_to_parseable_string(a, value);
        outcome_set_add(os, atom_string(a, rendered), &empty);
    }
    outcome_set_free(&values);
    return true;
}

static bool dispatch_petta_callable_map_outcomes(Space *s, Arena *a, Atom *atom,
                                                 int fuel,
                                                 const Bindings *prefix,
                                                 OutcomeSet *os) {
    if (!language_is_petta() || !atom || atom->kind != ATOM_EXPR)
        return false;

    const char *head_name = atom_name_cstr(atom->expr.elems[0]);
    uint32_t nargs = atom->expr.len > 0 ? atom->expr.len - 1 : 0;
    bool is_maplist = head_name && strcmp(head_name, "maplist") == 0 && nargs == 2;
    bool is_map_atom_callable =
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.map_atom) &&
        nargs == 2;
    bool is_filter_atom_callable =
        atom_is_symbol_id(atom->expr.elems[0], g_builtin_syms.filter_atom) &&
        nargs == 2;
    if (!is_maplist && !is_map_atom_callable && !is_filter_atom_callable)
        return false;

    Atom *callable_expr = is_maplist ? expr_arg(atom, 0) : expr_arg(atom, 1);
    Atom *list_expr = is_maplist ? expr_arg(atom, 1) : expr_arg(atom, 0);
    callable_expr = bindings_apply_if_vars(prefix, a, callable_expr);
    list_expr = bindings_apply_if_vars(prefix, a, list_expr);

    OutcomeSet callables;
    OutcomeSet lists;
    outcome_set_init(&callables);
    outcome_set_init(&lists);
    metta_eval_bind(s, a, callable_expr, fuel, &callables);
    metta_eval_bind(s, a, list_expr, fuel, &lists);

    Bindings empty;
    bindings_init(&empty);
    for (uint32_t ci = 0; ci < callables.len; ci++) {
        Atom *callable = outcome_atom_materialize(a, &callables.items[ci]);
        if (atom_is_empty(callable))
            continue;
        if (atom_is_error(callable)) {
            outcome_set_add_existing(os, &callables.items[ci]);
            continue;
        }
        for (uint32_t li = 0; li < lists.len; li++) {
            Atom *list = outcome_atom_materialize(a, &lists.items[li]);
            if (atom_is_empty(list))
                continue;
            if (atom_is_error(list)) {
                outcome_set_add_existing(os, &lists.items[li]);
                continue;
            }
            if (!list || list->kind != ATOM_EXPR) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "ExpressionAtomExpected")),
                    &empty);
                continue;
            }
            Atom **mapped = list->expr.len > 0
                ? arena_alloc(a, sizeof(Atom *) * list->expr.len)
                : NULL;
            uint32_t mapped_len = 0;
            bool ok = true;
            for (uint32_t i = 0; i < list->expr.len; i++) {
                Atom *arg = list->expr.elems[i];
                Atom *result = NULL;
                Atom *error = NULL;
                if (!petta_eval_callable_single(s, a, callable, &arg, 1, fuel,
                                                atom, &result, &error)) {
                    outcome_set_add(os, error, &empty);
                    ok = false;
                    break;
                }
                if (is_filter_atom_callable) {
                    if (atom_is_empty(result) || is_false_atom(result))
                        continue;
                    mapped[mapped_len++] = arg;
                    continue;
                }
                mapped[mapped_len++] = result;
            }
            if (ok)
                outcome_set_add(os, atom_expr(a, mapped, mapped_len), &empty);
        }
    }
    outcome_set_free(&lists);
    outcome_set_free(&callables);
    return true;
}

static bool dispatch_petta_forall_outcomes(Space *s, Arena *a, Atom *atom,
                                           int fuel, const Bindings *prefix,
                                           bool preserve_bindings,
                                           OutcomeSet *os) {
    if (!language_is_petta() || !atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3)
        return false;
    const char *head_name = atom_name_cstr(atom->expr.elems[0]);
    if (!head_name || strcmp(head_name, "forall") != 0)
        return false;

    Atom *generator_expr = bindings_apply_if_vars(prefix, a, expr_arg(atom, 0));
    Atom *test_expr = bindings_apply_if_vars(prefix, a, expr_arg(atom, 1));

    OutcomeSet tests;
    outcome_set_init(&tests);
    metta_eval_bind(s, a, test_expr, fuel, &tests);

    Bindings empty;
    bindings_init(&empty);
    OutcomeSet generators;
    outcome_set_init(&generators);
    ResultSet gen_values;
    result_set_init(&gen_values);
    metta_eval(s, a, NULL, generator_expr, fuel, &gen_values);
    for (uint32_t i = 0; i < gen_values.len; i++) {
        if (!atom_is_empty(gen_values.items[i]))
            outcome_set_add(&generators, gen_values.items[i], &empty);
    }
    result_set_free(&gen_values);
    if (generators.len == 0 && atom_contains_vars(generator_expr)) {
        (void)petta_eval_open_callable_relation_values(
            s, a, generator_expr, fuel, prefix, preserve_bindings, &generators);
    }

    bool all_ok = true;
    bool had_error = false;
    for (uint32_t gi = 0; gi < generators.len && all_ok; gi++) {
        Atom *item = outcome_atom_materialize(a, &generators.items[gi]);
        if (atom_is_empty(item))
            continue;
        if (atom_is_error(item)) {
            outcome_set_add_existing(os, &generators.items[gi]);
            had_error = true;
            all_ok = false;
            break;
        }
        bool item_ok = false;
        for (uint32_t ti = 0; ti < tests.len; ti++) {
            Atom *callable = outcome_atom_materialize(a, &tests.items[ti]);
            if (atom_is_empty(callable))
                continue;
            if (atom_is_error(callable)) {
                outcome_set_add_existing(os, &tests.items[ti]);
                had_error = true;
                all_ok = false;
                break;
            }
            Atom *truth = NULL;
            Atom *error = NULL;
            if (!petta_eval_callable_single(s, a, callable, &item, 1, fuel,
                                            atom, &truth, &error)) {
                if (error)
                    outcome_set_add(os, error, &empty);
                all_ok = false;
                break;
            }
            if (atom_is_empty(truth) || is_false_atom(truth)) {
                all_ok = false;
                break;
            }
            item_ok = true;
            break;
        }
        if (!item_ok)
            all_ok = false;
    }

    if (!had_error)
        outcome_set_add(os, all_ok ? atom_true(a) : atom_false(a), &empty);
    outcome_set_free(&generators);
    outcome_set_free(&tests);
    return true;
}

static bool try_petta_returned_callable_prefix_dispatch(
    Space *s, Arena *a, Atom *call_atom, int fuel,
    const Bindings *base_env, bool preserve_bindings, OutcomeSet *os) {
    if (!language_is_petta() || !s || !call_atom ||
        call_atom->kind != ATOM_EXPR || call_atom->expr.len < 2)
        return false;
    Atom *head = call_atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;

    uint32_t nargs = call_atom->expr.len - 1;
    bool handled = false;
    for (uint32_t prefix_n = nargs; prefix_n > 0; prefix_n--) {
        uint32_t bound_n = prefix_n - 1;
        if (!space_equations_have_head_with_arity(s, head->sym_id, bound_n))
            continue;
        Atom *prefix_call = make_call_expr(a, head,
                                           call_atom->expr.elems + 1,
                                           bound_n);
        OutcomeSet prefixes;
        outcome_set_init(&prefixes);
        eval_for_caller(s, a, NULL, prefix_call, fuel, base_env,
                        preserve_bindings, &prefixes);
        for (uint32_t i = 0; i < prefixes.len; i++) {
            Atom *callable = outcome_atom_materialize(a, &prefixes.items[i]);
            if (atom_is_empty(callable) || atom_is_error(callable))
                continue;
            Atom *applied = petta_callable_apply_atom(
                s, a, callable,
                call_atom->expr.elems + 1 + bound_n,
                nargs - bound_n);
            if (!applied || atom_eq(applied, call_atom))
                continue;
            eval_for_caller(s, a, NULL, applied, fuel, &prefixes.items[i].env,
                            preserve_bindings, os);
            handled = true;
        }
        outcome_set_free(&prefixes);
        if (handled)
            return true;
    }
    return false;
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
        if (outcome_atom_is_empty_or_error(a, &heads.items[hi]))
            continue;
        Atom *head_atom =
            outcome_atom_materialize_traced(
                a, &heads.items[hi],
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_DISPATCH_HEAD);
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
        Atom *head_atom =
            outcome_atom_materialize_traced(
                a, &heads.items[hi],
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_DISPATCH_HEAD);
        Bindings *head_env = &heads.items[hi].env;

        if (atom_is_empty_or_error(head_atom)) {
            outcome_set_add_move(os, head_atom, head_env);
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
                outcome_set_add(os, errors[ei], head_env);
            for (uint32_t si = 0; si < n_succs; si++)
                bindings_free(&succs[si]);
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
                                expr_narg, 0, prefix, head_env, fuel, &call_terms);

        for (uint32_t ci = 0; ci < call_terms.len; ci++) {
            Atom *call_atom =
                outcome_atom_materialize_traced(
                    a, &call_terms.items[ci],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_DISPATCH_CALL_TERM);
            Bindings *combo_ctx = &call_terms.items[ci].env;
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 1 &&
                is_capture_closure(call_atom->expr.elems[0])) {
                dispatch_capture_outcomes(s, a, call_atom->expr.elems[0],
                                          call_atom->expr.elems + 1,
                                          call_atom->expr.len - 1,
                                          fuel, combo_ctx, preserve_bindings, os);
            } else {
                outcome_set_add_existing_move(os, &call_terms.items[ci]);
            }
        }
        outcome_set_free(&call_terms);
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

static bool outcome_atom_is_error_at_site(Arena *a, Outcome *out,
                                          CettaRuntimeCounter site_counter) {
    Atom *candidate;
    Atom *head;
    if (!out)
        return false;
    if (out->materialized_atom)
        return atom_is_error(out->materialized_atom);
    candidate = out->atom;
    if (!candidate)
        return false;
    if (candidate->kind == ATOM_EXPR && candidate->expr.len > 0) {
        head = candidate->expr.elems[0];
        if (head && head->kind == ATOM_SYMBOL)
            return atom_is_symbol_id(head, g_builtin_syms.error);
    } else if (candidate->kind != ATOM_VAR) {
        return false;
    }
    return atom_is_error(outcome_atom_materialize_traced(a, out, site_counter));
}

static void interpret_tuple(Space *s, Arena *a,
                            Atom **orig_elems, uint32_t len,
                            uint32_t idx, Atom **prefix,
                            Bindings *ctx, const VariantInstance *prefix_variant,
                            int fuel, ResultBindSet *rbs) {
    if (idx == len) {
        Atom *tuple_atom = atom_expr(a, prefix, len);
        if (prefix_variant && variant_instance_present(prefix_variant)) {
            outcome_set_add_with_variant(rbs, tuple_atom, ctx, prefix_variant);
        } else {
            rb_set_add(rbs, tuple_atom, ctx);
        }
        return;
    }
    /* Apply accumulated bindings to this element before evaluating */
    Atom *elem = bindings_apply_if_vars(ctx, a, orig_elems[idx]);
    ResultBindSet sub;
    rb_set_init(&sub);
    metta_eval_bind(s, a, elem, fuel, &sub);
    if (sub.len == 0) {
        outcome_set_free(&sub);
        return;
    }
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder merged_builder;
    if (!bindings_builder_init(&merged_builder, ctx)) {
        outcome_set_free(&sub);
        return;
    }
    for (uint32_t i = 0; i < sub.len; i++) {
        Atom *sub_atom = sub.items[i].atom;
        Atom *effective_atom = sub_atom;
        const VariantInstance *next_variant_ref = prefix_variant;
        VariantInstance next_variant;
        bool owns_next_variant = false;

        if (atom_is_empty(sub_atom) ||
            outcome_atom_is_error_at_site(
                a, &sub.items[i],
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_INTERPRET_TUPLE)) {
            effective_atom = outcome_atom_materialize_traced(
                a, &sub.items[i],
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_INTERPRET_TUPLE);
            rb_set_add(rbs, effective_atom, &empty);
            continue;
        }
        if (variant_instance_present(&sub.items[i].variant)) {
            variant_instance_init(&next_variant);
            if (prefix_variant && variant_instance_present(prefix_variant) &&
                !variant_instance_clone(&next_variant, prefix_variant)) {
                continue;
            }
            if (!variant_instance_append_rebased(a, &next_variant, &effective_atom,
                                                 sub_atom, &sub.items[i].variant)) {
                variant_instance_free(&next_variant);
                continue;
            }
            next_variant_ref = &next_variant;
            owns_next_variant = true;
        }
        if (atom_is_empty(effective_atom) || atom_is_error(effective_atom)) {
            if (owns_next_variant)
                variant_instance_free(&next_variant);
            rb_set_add(rbs, effective_atom, &empty);
        } else {
            BindingsMergeAttempt attempt;
            if (!bindings_builder_merge_or_clone(&merged_builder, ctx,
                                                 &sub.items[i].env, &attempt))
                goto next_item;
            prefix[idx] = effective_atom;
            interpret_tuple(s, a, orig_elems, len, idx + 1, prefix,
                            (Bindings *)attempt.env, next_variant_ref, fuel, rbs);
            bindings_merge_attempt_finish(&merged_builder, &attempt);
        }
next_item:
        if (owns_next_variant)
            variant_instance_free(&next_variant);
    }
    bindings_builder_free(&merged_builder);
    outcome_set_free(&sub);
}

static bool petta_match_template_raw_result(Atom *template, Atom *result,
                                            Atom **raw_out) {
    if (!language_is_petta() || !raw_out)
        return false;
    if (template && template->kind == ATOM_VAR) {
        *raw_out = result;
        return true;
    }
    if (template && template->kind == ATOM_EXPR && template->expr.len == 2 &&
        atom_is_symbol_id(template->expr.elems[0], g_builtin_syms.quote)) {
        if (result && result->kind == ATOM_EXPR && result->expr.len == 2 &&
            atom_is_symbol_id(result->expr.elems[0], g_builtin_syms.quote)) {
            *raw_out = result->expr.elems[1];
        } else {
            *raw_out = result;
        }
        return true;
    }
    return false;
}

static bool petta_match_raw_result_needs_surface_eval(Atom *raw_result) {
    return language_is_petta() &&
           raw_result &&
           raw_result->kind == ATOM_EXPR &&
           raw_result->expr.len == 4 &&
           atom_is_symbol_id(raw_result->expr.elems[0], g_builtin_syms.let);
}

static __attribute__((noinline)) bool
handle_match(Space *s, Arena *a, Atom *atom, int fuel,
             const Bindings *current_env, bool preserve_bindings,
             OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    uint32_t nargs = expr_nargs(atom);
    if (atom_head_symbol_id(atom) != g_builtin_syms.match || nargs != 3) return false;

    Atom *space_ref = expr_arg(atom, 0);
    Atom *pattern = expr_arg(atom, 1);
    Atom *template = expr_arg(atom, 2);
    if (current_env && bindings_has_any_entries(current_env)) {
        space_ref = bindings_apply_if_vars(current_env, a, space_ref);
        pattern = bindings_apply_if_vars(current_env, a, pattern);
        template = bindings_apply_if_vars(current_env, a, template);
    }
    Atom *mork_args[] = { space_ref, pattern, template };
    if (emit_generic_mork_handle_match_surface(s, a, atom, mork_args, fuel, os)) {
        return true;
    }
    pattern = resolve_registry_refs(a, pattern);
    template = resolve_registry_refs(a, template);
    Atom *mork_handle_error = guard_mork_handle_surface(
        s, a, atom, space_ref, fuel, "match", "mork:match");
    if (mork_handle_error) {
        outcome_set_add(os, mork_handle_error, &_empty);
        return true;
    }
    Space *ms = resolve_single_space_arg(s, a, space_ref, fuel);
    if (g_registry && !ms) {
        outcome_set_add(os, space_arg_error(a, atom,
            "match expects a space as the first argument"), &_empty);
        return true;
    }
    if (!ms) ms = s;
    Atom *mork_error = guard_mork_space_surface(
        a, atom, ms, "match", "mork:match");
    if (mork_error) {
        outcome_set_add(os, mork_error, &_empty);
        return true;
    }

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        uint32_t n_conjuncts = pattern->expr.len - 1;
        BindingSet matches;
        MatchVisibleVarSet visible;
        match_visible_var_set_init(&visible);
        if (!collect_match_visible_vars_rec(pattern, &visible)) {
            match_visible_var_set_free(&visible);
            return true;
        }
        space_query_conjunction(ms, a, pattern->expr.elems + 1, n_conjuncts,
                                NULL, &matches);
        for (uint32_t bi = 0; bi < matches.len; bi++) {
            Bindings projected;
            if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                &projected)) {
                continue;
            }
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_MATCH_TEMPLATE);
            Atom *result = bindings_apply_if_vars(&projected, a, template);
            const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
            uint32_t before = os->len;
            Atom *raw_result = NULL;
            if (petta_match_template_raw_result(template, result, &raw_result)) {
                if (petta_match_raw_result_needs_surface_eval(raw_result)) {
                    eval_for_caller(s, a, NULL, raw_result, fuel, forward_env,
                                    preserve_bindings, os);
                } else {
                    outcome_set_add(os, raw_result, forward_env);
                }
            } else {
                eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                preserve_bindings, os);
            }
            bool cut_seen = outcome_set_consume_control_since(
                os, before, CETTA_OUTCOME_CONTROL_CUT);
            bindings_free(&projected);
            if (cut_seen)
                break;
        }
        binding_set_free(&matches);
        match_visible_var_set_free(&visible);
        return true;
    }

    {
        #define MAX_IMPORTED_SAME_SPACE_CHAIN 32
        bool allow_pathmap_same_space_chain = true;
        /* Recursive evaluation is the semantic baseline for pathmap-backed
           spaces. The imported same-space conjunction planner is useful only
           once it preserves non-ground residual bindings as faithfully as the
           ordinary nested match path. */
        if (allow_pathmap_same_space_chain &&
            space_engine_uses_pathmap(ms->match_backend.kind)) {
            Atom *same_space_patterns[MAX_IMPORTED_SAME_SPACE_CHAIN];
            uint32_t nsame = 1;
            Atom *residual_body = template;

            same_space_patterns[0] = pattern;
            while (nsame < MAX_IMPORTED_SAME_SPACE_CHAIN &&
                   residual_body->kind == ATOM_EXPR && residual_body->expr.len == 4 &&
                   atom_is_symbol_id(residual_body->expr.elems[0], g_builtin_syms.match)) {
                Atom *inner_ref = resolve_registry_refs(a, residual_body->expr.elems[1]);
                Space *inner_sp = g_registry
                                      ? resolve_runtime_space_ref(s, a, inner_ref)
                                      : NULL;
                if (!inner_sp) inner_sp = s;
                if (inner_sp != ms)
                    break;
                same_space_patterns[nsame++] =
                    resolve_registry_refs(a, residual_body->expr.elems[2]);
                residual_body = residual_body->expr.elems[3];
            }

            if (nsame >= 2) {
                BindingSet matches;
                MatchVisibleVarSet visible;
                match_visible_var_set_init(&visible);
                if (!collect_match_visible_vars_many(same_space_patterns, nsame, &visible)) {
                    match_visible_var_set_free(&visible);
                    return true;
                }
                space_query_conjunction(ms, a, same_space_patterns, nsame, NULL, &matches);
                for (uint32_t bi = 0; bi < matches.len; bi++) {
                    Bindings projected;
                    if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                        &projected)) {
                        continue;
                    }
                    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_MATCH_TEMPLATE);
                    Atom *result = bindings_apply_if_vars(&projected, a, residual_body);
                    const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                    uint32_t before = os->len;
                    eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                    preserve_bindings, os);
                    bool cut_seen = outcome_set_consume_control_since(
                        os, before, CETTA_OUTCOME_CONTROL_CUT);
                    bindings_free(&projected);
                    if (cut_seen)
                        break;
                }
                binding_set_free(&matches);
                match_visible_var_set_free(&visible);
                return true;
            }
        }
        #undef MAX_IMPORTED_SAME_SPACE_CHAIN
    }

    {
        #define MAX_CHAIN 16
        #define MAX_VARS_PER_PAT 32
        typedef struct { Space *space; Atom *pattern; } MatchStep;

        bool allow_chain_flatten = true;

        MatchStep steps[MAX_CHAIN];
        uint32_t nsteps = 0;

        steps[nsteps].space = ms;
        steps[nsteps].pattern = pattern;
        nsteps++;

        Atom *body = template;
        while (allow_chain_flatten && nsteps < MAX_CHAIN &&
               body->kind == ATOM_EXPR && body->expr.len == 4 &&
               atom_is_symbol_id(body->expr.elems[0], g_builtin_syms.match)) {
            Atom *inner_ref = resolve_registry_refs(a, body->expr.elems[1]);
            Atom *inner_pat = resolve_registry_refs(a, body->expr.elems[2]);
            Space *inner_sp = g_registry
                                  ? resolve_runtime_space_ref(s, a, inner_ref)
                                  : NULL;
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

        MatchVisibleVarSet visible;
        match_visible_var_set_init(&visible);
        for (uint32_t i = 0; i < nsteps; i++) {
            if (!collect_match_visible_vars_rec(steps[i].pattern, &visible)) {
                match_visible_var_set_free(&visible);
                return true;
            }
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
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_STEP);
                Atom *grounded = bindings_apply_if_vars(&cur_binds[bi], a, step->pattern);
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
            bool cut_seen = false;
            for (uint32_t bi = 0; bi < ncur; bi++) {
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_LAST);
                Atom *grounded = bindings_apply_if_vars(&cur_binds[bi], a, last->pattern);
                SubstMatchSet smr;
                smset_init(&smr);
                space_subst_query(last->space, a, grounded, &smr);
                for (uint32_t ci = 0; ci < smr.len; ci++) {
                    Bindings mb;
                    if (space_subst_match_with_seed(last->space, grounded, &smr.items[ci],
                                                    &cur_binds[bi], a, &mb)) {
                        Bindings projected;
                        if (!project_match_visible_bindings(a, &visible, &mb,
                                                            &projected)) {
                            bindings_free(&mb);
                            continue;
                        }
                        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_BODY);
                        Atom *result = bindings_apply_if_vars(&projected, a, body);
                        const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                        uint32_t before = os->len;
                        Atom *raw_result = NULL;
                        if (petta_match_template_raw_result(body, result, &raw_result)) {
                            if (petta_match_raw_result_needs_surface_eval(raw_result)) {
                                eval_for_caller(s, a, NULL, raw_result, fuel,
                                                forward_env, preserve_bindings, os);
                            } else {
                                outcome_set_add(os, raw_result, forward_env);
                            }
                        } else {
                            eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                            preserve_bindings, os);
                        }
                        cut_seen = outcome_set_consume_control_since(
                            os, before, CETTA_OUTCOME_CONTROL_CUT);
                        bindings_free(&projected);
                        bindings_free(&mb);
                        g_chain_progress++;
                        if ((g_chain_progress % 100000) == 0) {
                            fprintf(stderr, "[chain] %luk results  (step %u/%u, bi=%u/%u)\n",
                                (unsigned long)(g_chain_progress / 1000),
                                nsteps, nsteps, bi, ncur);
                        }
                        if (cut_seen)
                            break;
                    }
                }
                smset_free(&smr);
                if (cut_seen)
                    break;
            }
        } else {
            for (uint32_t bi = 0; bi < ncur; bi++) {
                Bindings projected;
                if (!project_match_visible_bindings(a, &visible, &cur_binds[bi],
                                                    &projected)) {
                    continue;
                }
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_BODY);
                Atom *result = bindings_apply_if_vars(&projected, a, body);
                const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                uint32_t before = os->len;
                Atom *raw_result = NULL;
                if (petta_match_template_raw_result(body, result, &raw_result)) {
                    if (petta_match_raw_result_needs_surface_eval(raw_result)) {
                        eval_for_caller(s, a, NULL, raw_result, fuel, forward_env,
                                        preserve_bindings, os);
                    } else {
                        outcome_set_add(os, raw_result, forward_env);
                    }
                } else {
                    eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                    preserve_bindings, os);
                }
                bool cut_seen = outcome_set_consume_control_since(
                    os, before, CETTA_OUTCOME_CONTROL_CUT);
                bindings_free(&projected);
                if (cut_seen)
                    break;
            }
        }
        match_visible_var_set_free(&visible);
        bindings_array_free(cur_binds, ncur);
    }

    return true;
}

static bool emit_unquoted_mork_rows(Space *s, Arena *a, SymbolId internal_head_id,
                                    Atom *surface_atom, uint32_t nargs,
                                    Atom **args, bool evaluate_rows, int fuel,
                                    OutcomeSet *os) {
    Bindings _empty;
    bindings_init(&_empty);
    Atom *internal_head = atom_symbol_id(a, internal_head_id);
    Atom **resolved_args = arena_alloc(a, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        resolved_args[i] = resolve_registry_refs(a, args[i]);
    }
    Atom *result = dispatch_native_op(s, a, internal_head, resolved_args, nargs);
    if (!result)
        return false;
    if (atom_is_error(result)) {
        outcome_set_add(os, result, &_empty);
        return true;
    }
    Atom *payload = result;
    if (payload->kind == ATOM_EXPR && payload->expr.len == 2 &&
        atom_is_symbol_id(payload->expr.elems[0], g_builtin_syms.quote)) {
        payload = payload->expr.elems[1];
    }
    if (payload->kind == ATOM_EXPR) {
        for (uint32_t i = 0; i < payload->expr.len; i++) {
            Atom *row = payload->expr.elems[i];
            if (row->kind == ATOM_EXPR && row->expr.len == 2 &&
                atom_is_symbol_id(row->expr.elems[0], g_builtin_syms.quote)) {
                row = row->expr.elems[1];
            }
            if (evaluate_rows) {
                eval_for_caller(s, a, NULL, row, fuel, &_empty, false, os);
            } else {
                outcome_set_add(os, row, &_empty);
            }
        }
        return true;
    }
    if (payload != surface_atom) {
        outcome_set_add(os, payload, &_empty);
        return true;
    }
    return false;
}

typedef struct {
    Space *space;
    Arena *arena;
    Atom *templ;
    int fuel;
    OutcomeSet *outcomes;
} DirectMorkEmitCtx;

static bool direct_mork_emit_row(const Bindings *bindings, void *ctx) {
    DirectMorkEmitCtx *emit = ctx;
    Bindings empty_bindings;
    bindings_init(&empty_bindings);
    Atom *row = bindings_apply_if_vars(bindings, emit->arena, emit->templ);
    eval_for_caller(emit->space, emit->arena, NULL, row, emit->fuel,
                    &empty_bindings, false, emit->outcomes);
    bindings_free(&empty_bindings);
    return true;
}

static bool emit_direct_mork_match_rows(Space *s, Arena *a, Atom *surface_atom,
                                        Atom **args, int fuel,
                                        OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    if (!g_library_context)
        return false;

    Atom *space_arg = resolve_registry_refs(a, args[0]);
    CettaMorkSpaceHandle *bridge = NULL;
    if (!cetta_library_lookup_explicit_mork_bridge(g_library_context, space_arg,
                                                   &bridge) || !bridge) {
        return false;
    }

    Atom *pattern = resolve_registry_refs(a, args[1]);
    Atom *templ = resolve_registry_refs(a, args[2]);
    DirectMorkEmitCtx emit = {
        .space = s,
        .arena = a,
        .templ = templ,
        .fuel = fuel,
        .outcomes = os,
    };

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        bool ok = space_match_backend_mork_visit_conjunction_direct(
            bridge, a, pattern->expr.elems + 1, pattern->expr.len - 1, NULL,
            direct_mork_emit_row, &emit);
        if (!ok) {
            const char *err = cetta_mork_bridge_last_error();
            outcome_set_add(os,
                            atom_error(a, surface_atom,
                                       atom_string(a, err && *err
                                                          ? err
                                                          : "MORK direct match failed")),
                            &empty);
            return true;
        }
        return true;
    }

    bool ok = space_match_backend_mork_visit_bindings_direct(
        bridge, a, pattern, direct_mork_emit_row, &emit);
    if (!ok) {
        const char *err = cetta_mork_bridge_last_error();
        outcome_set_add(os,
                        atom_error(a, surface_atom,
                                   atom_string(a, err && *err
                                                      ? err
                                                      : "MORK direct match failed")),
                        &empty);
        return true;
    }
    return true;
}

static __attribute__((noinline)) bool
handle_dispatch(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                const Bindings *current_env,
                bool preserve_bindings,
                EvalLocalTailSurvivor *local_survivor,
                Atom **tail_next, Atom **tail_type,
                Bindings *tail_env, CettaTailScope *tail_scope,
                OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    *tail_next = NULL;
    *tail_type = NULL;
    if (tail_scope)
        *tail_scope = CETTA_TAIL_SCOPE_CONTINUATION;
    bindings_init(tail_env);
    if (atom->kind != ATOM_EXPR || atom->expr.len < 1) return false;

    Atom *op = atom->expr.elems[0];
    SymbolId head_id = op->kind == ATOM_SYMBOL ? op->sym_id : SYMBOL_ID_NONE;
    uint32_t nargs = atom->expr.len - 1;
    if (head_id == g_builtin_syms.mork_get_atoms_surface && nargs == 1) {
        if (emit_unquoted_mork_rows(s, a, g_builtin_syms.lib_mork_space_atoms,
                                    atom, nargs, atom->expr.elems + 1,
                                    false, fuel, os)) {
            return true;
        }
    }
    if (head_id == g_builtin_syms.mork_match_surface && nargs == 3) {
        if (emit_direct_mork_match_rows(s, a, atom, atom->expr.elems + 1,
                                        fuel, os)) {
            return true;
        }
        if (emit_unquoted_mork_rows(s, a, g_builtin_syms.lib_mork_space_match,
                                    atom, nargs, atom->expr.elems + 1,
                                    true, fuel, os)) {
            return true;
        }
    }
    if ((head_id == g_builtin_syms.mork_add_atoms ||
         head_id == g_builtin_syms.lib_mork_space_add_atoms) &&
        nargs == 2) {
        Atom *space_arg = resolve_registry_refs(a, atom->expr.elems[1]);
        if (emit_mork_add_atoms_from_collapse(s, a, atom, space_arg,
                                              atom->expr.elems[2], current_env,
                                              fuel, os)) {
            return true;
        }
    }
    if (head_id == g_builtin_syms.lib_mork_space_add_stream &&
        nargs == 2) {
        Atom *space_arg = resolve_registry_refs(a, atom->expr.elems[1]);
        if (emit_mork_add_atoms_from_stream_source(
                s, a, atom, space_arg, atom->expr.elems[2],
                current_env, fuel, os)) {
            return true;
        }
    }
    if (op->kind == ATOM_SYMBOL &&
        (head_id == g_builtin_syms.range_atom ||
         head_id == g_builtin_syms.repeat_atom)) {
        Atom *direct = dispatch_native_op(s, a, op,
                                          atom->expr.elems + 1,
                                          atom->expr.len - 1);
        if (direct) {
            outcome_set_add(os, direct, &_empty);
            return true;
        }
    }
    if (op->kind == ATOM_SYMBOL &&
        (head_id == g_builtin_syms.lib_mork_space_add_atoms ||
         head_id == g_builtin_syms.lib_mork_space_add_stream ||
         head_id == g_builtin_syms.mork_add_atoms ||
         head_id == g_builtin_syms.mork_add_atom ||
         head_id == g_builtin_syms.mork_remove_atom)) {
        const bool emit_timing = cetta_runtime_timing_is_enabled();
        uint64_t dispatch_started_ns = emit_timing ? eval_monotonic_ns() : 0;
        uint64_t resolve_started_ns = dispatch_started_ns;
        Atom **resolved_args = arena_alloc(a, sizeof(Atom *) * nargs);
        for (uint32_t i = 0; i < nargs; i++) {
            resolved_args[i] = resolve_registry_refs(a, atom->expr.elems[i + 1]);
        }
        if (emit_timing &&
            (head_id == g_builtin_syms.mork_add_atoms ||
             head_id == g_builtin_syms.lib_mork_space_add_atoms ||
             head_id == g_builtin_syms.lib_mork_space_add_stream)) {
            uint64_t resolve_finished_ns = eval_monotonic_ns();
            if (resolve_finished_ns >= resolve_started_ns) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_RESOLVE_NS,
                                        resolve_finished_ns - resolve_started_ns);
            }
        }
        Atom *direct = dispatch_native_op(s, a, op, resolved_args, nargs);
        if (emit_timing &&
            (head_id == g_builtin_syms.mork_add_atoms ||
             head_id == g_builtin_syms.lib_mork_space_add_atoms ||
             head_id == g_builtin_syms.lib_mork_space_add_stream)) {
            uint64_t dispatch_finished_ns = eval_monotonic_ns();
            if (dispatch_finished_ns >= dispatch_started_ns) {
                cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_MORK_ADD_BATCH_DISPATCH_NS,
                                        dispatch_finished_ns - dispatch_started_ns);
            }
        }
        if (direct) {
            outcome_set_add(os, direct, &_empty);
            return true;
        }
    }
    if (dispatch_direct_surface_no_type(s, a, op, atom, fuel, current_env,
                                        preserve_bindings,
                                        tail_next, tail_type, tail_env, os)) {
        return true;
    }
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
    outcome_set_init_with_owner(&func_results, eval_active_episode_survivor_arena());
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
            Atom *fresh_ft = atom_freshen_epoch(a, op_types[ti], fresh_var_suffix());
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
                    Atom *head_atom = outcome_atom_materialize(a, &heads.items[hi]);
                    Bindings *head_env = &heads.items[hi].env;
                    if (atom_is_empty_or_error(head_atom) && !atom_eq(head_atom, op)) {
                        outcome_set_add_existing_move(&func_results, &heads.items[hi]);
                        continue;
                    }

                    for (uint32_t si = 0; si < n_succs; si++) {
                        __attribute__((cleanup(bindings_free)))
                        Bindings merged_success_env;
                        const Bindings *call_head_env = head_env;
                        bindings_init(&merged_success_env);
                        if (bindings_has_any_entries(&succs[si])) {
                            if (!bindings_clone_merge(&merged_success_env, head_env,
                                                      &succs[si])) {
                                continue;
                            }
                            call_head_env = &merged_success_env;
                        }

                        uint32_t expr_narg = atom->expr.len - 1;
                        OutcomeSet call_terms;
                        outcome_set_init(&call_terms);
                        Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
                        interpret_function_args(s, a, head_atom,
                                                atom->expr.elems + 1, arg_types,
                                                expr_narg, 0, prefix, call_head_env,
                                                fuel, &call_terms);
                        const bool allow_single_tail =
                            n_succs == 1 && only_function_types &&
                            n_op_types == 1 && heads.len == 1 &&
                            call_terms.len == 1;

                        for (uint32_t ci = 0; ci < call_terms.len; ci++) {
                            Atom *call_atom =
                                outcome_atom_materialize(a, &call_terms.items[ci]);
                            Bindings *combo_ctx = &call_terms.items[ci].env;
                            Atom *inst_ret_type =
                                bindings_apply_if_vars(combo_ctx, a, ret_type);

                        bool dispatched = false;
                        Atom *flattened_call =
                            petta_flatten_higher_order_call(s, a, call_atom);
                        if (flattened_call) {
                            eval_for_caller(s, a, inst_ret_type, flattened_call,
                                            fuel, combo_ctx, preserve_bindings,
                                            &func_results);
                            dispatched = true;
                        }
                        if (!dispatched &&
                            call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                            Atom *h = call_atom->expr.elems[0];
                            if (dispatch_foreign_outcomes(
                                    s, a, h,
                                    call_atom->expr.elems + 1, 0,
                                    inst_ret_type, fuel, combo_ctx,
                                    allow_single_tail,
                                    preserve_bindings,
                                    tail_next, tail_type, tail_env,
                                    &func_results)) {
                                if (allow_single_tail && *tail_next) {
                                    outcome_set_free(&call_terms);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    if (tail_scope)
                                        *tail_scope = CETTA_TAIL_SCOPE_CONTINUATION;
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
                                    fuel, combo_ctx, preserve_bindings,
                                    &func_results);
                                dispatched = true;
                            } else if (dispatch_foreign_outcomes(
                                           s, a, h,
                                           call_atom->expr.elems + 1, call_atom->expr.len - 1,
                                           inst_ret_type, fuel, combo_ctx,
                                           allow_single_tail,
                                           preserve_bindings,
                                           tail_next, tail_type, tail_env,
                                           &func_results)) {
                                if (allow_single_tail && *tail_next) {
                                    outcome_set_free(&call_terms);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    if (tail_scope)
                                        *tail_scope = CETTA_TAIL_SCOPE_CONTINUATION;
                                    return true;
                                }
                                dispatched = true;
                            } else if (dispatch_petta_equals_native_outcomes(
                                           s, a, h,
                                           call_atom->expr.elems + 1,
                                           call_atom->expr.len - 1,
                                           inst_ret_type, fuel, combo_ctx,
                                           preserve_bindings,
                                           &func_results)) {
                                dispatched = true;
                            } else if (atom_is_native_dispatch_head(h)) {
                                Atom *gr = dispatch_native_op(s, a, h,
                                    call_atom->expr.elems + 1, call_atom->expr.len - 1);
                                if (gr) {
                                    if (allow_single_tail) {
                                        *tail_next = gr;
                                        *tail_type = inst_ret_type;
                                        bindings_copy(tail_env, combo_ctx);
                                        outcome_set_free(&call_terms);
                                        outcome_set_free(&heads);
                                        outcome_set_free(&func_results);
                                        for (uint32_t sj = 0; sj < n_succs; sj++)
                                            bindings_free(&succs[sj]);
                                        free(op_types);
                                        if (tail_scope)
                                            *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                                        return true;
                                    }
                                    eval_for_caller(s, a, inst_ret_type, gr, fuel,
                                                    combo_ctx, preserve_bindings,
                                                    &func_results);
                                    dispatched = true;
                                }
                            }
                        }
                        if (!dispatched) {
                            SearchContext qr_context;
                            if (!search_context_init(&qr_context, combo_ctx, NULL)) {
                                continue;
                            }
                            __attribute__((cleanup(eval_query_episode_cleanup)))
                            EvalQueryEpisode query_episode = {0};
                            eval_query_episode_init(&query_episode);
                            Arena *query_arena =
                                eval_query_episode_scratch(&query_episode);
                            outcome_set_bind_owner_if_missing(
                                &func_results,
                                eval_query_episode_result_arena(&query_episode, a));
                            QueryEvalVisitorCtx query_eval = {
                                .space = s,
                                .arena = a,
                                .declared_type = inst_ret_type,
                                .fuel = fuel,
                                .base_env = combo_ctx,
                                .preserve_bindings = preserve_bindings,
                                .context = &qr_context,
                                .outcomes = &func_results,
                                .episode = &query_episode,
                            };
                            QueryTableTailState table_tail = QUERY_TABLE_TAIL_MISS;
                            if (allow_single_tail) {
                                table_tail = query_equations_table_hit_single_tail(
                                    s, call_atom, &query_episode, query_arena,
                                    &query_eval, local_survivor,
                                    tail_next, tail_type, tail_env);
                            }
                            if (table_tail != QUERY_TABLE_TAIL_MISS) {
                                search_context_free(&qr_context);
                                if (table_tail == QUERY_TABLE_TAIL_SINGLE) {
                                    outcome_set_free(&call_terms);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    if (tail_scope)
                                        *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                                    return true;
                                }
                                if (table_tail == QUERY_TABLE_TAIL_MULTI) {
                                    query_equations_table_hit_visit(
                                        s, call_atom, query_arena,
                                        &query_eval, NULL);
                                    dispatched = true;
                                }
                                goto query_done;
                            }
                            QueryTableTailState miss_tail =
                                query_equations_miss_single_tail_stream(
                                    s, call_atom, &query_episode, query_arena,
                                    &query_eval,
                                    local_survivor,
                                    allow_single_tail,
                                    tail_next, tail_type, tail_env);
                            search_context_free(&qr_context);
                            if (miss_tail == QUERY_TABLE_TAIL_SINGLE &&
                                allow_single_tail) {
                                outcome_set_free(&call_terms);
                                outcome_set_free(&heads);
                                outcome_set_free(&func_results);
                                for (uint32_t sj = 0; sj < n_succs; sj++)
                                    bindings_free(&succs[sj]);
                                free(op_types);
                                if (tail_scope)
                                    *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                                return true;
                            }
                            if (miss_tail == QUERY_TABLE_TAIL_MULTI)
                                dispatched = true;
                        }
query_done:
                        if (!dispatched)
                            outcome_set_add_existing_move(&func_results, &call_terms.items[ci]);
                    }
                    outcome_set_free(&call_terms);
                    }
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
        if (language_is_petta() &&
            outcome_set_all_petta_typecheck_errors(a, &func_results)) {
            outcome_set_free(&func_results);
            return true;
        }
        for (uint32_t i = 0; i < func_results.len; i++)
            outcome_set_add_existing(os, &func_results.items[i]);
        outcome_set_free(&func_results);
        if (!has_non_func_type) return true;
    } else {
        outcome_set_free(&func_results);
    }

    if (has_func_type && n_func_errors > 0 &&
        (!has_non_func_type || eval_type_check_auto_enabled())) {
        if (language_is_petta() && !eval_type_check_auto_enabled())
            return true;
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
                        0, prefix, &empty_ctx, NULL, fuel, &tuples);

        if (tuples.len == 1) {
            if (outcome_skip_call_observation_fast_path(s, &tuples.items[0])) {
                outcome_set_add_existing_move(os, &tuples.items[0]);
                outcome_set_free(&tuples);
                return true;
            }
            Bindings *tuple_bindings = &tuples.items[0].env;
            Atom *call_atom =
                outcome_atom_materialize_traced(
                    a, &tuples.items[0],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_DISPATCH_CALL_TERM);
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add(os, call_atom, &_empty);
                outcome_set_free(&tuples);
                return true;
            }
            Atom *flattened_call =
                petta_flatten_higher_order_call(s, a, call_atom);
            if (flattened_call) {
                *tail_next = flattened_call;
                *tail_type = etype;
                bindings_copy(tail_env, tuple_bindings);
                if (tail_scope)
                    *tail_scope = CETTA_TAIL_SCOPE_CONTINUATION;
                outcome_set_free(&tuples);
                return true;
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                Atom *h = call_atom->expr.elems[0];
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, 0,
                        NULL, fuel, tuple_bindings, true,
                        preserve_bindings,
                        tail_next, tail_type, tail_env, os)) {
                    outcome_set_free(&tuples);
                    return true;
                }
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (is_capture_closure(h)) {
                    dispatch_capture_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        fuel, tuple_bindings, preserve_bindings, os);
                    outcome_set_free(&tuples);
                    return true;
                }
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        NULL, fuel, tuple_bindings, true,
                        preserve_bindings,
                        tail_next, tail_type, tail_env, os)) {
                    outcome_set_free(&tuples);
                    return true;
                }
                if (dispatch_petta_equals_native_tail(
                        a, h, call_atom->expr.elems + 1,
                        call_atom->expr.len - 1, etype, tuple_bindings,
                        tail_next, tail_type, tail_env)) {
                    if (tail_scope)
                        *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                    outcome_set_free(&tuples);
                    return true;
                }
                if (atom_is_native_dispatch_head(h)) {
                    Atom *result = dispatch_native_op(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        *tail_next = result;
                        *tail_type = etype;
                        bindings_copy(tail_env, tuple_bindings);
                        if (tail_scope)
                            *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                        outcome_set_free(&tuples);
                        return true;
                    }
                }
            }

            SearchContext qr_context;
            if (!search_context_init(&qr_context, tuple_bindings, NULL)) {
                outcome_set_free(&tuples);
                return true;
            }
            __attribute__((cleanup(eval_query_episode_cleanup)))
            EvalQueryEpisode query_episode = {0};
            eval_query_episode_init(&query_episode);
            Arena *query_arena = eval_query_episode_scratch(&query_episode);
            outcome_set_bind_owner_if_missing(
                os, eval_query_episode_result_arena(&query_episode, a));
            QueryEvalVisitorCtx query_eval = {
                .space = s,
                .arena = a,
                .declared_type = NULL,
                .fuel = fuel,
                .base_env = tuple_bindings,
                .preserve_bindings = preserve_bindings,
                .context = &qr_context,
                .outcomes = os,
                .episode = &query_episode,
            };
            QueryTableTailState table_tail =
                query_equations_table_hit_single_tail(
                    s, call_atom, &query_episode, query_arena, &query_eval,
                    local_survivor,
                    tail_next, tail_type, tail_env);
            if (table_tail != QUERY_TABLE_TAIL_MISS) {
                search_context_free(&qr_context);
                if (table_tail == QUERY_TABLE_TAIL_SINGLE) {
                    if (tail_scope)
                        *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                    outcome_set_free(&tuples);
                    return true;
                }
                if (table_tail == QUERY_TABLE_TAIL_MULTI) {
                    query_equations_table_hit_visit(s, call_atom, query_arena,
                                                    &query_eval, NULL);
                    outcome_set_free(&tuples);
                    return true;
                }
                outcome_set_add_existing_move(os, &tuples.items[0]);
                outcome_set_free(&tuples);
                return true;
            }
            QueryTableTailState miss_tail =
                query_equations_miss_single_tail_stream(
                    s, call_atom, &query_episode, query_arena, &query_eval,
                    local_survivor,
                    true,
                    tail_next, tail_type, tail_env);
            search_context_free(&qr_context);
            if (miss_tail == QUERY_TABLE_TAIL_SINGLE ||
                miss_tail == QUERY_TABLE_TAIL_MULTI) {
                if (miss_tail == QUERY_TABLE_TAIL_SINGLE && tail_scope)
                    *tail_scope = CETTA_TAIL_SCOPE_LOCAL;
                outcome_set_free(&tuples);
                return true;
            }
            if (try_petta_returned_callable_prefix_dispatch(
                    s, a, call_atom, fuel, tuple_bindings,
                    preserve_bindings, os)) {
                outcome_set_free(&tuples);
                return true;
            }
            if (known_head_query_miss_should_fail(s, call_atom)) {
                outcome_set_free(&tuples);
                return true;
            }
            outcome_set_add_existing_move(os, &tuples.items[0]);
            outcome_set_free(&tuples);
            return true;
        }

        for (uint32_t ti = 0; ti < tuples.len; ti++) {
            if (outcome_skip_call_observation_fast_path(s, &tuples.items[ti])) {
                outcome_set_add_existing_move(os, &tuples.items[ti]);
                continue;
            }
            Atom *call_atom =
                outcome_atom_materialize_traced(
                    a, &tuples.items[ti],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_DISPATCH_CALL_TERM);
            Bindings *tuple_bindings = &tuples.items[ti].env;

            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add(os, call_atom, &_empty);
                continue;
            }

            Atom *flattened_call =
                petta_flatten_higher_order_call(s, a, call_atom);
            if (flattened_call) {
                eval_for_caller(s, a, NULL, flattened_call, fuel,
                                tuple_bindings, preserve_bindings, os);
                continue;
            }

            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                Atom *h = call_atom->expr.elems[0];
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, 0,
                        NULL, fuel, tuple_bindings, false, preserve_bindings,
                        tail_next, tail_type, tail_env, os)) {
                    continue;
                }
            }
            if (call_atom->kind == ATOM_EXPR && call_atom->expr.len >= 2) {
                Atom *h = call_atom->expr.elems[0];
                if (is_capture_closure(h)) {
                    dispatch_capture_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        fuel, tuple_bindings, preserve_bindings, os);
                    continue;
                }
                if (dispatch_foreign_outcomes(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1,
                        NULL, fuel, tuple_bindings, false, preserve_bindings,
                        tail_next, tail_type, tail_env, os)) {
                    continue;
                }
                if (dispatch_petta_equals_native_outcomes(
                        s, a, h, call_atom->expr.elems + 1,
                        call_atom->expr.len - 1, NULL, fuel, tuple_bindings,
                        preserve_bindings, os)) {
                    continue;
                }
                if (atom_is_native_dispatch_head(h)) {
                    Atom *result = dispatch_native_op(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        eval_for_caller(s, a, NULL, result, fuel, tuple_bindings,
                                        preserve_bindings, os);
                        continue;
                    }
                }
            }

            SearchContext qr_context;
            if (!search_context_init(&qr_context, tuple_bindings, NULL)) {
                continue;
            }
            QueryEvalVisitorCtx query_eval = {
                .space = s,
                .arena = a,
                .declared_type = NULL,
                .fuel = fuel,
                .base_env = tuple_bindings,
                .preserve_bindings = preserve_bindings,
                .context = &qr_context,
                .outcomes = os,
                .episode = NULL,
            };
            if (query_equations_cached_visit(s, call_atom, a, &query_eval) > 0) {
                search_context_free(&qr_context);
                continue;
            }
            search_context_free(&qr_context);

            if (try_petta_returned_callable_prefix_dispatch(
                    s, a, call_atom, fuel, tuple_bindings,
                    preserve_bindings, os)) {
                continue;
            }
            if (known_head_query_miss_should_fail(s, call_atom)) {
                continue;
            }
            outcome_set_add_existing_move(os, &tuples.items[ti]);
        }
        outcome_set_free(&tuples);
    }

    return true;
}

/* ── metta_call: dispatch expressions ───────────────────────────────────── */

static void metta_call(Space *s, Arena *a, Atom *atom, Atom *etype, int fuel,
                       bool preserve_bindings, OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    __attribute__((cleanup(eval_local_tail_survivor_cleanup)))
    EvalLocalTailSurvivor local_tail_survivor;
    __attribute__((cleanup(bindings_builder_free))) BindingsBuilder current_env_builder;
    eval_local_tail_survivor_init(&local_tail_survivor);
    if (!bindings_builder_init(&current_env_builder, NULL))
        return;
    if (!etype) etype = atom_undefined_type(a);
    ArenaMark local_tail_eval_mark = arena_mark(a);
#define CURRENT_ENV bindings_builder_bindings(&current_env_builder)
#define NOTE_TAIL_REENTER(extra_env) do { \
    const Bindings *_tail_live_env = CURRENT_ENV; \
    const Bindings *_tail_extra_env = (extra_env); \
    (void)_tail_live_env; \
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_REENTER_COUNT); \
    if (_tail_extra_env && (_tail_extra_env->len != 0 || _tail_extra_env->eq_len != 0)) \
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_TAIL_REENTER_EXTRA_ENV_COUNT); \
    cetta_runtime_stats_update_max( \
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_LIVE_BINDING_ENTRIES_PEAK, \
        _tail_live_env ? (uint64_t)_tail_live_env->len : 0); \
    cetta_runtime_stats_update_max( \
        CETTA_RUNTIME_COUNTER_EVAL_TAIL_LIVE_BINDING_CONSTRAINTS_PEAK, \
        _tail_live_env ? (uint64_t)_tail_live_env->eq_len : 0); \
} while (0)
#define TAIL_REENTER_ENV_SCOPED(next_atom, extra_env, scope) do { \
    CettaTailScope _tail_scope = (scope); \
    Atom *_tail_next_atom; \
    switch (_tail_scope) { \
    case CETTA_TAIL_SCOPE_LOCAL: \
    case CETTA_TAIL_SCOPE_CONTINUATION: \
        break; \
    } \
    const Bindings *_tail_extra_env = (extra_env); \
    if (_tail_extra_env != NULL && \
        !bindings_builder_merge_commit(&current_env_builder, _tail_extra_env)) return; \
    NOTE_TAIL_REENTER(_tail_extra_env); \
    _tail_next_atom = resolve_registry_refs(a, (next_atom)); \
    atom = _tail_next_atom; \
    if (fuel == 0) return; \
    if (fuel > 0) fuel--; \
    goto tail_call; \
} while (0)
#define TAIL_REENTER_LOCAL(next_atom) \
    TAIL_REENTER_ENV_SCOPED((next_atom), NULL, CETTA_TAIL_SCOPE_LOCAL)
#define TAIL_REENTER_CONTINUATION(next_atom) \
    TAIL_REENTER_ENV_SCOPED((next_atom), NULL, CETTA_TAIL_SCOPE_CONTINUATION)
#define outcome_set_add(_os, _atom, _env) \
    outcome_set_add_prefixed(a, (_os), (_atom), (_env), CURRENT_ENV, preserve_bindings)
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

    if (language_is_petta() &&
        petta_partial_value_unpack(atom, NULL, NULL, NULL)) {
        outcome_set_add(os, atom, &_empty);
        return;
    }

    uint32_t nargs = expr_nargs(atom);
    const SymbolId head_id = atom_head_symbol_id(atom);
    if (language_is_petta() && head_id == g_builtin_syms.cut) {
        if (nargs != 0) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        uint32_t before = os->len;
        outcome_set_add(os, atom_true(a), &_empty);
        outcome_set_mark_control_since(os, before, CETTA_OUTCOME_CONTROL_CUT);
        return;
    }
    if (language_is_petta() && head_id == g_builtin_syms.catch) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *body = bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0));
        OutcomeSet inner;
        outcome_set_init(&inner);
        metta_eval_bind(s, a, body, fuel, &inner);
        if (inner.len == 0) {
            outcome_set_add(os, atom_empty(a), &_empty);
            outcome_set_free(&inner);
            return;
        }
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *candidate = outcome_atom_materialize(a, &inner.items[i]);
            uint32_t before = os->len;
            if (atom_is_error(candidate)) {
                outcome_set_add(os, candidate, &inner.items[i].env);
                outcome_set_mark_control_since(
                    os, before, CETTA_OUTCOME_CONTROL_ERROR_VALUE);
            } else if (catch_result_is_stuck_known_call(body, candidate)) {
                outcome_set_add(os,
                    atom_error(a, body, atom_symbol(a, "EvaluationError")),
                    &inner.items[i].env);
                outcome_set_mark_control_since(
                    os, before, CETTA_OUTCOME_CONTROL_ERROR_VALUE);
            } else {
                outcome_set_add(os, candidate, &inner.items[i].env);
                if (inner.items[i].control != CETTA_OUTCOME_CONTROL_NONE) {
                    outcome_set_mark_control_since(
                        os, before, inner.items[i].control);
                }
            }
        }
        outcome_set_free(&inner);
        return;
    }
    if (dispatch_petta_repr_outcomes(s, a, atom, fuel, CURRENT_ENV,
                                     preserve_bindings, os)) {
        return;
    }
    if (dispatch_petta_callable_map_outcomes(s, a, atom, fuel, CURRENT_ENV, os)) {
        return;
    }
    if (dispatch_petta_forall_outcomes(s, a, atom, fuel, CURRENT_ENV,
                                       preserve_bindings, os)) {
        return;
    }
    if (petta_grounded_op_is_under_applied(atom->expr.elems[0], nargs)) {
        outcome_set_add(os, petta_make_partial_from_call(a, atom), &_empty);
        return;
    }
    if (petta_user_callable_is_under_applied(s, atom)) {
        outcome_set_add(os, petta_make_partial_from_call(a, atom), &_empty);
        return;
    }

	    const char *head_name = atom_name_cstr(atom->expr.elems[0]);
	    if (language_is_petta() && head_name && strcmp(head_name, "test") == 0 &&
	        nargs == 2) {
        ResultSet actual;
        ResultSet expected;
        result_set_init(&actual);
        result_set_init(&expected);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &actual);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &expected);
        result_set_resolve_registry_refs(a, &actual);
        result_set_resolve_registry_refs(a, &expected);
        Atom *actual_value = actual.len == 1 ? actual.items[0]
                                             : atom_expr(a, actual.items, actual.len);
        Atom *expected_value = expected.len == 1 ? expected.items[0]
                                                 : atom_expr(a, expected.items, expected.len);
        bool ok = atom_assert_eq(actual_value, expected_value) ||
                  atom_alpha_eq(actual_value, expected_value);
        free(actual.items);
        free(expected.items);
        if (ok) {
            outcome_set_add(os, atom_true(a), &_empty);
        } else {
            outcome_set_add(os,
                atom_error(a,
                    atom_expr3(a, atom_symbol(a, "test"),
                               expr_arg(atom, 0), expr_arg(atom, 1)),
                    atom_string(a, "mismatch")),
                &_empty);
	        }
	        return;
	    }
	    if (language_is_petta() && head_name && strcmp(head_name, "is-function") == 0 &&
	        nargs == 1) {
	        Atom *subject = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 0));
	        outcome_set_add(os, is_function_type(subject) ? atom_true(a) : atom_false(a),
	                        &_empty);
	        return;
	    }
	    if (language_is_petta() && head_name && strcmp(head_name, "match-types") == 0 &&
	        nargs == 4) {
	        Atom *lhs = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 0));
	        Atom *rhs = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 1));
	        Atom *selected =
	            (atom_assert_eq(lhs, rhs) || atom_alpha_eq(lhs, rhs))
	                ? expr_arg(atom, 2)
	                : expr_arg(atom, 3);
	        selected = bindings_apply_if_vars(CURRENT_ENV, a, selected);
	        outcome_set_add(os, selected, &_empty);
	        return;
	    }
	    if (language_is_petta() && head_name &&
	        strcmp(head_name, "match-type-or") == 0 && nargs == 3) {
	        Atom *fallback = bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0));
	        Atom *lhs = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 1));
	        Atom *rhs = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 2));
	        Atom *selected =
	            (atom_assert_eq(lhs, rhs) || atom_alpha_eq(lhs, rhs))
	                ? atom_true(a)
	                : fallback;
	        outcome_set_add(os, selected, &_empty);
	        return;
	    }
	    if (language_is_petta() && head_name && strcmp(head_name, "if-error") == 0 &&
	        nargs == 3) {
        Atom *subject =
            petta_unwrap_noeval_value(
                bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0)));
        Atom *selected = atom_is_error(subject) ? expr_arg(atom, 1)
                                                : expr_arg(atom, 2);
        selected = bindings_apply_if_vars(CURRENT_ENV, a, selected);
        if (atom_is_error(selected)) {
            uint32_t before = os->len;
            outcome_set_add(os, selected, &_empty);
            outcome_set_mark_control_since(
                os, before, CETTA_OUTCOME_CONTROL_ERROR_VALUE);
            return;
        }
        TAIL_REENTER_CONTINUATION(selected);
    }
    if (language_is_petta() && head_name &&
        strcmp(head_name, "return-on-error") == 0 && nargs == 2) {
        Atom *subject =
            petta_unwrap_noeval_value(
                bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0)));
        Atom *selected = atom_is_error(subject) ? subject : expr_arg(atom, 1);
        selected = bindings_apply_if_vars(CURRENT_ENV, a, selected);
        if (atom_is_error(selected)) {
            uint32_t before = os->len;
            outcome_set_add(os, selected, &_empty);
            outcome_set_mark_control_since(
                os, before, CETTA_OUTCOME_CONTROL_ERROR_VALUE);
            return;
        }
        TAIL_REENTER_CONTINUATION(selected);
    }
    if (language_is_petta() && head_name && strcmp(head_name, "unquote") == 0 &&
        nargs == 1) {
        Atom *subject =
            petta_unwrap_noeval_value(
                bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0)));
        if (subject && subject->kind == ATOM_EXPR && subject->expr.len == 2 &&
            atom_is_symbol_id(subject->expr.elems[0], g_builtin_syms.quote)) {
            TAIL_REENTER_CONTINUATION(subject->expr.elems[1]);
        }
        Atom *surface = atom_expr2(a, atom_symbol(a, "unquote"), subject);
        outcome_set_add(os, surface, &_empty);
        return;
    }
    if (language_is_petta() && head_name &&
        strcmp(head_name, "noreduce-eq") == 0 && nargs == 2) {
        Atom *lhs =
            petta_unwrap_noeval_value(
                bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0)));
        Atom *rhs =
            petta_unwrap_noeval_value(
                bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 1)));
        outcome_set_add(os, atom_assert_eq(lhs, rhs) ? atom_true(a) : atom_false(a),
                        &_empty);
        return;
    }
    if (language_is_petta() && head_name &&
        strcmp(head_name, "for-each-in-atom") == 0 && nargs == 2) {
        Atom *list =
            petta_unwrap_noeval_value(
                bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0)));
        Atom *callable = bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 1));
        OutcomeSet callables;
        outcome_set_init(&callables);
        metta_eval_bind(s, a, callable, fuel, &callables);
        for (uint32_t ci = 0; ci < callables.len; ci++) {
            Atom *callable_atom = outcome_atom_materialize(a, &callables.items[ci]);
            if (atom_is_empty(callable_atom))
                continue;
            if (atom_is_error(callable_atom)) {
                outcome_set_add_existing(os, &callables.items[ci]);
                continue;
            }
            if (!list || list->kind != ATOM_EXPR) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "ExpressionAtomExpected")),
                    &_empty);
                continue;
            }
            Atom **items = list->expr.len > 0
                ? arena_alloc(a, sizeof(Atom *) * list->expr.len)
                : NULL;
            bool ok = true;
            for (uint32_t i = 0; i < list->expr.len; i++) {
                Atom *arg = list->expr.elems[i];
                Atom *result = NULL;
                Atom *error = NULL;
                if (!petta_eval_callable_single(s, a, callable_atom, &arg, 1,
                                                fuel, atom, &result, &error)) {
                    outcome_set_add(os, error, &_empty);
                    ok = false;
                    break;
                }
                items[i] = atom_is_unit_literal(result) ? atom_true(a) : result;
            }
            if (ok)
                outcome_set_add(os, atom_expr(a, items, list->expr.len), &_empty);
        }
        outcome_set_free(&callables);
        return;
    }

    if (dispatch_petta_lambda_make_outcomes(s, a, atom, CURRENT_ENV, os)) {
        return;
    }
    if (dispatch_petta_lambda_closure_outcomes(s, a, atom, fuel, CURRENT_ENV,
                                               preserve_bindings, os)) {
        return;
    }

    /* ── Special forms (arguments NOT pre-evaluated) ───────────────────── */

    /* ── superpose ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.superpose) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *list_expr =
            bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0));
        OutcomeSet lists;
        outcome_set_init(&lists);
        if (expression_should_eval_as_superpose_list(s, list_expr)) {
            metta_eval_bind(s, a, list_expr, fuel, &lists);
        } else {
            outcome_set_add(&lists, list_expr, &_empty);
        }
        for (uint32_t li = 0; li < lists.len; li++) {
            Atom *list = outcome_atom_materialize(a, &lists.items[li]);
            if (!list || atom_is_empty(list) || list->kind != ATOM_EXPR)
                continue;
            for (uint32_t i = 0; i < list->expr.len; i++) {
                OutcomeSet branch;
                outcome_set_init(&branch);
                eval_for_current_caller(s, a, etype, list->expr.elems[i],
                                        fuel, &lists.items[li].env,
                                        CURRENT_ENV, preserve_bindings,
                                        &branch);
                for (uint32_t j = 0; j < branch.len; j++) {
                    Atom *branch_atom =
                        outcome_atom_materialize(a, &branch.items[j]);
                    if (atom_is_empty(branch_atom))
                        continue;
                    outcome_set_add_existing(os, &branch.items[j]);
                }
                outcome_set_free(&branch);
            }
        }
        outcome_set_free(&lists);
        return;
    }

    /* ── collapse ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.collapse && nargs == 1) {
        if (!preserve_bindings &&
            try_effect_batch_append_collapse(s, a, expr_arg(atom, 0),
                                             fuel, CURRENT_ENV, os)) {
            return;
        }
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        /* HE treats Empty as an internal no-result sentinel here: collapse
           collects only surviving branches, not literal Empty placeholders. */
        Atom **collected_items = NULL;
        uint32_t collected_len = 0;
        if (inner.len > 0) {
            collected_items = arena_alloc(a, sizeof(Atom *) * inner.len);
            for (uint32_t i = 0; i < inner.len; i++) {
                if (atom_is_empty(inner.items[i]))
                    continue;
                collected_items[collected_len++] = inner.items[i];
            }
        }
        Atom *collected = atom_expr(a, collected_items, collected_len);
        free(inner.items);
        outcome_set_add(os, collected, &_empty);
        return;
    }

    /* ── cons-atom ─────────────────────────────────────────────────────── */
    bool petta_cons_alias =
        language_is_petta() &&
        atom->expr.elems[0] &&
        atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(atom->expr.elems[0]), "cons") == 0;
    if ((head_id == g_builtin_syms.cons_atom || petta_cons_alias) && nargs == 2) {
        Atom *arg_types[] = {
            atom_symbol_id(a, g_builtin_syms.atom),
            atom_expression_type(a),
        };
        OutcomeSet calls;
        outcome_set_init(&calls);
        Atom *surface_head = petta_cons_alias
            ? atom_symbol_id(a, g_builtin_syms.cons_atom)
            : atom->expr.elems[0];
        interpret_surface_args_with_policy(s, a, surface_head,
                                           atom->expr.elems + 1, arg_types, 2,
                                           CURRENT_ENV, fuel, &calls);
        for (uint32_t ci = 0; ci < calls.len; ci++) {
            Atom *call_atom = outcome_atom_materialize(a, &calls.items[ci]);
            Bindings *combo_env = &calls.items[ci].env;
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add_existing_move(os, &calls.items[ci]);
                continue;
            }
            Atom *hd = expr_arg(call_atom, 0);
            Atom *tl = expr_arg(call_atom, 1);
            if (tl->kind == ATOM_EXPR) {
                Atom **elems = arena_alloc(a, sizeof(Atom *) * (tl->expr.len + 1));
                elems[0] = hd;
                for (uint32_t i = 0; i < tl->expr.len; i++)
                    elems[i + 1] = tl->expr.elems[i];
                outcome_set_add(os, atom_expr(a, elems, tl->expr.len + 1),
                                combo_env);
            } else {
                outcome_set_add(os, atom_expr2(a, hd, tl), combo_env);
            }
        }
        outcome_set_free(&calls);
        return;
    }

    /* ── union-atom ────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.union_atom && nargs == 2) {
        Atom *arg_types[] = {
            atom_expression_type(a),
            atom_expression_type(a),
        };
        OutcomeSet calls;
        outcome_set_init(&calls);
        interpret_surface_args_with_policy(s, a, atom->expr.elems[0],
                                           atom->expr.elems + 1, arg_types, 2,
                                           CURRENT_ENV, fuel, &calls);
        for (uint32_t ci = 0; ci < calls.len; ci++) {
            Atom *call_atom = outcome_atom_materialize(a, &calls.items[ci]);
            Bindings *combo_env = &calls.items[ci].env;
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add_existing_move(os, &calls.items[ci]);
                continue;
            }
            Atom *lhs = expr_arg(call_atom, 0);
            Atom *rhs = expr_arg(call_atom, 1);
            if (lhs->kind == ATOM_EXPR && rhs->kind == ATOM_EXPR) {
                uint32_t len = lhs->expr.len + rhs->expr.len;
                Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
                for (uint32_t i = 0; i < lhs->expr.len; i++)
                    elems[i] = lhs->expr.elems[i];
                for (uint32_t i = 0; i < rhs->expr.len; i++)
                    elems[lhs->expr.len + i] = rhs->expr.elems[i];
                outcome_set_add(os, atom_expr(a, elems, len), combo_env);
            } else {
                outcome_set_add(os, call_atom, combo_env);
            }
        }
        outcome_set_free(&calls);
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
        Atom *arg_types[] = { atom_expression_type(a) };
        OutcomeSet calls;
        outcome_set_init(&calls);
        interpret_surface_args_with_policy(s, a, atom->expr.elems[0],
                                           atom->expr.elems + 1, arg_types, 1,
                                           CURRENT_ENV, fuel, &calls);
        for (uint32_t ci = 0; ci < calls.len; ci++) {
            Atom *call_atom = outcome_atom_materialize(a, &calls.items[ci]);
            Bindings *combo_env = &calls.items[ci].env;
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add_existing_move(os, &calls.items[ci]);
                continue;
            }
            Atom *e = expr_arg(call_atom, 0);
            if (e->kind == ATOM_EXPR && e->expr.len > 0) {
                Atom *hd = e->expr.elems[0];
                Atom *tl = atom_expr(a, e->expr.elems + 1, e->expr.len - 1);
                outcome_set_add(os, atom_expr2(a, hd, tl), combo_env);
            } else {
                outcome_set_add(os,
                    call_signature_error(a, call_atom,
                        "(decons-atom (: <expr> Expression))"),
                    combo_env);
            }
        }
        outcome_set_free(&calls);
        return;
    }

    /* ── car-atom / cdr-atom ─────────────────────────────────────────── */
    if (head_id == g_builtin_syms.car_atom && nargs == 1) {
        Atom *arg_types[] = { atom_expression_type(a) };
        OutcomeSet calls;
        outcome_set_init(&calls);
        interpret_surface_args_with_policy(s, a, atom->expr.elems[0],
                                           atom->expr.elems + 1, arg_types, 1,
                                           CURRENT_ENV, fuel, &calls);
        for (uint32_t ci = 0; ci < calls.len; ci++) {
            Atom *call_atom = outcome_atom_materialize(a, &calls.items[ci]);
            Bindings *combo_env = &calls.items[ci].env;
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add_existing_move(os, &calls.items[ci]);
                continue;
            }
            Atom *e = expr_arg(call_atom, 0);
            if (e->kind == ATOM_EXPR && e->expr.len > 0) {
                outcome_set_add(os, e->expr.elems[0], combo_env);
            } else {
                outcome_set_add(
                    os,
                    atom_error(a, call_atom,
                               atom_string(a,
                                           "car-atom expects a non-empty expression as an argument")),
                    combo_env);
            }
        }
        outcome_set_free(&calls);
        return;
    }
    if (head_id == g_builtin_syms.cdr_atom && nargs == 1) {
        Atom *arg_types[] = { atom_expression_type(a) };
        OutcomeSet calls;
        outcome_set_init(&calls);
        interpret_surface_args_with_policy(s, a, atom->expr.elems[0],
                                           atom->expr.elems + 1, arg_types, 1,
                                           CURRENT_ENV, fuel, &calls);
        for (uint32_t ci = 0; ci < calls.len; ci++) {
            Atom *call_atom = outcome_atom_materialize(a, &calls.items[ci]);
            Bindings *combo_env = &calls.items[ci].env;
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add_existing_move(os, &calls.items[ci]);
                continue;
            }
            Atom *e = expr_arg(call_atom, 0);
            if (e->kind == ATOM_EXPR && e->expr.len > 0) {
                outcome_set_add(
                    os,
                    atom_expr(a, e->expr.elems + 1, e->expr.len - 1),
                    combo_env);
            } else {
                outcome_set_add(
                    os,
                    atom_error(a, call_atom,
                               atom_string(a,
                                           "cdr-atom expects a non-empty expression as an argument")),
                    combo_env);
            }
        }
        outcome_set_free(&calls);
        return;
    }

    /* ── is-member (PeTTa relational membership) ─────────────────────── */
    if (head_id == g_builtin_syms.is_member) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *arg_types[] = {
            atom_symbol_id(a, g_builtin_syms.atom),
            atom_expression_type(a),
        };
        OutcomeSet calls;
        outcome_set_init(&calls);
        interpret_surface_args_with_policy(s, a, atom->expr.elems[0],
                                           atom->expr.elems + 1, arg_types, 2,
                                           CURRENT_ENV, fuel, &calls);
        for (uint32_t ci = 0; ci < calls.len; ci++) {
            Atom *call_atom = outcome_atom_materialize(a, &calls.items[ci]);
            Bindings *combo_env = &calls.items[ci].env;
            if (atom_is_empty(call_atom) || atom_is_error(call_atom)) {
                outcome_set_add_existing_move(os, &calls.items[ci]);
                continue;
            }

            Atom *needle = expr_arg(call_atom, 0);
            Atom *list = expr_arg(call_atom, 1);
            bool saw_match = false;

            if (list->kind == ATOM_EXPR) {
                for (uint32_t i = 0; i < list->expr.len; i++) {
                    BindingsBuilder b;
                    if (!bindings_builder_init(&b, combo_env)) {
                        outcome_set_free(&calls);
                        return;
                    }
                    if (match_atoms_builder(needle, list->expr.elems[i], &b)) {
                        const Bindings *bb = bindings_builder_bindings(&b);
                        if (!bindings_has_loop((Bindings *)bb)) {
                            outcome_set_add(os, atom_true(a), bb);
                            saw_match = true;
                        }
                    }
                    bindings_builder_free(&b);
                }
            }

            if (!saw_match)
                outcome_set_add(os, atom_false(a), combo_env);
        }
        outcome_set_free(&calls);
        return;
    }

    /* ── explicit mork: surface reads ────────────────────────────────── */
    if (head_id == g_builtin_syms.mork_get_atoms_surface && nargs == 1) {
        if (emit_unquoted_mork_rows(s, a, g_builtin_syms.lib_mork_space_atoms,
                                    atom, nargs, atom->expr.elems + 1,
                                    false, fuel, os)) {
            return;
        }
    }
    if (head_id == g_builtin_syms.mork_match_surface && nargs == 3) {
        if (emit_unquoted_mork_rows(s, a, g_builtin_syms.lib_mork_space_match,
                                    atom, nargs, atom->expr.elems + 1,
                                    true, fuel, os)) {
            return;
        }
    }

    /* ── match (with nested-match fusion + join reordering) ──────────── */
    OutcomeSet match_results;
    outcome_set_init(&match_results);
    if (handle_match(s, a, atom, fuel, CURRENT_ENV, preserve_bindings,
                     &match_results)) {
        outcome_set_append_prefixed(a, os, &match_results, NULL,
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
        Atom *space_candidate = dispatch_observed_arg(a, CURRENT_ENV, target);
        Space *target_space =
            resolve_unify_space_target_if_obvious(s, a, space_candidate, fuel);
        if (target_space) {
            Atom *space_pattern = dispatch_observed_arg(a, CURRENT_ENV, pattern);
            Atom *then_observed = dispatch_observed_arg(a, CURRENT_ENV, then_br);
            Atom *else_observed = dispatch_observed_arg(a, CURRENT_ENV, else_br);
            Atom *patterns[] = { space_pattern };
            MatchVisibleVarSet visible;
            match_visible_var_set_init(&visible);
            if (!collect_match_visible_vars_rec(space_pattern, &visible)) {
                match_visible_var_set_free(&visible);
                return;
            }
            BindingSet matches;
            space_query_conjunction(target_space, a, patterns, 1, NULL, &matches);
            if (matches.len == 0) {
                binding_set_free(&matches);
                match_visible_var_set_free(&visible);
                TAIL_REENTER_CONTINUATION(else_observed);
            }
            bool cut_seen = false;
            for (uint32_t bi = 0; bi < matches.len; bi++) {
                Bindings projected;
                if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                    &projected)) {
                    continue;
                }
                Atom *next_atom = bindings_apply_if_vars(&projected, a, then_observed);
                const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                uint32_t before = os->len;
                eval_for_caller(s, a, NULL, next_atom, fuel, forward_env,
                                preserve_bindings, os);
                cut_seen = outcome_set_consume_control_since(
                    os, before, CETTA_OUTCOME_CONTROL_CUT);
                bindings_free(&projected);
                if (cut_seen)
                    break;
            }
            binding_set_free(&matches);
            match_visible_var_set_free(&visible);
            return;
        }
        Bindings b;
        bindings_init(&b);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_UNIFY);
        if (match_atoms(target, pattern, &b) && !bindings_has_loop(&b)) {
            Atom *next_atom = bindings_apply_if_vars(&b, a, then_br);
            if (preserve_bindings &&
                !bindings_builder_merge_commit(&current_env_builder, &b)) {
                bindings_free(&b);
                return;
            }
            bindings_free(&b);
            TAIL_REENTER_CONTINUATION(next_atom);
        } else {
            bindings_free(&b);
            TAIL_REENTER_CONTINUATION(else_br);
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
                    BindingsBuilder b;
                    if (!bindings_builder_init(&b, NULL)) {
                        free(scrut.items);
                        return;
                    }
                    if (simple_match_builder(branch->expr.elems[0], sv, &b)) {
                        const Bindings *bb = bindings_builder_bindings(&b);
                        Atom *next_atom =
                            bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_builder_merge_commit(&current_env_builder, bb)) {
                            bindings_builder_free(&b);
                            free(scrut.items);
                            return;
                        }
                        bindings_builder_free(&b);
                        free(scrut.items);
                        TAIL_REENTER_CONTINUATION(next_atom);
                    }
                    bindings_builder_free(&b);
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
                        BindingsBuilder b;
                        if (!bindings_builder_init(&b, NULL))
                            continue;
                        if (simple_match_builder(branch->expr.elems[0], sv, &b)) {
                            const Bindings *bb = bindings_builder_bindings(&b);
                            Atom *result =
                                bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                            eval_for_current_caller(s, a, NULL, result, fuel, bb,
                                                    CURRENT_ENV,
                                                    preserve_bindings, os);
                            bindings_builder_free(&b);
                            break;
                        }
                        bindings_builder_free(&b);
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
                    BindingsBuilder b;
                    if (!bindings_builder_init(&b, NULL))
                        return;
                    if (simple_match_builder(branch->expr.elems[0], scrutinee, &b)) {
                        const Bindings *bb = bindings_builder_bindings(&b);
                        Atom *next_atom =
                            bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_builder_merge_commit(&current_env_builder, bb)) {
                            bindings_builder_free(&b);
                            return;
                        }
                        bindings_builder_free(&b);
                        TAIL_REENTER_CONTINUATION(next_atom);
                        return;
                    }
                    bindings_builder_free(&b);
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
            TAIL_REENTER_LOCAL(body);
        } else {
            Atom *first = blist->expr.elems[0];
            Atom *rest = atom_expr(a, blist->expr.elems + 1, blist->expr.len - 1);
            if (first->kind == ATOM_EXPR && first->expr.len == 2) {
                Atom *inner = atom_expr3(a, atom_symbol_id(a, g_builtin_syms.let_star), rest, body);
                Atom *elems[4] = { atom_symbol_id(a, g_builtin_syms.let),
                    first->expr.elems[0], first->expr.elems[1], inner };
                Atom *desugared = atom_expr(a, elems, 4);
                TAIL_REENTER_LOCAL(desugared);
            }
        }
        return;
    }

    /* ── let ───────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.let && nargs == 3) {
        Atom *pat = expr_arg(atom, 0);
        Atom *val_expr = expr_arg(atom, 1);
        Atom *body_let = expr_arg(atom, 2);
        Atom *applied_val_expr = bindings_apply_if_vars(CURRENT_ENV, a, val_expr);
        bool body_let_closed = !atom_contains_vars(body_let);
        bool petta_literal_error_value =
            language_is_petta() && atom_is_error(val_expr);
        OutcomeSet vals;
        if (!preserve_bindings && !petta_literal_error_value &&
            direct_outcome_walk_supported(s, a, applied_val_expr, fuel)) {
            LetDirectVisitCtx visit = {
                .s = s,
                .a = a,
                .pat = pat,
                .body = body_let,
                .type = etype,
                .fuel = fuel,
                .outer_env = CURRENT_ENV,
                .preserve_bindings = preserve_bindings,
                .body_closed = body_let_closed,
                .os = os,
                .errors = {0},
                .has_success = false,
            };
            result_set_init(&visit.errors);
            (void)metta_eval_bind_visit(s, a, applied_val_expr, fuel,
                                        CETTA_SEARCH_POLICY_ORDER_NATIVE,
                                        let_direct_branch_visit, &visit);
            if (!visit.has_success) {
                for (uint32_t i = 0; i < visit.errors.len; i++)
                    outcome_set_add(os, visit.errors.items[i], &_empty);
            }
            result_set_free(&visit.errors);
            return;
        }
        outcome_set_init(&vals);
        metta_eval_bind(s, a, applied_val_expr, fuel, &vals);
        bool all_errors = vals.len > 0;
        for (uint32_t i = 0; i < vals.len; i++) {
            if (petta_literal_error_value ||
                vals.items[i].control == CETTA_OUTCOME_CONTROL_ERROR_VALUE) {
                all_errors = false;
                break;
            }
            if (!atom_is_error(
                    outcome_atom_materialize_traced(
                        a, &vals.items[i],
                        CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN))) {
                all_errors = false;
                break;
            }
        }
        if (all_errors) {
            for (uint32_t i = 0; i < vals.len; i++)
                outcome_set_add(os,
                                outcome_atom_materialize_traced(
                                    a, &vals.items[i],
                                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN),
                                &_empty);
            outcome_set_free(&vals);
            return;
        }
        if (vals.len == 1) {
            /* Single-result fast path with TCO */
            bool ok = false;
            bool val_cut =
                vals.items[0].control == CETTA_OUTCOME_CONTROL_CUT;
            Atom *val_atom =
                outcome_atom_materialize_traced(
                    a, &vals.items[0],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            const Bindings *val_env = &vals.items[0].env;
            if (pat->kind == ATOM_VAR) {
                Atom *bound_atom = bindings_apply_if_vars(val_env, a, val_atom);
                BindingsBuilder b;
                if (!bindings_builder_init(&b, NULL)) {
                    outcome_set_free(&vals);
                    return;
                }
                ok = bindings_builder_add_var_fresh(&b, pat, bound_atom);
                outcome_set_free(&vals);
                if (ok) {
                    const Bindings *bb = bindings_builder_bindings(&b);
                    Bindings visible;
                    if (!bindings_project_body_visible_env(a, body_let,
                                                          bb,
                                                          &visible)) {
                        bindings_builder_free(&b);
                        return;
                    }
                    Atom *next_atom =
                        bindings_apply_projected_body_visible(&visible, a, body_let);
                    if (preserve_bindings) {
                        Bindings tail_visible;
                        if (!bindings_project_tail_env(a, next_atom, bb,
                                                       &tail_visible)) {
                            bindings_free(&visible);
                            bindings_builder_free(&b);
                            return;
                        }
                        if (!bindings_builder_merge_commit(&current_env_builder,
                                                           &tail_visible)) {
                            bindings_free(&tail_visible);
                            bindings_free(&visible);
                            bindings_builder_free(&b);
                            return;
                        }
                        bindings_free(&tail_visible);
                    }
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    if (val_cut) {
                        uint32_t before = os->len;
                        eval_for_current_caller(s, a, NULL, next_atom, fuel,
                                                &_empty, CURRENT_ENV,
                                                preserve_bindings, os);
                        outcome_set_mark_control_since(
                            os, before, CETTA_OUTCOME_CONTROL_CUT);
                        return;
                    }
                    TAIL_REENTER_LOCAL(next_atom);
                }
                bindings_builder_free(&b);
                return;
            } else {
                BindingsBuilder b;
                const Bindings *bb = NULL;
                bool used_builder = false;
                if ((is_true_atom(pat) && is_true_atom(val_atom)) ||
                    (is_false_atom(pat) && is_false_atom(val_atom))) {
                    ok = true;
                    bb = val_env;
                } else {
                    if (!bindings_builder_init(&b, val_env)) {
                        outcome_set_free(&vals);
                        return;
                    }
                    used_builder = true;
                    ok = simple_match_builder(pat, val_atom, &b);
                    if (ok)
                        bb = bindings_builder_bindings(&b);
                }
                outcome_set_free(&vals);
                if (ok) {
                    Bindings visible;
                    if (!bindings_project_body_visible_env(a, body_let,
                                                          bb,
                                                          &visible)) {
                        if (used_builder)
                            bindings_builder_free(&b);
                        return;
                    }
                    Atom *next_atom =
                        bindings_apply_projected_body_visible(&visible, a, body_let);
                    if (preserve_bindings) {
                        Bindings tail_visible;
                        if (!bindings_project_tail_env(a, next_atom, bb,
                                                       &tail_visible)) {
                            bindings_free(&visible);
                            if (used_builder)
                                bindings_builder_free(&b);
                            return;
                        }
                        if (!bindings_builder_merge_commit(&current_env_builder,
                                                           &tail_visible)) {
                            bindings_free(&tail_visible);
                            bindings_free(&visible);
                            if (used_builder)
                                bindings_builder_free(&b);
                            return;
                        }
                        bindings_free(&tail_visible);
                    }
                    bindings_free(&visible);
                    if (used_builder)
                        bindings_builder_free(&b);
                    if (val_cut) {
                        uint32_t before = os->len;
                        eval_for_current_caller(s, a, NULL, next_atom, fuel,
                                                &_empty, CURRENT_ENV,
                                                preserve_bindings, os);
                        outcome_set_mark_control_since(
                            os, before, CETTA_OUTCOME_CONTROL_CUT);
                        return;
                    }
                    TAIL_REENTER_LOCAL(next_atom);
                }
                if (used_builder)
                    bindings_builder_free(&b);
                return;
            }
        }
        /* Multi-result: no TCO */
        for (uint32_t i = 0; i < vals.len; i++) {
            Atom *val_atom =
                outcome_atom_materialize_traced(
                    a, &vals.items[i],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            const Bindings *val_env = &vals.items[i].env;
            if (pat->kind == ATOM_VAR) {
                Atom *bound_atom = bindings_apply_if_vars(val_env, a, val_atom);
                BindingsBuilder b;
                if (!bindings_builder_init(&b, NULL))
                    continue;
                if (!bindings_builder_add_var_fresh(&b, pat, bound_atom)) {
                    bindings_builder_free(&b);
                    continue;
                }
                const Bindings *bb = bindings_builder_bindings(&b);
                Bindings visible;
                if (!bindings_project_body_visible_env(a, body_let, bb, &visible)) {
                    bindings_builder_free(&b);
                    continue;
                }
                Bindings branch_outer_owned;
                const Bindings *branch_outer = CURRENT_ENV;
                if (!branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                            CURRENT_ENV, bb)) {
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    continue;
                }
                Atom *subst =
                    bindings_apply_projected_body_visible(&visible, a, body_let);
                uint32_t before = os->len;
                eval_for_current_caller(s, a, NULL,
                                        subst, fuel, &_empty,
                                        branch_outer, preserve_bindings, os);
                bool cut_seen = outcome_set_consume_control_since(
                    os, before, CETTA_OUTCOME_CONTROL_CUT);
                branch_outer_env_finish(&branch_outer_owned, branch_outer);
                bindings_free(&visible);
                bindings_builder_free(&b);
                if (cut_seen)
                    break;
            } else {
                BindingsBuilder b;
                const Bindings *bb = NULL;
                bool used_builder = false;
                bool matched = false;
                if ((is_true_atom(pat) && is_true_atom(val_atom)) ||
                    (is_false_atom(pat) && is_false_atom(val_atom))) {
                    matched = true;
                    bb = val_env;
                } else {
                    if (!bindings_builder_init(&b, val_env))
                        continue;
                    used_builder = true;
                    matched = simple_match_builder(pat, val_atom, &b);
                    if (matched)
                        bb = bindings_builder_bindings(&b);
                }
                if (matched) {
                    Bindings visible;
                    if (!bindings_project_body_visible_env(a, body_let, bb, &visible)) {
                        if (used_builder)
                            bindings_builder_free(&b);
                        continue;
                    }
                    Bindings branch_outer_owned;
                    const Bindings *branch_outer = CURRENT_ENV;
                    if (!branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                                CURRENT_ENV, bb)) {
                        bindings_free(&visible);
                        bindings_builder_free(&b);
                        continue;
                    }
                    Atom *subst =
                        bindings_apply_projected_body_visible(&visible, a, body_let);
                    uint32_t before = os->len;
                    eval_for_current_caller(s, a, NULL,
                                            subst, fuel, &_empty,
                                            branch_outer, preserve_bindings, os);
                    bool cut_seen = outcome_set_consume_control_since(
                        os, before, CETTA_OUTCOME_CONTROL_CUT);
                    branch_outer_env_finish(&branch_outer_owned, branch_outer);
                    bindings_free(&visible);
                    if (cut_seen) {
                        if (used_builder)
                            bindings_builder_free(&b);
                        break;
                    }
                }
                if (used_builder)
                    bindings_builder_free(&b);
            }
        }
        outcome_set_free(&vals);
        return;
    }

    /* ── chain ─────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.chain) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_CHAIN_CALL_COUNT);
        if (nargs != 3) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *to_eval = expr_arg(atom, 0);
        Atom *var = expr_arg(atom, 1);
        Atom *tmpl_chain = expr_arg(atom, 2);
        if (var->kind != ATOM_VAR) {
            outcome_set_add(os, call_signature_error(a, atom,
                "(chain <nested> (: <var> Variable) <templ>)"), &_empty);
            return;
        }
        if (atom_is_same_var(tmpl_chain, var) && to_eval->kind == ATOM_EXPR &&
            to_eval->expr.len > 0) {
            SymbolId to_eval_head = atom_head_symbol_id(to_eval);
            if ((to_eval_head == g_builtin_syms.eval && expr_nargs(to_eval) == 1) ||
                (to_eval_head == g_builtin_syms.unify && expr_nargs(to_eval) == 4)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_EVAL_CHAIN_IDENTITY_TAIL_REENTER_COUNT);
                TAIL_REENTER_LOCAL(to_eval);
            }
        }
        OutcomeSet inner;
        outcome_set_init(&inner);
        metta_eval_bind(s, a, to_eval, fuel, &inner);
        if (inner.len == 0) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_CHAIN_EMPTY_RESULT_COUNT);
            outcome_set_add(os, atom_empty(a), &_empty);
            outcome_set_free(&inner);
            return;
        }
        if (inner.len == 1 &&
            !atom_is_empty(
                outcome_atom_materialize_traced(
                    a, &inner.items[0],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN))) {
            /* Single-result fast path with TCO */
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_EVAL_CHAIN_SINGLE_RESULT_FASTPATH_COUNT);
            Atom *next_atom;
            bool has_binding = false;
            {
                Atom *inner_atom =
                    outcome_atom_materialize_traced(
                        a, &inner.items[0],
                        CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
                const Bindings *inner_env = &inner.items[0].env;
                __attribute__((cleanup(bindings_free))) Bindings projected_inner_env;
                bindings_init(&projected_inner_env);
                if (inner_env->eq_len == 0 && inner_env->len != 0) {
                    inner_atom = bindings_apply_if_vars(inner_env, a, inner_atom);
                    if (!bindings_project_body_visible_env(a, tmpl_chain, inner_env,
                                                           &projected_inner_env)) {
                        outcome_set_free(&inner);
                        return;
                    }
                    if (projected_inner_env.len < inner_env->len) {
                        cetta_runtime_stats_inc(
                            CETTA_RUNTIME_COUNTER_EVAL_TAIL_SAFE_POINT_COUNT);
                        cetta_runtime_stats_update_max(
                            CETTA_RUNTIME_COUNTER_EVAL_TAIL_PROMOTED_BINDING_ENTRIES_PEAK,
                            inner_env->len - projected_inner_env.len);
                    }
                    inner_env = &projected_inner_env;
                }
                BindingsBuilder b;
                if (!bindings_builder_init(&b, inner_env)) {
                    outcome_set_free(&inner);
                    return;
                }
                has_binding = bindings_builder_add_var_fresh(&b, var, inner_atom);
                if (!has_binding) {
                    bindings_builder_free(&b);
                    outcome_set_free(&inner);
                    return;
                }
                const Bindings *bb = bindings_builder_bindings(&b);
                Bindings visible;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_EVAL_CHAIN_VISIBLE_PROJECT_COUNT);
                if (!bindings_project_body_visible_env(a, tmpl_chain, bb, &visible)) {
                    bindings_builder_free(&b);
                    outcome_set_free(&inner);
                    return;
                }
                next_atom =
                    bindings_apply_projected_body_visible(&visible, a, tmpl_chain);
                if (preserve_bindings) {
                    Bindings tail_visible;
                    if (!bindings_project_tail_env(a, next_atom, bb,
                                                   &tail_visible)) {
                        bindings_free(&visible);
                        bindings_builder_free(&b);
                        outcome_set_free(&inner);
                        return;
                    }
                    if (!bindings_builder_merge_commit(&current_env_builder,
                                                       &tail_visible)) {
                        bindings_free(&tail_visible);
                        bindings_free(&visible);
                        bindings_builder_free(&b);
                        outcome_set_free(&inner);
                        return;
                    }
                    bindings_free(&tail_visible);
                }
                outcome_set_free(&inner);
                bindings_free(&visible);
                bindings_builder_free(&b);
                TAIL_REENTER_LOCAL(next_atom);
            }
            outcome_set_free(&inner);
            TAIL_REENTER_LOCAL(next_atom);
        }
        /* Multi-result: no TCO */
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_EVAL_CHAIN_MULTI_RESULT_FALLBACK_COUNT);
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *r =
                outcome_atom_materialize_traced(
                    a, &inner.items[i],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            if (atom_is_empty(r)) continue;
            {
                const Bindings *inner_env = &inner.items[i].env;
                BindingsBuilder b;
                if (!bindings_builder_init(&b, inner_env))
                    continue;
                if (!bindings_builder_add_var_fresh(&b, var, r)) {
                    bindings_builder_free(&b);
                    continue;
                }
                const Bindings *bb = bindings_builder_bindings(&b);
                Bindings visible;
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_EVAL_CHAIN_VISIBLE_PROJECT_COUNT);
                if (!bindings_project_body_visible_env(a, tmpl_chain, bb, &visible)) {
                    bindings_builder_free(&b);
                    continue;
                }
                Atom *subst =
                    bindings_apply_projected_body_visible(&visible, a, tmpl_chain);
                Bindings branch_outer_owned;
                const Bindings *branch_outer = CURRENT_ENV;
                if (preserve_bindings &&
                    !branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                            CURRENT_ENV, bb)) {
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    continue;
                }
                eval_for_current_caller(s, a, NULL, subst, fuel, &_empty,
                                        branch_outer, preserve_bindings, os);
                branch_outer_env_finish(&branch_outer_owned, branch_outer);
                bindings_free(&visible);
                bindings_builder_free(&b);
            }
        }
        if (inner.len == 0)
            outcome_set_add(os, atom_empty(a), &_empty);
        outcome_set_free(&inner);
        return;
    }

    /* ── __petta_rel_run (internal Petta guarded-pattern relation runner) ── */
    if (head_id == g_builtin_syms.petta_rel_run) {
        if (nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *goal = expr_arg(atom, 0);
        Atom *body = expr_arg(atom, 1);
        OutcomeSet goal_outcomes;
        outcome_set_init(&goal_outcomes);
        petta_rel_goal_eval(s, a, goal, fuel, CURRENT_ENV, &goal_outcomes);

        bool had_success = false;
        bool had_error = false;
        bool cut_seen = false;
        for (uint32_t i = 0; i < goal_outcomes.len; i++) {
            Atom *goal_atom = outcome_atom_materialize(a, &goal_outcomes.items[i]);
            if (atom_is_error(goal_atom)) {
                had_error = true;
                outcome_set_add_existing(os, &goal_outcomes.items[i]);
                continue;
            }
            if (atom_is_empty(goal_atom) || is_false_atom(goal_atom) ||
                atom_is_unit_literal(goal_atom))
                continue;
            had_success = true;
            Bindings visible_body_env;
            const Bindings *body_outer_env = &goal_outcomes.items[i].env;
            if (petta_rel_goal_project_env(a, body, &goal_outcomes.items[i].env,
                                           &visible_body_env)) {
                body_outer_env = &visible_body_env;
            }
            Atom *applied_body =
                bindings_apply_body_visible_vars(body_outer_env, a, body);
            if (body &&
                body->kind == ATOM_VAR &&
                applied_body &&
                applied_body->kind == ATOM_VAR &&
                applied_body->var_id == body->var_id &&
                applied_body->sym_id == body->sym_id) {
                Atom *spelling_match = NULL;
                bool ambiguous = false;
                for (uint32_t j = 0; j < body_outer_env->len; j++) {
                    if (body_outer_env->entries[j].spelling != body->sym_id)
                        continue;
                    VisibleVarRef producer = {
                        .var_id = body_outer_env->entries[j].var_id,
                        .spelling = body_outer_env->entries[j].spelling,
                    };
                    Atom *producer_resolved =
                        bindings_resolve_body_visible_var(a, body_outer_env, &producer);
                    if (producer_resolved->kind == ATOM_VAR &&
                        producer_resolved->var_id == producer.var_id &&
                        producer_resolved->sym_id == producer.spelling) {
                        continue;
                    }
                    if (spelling_match &&
                        !atom_eq(spelling_match, producer_resolved)) {
                        ambiguous = true;
                        break;
                    }
                    spelling_match = producer_resolved;
                }
                if (!ambiguous && spelling_match)
                    applied_body = spelling_match;
            }
            OutcomeSet body_outcomes;
            outcome_set_init(&body_outcomes);
            metta_eval_bind_typed(s, a, NULL, applied_body, fuel, &body_outcomes);
            uint32_t before = os->len;
            outcome_set_append_prefixed_move(a, os, &body_outcomes,
                                             body_outer_env, preserve_bindings);
            if (goal_outcomes.items[i].control == CETTA_OUTCOME_CONTROL_CUT)
                outcome_set_mark_control_since(os, before,
                                               CETTA_OUTCOME_CONTROL_CUT);
            cut_seen = outcome_set_consume_control_since(
                os, before, CETTA_OUTCOME_CONTROL_CUT);
            if (cut_seen)
                outcome_set_mark_control_since(os, before,
                                               CETTA_OUTCOME_CONTROL_CUT);
            outcome_set_free(&body_outcomes);
            bindings_free(&visible_body_env);
            if (cut_seen)
                break;
        }
        if (!had_success && !had_error)
            outcome_set_add(os, atom_empty(a), &_empty);
        outcome_set_free(&goal_outcomes);
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

    /* ── collect / fold / fold-by-key / select / once ─────────────────── */
    if (head_id == g_builtin_syms.collect ||
        head_id == g_builtin_syms.fold ||
        head_id == g_builtin_syms.fold_by_key ||
        head_id == g_builtin_syms.reduce ||
        head_id == g_builtin_syms.select ||
        head_id == g_builtin_syms.once) {
        bool is_collect = head_id == g_builtin_syms.collect;
        bool is_fold = head_id == g_builtin_syms.fold ||
                       head_id == g_builtin_syms.reduce;
        bool is_fold_by_key = head_id == g_builtin_syms.fold_by_key;
        bool is_reduce_alias = head_id == g_builtin_syms.reduce;
        bool is_once = head_id == g_builtin_syms.once;
        const char *surface = is_collect ? "collect" :
                              (is_fold ? (is_reduce_alias ? "reduce" : "fold") :
                               (is_fold_by_key ? "fold-by-key" :
                                (is_once ? "once" : "select")));
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

        if (is_fold) {
            Atom *init = NULL;
            Atom *acc_var = NULL;
            Atom *item_var = NULL;
            Atom *step_expr = NULL;

            if (nargs == 5) {
                stream_expr = expr_arg(atom, 0);
                init = expr_arg(atom, 1);
                acc_var = expr_arg(atom, 2);
                item_var = expr_arg(atom, 3);
                step_expr = expr_arg(atom, 4);
            } else if (nargs == 6) {
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
                init = expr_arg(atom, 2);
                acc_var = expr_arg(atom, 3);
                item_var = expr_arg(atom, 4);
                step_expr = expr_arg(atom, 5);
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
            if (!acc_var || acc_var->kind != ATOM_VAR) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, nargs == 5 ? 3 : 4,
                                       atom_variable_type(a), acc_var),
                    &_empty);
                return;
            }
            if (!item_var || item_var->kind != ATOM_VAR) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, nargs == 5 ? 4 : 5,
                                       atom_variable_type(a), item_var),
                    &_empty);
                return;
            }

            Arena stream_scratch;
            arena_init(&stream_scratch);
            arena_set_runtime_kind(&stream_scratch,
                                   CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            if (a->hashcons)
                arena_set_hashcons(&stream_scratch, a->hashcons);
            reduce_stream_results(s, a, &stream_scratch, atom, stream_expr, policy.order,
                                  init, acc_var->sym_id, item_var->sym_id,
                                  step_expr, fuel, os);
            arena_free(&stream_scratch);
            return;
        }

        if (is_fold_by_key) {
            Atom *init = NULL;
            Atom *acc_var = NULL;
            Atom *item_var = NULL;
            Atom *key_expr = NULL;
            Atom *step_expr = NULL;

            if (nargs == 6) {
                stream_expr = expr_arg(atom, 0);
                init = expr_arg(atom, 1);
                acc_var = expr_arg(atom, 2);
                item_var = expr_arg(atom, 3);
                key_expr = expr_arg(atom, 4);
                step_expr = expr_arg(atom, 5);
            } else if (nargs == 7) {
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
                init = expr_arg(atom, 2);
                acc_var = expr_arg(atom, 3);
                item_var = expr_arg(atom, 4);
                key_expr = expr_arg(atom, 5);
                step_expr = expr_arg(atom, 6);
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
            if (!acc_var || acc_var->kind != ATOM_VAR) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, nargs == 6 ? 3 : 4,
                                       atom_variable_type(a), acc_var),
                    &_empty);
                return;
            }
            if (!item_var || item_var->kind != ATOM_VAR) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, nargs == 6 ? 4 : 5,
                                       atom_variable_type(a), item_var),
                    &_empty);
                return;
            }

            Arena stream_scratch;
            arena_init(&stream_scratch);
            arena_set_runtime_kind(&stream_scratch,
                                   CETTA_ARENA_RUNTIME_KIND_SCRATCH);
            if (a->hashcons)
                arena_set_hashcons(&stream_scratch, a->hashcons);
            fold_by_key_stream_results(s, a, &stream_scratch, atom, stream_expr, policy.order,
                                       init, acc_var->sym_id, item_var->sym_id,
                                       key_expr, step_expr, fuel, os);
            arena_free(&stream_scratch);
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
                outcome_set_add(os,
                    language_is_petta()
                        ? atom_true(a)
                        : atom_unit(a),
                    &_empty);
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

    /* ── call (dynamic function call) ──────────────────────────────────── */
    if (head_id == g_builtin_syms.call_text) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        TAIL_REENTER_CONTINUATION(expr_arg(atom, 0));
    }

    /* ── parse (source string to syntax data) ──────────────────────────── */
    if (head_id == g_builtin_syms.parse) {
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *source = expr_arg(atom, 0);
        if (!source || source->kind != ATOM_GROUNDED ||
            source->ground.gkind != GV_STRING) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "StringExpected")),
                &_empty);
            return;
        }
        Atom **parsed = NULL;
        int count = parse_metta_text(source->ground.sval, a, &parsed);
        if (count < 0 || !parsed) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "ParseFailed")),
                &_empty);
            return;
        }
        if (count != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "SingleExpressionExpected")),
                &_empty);
            free(parsed);
            return;
        }
        Atom *syntax = parsed[0];
        if (parsed_syntax_needs_quote(syntax))
            syntax = atom_expr2(a, atom_symbol_id(a, g_builtin_syms.quote), syntax);
        outcome_set_add(os, syntax, &_empty);
        free(parsed);
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
        TAIL_REENTER_CONTINUATION(expr_arg(atom, 0));
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
        TAIL_REENTER_LOCAL(rewrite);
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
        SpaceEngine backend_kind = default_runtime_space_backend_kind(s);
        if (nargs == 1) {
            if (active_profile() &&
                !cetta_profile_allows_surface(active_profile(), "new-space-kind")) {
                outcome_set_add(os, profile_surface_error(a, atom, "new-space-kind"), &_empty);
                return;
            }
            const char *kind_name = string_like_atom(expr_arg(atom, 0));
            if (kind_name && strcmp(kind_name, "pathmap") == 0) {
                backend_kind = SPACE_ENGINE_PATHMAP;
                const char *reason = space_match_backend_unavailable_reason(backend_kind);
                if (reason) {
                    outcome_set_add(os,
                        atom_error(a, atom, atom_string(a, reason)),
                        &_empty);
                    return;
                }
            } else if (kind_name && strcmp(kind_name, "mork") == 0) {
                outcome_set_add(os,
                    atom_error(a, atom,
                               atom_string(a, "generic (new-space mork) is disabled; use (mork:new-space)")),
                    &_empty);
                return;
            } else if (kind_name && strcmp(kind_name, "native") == 0) {
                backend_kind = SPACE_ENGINE_NATIVE;
            } else if (!space_kind_from_name(kind_name, &kind)) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "UnknownSpaceKind")),
                    &_empty);
                return;
            }
            if (kind == SPACE_KIND_STACK || kind == SPACE_KIND_QUEUE)
                backend_kind = SPACE_ENGINE_NATIVE;
        }
        Arena *pa = eval_storage_arena(a);
        Space *ns = alloc_runtime_space(pa, s, kind, backend_kind);
        if (!ns) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnknownSpaceEngine")),
                &_empty);
            return;
        }
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
        char spec_buf[PATH_MAX];
        const char *spec = module_spec_atom(expr_arg(atom, 1), spec_buf,
                                            sizeof(spec_buf));
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
                                        a, eval_storage_arena(a),
                                        g_registry, fuel, &error)) {
            if (dest.is_fresh) {
                Arena *pa = eval_storage_arena(a);
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
        if (nargs != 1 && nargs != 2) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (nargs == 2 &&
            emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.lib_mork_space_include, os)) {
            return;
        }
        Atom *error = NULL;
        Space *target_space = s;
        const char *spec = NULL;
        char spec_buf[PATH_MAX];
        if (nargs == 1) {
            spec = module_spec_atom(expr_arg(atom, 0), spec_buf,
                                    sizeof(spec_buf));
        } else {
            target_space = resolve_include_destination(a, expr_arg(atom, 0), &error);
            if (!error) {
                spec = module_spec_atom(expr_arg(atom, 1), spec_buf,
                                        sizeof(spec_buf));
            }
        }
        if (!spec && !error) {
            error = atom_symbol(a, "include expects a module name argument");
        }
        if (!error &&
            cetta_library_include_module(g_library_context, spec, target_space, a,
                                        eval_storage_arena(a),
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
        char spec_buf[PATH_MAX];
        const char *spec = module_spec_atom(expr_arg(atom, 0), spec_buf,
                                            sizeof(spec_buf));
        Atom *error = NULL;
        if (!spec) {
            error = atom_symbol(a, "mod-space! expects a module name argument");
        }
        Atom *space_atom = NULL;
        if (!error) {
            space_atom = cetta_library_mod_space(g_library_context, spec, a,
                                                eval_storage_arena(a),
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
            g_library_context, a, eval_storage_arena(a), &error);
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
        cetta_runtime_stats_enable();
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
        Space *target = resolve_runtime_space_ref(s, a, space_ref);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        Space *snapshot = space_snapshot_clone(target, a);
        if (!snapshot) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed")),
                &_empty);
            return;
        }
        Atom *snapshot_atom = atom_space(a, snapshot);
        if (binder->kind == ATOM_VAR) {
            BindingsBuilder b;
            if (!bindings_builder_init(&b, NULL))
                return;
            if (!bindings_builder_add_var_fresh(&b, binder, snapshot_atom)) {
                bindings_builder_free(&b);
                return;
            }
            const Bindings *bb = bindings_builder_bindings(&b);
            Atom *next_atom = bindings_apply_if_vars(bb, a, body);
            if (preserve_bindings) {
                Bindings tail_visible;
                if (!bindings_project_tail_env(a, next_atom, bb,
                                               &tail_visible)) {
                    bindings_builder_free(&b);
                    return;
                }
                if (!bindings_builder_merge_commit(&current_env_builder,
                                                   &tail_visible)) {
                    bindings_free(&tail_visible);
                    bindings_builder_free(&b);
                    return;
                }
                bindings_free(&tail_visible);
            }
            bindings_builder_free(&b);
            TAIL_REENTER_LOCAL(next_atom);
        } else {
            BindingsBuilder b;
            if (!bindings_builder_init(&b, NULL))
                return;
            if (simple_match_builder(binder, snapshot_atom, &b)) {
                const Bindings *bb = bindings_builder_bindings(&b);
                Atom *next_atom = bindings_apply_if_vars(bb, a, body);
                if (preserve_bindings) {
                    Bindings tail_visible;
                    if (!bindings_project_tail_env(a, next_atom, bb,
                                                   &tail_visible)) {
                        bindings_builder_free(&b);
                        return;
                    }
                    if (!bindings_builder_merge_commit(&current_env_builder,
                                                       &tail_visible)) {
                        bindings_free(&tail_visible);
                        bindings_builder_free(&b);
                        return;
                    }
                    bindings_free(&tail_visible);
                }
                bindings_builder_free(&b);
                TAIL_REENTER_LOCAL(next_atom);
            }
            bindings_builder_free(&b);
        }
        return;
    }

    /* ── structured space introspection / ordered-space ops ───────────── */
    if (head_id == g_builtin_syms.space_set_backend_bang ||
        head_id == g_builtin_syms.space_set_match_backend_bang) {
        const char *surface_name =
            (head_id == g_builtin_syms.space_set_backend_bang)
                ? "space-set-backend!"
                : "space-set-match-backend!";
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), surface_name)) {
            outcome_set_add(os,
                profile_surface_error(a, atom, surface_name), &_empty);
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
                head_id == g_builtin_syms.space_set_backend_bang
                    ? "space-set-backend! expects a space as its first argument"
                    : "space-set-match-backend! expects a space as its first argument"),
                &_empty);
            return;
        }
        ResultSet backend_rs;
        result_set_init(&backend_rs);
        metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &backend_rs);
        Atom *backend_atom = (backend_rs.len > 0) ? backend_rs.items[0] : expr_arg(atom, 1);
        const char *backend_name = string_like_atom(backend_atom);
        SpaceEngine kind = SPACE_ENGINE_NATIVE;
        if (backend_name && strcmp(backend_name, "mork") == 0) {
            free(backend_rs.items);
            outcome_set_add(os,
                atom_error(a, atom,
                           atom_string(a,
                                       head_id == g_builtin_syms.space_set_backend_bang
                                           ? "generic space-set-backend! no longer accepts mork; use (mork:new-space)"
                                           : "generic space-set-match-backend! no longer accepts mork; use (mork:new-space)")),
                &_empty);
            return;
        }
        if (!backend_name ||
            !space_match_backend_kind_from_name(backend_name, &kind)) {
            free(backend_rs.items);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnknownSpaceEngine")),
                &_empty);
            return;
        }
        const char *reason = space_match_backend_unavailable_reason(kind);
        if (reason) {
            free(backend_rs.items);
            outcome_set_add(os,
                atom_error(a, atom, atom_string(a, reason)),
                &_empty);
            return;
        }
        if (!space_match_backend_try_set(target, kind)) {
            free(backend_rs.items);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnknownSpaceEngine")),
                &_empty);
            return;
        }
        free(backend_rs.items);
        outcome_set_add(os, atom_unit(a), &_empty);
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
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.lib_mork_space_size, os)) {
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, expr_arg(atom, 0), fuel, "space-len", "mork:size");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "space-len expects a space as its argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "space-len", "mork:size");
        if (mork_error) {
            outcome_set_add(os, mork_error, &_empty);
            return;
        }
        outcome_set_add(os, atom_int(a, (int64_t)space_length(target)), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.step_bang) {
        if (active_profile() &&
            !cetta_profile_allows_surface(active_profile(), "step!")) {
            outcome_set_add(os, profile_surface_error(a, atom, "step!"), &_empty);
            return;
        }
        if ((nargs != 1 && nargs != 2) || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.lib_mork_space_step, os)) {
            return;
        }
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, expr_arg(atom, 0), fuel, "step!", "mork:step!");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "step! expects a space as its first argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "step!", "mork:step!");
        if (mork_error) {
            outcome_set_add(os, mork_error, &_empty);
            return;
        }
        uint64_t limit = 1;
        if (nargs == 2) {
            ResultSet step_rs;
            result_set_init(&step_rs);
            metta_eval(s, a, NULL, expr_arg(atom, 1), fuel, &step_rs);
            Atom *step_atom = (step_rs.len > 0) ? step_rs.items[0] : expr_arg(atom, 1);
            bool bad_limit = step_atom->kind != ATOM_GROUNDED ||
                             step_atom->ground.gkind != GV_INT ||
                             step_atom->ground.ival < 0;
            if (bad_limit) {
                free(step_rs.items);
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "ExpectedNonNegativeStepCount")),
                    &_empty);
                return;
            }
            limit = (uint64_t)step_atom->ground.ival;
            free(step_rs.items);
        }
        if (!space_engine_supports_exec(target->match_backend.kind) ||
            space_is_ordered(target)) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "NoStepSemantics")),
                &_empty);
            return;
        }
        uint64_t performed = 0;
        Arena *dst = eval_storage_arena(a);
        if (!space_match_backend_step(target, dst, limit, &performed)) {
            const char *detail = cetta_mork_bridge_last_error();
            if (detail && *detail) {
                outcome_set_add(os, atom_error(a, atom, atom_string(a, detail)), &_empty);
            } else {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "SpaceStepFailed")),
                    &_empty);
            }
            return;
        }
        outcome_set_add(os, atom_int(a, (int64_t)performed), &_empty);
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
        Arena *dst = eval_storage_arena(a);
        (void)space_admit_atom(target, dst, expr_arg(atom, 1));
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
            Arena *dst = eval_storage_arena(a);
            Atom *stored = NULL;
            if (eval_storage_is_persistent(dst) &&
                val->kind == ATOM_GROUNDED &&
                val->ground.gkind == GV_SPACE &&
                temp_space_is_registered((Space *)val->ground.ptr)) {
                Space *clone = space_persistent_clone((Space *)val->ground.ptr, dst);
                if (!clone) {
                    free(val_rs.items);
                    outcome_set_add(os,
                        atom_error(a, atom,
                                   atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed")),
                        &_empty);
                    return;
                }
                stored = atom_space(dst, clone);
            } else {
                stored = eval_store_atom(dst, val);
            }
            registry_bind_id(g_registry, name->sym_id, stored);
        }
        free(val_rs.items);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-reduct ────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_reduct && nargs == 2 && g_registry) {
        Atom *space_ref = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 0));
        Atom *surface_atom = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 1));
        surface_atom = petta_unwrap_noeval_value(surface_atom);
        if (surface_atom && surface_atom->kind == ATOM_EXPR &&
            surface_atom->expr.len == 3 &&
            atom_is_symbol_id(surface_atom->expr.elems[0], g_builtin_syms.equals)) {
            Atom *decl_head = surface_atom->expr.elems[1];
            Atom *body_expr = surface_atom->expr.elems[2];
            Atom *body_tuple = atom_expr(a, &body_expr, 1);
            OutcomeSet vals;
            outcome_set_init(&vals);
            metta_eval_bind(s, a, body_tuple, fuel, &vals);

            bool emitted_error = false;
            for (uint32_t vi = 0; vi < vals.len; vi++) {
                Atom *reduced = outcome_atom_materialize(a, &vals.items[vi]);
                if (atom_is_empty(reduced))
                    continue;
                if (atom_is_error(reduced)) {
                    outcome_set_add(os, reduced, &vals.items[vi].env);
                    emitted_error = true;
                    continue;
                }
                Atom *reduced_decl =
                    atom_expr3(a,
                               atom_symbol_id(a, g_builtin_syms.equals),
                               decl_head, reduced);
                Atom **adapted_atoms = NULL;
                int adapted_len = 0;
                Space *target = NULL;
                Atom *prepare_error = prepare_runtime_mutation_atoms_or_error(
                    s, a, atom, space_ref, reduced_decl, fuel,
                    "add-reduct", "mork:add-atom",
                    "add-reduct expects a space as the first argument",
                    &target, &adapted_atoms, &adapted_len);
                if (prepare_error) {
                    outcome_set_add(os, prepare_error, &_empty);
                    emitted_error = true;
                    continue;
                }
                Arena *dst = eval_storage_arena(a);
                for (int ai = 0; ai < adapted_len; ai++)
                    (void)space_admit_atom(target, dst, adapted_atoms[ai]);
                free(adapted_atoms);
            }
            if (!emitted_error)
                outcome_set_add(os, atom_unit(a), &_empty);
            outcome_set_free(&vals);
            return;
        }

        Space **targets = NULL;
        uint32_t ntargets = 0;
        collect_resolved_spaces(s, a, space_ref, fuel, &targets, &ntargets);

        OutcomeSet vals;
        outcome_set_init(&vals);
        metta_eval_bind(s, a, surface_atom, fuel, &vals);

        if (ntargets > 0) {
            Arena *dst = eval_storage_arena(a);
            for (uint32_t ti = 0; ti < ntargets; ti++) {
                for (uint32_t vi = 0; vi < vals.len; vi++) {
                    Atom *stored_val = outcome_atom_materialize(a, &vals.items[vi]);
                    (void)space_admit_atom(targets[ti], dst, stored_val);
                    outcome_set_add(os, atom_unit(a), &vals.items[vi].env);
                }
            }
        }

        free(targets);
        outcome_set_free(&vals);
        return;
    }

    /* ── add-atoms ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_atoms && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *items = expr_arg(atom, 1);
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.mork_add_atoms, os)) {
            return;
        }
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "add-atoms", "mork:add-atoms");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atoms expects a space as the first argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "add-atoms", "mork:add-atoms");
        if (mork_error) {
            outcome_set_add(os, mork_error, &_empty);
            return;
        }
        if (items->kind != ATOM_EXPR) {
            outcome_set_add(os, atom_error(a, atom,
                atom_symbol(a, "add-atoms expects an expression of atoms as the second argument")),
                &_empty);
            return;
        }
        if (!space_match_backend_materialize_attached(
                target, eval_storage_arena(a))) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed")),
                &_empty);
            return;
        }
        Arena *dst = eval_storage_arena(a);
        for (uint32_t i = 0; i < items->expr.len; i++) {
            if (!space_admit_atom(target, dst, items->expr.elems[i])) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "AddAtomsFailed")),
                    &_empty);
                return;
            }
        }
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-atom ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_atom && nargs == 2 && g_registry) {
        Atom *space_ref = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 0));
        Atom *atom_to_add = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 1));
        Atom **adapted_atoms = NULL;
        int adapted_len = 0;
        Space *target = NULL;
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.mork_add_atom, os)) {
            return;
        }
        Atom *prepare_error = prepare_runtime_mutation_atoms_or_error(
            s, a, atom, space_ref, atom_to_add, fuel,
            "add-atom", "mork:add-atom",
            "add-atom expects a space as the first argument",
            &target, &adapted_atoms, &adapted_len);
        if (prepare_error) {
            outcome_set_add(os, prepare_error, &_empty);
            return;
        }
        /* Deep-copy to persistent arena so atom survives eval_arena reset */
        Arena *dst = eval_storage_arena(a);
        for (int i = 0; i < adapted_len; i++) {
            (void)space_admit_atom(target, dst, adapted_atoms[i]);
        }
        free(adapted_atoms);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-atom-nodup (dedup variant for forward chaining) ────────────── */
    if (head_id == g_builtin_syms.add_atom_nodup && nargs == 2 && g_registry) {
        Atom *space_ref = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 0));
        Atom *atom_to_add = dispatch_observed_arg(a, CURRENT_ENV, expr_arg(atom, 1));
        Atom **adapted_atoms = NULL;
        int adapted_len = 0;
        Space *target = NULL;
        Atom *prepare_error = prepare_runtime_mutation_atoms_or_error(
            s, a, atom, space_ref, atom_to_add, fuel,
            "add-atom-nodup", "mork:add-atom",
            "add-atom-nodup expects a space as the first argument",
            &target, &adapted_atoms, &adapted_len);
        if (prepare_error) {
            outcome_set_add(os, prepare_error, &_empty);
            return;
        }
        Arena *dst = eval_storage_arena(a);
        for (int i = 0; i < adapted_len; i++) {
            Atom *compare_atom = space_compare_atom(target, a, adapted_atoms[i]);
            bool found = false;
            bool backend_checked =
                space_match_backend_contains_atom_structural_direct(
                    target, compare_atom, &found);
            if (!backend_checked)
                found = space_contains_exact(target, compare_atom);
            if (!found && !backend_checked) {
                bool alpha_fallback = atom_has_vars(compare_atom);
                for (uint32_t si = 0; si < space_length(target) && !found; si++) {
                    Atom *candidate = space_get_at(target, si);
                    if (!candidate)
                        continue;
                    if (alpha_fallback ? atom_alpha_eq(candidate, compare_atom)
                                       : atom_eq(candidate, compare_atom)) {
                        found = true;
                    }
                }
            }
            if (!found) {
                (void)space_admit_atom(target, dst, adapted_atoms[i]);
            }
        }
        free(adapted_atoms);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── remove-atom ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.remove_atom && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_rm = expr_arg(atom, 1);
        Atom **adapted_atoms = NULL;
        int adapted_len = 0;
        Space *target = NULL;
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.mork_remove_atom, os)) {
            return;
        }
        Atom *prepare_error = prepare_runtime_mutation_atoms_or_error(
            s, a, atom, space_ref, atom_to_rm, fuel,
            "remove-atom", "mork:remove-atom",
            "remove-atom expects a space as the first argument",
            &target, &adapted_atoms, &adapted_len);
        if (prepare_error) {
            outcome_set_add(os, prepare_error, &_empty);
            return;
        }
        for (int i = 0; i < adapted_len; i++) {
            Atom *compare_atom = space_compare_atom(target, a, adapted_atoms[i]);
            bool removed = false;
            if (target && target->native.universe) {
                AtomId compare_id =
                    term_universe_lookup_atom_id(target->native.universe, compare_atom);
                if (compare_id != CETTA_ATOM_ID_NONE) {
                    removed = space_remove_atom_id(target, compare_id);
                }
            }
            if (!removed && language_is_petta() && atom_has_vars(compare_atom)) {
                removed = space_remove_alpha_eq_all(target, compare_atom);
            }
            if (!removed) {
                space_remove(target, compare_atom);
            }
        }
        free(adapted_atoms);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── get-atoms ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_atoms && nargs == 1 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        if (emit_generic_mork_handle_atoms_surface(
                s, a, atom, space_ref, fuel, os)) {
            return;
        }
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "get-atoms", "mork:get-atoms");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "get-atoms expects a space as its argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "get-atoms", "mork:get-atoms");
        if (mork_error) {
            outcome_set_add(os, mork_error, &_empty);
            return;
        }
        if (!space_match_backend_materialize_attached(
                target, eval_storage_arena(a))) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "AttachedCompiledSpaceMaterializeFailed")),
                &_empty);
            return;
        }
        for (uint32_t i = 0; i < space_length(target); i++)
            outcome_set_add_unfactored(os, space_get_at(target, i), &_empty);
        return;
    }

    /* ── count-atoms ──────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.count_atoms && nargs == 1 && g_registry) {
        if (active_profile() && !cetta_profile_allows_surface(active_profile(), "count-atoms")) {
            outcome_set_add(os, profile_surface_error(a, atom, "count-atoms"), &_empty);
            return;
        }
        Atom *space_ref = expr_arg(atom, 0);
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "count-atoms", "mork:count-atoms");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "count-atoms", "mork:count-atoms");
        if (mork_error) {
            outcome_set_add(os, mork_error, &_empty);
            return;
        }
        outcome_set_add(os, atom_int(a, (int64_t)space_length(target)), &_empty);
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
        Atom **pairs = NULL;
        uint32_t pair_len = 0;
        if (inner.len > 0)
            pairs = arena_alloc(a, sizeof(Atom *) * inner.len);
        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *result_atom = outcome_atom_materialize(a, &inner.items[i]);
            if (atom_is_empty(result_atom))
                continue;
            pairs[pair_len++] =
                atom_expr2(a, result_atom, bindings_to_atom(a, &inner.items[i].env));
        }
        outcome_set_add(os, atom_expr(a, pairs, pair_len), &_empty);
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
        Space *target = resolve_runtime_space_ref(s, a, space_ref);
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
        Arena *pa = eval_storage_arena(a);
        StateCell *cell = arena_alloc(pa, sizeof(StateCell));
        cell->value = eval_store_atom(pa, initial);
        /* Infer content type from initial value */
        Atom **itypes;
        uint32_t nit = get_atom_types(s, a, initial, &itypes);
        cell->content_type = (nit > 0) ? eval_store_atom(pa, itypes[0]) : atom_undefined_type(pa);
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
            Atom *state_ref = outcome_atom_materialize(a, &refs.items[i]);
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
            Atom *state_ref = outcome_atom_materialize(a, &refs.items[i]);
            state_ref = resolve_registry_refs(a, state_ref);
            if (!(state_ref->kind == ATOM_GROUNDED && state_ref->ground.gkind == GV_STATE)) {
                outcome_set_add(os,
                    state_bad_arg_type_error(s, a, atom, 1, state_ref),
                    &refs.items[i].env);
                continue;
            }

            ResultBindSet vals;
            rb_set_init(&vals);
            Atom *bound_val_expr =
                bindings_apply_if_vars(&refs.items[i].env, a, expr_arg(atom, 1));
            metta_eval_bind(s, a, bound_val_expr, fuel, &vals);
            BindingsBuilder merged_builder;
            if (!bindings_builder_init(&merged_builder, &refs.items[i].env)) {
                outcome_set_free(&vals);
                continue;
            }
            for (uint32_t vi = 0; vi < vals.len; vi++) {
                BindingsMergeAttempt attempt;
                Atom *new_v = outcome_atom_materialize(a, &vals.items[vi]);
                if (!bindings_builder_merge_or_clone(&merged_builder, &refs.items[i].env,
                                                     &vals.items[vi].env, &attempt))
                    continue;
                StateCell *cell = (StateCell *)state_ref->ground.ptr;
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
                    cell->value = eval_persistent_arena()
                        ? eval_store_atom(eval_persistent_arena(), new_v)
                        : new_v;
                    outcome_set_add(os, state_ref, attempt.env);
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
                        atom_symbol_id(a, g_builtin_syms.bad_arg_type), atom_int(a, 2),
                        cell->content_type, actual_t
                    }, 4);
                    outcome_set_add(os, atom_error(a, atom_expr(a, full, 3), reason),
                                    attempt.env);
                }
                bindings_merge_attempt_finish(&merged_builder, &attempt);
            }
            bindings_builder_free(&merged_builder);
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
        if (target->kind == ATOM_SYMBOL && is_grounded_op(target->sym_id)) {
            outcome_set_add(os, atom_grounded_type(a), &_empty);
            return;
        }
        outcome_set_add(os, get_meta_type(a, target), &_empty);
        return;
    }

    /* ── get-type ───────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_type && nargs == 1) {
        Atom *target = expr_arg(atom, 0);
        /* Resolve registry tokens for get-type */
        Atom *val = registry_lookup_atom(target);
        if (val) target = val;
        if (language_is_petta() && target->kind == ATOM_VAR) {
            char type_var_name[48];
            snprintf(type_var_name, sizeof(type_var_name),
                     "$__petta_type_%" PRIu32, fresh_var_suffix());
            outcome_set_add(os, atom_var(a, type_var_name), &_empty);
            return;
        }
        Atom **types;
        uint32_t n = get_atom_types_profiled(s, a, target, &types);
        if (language_is_petta()) {
            Atom **rule_types = NULL;
            uint32_t nrule_types =
                petta_collect_user_get_type_rule_types(s, a, target, &rule_types);
            if (nrule_types > 0) {
                if (petta_get_type_is_single_undefined(types, n)) {
                    free(types);
                    types = rule_types;
                    n = nrule_types;
                } else {
                    for (uint32_t i = 0; i < nrule_types; i++) {
                        (void)type_array_add_unique_alpha(&types, &n,
                                                          rule_types[i]);
                    }
                    free(rule_types);
                }
            }
        }
        if (get_type_should_try_value_result(target, types, n)) {
            bool was_undefined =
                n == 1 && atom_is_symbol_id(types[0], g_builtin_syms.undefined_type);
            Atom **value_types = NULL;
            uint32_t nvalue_types = 0;
            bool have_value_types =
                get_type_eval_result_types(s, a, target, fuel,
                                           &value_types, &nvalue_types);
            if (have_value_types) {
                free(types);
                types = value_types;
                n = nvalue_types;
            } else if (was_undefined) {
                free(types);
                types = cetta_malloc(sizeof(Atom *));
                types[0] = atom_undefined_type(a);
                n = 1;
            }
        }
        if (language_is_petta() &&
            petta_get_type_is_single_undefined(types, n) &&
            target->kind == ATOM_EXPR) {
            Atom **tuple_types = NULL;
            uint32_t ntuple_types =
                petta_infer_tuple_types(s, a, target, &tuple_types);
            if (ntuple_types > 0) {
                free(types);
                types = tuple_types;
                n = ntuple_types;
            }
        }
        for (uint32_t i = 0; i < n; i++) {
            if (!atom_list_contains_alpha_eq(types, i, types[i]))
                outcome_set_add(os, types[i], &_empty);
        }
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
            outcome_set_add_unique_alpha(os, a, types[i], &_empty);
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
            bool *used = alloc_zeroed_bool_array(expected.len);
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_assert_eq(actual.items[i], expected.items[j])) {
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
            outcome_set_add(os,
                language_is_petta()
                    ? atom_true(a)
                    : atom_unit(a),
                &_empty);
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
                    if (atom_assert_eq(expected.items[i], actual.items[j])) { found = true; break; }
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
                    if (atom_assert_eq(actual.items[i], expected.items[j])) { found = true; break; }
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
                    if (!atom_assert_eq(actual.items[i], expected_item))
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
            bool *used = alloc_zeroed_bool_array(expected.len);
            for (uint32_t i = 0; i < actual.len && ok; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++) {
                    if (!used[j] && atom_assert_eq(actual.items[i], expected.items[j])) {
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
            outcome_set_add(os,
                language_is_petta()
                    ? atom_true(a)
                    : atom_unit(a),
                &_empty);
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
                    if (!atom_assert_eq(actual.items[i], expected_item))
                        ok = false;
                }
            }
        }
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0)
            ok = true;
        free(actual.items);
        if (ok) {
            outcome_set_add(os,
                language_is_petta()
                    ? atom_true(a)
                    : atom_unit(a),
                &_empty);
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
            bool *used = alloc_zeroed_bool_array(expected.len);
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
            outcome_set_add(os,
                language_is_petta()
                    ? atom_true(a)
                    : atom_unit(a),
                &_empty);
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
            bool *used = alloc_zeroed_bool_array(expected.len);
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
        CettaTailScope tail_scope = CETTA_TAIL_SCOPE_CONTINUATION;
        __attribute__((cleanup(bindings_free))) Bindings tail_env;
        OutcomeSet dispatch_results;
        outcome_set_init(&dispatch_results);
        if (handle_dispatch(s, a, atom, etype, fuel, CURRENT_ENV, preserve_bindings,
                            &local_tail_survivor,
                            &tail_next, &tail_type, &tail_env,
                            &tail_scope,
                            &dispatch_results)) {
            if (tail_next) {
                bool tail_env_changed = false;
                bool tail_env_applied =
                    tail_apply_bindings_to_next(a, &tail_next, &tail_type,
                                                &tail_env,
                                                &tail_env_changed);
                if (!tail_env_applied &&
                    !bindings_builder_merge_commit(&current_env_builder, &tail_env)) {
                    outcome_set_free(&dispatch_results);
                    return;
                }
                outcome_set_free(&dispatch_results);
                if (tail_type) etype = tail_type;
                if (tail_scope == CETTA_TAIL_SCOPE_LOCAL &&
                    tail_env_applied &&
                    (!CURRENT_ENV ||
                     (CURRENT_ENV->len == 0 && CURRENT_ENV->eq_len == 0))) {
                    bool committed_pending = false;
                    if (!tail_env_changed) {
                        committed_pending =
                            eval_local_tail_survivor_commit_pending_reentry(
                                &local_tail_survivor, a,
                                local_tail_eval_mark);
                    }
                    if (tail_env_changed || !committed_pending) {
                        bool needs_promotion;
                        if (tail_env_changed &&
                            eval_local_tail_survivor_has_pending(
                                &local_tail_survivor)) {
                            tail_next = atom_deep_copy(a, tail_next);
                            if (etype)
                                etype = atom_deep_copy(a, etype);
                        }
                        eval_local_tail_survivor_abort_pending(
                            &local_tail_survivor);
                        needs_promotion =
                            tail_env_changed ||
                            atom_references_reclaimable_region(
                                a, local_tail_eval_mark, tail_next) ||
                            atom_references_reclaimable_region(
                                a, local_tail_eval_mark, etype);
                        if (needs_promotion) {
                            (void)eval_local_tail_survivor_promote_reentry(
                                &local_tail_survivor, a,
                                local_tail_eval_mark, &tail_next, &etype);
                        } else {
                            eval_tail_reset_to_mark(a, local_tail_eval_mark);
                        }
                    }
                }
                TAIL_REENTER_ENV_SCOPED(tail_next, NULL, tail_scope);
            }
            outcome_set_append_prefixed(a, os, &dispatch_results, CURRENT_ENV,
                                        preserve_bindings);
            outcome_set_free(&dispatch_results);
            return;
        }
        outcome_set_free(&dispatch_results);
    }
}

#undef outcome_set_add
#undef TAIL_REENTER_CONTINUATION
#undef TAIL_REENTER_LOCAL
#undef TAIL_REENTER_ENV_SCOPED
#undef NOTE_TAIL_REENTER
#undef CURRENT_ENV


/* ── Top-level evaluation ───────────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs) {
    Registry *prev_registry = g_registry;
    Space *prev_root_space = g_eval_root_space;
    Arena *prev_fallback_persistent = g_eval_fallback_universe.persistent_arena;
    g_registry = NULL;
    g_eval_root_space = s;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, NULL);
    eval_release_outcome_variant_bank();
    metta_eval(s, a, NULL, expr, current_eval_fuel_limit(), rs);
    eval_release_outcome_variant_bank();
    g_registry = prev_registry;
    g_eval_root_space = prev_root_space;
    term_universe_set_persistent_arena(&g_eval_fallback_universe,
                                       prev_fallback_persistent);
}

void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs) {
    Registry *prev_registry = g_registry;
    Space *prev_root_space = g_eval_root_space;
    Arena *prev_fallback_persistent = g_eval_fallback_universe.persistent_arena;
    g_registry = r;
    g_eval_root_space = s;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, persistent);
    eval_release_outcome_variant_bank();
    metta_eval(s, a, NULL, expr, current_eval_fuel_limit(), rs);
    eval_release_outcome_variant_bank();
    g_registry = prev_registry;
    g_eval_root_space = prev_root_space;
    term_universe_set_persistent_arena(&g_eval_fallback_universe,
                                       prev_fallback_persistent);
}

void eval_set_default_fuel(int fuel) {
    cetta_eval_session_set_fuel_limit(fallback_eval_session(), fuel);
}

int eval_get_default_fuel(void) {
    return fallback_eval_session()->options.fuel_limit;
}

void eval_set_library_context(CettaLibraryContext *ctx) {
    g_library_context = ctx;
    if (!ctx) {
        cetta_language_set_current(fallback_eval_session()->language);
        return;
    }
    ctx->session.options.fuel_limit = fallback_eval_session()->options.fuel_limit;
    cetta_language_set_current(ctx->session.language);
}
