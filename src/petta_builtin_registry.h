#ifndef CETTA_PETTA_BUILTIN_REGISTRY_H
#define CETTA_PETTA_BUILTIN_REGISTRY_H

#include "petta_analysis.h"

/* A language declaration and a cardinality judgment are different evidence.
 * A Bool parameter, for example, does not imply that its value is bound. */
typedef enum {
    PETTA_BUILTIN_TYPE_NONE,
    PETTA_BUILTIN_TYPE_NUMBER,
    PETTA_BUILTIN_TYPE_BOOL,
    PETTA_BUILTIN_TYPE_STRING,
    PETTA_BUILTIN_TYPE_UNDEFINED,
    PETTA_BUILTIN_TYPE_VAR0,
    PETTA_BUILTIN_TYPE_VAR1,
    PETTA_BUILTIN_TYPE_LIST_VAR0,
    PETTA_BUILTIN_TYPE_LIST_UNDEFINED,
} PettaBuiltinType;

typedef enum {
    PETTA_BUILTIN_CARD_UNSPECIFIED,
    PETTA_BUILTIN_CARD_FIXED,
    PETTA_BUILTIN_CARD_MANIFEST_BOOLEANS,
    PETTA_BUILTIN_CARD_NONEMPTY_LIST_ARG0,
    PETTA_BUILTIN_CARD_PROPER_LIST_ARG0,
    PETTA_BUILTIN_CARD_PROPER_LIST_ARG1,
    PETTA_BUILTIN_CARD_MANIFEST_INDEXED_LIST,
    PETTA_BUILTIN_CARD_BOUND_MEMBERSHIP_PROBE,
    PETTA_BUILTIN_CARD_MANIFEST_FOREIGN_GOAL,
    PETTA_BUILTIN_CARD_SPACE_UPDATE,
    PETTA_BUILTIN_CARD_HIGHER_ORDER_LIST,
    PETTA_BUILTIN_CARD_COMPILER_DERIVED,
} PettaBuiltinCardinalityRule;

enum { PETTA_BUILTIN_VARIADIC = UINT16_MAX };

typedef struct {
    const char *name;
    uint16_t arity;
    uint16_t ordinal;
    const char *implementation;
    bool eager;
    bool has_signature;
    PettaAnalysisCardinality declared_grade;
    PettaBuiltinType arguments[2];
    PettaBuiltinType result;
    const char *contextual_typing;
    PettaBuiltinCardinalityRule cardinality_rule;
    PettaAnalysisCardinality flat_cardinality;
    const char *lowering;
} PettaBuiltinSpec;

/* These records describe the pinned PeTTa language. They do not register an
 * implementation or admit a call to any native execution route. */
size_t petta_builtin_spec_count(void);
const PettaBuiltinSpec *petta_builtin_spec_at(size_t index);
const PettaBuiltinSpec *petta_builtin_spec_find(const char *name, uint16_t arity);

/* Cardinality evidence from a call's literal inputs. This does not replace
 * the caller's analysis of argument evaluation or additional equations. */
PettaAnalysisCardinality petta_builtin_literal_cardinality(
    const PettaBuiltinSpec *spec, Atom *const *arguments, size_t arity);

/* Instantiate a declaration with fresh variables, preserving sharing within
 * one scheme. Untyped and contextual records have no standalone declaration. */
Atom *petta_builtin_signature(Arena *arena, const PettaBuiltinSpec *spec);

#endif
