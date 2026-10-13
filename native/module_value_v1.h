#ifndef CETTA_MODULE_VALUE_V1_H
#define CETTA_MODULE_VALUE_V1_H

#include "lbnf_document_v1.h"
#include "gslt_composition_v1.h"

/* Historical module declarations are ordinary immutable data. This
 * frontend consumes the existing parser's labelled trees and introduces no
 * text parser. Source locations are held in a separate ordered source map. */
bool cetta_module_value_v1_project(
    Arena *, Atom *source_namespace, Atom *labelled_ast, uint32_t work_limit,
    Atom **value, Atom **refusal);
bool cetta_module_value_v1_finish_read(
    Arena *, Atom *source_namespace, Atom *parser_result, uint32_t work_limit,
    Atom **value, Atom **refusal);

/* The instance key is explicit portable data. Nested calls mint distinct
 * paths below that key; supplied presentations and let bindings retain their
 * existing origins. A catalog contains already-read module values. */
bool cetta_module_value_v1_assemble(
    Arena *, Atom *module, Atom *theory_path, Atom *arguments, Atom *catalog,
    CettaGsltOriginPolicyV1, Atom *instance_key, uint32_t work_limit,
    Atom **presentation, Atom **refusal);

/* Construct the authoritative core without requesting a concrete parser.
 * Lexical policy and parser capabilities are separate consumer decisions. */
bool cetta_module_value_v1_core(
    Arena *, Atom *presentation, Atom *name, uint32_t work_limit,
    Atom **language, Atom **refusal);

/* Partial interpretation search over two presentations. Found carries the
 * checked declaration correspondence. Unknown describes this method's limit;
 * it is not a proof that no interpretation exists. */
bool cetta_module_value_v1_interpret(
    Arena *, Atom *required, Atom *supplied, Atom *method, uint32_t work_limit,
    Atom **result, Atom **refusal);

/* Ordered rule occurrences retain their module origins. This is a value
 * handoff, not installation in the host or a proof-search policy. */
bool cetta_module_value_v1_rewrite_rules(
    Arena *, Atom *presentation, Atom *name, uint32_t work_limit,
    Atom **rules, Atom **refusal);

bool cetta_module_value_v1_language(
    Arena *, Atom *presentation, Atom *name, Atom *start, Atom *lexical_policy,
    uint32_t work_limit, CettaLbnfLoweredV1 *syntax,
    Atom **complete_language, Atom **refusal);

#endif
