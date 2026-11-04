#include "runtime_session.h"
#include "term_domain.h"

static bool runtime_session_init(CettaRuntimeSession *runtime, Arena *persistent,
    CettaTermDomain *domain, CettaLanguageId language, const CettaProfile *profile) {
    if (!runtime || !persistent || runtime->libraries_ready) return false;
    runtime->work_closed = false;
    runtime->libraries_ready = true;
    if (!cetta_library_context_try_init_for_language_profile(&runtime->libraries, language, profile))
        return false;
    if (domain && !cetta_library_context_set_term_domain(&runtime->libraries, domain))
        return false;
    term_universe_set_persistent_arena(cetta_library_context_universe(&runtime->libraries), persistent);
    runtime->resources = eval_resources_new();
    return runtime->resources != NULL;
}

bool cetta_runtime_session_init(CettaRuntimeSession *runtime, Arena *persistent,
    CettaLanguageId language, const CettaProfile *profile) {
    return runtime_session_init(runtime, persistent, NULL, language, profile);
}
bool cetta_runtime_session_init_in_domain(CettaRuntimeSession *runtime, Arena *persistent,
    CettaTermDomain *domain, CettaLanguageId language, const CettaProfile *profile) {
    return domain && runtime_session_init(runtime, persistent, domain, language, profile);
}

bool cetta_runtime_session_open_root(CettaRuntimeSession *runtime, Arena *persistent) {
    if (!runtime || !persistent || !runtime->resources || !runtime->libraries_ready ||
        runtime->space_ready || runtime->work_closed) return false;
    if (!space_try_init_with_universe(&runtime->space, cetta_library_context_universe(&runtime->libraries)))
        return false;
    runtime->space_ready = true;
    if (!registry_try_init(&runtime->registry)) return false;
    runtime->registry_ready = true;
    if (!arena_try_reserve(persistent, sizeof(Atom))) return false;
    Atom *self = atom_space(persistent, &runtime->space);
    if (!self) return false;
    cetta_provenance_assert_not_transient(self, "runtime.registry.self");
    registry_bind_id(&runtime->registry, g_builtin_syms.self, self);
    if (runtime->libraries.session.language_id == CETTA_LANGUAGE_HE) {
        cetta_library_set_top_module_space(&runtime->libraries, &runtime->space);
        cetta_library_set_top_module_registry(&runtime->libraries, &runtime->registry);
    }
    return true;
}

void cetta_runtime_session_stop(CettaRuntimeSession *runtime) {
    if (!runtime || !runtime->libraries_ready || runtime->work_closed) return;
    runtime->work_closed = true;
    cetta_native_handle_cleanup_all(&runtime->libraries);
}

static void release_registry_spaces(CettaRuntimeSession *runtime) {
    Registry *registry = &runtime->registry;
    for (uint32_t index = 0; index < registry->len; ++index) {
        Atom *value = registry->entries[index].value;
        if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_SPACE) continue;
        Space *space = value->ground.ptr;
        /* A module's space belongs to the library context, which frees it
         * with the other modules; a held space's record frees it
         * (eval_cleanup_owned_new_spaces). */
        if (space == &runtime->space || space->arena_hold ||
            cetta_library_owns_space(&runtime->libraries, space)) continue;
        bool repeated = false;
        for (uint32_t prior = 0; prior < index; ++prior) {
            Atom *earlier = registry->entries[prior].value;
            if (earlier && earlier->kind == ATOM_GROUNDED &&
                earlier->ground.gkind == GV_SPACE && earlier->ground.ptr == space) {
                repeated = true;
                break;
            }
        }
        if (repeated) continue;
        if (runtime->libraries.petta_program)
            petta_program_forget_space(runtime->libraries.petta_program, space);
        space_free(space);
    }
}

static void runtime_session_dispose(CettaRuntimeSession *runtime, bool defer_root) {
    if (!runtime) return;
    CettaLibraryContext *library = runtime->libraries_ready ? &runtime->libraries : NULL;
    CettaLibraryContext *previous_library = eval_exchange_library_context(library);
    CettaEvalResources *resources = runtime->resources;
    CettaEvalResources *previous_resources = eval_resources_exchange(resources);
    CettaEvalGc *collector = eval_resources_collector(resources);
    CettaEvalGc *previous_collector = eval_collector_exchange(collector);
    bool own_library = previous_library == library;
    bool own_resources = previous_resources == resources;
    bool own_collector = previous_collector == collector;
    cetta_runtime_session_stop(runtime);
    if (library && !cetta_library_context_term_domain(library))
        term_universe_set_collection_mode(cetta_library_context_universe(library), TERM_UNIVERSE_COLLECTION_OFF);
    if (runtime->registry_ready) release_registry_spaces(runtime);
    eval_resources_free(resources, library ? library->petta_program : NULL,
        runtime->registry_ready ? &runtime->registry : NULL,
        runtime->space_ready ? &runtime->space : NULL);
    runtime->resources = NULL;
    eval_resources_exchange(own_resources ? NULL : previous_resources);
    eval_collector_exchange(own_collector ? NULL : previous_collector);
    if (runtime->registry_ready) registry_free(&runtime->registry);
    runtime->registry_ready = false;
    if (library) cetta_library_context_free(library);
    runtime->libraries_ready = false;
    if (!defer_root && runtime->space_ready) {
        space_free(&runtime->space);
        runtime->space_ready = false;
    }
    eval_exchange_library_context(own_library ? NULL : previous_library);
}

void cetta_runtime_session_dispose(CettaRuntimeSession *runtime) {
    runtime_session_dispose(runtime, false);
}
void cetta_runtime_session_dispose_before_root(CettaRuntimeSession *runtime) {
    runtime_session_dispose(runtime, true);
}
