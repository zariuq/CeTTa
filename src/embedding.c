#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "cetta_embed.h"
#include "embedding_python.h"
#include "foreign.h"
#include "runtime_names.h"
#include "eval.h"
#include "eval_invocation.h"
#include "eval_query.h"
#include "foreign_provider.h"
#include "library.h"
#include "parser.h"
#include "petta_program.h"
#include "cetta_stdlib.h"
#include "runtime_prelude.h"
#include "runtime_session.h"
#include "he_type_policy.h"
#include "petta_compiled_reader.h"
#include "he_compiled_reader.h"
#include "stats.h"
#include "tensor.h"
#include "text_source.h"
#include "host_services.h"

#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dlfcn.h>
#include <sys/stat.h>

typedef struct EmbeddingCleanup EmbeddingCleanup;
struct EmbeddingCleanup {
    EmbeddingCleanup *next;
    void (*run)(EmbeddingCleanup *);
};
static _Thread_local cetta_engine_t *g_embedding_deferred_cleanup;

struct cetta_engine {
    atomic_uint references;
    atomic_bool closed;
    atomic_uint active_callbacks;
    _Atomic(EmbeddingCleanup *) cleanup;
    pthread_mutex_t mutex;
    unsigned active_entries;
    CettaNameDomain names;
    CettaRuntimeSession session;
    Arena persistent;
    CettaLanguageId language;
    const CettaProfile *profile;
    CettaHeTypeTables *he_types;
    CettaRuntimeStatsOwner *stats;
    PeTTaCompiledReaderV1 *petta_reader;
    HECompiledReaderV1 *he_reader;
    cetta_host_hooks_t host;
    CettaNativeMemory *memory;
    cetta_diagnostic_t diagnostic;
    char import_error[512];
};

typedef struct {
    EmbeddingCleanup cleanup;
    atomic_uint references;
    cetta_engine_t *engine;
    Arena arena;
} EmbeddingOwner;

struct cetta_atom {
    atomic_uint references;
    EmbeddingOwner *owner;
    Atom *value;
};

struct cetta_result {
    EmbeddingOwner *owner;
    Atom **values;
    size_t count;
    size_t *groups;
    size_t group_count;
    cetta_status_t status;
    int system_error;
    int exit_code;
    cetta_diagnostic_t diagnostic;
};

struct cetta_query {
    EmbeddingCleanup close_cleanup, destroy_cleanup;
    atomic_bool close_scheduled;
    atomic_uint references;
    atomic_bool cancelled;
    atomic_bool closed;
    EmbeddingOwner *owner;
    CettaEvalQuery *core;
    Atom *matched_value_pattern;
    bool stepping;
};

struct cetta_space {
    atomic_uint references;
    cetta_atom_t *value;
};

typedef struct {
    cetta_engine_t *engine;
    CettaNameContext names;
    HashConsTable *hashcons;
    CettaLibraryContext *libraries;
    CettaHeTypeTables *he_types;
    CettaEvalResources *resources;
    CettaEvalGc *collector;
    CettaRuntimeStatsOwner *stats;
    bool rationals, universal, lists, namespaces, petta_scalars;
    CettaSurvivorAllocationScope allocation_role;
    ParserDocumentBackendScope reader;
    CettaEvalCancellationScope cancellation;
    cetta_engine_t *parent_cleanup;
    bool host_entry;
    CettaHostScope host;
    CettaNativeMemoryScope memory;
    CettaImportFailureScope imports;
} EmbeddingScope;
static void engine_close_native_work(cetta_engine_t *engine);

static void import_failure(void *context, const char *specification, Atom *reason) {
    cetta_engine_t *engine = context;
    const char *message = reason && reason->kind == ATOM_SYMBOL ? atom_name_cstr(reason) : "module loading failed";
    snprintf(engine->import_error, sizeof(engine->import_error), "%s: %s",
        specification ? specification : "module", message);
}

static void diagnostic_source(cetta_diagnostic_t *diagnostic, const char *text, size_t length, const char *origin) {
    if (origin) snprintf(diagnostic->source, sizeof(diagnostic->source), "%s", origin);
    diagnostic->line = 1u; diagnostic->column = 1u;
    size_t position = 0u;
    unsigned depth = 0u;
    bool string = false, escaped = false, comment = false;
    for (; position < length; position++) {
        char ch = text[position];
        if (comment) { if (ch == '\n') comment = false; }
        else if (string) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') string = false;
        } else if (ch == ';') comment = true;
        else if (ch == '"') string = true;
        else if (ch == '(') depth++;
        else if (ch == ')') { if (!depth) break; depth--; }
        if (ch == '\n') { diagnostic->line++; diagnostic->column = 1u; }
        else diagnostic->column++;
    }
    if (!diagnostic->message[0])
        snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
            string ? "unterminated string" : depth ? "unclosed expression" : "invalid source syntax");
}

static bool host_write(void *context, unsigned channel, const char *bytes, size_t length) {
    cetta_engine_t *engine = context;
    atomic_fetch_add_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    cetta_status_t status = engine->host.write(engine->host.context, channel, bytes, length);
    atomic_fetch_sub_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    if (status != CETTA_OK)
        cetta_host_fail(status == CETTA_OUT_OF_MEMORY ? CETTA_HOST_MEMORY : CETTA_HOST_IO);
    return status == CETTA_OK;
}
static bool host_flush(void *context, unsigned channel) {
    cetta_engine_t *engine = context;
    atomic_fetch_add_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    cetta_status_t status = engine->host.flush(engine->host.context, channel);
    atomic_fetch_sub_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    if (status != CETTA_OK)
        cetta_host_fail(status == CETTA_OUT_OF_MEMORY ? CETTA_HOST_MEMORY : CETTA_HOST_IO);
    return status == CETTA_OK;
}
static bool host_interrupt(void *context) {
    cetta_engine_t *engine = context;
    atomic_fetch_add_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    bool interrupted = engine->host.interrupt(engine->host.context) != 0;
    atomic_fetch_sub_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    return interrupted;
}
static void host_foreign(void *context, bool entering) {
    cetta_engine_t *engine = context;
    if (entering) atomic_fetch_add_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
    else atomic_fetch_sub_explicit(&engine->active_callbacks, 1u, memory_order_acq_rel);
}

/* Callback entry may begin while another thread is waiting for the engine.
 * A bounded mutex wait lets that waiter observe the callback and decline. */
static cetta_status_t engine_lock(cetta_engine_t *engine) {
    int error = pthread_mutex_trylock(&engine->mutex);
    while (error == EBUSY || error == ETIMEDOUT) {
        if (atomic_load_explicit(&engine->active_callbacks, memory_order_acquire))
            return CETTA_BUSY;
        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline)) return CETTA_ENGINE_ERROR;
        deadline.tv_nsec += 1000000L;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_nsec -= 1000000000L;
            deadline.tv_sec++;
        }
        error = pthread_mutex_timedlock(&engine->mutex, &deadline);
    }
    return error ? CETTA_ENGINE_ERROR : CETTA_OK;
}

static void engine_drain_cleanup(cetta_engine_t *engine) {
    cetta_engine_t *previous = g_embedding_deferred_cleanup;
    g_embedding_deferred_cleanup = engine;
    EmbeddingCleanup *list;
    while ((list = atomic_exchange_explicit(&engine->cleanup, NULL, memory_order_acquire))) {
        while (list) {
            EmbeddingCleanup *next = list->next;
            list->run(list);
            list = next;
        }
    }
    g_embedding_deferred_cleanup = previous;
}

/* Recheck after unlocking: a producer that lost trylock just before unlock
 * must hand its cleanup to this thread or acquire the unlocked engine itself. */
static void engine_unlock(cetta_engine_t *engine) {
    do {
        engine_drain_cleanup(engine);
        pthread_mutex_unlock(&engine->mutex);
        if (!atomic_load_explicit(&engine->cleanup, memory_order_acquire)) return;
    } while (!pthread_mutex_trylock(&engine->mutex));
}

static void engine_cleanup(cetta_engine_t *engine, EmbeddingCleanup *cleanup) {
    cetta_embed_engine_retain(engine);
    if (!pthread_mutex_trylock(&engine->mutex)) {
        cetta_engine_t *previous = g_embedding_deferred_cleanup;
        g_embedding_deferred_cleanup = NULL;
        cleanup->run(cleanup);
        engine_unlock(engine);
        g_embedding_deferred_cleanup = previous;
    } else {
        EmbeddingCleanup *head = atomic_load_explicit(&engine->cleanup, memory_order_relaxed);
        do { cleanup->next = head; }
        while (!atomic_compare_exchange_weak_explicit(&engine->cleanup, &head, cleanup,
            memory_order_release, memory_order_relaxed));
        if (!pthread_mutex_trylock(&engine->mutex)) engine_unlock(engine);
    }
    cetta_embed_engine_release(engine);
}

static int reader_text(void *context, const char *text, TermUniverse *universe,
                       AtomId **ids, char *error, size_t capacity) {
    cetta_engine_t *engine = context;
    return engine->petta_reader ? petta_compiled_reader_v1_parse_text_ids(
        engine->petta_reader, text, universe, ids, NULL, error, capacity)
        : he_compiled_reader_v1_parse_text_ids(engine->he_reader, text, universe, ids, NULL, error, capacity);
}
static int reader_file(void *context, const char *path, TermUniverse *universe,
                       AtomId **ids, char *error, size_t capacity) {
    cetta_engine_t *engine = context;
    return engine->petta_reader ? petta_compiled_reader_v1_parse_file_ids(
        engine->petta_reader, path, universe, ids, NULL, error, capacity)
        : he_compiled_reader_v1_parse_file_ids(engine->he_reader, path, universe, ids, NULL, error, capacity);
}
static int reader_term(void *context, const uint8_t *bytes, size_t length,
                       TermUniverse *universe, AtomId *id, char *error, size_t capacity) {
    cetta_engine_t *engine = context;
    return petta_compiled_reader_v1_parse_term_bytes_id(engine->petta_reader,
        bytes, length, universe, id, error, capacity);
}

static int reader_bytes_prefix(void *context, const uint8_t *bytes, size_t length,
        TermUniverse *universe, AtomId **ids, int *complete,
        char *error, size_t capacity) {
    cetta_engine_t *engine = context;
    if (!engine->petta_reader)
        return he_compiled_reader_v1_parse_bytes_prefix_ids(engine->he_reader,
            bytes, length, universe, ids, complete, error, capacity);
    return parse_metta_bytes_prefix_ids(bytes, length, universe,
        ids, complete, error, capacity);
}

static int reader_file_prefix(void *context, const char *path,
        TermUniverse *universe, AtomId **ids, int *complete,
        char *error, size_t capacity) {
    cetta_engine_t *engine = context;
    if (!engine->petta_reader)
        return he_compiled_reader_v1_parse_file_prefix_ids(engine->he_reader,
            path, universe, ids, complete, error, capacity);
    return parse_metta_file_prefix_ids(path, universe, ids, complete, error, capacity);
}

static void scope_enter(cetta_engine_t *engine, EmbeddingScope *scope) {
    scope->engine = engine;
    scope->parent_cleanup = g_embedding_deferred_cleanup;
    scope->host_entry = false;
    cetta_native_memory_enter(&scope->memory, engine->memory);
    engine->active_entries++;
    CettaHostServices host = {.context = engine,
        .write = engine->host.write ? host_write : NULL,
        .flush = engine->host.flush ? host_flush : NULL,
        .poll = engine->host.interrupt ? host_interrupt : NULL,
        .foreign = host_foreign};
    cetta_host_scope_enter(&scope->host, &host);
    scope->names = cetta_name_context_current();
    scope->hashcons = g_hashcons;
    CettaNameContext names = cetta_name_domain_context(&engine->names);
    cetta_name_context_install(&names);
    g_hashcons = NULL;
    scope->stats = cetta_runtime_stats_owner_exchange(engine->stats);
    scope->allocation_role = cetta_survivor_allocation_scope_enter(CETTA_SURVIVOR_ALLOC_ROLE_OTHER);
    scope->he_types = he_type_tables_exchange(engine->he_types);
    scope->resources = eval_resources_exchange(engine->session.resources);
    scope->collector = eval_collector_exchange(eval_resources_collector(engine->session.resources));
    eval_cancellation_enter(&scope->cancellation, &engine->closed);
    scope->libraries = eval_exchange_library_context(&engine->session.libraries);
    scope->imports = (CettaImportFailureScope){.context = engine, .observe = import_failure};
    cetta_library_import_observe_begin(&engine->session.libraries, &scope->imports);
    scope->rationals = parser_set_rational_literals_enabled(
        !cetta_language_uses_rust_he_compat_semantics(engine->language, engine->profile));
    scope->universal = parser_set_universal_name_syntax_enabled(false);
    scope->lists = parser_set_list_syntax_enabled(
        cetta_language_reads_lists(engine->language, engine->profile));
    scope->namespaces = parser_set_namespace_sugar_enabled(engine->language != CETTA_LANGUAGE_HE);
    scope->petta_scalars = parser_set_petta_scalar_policy(engine->language == CETTA_LANGUAGE_PETTA);
    ParserDocumentIdsBackend reader = {.context = engine, .parse_text_ids = reader_text,
        .parse_file_ids = reader_file, .parse_bytes_prefix_ids = reader_bytes_prefix,
        .parse_file_prefix_ids = reader_file_prefix,
        .parse_term_bytes_id = engine->petta_reader ? reader_term : NULL};
    parser_document_backend_enter(&scope->reader, &reader);
}

static void scope_leave(EmbeddingScope *scope) {
    cetta_engine_t *engine = scope->engine;
    engine->active_entries--;
    if (!engine->active_entries && atomic_load_explicit(&engine->closed, memory_order_acquire))
        engine_close_native_work(engine);
    eval_cancellation_leave(&scope->cancellation);
    parser_document_backend_leave(&scope->reader);
    parser_set_petta_scalar_policy(scope->petta_scalars);
    parser_set_namespace_sugar_enabled(scope->namespaces);
    parser_set_list_syntax_enabled(scope->lists);
    parser_set_universal_name_syntax_enabled(scope->universal);
    parser_set_rational_literals_enabled(scope->rationals);
    cetta_library_import_observe_end(&engine->session.libraries, &scope->imports);
    eval_exchange_library_context(scope->libraries);
    eval_resources_exchange(scope->resources);
    eval_collector_exchange(scope->collector);
    he_type_tables_exchange(scope->he_types);
    g_hashcons = scope->hashcons;
    cetta_name_context_install(&scope->names);
    cetta_survivor_allocation_scope_leave(scope->allocation_role);
    cetta_runtime_stats_owner_exchange(scope->stats);
    cetta_host_scope_leave(&scope->host);
    cetta_native_memory_leave(&scope->memory);
}

static cetta_status_t engine_enter(cetta_engine_t *engine, EmbeddingScope *scope) {
    if (!engine) return CETTA_INVALID_ARGUMENT;
    if (atomic_load_explicit(&engine->closed, memory_order_acquire)) return CETTA_ENGINE_CLOSED;
    cetta_embed_engine_retain(engine);
    cetta_status_t status = engine_lock(engine);
    if (status != CETTA_OK) {
        cetta_embed_engine_release(engine);
        return status;
    }
    if (atomic_load_explicit(&engine->closed, memory_order_acquire)) {
        engine_unlock(engine);
        cetta_embed_engine_release(engine);
        return CETTA_ENGINE_CLOSED;
    }
    if (!engine->active_entries) {
        cetta_eval_session_clear_process_exit(&engine->session.libraries.session);
        cetta_native_memory_clear_failure(engine->memory);
        engine->diagnostic = (cetta_diagnostic_t){.size = sizeof(cetta_diagnostic_t)};
        engine->import_error[0] = '\0';
    }
    scope_enter(engine, scope);
    scope->host_entry = true;
    g_embedding_deferred_cleanup = NULL;
    return CETTA_OK;
}

/* The engine lock protects this transition. Retained values keep their
 * storage; resumable/background native work is cancelled exactly once. */
static void engine_close_native_work(cetta_engine_t *engine) {
    cetta_runtime_session_stop(&engine->session);
    cetta_foreign_drain_releases();
}

static void engine_leave(cetta_engine_t *engine, EmbeddingScope *scope) {
    if (cetta_native_memory_abandoned(engine->memory))
        atomic_store_explicit(&engine->closed, true, memory_order_release);
    scope_leave(scope);
    engine_unlock(engine);
    if (scope->host_entry) g_embedding_deferred_cleanup = scope->parent_cleanup;
    cetta_embed_engine_release(engine);
}

uint32_t cetta_embed_abi_version(void) { return CETTA_EMBED_ABI_VERSION; }

cetta_status_t cetta_embed_engine_set_host_hooks(cetta_engine_t *engine, const cetta_host_hooks_t *hooks) {
    if (hooks && (hooks->size != sizeof(*hooks) || !!hooks->retain != !!hooks->release))
        return CETTA_INVALID_ARGUMENT;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    if (engine->active_entries > 1u) status = CETTA_BUSY;
    else {
        cetta_host_hooks_t previous = engine->host;
        if (hooks && hooks->retain) hooks->retain(hooks->context);
        engine->host = hooks ? *hooks : (cetta_host_hooks_t){0};
        if (previous.release) previous.release(previous.context);
    }
    engine_leave(engine, &scope);
    return status;
}

static bool installed_resource_root(char *root, size_t capacity) {
    Dl_info info;
    char library[PATH_MAX];
    if (!dladdr((void *)&cetta_embed_abi_version, &info) || !info.dli_fname ||
        !realpath(info.dli_fname, library)) return false;
    char *slash = strrchr(library, '/');
    if (!slash) return false;
    *slash = '\0';
    const char *suffixes[] = {"../share/cetta", ".."};
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(*suffixes); ++i) {
        char candidate[PATH_MAX], probe[PATH_MAX];
        struct stat status;
        int written = snprintf(candidate, sizeof(candidate), "%s/%s", library, suffixes[i]);
        if (written < 0 || (size_t)written >= sizeof(candidate)) continue;
        written = snprintf(probe, sizeof(probe), "%s/lib", candidate);
        if (written < 0 || (size_t)written >= sizeof(probe) ||
            stat(probe, &status) || !S_ISDIR(status.st_mode)) continue;
        if (!realpath(candidate, probe) || strlen(probe) >= capacity) continue;
        strcpy(root, probe);
        return true;
    }
    return false;
}

static void engine_free_allocation(cetta_engine_t *engine) {
    CettaNativeMemory *memory = engine->memory;
    free(engine);
    cetta_native_memory_release(memory);
}

static cetta_status_t engine_new_native(const cetta_engine_config_t *config, cetta_engine_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (config && (config->size != sizeof(*config) ||
                   config->abi_version != CETTA_EMBED_ABI_VERSION)) return CETTA_INVALID_ARGUMENT;
    const CettaLanguageSpec *language = cetta_language_lookup(
        config && config->language ? config->language : "petta");
    /* Other dialects join this interface when their lifecycle is qualified. */
    if (!language || (language->id != CETTA_LANGUAGE_PETTA && language->id != CETTA_LANGUAGE_HE))
        return CETTA_INVALID_ARGUMENT;
    const CettaProfile *profile = config && config->profile
        ? cetta_profile_from_name_for_language(language->id, config->profile) : NULL;
    if (config && config->profile && !profile) return CETTA_INVALID_ARGUMENT;
    if (config && config->resource_root && strlen(config->resource_root) >= 4096)
        return CETTA_INVALID_ARGUMENT;
    cetta_engine_t *engine = calloc(1, sizeof(*engine));
    if (!engine) return CETTA_OUT_OF_MEMORY;
    engine->memory = cetta_native_memory_current();
    cetta_native_memory_retain(engine->memory);
    pthread_mutexattr_t attr;
    int error = pthread_mutexattr_init(&attr);
    if (!error) {
        error = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        if (!error) error = pthread_mutex_init(&engine->mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }
    if (error) { engine_free_allocation(engine); return CETTA_ENGINE_ERROR; }
    atomic_init(&engine->references, 1u);
    atomic_init(&engine->closed, false);
    atomic_init(&engine->active_callbacks, 0u);
    atomic_init(&engine->cleanup, NULL);
    engine->language = language->id;
    engine->profile = profile;
    engine->stats = cetta_runtime_stats_owner_new();
    if (!engine->stats) {
        cetta_status_t status = errno == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_ENGINE_ERROR;
        pthread_mutex_destroy(&engine->mutex);
        engine_free_allocation(engine);
        return status;
    }
    CettaRuntimeStatsOwner *previous_stats = cetta_runtime_stats_owner_exchange(engine->stats);
    bool named = cetta_name_domain_try_init(&engine->names);
    cetta_status_t early_status = errno == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_ENGINE_ERROR;
    if (!named) goto early_failure;
    engine->he_types = he_type_tables_new();
    early_status = CETTA_OUT_OF_MEMORY;
    if (!engine->he_types) goto early_failure;
    bool lists = cetta_language_reads_lists(language->id, profile);
    if (language->id == CETTA_LANGUAGE_PETTA)
        engine->petta_reader = lists ? petta_compiled_reader_v1_new_with_lists() : petta_compiled_reader_v1_new();
    else
        engine->he_reader = lists ? he_compiled_reader_v1_new_with_lists() : he_compiled_reader_v1_new();
    if (!engine->petta_reader && !engine->he_reader) goto early_failure;
    if (!arena_try_init_detached(&engine->persistent)) {
        early_status = errno == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_ENGINE_ERROR;
        goto early_failure;
    }
    /* Initialization also runs in this engine's name domain. */
    CettaNameContext previous = cetta_name_context_current();
    HashConsTable *previous_hashcons = g_hashcons;
    g_hashcons = NULL;
    CettaNameContext names = cetta_name_domain_context(&engine->names);
    cetta_name_context_install(&names);
    bool started = cetta_runtime_session_init(&engine->session, &engine->persistent, language->id, profile);
    /* Resource-path discovery may change errno after a refused constructor. */
    int startup_error = started ? 0 : errno;
    if (started) eval_resources_set_native_memory(engine->session.resources, engine->memory);
    if (started && config && config->resource_root)
        strcpy(engine->session.libraries.root_dir, config->resource_root);
    else if (started) {
        char root[PATH_MAX];
        if (installed_resource_root(root, sizeof(root)))
            strcpy(engine->session.libraries.root_dir, root);
    }
    bool rooted = started && cetta_runtime_session_open_root(&engine->session, &engine->persistent);
    if (started && !rooted) startup_error = errno;
    EmbeddingScope scope;
    scope_enter(engine, &scope);
    char error_buffer[512];
    if (rooted) errno = 0;
    bool loaded = rooted && (engine->petta_reader
        ? petta_compiled_reader_v1_prepare(engine->petta_reader, error_buffer, sizeof(error_buffer))
        : engine->he_reader && he_compiled_reader_v1_prepare(engine->he_reader, error_buffer, sizeof(error_buffer)))
        && cetta_runtime_load_prelude(&engine->session.space, &engine->persistent, language->id, profile);
    if (rooted && !loaded) startup_error = errno;
    scope_leave(&scope);
    cetta_name_context_install(&previous);
    g_hashcons = previous_hashcons;
    cetta_runtime_stats_owner_exchange(previous_stats);
    if (!loaded) {
        cetta_embed_engine_release(engine);
        return startup_error == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_ENGINE_ERROR;
    }
    *output = engine;
    return CETTA_OK;
early_failure:
    petta_compiled_reader_v1_free(engine->petta_reader);
    he_compiled_reader_v1_free(engine->he_reader);
    he_type_tables_free(engine->he_types);
    if (named) cetta_name_domain_free(&engine->names);
    cetta_runtime_stats_owner_exchange(previous_stats);
    cetta_runtime_stats_owner_free(engine->stats);
    pthread_mutex_destroy(&engine->mutex);
    engine_free_allocation(engine);
    return early_status;
}

cetta_status_t cetta_embed_engine_new_limited(const cetta_engine_config_t *config,
        uint64_t memory_limit, cetta_engine_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    CettaNativeMemory *memory = cetta_native_memory_new(memory_limit);
    if (!memory) return CETTA_OUT_OF_MEMORY;
    CettaNativeMemoryScope scope;
    cetta_native_memory_enter(&scope, memory);
    cetta_status_t status = engine_new_native(config, output);
    if (cetta_native_memory_failed()) {
        if (*output) { cetta_embed_engine_release(*output); *output = NULL; }
        status = CETTA_OUT_OF_MEMORY;
    }
    cetta_native_memory_leave(&scope);
    cetta_native_memory_release(memory);
    return status;
}
cetta_status_t cetta_embed_engine_new(const cetta_engine_config_t *config, cetta_engine_t **output) {
    return cetta_embed_engine_new_limited(config, UINT64_MAX, output);
}

static bool engine_retain_live(cetta_engine_t *engine) {
    unsigned references = atomic_load_explicit(&engine->references, memory_order_relaxed);
    while (references) {
        if (atomic_compare_exchange_weak_explicit(&engine->references, &references, references + 1u,
                memory_order_relaxed, memory_order_relaxed)) return true;
    }
    return false;
}
void cetta_embed_engine_retain(cetta_engine_t *engine) {
    if (engine) (void)engine_retain_live(engine);
}

void cetta_embed_engine_close(cetta_engine_t *engine) {
    if (!engine) return;
    if (!engine_retain_live(engine)) return;
    cetta_engine_t *previous_cleanup = g_embedding_deferred_cleanup;
    g_embedding_deferred_cleanup = NULL;
    atomic_store_explicit(&engine->closed, true, memory_order_release);
    if (!pthread_mutex_trylock(&engine->mutex)) {
        if (!engine->active_entries) {
            EmbeddingScope scope;
            scope_enter(engine, &scope);
            engine_close_native_work(engine);
            scope_leave(&scope);
        }
        engine_unlock(engine);
    }
    g_embedding_deferred_cleanup = previous_cleanup;
    cetta_embed_engine_release(engine);
}

void cetta_embed_engine_release(cetta_engine_t *engine) {
    if (!engine || atomic_fetch_sub_explicit(&engine->references, 1u, memory_order_acq_rel) != 1u) return;
    atomic_store_explicit(&engine->closed, true, memory_order_release);
    EmbeddingScope scope;
    scope_enter(engine, &scope);
    engine_close_native_work(engine);
    cetta_runtime_session_dispose(&engine->session);
    petta_compiled_reader_v1_free(engine->petta_reader);
    he_compiled_reader_v1_free(engine->he_reader);
    arena_free(&engine->persistent);
    he_type_tables_exchange(NULL);
    he_type_tables_free(engine->he_types);
    scope_leave(&scope);
    CettaRuntimeStatsOwner *previous_stats = cetta_runtime_stats_owner_exchange(engine->stats);
    cetta_name_domain_free(&engine->names);
    cetta_runtime_stats_owner_exchange(previous_stats);
    cetta_runtime_stats_owner_free(engine->stats);
    if (engine->host.release) engine->host.release(engine->host.context);
    pthread_mutex_destroy(&engine->mutex);
    engine_free_allocation(engine);
}

static void owner_destroy_cleanup(EmbeddingCleanup *cleanup) {
    EmbeddingOwner *owner = (EmbeddingOwner *)cleanup;
    cetta_engine_t *engine = owner->engine;
    /* Foreign holds and arena finalizers execute in the owning context. */
    EmbeddingScope scope;
    scope_enter(engine, &scope);
    arena_free(&owner->arena);
    scope_leave(&scope);
    free(owner);
    cetta_embed_engine_release(engine);
}

static EmbeddingOwner *owner_new(cetta_engine_t *engine) {
    EmbeddingOwner *owner = malloc(sizeof(*owner));
    if (!owner) return NULL;
    atomic_init(&owner->references, 1u);
    owner->cleanup = (EmbeddingCleanup){.run = owner_destroy_cleanup};
    cetta_embed_engine_retain(engine);
    owner->engine = engine;
    arena_init_detached(&owner->arena);
    return owner;
}

static void owner_release(EmbeddingOwner *owner) {
    if (!owner || atomic_fetch_sub_explicit(&owner->references, 1u, memory_order_acq_rel) != 1u) return;
    engine_cleanup(owner->engine, &owner->cleanup);
}

static cetta_status_t owner_enter(const EmbeddingOwner *owner, EmbeddingScope *scope) {
    cetta_engine_t *engine = owner->engine;
    cetta_embed_engine_retain(engine);
    cetta_status_t status = engine_lock(engine);
    if (status == CETTA_OK) {
        scope_enter(engine, scope);
        scope->host_entry = true;
        g_embedding_deferred_cleanup = NULL;
    }
    else cetta_embed_engine_release(engine);
    return status;
}

static cetta_status_t atom_wrap(EmbeddingOwner *owner, Atom *value, cetta_atom_t **output) {
    if (!value) return CETTA_OUT_OF_MEMORY;
    CettaNativeMemoryScope memory;
    cetta_native_memory_enter(&memory, owner->engine->memory);
    cetta_atom_t *atom = malloc(sizeof(*atom));
    cetta_native_memory_leave(&memory);
    if (!atom) return CETTA_OUT_OF_MEMORY;
    atomic_init(&atom->references, 1u);
    atomic_fetch_add_explicit(&owner->references, 1u, memory_order_relaxed);
    atom->owner = owner;
    atom->value = value;
    *output = atom;
    return CETTA_OK;
}

void cetta_embed_atom_retain(cetta_atom_t *atom) {
    if (atom) atomic_fetch_add_explicit(&atom->references, 1u, memory_order_relaxed);
}

cetta_status_t cetta_embedding_python_object(cetta_engine_t *engine, void *object,
        cetta_atom_t **output) {
    if (!object || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    cetta_foreign_drain_releases();
    EmbeddingOwner *owner = owner_new(engine);
    status = owner ? atom_wrap(owner, cetta_foreign_object_wrap(
        engine->session.libraries.foreign_runtime, &owner->arena, object), output) : CETTA_OUT_OF_MEMORY;
    if (owner) owner_release(owner);
    engine_leave(engine, &scope);
    return status;
}

void *cetta_embedding_python_borrow(const cetta_atom_t *atom) {
    return atom ? cetta_foreign_python_value(atom->value) : NULL;
}

void *cetta_embedding_python_exception(const cetta_atom_t *atom, int raised) {
    if (!atom || atom->value->kind != ATOM_EXPR || atom->value->expr.len != 3u) return NULL;
    const cetta_engine_t *engine = atom->owner->engine;
    if (!raised && engine->language != CETTA_LANGUAGE_HE) return NULL;
    const Atom *head = atom->value->expr.elems[0];
    /* Retained atoms keep their name domain alive. Inspect its immutable
     * Error symbol before acquiring an execution scope for an actual error. */
    if (!head || head->kind != ATOM_SYMBOL || atom_symbol_current_id(head) != engine->names.builtins.error) return NULL;
    EmbeddingScope scope;
    if (owner_enter(atom->owner, &scope) != CETTA_OK) return NULL;
    void *exception = raised ? cetta_foreign_python_exception(atom->value)
                             : cetta_foreign_python_he_exception(atom->value);
    engine_leave(atom->owner->engine, &scope);
    return exception;
}

void *cetta_embedding_python_attach(cetta_engine_t *engine) {
    return cetta_foreign_python_owner_new(engine->session.libraries.foreign_runtime);
}
void *cetta_embedding_python_atom_owner(const cetta_atom_t *atom) {
    return cetta_foreign_python_owner(atom->owner->engine->session.libraries.foreign_runtime);
}
void *cetta_embedding_python_space_owner(const cetta_space_t *space) {
    return cetta_embedding_python_atom_owner(space->value);
}
void *cetta_embedding_python_query_owner(const cetta_query_t *query) {
    return cetta_foreign_python_owner(query->owner->engine->session.libraries.foreign_runtime);
}

void cetta_embed_atom_release(cetta_atom_t *atom) {
    if (!atom || atomic_fetch_sub_explicit(&atom->references, 1u, memory_order_acq_rel) != 1u) return;
    owner_release(atom->owner);
    free(atom);
}

static char *text_copy(const char *text, size_t length, cetta_status_t *status) {
    *status = CETTA_INVALID_ARGUMENT;
    if (!text || length == SIZE_MAX || memchr(text, 0, length)) return NULL;
    char *copy = malloc(length + 1u);
    if (copy) { memcpy(copy, text, length); copy[length] = 0; }
    *status = copy ? CETTA_OK : CETTA_OUT_OF_MEMORY;
    return copy;
}

static int read_forms(cetta_engine_t *engine, const char *text, Arena *owner, Atom ***forms) {
    AtomId *ids = NULL;
    int count = parse_metta_text_ids_diagnostic(text, &engine->session.libraries.term_universe, &ids,
        engine->diagnostic.message, sizeof(engine->diagnostic.message));
    *forms = NULL;
    if (count <= 0) { free(ids); return count; }
    Atom **values = calloc((size_t)count, sizeof(*values));
    AtomDeepCopySession *copy = atom_deep_copy_session_new(owner);
    if (!values || !copy) { free(values); free(ids); atom_deep_copy_session_free(copy); return -1; }
    bool ok = true;
    for (int i = 0; i < count && ok; ++i) {
        Atom *atom = term_universe_get_atom(&engine->session.libraries.term_universe, ids[i]);
        if (atom && engine->language == CETTA_LANGUAGE_HE)
            atom = eval_read_registry_words(&engine->session.registry, owner, atom);
        values[i] = atom ? atom_deep_copy_session_copy(copy, atom) : NULL;
        ok = values[i] != NULL;
    }
    atom_deep_copy_session_free(copy);
    free(ids);
    if (!ok) { free(values); return -1; }
    *forms = values;
    return count;
}

cetta_status_t cetta_embed_parse_diagnostic(cetta_engine_t *engine, const char *text, size_t length,
        cetta_diagnostic_t *diagnostic, cetta_atom_t **output) {
    if (diagnostic && diagnostic->size != sizeof(*diagnostic)) return CETTA_INVALID_ARGUMENT;
    if (diagnostic) *diagnostic = (cetta_diagnostic_t){.size = sizeof(*diagnostic)};
    if (!output || !text) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    char *copy = text_copy(text, length, &status);
    if (!copy) { engine_leave(engine, &scope); return status; }
    if (status == CETTA_OK) {
        EmbeddingOwner *owner = owner_new(engine);
        Atom **forms = NULL;
        bool handled = false;
        Atom *value = owner ? parser_read_document_term_n(&owner->arena, copy, length, &handled) : NULL;
        int count = owner && !handled ? read_forms(engine, copy, &owner->arena, &forms) : 0;
        if (!handled && count == 1) value = forms[0];
        status = !owner ? CETTA_OUT_OF_MEMORY : !value ? CETTA_PARSE_ERROR
            : atom_wrap(owner, value, output);
        if (cetta_native_memory_failed()) status = CETTA_OUT_OF_MEMORY;
        if (status == CETTA_PARSE_ERROR)
            diagnostic_source(&engine->diagnostic, text, length, NULL);
        if (diagnostic) *diagnostic = engine->diagnostic;
        free(forms);
        owner_release(owner);
        engine_leave(engine, &scope);
    }
    free(copy);
    return status;
}

static cetta_status_t atom_scalar(cetta_engine_t *engine, const char *text, int64_t integer,
                                   unsigned kind, double real, cetta_atom_t **output) {
    if (!output || ((kind == 0u || kind == 1u || kind >= 5u) && !text)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    EmbeddingOwner *owner = owner_new(engine);
    status = !owner ? CETTA_OUT_OF_MEMORY : atom_wrap(owner,
        kind == 0u ? atom_symbol(&owner->arena, text) : kind == 1u
            ? atom_string(&owner->arena, text) : kind == 2u ? atom_int(&owner->arena, integer)
            : kind == 3u ? atom_float(&owner->arena, real)
            : kind == 4u ? atom_bool(&owner->arena, integer != 0)
            : kind == 5u ? atom_var_with_id(&owner->arena, text, fresh_var_id())
            : kind == 6u ? atom_bigint(&owner->arena, text)
            : atom_rational(&owner->arena, text), output);
    owner_release(owner);
    engine_leave(engine, &scope);
    return status;
}

cetta_status_t cetta_embed_atom_symbol(cetta_engine_t *e, const char *s, cetta_atom_t **o) { return atom_scalar(e,s,0,0,0,o); }
cetta_status_t cetta_embed_atom_string(cetta_engine_t *e, const char *s, cetta_atom_t **o) { return atom_scalar(e,s,0,1,0,o); }
cetta_status_t cetta_embed_parse(cetta_engine_t *engine, const char *text, size_t length, cetta_atom_t **output) {
    return cetta_embed_parse_diagnostic(engine, text, length, NULL, output);
}
cetta_status_t cetta_embed_atom_integer(cetta_engine_t *e, int64_t n, cetta_atom_t **o) { return atom_scalar(e,NULL,n,2,0,o); }
cetta_status_t cetta_embed_atom_float(cetta_engine_t *e, double n, cetta_atom_t **o) { return atom_scalar(e,NULL,0,3,n,o); }
cetta_status_t cetta_embed_atom_boolean(cetta_engine_t *e, int n, cetta_atom_t **o) { return atom_scalar(e,NULL,n,4,0,o); }
cetta_status_t cetta_embed_atom_variable(cetta_engine_t *e, const char *s, cetta_atom_t **o) { return atom_scalar(e,s,0,5,0,o); }
static cetta_status_t atom_exact_decimal(cetta_engine_t *engine, const char *text, bool rational, cetta_atom_t **output) {
    if (!text || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    char *canonical = rational ? cetta_rational_canonicalize_owned(text) : cetta_bigint_canonicalize_owned(text);
    status = canonical ? atom_scalar(engine, canonical, 0, rational ? 7u : 6u, 0, output)
        : cetta_native_memory_failed() ? CETTA_OUT_OF_MEMORY : CETTA_INVALID_ARGUMENT;
    free(canonical);
    engine_leave(engine, &scope);
    return status;
}
cetta_status_t cetta_embed_atom_integer_decimal(cetta_engine_t *e, const char *s, cetta_atom_t **o) { return atom_exact_decimal(e,s,false,o); }
cetta_status_t cetta_embed_atom_rational_decimal(cetta_engine_t *e, const char *s, cetta_atom_t **o) { return atom_exact_decimal(e,s,true,o); }

cetta_status_t cetta_embed_atom_string_n(cetta_engine_t *engine, const char *bytes, size_t length, cetta_atom_t **output) {
    if (!output || (!bytes && length) || length > UINT32_MAX) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    EmbeddingOwner *owner = owner_new(engine);
    status = owner ? atom_wrap(owner, atom_string_n(&owner->arena, bytes ? bytes : "", length), output)
        : CETTA_OUT_OF_MEMORY;
    owner_release(owner);
    engine_leave(engine, &scope);
    return status;
}

cetta_atom_kind_t cetta_embed_atom_kind(const cetta_atom_t *atom) {
    if (!atom) return CETTA_ATOM_RESOURCE;
    const Atom *value = atom->value;
    if (value->kind == ATOM_SYMBOL) return CETTA_ATOM_SYMBOL;
    if (value->kind == ATOM_VAR) return CETTA_ATOM_VARIABLE;
    if (value->kind == ATOM_EXPR) return CETTA_ATOM_EXPRESSION;
    switch (value->ground.gkind) {
    case GV_INT: return CETTA_ATOM_INTEGER;
    case GV_FLOAT: return CETTA_ATOM_FLOAT;
    case GV_BOOL: return CETTA_ATOM_BOOLEAN;
    case GV_STRING: return CETTA_ATOM_STRING;
    case GV_BIGINT: return CETTA_ATOM_BIG_INTEGER;
    case GV_RATIONAL: return CETTA_ATOM_RATIONAL;
    default: return CETTA_ATOM_RESOURCE;
    }
}

cetta_status_t cetta_embed_atom_get_integer(const cetta_atom_t *atom, int64_t *value) {
    if (!value || !atom || cetta_embed_atom_kind(atom) != CETTA_ATOM_INTEGER) return CETTA_INVALID_ARGUMENT;
    *value = atom->value->ground.ival;
    return CETTA_OK;
}
cetta_status_t cetta_embed_atom_get_float(const cetta_atom_t *atom, double *value) {
    if (!value || !atom || cetta_embed_atom_kind(atom) != CETTA_ATOM_FLOAT) return CETTA_INVALID_ARGUMENT;
    *value = atom->value->ground.fval;
    return CETTA_OK;
}
cetta_status_t cetta_embed_atom_get_boolean(const cetta_atom_t *atom, int *value) {
    if (!value || !atom || cetta_embed_atom_kind(atom) != CETTA_ATOM_BOOLEAN) return CETTA_INVALID_ARGUMENT;
    *value = atom->value->ground.bval;
    return CETTA_OK;
}
cetta_status_t cetta_embed_atom_get_text(const cetta_atom_t *atom, char *buffer, size_t capacity, size_t *size) {
    if (!atom || !size || (!buffer && capacity)) return CETTA_INVALID_ARGUMENT;
    cetta_engine_t *engine = atom->owner->engine;
    EmbeddingScope scope;
    cetta_status_t status = owner_enter(atom->owner, &scope);
    if (status != CETTA_OK) return status;
    cetta_atom_kind_t kind = cetta_embed_atom_kind(atom);
    const char *text = kind == CETTA_ATOM_STRING ? atom->value->ground.sval
        : kind == CETTA_ATOM_SYMBOL || kind == CETTA_ATOM_VARIABLE ? atom_name_cstr(atom->value)
        : kind == CETTA_ATOM_BIG_INTEGER ? atom_bigint_cstr(atom->value)
        : kind == CETTA_ATOM_RATIONAL ? atom_rational_cstr(atom->value) : NULL;
    if (text) {
        *size = kind == CETTA_ATOM_STRING ? atom_string_len(atom->value) : strlen(text);
        if (capacity >= *size && *size) memcpy(buffer, text, *size);
    }
    engine_leave(engine, &scope);
    return text ? CETTA_OK : CETTA_INVALID_ARGUMENT;
}
size_t cetta_embed_atom_arity(const cetta_atom_t *atom) {
    return atom && atom->value->kind == ATOM_EXPR ? atom->value->expr.len : 0;
}
cetta_status_t cetta_embed_atom_child(const cetta_atom_t *atom, size_t index, cetta_atom_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    return !atom || index >= cetta_embed_atom_arity(atom) ? CETTA_INVALID_ARGUMENT
        : atom_wrap(atom->owner, atom->value->expr.elems[index], output);
}
cetta_status_t cetta_embed_atom_equal(const cetta_atom_t *left, const cetta_atom_t *right, int *equal) {
    if (!left || !right || !equal) return CETTA_INVALID_ARGUMENT;
    *equal = 0;
    if (left->owner->engine != right->owner->engine) return CETTA_OK;
    cetta_engine_t *engine = left->owner->engine;
    EmbeddingScope scope;
    cetta_status_t status = owner_enter(left->owner, &scope);
    if (status != CETTA_OK) return status;
    *equal = atom_eq(left->value, right->value);
    engine_leave(engine, &scope);
    return CETTA_OK;
}

cetta_status_t cetta_embed_atom_expression(cetta_engine_t *engine, cetta_atom_t *const *children,
                                     size_t count, cetta_atom_t **output) {
    if (!output || (count && !children) || count > SIZE_MAX / sizeof(Atom *)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (!children[i]) return CETTA_INVALID_ARGUMENT;
        if (children[i]->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    }
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    EmbeddingOwner *owner = owner_new(engine);
    Atom **values = count ? malloc(count * sizeof(*values)) : NULL;
    AtomDeepCopySession *copy = owner ? atom_deep_copy_session_new(&owner->arena) : NULL;
    status = !owner || !copy || (count && !values) ? CETTA_OUT_OF_MEMORY : CETTA_OK;
    for (size_t i = 0; status == CETTA_OK && i < count; ++i) {
        values[i] = atom_deep_copy_session_copy(copy, children[i]->value);
        if (!values[i]) status = CETTA_OUT_OF_MEMORY;
    }
    if (status == CETTA_OK) status = atom_wrap(owner, atom_expr(&owner->arena, values, count), output);
    atom_deep_copy_session_free(copy);
    free(values);
    owner_release(owner);
    engine_leave(engine, &scope);
    return status;
}

cetta_status_t cetta_embed_atom_format(const cetta_atom_t *atom, char *buffer, size_t capacity, size_t *required) {
    if (!atom || !required || (!buffer && capacity)) return CETTA_INVALID_ARGUMENT;
    cetta_engine_t *engine = atom->owner->engine;
    EmbeddingScope scope;
    cetta_status_t status = owner_enter(atom->owner, &scope);
    if (status != CETTA_OK) return status;
    Arena temporary;
    arena_init_detached(&temporary);
    size_t length = 0u;
    const char *text = atom_to_host_bytes(&temporary, atom->value, &length);
    status = text ? CETTA_OK : CETTA_OUT_OF_MEMORY;
    if (text) {
        *required = length + 1u;
        if (capacity >= *required) memcpy(buffer, text, *required);
    }
    arena_free(&temporary);
    engine_leave(engine, &scope);
    return status;
}

static cetta_result_t *result_new(cetta_engine_t *engine) {
    cetta_result_t *result = calloc(1, sizeof(*result));
    if (!result) return NULL;
    result->owner = owner_new(engine);
    if (!result->owner) { free(result); return NULL; }
    return result;
}

void cetta_embed_result_release(cetta_result_t *result) {
    if (!result) return;
    owner_release(result->owner);
    free(result->values);
    free(result->groups);
    free(result);
}
size_t cetta_embed_result_count(const cetta_result_t *r) { return r ? r->count : 0u; }
size_t cetta_embed_result_group_count(const cetta_result_t *r) { return r ? r->group_count : 0u; }
size_t cetta_embed_result_group_size(const cetta_result_t *r, size_t i) { return r && i < r->group_count ? r->groups[i] : 0u; }
cetta_status_t cetta_embed_result_status(const cetta_result_t *r) { return r ? r->status : CETTA_INVALID_ARGUMENT; }
int cetta_embed_result_system_error(const cetta_result_t *r) { return r && r->status == CETTA_IO_ERROR ? r->system_error : 0; }
int cetta_embed_result_exit_code(const cetta_result_t *r) { return r && r->status == CETTA_HALTED ? r->exit_code : 0; }
cetta_status_t cetta_embed_result_diagnostic(const cetta_result_t *result, cetta_diagnostic_t *diagnostic) {
    if (!result || !diagnostic || diagnostic->size != sizeof(*diagnostic)) return CETTA_INVALID_ARGUMENT;
    *diagnostic = result->diagnostic;
    diagnostic->size = sizeof(*diagnostic);
    diagnostic->system_error = result->system_error;
    diagnostic->exit_code = result->exit_code;
    return CETTA_OK;
}
cetta_status_t cetta_embed_result_get(const cetta_result_t *r, size_t i, cetta_atom_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    return !r || i >= r->count ? CETTA_INVALID_ARGUMENT : atom_wrap(r->owner, r->values[i], output);
}

static cetta_status_t outcome_status(const EvalOutcome *outcome) {
    cetta_status_t status = CETTA_OK;
    switch (outcome->completion) {
    case CETTA_EVAL_COMPLETE:
        if (outcome->petta_raised_error) status = CETTA_LANGUAGE_RAISED;
        break;
    case CETTA_EVAL_INCOMPLETE_FUEL: status = CETTA_FUEL_EXHAUSTED; break;
    case CETTA_EVAL_INCOMPLETE_CANCELLED: status = CETTA_CANCELLED; break;
    case CETTA_EVAL_INCOMPLETE_STACK:
    case CETTA_EVAL_INCOMPLETE_CAPACITY: status = CETTA_RESOURCE_ERROR; break;
    case CETTA_EVAL_INCOMPLETE_INVALIDATED: status = CETTA_INVALIDATED; break;
    default: status = CETTA_ENGINE_ERROR; break;
    }
    return status;
}
static cetta_status_t append_outcome(cetta_result_t *result, const EvalOutcome *outcome) {
    cetta_status_t status = outcome_status(outcome);
    if (cetta_native_memory_failed()) status = CETTA_OUT_OF_MEMORY;
    switch (cetta_host_failure()) {
    case CETTA_HOST_MEMORY: status = CETTA_OUT_OF_MEMORY; break;
    case CETTA_HOST_IO: status = CETTA_IO_ERROR; break;
    case CETTA_HOST_INTERRUPTED: status = CETTA_CANCELLED; break;
    default: break;
    }
    CettaEvalSession *session = &result->owner->engine->session.libraries.session;
    if (status == CETTA_OK && cetta_eval_session_process_exit_requested(session)) {
        status = CETTA_HALTED;
        result->exit_code = cetta_eval_session_process_exit_code(session);
    }
    if (outcome->results.len > SIZE_MAX - result->count ||
        result->count + outcome->results.len > SIZE_MAX / sizeof(Atom *) ||
        result->group_count >= SIZE_MAX / sizeof(size_t)) {
        return result->status = CETTA_RESOURCE_ERROR;
    }
    size_t count = result->count + outcome->results.len;
    Atom **values = count ? realloc(result->values, count * sizeof(*values)) : result->values;
    size_t *groups = realloc(result->groups, (result->group_count + 1u) * sizeof(*groups));
    if ((count && !values) || !groups) {
        if (values) result->values = values;
        if (groups) result->groups = groups;
        status = CETTA_OUT_OF_MEMORY;
    } else {
        result->values = values;
        result->groups = groups;
        if (outcome->results.len)
            memcpy(values + result->count, outcome->results.items, outcome->results.len * sizeof(*values));
        result->count = count;
        result->groups[result->group_count++] = outcome->results.len;
    }
    result->diagnostic = result->owner->engine->diagnostic;
    return result->status = status;
}
static cetta_status_t evaluate(cetta_engine_t *engine, Atom *expression,
                              const CettaInvocation *invocation, cetta_result_t *result) {
    EvalOutcome outcome;
    eval_outcome_init(&outcome);
    if (invocation)
        eval_top_with_registry_invocation_outcome(&engine->session.space, &result->owner->arena,
            &engine->persistent, &engine->session.registry, invocation, &outcome);
    else if (engine->session.libraries.session.language_id == CETTA_LANGUAGE_PETTA) {
        const PettaPlanNode *plan = petta_program_plan_current_in(
            engine->session.libraries.petta_program, &result->owner->arena, expression);
        if (plan)
            eval_top_with_registry_petta_plan_outcome(
                &engine->session.space, &result->owner->arena, &engine->persistent,
                &engine->session.registry, expression, plan, &outcome);
        else
            outcome.completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
    } else
        eval_top_with_registry_outcome(&engine->session.space, &result->owner->arena,
            &engine->persistent, &engine->session.registry, expression, &outcome, NULL, NULL);
    cetta_status_t status = append_outcome(result, &outcome);
    eval_outcome_free(&outcome);
    return status;
}

static bool observe_document(void *context, const EvalOutcome *outcome) {
    return append_outcome(context, outcome) == CETTA_OK;
}

static bool options_valid(const cetta_eval_options_t *options) {
    return !options || (options->size == sizeof(*options) && options->semantic_fuel >= -1 && options->semantic_fuel <= INT_MAX);
}

cetta_status_t cetta_embed_eval(cetta_engine_t *engine, const cetta_atom_t *atom,
                         const cetta_eval_options_t *options, cetta_result_t **output) {
    if (!output || !atom || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (atom->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    cetta_result_t *result = result_new(engine);
    int previous_fuel = engine->session.libraries.session.options.fuel_limit;
    engine->session.libraries.session.options.fuel_limit = options ? (int)options->semantic_fuel : -1;
    status = result ? evaluate(engine, atom->value, NULL, result) : CETTA_OUT_OF_MEMORY;
    engine->session.libraries.session.options.fuel_limit = previous_fuel;
    *output = result;
    engine_leave(engine, &scope);
    return status;
}

cetta_status_t cetta_embed_call(cetta_engine_t *engine, const cetta_atom_t *head,
        cetta_atom_t *const *arguments, size_t count, const cetta_eval_options_t *options,
        cetta_result_t **output) {
    if (!output || !head || (count && !arguments) || !options_valid(options) ||
        count >= SIZE_MAX / sizeof(Atom *)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (head->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    for (size_t i = 0; i < count; ++i) {
        if (!arguments[i]) return CETTA_INVALID_ARGUMENT;
        if (arguments[i]->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    }
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    cetta_result_t *result = result_new(engine);
    Atom **values = count ? malloc(count * sizeof(*values)) : NULL;
    if (result && (!count || values)) {
        for (size_t i = 0; i < count; ++i) values[i] = arguments[i]->value;
        CettaInvocation invocation = {.head = binding_value_from_atom(head->value),
                                      .supplied = {.atoms = values, .count = count}};
        int previous_fuel = engine->session.libraries.session.options.fuel_limit;
        engine->session.libraries.session.options.fuel_limit = options ? (int)options->semantic_fuel : -1;
        status = evaluate(engine, NULL, &invocation, result);
        engine->session.libraries.session.options.fuel_limit = previous_fuel;
    } else status = CETTA_OUT_OF_MEMORY;
    if (result) result->status = status;
    *output = result;
    free(values);
    engine_leave(engine, &scope);
    return status;
}

static cetta_status_t run_document_locked(cetta_engine_t *engine, const char *text,
        const char *origin, const cetta_eval_options_t *options, cetta_result_t *result) {
    int previous_fuel = engine->session.libraries.session.options.fuel_limit;
    engine->session.libraries.session.options.fuel_limit = options ? (int)options->semantic_fuel : -1;
    cetta_status_t status = result ? CETTA_OK : CETTA_OUT_OF_MEMORY;
    if (result) {
        CettaDocumentRunReport report;
        bool ok = origin ? cetta_library_run_text_at(&engine->session.libraries,
            &engine->session.space, &engine->session.registry, text, origin,
            &result->owner->arena, &engine->persistent, observe_document, result, &report)
            : cetta_library_run_text(&engine->session.libraries, &engine->session.space,
                &engine->session.registry, text, &result->owner->arena, &engine->persistent,
                observe_document, result, &report);
        if (report.detail && report.detail->kind == ATOM_SYMBOL)
            snprintf(engine->diagnostic.message, sizeof(engine->diagnostic.message), "%s", atom_name_cstr(report.detail));
        status = result->status;
        if (!ok && status == CETTA_OK) {
            status = report.status == CETTA_DOCUMENT_PARSE_FAILED ? CETTA_PARSE_ERROR
                : report.status == CETTA_DOCUMENT_REJECTED ? CETTA_LANGUAGE_RAISED
                : report.status == CETTA_DOCUMENT_INTERRUPTED
                    ? outcome_status(&(EvalOutcome){.completion = report.completion})
                    : CETTA_RESOURCE_ERROR;
            if (report.status == CETTA_DOCUMENT_REJECTED && report.detail) {
                EvalOutcome rejected;
                eval_outcome_init(&rejected);
                result_set_add(&rejected.results, report.detail);
                rejected.petta_raised_error = true;
                status = append_outcome(result, &rejected);
                eval_outcome_free(&rejected);
            }
        }
    }
    engine->session.libraries.session.options.fuel_limit = previous_fuel;
    if (cetta_native_memory_failed()) status = CETTA_OUT_OF_MEMORY;
    if (status == CETTA_PARSE_ERROR) diagnostic_source(&engine->diagnostic, text, strlen(text), origin);
    if (result) { result->status = status; result->diagnostic = engine->diagnostic; }
    return status;
}

cetta_status_t cetta_embed_run(cetta_engine_t *engine, const char *text, size_t length,
                        const cetta_eval_options_t *options, cetta_result_t **output) {
    if (!output || !text || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    char *copy = text_copy(text, length, &status);
    if (!copy) { engine_leave(engine, &scope); return status; }
    cetta_result_t *result = result_new(engine);
    status = run_document_locked(engine, copy, NULL, options, result);
    free(copy);
    *output = result;
    engine_leave(engine, &scope);
    return status;
}

cetta_status_t cetta_embed_load(cetta_engine_t *engine, const char *path,
        const cetta_eval_options_t *options, cetta_result_t **output) {
    if (!path || !output || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    cetta_result_t *result = result_new(engine);
    char *canonical = NULL;
    uint8_t *text = NULL;
    size_t length = 0u;
    if (!result) status = CETTA_OUT_OF_MEMORY;
    else {
        errno = 0;
        canonical = realpath(path, NULL);
        if (!canonical) {
            result->system_error = errno ? errno : EIO;
            status = result->system_error == ENOMEM ? CETTA_OUT_OF_MEMORY : CETTA_IO_ERROR;
        } else if (strlen(canonical) >= PATH_MAX) {
            result->system_error = ENAMETOOLONG; status = CETTA_IO_ERROR;
        } else {
            CettaTextReadStatus read = cetta_text_read_file(canonical, &text, &length, &result->system_error);
            status = read == CETTA_TEXT_READ_OK ? CETTA_OK
                : read == CETTA_TEXT_READ_MEMORY ? CETTA_OUT_OF_MEMORY
                : read == CETTA_TEXT_READ_OVERFLOW ? CETTA_RESOURCE_ERROR : CETTA_IO_ERROR;
            if (status == CETTA_OK) {
                if (atomic_load_explicit(&engine->closed, memory_order_acquire)) status = CETTA_CANCELLED;
                else if (memchr(text, 0, length)) status = CETTA_PARSE_ERROR;
                else status = run_document_locked(engine, (const char *)text, canonical, options, result);
            }
        }
        result->status = status;
    }
    free(text); free(canonical);
    *output = result;
    engine_leave(engine, &scope);
    return status;
}

cetta_status_t cetta_embed_import(cetta_engine_t *engine, const cetta_atom_t *target,
        const cetta_atom_t *spec, const cetta_eval_options_t *options, cetta_result_t **output) {
    if (!engine || !spec || !output || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (spec->owner->engine != engine ||
        (target && target->owner->engine != engine)) return CETTA_FOREIGN_ENGINE;
    cetta_atom_t *space = NULL, *head = NULL;
    cetta_space_t *root = NULL;
    cetta_status_t status = CETTA_OK;
    if (!target) {
        status = cetta_embed_engine_space(engine, &root);
        if (status == CETTA_OK) status = cetta_embed_space_atom(root, &space);
    } else {
        space = (cetta_atom_t *)target;
        cetta_embed_atom_retain(space);
    }
    if (status == CETTA_OK) status = cetta_embed_atom_symbol(engine, "import!", &head);
    cetta_atom_t *arguments[] = {space, (cetta_atom_t *)spec};
    EmbeddingScope import_scope;
    bool entered = false;
    if (status == CETTA_OK) {
        status = engine_enter(engine, &import_scope);
        entered = status == CETTA_OK;
    }
    if (status == CETTA_OK) status = cetta_embed_call(engine, head, arguments, 2u, options, output);
    if (status == CETTA_OK && *output && !(*output)->count && engine->import_error[0]) {
        status = CETTA_IO_ERROR;
        (*output)->status = status;
        snprintf((*output)->diagnostic.message, sizeof((*output)->diagnostic.message), "%s", engine->import_error);
    }
    if (entered) engine_leave(engine, &import_scope);
    cetta_embed_atom_release(space); cetta_embed_atom_release(head);
    cetta_embed_space_release(root);
    return status;
}

static void query_close_cleanup(EmbeddingCleanup *cleanup);
static void query_destroy_cleanup(EmbeddingCleanup *cleanup);

static cetta_status_t query_open_locked(cetta_engine_t *engine, Atom *source, Atom *exports,
    const cetta_eval_options_t *options, bool values, cetta_query_t **output) {
    cetta_status_t status = CETTA_OK;
    cetta_query_t *query = calloc(1u, sizeof(*query));
    if (query) {
        atomic_init(&query->references, 1u);
        atomic_init(&query->cancelled, false);
        atomic_init(&query->closed, false);
        atomic_init(&query->close_scheduled, false);
        query->close_cleanup.run = query_close_cleanup;
        query->destroy_cleanup.run = query_destroy_cleanup;
        query->owner = owner_new(engine);
        if (query->owner)
            query->core = values
                ? eval_query_open_values(&engine->session.space, &engine->persistent,
                    &engine->session.registry, source, options ? (int)options->semantic_fuel : -1)
                : eval_query_open_exports(&engine->session.space, &engine->persistent,
                    &engine->session.registry, source, exports, options ? (int)options->semantic_fuel : -1);
    }
    if (!query || !query->owner || !query->core || cetta_native_memory_failed()) {
        if (query) {
            if (query->core) eval_query_close(query->core);
            owner_release(query->owner); free(query);
        }
        status = CETTA_OUT_OF_MEMORY;
    } else *output = query;
    return status;
}

cetta_status_t cetta_embed_query_open(cetta_engine_t *engine, const cetta_atom_t *source,
    const cetta_eval_options_t *options, cetta_query_t **output) {
    if (!engine || !output || !source || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (source->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status == CETTA_OK) {
        status = query_open_locked(engine, source->value, NULL, options, false, output);
        engine_leave(engine, &scope);
    }
    return status;
}

cetta_status_t cetta_embedding_query_values(cetta_engine_t *engine, const cetta_atom_t *source,
    const cetta_eval_options_t *options, cetta_query_t **output) {
    if (!engine || !output || !source || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (source->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status == CETTA_OK) {
        status = query_open_locked(engine, source->value, NULL, options, true, output);
        engine_leave(engine, &scope);
    }
    return status;
}

void cetta_embed_query_retain(cetta_query_t *query) {
    if (query) atomic_fetch_add_explicit(&query->references, 1u, memory_order_relaxed);
}
void cetta_embed_query_cancel(cetta_query_t *query) {
    if (query) atomic_store_explicit(&query->cancelled, true, memory_order_release);
}
static void query_release_core(cetta_query_t *query) {
    if (query->core && !query->stepping) {
        eval_query_close(query->core);
        query->core = NULL;
    }
}
static void query_close_cleanup(EmbeddingCleanup *cleanup) {
    cetta_query_t *query = (cetta_query_t *)((char *)cleanup - offsetof(cetta_query_t, close_cleanup));
    cetta_engine_t *engine = query->owner->engine;
    EmbeddingScope scope;
    scope_enter(engine, &scope);
    query_release_core(query);
    scope_leave(&scope);
    cetta_embed_query_release(query);
}
static void query_destroy_cleanup(EmbeddingCleanup *cleanup) {
    cetta_query_t *query = (cetta_query_t *)((char *)cleanup - offsetof(cetta_query_t, destroy_cleanup));
    cetta_engine_t *engine = query->owner->engine;
    EmbeddingScope scope;
    scope_enter(engine, &scope);
    assert(!query->stepping);
    query_release_core(query);
    scope_leave(&scope);
    owner_release(query->owner);
    free(query);
}
void cetta_embed_query_close(cetta_query_t *query) {
    if (!query) return;
    cetta_embed_query_cancel(query);
    atomic_store_explicit(&query->closed, true, memory_order_release);
    if (!atomic_exchange_explicit(&query->close_scheduled, true, memory_order_acq_rel)) {
        cetta_embed_query_retain(query);
        engine_cleanup(query->owner->engine, &query->close_cleanup);
    }
}
void cetta_embed_query_release(cetta_query_t *query) {
    if (!query || atomic_fetch_sub_explicit(&query->references, 1u, memory_order_acq_rel) != 1u)
        return;
    cetta_embed_query_cancel(query);
    atomic_store_explicit(&query->closed, true, memory_order_release);
    engine_cleanup(query->owner->engine, &query->destroy_cleanup);
}

cetta_status_t cetta_embed_query_step(cetta_query_t *query, uint64_t allowance,
    cetta_query_step_t *info, cetta_result_t **output) {
    if (!query || !info || info->size != sizeof(*info) || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    *info = (cetta_query_step_t){.size = sizeof(*info), .state = CETTA_QUERY_STATE_STOPPED};
    cetta_embed_query_retain(query);
    if (atomic_load_explicit(&query->closed, memory_order_acquire)) {
        cetta_embed_query_release(query);
        return CETTA_QUERY_CLOSED;
    }
    cetta_engine_t *engine = query->owner->engine;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) { cetta_embed_query_release(query); return status; }
    if (query->stepping || !query->core) {
        status = query->stepping ? CETTA_BUSY : CETTA_QUERY_CLOSED;
    } else {
        query->stepping = true;
        cetta_result_t *result = result_new(engine);
        if (!result) status = CETTA_OUT_OF_MEMORY;
        else {
            CettaEvalCancellationScope cancellation;
            eval_cancellation_enter_query(&cancellation, &query->cancelled);
            EvalOutcome outcome;
            eval_outcome_init(&outcome);
            CettaEvalQueryAnswer packet;
            CettaEvalQueryStep step = eval_query_step(query->core, &result->owner->arena,
                allowance, &outcome, &packet, &info->physical_steps);
            if (query->matched_value_pattern && outcome.results.len) {
                Bindings bindings;
                if (!packet.bindings || !bindings_restore_captured_value(packet.bindings, &bindings))
                    status = CETTA_ENGINE_ERROR;
                else {
                    /* Matching projected an inert unit. Substitute the stored
                     * pattern here, after execution, and publish it as data. */
                    for (CettaCount i = 0; i < outcome.results.len; ++i) {
                        outcome.results.items[i] = bindings_apply(&bindings,
                            &result->owner->arena, query->matched_value_pattern);
                        if (!outcome.results.items[i]) status = CETTA_OUT_OF_MEMORY;
                    }
                    bindings_free(&bindings);
                }
            }
            if (status == CETTA_OK) status = append_outcome(result, &outcome);
            info->state = step == CETTA_QUERY_ANSWER ? CETTA_QUERY_STATE_ANSWER
                : step == CETTA_QUERY_EXHAUSTED ? CETTA_QUERY_STATE_COMPLETE
                : step == CETTA_QUERY_STOPPED ? CETTA_QUERY_STATE_STOPPED : CETTA_QUERY_STATE_PAUSED;
            if (step == CETTA_QUERY_DRIVER_PENDING) status = CETTA_PREPARATION_REQUIRED;
            if (status == CETTA_HALTED) {
                info->state = CETTA_QUERY_STATE_STOPPED;
                atomic_store_explicit(&query->closed, true, memory_order_release);
            }
            info->source_steps = outcome.steps_spent;
            info->source_remaining = outcome.budget_remaining;
            info->source_limited = outcome.budget_limited;
            info->conditional = packet.kind == CETTA_CALL_SUSPENDED;
            if (packet.bindings) {
                Bindings bindings;
                if (bindings_restore_captured_value(packet.bindings, &bindings)) {
                    Atom *pairs = bindings_to_atom(&result->owner->arena, &bindings);
                    bindings_free(&bindings);
                    if (!pairs || atom_wrap(result->owner, pairs, &info->bindings) != CETTA_OK)
                        status = CETTA_OUT_OF_MEMORY;
                } else status = CETTA_ENGINE_ERROR;
            }
            if (packet.delayed && atom_wrap(result->owner, packet.delayed, &info->delayed) != CETTA_OK)
                status = CETTA_OUT_OF_MEMORY;
            if (cetta_native_memory_failed()) status = CETTA_OUT_OF_MEMORY;
            result->status = status;
            *output = result;
            eval_outcome_free(&outcome);
            eval_cancellation_leave(&cancellation);
        }
        query->stepping = false;
        if (atomic_load_explicit(&query->closed, memory_order_acquire)) query_release_core(query);
    }
    engine_leave(engine, &scope);
    cetta_embed_query_release(query);
    return status;
}

cetta_status_t cetta_embed_space_from_atom(cetta_engine_t *engine, const cetta_atom_t *atom, cetta_space_t **output) {
    if (!engine || !atom || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    if (atom->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    if (atom->value->kind != ATOM_GROUNDED || atom->value->ground.gkind != GV_SPACE)
        return CETTA_INVALID_ARGUMENT;
    cetta_space_t *space = malloc(sizeof(*space));
    if (!space) return CETTA_OUT_OF_MEMORY;
    atomic_init(&space->references, 1u);
    space->value = (cetta_atom_t *)atom;
    cetta_embed_atom_retain(space->value);
    *output = space;
    return CETTA_OK;
}
void cetta_embed_space_retain(cetta_space_t *space) {
    if (space) atomic_fetch_add_explicit(&space->references, 1u, memory_order_relaxed);
}
void cetta_embed_space_release(cetta_space_t *space) {
    if (!space || atomic_fetch_sub_explicit(&space->references, 1u, memory_order_acq_rel) != 1u)
        return;
    cetta_embed_atom_release(space->value);
    free(space);
}
cetta_status_t cetta_embed_space_atom(const cetta_space_t *space, cetta_atom_t **output) {
    if (!space || !output) return CETTA_INVALID_ARGUMENT;
    *output = space->value;
    cetta_embed_atom_retain(*output);
    return CETTA_OK;
}
cetta_status_t cetta_embed_engine_space(cetta_engine_t *engine, cetta_space_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    EmbeddingOwner *owner = owner_new(engine);
    cetta_atom_t *atom = NULL;
    if (!owner) status = CETTA_OUT_OF_MEMORY;
    else {
        status = atom_wrap(owner, atom_space(&owner->arena, &engine->session.space), &atom);
        if (status == CETTA_OK) status = cetta_embed_space_from_atom(engine, atom, output);
        cetta_embed_atom_release(atom);
        owner_release(owner);
    }
    engine_leave(engine, &scope);
    return status;
}
cetta_status_t cetta_embed_space_new(cetta_engine_t *engine, cetta_space_t **output) {
    if (!output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    cetta_atom_t *head = NULL, *value = NULL;
    cetta_result_t *result = NULL;
    cetta_status_t status = cetta_embed_atom_symbol(engine, "new-space", &head);
    if (status == CETTA_OK) status = cetta_embed_call(engine, head, NULL, 0, NULL, &result);
    if (status == CETTA_OK) status = cetta_embed_result_get(result, 0, &value);
    if (status == CETTA_OK) status = cetta_embed_space_from_atom(engine, value, output);
    cetta_embed_atom_release(value);
    cetta_embed_atom_release(head);
    cetta_embed_result_release(result);
    return status;
}
cetta_status_t cetta_embed_space_count(cetta_space_t *space, uint64_t *count) {
    if (!space || !count) return CETTA_INVALID_ARGUMENT;
    cetta_engine_t *engine = space->value->owner->engine;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    *count = space_length64(space->value->value->ground.ptr);
    engine_leave(engine, &scope);
    return CETTA_OK;
}
static cetta_status_t space_mutate(cetta_space_t *space, const cetta_atom_t *fact,
                                   bool remove, int *removed) {
    if (!space || !fact) return CETTA_INVALID_ARGUMENT;
    cetta_engine_t *engine = space->value->owner->engine;
    if (fact->owner->engine != engine) return CETTA_FOREIGN_ENGINE;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    Space *target = space->value->value->ground.ptr;
    uint64_t before = space_length64(target);
    cetta_atom_t *head = NULL;
    cetta_result_t *result = NULL;
    status = cetta_embed_atom_symbol(engine, remove ? "remove-atom" : "add-atom", &head);
    cetta_atom_t *arguments[] = {space->value, (cetta_atom_t *)fact};
    if (status == CETTA_OK) status = cetta_embed_call(engine, head, arguments, 2, NULL, &result);
    if (status == CETTA_OK && removed) *removed = space_length64(target) < before;
    cetta_embed_atom_release(head);
    cetta_embed_result_release(result);
    engine_leave(engine, &scope);
    return status;
}
cetta_status_t cetta_embed_space_add_atom(cetta_space_t *space, const cetta_atom_t *fact) {
    return space_mutate(space, fact, false, NULL);
}
cetta_status_t cetta_embed_space_remove_atom(cetta_space_t *space, const cetta_atom_t *fact, int *removed) {
    if (!removed) return CETTA_INVALID_ARGUMENT;
    *removed = 0;
    return space_mutate(space, fact, true, removed);
}
cetta_status_t cetta_embed_space_match(cetta_space_t *space, const cetta_atom_t *pattern,
    const cetta_atom_t *template_value, const cetta_eval_options_t *options, cetta_query_t **output) {
    if (!space || !pattern || !output || !options_valid(options)) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    cetta_engine_t *engine = space->value->owner->engine;
    if (pattern->owner->engine != engine || (template_value && template_value->owner->engine != engine))
        return CETTA_FOREIGN_ENGINE;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    Arena scratch;
    arena_init_detached(&scratch);
    Atom *variables = eval_query_pattern_variables(&scratch, pattern->value);
    Atom *body = template_value ? template_value->value
        : engine->language == CETTA_LANGUAGE_PETTA
            ? atom_expr2(&scratch, atom_symbol_id(&scratch, g_builtin_syms.quote), pattern->value)
            : atom_unit(&scratch);
    Atom *source = body ? atom_expr(&scratch, (Atom *[]){
        atom_symbol_id(&scratch, g_builtin_syms.match), space->value->value, pattern->value, body}, 4) : NULL;
    status = source && variables ? query_open_locked(engine, source, variables, options, false, output)
                                : CETTA_OUT_OF_MEMORY;
    if (status == CETTA_OK && !template_value && engine->language == CETTA_LANGUAGE_HE) {
        (*output)->matched_value_pattern = atom_deep_copy(&(*output)->owner->arena, pattern->value);
        if (!(*output)->matched_value_pattern) {
            cetta_embed_query_release(*output);
            *output = NULL;
            status = CETTA_OUT_OF_MEMORY;
        }
    }
    arena_free(&scratch);
    engine_leave(engine, &scope);
    return status;
}

#include "embedding_callbacks.inc"
#include "embedding_buffers.inc"
#include "embedding_sparse.inc"

const char *cetta_embed_status_name(cetta_status_t status) {
    static const char *const names[] = {"ok", "invalid-argument", "out-of-memory",
        "engine-closed", "foreign-engine", "parse-error", "language-raised",
        "fuel-exhausted", "cancelled", "resource-error", "engine-error", "busy",
        "query-closed", "invalidated", "preparation-required", "buffer-incompatible", "io-error", "halted"};
    return (unsigned)status < sizeof(names) / sizeof(*names) ? names[status] : "unknown-status";
}
