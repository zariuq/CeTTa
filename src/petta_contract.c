#include "petta_contract.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

const char *cetta_petta_partial_head_name(void) {
    return "partial";
}

const char *cetta_petta_lambda_env_head_name(void) {
    return "__petta_lambda_env";
}

const char *cetta_petta_lambda_closure_head_name(void) {
    return "__petta_lambda_closure";
}

SymbolId cetta_petta_relation_head_id(Arena *arena, SymbolId head_id,
                                      uint32_t arity) {
    char buf[96];
    snprintf(buf, sizeof(buf), "__petta_rel_%" PRIu32 "_%" PRIu32,
             head_id, arity);
    return atom_symbol(arena, buf)->sym_id;
}

bool cetta_petta_relation_name_is_hidden(const char *name) {
    return name && strncmp(name, "__petta_rel_", 12) == 0;
}

bool cetta_petta_relation_symbol_is_hidden(Atom *head) {
    if (!head || head->kind != ATOM_SYMBOL)
        return false;
    return cetta_petta_relation_name_is_hidden(atom_name_cstr(head));
}

bool cetta_petta_relation_call_uses_hidden_head(Atom *call) {
    if (!call || call->kind != ATOM_EXPR || call->expr.len == 0)
        return false;
    return cetta_petta_relation_symbol_is_hidden(call->expr.elems[0]);
}

const char *cetta_petta_builtin_relation_head_name(
    CettaPettaBuiltinRelationKind kind) {
    switch (kind) {
    case CETTA_PETTA_BUILTIN_REL_APPEND:
        return "__petta_rel_builtin_append";
    case CETTA_PETTA_BUILTIN_REL_MEMBER:
        return "__petta_rel_builtin_member";
    case CETTA_PETTA_BUILTIN_REL_SIZE_ATOM:
        return "__petta_rel_builtin_size_atom";
    case CETTA_PETTA_BUILTIN_REL_HASH_PLUS:
        return "__petta_rel_builtin_hash_plus";
    case CETTA_PETTA_BUILTIN_REL_HASH_EQ:
        return "__petta_rel_builtin_hash_eq";
    case CETTA_PETTA_BUILTIN_REL_HASH_LT:
        return "__petta_rel_builtin_hash_lt";
    case CETTA_PETTA_BUILTIN_REL_HASH_GT:
        return "__petta_rel_builtin_hash_gt";
    default:
        return NULL;
    }
}

SymbolId cetta_petta_builtin_relation_head_id(
    Arena *arena, CettaPettaBuiltinRelationKind kind) {
    const char *name = cetta_petta_builtin_relation_head_name(kind);
    if (!arena || !name)
        return SYMBOL_ID_NONE;
    return atom_symbol(arena, name)->sym_id;
}

CettaPettaBuiltinRelationKind cetta_petta_builtin_relation_kind_from_call(
    Atom *call) {
    if (!call || call->kind != ATOM_EXPR || call->expr.len == 0)
        return CETTA_PETTA_BUILTIN_REL_NONE;
    const char *name = atom_name_cstr(call->expr.elems[0]);
    if (!name)
        return CETTA_PETTA_BUILTIN_REL_NONE;
    if (call->expr.len == 5 &&
        strcmp(name, "__petta_rel_builtin_append") == 0) {
        return CETTA_PETTA_BUILTIN_REL_APPEND;
    }
    if (call->expr.len == 5 &&
        strcmp(name, "__petta_rel_builtin_member") == 0) {
        return CETTA_PETTA_BUILTIN_REL_MEMBER;
    }
    if (call->expr.len == 4 &&
        strcmp(name, "__petta_rel_builtin_size_atom") == 0) {
        return CETTA_PETTA_BUILTIN_REL_SIZE_ATOM;
    }
    if (call->expr.len != 5)
        return CETTA_PETTA_BUILTIN_REL_NONE;
    if (strcmp(name, "__petta_rel_builtin_hash_plus") == 0)
        return CETTA_PETTA_BUILTIN_REL_HASH_PLUS;
    if (strcmp(name, "__petta_rel_builtin_hash_eq") == 0)
        return CETTA_PETTA_BUILTIN_REL_HASH_EQ;
    if (strcmp(name, "__petta_rel_builtin_hash_lt") == 0)
        return CETTA_PETTA_BUILTIN_REL_HASH_LT;
    if (strcmp(name, "__petta_rel_builtin_hash_gt") == 0)
        return CETTA_PETTA_BUILTIN_REL_HASH_GT;
    return CETTA_PETTA_BUILTIN_REL_NONE;
}
