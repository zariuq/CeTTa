#ifndef CETTA_LBNF_DOCUMENT_V1_H
#define CETTA_LBNF_DOCUMENT_V1_H

#include "src/atom.h"

typedef struct {
    Atom *language;
    Atom *profile;
    Atom *policy;
    Atom *origins;
} CettaLbnfLoweredV1;

/* Projection consumes labelled AST data, not source text. Lowering consumes
 * a source-spanned LBNF document and produces the existing syntax carrier.
 * Both operations leave their output unchanged on refusal. */
bool cetta_lbnf_document_v1_project(
    Arena *arena, Atom *namespace_value, Atom *ast, uint32_t work_limit,
    Atom **out, Atom **refusal);

/* Shared declaration projection for language-definition frontends whose
 * Terms blocks use the same LBNF pragmas. The input is labelled parser data. */
bool cetta_lbnf_definition_v1_project(
    Arena *arena, Atom *ast, uint32_t work_limit,
    Atom **out, Atom **refusal);

/* Finish a singleton reader result without interpreting its constructors.
 * Ambiguous results and their complete packed evidence remain unchanged. */
bool cetta_lbnf_document_v1_finish_read(
    Arena *arena, Atom *result, uint32_t work_limit,
    Atom **out, Atom **refusal);

bool cetta_lbnf_document_v1_lower(
    Arena *arena, Atom *document, Atom *name, Atom *start, Atom *policy,
    uint32_t work_limit, CettaLbnfLoweredV1 *out, Atom **refusal);

/* Expand the shared declaration notation into LanguageDef fields. This is
 * independent of lexical policy, entrypoints and parser preparation: declared
 * sorts need not have concrete productions, and an empty signature is valid.
 * Lexical declarations remain in the source document for its syntax consumer.
 * production_sites, when non-NULL, receives the source definition site of
 * each expanded term row, in the same order. */
bool cetta_lbnf_document_v1_declarations(
    Arena *arena, Atom *document, Atom *name, uint32_t *remaining_work,
    Atom **language, Atom **production_sites, Atom **refusal);

bool cetta_lbnf_document_v1_bootstrap(Arena *arena, Atom **out,
                                     char *error, size_t error_size);

#endif
