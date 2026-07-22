#ifndef CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_ACTION_V1_H
#define CETTA_GSLT2PARSE_PARSER_ATOM_PROJECTION_ACTION_V1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parser_action_bytecode_v1.h"
#include "parser_atom_projection_v1.h"

typedef enum {
    PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT = 0,
    PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT = 1,
    PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR = 2,
    PPATOM_PROJECTION_ACTION_V1_APPLY_CONS = 3,
    PPATOM_PROJECTION_ACTION_V1_APPLY_NODE = 4
} PPAtomProjectionActionV1InstructionKind;

typedef enum {
    PPATOM_PROJECTION_ACTION_V1_EXECUTE_INTERPRETED = 0,
    PPATOM_PROJECTION_ACTION_V1_EXECUTE_VALUE = 1,
    PPATOM_PROJECTION_ACTION_V1_EXECUTE_BINARY = 2
} PPAtomProjectionActionV1ExecutionKind;

typedef enum {
    PPATOM_PROJECTION_ACTION_V1_CONSTANT_OPAQUE = 0,
    PPATOM_PROJECTION_ACTION_V1_CONSTANT_NIL = 1,
    PPATOM_PROJECTION_ACTION_V1_CONSTANT_DOCUMENT = 2,
    PPATOM_PROJECTION_ACTION_V1_CONSTANT_EXPRESSION = 3,
    PPATOM_PROJECTION_ACTION_V1_CONSTANT_RULE = 4,
    PPATOM_PROJECTION_ACTION_V1_CONSTANT_WRAPPER = 5
} PPAtomProjectionActionV1ConstantKind;

typedef struct {
    PPAtomProjectionActionV1InstructionKind kind;
    PPAtomProjectionActionV1ConstantKind constant_kind;
    uint32_t operand;
    uint32_t dense_id;
    Atom *term;
} PPAtomProjectionActionV1Instruction;

typedef struct {
    uint32_t instruction_begin;
    uint32_t instruction_len;
    uint32_t arity;
    uint32_t max_stack_len;
    PPAtomProjectionActionV1ExecutionKind execution_kind;
} PPAtomProjectionActionV1Production;

/*
 * Projection-specialized action bytecode.  Application heads and semantic
 * constants are classified once against the compiled projection plan; no
 * guest vocabulary or runtime symbol dispatch is introduced here.
 */
typedef struct {
    Arena arena;
    Atom *codepoint_label;
    PPAtomProjectionActionV1Production *productions;
    PPAtomProjectionActionV1Instruction *instructions;
    uint32_t production_len;
    uint32_t instruction_len;
    char action_program_digest[65];
    char projection_plan_digest[65];
    char program_digest[65];
} PPAtomProjectionActionV1Program;

void ppatom_projection_action_v1_program_init(
    PPAtomProjectionActionV1Program *program);

void ppatom_projection_action_v1_program_free(
    PPAtomProjectionActionV1Program *program);

/* Failed replacement is atomic: an already-built program remains usable. */
bool ppatom_projection_action_v1_program_build_prevalidated(
    PPAtomProjectionActionV1Program *program,
    const PPActionBytecodeV1Program *actions,
    const PPAtomProjectionV1Plan *projection,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_action_v1_program_validate(
    const PPAtomProjectionActionV1Program *program,
    const PPActionBytecodeV1Program *actions,
    const PPAtomProjectionV1Plan *projection,
    char *error_buf,
    size_t error_buf_size);

typedef enum {
    PPATOM_PROJECTION_ACTION_V1_VALUE_SCALAR = 0,
    PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT = 1,
    PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE = 2,
    PPATOM_PROJECTION_ACTION_V1_VALUE_EOF = 3,
    PPATOM_PROJECTION_ACTION_V1_VALUE_TEXT = 4
} PPAtomProjectionActionV1ValueKind;

/* Eight-byte semantic handle used by deterministic parser value stacks. */
typedef struct {
    PPAtomProjectionActionV1ValueKind kind;
    uint32_t operand;
} PPAtomProjectionActionV1Value;

typedef struct {
    void *implementation;
} PPAtomProjectionActionV1Store;

void ppatom_projection_action_v1_store_init(
    PPAtomProjectionActionV1Store *store);

void ppatom_projection_action_v1_store_free(
    PPAtomProjectionActionV1Store *store);

/* Invalidates every composite and retained-text handle from this store. */
void ppatom_projection_action_v1_store_reset(
    PPAtomProjectionActionV1Store *store);

bool ppatom_projection_action_v1_scalar(
    uint32_t scalar,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size);

bool ppatom_projection_action_v1_eof(
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * A build-time binding for one projection rule's compact text-node carrier.
 * Instruction identities are derived from the projection-specialized action
 * program; callers never infer carrier spellings or guest-language labels.
 */
typedef struct {
    uint32_t rule_dense_id;
    uint32_t rule_constant_instruction;
    uint32_t node_instruction;
} PPAtomProjectionActionV1TextBinding;

bool ppatom_projection_action_v1_text_binding(
    const PPAtomProjectionActionV1Program *program,
    uint32_t rule_dense_id,
    PPAtomProjectionActionV1TextBinding *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Retain one already-decoded scalar sequence as a compact semantic leaf.
 * This is a projection quotient: project_document emits the same dense text
 * events, while value_to_atom deliberately rejects it because the original
 * pair/cons bracketing is not represented by this carrier.
 */
bool ppatom_projection_action_v1_text_node_prevalidated(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1TextBinding *binding,
    const uint32_t *scalars,
    uint32_t scalar_len,
    PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute one prevalidated production.  Failure rolls the compact node store
 * back to its entry mark, so a rejected action cannot leak partial carriers.
 */
bool ppatom_projection_action_v1_execute_prevalidated(
    const PPAtomProjectionActionV1Program *program,
    uint32_t production_id,
    const PPAtomProjectionActionV1Value *slots,
    uint32_t slot_len,
    PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Execute a build-time-classified action without materializing a temporary
 * slot vector.  Each slot_stride-byte record must begin with the object
 * representation of PPAtomProjectionActionV1Value.  The error buffer is
 * written only on failure; successful hot-path calls leave it untouched.
 * Interpreted actions remain a fail-closed fallback for future bytecode
 * shapes, while VALUE and BINARY actions use their validated direct forms.
 */
bool ppatom_projection_action_v1_execute_specialized_view_prevalidated(
    const PPAtomProjectionActionV1Program *program,
    uint32_t production_id,
    const void *slots,
    uint32_t slot_len,
    size_t slot_stride,
    PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size);

/* Reference reconstruction used to prove carrier equality with Atom replay. */
bool ppatom_projection_action_v1_value_to_atom(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value value,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom **out,
    char *error_buf,
    size_t error_buf_size);

/*
 * Emit the exact dense event stream for one compact document root and build
 * host Atoms through the shared atomic event consumer.
 */
bool ppatom_projection_action_v1_project_document(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value document,
    const PPAtomProjectionV1Plan *projection,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom ***out_atoms,
    uint32_t *out_len,
    char *error_buf,
    size_t error_buf_size);

#endif
