#ifndef CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_V1_H
#define CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

/*
 * A compiled projection plan maps neutral S-expression syntax nodes to host
 * Atoms.  All node names, carrier names, text decoders, and escape tables are
 * presentation data; this executor contains no guest-language vocabulary.
 */
typedef struct {
    void *implementation;
} PPAtomProjectionV1Plan;

typedef struct {
    const char *profile;
    const char *source_digest;
    const char *reader_digest;
    const char *compiler_digest;
    const char *plan_digest;
} PPAtomProjectionV1ProvenanceView;

typedef enum {
    PPATOM_PROJECTION_V1_RULE_TEXT = 0,
    PPATOM_PROJECTION_V1_RULE_VARIABLE = 1
} PPAtomProjectionV1RuleKind;

typedef enum {
    PPATOM_PROJECTION_V1_OUTPUT_SYMBOL = 0,
    PPATOM_PROJECTION_V1_OUTPUT_STRING = 1
} PPAtomProjectionV1OutputKind;

typedef enum {
    PPATOM_PROJECTION_V1_TEXT_RAW = 0,
    PPATOM_PROJECTION_V1_TEXT_DECODED = 1,
    PPATOM_PROJECTION_V1_TEXT_DECODED_QUOTED = 2
} PPAtomProjectionV1TextMode;

/* Borrowed, immutable carrier identities for projection specialization. */
typedef struct {
    const char *node_label;
    const char *pair_label;
    const char *cons_label;
    const char *nil_label;
    const char *codepoint_label;
    const char *document_label;
    const char *expression_label;
} PPAtomProjectionV1CarrierView;

/* Borrowed, immutable leaf rule valid while its loaded plan remains alive. */
typedef struct {
    PPAtomProjectionV1RuleKind kind;
    const char *source_label;
    PPAtomProjectionV1OutputKind output_kind;
    PPAtomProjectionV1TextMode text_mode;
} PPAtomProjectionV1RuleView;

typedef enum {
    PPATOM_PROJECTION_V1_WRAPPER_MAP = 0,
    PPATOM_PROJECTION_V1_WRAPPER_HEX = 1
} PPAtomProjectionV1WrapperKind;

typedef enum {
    PPATOM_PROJECTION_V1_MAP_REJECT = 0,
    PPATOM_PROJECTION_V1_MAP_IDENTITY = 1
} PPAtomProjectionV1MapDefault;

typedef struct {
    uint32_t source;
    uint32_t target;
} PPAtomProjectionV1CodepointMapView;

/* Borrowed, immutable view valid only while its loaded plan remains alive. */
typedef struct {
    PPAtomProjectionV1WrapperKind kind;
    const char *label;
    PPAtomProjectionV1MapDefault map_default;
    const PPAtomProjectionV1CodepointMapView *mappings;
    uint32_t mapping_len;
    uint32_t min_digits;
    uint32_t max_digits;
    uint32_t max_value;
} PPAtomProjectionV1WrapperView;

void ppatom_projection_v1_plan_init(PPAtomProjectionV1Plan *plan);
void ppatom_projection_v1_plan_free(PPAtomProjectionV1Plan *plan);

/* Failed replacement is atomic: a previously loaded plan remains usable. */
bool ppatom_projection_v1_plan_load(
    PPAtomProjectionV1Plan *plan,
    const Atom *compiled_plan,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_v1_plan_provenance(
    const PPAtomProjectionV1Plan *plan,
    PPAtomProjectionV1ProvenanceView *out,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_v1_plan_carriers(
    const PPAtomProjectionV1Plan *plan,
    PPAtomProjectionV1CarrierView *out,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_v1_plan_rule_count(
    const PPAtomProjectionV1Plan *plan,
    uint32_t *out,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_v1_plan_rule(
    const PPAtomProjectionV1Plan *plan,
    uint32_t index,
    PPAtomProjectionV1RuleView *out,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_v1_plan_wrapper_count(
    const PPAtomProjectionV1Plan *plan,
    uint32_t *out,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_v1_plan_wrapper(
    const PPAtomProjectionV1Plan *plan,
    uint32_t index,
    PPAtomProjectionV1WrapperView *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Apply one build-time-bound text wrapper directly to its scalar payload.
 * Map and hexadecimal semantics remain projection-plan data; deterministic
 * parser backends therefore need no guest escape vocabulary.
 */
bool ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
    const PPAtomProjectionV1Plan *plan,
    uint32_t wrapper_index,
    const uint32_t *source,
    uint32_t source_len,
    uint32_t *out_scalar,
    char *error_buf,
    size_t error_buf_size);

/*
 * Project one completed ParserPack semantic result to a top-level Atom
 * stream.  The output array and every Atom belong to output_arena.  Variable
 * identity is shared by spelling within one top-level Atom and reset between
 * top-level Atoms, matching an ordinary document reader boundary.
 */
bool ppatom_projection_v1_project_document(
    const PPAtomProjectionV1Plan *plan,
    const Atom *semantic_result,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size);

#endif
