/* Generated from the admitted RuleMachine Program GSLT package. */
/* Regenerate with tools/generate_rule_machine_program_v1.py. */
#ifndef CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H
#define CETTA_RULE_MACHINE_PROGRAM_V1_GENERATED_H

#define RM_RULE_PROGRAM_GSLT_DIGEST "447a3672d6843d2eba97067f5a24d398b685017ea9b0fac189928eb5739bc7dd"
#define RM_RULE_PROGRAM_GSLT_IDENTITY "HilbertBFCProgramSpecializationV1-447a3672d6843d2eba97067f5a24d398b685017ea9b0fac189928eb5739bc7dd"

static const RMRuleProgramGeneratedOp rm_generated_rule_program_axiom_ops[] = {
    {"rmbc-require-fun", 2, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r0", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r1", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-instantiate-template", 2, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r2", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "template0", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-unify", 2, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r0", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r2", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-set-type", 1, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r1", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-proof-apply", 1, {{RM_RULE_PROGRAM_OPERAND_PROOF, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-hyp-add", 1, {{RM_RULE_PROGRAM_OPERAND_INT, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-emit", 0, {{RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
};
static const uint32_t rm_generated_rule_program_axiom_op_count =
    (uint32_t)(sizeof(rm_generated_rule_program_axiom_ops) /
               sizeof(rm_generated_rule_program_axiom_ops[0]));

static const RMRuleProgramGeneratedOp rm_generated_rule_program_inverse_mp_ops[] = {
    {"rmbc-require-fun", 2, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r0", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r1", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-fresh", 1, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r2", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-make-imp", 3, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r3", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r2", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r0", INT64_C(0)}}},
    {"rmbc-make-fun", 3, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r4", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r2", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r1", INT64_C(0)}}},
    {"rmbc-make-fun", 3, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r5", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r3", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_SYMBOL, "r4", INT64_C(0)}}},
    {"rmbc-set-type", 1, {{RM_RULE_PROGRAM_OPERAND_SYMBOL, "r5", INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-proof-wrap", 1, {{RM_RULE_PROGRAM_OPERAND_PROOF, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-hyp-add", 1, {{RM_RULE_PROGRAM_OPERAND_INT, NULL, INT64_C(2)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
    {"rmbc-emit", 0, {{RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}, {RM_RULE_PROGRAM_OPERAND_NONE, NULL, INT64_C(0)}}},
};
static const uint32_t rm_generated_rule_program_inverse_mp_op_count =
    (uint32_t)(sizeof(rm_generated_rule_program_inverse_mp_ops) /
               sizeof(rm_generated_rule_program_inverse_mp_ops[0]));

#endif
