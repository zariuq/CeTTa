#ifndef CETTA_PETTA_CONTRACT_H
#define CETTA_PETTA_CONTRACT_H

#include "atom.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CETTA_PETTA_BUILTIN_REL_NONE = 0,
    CETTA_PETTA_BUILTIN_REL_APPEND,
    CETTA_PETTA_BUILTIN_REL_MEMBER,
    CETTA_PETTA_BUILTIN_REL_SIZE_ATOM,
    CETTA_PETTA_BUILTIN_REL_HASH_PLUS,
    CETTA_PETTA_BUILTIN_REL_HASH_EQ,
    CETTA_PETTA_BUILTIN_REL_HASH_LT,
    CETTA_PETTA_BUILTIN_REL_HASH_GT,
} CettaPettaBuiltinRelationKind;

const char *cetta_petta_partial_head_name(void);
const char *cetta_petta_lambda_env_head_name(void);
const char *cetta_petta_lambda_closure_head_name(void);

SymbolId cetta_petta_relation_head_id(Arena *arena, SymbolId head_id,
                                      uint32_t arity);
bool cetta_petta_relation_name_is_hidden(const char *name);
bool cetta_petta_relation_symbol_is_hidden(Atom *head);
bool cetta_petta_relation_call_uses_hidden_head(Atom *call);

const char *cetta_petta_builtin_relation_head_name(
    CettaPettaBuiltinRelationKind kind);
SymbolId cetta_petta_builtin_relation_head_id(
    Arena *arena, CettaPettaBuiltinRelationKind kind);
CettaPettaBuiltinRelationKind cetta_petta_builtin_relation_kind_from_call(
    Atom *call);

#endif /* CETTA_PETTA_CONTRACT_H */
