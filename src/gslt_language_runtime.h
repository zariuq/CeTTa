#ifndef CETTA_GSLT_LANGUAGE_RUNTIME_H
#define CETTA_GSLT_LANGUAGE_RUNTIME_H

#include "atom.h"
#include "gslt_compiled_runtime.h"
#include "gslt_horn_runtime.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct CettaGsltLanguage CettaGsltLanguage;

typedef struct {
    CettaGsltHornInput input;
    const char *sha256;
} CettaGsltEmbeddedSourceV1;

/* A staged document runner.  Each relation has a fixed generic ABI:
 *
 *   classify(source-occurrence, quoted-form, request)
 *   produce(program, request, evidence, quoted-result)
 *   observe(request, produced-bag, evidence, quoted-result)
 *
 * The producer must close before its bag is passed to the observer.  This
 * makes empty-bag observations depend on completion evidence rather than on
 * the transient absence of rows. */
typedef struct {
    const char *classify_relation;
    const char *produce_relation;
    const char *observe_relation;
    const char *produced_nil;
    const char *produced_cons;
} CettaGsltRequestPipelineV1;

typedef struct {
    const char *name;
    /* NULL selects the authored base.  A non-NULL name selects one manifest
     * profile whose semantic sources extend the base in declaration order. */
    const char *profile_name;
    const char *syntax_backend;
    const char *term_abi;
    CettaGsltEmbeddedSourceV1 manifest;
    const CettaGsltEmbeddedSourceV1 *semantic_sources;
    size_t semantic_source_count;
    CettaGsltCompiledInputV1 compiled_plan;
    const char *program_nil;
    const char *program_cons;
    const char *entry_relation;
    uint32_t entry_arity;
    uint32_t program_position;
    uint32_t result_position;
    const char *query_relation;
    uint32_t query_arity;
    const CettaGsltRequestPipelineV1 *request_pipeline;
    const char *observation;
    const char *manifest_sha256;
    const char *compiler_sha256;
} CettaGsltEmbeddedLanguageV1;

typedef enum {
    CETTA_GSLT_REALIZATION_HORN_REFERENCE = 0,
    CETTA_GSLT_REALIZATION_COMPILED_WORKLIST = 1,
} CettaGsltRealization;

typedef struct {
    CettaGsltHornOutcome outcome;
    Atom **answers;
    /* Observation evidence is aligned with answers when the authored
     * request pipeline supplies it.  Entry-only languages leave this NULL. */
    Atom **evidence;
    size_t answer_count;
    uint64_t rule_attempts;
    uint64_t rule_matches;
    uint64_t rule_dispatch_rejects;
    uint64_t rule_outer_head_elisions;
    uint64_t rule_prefilter_rejects;
    uint64_t rule_ground_dense_attempts;
    uint64_t rule_flat_head_attempts;
    uint64_t rule_general_head_attempts;
    uint64_t rule_constructor_guided_attempts;
    uint64_t rule_constructor_guided_matches;
    uint64_t rule_constructor_nodes_elided;
    uint64_t rule_flat_head_matches;
    uint64_t rule_ground_dense_matches;
    uint64_t rule_variable_slot_buffer_uses;
    uint64_t rule_variable_slot_bytes_elided;
    uint64_t rule_variable_slot_clear_bytes_elided;
    uint64_t rule_ground_subterm_cache_hits;
    uint64_t rule_ground_subterm_nodes_elided;
    uint64_t worklist_states_created;
    uint64_t worklist_states_reclaimed;
    uint64_t worklist_pending_peak;
    uint64_t worklist_state_bytes_peak;
    uint32_t max_depth_observed;
} CettaGsltLanguageResult;

bool cetta_gslt_language_load_manifest(
    const char *manifest_path, CettaGsltLanguage **out,
    char *error, size_t error_size);

bool cetta_gslt_language_load_embedded(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    CettaGsltLanguage **out, char *error, size_t error_size);

bool cetta_gslt_language_load_embedded_for_realization(
    const CettaGsltEmbeddedLanguageV1 *descriptor,
    CettaGsltRealization realization,
    CettaGsltLanguage **out, char *error, size_t error_size);

void cetta_gslt_language_free(CettaGsltLanguage *language);

const char *cetta_gslt_language_name(const CettaGsltLanguage *language);
const char *cetta_gslt_language_syntax_backend(
    const CettaGsltLanguage *language);
const char *cetta_gslt_language_observation(
    const CettaGsltLanguage *language);
size_t cetta_gslt_language_semantic_rule_count(
    const CettaGsltLanguage *language);

bool cetta_gslt_language_execute_atoms(
    const CettaGsltLanguage *language,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size);

bool cetta_gslt_language_execute_atoms_with_realization(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size);

/* Execute through a provider catalog compiled for this exact language
 * manifest/profile.  The physical registry may contain implementations for
 * other catalogs; only matching authored declarations become visible. */
bool cetta_gslt_language_execute_atoms_with_realization_and_providers_v1(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    const CettaGsltProviderCatalogV1 *catalog,
    const CettaGsltProviderRegistryV1 *physical_providers,
    Atom *const *forms, size_t form_count,
    Arena *output_arena, CettaGsltHornLimits limits,
    CettaGsltLanguageResult *result,
    char *error, size_t error_size);

/* Execute one admitted relation exposed by a `(query-entry ...)` manifest.
 * This is the internal-service counterpart of whole-document execution: the
 * caller supplies the complete query and receives the raw relation answers. */
bool cetta_gslt_language_query_v1(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    Arena *output_arena, Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error, size_t error_size);

/* Provider-backed counterpart of query_v1.  The catalog must target this
 * exact manifest and realization; only catalog-authorized physical relations
 * are visible to the query. */
bool cetta_gslt_language_query_with_providers_v1(
    const CettaGsltLanguage *language,
    CettaGsltRealization realization,
    const CettaGsltProviderCatalogV1 *catalog,
    const CettaGsltProviderRegistryV1 *physical_providers,
    Arena *output_arena, Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error, size_t error_size);

const char *cetta_gslt_realization_name(CettaGsltRealization realization);
bool cetta_gslt_realization_parse(
    const char *name, CettaGsltRealization *realization);

void cetta_gslt_language_result_free(CettaGsltLanguageResult *result);

#endif /* CETTA_GSLT_LANGUAGE_RUNTIME_H */
