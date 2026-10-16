#ifndef CETTA_SYNTAX_GSLT_V1_H
#define CETTA_SYNTAX_GSLT_V1_H

#include "gslt_composition_v1.h"

/* Admission uses the existing typed LanguageDef/profile compiler boundary.
 * This module owns only an inspectable envelope and declaration-site origins;
 * it does not define another grammar, parser, lexer, or .module file format. */
typedef bool (*CettaSyntaxGsltAdmissionV1)(
    Atom *language, Atom *profile, char *error, size_t error_size);

Atom *cetta_syntax_gslt_admit_v1(
    Arena *arena, Atom *language, Atom *profile, Atom *policy, Atom *origins,
    CettaSyntaxGsltAdmissionV1 admit);
Atom *cetta_syntax_gslt_inspect_v1(
    Arena *arena, Atom *value);
Atom *cetta_syntax_gslt_inspect_checked_v1(
    Arena *arena, Atom *value, CettaSyntaxGsltAdmissionV1 admit);
Atom *cetta_syntax_gslt_join_v1(
    Arena *arena, Atom *name, Atom *start,
    Atom *first, Atom *second, CettaGsltOriginPolicyV1 origin_policy,
    CettaSyntaxGsltAdmissionV1 admit);

bool cetta_syntax_gslt_data_arg_v1(const char *operation, uint32_t index);

#endif
