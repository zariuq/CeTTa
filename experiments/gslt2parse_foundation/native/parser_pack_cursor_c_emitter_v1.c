#include "parser_pack_cursor_c_emitter_v1.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "finite_horn_ground_term_v1.h"

typedef struct {
    FILE *output;
    const char *prefix;
    char *error_buf;
    size_t error_buf_size;
    bool failed;
} PPCursorCEmitterV1;

static void pp_cursor_c_v1_error(PPCursorCEmitterV1 *emitter,
                                 const char *format, ...) {
    va_list args;

    if (!emitter || emitter->failed)
        return;
    emitter->failed = true;
    if (!emitter->error_buf || emitter->error_buf_size == 0u)
        return;
    va_start(args, format);
    vsnprintf(emitter->error_buf, emitter->error_buf_size, format, args);
    va_end(args);
}

static void pp_cursor_c_v1_write(PPCursorCEmitterV1 *emitter,
                                 const char *format, ...) {
    va_list args;

    if (!emitter || emitter->failed)
        return;
    va_start(args, format);
    if (vfprintf(emitter->output, format, args) < 0)
        pp_cursor_c_v1_error(emitter, "failed to write generated C");
    va_end(args);
}

static bool pp_cursor_c_v1_identifier_valid(const char *identifier) {
    size_t index;

    if (!identifier || !identifier[0] ||
        !(identifier[0] == '_' || isalpha((unsigned char)identifier[0]))) {
        return false;
    }
    for (index = 1u; identifier[index]; index++) {
        if (!(identifier[index] == '_' ||
              isalnum((unsigned char)identifier[index]))) {
            return false;
        }
    }
    return true;
}

static void pp_cursor_c_v1_string(PPCursorCEmitterV1 *emitter,
                                  const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;

    pp_cursor_c_v1_write(emitter, "\"");
    while (cursor && *cursor) {
        unsigned char ch = *cursor++;
        if (ch == '\\' || ch == '"') {
            pp_cursor_c_v1_write(emitter, "\\%c", (int)ch);
        } else if (ch >= 0x20u && ch <= 0x7eu) {
            pp_cursor_c_v1_write(emitter, "%c", (int)ch);
        } else {
            pp_cursor_c_v1_write(emitter, "\\%03o", (unsigned int)ch);
        }
    }
    pp_cursor_c_v1_write(emitter, "\"");
}

static void pp_cursor_c_v1_u32_array(PPCursorCEmitterV1 *emitter,
                                     const char *suffix,
                                     const uint32_t *values,
                                     uint32_t len) {
    uint32_t index;

    if (len == 0u)
        return;
    pp_cursor_c_v1_write(
        emitter, "static const uint32_t %s_%s[%u] = {\n    ",
        emitter->prefix, suffix, len);
    for (index = 0u; index < len; index++) {
        pp_cursor_c_v1_write(
            emitter, "UINT32_C(%u)%s", values[index],
            index + 1u == len ? "" : ",");
        if (index + 1u != len) {
            pp_cursor_c_v1_write(
                emitter, (index + 1u) % 8u == 0u ? "\n    " : " ");
        }
    }
    pp_cursor_c_v1_write(emitter, "\n};\n\n");
}

static void pp_cursor_c_v1_ranges(PPCursorCEmitterV1 *emitter,
                                  const char *suffix,
                                  const CettaLpNativeUnicodeRange *ranges,
                                  uint32_t len) {
    uint32_t index;

    if (len == 0u)
        return;
    pp_cursor_c_v1_write(
        emitter,
        "static const CettaLpNativeUnicodeRange %s_%s[%u] = {\n",
        emitter->prefix, suffix, len);
    for (index = 0u; index < len; index++) {
        pp_cursor_c_v1_write(
            emitter, "    { UINT32_C(%u), UINT32_C(%u) }%s\n",
            ranges[index].low, ranges[index].high,
            index + 1u == len ? "" : ",");
    }
    pp_cursor_c_v1_write(emitter, "};\n\n");
}

static void pp_cursor_c_v1_slr(PPCursorCEmitterV1 *emitter,
                               const char *suffix,
                               const CettaLpNativeSlrProgram *program) {
    uint32_t index;
    char part[128];

    snprintf(part, sizeof(part), "%s_terminals", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->terminals, program->terminal_len);
    snprintf(part, sizeof(part), "%s_nonterminals", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->nonterminals, program->nonterminal_len);
    if (program->production_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const CettaLpNativeSlrProgramProduction "
            "%s_%s_productions[%u] = {\n",
            emitter->prefix, suffix, program->production_len);
        for (index = 0u; index < program->production_len; index++) {
            const CettaLpNativeSlrProgramProduction *production =
                &program->productions[index];
            pp_cursor_c_v1_write(
                emitter,
                "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
                "UINT32_C(%u), %s }%s\n",
                production->label, production->lhs,
                production->rhs_begin, production->rhs_len,
                production->authored ? "true" : "false",
                index + 1u == program->production_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    if (program->rhs_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const CettaLpNativeSymbol %s_%s_rhs[%u] = {\n",
            emitter->prefix, suffix, program->rhs_len);
        for (index = 0u; index < program->rhs_len; index++) {
            const CettaLpNativeSymbol *symbol = &program->rhs[index];
            pp_cursor_c_v1_write(
                emitter,
                "    { (CettaLpNativeSymbolKind)%u, UINT32_C(%u), "
                "UINT32_C(%u) }%s\n",
                (unsigned int)symbol->kind, symbol->name, symbol->scope,
                index + 1u == program->rhs_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    if (program->action_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const CettaLpNativeSlrProgramAction "
            "%s_%s_actions[%u] = {\n",
            emitter->prefix, suffix, program->action_len);
        for (index = 0u; index < program->action_len; index++) {
            const CettaLpNativeSlrProgramAction *action =
                &program->actions[index];
            pp_cursor_c_v1_write(
                emitter, "    { (CettaLpNativeSlrProgramActionKind)%u, "
                "INT32_C(%d) }%s\n",
                (unsigned int)action->kind, action->value,
                index + 1u == program->action_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    snprintf(part, sizeof(part), "%s_gotos", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->gotos, program->goto_len);
}

static void pp_cursor_c_v1_slr_assign(PPCursorCEmitterV1 *emitter,
                                      const char *target,
                                      const char *suffix,
                                      const CettaLpNativeSlrProgram *program) {
    pp_cursor_c_v1_write(
        emitter,
        "    %s.start_nonterminal = UINT32_C(%u);\n"
        "    %s.terminal_len = UINT32_C(%u);\n"
        "    %s.nonterminal_len = UINT32_C(%u);\n"
        "    %s.production_len = UINT32_C(%u);\n"
        "    %s.authored_production_len = UINT32_C(%u);\n"
        "    %s.rhs_len = UINT32_C(%u);\n"
        "    %s.action_len = UINT32_C(%u);\n"
        "    %s.goto_len = UINT32_C(%u);\n"
        "    %s.summary = (CettaLpNativeSlrSummary){ "
        "UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
        "UINT32_C(%u), UINT32_C(%u) };\n",
        target, program->start_nonterminal,
        target, program->terminal_len,
        target, program->nonterminal_len,
        target, program->production_len,
        target, program->authored_production_len,
        target, program->rhs_len,
        target, program->action_len,
        target, program->goto_len,
        target,
        program->summary.state_len, program->summary.shift_len,
        program->summary.goto_len, program->summary.reduce_len,
        program->summary.accept_len, program->summary.conflict_len);
    if (program->terminal_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.terminals, %s_%s_terminals, "
            "sizeof(%s_%s_terminals), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->nonterminal_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.nonterminals, "
            "%s_%s_nonterminals, sizeof(%s_%s_nonterminals), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->production_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.productions, "
            "%s_%s_productions, sizeof(%s_%s_productions), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->rhs_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.rhs, %s_%s_rhs, "
            "sizeof(%s_%s_rhs), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->action_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.actions, %s_%s_actions, "
            "sizeof(%s_%s_actions), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->goto_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.gotos, %s_%s_gotos, "
            "sizeof(%s_%s_gotos), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
}

static void pp_cursor_c_v1_dfa(PPCursorCEmitterV1 *emitter,
                               const RSDFAV1Program *program) {
    uint32_t index;

    if (program->state_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const RSDFAV1ProgramState %s_dfa_states[%u] = {\n",
            emitter->prefix, program->state_len);
        for (index = 0u; index < program->state_len; index++) {
            const RSDFAV1ProgramState *state = &program->states[index];
            pp_cursor_c_v1_write(
                emitter,
                "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
                "UINT32_C(%u), UINT32_C(%u), UINT32_C(%u) }%s\n",
                state->transition_begin, state->transition_len,
                state->accept_begin, state->accept_len,
                state->eof_accept_begin, state->eof_accept_len,
                index + 1u == program->state_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    if (program->transition_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const RSDFAV1ProgramTransition "
            "%s_dfa_transitions[%u] = {\n",
            emitter->prefix, program->transition_len);
        for (index = 0u; index < program->transition_len; index++) {
            const RSDFAV1ProgramTransition *transition =
                &program->transitions[index];
            pp_cursor_c_v1_write(
                emitter,
                "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u) }%s\n",
                transition->low, transition->high, transition->target,
                index + 1u == program->transition_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    pp_cursor_c_v1_u32_array(
        emitter, "dfa_accept_tags", program->accept_tags,
        program->accept_tag_len);
    pp_cursor_c_v1_u32_array(
        emitter, "dfa_eof_accept_tags", program->eof_accept_tags,
        program->eof_accept_tag_len);
}

static void pp_cursor_c_v1_terminals(
    PPCursorCEmitterV1 *emitter,
    const PPGuardedLexCursorV1Terminal *terminals,
    uint32_t len) {
    uint32_t index;

    if (len == 0u)
        return;
    pp_cursor_c_v1_write(
        emitter,
        "static const PPGuardedLexCursorV1Terminal %s_terminals[%u] = {\n",
        emitter->prefix, len);
    for (index = 0u; index < len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal = &terminals[index];
        pp_cursor_c_v1_write(
            emitter,
            "    { UINT32_C(%u), UINT32_C(%u), "
            "(PPGuardedLexCursorV1TerminalKind)%u, "
            "(CettaLpNativeUtf8TerminalKind)%u, UINT32_C(%u), "
            "UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
            "UINT32_C(%u), UINT32_C(%u), %s, %s, UINT32_C(%u), "
            "UINT32_C(%u) }%s\n",
            terminal->terminal_id, terminal->slr_terminal_index,
            (unsigned int)terminal->kind,
            (unsigned int)terminal->scalar_kind, terminal->scalar,
            terminal->scalar_range_begin, terminal->scalar_range_len,
            terminal->dfa_tag, terminal->guard_dfa_tag,
            terminal->guard_range_begin, terminal->guard_range_len,
            terminal->guard_accepts_eof ? "true" : "false",
            terminal->semantic_value_required ? "true" : "false",
            terminal->semantic_start_state_id,
            terminal->semantic_program_index,
            index + 1u == len ? "" : ",");
    }
    pp_cursor_c_v1_write(emitter, "};\n\n");
}

static void pp_cursor_c_v1_value_terminals(
    PPCursorCEmitterV1 *emitter,
    const char *suffix,
    const PPGuardedLexCursorV1ValueTerminal *terminals,
    uint32_t len) {
    uint32_t index;

    if (len == 0u)
        return;
    pp_cursor_c_v1_write(
        emitter,
        "static const PPGuardedLexCursorV1ValueTerminal "
        "%s_%s_terminals[%u] = {\n",
        emitter->prefix, suffix, len);
    for (index = 0u; index < len; index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &terminals[index];
        pp_cursor_c_v1_write(
            emitter,
            "    { UINT32_C(%u), UINT32_C(%u), "
            "(PPGuardedLexCursorV1ValueTerminalKind)%u, "
            "(CettaLpNativeUtf8TerminalKind)%u, UINT32_C(%u), "
            "UINT32_C(%u), UINT32_C(%u), UINT32_C(%u) }%s\n",
            terminal->terminal_id, terminal->slr_terminal_index,
            (unsigned int)terminal->kind,
            (unsigned int)terminal->scalar_kind, terminal->scalar,
            terminal->range_begin, terminal->range_len,
            terminal->guard_value_program_index,
            index + 1u == len ? "" : ",");
    }
    pp_cursor_c_v1_write(emitter, "};\n\n");
}

static void pp_cursor_c_v1_slr_actions(
    PPCursorCEmitterV1 *emitter,
    const char *suffix,
    const CettaLpNativeSlrProgramAction *actions,
    uint32_t len) {
    uint32_t index;

    if (len == 0u)
        return;
    pp_cursor_c_v1_write(
        emitter,
        "static const CettaLpNativeSlrProgramAction %s_%s[%u] = {\n",
        emitter->prefix, suffix, len);
    for (index = 0u; index < len; index++) {
        pp_cursor_c_v1_write(
            emitter,
            "    { (CettaLpNativeSlrProgramActionKind)%u, "
            "(int32_t)%d }%s\n",
            (unsigned int)actions[index].kind, actions[index].value,
            index + 1u == len ? "" : ",");
    }
    pp_cursor_c_v1_write(emitter, "};\n\n");
}

static void pp_cursor_c_v1_value_program(
    PPCursorCEmitterV1 *emitter,
    const PPGuardedLexCursorV1ValueProgram *program,
    uint32_t program_index) {
    char suffix[64];
    char part[96];
    uint32_t dispatch_len = 0u;

    snprintf(suffix, sizeof(suffix), "value_%u", program_index);
    snprintf(part, sizeof(part), "%s_slr", suffix);
    pp_cursor_c_v1_slr(emitter, part, &program->slr);
    snprintf(part, sizeof(part), "%s_lhs_indices", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->production_lhs_indices,
        program->slr.production_len);
    pp_cursor_c_v1_value_terminals(
        emitter, suffix, program->terminals, program->terminal_len);
    snprintf(part, sizeof(part), "%s_ranges", suffix);
    pp_cursor_c_v1_ranges(emitter, part, program->ranges, program->range_len);
    snprintf(part, sizeof(part), "%s_candidate_offsets", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->state_candidate_offsets,
        program->slr.summary.state_len + 1u);
    snprintf(part, sizeof(part), "%s_candidate_indices", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->state_candidate_terminal_indices,
        program->state_candidate_len);
    snprintf(part, sizeof(part), "%s_dispatch_ranges", suffix);
    pp_cursor_c_v1_ranges(
        emitter, part, program->scalar_dispatch_ranges,
        program->scalar_dispatch_range_len);
    if (program->slr.summary.state_len > 0u &&
        program->scalar_dispatch_range_len <=
            UINT32_MAX / program->slr.summary.state_len) {
        dispatch_len = program->slr.summary.state_len *
            program->scalar_dispatch_range_len;
    }
    snprintf(part, sizeof(part), "%s_dispatch_actions", suffix);
    pp_cursor_c_v1_slr_actions(
        emitter, part, program->scalar_dispatch_actions, dispatch_len);
    snprintf(part, sizeof(part), "%s_dispatch_shift_sources", suffix);
    pp_cursor_c_v1_u32_array(
        emitter, part, program->scalar_dispatch_shift_sources,
        dispatch_len);
}

static void pp_cursor_c_v1_action_program(PPCursorCEmitterV1 *emitter,
                                          const PPActionBytecodeV1Program *p) {
    uint32_t index;

    if (p->production_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const PPActionBytecodeV1Production "
            "%s_action_productions[%u] = {\n",
            emitter->prefix, p->production_len);
        for (index = 0u; index < p->production_len; index++) {
            const PPActionBytecodeV1Production *production =
                &p->productions[index];
            pp_cursor_c_v1_write(
                emitter,
                "    { UINT32_C(%u), UINT32_C(%u), UINT32_C(%u), "
                "UINT32_C(%u) }%s\n",
                production->instruction_begin, production->instruction_len,
                production->arity, production->max_stack_len,
                index + 1u == p->production_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    if (p->instruction_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "static const PPActionBytecodeV1Instruction "
            "%s_action_instructions[%u] = {\n",
            emitter->prefix, p->instruction_len);
        for (index = 0u; index < p->instruction_len; index++) {
            const PPActionBytecodeV1Instruction *instruction =
                &p->instructions[index];
            pp_cursor_c_v1_write(
                emitter,
                "    { (PPActionBytecodeV1InstructionKind)%u, "
                "UINT32_C(%u), NULL }%s\n",
                (unsigned int)instruction->kind, instruction->operand,
                index + 1u == p->instruction_len ? "" : ",");
        }
        pp_cursor_c_v1_write(emitter, "};\n\n");
    }
    for (index = 0u; index < p->instruction_len; index++) {
        const PPActionBytecodeV1Instruction *instruction =
            &p->instructions[index];
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        size_t byte_index;

        if (instruction->kind == PP_ACTION_BYTECODE_V1_PUSH_SLOT)
            continue;
        if (!instruction->term ||
            !fh_ground_term_v1_render(
                instruction->term, &canonical, &canonical_len,
                emitter->error_buf, emitter->error_buf_size) ||
            canonical_len == 0u) {
            free(canonical);
            pp_cursor_c_v1_error(
                emitter, "cannot render action instruction %u", index);
            return;
        }
        pp_cursor_c_v1_write(
            emitter,
            "static const uint8_t %s_action_term_%u[%zu] = {\n    ",
            emitter->prefix, index, canonical_len);
        for (byte_index = 0u; byte_index < canonical_len; byte_index++) {
            pp_cursor_c_v1_write(
                emitter, "UINT8_C(%u)%s", (unsigned int)canonical[byte_index],
                byte_index + 1u == canonical_len ? "" : ",");
            if (byte_index + 1u != canonical_len) {
                pp_cursor_c_v1_write(
                    emitter,
                    (byte_index + 1u) % 12u == 0u ? "\n    " : " ");
            }
        }
        pp_cursor_c_v1_write(emitter, "\n};\n\n");
        free(canonical);
        if (emitter->failed)
            return;
    }
}

static void pp_cursor_c_v1_value_assign(
    PPCursorCEmitterV1 *emitter,
    const PPGuardedLexCursorV1ValueProgram *program,
    uint32_t program_index) {
    char suffix[64];
    char slr_suffix[80];
    char target[80];
    uint32_t dispatch_len = program->slr.summary.state_len *
        program->scalar_dispatch_range_len;

    snprintf(suffix, sizeof(suffix), "value_%u", program_index);
    snprintf(slr_suffix, sizeof(slr_suffix), "%s_slr", suffix);
    snprintf(
        target, sizeof(target), "result.value_programs[%u]", program_index);
    {
        char slr_target[96];
        snprintf(slr_target, sizeof(slr_target), "%s.slr", target);
        pp_cursor_c_v1_slr_assign(
            emitter, slr_target, slr_suffix, &program->slr);
    }
    if (program->slr.production_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.production_lhs_indices, "
            "%s_%s_lhs_indices, sizeof(%s_%s_lhs_indices), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->terminal_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.terminals, %s_%s_terminals, "
            "sizeof(%s_%s_terminals), error_buf, error_buf_size)) "
            "goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    if (program->range_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.ranges, %s_%s_ranges, "
            "sizeof(%s_%s_ranges), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    pp_cursor_c_v1_write(
        emitter,
        "    if (!%s_copy((void **)&%s.state_candidate_offsets, "
        "%s_%s_candidate_offsets, sizeof(%s_%s_candidate_offsets), "
        "error_buf, error_buf_size)) goto fail;\n",
        emitter->prefix, target, emitter->prefix, suffix,
        emitter->prefix, suffix);
    if (program->state_candidate_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.state_candidate_terminal_indices, "
            "%s_%s_candidate_indices, sizeof(%s_%s_candidate_indices), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    pp_cursor_c_v1_write(
        emitter,
        "    if (!%s_copy((void **)&%s.scalar_dispatch_ranges, "
        "%s_%s_dispatch_ranges, sizeof(%s_%s_dispatch_ranges), "
        "error_buf, error_buf_size)) goto fail;\n",
        emitter->prefix, target, emitter->prefix, suffix,
        emitter->prefix, suffix);
    if (dispatch_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&%s.scalar_dispatch_actions, "
            "%s_%s_dispatch_actions, sizeof(%s_%s_dispatch_actions), "
            "error_buf, error_buf_size) ||\n"
            "        !%s_copy((void **)&%s.scalar_dispatch_shift_sources, "
            "%s_%s_dispatch_shift_sources, "
            "sizeof(%s_%s_dispatch_shift_sources), error_buf, "
            "error_buf_size)) goto fail;\n",
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix,
            emitter->prefix, target, emitter->prefix, suffix,
            emitter->prefix, suffix);
    }
    pp_cursor_c_v1_write(
        emitter,
        "    %s.terminal_len = UINT32_C(%u);\n"
        "    %s.range_len = UINT32_C(%u);\n"
        "    %s.state_candidate_len = UINT32_C(%u);\n"
        "    %s.scalar_dispatch_range_len = UINT32_C(%u);\n"
        "    %s.start_state_id = UINT32_C(%u);\n"
        "    %s.guarded = %s;\n"
        "    memcpy(%s.program_digest, ",
        target, program->terminal_len,
        target, program->range_len,
        target, program->state_candidate_len,
        target, program->scalar_dispatch_range_len,
        target, program->start_state_id,
        target, program->guarded ? "true" : "false",
        target);
    pp_cursor_c_v1_string(emitter, program->program_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(%s.program_digest));\n", target);
}

static void pp_cursor_c_v1_emit_preamble(PPCursorCEmitterV1 *emitter,
                                         const char *program_digest) {
    pp_cursor_c_v1_write(
        emitter,
        "/* Generated from ParserPackGuardedLexicalCursorProgramV1. */\n"
        "#include <stdbool.h>\n"
        "#include <stddef.h>\n"
        "#include <stdint.h>\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n\n"
        "#include \"finite_horn_ground_term_v1.h\"\n"
        "#include \"parser_pack_guarded_lexical_exec_v1.h\"\n\n"
        "const char *%s_program_digest(void) {\n"
        "    return ",
        emitter->prefix);
    pp_cursor_c_v1_string(emitter, program_digest);
    pp_cursor_c_v1_write(
        emitter,
        ";\n}\n\n"
        "static bool %s_copy(void **target, const void *source, size_t size,\n"
        "                           char *error_buf, size_t error_buf_size) {\n"
        "    void *copy;\n"
        "    if (!target || !source || size == 0u) {\n"
        "        if (error_buf && error_buf_size > 0u)\n"
        "            snprintf(error_buf, error_buf_size, "
        "\"bad generated-program copy\");\n"
        "        return false;\n"
        "    }\n"
        "    copy = malloc(size);\n"
        "    if (!copy) {\n"
        "        if (error_buf && error_buf_size > 0u)\n"
        "            snprintf(error_buf, error_buf_size, "
        "\"cannot allocate generated program\");\n"
        "        return false;\n"
        "    }\n"
        "    memcpy(copy, source, size);\n"
        "    *target = copy;\n"
        "    return true;\n"
        "}\n\n",
        emitter->prefix);
}

static void pp_cursor_c_v1_emit_data(
    PPCursorCEmitterV1 *emitter,
    const PPGuardedLexCursorV1Program *program) {
    uint32_t index;

    pp_cursor_c_v1_dfa(emitter, &program->dfa);
    pp_cursor_c_v1_slr(emitter, "final_slr", &program->final_slr);
    pp_cursor_c_v1_u32_array(
        emitter, "final_lhs_indices", program->final_production_lhs_indices,
        program->final_slr.production_len);
    pp_cursor_c_v1_terminals(
        emitter, program->terminals, program->terminal_len);
    pp_cursor_c_v1_ranges(
        emitter, "ranges", program->ranges, program->range_len);
    for (index = 0u; index < program->value_program_len; index++)
        pp_cursor_c_v1_value_program(
            emitter, &program->value_programs[index], index);
    if (program->actions_bound)
        pp_cursor_c_v1_action_program(emitter, &program->actions);
}

static void pp_cursor_c_v1_emit_init(
    PPCursorCEmitterV1 *emitter,
    const PPGuardedLexCursorV1Program *program) {
    uint32_t index;

    pp_cursor_c_v1_write(
        emitter,
        "bool %s_program_init(PPGuardedLexCursorV1Program *out,\n"
        "                            char *error_buf,\n"
        "                            size_t error_buf_size) {\n"
        "    PPGuardedLexCursorV1Program result;\n\n"
        "    if (error_buf && error_buf_size > 0u) error_buf[0] = '\\0';\n"
        "    if (!out) {\n"
        "        if (error_buf && error_buf_size > 0u)\n"
        "            snprintf(error_buf, error_buf_size, "
        "\"missing generated program output\");\n"
        "        return false;\n"
        "    }\n"
        "    ppguarded_lex_cursor_v1_program_init(&result);\n",
        emitter->prefix);

    pp_cursor_c_v1_write(
        emitter,
        "    result.dfa.state_len = UINT32_C(%u);\n"
        "    result.dfa.transition_len = UINT32_C(%u);\n"
        "    result.dfa.accept_tag_len = UINT32_C(%u);\n"
        "    result.dfa.eof_accept_tag_len = UINT32_C(%u);\n"
        "    result.dfa.start_state = UINT32_C(%u);\n"
        "    result.dfa.tag_len = UINT32_C(%u);\n",
        program->dfa.state_len, program->dfa.transition_len,
        program->dfa.accept_tag_len, program->dfa.eof_accept_tag_len,
        program->dfa.start_state, program->dfa.tag_len);
    if (program->dfa.state_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.dfa.states, %s_dfa_states, "
            "sizeof(%s_dfa_states), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    if (program->dfa.transition_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.dfa.transitions, "
            "%s_dfa_transitions, sizeof(%s_dfa_transitions), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    if (program->dfa.accept_tag_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.dfa.accept_tags, "
            "%s_dfa_accept_tags, sizeof(%s_dfa_accept_tags), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    if (program->dfa.eof_accept_tag_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.dfa.eof_accept_tags, "
            "%s_dfa_eof_accept_tags, sizeof(%s_dfa_eof_accept_tags), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }

    pp_cursor_c_v1_slr_assign(
        emitter, "result.final_slr", "final_slr", &program->final_slr);
    if (program->final_slr.production_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.final_production_lhs_indices, "
            "%s_final_lhs_indices, sizeof(%s_final_lhs_indices), "
            "error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    if (program->terminal_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.terminals, %s_terminals, "
            "sizeof(%s_terminals), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    if (program->range_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    if (!%s_copy((void **)&result.ranges, %s_ranges, "
            "sizeof(%s_ranges), error_buf, error_buf_size)) goto fail;\n",
            emitter->prefix, emitter->prefix, emitter->prefix);
    }
    if (program->value_program_len > 0u) {
        pp_cursor_c_v1_write(
            emitter,
            "    result.value_programs = calloc(UINT32_C(%u), "
            "sizeof(*result.value_programs));\n"
            "    if (!result.value_programs) {\n"
            "        if (error_buf && error_buf_size > 0u)\n"
            "            snprintf(error_buf, error_buf_size, "
            "\"cannot allocate generated value programs\");\n"
            "        goto fail;\n"
            "    }\n"
            "    result.value_program_len = UINT32_C(%u);\n",
            program->value_program_len, program->value_program_len);
        for (index = 0u; index < program->value_program_len; index++)
            pp_cursor_c_v1_value_assign(
                emitter, &program->value_programs[index], index);
    }
    pp_cursor_c_v1_write(
        emitter,
        "    result.terminal_len = UINT32_C(%u);\n"
        "    result.value_program_conflict_len = UINT32_C(%u);\n"
        "    result.range_len = UINT32_C(%u);\n"
        "    result.value_programs_complete = %s;\n"
        "    result.actions_bound = %s;\n",
        program->terminal_len, program->value_program_conflict_len,
        program->range_len,
        program->value_programs_complete ? "true" : "false",
        program->actions_bound ? "true" : "false");

    if (program->actions_bound) {
        const PPActionBytecodeV1Program *actions = &program->actions;
        pp_cursor_c_v1_write(
            emitter,
            "    result.actions.production_len = UINT32_C(%u);\n"
            "    result.actions.instruction_len = UINT32_C(%u);\n",
            actions->production_len, actions->instruction_len);
        if (actions->production_len > 0u) {
            pp_cursor_c_v1_write(
                emitter,
                "    if (!%s_copy((void **)&result.actions.productions, "
                "%s_action_productions, sizeof(%s_action_productions), "
                "error_buf, error_buf_size)) goto fail;\n",
                emitter->prefix, emitter->prefix, emitter->prefix);
        }
        if (actions->instruction_len > 0u) {
            pp_cursor_c_v1_write(
                emitter,
                "    if (!%s_copy((void **)&result.actions.instructions, "
                "%s_action_instructions, sizeof(%s_action_instructions), "
                "error_buf, error_buf_size)) goto fail;\n",
                emitter->prefix, emitter->prefix, emitter->prefix);
        }
        for (index = 0u; index < actions->instruction_len; index++) {
            if (actions->instructions[index].kind ==
                    PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
                continue;
            }
            pp_cursor_c_v1_write(
                emitter,
                "    if (!fh_ground_term_v1_parse(&result.actions.arena, "
                "%s_action_term_%u, sizeof(%s_action_term_%u), "
                "&result.actions.instructions[%u].term, error_buf, "
                "error_buf_size)) goto fail;\n",
                emitter->prefix, index, emitter->prefix, index, index);
        }
        pp_cursor_c_v1_write(
            emitter, "    memcpy(result.actions.base_pack_digest, ");
        pp_cursor_c_v1_string(emitter, actions->base_pack_digest);
        pp_cursor_c_v1_write(
            emitter, ", sizeof(result.actions.base_pack_digest));\n"
            "    memcpy(result.actions.compiler_digest, ");
        pp_cursor_c_v1_string(emitter, actions->compiler_digest);
        pp_cursor_c_v1_write(
            emitter, ", sizeof(result.actions.compiler_digest));\n"
            "    memcpy(result.actions.answer_set_digest, ");
        pp_cursor_c_v1_string(emitter, actions->answer_set_digest);
        pp_cursor_c_v1_write(
            emitter, ", sizeof(result.actions.answer_set_digest));\n"
            "    memcpy(result.actions.program_digest, ");
        pp_cursor_c_v1_string(emitter, actions->program_digest);
        pp_cursor_c_v1_write(
            emitter, ", sizeof(result.actions.program_digest));\n");
    }

    pp_cursor_c_v1_write(
        emitter, "    memcpy(result.base_pack_digest, ");
    pp_cursor_c_v1_string(emitter, program->base_pack_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(result.base_pack_digest));\n"
        "    memcpy(result.lexical_plan_digest, ");
    pp_cursor_c_v1_string(emitter, program->lexical_plan_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(result.lexical_plan_digest));\n"
        "    memcpy(result.guard_plan_digest, ");
    pp_cursor_c_v1_string(emitter, program->guard_plan_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(result.guard_plan_digest));\n"
        "    memcpy(result.guarded_plan_digest, ");
    pp_cursor_c_v1_string(emitter, program->guarded_plan_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(result.guarded_plan_digest));\n"
        "    memcpy(result.execution_plan_digest, ");
    pp_cursor_c_v1_string(emitter, program->execution_plan_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(result.execution_plan_digest));\n"
        "    memcpy(result.certificate_digest, ");
    pp_cursor_c_v1_string(emitter, program->certificate_digest);
    pp_cursor_c_v1_write(
        emitter, ", sizeof(result.certificate_digest));\n"
        "    memcpy(result.program_digest, ");
    pp_cursor_c_v1_string(emitter, program->program_digest);
    pp_cursor_c_v1_write(
        emitter,
        ", sizeof(result.program_digest));\n"
        "    if (!ppguarded_lex_cursor_v1_program_validate(\n"
        "            &result, error_buf, error_buf_size)) goto fail;\n"
        "    ppguarded_lex_cursor_v1_program_free(out);\n"
        "    *out = result;\n"
        "    return true;\n\n"
        "fail:\n"
        "    ppguarded_lex_cursor_v1_program_free(&result);\n"
        "    return false;\n"
        "}\n");
}

bool ppguarded_lex_cursor_v1_emit_c(
    const PPGuardedLexCursorV1Program *program,
    FILE *output,
    const char *identifier_prefix,
    char *error_buf,
    size_t error_buf_size) {
    PPCursorCEmitterV1 emitter;
    uint32_t index;
    char validation_error[256];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    memset(&emitter, 0, sizeof(emitter));
    emitter.output = output;
    emitter.prefix = identifier_prefix;
    emitter.error_buf = error_buf;
    emitter.error_buf_size = error_buf_size;
    if (!program || !output ||
        !pp_cursor_c_v1_identifier_valid(identifier_prefix)) {
        pp_cursor_c_v1_error(&emitter, "bad cursor C-emitter arguments");
        return false;
    }
    if (!program->actions_bound) {
        pp_cursor_c_v1_error(
            &emitter, "cursor C emitter requires bound semantic actions");
        return false;
    }
    validation_error[0] = '\0';
    if (!ppguarded_lex_cursor_v1_program_validate(
            program, validation_error, sizeof(validation_error))) {
        pp_cursor_c_v1_error(
            &emitter, "cannot emit invalid cursor program: %s",
            validation_error[0] ? validation_error : "validation failed");
        return false;
    }
    for (index = 0u; index < program->value_program_len; index++) {
        const PPGuardedLexCursorV1ValueProgram *value =
            &program->value_programs[index];
        if (value->slr.summary.state_len == 0u ||
            value->scalar_dispatch_range_len == 0u ||
            value->scalar_dispatch_range_len >
                UINT32_MAX / value->slr.summary.state_len) {
            pp_cursor_c_v1_error(
                &emitter, "value-program dispatch table is too large");
            return false;
        }
    }

    pp_cursor_c_v1_emit_preamble(&emitter, program->program_digest);
    pp_cursor_c_v1_emit_data(&emitter, program);
    pp_cursor_c_v1_emit_init(&emitter, program);
    if (!emitter.failed && ferror(output))
        pp_cursor_c_v1_error(&emitter, "generated C stream failed");
    return !emitter.failed;
}
