#ifndef CETTA_GSLT2PARSE_PARSER_PACK_ABI_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_ABI_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "atom.h"

typedef enum {
    PPABI_V1_TERMINAL_ANY = 0,
    PPABI_V1_TERMINAL_EOF = 1,
    PPABI_V1_TERMINAL_CHAR = 2,
    PPABI_V1_TERMINAL_CLASS = 3
} PPABIV1TerminalKind;

typedef enum {
    PPABI_V1_ITEM_TERMINAL = 0,
    PPABI_V1_ITEM_NONTERMINAL = 1
} PPABIV1ItemKind;

typedef enum {
    PPABI_V1_CLASS_POINT = 0,
    PPABI_V1_CLASS_EXCEPT = 1
} PPABIV1ClassClauseKind;

typedef enum {
    PPABI_V1_EVIDENCE_PRODUCTION = 0,
    PPABI_V1_EVIDENCE_CLASS = 1
} PPABIV1EvidenceKind;

typedef struct {
    PPABIV1ItemKind kind;
    uint32_t dense_id;
} PPABIV1Item;

typedef struct {
    Atom *identity;
    char *canonical;
    uint32_t dense_id;
    bool defined;
} PPABIV1State;

typedef struct {
    Atom *identity;
    char *canonical;
    uint32_t dense_id;
    PPABIV1TerminalKind kind;
    uint32_t codepoint;
    Atom *class_expression;
} PPABIV1Terminal;

typedef struct {
    Atom *identity;
    char *canonical;
    uint32_t dense_id;
    uint32_t lhs_state_id;
    PPABIV1Item *items;
    uint32_t item_len;
    Atom *action;
    uint32_t evidence_begin;
    uint32_t evidence_len;
} PPABIV1Production;

typedef struct {
    Atom *identity;
    char *canonical;
    Atom *class_identity;
    PPABIV1ClassClauseKind kind;
    uint32_t point;
    uint32_t *excluded_points;
    uint32_t excluded_len;
    uint32_t evidence_begin;
    uint32_t evidence_len;
} PPABIV1ClassClause;

typedef struct {
    PPABIV1EvidenceKind kind;
    Atom *artifact;
    Atom *answer;
    Atom *certificate;
} PPABIV1DerivationInput;

typedef struct {
    PPABIV1EvidenceKind kind;
    uint32_t artifact_index;
    Atom *answer;
    Atom *certificate;
    char *canonical;
} PPABIV1Derivation;

typedef struct {
    const char *source_digest;
    const char *compiler_digest;
    const char *environment_digest;
    const PPABIV1DerivationInput *derivations;
    size_t derivation_len;
} PPABIV1ProvenanceInput;

typedef struct {
    Arena arena;
    PPABIV1State *states;
    uint32_t state_len;
    PPABIV1Terminal *terminals;
    uint32_t terminal_len;
    PPABIV1Production *productions;
    uint32_t production_len;
    PPABIV1ClassClause *class_clauses;
    uint32_t class_clause_len;
    PPABIV1Derivation *derivations;
    uint32_t derivation_len;
    char source_digest[65];
    char compiler_digest[65];
    char environment_digest[65];
    char pack_digest[65];
} PPABIV1Pack;

void ppabi_v1_pack_init(PPABIV1Pack *pack);
void ppabi_v1_pack_free(PPABIV1Pack *pack);

bool ppabi_v1_pack_load(PPABIV1Pack *out,
                        Atom *const *production_terms,
                        size_t production_len,
                        Atom *const *class_clause_terms,
                        size_t class_clause_len,
                        const PPABIV1ProvenanceInput *provenance,
                        char *error_buf,
                        size_t error_buf_size);

bool ppabi_v1_pack_start_is_closed(const PPABIV1Pack *pack,
                                   const Atom *start_state,
                                   char *error_buf,
                                   size_t error_buf_size);

#endif
