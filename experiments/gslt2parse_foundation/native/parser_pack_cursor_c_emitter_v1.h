#ifndef CETTA_GSLT2PARSE_PARSER_PACK_CURSOR_C_EMITTER_V1_H
#define CETTA_GSLT2PARSE_PARSER_PACK_CURSOR_C_EMITTER_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "parser_pack_guarded_lexical_exec_v1.h"

/*
 * Emit an owning C initializer for a validated cursor/action program.
 * The output contains only data and action terms derived from the supplied
 * program.  It does not contain guest-language recognition logic.
 */
bool ppguarded_lex_cursor_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size);

#endif
