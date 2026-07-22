#ifndef CETTA_GSLT2PARSE_PARSER_PACK_GUARDED_LEXICAL_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_GUARDED_LEXICAL_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_guard_plan_v1.h"
#include "regular_span_nfa_v1.h"

typedef struct {
    uint32_t tag_index;
    uint32_t state_id;
    uint32_t terminal_index;
    uint32_t guard_entry_index;
    Atom *tag;
    Atom *guard_state;
    Atom *guard_body;
    char *tag_canonical;
    char *state_canonical;
    char *guard_state_canonical;
    char *guard_body_canonical;
} PPGuardedLexV1Entry;

typedef struct {
    Arena arena;
    RSNFAV1Plan prefix_nfa;
    PPGuardedLexV1Entry *entries;
    PPNativeV1TerminalExtension *terminals;
    uint32_t *tag_terminal_ids;
    uint32_t entry_len;
    char base_pack_digest[65];
    char lexical_plan_digest[65];
    char guard_plan_digest[65];
    char guarded_compiler_digest[65];
    char guarded_nfa_answer_digest[65];
    char plan_digest[65];
} PPGuardedLexV1Plan;

void ppguarded_lex_v1_plan_init(PPGuardedLexV1Plan *plan);
void ppguarded_lex_v1_plan_free(PPGuardedLexV1Plan *plan);

bool ppguarded_lex_v1_plan_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    Atom *const *guarded_nfa_answers,
    size_t guarded_nfa_answer_len,
    const char *guarded_compiler_digest,
    const char *guarded_nfa_answer_digest,
    PPGuardedLexV1Plan *out,
    char *error_buf,
    size_t error_buf_size);

bool ppguarded_lex_v1_grammar_build(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *plan,
    CettaLpNativeGrammar *grammar,
    char *error_buf,
    size_t error_buf_size);

#endif
