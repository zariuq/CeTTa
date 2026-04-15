#define _GNU_SOURCE
#include "eval.h"
#include "match.h"
#include "search_machine.h"
#include "grounded.h"
#include "library.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "stats.h"
#include "table_store.h"
#include "term_universe.h"
#include "variant_shape.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Global registry for named spaces/values (set by eval_top_with_registry) */
static Registry *g_registry = NULL;
/* Current evaluation root space and persistent fallback. */
static Space *g_eval_root_space = NULL;
static TermUniverse g_eval_fallback_universe = {0};
/* Query cache for the current logical evaluation episode. */
static TableStore g_episode_table;
static bool g_episode_table_ready = false;
static VariantBank g_episode_outcome_variant_bank;
static bool g_episode_outcome_variant_bank_ready = false;
/* Active importable library set */
static CettaLibraryContext *g_library_context = NULL;
static CettaEvalSession g_fallback_eval_session;
static bool g_fallback_eval_session_ready = false;

typedef struct {
    Space **items;
    uint32_t len, cap;
} TempSpaceSet;

static TempSpaceSet g_temp_spaces = {0};

static Atom *resolve_registry_refs(Arena *a, Atom *atom);

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

static bool eval_type_check_auto_enabled(void) {
    return active_eval_options_const()->type_check_auto;
}

static bool eval_bare_minimal_enabled(void) {
    return cetta_evaluator_options_is_bare_minimal(active_eval_options_const());
}

static int current_eval_fuel_limit(void) {
    return cetta_evaluator_options_effective_fuel_limit(active_eval_options_const());
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
             "Space mork requires explicit %s; generic %s is disabled unless you enable (pragma! mork-space-sugar allow)",
             explicit_surface, surface);
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

    Atom *direct = resolve_registry_refs(a, space_expr);
    if (atom_is_mork_space_handle_value(direct)) {
        return mork_handle_surface_error(a, call, surface, explicit_surface);
    }

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (uint32_t i = 0; i < rs.len; i++) {
        if (atom_is_mork_space_handle_value(rs.items[i])) {
            free(rs.items);
            return mork_handle_surface_error(a, call, surface, explicit_surface);
        }
    }
    free(rs.items);
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

static TableStore *eval_active_episode_table(void) {
    CettaTableMode mode = active_search_table_mode();
    if (mode == CETTA_TABLE_MODE_NONE) {
        if (g_episode_table_ready) {
            table_store_free(&g_episode_table);
            g_episode_table_ready = false;
        }
        return NULL;
    }
    if (!g_episode_table_ready) {
        table_store_init(&g_episode_table, mode);
        g_episode_table_ready = true;
    } else if (g_episode_table.mode != mode) {
        table_store_free(&g_episode_table);
        table_store_init(&g_episode_table, mode);
    }
    return &g_episode_table;
}

static void eval_release_episode_table(void) {
    if (!g_episode_table_ready)
        return;
    table_store_free(&g_episode_table);
    g_episode_table_ready = false;
}

static bool outcome_variant_sharing_enabled(void) {
    const CettaEvalOptionEntry *option = active_eval_option("outcome-variant-sharing");
    if (!option)
        return true;
    if (option->kind == CETTA_EVAL_OPTION_VALUE_INT)
        return option->int_value != 0;
    return strcmp(option->repr, "off") != 0 &&
           strcmp(option->repr, "false") != 0 &&
           strcmp(option->repr, "0") != 0;
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

static TermUniverse *eval_current_term_universe(void) {
    if (g_eval_root_space && g_eval_root_space->universe)
        return g_eval_root_space->universe;
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

static Atom *eval_store_atom(Arena *dst, Atom *src) {
    if (!eval_storage_is_persistent(dst))
        return atom_deep_copy(dst, src);
    return term_universe_store_atom(eval_current_term_universe(), dst, src);
}

static Atom *space_compare_atom(const Space *space, Arena *dst, Atom *src) {
    if (space && space->universe && space->universe->persistent_arena)
        return term_universe_canonicalize_atom(dst, src);
    return atom_deep_copy(dst, src);
}

/* ── Outcome set (unified result type: atom + bindings) ─────────────────── */

void outcome_set_init(OutcomeSet *os) {
    os->items = NULL;
    os->len = 0;
    os->cap = 0;
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
    out->atom = NULL;
    out->materialized_atom = NULL;
    bindings_init(&out->env);
    variant_instance_init(&out->variant);
}

static void outcome_free_fields(Outcome *out) {
    if (!out)
        return;
    bindings_free(&out->env);
    variant_instance_free(&out->variant);
    out->atom = NULL;
    out->materialized_atom = NULL;
}

static void outcome_move(Outcome *dst, Outcome *src) {
    dst->atom = src->atom;
    dst->materialized_atom = src->materialized_atom;
    bindings_move(&dst->env, &src->env);
    variant_instance_move(&dst->variant, &src->variant);
    src->atom = NULL;
    src->materialized_atom = NULL;
}

static bool atom_contains_vars(const Atom *atom) {
    return atom_has_vars(atom);
}

static Atom *dispatch_native_op(Space *s, Arena *a, Atom *head, Atom **args, uint32_t nargs);
static Atom *materialize_runtime_token(Space *s, Arena *a, Atom *atom);

static bool atom_eval_is_immediate_value(Atom *atom, int fuel) {
    return fuel == 0 ||
           atom->kind == ATOM_SYMBOL ||
           atom->kind == ATOM_GROUNDED ||
           atom->kind == ATOM_VAR ||
           (atom->kind == ATOM_EXPR && atom->expr.len == 0);
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
        if (prefix && prefix->len != 0)
            arg = bindings_apply((Bindings *)prefix, a, arg);
        Atom *resolved = registry_lookup_atom(arg);
        args[i] = materialize_runtime_token(s, a, resolved ? resolved : arg);
        if (atom_is_empty(args[i]) || atom_is_error(args[i]) ||
            !atom_eval_is_immediate_value(args[i], fuel))
            return NULL;
    }
    return dispatch_native_op(s, a, head, args, nargs);
}

static void outcome_refresh_materialized_fast_path(Outcome *out) {
    if (!out)
        return;
    if (!variant_instance_present(&out->variant) &&
        (out->env.len == 0 || !atom_contains_vars(out->atom))) {
        out->materialized_atom = out->atom;
    } else {
        out->materialized_atom = NULL;
    }
}

static void outcome_try_factor_variant(Outcome *out) {
    if (!out || variant_instance_present(&out->variant) ||
        !out->atom || !atom_contains_vars(out->atom))
        return;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_FACTOR_ATTEMPT);
    VariantBank *bank = eval_active_outcome_variant_bank();
    if (!bank)
        return;
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
    } else if (out->env.len > 0 && atom_contains_vars(materialized)) {
        materialized = bindings_apply(&out->env, a, materialized);
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
    if (!variant_instance_present(&out->variant))
        return out->atom;
    return variant_instance_materialize(a, out->atom, &out->variant);
}

static bool symbol_id_is_builtin_surface(SymbolId id) {
    return id != SYMBOL_ID_NONE && id <= g_builtin_syms.native_handle;
}

static bool outcome_skip_call_observation_fast_path(Space *s,
                                                    const Outcome *out) {
    Atom *head;
    if (!s || !out || !out->atom || out->atom->kind != ATOM_EXPR ||
        out->atom->expr.len == 0) {
        return false;
    }
    head = out->atom->expr.elems[0];
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    if (is_grounded_op(head->sym_id))
        return false;
    if (g_library_context && g_library_context->foreign_runtime &&
        cetta_foreign_is_callable_atom(head)) {
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
    outcome_init(slot);
    slot->atom = src->atom;
    slot->materialized_atom = src->materialized_atom;
    bindings_assert_no_private_variant_slots(&src->env);
    if (!bindings_clone(&slot->env, &src->env) ||
        !variant_instance_clone(&slot->variant, &src->variant)) {
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

static void outcome_set_add_existing_with_env(OutcomeSet *os,
                                              const Outcome *src,
                                              const Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = src->atom;
    bindings_assert_no_private_variant_slots(&src->env);
    bindings_assert_no_private_variant_slots(env);
    if (!bindings_clone(&slot->env, env) ||
        !variant_instance_clone(&slot->variant, &src->variant)) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
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

static Atom *outcome_materialize_with_outer_env(Arena *a, const Outcome *src,
                                                const Bindings *outer_env) {
    Atom *materialized = src->atom;
    bindings_assert_no_private_variant_slots(&src->env);
    bindings_assert_no_private_variant_slots(outer_env);
    if (variant_instance_present(&src->variant))
        return NULL;
    if ((outer_env && outer_env->len > 0) || src->env.len > 0) {
        if (outer_env && outer_env->len > 0 && src->env.len > 0) {
            Bindings merged;
            if (!bindings_clone_merge(&merged, outer_env, &src->env))
                return NULL;
            materialized = bindings_apply(&merged, a, materialized);
            bindings_free(&merged);
        } else {
            Bindings *single = (Bindings *)((outer_env && outer_env->len > 0)
                ? outer_env
                : &src->env);
            materialized = bindings_apply(single, a, materialized);
        }
    }
    return materialized;
}

static bool outcome_set_add_compacted_variant(Arena *a, OutcomeSet *os,
                                              const Outcome *src,
                                              const Bindings *outer_env) {
    Outcome *slot;
    Bindings merged;
    Bindings *effective = NULL;

    if (!variant_instance_present(&src->variant))
        return false;

    bindings_init(&merged);
    if ((outer_env && (outer_env->len > 0 || outer_env->eq_len > 0)) &&
        (src->env.len > 0 || src->env.eq_len > 0)) {
        if (!bindings_clone_merge(&merged, outer_env, &src->env)) {
            bindings_free(&merged);
            return false;
        }
        effective = &merged;
    } else if (outer_env && (outer_env->len > 0 || outer_env->eq_len > 0)) {
        effective = (Bindings *)outer_env;
    } else if (src->env.len > 0 || src->env.eq_len > 0) {
        effective = (Bindings *)&src->env;
    }

    slot = outcome_set_push_slot(os);
    outcome_init(slot);
    slot->atom = src->atom;
    if (effective) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_PREFIX_COMPACT);
        if (!variant_instance_sink_env(a, &slot->variant, &src->variant, effective)) {
            bindings_free(&merged);
            outcome_free_fields(slot);
            os->len--;
            return false;
        }
    } else if (!variant_instance_clone(&slot->variant, &src->variant)) {
        bindings_free(&merged);
        outcome_free_fields(slot);
        os->len--;
        return false;
    }
    outcome_refresh_materialized_fast_path(slot);
    bindings_free(&merged);
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
        outcome_set_add(os, applied, &empty);
        return;
    }
    if (!outer_env || outer_env->len == 0) {
        outcome_set_add_existing(os, src);
        return;
    }
    Bindings merged;
    if (!bindings_clone_merge(&merged, outer_env, &src->env))
        return;
    outcome_set_add_existing_with_env(os, src, &merged);
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

void outcome_set_free(OutcomeSet *os) {
    for (uint32_t i = 0; i < os->len; i++)
        outcome_free_fields(&os->items[i]);
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

static Atom *dispatch_native_op(Space *s, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    const char *head_name = head ? atom_name_cstr(head) : NULL;
    if (head && head_name && active_profile() &&
        !cetta_profile_allows_surface(active_profile(), head_name)) {
        return profile_surface_error(a, make_call_expr(a, head, args, nargs), head_name);
    }
    if (head && atom_is_symbol_id(head, g_builtin_syms.size) &&
        nargs == 1) {
        Atom *call = make_call_expr(a, head, args, nargs);
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
        ? bindings_apply(&reduced, a, value)
        : bindings_apply(full, a, value);
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
    Atom *resolved = bindings_apply((Bindings *)full, a, slot_var);
    if (resolved != slot_var)
        return resolved;

    uint32_t wanted_base = var_base_id(wanted->var_id);
    if (wanted_base == 0)
        return slot_var;
    for (uint32_t i = full->len; i > 0; i--) {
        const Binding *entry = &full->entries[i - 1];
        if (var_base_id(entry->var_id) != wanted_base)
            continue;
        if (entry->spelling != wanted->spelling)
            continue;
        if (!atom_contains_vars(entry->val))
            return entry->val;
        return bindings_apply_without_self((Bindings *)full, a, entry->var_id,
                                           entry->val);
    }
    return slot_var;
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
    for (uint32_t i = 0; i < full->len; i++) {
        bool used = false;
        for (uint32_t j = 0; j < nbody; j++) {
            if (body_ids[j] == full->entries[i].var_id ||
                body_spellings[j] == full->entries[i].spelling) {
                used = true;
                break;
            }
        }
        if (!used)
            continue;
        Atom *val = full->entries[i].val;
        Atom *projected = atom_contains_vars(val)
            ? bindings_apply_without_self((Bindings *)full, a,
                                          full->entries[i].var_id, val)
            : val;
        if (!bindings_add_id(out, full->entries[i].var_id,
                             full->entries[i].spelling, projected)) {
            bindings_free(out);
            return false;
        }
    }
    return true;
}

static bool bindings_builder_merge_commit(BindingsBuilder *dst,
                                          const Bindings *src) {
    if (!bindings_builder_try_merge(dst, src))
        return false;
    bindings_builder_commit(dst);
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
    if (!bindings_clone_merge(&attempt->owned, base, extra))
        return false;
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

static void query_equations_cache_store(TableStore *table, Space *s,
                                        uint64_t revision, Atom *query,
                                        const QueryResults *results) {
    if (!table)
        return;
    TableQueryHandle handle = {0};
    if (!table_store_begin_query(table, s, revision, query, &handle))
        return;
    for (uint32_t i = 0; i < results->len; i++) {
        if (!table_store_add_answer(&handle, results->items[i].result,
                                    &results->items[i].bindings)) {
            table_store_abort_query(&handle);
            return;
        }
    }
    if (!table_store_commit_query(&handle))
        table_store_abort_query(&handle);
}

static void query_equations_cached(Space *s, Atom *query, Arena *a,
                                   QueryResults *out) {
    TableStore *table = eval_active_episode_table();
    if (!table) {
        query_equations(s, query, a, out);
        return;
    }
    uint64_t revision = space_revision(s);
    if (table_store_lookup(table, s, revision, query, a, out))
        return;
    query_equations(s, query, a, out);
    query_equations_cache_store(table, s, revision, query, out);
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
} QueryEvalVisitorCtx;

static void eval_delayed_outcome_for_caller(Space *s, Arena *a,
                                            Atom *declared_type,
                                            Outcome *seed, int fuel,
                                            bool preserve_bindings,
                                            OutcomeSet *outcomes) {
    Atom *head = NULL;
    if (!seed || !seed->atom)
        return;

    if (atom_is_empty(seed->atom) || outcome_atom_is_error(a, seed) ||
        seed->atom->kind != ATOM_EXPR || seed->atom->expr.len == 0) {
        outcome_set_add_prefixed_outcome(a, outcomes, seed, NULL, preserve_bindings);
        return;
    }
    head = seed->atom->expr.elems[0];
    if (head && head->kind == ATOM_SYMBOL &&
        !symbol_id_is_builtin_surface(head->sym_id) &&
        !is_grounded_op(head->sym_id) &&
        !(g_library_context && g_library_context->foreign_runtime &&
          cetta_foreign_is_callable_atom(head)) &&
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

    eval_delayed_outcome_for_caller(
        query_eval->space,
        query_eval->arena,
        result_eval_type_hint(query_eval->declared_type, result),
        &delayed,
        query_eval->fuel,
        query_eval->preserve_bindings,
        query_eval->outcomes);
    outcome_free_fields(&delayed);
    bindings_merge_attempt_finish(search_context_builder(query_eval->context),
                                  &attempt);
    return true;
}

static bool query_visit_eval_for_caller(Atom *result, const Bindings *bindings,
                                        void *ctx) {
    return query_visit_eval_for_caller_common(result, bindings, NULL, ctx);
}

static bool query_visit_eval_for_caller_delayed(Atom *result,
                                                const Bindings *bindings,
                                                const VariantInstance *variant,
                                                void *ctx) {
    return query_visit_eval_for_caller_common(result, bindings, variant, ctx);
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
    bool ok;
} QuerySingleTailDelayedCtx;

static bool query_delayed_result_apply_single_tail(SearchContext *context,
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
    if (!bindings_builder_merge_or_clone(search_context_builder(context),
                                         base_env,
                                         bindings,
                                         &attempt)) {
        return false;
    }
    variant_applied =
        (variant && variant_instance_present(variant))
            ? variant_instance_materialize(a, result, variant)
            : result;
    if (!variant_applied) {
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    *tail_next = variant_applied;
    *tail_type = result_eval_type_hint(declared_type, result);
    bindings_copy(tail_env, attempt.env);
    bindings_merge_attempt_finish(search_context_builder(context), &attempt);
    return true;
}

static bool query_visit_collect_single_tail_delayed(Atom *result,
                                                    const Bindings *bindings,
                                                    const VariantInstance *variant,
                                                    void *ctx) {
    QuerySingleTailDelayedCtx *collect = ctx;
    if (!collect->ok || *collect->tail_next != NULL)
        return true;
    collect->ok = query_delayed_result_apply_single_tail(
        collect->context,
        collect->base_env,
        collect->arena,
        collect->declared_type,
        result,
        bindings,
        variant,
        collect->tail_next,
        collect->tail_type,
        collect->tail_env);
    return true;
}

static bool query_equations_table_hit_visit(Space *s, Atom *query, Arena *a,
                                            QueryEvalVisitorCtx *query_eval,
                                            uint32_t *visited_out) {
    TableStore *table = eval_active_episode_table();
    if (!table)
        return false;
    return table_store_lookup_visit_delayed(table, s, space_revision(s), query, a,
                                            query_visit_eval_for_caller_delayed,
                                            query_eval, visited_out);
}

static QueryTableTailState
query_equations_table_hit_single_tail(Space *s, Atom *query, Arena *a,
                                      QueryEvalVisitorCtx *query_eval,
                                      Atom **tail_next,
                                      Atom **tail_type,
                                      Bindings *tail_env) {
    TableStore *table = eval_active_episode_table();
    uint32_t visited = 0;
    QuerySingleTailDelayedCtx collect = {
        .context = query_eval->context,
        .base_env = query_eval->base_env,
        .arena = a,
        .declared_type = query_eval->declared_type,
        .tail_next = tail_next,
        .tail_type = tail_type,
        .tail_env = tail_env,
        .ok = true,
    };

    if (!table)
        return QUERY_TABLE_TAIL_MISS;
    if (!table_store_lookup_visit_delayed(table, s, space_revision(s), query, a,
                                          query_visit_collect_single_tail_delayed,
                                          &collect, &visited)) {
        return QUERY_TABLE_TAIL_MISS;
    }
    if (visited == 0)
        return QUERY_TABLE_TAIL_EMPTY;
    if (visited == 1 && collect.ok && *tail_next != NULL)
        return QUERY_TABLE_TAIL_SINGLE;
    return QUERY_TABLE_TAIL_MULTI;
}

static uint32_t query_equations_cached_visit(Space *s, Atom *query, Arena *a,
                                             QueryEvalVisitorCtx *query_eval) {
    uint32_t visited = 0;
    if (query_equations_table_hit_visit(s, query, a, query_eval, &visited))
        return visited;
    QueryResults results;
    query_results_init(&results);
    query_equations(s, query, a, &results);
    visited = query_results_visit(&results, query_visit_eval_for_caller, query_eval);
    TableStore *table = eval_active_episode_table();
    uint64_t revision = space_revision(s);
    query_equations_cache_store(table, s, revision, query, &results);
    query_results_free(&results);
    return visited;
}

static bool query_result_apply_single_tail(SearchContext *context,
                                           const Bindings *base_env,
                                           Arena *a,
                                           Atom *declared_type,
                                           const QueryResult *result,
                                           Atom **tail_next,
                                           Atom **tail_type,
                                           Bindings *tail_env) {
    BindingsMergeAttempt attempt;
    (void)a;
    if (!bindings_builder_merge_or_clone(search_context_builder(context),
                                         base_env,
                                         &result->bindings,
                                         &attempt)) {
        return false;
    }
    *tail_next = result->result;
    *tail_type = result_eval_type_hint(declared_type, result->result);
    bindings_copy(tail_env, attempt.env);
    bindings_merge_attempt_finish(search_context_builder(context), &attempt);
    return true;
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
    Atom *row = bindings_apply((Bindings *)bindings, mork->arena, mork->templ);
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
    if (atom_is_empty(atom) || atom_is_error(atom) || atom_eval_is_immediate_value(atom, fuel)) {
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
    int fuel;
    const Bindings *outer_env;
    bool preserve_bindings;
    bool body_closed;
    OutcomeSet *os;
    ResultSet errors;
    bool has_success;
} LetDirectVisitCtx;

static bool let_direct_branch_visit(Arena *a, Atom *atom, const Bindings *env, void *ctx) {
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
        eval_for_current_caller(let_ctx->s, let_ctx->a, NULL, let_ctx->body,
                                let_ctx->fuel, &empty, let_ctx->outer_env,
                                false, let_ctx->os);
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
        eval_for_current_caller(let_ctx->s, let_ctx->a, NULL, let_ctx->body,
                                let_ctx->fuel, bb, let_ctx->outer_env,
                                let_ctx->preserve_bindings, let_ctx->os);
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
    if (a->hashcons)
        arena_set_hashcons(&ctx.scratch, a->hashcons);
    Atom *applied_stream = (!current_env || current_env->len == 0)
        ? stream_source
        : bindings_apply((Bindings *)current_env, a, stream_source);
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
    space_init_with_universe(clone, src ? src->universe : NULL);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (uint32_t i = 0; i < space_length(src); i++) {
        Atom *stored = space_store_atom(clone, dst, space_get_at(src, i));
        space_add(clone, stored);
    }
    return clone;
}

void eval_release_temporary_spaces(void) {
    eval_release_episode_table();
    eval_release_outcome_variant_bank();
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

    dst->atom_ids = cetta_malloc(sizeof(AtomId) * n);
    dst->cap = n;
    dst->len = n;
    dst->start = 0;
    for (uint32_t i = 0; i < n; i++) {
        dst->atom_ids[i] = space_get_atom_id_at(src, i);
    }

    /* The clone starts with no rebuilt indexes; they must be derived from the
       frozen logical view on first use. */
    dst->eq_idx_dirty = true;
    dst->ty_idx_dirty = true;
    dst->exact_idx_dirty = true;
}

static Space *space_snapshot_clone(Space *src, Arena *a) {
    if (!space_match_backend_materialize_attached(
            src, eval_storage_arena(a)))
        return NULL;
    Space *clone = cetta_malloc(sizeof(Space));
    space_init_with_universe(clone, src ? src->universe : NULL);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    space_snapshot_copy_logical_view(clone, src);
    /* Backend indexes are rebuilt lazily on first match against the snapshot;
       the clone only freezes the logical atom sequence. */
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
        Atom *fresh_ft = rename_vars(&scratch, ft, fresh_var_suffix());
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
                    Atom *arg_term = bindings_apply((Bindings *)search_context_bindings(&trial_context),
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
                normalize_type_expr_profiled(&scratch, bindings_apply(&tb, &scratch, fresh_ret));
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
            SearchContext candidate_context;
            if (!search_context_init(&candidate_context, &results[r], NULL)) {
                free(atypes);
                for (uint32_t rr = 0; rr < 64; rr++) {
                    bindings_free(&results[rr]);
                    bindings_free(&next[rr]);
                }
                return false;
            }
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
        SearchContext ret_context;
        search_context_init_owned(&ret_context, &results[r], NULL);
        Atom *inst_ret =
            eval_dependent_telescope_enabled()
                ? bindings_apply((Bindings *)search_context_bindings(&ret_context), a, retType)
                : retType;
        if (match_types_builder(expectedType, inst_ret,
                                search_context_builder(&ret_context))) {
            if (ret_ok < 64) {
                search_context_take(&ret_context, &success_bindings[ret_ok]);
                ret_ok++;
            }
        } else {
            Atom *reason = atom_expr3(a, atom_symbol(a, "BadType"),
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
    metta_eval_bind_typed(s, a, type, atom, fuel, &inner);
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
        outcome_set_add_existing_with_env(os, &inner.items[i], attempt.env);
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

static __attribute__((unused)) bool project_match_visible_bindings(Arena *a,
                                           const MatchVisibleVarSet *visible,
                                           const Bindings *full,
                                           Bindings *projected) {
    bindings_init(projected);
    if (!full || visible->len == 0)
        return true;

    for (uint32_t i = 0; i < visible->len; i++) {
        Atom *var = atom_var_with_spelling(a, visible->items[i].spelling,
                                           visible->items[i].var_id);
        Atom *resolved = bindings_apply((Bindings *)full, a, var);
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
        Atom *lhs = bindings_apply((Bindings *)full, a, full->constraints[i].lhs);
        Atom *rhs = bindings_apply((Bindings *)full, a, full->constraints[i].rhs);
        if (!atom_refs_only_match_visible_vars(lhs, visible) ||
            !atom_refs_only_match_visible_vars(rhs, visible)) {
            continue;
        }
        if (!bindings_add_constraint(projected, lhs, rhs)) {
            bindings_free(projected);
            return false;
        }
    }
    return true;
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
        if ((outer_env && outer_env->len > 0) || inner->len > 0) {
            if (outer_env && outer_env->len > 0 && inner->len > 0) {
                Bindings merged;
                if (!bindings_clone_merge(&merged, outer_env, inner))
                    return;
                applied = bindings_apply(&merged, a, atom);
                bindings_free(&merged);
            } else {
                Bindings *single = (Bindings *)((outer_env && outer_env->len > 0)
                    ? outer_env
                    : inner);
                applied = bindings_apply(single, a, atom);
            }
        }
        outcome_set_add(os, applied, &empty);
        return;
    }
    if (!outer_env || outer_env->len == 0) {
        outcome_set_add(os, atom, inner);
        return;
    }

    Bindings merged;
    if (!bindings_clone_merge(&merged, outer_env, inner))
        return;
    outcome_set_add_move(os, atom, &merged);
    bindings_free(&merged);
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
        if ((outer_env && outer_env->len > 0) || inner->len > 0) {
            if (outer_env && outer_env->len > 0 && inner->len > 0) {
                Bindings merged;
                if (!bindings_clone_merge(&merged, outer_env, inner))
                    return;
                applied = bindings_apply(&merged, a, atom);
                bindings_free(&merged);
            } else {
                Bindings *single = (Bindings *)((outer_env && outer_env->len > 0)
                    ? outer_env
                    : inner);
                applied = bindings_apply(single, a, atom);
            }
        }
        outcome_set_add(os, applied, &empty);
        return;
    }
    if (!outer_env || outer_env->len == 0) {
        if (local_env) {
            outcome_set_add_move(os, atom, inner);
        } else {
            outcome_set_add(os, atom, &empty);
        }
        return;
    }

    Bindings merged;
    if (!bindings_clone_merge(&merged, outer_env, inner))
        return;
    outcome_set_add_move(os, atom, &merged);
    bindings_free(&merged);
}

static void outcome_set_append_prefixed(Arena *a, OutcomeSet *dst, OutcomeSet *src,
                                        const Bindings *outer_env,
                                        bool preserve_bindings) {
    if (preserve_bindings && outer_env && outer_env->len > 0) {
        BindingsBuilder merged_builder;
        if (!bindings_builder_init(&merged_builder, outer_env))
            return;
        for (uint32_t i = 0; i < src->len; i++) {
            BindingsMergeAttempt attempt;
            if (!bindings_builder_merge_or_clone(&merged_builder, outer_env,
                                                 &src->items[i].env, &attempt))
                continue;
            outcome_set_add_existing_with_env(dst, &src->items[i], attempt.env);
            bindings_merge_attempt_finish(&merged_builder, &attempt);
        }
        bindings_builder_free(&merged_builder);
        return;
    }
    for (uint32_t i = 0; i < src->len; i++) {
        if (preserve_bindings && (!outer_env || outer_env->len == 0)) {
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
    if (preserve_bindings && outer_env && outer_env->len > 0) {
        BindingsBuilder merged_builder;
        if (!bindings_builder_init(&merged_builder, outer_env))
            return;
        for (uint32_t i = 0; i < src->len; i++) {
            BindingsMergeAttempt attempt;
            if (!bindings_builder_merge_or_clone(&merged_builder, outer_env,
                                                 &src->items[i].env, &attempt))
                continue;
            outcome_set_add_existing_with_env(dst, &src->items[i], attempt.env);
            bindings_merge_attempt_finish(&merged_builder, &attempt);
        }
        bindings_builder_free(&merged_builder);
        return;
    }
    for (uint32_t i = 0; i < src->len; i++) {
        if (preserve_bindings && (!outer_env || outer_env->len == 0)) {
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
        : bindings_apply((Bindings *)prefix, a, atom);
    if (atom_eval_is_immediate_value(applied, fuel)) {
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
    bindings_init(owned);
    if (!branch_env || branch_env->len == 0) {
        *effective_outer = outer_env;
        return true;
    }
    if (!outer_env || outer_env->len == 0) {
        *effective_outer = branch_env;
        return true;
    }
    if (!bindings_clone_merge(owned, outer_env, branch_env)) {
        *effective_outer = NULL;
        return false;
    }
    *effective_outer = owned;
    return true;
}

static void branch_outer_env_finish(Bindings *owned,
                                    const Bindings *effective_outer) {
    if (effective_outer == owned)
        bindings_free(owned);
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
    Atom *bound_arg = bindings_apply((Bindings *)env, a, orig_arg);
    if (atom_is_symbol_id(arg_type, g_builtin_syms.atom) || orig_arg->kind == ATOM_VAR) {
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

static __attribute__((noinline)) bool
handle_match(Space *s, Arena *a, Atom *atom, int fuel, bool preserve_bindings,
             OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    uint32_t nargs = expr_nargs(atom);
    if (atom_head_symbol_id(atom) != g_builtin_syms.match || nargs != 3) return false;

    Atom *space_ref = expr_arg(atom, 0);
    Atom *pattern = resolve_registry_refs(a, expr_arg(atom, 1));
    Atom *template = resolve_registry_refs(a, expr_arg(atom, 2));
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
            Atom *result = bindings_apply(&projected, a, template);
            const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
            eval_for_caller(s, a, NULL, result, fuel, forward_env,
                            preserve_bindings, os);
            bindings_free(&projected);
        }
        binding_set_free(&matches);
        match_visible_var_set_free(&visible);
        return true;
    }

    {
        #define MAX_IMPORTED_SAME_SPACE_CHAIN 32
        if (space_engine_uses_pathmap(ms->match_backend.kind)) {
            Atom *same_space_patterns[MAX_IMPORTED_SAME_SPACE_CHAIN];
            uint32_t nsame = 1;
            Atom *residual_body = template;

            same_space_patterns[0] = pattern;
            while (nsame < MAX_IMPORTED_SAME_SPACE_CHAIN &&
                   residual_body->kind == ATOM_EXPR && residual_body->expr.len == 4 &&
                   atom_is_symbol_id(residual_body->expr.elems[0], g_builtin_syms.match)) {
                Atom *inner_ref = resolve_registry_refs(a, residual_body->expr.elems[1]);
                Space *inner_sp = g_registry ? resolve_space(g_registry, inner_ref) : NULL;
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
                    Atom *result = bindings_apply(&projected, a, residual_body);
                    const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                    eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                    preserve_bindings, os);
                    bindings_free(&projected);
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

        bool allow_chain_flatten = !space_engine_uses_pathmap(ms->match_backend.kind);
        if (allow_chain_flatten) {
            Atom *scan = template;
            while (scan->kind == ATOM_EXPR && scan->expr.len == 4 &&
                   atom_is_symbol_id(scan->expr.elems[0], g_builtin_syms.match)) {
                Atom *inner_ref = resolve_registry_refs(a, scan->expr.elems[1]);
                Space *inner_sp = g_registry ? resolve_space(g_registry, inner_ref) : NULL;
                if (!inner_sp) inner_sp = s;
                if (inner_sp && space_engine_uses_pathmap(inner_sp->match_backend.kind)) {
                    /* Imported path still has a nested-match chain regression on the
                       optimized flattening lane. Fall back to ordinary recursive
                       match evaluation so semantics stay correct while we refine
                       the imported chain planner. */
                    allow_chain_flatten = false;
                    break;
                }
                scan = scan->expr.elems[3];
            }
        }

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
                        Bindings projected;
                        if (!project_match_visible_bindings(a, &visible, &mb, &projected)) {
                            bindings_free(&mb);
                            continue;
                        }
                        Atom *result = bindings_apply(&projected, a, body);
                        const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                        eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                        preserve_bindings, os);
                        bindings_free(&projected);
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
                Bindings projected;
                if (!project_match_visible_bindings(a, &visible, &cur_binds[bi],
                                                    &projected)) {
                    continue;
                }
                Atom *result = bindings_apply(&projected, a, body);
                const Bindings *forward_env = preserve_bindings ? &projected : &_empty;
                eval_for_caller(s, a, NULL, result, fuel, forward_env,
                                preserve_bindings, os);
                bindings_free(&projected);
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
    Atom *row = bindings_apply((Bindings *)bindings, emit->arena, emit->templ);
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
                bool preserve_bindings, Atom **tail_next, Atom **tail_type,
                Bindings *tail_env,
                OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    *tail_next = NULL;
    *tail_type = NULL;
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
                    Atom *head_atom = outcome_atom_materialize(a, &heads.items[hi]);
                    Bindings *head_env = &heads.items[hi].env;
                    if (atom_is_empty_or_error(head_atom) && !atom_eq(head_atom, op)) {
                        outcome_set_add_existing_move(&func_results, &heads.items[hi]);
                        continue;
                    }

                    uint32_t expr_narg = atom->expr.len - 1;
                    OutcomeSet call_terms;
                    outcome_set_init(&call_terms);
                    Atom **prefix = arena_alloc(a, sizeof(Atom *) * expr_narg);
                    interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                            expr_narg, 0, prefix, head_env, fuel, &call_terms);

                    for (uint32_t ci = 0; ci < call_terms.len; ci++) {
                        Atom *call_atom = outcome_atom_materialize(a, &call_terms.items[ci]);
                        Bindings *combo_ctx = &call_terms.items[ci].env;
                        Atom *inst_ret_type =
                            eval_dependent_telescope_enabled()
                                ? bindings_apply(combo_ctx, a, ret_type)
                                : bindings_apply(head_env, a, ret_type);

                        bool dispatched = false;
                        if (!dispatched &&
                            call_atom->kind == ATOM_EXPR && call_atom->expr.len == 1) {
                            Atom *h = call_atom->expr.elems[0];
                            if (dispatch_foreign_outcomes(
                                    s, a, h,
                                    call_atom->expr.elems + 1, 0,
                                    inst_ret_type, fuel, combo_ctx,
                                    only_function_types &&
                                        n_op_types == 1 && heads.len == 1 &&
                                        call_terms.len == 1,
                                    preserve_bindings,
                                    tail_next, tail_type, tail_env,
                                    &func_results)) {
                                if (only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && *tail_next) {
                                    outcome_set_free(&call_terms);
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
                                    fuel, combo_ctx, preserve_bindings,
                                    &func_results);
                                dispatched = true;
                            } else if (dispatch_foreign_outcomes(
                                           s, a, h,
                                           call_atom->expr.elems + 1, call_atom->expr.len - 1,
                                           inst_ret_type, fuel, combo_ctx,
                                           only_function_types &&
                                               n_op_types == 1 && heads.len == 1 &&
                                               call_terms.len == 1,
                                           preserve_bindings,
                                           tail_next, tail_type, tail_env,
                                           &func_results)) {
                                if (only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && *tail_next) {
                                    outcome_set_free(&call_terms);
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
                                        *tail_next = gr;
                                        *tail_type = inst_ret_type;
                                        bindings_copy(tail_env, combo_ctx);
                                        outcome_set_free(&call_terms);
                                        outcome_set_free(&heads);
                                        outcome_set_free(&func_results);
                                        for (uint32_t sj = 0; sj < n_succs; sj++)
                                            bindings_free(&succs[sj]);
                                        free(op_types);
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
                            QueryEvalVisitorCtx query_eval = {
                                .space = s,
                                .arena = a,
                                .declared_type = inst_ret_type,
                                .fuel = fuel,
                                .base_env = combo_ctx,
                                .preserve_bindings = preserve_bindings,
                                .context = &qr_context,
                                .outcomes = &func_results,
                            };
                            QueryTableTailState table_tail =
                                query_equations_table_hit_single_tail(
                                    s, call_atom, a, &query_eval,
                                    tail_next, tail_type, tail_env);
                            if (table_tail != QUERY_TABLE_TAIL_MISS) {
                                search_context_free(&qr_context);
                                if (table_tail == QUERY_TABLE_TAIL_SINGLE) {
                                    outcome_set_free(&call_terms);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    return true;
                                }
                                if (table_tail == QUERY_TABLE_TAIL_MULTI) {
                                    query_equations_table_hit_visit(s, call_atom, a,
                                                                    &query_eval, NULL);
                                    dispatched = true;
                                }
                                goto query_done;
                            }
                            QueryResults qr;
                            query_results_init(&qr);
                            query_equations_cached(s, call_atom, a, &qr);
                            if (qr.len > 0) {
                                if (only_function_types &&
                                    n_op_types == 1 && heads.len == 1 &&
                                    call_terms.len == 1 && qr.len == 1) {
                                    if (!query_result_apply_single_tail(&qr_context,
                                                                        combo_ctx,
                                                                        a,
                                                                        inst_ret_type,
                                                                        &qr.items[0],
                                                                        tail_next,
                                                                        tail_type,
                                                                        tail_env)) {
                                        search_context_free(&qr_context);
                                        query_results_free(&qr);
                                        continue;
                                    }
                                    search_context_free(&qr_context);
                                    query_results_free(&qr);
                                    outcome_set_free(&call_terms);
                                    outcome_set_free(&heads);
                                    outcome_set_free(&func_results);
                                    for (uint32_t sj = 0; sj < n_succs; sj++)
                                        bindings_free(&succs[sj]);
                                    free(op_types);
                                    return true;
                                }
                                query_results_visit(&qr, query_visit_eval_for_caller,
                                                    &query_eval);
                                search_context_free(&qr_context);
                                dispatched = true;
                            } else {
                                search_context_free(&qr_context);
                            }
                            query_results_free(&qr);
                        }
query_done:
                        if (!dispatched)
                            outcome_set_add_existing_move(&func_results, &call_terms.items[ci]);
                    }
                    outcome_set_free(&call_terms);
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
            outcome_set_add_existing(os, &func_results.items[i]);
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
                if (h->kind == ATOM_SYMBOL &&
                    is_grounded_op(h->sym_id)) {
                    Atom *result = dispatch_native_op(s, a, h,
                        call_atom->expr.elems + 1, call_atom->expr.len - 1);
                    if (result) {
                        *tail_next = result;
                        *tail_type = etype;
                        bindings_copy(tail_env, tuple_bindings);
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
            QueryEvalVisitorCtx query_eval = {
                .space = s,
                .arena = a,
                .declared_type = NULL,
                .fuel = fuel,
                .base_env = tuple_bindings,
                .preserve_bindings = preserve_bindings,
                .context = &qr_context,
                .outcomes = os,
            };
            QueryTableTailState table_tail =
                query_equations_table_hit_single_tail(
                    s, call_atom, a, &query_eval,
                    tail_next, tail_type, tail_env);
            if (table_tail != QUERY_TABLE_TAIL_MISS) {
                search_context_free(&qr_context);
                if (table_tail == QUERY_TABLE_TAIL_SINGLE) {
                    outcome_set_free(&tuples);
                    return true;
                }
                if (table_tail == QUERY_TABLE_TAIL_MULTI) {
                    query_equations_table_hit_visit(s, call_atom, a, &query_eval,
                                                    NULL);
                    outcome_set_free(&tuples);
                    return true;
                }
                outcome_set_add_existing_move(os, &tuples.items[0]);
                outcome_set_free(&tuples);
                return true;
            }
            QueryResults qr;
            query_results_init(&qr);
            query_equations_cached(s, call_atom, a, &qr);
            if (qr.len == 1) {
                if (!query_result_apply_single_tail(&qr_context,
                                                    tuple_bindings,
                                                    a,
                                                    etype,
                                                    &qr.items[0],
                                                    tail_next,
                                                    tail_type,
                                                    tail_env)) {
                    search_context_free(&qr_context);
                    query_results_free(&qr);
                    outcome_set_free(&tuples);
                    return true;
                }
                search_context_free(&qr_context);
                query_results_free(&qr);
                outcome_set_free(&tuples);
                return true;
            }
            if (qr.len > 0) {
                query_results_visit(&qr, query_visit_eval_for_caller, &query_eval);
                search_context_free(&qr_context);
                query_results_free(&qr);
                outcome_set_free(&tuples);
                return true;
            }
            search_context_free(&qr_context);
            query_results_free(&qr);
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
                if (h->kind == ATOM_SYMBOL &&
                    is_grounded_op(h->sym_id)) {
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
            };
            if (query_equations_cached_visit(s, call_atom, a, &query_eval) > 0) {
                search_context_free(&qr_context);
                continue;
            }
            search_context_free(&qr_context);

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
    __attribute__((cleanup(bindings_builder_free))) BindingsBuilder current_env_builder;
    if (!bindings_builder_init(&current_env_builder, NULL))
        return;
    if (!etype) etype = atom_undefined_type(a);
#define CURRENT_ENV bindings_builder_bindings(&current_env_builder)
#define TAIL_REENTER_ENV(next_atom, extra_env) do { \
    if ((extra_env) != NULL && \
        !bindings_builder_merge_commit(&current_env_builder, (extra_env))) return; \
    atom = resolve_registry_refs(a, (next_atom)); \
    if (fuel == 0) return; \
    if (fuel > 0) fuel--; \
    goto tail_call; \
} while (0)
#define TAIL_REENTER(next_atom) TAIL_REENTER_ENV((next_atom), NULL)
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
                                        CURRENT_ENV, preserve_bindings, os);
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
    if (handle_match(s, a, atom, fuel, preserve_bindings, &match_results)) {
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
        Bindings b;
        bindings_init(&b);
        if (match_atoms(target, pattern, &b) && !bindings_has_loop(&b)) {
            Atom *next_atom = bindings_apply(&b, a, then_br);
            if (preserve_bindings &&
                !bindings_builder_merge_commit(&current_env_builder, &b)) {
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
                    BindingsBuilder b;
                    if (!bindings_builder_init(&b, NULL)) {
                        free(scrut.items);
                        return;
                    }
                    if (simple_match_builder(branch->expr.elems[0], sv, &b)) {
                        const Bindings *bb = bindings_builder_bindings(&b);
                        Atom *next_atom =
                            bindings_apply((Bindings *)bb, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_builder_merge_commit(&current_env_builder, bb)) {
                            bindings_builder_free(&b);
                            free(scrut.items);
                            return;
                        }
                        bindings_builder_free(&b);
                        free(scrut.items);
                        TAIL_REENTER(next_atom);
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
                                bindings_apply((Bindings *)bb, a, branch->expr.elems[1]);
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
                            bindings_apply((Bindings *)bb, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_builder_merge_commit(&current_env_builder, bb)) {
                            bindings_builder_free(&b);
                            return;
                        }
                        bindings_builder_free(&b);
                        TAIL_REENTER(next_atom);
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
        OutcomeSet vals;
        bool body_let_closed = !atom_contains_vars(body_let);
        if (!preserve_bindings) {
            Atom *applied_val_expr = (!CURRENT_ENV || CURRENT_ENV->len == 0)
                ? val_expr
                : bindings_apply((Bindings *)CURRENT_ENV, a, val_expr);
            if (direct_outcome_walk_supported(s, a, applied_val_expr, fuel)) {
                LetDirectVisitCtx visit = {
                    .s = s,
                    .a = a,
                    .pat = pat,
                    .body = body_let,
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
        }
        outcome_set_init(&vals);
        metta_eval_bind(s, a, val_expr, fuel, &vals);
        bool all_errors = vals.len > 0;
        for (uint32_t i = 0; i < vals.len; i++) {
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
            Atom *val_atom =
                outcome_atom_materialize_traced(
                    a, &vals.items[0],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            const Bindings *val_env = &vals.items[0].env;
            if (pat->kind == ATOM_VAR) {
                BindingsBuilder b;
                if (!bindings_builder_init(&b, val_env)) {
                    outcome_set_free(&vals);
                    return;
                }
                ok = bindings_builder_add_var_fresh(&b, pat, val_atom);
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
                    Atom *next_atom = bindings_apply(&visible, a, body_let);
                    if (preserve_bindings &&
                        !bindings_builder_merge_commit(&current_env_builder, bb)) {
                        bindings_free(&visible);
                        bindings_builder_free(&b);
                        return;
                    }
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    TAIL_REENTER(next_atom);
                }
                bindings_builder_free(&b);
                return;
            } else {
                BindingsBuilder b;
                if (!bindings_builder_init(&b, val_env)) {
                    outcome_set_free(&vals);
                    return;
                }
                ok = simple_match_builder(pat, val_atom, &b);
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
                    Atom *next_atom = bindings_apply(&visible, a, body_let);
                    if (preserve_bindings &&
                        !bindings_builder_merge_commit(&current_env_builder, bb)) {
                        bindings_free(&visible);
                        bindings_builder_free(&b);
                        return;
                    }
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    TAIL_REENTER(next_atom);
                }
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
                BindingsBuilder b;
                if (!bindings_builder_init(&b, val_env))
                        continue;
                if (!bindings_builder_add_var_fresh(&b, pat, val_atom)) {
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
                if (preserve_bindings &&
                    !branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                            CURRENT_ENV, bb)) {
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    continue;
                }
                eval_for_current_caller(s, a, NULL,
                                        bindings_apply(&visible, a, body_let), fuel, &_empty,
                                        branch_outer, preserve_bindings, os);
                branch_outer_env_finish(&branch_outer_owned, branch_outer);
                bindings_free(&visible);
                bindings_builder_free(&b);
            } else {
                BindingsBuilder b;
                if (!bindings_builder_init(&b, val_env))
                    continue;
                if (simple_match_builder(pat, val_atom, &b)) {
                    const Bindings *bb = bindings_builder_bindings(&b);
                    Bindings visible;
                    if (!bindings_project_body_visible_env(a, body_let, bb, &visible)) {
                        bindings_builder_free(&b);
                        continue;
                    }
                    Bindings branch_outer_owned;
                    const Bindings *branch_outer = CURRENT_ENV;
                    if (preserve_bindings &&
                        !branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                                CURRENT_ENV, bb)) {
                        bindings_free(&visible);
                        bindings_builder_free(&b);
                        continue;
                    }
                    eval_for_current_caller(s, a, NULL,
                                            bindings_apply(&visible, a, body_let),
                                            fuel, &_empty,
                                            branch_outer, preserve_bindings, os);
                    branch_outer_env_finish(&branch_outer_owned, branch_outer);
                    bindings_free(&visible);
                }
                bindings_builder_free(&b);
            }
        }
        outcome_set_free(&vals);
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
        if (var->kind != ATOM_VAR) {
            outcome_set_add(os, call_signature_error(a, atom,
                "(chain <nested> (: <var> Variable) <templ>)"), &_empty);
            return;
        }
        OutcomeSet inner;
        outcome_set_init(&inner);
        metta_eval_bind(s, a, to_eval, fuel, &inner);
        if (inner.len == 0) {
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
            Atom *next_atom;
            bool has_binding = false;
            {
                Atom *inner_atom =
                    outcome_atom_materialize_traced(
                        a, &inner.items[0],
                        CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
                const Bindings *inner_env = &inner.items[0].env;
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
                if (!bindings_project_body_visible_env(a, tmpl_chain, bb, &visible)) {
                    bindings_builder_free(&b);
                    outcome_set_free(&inner);
                    return;
                }
                next_atom = bindings_apply(&visible, a, tmpl_chain);
                if (preserve_bindings &&
                    !bindings_builder_merge_commit(&current_env_builder, bb)) {
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    outcome_set_free(&inner);
                    return;
                }
                outcome_set_free(&inner);
                bindings_free(&visible);
                bindings_builder_free(&b);
                TAIL_REENTER(next_atom);
            }
            outcome_set_free(&inner);
            TAIL_REENTER(next_atom);
        }
        /* Multi-result: no TCO */
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
                if (!bindings_project_body_visible_env(a, tmpl_chain, bb, &visible)) {
                    bindings_builder_free(&b);
                    continue;
                }
                Atom *subst = bindings_apply(&visible, a, tmpl_chain);
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
        SpaceEngine backend_kind = s->match_backend.kind;
        if (backend_kind == SPACE_ENGINE_MORK) {
            backend_kind = SPACE_ENGINE_PATHMAP;
        }
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
        }
        Arena *pa = eval_storage_arena(a);
        Space *ns = arena_alloc(pa, sizeof(Space));
        space_init_with_universe(ns, eval_current_term_universe());
        ns->kind = kind;
        if (!space_match_backend_try_set(ns, backend_kind)) {
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
        Atom *error = NULL;
        Space *target_space = s;
        const char *spec = NULL;
        if (nargs == 1) {
            spec = string_like_atom(expr_arg(atom, 0));
        } else {
            target_space = resolve_include_destination(a, expr_arg(atom, 0), &error);
            if (!error) {
                spec = string_like_atom(expr_arg(atom, 1));
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
        const char *spec = string_like_atom(expr_arg(atom, 0));
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
        Space *target = resolve_space(g_registry, space_ref);
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
            Atom *next_atom = bindings_apply((Bindings *)bb, a, body);
            if (preserve_bindings &&
                !bindings_builder_merge_commit(&current_env_builder, bb)) {
                bindings_builder_free(&b);
                return;
            }
            bindings_builder_free(&b);
            TAIL_REENTER(next_atom);
        } else {
            BindingsBuilder b;
            if (!bindings_builder_init(&b, NULL))
                return;
            if (simple_match_builder(binder, snapshot_atom, &b)) {
                const Bindings *bb = bindings_builder_bindings(&b);
                Atom *next_atom = bindings_apply((Bindings *)bb, a, body);
                if (preserve_bindings &&
                    !bindings_builder_merge_commit(&current_env_builder, bb)) {
                    bindings_builder_free(&b);
                    return;
                }
                bindings_builder_free(&b);
                TAIL_REENTER(next_atom);
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
        Atom *stored = space_store_atom(target, dst, expr_arg(atom, 1));
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
        Space **targets = NULL;
        uint32_t ntargets = 0;
        collect_resolved_spaces(s, a, expr_arg(atom, 0), fuel, &targets, &ntargets);

        OutcomeSet vals;
        outcome_set_init(&vals);
        metta_eval_bind(s, a, expr_arg(atom, 1), fuel, &vals);

        if (ntargets > 0) {
            Arena *dst = eval_storage_arena(a);
            for (uint32_t ti = 0; ti < ntargets; ti++) {
                for (uint32_t vi = 0; vi < vals.len; vi++) {
                    Atom *stored_val = outcome_atom_materialize(a, &vals.items[vi]);
                    Atom *stored = space_store_atom(targets[ti], dst, stored_val);
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
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "add-atom", "mork:add-atom");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom expects a space as the first argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "add-atom", "mork:add-atom");
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
        /* Deep-copy to persistent arena so atom survives eval_arena reset */
        Arena *dst = eval_storage_arena(a);
        Atom *stored = space_store_atom(target, dst, atom_to_add);
        space_add(target, stored);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-atom-nodup (dedup variant for forward chaining) ────────────── */
    if (head_id == g_builtin_syms.add_atom_nodup && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "add-atom-nodup", "mork:add-atom");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom expects a space as the first argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "add-atom-nodup", "mork:add-atom");
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
        Atom *compare_atom = space_compare_atom(target, a, atom_to_add);
        bool found = space_is_hash(target) ? space_contains_exact(target, compare_atom) : false;
        if (!found) {
            for (uint32_t i = 0; i < space_length(target) && !found; i++)
                if (atom_eq(space_get_at(target, i), compare_atom)) found = true;
        }
        if (!found) {
            Arena *dst = eval_storage_arena(a);
            Atom *stored = space_store_atom(target, dst, atom_to_add);
            space_add(target, stored);
        }
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── remove-atom ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.remove_atom && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_rm = expr_arg(atom, 1);
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "remove-atom", "mork:remove-atom");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "remove-atom expects a space as the first argument"), &_empty);
            return;
        }
        Atom *mork_error = guard_mork_space_surface(
            a, atom, target, "remove-atom", "mork:remove-atom");
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
        Atom *compare_atom = space_compare_atom(target, a, atom_to_rm);
        space_remove(target, compare_atom);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── get-atoms ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.get_atoms && nargs == 1 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
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
        Atom **pairs = arena_alloc(a, sizeof(Atom *) * inner.len);
        for (uint32_t i = 0; i < inner.len; i++) {
            pairs[i] = atom_expr2(a, outcome_atom_materialize(a, &inner.items[i]),
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
            Atom *bound_val_expr = bindings_apply(&refs.items[i].env, a, expr_arg(atom, 1));
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
                        atom_symbol(a, "BadArgType"), atom_int(a, 2),
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
        if (handle_dispatch(s, a, atom, etype, fuel, CURRENT_ENV, preserve_bindings,
                            &tail_next, &tail_type, &tail_env,
                            &dispatch_results)) {
            if (tail_next) {
                if ((tail_env.len != 0 || tail_env.eq_len != 0) &&
                    !bindings_builder_merge_commit(&current_env_builder, &tail_env)) {
                    outcome_set_free(&dispatch_results);
                    return;
                }
                outcome_set_free(&dispatch_results);
                if (tail_type) etype = tail_type;
                TAIL_REENTER(tail_next);
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
#undef TAIL_REENTER
#undef TAIL_REENTER_ENV
#undef CURRENT_ENV


/* ── Top-level evaluation ───────────────────────────────────────────────── */

void eval_top(Space *s, Arena *a, Atom *expr, ResultSet *rs) {
    g_registry = NULL;
    g_eval_root_space = s;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, NULL);
    eval_release_outcome_variant_bank();
    metta_eval(s, a, NULL, expr, current_eval_fuel_limit(), rs);
    eval_release_outcome_variant_bank();
}

void eval_top_with_registry(Space *s, Arena *a, Arena *persistent, Registry *r, Atom *expr, ResultSet *rs) {
    g_registry = r;
    g_eval_root_space = s;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, persistent);
    eval_release_outcome_variant_bank();
    metta_eval(s, a, NULL, expr, current_eval_fuel_limit(), rs);
    eval_release_outcome_variant_bank();
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
