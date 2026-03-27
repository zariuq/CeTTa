#ifndef CETTA_SESSION_H
#define CETTA_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>

#define CETTA_MAX_MODULE_NAMESPACE 128
#define CETTA_MAX_REMOTE_REVISION 128
#define CETTA_MAX_EVAL_OPTION_KEY 64
#define CETTA_MAX_EVAL_OPTION_REPR 128
#define CETTA_MAX_EVAL_OPTIONS 32

typedef struct Space Space;

typedef enum {
    CETTA_PROFILE_HE_COMPAT = 0,
    CETTA_PROFILE_HE_EXTENDED = 1,
    CETTA_PROFILE_HE_PRIME = 2
} CettaProfileId;

typedef enum {
    CETTA_PROFILE_MASK_HE_COMPAT = 1u << 0,
    CETTA_PROFILE_MASK_HE_EXTENDED = 1u << 1,
    CETTA_PROFILE_MASK_HE_PRIME = 1u << 2,
    CETTA_PROFILE_MASK_HE_PUBLIC =
        CETTA_PROFILE_MASK_HE_COMPAT |
        CETTA_PROFILE_MASK_HE_EXTENDED |
        CETTA_PROFILE_MASK_HE_PRIME,
    CETTA_PROFILE_MASK_HE_EXTENDED_PLUS =
        CETTA_PROFILE_MASK_HE_EXTENDED |
        CETTA_PROFILE_MASK_HE_PRIME,
    CETTA_PROFILE_MASK_ALL = CETTA_PROFILE_MASK_HE_PUBLIC
} CettaProfileMask;

typedef struct {
    CettaProfileId id;
    const char *name;
    const char *note;
    bool he_compatible_surface;
    bool enable_cetta_extensions;
    bool enable_dependent_telescope;
} CettaProfile;

typedef enum {
    CETTA_MODULE_PROVIDER_REGISTERED_ROOTS = 1u << 0,
    CETTA_MODULE_PROVIDER_RELATIVE_FILES = 1u << 1,
    CETTA_MODULE_PROVIDER_STDLIB = 1u << 2,
    CETTA_MODULE_PROVIDER_GIT = 1u << 3,
    CETTA_MODULE_PROVIDER_CATALOG = 1u << 4
} CettaModuleProviderFlags;

typedef enum {
    CETTA_MODULE_SPEC_REGISTERED_ROOT = 0,
    CETTA_MODULE_SPEC_RELATIVE_FILE = 1,
    CETTA_MODULE_SPEC_STDLIB = 2,
    CETTA_MODULE_SPEC_MODULE_NAME = 3
} CettaModuleSpecKind;

typedef enum {
    CETTA_MODULE_PROVIDER_REGISTERED_ROOT = 0,
    CETTA_MODULE_PROVIDER_RELATIVE_FILE = 1,
    CETTA_MODULE_PROVIDER_STDLIB_FILE = 2,
    CETTA_MODULE_PROVIDER_GIT_REMOTE = 3,
    CETTA_MODULE_PROVIDER_CATALOG_ENTRY = 4
} CettaModuleProviderKind;

typedef enum {
    CETTA_MODULE_FORMAT_METTA = 0,
    CETTA_MODULE_FORMAT_FOREIGN = 1
} CettaModuleFormatKind;

typedef enum {
    CETTA_FOREIGN_BACKEND_NONE = 0,
    CETTA_FOREIGN_BACKEND_PYTHON = 1
} CettaForeignBackendKind;

typedef struct {
    CettaModuleFormatKind kind;
    CettaForeignBackendKind foreign_backend;
} CettaModuleFormat;

typedef enum {
    CETTA_MODULE_LOCATOR_FILESYSTEM_PATH = 0,
    CETTA_MODULE_LOCATOR_GIT_URL = 1,
    CETTA_MODULE_LOCATOR_CATALOG_KEY = 2
} CettaModuleLocatorKind;

typedef enum {
    CETTA_REMOTE_REVISION_NONE = 0,
    CETTA_REMOTE_REVISION_DEFAULT_BRANCH_ONLY = 1,
    CETTA_REMOTE_REVISION_EXPLICIT_REF_FUTURE = 2,
    CETTA_REMOTE_REVISION_CATALOG_CONTROLLED = 3
} CettaRemoteRevisionPolicy;

typedef struct {
    CettaModuleProviderKind kind;
    const char *name;
    CettaModuleProviderFlags flag;
    bool implemented;
    bool remote_source;
    bool cache_backed;
    CettaModuleLocatorKind locator_kind;
    CettaRemoteRevisionPolicy revision_policy;
    const char *update_policy;
    const char *note;
} CettaModuleProviderDescriptor;

typedef struct {
    CettaModuleSpecKind kind;
    char raw_spec[PATH_MAX];
    char namespace_name[CETTA_MAX_MODULE_NAMESPACE];
    char path_or_member[PATH_MAX];
} CettaModuleSpec;

typedef struct {
    CettaModuleProviderKind provider_kind;
    char namespace_name[CETTA_MAX_MODULE_NAMESPACE];
    char root_path[PATH_MAX];
    CettaModuleLocatorKind locator_kind;
    char source_locator[PATH_MAX];
    CettaRemoteRevisionPolicy revision_policy;
    char revision_value[CETTA_MAX_REMOTE_REVISION];
    uint32_t profile_visibility_mask;
} CettaModuleMount;

typedef struct {
    CettaModuleSpec spec;
    CettaModuleProviderKind provider_kind;
    CettaModuleFormat format;
    char canonical_path[PATH_MAX];
    Space *logical_target_space;
    Space *execution_target_space;
    bool target_is_fresh;
    bool transactional;
} CettaImportPlan;

typedef struct {
    uint32_t provider_flags;
    bool transactional_imports;
} CettaModuleResolver;

typedef enum {
    CETTA_INTERPRETER_DEFAULT = 0,
    CETTA_INTERPRETER_BARE_MINIMAL = 1
} CettaInterpreterMode;

typedef enum {
    CETTA_EVAL_OPTION_VALUE_SYMBOL = 0,
    CETTA_EVAL_OPTION_VALUE_INT = 1,
    CETTA_EVAL_OPTION_VALUE_TEXT = 2
} CettaEvalOptionValueKind;

typedef struct {
    char key[CETTA_MAX_EVAL_OPTION_KEY];
    CettaEvalOptionValueKind kind;
    int64_t int_value;
    char repr[CETTA_MAX_EVAL_OPTION_REPR];
} CettaEvalOptionEntry;

typedef struct {
    bool type_check_auto;
    CettaInterpreterMode interpreter_mode;
    int max_stack_depth;
    int fuel_limit;
    CettaEvalOptionEntry entries[CETTA_MAX_EVAL_OPTIONS];
    uint32_t entry_len;
} CettaEvaluatorOptions;

typedef struct {
    const CettaProfile *profile;
    CettaModuleResolver module_resolver;
    CettaEvaluatorOptions options;
} CettaEvalSession;

typedef struct {
    const char *name;
    uint32_t visibility_mask;
    const char *surface_classification;
} CettaSurfacePolicy;

const CettaProfile *cetta_profile_he_compat(void);
const CettaProfile *cetta_profile_he_extended(void);
const CettaProfile *cetta_profile_he_prime(void);
const CettaProfile *cetta_profile_from_name(const char *name);
uint32_t cetta_profile_mask(const CettaProfile *profile);
bool cetta_profile_visible_in(const CettaProfile *profile, uint32_t visibility_mask);
void cetta_profile_print_inventory(FILE *out);
const CettaSurfacePolicy *cetta_surface_policy_lookup(const char *name);
bool cetta_profile_allows_surface(const CettaProfile *profile, const char *name);
bool cetta_profile_enables_dependent_telescope(const CettaProfile *profile);
uint32_t cetta_module_provider_count(void);
const CettaModuleProviderDescriptor *cetta_module_provider_at(uint32_t index);
const CettaModuleProviderDescriptor *cetta_module_provider_descriptor(CettaModuleProviderKind kind);
const char *cetta_module_provider_name(CettaModuleProviderKind kind);
CettaModuleProviderFlags cetta_module_provider_flag(CettaModuleProviderKind kind);
const char *cetta_module_locator_kind_name(CettaModuleLocatorKind kind);
const char *cetta_remote_revision_policy_name(CettaRemoteRevisionPolicy policy);
bool cetta_module_provider_is_remote(CettaModuleProviderKind kind);
bool cetta_module_provider_is_cache_backed(CettaModuleProviderKind kind);
const char *cetta_module_provider_update_policy(CettaModuleProviderKind kind);
bool cetta_profile_allows_provider_kind(const CettaProfile *profile,
                                        CettaModuleProviderKind kind);

bool cetta_module_resolver_allows(const CettaModuleResolver *resolver,
                                  CettaModuleProviderFlags provider_flag);
void cetta_module_resolver_init_for_profile(CettaModuleResolver *resolver,
                                            const CettaProfile *profile);
void cetta_evaluator_options_init(CettaEvaluatorOptions *options);
bool cetta_evaluator_options_is_bare_minimal(const CettaEvaluatorOptions *options);
int cetta_evaluator_options_effective_fuel_limit(const CettaEvaluatorOptions *options);
bool cetta_eval_session_set_type_check_auto(CettaEvalSession *session, bool enabled);
bool cetta_eval_session_set_interpreter_mode(CettaEvalSession *session,
                                             CettaInterpreterMode mode);
bool cetta_eval_session_set_max_stack_depth(CettaEvalSession *session, int depth);
void cetta_eval_session_set_fuel_limit(CettaEvalSession *session, int fuel_limit);
bool cetta_eval_session_record_generic_setting(CettaEvalSession *session,
                                               const char *key,
                                               CettaEvalOptionValueKind kind,
                                               const char *repr,
                                               int64_t int_value);
void cetta_eval_session_init(CettaEvalSession *session, const CettaProfile *profile);
void cetta_eval_session_init_he_compat(CettaEvalSession *session);
void cetta_eval_session_init_he_extended(CettaEvalSession *session);
void cetta_eval_session_init_he_prime(CettaEvalSession *session);

#endif /* CETTA_SESSION_H */
