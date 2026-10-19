#ifndef CETTA_REGEX_SOURCE_V1_H
#define CETTA_REGEX_SOURCE_V1_H
#include "src/atom.h"

/* Project labelled syntax produced by RegexSyntax.module. This does not
 * scan pattern text, select a parser derivation or install host equations.
 * A non-singleton parse remains an explicit parser observation. Zero depth
 * or work means no caller-imposed bound; traversal uses an explicit stack. */
Atom *cetta_re_v1_lower_source(Arena *, Atom *parser_observation,
                              uint32_t depth_limit, uint64_t work_limit);
Atom *cetta_re_v1_lower_source_policy(Arena *, Atom *parser_observation,
    bool reverse_lookbehind, uint32_t depth_limit, uint64_t work_limit);
/* Explicit lexical observation: a numeric reference consumes its entire Nd
 * run. Selection uses cleaned scalar coordinates and never known group IDs.
 * The packed forest remains available in the returned parser observation. */
Atom *cetta_re_v1_select_numeric_references(Arena *, Atom *parser_observation, Atom *cleaned_text);
#endif
