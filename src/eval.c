#define _GNU_SOURCE
#include "eval.h"
#include "match.h"
#include "search_machine.h"
#include "grounded.h"
#include "library.h"
#include "mm2_lower.h"
#include "mork_space_bridge_runtime.h"
#include "parallel_executor.h"
#include "stats.h"
#include "answer_bank.h"
#include "table_store.h"
#include "term_universe.h"
#include "variant_shape.h"
#include "langdef_pack.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/resource.h>
#include <assert.h>
#include "eval_gc.h"

/* Global registry for named spaces/values (set by eval_top_with_registry) */
static __thread Registry *g_registry = NULL;
/* Current evaluation root space and persistent fallback. */
static __thread Space *g_eval_root_space = NULL;
static __thread TermUniverse g_eval_fallback_universe = {0};
/* Query cache for the current logical evaluation episode. */
static __thread TableStore g_episode_table;
static __thread bool g_episode_table_ready = false;
static __thread AnswerBank g_episode_answer_bank;
static __thread bool g_episode_answer_bank_ready = false;
static __thread VariantBank g_episode_outcome_variant_bank;
static __thread bool g_episode_outcome_variant_bank_ready = false;
static __thread Arena g_episode_survivor_arena;
static __thread bool g_episode_survivor_arena_ready = false;
/* Active importable library set */
static __thread CettaLibraryContext *g_library_context = NULL;
static __thread CettaEvalSession g_fallback_eval_session;
static __thread bool g_fallback_eval_session_ready = false;
static __thread _Atomic bool *g_eval_cancel_requested = NULL;
static __thread bool g_eval_cancel_observed = false;
static __thread _Atomic bool *g_hyperpose_thread_unsafe_requested = NULL;
/* When set, the current evaluation is a Rhometta deferred payload running as
 * a sibling-isolated transaction over a frozen shared base. Space and state
 * redirects below provide the write-forward scratch layer. */
static __thread bool g_eval_payload_transactional = false;
static __thread uint64_t g_eval_payload_owner_epoch = 0;
static __thread uint64_t g_eval_payload_owner_epoch_next = 1;
static __thread Arena *g_eval_payload_redirect_owner = NULL;

typedef struct {
    Space **orig;
    Space **redirect;
    CettaCount len, cap;
    Space *inline_orig[16];
    Space *inline_redirect[16];
} EvalPayloadSpaceRedirectMap;

typedef struct {
    StateCell **orig;
    StateCell **redirect;
    CettaCount len, cap;
    StateCell *inline_orig[16];
    StateCell *inline_redirect[16];
} EvalPayloadStateRedirectMap;

static __thread EvalPayloadSpaceRedirectMap g_eval_payload_space_redirects = {0};
static __thread EvalPayloadStateRedirectMap g_eval_payload_state_redirects = {0};

typedef struct {
    Space **items;
    CettaCount len, cap;
} EvalPayloadScratchSpaceSet;

typedef struct {
    StateCell **items;
    CettaCount len, cap;
} EvalPayloadScratchStateSet;

static __thread EvalPayloadScratchSpaceSet g_eval_payload_scratch_spaces = {0};
static __thread EvalPayloadScratchStateSet g_eval_payload_scratch_states = {0};

static bool eval_next_capacity(CettaCount current_cap, CettaCount min_needed,
                               size_t elem_size, CettaCount *out_next);

static void eval_payload_space_redirect_init_if_needed(void) {
    if (g_eval_payload_space_redirects.orig)
        return;
    g_eval_payload_space_redirects.orig =
        g_eval_payload_space_redirects.inline_orig;
    g_eval_payload_space_redirects.redirect =
        g_eval_payload_space_redirects.inline_redirect;
    g_eval_payload_space_redirects.cap =
        (CettaCount)(sizeof(g_eval_payload_space_redirects.inline_orig) /
                     sizeof(g_eval_payload_space_redirects.inline_orig[0]));
}

static void eval_payload_state_redirect_init_if_needed(void) {
    if (g_eval_payload_state_redirects.orig)
        return;
    g_eval_payload_state_redirects.orig =
        g_eval_payload_state_redirects.inline_orig;
    g_eval_payload_state_redirects.redirect =
        g_eval_payload_state_redirects.inline_redirect;
    g_eval_payload_state_redirects.cap =
        (CettaCount)(sizeof(g_eval_payload_state_redirects.inline_orig) /
                     sizeof(g_eval_payload_state_redirects.inline_orig[0]));
}

static bool eval_payload_space_redirect_ensure(CettaCount min_needed) {
    CettaCount next_cap;
    eval_payload_space_redirect_init_if_needed();
    if (g_eval_payload_space_redirects.cap >= min_needed)
        return true;
    if (!eval_next_capacity(g_eval_payload_space_redirects.cap, min_needed,
                            sizeof(Space *), &next_cap))
        return false;
    if (g_eval_payload_space_redirects.orig ==
        g_eval_payload_space_redirects.inline_orig) {
        Space **orig = cetta_malloc(sizeof(Space *) * (size_t)next_cap);
        Space **redirect = cetta_malloc(sizeof(Space *) * (size_t)next_cap);
        if (g_eval_payload_space_redirects.len > 0) {
            memcpy(orig, g_eval_payload_space_redirects.orig,
                   sizeof(Space *) * (size_t)g_eval_payload_space_redirects.len);
            memcpy(redirect, g_eval_payload_space_redirects.redirect,
                   sizeof(Space *) * (size_t)g_eval_payload_space_redirects.len);
        }
        g_eval_payload_space_redirects.orig = orig;
        g_eval_payload_space_redirects.redirect = redirect;
    } else {
        g_eval_payload_space_redirects.orig =
            cetta_realloc(g_eval_payload_space_redirects.orig,
                          sizeof(Space *) * (size_t)next_cap);
        g_eval_payload_space_redirects.redirect =
            cetta_realloc(g_eval_payload_space_redirects.redirect,
                          sizeof(Space *) * (size_t)next_cap);
    }
    g_eval_payload_space_redirects.cap = next_cap;
    return true;
}

static bool eval_payload_state_redirect_ensure(CettaCount min_needed) {
    CettaCount next_cap;
    eval_payload_state_redirect_init_if_needed();
    if (g_eval_payload_state_redirects.cap >= min_needed)
        return true;
    if (!eval_next_capacity(g_eval_payload_state_redirects.cap, min_needed,
                            sizeof(StateCell *), &next_cap))
        return false;
    if (g_eval_payload_state_redirects.orig ==
        g_eval_payload_state_redirects.inline_orig) {
        StateCell **orig =
            cetta_malloc(sizeof(StateCell *) * (size_t)next_cap);
        StateCell **redirect =
            cetta_malloc(sizeof(StateCell *) * (size_t)next_cap);
        if (g_eval_payload_state_redirects.len > 0) {
            memcpy(orig, g_eval_payload_state_redirects.orig,
                   sizeof(StateCell *) *
                       (size_t)g_eval_payload_state_redirects.len);
            memcpy(redirect, g_eval_payload_state_redirects.redirect,
                   sizeof(StateCell *) *
                       (size_t)g_eval_payload_state_redirects.len);
        }
        g_eval_payload_state_redirects.orig = orig;
        g_eval_payload_state_redirects.redirect = redirect;
    } else {
        g_eval_payload_state_redirects.orig =
            cetta_realloc(g_eval_payload_state_redirects.orig,
                          sizeof(StateCell *) * (size_t)next_cap);
        g_eval_payload_state_redirects.redirect =
            cetta_realloc(g_eval_payload_state_redirects.redirect,
                          sizeof(StateCell *) * (size_t)next_cap);
    }
    g_eval_payload_state_redirects.cap = next_cap;
    return true;
}

static bool payload_scratch_space_append(Space *space) {
    if (!space)
        return false;
    for (CettaCount i = 0; i < g_eval_payload_scratch_spaces.len; i++) {
        if (g_eval_payload_scratch_spaces.items[i] == space)
            return true;
    }
    if (g_eval_payload_scratch_spaces.len >= g_eval_payload_scratch_spaces.cap) {
        CettaCount next_cap;
        if (!eval_next_capacity(g_eval_payload_scratch_spaces.cap,
                                g_eval_payload_scratch_spaces.len + 1u,
                                sizeof(Space *), &next_cap)) {
            return false;
        }
        g_eval_payload_scratch_spaces.items =
            cetta_realloc(g_eval_payload_scratch_spaces.items,
                          sizeof(Space *) * (size_t)next_cap);
        g_eval_payload_scratch_spaces.cap = next_cap;
    }
    g_eval_payload_scratch_spaces.items[g_eval_payload_scratch_spaces.len++] = space;
    return true;
}

static bool payload_scratch_state_append(StateCell *cell) {
    if (!cell)
        return false;
    for (CettaCount i = 0; i < g_eval_payload_scratch_states.len; i++) {
        if (g_eval_payload_scratch_states.items[i] == cell)
            return true;
    }
    if (g_eval_payload_scratch_states.len >= g_eval_payload_scratch_states.cap) {
        CettaCount next_cap;
        if (!eval_next_capacity(g_eval_payload_scratch_states.cap,
                                g_eval_payload_scratch_states.len + 1u,
                                sizeof(StateCell *), &next_cap)) {
            return false;
        }
        g_eval_payload_scratch_states.items =
            cetta_realloc(g_eval_payload_scratch_states.items,
                          sizeof(StateCell *) * (size_t)next_cap);
        g_eval_payload_scratch_states.cap = next_cap;
    }
    g_eval_payload_scratch_states.items[g_eval_payload_scratch_states.len++] = cell;
    return true;
}

static void payload_release_scratch(void) {
    for (CettaCount i = 0; i < g_eval_payload_scratch_spaces.len; i++) {
        Space *space = g_eval_payload_scratch_spaces.items[i];
        if (!space)
            continue;
        space_free(space);
        free(space);
    }
    free(g_eval_payload_scratch_spaces.items);
    g_eval_payload_scratch_spaces.items = NULL;
    g_eval_payload_scratch_spaces.len = 0;
    g_eval_payload_scratch_spaces.cap = 0;

    for (CettaCount i = 0; i < g_eval_payload_scratch_states.len; i++) {
        StateCell *cell = g_eval_payload_scratch_states.items[i];
        free(cell);
    }
    free(g_eval_payload_scratch_states.items);
    g_eval_payload_scratch_states.items = NULL;
    g_eval_payload_scratch_states.len = 0;
    g_eval_payload_scratch_states.cap = 0;
}

bool eval_payload_transactional(void) { return g_eval_payload_transactional; }
bool eval_set_payload_transactional(bool v) {
    bool prev = g_eval_payload_transactional;
    g_eval_payload_transactional = v;
    return prev;
}
uint64_t eval_payload_owner_epoch(void) { return g_eval_payload_owner_epoch; }
uint64_t eval_set_payload_owner_epoch(uint64_t epoch) {
    uint64_t prev = g_eval_payload_owner_epoch;
    g_eval_payload_owner_epoch = epoch;
    return prev;
}
uint64_t eval_next_payload_owner_epoch(void) {
    uint64_t next = g_eval_payload_owner_epoch_next++;
    if (next == 0) {
        next = g_eval_payload_owner_epoch_next++;
    }
    return next;
}

bool eval_payload_redirects_begin(Arena *owner) {
    g_eval_payload_space_redirects.len = 0;
    g_eval_payload_state_redirects.len = 0;
    g_eval_payload_redirect_owner = owner;
    return true;
}

void eval_payload_redirects_end(void) {
    payload_release_scratch();
    if (g_eval_payload_space_redirects.orig &&
        g_eval_payload_space_redirects.orig !=
            g_eval_payload_space_redirects.inline_orig)
        free(g_eval_payload_space_redirects.orig);
    if (g_eval_payload_space_redirects.redirect &&
        g_eval_payload_space_redirects.redirect !=
            g_eval_payload_space_redirects.inline_redirect)
        free(g_eval_payload_space_redirects.redirect);
    if (g_eval_payload_state_redirects.orig &&
        g_eval_payload_state_redirects.orig !=
            g_eval_payload_state_redirects.inline_orig)
        free(g_eval_payload_state_redirects.orig);
    if (g_eval_payload_state_redirects.redirect &&
        g_eval_payload_state_redirects.redirect !=
            g_eval_payload_state_redirects.inline_redirect)
        free(g_eval_payload_state_redirects.redirect);
    g_eval_payload_space_redirects.orig =
        g_eval_payload_space_redirects.inline_orig;
    g_eval_payload_space_redirects.redirect =
        g_eval_payload_space_redirects.inline_redirect;
    g_eval_payload_space_redirects.len = 0;
    g_eval_payload_space_redirects.cap =
        (CettaCount)(sizeof(g_eval_payload_space_redirects.inline_orig) /
                     sizeof(g_eval_payload_space_redirects.inline_orig[0]));
    g_eval_payload_state_redirects.orig =
        g_eval_payload_state_redirects.inline_orig;
    g_eval_payload_state_redirects.redirect =
        g_eval_payload_state_redirects.inline_redirect;
    g_eval_payload_state_redirects.len = 0;
    g_eval_payload_state_redirects.cap =
        (CettaCount)(sizeof(g_eval_payload_state_redirects.inline_orig) /
                     sizeof(g_eval_payload_state_redirects.inline_orig[0]));
    g_eval_payload_redirect_owner = NULL;
}

static Space *payload_space_redirect_for(Space *orig) {
    for (CettaCount i = 0; i < g_eval_payload_space_redirects.len; i++) {
        if (g_eval_payload_space_redirects.orig[i] == orig)
            return g_eval_payload_space_redirects.redirect[i];
    }
    return NULL;
}

static StateCell *payload_state_redirect_for(StateCell *orig) {
    for (CettaCount i = 0; i < g_eval_payload_state_redirects.len; i++) {
        if (g_eval_payload_state_redirects.orig[i] == orig)
            return g_eval_payload_state_redirects.redirect[i];
    }
    return NULL;
}

bool eval_payload_note_space_redirect(Space *orig, Space *redirect) {
    if (!orig || !redirect)
        return false;
    for (CettaCount i = 0; i < g_eval_payload_space_redirects.len; i++) {
        if (g_eval_payload_space_redirects.orig[i] == orig) {
            g_eval_payload_space_redirects.redirect[i] = redirect;
            return true;
        }
    }
    if (!eval_payload_space_redirect_ensure(g_eval_payload_space_redirects.len + 1u))
        return false;
    g_eval_payload_space_redirects.orig[g_eval_payload_space_redirects.len] = orig;
    g_eval_payload_space_redirects.redirect[g_eval_payload_space_redirects.len] = redirect;
    g_eval_payload_space_redirects.len++;
    return true;
}

bool eval_payload_note_state_redirect(StateCell *orig, StateCell *redirect) {
    if (!orig || !redirect)
        return false;
    for (CettaCount i = 0; i < g_eval_payload_state_redirects.len; i++) {
        if (g_eval_payload_state_redirects.orig[i] == orig) {
            g_eval_payload_state_redirects.redirect[i] = redirect;
            return true;
        }
    }
    if (!eval_payload_state_redirect_ensure(g_eval_payload_state_redirects.len + 1u))
        return false;
    g_eval_payload_state_redirects.orig[g_eval_payload_state_redirects.len] = orig;
    g_eval_payload_state_redirects.redirect[g_eval_payload_state_redirects.len] = redirect;
    g_eval_payload_state_redirects.len++;
    return true;
}

bool eval_payload_track_scratch_space(Space *space) {
    return payload_scratch_space_append(space);
}

bool eval_payload_track_scratch_state(StateCell *cell) {
    return payload_scratch_state_append(cell);
}

CettaCount eval_payload_space_redirect_count(void) {
    return g_eval_payload_space_redirects.len;
}

bool eval_payload_space_redirect_at(CettaCount idx, Space **orig, Space **redirect) {
    if (idx >= g_eval_payload_space_redirects.len || !orig || !redirect)
        return false;
    *orig = g_eval_payload_space_redirects.orig[idx];
    *redirect = g_eval_payload_space_redirects.redirect[idx];
    return true;
}

CettaCount eval_payload_state_redirect_count(void) {
    return g_eval_payload_state_redirects.len;
}

bool eval_payload_state_redirect_at(CettaCount idx, StateCell **orig,
                                    StateCell **redirect) {
    if (idx >= g_eval_payload_state_redirects.len || !orig || !redirect)
        return false;
    *orig = g_eval_payload_state_redirects.orig[idx];
    *redirect = g_eval_payload_state_redirects.redirect[idx];
    return true;
}

typedef struct {
    Space **items;
    CettaCount len, cap;
} TempSpaceSet;

static __thread TempSpaceSet g_temp_spaces = {0};
static __thread TempSpaceSet g_new_space_track = {0};
static pthread_mutex_t g_new_space_drained_mutex = PTHREAD_MUTEX_INITIALIZER;
static TempSpaceSet g_new_space_track_drained = {0};

typedef struct {
    Arena scratch;
    Arena generated;
    Arena *survivor_arena;
    bool active;
} EvalQueryEpisode;

static Atom *resolve_registry_refs(Arena *a, Atom *atom);
static Arena *eval_storage_arena(Arena *fallback);
static Atom *payload_rebind_resources(Arena *a, Atom *atom);
static Space *payload_resolve_space_read(Space *sp);
static Space *payload_resolve_space_write(Space *sp);
static StateCell *payload_resolve_state_read(StateCell *cell);
static StateCell *payload_resolve_state_write(Arena *a, StateCell *cell);
static Space *resolve_registry_space_payload(Registry *r, Atom *ref);
static Space *resolve_single_space_arg_write(Space *s, Arena *a, Atom *space_expr,
                                             int fuel);

static uint64_t eval_monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void ensure_fallback_eval_session(void) {
    if (g_fallback_eval_session_ready) return;
    cetta_eval_session_init(&g_fallback_eval_session, CETTA_LANGUAGE_HE, NULL);
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

static Space *payload_resolve_space_read(Space *sp) {
    Space *redirect;
    if (!g_eval_payload_transactional || !sp)
        return sp;
    redirect = payload_space_redirect_for(sp);
    if (redirect)
        return redirect;
    return sp;
}

static Atom *payload_rebind_resources(Arena *a, Atom *atom) {
    Atom **elems;
    if (!a || !atom)
        return NULL;
    if (atom->kind == ATOM_GROUNDED) {
        if (atom->ground.gkind == GV_SPACE) {
            Space *redirect =
                payload_resolve_space_read((Space *)atom->ground.ptr);
            if (redirect != (Space *)atom->ground.ptr)
                return atom_space(a, redirect);
        } else if (atom->ground.gkind == GV_STATE) {
            StateCell *redirect =
                payload_resolve_state_read((StateCell *)atom->ground.ptr);
            if (redirect != (StateCell *)atom->ground.ptr)
                return atom_state(a, redirect);
        }
        return atom_deep_copy(a, atom);
    }
    if (atom->kind == ATOM_SYMBOL)
        return atom_symbol_id(a, atom->sym_id);
    if (atom->kind == ATOM_VAR)
        return atom_var_with_spelling(a, atom->sym_id, atom->var_id);
    if (atom->kind != ATOM_EXPR)
        return atom_deep_copy(a, atom);
    elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    if (!elems)
        return NULL;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        elems[i] = payload_rebind_resources(a, atom->expr.elems[i]);
        if (!elems[i])
            return NULL;
    }
    return atom_expr(a, elems, atom->expr.len);
}

static StateCell *payload_resolve_state_read(StateCell *cell) {
    StateCell *redirect;
    if (!g_eval_payload_transactional || !cell)
        return cell;
    redirect = payload_state_redirect_for(cell);
    if (redirect)
        return redirect;
    return cell;
}

static Space *payload_resolve_space_write(Space *sp) {
    Space *redirect;
    if (!g_eval_payload_transactional || !sp)
        return sp;
    redirect = payload_space_redirect_for(sp);
    if (redirect)
        return redirect;
    if (sp->overlay_base)
        return sp;
    redirect = cetta_malloc(sizeof(Space));
    if (!redirect)
        return NULL;
    space_init_overlay(redirect, sp);
    if (!eval_payload_track_scratch_space(redirect)) {
        space_free(redirect);
        free(redirect);
        return NULL;
    }
    if (!eval_payload_note_space_redirect(sp, redirect)) {
        space_free(redirect);
        free(redirect);
        return NULL;
    }
    return redirect;
}

static StateCell *payload_resolve_state_write(Arena *a, StateCell *cell) {
    Arena *owner;
    StateCell *redirect;
    if (!g_eval_payload_transactional || !cell)
        return cell;
    redirect = payload_state_redirect_for(cell);
    if (redirect)
        return redirect;
    if (cell->payload_owner_epoch != 0 &&
        cell->payload_owner_epoch == g_eval_payload_owner_epoch)
        return cell;
    owner = g_eval_payload_redirect_owner;
    if (!owner)
        owner = eval_current_persistent_arena();
    if (!owner)
        owner = eval_storage_arena(a);
    redirect = cetta_malloc(sizeof(StateCell));
    if (!redirect)
        return NULL;
    redirect->value = payload_rebind_resources(owner, cell->value);
    redirect->content_type = payload_rebind_resources(owner, cell->content_type);
    redirect->payload_owner_epoch = g_eval_payload_owner_epoch;
    redirect->payload_export_owner_epoch = 0;
    if (!eval_payload_track_scratch_state(redirect)) {
        free(redirect);
        return NULL;
    }
    if (!eval_payload_note_state_redirect(cell, redirect))
        return NULL;
    return redirect;
}

static bool eval_type_check_auto_enabled(void) {
    return active_eval_options_const()->type_check_auto;
}

static bool eval_bare_minimal_enabled(void) {
    return cetta_evaluator_options_is_bare_minimal(active_eval_options_const());
}

static int64_t eval_option_int_or_default(const char *key, int64_t default_value);

static _Atomic bool *eval_set_cancel_token(_Atomic bool *cancel_requested) {
    _Atomic bool *prev = g_eval_cancel_requested;
    g_eval_cancel_requested = cancel_requested;
    g_eval_cancel_observed = false;
    return prev;
}

static _Atomic bool *eval_set_hyperpose_thread_unsafe_token(
    _Atomic bool *unsafe_requested) {
    _Atomic bool *prev = g_hyperpose_thread_unsafe_requested;
    g_hyperpose_thread_unsafe_requested = unsafe_requested;
    return prev;
}

static bool eval_mark_hyperpose_thread_unsafe(void) {
    if (!g_hyperpose_thread_unsafe_requested)
        return false;
    atomic_store_explicit(g_hyperpose_thread_unsafe_requested, true,
                          memory_order_release);
    if (g_eval_cancel_requested) {
        atomic_store_explicit(g_eval_cancel_requested, true,
                              memory_order_release);
    }
    return true;
}

static bool eval_cancel_check(void) {
    if (!g_eval_cancel_requested ||
        !atomic_load_explicit(g_eval_cancel_requested, memory_order_acquire)) {
        return false;
    }
    if (!g_eval_cancel_observed) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HYPERPOSE_CANCEL_OBSERVED);
        g_eval_cancel_observed = true;
    }
    return true;
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

bool eval_current_prefer_rationals(void) {
    CettaEvalSession *session = active_eval_session();
    if (cetta_language_uses_rust_he_compat_semantics(session->language_id,
                                                     session->profile))
        return false;
    const CettaEvalOptionEntry *option = active_eval_option("prefer-rationals");
    if (!option) return false;
    if (option->kind == CETTA_EVAL_OPTION_VALUE_INT) {
        return option->int_value != 0;
    }
    return strcmp(option->repr, "allow") == 0 ||
           strcmp(option->repr, "on") == 0 ||
           strcmp(option->repr, "true") == 0 ||
           strcmp(option->repr, "True") == 0 ||
           strcmp(option->repr, "yes") == 0;
}

bool eval_current_uses_rust_he_compat_semantics(void) {
    CettaEvalSession *session = active_eval_session();
    return cetta_language_uses_rust_he_compat_semantics(session->language_id,
                                                        session->profile);
}

uint32_t eval_current_num_threads(void) {
    int64_t configured = eval_option_int_or_default("num-threads", 1);
    if (configured < 1)
        return 1u;
    if (configured > 1024)
        return 1024u;
    return (uint32_t)configured;
}

static int64_t eval_option_int_or_default(const char *key, int64_t default_value) {
    const CettaEvalOptionEntry *option = active_eval_option(key);
    if (!option)
        return default_value;
    if (option->kind == CETTA_EVAL_OPTION_VALUE_INT)
        return option->int_value;
    if (strcmp(option->repr, "off") == 0 ||
        strcmp(option->repr, "false") == 0 ||
        strcmp(option->repr, "disabled") == 0) {
        return 0;
    }
    if (strcmp(option->repr, "on") == 0 ||
        strcmp(option->repr, "true") == 0 ||
        strcmp(option->repr, "fair") == 0 ||
        strcmp(option->repr, "threaded") == 0 ||
        strcmp(option->repr, "parallel") == 0) {
        return 2;
    }
    return default_value;
}

static const char *validate_thread_count_pragma_value(Atom *value) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT)
        return "UnsignedIntegerIsExpected";
    if (value->ground.ival < 0)
        return "UnsignedIntegerIsExpected";
    if (value->ground.ival > 1024)
        return "ThreadCountOutOfRange";
    return NULL;
}

static bool generic_mork_space_sugar_allowed(void) {
    return mork_space_sugar_option_allows(active_eval_option("mork-space-sugar"));
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
    for (CettaCount i = 0; i < rs.len; i++) {
        if (cetta_native_handle_arg(rs.items[i], "mork-space", &id)) {
            result_set_free(&rs);
            return true;
        }
    }
    result_set_free(&rs);
    return false;
}

static bool resolve_explicit_mork_bridge_arg(Space *s, Arena *a,
                                             Atom *space_expr, int fuel,
                                             CettaMorkSpaceHandle **out_bridge) {
    if (out_bridge)
        *out_bridge = NULL;
    if (!g_library_context)
        return false;

    Atom *direct = resolve_registry_refs(a, space_expr);
    if (cetta_library_lookup_explicit_mork_bridge(g_library_context, direct,
                                                  out_bridge) &&
        out_bridge && *out_bridge) {
        return true;
    }

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (CettaCount i = 0; i < rs.len; i++) {
        if (cetta_library_lookup_explicit_mork_bridge(g_library_context,
                                                      rs.items[i],
                                                      out_bridge) &&
            out_bridge && *out_bridge) {
            result_set_free(&rs);
            return true;
        }
    }
    result_set_free(&rs);
    if (out_bridge)
        *out_bridge = NULL;
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
static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel, OutcomeSet *os);
static bool emit_unquoted_mork_rows(Space *s, Arena *a, SymbolId internal_head_id,
                                    Atom *surface_atom, uint32_t nargs,
                                    Atom **args, bool evaluate_rows, int fuel,
                                    OutcomeSet *os);
static bool emit_direct_mork_match_rows(Space *s, Arena *a, Atom *surface_atom,
                                        Atom **args, int fuel,
                                        OutcomeSet *os);
static bool hyperpose_thread_barrier_head(SymbolId head_id, Atom *head);
static bool emit_direct_mork_atoms_rows(Space *s, Arena *a, Atom *surface_atom,
                                        Atom **args, int fuel,
                                        OutcomeSet *os);

static Atom *eval_llist_call_error(Arena *a, Atom *head, Atom **args,
                                   uint32_t nargs, Atom *reason) {
    return atom_error(a, make_call_expr(a, head, args, nargs), reason);
}

static Atom *eval_llist_bad_arg_type(Arena *a, Atom *head, Atom **args,
                                     uint32_t nargs, int bad_idx,
                                     Atom *expected_type, Atom *actual_atom) {
    Atom *actual_type =
        (actual_atom && actual_atom->kind == ATOM_GROUNDED)
            ? get_grounded_type(a, actual_atom)
            : get_meta_type(a, actual_atom);
    Atom *reason = atom_expr(a, (Atom *[]){
        atom_symbol(a, "BadArgType"),
        atom_int(a, bad_idx),
        expected_type,
        actual_type
    }, 4);
    return eval_llist_call_error(a, head, args, nargs, reason);
}

static Atom *eval_minimal_foldl_llist(Arena *a, Atom *head, Atom **args,
                                      uint32_t nargs) {
    if (nargs != 6) {
        return eval_llist_call_error(
            a, head, args, nargs, atom_symbol(a, "IncorrectNumberOfArguments"));
    }
    if (args[2]->kind != ATOM_VAR) {
        return eval_llist_bad_arg_type(
            a, head, args, nargs, 3, atom_variable_type(a), args[2]);
    }
    if (args[3]->kind != ATOM_VAR) {
        return eval_llist_bad_arg_type(
            a, head, args, nargs, 4, atom_variable_type(a), args[3]);
    }
    if (!(args[5]->kind == ATOM_GROUNDED &&
          args[5]->ground.gkind == GV_SPACE)) {
        return eval_llist_bad_arg_type(
            a, head, args, nargs, 6, atom_symbol(a, "SpaceType"), args[5]);
    }

    Atom *cursor = args[0];
    Atom *acc = args[1];
    Atom *acc_var = args[2];
    Atom *item_var = args[3];
    Atom *op_expr = args[4];
    Space *target = (Space *)args[5]->ground.ptr;
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&scratch, NULL);
    ArenaMark scratch_mark = arena_mark(&scratch);

    for (;;) {
        if (atom_is_symbol_id(cursor, g_builtin_syms.llist_nil)) {
            Atom *result = atom_expr2(a, atom_symbol(a, "return"), acc);
            arena_free(&scratch);
            return result;
        }
        if (cursor->kind != ATOM_EXPR || cursor->expr.len != 3 ||
            !atom_is_symbol_id(cursor->expr.elems[0],
                               g_builtin_syms.llist_cons)) {
            Atom *error = eval_llist_call_error(
                a, head, args, nargs,
                atom_symbol(a,
                    "_minimal-foldl-llist expects a proper LCons/LNil list"));
            arena_free(&scratch);
            return error;
        }

        Atom *step_op = cetta_fold_bind_step_atom(
            &scratch, op_expr,
            acc_var->sym_id, acc,
            item_var->sym_id, cursor->expr.elems[1]);
        ResultSet results;
        Atom *next = NULL;
        Atom *step_error = NULL;
        bool multiple = false;
        result_set_init(&results);
        metta_eval(target, &scratch, NULL, step_op,
                   eval_get_default_fuel(), &results);

        for (CettaCount i = 0; i < results.len; i++) {
            Atom *candidate = results.items[i];
            if (atom_is_empty(candidate))
                continue;
            if (atom_is_error(candidate)) {
                step_error = candidate;
                break;
            }
            if (next) {
                multiple = true;
                break;
            }
            next = candidate;
        }

        Atom *persisted = NULL;
        if (step_error)
            persisted = atom_deep_copy(a, step_error);
        else if (!multiple && next)
            persisted = atom_deep_copy(a, next);
        result_set_free(&results);
        arena_reset(&scratch, scratch_mark);

        if (step_error) {
            arena_free(&scratch);
            return persisted;
        }
        if (multiple) {
            Atom *error = eval_llist_call_error(
                a, head, args, nargs,
                atom_symbol(a,
                    "_minimal-foldl-llist step produced multiple results"));
            arena_free(&scratch);
            return error;
        }
        if (!persisted) {
            Atom *error = eval_llist_call_error(
                a, head, args, nargs,
                atom_symbol(a,
                    "_minimal-foldl-llist step produced no result"));
            arena_free(&scratch);
            return error;
        }

        acc = persisted;
        cursor = cursor->expr.elems[2];
    }
}

static Atom *dispatch_named_native(Space *s, Arena *a, SymbolId head_id,
                                   Atom **args, uint32_t nargs) {
    Atom *head = atom_symbol_id(a, head_id);
    if (head_id == g_builtin_syms.minimal_foldl_llist)
        return eval_minimal_foldl_llist(a, head, args, nargs);
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
    if (emit_direct_mork_atoms_rows(s, a, surface_atom, args, fuel, os)) {
        return true;
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

static CettaCount eval_array_capacity_limit(size_t elem_size) {
    return elem_size == 0 ? (CettaCount)SIZE_MAX
                          : (CettaCount)(SIZE_MAX / elem_size);
}

static bool eval_next_capacity(CettaCount current, CettaCount needed,
                               size_t elem_size, CettaCount *out_next) {
    CettaCount limit;
    CettaCount next;
    if (!out_next)
        return false;
    limit = eval_array_capacity_limit(elem_size);
    if (needed > limit)
        return false;
    next = current ? current * 2u : 8u;
    if (next <= current || next < needed)
        next = needed;
    if (next > limit)
        next = limit;
    if (next < needed)
        return false;
    *out_next = next;
    return true;
}

static bool result_set_ensure_one(ResultSet *rs) {
    CettaCount next_cap;
    bool using_inline;
    if (!rs)
        return false;
    if (rs->len < rs->cap)
        return true;
    if (!eval_next_capacity(rs->cap, rs->len + 1u, sizeof(Atom *), &next_cap))
        return false;
    using_inline = rs->items == rs->inline_items;
    if (using_inline) {
        Atom **next = cetta_malloc(sizeof(Atom *) * (size_t)next_cap);
        if (rs->len > 0)
            memcpy(next, rs->items, sizeof(Atom *) * (size_t)rs->len);
        rs->items = next;
    } else {
        rs->items = cetta_realloc(rs->items, sizeof(Atom *) * (size_t)next_cap);
    }
    rs->cap = next_cap;
    return true;
}

void result_set_init(ResultSet *rs) {
    rs->items = rs->inline_items;
    rs->len = 0;
    rs->cap = 1;
}

void result_set_add(ResultSet *rs, Atom *atom) {
    if (!result_set_ensure_one(rs))
        return;
    rs->items[rs->len++] = atom;
}

static void result_set_filter_empty(ResultSet *rs) {
    CettaCount out = 0;
    if (!rs)
        return;
    for (CettaCount i = 0; i < rs->len; i++) {
        if (atom_is_empty(rs->items[i]))
            continue;
        rs->items[out++] = rs->items[i];
    }
    rs->len = out;
}

void result_set_free(ResultSet *rs) {
    if (rs->items != rs->inline_items)
        free(rs->items);
    rs->items = rs->inline_items;
    rs->len = 0;
    rs->cap = 1;
}

/* ── Helpers ────────────────────────────────────────────────────────────── */

static bool expr_head_is_id(Atom *a, SymbolId id) {
    return a->kind == ATOM_EXPR && a->expr.len >= 1 &&
           atom_is_symbol_id(a->expr.elems[0], id);
}

static CettaExprLen expr_nargs(Atom *a) {
    return (a->kind == ATOM_EXPR && a->expr.len > 0) ? a->expr.len - 1 : 0;
}

static bool expr_nargs_u32(Atom *a, uint32_t *out) {
    CettaExprLen nargs = expr_nargs(a);
    if (!out)
        return false;
    if (!cetta_expr_len_fits_u32(nargs))
        return false;
    *out = (uint32_t)nargs;
    return true;
}

static Atom *expr_arg(Atom *a, CettaExprIndex i) {
    return a->expr.elems[i + 1];
}

static Atom *expr_arity_too_large_error(Arena *a, Atom *atom) {
    return atom_error(a, atom, atom_symbol(a, "ArityTooLarge"));
}

static bool is_true_atom(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.true_text) ||
           (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL && a->ground.bval);
}

static bool is_false_atom(Atom *a) {
    return atom_is_symbol_id(a, g_builtin_syms.false_text) ||
           (a->kind == ATOM_GROUNDED && a->ground.gkind == GV_BOOL && !a->ground.bval);
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

static void eval_release_episode_survivor_arena(void) {
    if (!g_episode_survivor_arena_ready)
        return;
    arena_free(&g_episode_survivor_arena);
    g_episode_survivor_arena_ready = false;
}

static void eval_query_episode_init(EvalQueryEpisode *episode) {
    Arena *persistent = eval_persistent_arena();
    if (!episode)
        return;
    arena_init(&episode->scratch);
    arena_set_hashcons(&episode->scratch, NULL);
    arena_set_runtime_kind(&episode->scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_init(&episode->generated);
    arena_set_hashcons(&episode->generated, NULL);
    arena_set_runtime_kind(&episode->generated, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    episode->survivor_arena = persistent ? persistent
                                         : eval_active_episode_survivor_arena();
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
    if (!episode || !episode->active)
        return fallback;
    if (fallback) {
        episode->survivor_arena = fallback;
        return fallback;
    }
    if (!episode->survivor_arena)
        return fallback;
    return episode->survivor_arena;
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

static bool eval_query_episode_promote_bindings(EvalQueryEpisode *episode,
                                                Bindings *bindings) {
    if (!episode || !episode->active || !episode->survivor_arena || !bindings)
        return true;
    return bindings_promote_atoms_to_arena(bindings,
                                           episode->survivor_arena);
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

/* Match standardization epochs are query-local tags, not stored identity.
   Removal must recover the stored base VarId instead of alpha-canonicalizing
   distinct variable-bearing rows into the same structural shape. */
static Atom *space_remove_compare_rewrite_var(Arena *dst, Atom *src_var,
                                              void *ctx) {
    (void)ctx;
    if (!src_var || src_var->kind != ATOM_VAR)
        return NULL;
    if (var_epoch_suffix(src_var->var_id) == 0)
        return atom_deep_copy(dst, src_var);
    VarId base_id = (VarId)var_base_id(src_var->var_id);
    if (base_id == VAR_ID_NONE)
        return atom_deep_copy(dst, src_var);
    return atom_var_with_spelling(dst, src_var->sym_id, base_id);
}

static Atom *space_remove_compare_atom(const Space *space, Arena *dst,
                                       Atom *src) {
    if (space && space->native.universe &&
        space->native.universe->persistent_arena) {
        return cetta_atom_rewrite_vars(dst, src,
                                       space_remove_compare_rewrite_var,
                                       NULL, true);
    }
    return atom_deep_copy(dst, src);
}

/* ── Outcome set (unified result type: atom + bindings) ─────────────────── */

void outcome_set_init_with_owner(OutcomeSet *os, Arena *owner) {
    memset(os->inline_items, 0, sizeof(os->inline_items));
    os->items = os->inline_items;
    os->len = 0;
    os->cap = 1;
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

static bool outcome_set_ensure_one(OutcomeSet *os) {
    CettaCount next_cap;
    bool using_inline;
    if (!os)
        return false;
    if (os->len < os->cap)
        return true;
    if (!eval_next_capacity(os->cap, os->len + 1u, sizeof(Outcome), &next_cap))
        return false;
    using_inline = os->items == os->inline_items;
    if (using_inline) {
        Outcome *next = cetta_malloc(sizeof(Outcome) * (size_t)next_cap);
        if (os->len > 0)
            memcpy(next, os->items, sizeof(Outcome) * (size_t)os->len);
        os->items = next;
    } else {
        os->items = cetta_realloc(os->items, sizeof(Outcome) * (size_t)next_cap);
    }
    os->cap = next_cap;
    return true;
}

static Outcome *outcome_set_push_slot(OutcomeSet *os) {
    if (!outcome_set_ensure_one(os))
        return NULL;
    return &os->items[os->len++];
}

static void outcome_init(Outcome *out) {
    if (!out)
        return;
    out->kind = CETTA_OUTCOME_INLINE;
    out->atom = NULL;
    out->materialized_atom = NULL;
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
}

static void outcome_move(Outcome *dst, Outcome *src) {
    dst->kind = src->kind;
    dst->atom = src->atom;
    dst->materialized_atom = src->materialized_atom;
    bindings_move(&dst->env, &src->env);
    variant_instance_move(&dst->variant, &src->variant);
    dst->answer_ref = src->answer_ref;
    cetta_var_map_init(&src->answer_ref.goal_instantiation);
    src->answer_ref.bank = NULL;
    src->answer_ref.ref = CETTA_ANSWER_REF_NONE;
    src->kind = CETTA_OUTCOME_INLINE;
    src->atom = NULL;
    src->materialized_atom = NULL;
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

static bool outcome_promote_payload(Arena *owner, Outcome *out) {
    if (!owner || !out)
        return true;
    if (out->kind == CETTA_OUTCOME_INLINE && out->atom) {
        Atom *promoted = atom_deep_copy(owner, out->atom);
        if (!promoted)
            return false;
        out->atom = promoted;
    }
    if (!bindings_promote_atoms_to_arena(&out->env, owner) ||
        !variant_instance_promote_atoms_to_arena(owner, &out->variant)) {
        return false;
    }
    out->materialized_atom = NULL;
    outcome_refresh_materialized_fast_path(out);
    return true;
}

static bool outcome_clone(Outcome *dst, const Outcome *src, Arena *owner) {
    if (!dst || !src)
        return false;
    outcome_init(dst);
    dst->kind = src->kind;
    dst->atom = src->atom;
    dst->materialized_atom = src->materialized_atom;
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
    if (owner && !outcome_promote_payload(owner, dst)) {
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
static bool try_count_collapse_match(Space *s, Arena *a, Atom *atom,
                                     const Bindings *current_env, int fuel,
                                     uint64_t *out_count);
static Atom *materialize_runtime_token(Space *s, Arena *a, Atom *atom);
static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr, int fuel);
static Atom *space_arg_error(Arena *a, Atom *call, const char *message);
static Atom *space_backend_error_if_set(Arena *a, Atom *call);
static Atom *space_backend_or_symbol_error(Arena *a, Atom *call,
                                           const char *fallback_symbol);
static bool is_function_type(Atom *a);
static uint32_t get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types);
static bool type_expr_is_well_formed_profiled(Space *s, Arena *a, Atom *ty);
static bool add_atoms_source_shape(Atom *items, Atom **out_source_ref,
                                   bool *out_collapsed,
                                   SpaceTransferEndpointKind *out_source_kind);
static bool add_atoms_public_surface_has_only_default(Space *s);

static bool grounded_dispatch_accepts_data_arg(Atom *head, uint32_t arg_index) {
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    if (head->sym_id == g_builtin_syms.lib_gparse_inference_presentation ||
        head->sym_id == g_builtin_syms.lib_gparse_inference_dag ||
        head->sym_id == g_builtin_syms.lib_gparse_inference_dag_presentation ||
        head->sym_id == g_builtin_syms.lib_gparse_inference_dag_proof)
        return true;
    if (head->sym_id == g_builtin_syms.minimal_foldl_llist &&
        (arg_index == 0 || arg_index == 4))
        return true;
    if (head->sym_id == g_builtin_syms.minimal_space_contains_exact &&
        arg_index == 1)
        return true;
    return arg_index == 1 &&
           (head->sym_id == g_builtin_syms.add_atom ||
            head->sym_id == g_builtin_syms.remove_atom);
}
static bool symbol_id_is_builtin_surface(SymbolId id);
static bool bindings_project_answer_ref_env(Arena *a,
                                            const CettaVarMap *goal_instantiation,
                                            const Bindings *full,
                                            Bindings *projected);

static bool atom_eval_is_immediate_value(Atom *atom, int fuel) {
    return fuel == 0 ||
           atom->kind == ATOM_SYMBOL ||
           atom->kind == ATOM_GROUNDED ||
           atom->kind == ATOM_VAR ||
           (atom->kind == ATOM_EXPR && atom->expr.len == 0);
}

static bool atom_is_constructor_normal_form(Space *s, Arena *a, Atom *atom,
                                            int fuel) {
    Atom **stack = NULL;
    size_t len = 0;
    size_t cap = 0;
    bool ok = false;

#define PUSH_ATOM(candidate) do { \
    if (len == cap) { \
        size_t next_cap = cap ? cap * 2u : 32u; \
        Atom **next_stack = realloc(stack, next_cap * sizeof(Atom *)); \
        if (!next_stack) goto done; \
        stack = next_stack; \
        cap = next_cap; \
    } \
    stack[len++] = (candidate); \
} while (0)

    if (!atom)
        goto done;
    PUSH_ATOM(atom);
    while (len > 0) {
        Atom *cur = stack[--len];
        if (!cur || atom_is_empty(cur) || atom_is_error(cur) ||
            atom_eval_is_immediate_value(cur, fuel)) {
            continue;
        }
        if (cur->kind != ATOM_EXPR || cur->expr.len == 0)
            goto done;
        Atom *head = cur->expr.elems[0];
        if (!head)
            goto done;
        if (registry_lookup_atom(head))
            goto done;
        if (head->kind == ATOM_GROUNDED) {
            if (head->ground.gkind == GV_CAPTURE ||
                head->ground.gkind == GV_FOREIGN)
                goto done;
    for (CettaExprIndex i = 1; i < cur->expr.len; i++)
                PUSH_ATOM(cur->expr.elems[i]);
            continue;
        }
        if (head->kind != ATOM_SYMBOL)
            goto done;
        if (symbol_id_is_builtin_surface(head->sym_id) ||
            is_grounded_op(head->sym_id) ||
            (g_library_context && g_library_context->foreign_runtime &&
             cetta_foreign_is_callable_atom(head)) ||
            space_equations_may_match_known_head(s, head->sym_id)) {
            goto done;
        }
        Atom **head_types = NULL;
        uint32_t ntypes = get_atom_types_profiled(s, a, head, &head_types);
        for (uint32_t ti = 0; ti < ntypes; ti++) {
            if (is_function_type(head_types[ti])) {
                free(head_types);
                goto done;
            }
        }
        free(head_types);
        for (CettaExprIndex i = 1; i < cur->expr.len; i++)
            PUSH_ATOM(cur->expr.elems[i]);
    }
    ok = true;

done:
    free(stack);
#undef PUSH_ATOM
    return ok;
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

    uint32_t nargs = 0;
    if (!expr_nargs_u32(atom, &nargs))
        return expr_arity_too_large_error(a, atom);
    if ((head->sym_id == g_builtin_syms.size ||
         head->sym_id == g_builtin_syms.size_atom) &&
        nargs == 1) {
        uint64_t count = 0;
        Atom *applied = (!prefix || prefix->len == 0)
            ? atom
            : bindings_apply_if_vars(prefix, a, atom);
        if (try_count_collapse_match(s, a, applied, NULL, fuel, &count))
            return atom_int(a, (int64_t)count);
    }
    Atom **args = arena_alloc(a, sizeof(Atom *) * nargs);
    for (uint32_t i = 0; i < nargs; i++) {
        Atom *arg = atom->expr.elems[i + 1];
        arg = bindings_apply_if_vars(prefix, a, arg);
        Atom *resolved = registry_lookup_atom(arg);
        args[i] = materialize_runtime_token(s, a, resolved ? resolved : arg);
        if (atom_is_empty(args[i]) || atom_is_error(args[i]) ||
            (!atom_eval_is_immediate_value(args[i], fuel) &&
             !grounded_dispatch_accepts_data_arg(head, i) &&
             !((head->sym_id == g_builtin_syms.op_eq ||
                head->sym_id == g_builtin_syms.alpha_eq) &&
               atom_is_constructor_normal_form(s, a, args[i], fuel))))
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

static bool symbol_id_is_if_surface(SymbolId id) {
    const char *name = id == SYMBOL_ID_NONE ? NULL : symbol_bytes(g_symbols, id);
    return name && strcmp(name, "if") == 0;
}

static Atom *outcome_preview_resolve_atom(Bindings *env,
                                          const VariantInstance *variant,
                                          Atom *atom, uint32_t depth);

static bool outcome_skip_call_observation_fast_path(Space *s,
                                                    const Outcome *out) {
    Atom *preview = outcome_preview_atom(out);
    Atom *head;
    if (!s || !preview || preview->kind != ATOM_EXPR ||
        preview->expr.len == 0) {
        return false;
    }
    preview = outcome_preview_resolve_atom((Bindings *)&out->env, &out->variant,
                                           preview, 0);
    if (!preview || preview->kind != ATOM_EXPR || preview->expr.len == 0)
        return false;
    head = outcome_preview_resolve_atom((Bindings *)&out->env, &out->variant,
                                        preview->expr.elems[0], 1);
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

typedef enum {
    CETTA_OUTCOME_ERROR_PREVIEW_UNKNOWN = 0,
    CETTA_OUTCOME_ERROR_PREVIEW_FALSE = 1,
    CETTA_OUTCOME_ERROR_PREVIEW_TRUE = 2,
} CettaOutcomeErrorPreview;

static Atom *outcome_preview_resolve_atom(Bindings *env,
                                          const VariantInstance *variant,
                                          Atom *atom, uint32_t depth) {
    if (!atom || depth > CETTA_MATCH_DEPTH_LIMIT)
        return atom;
    while (atom && atom->kind == ATOM_VAR && depth <= CETTA_MATCH_DEPTH_LIMIT) {
        Atom *resolved =
            env ? bindings_resolve_atom_preview(env, atom) : atom;
        if (resolved && resolved != atom) {
            atom = resolved;
            depth++;
            continue;
        }
        if (variant && variant_instance_present(variant)) {
            Atom *slot_val = variant_instance_peek_private_var(variant, atom);
            if (slot_val && slot_val != atom) {
                atom = slot_val;
                depth++;
                continue;
            }
        }
        break;
    }
    return atom;
}

static CettaOutcomeErrorPreview outcome_error_preview_from_atom(
    Bindings *env, const VariantInstance *variant, Atom *atom, uint32_t depth) {
    if (!atom)
        return CETTA_OUTCOME_ERROR_PREVIEW_FALSE;
    if (depth > CETTA_MATCH_DEPTH_LIMIT)
        return CETTA_OUTCOME_ERROR_PREVIEW_UNKNOWN;

    atom = outcome_preview_resolve_atom(env, variant, atom, depth);

    switch (atom->kind) {
    case ATOM_SYMBOL:
        return atom_is_symbol_id(atom, g_builtin_syms.error)
                   ? CETTA_OUTCOME_ERROR_PREVIEW_TRUE
                   : CETTA_OUTCOME_ERROR_PREVIEW_FALSE;
    case ATOM_EXPR:
        if (atom->expr.len == 0)
            return CETTA_OUTCOME_ERROR_PREVIEW_FALSE;
        return outcome_error_preview_from_atom(env, variant, atom->expr.elems[0],
                                               depth + 1);
    case ATOM_VAR:
        return variant && variant_instance_present(variant)
                   ? CETTA_OUTCOME_ERROR_PREVIEW_UNKNOWN
                   : CETTA_OUTCOME_ERROR_PREVIEW_FALSE;
    case ATOM_GROUNDED:
        return CETTA_OUTCOME_ERROR_PREVIEW_FALSE;
    }
    return CETTA_OUTCOME_ERROR_PREVIEW_UNKNOWN;
}

static __attribute__((unused)) bool outcome_atom_is_empty_or_error(Arena *a, Outcome *out) {
    return atom_is_empty_or_error(outcome_atom_materialize(a, out));
}

static bool outcome_atom_is_empty_result(Arena *a, Outcome *out) {
    return atom_is_empty(outcome_atom_materialize(a, out));
}

static void bindings_array_release(Bindings *items, uint32_t len,
                                   Bindings *inline_items) {
    if (!items)
        return;
    for (uint32_t i = 0; i < len; i++)
        bindings_free(&items[i]);
    if (items != inline_items)
        free(items);
}

static bool bindings_array_grow(Bindings **items, uint32_t len, uint32_t *cap,
                                Bindings *inline_items) {
    uint32_t next_cap;
    Bindings *next_items;
    if (!items || !*items || !cap)
        return false;
    next_cap = *cap ? (*cap * 2u) : 8u;
    if (next_cap <= *cap)
        return false;
    if (*items == inline_items) {
        next_items = cetta_malloc(sizeof(Bindings) * (size_t)next_cap);
        for (uint32_t i = 0; i < len; i++)
            bindings_move(&next_items[i], &inline_items[i]);
    } else {
        next_items =
            cetta_realloc(*items, sizeof(Bindings) * (size_t)next_cap);
    }
    *items = next_items;
    *cap = next_cap;
    return true;
}

void outcome_set_add(OutcomeSet *os, Atom *atom, const Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    if (!slot)
        return;
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
    if (!slot)
        return;
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
    if (!slot)
        return;
    outcome_init(slot);
    slot->atom = atom;
    bindings_assert_no_private_variant_slots(env);
    bindings_move(&slot->env, env);
    outcome_try_factor_variant(slot);
    outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_existing(OutcomeSet *os, const Outcome *src) {
    Outcome *slot = outcome_set_push_slot(os);
    if (!slot)
        return;
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
    if (!slot)
        return;
    outcome_init(slot);
    outcome_move(slot, src);
    if (!outcome_promote_payload(outcome_set_payload_arena(os, NULL), slot)) {
        outcome_free_fields(slot);
        os->len--;
        return;
    }
    if (!slot->materialized_atom)
        outcome_refresh_materialized_fast_path(slot);
}

static void outcome_set_add_existing_with_env(Arena *a,
                                              OutcomeSet *os,
                                              const Outcome *src,
                                              const Bindings *env) {
    Outcome *slot = outcome_set_push_slot(os);
    if (!slot)
        return;
    Arena *payload_arena = outcome_set_payload_arena(os, a);
    outcome_init(slot);
    slot->kind = src->kind;
    if (payload_arena && src->kind == CETTA_OUTCOME_INLINE && src->atom) {
        slot->atom = atom_deep_copy(payload_arena, src->atom);
    } else {
        slot->atom = src->atom;
    }
    bindings_assert_no_private_variant_slots(&src->env);
    bindings_assert_no_private_variant_slots(env);
    if ((src->kind == CETTA_OUTCOME_INLINE && src->atom && !slot->atom) ||
        !(src->kind == CETTA_OUTCOME_ANSWER_REF
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
    if (payload_arena && !outcome_promote_payload(payload_arena, slot)) {
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
    if (!slot)
        return;
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
    if (!slot)
        return false;
    outcome_init(slot);
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
        !bindings_promote_atoms_to_arena(&slot->env, payload_arena) ||
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
    for (CettaCount i = 0; i < src->len; i++)
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
    if (!slot) {
        if (effective == &merged)
            bindings_free(&merged);
        outcome_free_fields(&inflated);
        return false;
    }
    outcome_init(slot);
    slot->atom = effective_src->atom;
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
        outcome_set_add(os, applied, &empty);
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

static void outcome_set_filter_empty_if_nonempty(Arena *a, OutcomeSet *os) {
    bool has_non_empty = false;
    for (CettaCount i = 0; i < os->len; i++) {
        if (!outcome_atom_is_empty_result(a, &os->items[i])) {
            has_non_empty = true;
            break;
        }
    }
    if (!has_non_empty) return;

    CettaCount out = 0;
    for (CettaCount i = 0; i < os->len; i++) {
        if (outcome_atom_is_empty_result(a, &os->items[i]))
            continue;
        if (out != i) {
            outcome_free_fields(&os->items[out]);
            outcome_init(&os->items[out]);
            outcome_move(&os->items[out], &os->items[i]);
        }
        out++;
    }
    for (CettaCount i = out; i < os->len; i++)
        outcome_free_fields(&os->items[i]);
    os->len = out;
}

static void outcome_set_filter_errors_if_success(Arena *a, OutcomeSet *os) {
    bool has_success = false;
    for (CettaCount i = 0; i < os->len; i++) {
        if (!outcome_atom_is_empty_result(a, &os->items[i]) &&
            !outcome_atom_is_error(a, &os->items[i])) {
            has_success = true;
            break;
        }
    }
    if (!has_success) return;

    CettaCount out = 0;
    for (CettaCount i = 0; i < os->len; i++) {
        if (outcome_atom_is_error(a, &os->items[i]))
            continue;
        if (out != i) {
            outcome_free_fields(&os->items[out]);
            outcome_init(&os->items[out]);
            outcome_move(&os->items[out], &os->items[i]);
        }
        out++;
    }
    for (CettaCount i = out; i < os->len; i++)
        outcome_free_fields(&os->items[i]);
    os->len = out;
}

static void outcome_set_normalize_visible_frontier(Arena *a, OutcomeSet *os) {
    outcome_set_filter_empty_if_nonempty(a, os);
    outcome_set_filter_errors_if_success(a, os);
}

void outcome_set_free(OutcomeSet *os) {
    for (CettaCount i = 0; i < os->len; i++)
        outcome_free_fields(&os->items[i]);
    if (os->items != os->inline_items)
        free(os->items);
    memset(os->inline_items, 0, sizeof(os->inline_items));
    os->items = os->inline_items;
    os->len = 0;
    os->cap = 1;
    os->payload_owner = NULL;
}

static Atom *make_call_expr(Arena *a, Atom *head, Atom **args, uint32_t nargs);

static const CettaProfile *active_profile(void) {
    return g_library_context ? g_library_context->session.profile : NULL;
}

static bool active_profile_uses_total_structural_eq(void) {
    const CettaProfile *profile = active_profile();
    return profile && profile->id == CETTA_PROFILE_HE_PRIME;
}

static CettaLanguageId active_language_id(void) {
    return g_library_context ? g_library_context->session.language_id : CETTA_LANGUAGE_HE;
}

static bool active_profile_uses_rust_he_compat_semantics(void) {
    return cetta_language_uses_rust_he_compat_semantics(active_language_id(),
                                                        active_profile());
}

static bool active_surface_allowed(const char *surface_name) {
    return cetta_language_allows_surface(active_language_id(), active_profile(),
                                         surface_name);
}

static const char *whole_call_extension_surface_name(SymbolId head_id) {
    if (head_id == g_builtin_syms.collect) return "collect";
    if (head_id == g_builtin_syms.fold) return "fold";
    if (head_id == g_builtin_syms.fold_by_key) return "fold-by-key";
    if (head_id == g_builtin_syms.reduce) return "reduce";
    if (head_id == g_builtin_syms.select) return "select";
    if (head_id == g_builtin_syms.once) return "once";
    if (head_id == g_builtin_syms.search_policy) return "search-policy";
    if (head_id == g_builtin_syms.foldl_atom_in_space) return "foldl-atom-in-space";
    if (head_id == g_builtin_syms.foldl_atom) return "foldl-atom";
    if (head_id == g_builtin_syms.filter_atom) return "filter-atom";
    if (head_id == g_builtin_syms.range_atom) return "range-atom";
    if (head_id == g_builtin_syms.repeat_atom) return "repeat-atom";
    if (head_id == g_builtin_syms.add_atom_nodup) return "add-atom-nodup";
    if (head_id == g_builtin_syms.module_inventory_bang) return "module-inventory!";
    if (head_id == g_builtin_syms.reset_runtime_stats_bang) return "reset-runtime-stats!";
    if (head_id == g_builtin_syms.runtime_stats_bang) return "runtime-stats!";
    if (head_id == g_builtin_syms.with_space_snapshot) return "with-space-snapshot";
    if (head_id == g_builtin_syms.space_set_backend_bang) return "space-set-backend!";
    if (head_id == g_builtin_syms.space_set_match_backend_bang) return "space-set-match-backend!";
    if (head_id == g_builtin_syms.size) return "size";
    if (head_id == g_builtin_syms.space_len) return "space-len";
    if (head_id == g_builtin_syms.space_push) return "space-push";
    if (head_id == g_builtin_syms.space_peek) return "space-peek";
    if (head_id == g_builtin_syms.space_pop) return "space-pop";
    if (head_id == g_builtin_syms.space_get) return "space-get";
    if (head_id == g_builtin_syms.space_truncate) return "space-truncate";
    if (head_id == g_builtin_syms.step_bang) return "step!";
    if (head_id == g_builtin_syms.count_atoms) return "count-atoms";
    return NULL;
}

static bool active_profile_disables_whole_call_surface(SymbolId head_id) {
    const char *surface = whole_call_extension_surface_name(head_id);
    return surface && !active_surface_allowed(surface);
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
    CettaCount ordinal;
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

static Atom *search_policy_reason_unknown(Arena *a, Atom *lane_atom) {
    return atom_expr2(a, atom_symbol(a, "UnknownSearchPolicy"), lane_atom);
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

    CettaExprLen nargs = expr_nargs(policy_atom);
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
    }

    if (spec) {
        spec->present = true;
        spec->lane = lane;
        spec->order = order;
    }
    return CETTA_SEARCH_POLICY_PARSE_OK;
}

static void emit_policy_stream_call_inert(Space *s, Arena *a, Atom *atom,
                                          CettaExprIndex stream_arg_idx,
                                          const Bindings *env, int fuel,
                                          OutcomeSet *os) {
    if (!atom || atom->kind != ATOM_EXPR || stream_arg_idx >= expr_nargs(atom))
        return;

    Bindings empty;
    bindings_init(&empty);

    OutcomeSet stream;
    outcome_set_init(&stream);
    Atom *stream_expr = bindings_apply_if_vars(env, a, expr_arg(atom, stream_arg_idx));
    metta_eval_bind(s, a, stream_expr, fuel, &stream);

    for (CettaCount i = 0; i < stream.len; i++) {
        Atom *result = outcome_atom_materialize(a, &stream.items[i]);
        if (!result)
            continue;
        if (atom_is_empty_or_error(result)) {
            outcome_set_add(os, result, &empty);
            continue;
        }

        Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
        for (CettaExprIndex j = 0; j < atom->expr.len; j++) {
            if (j == stream_arg_idx + 1) {
                elems[j] = result;
            } else {
                elems[j] = bindings_apply_if_vars(env, a, atom->expr.elems[j]);
            }
        }
        outcome_set_add(os, atom_expr(a, elems, atom->expr.len), &empty);
    }

    outcome_set_free(&stream);
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

    Space *target = resolve_single_space_arg_write(s, a, space_ref, fuel);
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

    if (space_match_backend_is_attached_compiled(target) &&
        !space_match_backend_materialize_attached(
            target, eval_storage_arena(a))) {
        return space_backend_or_symbol_error(
            a, call, "AttachedCompiledSpaceMaterializeFailed");
    }

    if (is_add) {
        Arena *dst = eval_storage_arena(a);
        (void)space_admit_atom(target, dst, payload);
        return atom_unit(a);
    }

    Atom *compare_atom = space_remove_compare_atom(target, a, payload);
    if (!(target && target->universe &&
          space_remove_atom_id(target,
                               term_universe_lookup_atom_id(target->universe,
                                                            compare_atom)))) {
        space_remove(target, compare_atom);
    }
    return atom_unit(a);
}

static Atom *dispatch_native_op(Space *s, Arena *a, Atom *head, Atom **args, uint32_t nargs) {
    if (head && head->kind == ATOM_SYMBOL &&
        hyperpose_thread_barrier_head(head->sym_id, head) &&
        eval_mark_hyperpose_thread_unsafe()) {
        return NULL;
    }
    const char *head_name = head ? atom_name_cstr(head) : NULL;
    if (head && head_name && !active_surface_allowed(head_name)) {
        return NULL;
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
    if (head &&
        atom_is_symbol_id(head, g_builtin_syms.minimal_foldl_llist))
        return eval_minimal_foldl_llist(a, head, args, nargs);
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
    CettaExprIndex path_offset;
    CettaExprLen path_len;
} VisibleVarShapeRef;

#define BODY_VISIBLE_INLINE_CAP 64
#define BODY_VISIBLE_PATH_UNSET ((CettaExprIndex)UINT64_MAX)

typedef struct {
    VisibleVarRef inline_items[BODY_VISIBLE_INLINE_CAP];
    VisibleVarRef *items;
    CettaExprLen len;
    CettaExprLen cap;
} FreeVarSet;

typedef struct {
    VisibleVarShapeRef inline_items[BODY_VISIBLE_INLINE_CAP];
    VisibleVarShapeRef *items;
    CettaExprLen len;
    CettaExprLen cap;
    CettaExprIndex inline_path_items[BODY_VISIBLE_INLINE_CAP];
    CettaExprIndex *path_items;
    CettaExprLen path_len;
    CettaExprLen path_cap;
} FreeVarShapeSet;

typedef struct {
    CettaExprIndex inline_items[BODY_VISIBLE_INLINE_CAP];
    CettaExprIndex *items;
    CettaExprLen len;
    CettaExprLen cap;
} IndexPathStack;

typedef struct {
    VarId inline_ids[BODY_VISIBLE_INLINE_CAP];
    VarId *ids;
    CettaExprLen len;
    CettaExprLen cap;
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
                   sizeof(VisibleVarShapeRef) * (size_t)src->len);
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
                   sizeof(CettaExprIndex) * (size_t)src->path_len);
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

static bool index_path_stack_reserve(IndexPathStack *stack, CettaExprLen needed) {
    if (needed <= stack->cap)
        return true;
    CettaExprLen next_cap = stack->cap ? stack->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(CettaExprIndex)))
        return false;
    CettaExprIndex *next = stack->items == stack->inline_items
        ? cetta_malloc(sizeof(CettaExprIndex) * (size_t)next_cap)
        : cetta_realloc(stack->items, sizeof(CettaExprIndex) * (size_t)next_cap);
    if (stack->items == stack->inline_items && stack->len > 0)
        memcpy(next, stack->items, sizeof(CettaExprIndex) * (size_t)stack->len);
    stack->items = next;
    stack->cap = next_cap;
    return true;
}

static bool index_path_stack_push(IndexPathStack *stack, CettaExprIndex index) {
    if (!index_path_stack_reserve(stack, stack->len + 1))
        return false;
    stack->items[stack->len++] = index;
    return true;
}

static void index_path_stack_pop(IndexPathStack *stack) {
    if (stack->len > 0)
        stack->len--;
}

static bool free_var_set_reserve(FreeVarSet *set, CettaExprLen needed) {
    if (needed <= set->cap)
        return true;
    CettaExprLen next_cap = set->cap ? set->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(VisibleVarRef)))
        return false;
    VisibleVarRef *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(VisibleVarRef) * (size_t)next_cap)
        : cetta_realloc(set->items, sizeof(VisibleVarRef) * (size_t)next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(VisibleVarRef) * (size_t)set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool free_var_set_add(FreeVarSet *set, VarId var_id, SymbolId spelling) {
    for (CettaExprIndex i = 0; i < set->len; i++) {
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

static bool free_var_shape_set_reserve(FreeVarShapeSet *set, CettaExprLen needed) {
    if (needed <= set->cap)
        return true;
    CettaExprLen next_cap = set->cap ? set->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(VisibleVarShapeRef)))
        return false;
    VisibleVarShapeRef *next = set->items == set->inline_items
        ? cetta_malloc(sizeof(VisibleVarShapeRef) * (size_t)next_cap)
        : cetta_realloc(set->items, sizeof(VisibleVarShapeRef) * (size_t)next_cap);
    if (set->items == set->inline_items && set->len > 0)
        memcpy(next, set->items, sizeof(VisibleVarShapeRef) * (size_t)set->len);
    set->items = next;
    set->cap = next_cap;
    return true;
}

static bool free_var_shape_set_reserve_paths(FreeVarShapeSet *set, CettaExprLen needed) {
    if (needed <= set->path_cap)
        return true;
    CettaExprLen next_cap = set->path_cap ? set->path_cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(CettaExprIndex)))
        return false;
    CettaExprIndex *next = set->path_items == set->inline_path_items
        ? cetta_malloc(sizeof(CettaExprIndex) * (size_t)next_cap)
        : cetta_realloc(set->path_items, sizeof(CettaExprIndex) * (size_t)next_cap);
    if (set->path_items == set->inline_path_items && set->path_len > 0)
        memcpy(next, set->path_items, sizeof(CettaExprIndex) * (size_t)set->path_len);
    set->path_items = next;
    set->path_cap = next_cap;
    return true;
}

static bool free_var_shape_set_add(FreeVarShapeSet *set, uint32_t base_id,
                                   SymbolId spelling) {
    for (CettaExprIndex i = 0; i < set->len; i++) {
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

static bool free_var_shape_set_store_path(FreeVarShapeSet *set, CettaExprIndex index,
                                          const IndexPathStack *path) {
    if (index >= set->len)
        return false;
    if (!free_var_shape_set_reserve_paths(set, set->path_len + path->len))
        return false;
    set->items[index].path_offset = set->path_len;
    set->items[index].path_len = path->len;
    if (path->len > 0) {
        memcpy(set->path_items + set->path_len, path->items,
               sizeof(CettaExprIndex) * (size_t)path->len);
        set->path_len += path->len;
    }
    return true;
}

static bool free_var_shape_set_from_visible(const FreeVarSet *visible,
                                            FreeVarShapeSet *shape) {
    free_var_shape_set_init(shape);
    for (CettaExprIndex i = 0; i < visible->len; i++) {
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
                                                 CettaExprLen *found_count) {
    if (!atom || !atom_has_vars(atom) || *found_count == shape->len)
        return true;
    if (atom->kind == ATOM_VAR) {
        uint32_t base_id = var_base_id(atom->var_id);
        for (CettaExprIndex i = 0; i < shape->len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    CettaExprLen found_count = 0;
    for (CettaExprIndex i = 0; i < shape->len; i++) {
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
        h = ((h << 5) + h) ^ (uint32_t)(atom->expr.len & 0xFFFFFFFFu);
        h = ((h << 5) + h) ^ (uint32_t)(atom->expr.len >> 32);
        for (CettaExprIndex i = 0; i < atom->expr.len; i++)
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
        for (CettaExprIndex i = 0; i < lhs->expr.len; i++) {
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
        for (CettaExprIndex i = 0; i < shape->len; i++) {
            if (shape->items[i].base_id == base_id &&
                shape->items[i].spelling == atom->sym_id) {
                return free_var_set_add(out, atom->var_id, atom->sym_id);
            }
        }
        return true;
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < shape->len; i++) {
        if (shape->items[i].path_offset == BODY_VISIBLE_PATH_UNSET) {
            have_paths = false;
            break;
        }
    }
    if (have_paths) {
        free_var_set_init(out);
        for (CettaExprIndex i = 0; i < shape->len; i++) {
            const VisibleVarShapeRef *ref = &shape->items[i];
            Atom *cursor = body;
            if (ref->path_offset + ref->path_len > shape->path_len) {
                free_var_set_free(out);
                have_paths = false;
                break;
            }
            for (CettaExprIndex j = 0; j < ref->path_len; j++) {
                CettaExprIndex child_index = shape->path_items[ref->path_offset + j];
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

static bool bound_var_stack_reserve(BoundVarStack *stack, CettaExprLen needed) {
    if (needed <= stack->cap)
        return true;
    CettaExprLen next_cap = stack->cap ? stack->cap : BODY_VISIBLE_INLINE_CAP;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(VarId)))
        return false;
    VarId *next = stack->ids == stack->inline_ids
        ? cetta_malloc(sizeof(VarId) * (size_t)next_cap)
        : cetta_realloc(stack->ids, sizeof(VarId) * (size_t)next_cap);
    if (stack->ids == stack->inline_ids && stack->len > 0)
        memcpy(next, stack->ids, sizeof(VarId) * (size_t)stack->len);
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
    for (CettaExprIndex i = stack->len; i > 0; i--) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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

    CettaExprLen nargs = expr_nargs(atom);
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
            for (CettaExprIndex i = 0; i < bindings_list->expr.len; i++) {
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
        CettaExprIndex stream_idx = nargs == 6 ? 1 : 0;
        CettaExprIndex init_idx = stream_idx + 1;
        CettaExprIndex acc_idx = stream_idx + 2;
        CettaExprIndex item_idx = stream_idx + 3;
        CettaExprIndex step_idx = stream_idx + 4;
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
        CettaExprIndex stream_idx = nargs == 7 ? 1 : 0;
        CettaExprIndex init_idx = stream_idx + 1;
        CettaExprIndex acc_idx = stream_idx + 2;
        CettaExprIndex item_idx = stream_idx + 3;
        CettaExprIndex key_idx = stream_idx + 4;
        CettaExprIndex step_idx = stream_idx + 5;
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

    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
            CettaExprLen mark = bound->len;
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
    CettaExprLen mark = bound->len;
    if (bindings_list && bindings_list->kind == ATOM_EXPR) {
        for (CettaExprIndex i = 0; i < bindings_list->expr.len; i++) {
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

static bool collect_free_vars_fold(Atom *atom, CettaExprLen nargs,
                                   BoundVarStack *bound,
                                   FreeVarSet *free_vars) {
    CettaExprIndex stream_idx = nargs == 6 ? 1 : 0;
    CettaExprIndex init_idx = stream_idx + 1;
    CettaExprIndex acc_idx = stream_idx + 2;
    CettaExprIndex item_idx = stream_idx + 3;
    CettaExprIndex step_idx = stream_idx + 4;
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
    CettaExprLen mark = bound->len;
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

static bool collect_free_vars_fold_by_key(Atom *atom, CettaExprLen nargs,
                                          BoundVarStack *bound,
                                          FreeVarSet *free_vars) {
    CettaExprIndex stream_idx = nargs == 7 ? 1 : 0;
    CettaExprIndex init_idx = stream_idx + 1;
    CettaExprIndex acc_idx = stream_idx + 2;
    CettaExprIndex item_idx = stream_idx + 3;
    CettaExprIndex key_idx = stream_idx + 4;
    CettaExprIndex step_idx = stream_idx + 5;
    if (nargs == 7 && !collect_free_vars_rec(expr_arg(atom, 0), bound, free_vars))
        return false;
    if (!collect_free_vars_rec(expr_arg(atom, stream_idx), bound, free_vars) ||
        !collect_free_vars_rec(expr_arg(atom, init_idx), bound, free_vars)) {
        return false;
    }

    if (!collect_bound_pattern_ref_vars(expr_arg(atom, item_idx), bound, free_vars))
        return false;
    CettaExprLen key_mark = bound->len;
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
    CettaExprLen step_mark = bound->len;
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

    CettaExprLen nargs = expr_nargs(atom);
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
        CettaExprLen mark = bound->len;
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
        CettaExprLen mark = bound->len;
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
        CettaExprLen mark = bound->len;
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
        CettaExprLen mark = bound->len;
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

    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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

static bool bindings_project_body_visible_env(Arena *a, Atom *body,
                                              const Bindings *full, Bindings *out) {
    FreeVarSet body_vars;

    bindings_init(out);
    if (!full || full->len == 0)
        return true;
    if (!atom_contains_vars(body))
        return true;

    free_var_set_init(&body_vars);
    if (!collect_structural_vars_rec(body, &body_vars)) {
        free_var_set_free(&body_vars);
        bindings_free(out);
        return false;
    }
    if (body_vars.len == 0) {
        free_var_set_free(&body_vars);
        return true;
    }

    for (uint32_t i = 0; i < full->len; i++) {
        bool used = false;
        for (uint32_t j = 0; j < body_vars.len; j++) {
            if (body_vars.items[j].var_id == full->entries[i].var_id ||
                body_vars.items[j].spelling == full->entries[i].spelling) {
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
            free_var_set_free(&body_vars);
            bindings_free(out);
            return false;
        }
    }
    free_var_set_free(&body_vars);
    return true;
}

static void metta_eval_bind(Space *s, Arena *a, Atom *atom, int fuel,
                            OutcomeSet *os);
static void emit_singleton_visible_witness(Space *s, Arena *a, Atom *atom,
                                           Atom *expr, int fuel, OutcomeSet *os) {
    Bindings _empty;
    bindings_init(&_empty);

    ResultBindSet inner;
    rb_set_init(&inner);
    metta_eval_bind(s, a, expr, fuel, &inner);

    bool found = false;
    bool unique = true;
    Atom *preferred = NULL;
    Bindings shared_visible;
    bindings_init(&shared_visible);

    for (CettaCount i = 0; i < inner.len; i++) {
        Atom *witness = outcome_atom_materialize(a, &inner.items[i]);
        if (atom_is_empty(witness))
            continue;

        Bindings visible;
        if (!bindings_project_body_visible_env(a, expr, &inner.items[i].env, &visible)) {
            bindings_free(&shared_visible);
            rb_set_free(&inner);
            return;
        }

        if (!found) {
            bindings_move(&shared_visible, &visible);
            preferred = witness;
            found = true;
        } else {
            if (!bindings_eq(&shared_visible, &visible))
                unique = false;
            bindings_free(&visible);
            if (!unique)
                break;
        }

        if (is_true_atom(witness))
            preferred = witness;
    }

    if (!found) {
        outcome_set_add(os, atom_empty(a), &_empty);
    } else if (!unique) {
        outcome_set_add(os,
                        atom_error(a, atom,
                                   atom_symbol(a, "MultipleVisibleWitnesses")),
                        &_empty);
    } else {
        outcome_set_add(os, preferred, &shared_visible);
    }

    bindings_free(&shared_visible);
    rb_set_free(&inner);
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
    for (CettaExprIndex i = 0; i < body->expr.len; i++) {
        Atom *child = body->expr.elems[i];
        Atom *next = atom_has_vars(child)
            ? bindings_apply_projected_body_visible(visible, a, child)
            : child;
        if (!next)
            return NULL;
        if (!new_elems && next != child) {
            new_elems = arena_alloc(a, sizeof(Atom *) * body->expr.len);
            for (CettaExprIndex j = 0; j < i; j++)
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

typedef struct {
    /* This is intentionally copied by value in interpret_tuple. When
       used_builder is false, callers that copy it must rebind env to the
       copy's owned field before finishing the attempt. */
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
    assert(attempt->used_builder || attempt->env == &attempt->owned);
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
    Atom *preview = outcome_preview_atom(seed);
    if (!seed || !preview)
        return;

    if (atom_is_empty(preview) || outcome_atom_is_error(a, seed) ||
        preview->kind != ATOM_EXPR || preview->expr.len == 0) {
        outcome_set_add_prefixed_outcome(a, outcomes, seed, NULL, preserve_bindings);
        return;
    }

    Atom *normal_candidate = preview;
    if (seed->env.len != 0 || seed->env.eq_len != 0 ||
        variant_instance_present(&seed->variant)) {
        normal_candidate = outcome_atom_materialize(a, seed);
        if (!normal_candidate)
            return;
    }
    if (atom_is_constructor_normal_form(s, a, normal_candidate, fuel)) {
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

static bool query_delayed_result_apply_single_tail(EvalQueryEpisode *episode,
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
    size_t before_bytes = a ? a->live_bytes : 0;
    Arena *result_arena = a;
    if (!bindings_builder_merge_or_clone(search_context_builder(context),
                                         base_env,
                                         bindings,
                                         &attempt)) {
        return false;
    }
    if (variant && variant_instance_present(variant)) {
        variant_applied = variant_instance_materialize(result_arena, result, variant);
    } else if (episode) {
        variant_applied = atom_deep_copy(a, result);
    } else {
        variant_applied = result;
    }
    if (!variant_applied) {
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    *tail_next = variant_applied;
    *tail_type = result_eval_type_hint(declared_type, result);
    bindings_copy(tail_env, attempt.env);
    if (episode && !eval_query_episode_promote_bindings(episode, tail_env)) {
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
                                            CettaCount *visited_out) {
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
                                      Atom **tail_next,
                                      Atom **tail_type,
                                      Bindings *tail_env) {
    TableStore *table = eval_active_episode_table();
    CettaCount visited = 0;
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

static CettaCount query_equations_cached_visit(Space *s, Atom *query, Arena *a,
                                               QueryEvalVisitorCtx *query_eval) {
    CettaCount visited = 0;
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

typedef struct {
    Space *space;
    Arena *arena;
    Atom *declared_type;
    int fuel;
    const Bindings *base_env;
    bool preserve_bindings;
    SearchContext *context;
    OutcomeSet *outcomes;
} ShadowableAddAtomsQueryCtx;

static bool query_visit_shadowable_add_atoms_result(Atom *result,
                                                    const Bindings *bindings,
                                                    void *ctx) {
    ShadowableAddAtomsQueryCtx *shadow = ctx;
    BindingsMergeAttempt attempt;
    Outcome delayed;
    Atom *preview;
    Atom *head;
    bool preserve_raw = false;

    if (!shadow)
        return true;
    if (!bindings_builder_merge_or_clone(search_context_builder(shadow->context),
                                         shadow->base_env,
                                         bindings,
                                         &attempt)) {
        return true;
    }

    outcome_init(&delayed);
    delayed.atom = result;
    if (!bindings_clone(&delayed.env, attempt.env)) {
        outcome_free_fields(&delayed);
        bindings_merge_attempt_finish(search_context_builder(shadow->context),
                                      &attempt);
        return true;
    }

    preview = outcome_preview_atom(&delayed);
    if (preview && preview->kind == ATOM_EXPR && preview->expr.len > 0) {
        head = preview->expr.elems[0];
        preserve_raw =
            head && head->kind == ATOM_SYMBOL &&
            !symbol_id_is_builtin_surface(head->sym_id) &&
            !is_grounded_op(head->sym_id) &&
            !(g_library_context && g_library_context->foreign_runtime &&
              cetta_foreign_is_callable_atom(head));
    }

    if (preserve_raw) {
        outcome_set_add_prefixed_outcome(shadow->arena,
                                         shadow->outcomes,
                                         &delayed,
                                         NULL,
                                         shadow->preserve_bindings);
    } else {
        eval_delayed_outcome_for_caller(shadow->space,
                                        shadow->arena,
                                        result_eval_type_hint(
                                            shadow->declared_type, result),
                                        &delayed,
                                        shadow->fuel,
                                        shadow->preserve_bindings,
                                        shadow->outcomes);
    }
    outcome_free_fields(&delayed);
    bindings_merge_attempt_finish(search_context_builder(shadow->context),
                                  &attempt);
    return true;
}

static bool dispatch_shadowable_add_atoms_source_query(
    Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
    const Bindings *current_env, bool preserve_bindings, OutcomeSet *os) {
    if (!atom || atom->kind != ATOM_EXPR ||
        atom_head_symbol_id(atom) != g_builtin_syms.add_atoms ||
        expr_nargs(atom) != 2 || !g_registry ||
        add_atoms_public_surface_has_only_default(s)) {
        return false;
    }

    Atom *call_atom =
        (!current_env || (current_env->len == 0 && current_env->eq_len == 0))
            ? atom
            : bindings_apply_if_vars(current_env, a, atom);
    if (!call_atom || call_atom->kind != ATOM_EXPR || expr_nargs(call_atom) != 2 ||
        !add_atoms_source_shape(expr_arg(call_atom, 1), NULL, NULL, NULL)) {
        return false;
    }

    SearchContext qr_context;
    if (!search_context_init(&qr_context, current_env, NULL))
        return false;
    ShadowableAddAtomsQueryCtx shadow_ctx = {
        .space = s,
        .arena = a,
        .declared_type = type,
        .fuel = fuel,
        .base_env = current_env,
        .preserve_bindings = preserve_bindings,
        .context = &qr_context,
        .outcomes = os,
    };
    bool handled =
        query_equations_visit(s, call_atom, a,
                              query_visit_shadowable_add_atoms_result,
                              &shadow_ctx) > 0;
    search_context_free(&qr_context);
    return handled;
}

static bool query_result_apply_single_tail(EvalQueryEpisode *episode,
                                           SearchContext *context,
                                           const Bindings *base_env,
                                           Arena *a,
                                           Atom *declared_type,
                                           const QueryResult *result,
                                           Atom **tail_next,
                                           Atom **tail_type,
                                           Bindings *tail_env) {
    BindingsMergeAttempt attempt;
    size_t before_bytes = a ? a->live_bytes : 0;
    Arena *result_arena = a;
    if (!bindings_builder_merge_or_clone(search_context_builder(context),
                                         base_env,
                                         &result->bindings,
                                         &attempt)) {
        return false;
    }
    *tail_next = episode
        ? atom_deep_copy(result_arena, result->result)
        : result->result;
    if (!*tail_next) {
        bindings_merge_attempt_finish(search_context_builder(context), &attempt);
        return false;
    }
    *tail_type = result_eval_type_hint(declared_type, result->result);
    bindings_copy(tail_env, attempt.env);
    if (episode && !eval_query_episode_promote_bindings(episode, tail_env)) {
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
static bool branch_outer_env_begin(Bindings *owned,
                                   const Bindings **effective_outer,
                                   const Bindings *outer_env,
                                   const Bindings *branch_env);
static void branch_outer_env_finish(Bindings *owned,
                                    const Bindings *effective_outer);

static Atom *bindings_apply_if_vars_chain(Arena *a,
                                          const Bindings *base_env,
                                          const Bindings *extra_env,
                                          Atom *atom) {
    Atom *applied = atom;
    if (base_env && (base_env->len > 0 || base_env->eq_len > 0))
        applied = bindings_apply_if_vars(base_env, a, applied);
    if (extra_env && (extra_env->len > 0 || extra_env->eq_len > 0))
        applied = bindings_apply_if_vars(extra_env, a, applied);
    return applied;
}

static Atom *rebuild_stream_call_with_replaced_arg(Arena *a,
                                                   Atom *atom,
                                                   const Bindings *base_env,
                                                   const Bindings *extra_env,
                                                   CettaExprIndex arg_idx,
                                                   Atom *replacement) {
    Atom **elems;
    CettaExprLen nargs;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return atom;
    nargs = expr_nargs(atom);
    elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = atom->expr.elems[0];
    for (CettaExprIndex i = 0; i < nargs; i++) {
        elems[i + 1] = (i == arg_idx)
            ? replacement
            : bindings_apply_if_vars_chain(a, base_env, extra_env, expr_arg(atom, i));
    }
    return atom_expr(a, elems, atom->expr.len);
}

static void eval_stream_call_non_ground_arg(Space *s, Arena *a, Atom *atom,
                                            const Bindings *base_env,
                                            CettaExprIndex arg_idx,
                                            CettaExprIndex stream_arg_idx,
                                            int fuel,
                                            bool preserve_bindings,
                                            OutcomeSet *os) {
    Atom *applied_arg;
    Bindings empty;
    OutcomeSet values;

    if (!atom || atom->kind != ATOM_EXPR || arg_idx >= expr_nargs(atom))
        return;

    bindings_init(&empty);
    applied_arg = bindings_apply_if_vars(base_env, a, expr_arg(atom, arg_idx));
    outcome_set_init(&values);
    metta_eval_bind(s, a, applied_arg, fuel, &values);

    for (CettaCount i = 0; i < values.len; i++) {
        Atom *value = outcome_atom_materialize(a, &values.items[i]);
        Atom *ready_call;
        bool progressed;
        if (!value)
            continue;
        if (atom_is_empty_or_error(value)) {
            outcome_set_add(os, value, &empty);
            continue;
        }
        ready_call = rebuild_stream_call_with_replaced_arg(
            a, atom, base_env, &values.items[i].env, arg_idx, value);
        progressed = !atom_eq(value, applied_arg) ||
                     values.items[i].env.len > 0 ||
                     values.items[i].env.eq_len > 0;
        if (progressed) {
            eval_for_current_caller(s, a, NULL, ready_call, fuel, &empty, &empty,
                                    preserve_bindings, os);
        } else {
            emit_policy_stream_call_inert(s, a, ready_call, stream_arg_idx,
                                          &empty, fuel, os);
        }
    }

    outcome_set_free(&values);
}

typedef struct {
    OrderedOutcomeVisitor visitor;
    void *ctx;
    CettaCount *visited;
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
    for (CettaCount i = 0; i < inner->len; i++) {
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
                                           CettaCount *visited) {
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

typedef struct {
    Atom **items;
    CettaExprLen len;
    CettaExprLen cap;
} DirectWalkStack;

static bool direct_walk_stack_push(DirectWalkStack *stack, Atom *atom) {
    if (stack->len >= stack->cap) {
        CettaExprLen next_cap = stack->cap ? stack->cap * 2 : 32;
        if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(Atom *)))
            return false;
        Atom **next = cetta_realloc(stack->items, sizeof(Atom *) * (size_t)next_cap);
        if (!next)
            return false;
        stack->items = next;
        stack->cap = next_cap;
    }
    stack->items[stack->len++] = atom;
    return true;
}

static Atom *direct_walk_stack_pop(DirectWalkStack *stack) {
    return stack->items[--stack->len];
}

static void direct_walk_stack_free(DirectWalkStack *stack) {
    free(stack->items);
    stack->items = NULL;
    stack->len = 0;
    stack->cap = 0;
}

static bool direct_walk_stack_push_superpose(DirectWalkStack *stack, Atom *list) {
    for (CettaExprIndex i = list->expr.len; i > 0; i--) {
        if (!direct_walk_stack_push(stack, list->expr.elems[i - 1]))
            return false;
    }
    return true;
}

static bool hyperpose_static_branch_list(Atom *atom, Atom **list_out) {
    if (list_out) *list_out = NULL;
    if (!atom || !expr_head_is_id(atom, g_builtin_syms.hyperpose) ||
        expr_nargs(atom) != 1) {
        return false;
    }
    Atom *list = expr_arg(atom, 0);
    if (!list || list->kind != ATOM_EXPR)
        return false;
    if (list_out) *list_out = list;
    return true;
}

static bool direct_outcome_walk_supported(Space *s, Arena *a, Atom *atom, int fuel) {
    DirectWalkStack stack = {0};
    if (!direct_walk_stack_push(&stack, atom))
        return false;

    while (stack.len > 0) {
        Atom *current = direct_walk_stack_pop(&stack);
        Atom *bound = registry_lookup_atom(current);
        if (bound)
            current = bound;
        current = materialize_runtime_token(s, a, current);
        if (atom_is_empty(current) || atom_is_error(current) ||
            atom_eval_is_immediate_value(current, fuel)) {
            continue;
        }
        if (direct_outcome_walk_mork_match_supported(a, current))
            continue;
        if ((expr_head_is_id(current, g_builtin_syms.superpose) &&
             expr_nargs(current) == 1) ||
            hyperpose_static_branch_list(current, NULL)) {
            Atom *list = expr_arg(current, 0);
            if (expr_head_is_id(current, g_builtin_syms.hyperpose) &&
                !active_surface_allowed("hyperpose")) {
                direct_walk_stack_free(&stack);
                return false;
            }
            if (list->kind != ATOM_EXPR ||
                !direct_walk_stack_push_superpose(&stack, list)) {
                direct_walk_stack_free(&stack);
                return false;
            }
            continue;
        }
        direct_walk_stack_free(&stack);
        return false;
    }

    direct_walk_stack_free(&stack);
    return true;
}

static bool direct_outcome_walk(Space *s, Arena *a, Atom *atom, int fuel,
                                OrderedOutcomeVisitor visitor, void *ctx,
                                CettaCount *visited) {
    Bindings empty;
    bindings_init(&empty);

    DirectWalkStack stack = {0};
    if (!direct_walk_stack_push(&stack, atom))
        return false;

    while (stack.len > 0) {
        Atom *current = direct_walk_stack_pop(&stack);
        Atom *bound = registry_lookup_atom(current);
        if (bound)
            current = bound;
        current = materialize_runtime_token(s, a, current);
        if (atom_is_empty(current))
            continue;
        if (atom_is_error(current) || atom_eval_is_immediate_value(current, fuel)) {
            (*visited)++;
            if (!visitor(a, current, &empty, ctx)) {
                direct_walk_stack_free(&stack);
                return false;
            }
            continue;
        }
        if (direct_outcome_walk_mork_match_supported(a, current)) {
            if (!direct_outcome_walk_mork_match(s, a, current, fuel, visitor, ctx,
                                                visited)) {
                direct_walk_stack_free(&stack);
                return false;
            }
            continue;
        }
        if ((expr_head_is_id(current, g_builtin_syms.superpose) &&
             expr_nargs(current) == 1) ||
            hyperpose_static_branch_list(current, NULL)) {
            Atom *list = expr_arg(current, 0);
            if (expr_head_is_id(current, g_builtin_syms.hyperpose) &&
                !active_surface_allowed("hyperpose")) {
                direct_walk_stack_free(&stack);
                return false;
            }
            if (list->kind != ATOM_EXPR ||
                !direct_walk_stack_push_superpose(&stack, list)) {
                direct_walk_stack_free(&stack);
                return false;
            }
            continue;
        }
        direct_walk_stack_free(&stack);
        return false;
    }

    direct_walk_stack_free(&stack);
    return true;
}

static CettaCount outcome_set_visit_ordered(Arena *a, OutcomeSet *inner,
                                            CettaSearchPolicyOrder order,
                                            OrderedOutcomeVisitor visitor,
                                            void *ctx) {
    CettaCount visited = 0;
    bool sorted_order = order == CETTA_SEARCH_POLICY_ORDER_LEX ||
                        order == CETTA_SEARCH_POLICY_ORDER_SHORTLEX;

    if (sorted_order) {
        CettaCount candidate_cap = inner->len > 0 ? inner->len : 1;
        SearchEmitCandidate *candidates =
            arena_alloc(a, sizeof(SearchEmitCandidate) * (size_t)candidate_cap);
        CettaCount candidate_len = 0;
        for (CettaCount i = 0; i < inner->len; i++) {
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
        for (CettaCount i = 0; i < candidate_len; i++) {
            visited++;
            if (!visitor(a, candidates[i].raw_atom, candidates[i].env, ctx))
                break;
        }
        return visited;
    }

    if (order == CETTA_SEARCH_POLICY_ORDER_REVERSE) {
        for (CettaCount i = inner->len; i > 0; i--) {
            Atom *r = outcome_atom_materialize(a, &inner->items[i - 1]);
            if (atom_is_empty(r))
                continue;
            visited++;
            if (!visitor(a, r, &inner->items[i - 1].env, ctx))
                break;
        }
        return visited;
    }

    for (CettaCount i = 0; i < inner->len; i++) {
        Atom *r = outcome_atom_materialize(a, &inner->items[i]);
        if (atom_is_empty(r))
            continue;
        visited++;
        if (!visitor(a, r, &inner->items[i].env, ctx))
            break;
    }
    return visited;
}

static CettaCount metta_eval_bind_visit(Space *s, Arena *a, Atom *atom, int fuel,
                                        CettaSearchPolicyOrder order,
                                        OrderedOutcomeVisitor visitor,
                                        void *ctx) {
    if (order == CETTA_SEARCH_POLICY_ORDER_NATIVE &&
        direct_outcome_walk_supported(s, a, atom, fuel)) {
        CettaCount visited = 0;
        direct_outcome_walk(s, a, atom, fuel, visitor, ctx, &visited);
        return visited;
    }
    OutcomeSet inner;
    outcome_set_init(&inner);
    metta_eval_bind(s, a, atom, fuel, &inner);
    CettaCount visited = outcome_set_visit_ordered(a, &inner, order, visitor, ctx);
    outcome_set_free(&inner);
    return visited;
}

typedef struct {
    Atom **items;
    CettaCount len;
    CettaCount cap;
} StreamItemBuffer;

static bool stream_item_buffer_push(StreamItemBuffer *buffer, Atom *item) {
    if (buffer->len >= buffer->cap) {
        CettaCount next_cap;
        Atom **next;
        if (!eval_next_capacity(buffer->cap, buffer->len + 1u,
                                sizeof(Atom *), &next_cap))
            return false;
        next = cetta_realloc(buffer->items, sizeof(Atom *) * (size_t)next_cap);
        if (!next)
            return false;
        buffer->items = next;
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

static bool symbol_name_has_prefix(Atom *atom, const char *prefix) {
    if (!atom || atom->kind != ATOM_SYMBOL || !prefix)
        return false;
    const char *name = atom_name_cstr(atom);
    if (!name)
        return false;
    size_t prefix_len = strlen(prefix);
    return strncmp(name, prefix, prefix_len) == 0;
}

static bool symbol_name_equals(Atom *atom, const char *name) {
    if (!atom || atom->kind != ATOM_SYMBOL || !name)
        return false;
    const char *actual = atom_name_cstr(atom);
    return actual && strcmp(actual, name) == 0;
}

static bool hyperpose_external_unsafe_head(Atom *head) {
    return symbol_name_has_prefix(head, "mork:") ||
           symbol_name_has_prefix(head, "mm2:") ||
           symbol_name_equals(head, "fs:write-text") ||
           symbol_name_equals(head, "fs:append-text") ||
           symbol_name_equals(head, "system:exit") ||
           symbol_name_has_prefix(head, "foreign:");
}

static bool hyperpose_internal_unsafe_head(SymbolId head_id, Atom *head) {
    if (head_id == g_builtin_syms.lib_system_exit_with_code ||
        head_id == g_builtin_syms.lib_fs_write_text ||
        head_id == g_builtin_syms.lib_fs_append_text) {
        return true;
    }
    return symbol_name_has_prefix(head, "__cetta_lib_mork_") ||
           symbol_name_has_prefix(head, "__cetta_lib_mm2_");
}

static bool hyperpose_global_shared_head(SymbolId head_id) {
    if (head_id == g_builtin_syms.pragma_bang ||
        head_id == g_builtin_syms.import_bang ||
        head_id == g_builtin_syms.include ||
        head_id == g_builtin_syms.register_module_bang ||
        head_id == g_builtin_syms.git_module_bang ||
        head_id == g_builtin_syms.mod_space_bang ||
        head_id == g_builtin_syms.module_inventory_bang ||
        head_id == g_builtin_syms.space_set_backend_bang ||
        head_id == g_builtin_syms.space_set_match_backend_bang ||
        head_id == g_builtin_syms.call_native) {
        return true;
    }
    return false;
}

static bool hyperpose_thread_barrier_head(SymbolId head_id, Atom *head) {
    return hyperpose_global_shared_head(head_id) ||
           hyperpose_external_unsafe_head(head) ||
           hyperpose_internal_unsafe_head(head_id, head);
}

static bool hyperpose_atom_is_thread_local_resource(Atom *atom) {
    if (!atom)
        return false;
    if (atom_is_symbol_id(atom, g_builtin_syms.capture))
        return true;
    return atom->kind == ATOM_GROUNDED &&
           (atom->ground.gkind == GV_SPACE ||
            atom->ground.gkind == GV_STATE ||
            atom->ground.gkind == GV_CAPTURE ||
            atom->ground.gkind == GV_FOREIGN);
}

static bool hyperpose_atom_has_thread_local_resource(Atom *atom) {
    if (!atom)
        return false;
    if (hyperpose_atom_is_thread_local_resource(atom))
        return true;
    if (atom->kind == ATOM_EXPR) {
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (hyperpose_atom_has_thread_local_resource(atom->expr.elems[i]))
                return true;
        }
    }
    return false;
}

static bool hyperpose_atom_parent_portable(Atom *atom) {
    return !hyperpose_atom_has_thread_local_resource(atom);
}

#define HYPERPOSE_THREAD_ELIGIBILITY_STACK_MAX 128

typedef struct {
    SymbolId visiting[HYPERPOSE_THREAD_ELIGIBILITY_STACK_MAX];
    uint32_t visiting_len;
} HyperposeThreadEligibilityCtx;

static bool hyperpose_thread_eligibility_visiting(
    const HyperposeThreadEligibilityCtx *ctx, SymbolId head) {
    for (uint32_t i = 0; i < ctx->visiting_len; i++) {
        if (ctx->visiting[i] == head)
            return true;
    }
    return false;
}

static bool hyperpose_thread_eligibility_push(
    HyperposeThreadEligibilityCtx *ctx, SymbolId head) {
    if (hyperpose_thread_eligibility_visiting(ctx, head))
        return true;
    if (ctx->visiting_len >= HYPERPOSE_THREAD_ELIGIBILITY_STACK_MAX)
        return false;
    ctx->visiting[ctx->visiting_len++] = head;
    return true;
}

static void hyperpose_thread_eligibility_pop(
    HyperposeThreadEligibilityCtx *ctx, SymbolId head) {
    if (ctx->visiting_len > 0 &&
        ctx->visiting[ctx->visiting_len - 1] == head) {
        ctx->visiting_len--;
    }
}

static bool hyperpose_branch_thread_eligible_rec(
    Space *s, Atom *atom, HyperposeThreadEligibilityCtx *ctx);

static bool hyperpose_equation_bodies_thread_eligible_for_head(
    Space *s, SymbolId head_id, HyperposeThreadEligibilityCtx *ctx) {
    if (!s || head_id == SYMBOL_ID_NONE)
        return true;
    if (hyperpose_thread_eligibility_visiting(ctx, head_id))
        return true;
    if (!hyperpose_thread_eligibility_push(ctx, head_id))
        return false;

    bool ok = true;
    CettaCount len = space_length64(s);
    for (CettaCount i = 0; i < len && ok; i++) {
        Atom *eq = space_get_at(s, i);
        if (!expr_head_is_id(eq, g_builtin_syms.equals) || expr_nargs(eq) != 2)
            continue;
        Atom *lhs = expr_arg(eq, 0);
        Atom *rhs = expr_arg(eq, 1);
        SymbolId lhs_head = atom_head_symbol_id(lhs);
        if (lhs_head == head_id) {
            ok = hyperpose_branch_thread_eligible_rec(s, rhs, ctx);
        }
    }

    hyperpose_thread_eligibility_pop(ctx, head_id);
    return ok;
}

static bool hyperpose_branch_thread_eligible_rec(
    Space *s, Atom *atom, HyperposeThreadEligibilityCtx *ctx) {
    if (!atom)
        return true;
    if (hyperpose_atom_is_thread_local_resource(atom))
        return false;
    if (atom_has_registry_refs(atom))
        return false;
    if (atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return true;
    Atom *head = atom->expr.elems[0];
    SymbolId head_id = atom_head_symbol_id(atom);
    if (hyperpose_thread_barrier_head(head_id, head))
        return false;
    if (!hyperpose_equation_bodies_thread_eligible_for_head(s, head_id, ctx))
        return false;
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        if (!hyperpose_branch_thread_eligible_rec(s, atom->expr.elems[i], ctx))
            return false;
    }
    return true;
}

static bool hyperpose_branch_thread_eligible(Space *s, Atom *atom) {
    HyperposeThreadEligibilityCtx ctx = {0};
    return hyperpose_branch_thread_eligible_rec(s, atom, &ctx);
}

static bool hyperpose_all_branches_thread_eligible(Space *s, Atom *branches) {
    if (!branches || branches->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
        if (!hyperpose_branch_thread_eligible(s, branches->expr.elems[i]))
            return false;
    }
    return true;
}

static bool hyperpose_all_branches_thread_eligible_without_capture(Space *s,
                                                                   Atom *branches) {
    if (!hyperpose_all_branches_thread_eligible(s, branches))
        return false;
    for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        if (atom_has_vars(branch) || atom_has_registry_refs(branch))
            return false;
    }
    return true;
}

typedef struct {
    Atom **items;
    CettaCount len;
    CettaCount cap;
} HyperposeThreadResultBuffer;

typedef struct {
    Space **items;
    CettaCount len;
    CettaCount cap;
} HyperposeOwnedSpaces;

typedef struct HyperposeThreadBranch {
    CettaExprIndex index;
    Atom *branch;
    Arena persistent_arena;
    Arena eval_arena;
    TermUniverse term_universe;
    Space *space;
    Registry registry;
    HyperposeOwnedSpaces owned_spaces;
    HyperposeThreadResultBuffer results;
    bool has_success;
    bool prepared;
} HyperposeThreadBranch;

typedef struct {
    HyperposeThreadBranch *branches;
    CettaCount branch_count;
    int fuel;
    bool preserve_bindings;
    bool first_success;
    CettaLibraryContext *library_context;
    CettaParallelExecutor parallel;
    _Atomic bool cancel_requested;
    _Atomic bool unsafe_result;
    _Atomic int winner_index;
} HyperposeThreadRun;

static bool hyperpose_result_buffer_push(HyperposeThreadResultBuffer *buffer,
                                         Atom *atom) {
    if (buffer->len >= buffer->cap) {
        CettaCount next_cap;
        Atom **next;
        if (!eval_next_capacity(buffer->cap, buffer->len + 1u,
                                sizeof(Atom *), &next_cap))
            return false;
        next = cetta_realloc(buffer->items, sizeof(Atom *) * (size_t)next_cap);
        if (!next)
            return false;
        buffer->items = next;
        buffer->cap = next_cap;
    }
    buffer->items[buffer->len++] = atom;
    return true;
}

static void hyperpose_result_buffer_free(HyperposeThreadResultBuffer *buffer) {
    free(buffer->items);
    buffer->items = NULL;
    buffer->len = 0;
    buffer->cap = 0;
}

static bool hyperpose_owned_spaces_push(HyperposeOwnedSpaces *spaces,
                                        Space *space) {
    if (spaces->len >= spaces->cap) {
        CettaCount next_cap;
        Space **next;
        if (!eval_next_capacity(spaces->cap, spaces->len + 1u,
                                sizeof(Space *), &next_cap))
            return false;
        next = cetta_realloc(spaces->items, sizeof(Space *) * (size_t)next_cap);
        if (!next)
            return false;
        spaces->items = next;
        spaces->cap = next_cap;
    }
    spaces->items[spaces->len++] = space;
    return true;
}

static Atom *hyperpose_clone_atom_materialized(Arena *owner, Atom *src) {
    if (!owner || !src)
        return NULL;
    switch (src->kind) {
    case ATOM_SYMBOL:
        return atom_symbol_id(owner, src->sym_id);
    case ATOM_VAR:
        return atom_var_with_spelling(owner, src->sym_id, src->var_id);
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            return atom_int(owner, src->ground.ival);
        case GV_FLOAT:
            return atom_float(owner, src->ground.fval);
        case GV_BOOL:
            return atom_bool(owner, src->ground.bval);
        case GV_STRING:
            return atom_string(owner, src->ground.sval);
        case GV_BIGINT:
            return atom_bigint(owner, atom_bigint_cstr(src));
        case GV_RATIONAL:
            return atom_rational(owner, atom_rational_cstr(src));
        case GV_STATE: {
            StateCell *src_cell = (StateCell *)src->ground.ptr;
            StateCell *dst_cell = arena_alloc(owner, sizeof(StateCell));
            dst_cell->value = NULL;
            dst_cell->content_type = NULL;
            dst_cell->payload_owner_epoch = 0;
            dst_cell->payload_export_owner_epoch = 0;
            if (src_cell) {
                if (src_cell->value) {
                    dst_cell->value =
                        hyperpose_clone_atom_materialized(owner, src_cell->value);
                    if (!dst_cell->value)
                        return NULL;
                }
                if (src_cell->content_type) {
                    dst_cell->content_type =
                        hyperpose_clone_atom_materialized(owner,
                                                          src_cell->content_type);
                    if (!dst_cell->content_type)
                        return NULL;
                }
            }
            return atom_state(owner, dst_cell);
        }
        case GV_SPACE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return NULL;
        }
        return NULL;
    case ATOM_EXPR: {
        Atom **elems = arena_alloc(owner, sizeof(Atom *) * src->expr.len);
        for (CettaExprIndex i = 0; i < src->expr.len; i++) {
            elems[i] =
                hyperpose_clone_atom_materialized(owner, src->expr.elems[i]);
            if (!elems[i])
                return NULL;
        }
        return atom_expr(owner, elems, src->expr.len);
    }
    }
    return NULL;
}

static Space *hyperpose_clone_space_materialized(Space *src, Arena *owner,
                                                 TermUniverse *universe) {
    if (!src || !owner || !universe)
        return NULL;
    Space *clone = cetta_malloc(sizeof(Space));
    space_init_with_universe(clone, universe);
    clone->kind = src->kind;
    CettaCount len = space_length64(src);
    for (CettaCount i = 0; i < len; i++) {
        Atom *item = space_get_at(src, i);
        if (!item)
            continue;
        Atom *cloned_item = hyperpose_clone_atom_materialized(owner, item);
        if (!cloned_item) {
            space_free(clone);
            free(clone);
            return NULL;
        }
        space_add(clone, cloned_item);
    }
    return clone;
}

static bool hyperpose_clone_registry(Registry *dst, Registry *src,
                                     Space *src_root, Space *root_clone,
                                     Arena *owner,
                                     HyperposeOwnedSpaces *owned_spaces) {
    registry_init(dst);
    bool saw_self = false;
    if (src) {
        for (CettaCount i = 0; i < src->len; i++) {
            SymbolId key = src->entries[i].key;
            Atom *value = src->entries[i].value;
            if (key == SYMBOL_ID_NONE || !value)
                continue;
            if (key == g_builtin_syms.self)
                saw_self = true;
            if (value->kind == ATOM_GROUNDED &&
                value->ground.gkind == GV_SPACE) {
                Space *space_value = (Space *)value->ground.ptr;
                Space *space_clone = space_value == src_root
                    ? root_clone
                    : hyperpose_clone_space_materialized(
                          space_value, owner, root_clone->native.universe);
                if (!space_clone)
                    return false;
                if (space_clone != root_clone &&
                    !hyperpose_owned_spaces_push(owned_spaces, space_clone)) {
                    space_free(space_clone);
                    free(space_clone);
                    return false;
                }
                Atom *registry_space_atom = atom_space(owner, space_clone);
                cetta_provenance_assert_not_transient(
                    registry_space_atom, "hyperpose.registry.space");
                registry_bind_id(dst, key, registry_space_atom);
            } else {
                Atom *cloned_value =
                    hyperpose_clone_atom_materialized(owner, value);
                if (!cloned_value)
                    return false;
                cetta_provenance_assert_not_transient(
                    cloned_value, "hyperpose.registry.value");
                registry_bind_id(dst, key, cloned_value);
            }
        }
    }
    if (!saw_self) {
        Atom *self_value = atom_space(owner, root_clone);
        cetta_provenance_assert_not_transient(
            self_value, "hyperpose.registry.self");
        registry_bind_id(dst, g_builtin_syms.self, self_value);
    }
    return true;
}

static bool hyperpose_prepare_thread_branch(HyperposeThreadBranch *branch,
                                            CettaExprIndex index,
                                            Space *source_space,
                                            Registry *source_registry,
                                            Atom *source_branch) {
#define HYPERPOSE_THREAD_PERSISTENT_ARENA_RESERVE (2u * ARENA_BLOCK_SIZE)
#define HYPERPOSE_THREAD_EVAL_ARENA_RESERVE (4u * ARENA_BLOCK_SIZE)
    memset(branch, 0, sizeof(*branch));
    branch->index = index;
    arena_init(&branch->persistent_arena);
    arena_reserve(&branch->persistent_arena,
                  HYPERPOSE_THREAD_PERSISTENT_ARENA_RESERVE);
    arena_set_hashcons(&branch->persistent_arena, NULL);
    arena_set_runtime_kind(&branch->persistent_arena,
                           CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&branch->eval_arena);
    arena_reserve(&branch->eval_arena, HYPERPOSE_THREAD_EVAL_ARENA_RESERVE);
    arena_set_hashcons(&branch->eval_arena, NULL);
    arena_set_runtime_kind(&branch->eval_arena,
                           CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&branch->term_universe);
    term_universe_set_persistent_arena(&branch->term_universe,
                                       &branch->persistent_arena);
    branch->space =
        hyperpose_clone_space_materialized(source_space,
                                          &branch->persistent_arena,
                                          &branch->term_universe);
    if (!branch->space)
        return false;
    if (!hyperpose_owned_spaces_push(&branch->owned_spaces, branch->space))
        return false;
    if (!hyperpose_clone_registry(&branch->registry, source_registry,
                                  source_space, branch->space,
                                  &branch->persistent_arena,
                                  &branch->owned_spaces)) {
        return false;
    }
    branch->branch =
        hyperpose_clone_atom_materialized(&branch->persistent_arena,
                                          source_branch);
    if (!branch->branch)
        return false;
    branch->prepared = true;
    return true;
}

static void hyperpose_thread_branch_free(HyperposeThreadBranch *branch) {
    if (!branch)
        return;
    hyperpose_result_buffer_free(&branch->results);
    for (CettaCount i = 0; i < branch->owned_spaces.len; i++) {
        if (branch->owned_spaces.items[i]) {
            space_free(branch->owned_spaces.items[i]);
            free(branch->owned_spaces.items[i]);
        }
    }
    free(branch->owned_spaces.items);
    branch->owned_spaces.items = NULL;
    branch->owned_spaces.len = 0;
    branch->owned_spaces.cap = 0;
    registry_free(&branch->registry);
    term_universe_free(&branch->term_universe);
    arena_free(&branch->eval_arena);
    arena_free(&branch->persistent_arena);
    memset(branch, 0, sizeof(*branch));
}

static void hyperpose_eval_branch_in_thread(HyperposeThreadRun *run,
                                            HyperposeThreadBranch *branch) {
    if (atomic_load_explicit(&run->unsafe_result, memory_order_acquire))
        return;
    if (run->first_success &&
        atomic_load_explicit(&run->cancel_requested, memory_order_acquire))
        return;

    Registry *prev_registry = g_registry;
    Space *prev_root_space = g_eval_root_space;
    Arena *prev_fallback_persistent =
        g_eval_fallback_universe.persistent_arena;
    CettaLibraryContext *prev_library_context = g_library_context;
    _Atomic bool *prev_cancel = eval_set_cancel_token(&run->cancel_requested);
    _Atomic bool *prev_unsafe =
        eval_set_hyperpose_thread_unsafe_token(&run->unsafe_result);

    g_registry = &branch->registry;
    g_eval_root_space = branch->space;
    g_library_context = run->library_context;
    term_universe_set_persistent_arena(&g_eval_fallback_universe,
                                       &branch->persistent_arena);
    eval_release_outcome_variant_bank();

    Bindings empty;
    bindings_init(&empty);
    OutcomeSet outcomes;
    outcome_set_init(&outcomes);
    eval_for_current_caller(branch->space, &branch->eval_arena, NULL,
                            branch->branch, run->fuel, &empty, &empty,
                            run->preserve_bindings, &outcomes);
    for (CettaCount i = 0; i < outcomes.len; i++) {
        Atom *candidate =
            outcome_atom_materialize(&branch->eval_arena, &outcomes.items[i]);
        if (atom_is_empty(candidate))
            continue;
        if (!hyperpose_atom_parent_portable(candidate)) {
            atomic_store_explicit(&run->unsafe_result, true, memory_order_release);
            atomic_store_explicit(&run->cancel_requested, true, memory_order_release);
            cetta_parallel_executor_cancel(&run->parallel);
            break;
        }
        bool success = !atom_is_error(candidate);
        Atom *stable = atom_deep_copy(&branch->persistent_arena, candidate);
        if (!hyperpose_result_buffer_push(&branch->results, stable)) {
            atomic_store_explicit(&run->unsafe_result, true, memory_order_release);
            atomic_store_explicit(&run->cancel_requested, true, memory_order_release);
            cetta_parallel_executor_cancel(&run->parallel);
            break;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HYPERPOSE_RESULT_EMITTED);
        if (success)
            branch->has_success = true;
        if (success && run->first_success) {
            int expected = -1;
            if (atomic_compare_exchange_strong_explicit(
                    &run->winner_index, &expected, (int)branch->index,
                    memory_order_acq_rel, memory_order_acquire)) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_HYPERPOSE_CANCEL_REQUEST);
                atomic_store_explicit(&run->cancel_requested, true,
                                      memory_order_release);
                cetta_parallel_executor_cancel(&run->parallel);
            }
            break;
        }
    }
    outcome_set_free(&outcomes);
    eval_release_temporary_spaces();
    if (atomic_load_explicit(&run->unsafe_result, memory_order_acquire))
        cetta_parallel_executor_cancel(&run->parallel);

    eval_set_hyperpose_thread_unsafe_token(prev_unsafe);
    eval_set_cancel_token(prev_cancel);
    g_library_context = prev_library_context;
    g_registry = prev_registry;
    g_eval_root_space = prev_root_space;
    term_universe_set_persistent_arena(&g_eval_fallback_universe,
                                       prev_fallback_persistent);
}

static void hyperpose_worker_enter(CettaParallelWorker *worker, void *user) {
    (void)worker;
    (void)user;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HYPERPOSE_WORKER_STARTED);
}

void eval_cleanup_owned_new_spaces_for_current_thread(void);

static void hyperpose_worker_leave(CettaParallelWorker *worker, void *user) {
    (void)worker;
    (void)user;
    /* Threaded hyperpose branches evaluate inside private cloned arenas. Any
       `new-space` they create is branch-local and must have its internals
       released before the branch arena is torn down; draining those pointers
       into session-wide teardown bookkeeping leaves stale arena-owned structs
       behind for later cleanup. */
    eval_cleanup_owned_new_spaces_for_current_thread();
    eval_profiled_type_cache_free_for_current_thread();
    bindings_thread_cache_free();
}

static bool hyperpose_thread_task(CettaParallelWorker *worker, void *task,
                                  void *user) {
    (void)worker;
    HyperposeThreadRun *run = user;
    HyperposeThreadBranch *branch = task;
    if (!run || !branch)
        return false;
    if (atomic_load_explicit(&run->unsafe_result, memory_order_acquire))
        return true;
    if (run->first_success &&
        atomic_load_explicit(&run->cancel_requested, memory_order_acquire))
        return true;
    hyperpose_eval_branch_in_thread(run, branch);
    return true;
}

static bool hyperpose_threaded_stream(Space *s, Arena *a, Atom *stream_expr,
                                      int fuel, bool bounded, int64_t limit,
                                      bool preserve_bindings, OutcomeSet *os) {
#define HYPERPOSE_WORKER_STACK_BYTES (16u * 1024u * 1024u)
    Atom *branches_expr = NULL;
    if (!hyperpose_static_branch_list(stream_expr, &branches_expr))
        return false;
    if (!active_surface_allowed("hyperpose"))
        return false;
    if (preserve_bindings)
        return false;

    int64_t configured_threads =
        eval_option_int_or_default("num-threads", 1);
    if (configured_threads <= 1)
        return false;
    if (configured_threads > 1024)
        configured_threads = 1024;

    CettaCount branch_count = branches_expr->expr.len;
    if (branch_count == 0)
        return false;
    if (bounded && limit > 1) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HYPERPOSE_SELECT_K_RUN);
        return false;
    }
    if (!hyperpose_all_branches_thread_eligible_without_capture(s, branches_expr)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_HYPERPOSE_COOPERATIVE_FALLBACK);
        return false;
    }

    HyperposeThreadBranch *branches =
        calloc(branch_count, sizeof(HyperposeThreadBranch));
    if (!branches)
        return false;
    bool prepared = true;
    for (CettaCount i = 0; i < branch_count; i++) {
        if (!hyperpose_prepare_thread_branch(&branches[i], i, s, g_registry,
                                             branches_expr->expr.elems[i])) {
            prepared = false;
            break;
        }
    }
    if (!prepared) {
        for (CettaCount i = 0; i < branch_count; i++)
            hyperpose_thread_branch_free(&branches[i]);
        free(branches);
        return false;
    }

    uint32_t thread_count = (uint32_t)configured_threads;
    if (thread_count > branch_count)
        thread_count = (uint32_t)branch_count;

    HyperposeThreadRun run = {
        .branches = branches,
        .branch_count = branch_count,
        .fuel = fuel,
        .preserve_bindings = preserve_bindings,
        .first_success = bounded && limit == 1,
        .library_context = g_library_context,
    };
    atomic_init(&run.cancel_requested, false);
    atomic_init(&run.unsafe_result, false);
    atomic_init(&run.winner_index, -1);

    CettaParallelExecutorConfig config = {
        .thread_count = thread_count,
        .stack_size_bytes = HYPERPOSE_WORKER_STACK_BYTES,
        .user = &run,
        .task_fn = hyperpose_thread_task,
        .worker_enter = hyperpose_worker_enter,
        .worker_leave = hyperpose_worker_leave,
        .worker_failure_message = "hyperpose threaded worker failed",
    };
    if (!cetta_parallel_executor_init(&run.parallel, &config)) {
        for (CettaCount i = 0; i < branch_count; i++)
            hyperpose_thread_branch_free(&branches[i]);
        free(branches);
        return false;
    }

    bool pushed = true;
    for (CettaCount i = 0; i < branch_count; i++) {
        if (!cetta_parallel_executor_push(&run.parallel, &branches[i])) {
            pushed = false;
            break;
        }
    }

    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HYPERPOSE_THREADED_RUN);
    if (run.first_success)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_HYPERPOSE_ONCE_RUN);

    bool run_ok = pushed && cetta_parallel_executor_run(&run.parallel);

    bool unsafe =
        atomic_load_explicit(&run.unsafe_result, memory_order_acquire);
    cetta_parallel_executor_free(&run.parallel);

    if (!run_ok || unsafe) {
        cetta_runtime_stats_inc(
            unsafe ? CETTA_RUNTIME_COUNTER_HYPERPOSE_COOPERATIVE_FALLBACK
                   : CETTA_RUNTIME_COUNTER_HYPERPOSE_FALLBACK_THREAD_LIMIT);
        for (CettaCount i = 0; i < branch_count; i++)
            hyperpose_thread_branch_free(&branches[i]);
        free(branches);
        return false;
    }

    Bindings empty;
    bindings_init(&empty);
    if (run.first_success) {
        int winner =
            atomic_load_explicit(&run.winner_index, memory_order_acquire);
        Atom *selected_atom = NULL;
        if (winner >= 0 && (CettaCount)winner < branch_count) {
            HyperposeThreadBranch *branch = &branches[winner];
            for (CettaCount i = 0; i < branch->results.len; i++) {
                if (!atom_is_error(branch->results.items[i])) {
                    selected_atom = branch->results.items[i];
                    break;
                }
            }
        }
        if (!selected_atom) {
            for (CettaCount i = 0; i < branch_count && !selected_atom; i++) {
                if (branches[i].results.len > 0)
                    selected_atom = branches[i].results.items[0];
            }
        }
        outcome_set_add(os,
                        selected_atom ? atom_deep_copy(a, selected_atom)
                                      : atom_empty(a),
                        &empty);
    } else {
        bool has_success = false;
        CettaCount result_count = 0;
        for (CettaCount i = 0; i < branch_count; i++) {
            result_count += branches[i].results.len;
            if (branches[i].has_success)
                has_success = true;
        }
        Atom **items =
            arena_alloc(a, sizeof(Atom *) * (result_count ? result_count : 1));
        CettaCount len = 0;
        for (CettaCount i = 0; i < branch_count; i++) {
            for (CettaCount j = 0; j < branches[i].results.len; j++) {
                Atom *item = branches[i].results.items[j];
                if (has_success && atom_is_error(item))
                    continue;
                items[len++] = atom_deep_copy(a, item);
            }
        }
        outcome_set_add(os, atom_expr(a, items, len), &empty);
    }

    for (CettaCount i = 0; i < branch_count; i++)
        hyperpose_thread_branch_free(&branches[i]);
    free(branches);
#undef HYPERPOSE_WORKER_STACK_BYTES
    return true;
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
        ok = match_atoms_builder(atom, let_ctx->pat, &b) &&
             !bindings_has_loop(bindings_builder_bindings(&b));
    if (ok) {
        const Bindings *bb = bindings_builder_bindings(&b);
        Bindings visible;
        if (!bindings_project_body_visible_env(let_ctx->a, let_ctx->body,
                                               bb, &visible)) {
            bindings_builder_free(&b);
            return true;
        }
        Bindings branch_outer_owned;
        const Bindings *branch_outer = let_ctx->outer_env;
        if (!branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                    let_ctx->outer_env, bb)) {
            bindings_free(&visible);
            bindings_builder_free(&b);
            return true;
        }
        Atom *subst =
            bindings_apply_projected_body_visible(&visible, let_ctx->a,
                                                  let_ctx->body);
        eval_for_current_caller(let_ctx->s, let_ctx->a, NULL, subst,
                                let_ctx->fuel, &empty, branch_outer,
                                let_ctx->preserve_bindings, let_ctx->os);
        branch_outer_env_finish(&branch_outer_owned, branch_outer);
        bindings_free(&visible);
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

    if (bounded && limit == 0) {
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    if (order == CETTA_SEARCH_POLICY_ORDER_NATIVE &&
        hyperpose_threaded_stream(s, a, stream_expr, fuel, bounded, limit,
                                  preserve_bindings, os)) {
        return;
    }

    if (bounded && limit == 1) {
        StreamFirstResultCtx first = { .os = os };
        if (metta_eval_bind_visit(s, a, stream_expr, fuel, order,
                                  stream_visit_emit_first, &first) == 0) {
            outcome_set_add(os, atom_empty(a), &_empty);
        }
        return;
    }

    StreamCollectCtx collect = {0};
    collect.bounded = bounded;
    collect.limit = limit;
    (void)preserve_bindings;
    metta_eval_bind_visit(s, a, stream_expr, fuel, order, stream_visit_collect, &collect);

    Atom **items = arena_alloc(a, sizeof(Atom *) * (collect.buffer.len > 0 ? collect.buffer.len : 1));
    for (CettaCount i = 0; i < collect.buffer.len; i++)
        items[i] = collect.buffer.items[i];
    outcome_set_add(os, atom_expr(a, items, collect.buffer.len), &_empty);
    stream_item_buffer_free(&collect.buffer);
}

static bool collapse_direct_stream(Space *s, Arena *a, Atom *stream_expr, int fuel,
                                   OutcomeSet *os) {
    if (!direct_outcome_walk_supported(s, a, stream_expr, fuel))
        return false;

    StreamCollectCtx collect = {0};
    CettaCount visited = 0;
    if (!direct_outcome_walk(s, a, stream_expr, fuel,
                             stream_visit_collect, &collect, &visited)) {
        stream_item_buffer_free(&collect.buffer);
        return false;
    }

    bool has_success = false;
    for (CettaCount i = 0; i < collect.buffer.len; i++) {
        if (!atom_is_error(collect.buffer.items[i])) {
            has_success = true;
            break;
        }
    }

    Atom **items = arena_alloc(a, sizeof(Atom *) *
                                  (collect.buffer.len > 0 ? collect.buffer.len : 1));
    uint32_t len = 0;
    for (CettaCount i = 0; i < collect.buffer.len; i++) {
        if (has_success && atom_is_error(collect.buffer.items[i]))
            continue;
        items[len++] = collect.buffer.items[i];
    }

    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(os, atom_expr(a, items, len), &empty);
    stream_item_buffer_free(&collect.buffer);
    return true;
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
    for (CettaCount i = 0; i < results.len; i++) {
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
    /* Scratch arenas are freed eagerly; they must not seed the long-lived
       hashcons table with child pointers that outlive the scratch storage. */
    arena_set_hashcons(&reduce.stream_scratch, NULL);

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
    arena_set_hashcons(&ctx.stream_scratch, NULL);

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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        bool child_changed = false;
        Atom *resolved = resolve_registry_refs_impl(a, atom->expr.elems[i], &child_changed);
        if (child_changed && !new_elems) {
            new_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            for (CettaExprIndex j = 0; j < i; j++)
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
    if (!g_registry || !atom)
        return atom;

    if (atom->kind != ATOM_EXPR && atom->kind != ATOM_SYMBOL)
        return atom;

    if (atom->kind == ATOM_SYMBOL && !atom_has_registry_refs(atom)) {
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
    arena_set_hashcons(&ctx.scratch, NULL);
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
    CettaCount total_items = 0;
    for (CettaCount i = 0; i < stream_items.len; i++) {
        if (atom_is_error(stream_items.items[i])) {
            ctx.error_atom = stream_items.items[i];
            break;
        }
        total_items++;
    }

    if (total_items > 0) {
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

typedef struct {
    SpaceTransferEndpointKind target_kind;
    SpaceTransferEndpointKind source_kind;
    Atom *target_ref;
    Atom *source_ref;
} CettaAtomsTransferRefs;

static SpaceTransferEndpointKind atoms_source_head_kind(SymbolId head_id) {
    if (head_id == g_builtin_syms.get_atoms)
        return SPACE_TRANSFER_ENDPOINT_SPACE;
    if (head_id == g_builtin_syms.mork_get_atoms_surface ||
        head_id == g_builtin_syms.lib_mork_space_atoms) {
        return SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE;
    }
    return SPACE_TRANSFER_ENDPOINT_NONE;
}

static bool add_atoms_source_shape(Atom *items, Atom **out_source_ref,
                                   bool *out_collapsed,
                                   SpaceTransferEndpointKind *out_source_kind) {
    Atom *inner = NULL;
    SpaceTransferEndpointKind kind = SPACE_TRANSFER_ENDPOINT_NONE;

    if (out_source_ref)
        *out_source_ref = NULL;
    if (out_collapsed)
        *out_collapsed = false;
    if (out_source_kind)
        *out_source_kind = SPACE_TRANSFER_ENDPOINT_NONE;
    if (!items)
        return false;

    kind = atoms_source_head_kind(atom_head_symbol_id(items));
    if (kind != SPACE_TRANSFER_ENDPOINT_NONE && expr_nargs(items) == 1) {
        if (out_source_ref)
            *out_source_ref = expr_arg(items, 0);
        if (out_source_kind)
            *out_source_kind = kind;
        return true;
    }

    if (!expr_head_is_id(items, g_builtin_syms.collapse) ||
        expr_nargs(items) != 1) {
        return false;
    }
    inner = expr_arg(items, 0);
    kind = atoms_source_head_kind(atom_head_symbol_id(inner));
    if (kind == SPACE_TRANSFER_ENDPOINT_NONE || expr_nargs(inner) != 1) {
        return false;
    }

    if (out_source_ref)
        *out_source_ref = expr_arg(inner, 0);
    if (out_collapsed)
        *out_collapsed = true;
    if (out_source_kind)
        *out_source_kind = kind;
    return true;
}

static bool atom_is_default_add_atoms_fold_equation(Atom *atom) {
    if (!expr_head_is_id(atom, g_builtin_syms.equals) || expr_nargs(atom) != 2)
        return false;
    Atom *lhs = expr_arg(atom, 0);
    Atom *rhs = expr_arg(atom, 1);
    if (!expr_head_is_id(lhs, g_builtin_syms.add_atoms) ||
        expr_nargs(lhs) != 2 ||
        !expr_head_is_id(rhs, g_builtin_syms.foldl_atom) ||
        expr_nargs(rhs) != 5) {
        return false;
    }
    Atom *space_arg = expr_arg(lhs, 0);
    Atom *tuple_arg = expr_arg(lhs, 1);
    Atom *unit_arg = expr_arg(rhs, 1);
    Atom *row_var = expr_arg(rhs, 3);
    Atom *body = expr_arg(rhs, 4);
    return atom_eq(expr_arg(rhs, 0), tuple_arg) &&
           unit_arg->kind == ATOM_EXPR && unit_arg->expr.len == 0 &&
           row_var->kind == ATOM_VAR &&
           expr_head_is_id(body, g_builtin_syms.add_atom) &&
           expr_nargs(body) == 2 &&
           atom_eq(expr_arg(body, 0), space_arg) &&
           atom_eq(expr_arg(body, 1), row_var);
}

static bool atom_is_add_atoms_equation(Atom *atom) {
    if (!expr_head_is_id(atom, g_builtin_syms.equals) || expr_nargs(atom) != 2)
        return false;
    Atom *lhs = expr_arg(atom, 0);
    return expr_head_is_id(lhs, g_builtin_syms.add_atoms) &&
           expr_nargs(lhs) == 2;
}

static bool add_atoms_public_surface_has_only_default(Space *s) {
    bool found_default = false;
    CettaCount len = space_length64(s);
    for (CettaIndex i = 0; i < len; i++) {
        Atom *atom = space_get_at64(s, i);
        if (!atom_is_add_atoms_equation(atom))
            continue;
        if (!atom_is_default_add_atoms_fold_equation(atom))
            return false;
        found_default = true;
    }
    return found_default;
}

static bool resolve_atoms_transfer_endpoint(Space *s, Arena *a, Atom *call_atom,
                                            SpaceTransferEndpointKind kind,
                                            Atom *ref,
                                            bool target_space_guard,
                                            int fuel,
                                            SpaceTransferEndpoint *out) {
    if (!out)
        return false;
    *out = (SpaceTransferEndpoint){ .kind = SPACE_TRANSFER_ENDPOINT_NONE };
    if (!ref || kind == SPACE_TRANSFER_ENDPOINT_NONE)
        return false;

    if (kind == SPACE_TRANSFER_ENDPOINT_SPACE) {
        Space *space = target_space_guard
            ? resolve_single_space_arg_write(s, a, ref, fuel)
            : resolve_single_space_arg(s, a, ref, fuel);
        if (!space)
            return false;
        if (target_space_guard &&
            guard_mork_space_surface(a, call_atom, space, "add-atoms",
                                     "mork:add-atoms")) {
            return false;
        }
        *out = (SpaceTransferEndpoint){
            .kind = SPACE_TRANSFER_ENDPOINT_SPACE,
            .space = space,
        };
        return true;
    }

    CettaMorkSpaceHandle *bridge = NULL;
    if (!resolve_explicit_mork_bridge_arg(s, a, ref, fuel, &bridge) ||
        !bridge) {
        return false;
    }
    *out = (SpaceTransferEndpoint){
        .kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
        .bridge = bridge,
    };
    return true;
}

static bool emit_atoms_transfer_refs_direct(Space *s, Arena *a, Atom *call_atom,
                                            const CettaAtomsTransferRefs *refs,
                                            const Bindings *current_env,
                                            int fuel, OutcomeSet *os) {
    Bindings empty;
    uint64_t added = 0;
    SpaceTransferResult result = SPACE_TRANSFER_ERROR;
    SpaceTransferEndpoint target = { .kind = SPACE_TRANSFER_ENDPOINT_NONE };
    SpaceTransferEndpoint source = { .kind = SPACE_TRANSFER_ENDPOINT_NONE };

    if (!refs || !refs->target_ref || !refs->source_ref ||
        refs->source_kind == SPACE_TRANSFER_ENDPOINT_NONE) {
        return false;
    }
    bindings_init(&empty);

    Atom *target_ref =
        (!current_env || current_env->len == 0)
            ? refs->target_ref
            : bindings_apply_if_vars(current_env, a, refs->target_ref);
    Atom *source_ref =
        (!current_env || current_env->len == 0)
            ? refs->source_ref
            : bindings_apply_if_vars(current_env, a, refs->source_ref);

    if (!resolve_atoms_transfer_endpoint(s, a, call_atom, refs->target_kind,
                                         target_ref, true, fuel, &target) ||
        !resolve_atoms_transfer_endpoint(s, a, call_atom, refs->source_kind,
                                         source_ref, false, fuel, &source)) {
        return false;
    }

    result = space_match_backend_transfer_resolved_result(
        target, source, eval_storage_arena(a), &added);
    if (result == SPACE_TRANSFER_OK) {
        (void)added;
        outcome_set_add(os, atom_unit(a), &empty);
        return true;
    }
    if (result == SPACE_TRANSFER_ERROR) {
        outcome_set_add(os, atom_error(a, call_atom,
                                       atom_symbol(a, "SpaceTransferFailed")),
                        &empty);
        return true;
    }

    return false;
}

static Atom *space_term_universe_or_symbol_error(Arena *a, Atom *call,
                                                 Space *space,
                                                 const char *fallback_symbol);

static bool add_atoms_from_evaluated_source_results(Space *s, Arena *a,
                                                    Atom *call_atom,
                                                    Space *target,
                                                    Atom *items,
                                                    bool collapsed,
                                                    int fuel,
                                                    OutcomeSet *os) {
    ResultSet rs;
    Bindings empty;
    Arena *dst = eval_storage_arena(a);

    bindings_init(&empty);
    result_set_init(&rs);
    metta_eval(s, a, NULL, items, fuel, &rs);

    for (CettaCount i = 0; i < rs.len; i++) {
        Atom *item = rs.items[i];
        if (atom_is_error(item)) {
            outcome_set_add(os, item, &empty);
            result_set_free(&rs);
            return true;
        }
        if (collapsed) {
            if (!item || item->kind != ATOM_EXPR) {
                outcome_set_add(os, atom_error(a, call_atom,
                    atom_symbol(a, "add-atoms source collapse did not produce an expression")),
                    &empty);
                result_set_free(&rs);
                return true;
            }
            for (CettaExprIndex j = 0; j < item->expr.len; j++) {
                if (!space_admit_atom(target, dst, item->expr.elems[j])) {
                    outcome_set_add(os,
                        space_term_universe_or_symbol_error(a, call_atom, target,
                                                            "AddAtomsFailed"),
                        &empty);
                    result_set_free(&rs);
                    return true;
                }
            }
        } else if (!space_admit_atom(target, dst, item)) {
            outcome_set_add(os,
                space_term_universe_or_symbol_error(a, call_atom, target,
                                                    "AddAtomsFailed"),
                &empty);
            result_set_free(&rs);
            return true;
        }
    }

    result_set_free(&rs);
    outcome_set_add(os, atom_unit(a), &empty);
    return true;
}

static bool emit_add_atoms_from_source_shape(Space *s, Arena *a,
                                             Atom *call_atom,
                                             Space *target,
                                             Atom *items,
                                             int fuel, OutcomeSet *os) {
    Atom *source_ref = NULL;
    bool collapsed = false;
    SpaceTransferEndpointKind source_kind = SPACE_TRANSFER_ENDPOINT_NONE;

    if (!add_atoms_source_shape(items, &source_ref, &collapsed, &source_kind))
        return false;

    CettaAtomsTransferRefs refs = {
        .target_kind = SPACE_TRANSFER_ENDPOINT_SPACE,
        .source_kind = source_kind,
        .target_ref = atom_space(a, target),
        .source_ref = source_ref,
    };
    if (emit_atoms_transfer_refs_direct(s, a, call_atom, &refs, NULL, fuel, os))
        return true;

    return add_atoms_from_evaluated_source_results(
        s, a, call_atom, target, items, collapsed, fuel, os);
}

static bool emit_mork_add_atoms_from_source_shape(Space *s, Arena *a,
                                                  Atom *call_atom,
                                                  Atom *space_arg,
                                                  Atom *items,
                                                  const Bindings *current_env,
                                                  int fuel, OutcomeSet *os) {
    Atom *source_ref = NULL;
    bool collapsed = false;
    SpaceTransferEndpointKind source_kind = SPACE_TRANSFER_ENDPOINT_NONE;

    if (!add_atoms_source_shape(items, &source_ref, &collapsed, &source_kind))
        return false;

    CettaAtomsTransferRefs refs = {
        .target_kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE,
        .source_kind = source_kind,
        .target_ref = space_arg,
        .source_ref = source_ref,
    };
    if (emit_atoms_transfer_refs_direct(s, a, call_atom, &refs, current_env,
                                        fuel, os)) {
        return true;
    }

    Atom *stream_source = collapsed ? expr_arg(items, 0) : items;
    return emit_mork_add_atoms_from_stream_source(
        s, a, call_atom, space_arg, stream_source, current_env, fuel, os);
}

static bool let_add_atoms_source_shape(Atom *pat, Atom *val_expr,
                                       Atom *body, Atom **out_target_ref,
                                       Atom **out_source_ref,
                                       SpaceTransferEndpointKind *out_target_kind,
                                       SpaceTransferEndpointKind *out_source_kind) {
    Atom *source_ref = NULL;
    bool collapsed = false;
    SpaceTransferEndpointKind source_kind = SPACE_TRANSFER_ENDPOINT_NONE;
    SpaceTransferEndpointKind target_kind = SPACE_TRANSFER_ENDPOINT_SPACE;

    if (out_target_ref)
        *out_target_ref = NULL;
    if (out_source_ref)
        *out_source_ref = NULL;
    if (out_target_kind)
        *out_target_kind = SPACE_TRANSFER_ENDPOINT_SPACE;
    if (out_source_kind)
        *out_source_kind = SPACE_TRANSFER_ENDPOINT_NONE;
    if (!pat || pat->kind != ATOM_VAR || !body ||
        body->kind != ATOM_EXPR || body->expr.len != 3) {
        return false;
    }
    if (!add_atoms_source_shape(val_expr, &source_ref, &collapsed, &source_kind) ||
        !collapsed) {
        return false;
    }
    if (expr_head_is_id(body, g_builtin_syms.add_atoms)) {
        target_kind = SPACE_TRANSFER_ENDPOINT_SPACE;
    } else if (expr_head_is_id(body, g_builtin_syms.mork_add_atoms) ||
               expr_head_is_id(body, g_builtin_syms.lib_mork_space_add_atoms)) {
        target_kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE;
    } else {
        return false;
    }
    if (expr_nargs(body) != 2 || !atom_eq(expr_arg(body, 1), pat))
        return false;

    if (out_target_ref)
        *out_target_ref = expr_arg(body, 0);
    if (out_source_ref)
        *out_source_ref = source_ref;
    if (out_target_kind)
        *out_target_kind = target_kind;
    if (out_source_kind)
        *out_source_kind = source_kind;
    return true;
}

static void temp_space_register(Space *space) {
    if (g_temp_spaces.len >= g_temp_spaces.cap) {
        CettaCount next_cap;
        if (!eval_next_capacity(g_temp_spaces.cap, g_temp_spaces.len + 1u,
                                sizeof(Space *), &next_cap))
            return;
        g_temp_spaces.items = cetta_realloc(g_temp_spaces.items,
                                            sizeof(Space *) * (size_t)next_cap);
        g_temp_spaces.cap = next_cap;
    }
    g_temp_spaces.items[g_temp_spaces.len++] = space;
}

static bool temp_space_is_registered(Space *space) {
    if (!space) return false;
    for (CettaCount i = 0; i < g_temp_spaces.len; i++) {
        if (g_temp_spaces.items[i] == space)
            return true;
    }
    return false;
}

static Space *space_persistent_clone(Space *src, Arena *dst) {
    uint32_t len = 0;

    if (!space_match_backend_materialize_attached(src, dst))
        return NULL;
    if (!space_length_u32_checked(src, &len))
        return NULL;
    Space *clone = arena_alloc(dst, sizeof(Space));
    space_init_with_universe(clone, src ? src->native.universe : NULL);
    clone->kind = src->kind;
    (void)space_match_backend_try_set(clone, src->match_backend.kind);
    for (uint32_t i = 0; i < len; i++) {
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

/* Spaces created by `new-space` whose struct lives in the persistent arena.  The
 * struct is reclaimed with the arena, but its heap-allocated internals
 * (atom_ids, indexes) are only released by `space_free`, which nothing calls for
 * an arena-owned struct.  We track them and `space_free` the ones not bound in
 * the registry at session teardown (registry-bound ones are freed by
 * cetta_main_cleanup_registry_spaces).  This makes a session's `new-space`
 * resources reclaimed at teardown instead of leaked. */
void eval_track_new_space(Space *space) {
    if (!space) return;
    if (g_new_space_track.len >= g_new_space_track.cap) {
        CettaCount next_cap;
        if (!eval_next_capacity(g_new_space_track.cap, g_new_space_track.len + 1u,
                                sizeof(Space *), &next_cap))
            return;
        g_new_space_track.items = cetta_realloc(g_new_space_track.items,
                                                sizeof(Space *) * (size_t)next_cap);
        g_new_space_track.cap = next_cap;
    }
    g_new_space_track.items[g_new_space_track.len++] = space;
}

static void eval_cleanup_owned_new_spaces_in_set(TempSpaceSet *set,
                                                 Registry *registry,
                                                 Space *root) {
    if (!set)
        return;
    for (CettaCount i = 0; i < set->len; i++) {
        Space *sp = set->items[i];
        bool in_registry = false;
        if (!sp || sp == root)
            continue;
        if (registry) {
            for (uint32_t ri = 0; ri < registry->len; ri++) {
                Atom *val = registry->entries[ri].value;
                if (val && val->kind == ATOM_GROUNDED &&
                    val->ground.gkind == GV_SPACE &&
                    (Space *)val->ground.ptr == sp) {
                    in_registry = true;
                    break;
                }
            }
        }
        if (!in_registry)
            space_free(sp);
    }
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

void eval_cleanup_owned_new_spaces_for_current_thread(void) {
    for (CettaCount i = 0; i < g_new_space_track.len; i++) {
        Space *sp = g_new_space_track.items[i];
        if (!sp)
            continue;
        space_free(sp);
    }
    free(g_new_space_track.items);
    g_new_space_track.items = NULL;
    g_new_space_track.len = 0;
    g_new_space_track.cap = 0;
}

void eval_drain_owned_new_spaces_for_current_thread(void) {
    if (g_new_space_track.len == 0)
        return;
    pthread_mutex_lock(&g_new_space_drained_mutex);
    for (CettaCount i = 0; i < g_new_space_track.len; i++) {
        Space *space = g_new_space_track.items[i];
        if (!space)
            continue;
        if (g_new_space_track_drained.len >= g_new_space_track_drained.cap) {
            CettaCount next_cap;
            if (!eval_next_capacity(g_new_space_track_drained.cap,
                                    g_new_space_track_drained.len + 1u,
                                    sizeof(Space *), &next_cap)) {
                break;
            }
            g_new_space_track_drained.items =
                cetta_realloc(g_new_space_track_drained.items,
                              sizeof(Space *) * (size_t)next_cap);
            g_new_space_track_drained.cap = next_cap;
        }
        g_new_space_track_drained
            .items[g_new_space_track_drained.len++] = space;
    }
    pthread_mutex_unlock(&g_new_space_drained_mutex);
    free(g_new_space_track.items);
    g_new_space_track.items = NULL;
    g_new_space_track.len = 0;
    g_new_space_track.cap = 0;
}

void eval_cleanup_owned_new_spaces(Registry *registry, Space *root) {
    eval_cleanup_owned_new_spaces_in_set(&g_new_space_track, registry, root);
    pthread_mutex_lock(&g_new_space_drained_mutex);
    eval_cleanup_owned_new_spaces_in_set(&g_new_space_track_drained,
                                         registry, root);
    pthread_mutex_unlock(&g_new_space_drained_mutex);
}

void eval_release_temporary_spaces(void) {
    eval_release_episode_table();
    eval_release_outcome_variant_bank();
    eval_release_episode_survivor_arena();
    for (CettaCount i = 0; i < g_temp_spaces.len; i++) {
        space_free(g_temp_spaces.items[i]);
        free(g_temp_spaces.items[i]);
    }
    free(g_temp_spaces.items);
    g_temp_spaces.items = NULL;
    g_temp_spaces.len = 0;
    g_temp_spaces.cap = 0;
}

void eval_reset_form_gc_survivor(void) {
    eval_gc_survivor_reset();
}

Registry *eval_current_registry(void) {
    return g_registry;
}

Arena *eval_current_persistent_arena(void) {
    return eval_persistent_arena();
}

CettaLibraryContext *eval_current_library_context(void) {
    return g_library_context;
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

static Atom *space_backend_error_if_set(Arena *a, Atom *call) {
    SpaceMatchBackendError code = space_match_backend_last_error_code();
    if (code == SPACE_MATCH_BACKEND_ERROR_NONE)
        return NULL;
    return atom_error(a, call,
                      atom_symbol(a, space_match_backend_error_name(code)));
}

static Atom *space_backend_or_symbol_error(Arena *a, Atom *call,
                                           const char *fallback_symbol) {
    Atom *space_error = space_backend_error_if_set(a, call);
    if (space_error)
        return space_error;
    return atom_error(a, call, atom_symbol(a, fallback_symbol));
}

static Atom *space_term_universe_or_symbol_error(Arena *a, Atom *call,
                                                 Space *space,
                                                 const char *fallback_symbol) {
    TermUniverseError code = space_term_universe_last_error_code(space);
    if (code != TERM_UNIVERSE_ERROR_NONE) {
        return atom_error(a, call,
                          atom_symbol(a, term_universe_error_name(code)));
    }
    return space_backend_or_symbol_error(a, call, fallback_symbol);
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
        Space *self = resolve_registry_space_payload(g_registry, target);
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

    Arena *pa = eval_storage_arena(a);
    Space *ns = arena_alloc(pa, sizeof(Space));
    if (!ns) {
        *error_out = atom_symbol(a, "OutOfMemory");
        return dest;
    }
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
    Space *space = resolve_registry_space_payload(g_registry, target);
    if (!space) {
        *error_out = atom_symbol(a, "include destination must be &self or an existing &name");
    }
    return space;
}

static void collect_resolved_spaces(Space *s, Arena *a, Atom *space_expr,
                                    int fuel, Space ***spaces_out,
                                    CettaCount *len_out) {
    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);

    Space **spaces = NULL;
    CettaCount len = 0;
    if (rs.len > 0) {
        spaces = cetta_malloc(sizeof(Space *) * (size_t)rs.len);
        for (CettaCount i = 0; i < rs.len; i++) {
            Space *sp = payload_resolve_space_read(
                resolve_space(g_registry, rs.items[i]));
            if (sp) spaces[len++] = sp;
        }
    }
    result_set_free(&rs);
    *spaces_out = spaces;
    *len_out = len;
}

static Space *resolve_registry_space_payload(Registry *r, Atom *ref) {
    return payload_resolve_space_read(resolve_space(r, ref));
}

static Space *resolve_single_space_arg(Space *s, Arena *a, Atom *space_expr, int fuel) {
    if (!g_registry) return NULL;

    Atom *direct = resolve_registry_refs(a, space_expr);
    Space *sp = resolve_registry_space_payload(g_registry, direct);
    if (sp) return sp;

    ResultSet rs;
    result_set_init(&rs);
    metta_eval(s, a, NULL, space_expr, fuel, &rs);
    for (CettaCount i = 0; i < rs.len; i++) {
        sp = resolve_registry_space_payload(g_registry, rs.items[i]);
        if (sp) break;
    }
    result_set_free(&rs);
    return sp;
}

static Space *resolve_single_space_arg_write(Space *s, Arena *a, Atom *space_expr,
                                             int fuel) {
    return payload_resolve_space_write(
        resolve_single_space_arg(s, a, space_expr, fuel));
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
        if (head_id == g_builtin_syms.add_atom_nodup &&
            !active_surface_allowed("add-atom-nodup")) {
            return false;
        }
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
        CettaCount logical_len = space_length64(target);
        for (CettaIndex i = 0; i < logical_len && !found; i++) {
            Atom *candidate = space_get_at64(target, i);
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
    Space *target = resolve_single_space_arg_write(s, a, space_ref, fuel);
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
                space_backend_or_symbol_error(
                    a, call_atom, "AttachedCompiledSpaceMaterializeFailed"),
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
        for (CettaCount i = 0; i < ctx.errors.len; i++)
            result_set_add(errors_out, ctx.errors.items[i]);
    } else if (ctx.errors.len > 0 && os && ctx.emitted_units == 0) {
        Bindings empty;
        bindings_init(&empty);
        for (CettaCount i = 0; i < ctx.errors.len; i++)
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
    for (CettaCount i = 0; i < vals.len; i++) {
        if (atom_is_empty(vals.items[i]))
            continue;
        if (atom_is_error(vals.items[i]) || single) {
            result_set_free(&vals);
            return false;
        }
        single = vals.items[i];
    }
    result_set_free(&vals);
    if (!single)
        return false;
    *out = single;
    return true;
}

typedef struct {
    StreamItemBuffer *items;
    ResultSet stream_errors;
    ResultSet *body_results;
    bool identity_body;
    bool has_success;
} CollapseLetStreamCtx;

enum {
    COLLAPSE_LET_MAX_FAST_DEPTH = 8,
};

static bool collapse_push_result_set(StreamItemBuffer *items, ResultSet *rs) {
    for (CettaCount i = 0; i < rs->len; i++) {
        if (atom_is_empty(rs->items[i]))
            continue;
        if (!stream_item_buffer_push(items, rs->items[i]))
            return false;
    }
    return true;
}

static bool collapse_let_stream_visit(Arena *a, Atom *atom,
                                      const Bindings *env, void *user_ctx) {
    (void)a;
    (void)env;
    CollapseLetStreamCtx *ctx = user_ctx;
    if (atom_is_error(atom)) {
        result_set_add(&ctx->stream_errors, atom);
        return true;
    }
    ctx->has_success = true;
    if (ctx->identity_body)
        return stream_item_buffer_push(ctx->items, atom);
    return collapse_push_result_set(ctx->items, ctx->body_results);
}

static bool collapse_let_collect(Space *s, Arena *a, Atom *inner, int fuel,
                                 const Bindings *outer_env, uint32_t depth,
                                 StreamItemBuffer *items) {
    if (depth >= COLLAPSE_LET_MAX_FAST_DEPTH || !inner ||
        inner->kind != ATOM_EXPR ||
        !expr_head_is_id(inner, g_builtin_syms.let) ||
        expr_nargs(inner) != 3) {
        return false;
    }

    Atom *pat = expr_arg(inner, 0);
    if (!pat || pat->kind != ATOM_VAR)
        return false;

    Atom *stream_expr =
        bindings_apply_if_vars(outer_env, a, expr_arg(inner, 1));
    Atom *body = expr_arg(inner, 2);

    if (direct_outcome_walk_supported(s, a, stream_expr, fuel)) {
        Atom *body_applied = bindings_apply_if_vars(outer_env, a, body);
        bool identity_body =
            body_applied->kind == ATOM_VAR && body_applied->var_id == pat->var_id;

        ResultSet body_results;
        result_set_init(&body_results);
        if (!identity_body) {
            if (atom_contains_vars(body_applied) ||
                !direct_outcome_walk_supported(s, a, body_applied, fuel)) {
                result_set_free(&body_results);
                return false;
            }
            metta_eval(s, a, NULL, body_applied, fuel, &body_results);
        }

        CollapseLetStreamCtx ctx = {
            .items = items,
            .stream_errors = {0},
            .body_results = &body_results,
            .identity_body = identity_body,
            .has_success = false,
        };
        result_set_init(&ctx.stream_errors);
        CettaCount visited = 0;
        bool ok = direct_outcome_walk(s, a, stream_expr, fuel,
                                      collapse_let_stream_visit, &ctx, &visited);
        if (ok && !ctx.has_success)
            ok = collapse_push_result_set(items, &ctx.stream_errors);
        result_set_free(&ctx.stream_errors);
        result_set_free(&body_results);
        return ok;
    }

    Atom *single = NULL;
    if (!effect_safe_single_feeder_value(s, a, stream_expr, fuel, &single))
        return false;

    BindingsBuilder b;
    if (!bindings_builder_init(&b, outer_env))
        return false;
    if (!bindings_builder_add_var_fresh(&b, pat, single)) {
        bindings_builder_free(&b);
        return false;
    }

    bool handled = collapse_let_collect(
        s, a, body, fuel, bindings_builder_bindings(&b), depth + 1, items);
    bindings_builder_free(&b);
    return handled;
}

static bool collapse_let_stream(Space *s, Arena *a, Atom *inner, int fuel,
                                const Bindings *outer_env, OutcomeSet *os) {
    StreamItemBuffer items_buf = {0};
    if (!collapse_let_collect(s, a, inner, fuel, outer_env, 0, &items_buf)) {
        stream_item_buffer_free(&items_buf);
        return false;
    }

    Atom **items = arena_alloc(a, sizeof(Atom *) *
                                  (items_buf.len > 0 ? items_buf.len : 1));
    for (CettaCount i = 0; i < items_buf.len; i++)
        items[i] = items_buf.items[i];

    Bindings empty;
    bindings_init(&empty);
    outcome_set_add(os, atom_expr(a, items, items_buf.len), &empty);
    stream_item_buffer_free(&items_buf);
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

    /* This is only a fast-path recursion cap; deeper terms fall back to the
       ordinary evaluator, preserving semantics. */
    if (depth >= COLLAPSE_LET_MAX_FAST_DEPTH)
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
        : (match_atoms_builder(single, pat, &b) &&
           !bindings_has_loop(bindings_builder_bindings(&b)));
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
    CettaCount error_count = unit_count == 0 ? errors.len : 0;
    CettaCount len = unit_count + error_count;
    Atom **items = arena_alloc(a, sizeof(Atom *) * (len ? len : 1));
    Atom *unit = atom_unit(a);
    for (CettaCount i = 0; i < unit_count; i++)
        items[i] = unit;
    for (CettaCount i = 0; i < error_count; i++)
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

static void capped_msg_appendf(char *buf, size_t cap, size_t *pos,
                               const char *fmt, ...) {
    if (cap == 0 || *pos >= cap - 1)
        return;
    va_list ap;
    va_start(ap, fmt);
    int written = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (written < 0)
        return;
    size_t used = (size_t)written;
    if (used >= cap - *pos) {
        *pos = cap - 1;
        buf[*pos] = '\0';
    } else {
        *pos += used;
    }
}

static void capped_msg_append_atom(char *buf, size_t cap, size_t *pos,
                                   Atom *atom) {
    if (cap == 0 || *pos >= cap - 1)
        return;
    FILE *tmp = fmemopen(buf + *pos, cap - *pos, "w");
    if (!tmp)
        return;
    atom_print(atom, tmp);
    long end = ftell(tmp);
    fclose(tmp);
    if (end < 0)
        return;
    size_t used = (size_t)end;
    if (used >= cap - *pos) {
        *pos = cap - 1;
        buf[*pos] = '\0';
    } else {
        *pos += used;
    }
}

static void result_set_resolve_registry_refs(Arena *a, ResultSet *rs) {
    for (CettaCount i = 0; i < rs->len; i++) {
        rs->items[i] = resolve_registry_refs(a, rs->items[i]);
    }
}

/* Snapshot clone: shares universe-backed term identity but freezes the
   source's logical view. The snapshot owns only its atom-id sequence and
   per-space indexes/backend state. */
static bool space_snapshot_copy_logical_view(Space *dst, const Space *src) {
    uint32_t n = 0;

    if (!dst || !src)
        return false;
    if (!space_length_u32_checked(src, &n))
        return false;
    dst->revision = space_revision(src);
    if (n == 0)
        return true;
    for (uint32_t i = 0; i < n; i++) {
        AtomId atom_id = space_get_atom_id_at(src, i);
        if (atom_id == CETTA_ATOM_ID_NONE) {
            Atom *atom = space_get_at(src, i);
            atom_id = term_universe_store_atom_id(dst->native.universe, NULL, atom);
        }
        if (atom_id != CETTA_ATOM_ID_NONE)
            space_add_atom_id(dst, atom_id);
    }

    /* The clone starts with no rebuilt indexes; they must be derived from the
       frozen logical view on first use. */
    space_mark_derived_state_dirty(dst);
    return true;
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
    if (!space_snapshot_copy_logical_view(clone, src)) {
        space_free(clone);
        free(clone);
        return NULL;
    }
    /* Backend indexes are rebuilt lazily on first match against the snapshot;
       the clone only freezes the logical atom sequence. */
    temp_space_register(clone);
    return clone;
}

Space *eval_space_snapshot_clone(Space *src, Arena *a) {
    return space_snapshot_clone(src, a);
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

static CettaExprLen get_function_arg_count(Atom *ft) {
    if (!is_function_type(ft))
        return 0;
    return ft->expr.len - 2; /* skip "->" at [0] and ret at [len-1] */
}

/* Extract arg types from (-> t1 t2 ... tN ret).
   Excludes "->" and the final return type. */
static void get_function_arg_types(Atom *ft, Atom **out) {
    CettaExprLen n = get_function_arg_count(ft);
    for (CettaExprIndex i = 0; i < n; i++)
        out[i] = ft->expr.elems[i + 1];
}

static Atom *get_function_ret_type(Atom *ft) {
    if (!is_function_type(ft)) return NULL;
    return ft->expr.elems[ft->expr.len - 1];
}

static bool eval_dependent_telescope_enabled(void) {
    return cetta_language_enables_dependent_telescope(active_eval_session()->language_id,
                                                      active_eval_session()->profile);
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

#define PROFILED_TYPE_CACHE_CAP 4096u
#define PROFILED_TYPE_FORMATION_CACHE_CAP 4096u
#define PROFILED_TYPE_CACHE_RESET_STORES 32768u

typedef struct {
    Space *space;
    uint64_t revision;
    CettaLanguageId language_id;
    uint32_t profile_id;
    bool dependent_telescope_enabled;
    uint32_t atom_hash;
    uint32_t hash;
} ProfiledTypeCacheKey;

typedef struct {
    bool occupied;
    ProfiledTypeCacheKey key;
    Atom *atom_key;
    Atom **types;
    uint32_t count;
} ProfiledTypeListCacheEntry;

typedef struct {
    bool occupied;
    ProfiledTypeCacheKey key;
    Atom *atom_key;
    bool result;
} ProfiledTypeFormationCacheEntry;

static __thread Arena g_profiled_type_cache_arena;
static __thread bool g_profiled_type_cache_arena_ready = false;
static __thread uint32_t g_profiled_type_cache_stores = 0;
static __thread ProfiledTypeListCacheEntry
    g_profiled_type_cache[PROFILED_TYPE_CACHE_CAP];
static __thread ProfiledTypeFormationCacheEntry
    g_profiled_type_formation_cache[PROFILED_TYPE_FORMATION_CACHE_CAP];
static __thread bool g_profiled_type_cache_config_ready = false;
static __thread bool g_profiled_type_cache_config_enabled = true;

void eval_profiled_type_cache_free_for_current_thread(void) {
    if (g_profiled_type_cache_arena_ready)
        arena_free(&g_profiled_type_cache_arena);
    memset(g_profiled_type_cache, 0, sizeof(g_profiled_type_cache));
    memset(g_profiled_type_formation_cache, 0,
           sizeof(g_profiled_type_formation_cache));
    g_profiled_type_cache_arena_ready = false;
    g_profiled_type_cache_stores = 0;
}

static bool profiled_type_cache_disabled_value(const char *raw) {
    return raw &&
           (strcmp(raw, "0") == 0 ||
            strcmp(raw, "false") == 0 ||
            strcmp(raw, "off") == 0 ||
            strcmp(raw, "no") == 0);
}

static bool profiled_type_cache_enabled(void) {
    if (!g_profiled_type_cache_config_ready) {
        g_profiled_type_cache_config_enabled =
            !profiled_type_cache_disabled_value(getenv("CETTA_TYPE_CACHE"));
        g_profiled_type_cache_config_ready = true;
    }
    return g_profiled_type_cache_config_enabled;
}

static void profiled_type_cache_clear_all(void) {
    eval_profiled_type_cache_free_for_current_thread();
    arena_init(&g_profiled_type_cache_arena);
    arena_set_hashcons(&g_profiled_type_cache_arena, NULL);
    arena_set_runtime_kind(&g_profiled_type_cache_arena,
                           CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    g_profiled_type_cache_arena_ready = true;
}

static bool profiled_type_cache_prepare_store(void) {
    if (!profiled_type_cache_enabled())
        return false;
    if (!g_profiled_type_cache_arena_ready ||
        g_profiled_type_cache_stores >= PROFILED_TYPE_CACHE_RESET_STORES) {
        profiled_type_cache_clear_all();
    }
    g_profiled_type_cache_stores++;
    return true;
}

static uint32_t profiled_type_cache_mix_u32(uint32_t h, uint32_t v) {
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

static uint32_t profiled_type_cache_mix_u64(uint32_t h, uint64_t v) {
    h = profiled_type_cache_mix_u32(h, (uint32_t)(v & 0xFFFFFFFFu));
    h = profiled_type_cache_mix_u32(h, (uint32_t)(v >> 32));
    return h;
}

static bool profiled_type_cache_atom_input_stable(Atom *atom) {
    if (!atom || atom_has_vars(atom) || atom_has_registry_refs(atom))
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return true;
    case ATOM_VAR:
        return false;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!profiled_type_cache_atom_input_stable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool profiled_type_cache_result_stable(Atom *atom) {
    if (!atom || atom_has_registry_refs(atom))
        return false;
    switch (atom->kind) {
    case ATOM_SYMBOL:
    case ATOM_VAR:
        return true;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
        case GV_FLOAT:
        case GV_BOOL:
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL:
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
            return false;
        }
        return false;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            if (!profiled_type_cache_result_stable(atom->expr.elems[i]))
                return false;
        }
        return true;
    }
    return false;
}

static bool profiled_type_cache_make_key(Space *s, Atom *atom,
                                         ProfiledTypeCacheKey *out) {
    if (!profiled_type_cache_enabled() || !s || !atom ||
        !profiled_type_cache_atom_input_stable(atom)) {
        return false;
    }
    CettaEvalSession *session = active_eval_session();
    uint32_t profile_id = UINT32_MAX;
    if (session && session->profile)
        profile_id = (uint32_t)session->profile->id;
    uint32_t atom_h = atom_hash(atom);
    uint32_t h = 2166136261u;
    h = profiled_type_cache_mix_u64(h, (uint64_t)(uintptr_t)s);
    h = profiled_type_cache_mix_u64(h, space_revision(s));
    h = profiled_type_cache_mix_u32(h, session ? (uint32_t)session->language_id : 0u);
    h = profiled_type_cache_mix_u32(h, profile_id);
    h = profiled_type_cache_mix_u32(h, eval_dependent_telescope_enabled() ? 1u : 0u);
    h = profiled_type_cache_mix_u32(h, atom_h);
    *out = (ProfiledTypeCacheKey){
        .space = s,
        .revision = space_revision(s),
        .language_id = session ? session->language_id : CETTA_LANGUAGE_HE,
        .profile_id = profile_id,
        .dependent_telescope_enabled = eval_dependent_telescope_enabled(),
        .atom_hash = atom_h,
        .hash = h,
    };
    return true;
}

static bool profiled_type_cache_key_matches(const ProfiledTypeCacheKey *lhs,
                                            const ProfiledTypeCacheKey *rhs) {
    return lhs->space == rhs->space &&
           lhs->revision == rhs->revision &&
           lhs->language_id == rhs->language_id &&
           lhs->profile_id == rhs->profile_id &&
           lhs->dependent_telescope_enabled == rhs->dependent_telescope_enabled &&
           lhs->atom_hash == rhs->atom_hash &&
           lhs->hash == rhs->hash;
}

static bool profiled_type_cache_copy_types_from_entry(Arena *a,
                                                      const ProfiledTypeListCacheEntry *entry,
                                                      Atom ***out_types,
                                                      uint32_t *out_count) {
    if (!entry || !entry->occupied || !out_types || !out_count)
        return false;
    if (entry->count == 0) {
        *out_types = NULL;
        *out_count = 0;
        return true;
    }
    Atom **types = cetta_malloc(sizeof(Atom *) * entry->count);
    for (uint32_t i = 0; i < entry->count; i++) {
        Atom *fresh = atom_freshen_epoch(a, entry->types[i], fresh_var_suffix());
        types[i] = atom_deep_copy(a, fresh);
    }
    *out_types = types;
    *out_count = entry->count;
    return true;
}

static bool profiled_type_cache_lookup(Space *s, Arena *a, Atom *atom,
                                       Atom ***out_types,
                                       uint32_t *out_count) {
    ProfiledTypeCacheKey key;
    if (!profiled_type_cache_make_key(s, atom, &key))
        return false;
    ProfiledTypeListCacheEntry *entry =
        &g_profiled_type_cache[key.hash % PROFILED_TYPE_CACHE_CAP];
    if (!entry->occupied ||
        !profiled_type_cache_key_matches(&entry->key, &key) ||
        !atom_eq(entry->atom_key, atom)) {
        return false;
    }
    return profiled_type_cache_copy_types_from_entry(a, entry, out_types, out_count);
}

static void profiled_type_cache_store(Space *s, Atom *atom,
                                      Atom **types, uint32_t count) {
    ProfiledTypeCacheKey key;
    if (!profiled_type_cache_make_key(s, atom, &key))
        return;
    for (uint32_t i = 0; i < count; i++) {
        if (!profiled_type_cache_result_stable(types[i]))
            return;
    }
    if (!profiled_type_cache_prepare_store())
        return;

    ProfiledTypeListCacheEntry *entry =
        &g_profiled_type_cache[key.hash % PROFILED_TYPE_CACHE_CAP];
    entry->occupied = true;
    entry->key = key;
    entry->atom_key = atom_deep_copy(&g_profiled_type_cache_arena, atom);
    entry->count = count;
    entry->types = NULL;
    if (count == 0)
        return;
    entry->types =
        arena_alloc(&g_profiled_type_cache_arena, sizeof(Atom *) * count);
    for (uint32_t i = 0; i < count; i++)
        entry->types[i] = atom_deep_copy(&g_profiled_type_cache_arena, types[i]);
}

static bool profiled_type_formation_cache_lookup(Space *s, Atom *ty,
                                                 bool *out_result) {
    ProfiledTypeCacheKey key;
    if (!profiled_type_cache_make_key(s, ty, &key))
        return false;
    ProfiledTypeFormationCacheEntry *entry =
        &g_profiled_type_formation_cache[key.hash % PROFILED_TYPE_FORMATION_CACHE_CAP];
    if (!entry->occupied ||
        !profiled_type_cache_key_matches(&entry->key, &key) ||
        !atom_eq(entry->atom_key, ty)) {
        return false;
    }
    *out_result = entry->result;
    return true;
}

static void profiled_type_formation_cache_store(Space *s, Atom *ty, bool result) {
    ProfiledTypeCacheKey key;
    if (!profiled_type_cache_make_key(s, ty, &key) ||
        !profiled_type_cache_prepare_store()) {
        return;
    }
    ProfiledTypeFormationCacheEntry *entry =
        &g_profiled_type_formation_cache[key.hash % PROFILED_TYPE_FORMATION_CACHE_CAP];
    entry->occupied = true;
    entry->key = key;
    entry->atom_key = atom_deep_copy(&g_profiled_type_cache_arena, ty);
    entry->result = result;
}

static Atom *normalize_type_expr_profiled(Arena *a, Atom *ty) {
    if (ty->kind != ATOM_EXPR || ty->expr.len < 2) return ty;
    Atom **new_elems = arena_alloc(a, sizeof(Atom *) * ty->expr.len);
    bool changed = false;
    for (CettaExprIndex i = 0; i < ty->expr.len; i++) {
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

static bool atom_is_type_sort(Atom *atom) {
    static SymbolId type_id = SYMBOL_ID_NONE;
    if (type_id == SYMBOL_ID_NONE)
        type_id = symbol_intern_cstr(g_symbols, "Type");
    return atom_is_symbol_id(atom, type_id);
}

static bool type_expr_bind_success(Bindings *env, Arena *scratch,
                                   Atom *domain, Atom *arg,
                                   Atom *plain_domain_var) {
    BindingsBuilder bb;
    if (!bindings_builder_init(&bb, env))
        return false;
    bool ok = true;
    if (plain_domain_var) {
        Atom *arg_term =
            bindings_apply_if_vars(bindings_builder_bindings(&bb), scratch, arg);
        ok = bindings_builder_add_var_fresh(&bb, plain_domain_var, arg_term);
    }
    if (ok) {
        Atom *arg_term =
            bindings_apply_if_vars(bindings_builder_bindings(&bb), scratch, arg);
        ok = bind_domain_binder_builder(&bb, domain, arg_term);
    }
    if (ok) {
        Bindings next;
        bindings_init(&next);
        bindings_builder_take(&bb, &next);
        bindings_replace(env, &next);
    } else {
        bindings_builder_free(&bb);
    }
    return ok;
}

static bool type_expr_constructor_contract_matches(Space *s, Arena *a,
                                                   Atom *type_app,
                                                   Atom *func_type) {
    if (!is_function_type(func_type) || !type_app ||
        type_app->kind != ATOM_EXPR || type_app->expr.len < 2) {
        return false;
    }
    if (func_type->expr.len - 2 != type_app->expr.len - 1)
        return false;

    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&scratch, NULL);

    Atom *fresh_ft = atom_freshen_epoch(&scratch, func_type, fresh_var_suffix());
    Bindings env;
    bindings_init(&env);
    bool all_ok = true;

    for (CettaExprIndex ai = 0;
         ai < type_app->expr.len - 1 && all_ok;
         ai++) {
        Atom *domain = fresh_ft->expr.elems[ai + 1];
        Atom *binder = NULL;
        Atom *decl = function_domain_type(&env, &scratch, domain, &binder);
        Atom *arg = type_app->expr.elems[ai + 1];

        if (!type_expr_is_well_formed_profiled(s, &scratch, decl)) {
            all_ok = false;
            break;
        }

        if (decl->kind == ATOM_VAR) {
            all_ok = type_expr_bind_success(&env, &scratch, domain, arg, decl);
            continue;
        }

        if (atom_is_type_sort(decl)) {
            all_ok =
                type_expr_is_well_formed_profiled(s, &scratch, arg) &&
                type_expr_bind_success(&env, &scratch, domain, arg, NULL);
            continue;
        }

        if (atom_is_meta_type(decl)) {
            all_ok =
                atom_meta_type_accepts(&scratch, decl, arg) &&
                type_expr_bind_success(&env, &scratch, domain, arg, NULL);
            continue;
        }

        Atom **arg_types = NULL;
        uint32_t n_arg_types = get_atom_types_profiled(s, &scratch, arg, &arg_types);
        bool found = false;
        SearchContext trial_context;
        if (!search_context_init(&trial_context, &env, &scratch)) {
            free(arg_types);
            all_ok = false;
            break;
        }

        for (uint32_t ti = 0; ti < n_arg_types; ti++) {
            ChoicePoint point = search_context_save(&trial_context);
            if (match_types_builder(decl, arg_types[ti],
                                    search_context_builder(&trial_context))) {
                Atom *arg_term =
                    bindings_apply_if_vars(search_context_bindings(&trial_context),
                                           search_context_scratch(&trial_context),
                                           arg);
                if (bind_domain_binder_builder(search_context_builder(&trial_context),
                                               domain, arg_term)) {
                    Bindings next;
                    bindings_init(&next);
                    search_context_take(&trial_context, &next);
                    bindings_replace(&env, &next);
                    found = true;
                    break;
                }
            }
            search_context_rollback(&trial_context, point);
        }
        search_context_free(&trial_context);
        free(arg_types);
        all_ok = found;
    }

    bool ret_ok = false;
    if (all_ok) {
        Atom *ret = bindings_apply_if_vars(&env, &scratch,
                                           get_function_ret_type(fresh_ft));
        ret = normalize_type_expr_profiled(&scratch, ret);
        ret_ok = atom_is_type_sort(ret);
    }

    bindings_free(&env);
    arena_free(&scratch);
    return all_ok && ret_ok;
}

static bool function_type_expr_is_well_formed_profiled(Space *s, Arena *a,
                                                       Atom *ty) {
    if (!ty || ty->kind != ATOM_EXPR || ty->expr.len == 0 ||
        !atom_is_symbol_id(ty->expr.elems[0], g_builtin_syms.arrow)) {
        return false;
    }
    if (ty->expr.len == 1)
        return true;
    if (!is_function_type(ty))
        return false;
    for (CettaExprIndex i = 1; i + 1 < ty->expr.len; i++) {
        Atom *binder = NULL;
        Atom *body = NULL;
        split_dependent_domain(ty->expr.elems[i], &binder, &body);
        (void)binder;
        if (!type_expr_is_well_formed_profiled(s, a, body))
            return false;
    }
    return type_expr_is_well_formed_profiled(s, a,
                                             get_function_ret_type(ty));
}

static bool type_expr_is_well_formed_profiled_uncached(Space *s, Arena *a,
                                                       Atom *ty) {
    if (!ty)
        return false;
    if (ty->kind == ATOM_GROUNDED)
        return false;
    if (ty->kind == ATOM_SYMBOL || ty->kind == ATOM_VAR)
        return true;
    if (ty->kind != ATOM_EXPR)
        return true;
    if (ty->expr.len == 0)
        return true;
    if (atom_is_symbol_id(ty->expr.elems[0], g_builtin_syms.arrow))
        return function_type_expr_is_well_formed_profiled(s, a, ty);
    if (ty->expr.len < 2)
        return true;

    Atom *head = ty->expr.elems[0];
    Atom **head_types = NULL;
    uint32_t n_head_types = get_atom_types(s, a, head, &head_types);
    bool saw_type_constructor_contract = false;
    bool accepted = false;

    for (uint32_t i = 0; i < n_head_types; i++) {
        Atom *ft = head_types[i];
        if (!is_function_type(ft))
            continue;
        Atom *ret = get_function_ret_type(ft);
        if (!atom_is_type_sort(ret))
            continue;
        saw_type_constructor_contract = true;
        if (type_expr_constructor_contract_matches(s, a, ty, ft)) {
            accepted = true;
            break;
        }
    }

    free(head_types);
    return saw_type_constructor_contract ? accepted : true;
}

static bool type_expr_is_well_formed_profiled(Space *s, Arena *a, Atom *ty) {
    bool result = false;
    if (profiled_type_formation_cache_lookup(s, ty, &result))
        return result;
    result = type_expr_is_well_formed_profiled_uncached(s, a, ty);
    profiled_type_formation_cache_store(s, ty, result);
    return result;
}

static uint32_t filter_well_formed_profiled_types(Space *s, Arena *a,
                                                  Atom ***types_inout,
                                                  uint32_t count) {
    if (!eval_dependent_telescope_enabled() || count == 0 || !types_inout ||
        !*types_inout) {
        return count;
    }
    Atom **types = *types_inout;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (type_expr_is_well_formed_profiled(s, a, types[i]))
            types[kept++] = types[i];
    }
    if (kept == 0) {
        free(types);
        *types_inout = NULL;
    }
    return kept;
}

static bool profile_declared_type_visible_for_atom(Atom *atom, Atom *ty) {
    if (atom && atom->kind == ATOM_SYMBOL) {
        const char *surface = atom_name_cstr(atom);
        if (surface && !active_surface_allowed(surface)) {
            return false;
        }
    }
    if (atom_is_symbol_id(atom, g_builtin_syms.new_space) &&
        is_function_type(ty) &&
        get_function_arg_count(ty) == 1 &&
        !active_surface_allowed("new-space-kind")) {
        return false;
    }
    return true;
}

static uint32_t filter_profile_visible_declared_types(Atom *atom,
                                                     Atom ***types_inout,
                                                     uint32_t count) {
    if (count == 0 || !types_inout || !*types_inout)
        return count;
    Atom **types = *types_inout;
    uint32_t kept = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (profile_declared_type_visible_for_atom(atom, types[i]))
            types[kept++] = types[i];
    }
    if (kept == 0) {
        free(types);
        *types_inout = NULL;
    }
    return kept;
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

        for (CettaExprIndex ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
            Atom *binder = NULL;
            Atom *decl =
                function_domain_type(&tb, &scratch, fresh_ft->expr.elems[ai + 1], &binder);
            if (atom_is_meta_type(decl)) {
                if (!atom_meta_type_accepts(a, decl, atom->expr.elems[ai + 1])) {
                    all_ok = false;
                    continue;
                }
                SearchContext trial_context;
                if (!search_context_init(&trial_context, &tb, &scratch)) {
                    bindings_free(&tb);
                    free(types);
                    arena_free(&scratch);
                    free(op_types);
                    *out_types = NULL;
                    return 0;
                }
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
                } else {
                    all_ok = false;
                }
                search_context_free(&trial_context);
                (void)binder;
                continue;
            }
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
            if (type_expr_is_well_formed_profiled(s, &scratch, concrete_ret)) {
                types = types ? cetta_realloc(types, sizeof(Atom *) * (count + 1))
                              : cetta_malloc(sizeof(Atom *));
                types[count++] = atom_deep_copy(a, concrete_ret);
            }
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

static uint32_t get_atom_types_profiled_uncached(Space *s, Arena *a, Atom *atom,
                                                 Atom ***out_types) {
    uint32_t count = get_atom_types(s, a, atom, out_types);
    count = filter_profile_visible_declared_types(atom, out_types, count);
    count = filter_well_formed_profiled_types(s, a, out_types, count);
    if (!eval_dependent_telescope_enabled() || atom->kind != ATOM_EXPR || count != 0) {
        return count;
    }
    return infer_dependent_application_types(s, a, atom, out_types);
}

static uint32_t get_atom_types_profiled(Space *s, Arena *a, Atom *atom,
                                        Atom ***out_types) {
    uint32_t cached_count = 0;
    if (profiled_type_cache_lookup(s, a, atom, out_types, &cached_count))
        return cached_count;

    uint32_t count = get_atom_types_profiled_uncached(s, a, atom, out_types);
    profiled_type_cache_store(s, atom, *out_types, count);
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
        if (match_types(expectedType, types[i], &mb)) {
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

    CettaExprLen nargs = get_function_arg_count(funcType);
    CettaExprLen expr_narg = (expr->kind == ATOM_EXPR && expr->expr.len > 0)
                             ? expr->expr.len - 1 : 0;

    /* Step 1: arity check */
    if (expr_narg != nargs) {
        errors[(*n_errors)++] = atom_error(a, expr,
            atom_symbol(a, "IncorrectNumberOfArguments"));
        return false;
    }

    Atom **arg_types = nargs
        ? arena_alloc(a, sizeof(Atom *) * (size_t)nargs)
        : NULL;
    if (arg_types)
        get_function_arg_types(funcType, arg_types);

    Atom *retType = get_function_ret_type(funcType);
    if (!retType) retType = atom_undefined_type(a);

    /* Step 2: check each argument type, threading bindings */
    Bindings results[64];
    for (uint32_t i = 0; i < 64; i++) bindings_init(&results[i]);
    uint32_t nresults = 1;
    bindings_init(&results[0]);

    for (CettaExprIndex i = 0; i < nargs && nresults > 0; i++) {
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
                if (nnext < 64) {
                    bindings_move(&next[nnext], &results[r]);
                    nnext++;
                }
                continue;
            }
            if (atom_is_meta_type(expected)) {
                if (atom_meta_type_accepts(a, expected, arg)) {
                    BindingsBuilder candidate_builder;
                    if (bindings_builder_init(&candidate_builder, &results[r])) {
                        if (bind_domain_binder_builder(&candidate_builder,
                                                       arg_types[i], arg) &&
                            nnext < 64 &&
                            bindings_clone(&next[nnext],
                                           bindings_builder_bindings(&candidate_builder))) {
                            nnext++;
                        }
                        bindings_builder_free(&candidate_builder);
                    }
                    found = true;
                } else if (*n_errors < 64) {
                    Atom **actual_types = NULL;
                    uint32_t n_actual_types =
                        get_atom_types_profiled(s, a, arg, &actual_types);
                    Atom *actual_type = n_actual_types > 0
                        ? actual_types[0]
                        : get_meta_type(a, arg);
                    Atom *reason = atom_expr(a, (Atom*[]){
                        atom_symbol(a, "BadArgType"),
                        atom_int(a, (int64_t)(i + 1)),
                        expected,
                        actual_type
                    }, 4);
                    free(actual_types);
                    errors[(*n_errors)++] = atom_error(a, expr, reason);
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
                ? bindings_apply_if_vars(search_context_bindings(&ret_context), a, retType)
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
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard stack_guard = {0};
    if (eval_cancel_check())
        return;
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

    if (atom_is_symbol_id(etype, g_builtin_syms.undefined_type) &&
        atom_is_constructor_normal_form(s, a, atom, fuel)) {
        result_set_add(rs, atom);
        return;
    }

    /* Expression: interpret_expression → metta_call (spec line 262) */
    {
        OutcomeSet os;
        outcome_set_init(&os);
        metta_call(s, a, atom, etype, fuel > 0 ? fuel - 1 : fuel, false, &os);
        outcome_set_normalize_visible_frontier(a, &os);
        for (CettaCount oi = 0; oi < os.len; oi++)
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
    if (eval_cancel_check())
        return;
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
    if (atom_is_constructor_normal_form(s, a, atom, fuel)) {
        outcome_set_add(os, atom, &empty);
        return;
    }
    metta_call(s, a, atom, NULL, fuel > 0 ? fuel - 1 : fuel, true, os);
    if (os->len == 0 && !atom_contains_vars(atom)) {
        metta_call(s, a, atom, NULL, fuel > 0 ? fuel - 1 : fuel, false, os);
    }
    outcome_set_normalize_visible_frontier(a, os);
}

static void metta_eval_bind_typed(Space *s, Arena *a, Atom *type, Atom *atom, int fuel, OutcomeSet *os) {
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard stack_guard = {0};
    if (eval_cancel_check())
        return;
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
        for (CettaCount i = 0; i < rs.len; i++)
            outcome_set_add(os, rs.items[i], &empty);
        result_set_free(&rs);
        return;
    }

    if (atom->kind == ATOM_VAR) {
        outcome_set_add(os, atom, &empty);
        return;
    }

    metta_call(s, a, atom, etype, fuel > 0 ? fuel - 1 : fuel, true, os);
    outcome_set_normalize_visible_frontier(a, os);
}

static void eval_with_prefix_bindings(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                      const Bindings *prefix, OutcomeSet *os) {
    OutcomeSet inner;
    outcome_set_init(&inner);
    metta_eval_bind_typed(s, a, type, atom, fuel, &inner);
    if (!prefix || prefix->len == 0) {
        for (CettaCount i = 0; i < inner.len; i++) {
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
    for (CettaCount i = 0; i < inner.len; i++) {
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
    CettaExprLen len;
    CettaExprLen cap;
} MatchVisibleVarSet;

typedef struct {
    VarId hidden_var_id;
    VarId visible_var_id;
    SymbolId spelling;
} MatchVisibleAliasRef;

typedef struct {
    MatchVisibleAliasRef *items;
    CettaExprLen len;
    CettaExprLen cap;
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
                                          CettaExprLen needed) {
    if (needed <= set->cap)
        return true;
    CettaExprLen next_cap = set->cap ? set->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(MatchVisibleVarRef)))
        return false;
    set->items = set->items
        ? cetta_realloc(set->items, sizeof(MatchVisibleVarRef) * (size_t)next_cap)
        : cetta_malloc(sizeof(MatchVisibleVarRef) * (size_t)next_cap);
    set->cap = next_cap;
    return true;
}

static bool match_visible_var_set_add(MatchVisibleVarSet *set, VarId var_id,
                                      SymbolId spelling) {
    for (CettaExprIndex i = 0; i < set->len; i++) {
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
    for (CettaExprIndex i = 0; i < set->len; i++) {
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
                                            CettaExprLen needed) {
    if (needed <= set->cap)
        return true;
    CettaExprLen next_cap = set->cap ? set->cap * 2 : 8;
    while (next_cap < needed)
        next_cap *= 2;
    if (!cetta_expr_len_mul_fits_size(next_cap, sizeof(MatchVisibleAliasRef)))
        return false;
    set->items = set->items
        ? cetta_realloc(set->items, sizeof(MatchVisibleAliasRef) * (size_t)next_cap)
        : cetta_malloc(sizeof(MatchVisibleAliasRef) * (size_t)next_cap);
    set->cap = next_cap;
    return true;
}

static bool match_visible_alias_set_add(MatchVisibleAliasSet *set,
                                        VarId hidden_var_id,
                                        VarId visible_var_id,
                                        SymbolId spelling) {
    for (CettaExprIndex i = 0; i < set->len; i++) {
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
    for (CettaExprIndex i = 0; i < set->len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
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
    for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
        Atom *child = atom->expr.elems[i];
        Atom *next = rewrite_match_visible_aliases(a, child, aliases);
        if (!rewritten && next != child) {
            rewritten = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            for (CettaExprIndex j = 0; j < i; j++)
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

    for (CettaExprIndex i = 0; i < visible->len; i++) {
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

    for (CettaExprIndex i = 0; i < visible->len; i++) {
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
    for (CettaIndex i = 0; i < goal_instantiation->len; i++) {
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
        for (CettaCount i = 0; i < src->len; i++) {
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
    for (CettaCount i = 0; i < src->len; i++) {
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
        for (CettaCount i = 0; i < src->len; i++) {
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
    for (CettaCount i = 0; i < src->len; i++) {
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
    outcome_set_normalize_visible_frontier(a, &inner);
    outcome_set_append_prefixed_move(a, os, &inner, NULL, false);
    outcome_set_free(&inner);
}

static void eval_direct_outcomes(Space *s, Arena *a, Atom *type, Atom *atom, int fuel,
                                 OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    eval_for_caller(s, a, type, atom, fuel, &empty, false, os);
}

typedef struct {
    Atom **items;
    CettaCount len;
    CettaCount cap;
} MatchResultSnapshot;

typedef struct {
    Space **items;
    CettaCount len;
    CettaCount cap;
} DeferredSpaceSet;

typedef struct {
    Arena arena;
    bool ready;
} MatchResultDirectEvalScratch;

static bool match_result_snapshot_push(MatchResultSnapshot *snapshot,
                                       Atom *atom);

static void deferred_space_set_free(DeferredSpaceSet *set) {
    if (!set)
        return;
    for (CettaCount i = 0; i < set->len; i++)
        space_end_secondary_index_deferral(set->items[i]);
    free(set->items);
    set->items = NULL;
    set->len = 0;
    set->cap = 0;
}

static bool deferred_space_set_add(DeferredSpaceSet *set, Space *space) {
    if (!set || !space)
        return false;
    for (CettaCount i = 0; i < set->len; i++) {
        if (set->items[i] == space)
            return true;
    }
    if (set->len >= set->cap) {
        CettaCount next_cap;
        if (!eval_next_capacity(set->cap, set->len + 1u, sizeof(Space *),
                                &next_cap))
            return false;
        set->items =
            cetta_realloc(set->items, sizeof(Space *) * (size_t)next_cap);
        set->cap = next_cap;
    }
    space_begin_secondary_index_deferral(space);
    set->items[set->len++] = space;
    return true;
}

static void
match_result_direct_eval_scratch_free(MatchResultDirectEvalScratch *scratch) {
    if (!scratch || !scratch->ready)
        return;
    arena_free(&scratch->arena);
    scratch->ready = false;
}

static Arena *match_result_direct_eval_scratch_arena(
    MatchResultDirectEvalScratch *scratch,
    const Arena *seed) {
    (void)seed;
    if (!scratch)
        return NULL;
    if (!scratch->ready) {
        arena_init(&scratch->arena);
        arena_set_runtime_kind(&scratch->arena,
                               CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        /* Direct-effect scratch terms are consumed immediately or promoted
           explicitly. Keep them out of the global hash-cons table so huge FC
           bodies do not pay recursive hash-stability costs just to be reset. */
        arena_set_hashcons(&scratch->arena, NULL);
        scratch->ready = true;
    }
    return &scratch->arena;
}

static void match_chain_note_eval_live_peak(const Arena *a,
                                            CettaRuntimeCounter counter) {
    if (!a)
        return;
    cetta_runtime_stats_update_max(counter, (uint64_t)a->live_bytes);
}

static void match_chain_note_eval_live_delta(const Arena *a,
                                             CettaRuntimeCounter counter,
                                             size_t before_bytes) {
    if (!a || a->live_bytes <= before_bytes)
        return;
    cetta_runtime_stats_update_max(counter,
                                   (uint64_t)(a->live_bytes - before_bytes));
}

static void match_chain_note_size_delta(size_t after_bytes,
                                        size_t before_bytes,
                                        CettaRuntimeCounter counter) {
    if (after_bytes <= before_bytes)
        return;
    cetta_runtime_stats_update_max(counter,
                                   (uint64_t)(after_bytes - before_bytes));
}

static Space *match_result_target_space(Atom *result, Arena *a) {
    if (!result || result->kind != ATOM_EXPR || result->expr.len < 2)
        return NULL;

    SymbolId head_id = atom_head_symbol_id(result);
    if ((head_id == g_builtin_syms.add_atom_nodup &&
         !active_surface_allowed("add-atom-nodup")) ||
        (head_id == g_builtin_syms.space_set_match_backend_bang &&
         !active_surface_allowed("space-set-match-backend!"))) {
        return NULL;
    }
    if (!(head_id == g_builtin_syms.add_atom ||
          head_id == g_builtin_syms.add_atom_nodup ||
          head_id == g_builtin_syms.remove_atom ||
          head_id == g_builtin_syms.mork_add_atom ||
          head_id == g_builtin_syms.mork_add_atoms ||
          head_id == g_builtin_syms.mork_remove_atom ||
          head_id == g_builtin_syms.space_set_backend_bang ||
          head_id == g_builtin_syms.space_set_match_backend_bang)) {
        return NULL;
    }

    Atom *space_ref = resolve_registry_refs(a, expr_arg(result, 0));
    return g_registry ? resolve_registry_space_payload(g_registry, space_ref) : NULL;
}

static bool match_result_targets_any_query_space(Space *target,
                                                 Space **query_spaces,
                                                 uint32_t nquery_spaces) {
    if (!target || !query_spaces || nquery_spaces == 0)
        return false;
    for (uint32_t i = 0; i < nquery_spaces; i++) {
        if (query_spaces[i] == target)
            return true;
    }
    return false;
}

static void match_result_apply_emit_or_snapshot(
    Space *s, Arena *a, int fuel, OutcomeSet *os,
    MatchResultSnapshot *snapshot,
    DeferredSpaceSet *deferred_spaces,
    MatchResultDirectEvalScratch *direct_scratch,
    const Bindings *projected,
    Atom *template,
    Space **query_spaces,
    uint32_t nquery_spaces) {
    Bindings empty;
    Arena *scratch = match_result_direct_eval_scratch_arena(direct_scratch, a);
    Arena *apply_arena = scratch ? scratch : a;
    ArenaMark mark = scratch ? arena_mark(scratch) : (ArenaMark){0};
    Atom *result = bindings_apply_if_vars(projected, apply_arena, template);
    Space *target = match_result_target_space(result, apply_arena);
    bindings_init(&empty);
    if (!match_result_targets_any_query_space(target, query_spaces,
                                              nquery_spaces)) {
        OutcomeSet generated;
        outcome_set_init(&generated);
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_MATCH_RESULT_DIRECT_STREAM_COUNT);
        (void)deferred_space_set_add(deferred_spaces, target);
        eval_for_caller(s, apply_arena, NULL, result, fuel, &empty, false,
                        &generated);
        cetta_runtime_stats_add(
            CETTA_RUNTIME_COUNTER_MATCH_RESULT_DIRECT_GENERATED_OUTCOME_COUNT,
            generated.len);
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_MATCH_RESULT_DIRECT_GENERATED_OUTCOME_PEAK,
            generated.len);
        outcome_set_append_promoted(a, os, &generated, false);
        outcome_set_free(&generated);
        if (scratch)
            arena_reset(scratch, mark);
        return;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_MATCH_RESULT_SNAPSHOT_COUNT);
    if (scratch) {
        Atom *promoted = atom_deep_copy(a, result);
        if (promoted)
            (void)match_result_snapshot_push(snapshot, promoted);
        arena_reset(scratch, mark);
        return;
    }
    (void)match_result_snapshot_push(snapshot, result);
}

static bool match_result_snapshot_push(MatchResultSnapshot *snapshot,
                                       Atom *atom) {
    if (!snapshot)
        return false;
    if (snapshot->len >= snapshot->cap) {
        CettaCount next_cap;
        if (!eval_next_capacity(snapshot->cap, snapshot->len + 1u,
                                sizeof(Atom *), &next_cap))
            return false;
        snapshot->items =
            cetta_realloc(snapshot->items, sizeof(Atom *) * (size_t)next_cap);
        snapshot->cap = next_cap;
    }
    snapshot->items[snapshot->len++] = atom;
    return true;
}

static void match_result_snapshot_eval(Space *s, Arena *a,
                                       MatchResultSnapshot *snapshot,
                                       int fuel, OutcomeSet *os) {
    Bindings empty;
    bindings_init(&empty);
    if (!snapshot)
        return;
    for (CettaCount i = 0; i < snapshot->len; i++)
        eval_for_caller(s, a, NULL, snapshot->items[i], fuel, &empty, false, os);
}

static void match_result_snapshot_free(MatchResultSnapshot *snapshot) {
    if (!snapshot)
        return;
    free(snapshot->items);
    snapshot->items = NULL;
    snapshot->len = 0;
    snapshot->cap = 0;
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
                                    Atom **orig_args, Atom **arg_types,
                                    CettaExprLen nargs, CettaExprIndex idx,
                                    Atom **prefix,
                                    const Bindings *env, int fuel, OutcomeSet *os) {
    if (idx == nargs) {
        Atom **call_elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
        call_elems[0] = op;
        for (CettaExprIndex i = 0; i < nargs; i++)
            call_elems[i + 1] = prefix[i];
        outcome_set_add(os, atom_expr(a, call_elems, nargs + 1), env);
        return;
    }

    Atom *orig_arg = orig_args[idx];
    Atom *arg_type = function_domain_type((Bindings *)env, a, arg_types[idx], NULL);
    Atom *bound_arg = bindings_apply_if_vars(env, a, orig_arg);
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
    for (CettaCount i = 0; i < arg_os.len; i++) {
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
    if (eval_mark_hyperpose_thread_unsafe()) {
        outcome_set_add(os, atom_empty(a), prefix);
        return;
    }

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
    if (eval_mark_hyperpose_thread_unsafe()) {
        outcome_set_add(os, atom_empty(a), prefix);
        return true;
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

    for (CettaCount i = 0; i < rs.len; i++)
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
    if (op->kind == ATOM_EXPR)
        return false;
    OutcomeSet heads;
    outcome_set_init(&heads);
    metta_eval_bind(s, a, op, fuel, &heads);

    bool saw_capture = false;
    bool saw_other = false;
        for (CettaCount hi = 0; hi < heads.len; hi++) {
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
    for (CettaCount hi = 0; hi < heads.len; hi++) {
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

        CettaExprLen expr_narg = atom->expr.len - 1;
        Atom **arg_types = expr_narg
            ? arena_alloc(a, sizeof(Atom *) * (size_t)expr_narg)
            : NULL;
        if (arg_types)
            get_function_arg_types(head_type, arg_types);

        OutcomeSet call_terms;
        outcome_set_init(&call_terms);
        Atom **prefix = expr_narg
            ? arena_alloc(a, sizeof(Atom *) * (size_t)expr_narg)
            : NULL;
        interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                expr_narg, 0, prefix, head_env, fuel, &call_terms);

        for (CettaCount ci = 0; ci < call_terms.len; ci++) {
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
    CettaOutcomeErrorPreview preview;
    if (!out)
        return false;
    if (out->materialized_atom)
        return atom_is_error(out->materialized_atom);
    candidate = outcome_preview_atom(out);
    if (!candidate)
        return false;
    preview = outcome_error_preview_from_atom(&out->env, &out->variant,
                                              candidate, 0);
    if (preview == CETTA_OUTCOME_ERROR_PREVIEW_TRUE)
        return true;
    if (preview == CETTA_OUTCOME_ERROR_PREVIEW_FALSE)
        return false;
    return atom_is_error(outcome_atom_materialize_traced(a, out, site_counter));
}

typedef enum {
    INTERPRET_TUPLE_FRAME_ENTER = 0,
    INTERPRET_TUPLE_FRAME_ITERATE = 1,
    INTERPRET_TUPLE_FRAME_AFTER_CHILD = 2,
} InterpretTupleFrameState;

/* Explicit frames replace recursive tuple interpretation so very wide tuples
   do not consume one C stack frame per element. ENTER/ITERATE/AFTER_CHILD
   mirrors the old DFS recursion, preserving left-to-right binding threading,
   result order, and empty/error short-circuit behavior. */
typedef struct {
    uint32_t idx;
    const Bindings *ctx;
    const VariantInstance *prefix_variant;
    ResultBindSet sub;
    BindingsBuilder merged_builder;
    BindingsMergeAttempt active_attempt;
    VariantInstance active_variant;
    uint32_t sub_index;
    InterpretTupleFrameState state;
    bool sub_initialized;
    bool builder_initialized;
    bool active_attempt_initialized;
    bool active_variant_owned;
} InterpretTupleFrame;

static void interpret_tuple_frame_init(InterpretTupleFrame *frame,
                                       uint32_t idx,
                                       const Bindings *ctx,
                                       const VariantInstance *prefix_variant) {
    frame->idx = idx;
    frame->ctx = ctx;
    frame->prefix_variant = prefix_variant;
    frame->sub_index = 0;
    frame->state = INTERPRET_TUPLE_FRAME_ENTER;
    frame->sub_initialized = false;
    frame->builder_initialized = false;
    frame->active_attempt_initialized = false;
    frame->active_variant_owned = false;
    variant_instance_init(&frame->active_variant);
}

static void interpret_tuple_frame_finish_child(InterpretTupleFrame *frame) {
    if (frame->active_attempt_initialized) {
        bindings_merge_attempt_finish(&frame->merged_builder,
                                      &frame->active_attempt);
        frame->active_attempt_initialized = false;
    }
    if (frame->active_variant_owned) {
        variant_instance_free(&frame->active_variant);
        frame->active_variant_owned = false;
    }
    frame->state = INTERPRET_TUPLE_FRAME_ITERATE;
}

static void interpret_tuple_frame_cleanup(InterpretTupleFrame *frame) {
    if (!frame)
        return;
    if (frame->active_attempt_initialized)
        interpret_tuple_frame_finish_child(frame);
    if (frame->builder_initialized) {
        bindings_builder_free(&frame->merged_builder);
        frame->builder_initialized = false;
    }
    if (frame->sub_initialized) {
        outcome_set_free(&frame->sub);
        frame->sub_initialized = false;
    }
    variant_instance_free(&frame->active_variant);
}

static void interpret_tuple(Space *s, Arena *a,
                            Atom **orig_elems, uint32_t len,
                            uint32_t idx, Atom **prefix,
                            Bindings *ctx, const VariantInstance *prefix_variant,
                            int fuel, ResultBindSet *rbs) {
    if (idx > len)
        return;

    Bindings empty;
    bindings_init(&empty);

    size_t frame_cap = (size_t)len - (size_t)idx + 1;
    if (frame_cap > SIZE_MAX / sizeof(InterpretTupleFrame))
        return;

    enum { INTERPRET_TUPLE_INLINE_FRAMES = 8 };
    InterpretTupleFrame inline_frames[INTERPRET_TUPLE_INLINE_FRAMES];
    InterpretTupleFrame *frames =
        frame_cap <= INTERPRET_TUPLE_INLINE_FRAMES
            ? inline_frames
            : cetta_malloc(sizeof(InterpretTupleFrame) * frame_cap);
    size_t frame_len = 0;
    interpret_tuple_frame_init(&frames[frame_len++], idx, ctx, prefix_variant);

    while (frame_len > 0) {
        InterpretTupleFrame *frame = &frames[frame_len - 1];

        if (frame->state == INTERPRET_TUPLE_FRAME_AFTER_CHILD) {
            interpret_tuple_frame_finish_child(frame);
            continue;
        }

        if (frame->idx == len) {
            Atom *tuple_atom = atom_expr(a, prefix, len);
            if (frame->prefix_variant &&
                variant_instance_present(frame->prefix_variant)) {
                outcome_set_add_with_variant(rbs, tuple_atom, frame->ctx,
                                             frame->prefix_variant);
            } else {
                rb_set_add(rbs, tuple_atom, (Bindings *)frame->ctx);
            }
            interpret_tuple_frame_cleanup(frame);
            frame_len--;
            continue;
        }

        if (frame->state == INTERPRET_TUPLE_FRAME_ENTER) {
            Atom *elem = bindings_apply_if_vars(frame->ctx, a,
                                                orig_elems[frame->idx]);
            rb_set_init(&frame->sub);
            frame->sub_initialized = true;
            metta_eval_bind(s, a, elem, fuel, &frame->sub);
            if (frame->sub.len == 0) {
                interpret_tuple_frame_cleanup(frame);
                frame_len--;
                continue;
            }
            if (!bindings_builder_init(&frame->merged_builder, frame->ctx)) {
                interpret_tuple_frame_cleanup(frame);
                frame_len--;
                continue;
            }
            frame->builder_initialized = true;
            frame->state = INTERPRET_TUPLE_FRAME_ITERATE;
        }

        if (frame->sub_index >= frame->sub.len) {
            interpret_tuple_frame_cleanup(frame);
            frame_len--;
            continue;
        }

        Outcome *sub_item = &frame->sub.items[frame->sub_index++];
        Atom *sub_atom = sub_item->atom;
        Atom *effective_atom = sub_atom;
        const VariantInstance *next_variant_ref = frame->prefix_variant;
        VariantInstance next_variant;
        bool owns_next_variant = false;
        variant_instance_init(&next_variant);

        if (atom_is_empty(sub_atom) ||
            outcome_atom_is_error_at_site(
                a, sub_item,
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_INTERPRET_TUPLE)) {
            effective_atom = outcome_atom_materialize_traced(
                a, sub_item,
                CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_INTERPRET_TUPLE);
            rb_set_add(rbs, effective_atom, &empty);
            continue;
        }
        if (variant_instance_present(&sub_item->variant)) {
            if (frame->prefix_variant &&
                variant_instance_present(frame->prefix_variant) &&
                !variant_instance_clone(&next_variant, frame->prefix_variant)) {
                variant_instance_free(&next_variant);
                continue;
            }
            if (!variant_instance_append_rebased(a, &next_variant, &effective_atom,
                                                 sub_atom, &sub_item->variant)) {
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
            continue;
        }

        BindingsMergeAttempt attempt;
        if (!bindings_builder_merge_or_clone(&frame->merged_builder, frame->ctx,
                                             &sub_item->env, &attempt)) {
            if (owns_next_variant)
                variant_instance_free(&next_variant);
            continue;
        }

        prefix[frame->idx] = effective_atom;
        frame->active_attempt = attempt;
        if (!frame->active_attempt.used_builder)
            frame->active_attempt.env = &frame->active_attempt.owned;
        assert(frame->active_attempt.used_builder ||
               frame->active_attempt.env == &frame->active_attempt.owned);
        frame->active_attempt_initialized = true;
        const Bindings *child_ctx = frame->active_attempt.env;
        if (owns_next_variant) {
            variant_instance_move(&frame->active_variant, &next_variant);
            frame->active_variant_owned = true;
            next_variant_ref = &frame->active_variant;
        }
        frame->state = INTERPRET_TUPLE_FRAME_AFTER_CHILD;
        assert(frame_len < frame_cap);
        interpret_tuple_frame_init(&frames[frame_len++], frame->idx + 1,
                                   child_ctx, next_variant_ref);
    }

    if (frames != inline_frames)
        free(frames);
}

static __attribute__((noinline)) bool
handle_match(Space *s, Arena *a, Atom *atom, int fuel, bool preserve_bindings,
             OutcomeSet *os) {
    Bindings _empty; bindings_init(&_empty);
    __attribute__((cleanup(deferred_space_set_free)))
    DeferredSpaceSet deferred_spaces = {0};
    __attribute__((cleanup(match_result_direct_eval_scratch_free)))
    MatchResultDirectEvalScratch direct_scratch = {0};
    CettaExprLen nargs = expr_nargs(atom);
    if (atom_head_symbol_id(atom) != g_builtin_syms.match || nargs != 3) return false;

    Atom *space_ref = expr_arg(atom, 0);
    Atom *mork_args[] = { expr_arg(atom, 0), expr_arg(atom, 1), expr_arg(atom, 2) };
    if (emit_generic_mork_handle_match_surface(s, a, atom, mork_args, fuel, os)) {
        return true;
    }
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
        CettaExprLen n_conjuncts = pattern->expr.len - 1;
        BindingSet matches;
        MatchVisibleVarSet visible;
        match_visible_var_set_init(&visible);
        if (!collect_match_visible_vars_rec(pattern, &visible)) {
            match_visible_var_set_free(&visible);
            return true;
        }
        space_query_conjunction(ms, a, pattern->expr.elems + 1, n_conjuncts,
                                NULL, &matches);
        if (!preserve_bindings) {
            MatchResultSnapshot snapshot = {0};
            Space *query_spaces[] = { ms };
            for (CettaIndex bi = 0; bi < matches.len; bi++) {
                Bindings projected;
                if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                    &projected)) {
                    continue;
                }
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_MATCH_TEMPLATE);
                match_result_apply_emit_or_snapshot(
                    s, a, fuel, os, &snapshot, &deferred_spaces,
                    &direct_scratch, &projected, template,
                    query_spaces, 1);
                bindings_free(&projected);
            }
            match_result_snapshot_eval(s, a, &snapshot, fuel, os);
            match_result_snapshot_free(&snapshot);
        } else {
            for (CettaIndex bi = 0; bi < matches.len; bi++) {
                Bindings projected;
                if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                    &projected)) {
                    continue;
                }
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_MATCH_TEMPLATE);
                Atom *result = bindings_apply_if_vars(&projected, a, template);
                eval_for_caller(s, a, NULL, result, fuel, &projected,
                                preserve_bindings, os);
                bindings_free(&projected);
            }
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
                    ? resolve_registry_space_payload(g_registry, inner_ref)
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
                if (!preserve_bindings) {
                    MatchResultSnapshot snapshot = {0};
                    Space *query_spaces[] = { ms };
                    for (CettaIndex bi = 0; bi < matches.len; bi++) {
                        Bindings projected;
                        if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                            &projected)) {
                            continue;
                        }
                        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_MATCH_TEMPLATE);
                        match_result_apply_emit_or_snapshot(
                            s, a, fuel, os, &snapshot, &deferred_spaces,
                            &direct_scratch, &projected, residual_body,
                            query_spaces, 1);
                        bindings_free(&projected);
                    }
                    match_result_snapshot_eval(s, a, &snapshot, fuel, os);
                    match_result_snapshot_free(&snapshot);
                } else {
                    for (CettaIndex bi = 0; bi < matches.len; bi++) {
                        Bindings projected;
                        if (!project_match_visible_bindings(a, &visible, &matches.items[bi],
                                                            &projected)) {
                            continue;
                        }
                        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_MATCH_TEMPLATE);
                        Atom *result = bindings_apply_if_vars(&projected, a, residual_body);
                        eval_for_caller(s, a, NULL, result, fuel, &projected,
                                        preserve_bindings, os);
                        bindings_free(&projected);
                    }
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
                ? resolve_registry_space_payload(g_registry, inner_ref)
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
                bool stack_full = false;
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
                        for (CettaExprIndex j = 0; j < cur->expr.len; j++) {
                            if (sp >= 64) {
                                stack_full = true;
                                break;
                            }
                            stack[sp++] = cur->expr.elems[j];
                        }
                        if (stack_full)
                            break;
                    }
                }
                if (stack_full) {
                    nsteps = 0;
                    break;
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

        Bindings frontier_inline_a[1];
        Bindings frontier_inline_b[1];
        Bindings *cur_inline = frontier_inline_a;
        Bindings *next_inline = frontier_inline_b;
        Bindings *cur_binds = cur_inline;
        uint32_t ncur = 1;
        bindings_init(&cur_binds[0]);
        cetta_runtime_stats_update_max(
            CETTA_RUNTIME_COUNTER_MATCH_CHAIN_FRONTIER_BINDINGS_PEAK, ncur);

        uint32_t accum_steps = (nsteps > 1) ? nsteps - 1 : nsteps;
        for (uint32_t si = 0; si < accum_steps; si++) {
            MatchStep *step = &steps[si];
            Bindings *next_binds = next_inline;
            uint32_t nnext = 0, cnext = 1;
            for (uint32_t bi = 0; bi < ncur; bi++) {
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_STEP);
                size_t grounded_before = a ? a->live_bytes : 0;
                Atom *grounded =
                    bindings_apply_if_vars(&cur_binds[bi], a, step->pattern);
                match_chain_note_eval_live_delta(
                    a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_GROUNDED_DELTA_BYTES_PEAK,
                    grounded_before);
                match_chain_note_eval_live_peak(
                    a,
                    CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_GROUNDED_PEAK);
                SubstMatchSet smr;
                smset_init(&smr);
                size_t query_before = a ? a->live_bytes : 0;
                size_t query_entry_before = bindings_entry_active_bytes();
                size_t query_constraint_before =
                    bindings_constraint_active_bytes();
                space_subst_query(step->space, a, grounded, &smr);
                match_chain_note_eval_live_delta(
                    a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_DELTA_BYTES_PEAK,
                    query_before);
                match_chain_note_size_delta(
                    bindings_entry_active_bytes(), query_entry_before,
                    CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_BINDINGS_ENTRY_DELTA_PEAK);
                match_chain_note_size_delta(
                    bindings_constraint_active_bytes(),
                    query_constraint_before,
                    CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_BINDINGS_CONSTRAINT_DELTA_PEAK);
                cetta_runtime_stats_update_max(
                    CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SUBST_RESULTS_PEAK,
                    smr.len);
                cetta_runtime_stats_update_max(
                    CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SUBSTMATCHSET_BYTES_PEAK,
                    (uint64_t)smr.cap * (uint64_t)sizeof(SubstMatch));
                match_chain_note_eval_live_peak(
                    a,
                    CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_QUERY_PEAK);
                for (CettaIndex ci = 0; ci < smr.len; ci++) {
                    Bindings mb;
                    size_t merge_entry_before = bindings_entry_active_bytes();
                    size_t merge_constraint_before =
                        bindings_constraint_active_bytes();
                    if (space_subst_match_with_seed(step->space, grounded,
                                                    &smr.items[ci],
                                                    &cur_binds[bi], a, &mb)) {
                        match_chain_note_size_delta(
                            bindings_entry_active_bytes(), merge_entry_before,
                            CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SEED_MERGE_BINDINGS_ENTRY_DELTA_PEAK);
                        match_chain_note_size_delta(
                            bindings_constraint_active_bytes(),
                            merge_constraint_before,
                            CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SEED_MERGE_BINDINGS_CONSTRAINT_DELTA_PEAK);
                        if (nnext >= cnext) {
                            if (!bindings_array_grow(&next_binds, nnext, &cnext,
                                                     next_inline)) {
                                bindings_free(&mb);
                                continue;
                            }
                        }
                        bindings_move(&next_binds[nnext++], &mb);
                        cetta_runtime_stats_update_max(
                            CETTA_RUNTIME_COUNTER_MATCH_CHAIN_FRONTIER_BINDINGS_PEAK,
                            nnext);
                    }
                }
                smset_free(&smr);
            }
            bindings_array_release(cur_binds, ncur, cur_inline);
            cur_binds = next_binds;
            {
                Bindings *prev_inline = cur_inline;
                cur_inline = next_inline;
                next_inline = prev_inline;
            }
            ncur = nnext;
            if (ncur == 0) break;
        }

        if (nsteps > 1 && ncur > 0) {
            MatchStep *last = &steps[nsteps - 1];
            static uint64_t g_chain_progress = 0;
            if (!preserve_bindings) {
                MatchResultSnapshot snapshot = {0};
                Space *query_spaces[MAX_CHAIN];
                uint32_t nquery_spaces = nsteps;
                for (uint32_t qi = 0; qi < nsteps; qi++)
                    query_spaces[qi] = steps[qi].space;
                for (uint32_t bi = 0; bi < ncur; bi++) {
                    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_LAST);
                    size_t grounded_before = a ? a->live_bytes : 0;
                    Atom *grounded =
                        bindings_apply_if_vars(&cur_binds[bi], a, last->pattern);
                    match_chain_note_eval_live_delta(
                        a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_GROUNDED_DELTA_BYTES_PEAK,
                        grounded_before);
                    match_chain_note_eval_live_peak(
                        a,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_GROUNDED_PEAK);
                    SubstMatchSet smr;
                    smset_init(&smr);
                    size_t query_before = a ? a->live_bytes : 0;
                    size_t query_entry_before = bindings_entry_active_bytes();
                    size_t query_constraint_before =
                        bindings_constraint_active_bytes();
                    space_subst_query(last->space, a, grounded, &smr);
                    match_chain_note_eval_live_delta(
                        a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_DELTA_BYTES_PEAK,
                        query_before);
                    match_chain_note_size_delta(
                        bindings_entry_active_bytes(), query_entry_before,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_BINDINGS_ENTRY_DELTA_PEAK);
                    match_chain_note_size_delta(
                        bindings_constraint_active_bytes(),
                        query_constraint_before,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_BINDINGS_CONSTRAINT_DELTA_PEAK);
                    cetta_runtime_stats_update_max(
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SUBST_RESULTS_PEAK,
                        smr.len);
                    cetta_runtime_stats_update_max(
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SUBSTMATCHSET_BYTES_PEAK,
                        (uint64_t)smr.cap * (uint64_t)sizeof(SubstMatch));
                    match_chain_note_eval_live_peak(
                        a,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_QUERY_PEAK);
                    for (CettaIndex ci = 0; ci < smr.len; ci++) {
                        Bindings mb;
                        size_t merge_entry_before = bindings_entry_active_bytes();
                        size_t merge_constraint_before =
                            bindings_constraint_active_bytes();
                        if (space_subst_match_with_seed(last->space, grounded,
                                                        &smr.items[ci],
                                                        &cur_binds[bi], a, &mb)) {
                            match_chain_note_size_delta(
                                bindings_entry_active_bytes(),
                                merge_entry_before,
                                CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SEED_MERGE_BINDINGS_ENTRY_DELTA_PEAK);
                            match_chain_note_size_delta(
                                bindings_constraint_active_bytes(),
                                merge_constraint_before,
                                CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SEED_MERGE_BINDINGS_CONSTRAINT_DELTA_PEAK);
                            Bindings projected;
                            size_t project_before = a ? a->live_bytes : 0;
                            if (!project_match_visible_bindings(a, &visible, &mb,
                                                                &projected)) {
                                bindings_free(&mb);
                                continue;
                            }
                            match_chain_note_eval_live_delta(
                                a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_PROJECT_DELTA_BYTES_PEAK,
                                project_before);
                            match_chain_note_eval_live_peak(
                                a,
                                CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_PROJECT_PEAK);
                            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_BODY);
                            match_result_apply_emit_or_snapshot(
                                s, a, fuel, os, &snapshot, &deferred_spaces,
                                &direct_scratch, &projected, body,
                                query_spaces, nquery_spaces);
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
                match_result_snapshot_eval(s, a, &snapshot, fuel, os);
                match_result_snapshot_free(&snapshot);
            } else {
                for (uint32_t bi = 0; bi < ncur; bi++) {
                    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_LAST);
                    size_t grounded_before = a ? a->live_bytes : 0;
                    Atom *grounded =
                        bindings_apply_if_vars(&cur_binds[bi], a, last->pattern);
                    match_chain_note_eval_live_delta(
                        a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_GROUNDED_DELTA_BYTES_PEAK,
                        grounded_before);
                    match_chain_note_eval_live_peak(
                        a,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_GROUNDED_PEAK);
                    SubstMatchSet smr;
                    smset_init(&smr);
                    size_t query_before = a ? a->live_bytes : 0;
                    size_t query_entry_before = bindings_entry_active_bytes();
                    size_t query_constraint_before =
                        bindings_constraint_active_bytes();
                    space_subst_query(last->space, a, grounded, &smr);
                    match_chain_note_eval_live_delta(
                        a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_DELTA_BYTES_PEAK,
                        query_before);
                    match_chain_note_size_delta(
                        bindings_entry_active_bytes(), query_entry_before,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_BINDINGS_ENTRY_DELTA_PEAK);
                    match_chain_note_size_delta(
                        bindings_constraint_active_bytes(),
                        query_constraint_before,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_QUERY_BINDINGS_CONSTRAINT_DELTA_PEAK);
                    cetta_runtime_stats_update_max(
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SUBST_RESULTS_PEAK,
                        smr.len);
                    cetta_runtime_stats_update_max(
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SUBSTMATCHSET_BYTES_PEAK,
                        (uint64_t)smr.cap * (uint64_t)sizeof(SubstMatch));
                    match_chain_note_eval_live_peak(
                        a,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_QUERY_PEAK);
                    for (CettaIndex ci = 0; ci < smr.len; ci++) {
                        Bindings mb;
                        size_t merge_entry_before = bindings_entry_active_bytes();
                        size_t merge_constraint_before =
                            bindings_constraint_active_bytes();
                        if (space_subst_match_with_seed(last->space, grounded,
                                                        &smr.items[ci],
                                                        &cur_binds[bi], a, &mb)) {
                            match_chain_note_size_delta(
                                bindings_entry_active_bytes(),
                                merge_entry_before,
                                CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SEED_MERGE_BINDINGS_ENTRY_DELTA_PEAK);
                            match_chain_note_size_delta(
                                bindings_constraint_active_bytes(),
                                merge_constraint_before,
                                CETTA_RUNTIME_COUNTER_MATCH_CHAIN_SEED_MERGE_BINDINGS_CONSTRAINT_DELTA_PEAK);
                            Bindings projected;
                            size_t project_before = a ? a->live_bytes : 0;
                            if (!project_match_visible_bindings(a, &visible, &mb,
                                                                &projected)) {
                                bindings_free(&mb);
                                continue;
                            }
                            match_chain_note_eval_live_delta(
                                a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_PROJECT_DELTA_BYTES_PEAK,
                                project_before);
                            match_chain_note_eval_live_peak(
                                a,
                                CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_PROJECT_PEAK);
                            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_BODY);
                            Atom *result = bindings_apply_if_vars(&projected, a, body);
                            eval_for_caller(s, a, NULL, result, fuel, &projected,
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
            }
        } else {
            if (!preserve_bindings) {
                MatchResultSnapshot snapshot = {0};
                Space *query_spaces[MAX_CHAIN];
                uint32_t nquery_spaces = nsteps;
                for (uint32_t qi = 0; qi < nsteps; qi++)
                    query_spaces[qi] = steps[qi].space;
                for (uint32_t bi = 0; bi < ncur; bi++) {
                    Bindings projected;
                    size_t project_before = a ? a->live_bytes : 0;
                    if (!project_match_visible_bindings(a, &visible, &cur_binds[bi],
                                                        &projected)) {
                        continue;
                    }
                    match_chain_note_eval_live_delta(
                        a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_PROJECT_DELTA_BYTES_PEAK,
                        project_before);
                    match_chain_note_eval_live_peak(
                        a,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_PROJECT_PEAK);
                    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_BODY);
                    match_result_apply_emit_or_snapshot(
                        s, a, fuel, os, &snapshot, &deferred_spaces,
                        &direct_scratch, &projected, body,
                        query_spaces, nquery_spaces);
                    bindings_free(&projected);
                }
                match_result_snapshot_eval(s, a, &snapshot, fuel, os);
                match_result_snapshot_free(&snapshot);
            } else {
                for (uint32_t bi = 0; bi < ncur; bi++) {
                    Bindings projected;
                    size_t project_before = a ? a->live_bytes : 0;
                    if (!project_match_visible_bindings(a, &visible, &cur_binds[bi],
                                                        &projected)) {
                        continue;
                    }
                    match_chain_note_eval_live_delta(
                        a, CETTA_RUNTIME_COUNTER_MATCH_CHAIN_PROJECT_DELTA_BYTES_PEAK,
                        project_before);
                    match_chain_note_eval_live_peak(
                        a,
                        CETTA_RUNTIME_COUNTER_MATCH_CHAIN_EVAL_BYTES_AFTER_PROJECT_PEAK);
                    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_APPLY_EVAL_CHAIN_BODY);
                    Atom *result = bindings_apply_if_vars(&projected, a, body);
                    eval_for_caller(s, a, NULL, result, fuel, &projected,
                                    preserve_bindings, os);
                    bindings_free(&projected);
                }
            }
        }
        match_visible_var_set_free(&visible);
        bindings_array_release(cur_binds, ncur, cur_inline);
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
        for (CettaExprIndex i = 0; i < payload->expr.len; i++) {
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

typedef struct {
    OutcomeSet *outcomes;
} DirectMorkAtomEmitCtx;

static bool direct_mork_emit_atom(Atom *atom, void *ctx) {
    DirectMorkAtomEmitCtx *emit = ctx;
    Bindings empty_bindings;
    if (!emit || !emit->outcomes || !atom)
        return false;
    bindings_init(&empty_bindings);
    outcome_set_add(emit->outcomes, atom, &empty_bindings);
    bindings_free(&empty_bindings);
    return true;
}

static bool emit_direct_mork_atoms_rows(Space *s, Arena *a, Atom *surface_atom,
                                        Atom **args, int fuel,
                                        OutcomeSet *os) {
    (void)s;
    (void)fuel;
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
    TermUniverse *universe = eval_current_term_universe();
    if (!universe)
        return false;
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    DirectMorkAtomEmitCtx emit = {.outcomes = os};
    bool ok = space_match_backend_mork_visit_atoms_direct(
        bridge, universe, &scratch, direct_mork_emit_atom, &emit);
    arena_free(&scratch);
    if (!ok) {
        Atom *space_error = space_backend_error_if_set(a, surface_atom);
        const char *err = cetta_mork_bridge_last_error();
        outcome_set_add(os,
                        space_error ? space_error
                                    : atom_error(a, surface_atom,
                                                 atom_string(a, err && *err
                                                                    ? err
                                                                    : "MORK atom stream failed")),
                        &empty);
        return true;
    }
    return true;
}

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

typedef struct {
    Space *space;
    Arena *arena;
    Atom *templ;
    int fuel;
    uint64_t count;
    bool unsupported;
} MatchCountCtx;

static bool atom_is_single_result_data(Space *s, Atom *atom, int fuel) {
    if (!atom || atom_is_error(atom))
        return false;
    if (atom_is_empty(atom))
        return true;
    if (atom_eval_is_immediate_value(atom, fuel))
        return true;
    if (atom->kind == ATOM_EXPR && atom->expr.len > 0) {
        Atom *head = atom->expr.elems[0];
        if (head->kind != ATOM_SYMBOL)
            return false;
        if (is_grounded_op(head->sym_id))
            return false;
        if (space_equations_may_match_known_head(s, head->sym_id))
            return false;
        return true;
    }
    return false;
}

static bool count_template_units(Space *s, Arena *a, Atom *templ,
                                 const Bindings *bindings, int fuel,
                                 uint64_t *out_units) {
    Atom *result;
    if (!a || !templ || !bindings || !out_units)
        return false;
    *out_units = 0;
    result = bindings_apply_if_vars(bindings, a, templ);
    if (atom_is_empty(result))
        return true;
    if (!atom_is_single_result_data(s, result, fuel))
        return false;
    *out_units = 1;
    return true;
}

static bool count_mork_match_row(const Bindings *bindings, void *ctx) {
    MatchCountCtx *count = ctx;
    uint64_t units = 0;
    if (!count_template_units(count->space, count->arena, count->templ, bindings,
                              count->fuel, &units)) {
        count->unsupported = true;
        return false;
    }
    count->count += units;
    return true;
}

static bool try_count_mork_match_collapse(Space *s, Arena *a, Atom *match_atom,
                                          int fuel, uint64_t *out_count) {
    Atom *space_arg;
    Atom *pattern;
    Atom *templ;
    CettaMorkSpaceHandle *bridge = NULL;
    MatchCountCtx ctx;
    bool ok;

    if (!match_atom || match_atom->kind != ATOM_EXPR ||
        atom_head_symbol_id(match_atom) != g_builtin_syms.mork_match_surface ||
        expr_nargs(match_atom) != 3 || !g_library_context || !out_count) {
        return false;
    }

    space_arg = resolve_registry_refs(a, expr_arg(match_atom, 0));
    if (!cetta_library_lookup_explicit_mork_bridge(g_library_context, space_arg,
                                                   &bridge) || !bridge) {
        return false;
    }
    pattern = resolve_registry_refs(a, expr_arg(match_atom, 1));
    templ = resolve_registry_refs(a, expr_arg(match_atom, 2));
    ctx = (MatchCountCtx){
        .space = s,
        .arena = a,
        .templ = templ,
        .fuel = fuel,
        .count = 0,
        .unsupported = false,
    };

    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        ok = space_match_backend_mork_visit_conjunction_direct(
            bridge, a, pattern->expr.elems + 1, pattern->expr.len - 1, NULL,
            count_mork_match_row, &ctx);
    } else {
        ok = space_match_backend_mork_visit_bindings_direct(
            bridge, a, pattern, count_mork_match_row, &ctx);
    }
    if (!ok || ctx.unsupported)
        return false;
    *out_count = ctx.count;
    (void)s;
    return true;
}

static bool count_projected_template(Arena *a, const MatchVisibleVarSet *visible,
                                     const Bindings *bindings, Atom *templ,
                                     Space *s, int fuel, uint64_t *count) {
    Bindings projected;
    uint64_t units = 0;
    if (!project_match_visible_bindings(a, visible, bindings, &projected))
        return true;
    if (!count_template_units(s, a, templ, &projected, fuel, &units)) {
        bindings_free(&projected);
        return false;
    }
    *count += units;
    bindings_free(&projected);
    return true;
}

typedef struct {
    Arena *arena;
    const MatchVisibleVarSet *visible;
    Atom *templ;
    Space *space;
    int fuel;
    uint64_t count;
    bool unsupported;
} GenericMatchCountCtx;

static bool count_generic_direct_match_row(const Bindings *bindings, void *ctx) {
    GenericMatchCountCtx *count = ctx;
    if (!count_projected_template(count->arena, count->visible, bindings,
                                  count->templ, count->space, count->fuel,
                                  &count->count)) {
        count->unsupported = true;
        return false;
    }
    return true;
}

static bool try_count_generic_match_collapse(Space *s, Arena *a, Atom *match_atom,
                                             int fuel, uint64_t *out_count) {
    Bindings empty;
    Atom *space_ref;
    Atom *pattern;
    Atom *templ;
    Space *ms;
    MatchVisibleVarSet visible;
    uint64_t count = 0;
    bool ok = true;

    if (!match_atom || match_atom->kind != ATOM_EXPR ||
        atom_head_symbol_id(match_atom) != g_builtin_syms.match ||
        expr_nargs(match_atom) != 3 || !out_count) {
        return false;
    }

    bindings_init(&empty);
    space_ref = expr_arg(match_atom, 0);
    pattern = resolve_registry_refs(a, expr_arg(match_atom, 1));
    templ = resolve_registry_refs(a, expr_arg(match_atom, 2));
    ms = resolve_single_space_arg(s, a, space_ref, fuel);
    if (!ms)
        ms = s;
    if (!ms || space_is_ordered(ms))
        return false;
    if (!space_engine_uses_pathmap(ms->match_backend.kind) && atom_has_vars(templ))
        return false;
    if (guard_mork_space_surface(a, match_atom, ms, "match", "mork:match"))
        return false;

    match_visible_var_set_init(&visible);
    if (!collect_match_visible_vars_rec(pattern, &visible)) {
        match_visible_var_set_free(&visible);
        return false;
    }
    if (pattern->kind == ATOM_EXPR && pattern->expr.len >= 3 &&
        atom_is_symbol_id(pattern->expr.elems[0], g_builtin_syms.comma)) {
        BindingSet matches;
        space_query_conjunction(ms, a, pattern->expr.elems + 1,
                                pattern->expr.len - 1, NULL, &matches);
        for (CettaIndex i = 0; i < matches.len && ok; i++) {
            ok = count_projected_template(a, &visible, &matches.items[i],
                                          templ, s, fuel, &count);
        }
        binding_set_free(&matches);
    } else {
        SubstMatchSet matches;
        GenericMatchCountCtx direct_ctx = {
            .arena = a,
            .visible = &visible,
            .templ = templ,
            .space = s,
            .fuel = fuel,
            .count = 0,
            .unsupported = false,
        };
        if (space_match_backend_visit_bindings_direct(
                ms, a, pattern, count_generic_direct_match_row, &direct_ctx)) {
            match_visible_var_set_free(&visible);
            if (direct_ctx.unsupported)
                return false;
            *out_count = direct_ctx.count;
            return true;
        }
        smset_init(&matches);
        space_subst_query(ms, a, pattern, &matches);
        for (CettaCount i = 0; i < matches.len && ok; i++) {
            Bindings mb;
            if (space_subst_match_with_seed(ms, pattern, &matches.items[i],
                                            &empty, a, &mb)) {
                ok = count_projected_template(a, &visible, &mb, templ,
                                              s, fuel, &count);
                bindings_free(&mb);
            }
        }
        smset_free(&matches);
    }
    match_visible_var_set_free(&visible);
    if (!ok)
        return false;
    *out_count = count;
    return true;
}

static bool try_count_collapse_match(Space *s, Arena *a, Atom *atom,
                                     const Bindings *current_env, int fuel,
                                     uint64_t *out_count) {
    Atom *target;
    if (!atom || atom->kind != ATOM_EXPR || expr_nargs(atom) != 1 ||
        !out_count || (current_env && current_env->len != 0)) {
        return false;
    }
    if (atom_head_symbol_id(atom) != g_builtin_syms.size &&
        atom_head_symbol_id(atom) != g_builtin_syms.size_atom) {
        return false;
    }
    target = expr_arg(atom, 0);
    if (!target || target->kind != ATOM_EXPR ||
        atom_head_symbol_id(target) != g_builtin_syms.collapse ||
        expr_nargs(target) != 1) {
        return false;
    }
    target = expr_arg(target, 0);
    return try_count_mork_match_collapse(s, a, target, fuel, out_count) ||
           try_count_generic_match_collapse(s, a, target, fuel, out_count);
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
    uint32_t nargs = 0;
    if (!expr_nargs_u32(atom, &nargs)) {
        outcome_set_add(os, expr_arity_too_large_error(a, atom), &_empty);
        return true;
    }
    bool profile_disabled_whole_call_surface =
        active_profile_disables_whole_call_surface(head_id);
    if (!profile_disabled_whole_call_surface &&
        (head_id == g_builtin_syms.size || head_id == g_builtin_syms.size_atom) &&
        nargs == 1) {
        uint64_t count = 0;
        if (try_count_collapse_match(s, a, atom, current_env, fuel, &count)) {
            outcome_set_add(os, atom_int(a, (int64_t)count), &_empty);
            return true;
        }
    }
    if (head_id == g_builtin_syms.mork_get_atoms_surface && nargs == 1) {
        if (emit_direct_mork_atoms_rows(s, a, atom, atom->expr.elems + 1,
                                        fuel, os)) {
            return true;
        }
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
    /* mork:* add surfaces are explicit bridge-extension entry points; generic
       add-atoms remains the shadowable stdlib/optimizer surface. */
    if ((head_id == g_builtin_syms.mork_add_atoms ||
         head_id == g_builtin_syms.lib_mork_space_add_atoms) &&
        nargs == 2) {
        Atom *space_arg = resolve_registry_refs(a, atom->expr.elems[1]);
        if (emit_mork_add_atoms_from_source_shape(
                s, a, atom, space_arg, atom->expr.elems[2], current_env,
                fuel, os)) {
            return true;
        }
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
    if (dispatch_shadowable_add_atoms_source_query(
            s, a, etype, atom, fuel, current_env, preserve_bindings, os)) {
        return true;
    }
    if (!profile_disabled_whole_call_surface &&
        op->kind == ATOM_SYMBOL &&
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
    if (op->kind == ATOM_SYMBOL) {
        Atom *resolved_head = registry_lookup_atom(op);
        if (resolved_head) {
            Atom **resolved_elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
            resolved_elems[0] = resolved_head;
            for (CettaExprIndex i = 1; i < atom->expr.len; i++)
                resolved_elems[i] = atom->expr.elems[i];
            atom = atom_expr(a, resolved_elems, atom->expr.len);
            op = atom->expr.elems[0];
            head_id = op->kind == ATOM_SYMBOL ? op->sym_id : SYMBOL_ID_NONE;
            if (!expr_nargs_u32(atom, &nargs)) {
                outcome_set_add(os, expr_arity_too_large_error(a, atom), &_empty);
                return true;
            }
        }
    }

    Atom **op_types = NULL;
    uint32_t n_op_types =
        profile_disabled_whole_call_surface ? 0 : get_atom_types_profiled(s, a, op, &op_types);
    bool total_structural_eq =
        head_id == g_builtin_syms.op_eq && nargs == 2 &&
        active_profile_uses_total_structural_eq();
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
    outcome_set_init_with_owner(&func_results, eval_storage_arena(a));
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
                CettaExprLen func_nargs = get_function_arg_count(fresh_ft);
                Atom **arg_types = func_nargs
                    ? arena_alloc(a, sizeof(Atom *) * (size_t)func_nargs)
                    : NULL;
                if (arg_types)
                    get_function_arg_types(fresh_ft, arg_types);
                Atom *ret_type = get_function_ret_type(fresh_ft);
                if (atom_is_symbol_id(ret_type, g_builtin_syms.expression))
                    ret_type = atom_undefined_type(a);

                OutcomeSet heads;
                outcome_set_init(&heads);
                metta_eval_bind_typed(s, a, fresh_ft, op, fuel, &heads);

                for (CettaCount hi = 0; hi < heads.len; hi++) {
                    Atom *head_atom = outcome_atom_materialize(a, &heads.items[hi]);
                    Bindings *head_env = &heads.items[hi].env;
                    if (atom_is_empty_or_error(head_atom) && !atom_eq(head_atom, op)) {
                        outcome_set_add_existing_move(&func_results, &heads.items[hi]);
                        continue;
                    }

                    CettaExprLen expr_narg = atom->expr.len - 1;
                    OutcomeSet call_terms;
                    outcome_set_init(&call_terms);
                    Atom **prefix = expr_narg
                        ? arena_alloc(a, sizeof(Atom *) * (size_t)expr_narg)
                        : NULL;
                    interpret_function_args(s, a, head_atom, atom->expr.elems + 1, arg_types,
                                            expr_narg, 0, prefix, head_env, fuel, &call_terms);

                    for (CettaCount ci = 0; ci < call_terms.len; ci++) {
                        Atom *call_atom = outcome_atom_materialize(a, &call_terms.items[ci]);
                        Bindings *combo_ctx = &call_terms.items[ci].env;
                        Atom *inst_ret_type =
                            eval_dependent_telescope_enabled()
                                ? bindings_apply_if_vars(combo_ctx, a, ret_type)
                                : bindings_apply_if_vars(head_env, a, ret_type);

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
                            QueryTableTailState table_tail =
                                query_equations_table_hit_single_tail(
                                    s, call_atom, &query_episode, query_arena,
                                    &query_eval,
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
                                    only_function_types &&
                                        n_op_types == 1 && heads.len == 1 &&
                                        call_terms.len == 1,
                                    tail_next, tail_type, tail_env);
                            search_context_free(&qr_context);
                            if (miss_tail == QUERY_TABLE_TAIL_SINGLE &&
                                only_function_types &&
                                n_op_types == 1 && heads.len == 1 &&
                                call_terms.len == 1) {
                                outcome_set_free(&call_terms);
                                outcome_set_free(&heads);
                                outcome_set_free(&func_results);
                                for (uint32_t sj = 0; sj < n_succs; sj++)
                                    bindings_free(&succs[sj]);
                                free(op_types);
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
        for (CettaCount i = 0; i < func_results.len; i++)
            outcome_set_add_existing(os, &func_results.items[i]);
        outcome_set_free(&func_results);
        if (!has_non_func_type) return true;
    } else {
        outcome_set_free(&func_results);
    }

    if (has_func_type && n_func_errors > 0 && !total_structural_eq &&
        (!has_non_func_type || eval_type_check_auto_enabled())) {
        for (uint32_t i = 0; i < n_func_errors; i++)
            outcome_set_add(os, func_errors[i], &_empty);
        if (!has_non_func_type) return true;
    }

    if (has_func_type && !has_non_func_type && !total_structural_eq) {
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

        if (profile_disabled_whole_call_surface) {
            for (CettaCount ti = 0; ti < tuples.len; ti++) {
                outcome_set_add_existing_move(os, &tuples.items[ti]);
            }
            outcome_set_free(&tuples);
            return true;
        }

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
            call_atom = resolve_registry_refs(a, call_atom);
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
                    tail_next, tail_type, tail_env);
            if (table_tail != QUERY_TABLE_TAIL_MISS) {
                search_context_free(&qr_context);
                if (table_tail == QUERY_TABLE_TAIL_SINGLE) {
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
                    true,
                    tail_next, tail_type, tail_env);
            search_context_free(&qr_context);
            if (miss_tail == QUERY_TABLE_TAIL_SINGLE ||
                miss_tail == QUERY_TABLE_TAIL_MULTI) {
                outcome_set_free(&tuples);
                return true;
            }
            outcome_set_add_existing_move(os, &tuples.items[0]);
            outcome_set_free(&tuples);
            return true;
        }

        for (CettaCount ti = 0; ti < tuples.len; ti++) {
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

            call_atom = resolve_registry_refs(a, call_atom);
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
                .episode = NULL,
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
    ArenaMark eval_gc_anchor = arena_mark(a);
    bool eval_gc_query_closed = eval_gc_enabled() && !atom_contains_vars(atom);
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
    size_t eval_gc_live_above_anchor =
        (a->live_bytes >= eval_gc_anchor.live_bytes)
            ? (a->live_bytes - eval_gc_anchor.live_bytes)
            : 0;
    if (eval_gc_safe_point(a, (size_t)os->len, eval_gc_live_above_anchor))
        eval_gc_collect(a, eval_gc_anchor, &atom,
                        &current_env_builder.current, &etype);
    if (eval_cancel_check())
        return;
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
        for (CettaCount i = 0; i < rs.len; i++)
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

    CettaExprLen nargs = expr_nargs(atom);
    const SymbolId head_id = atom_head_symbol_id(atom);
    Atom *head = atom->expr.elems[0];

    if (g_hyperpose_thread_unsafe_requested &&
        hyperpose_thread_barrier_head(head_id, head)) {
        eval_mark_hyperpose_thread_unsafe();
        outcome_set_add(os, atom_empty(a), &_empty);
        return;
    }

    /* ── Special forms (arguments NOT pre-evaluated) ───────────────────── */

    /* ── if ────────────────────────────────────────────────────────────── */
    if (symbol_id_is_if_surface(head_id)) {
        if (active_profile_uses_rust_he_compat_semantics() && nargs != 3) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        if (nargs != 2 && nargs != 3) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }

        Atom *cond_expr = bindings_apply_if_vars(CURRENT_ENV, a, expr_arg(atom, 0));
        Atom *then_br = expr_arg(atom, 1);
        Atom *else_br = (nargs == 3) ? expr_arg(atom, 2) : atom_empty(a);
        OutcomeSet conds;
        outcome_set_init(&conds);
        metta_eval_bind(s, a, cond_expr, fuel, &conds);

        if (conds.len == 1) {
            Atom *cond =
                outcome_atom_materialize_traced(
                    a, &conds.items[0],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            const Bindings *cond_env = &conds.items[0].env;
            if (is_true_atom(cond) || is_false_atom(cond)) {
                Atom *branch = is_true_atom(cond) ? then_br : else_br;
                Atom *next_atom = bindings_apply_if_vars(cond_env, a, branch);
                if (!bindings_builder_merge_commit(&current_env_builder, cond_env)) {
                    outcome_set_free(&conds);
                    return;
                }
                outcome_set_free(&conds);
                TAIL_REENTER(next_atom);
            }
        }

        for (CettaCount i = 0; i < conds.len; i++) {
            Atom *cond =
                outcome_atom_materialize_traced(
                    a, &conds.items[i],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            const Bindings *cond_env = &conds.items[i].env;

            if (atom_is_empty(cond))
                continue;
            if (atom_is_error(cond)) {
                outcome_set_add(os, cond, cond_env);
                continue;
            }
            if (is_true_atom(cond) || is_false_atom(cond)) {
                Atom *branch = is_true_atom(cond) ? then_br : else_br;
                eval_for_current_caller(s, a, etype, branch, fuel,
                                        cond_env, CURRENT_ENV,
                                        preserve_bindings, os);
                continue;
            }
            if (cond->kind == ATOM_VAR) {
                Atom *branches[2] = { then_br, else_br };
                SymbolId bool_ids[2] = {
                    g_builtin_syms.true_text,
                    g_builtin_syms.false_text,
                };
                for (uint32_t bi = 0; bi < 2; bi++) {
                    BindingsBuilder b;
                    if (!bindings_builder_init(&b, cond_env))
                        continue;
                    if (bindings_builder_add_var_fresh(
                            &b, cond, atom_symbol_id(a, bool_ids[bi]))) {
                        eval_for_current_caller(s, a, etype, branches[bi], fuel,
                                                bindings_builder_bindings(&b),
                                                CURRENT_ENV,
                                                preserve_bindings, os);
                    }
                    bindings_builder_free(&b);
                }
                continue;
            }

            Atom **actual_types = NULL;
            uint32_t ntypes = get_atom_types_profiled(s, a, cond, &actual_types);
            Atom *bool_type = atom_symbol(a, "Bool");
            bool has_nonbool_concrete_type = false;
            bool may_be_bool = false;
            for (uint32_t ti = 0; ti < ntypes; ti++) {
                if (atom_is_symbol_id(actual_types[ti], g_builtin_syms.undefined_type) ||
                    atom_is_meta_type(actual_types[ti])) {
                    continue;
                }
                if (atom_eq_fast(actual_types[ti], bool_type)) {
                    may_be_bool = true;
                    continue;
                }
                has_nonbool_concrete_type = true;
            }
            free(actual_types);
            if ((has_nonbool_concrete_type || cond->kind == ATOM_GROUNDED) && !may_be_bool) {
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "Bool"), cond),
                    cond_env);
                continue;
            }

            Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
            elems[0] = atom->expr.elems[0];
            elems[1] = cond;
            elems[2] = then_br;
            if (nargs == 3)
                elems[3] = else_br;
            outcome_set_add(os, atom_expr(a, elems, nargs + 1), cond_env);
        }
        outcome_set_free(&conds);
        return;
    }

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
            for (CettaExprIndex i = 0; i < list->expr.len; i++) {
                OutcomeSet branch;
                outcome_set_init(&branch);
                eval_for_current_caller(s, a, etype, list->expr.elems[i], fuel, &_empty,
                                        CURRENT_ENV, preserve_bindings, &branch);
                for (CettaCount j = 0; j < branch.len; j++) {
                    Atom *branch_atom = outcome_atom_materialize(a, &branch.items[j]);
                    if (atom_is_empty(branch_atom))
                        continue;
                    outcome_set_add_existing(os, &branch.items[j]);
                }
                outcome_set_free(&branch);
            }
        }
        return;
    }

    /* ── hyperpose ─────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.hyperpose) {
        if (!active_surface_allowed("hyperpose")) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Atom *list = expr_arg(atom, 0);
        if (list->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0; i < list->expr.len; i++) {
                OutcomeSet branch;
                outcome_set_init(&branch);
                eval_for_current_caller(s, a, etype, list->expr.elems[i], fuel, &_empty,
                                        CURRENT_ENV, preserve_bindings, &branch);
                for (CettaCount j = 0; j < branch.len; j++) {
                    Atom *branch_atom = outcome_atom_materialize(a, &branch.items[j]);
                    if (atom_is_empty(branch_atom))
                        continue;
                    outcome_set_add_existing(os, &branch.items[j]);
                }
                outcome_set_free(&branch);
            }
        }
        return;
    }

    /* ── collapse ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.collapse && nargs == 1) {
        if (!preserve_bindings &&
            try_effect_batch_append_collapse(s, a, expr_arg(atom, 0),
                                             fuel, CURRENT_ENV, os)) {
            return;
        }
        if (!preserve_bindings &&
            collapse_let_stream(s, a, expr_arg(atom, 0), fuel, CURRENT_ENV, os)) {
            return;
        }
        if (!preserve_bindings &&
            hyperpose_threaded_stream(s, a, expr_arg(atom, 0), fuel, false, 0,
                                      preserve_bindings, os)) {
            return;
        }
        if (!preserve_bindings &&
            collapse_direct_stream(s, a, expr_arg(atom, 0), fuel, os)) {
            return;
        }
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        /* HE treats Empty as an internal no-result sentinel here: collapse
           collects only surviving branches, not literal Empty placeholders. */
        Atom **collected_items = NULL;
        CettaExprLen collected_len = 0;
        if (inner.len > 0) {
            collected_items = arena_alloc(a, sizeof(Atom *) * inner.len);
            for (CettaCount i = 0; i < inner.len; i++) {
                if (atom_is_empty(inner.items[i]))
                    continue;
                collected_items[collected_len++] = inner.items[i];
            }
        }
        Atom *collected = atom_expr(a, collected_items, collected_len);
        result_set_free(&inner);
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
            for (CettaExprIndex i = 0; i < tl->expr.len; i++)
                elems[i + 1] = tl->expr.elems[i];
            outcome_set_add(os, atom_expr(a, elems, tl->expr.len + 1), &_empty);
        } else {
            outcome_set_add(os,
                call_signature_error(a, atom,
                    "(cons-atom <head> (: <tail> Expression))"),
                &_empty);
        }
        return;
    }

    /* ── union-atom ────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.union_atom && nargs == 2) {
        Atom *lhs = expr_arg(atom, 0);
        Atom *rhs = expr_arg(atom, 1);
        if (lhs->kind == ATOM_EXPR && rhs->kind == ATOM_EXPR) {
            CettaExprLen len = lhs->expr.len + rhs->expr.len;
            Atom **elems = arena_alloc(a, sizeof(Atom *) * len);
            for (CettaExprIndex i = 0; i < lhs->expr.len; i++)
                elems[i] = lhs->expr.elems[i];
            for (CettaExprIndex i = 0; i < rhs->expr.len; i++)
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
        if (emit_direct_mork_atoms_rows(s, a, atom, atom->expr.elems + 1,
                                        fuel, os)) {
            return;
        }
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
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_BINDINGS_LOOP_CALL_UNIFY);
        if (match_atoms(target, pattern, &b) && !bindings_has_loop(&b)) {
            Atom *next_atom = bindings_apply_if_vars(&b, a, then_br);
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
            for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
                Atom *branch = branches->expr.elems[i];
                if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                    BindingsBuilder b;
                    if (!bindings_builder_init(&b, NULL)) {
                        result_set_free(&scrut);
                        return;
                    }
                    if (simple_match_builder(branch->expr.elems[0], sv, &b)) {
                        const Bindings *bb = bindings_builder_bindings(&b);
                        Atom *next_atom =
                            bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                        if (preserve_bindings &&
                            !bindings_builder_merge_commit(&current_env_builder, bb)) {
                            bindings_builder_free(&b);
                            result_set_free(&scrut);
                            return;
                        }
                        bindings_builder_free(&b);
                        result_set_free(&scrut);
                        TAIL_REENTER(next_atom);
                    }
                    bindings_builder_free(&b);
                }
            }
            result_set_free(&scrut);
            return;
        }
        for (CettaCount si = 0; si < scrut.len; si++) {
            Atom *sv = scrut.items[si];
            if (branches->kind == ATOM_EXPR) {
                for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
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
        result_set_free(&scrut);
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
        if (head_id == g_builtin_syms.switch_text) {
            /* Upstream contract: switch evaluates its scrutinee
             * ((: switch (-> %Undefined% Expression %Undefined%)) then
             * matches structurally; switch-minimal (Atom-typed) below
             * matches the raw scrutinee. */
            ResultSet scrut;
            result_set_init(&scrut);
            metta_eval(s, a, NULL, scrutinee, fuel, &scrut);
            if (scrut.len == 1 && branches->kind == ATOM_EXPR) {
                Atom *sv = scrut.items[0];
                for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
                    Atom *branch = branches->expr.elems[i];
                    if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
                        BindingsBuilder b;
                        if (!bindings_builder_init(&b, NULL)) {
                            result_set_free(&scrut);
                            return;
                        }
                        if (simple_match_builder(branch->expr.elems[0], sv, &b)) {
                            const Bindings *bb = bindings_builder_bindings(&b);
                            Atom *next_atom =
                                bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                            if (preserve_bindings &&
                                !bindings_builder_merge_commit(&current_env_builder, bb)) {
                                bindings_builder_free(&b);
                                result_set_free(&scrut);
                                return;
                            }
                            bindings_builder_free(&b);
                            result_set_free(&scrut);
                            TAIL_REENTER(next_atom);
                        }
                        bindings_builder_free(&b);
                    }
                }
                result_set_free(&scrut);
                return;
            }
            for (CettaCount si = 0; si < scrut.len; si++) {
                Atom *sv = scrut.items[si];
                if (branches->kind == ATOM_EXPR) {
                    for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
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
            result_set_free(&scrut);
            return;
        }
        if (branches->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0; i < branches->expr.len; i++) {
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
        Atom *bulk_target_ref = NULL;
        Atom *bulk_source_ref = NULL;
        SpaceTransferEndpointKind bulk_target_kind = SPACE_TRANSFER_ENDPOINT_SPACE;
        SpaceTransferEndpointKind bulk_source_kind = SPACE_TRANSFER_ENDPOINT_NONE;
        bool public_add_atoms_body =
            expr_head_is_id(body_let, g_builtin_syms.add_atoms);
        if (!preserve_bindings &&
            CURRENT_ENV->len == 0 && CURRENT_ENV->eq_len == 0 &&
            let_add_atoms_source_shape(pat, val_expr, body_let,
                                       &bulk_target_ref, &bulk_source_ref,
                                       &bulk_target_kind,
                                       &bulk_source_kind) &&
            (!public_add_atoms_body ||
             add_atoms_public_surface_has_only_default(s))) {
            CettaAtomsTransferRefs transfer_refs = {
                .target_kind = bulk_target_kind,
                .source_kind = bulk_source_kind,
                .target_ref = bulk_target_ref,
                .source_ref = bulk_source_ref,
            };
            if (transfer_refs.target_kind == SPACE_TRANSFER_ENDPOINT_SPACE &&
                public_add_atoms_body &&
                generic_mork_handle_sugar_allowed(s, a, bulk_target_ref, fuel)) {
                transfer_refs.target_kind = SPACE_TRANSFER_ENDPOINT_MORK_BRIDGE;
            }
            if (emit_atoms_transfer_refs_direct(s, a, body_let, &transfer_refs,
                                                CURRENT_ENV, fuel, os)) {
                return;
            }
        }
        Atom *applied_val_expr = bindings_apply_if_vars(CURRENT_ENV, a, val_expr);
        bool body_let_closed = !atom_contains_vars(body_let);
        OutcomeSet vals;
        if (!preserve_bindings &&
            direct_outcome_walk_supported(s, a, applied_val_expr, fuel)) {
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
                for (CettaCount i = 0; i < visit.errors.len; i++)
                    outcome_set_add(os, visit.errors.items[i], &_empty);
            }
            result_set_free(&visit.errors);
            return;
        }
        outcome_set_init(&vals);
        metta_eval_bind(s, a, applied_val_expr, fuel, &vals);
        bool all_errors = vals.len > 0;
        for (CettaCount i = 0; i < vals.len; i++) {
            if (!atom_is_error(
                    outcome_atom_materialize_traced(
                        a, &vals.items[i],
                        CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN))) {
                all_errors = false;
                break;
            }
        }
        if (all_errors) {
            for (CettaCount i = 0; i < vals.len; i++)
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
                    Atom *next_atom =
                        bindings_apply_projected_body_visible(&visible, a, body_let);
                    if (preserve_bindings && !eval_gc_query_closed &&
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
                ok = match_atoms_builder(val_atom, pat, &b) &&
                     !bindings_has_loop(bindings_builder_bindings(&b));
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
                    if (preserve_bindings && !eval_gc_query_closed &&
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
        for (CettaCount i = 0; i < vals.len; i++) {
            Atom *val_atom =
                outcome_atom_materialize_traced(
                    a, &vals.items[i],
                    CETTA_RUNTIME_COUNTER_OUTCOME_VARIANT_MATERIALIZE_LET_CHAIN);
            if (atom_is_empty(val_atom))
                continue;
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
                if (!branch_outer_env_begin(&branch_outer_owned, &branch_outer,
                                            CURRENT_ENV, bb)) {
                    bindings_free(&visible);
                    bindings_builder_free(&b);
                    continue;
                }
                Atom *subst =
                    bindings_apply_projected_body_visible(&visible, a, body_let);
                eval_for_current_caller(s, a, NULL,
                                        subst, fuel, &_empty,
                                        branch_outer, preserve_bindings, os);
                branch_outer_env_finish(&branch_outer_owned, branch_outer);
                bindings_free(&visible);
                bindings_builder_free(&b);
            } else {
                BindingsBuilder b;
                if (!bindings_builder_init(&b, val_env))
                    continue;
                if (match_atoms_builder(val_atom, pat, &b) &&
                    !bindings_has_loop(bindings_builder_bindings(&b))) {
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
                    eval_for_current_caller(s, a, NULL,
                                            subst, fuel, &_empty,
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
                next_atom =
                    bindings_apply_projected_body_visible(&visible, a, tmpl_chain);
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
        for (CettaCount i = 0; i < inner.len; i++) {
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

    /* ── search-policy ─────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.search_policy) {
        if (!active_surface_allowed("search-policy")) {
            goto generic_dispatch;
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
        CettaExprIndex policy_stream_arg_idx = 0;
        bool policy_stream_arg_present = false;
        if (!active_surface_allowed(surface)) {
            goto generic_dispatch;
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
                    if (expr_arg(atom, 0)->kind != ATOM_GROUNDED) {
                        eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 0, 1, fuel,
                                                        preserve_bindings, os);
                        return;
                    }
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "SearchPolicy"),
                                           expr_arg(atom, 0)),
                        &_empty);
                    return;
                }
                if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                    emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
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
                emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
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
                    if (expr_arg(atom, 0)->kind != ATOM_GROUNDED) {
                        eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 0, 1, fuel,
                                                        preserve_bindings, os);
                        return;
                    }
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "SearchPolicy"),
                                           expr_arg(atom, 0)),
                        &_empty);
                    return;
                }
                if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                    emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
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
                emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
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
            arena_set_hashcons(&stream_scratch, NULL);
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
                    if (expr_arg(atom, 0)->kind != ATOM_GROUNDED) {
                        eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 0, 1, fuel,
                                                        preserve_bindings, os);
                        return;
                    }
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "SearchPolicy"),
                                           expr_arg(atom, 0)),
                        &_empty);
                    return;
                }
                if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                    emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
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
                emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
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
            arena_set_hashcons(&stream_scratch, NULL);
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
                    if (expr_arg(atom, 0)->kind != ATOM_GROUNDED) {
                        eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 0, 1, fuel,
                                                        preserve_bindings, os);
                        return;
                    }
                    outcome_set_add(os,
                        bad_arg_type_error(s, a, atom, 1, atom_symbol(a, "SearchPolicy"),
                                           expr_arg(atom, 0)),
                        &_empty);
                    return;
                }
                if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                    emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
                    return;
                }
                stream_expr = expr_arg(atom, 1);
                policy_stream_arg_idx = 1;
                policy_stream_arg_present = true;
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
                policy_stream_arg_idx = 1;
                policy_stream_arg_present = true;
            } else if (parsed == CETTA_SEARCH_POLICY_PARSE_ERROR) {
                emit_policy_stream_call_inert(s, a, atom, 1, CURRENT_ENV, fuel, os);
                return;
            } else {
                Atom *limit_atom = expr_arg(atom, 0);
                if (limit_atom->kind != ATOM_GROUNDED) {
                    eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 0, 1, fuel,
                                                    preserve_bindings, os);
                    return;
                }
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
            if (limit_atom->kind != ATOM_GROUNDED) {
                eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 0, 2, fuel,
                                                preserve_bindings, os);
                return;
            }
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
                if (expr_arg(atom, 1)->kind != ATOM_GROUNDED) {
                    eval_stream_call_non_ground_arg(s, a, atom, CURRENT_ENV, 1, 2, fuel,
                                                    preserve_bindings, os);
                    return;
                }
                outcome_set_add(os,
                    bad_arg_type_error(s, a, atom, 2, atom_symbol(a, "SearchPolicy"),
                                       expr_arg(atom, 1)),
                    &_empty);
                return;
            }
            if (parsed != CETTA_SEARCH_POLICY_PARSE_OK) {
                emit_policy_stream_call_inert(s, a, atom, 2, CURRENT_ENV, fuel, os);
                return;
            }
            limit = limit_atom->ground.ival;
            stream_expr = expr_arg(atom, 2);
            policy_stream_arg_idx = 2;
            policy_stream_arg_present = true;
        } else {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }

        if (policy.present &&
            policy.lane != CETTA_SEARCH_POLICY_LANE_RECURSIVE_DEPENDENT_PROOF) {
            if (policy_stream_arg_present) {
                emit_policy_stream_call_inert(s, a, atom, policy_stream_arg_idx,
                                              CURRENT_ENV, fuel, os);
                return;
            }
            goto generic_dispatch;
        }

        stream_emit(s, a, stream_expr, fuel, true, limit, preserve_bindings,
                    policy.order, os);
        return;
    }

    /* ── function / return ─────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.function && nargs == 1) {
        Atom *body = expr_arg(atom, 0);
        ResultSet inner;
        bool rust_compat = active_profile_uses_rust_he_compat_semantics();
        result_set_init(&inner);
        metta_eval(s, a, NULL,body, fuel, &inner);
        for (CettaCount i = 0; i < inner.len; i++) {
            Atom *r = inner.items[i];
            if (atom_head_symbol_id(r) == g_builtin_syms.return_text && r->expr.len == 2) {
                outcome_set_add(os, r->expr.elems[1], &_empty);
            } else if (rust_compat) {
                outcome_set_add(os,
                    atom_var_with_id(a, "result", var_epoch_id(1, 17)),
                    &_empty);
            } else {
                outcome_set_add(os, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "function"), body),
                    atom_symbol(a, "NoReturn")), &_empty);
            }
        }
        if (inner.len == 0 && rust_compat) {
            outcome_set_add(os,
                atom_var_with_id(a, "result", var_epoch_id(1, 17)),
                &_empty);
        } else if (inner.len == 0) {
            outcome_set_add(os, atom_error(a,
                atom_expr2(a, atom_symbol(a, "function"), body),
                atom_symbol(a, "NoReturn")), &_empty);
        }
        result_set_free(&inner);
        return;
    }

    /* ── assert ────────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.assert_text && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL,expr_arg(atom, 0), fuel, &inner);
        for (CettaCount i = 0; i < inner.len; i++) {
            if (is_true_atom(inner.items[i])) {
                outcome_set_add(os, atom_unit(a), &_empty);
            } else {
                outcome_set_add(os, atom_error(a,
                    atom_expr2(a, atom_symbol(a, "assert"), expr_arg(atom, 0)),
                    atom_expr3(a, expr_arg(atom, 0),
                        atom_symbol(a, "not"), atom_symbol(a, "True"))), &_empty);
            }
        }
        result_set_free(&inner);
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
        if (active_profile_uses_rust_he_compat_semantics()) {
            Atom *arg = expr_arg(atom, 0);
            ResultSet inner;
            result_set_init(&inner);
            metta_eval(s, a, NULL, arg, fuel, &inner);
            for (CettaCount i = 0; i < inner.len; i++) {
                outcome_set_add(os, atom_eq(inner.items[i], arg) ? atom : inner.items[i],
                                &_empty);
            }
            if (inner.len == 0)
                outcome_set_add(os, atom, &_empty);
            result_set_free(&inner);
            return;
        }
        TAIL_REENTER(expr_arg(atom, 0));
    }

    /* ── foldl-atom-in-space (clean extension surface) ────────────────── */
    if (head_id == g_builtin_syms.foldl_atom_in_space) {
        if (!active_surface_allowed("foldl-atom-in-space")) {
            goto generic_dispatch;
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
            if (!active_surface_allowed("new-space-kind")) {
                goto generic_dispatch;
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
        Space *ns = g_eval_payload_transactional
            ? cetta_malloc(sizeof(Space))
            : arena_alloc(pa, sizeof(Space));
        if (!ns) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "OutOfMemory")),
                &_empty);
            return;
        }
        space_init_with_universe(ns, eval_current_term_universe());
        ns->kind = kind;
        if (!space_match_backend_try_set(ns, backend_kind)) {
            if (g_eval_payload_transactional) {
                space_free(ns);
                free(ns);
            }
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnknownSpaceEngine")),
                &_empty);
            return;
        }
        if (g_eval_payload_transactional) {
            ns->payload_owner_epoch = eval_payload_owner_epoch();
            if (!eval_payload_track_scratch_space(ns)) {
                space_free(ns);
                free(ns);
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "PayloadIsolationFailed")),
                    &_empty);
                return;
            }
        } else {
            eval_track_new_space(ns);
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
                Atom *space_value = atom_space(pa, dest.space);
                if (eval_storage_is_persistent(pa)) {
                    cetta_provenance_assert_not_transient(
                        space_value, "import.registry.fresh-space");
                }
                registry_bind_id(g_registry, dest.bind_key, space_value);
            }
            outcome_set_add(os, atom_unit(a), &_empty);
        } else if (error) {
            if (dest.is_fresh && dest.space) {
                space_free(dest.space);
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
        if (nargs == 2 && !active_surface_allowed("include-space-target")) {
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
            outcome_set_add(os,
                (nargs == 1 && active_profile_uses_rust_he_compat_semantics())
                    ? atom_empty(a)
                    : atom_unit(a),
                &_empty);
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
        if (!active_surface_allowed("module-inventory!")) {
            goto generic_dispatch;
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
        if (!active_surface_allowed("reset-runtime-stats!")) {
            goto generic_dispatch;
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
        if (!active_surface_allowed("runtime-stats!")) {
            goto generic_dispatch;
        }
        outcome_set_add(os, runtime_stats_inventory_atom(a), &_empty);
        return;
    }

    /* ── with-space-snapshot ───────────────────────────────────────────── */
    if (head_id == g_builtin_syms.with_space_snapshot &&
        nargs == 3 && g_registry) {
        if (!active_surface_allowed("with-space-snapshot")) {
            goto generic_dispatch;
        }
        Atom *binder = expr_arg(atom, 0);
        Atom *space_ref = resolve_registry_refs(a, expr_arg(atom, 1));
        Atom *body = expr_arg(atom, 2);
        Space *target = resolve_registry_space_payload(g_registry, space_ref);
        if (!target) {
            outcome_set_add(os, atom, &_empty);
            return;
        }
        Space *snapshot = space_snapshot_clone(target, a);
        if (!snapshot) {
            outcome_set_add(os,
                space_backend_or_symbol_error(
                    a, atom, "AttachedCompiledSpaceMaterializeFailed"),
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
                Atom *next_atom = bindings_apply_if_vars(bb, a, body);
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
        if (!active_surface_allowed(surface_name)) {
            goto generic_dispatch;
        }
        if (nargs != 2 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg_write(s, a, expr_arg(atom, 0), fuel);
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
            result_set_free(&backend_rs);
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
            result_set_free(&backend_rs);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnknownSpaceEngine")),
                &_empty);
            return;
        }
        const char *reason = space_match_backend_unavailable_reason(kind);
        if (reason) {
            result_set_free(&backend_rs);
            outcome_set_add(os,
                atom_error(a, atom, atom_string(a, reason)),
                &_empty);
            return;
        }
        if (!space_match_backend_try_set(target, kind)) {
            result_set_free(&backend_rs);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "UnknownSpaceEngine")),
                &_empty);
            return;
        }
        result_set_free(&backend_rs);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_len) {
        if (!active_surface_allowed("space-len")) {
            goto generic_dispatch;
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
        outcome_set_add(os, atom_int(a, (int64_t)space_length64(target)), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.step_bang) {
        if (!active_surface_allowed("step!")) {
            goto generic_dispatch;
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
                result_set_free(&step_rs);
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "ExpectedNonNegativeStepCount")),
                    &_empty);
                return;
            }
            limit = (uint64_t)step_atom->ground.ival;
            result_set_free(&step_rs);
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
        if (!active_surface_allowed("space-push")) {
            goto generic_dispatch;
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
        if (!space_admit_atom(target, dst, expr_arg(atom, 1))) {
            outcome_set_add(os,
                space_term_universe_or_symbol_error(a, atom, target,
                                                    "SpacePushFailed"),
                &_empty);
            return;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_PUSH);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_peek) {
        if (!active_surface_allowed("space-peek")) {
            goto generic_dispatch;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg_write(s, a, expr_arg(atom, 0), fuel);
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
            Atom *space_error = space_backend_error_if_set(a, atom);
            if (space_error) {
                outcome_set_add(os, space_error, &_empty);
                return;
            }
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, ordered_space_empty_error_symbol(target))),
                &_empty);
            return;
        }
        outcome_set_add(os, payload_rebind_resources(a, top), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_pop) {
        if (!active_surface_allowed("space-pop")) {
            goto generic_dispatch;
        }
        if (nargs != 1 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = resolve_single_space_arg_write(s, a, expr_arg(atom, 0), fuel);
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
            Atom *space_error = space_backend_error_if_set(a, atom);
            if (space_error) {
                outcome_set_add(os, space_error, &_empty);
                return;
            }
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, ordered_space_empty_error_symbol(target))),
                &_empty);
            return;
        }
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_SPACE_POP);
        outcome_set_add(os, payload_rebind_resources(a, popped), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.space_get ||
        head_id == g_builtin_syms.space_truncate) {
        const bool is_get = head_id == g_builtin_syms.space_get;
        const char *surface = is_get ? "space-get" : "space-truncate";
        if (!active_surface_allowed(surface)) {
            goto generic_dispatch;
        }
        if (nargs != 2 || !g_registry) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        Space *target = is_get
            ? resolve_single_space_arg(s, a, expr_arg(atom, 0), fuel)
            : resolve_single_space_arg_write(s, a, expr_arg(atom, 0), fuel);
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
            result_set_free(&idx_rs);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "ExpectedNonNegativeIndex")),
                &_empty);
            return;
        }
        CettaIndex idx = (CettaIndex)idx_atom->ground.ival;
        result_set_free(&idx_rs);
        if (is_get) {
            Atom *item = space_get_at64(target, idx);
            if (!item) {
                outcome_set_add(os,
                    atom_error(a, atom, atom_symbol(a, "IndexOutOfBounds")),
                    &_empty);
                return;
            }
            outcome_set_add(os, payload_rebind_resources(a, item), &_empty);
        } else {
            if (!space_truncate64(target, idx)) {
                Atom *space_error = space_backend_error_if_set(a, atom);
                if (space_error) {
                    outcome_set_add(os, space_error, &_empty);
                    return;
                }
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
        if (active_profile_uses_rust_he_compat_semantics() &&
            val_rs.len > 0 &&
            atom_is_error(val) &&
            !atom_is_error(val_expr)) {
            outcome_set_add(os, val, &_empty);
            result_set_free(&val_rs);
            return;
        }
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
                    result_set_free(&val_rs);
                    outcome_set_add(os,
                        space_backend_or_symbol_error(
                            a, atom, "AttachedCompiledSpaceMaterializeFailed"),
                        &_empty);
                    return;
                }
                stored = atom_space(dst, clone);
            } else {
                stored = eval_store_atom(dst, val);
            }
            cetta_provenance_assert_not_transient(stored, "bind.registry.value");
            registry_bind_id(g_registry, name->sym_id, stored);
        }
        result_set_free(&val_rs);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-reduct ────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_reduct && nargs == 2 && g_registry) {
        Space **targets = NULL;
        CettaCount ntargets = 0;
        collect_resolved_spaces(s, a, expr_arg(atom, 0), fuel, &targets, &ntargets);

        OutcomeSet vals;
        outcome_set_init(&vals);
        metta_eval_bind(s, a, expr_arg(atom, 1), fuel, &vals);

        if (ntargets > 0) {
            Arena *dst = eval_storage_arena(a);
            for (CettaCount ti = 0; ti < ntargets; ti++) {
                for (CettaCount vi = 0; vi < vals.len; vi++) {
                    Atom *stored_val = outcome_atom_materialize(a, &vals.items[vi]);
                    Space *target = payload_resolve_space_write(targets[ti]);
                    targets[ti] = target;
                    if (!space_admit_atom(target, dst, stored_val)) {
                        outcome_set_add(os,
                            space_term_universe_or_symbol_error(a, atom,
                                                                target,
                                                                "AddReductFailed"),
                            &vals.items[vi].env);
                        free(targets);
                        outcome_set_free(&vals);
                        return;
                    }
                    outcome_set_add(os, atom_unit(a), &vals.items[vi].env);
                }
            }
        }

        free(targets);
        outcome_set_free(&vals);
        return;
    }

    /* ── add-atoms bulk-transfer shortcut for the default MeTTa equation ─ */
    if (head_id == g_builtin_syms.add_atoms && nargs == 2 && g_registry &&
        add_atoms_public_surface_has_only_default(s)) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *items = expr_arg(atom, 1);
        if (add_atoms_source_shape(items, NULL, NULL, NULL)) {
            if (generic_mork_handle_sugar_allowed(s, a, space_ref, fuel) &&
                emit_mork_add_atoms_from_source_shape(
                    s, a, atom, space_ref, items, CURRENT_ENV, fuel, os)) {
                return;
            }
            Space *target = resolve_single_space_arg_write(s, a, space_ref, fuel);
            if (target &&
                !guard_mork_space_surface(a, atom, target, "add-atoms",
                                          "mork:add-atoms") &&
                emit_add_atoms_from_source_shape(s, a, atom, target, items,
                                                 fuel, os)) {
                return;
            }
        }
    }

    /* ── add-atom ──────────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.add_atom && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.mork_add_atom, os)) {
            return;
        }
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "add-atom", "mork:add-atom");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg_write(s, a, space_ref, fuel);
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
                space_backend_or_symbol_error(
                    a, atom, "AttachedCompiledSpaceMaterializeFailed"),
                &_empty);
            return;
        }
        /* Deep-copy to persistent arena so atom survives eval_arena reset */
        Arena *dst = eval_storage_arena(a);
        if (!space_admit_atom(target, dst, atom_to_add)) {
            outcome_set_add(os,
                space_term_universe_or_symbol_error(a, atom, target,
                                                    "AddAtomFailed"),
                &_empty);
            return;
        }
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── add-atom-nodup (dedup variant for forward chaining) ────────────── */
    if (head_id == g_builtin_syms.add_atom_nodup && nargs == 2 && g_registry) {
        if (!active_surface_allowed("add-atom-nodup")) {
            goto generic_dispatch;
        }
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_add = expr_arg(atom, 1);
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "add-atom-nodup", "mork:add-atom");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg_write(s, a, space_ref, fuel);
        if (!target) {
            outcome_set_add(os, space_arg_error(a, atom,
                "add-atom-nodup expects a space as the first argument"), &_empty);
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
                space_backend_or_symbol_error(
                    a, atom, "AttachedCompiledSpaceMaterializeFailed"),
                &_empty);
            return;
        }
        Atom *compare_atom = space_compare_atom(target, a, atom_to_add);
        bool found = false;
        bool backend_checked =
            space_match_backend_contains_atom_structural_direct(
                target, compare_atom, &found);
        if (!backend_checked)
            found = space_contains_exact(target, compare_atom);
        if (!found && !backend_checked) {
            /* Non-ground theorem dedup must be alpha-aware: local pathmap
               projection uses synthetic stable variable spellings, while the
               evaluator may still hold the same theorem under source spellings. */
            bool alpha_fallback = atom_has_vars(compare_atom);
            CettaCount logical_len = space_length64(target);
            for (CettaIndex i = 0; i < logical_len && !found; i++) {
                Atom *candidate = space_get_at64(target, i);
                if (!candidate)
                    continue;
                if (alpha_fallback ? atom_alpha_eq(candidate, compare_atom)
                                   : atom_eq(candidate, compare_atom)) {
                    found = true;
                }
            }
        }
        if (!found) {
            Arena *dst = eval_storage_arena(a);
            if (!space_admit_atom(target, dst, atom_to_add)) {
                outcome_set_add(os,
                    space_term_universe_or_symbol_error(a, atom, target,
                                                        "AddAtomFailed"),
                    &_empty);
                return;
            }
        }
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    /* ── remove-atom ───────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.remove_atom && nargs == 2 && g_registry) {
        Atom *space_ref = expr_arg(atom, 0);
        Atom *atom_to_rm = expr_arg(atom, 1);
        if (emit_generic_mork_handle_native_surface(
                s, a, atom, atom->expr.elems + 1, nargs, fuel,
                g_builtin_syms.mork_remove_atom, os)) {
            return;
        }
        Atom *mork_handle_error = guard_mork_handle_surface(
            s, a, atom, space_ref, fuel, "remove-atom", "mork:remove-atom");
        if (mork_handle_error) {
            outcome_set_add(os, mork_handle_error, &_empty);
            return;
        }
        Space *target = resolve_single_space_arg_write(s, a, space_ref, fuel);
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
        if (space_match_backend_is_attached_compiled(target) &&
            !space_match_backend_materialize_attached(
                target, eval_storage_arena(a))) {
            outcome_set_add(os,
                space_backend_or_symbol_error(
                    a, atom, "AttachedCompiledSpaceMaterializeFailed"),
                &_empty);
            return;
        }
        Atom *compare_atom = space_remove_compare_atom(target, a, atom_to_rm);
        if (!(target && target->native.universe &&
              space_remove_atom_id(target,
                                   term_universe_lookup_atom_id(target->native.universe,
                                                                compare_atom)))) {
            space_remove(target, compare_atom);
        }
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
                space_backend_or_symbol_error(
                    a, atom, "AttachedCompiledSpaceMaterializeFailed"),
                &_empty);
            return;
        }
        CettaCount logical_len = space_length64(target);
        for (CettaIndex i = 0; i < logical_len; i++) {
            Atom *item = payload_rebind_resources(a, space_get_at64(target, i));
            outcome_set_add_unfactored(os, item, &_empty);
        }
        return;
    }

    /* ── count-atoms ──────────────────────────────────────────────────── */
    if (head_id == g_builtin_syms.count_atoms && nargs == 1 && g_registry) {
        if (!active_surface_allowed("count-atoms")) {
            goto generic_dispatch;
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
        outcome_set_add(os, atom_int(a, (int64_t)space_length64(target)), &_empty);
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
        for (CettaCount i = 0; i < inner.len; i++) {
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

    /* ── singleton-visible-witness ─────────────────────────────────────── */
    if (head_id == g_builtin_syms.singleton_visible_witness) {
        if (!active_surface_allowed("singleton-visible-witness")) {
            goto generic_dispatch;
        }
        if (nargs != 1) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
                &_empty);
            return;
        }
        emit_singleton_visible_witness(s, a, atom, expr_arg(atom, 0), fuel, os);
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
            for (CettaExprIndex i = 0; i < list->expr.len; i++) {
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
        Space *target = resolve_registry_space_payload(g_registry, space_ref);
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
        /* Allocate state in persistent storage unless a transactional payload
         * needs a scratch-local cell that can be reclaimed at payload end. */
        Arena *pa = eval_storage_arena(a);
        StateCell *cell = g_eval_payload_transactional
            ? cetta_malloc(sizeof(StateCell))
            : arena_alloc(pa, sizeof(StateCell));
        if (!cell) {
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "OutOfMemory")),
                &_empty);
            return;
        }
        cell->value = eval_store_atom(pa, initial);
        /* Infer content type from initial value */
        Atom **itypes;
        uint32_t nit = get_atom_types(s, a, initial, &itypes);
        cell->content_type = (nit > 0) ? eval_store_atom(pa, itypes[0]) : atom_undefined_type(pa);
        cell->payload_owner_epoch = eval_payload_owner_epoch();
        cell->payload_export_owner_epoch = 0;
        free(itypes);
        if (eval_storage_is_persistent(pa))
            cetta_provenance_assert_state_cell_not_transient(cell,
                                                            "new-state.cell");
        if (g_eval_payload_transactional &&
            !eval_payload_track_scratch_state(cell)) {
            free(cell);
            outcome_set_add(os,
                atom_error(a, atom, atom_symbol(a, "PayloadIsolationFailed")),
                &_empty);
            return;
        }
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
                StateCell *cell =
                    payload_resolve_state_read((StateCell *)state_ref->ground.ptr);
                outcome_set_add(os, payload_rebind_resources(a, cell->value),
                                &refs.items[i].env);
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
            {
                StateCell *cell =
                    payload_resolve_state_write(a, (StateCell *)state_ref->ground.ptr);
                if (!cell) {
                    outcome_set_add(os,
                        atom_error(a, atom, atom_symbol(a, "PayloadIsolationFailed")),
                        &refs.items[i].env);
                    continue;
                }
                state_ref = atom_state(a, cell);
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
                    if (eval_persistent_arena())
                        cetta_provenance_assert_state_cell_not_transient(
                            cell, "change-state.cell");
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
            const char *key_name = atom_name_cstr(key);
            bool strict_thread_count =
                key_name && strcmp(key_name, "num-threads") == 0;

            if (strict_thread_count) {
                const char *reason = validate_thread_count_pragma_value(value);
                if (reason) {
                    outcome_set_add(os, atom_error(a, atom, atom_symbol(a, reason)),
                                    &_empty);
                    return;
                }
            }

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
                session, key_name, value_kind, value_repr, int_value);
        }

        outcome_set_add(os, handled ? atom_unit(a) : atom, &_empty);
        return;
    }

    /* ── nop (evaluate for side effects, return unit) ────────────────────── */
    if (head_id == g_builtin_syms.nop && nargs == 1) {
        ResultSet inner;
        result_set_init(&inner);
        metta_eval(s, a, NULL, expr_arg(atom, 0), fuel, &inner);
        result_set_free(&inner);
        outcome_set_add(os, atom_unit(a), &_empty);
        return;
    }

    if (head_id == g_builtin_syms.cetta_surface_available && nargs == 1) {
        Atom *target = expr_arg(atom, 0);
        const char *surface_name = NULL;
        if (target->kind == ATOM_SYMBOL) {
            surface_name = atom_name_cstr(target);
        } else if (target->kind == ATOM_EXPR && target->expr.len > 0 &&
                   target->expr.elems[0]->kind == ATOM_SYMBOL) {
            surface_name = atom_name_cstr(target->expr.elems[0]);
        }
        outcome_set_add(os, atom_bool(a,
            !surface_name || active_surface_allowed(surface_name)), &_empty);
        return;
    }

    if (active_profile_uses_rust_he_compat_semantics() &&
        head_id != SYMBOL_ID_NONE &&
        strcmp(atom_name_cstr(atom->expr.elems[0]), "get-doc") == 0 &&
        nargs == 1) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
        return;
    }

    if (active_profile_uses_rust_he_compat_semantics() &&
        head_id == g_builtin_syms.filter_atom &&
        nargs != 3) {
        outcome_set_add(os,
            atom_error(a, atom, atom_symbol(a, "IncorrectNumberOfArguments")),
            &_empty);
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
            result_set_free(&evr);
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
            bool *used = alloc_zeroed_bool_array(expected.len);
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
            result_set_free(&actual);
            result_set_free(&expected);
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            /* Build HE-compatible error message:
               \nExpected: [e1, e2]\nGot: [a1, a2]\nMissed/Excessive */
            char buf[2048];
            size_t pos = 0;
            capped_msg_appendf(buf, sizeof(buf), &pos, "\nExpected: [");
            for (uint32_t i = 0; i < expected.len; i++) {
                if (i > 0) capped_msg_appendf(buf, sizeof(buf), &pos, ", ");
                capped_msg_append_atom(buf, sizeof(buf), &pos, expected.items[i]);
            }
            capped_msg_appendf(buf, sizeof(buf), &pos, "]\nGot: [");
            for (uint32_t i = 0; i < actual.len; i++) {
                if (i > 0) capped_msg_appendf(buf, sizeof(buf), &pos, ", ");
                capped_msg_append_atom(buf, sizeof(buf), &pos, actual.items[i]);
            }
            capped_msg_appendf(buf, sizeof(buf), &pos, "]");
            /* Missed results (in expected but not actual) */
            bool has_missed = false;
            for (uint32_t i = 0; i < expected.len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < actual.len; j++)
                    if (atom_eq(expected.items[i], actual.items[j])) { found = true; break; }
                if (!found) {
                    capped_msg_appendf(buf, sizeof(buf), &pos,
                                       has_missed ? ", " : "\nMissed results: ");
                    has_missed = true;
                    capped_msg_append_atom(buf, sizeof(buf), &pos, expected.items[i]);
                }
            }
            /* Excessive results (in actual but not expected) */
            bool has_excess = false;
            for (uint32_t i = 0; i < actual.len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < expected.len; j++)
                    if (atom_eq(actual.items[i], expected.items[j])) { found = true; break; }
                if (!found) {
                    capped_msg_appendf(buf, sizeof(buf), &pos,
                                       has_excess ? ", " : "\nExcessive results: ");
                    has_excess = true;
                    capped_msg_append_atom(buf, sizeof(buf), &pos, actual.items[i]);
                }
            }
            result_set_free(&actual);
            result_set_free(&expected);
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
        result_set_filter_empty(&actual);
        result_set_resolve_registry_refs(a, &actual);
        Atom *expected_list = expr_arg(atom, 1);
        Atom **expected_items = NULL;
        CettaExprLen expected_len = 0;
        bool ok = false;
        if (expected_list->kind == ATOM_EXPR) {
            expected_len = expected_list->expr.len;
            expected_items = expected_len
                ? arena_alloc(a, sizeof(Atom *) * (size_t)expected_len)
                : NULL;
            for (CettaExprIndex i = 0; i < expected_len; i++) {
                expected_items[i] = resolve_registry_refs(a, expected_list->expr.elems[i]);
            }
            if (actual.len == expected_len) {
                ok = true;
                for (uint32_t i = 0; i < actual.len && ok; i++) {
                    if (!atom_eq(actual.items[i], expected_items[i]))
                        ok = false;
                }
            }
        }
        /* Empty expected () matches empty result set */
        if (actual.len == 0 && expected_list->kind == ATOM_EXPR &&
            expected_list->expr.len == 0) {
            ok = true;
        }
        if (ok) {
            result_set_free(&actual);
            outcome_set_add(os, atom_unit(a), &_empty);
        } else {
            char buf[2048];
            size_t pos = 0;
            capped_msg_appendf(buf, sizeof(buf), &pos, "\nExpected: [");
            for (CettaExprIndex i = 0; i < expected_len; i++) {
                if (i > 0) capped_msg_appendf(buf, sizeof(buf), &pos, ", ");
                capped_msg_append_atom(buf, sizeof(buf), &pos, expected_items[i]);
            }
            capped_msg_appendf(buf, sizeof(buf), &pos, "]\nGot: [");
            for (uint32_t i = 0; i < actual.len; i++) {
                if (i > 0) capped_msg_appendf(buf, sizeof(buf), &pos, ", ");
                capped_msg_append_atom(buf, sizeof(buf), &pos, actual.items[i]);
            }
            capped_msg_appendf(buf, sizeof(buf), &pos, "]");
            bool has_missed = false;
            for (CettaExprIndex i = 0; i < expected_len; i++) {
                bool found = false;
                for (uint32_t j = 0; j < actual.len; j++)
                    if (atom_eq(expected_items[i], actual.items[j])) { found = true; break; }
                if (!found) {
                    capped_msg_appendf(buf, sizeof(buf), &pos,
                                       has_missed ? ", " : "\nMissed results: ");
                    has_missed = true;
                    capped_msg_append_atom(buf, sizeof(buf), &pos, expected_items[i]);
                }
            }
            bool has_excess = false;
            for (uint32_t i = 0; i < actual.len; i++) {
                bool found = false;
                for (CettaExprIndex j = 0; j < expected_len; j++)
                    if (atom_eq(actual.items[i], expected_items[j])) { found = true; break; }
                if (!found) {
                    capped_msg_appendf(buf, sizeof(buf), &pos,
                                       has_excess ? ", " : "\nExcessive results: ");
                    has_excess = true;
                    capped_msg_append_atom(buf, sizeof(buf), &pos, actual.items[i]);
                }
            }
            result_set_free(&actual);
            outcome_set_add(os, atom_error(a,
                atom_expr3(a, atom_symbol(a, "assertEqualToResult"),
                    expr_arg(atom, 0), expected_list),
                atom_string(a, buf)), &_empty);
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
                    if (!used[j] && atom_eq(actual.items[i], expected.items[j])) {
                        used[j] = true; found = true; break;
                    }
                }
                if (!found) ok = false;
            }
            free(used);
        }
        result_set_free(&actual);
        result_set_free(&expected);
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
        result_set_filter_empty(&actual);
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
        result_set_free(&actual);
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
        result_set_free(&actual);
        result_set_free(&expected);
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
        result_set_free(&actual);
        result_set_free(&expected);
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
        result_set_filter_empty(&actual);
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
        result_set_free(&actual);
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
        result_set_filter_empty(&actual);
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
        result_set_free(&actual);
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
        result_set_filter_empty(&actual);
        Atom *expected_list = expr_arg(atom, 1);
        /* Check that every expected item is in the actual results */
        bool ok = true;
        if (expected_list->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0; i < expected_list->expr.len && ok; i++) {
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
        result_set_free(&actual);
        return;
    }

generic_dispatch:
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

static void metta_eval_one_step(Space *s, Arena *a, Atom *type, Atom *atom,
                                ResultSet *rs);

static void metta_eval_one_step_chain(Space *s, Arena *a, Atom *atom,
                                      Atom *to_eval, Atom *var,
                                      Atom *body, ResultSet *rs) {
    ResultSet inner;
    bool emitted = false;

    if (var->kind != ATOM_VAR) {
        result_set_add(rs, call_signature_error(
                               a, atom,
                               "(chain <nested> (: <var> Variable) <templ>)"));
        return;
    }

    result_set_init(&inner);
    metta_eval_one_step(s, a, NULL, to_eval, &inner);

    for (uint32_t i = 0; i < inner.len; i++) {
        Atom *next = inner.items[i];
        if (atom_is_empty(next)) {
            emitted = true;
            continue;
        }
        if (atom_is_error(next)) {
            result_set_add(rs, next);
            emitted = true;
            continue;
        }
        if (atom_eq(next, to_eval))
            continue;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * 4);
        elems[0] = atom_symbol_id(a, g_builtin_syms.chain);
        elems[1] = next;
        elems[2] = var;
        elems[3] = body;
        result_set_add(rs, atom_expr(a, elems, 4));
        emitted = true;
    }
    result_set_free(&inner);
    if (emitted)
        return;

    if (atom_is_empty(to_eval)) {
        result_set_add(rs, atom_empty(a));
        return;
    }
    if (atom_is_error(to_eval)) {
        result_set_add(rs, to_eval);
        return;
    }

    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder b;
    if (!bindings_builder_init(&b, &empty))
        return;
    if (!bindings_builder_add_var_fresh(&b, var, to_eval)) {
        bindings_builder_free(&b);
        return;
    }
    const Bindings *bb = bindings_builder_bindings(&b);
    Bindings visible;
    if (!bindings_project_body_visible_env(a, body, bb, &visible)) {
        bindings_builder_free(&b);
        return;
    }
    Atom *subst = bindings_apply_projected_body_visible(&visible, a, body);
    result_set_add(rs, subst);
    bindings_free(&visible);
    bindings_builder_free(&b);
}

static void metta_eval_one_step_let(Space *s, Arena *a, Atom *val_expr,
                                    Atom *pat, Atom *body,
                                    ResultSet *rs) {
    ResultSet inner;
    bool emitted = false;

    result_set_init(&inner);
    metta_eval_one_step(s, a, NULL, val_expr, &inner);

    for (uint32_t i = 0; i < inner.len; i++) {
        Atom *next = inner.items[i];
        if (atom_is_empty(next)) {
            emitted = true;
            continue;
        }
        if (atom_is_error(next)) {
            result_set_add(rs, next);
            emitted = true;
            continue;
        }
        if (atom_eq(next, val_expr))
            continue;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * 4);
        elems[0] = atom_symbol_id(a, g_builtin_syms.let);
        elems[1] = pat;
        elems[2] = next;
        elems[3] = body;
        result_set_add(rs, atom_expr(a, elems, 4));
        emitted = true;
    }
    result_set_free(&inner);
    if (emitted)
        return;

    if (atom_is_empty(val_expr)) {
        result_set_add(rs, atom_empty(a));
        return;
    }
    if (atom_is_error(val_expr)) {
        result_set_add(rs, val_expr);
        return;
    }

    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder b;
    if (!bindings_builder_init(&b, &empty))
        return;
    bool ok = pat->kind == ATOM_VAR
        ? bindings_builder_add_var_fresh(&b, pat, val_expr)
        : simple_match_builder(pat, val_expr, &b);
    if (!ok) {
        bindings_builder_free(&b);
        return;
    }
    const Bindings *bb = bindings_builder_bindings(&b);
    Bindings visible;
    if (!bindings_project_body_visible_env(a, body, bb, &visible)) {
        bindings_builder_free(&b);
        return;
    }
    Atom *subst = bindings_apply_projected_body_visible(&visible, a, body);
    result_set_add(rs, subst);
    bindings_free(&visible);
    bindings_builder_free(&b);
}

static void metta_eval_one_step_let_star(Arena *a, Atom *binding_list,
                                         Atom *body, ResultSet *rs) {
    if (binding_list->kind != ATOM_EXPR)
        return;
    if (binding_list->expr.len == 0) {
        result_set_add(rs, body);
        return;
    }

    Atom *first = binding_list->expr.elems[0];
    if (first->kind != ATOM_EXPR || first->expr.len != 2)
        return;

    Atom *rest =
        atom_expr(a, binding_list->expr.elems + 1, binding_list->expr.len - 1);
    Atom *inner =
        atom_expr3(a, atom_symbol_id(a, g_builtin_syms.let_star), rest, body);
    Atom **elems = arena_alloc(a, sizeof(Atom *) * 4);
    elems[0] = atom_symbol_id(a, g_builtin_syms.let);
    elems[1] = first->expr.elems[0];
    elems[2] = first->expr.elems[1];
    elems[3] = inner;
    result_set_add(rs, atom_expr(a, elems, 4));
}

static void metta_eval_one_step_case(Space *s, Arena *a, Atom *atom,
                                     SymbolId head_sym,
                                     Atom *scrutinee, Atom *branches,
                                     ResultSet *rs) {
    ResultSet inner;
    bool emitted = false;

    result_set_init(&inner);
    metta_eval_one_step(s, a, NULL, scrutinee, &inner);

    for (uint32_t i = 0; i < inner.len; i++) {
        Atom *next = inner.items[i];
        if (atom_is_empty(next)) {
            emitted = true;
            continue;
        }
        if (atom_is_error(next)) {
            result_set_add(rs, next);
            emitted = true;
            continue;
        }
        if (atom_eq(next, scrutinee))
            continue;
        Atom **elems = arena_alloc(a, sizeof(Atom *) * 3);
        elems[0] = atom_symbol_id(a, head_sym);
        elems[1] = next;
        elems[2] = branches;
        result_set_add(rs, atom_expr(a, elems, 3));
        emitted = true;
    }
    result_set_free(&inner);
    if (emitted)
        return;

    if (atom_is_empty(scrutinee)) {
        result_set_add(rs, atom_empty(a));
        return;
    }
    if (atom_is_error(scrutinee)) {
        result_set_add(rs, scrutinee);
        return;
    }

    if (branches->kind != ATOM_EXPR)
        return;
    for (uint32_t i = 0; i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
            BindingsBuilder b;
            if (!bindings_builder_init(&b, NULL))
                return;
            if (simple_match_builder(branch->expr.elems[0], scrutinee, &b)) {
                const Bindings *bb = bindings_builder_bindings(&b);
                Atom *next =
                    bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                result_set_add(rs, next);
                bindings_builder_free(&b);
                return;
            }
            bindings_builder_free(&b);
        }
    }
}

static void metta_eval_one_step_switch(Arena *a, Atom *scrutinee,
                                       Atom *branches, ResultSet *rs) {
    if (branches->kind != ATOM_EXPR)
        return;
    for (uint32_t i = 0; i < branches->expr.len; i++) {
        Atom *branch = branches->expr.elems[i];
        if (branch->kind == ATOM_EXPR && branch->expr.len == 2) {
            BindingsBuilder b;
            if (!bindings_builder_init(&b, NULL))
                return;
            if (simple_match_builder(branch->expr.elems[0], scrutinee, &b)) {
                const Bindings *bb = bindings_builder_bindings(&b);
                Atom *next =
                    bindings_apply_if_vars(bb, a, branch->expr.elems[1]);
                result_set_add(rs, next);
                bindings_builder_free(&b);
                return;
            }
            bindings_builder_free(&b);
        }
    }
}

/* True when the HE small-step rule table has this rule live and enabled. */
static bool he_step_rule_on(CettaLangdefRuleId rule_id) {
    return cetta_langdef_pack_rule_enabled(cetta_langdef_pack_he_small_step(),
                                           rule_id);
}

static bool metta_eval_one_step_expr_congruence(Space *s, Arena *a, Atom *atom,
                                                ResultSet *rs) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0)
        return false;

    for (CettaExprIndex idx = 0; idx < atom->expr.len; idx++) {
        Atom *elem = atom->expr.elems[idx];
        ResultSet inner;
        bool emitted = false;

        result_set_init(&inner);
        metta_eval_one_step(s, a, NULL, elem, &inner);

        for (uint32_t i = 0; i < inner.len; i++) {
            Atom *next = inner.items[i];
            if (atom_eq(next, elem))
                continue;
            if (atom_is_empty(next) || atom_is_error(next)) {
                result_set_add(rs, next);
            } else {
                Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
                for (CettaExprIndex j = 0; j < atom->expr.len; j++)
                    elems[j] = (j == idx) ? next : atom->expr.elems[j];
                result_set_add(rs, atom_expr(a, elems, atom->expr.len));
            }
            emitted = true;
        }

        result_set_free(&inner);
        if (emitted)
            return true;
    }

    return false;
}

static void metta_eval_one_step(Space *s, Arena *a, Atom *type, Atom *atom,
                                ResultSet *rs) {
    __attribute__((cleanup(eval_c_stack_guard_leave)))
    EvalCStackGuard stack_guard = {0};
    if (eval_cancel_check())
        return;
    if (!eval_c_stack_guard_enter(1, &stack_guard)) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_EVAL_C_STACK_GUARD_TRIP_EVAL);
        result_set_add(rs, atom_error(a, atom, atom_symbol(a, "StackOverflow")));
        return;
    }

    Atom *etype = type ? type : atom_undefined_type(a);

    Atom *bound = registry_lookup_atom(atom);
    if (bound) {
        result_set_add(rs, bound);
        return;
    }
    atom = materialize_runtime_token(s, a, atom);

    if (atom_is_empty(atom) || atom_is_error(atom)) {
        result_set_add(rs, atom);
        return;
    }

    Atom *meta = get_meta_type(a, atom);
    if (atom_is_symbol_id(etype, g_builtin_syms.atom) || atom_eq(etype, meta) ||
        atom_is_symbol_id(meta, g_builtin_syms.variable)) {
        result_set_add(rs, atom);
        return;
    }

    if (atom->kind == ATOM_SYMBOL || atom->kind == ATOM_GROUNDED ||
        (atom->kind == ATOM_EXPR && atom->expr.len == 0)) {
        type_cast_fn(s, a, atom, etype, 1, rs);
        return;
    }

    if (atom->kind == ATOM_VAR) {
        result_set_add(rs, atom);
        return;
    }

    if (atom->kind != ATOM_EXPR || atom->expr.len == 0) {
        result_set_add(rs, atom);
        return;
    }

    {
        Atom *head = atom->expr.elems[0];
        uint32_t nargs = atom->expr.len - 1;
        SymbolId head_id = head->kind == ATOM_SYMBOL ? head->sym_id : SYMBOL_ID_NONE;

        /* Special forms: each rule class below is table-gated (HES_*) and is
         * the only implementation of that rule in the one-step subsystem.
         * A disabled rule leaves its head uninterpreted (inert; argument
         * congruence still applies, per the unknown-symbols-stay-inert
         * principle). */
        if (head_id == g_builtin_syms.eval && nargs == 1 &&
            he_step_rule_on(CETTA_HES_RULE_EVAL)) {
            result_set_add(rs, expr_arg(atom, 0));
            return;
        }
        if (head_id == g_builtin_syms.chain &&
            he_step_rule_on(CETTA_HES_RULE_CHAIN)) {
            if (nargs == 3) {
                metta_eval_one_step_chain(s, a, atom, expr_arg(atom, 0),
                                          expr_arg(atom, 1), expr_arg(atom, 2), rs);
            } else {
                result_set_add(rs,
                               atom_error(a, atom,
                                          atom_symbol(a, "IncorrectNumberOfArguments")));
            }
            return;
        }
        if (head_id == g_builtin_syms.let_star && nargs == 2 &&
            he_step_rule_on(CETTA_HES_RULE_LET_STAR)) {
            metta_eval_one_step_let_star(a, expr_arg(atom, 0), expr_arg(atom, 1), rs);
            return;
        }
        if (head_id == g_builtin_syms.let && nargs == 3 &&
            he_step_rule_on(CETTA_HES_RULE_LET)) {
            metta_eval_one_step_let(s, a, expr_arg(atom, 1),
                                    expr_arg(atom, 0), expr_arg(atom, 2), rs);
            return;
        }
        if (head_id == g_builtin_syms.case_text &&
            he_step_rule_on(CETTA_HES_RULE_CASE)) {
            if (nargs == 2) {
                metta_eval_one_step_case(s, a, atom, g_builtin_syms.case_text,
                                         expr_arg(atom, 0),
                                         expr_arg(atom, 1), rs);
            } else {
                result_set_add(rs,
                               atom_error(a, atom,
                                          atom_symbol(a, "IncorrectNumberOfArguments")));
            }
            return;
        }
        if (head_id == g_builtin_syms.switch_text &&
            he_step_rule_on(CETTA_HES_RULE_SWITCH)) {
            /* Upstream contract: switch evaluates its scrutinee, so the
             * coarse rule is case-shaped (scrutinee progress, then
             * structural branch selection). */
            if (nargs == 2) {
                metta_eval_one_step_case(s, a, atom, g_builtin_syms.switch_text,
                                         expr_arg(atom, 0),
                                         expr_arg(atom, 1), rs);
            } else {
                result_set_add(rs,
                               atom_error(a, atom,
                                          atom_symbol(a, "IncorrectNumberOfArguments")));
            }
            return;
        }
        if (head_id == g_builtin_syms.switch_minimal &&
            he_step_rule_on(CETTA_HES_RULE_SWITCH_MINIMAL)) {
            if (nargs == 2) {
                metta_eval_one_step_switch(a, expr_arg(atom, 0), expr_arg(atom, 1), rs);
            } else {
                result_set_add(rs,
                               atom_error(a, atom,
                                          atom_symbol(a, "IncorrectNumberOfArguments")));
            }
            return;
        }
        /* HES_GroundedDispatch: pack-gated; this is the only grounded-dispatch
         * implementation in the one-step subsystem. */
        if (head->kind == ATOM_SYMBOL && is_grounded_op(head_id) &&
            he_step_rule_on(CETTA_HES_RULE_GROUNDED_DISPATCH)) {
            Atom *direct = dispatch_native_op(s, a, head, atom->expr.elems + 1, nargs);
            if (direct) {
                result_set_add(rs, direct);
                return;
            }
        }
        /* HES_EquationMatch: table-gated; this is the only equation-match
         * implementation in the one-step subsystem. */
        if (he_step_rule_on(CETTA_HES_RULE_EQUATION_MATCH)) {
            QueryResults qr;
            query_results_init(&qr);
            query_equations(s, atom, a, &qr);
            if (qr.len > 0) {
                for (uint32_t i = 0; i < qr.len; i++) {
                    Atom *next =
                        bindings_apply_if_vars(&qr.items[i].bindings, a, qr.items[i].result);
                    result_set_add(rs, next);
                }
                query_results_free(&qr);
                return;
            }
            query_results_free(&qr);
        }
        if (head_id == g_builtin_syms.quote || head_id == g_builtin_syms.return_text) {
            result_set_add(rs, atom);
            return;
        }
        /* HES_LeftmostExprCongruence: pack-gated; this is the only argument-
         * congruence implementation in the one-step subsystem. */
        if (he_step_rule_on(CETTA_HES_RULE_LEFTMOST_EXPR_CONGRUENCE) &&
            metta_eval_one_step_expr_congruence(s, a, atom, rs))
            return;
    }

    result_set_add(rs, atom);
}


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

void eval_top_one_step(Space *s, Arena *a, Atom *expr, ResultSet *rs) {
    Registry *prev_registry = g_registry;
    Space *prev_root_space = g_eval_root_space;
    Arena *prev_fallback_persistent = g_eval_fallback_universe.persistent_arena;
    g_registry = NULL;
    g_eval_root_space = s;
    term_universe_set_persistent_arena(&g_eval_fallback_universe, NULL);
    eval_release_outcome_variant_bank();
    metta_eval_one_step(s, a, NULL, expr, rs);
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
    if (!ctx) return;
    ctx->session.options.fuel_limit = fallback_eval_session()->options.fuel_limit;
}
