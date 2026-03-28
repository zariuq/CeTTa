#include "session.h"

#include <stdio.h>
#include <string.h>

static const CettaProfile CETTA_PROFILE_HE_COMPAT_VALUE = {
    .id = CETTA_PROFILE_HE_COMPAT,
    .name = "he_compat",
    .note = "HE-compatible public/runtime surface.",
    .he_compatible_surface = true,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
};

static const CettaProfile CETTA_PROFILE_HE_EXTENDED_VALUE = {
    .id = CETTA_PROFILE_HE_EXTENDED,
    .name = "he_extended",
    .note = "HE-compatible surface plus labeled CeTTa extensions.",
    .he_compatible_surface = true,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = false,
};

static const CettaProfile CETTA_PROFILE_HE_PRIME_VALUE = {
    .id = CETTA_PROFILE_HE_PRIME,
    .name = "he_prime",
    .note = "Binder-aware dependent telescope elaboration atop he_extended.",
    .he_compatible_surface = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = true,
};

static const CettaSurfacePolicy CETTA_SURFACE_POLICIES[] = {
    {"_minimal-foldl-atom", CETTA_PROFILE_MASK_ALL, "compat_alias"},
    {"foldl-atom-in-space", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"foldl-atom", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"count-atoms", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "extension_only"},
    {"module-inventory!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"runtime-stats!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"reset-runtime-stats!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"register-module!", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"git-module!", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"import!", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"include", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"mod-space!", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"print-mods!", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"capture", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"quote", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"unquote", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"sealed", CETTA_PROFILE_MASK_ALL, "keep_he_public_surface"},
    {"collect", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"select", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"once", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "compat_alias"},
    {"search-policy", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"new-space-kind", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-kind", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-match-backend", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-capabilities", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-len", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-push", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-peek", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-pop", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-get", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-truncate", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
};

static const CettaModuleProviderDescriptor CETTA_MODULE_PROVIDER_DESCRIPTORS[] = {
    {CETTA_MODULE_PROVIDER_REGISTERED_ROOT, "registered-root", CETTA_MODULE_PROVIDER_REGISTERED_ROOTS,
     true, false, false, CETTA_MODULE_LOCATOR_FILESYSTEM_PATH, CETTA_REMOTE_REVISION_NONE,
     "manual-register", "Namespace root registered through register-module!."},
    {CETTA_MODULE_PROVIDER_RELATIVE_FILE, "relative-file", CETTA_MODULE_PROVIDER_RELATIVE_FILES,
     true, false, false, CETTA_MODULE_LOCATOR_FILESYSTEM_PATH, CETTA_REMOTE_REVISION_NONE,
     "filesystem-live", "File or module resolved relative to the current import/script directory."},
    {CETTA_MODULE_PROVIDER_STDLIB_FILE, "stdlib-file", CETTA_MODULE_PROVIDER_STDLIB,
     true, false, false, CETTA_MODULE_LOCATOR_FILESYSTEM_PATH, CETTA_REMOTE_REVISION_NONE,
     "immutable-builtin", "Builtin CeTTa/HE stdlib module resolved from the local lib/ tree."},
    {CETTA_MODULE_PROVIDER_GIT_REMOTE, "git-remote", CETTA_MODULE_PROVIDER_GIT,
     true, true, true, CETTA_MODULE_LOCATOR_GIT_URL, CETTA_REMOTE_REVISION_DEFAULT_BRANCH_ONLY,
     "try-fetch-latest", "Provider-backed git-module! mount that clones a local cache entry and then soft-refreshes it on later use."},
    {CETTA_MODULE_PROVIDER_CATALOG_ENTRY, "catalog", CETTA_MODULE_PROVIDER_CATALOG,
     false, true, true, CETTA_MODULE_LOCATOR_CATALOG_KEY, CETTA_REMOTE_REVISION_CATALOG_CONTROLLED,
     "deferred", "Reserved landing zone for future catalog-backed module resolution."},
};

static void copy_setting_string(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static CettaEvalOptionEntry *cetta_eval_option_entry(CettaEvaluatorOptions *options,
                                                     const char *key) {
    if (!options || !key) return NULL;
    for (uint32_t i = 0; i < options->entry_len; i++) {
        if (strcmp(options->entries[i].key, key) == 0) {
            return &options->entries[i];
        }
    }
    if (options->entry_len >= CETTA_MAX_EVAL_OPTIONS) {
        return NULL;
    }
    CettaEvalOptionEntry *entry = &options->entries[options->entry_len++];
    memset(entry, 0, sizeof(*entry));
    copy_setting_string(entry->key, sizeof(entry->key), key);
    return entry;
}

static bool cetta_eval_option_store(CettaEvaluatorOptions *options,
                                    const char *key,
                                    CettaEvalOptionValueKind kind,
                                    const char *repr,
                                    int64_t int_value) {
    CettaEvalOptionEntry *entry = cetta_eval_option_entry(options, key);
    if (!entry) return false;
    entry->kind = kind;
    entry->int_value = int_value;
    copy_setting_string(entry->repr, sizeof(entry->repr), repr);
    return true;
}

const CettaProfile *cetta_profile_he_compat(void) {
    return &CETTA_PROFILE_HE_COMPAT_VALUE;
}

const CettaProfile *cetta_profile_he_extended(void) {
    return &CETTA_PROFILE_HE_EXTENDED_VALUE;
}

const CettaProfile *cetta_profile_he_prime(void) {
    return &CETTA_PROFILE_HE_PRIME_VALUE;
}

const CettaProfile *cetta_profile_from_name(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, CETTA_PROFILE_HE_COMPAT_VALUE.name) == 0) {
        return &CETTA_PROFILE_HE_COMPAT_VALUE;
    }
    if (strcmp(name, CETTA_PROFILE_HE_EXTENDED_VALUE.name) == 0) {
        return &CETTA_PROFILE_HE_EXTENDED_VALUE;
    }
    if (strcmp(name, CETTA_PROFILE_HE_PRIME_VALUE.name) == 0) {
        return &CETTA_PROFILE_HE_PRIME_VALUE;
    }
    return NULL;
}

uint32_t cetta_profile_mask(const CettaProfile *profile) {
    if (!profile) return CETTA_PROFILE_MASK_ALL;
    switch (profile->id) {
    case CETTA_PROFILE_HE_COMPAT:
        return CETTA_PROFILE_MASK_HE_COMPAT;
    case CETTA_PROFILE_HE_EXTENDED:
        return CETTA_PROFILE_MASK_HE_EXTENDED;
    case CETTA_PROFILE_HE_PRIME:
        return CETTA_PROFILE_MASK_HE_PRIME;
    }
    return CETTA_PROFILE_MASK_ALL;
}

bool cetta_profile_visible_in(const CettaProfile *profile, uint32_t visibility_mask) {
    return (cetta_profile_mask(profile) & visibility_mask) != 0;
}

void cetta_profile_print_inventory(FILE *out) {
    fprintf(out, "%s\t%s\n",
            CETTA_PROFILE_HE_COMPAT_VALUE.name, CETTA_PROFILE_HE_COMPAT_VALUE.note);
    fprintf(out, "%s\t%s\n",
            CETTA_PROFILE_HE_EXTENDED_VALUE.name, CETTA_PROFILE_HE_EXTENDED_VALUE.note);
    fprintf(out, "%s\t%s\n",
            CETTA_PROFILE_HE_PRIME_VALUE.name, CETTA_PROFILE_HE_PRIME_VALUE.note);
}

const CettaSurfacePolicy *cetta_surface_policy_lookup(const char *name) {
    if (!name) return NULL;
    size_t count = sizeof(CETTA_SURFACE_POLICIES) / sizeof(CETTA_SURFACE_POLICIES[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(CETTA_SURFACE_POLICIES[i].name, name) == 0) {
            return &CETTA_SURFACE_POLICIES[i];
        }
    }
    return NULL;
}

bool cetta_profile_allows_surface(const CettaProfile *profile, const char *name) {
    const CettaSurfacePolicy *policy = cetta_surface_policy_lookup(name);
    if (!policy) return true;
    return cetta_profile_visible_in(profile, policy->visibility_mask);
}

bool cetta_profile_enables_dependent_telescope(const CettaProfile *profile) {
    return profile && profile->enable_dependent_telescope;
}

uint32_t cetta_module_provider_count(void) {
    return (uint32_t)(sizeof(CETTA_MODULE_PROVIDER_DESCRIPTORS) /
                      sizeof(CETTA_MODULE_PROVIDER_DESCRIPTORS[0]));
}

const CettaModuleProviderDescriptor *cetta_module_provider_at(uint32_t index) {
    if (index >= cetta_module_provider_count()) return NULL;
    return &CETTA_MODULE_PROVIDER_DESCRIPTORS[index];
}

const CettaModuleProviderDescriptor *cetta_module_provider_descriptor(CettaModuleProviderKind kind) {
    for (uint32_t i = 0; i < cetta_module_provider_count(); i++) {
        const CettaModuleProviderDescriptor *desc = cetta_module_provider_at(i);
        if (desc && desc->kind == kind) {
            return desc;
        }
    }
    return NULL;
}

const char *cetta_module_provider_name(CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    return desc ? desc->name : "unknown-provider";
}

CettaModuleProviderFlags cetta_module_provider_flag(CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    return desc ? desc->flag : 0;
}

const char *cetta_module_locator_kind_name(CettaModuleLocatorKind kind) {
    switch (kind) {
    case CETTA_MODULE_LOCATOR_FILESYSTEM_PATH:
        return "filesystem-path";
    case CETTA_MODULE_LOCATOR_GIT_URL:
        return "git-url";
    case CETTA_MODULE_LOCATOR_CATALOG_KEY:
        return "catalog-key";
    }
    return "unknown-locator";
}

const char *cetta_remote_revision_policy_name(CettaRemoteRevisionPolicy policy) {
    switch (policy) {
    case CETTA_REMOTE_REVISION_NONE:
        return "none";
    case CETTA_REMOTE_REVISION_DEFAULT_BRANCH_ONLY:
        return "default-branch-only";
    case CETTA_REMOTE_REVISION_EXPLICIT_REF_FUTURE:
        return "explicit-ref-future";
    case CETTA_REMOTE_REVISION_CATALOG_CONTROLLED:
        return "catalog-controlled";
    }
    return "unknown-revision-policy";
}

bool cetta_module_provider_is_remote(CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    return desc ? desc->remote_source : false;
}

bool cetta_module_provider_is_cache_backed(CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    return desc ? desc->cache_backed : false;
}

const char *cetta_module_provider_update_policy(CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    return (desc && desc->update_policy) ? desc->update_policy : "unknown";
}

bool cetta_profile_allows_provider_kind(const CettaProfile *profile,
                                        CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    if (!desc || !desc->implemented) {
        return false;
    }
    (void)profile;
    switch (kind) {
    case CETTA_MODULE_PROVIDER_REGISTERED_ROOT:
    case CETTA_MODULE_PROVIDER_RELATIVE_FILE:
    case CETTA_MODULE_PROVIDER_STDLIB_FILE:
    case CETTA_MODULE_PROVIDER_GIT_REMOTE:
        return true;
    case CETTA_MODULE_PROVIDER_CATALOG_ENTRY:
        return false;
    }
    return false;
}

bool cetta_module_resolver_allows(const CettaModuleResolver *resolver,
                                  CettaModuleProviderFlags provider_flag) {
    return resolver && (resolver->provider_flags & provider_flag) != 0;
}

void cetta_module_resolver_init_for_profile(CettaModuleResolver *resolver,
                                            const CettaProfile *profile) {
    resolver->provider_flags = 0;
    for (uint32_t i = 0; i < cetta_module_provider_count(); i++) {
        const CettaModuleProviderDescriptor *desc = cetta_module_provider_at(i);
        if (desc && cetta_profile_allows_provider_kind(profile, desc->kind)) {
            resolver->provider_flags |= desc->flag;
        }
    }
    resolver->transactional_imports = true;
}

void cetta_evaluator_options_init(CettaEvaluatorOptions *options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
    options->interpreter_mode = CETTA_INTERPRETER_DEFAULT;
    options->max_stack_depth = -1;
    options->fuel_limit = -1;
}

bool cetta_evaluator_options_is_bare_minimal(const CettaEvaluatorOptions *options) {
    return options && options->interpreter_mode == CETTA_INTERPRETER_BARE_MINIMAL;
}

int cetta_evaluator_options_effective_fuel_limit(const CettaEvaluatorOptions *options) {
    if (!options) return -1;
    if (options->max_stack_depth > 0) {
        return options->max_stack_depth;
    }
    return options->fuel_limit;
}

bool cetta_eval_session_set_type_check_auto(CettaEvalSession *session, bool enabled) {
    if (!session) return false;
    session->options.type_check_auto = enabled;
    return cetta_eval_option_store(&session->options, "type-check",
                                   CETTA_EVAL_OPTION_VALUE_SYMBOL,
                                   enabled ? "auto" : "off", 0);
}

bool cetta_eval_session_set_interpreter_mode(CettaEvalSession *session,
                                             CettaInterpreterMode mode) {
    if (!session) return false;
    session->options.interpreter_mode = mode;
    return cetta_eval_option_store(&session->options, "interpreter",
                                   CETTA_EVAL_OPTION_VALUE_SYMBOL,
                                   mode == CETTA_INTERPRETER_BARE_MINIMAL
                                       ? "bare-minimal"
                                       : "default",
                                   0);
}

bool cetta_eval_session_set_max_stack_depth(CettaEvalSession *session, int depth) {
    char repr[32];
    if (!session || depth < 0) return false;
    session->options.max_stack_depth = depth;
    snprintf(repr, sizeof(repr), "%d", depth);
    return cetta_eval_option_store(&session->options, "max-stack-depth",
                                   CETTA_EVAL_OPTION_VALUE_INT, repr, depth);
}

void cetta_eval_session_set_fuel_limit(CettaEvalSession *session, int fuel_limit) {
    if (!session) return;
    session->options.fuel_limit = fuel_limit > 0 ? fuel_limit : -1;
}

bool cetta_eval_session_record_generic_setting(CettaEvalSession *session,
                                               const char *key,
                                               CettaEvalOptionValueKind kind,
                                               const char *repr,
                                               int64_t int_value) {
    if (!session || !key) return false;
    return cetta_eval_option_store(&session->options, key, kind, repr, int_value);
}

void cetta_eval_session_init(CettaEvalSession *session, const CettaProfile *profile) {
    const CettaProfile *resolved = profile ? profile : cetta_profile_he_extended();
    session->profile = resolved;
    cetta_module_resolver_init_for_profile(&session->module_resolver, resolved);
    cetta_evaluator_options_init(&session->options);
}

void cetta_eval_session_init_he_compat(CettaEvalSession *session) {
    cetta_eval_session_init(session, cetta_profile_he_compat());
}

void cetta_eval_session_init_he_extended(CettaEvalSession *session) {
    cetta_eval_session_init(session, cetta_profile_he_extended());
}

void cetta_eval_session_init_he_prime(CettaEvalSession *session) {
    cetta_eval_session_init(session, cetta_profile_he_prime());
}
