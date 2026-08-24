#ifndef CETTA_GSLT_COMPOSITION_V1_H
#define CETTA_GSLT_COMPOSITION_V1_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *presentation_name;
    const char *name;
    size_t arity;
} CettaGsltOperatorV1;

typedef struct {
    const char *presentation_name;
    Atom *term;
} CettaGsltEquationV1;

typedef struct {
    const char *presentation_name;
    const char *name;
    Atom *head;
    Atom *body;
} CettaGsltRewriteV1;

typedef struct {
    const char *name;
    size_t operator_begin;
    size_t operator_count;
    size_t equation_begin;
    size_t equation_count;
    size_t rewrite_begin;
    size_t rewrite_count;
} CettaGsltPresentationV1;

typedef struct {
    CettaGsltPresentationV1 *presentations;
    size_t presentation_count;
    CettaGsltOperatorV1 *operators;
    size_t operator_count;
    CettaGsltEquationV1 *equations;
    size_t equation_count;
    CettaGsltRewriteV1 *rewrites;
    size_t rewrite_count;
} CettaGsltCompositionV1;

/* Read an ordered collection of authored GSLT presentations without choosing
 * an execution strategy or target language.  Atom storage remains borrowed
 * from the caller; the returned index arrays are owned by the composition. */
bool cetta_gslt_composition_build_v1(
    Atom *const *presentations,
    size_t presentation_count,
    CettaGsltCompositionV1 *composition_out,
    char *error,
    size_t error_size);

/* Bind the ordered authored presentation sequence before any backend choice.
 * Every direct target uses this identity; target-specific rendering is not
 * part of the digest. */
bool cetta_gslt_composition_digest_v1(
    Atom *const *presentations,
    size_t presentation_count,
    char digest_out[65],
    char *error,
    size_t error_size);

void cetta_gslt_composition_free_v1(CettaGsltCompositionV1 *composition);

bool cetta_gslt_composition_has_operator_v1(
    const CettaGsltCompositionV1 *composition,
    const char *name,
    size_t arity);

bool cetta_gslt_source_variable_v1(const Atom *atom);
const char *cetta_gslt_source_variable_name_v1(const Atom *atom);

bool cetta_gslt_composition_validate_term_v1(
    const CettaGsltCompositionV1 *composition,
    const Atom *term,
    size_t depth_limit,
    char *error,
    size_t error_size);

/* Select named authored rewrites and then retain every rewrite defining a
 * relation called from a selected body.  The returned byte mask follows
 * composition rewrite order and is owned by the caller.  An empty entry list
 * selects the complete composition. */
bool cetta_gslt_composition_select_rewrite_closure_v1(
    const CettaGsltCompositionV1 *composition,
    const char *const *entry_rule_names,
    size_t entry_rule_count,
    uint8_t **selected_rewrites_out,
    size_t *selected_rewrite_count_out,
    char *error,
    size_t error_size);

#endif
