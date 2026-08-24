#ifndef CETTA_GDL_SOURCE_PRESENTATION_H
#define CETTA_GDL_SOURCE_PRESENTATION_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>

/* Authority-free authored GDL syntax and type-profile data.  Parsing builds
 * this language-owned presentation; it does not attach a judgment, verdict,
 * execution mode, or NIK admission. */
typedef struct GdlSourceRawExprV1 GdlSourceRawExprV1;

struct GdlSourceRawExprV1 {
    const char *token;
    GdlSourceRawExprV1 **items;
    size_t count;
};

typedef struct {
    GdlSourceRawExprV1 *form;
    size_t start_line;
    size_t end_line;
    /* Presentation projections select source occurrences in place so their
     * original ordinal and line identity never change.  Parsing selects every
     * form; later language-owned constructions may derive an exact subset. */
    bool selected;
} GdlSourceRawFormV1;

typedef struct {
    GdlSourceRawFormV1 *items;
    size_t count;
    size_t capacity;
    size_t foreign_lines;
} GdlSourceRawFormsV1;

typedef struct {
    size_t statement_ordinal;
    size_t name_ordinal;
    const char *name;
    const char **argument_types;
    size_t argument_count;
    const char *result_type;
} GdlSourceSignatureV1;

typedef struct {
    size_t statement_ordinal;
    const char *subtype;
    const char *supertype;
} GdlSourceSubtypeV1;

typedef struct {
    GdlSourceSignatureV1 *signatures;
    size_t signature_count;
    size_t signature_capacity;
    GdlSourceSubtypeV1 *subtypes;
    size_t subtype_count;
    size_t subtype_capacity;
    size_t statement_count;
} GdlSourceProfileV1;

typedef enum {
    GDL_SOURCE_PARSE_OK_V1 = 0,
    GDL_SOURCE_PARSE_OUTSIDE_FRAGMENT_V1,
    GDL_SOURCE_PARSE_INCOMPLETE_V1,
    GDL_SOURCE_PARSE_ENGINE_FAULT_V1,
} GdlSourceParseV1;

typedef struct {
    const char *source_text;
    const char *profile_text;
    char source_sha256[65];
    char profile_sha256[65];
    char calculus_input_sha256[65];
    char revision[81];
} GdlSourcePackageV1;

typedef struct {
    const char *target_name;
    size_t target_arity;
    size_t source_forms;
    size_t selected_forms;
    size_t reachable_relations;
    size_t external_relations;
} GdlSourceTargetSliceV1;

/* Validate the compact authored source carrier and recompute every identity
 * from its exact source/profile bytes.  Optional pins are all-or-none and do
 * not confer authority; they only constrain which presentation was asked
 * for.  Returned text pointers are borrowed from source_program. */
GdlSourceParseV1 gdl_source_package_view_v1(
    Atom *source_program,
    const char *expected_source_sha256,
    const char *expected_profile_sha256,
    const char *expected_revision,
    GdlSourcePackageV1 *package_out);

GdlSourceParseV1 gdl_source_parse_forms_v1(
    Arena *arena,
    const char *source,
    size_t max_depth,
    GdlSourceRawFormsV1 *forms_out);

GdlSourceParseV1 gdl_source_parse_profile_v1(
    Arena *arena,
    const char *profile,
    GdlSourceProfileV1 *profile_out);

/* Derive the exact dependency-closed presentation fibre for one requested
 * authored relation.  This operation only marks existing source forms; it
 * creates no rule, type, verdict, execution mode, or authority.  Foreign
 * source remains outside because it may define an otherwise missing edge. */
GdlSourceParseV1 gdl_source_select_target_dependency_v1(
    GdlSourceRawFormsV1 *forms,
    const char *target_name,
    size_t target_arity,
    size_t max_relations,
    size_t max_logical_depth,
    GdlSourceTargetSliceV1 *slice_out);

size_t gdl_source_selected_form_count_v1(
    const GdlSourceRawFormsV1 *forms);

/* Content identity of a target-indexed projection.  The full authored source
 * identity remains intact; the target and exact retained occurrence ordinals
 * refine the calculus identity used by admissions and receipts. */
bool gdl_source_target_calculus_input_v1(
    const char *source_calculus_input_sha256,
    const GdlSourceRawFormsV1 *forms,
    const char *target_name,
    size_t target_arity,
    char digest_out[65]);

void gdl_source_raw_forms_free_v1(GdlSourceRawFormsV1 *forms);
void gdl_source_profile_free_v1(GdlSourceProfileV1 *profile);

#endif /* CETTA_GDL_SOURCE_PRESENTATION_H */
