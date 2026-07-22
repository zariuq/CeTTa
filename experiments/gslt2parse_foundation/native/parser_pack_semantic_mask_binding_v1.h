#ifndef CETTA_GSLT2PARSE_PARSER_PACK_SEMANTIC_MASK_BINDING_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_SEMANTIC_MASK_BINDING_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_guarded_lexical_exec_v1.h"
#include "semantic_mask_nfa_v1.h"

struct PPGuardedLexCursorV1SemanticMaskBinding {
    PPSemanticMaskNfaV1Plan mask;
    PPSemanticMaskTraceDfaV1Program transducer;
    uint32_t *terminal_tags;
    PPAtomProjectionActionV1TextBinding *text_bindings;
    uint32_t *action_wrapper_indices;
    uint32_t terminal_len;
    uint32_t bound_terminal_len;
    char source_digest[65];
    char compiler_digest[65];
    char answer_set_digest[65];
    char cursor_program_digest[65];
    char projection_action_program_digest[65];
    char binding_digest[65];
};

void ppguarded_lex_cursor_v1_semantic_mask_binding_init(
    PPGuardedLexCursorV1SemanticMaskBinding *binding);

void ppguarded_lex_cursor_v1_semantic_mask_binding_free(
    PPGuardedLexCursorV1SemanticMaskBinding *binding);

bool ppguarded_lex_cursor_v1_semantic_mask_binding_validate(
    const PPGuardedLexCursorV1SemanticMaskBinding *binding,
    const PPGuardedLexCursorV1Program *cursor,
    const PPABIV1Pack *pack,
    const PPAtomProjectionActionV1Program *projection_actions,
    const PPAtomProjectionV1Plan *projection,
    char *error_buf,
    size_t error_buf_size);

/*
 * Bind compiler-derived definition-to-label links to exact cursor terminals
 * and projection rules.  Failed replacement is atomic.
 */
bool ppguarded_lex_cursor_v1_semantic_mask_binding_build(
    PPGuardedLexCursorV1SemanticMaskBinding *binding,
    const PPGuardedLexCursorV1Program *cursor,
    const PPABIV1Pack *pack,
    const PPAtomProjectionActionV1Program *projection_actions,
    const PPAtomProjectionV1Plan *projection,
    Atom *const *answer_terms,
    size_t answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    uint32_t state_limit,
    uint32_t transition_limit,
    char *error_buf,
    size_t error_buf_size);

#endif
