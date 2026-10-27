#include "session.h"

#include <stdio.h>
#include <string.h>

#define CETTA_CHECK_LANGUAGE_ID(language, ident) \
    _Static_assert(language == ident, "profile table language ID mismatch");
CETTA_LANGUAGE_ID_ROWS(CETTA_CHECK_LANGUAGE_ID)
#undef CETTA_CHECK_LANGUAGE_ID

#define CETTA_DEFINE_PROFILE(object, ident, lang, text, description, he, ext, dep, rust, ver, \
                             identity, self, quote) \
    static const CettaProfile object = { \
        .id = ident, .language_id = lang, .name = text, .note = description, \
        .he_compatible_builtin = he, .enable_cetta_extensions = ext, \
        .enable_dependent_telescope = dep, .rust_he_compat_semantics = rust, \
        .identity_policy = identity, .context_self = self, .quote_as_written = quote, \
        .version = ver \
    };

CETTA_PROFILE_BEFORE_ROWS(CETTA_DEFINE_PROFILE)

#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
CETTA_PROFILE_TYPING_ROWS(CETTA_DEFINE_PROFILE)
#endif

CETTA_PROFILE_AFTER_ROWS(CETTA_DEFINE_PROFILE)

#undef CETTA_DEFINE_PROFILE
#define CETTA_PROFILE_ENTRY(object, ident, lang, text, description, he, ext, dep, rust, ver, \
                            identity, self, quote) \
    {.id = ident, .language_id = lang, .name = text, .note = description, \
     .he_compatible_builtin = he, .enable_cetta_extensions = ext, \
     .enable_dependent_telescope = dep, .rust_he_compat_semantics = rust, \
     .identity_policy = identity, .context_self = self, .quote_as_written = quote, \
     .version = ver},
static const CettaProfile CETTA_HOL_PROFILES[] = {
    CETTA_PROFILE_HOL_ROWS(CETTA_PROFILE_ENTRY)
};
#undef CETTA_PROFILE_ENTRY

/* Sorted by name for policy lookup; presentation order is not observable. */
static const CettaBuiltinPolicy CETTA_BUILTIN_POLICIES[] = {
    {"@<", CETTA_PROFILE_MASK_HE_EXTENDED | CETTA_PROFILE_MASK_PETTA_EXTENDED, "extension_only"},
    {"@<=", CETTA_PROFILE_MASK_HE_EXTENDED | CETTA_PROFILE_MASK_PETTA_EXTENDED, "extension_only"},
    {"@>", CETTA_PROFILE_MASK_HE_EXTENDED | CETTA_PROFILE_MASK_PETTA_EXTENDED, "extension_only"},
    {"@>=", CETTA_PROFILE_MASK_HE_EXTENDED | CETTA_PROFILE_MASK_PETTA_EXTENDED, "extension_only"},
    {"_minimal-foldl-atom", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "compat_alias"},
    {"add-atom-nodup", CETTA_PROFILE_MASK_HE_NON_COMPAT | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"capture", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"collect", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "compat_alias"},
    {"count-atoms", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "extension_only"},
    {"eval-in-space", CETTA_PROFILE_MASK_HE_NON_COMPAT | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"filter-atom", CETTA_PROFILE_MASK_ALL, "compat_alias"},
    {"fold", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"fold-by-key", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"foldl-atom", CETTA_PROFILE_MASK_ALL, "compat_alias"},
    {"foldl-atom-in-space", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"git-module!", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"hyperpose", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_BASE_FORMS, "clean_primary_extension"},
    {"import!", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"include", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"include-space-target", CETTA_PROFILE_MASK_HE_NON_COMPAT | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"mod-space!", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"module-inventory!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"new-space-kind", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"once", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_BASE_FORMS, "compat_alias"},
    {"print-mods!", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"quote", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"range-atom", CETTA_PROFILE_MASK_HE_NON_COMPAT | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"reduce", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_BASE_FORMS, "compat_alias"},
    {"register-module!", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"reify", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"repeat-atom", CETTA_PROFILE_MASK_HE_NON_COMPAT | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"reset-runtime-stats!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"runtime-stats!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"sealed", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"search-policy", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"select", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"singleton-visible-witness", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "translator_compat_builtin"},
    {"size", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-get", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-len", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-peek", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-pop", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-push", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-set-backend!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"space-set-match-backend!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "compat_alias"},
    {"space-truncate", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"step!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"unify", CETTA_PROFILE_MASK_HE_PUBLIC | CETTA_PROFILE_MASK_PETTA_EXTENDED, "keep_he_public_builtin"},
    {"unquote", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"with-space-snapshot", CETTA_PROFILE_MASK_HE_NON_COMPAT | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-budget", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-cancel", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-count", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-exists", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-new", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-record", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-route", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
    {"work-take", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS | CETTA_PROFILE_MASK_PETTA_EXTENDED, "clean_primary_extension"},
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
    if (strcmp(key, "type-judgments") == 0) {
        unsigned mode = strcmp(entry->repr, "resolved") == 0
                ? CETTA_HE_TYPE_JUDGMENTS_RESOLVED
            : strcmp(entry->repr, "per-revision") == 0
                ? CETTA_HE_TYPE_JUDGMENTS_PER_REVISION
                : CETTA_HE_TYPE_JUDGMENTS_PER_CALL;
        options->he_typing = (uint8_t)
            ((options->he_typing & ~CETTA_HE_TYPE_JUDGMENTS_MASK) | mode);
    }
    if (strcmp(key, "curried-arrows") == 0) {
        bool on = kind == CETTA_EVAL_OPTION_VALUE_INT
            ? int_value != 0
            : strcmp(entry->repr, "on") == 0 ||
              strcmp(entry->repr, "true") == 0 ||
              strcmp(entry->repr, "True") == 0;
        options->he_typing = on
            ? (uint8_t)(options->he_typing | CETTA_HE_TYPING_CURRIED_ARROWS)
            : (uint8_t)(options->he_typing & ~CETTA_HE_TYPING_CURRIED_ARROWS);
    }
    return true;
}

static uint32_t cetta_language_base_builtin_mask(CettaLanguageId language_id) {
    switch (language_id) {
    case CETTA_LANGUAGE_HE:
        return CETTA_PROFILE_MASK_HE_FORMAL;
    case CETTA_LANGUAGE_PRIME:
        return CETTA_PROFILE_MASK_HE_PRIME;
    case CETTA_LANGUAGE_PETTA:
        return CETTA_PROFILE_MASK_PETTA_BASE;
    case CETTA_LANGUAGE_HOL_LIGHT:
        return 0;
    case CETTA_LANGUAGE_MM2:
    case CETTA_LANGUAGE_AMBIENT:
    case CETTA_LANGUAGE_CALCULATOR:
    case CETTA_LANGUAGE_IMP:
    case CETTA_LANGUAGE_LAMBDA:
    case CETTA_LANGUAGE_METTAFULL_LEGACY:
    case CETTA_LANGUAGE_MINSKYLITE:
    case CETTA_LANGUAGE_MM0LITE:
    case CETTA_LANGUAGE_PYASHCORE:
    case CETTA_LANGUAGE_RHOCALC:
    case CETTA_LANGUAGE_SUBZERO:
    case CETTA_LANGUAGE_ZERO:
    case CETTA_LANGUAGE_GSLT_IL:
    case CETTA_LANGUAGE_ZEROUV:
    case CETTA_LANGUAGE_METTA_INTERACT:
        return CETTA_PROFILE_MASK_ALL;
    }
    return CETTA_PROFILE_MASK_ALL;
}

static bool cetta_profile_name_matches(const char *name,
                                       const CettaProfile *profile) {
    if (!name || !profile || !profile->name) return false;
    return strcmp(name, profile->name) == 0;
}

const CettaProfile *cetta_profile_he_formal(void) {
    return &CETTA_PROFILE_HE_FORMAL_VALUE;
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

const CettaProfile *cetta_profile_prime_default(void) {
    return &CETTA_PROFILE_PRIME_DEFAULT_VALUE;
}

const CettaProfile *cetta_profile_petta_extended(void) {
    return &CETTA_PROFILE_PETTA_EXTENDED_VALUE;
}

#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
const CettaProfile *cetta_profile_petta_typecheck_v2(void) {
    return &CETTA_PROFILE_PETTA_TYPECHECK_V2_VALUE;
}

const CettaProfile *cetta_profile_petta_typecheck_v3(void) {
    return &CETTA_PROFILE_PETTA_TYPECHECK_V3_VALUE;
}
#endif

bool cetta_profile_uses_petta_typing(const CettaProfile *profile) {
    if (!profile)
        return false;
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    return profile->id == CETTA_PROFILE_PETTA_TYPECHECK_V2 ||
        profile->id == CETTA_PROFILE_PETTA_TYPECHECK_V3;
#else
    return false;
#endif
}

bool cetta_profile_is_petta_1_1_c(const CettaProfile *profile) {
    return profile && profile->id == CETTA_PROFILE_PETTA_1_1_C;
}

bool cetta_language_version_petta_typed_line(
    const CettaLanguageVersion *version) {
    return version && (version->id == CETTA_LANGUAGE_VERSION_PETTA_1_1 ||
                       version->id == CETTA_LANGUAGE_VERSION_PETTA_1_1_C);
}

/* Corrected stable PeTTa uses the audited stable libraries. PeTTa 1.1
 * reads the typed libraries of PeTTa's main line first (typecheck/), and
 * runs under its checker unless a program names another 1.1 profile.
 * PeTTa 1.1.c keeps 1.0.6's type language and libraries, takes the typed
 * line's evaluation and module laws, and runs declarations as contracts. */
static const CettaLanguageVersion CETTA_LANGUAGE_VERSIONS[] = {
    {CETTA_LANGUAGE_VERSION_PETTA_STABLE, CETTA_LANGUAGE_PETTA, "stable-fixed",
     NULL, NULL, "Corrected stable PeTTa; the default",
     CETTA_RELATIVE_MODULE_POLICY_WORKING_DIR_ONLY, false, .is_default = true},
    {CETTA_LANGUAGE_VERSION_PETTA_1_1, CETTA_LANGUAGE_PETTA, "1.1",
     "typecheck-v2", "typecheck", "PeTTa's typed language",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY, true, .is_default = false},
    {CETTA_LANGUAGE_VERSION_PETTA_1_1_C, CETTA_LANGUAGE_PETTA, "1.1.c",
     "petta@1.1.c", NULL, "PeTTa's typed line with declarations as call contracts",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY, true, .is_default = false},
    {CETTA_LANGUAGE_VERSION_HE_0_2_10, CETTA_LANGUAGE_HE, "0.2.10",
     NULL, NULL,
     "Hyperon Experimental 0.2.10, CeTTa's fork: profiles he (default), he-compat, extended, he-prime",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY, false, .is_default = true},
    {CETTA_LANGUAGE_VERSION_PRIME_0_0_1, CETTA_LANGUAGE_PRIME, "0.0.1",
     NULL, NULL, "MeTTa-Prime: this line's Prime runtime, under the registry's 0.0.1 spelling",
     CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY, false, .is_default = true},
};

static const CettaLanguageVersion *language_version_find(
    CettaLanguageId language_id, CettaLanguageVersionId id, const char *name) {
    for (size_t i = 0u;
         i < sizeof CETTA_LANGUAGE_VERSIONS / sizeof CETTA_LANGUAGE_VERSIONS[0];
         i++) {
        const CettaLanguageVersion *version = &CETTA_LANGUAGE_VERSIONS[i];
        if (version->language_id != language_id)
            continue;
        if (name ? strcmp(version->name, name) == 0
                 : id == CETTA_LANGUAGE_VERSION_NONE ? version->is_default
                                                     : version->id == id)
            return version;
    }
    return NULL;
}

const CettaLanguageVersion *cetta_language_version_from_name(
    CettaLanguageId language_id, const char *name) {
    return name ? language_version_find(language_id, CETTA_LANGUAGE_VERSION_NONE, name)
                : NULL;
}

const CettaLanguageVersion *cetta_language_version_of(
    CettaLanguageId language_id, const CettaProfile *profile) {
    CettaLanguageVersionId id = profile && profile->language_id == language_id
        ? profile->version : CETTA_LANGUAGE_VERSION_NONE;
    return language_version_find(language_id, id, NULL);
}

void cetta_language_print_versions(FILE *out, CettaLanguageId language_id) {
    for (size_t i = 0u;
         i < sizeof CETTA_LANGUAGE_VERSIONS / sizeof CETTA_LANGUAGE_VERSIONS[0];
         i++) {
        if (CETTA_LANGUAGE_VERSIONS[i].language_id == language_id)
            fprintf(out, "%s\t%s\n", CETTA_LANGUAGE_VERSIONS[i].name,
                    CETTA_LANGUAGE_VERSIONS[i].note);
    }
}

bool cetta_language_has_named_profiles(CettaLanguageId language_id) {
    return language_id == CETTA_LANGUAGE_HOL_LIGHT ||
           language_id == CETTA_LANGUAGE_HE ||
           language_id == CETTA_LANGUAGE_PRIME ||
           language_id == CETTA_LANGUAGE_MM2 ||
           language_id == CETTA_LANGUAGE_PETTA ||
           language_id == CETTA_LANGUAGE_RHOCALC ||
           language_id == CETTA_LANGUAGE_ZERO;
}

bool cetta_profile_is_valid_for_language(CettaLanguageId language_id,
                                         const CettaProfile *profile) {
    return !profile || profile->language_id == language_id;
}

const CettaProfile *cetta_profile_from_name_for_language(CettaLanguageId language_id,
                                                         const char *name) {
    if (!name) return NULL;
    if (language_id == CETTA_LANGUAGE_HOL_LIGHT) {
        for (size_t i = 0; i < sizeof CETTA_HOL_PROFILES / sizeof CETTA_HOL_PROFILES[0]; ++i)
            if (!strcmp(name, CETTA_HOL_PROFILES[i].name)) return &CETTA_HOL_PROFILES[i];
        return NULL;
    }
    if (!cetta_language_has_named_profiles(language_id)) {
        return NULL;
    }
    if (language_id == CETTA_LANGUAGE_HE) {
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_HE_FORMAL_VALUE) ||
            strcmp(name, "formal-he") == 0) {
            return &CETTA_PROFILE_HE_FORMAL_VALUE;
        }
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_HE_COMPAT_VALUE)) {
            return &CETTA_PROFILE_HE_COMPAT_VALUE;
        }
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_HE_EXTENDED_VALUE)) {
            return &CETTA_PROFILE_HE_EXTENDED_VALUE;
        }
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_HE_PRIME_VALUE)) {
            return &CETTA_PROFILE_HE_PRIME_VALUE;
        }
    }
    if (language_id == CETTA_LANGUAGE_PRIME) {
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_DEFAULT_VALUE))
            return &CETTA_PROFILE_PRIME_DEFAULT_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_IDENTITY_J_VALUE))
            return &CETTA_PROFILE_PRIME_IDENTITY_J_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_IDENTITY_SCOPED_VALUE))
            return &CETTA_PROFILE_PRIME_IDENTITY_SCOPED_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_IDENTITY_UIP_VALUE))
            return &CETTA_PROFILE_PRIME_IDENTITY_UIP_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_IDENTITY_UNIVALENCE_VALUE))
            return &CETTA_PROFILE_PRIME_IDENTITY_UNIVALENCE_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_CONTEXT_SELF_HOME_VALUE))
            return &CETTA_PROFILE_PRIME_CONTEXT_SELF_HOME_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_CONTEXT_SELF_HERE_VALUE))
            return &CETTA_PROFILE_PRIME_CONTEXT_SELF_HERE_VALUE;
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_PRIME_QUOTE_AS_WRITTEN_VALUE))
            return &CETTA_PROFILE_PRIME_QUOTE_AS_WRITTEN_VALUE;
    }
    if (language_id == CETTA_LANGUAGE_RHOCALC) {
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_RHOCALC_STRICT_CORE_VALUE)) {
            return &CETTA_PROFILE_RHOCALC_STRICT_CORE_VALUE;
        }
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_RHOCALC_COST_VALUE)) {
            return &CETTA_PROFILE_RHOCALC_COST_VALUE;
        }
        if (cetta_profile_name_matches(name, &CETTA_PROFILE_RHOCALC_RHOMETTA_VALUE)) {
            return &CETTA_PROFILE_RHOCALC_RHOMETTA_VALUE;
        }
    }
    if (language_id == CETTA_LANGUAGE_PETTA) {
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
        if (cetta_profile_name_matches(
                name, &CETTA_PROFILE_PETTA_TYPECHECK_V3_VALUE)) {
            return &CETTA_PROFILE_PETTA_TYPECHECK_V3_VALUE;
        }
        if (cetta_profile_name_matches(
                name, &CETTA_PROFILE_PETTA_TYPECHECK_V2_VALUE)) {
            return &CETTA_PROFILE_PETTA_TYPECHECK_V2_VALUE;
        }
#endif
        if (cetta_profile_name_matches(
                name, &CETTA_PROFILE_PETTA_EXTENDED_VALUE) ||
            strcmp(name, "petta-extended") == 0) {
            return &CETTA_PROFILE_PETTA_EXTENDED_VALUE;
        }
        if (cetta_profile_name_matches(
                name, &CETTA_PROFILE_PETTA_1_1_C_VALUE)) {
            return &CETTA_PROFILE_PETTA_1_1_C_VALUE;
        }
    }
    if (language_id == CETTA_LANGUAGE_ZERO &&
        cetta_profile_name_matches(name, &CETTA_PROFILE_ZERO_EXP_VALUE)) {
        return &CETTA_PROFILE_ZERO_EXP_VALUE;
    }
    if (language_id == CETTA_LANGUAGE_ZERO &&
        cetta_profile_name_matches(name, &CETTA_PROFILE_ZERO_EMIT_VALUE)) {
        return &CETTA_PROFILE_ZERO_EMIT_VALUE;
    }
    if (language_id == CETTA_LANGUAGE_ZERO &&
        cetta_profile_name_matches(name, &CETTA_PROFILE_ZERO_INTERACT_VALUE)) {
        return &CETTA_PROFILE_ZERO_INTERACT_VALUE;
    }
    if (language_id == CETTA_LANGUAGE_MM2 &&
        cetta_profile_name_matches(name, &CETTA_PROFILE_MM2_GSLT_VALUE)) {
        return &CETTA_PROFILE_MM2_GSLT_VALUE;
    }
    return NULL;
}

uint32_t cetta_profile_mask(const CettaProfile *profile) {
    if (!profile) return 0;
    switch (profile->id) {
    case CETTA_PROFILE_HE_FORMAL:
        return CETTA_PROFILE_MASK_HE_FORMAL;
    case CETTA_PROFILE_HE_COMPAT:
        return CETTA_PROFILE_MASK_HE_COMPAT;
    case CETTA_PROFILE_HE_EXTENDED:
        return CETTA_PROFILE_MASK_HE_EXTENDED;
    case CETTA_PROFILE_HE_PRIME:
        return CETTA_PROFILE_MASK_HE_PRIME;
    case CETTA_PROFILE_PRIME_DEFAULT:
    case CETTA_PROFILE_PRIME_IDENTITY_J:
    case CETTA_PROFILE_PRIME_IDENTITY_SCOPED:
    case CETTA_PROFILE_PRIME_IDENTITY_UIP:
    case CETTA_PROFILE_PRIME_IDENTITY_UNIVALENCE:
    case CETTA_PROFILE_PRIME_CONTEXT_SELF_HOME:
    case CETTA_PROFILE_PRIME_CONTEXT_SELF_HERE:
    case CETTA_PROFILE_PRIME_QUOTE_AS_WRITTEN:
        return CETTA_PROFILE_MASK_HE_PRIME;
    case CETTA_PROFILE_RHOCALC_STRICT_CORE:
    case CETTA_PROFILE_RHOCALC_COST:
    case CETTA_PROFILE_RHOCALC_RHOMETTA:
    case CETTA_PROFILE_ZERO_EXP:
    case CETTA_PROFILE_ZERO_EMIT:
    case CETTA_PROFILE_ZERO_INTERACT:
    case CETTA_PROFILE_MM2_GSLT:
        return CETTA_PROFILE_MASK_ALL;
    case CETTA_PROFILE_HOL_STANDARD:
    case CETTA_PROFILE_HOL_OPEN:
    case CETTA_PROFILE_HOL_SEALED:
        return 0;
    case CETTA_PROFILE_PETTA_TYPECHECK_V2:
    case CETTA_PROFILE_PETTA_TYPECHECK_V3:
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
        return CETTA_PROFILE_MASK_PETTA_EXTENDED;
#else
        return 0u;
#endif
    case CETTA_PROFILE_PETTA_EXTENDED:
        return CETTA_PROFILE_MASK_PETTA_EXTENDED;
    case CETTA_PROFILE_PETTA_1_1_C:
        return CETTA_PROFILE_MASK_PETTA_BASE;
    }
    return 0;
}

uint32_t cetta_language_builtin_mask(CettaLanguageId language_id,
                                     const CettaProfile *profile) {
    if (profile && cetta_profile_is_valid_for_language(language_id, profile)) {
        return cetta_profile_mask(profile);
    }
    return cetta_language_base_builtin_mask(language_id);
}

bool cetta_language_visible_in(CettaLanguageId language_id,
                               const CettaProfile *profile,
                               uint32_t visibility_mask) {
    return (cetta_language_builtin_mask(language_id, profile) & visibility_mask) != 0;
}

void cetta_profile_print_inventory_for_language(FILE *out,
                                                CettaLanguageId language_id) {
    if (!out) return;
    if (language_id == CETTA_LANGUAGE_HOL_LIGHT) {
        for (size_t i = 0; i < sizeof CETTA_HOL_PROFILES / sizeof CETTA_HOL_PROFILES[0]; ++i)
            fprintf(out, "%s\t%s\n", CETTA_HOL_PROFILES[i].name, CETTA_HOL_PROFILES[i].note);
        return;
    }
    if (!cetta_language_has_named_profiles(language_id)) {
        fprintf(out, "language '%s' has no named profiles\n",
                cetta_language_canonical_name(language_id));
        return;
    }
    if (language_id == CETTA_LANGUAGE_HE) {
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_HE_FORMAL_VALUE.name, CETTA_PROFILE_HE_FORMAL_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_HE_COMPAT_VALUE.name, CETTA_PROFILE_HE_COMPAT_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_HE_EXTENDED_VALUE.name, CETTA_PROFILE_HE_EXTENDED_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_HE_PRIME_VALUE.name, CETTA_PROFILE_HE_PRIME_VALUE.note);
        return;
    }
    if (language_id == CETTA_LANGUAGE_PRIME) {
        const CettaProfile *prime_profiles[] = {
            &CETTA_PROFILE_PRIME_DEFAULT_VALUE,
            &CETTA_PROFILE_PRIME_IDENTITY_J_VALUE,
            &CETTA_PROFILE_PRIME_IDENTITY_SCOPED_VALUE,
            &CETTA_PROFILE_PRIME_IDENTITY_UIP_VALUE,
            &CETTA_PROFILE_PRIME_IDENTITY_UNIVALENCE_VALUE,
            &CETTA_PROFILE_PRIME_CONTEXT_SELF_HOME_VALUE,
            &CETTA_PROFILE_PRIME_CONTEXT_SELF_HERE_VALUE,
            &CETTA_PROFILE_PRIME_QUOTE_AS_WRITTEN_VALUE,
        };
        for (size_t i = 0; i < sizeof prime_profiles / sizeof prime_profiles[0]; i++)
            fprintf(out, "%s\t%s\n", prime_profiles[i]->name, prime_profiles[i]->note);
        return;
    }
    if (language_id == CETTA_LANGUAGE_RHOCALC) {
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_RHOCALC_STRICT_CORE_VALUE.name,
                CETTA_PROFILE_RHOCALC_STRICT_CORE_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_RHOCALC_COST_VALUE.name,
                CETTA_PROFILE_RHOCALC_COST_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_RHOCALC_RHOMETTA_VALUE.name,
                CETTA_PROFILE_RHOCALC_RHOMETTA_VALUE.note);
        return;
    }
    if (language_id == CETTA_LANGUAGE_PETTA) {
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_PETTA_EXTENDED_VALUE.name,
                CETTA_PROFILE_PETTA_EXTENDED_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_PETTA_1_1_C_VALUE.name,
                CETTA_PROFILE_PETTA_1_1_C_VALUE.note);
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_PETTA_TYPECHECK_V2_VALUE.name,
                CETTA_PROFILE_PETTA_TYPECHECK_V2_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_PETTA_TYPECHECK_V3_VALUE.name,
                CETTA_PROFILE_PETTA_TYPECHECK_V3_VALUE.note);
#endif
        return;
    }
    if (language_id == CETTA_LANGUAGE_ZERO) {
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_ZERO_EXP_VALUE.name,
                CETTA_PROFILE_ZERO_EXP_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_ZERO_EMIT_VALUE.name,
                CETTA_PROFILE_ZERO_EMIT_VALUE.note);
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_ZERO_INTERACT_VALUE.name,
                CETTA_PROFILE_ZERO_INTERACT_VALUE.note);
        return;
    }
    if (language_id == CETTA_LANGUAGE_MM2) {
        fprintf(out, "%s\t%s\n",
                CETTA_PROFILE_MM2_GSLT_VALUE.name,
                CETTA_PROFILE_MM2_GSLT_VALUE.note);
    }
}

const CettaBuiltinPolicy *cetta_builtin_policy_lookup(const char *name) {
    if (!name) return NULL;
    size_t lo = 0u;
    size_t hi = sizeof(CETTA_BUILTIN_POLICIES) / sizeof(CETTA_BUILTIN_POLICIES[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const char *candidate = CETTA_BUILTIN_POLICIES[mid].name;
        /* Different first bytes already determine the lexicographic order. */
        int order = (unsigned char)name[0] - (unsigned char)candidate[0];
        if (order == 0)
            order = strcmp(name + 1u, candidate + 1u);
        if (order == 0)
            return &CETTA_BUILTIN_POLICIES[mid];
        if (order < 0)
            hi = mid;
        else
            lo = mid + 1u;
    }
    return NULL;
}

_Static_assert(sizeof(CETTA_BUILTIN_POLICIES) / sizeof(CETTA_BUILTIN_POLICIES[0]) <
                   CETTA_BUILTIN_POLICY_POSITIONS,
               "every policy has a position");

uint32_t cetta_builtin_policy_position(const CettaBuiltinPolicy *policy) {
    return policy ? (uint32_t)(policy - CETTA_BUILTIN_POLICIES) + 1u : 0u;
}

const CettaBuiltinPolicy *cetta_builtin_policy_at_position(uint32_t position) {
    size_t count = sizeof(CETTA_BUILTIN_POLICIES) / sizeof(CETTA_BUILTIN_POLICIES[0]);
    return position && position <= count ? &CETTA_BUILTIN_POLICIES[position - 1u] : NULL;
}

bool cetta_language_allows_builtin_policy(CettaLanguageId language_id,
                                          const CettaProfile *profile,
                                          const CettaBuiltinPolicy *policy) {
    if (!policy) return true;
    /* Other language adapters use the general builtin mask. Structural
       order is explicitly an HE/PeTTa extended-profile operation. */
    if (language_id != CETTA_LANGUAGE_HE &&
        language_id != CETTA_LANGUAGE_PETTA && policy->name[0] == '@' &&
        (strcmp(policy->name, "@<") == 0 ||
         strcmp(policy->name, "@<=") == 0 ||
         strcmp(policy->name, "@>") == 0 ||
         strcmp(policy->name, "@>=") == 0))
        return false;
    return cetta_language_visible_in(language_id, profile, policy->visibility_mask);
}

bool cetta_language_allows_builtin(CettaLanguageId language_id,
                                   const CettaProfile *profile,
                                   const char *name) {
    return cetta_language_allows_builtin_policy(
        language_id, profile, cetta_builtin_policy_lookup(name));
}

bool cetta_language_enables_dependent_telescope(CettaLanguageId language_id,
                                                const CettaProfile *profile) {
    if (!profile || !cetta_profile_is_valid_for_language(language_id, profile)) {
        return false;
    }
    return profile->enable_dependent_telescope;
}

bool cetta_language_uses_rust_he_compat_semantics(CettaLanguageId language_id,
                                                  const CettaProfile *profile) {
    return language_id == CETTA_LANGUAGE_HE &&
           profile &&
           cetta_profile_is_valid_for_language(language_id, profile) &&
           profile->rust_he_compat_semantics;
}

/* CeTTa's list syntax [x y] is an extension: Prime and the profiles that
 * enable CeTTa's extensions read it; the compatibility profiles keep the
 * reference tokenization, in which [a,b] is one symbol. */
bool cetta_language_reads_lists(CettaLanguageId language_id,
                                const CettaProfile *profile) {
    if (language_id == CETTA_LANGUAGE_PRIME)
        return true;
    return profile &&
           cetta_profile_is_valid_for_language(language_id, profile) &&
           profile->enable_cetta_extensions;
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
    case CETTA_REMOTE_REVISION_EXPLICIT_COMMIT:
        return "explicit-commit";
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

bool cetta_language_allows_provider_kind(CettaLanguageId language_id,
                                         const CettaProfile *profile,
                                         CettaModuleProviderKind kind) {
    const CettaModuleProviderDescriptor *desc = cetta_module_provider_descriptor(kind);
    if (!desc || !desc->implemented) {
        return false;
    }
    (void)language_id;
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

bool cetta_module_policy_allows(const CettaModulePolicy *policy,
                                CettaModuleProviderFlags provider_flag) {
    return policy && (policy->provider_flags & provider_flag) != 0;
}

void cetta_module_policy_init_for_language_profile(CettaModulePolicy *policy,
                                                   CettaLanguageId language_id,
                                                   const CettaProfile *profile) {
    if (!policy) {
        return;
    }
    policy->provider_flags = 0;
    for (uint32_t i = 0; i < cetta_module_provider_count(); i++) {
        const CettaModuleProviderDescriptor *desc = cetta_module_provider_at(i);
        if (desc &&
            cetta_language_allows_provider_kind(language_id, profile, desc->kind)) {
            policy->provider_flags |= desc->flag;
        }
    }
    const CettaLanguageVersion *version = cetta_language_version_of(language_id, profile);
    policy->relative_module_policy = version ? version->relative_imports
        : cetta_language_relative_module_policy(language_id);
    policy->transactional_imports = true;
}

bool cetta_module_resolver_allows(const CettaModuleResolver *resolver,
                                  CettaModuleProviderFlags provider_flag) {
    return cetta_module_policy_allows(resolver, provider_flag);
}

void cetta_module_resolver_init_for_language_profile(CettaModuleResolver *resolver,
                                                     CettaLanguageId language_id,
                                                     const CettaProfile *profile) {
    cetta_module_policy_init_for_language_profile(resolver, language_id, profile);
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

const CettaEvalOptionEntry *cetta_evaluator_options_find(const CettaEvaluatorOptions *options,
                                                         const char *key) {
    if (!options || !key)
        return NULL;
    for (uint32_t i = 0; i < options->entry_len; i++) {
        if (strcmp(options->entries[i].key, key) == 0)
            return &options->entries[i];
    }
    return NULL;
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

bool cetta_eval_session_set_relative_module_policy(CettaEvalSession *session,
                                                   CettaRelativeModulePolicy policy) {
    if (!session) {
        return false;
    }
    session->module_policy.relative_module_policy = policy;
    return cetta_eval_option_store(&session->options, "import-mode",
                                   CETTA_EVAL_OPTION_VALUE_SYMBOL,
                                   cetta_relative_module_policy_name(policy), 0);
}

CettaRelativeModulePolicy
cetta_eval_session_relative_module_policy(const CettaEvalSession *session) {
    if (!session) {
        return CETTA_RELATIVE_MODULE_POLICY_CURRENT_DIR_ONLY;
    }
    return session->module_policy.relative_module_policy;
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

void cetta_eval_session_clear_process_exit(CettaEvalSession *session) {
    if (!session)
        return;
    session->process_control.exit_requested = false;
    session->process_control.exit_code = 0;
    session->process_control.answer_policy = CETTA_EXIT_DISCARD_ANSWER_DISPLAY;
}

void cetta_eval_session_request_process_exit(CettaEvalSession *session,
                                             int exit_code) {
    cetta_eval_session_request_process_exit_with_answer_policy(
        session, exit_code, CETTA_EXIT_DISCARD_ANSWER_DISPLAY);
}

void cetta_eval_session_request_process_exit_with_answer_policy(
    CettaEvalSession *session, int exit_code, CettaExitAnswerPolicy policy) {
    if (!session || session->process_control.exit_requested)
        return;
    session->process_control.exit_requested = true;
    session->process_control.exit_code = exit_code;
    session->process_control.answer_policy = policy;
}

bool cetta_eval_session_process_exit_requested(
    const CettaEvalSession *session) {
    return session && session->process_control.exit_requested;
}

int cetta_eval_session_process_exit_code(const CettaEvalSession *session) {
    return session && session->process_control.exit_requested
        ? session->process_control.exit_code
        : 0;
}

void cetta_eval_session_init(CettaEvalSession *session,
                             CettaLanguageId language_id,
                             const CettaProfile *profile) {
    session->test_verdict = NULL;
    session->test_verdict_context = NULL;
    session->language_id = language_id;
    session->speculative = false;
    atomic_init(&session->effect_denials, 0);
    session->profile = cetta_profile_is_valid_for_language(language_id, profile)
        ? profile
        : NULL;
    cetta_module_policy_init_for_language_profile(
        &session->module_policy, session->language_id, session->profile);
    cetta_evaluator_options_init(&session->options);
    cetta_eval_session_clear_process_exit(session);
}

void cetta_eval_session_init_he_compat(CettaEvalSession *session) {
    cetta_eval_session_init(session, CETTA_LANGUAGE_HE, cetta_profile_he_compat());
}

void cetta_eval_session_init_he_extended(CettaEvalSession *session) {
    cetta_eval_session_init(session, CETTA_LANGUAGE_HE, cetta_profile_he_extended());
}

void cetta_eval_session_init_he_prime(CettaEvalSession *session) {
    cetta_eval_session_init(session, CETTA_LANGUAGE_HE, cetta_profile_he_prime());
}
