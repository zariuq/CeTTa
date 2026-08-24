#include "session.h"

#include <stdio.h>
#include <string.h>

static const CettaProfile CETTA_PROFILE_HE_FORMAL_VALUE = {
    .id = CETTA_PROFILE_HE_FORMAL,
    .language_id = CETTA_LANGUAGE_HE,
    .name = "he",
    .note = "Formal/spec-faithful HE base for OSLF/NTT/DTT work.",
    .he_compatible_builtin = true,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_HE_COMPAT_VALUE = {
    .id = CETTA_PROFILE_HE_COMPAT,
    .language_id = CETTA_LANGUAGE_HE,
    .name = "he-compat",
    .note = "Rust Hyperon Experimental HE 0.2.10 compatibility lane.",
    .he_compatible_builtin = true,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = true,
};

static const CettaProfile CETTA_PROFILE_HE_EXTENDED_VALUE = {
    .id = CETTA_PROFILE_HE_EXTENDED,
    .language_id = CETTA_LANGUAGE_HE,
    .name = "he-extended",
    .note = "HE-compatible syntax plus labeled CeTTa extensions.",
    .he_compatible_builtin = true,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_HE_PRIME_VALUE = {
    .id = CETTA_PROFILE_HE_PRIME,
    .language_id = CETTA_LANGUAGE_HE,
    .name = "he-prime",
    .note = "HE typing with explicit dependent binders, checked type-level computation, and typed inhabitation search.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = true,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_PRIME_DEFAULT_VALUE = {
    .id = CETTA_PROFILE_PRIME_DEFAULT,
    .language_id = CETTA_LANGUAGE_PRIME,
    .name = "prime-default",
    .note = "Built-in policy for the MeTTa-Prime semantic package.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = true,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_PETTA_EXTENDED_VALUE = {
    .id = CETTA_PROFILE_PETTA_EXTENDED,
    .language_id = CETTA_LANGUAGE_PETTA,
    .name = "extended",
    .note = "PeTTa semantics plus labeled shared CeTTa extensions.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
static const CettaProfile CETTA_PROFILE_PETTA_TYPECHECK_V2_VALUE = {
    .id = CETTA_PROFILE_PETTA_TYPECHECK_V2,
    .language_id = CETTA_LANGUAGE_PETTA,
    .name = "typecheck-v2",
    .note = "PeTTa with native Roman-compatible residual type checking.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_PETTA_TYPECHECK_V3_VALUE = {
    .id = CETTA_PROFILE_PETTA_TYPECHECK_V3,
    .language_id = CETTA_LANGUAGE_PETTA,
    .name = "typecheck-v3",
    .note = "PeTTa with native v3 judgments and coherent typecheck-v2 delegation.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};
#endif

static const CettaProfile CETTA_PROFILE_RHOCALC_STRICT_CORE_VALUE = {
    .id = CETTA_PROFILE_RHOCALC_STRICT_CORE,
    .language_id = CETTA_LANGUAGE_RHOCALC,
    .name = "strict-core",
    .note = "Strict-core rho-calculus without cost-layer extensions.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_RHOCALC_COST_VALUE = {
    .id = CETTA_PROFILE_RHOCALC_COST,
    .language_id = CETTA_LANGUAGE_RHOCALC,
    .name = "cost",
    .note = "Meredith cost-accounted rho extension (ground-signature token-gated slice).",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_RHOCALC_RHOMETTA_VALUE = {
    .id = CETTA_PROFILE_RHOCALC_RHOMETTA,
    .language_id = CETTA_LANGUAGE_RHOCALC,
    .name = "rhometta",
    .note = "Rho-calculus with inert MeTTa values and transactional COMM-time payload evaluation.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = true,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_ZERO_EXP_VALUE = {
    .id = CETTA_PROFILE_ZERO_EXP,
    .language_id = CETTA_LANGUAGE_ZERO,
    .name = "exp",
    .note = "Experimental authored semantic work and continuation layer.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_ZERO_EMIT_VALUE = {
    .id = CETTA_PROFILE_ZERO_EMIT,
    .language_id = CETTA_LANGUAGE_ZERO,
    .name = "emit",
    .note = "Experimental revision-threaded match/let/eval/emit profile; add-atom is the persistent-emission compatibility spelling.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_ZERO_INTERACT_VALUE = {
    .id = CETTA_PROFILE_ZERO_INTERACT,
    .language_id = CETTA_LANGUAGE_ZERO,
    .name = "interact",
    .note = "Experimental match/let/eval/emit profile over authenticated immutable space revisions; add-atom is an emit alias.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaProfile CETTA_PROFILE_MM2_GSLT_VALUE = {
    .id = CETTA_PROFILE_MM2_GSLT,
    .language_id = CETTA_LANGUAGE_MM2,
    .name = "gslt",
    .note = "Authored strict support-transform GSLT with open-world directives and exact fuel.",
    .he_compatible_builtin = false,
    .enable_cetta_extensions = false,
    .enable_dependent_telescope = false,
    .rust_he_compat_semantics = false,
};

static const CettaBuiltinPolicy CETTA_BUILTIN_POLICIES[] = {
    {"_minimal-foldl-atom", CETTA_PROFILE_MASK_ALL, "compat_alias"},
    {"foldl-atom-in-space", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"foldl-atom", CETTA_PROFILE_MASK_ALL, "compat_alias"},
    {"filter-atom", CETTA_PROFILE_MASK_ALL, "compat_alias"},
    {"range-atom", CETTA_PROFILE_MASK_HE_NON_COMPAT, "clean_primary_extension"},
    {"repeat-atom", CETTA_PROFILE_MASK_HE_NON_COMPAT, "clean_primary_extension"},
    {"add-atom-nodup", CETTA_PROFILE_MASK_HE_NON_COMPAT, "clean_primary_extension"},
    {"count-atoms", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "extension_only"},
    {"module-inventory!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"runtime-stats!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"reset-runtime-stats!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"register-module!", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"git-module!", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"import!", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"include", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"include-space-target", CETTA_PROFILE_MASK_HE_NON_COMPAT, "clean_primary_extension"},
    {"mod-space!", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"print-mods!", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"capture", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"quote", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"unquote", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"sealed", CETTA_PROFILE_MASK_ALL, "keep_he_public_builtin"},
    {"collect", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "compat_alias"},
    {"fold", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"fold-by-key", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"reduce", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "compat_alias"},
    {"select", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"hyperpose", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"once", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "compat_alias"},
    {"singleton-visible-witness", CETTA_PROFILE_MASK_ALL, "translator_compat_builtin"},
    {"search-policy", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"new-space-kind", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"with-space-snapshot", CETTA_PROFILE_MASK_HE_NON_COMPAT, "clean_primary_extension"},
    {"space-set-backend!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-set-match-backend!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "compat_alias"},
    {"size", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-len", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-push", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-peek", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-pop", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-get", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"space-truncate", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
    {"step!", CETTA_PROFILE_MASK_HE_EXTENDED_PLUS, "clean_primary_extension"},
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

static uint32_t cetta_language_base_builtin_mask(CettaLanguageId language_id) {
    switch (language_id) {
    case CETTA_LANGUAGE_HE:
        return CETTA_PROFILE_MASK_HE_FORMAL;
    case CETTA_LANGUAGE_PRIME:
        return CETTA_PROFILE_MASK_HE_PRIME;
    case CETTA_LANGUAGE_MM2:
    case CETTA_LANGUAGE_PETTA:
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

bool cetta_language_has_named_profiles(CettaLanguageId language_id) {
    return language_id == CETTA_LANGUAGE_HE ||
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
        return CETTA_PROFILE_MASK_HE_PRIME;
    case CETTA_PROFILE_RHOCALC_STRICT_CORE:
    case CETTA_PROFILE_RHOCALC_COST:
    case CETTA_PROFILE_RHOCALC_RHOMETTA:
    case CETTA_PROFILE_PETTA_EXTENDED:
    case CETTA_PROFILE_ZERO_EXP:
    case CETTA_PROFILE_ZERO_EMIT:
    case CETTA_PROFILE_ZERO_INTERACT:
    case CETTA_PROFILE_MM2_GSLT:
        return CETTA_PROFILE_MASK_ALL;
    case CETTA_PROFILE_PETTA_TYPECHECK_V2:
    case CETTA_PROFILE_PETTA_TYPECHECK_V3:
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
        return CETTA_PROFILE_MASK_ALL;
#else
        return 0u;
#endif
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
    size_t count = sizeof(CETTA_BUILTIN_POLICIES) / sizeof(CETTA_BUILTIN_POLICIES[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(CETTA_BUILTIN_POLICIES[i].name, name) == 0) {
            return &CETTA_BUILTIN_POLICIES[i];
        }
    }
    return NULL;
}

bool cetta_language_allows_builtin(CettaLanguageId language_id,
                                   const CettaProfile *profile,
                                   const char *name) {
    const CettaBuiltinPolicy *policy = cetta_builtin_policy_lookup(name);
    if (!policy) return true;
    return cetta_language_visible_in(language_id, profile, policy->visibility_mask);
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
    policy->relative_module_policy = cetta_language_relative_module_policy(language_id);
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
}

void cetta_eval_session_request_process_exit(CettaEvalSession *session,
                                             int exit_code) {
    if (!session || session->process_control.exit_requested)
        return;
    session->process_control.exit_requested = true;
    session->process_control.exit_code = exit_code;
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
    session->language_id = language_id;
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
