#ifndef CETTA_GSLT_HORN_RUNTIME_H
#define CETTA_GSLT_HORN_RUNTIME_H

#include "atom.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaGsltHornProgram CettaGsltHornProgram;

/* Read-only normalized view of one admitted source-rule occurrence.  Rule
 * names are globally unique inside a composed finite-Horn package and serve
 * as its stable occurrence identities. */
typedef struct {
    const char *name;
    const Atom *head;
    const Atom *const *body;
    size_t body_count;
} CettaGsltHornRuleViewV1;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    const char *source;
} CettaGsltHornInput;

typedef struct {
    uint64_t max_rule_attempts;
    uint64_t max_answers;
    uint32_t max_depth;
} CettaGsltHornLimits;

typedef enum {
    CETTA_GSLT_HORN_COMPLETED = 0,
    CETTA_GSLT_HORN_RULE_LIMIT,
    CETTA_GSLT_HORN_ANSWER_LIMIT,
    CETTA_GSLT_HORN_DEPTH_LIMIT,
    CETTA_GSLT_HORN_FAULT,
} CettaGsltHornOutcome;

typedef struct {
    CettaGsltHornOutcome outcome;
    Atom **answers;
    size_t answer_count;
    uint64_t rule_attempts;
    uint64_t rule_matches;
    uint32_t max_depth_observed;
} CettaGsltHornResult;

bool cetta_gslt_horn_program_load_paths(
    const char *const *paths, size_t path_count,
    CettaGsltHornProgram **out, char *error, size_t error_size);

bool cetta_gslt_horn_program_load_inputs(
    const CettaGsltHornInput *inputs, size_t input_count,
    CettaGsltHornProgram **out, char *error, size_t error_size);

void cetta_gslt_horn_program_free(CettaGsltHornProgram *program);

size_t cetta_gslt_horn_program_rule_count(
    const CettaGsltHornProgram *program);

bool cetta_gslt_horn_program_rule_view_v1(
    const CettaGsltHornProgram *program, size_t index,
    CettaGsltHornRuleViewV1 *view);

bool cetta_gslt_horn_query(
    const CettaGsltHornProgram *program, Arena *output_arena,
    Atom *query, CettaGsltHornLimits limits,
    CettaGsltHornResult *result, char *error, size_t error_size);

void cetta_gslt_horn_result_free(CettaGsltHornResult *result);

/* Shared finite-Horn quotation ABI for host Atoms.  Quotation preserves
 * repeated variables by occurrence-local de Bruijn indices; unknown grounded
 * values remain opaque data. */
Atom *cetta_gslt_quote_atom_v1(Arena *arena, const Atom *atom);
Atom *cetta_gslt_unquote_atom_v1(Arena *arena, const Atom *quoted);

#endif /* CETTA_GSLT_HORN_RUNTIME_H */
