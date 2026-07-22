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
 * Every admitted source rule becomes one ground source-rule/2 fact. */
bool fhgslt_package_reflected_presentation(const FHGSLTPackage *package,
                                           uint8_t **out,
                                           size_t *out_len,
                                           char *error,
                                           size_t error_cap);

bool fhgslt_package_digest(const FHGSLTPackage *package,
                           char out[65],
                           char *error,
                           size_t error_cap);

#endif
