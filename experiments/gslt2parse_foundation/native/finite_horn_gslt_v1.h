#ifndef FINITE_HORN_GSLT_V1_H
#define FINITE_HORN_GSLT_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct FHGSLTPackage FHGSLTPackage;

typedef struct {
    const uint8_t *bytes;
    size_t len;
    const char *source;
} FHGSLTInput;

typedef struct {
    size_t rule_count;
    size_t fact_rule_count;
    size_t implication_rule_count;
    size_t body_goal_count;
    size_t maximum_body_goal_count;
    size_t left_linear_head_rule_count;
    size_t nonlinear_head_rule_count;
    size_t multi_body_rule_count;
    size_t cross_goal_join_rule_count;
    size_t body_only_variable_rule_count;
    size_t head_only_variable_rule_count;
    size_t direct_recursive_rule_count;
} FHGSLTStructuralShapeV1;

bool fhgslt_package_from_inputs(const FHGSLTInput *inputs,
                                size_t input_count,
                                FHGSLTPackage **out,
                                char *error,
                                size_t error_cap);

bool fhgslt_package_from_paths(const char *const *paths,
                               size_t path_count,
                               FHGSLTPackage **out,
                               char *error,
                               size_t error_cap);

void fhgslt_package_free(FHGSLTPackage *package);

size_t fhgslt_package_presentation_count(const FHGSLTPackage *package);
size_t fhgslt_package_operator_count(const FHGSLTPackage *package);
size_t fhgslt_package_rule_count(const FHGSLTPackage *package);

/* Inspect backend-relevant syntactic structure without assigning execution
 * semantics or deciding whether a particular backend can admit the package. */
bool fhgslt_package_structural_shape_v1(
    const FHGSLTPackage *package,
    FHGSLTStructuralShapeV1 *out,
    char *error,
    size_t error_cap);

bool fhgslt_package_declares_operator(const FHGSLTPackage *package,
                                      const char *name,
                                      size_t arity);

bool fhgslt_package_canonical_presentation(const FHGSLTPackage *package,
                                           size_t index,
                                           uint8_t **out,
                                           size_t *out_len,
                                           char *error,
                                           size_t error_cap);

bool fhgslt_package_quoted_rules(const FHGSLTPackage *package,
                                 size_t index,
                                 uint8_t **out,
                                 size_t *out_len,
                                 char *error,
                                 size_t error_cap);

/* Derive the canonical finite-Horn reflection used by compiler GSLTs.
 * Every admitted operator declaration becomes one ground source-operator/3
 * fact, and every admitted source rule becomes one ground source-rule/2 fact. */
bool fhgslt_package_reflected_presentation(const FHGSLTPackage *package,
                                           uint8_t **out,
                                           size_t *out_len,
                                           char *error,
                                           size_t error_cap);

/* Reflect only presentations originating in selected source files, while
 * validating and digest-binding them as members of the complete package.
 * This is the composition-aware form used when a presentation imports
 * operators declared by another component of the package. */
bool fhgslt_package_reflected_sources(const FHGSLTPackage *package,
                                      const char *const *sources,
                                      size_t source_count,
                                      uint8_t **out,
                                      size_t *out_len,
                                      size_t *operator_count,
                                      size_t *rule_count,
                                      char *error,
                                      size_t error_cap);

bool fhgslt_package_digest(const FHGSLTPackage *package,
                           char out[65],
                           char *error,
                           size_t error_cap);

/* Render every admitted source rule into the canonical finite-Horn clause IR.
 * The result is canonical across input-path order and carries the canonical
 * source-package digest as an ordinary queryable Horn fact. */
bool fhgslt_package_horn_clause_ir_v1(const FHGSLTPackage *package,
                                      uint8_t **out,
                                      size_t *out_len,
                                      char *error,
                                      size_t error_cap);

#endif
