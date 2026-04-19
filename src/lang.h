#ifndef CETTA_LANG_H
#define CETTA_LANG_H

#include <stdbool.h>
#include <stdio.h>
#include "symbol.h"

typedef struct Atom Atom;

typedef enum {
    CETTA_LANGUAGE_HE = 0,
    CETTA_LANGUAGE_MM2 = 1,
    CETTA_LANGUAGE_PETTA = 2
} CettaLanguageId;

typedef enum {
    CETTA_FUNCTION_ARG_POLICY_RAW = 0,
    CETTA_FUNCTION_ARG_POLICY_TYPED_EVAL = 1,
    CETTA_FUNCTION_ARG_POLICY_UNTYPED_EVAL = 2
} CettaFunctionArgPolicy;

typedef struct {
    bool known_head_query_miss_is_failure;
    bool unique_atom_alpha_equivalence_for_open_terms;
    bool inject_builtin_type_decls;
    CettaFunctionArgPolicy undefined_arg_policy;
    CettaFunctionArgPolicy atom_arg_policy;
    CettaFunctionArgPolicy expression_arg_policy;
} CettaLanguageSemantics;

typedef struct {
    CettaLanguageId id;
    const char *name;
    const char *canonical;
    bool implemented;
    const char *note;
    CettaLanguageSemantics semantics;
} CettaLanguageSpec;

typedef struct {
    const char *head_name;
    const char *arg_type_name;
    const char *result_type_name;
} CettaTypeDeclSignature;

const CettaLanguageSpec *cetta_language_lookup(const char *name);
void cetta_language_print_inventory(FILE *out);
bool cetta_language_known_head_query_miss_is_failure(const CettaLanguageSpec *spec);
bool cetta_language_unique_atom_uses_alpha_for_open_terms(const CettaLanguageSpec *spec);
bool cetta_language_injects_builtin_type_decls(const CettaLanguageSpec *spec);
CettaFunctionArgPolicy cetta_language_function_arg_policy(const CettaLanguageSpec *spec,
                                                          SymbolId head_id,
                                                          uint32_t arg_index,
                                                          Atom *domain_type);
const CettaTypeDeclSignature *cetta_language_stdlib_type_prunes(const CettaLanguageSpec *spec,
                                                                size_t *count_out);
const char *cetta_language_prelude_text(const CettaLanguageSpec *spec);

#endif /* CETTA_LANG_H */
