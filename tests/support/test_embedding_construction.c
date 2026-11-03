#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "cetta_embed.h"
#include "runtime_names.h"
#include "stats.h"
#include "eval.h"
#include "library.h"
#include "he_type_policy.h"
#include "petta_compiled_reader.h"
#include "he_compiled_reader.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

enum Stage { NONE, SYMBOLS, BUILTINS, VARIABLES, STATISTICS, TYPES, READER,
    LIBRARY, ROOT_SPACE, REGISTRY, RESERVE, RESOURCES };
static enum Stage wanted, active;
static bool watching, refuse_engine_mutex;
static size_t allocation, failure = SIZE_MAX, tracked, held;
static void *owned[64];
static pthread_mutex_t *owned_mutex[16];
static size_t mutex_count, held_mutexes;
static size_t mutex_allocation, mutex_failure = SIZE_MAX;

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
static bool refuse(void) {
    if (!watching || active != wanted) return false;
    if (allocation++ != failure) return false;
    errno = ENOMEM;
    return true;
}
static void *retain_allocation(void *pointer) {
    if (pointer && watching && active != NONE) {
        assert(tracked < sizeof(owned) / sizeof(*owned));
        owned[tracked++] = pointer;
        held++;
    }
    return pointer;
}
void *__wrap_malloc(size_t size) {
    return refuse() ? NULL : retain_allocation(__real_malloc(size));
}
void *__wrap_calloc(size_t count, size_t size) {
    return refuse() ? NULL : retain_allocation(__real_calloc(count, size));
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (refuse()) return NULL;
    size_t slot = tracked;
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) {
        slot = i; break;
    }
    void *next = __real_realloc(pointer, size);
    if (slot < tracked) {
        if (next || !size) {
            owned[slot] = next;
            if (!next) { assert(held); held--; }
        }
        return next;
    }
    return retain_allocation(next);
}
void __wrap_free(void *pointer) {
    for (size_t i = 0; pointer && i < tracked; ++i) if (owned[i] == pointer) {
        owned[i] = NULL;
        assert(held);
        held--;
    }
    __real_free(pointer);
}
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    if (refuse_engine_mutex) {
        refuse_engine_mutex = false;
        return EAGAIN;
    }
    if (watching && active == wanted && mutex_allocation++ == mutex_failure) return EAGAIN;
    int result = __real_pthread_mutex_init(mutex, attr);
    if (!result && watching && active != NONE) {
        assert(mutex_count < sizeof(owned_mutex) / sizeof(*owned_mutex));
        owned_mutex[mutex_count++] = mutex;
        held_mutexes++;
    }
    return result;
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
    for (size_t i = 0; i < mutex_count; ++i) if (owned_mutex[i] == mutex) {
        owned_mutex[i] = NULL;
        assert(held_mutexes);
        held_mutexes--;
    }
    return __real_pthread_mutex_destroy(mutex);
}

bool __real_symbol_table_try_init(SymbolTable *);
bool __wrap_symbol_table_try_init(SymbolTable *table) {
    enum Stage previous = active; active = SYMBOLS;
    bool result = __real_symbol_table_try_init(table);
    active = previous; return result;
}
bool __real_symbol_table_try_init_builtins(SymbolTable *, BuiltinSyms *);
bool __wrap_symbol_table_try_init_builtins(SymbolTable *table, BuiltinSyms *builtins) {
    enum Stage previous = active; active = BUILTINS;
    bool result = __real_symbol_table_try_init_builtins(table, builtins);
    active = previous; return result;
}
bool __real_var_intern_try_init(VarInternTable *);
bool __wrap_var_intern_try_init(VarInternTable *table) {
    enum Stage previous = active; active = VARIABLES;
    bool result = __real_var_intern_try_init(table);
    active = previous; return result;
}
bool __real_cetta_library_context_try_init_for_language_profile(CettaLibraryContext *, CettaLanguageId, const CettaProfile *);
bool __wrap_cetta_library_context_try_init_for_language_profile(CettaLibraryContext *library,
        CettaLanguageId language, const CettaProfile *profile) {
    enum Stage previous = active; active = LIBRARY;
    bool result = __real_cetta_library_context_try_init_for_language_profile(library, language, profile);
    active = previous; return result;
}
bool __real_space_try_init_with_universe(Space *, TermUniverse *);
bool __wrap_space_try_init_with_universe(Space *space, TermUniverse *universe) {
    enum Stage previous = active; active = ROOT_SPACE;
    bool result = __real_space_try_init_with_universe(space, universe);
    active = previous; return result;
}
bool __real_registry_try_init(Registry *);
bool __wrap_registry_try_init(Registry *registry) {
    enum Stage previous = active; active = REGISTRY;
    bool result = __real_registry_try_init(registry);
    active = previous; return result;
}
bool __real_arena_try_reserve(Arena *, size_t);
bool __wrap_arena_try_reserve(Arena *arena, size_t size) {
    enum Stage previous = active; active = RESERVE;
    bool result = __real_arena_try_reserve(arena, size);
    active = previous; return result;
}
#define WRAP_NEW(type, name, stage) \
    type *__real_##name(void); \
    type *__wrap_##name(void) { \
        enum Stage previous = active; active = stage; \
        type *result = __real_##name(); \
        active = previous; return result; \
    }
WRAP_NEW(CettaRuntimeStatsOwner, cetta_runtime_stats_owner_new, STATISTICS)
WRAP_NEW(CettaHeTypeTables, he_type_tables_new, TYPES)
WRAP_NEW(PeTTaCompiledReaderV1, petta_compiled_reader_v1_new, READER)
WRAP_NEW(PeTTaCompiledReaderV1, petta_compiled_reader_v1_new_with_lists, READER)
WRAP_NEW(HECompiledReaderV1, he_compiled_reader_v1_new, READER)
WRAP_NEW(HECompiledReaderV1, he_compiled_reader_v1_new_with_lists, READER)
WRAP_NEW(CettaEvalResources, eval_resources_new, RESOURCES)
#undef WRAP_NEW

static void begin(enum Stage stage, size_t fail_at) {
    assert(!held && !held_mutexes && active == NONE);
    wanted = stage; allocation = tracked = mutex_count = 0;
    failure = fail_at; watching = true;
    mutex_allocation = 0u; mutex_failure = SIZE_MAX;
}
static void clean(void) {
    watching = false;
    assert(held == 0u && "constructor failure retained owned storage");
    assert(held_mutexes == 0u && "constructor failure retained a mutex");
}
static void answer_42(cetta_engine_t *engine) {
    cetta_result_t *result = NULL;
    const char source[] = "!(+ 20 22)";
    assert(cetta_embed_run(engine, source, sizeof(source) - 1u, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 1u);
    cetta_atom_t *value = NULL;
    int64_t integer;
    assert(cetta_embed_result_get(result, 0u, &value) == CETTA_OK);
    assert(cetta_embed_atom_get_integer(value, &integer) == CETTA_OK && integer == 42);
    cetta_embed_atom_release(value);
    cetta_embed_result_release(result);
}
static void restored(CettaNameDomain *names, CettaRuntimeStatsOwner *stats,
        CettaHeTypeTables *types, CettaEvalResources *resources) {
    assert(g_symbols == &names->symbols && g_var_intern == &names->variables);
    assert(g_builtin_syms.empty == names->builtins.empty);
    assert(cetta_runtime_stats_owner_current() == stats);
    assert(he_type_tables_exchange(types) == types);
    assert(eval_resources_exchange(resources) == resources);
}
int main(void) {
    CettaNameDomain outer;
    cetta_name_domain_init(&outer);
    CettaNameContext saved_names = cetta_name_context_current();
    CettaNameContext names = cetta_name_domain_context(&outer);
    cetta_name_context_install(&names);
    CettaRuntimeStatsOwner *stats = cetta_runtime_stats_owner_new();
    CettaHeTypeTables *types = he_type_tables_new();
    CettaEvalResources *resources = eval_resources_new();
    assert(stats && types && resources);
    CettaRuntimeStatsOwner *saved_stats = cetta_runtime_stats_owner_exchange(stats);
    CettaHeTypeTables *saved_types = he_type_tables_exchange(types);
    CettaEvalResources *saved_resources = eval_resources_exchange(resources);
    cetta_engine_t *sibling = NULL;
    assert(cetta_embed_engine_new(NULL, &sibling) == CETTA_OK);
    const char *languages[] = {"petta", "he"};
    unsigned refusals = 0u, mutex_refusals = 0u;
    for (unsigned language = 0; language < 2; ++language) {
        for (unsigned extended = 0; extended < 2; ++extended) {
            cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
                languages[language], extended ? "extended" : NULL, NULL};
            for (enum Stage stage = SYMBOLS; stage <= RESOURCES; ++stage) {
                begin(stage, SIZE_MAX);
                cetta_engine_t *engine = NULL;
                assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
                size_t sites = allocation;
                size_t locks = mutex_allocation;
                if (!sites) fprintf(stderr, "constructor stage %d has no allocation sites in %s/%s\n",
                    stage, config.language, config.profile ? config.profile : "default");
                assert(sites > 0u);
                cetta_embed_engine_release(engine);
                clean();
                for (size_t site = 0; site < sites; ++site) {
                    begin(stage, site);
                    engine = (cetta_engine_t *)(uintptr_t)1;
                    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OUT_OF_MEMORY && !engine);
                    clean();
                    restored(&outer, stats, types, resources);
                    answer_42(sibling);
                    refusals++;
                }
                for (size_t lock = 0; lock < locks; ++lock) {
                    begin(stage, SIZE_MAX);
                    mutex_failure = lock;
                    assert(cetta_embed_engine_new(&config, &engine) == CETTA_ENGINE_ERROR && !engine);
                    clean();
                    restored(&outer, stats, types, resources);
                    answer_42(sibling);
                    mutex_refusals++;
                }
                assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
                answer_42(engine);
                cetta_embed_engine_release(engine);
            }
        }
    }
    refuse_engine_mutex = true;
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_ENGINE_ERROR && !engine);
    restored(&outer, stats, types, resources);
    answer_42(sibling);
    cetta_embed_engine_release(sibling);
    he_type_tables_exchange(saved_types);
    he_type_tables_free(types);
    eval_resources_exchange(saved_resources);
    eval_resources_free(resources, NULL, NULL, NULL);
    cetta_runtime_stats_owner_exchange(saved_stats);
    cetta_runtime_stats_owner_free(stats);
    cetta_name_context_install(&saved_names);
    cetta_name_domain_free(&outer);
    printf("embedding construction: %u allocation refusals, %u mutex refusals, four dialect/profile routes; cleanup, context restoration and retry pass\n",
        refusals, mutex_refusals + 1u);
    return 0;
}
