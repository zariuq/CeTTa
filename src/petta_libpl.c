#define _GNU_SOURCE
#include "petta_libpl.h"

#include "grounded.h"
#include "stats.h"
#include "symbol.h"

#include <SWI-Prolog.h>

#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SymbolId symbol;
    uint64_t symbol_table_instance;
    char *name;
    size_t name_len;
    size_t *function_arities;
    size_t arity_len;
    size_t arity_cap;
    uint64_t scanned_revision;
} PettaLibplImport;

struct CettaLibPrologRuntime {
    bool prepared;
    char *module_name;
    module_t module;
    PettaLibplImport *imports;
    size_t import_len;
    size_t import_cap;
    uint64_t symbol_table_instance;
    uint64_t revision;
    _Atomic uint64_t import_admission[1024];
    _Atomic bool import_admission_saturated;
};

typedef struct {
    VarId id;
    Atom *prototype;
    term_t term;
} PettaLibplVar;

typedef struct {
    PettaLibplVar *items;
    size_t len;
    size_t cap;
} PettaLibplVarMap;

typedef struct {
    term_t term;
    Atom *variable;
} PettaLibplBackVar;

typedef struct {
    PettaLibplBackVar *items;
    size_t len;
    size_t cap;
} PettaLibplBackVarMap;

static pthread_once_t g_petta_libpl_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_petta_libpl_lock = PTHREAD_MUTEX_INITIALIZER;
static bool g_petta_libpl_ready;
static bool g_petta_libpl_owns_engine;
static PL_engine_t g_petta_libpl_worker_engine;
static _Atomic uint64_t g_petta_libpl_runtime_counter = 1u;
static _Thread_local Arena *g_petta_libpl_active_arena;

typedef struct {
#if defined(SIGSTKSZ) && defined(SA_ONSTACK)
    stack_t value;
    bool saved;
#endif
} PettaLibplHostAltStack;

/*
 * CeTTa owns the host thread's signal stack.  SWI allocates and selects a
 * private alternate stack while initialising each engine even when embedded
 * with --no-signals.  Preserve the host selection around those initialisers:
 * the engine retains ownership of its allocation, while short-lived CeTTa
 * workers keep the sanitizer/runtime stack they entered with.
 */
static bool petta_libpl_host_altstack_save(
    PettaLibplHostAltStack *saved) {
    if (!saved)
        return false;
#if defined(SIGSTKSZ) && defined(SA_ONSTACK)
    memset(saved, 0, sizeof(*saved));
    if (sigaltstack(NULL, &saved->value) != 0)
        return false;
    saved->saved = true;
#else
    (void)saved;
#endif
    return true;
}

static bool petta_libpl_host_altstack_restore(
    const PettaLibplHostAltStack *saved) {
    if (!saved)
        return false;
#if defined(SIGSTKSZ) && defined(SA_ONSTACK)
    return !saved->saved ||
           sigaltstack(&saved->value, NULL) == 0;
#else
    (void)saved;
    return true;
#endif
}

static uint64_t petta_libpl_import_hash(SymbolId symbol) {
    uint64_t value = (uint64_t)symbol +
                     UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) *
            UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) *
            UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}

static void petta_libpl_import_admission_clear(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    atomic_store_explicit(
        &runtime->import_admission_saturated, true,
        memory_order_release);
    for (size_t index = 0u; index < 1024u; index++) {
        atomic_store_explicit(
            &runtime->import_admission[index], 0u,
            memory_order_release);
    }
    atomic_store_explicit(
        &runtime->import_admission_saturated, false,
        memory_order_release);
}

static bool petta_libpl_import_admission_add(
    CettaLibPrologRuntime *runtime, SymbolId symbol) {
    if (!runtime || symbol == SYMBOL_ID_NONE)
        return false;
    uint64_t hash = petta_libpl_import_hash(symbol);
    size_t slot = (size_t)(hash & 1023u);
    size_t stride = (size_t)(((hash >> 32) & 1023u) | 1u);
    for (size_t probe = 0u; probe < 1024u; probe++) {
        uint64_t current = atomic_load_explicit(
            &runtime->import_admission[slot],
            memory_order_relaxed);
        if (current == (uint64_t)symbol)
            return true;
        if (current == 0u) {
            atomic_store_explicit(
                &runtime->import_admission[slot],
                (uint64_t)symbol, memory_order_release);
            return true;
        }
        slot = (slot + stride) & 1023u;
    }
    atomic_store_explicit(
        &runtime->import_admission_saturated, true,
        memory_order_release);
    return false;
}

static bool petta_libpl_import_admission_maybe_contains(
    const CettaLibPrologRuntime *runtime, SymbolId symbol) {
    if (!runtime || symbol == SYMBOL_ID_NONE)
        return false;
    /*
     * Saturation is a performance state, never a semantic failure: readers
     * conservatively fall through to the locked registry when the derived
     * index cannot represent another name.
     */
    if (atomic_load_explicit(
            &runtime->import_admission_saturated,
            memory_order_acquire)) {
        return true;
    }
    uint64_t hash = petta_libpl_import_hash(symbol);
    size_t slot = (size_t)(hash & 1023u);
    size_t stride = (size_t)(((hash >> 32) & 1023u) | 1u);
    for (size_t probe = 0u; probe < 1024u; probe++) {
        uint64_t current = atomic_load_explicit(
            &runtime->import_admission[slot],
            memory_order_acquire);
        if (current == (uint64_t)symbol)
            return true;
        if (current == 0u)
            return false;
        slot = (slot + stride) & 1023u;
    }
    return true;
}

static void petta_libpl_global_cleanup(void) {
    if (!g_petta_libpl_ready)
        return;
    if (g_petta_libpl_worker_engine) {
        if (!PL_destroy_engine(g_petta_libpl_worker_engine)) {
            fputs(
                "fatal: failed to destroy pooled libpl engine\n",
                stderr);
            abort();
        }
        g_petta_libpl_worker_engine = NULL;
    }
    if (g_petta_libpl_owns_engine)
        (void)PL_cleanup(PL_CLEANUP_NO_CANCEL);
    g_petta_libpl_ready = false;
    g_petta_libpl_owns_engine = false;
}

void cetta_lib_prolog_global_shutdown(void) {
    if (pthread_mutex_lock(&g_petta_libpl_lock) != 0)
        return;
    petta_libpl_global_cleanup();
    (void)pthread_mutex_unlock(&g_petta_libpl_lock);
}

static void petta_libpl_global_init(void) {
    if (PL_is_initialised(NULL, NULL)) {
        g_petta_libpl_ready = true;
    } else {
        static char program[] = "cetta-libpl";
        static char quiet[] = "-q";
        static char no_signals[] = "--no-signals";
        static char no_alert_signal[] = "--sigalert=0";
        static char no_init[] = "-f";
        static char no_init_file[] = "none";
        char *arguments[] = {
            program, quiet, no_signals, no_alert_signal,
            no_init, no_init_file, NULL,
        };
        PettaLibplHostAltStack host_altstack;
        if (!petta_libpl_host_altstack_save(&host_altstack))
            return;
        bool initialised = PL_initialise(6, arguments);
        bool restored =
            petta_libpl_host_altstack_restore(&host_altstack);
        if (!initialised || !restored) {
            if (initialised)
                (void)PL_cleanup(PL_CLEANUP_NO_CANCEL);
            return;
        }
        g_petta_libpl_ready = true;
        if (!g_petta_libpl_ready)
            return;
        g_petta_libpl_owns_engine = true;
    }

    if (atexit(petta_libpl_global_cleanup) != 0) {
        petta_libpl_global_cleanup();
    }
}

static bool petta_libpl_enter(bool *claimed) {
    if (claimed)
        *claimed = false;
    if (!claimed ||
        pthread_once(
            &g_petta_libpl_once,
            petta_libpl_global_init) != 0 ||
        !g_petta_libpl_ready ||
        pthread_mutex_lock(&g_petta_libpl_lock) != 0) {
        return false;
    }
    if (!PL_current_engine()) {
        /*
         * Keep the optional foreign boundary absent from native-only PeTTa
         * executions.  SWI's many-to-many engine interface is designed for
         * infrequent calls from a larger native worker pool: create one
         * detached engine on the first actual crossing, then claim it under
         * this lock for each boundary call.  This avoids both eager engine
         * construction and per-worker attach/destroy churn.
         */
        if (!g_petta_libpl_worker_engine) {
            PettaLibplHostAltStack host_altstack;
            if (!petta_libpl_host_altstack_save(
                    &host_altstack)) {
                (void)pthread_mutex_unlock(
                    &g_petta_libpl_lock);
                return false;
            }
            PL_engine_t engine = PL_create_engine(NULL);
            bool restored =
                petta_libpl_host_altstack_restore(
                    &host_altstack);
            if (!restored) {
                if (engine)
                    (void)PL_destroy_engine(engine);
                (void)pthread_mutex_unlock(
                    &g_petta_libpl_lock);
                return false;
            }
            g_petta_libpl_worker_engine = engine;
        }
        if (!g_petta_libpl_worker_engine ||
            PL_set_engine(
                g_petta_libpl_worker_engine,
                NULL) != PL_ENGINE_SET) {
            (void)pthread_mutex_unlock(
                &g_petta_libpl_lock);
            return false;
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_LIB_PROLOG_ENGINE_CLAIM);
        *claimed = true;
    }
    return true;
}

static void petta_libpl_leave(bool claimed) {
    if (claimed) {
        if (PL_set_engine(NULL, NULL) != PL_ENGINE_SET) {
            fputs(
                "fatal: failed to release libpl worker engine\n",
                stderr);
            abort();
        }
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_LIB_PROLOG_ENGINE_RELEASE);
    }
    (void)pthread_mutex_unlock(&g_petta_libpl_lock);
}

static char *petta_libpl_copy_bytes(
    const char *bytes, size_t length) {
    if (!bytes || length == SIZE_MAX)
        return NULL;
    char *copy = cetta_malloc(length + 1u);
    if (length)
        memcpy(copy, bytes, length);
    copy[length] = '\0';
    return copy;
}

/*
 * The process-wide SWI engine and each context-private module are both
 * demand-created.  This function runs only while g_petta_libpl_lock is held
 * and the calling thread owns an engine.
 */
static bool petta_libpl_prepare_locked(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return false;
    if (runtime->prepared)
        return true;

    uint64_t number = atomic_fetch_add_explicit(
        &g_petta_libpl_runtime_counter, 1u,
        memory_order_relaxed);
    char name[96];
    int length = snprintf(
        name, sizeof(name),
        "cetta_prolog_%llu",
        (unsigned long long)number);
    if (length <= 0 || (size_t)length >= sizeof(name))
        return false;

    char *module_name =
        petta_libpl_copy_bytes(name, (size_t)length);
    if (!module_name)
        return false;
    atom_t module_symbol =
        PL_new_atom_nchars((size_t)length, name);
    module_t module = module_symbol
        ? PL_new_module(module_symbol) : NULL;
    if (module_symbol)
        PL_unregister_atom(module_symbol);
    if (!module) {
        free(module_name);
        return false;
    }

    runtime->module_name = module_name;
    runtime->module = module;
    runtime->symbol_table_instance =
        symbol_table_instance_id(g_symbols);
    runtime->revision = 1u;
    runtime->prepared = true;
    cetta_runtime_stats_inc(
        CETTA_RUNTIME_COUNTER_LIB_PROLOG_PREPARE);
    return true;
}

static bool petta_libpl_symbol_is(
    SymbolId symbol, const char *name) {
    return g_symbols && name &&
           symbol_eq_cstr(g_symbols, symbol, name);
}

static bool petta_libpl_atom_is_symbol(
    const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
           petta_libpl_symbol_is(atom->sym_id, name);
}

static void petta_libpl_import_clear(
    PettaLibplImport *entry) {
    if (!entry)
        return;
    free(entry->name);
    free(entry->function_arities);
    memset(entry, 0, sizeof(*entry));
}

static void petta_libpl_sync_symbol_table(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    uint64_t current =
        symbol_table_instance_id(g_symbols);
    if (runtime->symbol_table_instance == current)
        return;
    for (size_t index = 0u;
         index < runtime->import_len; index++) {
        petta_libpl_import_clear(
            &runtime->imports[index]);
    }
    runtime->import_len = 0u;
    petta_libpl_import_admission_clear(runtime);
    runtime->symbol_table_instance = current;
    runtime->revision++;
}

static PettaLibplImport *petta_libpl_find_import(
    CettaLibPrologRuntime *runtime, SymbolId symbol) {
    if (!runtime || symbol == SYMBOL_ID_NONE)
        return NULL;
    petta_libpl_sync_symbol_table(runtime);
    for (size_t index = 0u;
         index < runtime->import_len; index++) {
        PettaLibplImport *entry =
            &runtime->imports[index];
        if (entry->symbol_table_instance ==
                runtime->symbol_table_instance &&
            entry->symbol == symbol) {
            return entry;
        }
    }
    return NULL;
}

/*
 * PeTTa's language basis contains a small set of Prolog-backed functions
 * that are callable without an authored import.  Keep this boundary explicit:
 * arbitrary unknown MeTTa heads remain inert, while the listed translator
 * predicates may use the optional adapter.
 */
static size_t petta_libpl_standard_function_arities(
    SymbolId symbol, size_t arities[2]) {
    if (arities) {
        arities[0] = 0u;
        arities[1] = 0u;
    }
    if (!g_symbols || symbol == SYMBOL_ID_NONE)
        return 0u;
    const char *name = symbol_bytes(g_symbols, symbol);
    if (name && strcmp(name, "exists_file") == 0) {
        if (arities)
            arities[0] = 0u;
        return 1u;
    }
    if (name && strcmp(name, "library") == 0) {
        if (arities) {
            arities[0] = 1u;
            arities[1] = 2u;
        }
        return 2u;
    }
    return 0u;
}

static PettaLibplImport *petta_libpl_register_import(
    CettaLibPrologRuntime *runtime, SymbolId symbol) {
    PettaLibplImport *existing =
        petta_libpl_find_import(runtime, symbol);
    if (existing)
        return existing;
    if (!runtime || !g_symbols ||
        symbol == SYMBOL_ID_NONE) {
        return NULL;
    }

    if (runtime->import_len == runtime->import_cap) {
        size_t next =
            runtime->import_cap
                ? runtime->import_cap * 2u : 16u;
        if (next <= runtime->import_cap ||
            next > SIZE_MAX / sizeof(*runtime->imports)) {
            return NULL;
        }
        runtime->imports = runtime->imports
            ? cetta_realloc(
                  runtime->imports,
                  sizeof(*runtime->imports) * next)
            : cetta_malloc(
                  sizeof(*runtime->imports) * next);
        memset(
            runtime->imports + runtime->import_cap, 0,
            sizeof(*runtime->imports) *
                (next - runtime->import_cap));
        runtime->import_cap = next;
    }

    uint32_t length = symbol_len(g_symbols, symbol);
    const char *name = symbol_bytes(g_symbols, symbol);
    char *copy = petta_libpl_copy_bytes(
        name, (size_t)length);
    if (!copy)
        return NULL;
    PettaLibplImport *entry =
        &runtime->imports[runtime->import_len++];
    *entry = (PettaLibplImport){
        .symbol = symbol,
        .symbol_table_instance =
            runtime->symbol_table_instance,
        .name = copy,
        .name_len = length,
    };
    (void)petta_libpl_import_admission_add(runtime, symbol);
    runtime->revision++;
    return entry;
}

static bool petta_libpl_import_add_arity(
    PettaLibplImport *entry, size_t arity) {
    if (!entry)
        return false;
    for (size_t index = 0u;
         index < entry->arity_len; index++) {
        if (entry->function_arities[index] == arity)
            return true;
    }
    if (entry->arity_len == entry->arity_cap) {
        size_t next =
            entry->arity_cap ? entry->arity_cap * 2u : 4u;
        if (next <= entry->arity_cap ||
            next > SIZE_MAX /
                sizeof(*entry->function_arities)) {
            return false;
        }
        entry->function_arities =
            entry->function_arities
                ? cetta_realloc(
                      entry->function_arities,
                      sizeof(*entry->function_arities) * next)
                : cetta_malloc(
                      sizeof(*entry->function_arities) * next);
        entry->arity_cap = next;
    }
    entry->function_arities[entry->arity_len++] =
        arity;
    return true;
}

static bool petta_libpl_make_indicator(
    const PettaLibplImport *entry,
    term_t indicator, term_t arity) {
    if (!entry || !indicator || !arity)
        return false;
    term_t arguments = PL_new_term_refs(2u);
    if (!arguments ||
        !PL_put_atom_nchars(
            arguments, entry->name_len, entry->name) ||
        !PL_put_variable(arguments + 1u)) {
        return false;
    }
    atom_t slash = PL_new_atom("/");
    functor_t indicator_functor =
        PL_new_functor(slash, 2u);
    bool built =
        indicator_functor &&
        PL_cons_functor_v(
            indicator, indicator_functor, arguments) &&
        /*
         * Read the variable back from the constructed indicator.  The FLI
         * copies the argument vector into the compound; retaining the
         * pre-construction reference is not a portable way to observe the
         * binding produced by current_predicate/1.
         */
        PL_get_arg(2, indicator, arity);
    PL_unregister_atom(slash);
    return built;
}

static bool petta_libpl_refresh_arities(
    CettaLibPrologRuntime *runtime,
    PettaLibplImport *entry) {
    if (!runtime || !entry)
        return false;
    if (entry->scanned_revision == runtime->revision)
        return true;

    entry->arity_len = 0u;
    fid_t frame = PL_open_foreign_frame();
    term_t indicator = PL_new_term_ref();
    term_t arity = PL_new_term_ref();
    if (!frame || !indicator || !arity ||
        !petta_libpl_make_indicator(
            entry, indicator, arity)) {
        if (frame)
            PL_discard_foreign_frame(frame);
        return false;
    }

    predicate_t current_predicate =
        PL_predicate("current_predicate", 1, NULL);
    qid_t query = PL_open_query(
        runtime->module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
            PL_Q_EXT_STATUS,
        current_predicate, indicator);
    if (!query) {
        PL_discard_foreign_frame(frame);
        return false;
    }

    bool ok = true;
    for (;;) {
        int status = PL_next_solution(query);
        if (status == PL_S_FALSE)
            break;
        if (status == PL_S_EXCEPTION ||
            status == PL_S_YIELD ||
            status == PL_S_YIELD_DEBUG ||
            (status != PL_S_TRUE &&
             status != PL_S_LAST)) {
            ok = false;
            break;
        }
        int64_t predicate_arity = 0;
        if (!PL_get_int64(arity, &predicate_arity) ||
            predicate_arity <= 0 ||
            (uint64_t)predicate_arity >
                (uint64_t)SIZE_MAX ||
            !petta_libpl_import_add_arity(
                entry,
                (size_t)predicate_arity - 1u)) {
            ok = false;
            break;
        }
        if (status == PL_S_LAST)
            break;
    }
    (void)PL_close_query(query);
    PL_discard_foreign_frame(frame);
    if (ok)
        entry->scanned_revision = runtime->revision;
    return ok;
}

static PettaLibplVar *petta_libpl_var_find(
    PettaLibplVarMap *variables, VarId id) {
    if (!variables || id == VAR_ID_NONE)
        return NULL;
    for (size_t index = 0u;
         index < variables->len; index++) {
        if (variables->items[index].id == id)
            return &variables->items[index];
    }
    return NULL;
}

static PettaLibplVar *petta_libpl_var_add(
    PettaLibplVarMap *variables, Atom *variable) {
    if (!variables || !variable ||
        variable->kind != ATOM_VAR) {
        return NULL;
    }
    PettaLibplVar *existing =
        petta_libpl_var_find(
            variables, variable->var_id);
    if (existing)
        return existing;
    if (variables->len == variables->cap) {
        size_t next =
            variables->cap ? variables->cap * 2u : 16u;
        if (next <= variables->cap ||
            next > SIZE_MAX / sizeof(*variables->items)) {
            return NULL;
        }
        variables->items = variables->items
            ? cetta_realloc(
                  variables->items,
                  sizeof(*variables->items) * next)
            : cetta_malloc(
                  sizeof(*variables->items) * next);
        variables->cap = next;
    }
    term_t term = PL_new_term_ref();
    if (!term || !PL_put_variable(term))
        return NULL;
    PettaLibplVar *entry =
        &variables->items[variables->len++];
    *entry = (PettaLibplVar){
        .id = variable->var_id,
        .prototype = variable,
        .term = term,
    };
    return entry;
}

static bool petta_libpl_to_term(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth);

static bool petta_libpl_quote_body(
    Atom *atom, Atom **body) {
    if (body)
        *body = NULL;
    if (!atom || !body ||
        atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        !petta_libpl_atom_is_symbol(
            atom->expr.elems[0], "quote")) {
        return false;
    }
    *body = atom->expr.elems[1];
    return *body != NULL;
}

static bool petta_libpl_predicate_body(
    Atom *atom, Atom **body) {
    if (body)
        *body = NULL;
    if (!atom || !body)
        return false;
    /*
     * A demanded Predicate has already crossed the list/compound boundary
     * and carries CeTTa's private Prolog-compound tag.  Predicate operations
     * accept that value just as they accept the visible source wrapper.
     */
    if (atom_petta_prolog_compound_body(atom, body))
        return true;
    if (atom_prolog_compound_body(atom, body))
        return true;
    if (
        atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        !petta_libpl_atom_is_symbol(
            atom->expr.elems[0], "Predicate")) {
        return false;
    }
    *body = atom->expr.elems[1];
    return *body != NULL;
}

static bool petta_libpl_to_callable(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth) {
    if (!atom || !output || !variables ||
        depth > 1024u) {
        return false;
    }
    Atom *unquoted = NULL;
    if (petta_libpl_quote_body(atom, &unquoted))
        return petta_libpl_to_callable(
            unquoted, output, variables, depth + 1u);

    Atom *predicate = NULL;
    if (petta_libpl_predicate_body(atom, &predicate))
        return petta_libpl_to_callable(
            predicate, output, variables, depth + 1u);
    if (atom_petta_prolog_compound_body(
            atom, &predicate)) {
        return petta_libpl_to_callable(
            predicate, output, variables, depth + 1u);
    }

    if (atom->kind == ATOM_SYMBOL) {
        return PL_put_atom_nchars(
            output, symbol_len(g_symbols, atom->sym_id),
            symbol_bytes(g_symbols, atom->sym_id));
    }
    if (atom->kind != ATOM_EXPR ||
        atom->expr.len == 0u ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL ||
        atom->expr.len - 1u > (CettaExprLen)INT_MAX) {
        return false;
    }

    size_t arity = (size_t)(atom->expr.len - 1u);
    term_t arguments =
        arity ? PL_new_term_refs(arity) : 0;
    if (arity && !arguments)
        return false;
    for (size_t index = 0u; index < arity; index++) {
        Atom *argument =
            atom->expr.elems[index + 1u];
        Atom *wrapped = NULL;
        bool ok = petta_libpl_predicate_body(
                      argument, &wrapped)
            ? petta_libpl_to_callable(
                  wrapped, arguments + index,
                  variables, depth + 1u)
            : petta_libpl_to_term(
                  argument, arguments + index,
                  variables, depth + 1u);
        if (!ok)
            return false;
    }

    atom_t name = PL_new_atom_nchars(
        symbol_len(
            g_symbols, atom->expr.elems[0]->sym_id),
        symbol_bytes(
            g_symbols, atom->expr.elems[0]->sym_id));
    functor_t functor = PL_new_functor_sz(name, arity);
    bool built = functor &&
                 PL_cons_functor_v(
                     output, functor, arguments);
    PL_unregister_atom(name);
    return built;
}

static bool petta_libpl_to_list(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth) {
    if (!atom || atom->kind != ATOM_EXPR ||
        !output || !variables || depth > 1024u ||
        !PL_put_nil(output)) {
        return false;
    }
    term_t item = PL_new_term_ref();
    term_t tail = PL_new_term_ref();
    if (!item || !tail)
        return false;
    for (CettaExprIndex index = atom->expr.len;
         index > 0u; index--) {
        if (!PL_put_variable(item) ||
            !petta_libpl_to_term(
                atom->expr.elems[index - 1u], item,
                variables, depth + 1u) ||
            !PL_put_term(tail, output) ||
            !PL_cons_list(output, item, tail)) {
            return false;
        }
    }
    return true;
}

static bool petta_libpl_to_term(
    Atom *atom, term_t output,
    PettaLibplVarMap *variables, uint32_t depth) {
    if (!atom || !output || !variables ||
        depth > 1024u) {
        return false;
    }
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return PL_put_atom_nchars(
            output, symbol_len(g_symbols, atom->sym_id),
            symbol_bytes(g_symbols, atom->sym_id));
    case ATOM_VAR: {
        PettaLibplVar *variable =
            petta_libpl_var_add(variables, atom);
        return variable &&
               PL_put_term(output, variable->term);
    }
    case ATOM_EXPR: {
        Atom *compound = NULL;
        if (atom_petta_prolog_compound_body(
                atom, &compound)) {
            return petta_libpl_to_callable(
                compound, output, variables,
                depth + 1u);
        }
        Atom *predicate = NULL;
        if (petta_libpl_predicate_body(
                atom, &predicate)) {
            return petta_libpl_to_callable(
                predicate, output, variables,
                depth + 1u);
        }
        return petta_libpl_to_list(
            atom, output, variables, depth + 1u);
    }
    case ATOM_GROUNDED:
        break;
    }

    switch (atom->ground.gkind) {
    case GV_INT:
        return PL_put_int64(output, atom->ground.ival);
    case GV_FLOAT:
        return PL_put_float(output, atom->ground.fval);
    case GV_BOOL:
        return PL_put_bool(
            output, atom->ground.bval ? 1 : 0);
    case GV_STRING: {
        const char *value =
            atom->ground.sval ? atom->ground.sval : "";
        return PL_put_string_nchars(
            output, strlen(value), value);
    }
    case GV_BIGINT: {
        const char *value = atom_bigint_cstr(atom);
        return value &&
               PL_put_term_from_chars(
                   output, REP_UTF8,
                   strlen(value), value);
    }
    case GV_RATIONAL: {
        const char *value = atom_rational_cstr(atom);
        return value &&
               PL_put_term_from_chars(
                   output, REP_UTF8,
                   strlen(value), value);
    }
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_FOREIGN:
    case GV_PRIME_NEED_CAPABILITY:
    case GV_PRIME_CONTEXT:
    case GV_INTERNAL_TAG:
        return false;
    }
    return false;
}

static Atom *petta_libpl_back_variable(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown) {
    if (!arena || !term || !variables || !unknown)
        return NULL;
    for (size_t index = 0u;
         index < variables->len; index++) {
        if (PL_compare(
                term, variables->items[index].term) == 0) {
            return atom_var_like(
                arena, variables->items[index].prototype,
                variables->items[index].id);
        }
    }
    for (size_t index = 0u;
         index < unknown->len; index++) {
        if (PL_compare(
                term, unknown->items[index].term) == 0) {
            return unknown->items[index].variable;
        }
    }
    if (unknown->len == unknown->cap) {
        size_t next =
            unknown->cap ? unknown->cap * 2u : 8u;
        if (next <= unknown->cap ||
            next > SIZE_MAX / sizeof(*unknown->items)) {
            return NULL;
        }
        unknown->items = unknown->items
            ? cetta_realloc(
                  unknown->items,
                  sizeof(*unknown->items) * next)
            : cetta_malloc(
                  sizeof(*unknown->items) * next);
        unknown->cap = next;
    }
    term_t copy = PL_new_term_ref();
    Atom *variable = atom_var_with_id(
        arena, "$foreign", fresh_var_id());
    if (!copy || !variable ||
        !PL_put_term(copy, term)) {
        return NULL;
    }
    unknown->items[unknown->len++] =
        (PettaLibplBackVar){
            .term = copy,
            .variable = variable,
        };
    return variable;
}

static Atom *petta_libpl_from_term_mode(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    bool visible_compounds, uint32_t depth);

static Atom *petta_libpl_from_term(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    uint32_t depth) {
    return petta_libpl_from_term_mode(
        arena, term, variables, unknown,
        false, depth);
}

static Atom *petta_libpl_from_list(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    bool visible_compounds, uint32_t depth) {
    if (!arena || !term || !variables || !unknown ||
        depth > 1024u || !PL_is_acyclic(term)) {
        return NULL;
    }

    term_t cursor = PL_new_term_ref();
    term_t head = PL_new_term_ref();
    term_t tail = PL_new_term_ref();
    if (!cursor || !head || !tail ||
        !PL_put_term(cursor, term)) {
        return NULL;
    }

    Atom **items = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    while (!PL_get_nil(cursor)) {
        if (!PL_get_list(cursor, head, tail)) {
            free(items);
            return NULL;
        }
        if (length == capacity) {
            size_t next = capacity ? capacity * 2u : 8u;
            if (next <= capacity ||
                next > SIZE_MAX / sizeof(*items)) {
                free(items);
                return NULL;
            }
            items = items
                ? cetta_realloc(
                      items, sizeof(*items) * next)
                : cetta_malloc(
                      sizeof(*items) * next);
            capacity = next;
        }
        Atom *item = petta_libpl_from_term_mode(
            arena, head, variables, unknown,
            visible_compounds, depth + 1u);
        if (!item) {
            free(items);
            return NULL;
        }
        items[length++] = item;
        if (!PL_put_term(cursor, tail)) {
            free(items);
            return NULL;
        }
    }
    Atom *result = atom_expr(
        arena, items, (CettaExprLen)length);
    free(items);
    return result;
}

static Atom *petta_libpl_from_term_mode(
    Arena *arena, term_t term,
    PettaLibplVarMap *variables,
    PettaLibplBackVarMap *unknown,
    bool visible_compounds, uint32_t depth) {
    if (!arena || !term || !variables || !unknown ||
        depth > 1024u) {
        return NULL;
    }

    int type = PL_term_type(term);
    if (type == PL_VARIABLE) {
        return petta_libpl_back_variable(
            arena, term, variables, unknown);
    }
    if (type == PL_NIL)
        return atom_expr(arena, NULL, 0u);
    if (type == PL_INTEGER) {
        int64_t value = 0;
        if (PL_get_int64(term, &value))
            return atom_int(arena, value);
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_nchars(
                term, &length, &text,
                CVT_INTEGER | BUF_MALLOC |
                    REP_UTF8)) {
            return NULL;
        }
        Atom *result = atom_bigint(arena, text);
        PL_free(text);
        return result;
    }
    if (type == PL_FLOAT) {
        double value = 0.0;
        return PL_get_float(term, &value)
            ? atom_float(arena, value) : NULL;
    }
    if (type == PL_RATIONAL) {
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_nchars(
                term, &length, &text,
                CVT_RATIONAL | CVT_WRITEQ |
                    BUF_MALLOC | REP_UTF8)) {
            return NULL;
        }
        Atom *result = atom_rational(arena, text);
        PL_free(text);
        return result;
    }
    if (type == PL_STRING) {
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_string(term, &text, &length))
            return NULL;
        char *copy = arena_alloc(arena, length + 1u);
        if (!copy)
            return NULL;
        if (length)
            memcpy(copy, text, length);
        copy[length] = '\0';
        return atom_string(arena, copy);
    }
    if (type == PL_ATOM) {
        char *text = NULL;
        size_t length = 0u;
        if (!PL_get_atom_nchars(
                term, &length, &text) ||
            length > UINT32_MAX) {
            return NULL;
        }
        SymbolId symbol = symbol_intern_bytes(
            g_symbols, (const uint8_t *)text,
            (uint32_t)length);
        return symbol == SYMBOL_ID_NONE
            ? NULL : atom_symbol_id(arena, symbol);
    }
    if (PL_is_list(term)) {
        return petta_libpl_from_list(
            arena, term, variables, unknown,
            visible_compounds, depth + 1u);
    }
    if (type != PL_TERM)
        return NULL;

    atom_t name = 0;
    size_t arity = 0u;
    if (!PL_get_compound_name_arity_sz(
            term, &name, &arity) ||
        arity > UINT64_MAX - 1u ||
        arity + 1u >
            SIZE_MAX / sizeof(Atom *)) {
        return NULL;
    }
    size_t name_len = 0u;
    const char *name_bytes =
        PL_atom_nchars(name, &name_len);
    if (!name_bytes || name_len > UINT32_MAX)
        return NULL;
    SymbolId symbol = symbol_intern_bytes(
        g_symbols, (const uint8_t *)name_bytes,
        (uint32_t)name_len);
    if (symbol == SYMBOL_ID_NONE)
        return NULL;

    Atom **items = cetta_malloc(
        sizeof(*items) * (arity + 1u));
    items[0] = atom_symbol_id(arena, symbol);
    term_t argument = PL_new_term_ref();
    bool ok = items[0] && argument;
    for (size_t index = 0u; ok && index < arity;
         index++) {
        ok = PL_get_arg_sz(
                 index + 1u, term, argument) &&
             (items[index + 1u] =
                  petta_libpl_from_term_mode(
                      arena, argument, variables,
                      unknown, visible_compounds,
                      depth + 1u));
    }
    Atom *body = ok
        ? atom_expr(
              arena, items,
              (CettaExprLen)(arity + 1u))
        : NULL;
    Atom *result = NULL;
    if (body) {
        if (visible_compounds) {
            Atom *wrapper =
                atom_symbol(arena, "prolog:compound");
            result = wrapper
                ? atom_expr2(arena, wrapper, body)
                : NULL;
        } else {
            result =
                atom_petta_prolog_compound(arena, body);
        }
    }
    free(items);
    return result;
}

static bool petta_libpl_solution_bindings(
    Arena *arena, PettaLibplVarMap *variables,
    Bindings *bindings) {
    if (!arena || !variables || !bindings)
        return false;
    bindings_init(bindings);
    PettaLibplBackVarMap unknown = {0};
    for (size_t index = 0u;
         index < variables->len; index++) {
        PettaLibplVar *variable =
            &variables->items[index];
        Atom *value = NULL;
        if (PL_is_variable(variable->term)) {
            for (size_t prior = 0u;
                 prior < index; prior++) {
                if (PL_is_variable(
                        variables->items[prior].term) &&
                    PL_compare(
                        variable->term,
                        variables->items[prior].term) == 0) {
                    value = atom_var_like(
                        arena,
                        variables->items[prior].prototype,
                        variables->items[prior].id);
                    break;
                }
            }
            if (!value)
                continue;
        } else {
            value = petta_libpl_from_term(
                arena, variable->term, variables,
                &unknown, 0u);
        }
        if (!value ||
            !bindings_add_var_acyclic(
                bindings, variable->prototype, value)) {
            free(unknown.items);
            bindings_free(bindings);
            return false;
        }
    }
    free(unknown.items);
    return true;
}

static bool petta_libpl_emit_solution(
    Arena *arena, term_t result_term,
    Atom *constant_result,
    PettaLibplVarMap *variables,
    OutcomeSet *outcomes) {
    if (!arena || !variables || !outcomes ||
        (!result_term && !constant_result)) {
        return false;
    }
    Bindings bindings;
    if (!petta_libpl_solution_bindings(
            arena, variables, &bindings)) {
        return false;
    }
    PettaLibplBackVarMap unknown = {0};
    Atom *result = constant_result
        ? constant_result
        : petta_libpl_from_term(
              arena, result_term, variables,
              &unknown, 0u);
    free(unknown.items);
    if (!result) {
        bindings_free(&bindings);
        return false;
    }
    outcome_set_add(outcomes, result, &bindings);
    bindings_free(&bindings);
    return true;
}

static bool petta_libpl_run_query(
    CettaLibPrologRuntime *runtime, Arena *arena,
    predicate_t predicate, term_t arguments,
    term_t result_term, Atom *constant_result,
    PettaLibplVarMap *variables,
    OutcomeSet *outcomes, bool first_only,
    bool *succeeded) {
    if (succeeded)
        *succeeded = false;
    if (!runtime || !arena || !predicate ||
        !arguments || !variables || !outcomes ||
        !succeeded) {
        return false;
    }
    qid_t query = PL_open_query(
        runtime->module,
        PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
            PL_Q_EXT_STATUS,
        predicate, arguments);
    if (!query)
        return false;

    Arena *previous_active_arena =
        g_petta_libpl_active_arena;
    g_petta_libpl_active_arena = arena;
    bool ok = true;
    for (;;) {
        int status = PL_next_solution(query);
        if (status == PL_S_FALSE)
            break;
        if (status == PL_S_EXCEPTION ||
            status == PL_S_YIELD ||
            status == PL_S_YIELD_DEBUG ||
            (status != PL_S_TRUE &&
             status != PL_S_LAST)) {
            ok = false;
            break;
        }
        *succeeded = true;
        if (!petta_libpl_emit_solution(
                arena, result_term, constant_result,
                variables, outcomes)) {
            ok = false;
            break;
        }
        if (first_only || status == PL_S_LAST)
            break;
    }
    if (first_only && *succeeded)
        (void)PL_cut_query(query);
    else
        (void)PL_close_query(query);
    g_petta_libpl_active_arena =
        previous_active_arena;
    return ok;
}

static bool petta_libpl_registered_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    PettaLibplImport *entry,
    OutcomeSet *outcomes) {
    if (!runtime || !arena || !expression ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u || !expected ||
        !entry || !outcomes ||
        expression->expr.len > (CettaExprLen)INT_MAX) {
        return false;
    }

    size_t predicate_arity =
        (size_t)expression->expr.len;
    term_t arguments =
        PL_new_term_refs(predicate_arity);
    if (!arguments)
        return false;
    PettaLibplVarMap variables = {0};
    bool converted = true;
    for (size_t index = 0u;
         converted && index + 1u < predicate_arity;
         index++) {
        converted = petta_libpl_to_term(
            expression->expr.elems[index + 1u],
            arguments + index, &variables, 0u);
    }
    converted =
        converted &&
        petta_libpl_to_term(
            expected,
            arguments + predicate_arity - 1u,
            &variables, 0u);
    if (!converted) {
        free(variables.items);
        return false;
    }

    term_t callable = PL_new_term_ref();
    term_t result = PL_new_term_ref();
    atom_t name = PL_new_atom_nchars(
        entry->name_len, entry->name);
    functor_t functor =
        PL_new_functor_sz(name, predicate_arity);
    bool built =
        callable && result && functor &&
        PL_cons_functor_v(
            callable, functor, arguments) &&
        PL_get_arg(
            (int)predicate_arity, callable, result);
    PL_unregister_atom(name);
    if (!built) {
        free(variables.items);
        return false;
    }

    /*
     * Invoke through call/1 in the runtime's context module.  This is the
     * public FLI path that preserves SWI's module import and autoload
     * semantics; constructing a predicate handle for the fresh module can
     * instead select an undefined local predicate with the same indicator.
     */
    predicate_t call = PL_predicate(
        "call", 1, runtime->module_name);
    bool succeeded = false;
    bool ok = petta_libpl_run_query(
        runtime, arena, call, callable,
        result, NULL,
        &variables, outcomes, false, &succeeded);
    free(variables.items);
    if (ok && succeeded &&
        (strcmp(entry->name, "consult") == 0 ||
         strcmp(entry->name, "use_module") == 0 ||
         strcmp(entry->name, "ensure_loaded") == 0 ||
         strcmp(entry->name, "load_files") == 0)) {
        runtime->revision++;
    }
    return ok;
}

static bool petta_libpl_call_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *wrapper, OutcomeSet *outcomes) {
    Atom *body = NULL;
    if (!runtime || !arena || !outcomes ||
        !petta_libpl_predicate_body(wrapper, &body)) {
        return false;
    }
    term_t goal = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    bool converted =
        goal &&
        petta_libpl_to_callable(
            body, goal, &variables, 0u);
    if (!converted) {
        free(variables.items);
        return false;
    }
    predicate_t call = PL_predicate(
        "call", 1, runtime->module_name);
    Atom *success =
        petta_semantics_success_value(arena);
    bool succeeded = false;
    bool ok = success &&
              petta_libpl_run_query(
                  runtime, arena, call, goal, 0,
                  success, &variables, outcomes,
                  false, &succeeded);
    free(variables.items);
    return ok;
}

/*
 * Asserted PeTTa clauses may contain Predicate-wrapped calls to the same
 * deterministic grounded operations used by the native evaluator.  Register
 * those exact function-convention arities in the private SWI module and
 * dispatch them back through grounded_dispatch.  This keeps one arithmetic
 * and comparison implementation instead of maintaining a second Prolog
 * prelude inside the optional adapter.
 */
static foreign_t petta_libpl_grounded_bridge(
    term_t arguments, int supplied_arity,
    control_t control) {
    if (!g_petta_libpl_active_arena ||
        !g_symbols || supplied_arity < 1) {
        return false;
    }
    predicate_t predicate =
        PL_foreign_context_predicate(control);
    atom_t predicate_name = 0;
    size_t predicate_arity = 0u;
    if (!predicate ||
        !PL_predicate_info(
            predicate, &predicate_name,
            &predicate_arity, NULL) ||
        predicate_arity != (size_t)supplied_arity ||
        predicate_arity - 1u > UINT32_MAX) {
        return false;
    }
    size_t name_len = 0u;
    const char *name =
        PL_atom_nchars(predicate_name, &name_len);
    if (!name || name_len > UINT32_MAX)
        return false;
    SymbolId head_id = symbol_intern_bytes(
        g_symbols, (const uint8_t *)name,
        (uint32_t)name_len);
    CettaExprLen function_arity = 0u;
    if (head_id == SYMBOL_ID_NONE ||
        !grounded_op_is_type_pure(head_id) ||
        !petta_semantics_intrinsic_partial_arity(
            head_id, &function_arity) ||
        function_arity !=
            (CettaExprLen)(predicate_arity - 1u)) {
        return false;
    }

    uint32_t input_count =
        (uint32_t)(predicate_arity - 1u);
    Atom **inputs = input_count
        ? arena_alloc(
              g_petta_libpl_active_arena,
              sizeof(*inputs) * (size_t)input_count)
        : NULL;
    if (input_count && !inputs)
        return false;

    PettaLibplVarMap variables = {0};
    PettaLibplBackVarMap unknown = {0};
    bool converted = true;
    for (uint32_t index = 0u;
         converted && index < input_count; index++) {
        term_t input = arguments + index;
        converted =
            PL_is_ground(input) &&
            (inputs[index] = petta_libpl_from_term(
                 g_petta_libpl_active_arena,
                 input, &variables, &unknown, 0u));
    }
    free(variables.items);
    free(unknown.items);
    if (!converted)
        return false;

    Atom *head = atom_symbol_id(
        g_petta_libpl_active_arena, head_id);
    Atom *result = head
        ? grounded_dispatch(
              g_petta_libpl_active_arena,
              head, inputs, input_count)
        : NULL;
    if (!result)
        return false;

    term_t result_term = PL_new_term_ref();
    PettaLibplVarMap result_variables = {0};
    bool encoded =
        result_term &&
        petta_libpl_to_term(
            result, result_term,
            &result_variables, 0u);
    free(result_variables.items);
    return encoded &&
           PL_unify(
               arguments + predicate_arity - 1u,
               result_term);
}

static bool petta_libpl_predicate_exists(
    CettaLibPrologRuntime *runtime,
    const char *name, size_t name_len, size_t arity,
    bool *exists) {
    if (exists)
        *exists = false;
    if (!runtime || !runtime->module || !name ||
        !exists || arity > INT64_MAX) {
        return false;
    }
    term_t indicator = PL_new_term_ref();
    term_t indicator_args = PL_new_term_refs(2u);
    atom_t slash = PL_new_atom("/");
    functor_t indicator_functor =
        PL_new_functor_sz(slash, 2u);
    bool built =
        indicator && indicator_args &&
        indicator_functor &&
        PL_put_atom_nchars(
            indicator_args, name_len, name) &&
        PL_put_int64(
            indicator_args + 1u, (int64_t)arity) &&
        PL_cons_functor_v(
            indicator, indicator_functor,
            indicator_args);
    PL_unregister_atom(slash);
    if (!built)
        return false;

    predicate_t current_predicate =
        PL_predicate("current_predicate", 1, NULL);
    qid_t query = current_predicate
        ? PL_open_query(
              runtime->module,
              PL_Q_NODEBUG | PL_Q_CATCH_EXCEPTION |
                  PL_Q_EXT_STATUS,
              current_predicate, indicator)
        : 0;
    if (!query)
        return false;
    int status = PL_next_solution(query);
    *exists =
        status == PL_S_TRUE ||
        status == PL_S_LAST;
    bool ok =
        *exists || status == PL_S_FALSE;
    (void)PL_close_query(query);
    return ok;
}

static bool petta_libpl_register_grounded_bridge(
    CettaLibPrologRuntime *runtime, SymbolId head,
    size_t predicate_arity) {
    if (!runtime || !runtime->module_name ||
        !g_symbols || head == SYMBOL_ID_NONE ||
        predicate_arity > INT_MAX) {
        return false;
    }
    const char *name = symbol_bytes(g_symbols, head);
    size_t name_len = symbol_len(g_symbols, head);
    bool exists = false;
    if (!name ||
        !petta_libpl_predicate_exists(
            runtime, name, name_len,
            predicate_arity, &exists)) {
        return false;
    }
    if (exists)
        return true;
    return PL_register_foreign_in_module(
        runtime->module_name, name,
        (int)predicate_arity,
        (pl_function_t)petta_libpl_grounded_bridge,
        PL_FA_VARARGS);
}

static bool petta_libpl_register_clause_grounded_calls(
    CettaLibPrologRuntime *runtime, Atom *atom,
    uint32_t depth) {
    if (!runtime || !atom || depth > 1024u)
        return false;
    Atom *body = NULL;
    if (petta_libpl_predicate_body(atom, &body)) {
        if (body->kind == ATOM_EXPR &&
            body->expr.len >= 2u &&
            body->expr.elems[0]->kind == ATOM_SYMBOL) {
            SymbolId head =
                body->expr.elems[0]->sym_id;
            CettaExprLen function_arity = 0u;
            size_t predicate_arity =
                (size_t)(body->expr.len - 1u);
            if (grounded_op_is_type_pure(head) &&
                petta_semantics_intrinsic_partial_arity(
                    head, &function_arity) &&
                function_arity + 1u ==
                    (CettaExprLen)predicate_arity &&
                !petta_libpl_register_grounded_bridge(
                    runtime, head, predicate_arity)) {
                return false;
            }
        }
        return petta_libpl_register_clause_grounded_calls(
            runtime, body, depth + 1u);
    }
    if (atom->kind != ATOM_EXPR)
        return true;
    for (CettaExprIndex index = 0u;
         index < atom->expr.len; index++) {
        if (!petta_libpl_register_clause_grounded_calls(
                runtime, atom->expr.elems[index],
                depth + 1u)) {
            return false;
        }
    }
    return true;
}

static bool petta_libpl_assert_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *wrapper, OutcomeSet *outcomes,
    bool at_start) {
    Atom *body = NULL;
    if (!runtime || !arena || !outcomes ||
        !petta_libpl_predicate_body(wrapper, &body)) {
        return false;
    }
    term_t clause = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    bool converted =
        clause &&
        petta_libpl_to_callable(
            body, clause, &variables, 0u);
    converted =
        converted &&
        petta_libpl_register_clause_grounded_calls(
            runtime, body, 0u);
    if (!converted) {
        free(variables.items);
        return false;
    }
    bool asserted = PL_assert(
        clause, runtime->module,
        at_start ? PL_ASSERTA : PL_ASSERTZ);
    if (asserted) {
        Atom *success =
            petta_semantics_success_value(arena);
        asserted = success &&
                   petta_libpl_emit_solution(
                       arena, 0, success, &variables,
                       outcomes);
        runtime->revision++;
    }
    free(variables.items);
    return asserted;
}

static bool petta_libpl_retract_predicate(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *wrapper, OutcomeSet *outcomes) {
    Atom *body = NULL;
    if (!runtime || !arena || !outcomes ||
        !petta_libpl_predicate_body(wrapper, &body)) {
        return false;
    }
    term_t clause = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    bool converted =
        clause &&
        petta_libpl_to_callable(
            body, clause, &variables, 0u);
    if (!converted) {
        free(variables.items);
        return false;
    }
    predicate_t retract_predicate = PL_predicate(
        "retract", 1, runtime->module_name);
    Atom *success =
        petta_semantics_success_value(arena);
    bool succeeded = false;
    bool ok = success &&
              petta_libpl_run_query(
                  runtime, arena, retract_predicate,
                  clause, 0, success, &variables,
                  outcomes, true, &succeeded);
    if (ok && succeeded) {
        runtime->revision++;
    } else if (ok) {
        Atom *failure =
            petta_semantics_boolean_value(arena, false);
        ok = failure &&
             petta_libpl_emit_solution(
                 arena, 0, failure, &variables,
                 outcomes);
    }
    free(variables.items);
    return ok;
}

CettaLibPrologRuntime *cetta_lib_prolog_runtime_new(void) {
    CettaLibPrologRuntime *runtime =
        cetta_malloc(sizeof(*runtime));
    memset(runtime, 0, sizeof(*runtime));
    for (size_t index = 0u; index < 1024u; index++)
        atomic_init(&runtime->import_admission[index], 0u);
    atomic_init(&runtime->import_admission_saturated, false);
    return runtime;
}

void cetta_lib_prolog_runtime_free(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return;
    for (size_t index = 0u;
         index < runtime->import_len; index++) {
        petta_libpl_import_clear(
            &runtime->imports[index]);
    }
    free(runtime->imports);
    free(runtime->module_name);
    free(runtime);
}

bool cetta_lib_prolog_runtime_available(
    CettaLibPrologRuntime *runtime) {
    if (!runtime)
        return false;
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    bool available = petta_libpl_prepare_locked(runtime);
    petta_libpl_leave(claimed);
    return available;
}

CettaLibPrologQueryStatus cetta_lib_prolog_query(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *goal, Atom *projection, Atom **answers) {
    if (answers)
        *answers = NULL;
    if (!runtime || !arena || !goal || !projection ||
        !answers) {
        return runtime
            ? CETTA_LIB_PROLOG_QUERY_FAILED
            : CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
    }

    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return CETTA_LIB_PROLOG_QUERY_UNAVAILABLE;
    }

    fid_t frame = PL_open_foreign_frame();
    if (!frame) {
        petta_libpl_leave(claimed);
        return CETTA_LIB_PROLOG_QUERY_FAILED;
    }

    term_t goal_term = PL_new_term_ref();
    term_t projection_term = PL_new_term_ref();
    PettaLibplVarMap variables = {0};
    Atom *projection_body = projection;
    Atom *unquoted = NULL;
    if (petta_libpl_quote_body(
            projection, &unquoted)) {
        projection_body = unquoted;
    }
    bool converted =
        goal_term && projection_term &&
        petta_libpl_to_callable(
            goal, goal_term, &variables, 0u) &&
        petta_libpl_to_term(
            projection_body, projection_term,
            &variables, 0u);

    Atom **items = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    qid_t query = 0;
    bool ok = converted;
    if (ok) {
        predicate_t call = PL_predicate(
            "call", 1, runtime->module_name);
        query = call
            ? PL_open_query(
                  runtime->module,
                  PL_Q_NODEBUG |
                      PL_Q_CATCH_EXCEPTION |
                      PL_Q_EXT_STATUS,
                  call, goal_term)
            : 0;
        ok = query != 0;
    }

    Arena *previous_active_arena =
        g_petta_libpl_active_arena;
    g_petta_libpl_active_arena = arena;
    while (ok) {
        int status = PL_next_solution(query);
        if (status == PL_S_FALSE)
            break;
        if (status == PL_S_EXCEPTION ||
            status == PL_S_YIELD ||
            status == PL_S_YIELD_DEBUG ||
            (status != PL_S_TRUE &&
             status != PL_S_LAST)) {
            ok = false;
            break;
        }

        PettaLibplBackVarMap unknown = {0};
        Atom *item = petta_libpl_from_term_mode(
            arena, projection_term, &variables,
            &unknown, true, 0u);
        free(unknown.items);
        if (!item) {
            ok = false;
            break;
        }
        if (length == capacity) {
            size_t next =
                capacity ? capacity * 2u : 8u;
            if (next <= capacity ||
                next > SIZE_MAX / sizeof(*items)) {
                ok = false;
                break;
            }
            items = items
                ? cetta_realloc(
                      items, sizeof(*items) * next)
                : cetta_malloc(
                      sizeof(*items) * next);
            capacity = next;
        }
        items[length++] = item;
        if (status == PL_S_LAST)
            break;
    }
    g_petta_libpl_active_arena =
        previous_active_arena;

    if (query)
        (void)PL_close_query(query);
    if (ok) {
        *answers = atom_expr(
            arena, items, (CettaExprLen)length);
        ok = *answers != NULL;
    }
    free(items);
    free(variables.items);
    PL_discard_foreign_frame(frame);
    petta_libpl_leave(claimed);
    return ok
        ? CETTA_LIB_PROLOG_QUERY_OK
        : CETTA_LIB_PROLOG_QUERY_FAILED;
}

PeTTaNamedArity petta_libpl_named_arity(
    CettaLibPrologRuntime *runtime, SymbolId head,
    CettaExprLen supplied) {
    PeTTaNamedArity result = {0};
    if (!runtime ||
        supplied > (CettaExprLen)SIZE_MAX) {
        return result;
    }
    /*
     * Most evaluator heads are native MeTTa names.  The lock-free admission
     * set proves that an absent SymbolId cannot name an optional Prolog
     * import.  A saturated index conservatively falls through to the locked
     * registry.  Built-in adapter names remain visible without initializing
     * or claiming a Prolog engine merely to classify them.
     */
    if (!petta_libpl_import_admission_maybe_contains(
            runtime, head)) {
        cetta_runtime_stats_inc(
            CETTA_RUNTIME_COUNTER_PETTA_LIBPL_ADMISSION_NEGATIVE);
        size_t standard_arities[2] = {0u, 0u};
        size_t standard_arity_count =
            petta_libpl_standard_function_arities(
                head, standard_arities);
        for (size_t index = 0u;
             index < standard_arity_count; index++) {
            result.known = true;
            result.exact =
                result.exact ||
                standard_arities[index] ==
                    (size_t)supplied;
            result.larger =
                result.larger ||
                standard_arities[index] >
                    (size_t)supplied;
        }
        return result;
    }
    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return result;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return result;
    }
    PettaLibplImport *entry =
        petta_libpl_find_import(runtime, head);
    size_t standard_arities[2] = {0u, 0u};
    size_t standard_arity_count =
        !entry
            ? petta_libpl_standard_function_arities(
                  head, standard_arities)
            : 0u;
    if (standard_arity_count > 0u) {
        result.known = true;
        for (size_t index = 0u;
             index < standard_arity_count; index++) {
            result.exact =
                result.exact ||
                standard_arities[index] ==
                    (size_t)supplied;
            result.larger =
                result.larger ||
                standard_arities[index] >
                    (size_t)supplied;
        }
    }
    if (entry &&
        petta_libpl_refresh_arities(runtime, entry)) {
        /*
         * SWI's current_predicate/1 intentionally does not enumerate an
         * unloaded autoload candidate.  An explicit PeTTa import nevertheless
         * makes the name callable: the first concrete application supplies
         * the arity, call/1 performs ordinary SWI autoloading, and a successful
         * boundary call records that arity below.
         */
        if (entry->arity_len == 0u) {
            result.known = true;
            result.exact = true;
        }
        for (size_t index = 0u;
             index < entry->arity_len; index++) {
            size_t arity =
                entry->function_arities[index];
            result.known = true;
            result.exact =
                result.exact ||
                arity == (size_t)supplied;
            result.larger =
                result.larger ||
                arity > (size_t)supplied;
        }
    }
    petta_libpl_leave(claimed);
    return result;
}

bool petta_libpl_call(
    CettaLibPrologRuntime *runtime, Arena *arena,
    Atom *expression, Atom *expected,
    const Bindings *environment, OutcomeSet *outcomes,
    bool *recognized) {
    (void)environment;
    if (recognized)
        *recognized = false;
    if (!runtime || !arena ||
        !expression || !expected || !environment ||
        !outcomes || !recognized ||
        expression->kind != ATOM_EXPR ||
        expression->expr.len == 0u ||
        expression->expr.elems[0]->kind != ATOM_SYMBOL) {
        return runtime && arena && expression && expected &&
               environment && outcomes && recognized;
    }

    bool claimed = false;
    if (!petta_libpl_enter(&claimed))
        return false;
    if (!petta_libpl_prepare_locked(runtime)) {
        petta_libpl_leave(claimed);
        return false;
    }
    fid_t frame = PL_open_foreign_frame();
    if (!frame) {
        petta_libpl_leave(claimed);
        return false;
    }

    SymbolId head =
        expression->expr.elems[0]->sym_id;
    PeTTaForm form = petta_semantics_form(head);
    bool ok = true;
    if (form == PETTA_FORM_IMPORT_PROLOG_FUNCTION &&
        expression->expr.len == 2u) {
        *recognized = true;
        Atom *name = expression->expr.elems[1];
        bool native_form =
            name && name->kind == ATOM_SYMBOL &&
            petta_semantics_form(name->sym_id) !=
                PETTA_FORM_NONE;
        PettaLibplImport *entry =
            name && name->kind == ATOM_SYMBOL &&
            !native_form
                ? petta_libpl_register_import(
                      runtime, name->sym_id)
                : NULL;
        if (native_form || entry) {
            Bindings empty;
            bindings_init(&empty);
            Atom *success =
                petta_semantics_success_value(arena);
            if (success)
                outcome_set_add(outcomes, success, &empty);
            else
                ok = false;
        }
    } else if (form == PETTA_FORM_CALL_PREDICATE &&
               expression->expr.len == 2u) {
        *recognized = true;
        ok = petta_libpl_call_predicate(
            runtime, arena,
            expression->expr.elems[1], outcomes);
    } else if (
        (form == PETTA_FORM_ASSERTA_PREDICATE ||
         form == PETTA_FORM_ASSERTZ_PREDICATE) &&
        expression->expr.len == 2u) {
        *recognized = true;
        ok = petta_libpl_assert_predicate(
            runtime, arena,
            expression->expr.elems[1], outcomes,
            form == PETTA_FORM_ASSERTA_PREDICATE);
    } else if (
        form == PETTA_FORM_RETRACT_PREDICATE &&
        expression->expr.len == 2u) {
        *recognized = true;
        ok = petta_libpl_retract_predicate(
            runtime, arena,
            expression->expr.elems[1], outcomes);
    } else {
        PettaLibplImport *entry =
            petta_libpl_find_import(runtime, head);
        size_t standard_arities[2] = {0u, 0u};
        size_t standard_arity_count =
            !entry
                ? petta_libpl_standard_function_arities(
                      head, standard_arities)
                : 0u;
        if (!entry && standard_arity_count > 0u) {
            size_t supplied =
                (size_t)(expression->expr.len - 1u);
            for (size_t index = 0u;
                 index < standard_arity_count; index++) {
                if (standard_arities[index] != supplied)
                    continue;
                entry = petta_libpl_register_import(
                    runtime, head);
                if (entry &&
                    !petta_libpl_import_add_arity(
                        entry, supplied)) {
                    ok = false;
                }
                break;
            }
        }
        if (entry &&
            ok &&
            petta_libpl_refresh_arities(
                runtime, entry)) {
            size_t function_arity =
                (size_t)(expression->expr.len - 1u);
            bool arity_matched = false;
            for (size_t index = 0u;
                 index < entry->arity_len; index++) {
                if (entry->function_arities[index] ==
                    function_arity) {
                    arity_matched = true;
                    *recognized = true;
                    ok = petta_libpl_registered_call(
                        runtime, arena, expression,
                        expected, entry, outcomes);
                    break;
                }
            }
            if (!arity_matched &&
                entry->arity_len == 0u) {
                *recognized = true;
                ok = petta_libpl_registered_call(
                    runtime, arena, expression,
                    expected, entry, outcomes);
                if (ok) {
                    ok = petta_libpl_import_add_arity(
                        entry, function_arity);
                }
            }
        }
    }

    PL_discard_foreign_frame(frame);
    petta_libpl_leave(claimed);
    return ok;
}
