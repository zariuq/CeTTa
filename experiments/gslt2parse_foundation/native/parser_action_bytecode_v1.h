#ifndef CETTA_GSLT2PARSE_PARSER_ACTION_BYTECODE_V1_H
#define CETTA_GSLT2PARSE_PARSER_ACTION_BYTECODE_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_pack_abi_v1.h"
#include "parser_pack_guard_plan_v1.h"

typedef enum {
    PP_ACTION_BYTECODE_V1_PUSH_SLOT = 0,
    PP_ACTION_BYTECODE_V1_PUSH_CONST = 1,
    PP_ACTION_BYTECODE_V1_APPLY = 2
} PPActionBytecodeV1InstructionKind;

typedef struct {
    PPActionBytecodeV1InstructionKind kind;
    uint32_t operand;
    Atom *term;
} PPActionBytecodeV1Instruction;

typedef struct {
    uint32_t instruction_begin;
    uint32_t instruction_len;
    uint32_t arity;
    uint32_t max_stack_len;
} PPActionBytecodeV1Production;

/*
 * Flat postfix semantic-action IR produced by the action-compiler GSLT.
 * Constant terms and application heads are owned by arena.  Production
 * indexes are the dense ParserPack production indexes, so recognition tables
 * and actions share one dispatch identity without carrying language policy.
 */
typedef struct {
    Arena arena;
    PPActionBytecodeV1Production *productions;
    PPActionBytecodeV1Instruction *instructions;
    uint32_t production_len;
    uint32_t instruction_len;
    char base_pack_digest[65];
    char compiler_digest[65];
    char answer_set_digest[65];
    char program_digest[65];
} PPActionBytecodeV1Program;

void pp_action_bytecode_v1_program_init(PPActionBytecodeV1Program *program);
void pp_action_bytecode_v1_program_free(PPActionBytecodeV1Program *program);

bool pp_action_bytecode_v1_program_build(
    const PPABIV1Pack *pack,
    Atom *const *compiler_answers,
    size_t compiler_answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    PPActionBytecodeV1Program *out,
    char *error_buf,
    size_t error_buf_size);

bool pp_action_bytecode_v1_program_validate(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    char *error_buf,
    size_t error_buf_size);

/*
 * Compose the base ParserPack action inventory with a validated positive-
 * guard production sidecar.  Compiler answers must use the
 * compile-guard-extended-action-program relation and cover the combined
 * dense production range exactly once.
 */
bool pp_action_bytecode_v1_program_build_guard_extended(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    Atom *const *compiler_answers,
    size_t compiler_answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    PPActionBytecodeV1Program *out,
    char *error_buf,
    size_t error_buf_size);

bool pp_action_bytecode_v1_program_validate_guard_extended(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    char *error_buf,
    size_t error_buf_size);

/*
 * Slot values are borrowed.  Constants and application nodes are allocated
 * in result_arena.  Consequently a result that is exactly a slot remains
 * borrowed, while every constructed result is owned by result_arena.
 */
bool pp_action_bytecode_v1_execute_prevalidated(
    const PPActionBytecodeV1Program *program,
    uint32_t production_id,
    Atom *const *slots,
    uint32_t slot_len,
    Arena *result_arena,
    Atom **out,
    char *error_buf,
    size_t error_buf_size);

bool pp_action_bytecode_v1_execute(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    uint32_t production_id,
    Atom *const *slots,
    uint32_t slot_len,
    Arena *result_arena,
    Atom **out,
    char *error_buf,
    size_t error_buf_size);

bool pp_action_bytecode_v1_execute_guard_extended(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    uint32_t production_id,
    Atom *const *slots,
    uint32_t slot_len,
    Arena *result_arena,
    Atom **out,
    char *error_buf,
    size_t error_buf_size);

#endif
