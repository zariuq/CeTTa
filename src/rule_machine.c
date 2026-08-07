#include "rule_machine.h"

#include "match.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    RM_MAX_RULE_DEPTH = 1024,
    RM_MAX_ARTIFACT_BLOCKS = 1000000,
};

typedef struct {
    Atom *id;
    Atom *source;
    Atom *proof;
    Atom *premises;
    Atom *conclusion;
} RMSourceBlock;

typedef struct {
    Atom *id;
    Atom *source;
    Atom *proof;
    Atom *premises;
    Atom *conclusion;
} RMBytecodeBlock;

typedef struct {
    Atom **items;
    uint32_t len;
    uint32_t cap;
} RMAtomVec;

typedef struct {
    Arena *arena;
    Atom **blocks;
    uint32_t block_count;
    Atom *revision;
    uint64_t max_states;
    uint64_t max_occurrences;
    uint64_t states;
    uint64_t block_attempts;
    uint64_t block_matches;
    uint64_t occurrences_seen;
    uint32_t requested_depth;
    const char *limit_reason;
    RMAtomVec answers;
} RMRun;

typedef void (*RMContinuation)(RMRun *run, BindingsBuilder *builder, void *ctx);

typedef struct {
    uint32_t depth;
    Atom *rest;
    RMContinuation continuation;
    void *continuation_ctx;
} RMPremiseContinuation;

typedef struct {
    Atom *proof_var;
} RMAnswerContinuation;

typedef enum {
    RM_RULE_PROGRAM_AXIOM,
    RM_RULE_PROGRAM_INVERSE_MP,
} RMRuleProgramRuleKind;

typedef struct {
    RMRuleProgramRuleKind kind;
    Atom *id;
    Atom *source;
    Atom *schema;
    Atom *proof_symbol;
    Atom *code;
} RMRuleProgramRule;

typedef enum {
    RM_RULE_PROGRAM_OPERAND_NONE,
    RM_RULE_PROGRAM_OPERAND_SYMBOL,
    RM_RULE_PROGRAM_OPERAND_INT,
    RM_RULE_PROGRAM_OPERAND_PROOF,
} RMRuleProgramGeneratedOperandKind;

typedef struct {
    RMRuleProgramGeneratedOperandKind kind;
    const char *symbol;
    int64_t integer;
} RMRuleProgramGeneratedOperand;

typedef struct {
    const char *opcode;
    uint8_t nargs;
    RMRuleProgramGeneratedOperand operands[3];
} RMRuleProgramGeneratedOp;

#include "generated/rule_machine_program_v1.generated.h"

typedef struct {
    int32_t depth;
    int32_t hypotheses;
    Atom *type;
    Atom *proof;
    uint32_t next_rule;
    ArenaMark pop_mark;
    bool entered;
} RMRuleProgramFrame;

typedef struct {
    RMRuleProgramFrame *items;
    uint32_t len;
    uint32_t cap;
} RMRuleProgramFrames;

typedef struct {
    Arena *output;
    Arena scratch;
    RMRuleProgramRule *rules;
    uint32_t rule_count;
    RMRuleProgramFrames frames;
    RMAtomVec proofs;
    Atom *target;
    Atom *revision;
    uint64_t max_states;
    uint64_t max_occurrences;
    uint64_t states;
    uint64_t rule_attempts;
    uint64_t rule_successes;
    uint64_t accepted;
    uint32_t max_size;
    uint32_t max_search_stack;
    bool native_backend;
    const char *limit_reason;
} RMRuleProgramRun;

static bool rm_is_symbol(Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr(atom), name) == 0;
}

static bool rm_is_expr_head(Atom *atom, const char *name,
                            CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           rm_is_symbol(atom->expr.elems[0], name);
}

static Atom *rm_expr(Arena *arena, CettaExprLen length, Atom **items) {
    Atom **copy = length
        ? arena_alloc(arena, sizeof(Atom *) * length)
        : NULL;
    for (CettaExprIndex i = 0; i < length; ++i)
        copy[i] = items[i];
    return atom_expr(arena, copy, length);
}

static Atom *rm_call(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (nargs + 1));
    items[0] = head;
    for (uint32_t i = 0; i < nargs; ++i)
        items[i + 1] = args[i];
    return atom_expr(arena, items, nargs + 1);
}

static Atom *rm_error(Arena *arena, Atom *head, Atom **args, uint32_t nargs,
                      const char *reason) {
    return atom_error(arena, rm_call(arena, head, args, nargs),
                      atom_symbol(arena, reason));
}

static bool rm_vec_push(RMAtomVec *vec, Atom *atom) {
    if (vec->len == vec->cap) {
        uint32_t next = vec->cap ? vec->cap * 2u : 16u;
        if (next < vec->cap)
            return false;
        vec->items = cetta_realloc(vec->items, sizeof(Atom *) * next);
        vec->cap = next;
    }
    vec->items[vec->len++] = atom;
    return true;
}

static void rm_vec_free(RMAtomVec *vec) {
    free(vec->items);
    vec->items = NULL;
    vec->len = 0;
    vec->cap = 0;
}

static bool rm_parse_nonnegative_int(Atom *atom, uint64_t *out) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_INT || atom->ground.ival < 0)
        return false;
    *out = (uint64_t)atom->ground.ival;
    return true;
}

static bool rm_parse_delta(Atom *delta, uint64_t *built,
                           uint64_t *index_built, uint64_t *reused) {
    return rm_is_expr_head(delta, "compile-delta", 4) &&
           rm_parse_nonnegative_int(delta->expr.elems[1], built) &&
           rm_parse_nonnegative_int(delta->expr.elems[2], index_built) &&
           rm_parse_nonnegative_int(delta->expr.elems[3], reused);
}

static bool rm_delta_matches_count(Atom *delta, uint32_t count) {
    uint64_t built = 0;
    uint64_t index_built = 0;
    uint64_t reused = 0;
    return rm_parse_delta(delta, &built, &index_built, &reused) &&
           built == index_built && built <= count && reused <= count &&
           built + reused == count;
}

static bool rm_validate_premises(Atom *premises) {
    uint32_t count = 0;
    Atom *cursor = premises;
    while (!rm_is_symbol(cursor, "rm-nil")) {
        if (!rm_is_expr_head(cursor, "rm-cons", 3) ||
            !rm_is_expr_head(cursor->expr.elems[1], "rm-premise", 3))
            return false;
        cursor = cursor->expr.elems[2];
        if (++count > RM_MAX_ARTIFACT_BLOCKS)
            return false;
    }
    return true;
}

static bool rm_parse_source_block(Atom *atom, RMSourceBlock *out) {
    if (!rm_is_expr_head(atom, "rm-block", 6))
        return false;
    *out = (RMSourceBlock){
        .id = atom->expr.elems[1],
        .source = atom->expr.elems[2],
        .proof = atom->expr.elems[3],
        .premises = atom->expr.elems[4],
        .conclusion = atom->expr.elems[5],
    };
    return rm_validate_premises(out->premises);
}

static bool rm_parse_bytecode_block(Atom *atom, RMBytecodeBlock *out) {
    if (!rm_is_expr_head(atom, "bc-block", 7) ||
        !rm_is_expr_head(atom->expr.elems[3], "bc-match", 2) ||
        !rm_is_expr_head(atom->expr.elems[4], "bc-goals", 2) ||
        !rm_is_expr_head(atom->expr.elems[5], "bc-build", 2) ||
        !rm_is_symbol(atom->expr.elems[6], "bc-emit"))
        return false;
    *out = (RMBytecodeBlock){
        .id = atom->expr.elems[1],
        .source = atom->expr.elems[2],
        .conclusion = atom->expr.elems[3]->expr.elems[1],
        .premises = atom->expr.elems[4]->expr.elems[1],
        .proof = atom->expr.elems[5]->expr.elems[1],
    };
    return rm_validate_premises(out->premises);
}

static bool rm_same_var(Atom *left, Atom *right) {
    return left && right && left->kind == ATOM_VAR &&
           right->kind == ATOM_VAR && left->var_id == right->var_id;
}

static int rm_symbol_atom_ptr_cmp(const void *left, const void *right) {
    Atom *left_atom = *(Atom *const *)left;
    Atom *right_atom = *(Atom *const *)right;
    return strcmp(atom_name_cstr(left_atom), atom_name_cstr(right_atom));
}

static bool rm_rule_program_term_supported(Atom *term) {
    if (!term)
        return false;
    if (term->kind == ATOM_VAR || term->kind == ATOM_SYMBOL)
        return true;
    if (rm_is_expr_head(term, "neg", 2))
        return rm_rule_program_term_supported(term->expr.elems[1]);
    if (rm_is_expr_head(term, "imp", 3))
        return rm_rule_program_term_supported(term->expr.elems[1]) &&
               rm_rule_program_term_supported(term->expr.elems[2]);
    return false;
}

static bool rm_rule_program_axiom(const RMBytecodeBlock *block, RMRuleProgramRule *out) {
    if (!rm_is_symbol(block->premises, "rm-nil") ||
        !rm_is_expr_head(block->proof, "rm-proof-atom", 2) ||
        !rm_rule_program_term_supported(block->conclusion))
        return false;
    *out = (RMRuleProgramRule){
        .kind = RM_RULE_PROGRAM_AXIOM,
        .id = block->id,
        .source = block->source,
        .schema = block->conclusion,
        .proof_symbol = block->proof->expr.elems[1],
    };
    return out->proof_symbol->kind == ATOM_SYMBOL;
}

static bool rm_rule_program_inverse_mp(const RMBytecodeBlock *block,
                                RMRuleProgramRule *out) {
    if (!rm_is_expr_head(block->premises, "rm-cons", 3) ||
        !rm_is_expr_head(block->premises->expr.elems[1], "rm-premise", 3) ||
        !rm_is_expr_head(block->premises->expr.elems[2], "rm-cons", 3) ||
        !rm_is_expr_head(
            block->premises->expr.elems[2]->expr.elems[1],
            "rm-premise", 3) ||
        !rm_is_symbol(
            block->premises->expr.elems[2]->expr.elems[2], "rm-nil") ||
        !rm_is_expr_head(block->proof, "rm-proof-app", 3))
        return false;

    Atom *first = block->premises->expr.elems[1];
    Atom *second = block->premises->expr.elems[2]->expr.elems[1];
    Atom *first_proof = first->expr.elems[1];
    Atom *antecedent = first->expr.elems[2];
    Atom *second_proof = second->expr.elems[1];
    Atom *implication = second->expr.elems[2];
    Atom *proof_args = block->proof->expr.elems[2];
    if (!rm_is_expr_head(implication, "imp", 3) ||
        !rm_same_var(antecedent, implication->expr.elems[1]) ||
        !rm_same_var(block->conclusion, implication->expr.elems[2]) ||
        !rm_is_expr_head(proof_args, "rm-cons", 3) ||
        !rm_same_var(first_proof, proof_args->expr.elems[1]) ||
        !rm_is_expr_head(proof_args->expr.elems[2], "rm-cons", 3) ||
        !rm_same_var(second_proof,
                     proof_args->expr.elems[2]->expr.elems[1]) ||
        !rm_is_symbol(proof_args->expr.elems[2]->expr.elems[2], "rm-nil") ||
        block->proof->expr.elems[1]->kind != ATOM_SYMBOL)
        return false;
    *out = (RMRuleProgramRule){
        .kind = RM_RULE_PROGRAM_INVERSE_MP,
        .id = block->id,
        .source = block->source,
        .schema = NULL,
        .proof_symbol = block->proof->expr.elems[1],
    };
    return true;
}

static bool rm_rule_program_admit(Atom **blocks, uint32_t count,
                           RMRuleProgramRule **rules_out) {
    RMRuleProgramRule *rules = count
        ? cetta_malloc(sizeof(RMRuleProgramRule) * count)
        : NULL;
    uint32_t inverse_mp_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        RMBytecodeBlock block;
        if (!rm_parse_bytecode_block(blocks[i], &block) ||
            (!rm_rule_program_axiom(&block, &rules[i]) &&
             !rm_rule_program_inverse_mp(&block, &rules[i]))) {
            free(rules);
            return false;
        }
        if (rules[i].kind == RM_RULE_PROGRAM_INVERSE_MP)
            ++inverse_mp_count;
    }
    if (count == 0 || inverse_mp_count != 1) {
        free(rules);
        return false;
    }
    *rules_out = rules;
    return true;
}

static bool rm_parse_artifact(Atom *artifact, Atom **revision,
                              Atom **delta, Atom **chain);
static bool rm_collect_chain(Atom *chain, RMAtomVec *blocks);
static Atom *rm_delta(Arena *arena, uint64_t built, uint64_t index_built,
                      uint64_t reused);

static Atom *rm_rule_program_op(Arena *arena, const char *name,
                         Atom **args, uint32_t nargs) {
    Atom **items = arena_alloc(arena, sizeof(Atom *) * (nargs + 1u));
    items[0] = atom_symbol(arena, name);
    for (uint32_t i = 0; i < nargs; ++i)
        items[i + 1u] = args[i];
    return rm_expr(arena, (CettaExprLen)(nargs + 1u), items);
}

static Atom *rm_rule_program_list(Arena *arena, Atom **items, uint32_t count) {
    Atom *list = atom_symbol(arena, "rule-program-nil");
    while (count > 0) {
        Atom *cons_items[] = {
            atom_symbol(arena, "rule-program-cons"), items[--count], list,
        };
        list = rm_expr(arena, 3, cons_items);
    }
    return list;
}

static Atom *rm_rule_program_code(Arena *arena, Atom **ops, uint32_t count) {
    Atom *items[] = {
        atom_symbol(arena, "rule-program-code"),
        rm_rule_program_list(arena, ops, count),
    };
    return rm_expr(arena, 2, items);
}

static void rm_rule_program_generated_program(
    RMRuleProgramRuleKind kind, const RMRuleProgramGeneratedOp **ops,
    uint32_t *count) {
    if (kind == RM_RULE_PROGRAM_AXIOM) {
        *ops = rm_generated_rule_program_axiom_ops;
        *count = rm_generated_rule_program_axiom_op_count;
    } else {
        *ops = rm_generated_rule_program_inverse_mp_ops;
        *count = rm_generated_rule_program_inverse_mp_op_count;
    }
}

static Atom *rm_rule_program_generated_operand(
    Arena *arena, const RMRuleProgramGeneratedOperand *operand,
    Atom *proof_symbol) {
    switch (operand->kind) {
    case RM_RULE_PROGRAM_OPERAND_SYMBOL:
        return atom_symbol(arena, operand->symbol);
    case RM_RULE_PROGRAM_OPERAND_INT:
        return atom_int(arena, operand->integer);
    case RM_RULE_PROGRAM_OPERAND_PROOF:
        return proof_symbol;
    case RM_RULE_PROGRAM_OPERAND_NONE:
        break;
    }
    return NULL;
}

static Atom *rm_rule_program_block(Arena *arena,
                                    const RMRuleProgramRule *rule) {
    Atom *template_item = rule->kind == RM_RULE_PROGRAM_AXIOM
        ? term_universe_canonicalize_atom(arena, rule->schema)
        : atom_symbol(arena, "rule-program-none");
    Atom *template_items[] = {
        atom_symbol(arena, "rule-program-template"), template_item,
    };
    Atom *template = rm_expr(arena, 2, template_items);
    const RMRuleProgramGeneratedOp *program = NULL;
    uint32_t program_count = 0;
    rm_rule_program_generated_program(rule->kind, &program, &program_count);
    Atom **ops = arena_alloc(arena, sizeof(Atom *) * program_count);
    for (uint32_t i = 0; i < program_count; ++i) {
        Atom *args[3] = {0};
        for (uint8_t j = 0; j < program[i].nargs; ++j) {
            args[j] = rm_rule_program_generated_operand(
                arena, &program[i].operands[j], rule->proof_symbol);
        }
        ops[i] = program[i].nargs == 0
            ? atom_symbol(arena, program[i].opcode)
            : rm_rule_program_op(
                arena, program[i].opcode, args, program[i].nargs);
    }
    Atom *code = rm_rule_program_code(arena, ops, program_count);
    Atom *block_items[] = {
        atom_symbol(arena, "rule-program-block"), rule->id, rule->source,
        template, code,
    };
    return rm_expr(arena, 5, block_items);
}

static Atom *rm_rule_program_declined(Arena *arena, Atom *revision);

static Atom *rm_rule_program(Arena *arena, Atom *revision, Atom *delta,
                             Atom *chain) {
    Atom *items[] = {
        atom_symbol(arena, "rule-program-v1"),
        atom_symbol(arena, "HilbertBFCProgramV1"),
        atom_symbol(arena, RM_RULE_PROGRAM_GSLT_IDENTITY), revision,
        atom_symbol(arena, "proof-occurrence-bag"), delta, chain,
    };
    return rm_expr(arena, 7, items);
}

static Atom *rm_compile_rule_program(Arena *arena, Atom *head, Atom **args,
                                     uint32_t nargs) {
    if (nargs != 1)
        return rm_error(arena, head, args, nargs,
                        "ExpectedCompiledArtifact");
    Atom *revision = NULL;
    Atom *delta = NULL;
    Atom *chain = NULL;
    if (!rm_parse_artifact(args[0], &revision, &delta, &chain))
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifact");
    RMAtomVec blocks = {0};
    if (!rm_collect_chain(chain, &blocks)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactChain");
    }
    if (!rm_delta_matches_count(delta, blocks.len)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifact");
    }
    RMRuleProgramRule *rules = NULL;
    if (!rm_rule_program_admit(blocks.items, blocks.len, &rules)) {
        rm_vec_free(&blocks);
        return rm_rule_program_declined(arena, revision);
    }
    Atom *empty_items[] = {
        atom_symbol(arena, "rule-program-empty"),
        atom_symbol(arena, "HilbertBFCProgramV1"),
        atom_symbol(arena, RM_RULE_PROGRAM_GSLT_IDENTITY),
    };
    Atom *program_chain = rm_expr(arena, 3, empty_items);
    for (uint32_t i = 0; i < blocks.len; ++i) {
        Atom *link_items[] = {
            atom_symbol(arena, "rule-program-link"), program_chain,
            rm_rule_program_block(arena, &rules[i]),
        };
        program_chain = rm_expr(arena, 3, link_items);
    }
    Atom *result = rm_rule_program(
        arena, revision, rm_delta(arena, blocks.len, blocks.len, 0),
        program_chain);
    free(rules);
    rm_vec_free(&blocks);
    return result;
}

static bool rm_rule_program_generated_op_matches(
    Atom *op, const RMRuleProgramGeneratedOp *expected,
    Atom **proof_symbol) {
    if (expected->nargs == 0)
        return rm_is_symbol(op, expected->opcode);
    if (!rm_is_expr_head(
            op, expected->opcode, (CettaExprLen)(expected->nargs + 1u)))
        return false;
    for (uint8_t i = 0; i < expected->nargs; ++i) {
        Atom *actual = op->expr.elems[i + 1u];
        const RMRuleProgramGeneratedOperand *operand = &expected->operands[i];
        switch (operand->kind) {
        case RM_RULE_PROGRAM_OPERAND_SYMBOL:
            if (!rm_is_symbol(actual, operand->symbol))
                return false;
            break;
        case RM_RULE_PROGRAM_OPERAND_INT:
            if (!actual || actual->kind != ATOM_GROUNDED ||
                actual->ground.gkind != GV_INT ||
                actual->ground.ival != operand->integer)
                return false;
            break;
        case RM_RULE_PROGRAM_OPERAND_PROOF:
            if (actual->kind != ATOM_SYMBOL ||
                (*proof_symbol && !atom_eq(*proof_symbol, actual)))
                return false;
            *proof_symbol = actual;
            break;
        case RM_RULE_PROGRAM_OPERAND_NONE:
            return false;
        }
    }
    return true;
}

static bool rm_rule_program_generated_code_matches(
    Atom *code, const RMRuleProgramGeneratedOp *program,
    uint32_t program_count, Atom **proof_symbol) {
    if (!rm_is_expr_head(code, "rule-program-code", 2))
        return false;
    Atom *cursor = code->expr.elems[1];
    for (uint32_t i = 0; i < program_count; ++i) {
        if (!rm_is_expr_head(cursor, "rule-program-cons", 3) ||
            !rm_rule_program_generated_op_matches(
                cursor->expr.elems[1], &program[i], proof_symbol))
            return false;
        cursor = cursor->expr.elems[2];
    }
    return rm_is_symbol(cursor, "rule-program-nil") && *proof_symbol;
}

static bool rm_parse_rule_program_block(Atom *atom, RMRuleProgramRule *out) {
    if (!rm_is_expr_head(atom, "rule-program-block", 5) ||
        atom->expr.elems[1]->kind != ATOM_SYMBOL ||
        !rm_is_expr_head(atom->expr.elems[3], "rule-program-template", 2))
        return false;
    Atom *template_item = atom->expr.elems[3]->expr.elems[1];
    Atom *code = atom->expr.elems[4];
    Atom *proof_symbol = NULL;
    if (rm_rule_program_generated_code_matches(
            code, rm_generated_rule_program_axiom_ops,
            rm_generated_rule_program_axiom_op_count, &proof_symbol) &&
        !rm_is_symbol(template_item, "rule-program-none") &&
        rm_rule_program_term_supported(template_item)) {
        *out = (RMRuleProgramRule){
            .kind = RM_RULE_PROGRAM_AXIOM,
            .id = atom->expr.elems[1],
            .source = atom->expr.elems[2],
            .schema = template_item,
            .proof_symbol = proof_symbol,
            .code = code,
        };
        return true;
    }
    proof_symbol = NULL;
    if (rm_rule_program_generated_code_matches(
            code, rm_generated_rule_program_inverse_mp_ops,
            rm_generated_rule_program_inverse_mp_op_count, &proof_symbol) &&
        rm_is_symbol(template_item, "rule-program-none")) {
        *out = (RMRuleProgramRule){
            .kind = RM_RULE_PROGRAM_INVERSE_MP,
            .id = atom->expr.elems[1],
            .source = atom->expr.elems[2],
            .schema = NULL,
            .proof_symbol = proof_symbol,
            .code = code,
        };
        return true;
    }
    return false;
}

static bool rm_collect_rule_program_chain(Atom *chain, RMAtomVec *blocks) {
    Atom *cursor = chain;
    while (rm_is_expr_head(cursor, "rule-program-link", 3)) {
        if (blocks->len >= RM_MAX_ARTIFACT_BLOCKS ||
            !rm_vec_push(blocks, cursor->expr.elems[2]))
            return false;
        cursor = cursor->expr.elems[1];
    }
    if (!rm_is_expr_head(cursor, "rule-program-empty", 3) ||
        !rm_is_symbol(cursor->expr.elems[1], "HilbertBFCProgramV1") ||
        !rm_is_symbol(cursor->expr.elems[2], RM_RULE_PROGRAM_GSLT_IDENTITY))
        return false;
    for (uint32_t left = 0, right = blocks->len ? blocks->len - 1u : 0;
         left < right; ++left, --right) {
        Atom *tmp = blocks->items[left];
        blocks->items[left] = blocks->items[right];
        blocks->items[right] = tmp;
    }
    return true;
}

static bool rm_rule_program_header(Atom *program, Atom **revision,
                                    Atom **delta, Atom **chain) {
    if (!rm_is_expr_head(program, "rule-program-v1", 7) ||
        !rm_is_symbol(program->expr.elems[1], "HilbertBFCProgramV1") ||
        !rm_is_symbol(program->expr.elems[2], RM_RULE_PROGRAM_GSLT_IDENTITY) ||
        program->expr.elems[3]->kind != ATOM_SYMBOL ||
        !rm_is_symbol(program->expr.elems[4], "proof-occurrence-bag") ||
        !rm_is_expr_head(program->expr.elems[5], "compile-delta", 4))
        return false;
    *revision = program->expr.elems[3];
    *delta = program->expr.elems[5];
    *chain = program->expr.elems[6];
    return true;
}

static bool rm_parse_rule_program(Atom *program, Atom **revision,
                                   Atom **delta_out, Atom **chain_out,
                                   RMRuleProgramRule **rules_out,
                                   uint32_t *rule_count_out) {
    Atom *delta = NULL;
    Atom *chain = NULL;
    if (!rm_rule_program_header(program, revision, &delta, &chain))
        return false;
    RMAtomVec block_vec = {0};
    if (!rm_collect_rule_program_chain(chain, &block_vec)) {
        rm_vec_free(&block_vec);
        return false;
    }
    uint32_t count = block_vec.len;
    if (count == 0) {
        rm_vec_free(&block_vec);
        return false;
    }
    RMRuleProgramRule *rules = cetta_malloc(sizeof(RMRuleProgramRule) * count);
    uint32_t inverse_mp_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (!rm_parse_rule_program_block(block_vec.items[i], &rules[i])) {
            free(rules);
            rm_vec_free(&block_vec);
            return false;
        }
        if (rules[i].kind == RM_RULE_PROGRAM_INVERSE_MP)
            ++inverse_mp_count;
    }
    if (inverse_mp_count != 1) {
        free(rules);
        rm_vec_free(&block_vec);
        return false;
    }
    Atom **sorted_ids = cetta_malloc(sizeof(Atom *) * count);
    for (uint32_t i = 0; i < count; ++i)
        sorted_ids[i] = rules[i].id;
    qsort(sorted_ids, count, sizeof(Atom *), rm_symbol_atom_ptr_cmp);
    for (uint32_t i = 1; i < count; ++i) {
        if (strcmp(atom_name_cstr(sorted_ids[i - 1]),
                   atom_name_cstr(sorted_ids[i])) == 0) {
            free(sorted_ids);
            free(rules);
            rm_vec_free(&block_vec);
            return false;
        }
    }
    free(sorted_ids);
    if (!rm_delta_matches_count(delta, count)) {
        free(rules);
        rm_vec_free(&block_vec);
        return false;
    }
    rm_vec_free(&block_vec);
    if (delta_out)
        *delta_out = delta;
    if (chain_out)
        *chain_out = chain;
    *rules_out = rules;
    *rule_count_out = count;
    return true;
}

static Atom *rm_rule_program_info(Arena *arena, Atom *head, Atom **args,
                                  uint32_t nargs) {
    Atom *revision = NULL;
    RMRuleProgramRule *rules = NULL;
    uint32_t count = 0;
    if (nargs != 1 ||
        !rm_parse_rule_program(
            args[0], &revision, NULL, NULL, &rules, &count))
        return rm_error(arena, head, args, nargs,
                        "MalformedRuleProgram");
    uint64_t instructions = 0;
    for (uint32_t i = 0; i < count; ++i)
        instructions += rules[i].kind == RM_RULE_PROGRAM_AXIOM
            ? rm_generated_rule_program_axiom_op_count
            : rm_generated_rule_program_inverse_mp_op_count;
    Atom *items[] = {
        atom_symbol(arena, "rule-program-info"), revision,
        atom_symbol(arena, "proof-occurrence-bag"),
        args[0]->expr.elems[5], atom_int(arena, count),
        atom_int(arena, (int64_t)instructions),
    };
    free(rules);
    return rm_expr(arena, 6, items);
}

static Atom *rm_compile_block(Arena *arena, const RMSourceBlock *source) {
    Atom *match_items[] = {
        atom_symbol(arena, "bc-match"), source->conclusion,
    };
    Atom *goal_items[] = {
        atom_symbol(arena, "bc-goals"), source->premises,
    };
    Atom *build_items[] = {
        atom_symbol(arena, "bc-build"), source->proof,
    };
    Atom *block_items[] = {
        atom_symbol(arena, "bc-block"),
        source->id,
        source->source,
        rm_expr(arena, 2, match_items),
        rm_expr(arena, 2, goal_items),
        rm_expr(arena, 2, build_items),
        atom_symbol(arena, "bc-emit"),
    };
    return rm_expr(arena, 7, block_items);
}

static Atom *rm_delta(Arena *arena, uint64_t built, uint64_t index_built,
                      uint64_t reused) {
    Atom *items[] = {
        atom_symbol(arena, "compile-delta"),
        atom_int(arena, (int64_t)built),
        atom_int(arena, (int64_t)index_built),
        atom_int(arena, (int64_t)reused),
    };
    return rm_expr(arena, 4, items);
}

static Atom *rm_artifact(Arena *arena, Atom *revision, Atom *delta,
                         Atom *chain) {
    Atom *items[] = {
        atom_symbol(arena, "compiled-artifact"),
        atom_symbol(arena, "RuleMachineCoreV1"),
        revision,
        atom_symbol(arena, "proof-occurrence-bag"),
        delta,
        chain,
    };
    return rm_expr(arena, 6, items);
}

static bool rm_parse_artifact(Atom *artifact, Atom **revision,
                              Atom **delta, Atom **chain) {
    if (!rm_is_expr_head(artifact, "compiled-artifact", 6) ||
        !rm_is_symbol(artifact->expr.elems[1], "RuleMachineCoreV1") ||
        artifact->expr.elems[2]->kind != ATOM_SYMBOL ||
        !rm_is_symbol(artifact->expr.elems[3], "proof-occurrence-bag") ||
        !rm_is_expr_head(artifact->expr.elems[4], "compile-delta", 4))
        return false;
    *revision = artifact->expr.elems[2];
    *delta = artifact->expr.elems[4];
    *chain = artifact->expr.elems[5];
    return true;
}

static bool rm_collect_chain(Atom *chain, RMAtomVec *blocks) {
    Atom *cursor = chain;
    while (rm_is_expr_head(cursor, "bc-artifact-link", 3)) {
        if (blocks->len >= RM_MAX_ARTIFACT_BLOCKS ||
            !rm_vec_push(blocks, cursor->expr.elems[2]))
            return false;
        cursor = cursor->expr.elems[1];
    }
    if (!rm_is_expr_head(cursor, "bc-artifact-empty", 2) ||
        !rm_is_symbol(cursor->expr.elems[1], "RuleMachineCoreV1"))
        return false;
    for (uint32_t left = 0, right = blocks->len ? blocks->len - 1 : 0;
         left < right; ++left, --right) {
        Atom *tmp = blocks->items[left];
        blocks->items[left] = blocks->items[right];
        blocks->items[right] = tmp;
    }
    return true;
}

static bool rm_has_block_id(const RMAtomVec *blocks, Atom *id) {
    for (uint32_t i = 0; i < blocks->len; ++i) {
        RMBytecodeBlock block;
        if (!rm_parse_bytecode_block(blocks->items[i], &block) ||
            atom_eq(block.id, id))
            return true;
    }
    return false;
}

static Atom *rm_compile_package(Arena *arena, Atom *head, Atom **args,
                                uint32_t nargs) {
    if (nargs != 2 || !args[0] ||
        args[0]->kind != ATOM_SYMBOL || !args[1] ||
        args[1]->kind != ATOM_EXPR || args[1]->expr.len < 1 ||
        !rm_is_symbol(args[1]->expr.elems[0], "rm-package"))
        return rm_error(arena, head, args, nargs,
                        "ExpectedRevisionAndRuleMachinePackage");

    uint32_t block_count = (uint32_t)args[1]->expr.len - 1u;
    Atom *empty_items[] = {
        atom_symbol(arena, "bc-artifact-empty"),
        atom_symbol(arena, "RuleMachineCoreV1"),
    };
    Atom *chain = rm_expr(arena, 2, empty_items);

    for (uint32_t i = 0; i < block_count; ++i) {
        RMSourceBlock source;
        Atom *source_atom = args[1]->expr.elems[i + 1];
        if (!rm_parse_source_block(source_atom, &source))
            return rm_error(arena, head, args, nargs,
                            "MalformedRuleMachineBlock");
        for (uint32_t j = 0; j < i; ++j) {
            RMSourceBlock previous;
            if (!rm_parse_source_block(args[1]->expr.elems[j + 1],
                                       &previous) ||
                atom_eq(previous.id, source.id))
                return rm_error(arena, head, args, nargs,
                                "DuplicateRuleMachineBlockId");
        }
        Atom *link_items[] = {
            atom_symbol(arena, "bc-artifact-link"),
            chain,
            rm_compile_block(arena, &source),
        };
        chain = rm_expr(arena, 3, link_items);
    }

    return rm_artifact(arena, args[0],
                       rm_delta(arena, block_count, block_count, 0), chain);
}

static Atom *rm_link_block(Arena *arena, Atom *head, Atom **args,
                           uint32_t nargs) {
    if (nargs != 3 || !args[1] || args[1]->kind != ATOM_SYMBOL)
        return rm_error(arena, head, args, nargs,
                        "ExpectedArtifactRevisionAndRuleMachineBlock");
    Atom *old_revision = NULL;
    Atom *old_delta = NULL;
    Atom *old_chain = NULL;
    if (!rm_parse_artifact(args[0], &old_revision, &old_delta, &old_chain))
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifact");
    if (atom_eq(old_revision, args[1]))
        return rm_error(arena, head, args, nargs,
                        "RevisionIdentityMustAdvance");

    RMSourceBlock source;
    if (!rm_parse_source_block(args[2], &source))
        return rm_error(arena, head, args, nargs,
                        "MalformedRuleMachineBlock");

    RMAtomVec old_blocks = {0};
    if (!rm_collect_chain(old_chain, &old_blocks)) {
        rm_vec_free(&old_blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactChain");
    }
    if (!rm_delta_matches_count(old_delta, old_blocks.len)) {
        rm_vec_free(&old_blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactDelta");
    }
    if (rm_has_block_id(&old_blocks, source.id)) {
        rm_vec_free(&old_blocks);
        return rm_error(arena, head, args, nargs,
                        "DuplicateRuleMachineBlockId");
    }

    Atom *link_items[] = {
        atom_symbol(arena, "bc-artifact-link"),
        old_chain,
        rm_compile_block(arena, &source),
    };
    Atom *result = rm_artifact(
        arena, args[1], rm_delta(arena, 1, 1, old_blocks.len),
        rm_expr(arena, 3, link_items));
    rm_vec_free(&old_blocks);
    return result;
}

static Atom *rm_link_rule_program_block(Arena *arena, Atom *head, Atom **args,
                                        uint32_t nargs) {
    if (nargs != 3 || !args[1] || args[1]->kind != ATOM_SYMBOL)
        return rm_error(arena, head, args, nargs,
                        "ExpectedRuleProgramRevisionAndRuleMachineBlock");
    Atom *old_revision = NULL;
    Atom *old_delta = NULL;
    Atom *old_chain = NULL;
    RMRuleProgramRule *old_rules = NULL;
    uint32_t old_count = 0;
    if (!rm_parse_rule_program(args[0], &old_revision, &old_delta,
                                &old_chain, &old_rules, &old_count))
        return rm_error(arena, head, args, nargs,
                        "MalformedRuleProgram");
    (void)old_delta;
    if (atom_eq(old_revision, args[1])) {
        free(old_rules);
        return rm_error(arena, head, args, nargs,
                        "RevisionIdentityMustAdvance");
    }
    RMSourceBlock source;
    if (!rm_parse_source_block(args[2], &source)) {
        free(old_rules);
        return rm_error(arena, head, args, nargs,
                        "MalformedRuleMachineBlock");
    }
    for (uint32_t i = 0; i < old_count; ++i) {
        if (atom_eq(old_rules[i].id, source.id)) {
            free(old_rules);
            return rm_error(arena, head, args, nargs,
                            "DuplicateRuleMachineBlockId");
        }
    }
    Atom *bytecode = rm_compile_block(arena, &source);
    RMBytecodeBlock block;
    RMRuleProgramRule rule;
    bool admitted = rm_parse_bytecode_block(bytecode, &block) &&
                    rm_rule_program_axiom(&block, &rule);
    if (!admitted) {
        free(old_rules);
        return rm_rule_program_declined(arena, args[1]);
    }
    Atom *link_items[] = {
        atom_symbol(arena, "rule-program-link"), old_chain,
        rm_rule_program_block(arena, &rule),
    };
    Atom *result = rm_rule_program(
        arena, args[1], rm_delta(arena, 1, 1, old_count),
        rm_expr(arena, 3, link_items));
    free(old_rules);
    return result;
}

static void rm_solve_goal(RMRun *run, uint32_t depth, Atom *goal,
                          Atom *desired_proof, BindingsBuilder *builder,
                          RMContinuation continuation,
                          void *continuation_ctx);

static void rm_solve_premises(RMRun *run, uint32_t depth, Atom *premises,
                              BindingsBuilder *builder,
                              RMContinuation continuation,
                              void *continuation_ctx);

static void rm_after_premise(RMRun *run, BindingsBuilder *builder, void *ctx) {
    RMPremiseContinuation *next = ctx;
    rm_solve_premises(run, next->depth, next->rest, builder,
                      next->continuation, next->continuation_ctx);
}

static void rm_solve_premises(RMRun *run, uint32_t depth, Atom *premises,
                              BindingsBuilder *builder,
                              RMContinuation continuation,
                              void *continuation_ctx) {
    if (run->limit_reason)
        return;
    if (rm_is_symbol(premises, "rm-nil")) {
        continuation(run, builder, continuation_ctx);
        return;
    }
    if (!rm_is_expr_head(premises, "rm-cons", 3) ||
        !rm_is_expr_head(premises->expr.elems[1], "rm-premise", 3)) {
        run->limit_reason = "malformed-bytecode";
        return;
    }
    Atom *premise = premises->expr.elems[1];
    RMPremiseContinuation next = {
        .depth = depth,
        .rest = premises->expr.elems[2],
        .continuation = continuation,
        .continuation_ctx = continuation_ctx,
    };
    rm_solve_goal(run, depth, premise->expr.elems[2],
                  premise->expr.elems[1], builder, rm_after_premise, &next);
}

static void rm_solve_goal(RMRun *run, uint32_t depth, Atom *goal,
                          Atom *desired_proof, BindingsBuilder *builder,
                          RMContinuation continuation,
                          void *continuation_ctx) {
    if (run->limit_reason)
        return;
    if (run->states == run->max_states) {
        run->limit_reason = "state-limit";
        return;
    }
    ++run->states;

    for (uint32_t i = 0; i < run->block_count && !run->limit_reason; ++i) {
        ++run->block_attempts;
        uint32_t mark = bindings_builder_save(builder);
        Atom *fresh = atom_freshen_epoch(
            run->arena, run->blocks[i], fresh_var_suffix());
        RMBytecodeBlock block;
        if (!fresh || !rm_parse_bytecode_block(fresh, &block)) {
            bindings_builder_rollback(builder, mark);
            run->limit_reason = "malformed-bytecode";
            return;
        }
        if (match_atoms_builder(block.conclusion, goal, builder) &&
            match_atoms_builder(block.proof, desired_proof, builder)) {
            ++run->block_matches;
            if (rm_is_symbol(block.premises, "rm-nil")) {
                continuation(run, builder, continuation_ctx);
            } else if (depth > 0) {
                rm_solve_premises(run, depth - 1, block.premises, builder,
                                  continuation, continuation_ctx);
            }
        }
        bindings_builder_rollback(builder, mark);
    }
}

static void rm_collect_answer(RMRun *run, BindingsBuilder *builder, void *ctx) {
    RMAnswerContinuation *answer = ctx;
    ++run->occurrences_seen;
    if (run->answers.len == run->max_occurrences) {
        run->limit_reason = "occurrence-limit";
        return;
    }
    Atom *proof = bindings_apply_if_vars(
        bindings_builder_bindings(builder), run->arena, answer->proof_var);
    if (!proof || !rm_vec_push(&run->answers, proof))
        run->limit_reason = "allocation-failure";
}

static Atom *rm_occurrences(Arena *arena, const RMAtomVec *answers) {
    Atom **items = arena_alloc(
        arena, sizeof(Atom *) * ((size_t)answers->len + 1u));
    items[0] = atom_symbol(arena, "occurrences");
    for (uint32_t i = 0; i < answers->len; ++i) {
        Atom *occurrence_items[] = {
            atom_symbol(arena, "occurrence"), answers->items[i],
        };
        items[i + 1] = rm_expr(arena, 2, occurrence_items);
    }
    return atom_expr(arena, items, answers->len + 1u);
}

static Atom *rm_metrics(Arena *arena, const RMRun *run) {
    Atom *items[] = {
        atom_symbol(arena, "run-metrics"),
        atom_int(arena, (int64_t)run->states),
        atom_int(arena, (int64_t)run->block_attempts),
        atom_int(arena, (int64_t)run->block_matches),
        atom_int(arena, (int64_t)run->occurrences_seen),
        atom_int(arena, (int64_t)run->requested_depth),
    };
    return rm_expr(arena, 6, items);
}

static Atom *rm_run_artifact(Arena *arena, Atom *head, Atom **args,
                             uint32_t nargs) {
    uint64_t depth64 = 0;
    uint64_t max_states = 0;
    uint64_t max_occurrences = 0;
    if (nargs != 5 || !rm_parse_nonnegative_int(args[1], &depth64) ||
        !rm_parse_nonnegative_int(args[2], &max_states) ||
        !rm_parse_nonnegative_int(args[3], &max_occurrences) ||
        depth64 > RM_MAX_RULE_DEPTH || max_states == 0 ||
        max_occurrences == 0 || max_states > INT64_MAX ||
        max_occurrences > UINT32_MAX)
        return rm_error(arena, head, args, nargs,
                        "ExpectedArtifactDepthStateLimitOccurrenceLimitAndGoal");

    Atom *revision = NULL;
    Atom *delta = NULL;
    Atom *chain = NULL;
    if (!rm_parse_artifact(args[0], &revision, &delta, &chain))
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifact");

    RMAtomVec blocks = {0};
    if (!rm_collect_chain(chain, &blocks)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactChain");
    }
    if (!rm_delta_matches_count(delta, blocks.len)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactDelta");
    }
    for (uint32_t i = 0; i < blocks.len; ++i) {
        RMBytecodeBlock parsed;
        if (!rm_parse_bytecode_block(blocks.items[i], &parsed)) {
            rm_vec_free(&blocks);
            return rm_error(arena, head, args, nargs,
                            "MalformedRuleMachineBytecodeBlock");
        }
    }

    RMRun run = {
        .arena = arena,
        .blocks = blocks.items,
        .block_count = blocks.len,
        .revision = revision,
        .max_states = max_states,
        .max_occurrences = max_occurrences,
        .requested_depth = (uint32_t)depth64,
    };
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs, "AllocationFailure");
    }
    Atom *proof_var = atom_var_with_id(arena, "compile-proof", fresh_var_id());
    RMAnswerContinuation answer = {.proof_var = proof_var};
    rm_solve_goal(&run, (uint32_t)depth64, args[4], proof_var, &builder,
                  rm_collect_answer, &answer);
    bindings_builder_free(&builder);

    Atom *occurrences = rm_occurrences(arena, &run.answers);
    Atom *metrics = rm_metrics(arena, &run);
    Atom *result = NULL;
    if (run.limit_reason) {
        Atom *items[] = {
            atom_symbol(arena, "compile-incomplete"),
            atom_symbol(arena, run.limit_reason),
            atom_symbol(arena, "proof-occurrence-bag"),
            occurrences,
            metrics,
            revision,
        };
        result = rm_expr(arena, 6, items);
    } else {
        Atom *items[] = {
            atom_symbol(arena, "compile-result"),
            atom_symbol(arena, "proof-occurrence-bag"),
            occurrences,
            metrics,
            revision,
        };
        result = rm_expr(arena, 5, items);
    }
    rm_vec_free(&run.answers);
    rm_vec_free(&blocks);
    return result;
}

static Atom *rm_artifact_info(Arena *arena, Atom *head, Atom **args,
                              uint32_t nargs) {
    if (nargs != 1)
        return rm_error(arena, head, args, nargs, "ExpectedCompiledArtifact");
    Atom *revision = NULL;
    Atom *delta = NULL;
    Atom *chain = NULL;
    if (!rm_parse_artifact(args[0], &revision, &delta, &chain))
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifact");
    RMAtomVec blocks = {0};
    if (!rm_collect_chain(chain, &blocks)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactChain");
    }
    if (!rm_delta_matches_count(delta, blocks.len)) {
        rm_vec_free(&blocks);
        return rm_error(arena, head, args, nargs,
                        "MalformedCompiledArtifactDelta");
    }
    Atom *items[] = {
        atom_symbol(arena, "compiled-artifact-info"),
        revision,
        atom_symbol(arena, "proof-occurrence-bag"),
        delta,
        atom_int(arena, blocks.len),
    };
    Atom *result = rm_expr(arena, 5, items);
    rm_vec_free(&blocks);
    return result;
}

static bool rm_rule_program_frames_push(RMRuleProgramRun *run, RMRuleProgramFrame frame) {
    RMRuleProgramFrames *frames = &run->frames;
    if (frames->len == frames->cap) {
        uint32_t next = frames->cap ? frames->cap * 2u : 32u;
        if (next < frames->cap)
            return false;
        frames->items = cetta_realloc(
            frames->items, sizeof(RMRuleProgramFrame) * next);
        frames->cap = next;
    }
    frames->items[frames->len++] = frame;
    if (frames->len > run->max_search_stack)
        run->max_search_stack = frames->len;
    return true;
}

static void rm_rule_program_frame_pop(RMRuleProgramRun *run) {
    RMRuleProgramFrame frame = run->frames.items[--run->frames.len];
    arena_reset(&run->scratch, frame.pop_mark);
}

static Atom *rm_rule_program_fun(Arena *arena, Atom *domain, Atom *codomain) {
    Atom *items[] = {atom_symbol(arena, "rule-program-fun"), domain, codomain};
    return rm_expr(arena, 3, items);
}

static bool rm_rule_program_fun_view(Atom *term, Atom **domain, Atom **codomain) {
    if (!rm_is_expr_head(term, "rule-program-fun", 3))
        return false;
    *domain = term->expr.elems[1];
    *codomain = term->expr.elems[2];
    return true;
}

static bool rm_rule_program_unify_apply(Arena *scratch, Atom *left, Atom *right,
                                 Atom *body, Atom **result) {
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL))
        return false;
    bool matched = match_atoms_builder(left, right, &builder);
    if (matched && bindings_has_loop(bindings_builder_bindings(&builder)))
        matched = false;
    if (matched) {
        *result = bindings_apply_if_vars(
            bindings_builder_bindings(&builder), scratch, body);
        matched = *result != NULL;
    }
    bindings_builder_free(&builder);
    return matched;
}

static bool rm_rule_program_unifies(Arena *scratch, Atom *left, Atom *right) {
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL))
        return false;
    bool matched = match_atoms_builder(left, right, &builder);
    if (matched && bindings_has_loop(bindings_builder_bindings(&builder)))
        matched = false;
    bindings_builder_free(&builder);
    (void)scratch;
    return matched;
}

static bool rm_rule_program_apply_rule_native(RMRuleProgramRun *run, RMRuleProgramRule *rule,
                                       Atom *type, Atom *proof,
                                       Atom **next_type, Atom **next_proof,
                                       int32_t *hypothesis_add) {
    Atom *domain = NULL;
    Atom *codomain = NULL;
    if (!rm_rule_program_fun_view(type, &domain, &codomain))
        return false;
    if (rule->kind == RM_RULE_PROGRAM_AXIOM) {
        Atom *schema = atom_deep_copy(&run->scratch, rule->schema);
        schema = atom_freshen_epoch(
            &run->scratch, schema, fresh_var_suffix());
        if (!schema ||
            !rm_rule_program_unify_apply(
                &run->scratch, domain, schema, codomain, next_type))
            return false;
        Atom *proof_symbol = atom_deep_copy(
            &run->scratch, rule->proof_symbol);
        *next_proof = atom_expr2(&run->scratch, proof, proof_symbol);
        *hypothesis_add = 0;
        return true;
    }

    Atom *fresh = atom_var_with_id(
        &run->scratch, "rule-program-antecedent", fresh_var_id());
    Atom *imp_items[] = {
        atom_symbol(&run->scratch, "imp"), fresh, domain,
    };
    Atom *implication = rm_expr(&run->scratch, 3, imp_items);
    Atom *inner = rm_rule_program_fun(&run->scratch, fresh, codomain);
    *next_type = rm_rule_program_fun(&run->scratch, implication, inner);
    Atom *proof_symbol = atom_deep_copy(
        &run->scratch, rule->proof_symbol);
    *next_proof = atom_expr2(&run->scratch, proof_symbol, proof);
    *hypothesis_add = 2;
    return true;
}

static int rm_rule_program_reg_index(Atom *reg) {
    if (!reg || reg->kind != ATOM_SYMBOL)
        return -1;
    const char *name = atom_name_cstr(reg);
    return name[0] == 'r' && name[1] >= '0' && name[1] <= '5' &&
           name[2] == '\0'
        ? name[1] - '0'
        : -1;
}

static bool rm_rule_program_apply_rule_bytecode(
    RMRuleProgramRun *run, RMRuleProgramRule *rule, Atom *type, Atom *proof,
    Atom **next_type, Atom **next_proof, int32_t *hypothesis_add) {
    if (!rule->code || !rm_is_expr_head(rule->code, "rule-program-code", 2))
        return false;
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL))
        return false;
    Atom *regs[6] = {0};
    Atom *current = type;
    Atom *built_proof = proof;
    Atom *cursor = rule->code->expr.elems[1];
    int32_t hyp_add = 0;
    bool emitted = false;

    while (!rm_is_symbol(cursor, "rule-program-nil")) {
        if (!rm_is_expr_head(cursor, "rule-program-cons", 3))
            goto fail;
        Atom *op = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (rm_is_expr_head(op, "rmbc-require-fun", 3)) {
            int a = rm_rule_program_reg_index(op->expr.elems[1]);
            int b = rm_rule_program_reg_index(op->expr.elems[2]);
            Atom *domain = NULL;
            Atom *codomain = NULL;
            if (a < 0 || b < 0 ||
                !rm_rule_program_fun_view(current, &domain, &codomain))
                goto fail;
            regs[a] = domain;
            regs[b] = codomain;
        } else if (rm_is_expr_head(
                       op, "rmbc-instantiate-template", 3)) {
            int dst = rm_rule_program_reg_index(op->expr.elems[1]);
            if (dst < 0 ||
                !rm_is_symbol(op->expr.elems[2], "template0") ||
                !rule->schema)
                goto fail;
            regs[dst] = atom_deep_copy(&run->scratch, rule->schema);
            regs[dst] = atom_freshen_epoch(
                &run->scratch, regs[dst], fresh_var_suffix());
            if (!regs[dst])
                goto fail;
        } else if (rm_is_expr_head(op, "rmbc-unify", 3)) {
            int a = rm_rule_program_reg_index(op->expr.elems[1]);
            int b = rm_rule_program_reg_index(op->expr.elems[2]);
            if (a < 0 || b < 0 || !regs[a] || !regs[b] ||
                !match_atoms_builder(regs[a], regs[b], &builder) ||
                bindings_has_loop(bindings_builder_bindings(&builder)))
                goto fail;
        } else if (rm_is_expr_head(op, "rmbc-set-type", 2)) {
            int src = rm_rule_program_reg_index(op->expr.elems[1]);
            if (src < 0 || !regs[src])
                goto fail;
            current = bindings_apply_if_vars(
                bindings_builder_bindings(&builder), &run->scratch,
                regs[src]);
            if (!current)
                goto fail;
        } else if (rm_is_expr_head(op, "rmbc-proof-apply", 2)) {
            Atom *symbol = op->expr.elems[1];
            if (symbol->kind != ATOM_SYMBOL)
                goto fail;
            built_proof = atom_expr2(&run->scratch, built_proof, symbol);
        } else if (rm_is_expr_head(op, "rmbc-proof-wrap", 2)) {
            Atom *symbol = op->expr.elems[1];
            if (symbol->kind != ATOM_SYMBOL)
                goto fail;
            built_proof = atom_expr2(&run->scratch, symbol, built_proof);
        } else if (rm_is_expr_head(op, "rmbc-hyp-add", 2)) {
            uint64_t value = 0;
            if (!rm_parse_nonnegative_int(op->expr.elems[1], &value) ||
                value > INT32_MAX)
                goto fail;
            hyp_add = (int32_t)value;
        } else if (rm_is_expr_head(op, "rmbc-fresh", 2)) {
            int dst = rm_rule_program_reg_index(op->expr.elems[1]);
            if (dst < 0)
                goto fail;
            regs[dst] = atom_var_with_id(
                &run->scratch, "rule-program-antecedent", fresh_var_id());
        } else if (rm_is_expr_head(op, "rmbc-make-imp", 4) ||
                   rm_is_expr_head(op, "rmbc-make-fun", 4)) {
            int dst = rm_rule_program_reg_index(op->expr.elems[1]);
            int left = rm_rule_program_reg_index(op->expr.elems[2]);
            int right = rm_rule_program_reg_index(op->expr.elems[3]);
            if (dst < 0 || left < 0 || right < 0 ||
                !regs[left] || !regs[right])
                goto fail;
            if (rm_is_symbol(op->expr.elems[0], "rmbc-make-imp")) {
                Atom *items[] = {
                    atom_symbol(&run->scratch, "imp"),
                    regs[left], regs[right],
                };
                regs[dst] = rm_expr(&run->scratch, 3, items);
            } else {
                regs[dst] = rm_rule_program_fun(
                    &run->scratch, regs[left], regs[right]);
            }
        } else if (rm_is_symbol(op, "rmbc-emit")) {
            emitted = true;
        } else {
            goto fail;
        }
    }
    if (!emitted)
        goto fail;
    *next_type = current;
    *next_proof = built_proof;
    *hypothesis_add = hyp_add;
    bindings_builder_free(&builder);
    return true;

fail:
    bindings_builder_free(&builder);
    return false;
}

static bool rm_rule_program_accept(RMRuleProgramRun *run, Atom *type, Atom *proof) {
    ArenaMark mark = arena_mark(&run->scratch);
    bool accepted = rm_rule_program_unifies(&run->scratch, type, run->target);
    arena_reset(&run->scratch, mark);
    if (!accepted)
        return true;
    if (run->accepted == run->max_occurrences) {
        run->limit_reason = "occurrence-limit";
        return false;
    }
    ++run->accepted;
    Atom *copy = atom_deep_copy(run->output, proof);
    if (!copy || !rm_vec_push(&run->proofs, copy)) {
        run->limit_reason = "allocation-failure";
        return false;
    }
    return true;
}

static void rm_rule_program_search(RMRuleProgramRun *run) {
    ArenaMark base = arena_mark(&run->scratch);
    run->target = atom_deep_copy(&run->scratch, run->target);
    Atom *initial_type = rm_rule_program_fun(
        &run->scratch, run->target, run->target);
    Atom *initial_proof = atom_symbol(&run->scratch, "I");
    if (!run->target || !rm_rule_program_frames_push(run, (RMRuleProgramFrame){
            .depth = (int32_t)run->max_size,
            .hypotheses = 1,
            .type = initial_type,
            .proof = initial_proof,
            .pop_mark = base,
        })) {
        run->limit_reason = "allocation-failure";
        return;
    }

    while (run->frames.len && !run->limit_reason) {
        RMRuleProgramFrame *frame = &run->frames.items[run->frames.len - 1u];
        if (!frame->entered) {
            if (run->states == run->max_states) {
                run->limit_reason = "state-limit";
                break;
            }
            frame->entered = true;
            ++run->states;
            if (frame->hypotheses == 0) {
                (void)rm_rule_program_accept(run, frame->type, frame->proof);
                rm_rule_program_frame_pop(run);
                continue;
            }
            if (frame->depth <= 0 || frame->hypotheses > frame->depth) {
                rm_rule_program_frame_pop(run);
                continue;
            }
        }
        if (frame->next_rule >= run->rule_count) {
            rm_rule_program_frame_pop(run);
            continue;
        }

        RMRuleProgramRule *rule = &run->rules[frame->next_rule++];
        ++run->rule_attempts;
        ArenaMark attempt = arena_mark(&run->scratch);
        Atom *next_type = NULL;
        Atom *next_proof = NULL;
        int32_t hypothesis_add = 0;
        bool applied = run->native_backend
            ? rm_rule_program_apply_rule_native(
                  run, rule, frame->type, frame->proof,
                  &next_type, &next_proof, &hypothesis_add)
            : rm_rule_program_apply_rule_bytecode(
                  run, rule, frame->type, frame->proof,
                  &next_type, &next_proof, &hypothesis_add);
        if (applied) {
            int32_t next_hypotheses =
                frame->hypotheses - 1 + hypothesis_add;
            if (next_hypotheses >= 0) {
                ++run->rule_successes;
                if (!rm_rule_program_frames_push(run, (RMRuleProgramFrame){
                        .depth = frame->depth - 1,
                        .hypotheses = next_hypotheses,
                        .type = next_type,
                        .proof = next_proof,
                        .pop_mark = attempt,
                    })) {
                    run->limit_reason = "allocation-failure";
                }
                continue;
            }
        }
        arena_reset(&run->scratch, attempt);
    }
    while (run->frames.len)
        rm_rule_program_frame_pop(run);
}

static Atom *rm_rule_program_metrics(Arena *arena, const RMRuleProgramRun *run) {
    Atom *items[] = {
        atom_symbol(arena, "rule-program-metrics"),
        atom_int(arena, (int64_t)run->accepted),
        atom_int(arena, (int64_t)run->states),
        atom_int(arena, (int64_t)run->rule_attempts),
        atom_int(arena, (int64_t)run->rule_successes),
        atom_int(arena, (int64_t)run->max_size),
        atom_int(arena, (int64_t)run->max_search_stack),
    };
    return rm_expr(arena, 7, items);
}

static Atom *rm_rule_program_declined(Arena *arena, Atom *revision) {
    Atom *items[] = {
        atom_symbol(arena, "compile-declined"),
        atom_symbol(arena, "rule-program-fragment"),
        atom_symbol(arena, "unsupported-rule-shape"),
        revision,
    };
    return rm_expr(arena, 4, items);
}

static Atom *rm_run_rule_program(Arena *arena, Atom *head, Atom **args,
                                 uint32_t nargs) {
    uint64_t size64 = 0;
    uint64_t max_states = 0;
    uint64_t max_occurrences = 0;
    if (nargs != 5 || !rm_parse_nonnegative_int(args[1], &size64) ||
        !rm_parse_nonnegative_int(args[2], &max_states) ||
        !rm_parse_nonnegative_int(args[3], &max_occurrences) ||
        size64 > RM_MAX_RULE_DEPTH || max_states == 0 ||
        max_occurrences == 0 || max_states > INT64_MAX ||
        max_occurrences > UINT32_MAX)
        return rm_error(arena, head, args, nargs,
                        "ExpectedRuleProgramSizeStateLimitOccurrenceLimitAndTarget");

    Atom *revision = NULL;
    RMRuleProgramRule *rules = NULL;
    uint32_t rule_count = 0;
    if (!rm_parse_rule_program(
            args[0], &revision, NULL, NULL, &rules, &rule_count))
        return rm_error(arena, head, args, nargs,
                        "MalformedRuleProgram");

    RMRuleProgramRun run = {
        .output = arena,
        .rules = rules,
        .rule_count = rule_count,
        .target = args[4],
        .revision = revision,
        .max_states = max_states,
        .max_occurrences = max_occurrences,
        .max_size = (uint32_t)size64,
        .native_backend = rm_is_symbol(head, "compile:rule-program-run-native"),
    };
    arena_init(&run.scratch);
    arena_set_runtime_kind(&run.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    arena_set_hashcons(&run.scratch, NULL);
    rm_rule_program_search(&run);

    Atom *occurrences = rm_occurrences(arena, &run.proofs);
    Atom *metrics = rm_rule_program_metrics(arena, &run);
    Atom *result = NULL;
    if (run.limit_reason) {
        Atom *items[] = {
            atom_symbol(arena, "compile-incomplete"),
            atom_symbol(arena, run.limit_reason),
            atom_symbol(arena, "proof-occurrence-bag"),
            occurrences,
            metrics,
            revision,
        };
        result = rm_expr(arena, 6, items);
    } else {
        Atom *items[] = {
            atom_symbol(arena, "rule-program-result"),
            atom_symbol(arena, "proof-occurrence-bag"),
            occurrences,
            metrics,
            revision,
        };
        result = rm_expr(arena, 5, items);
    }
    arena_free(&run.scratch);
    free(run.frames.items);
    free(rules);
    rm_vec_free(&run.proofs);
    return result;
}

Atom *cetta_rule_machine_dispatch(Arena *arena, Atom *head,
                                  Atom **args, uint32_t nargs) {
    if (!arena || !head || head->kind != ATOM_SYMBOL)
        return NULL;
    const char *name = atom_name_cstr(head);
    if (strcmp(name, "compile:rule-package") == 0)
        return rm_compile_package(arena, head, args, nargs);
    if (strcmp(name, "compile:link-rule") == 0)
        return rm_link_block(arena, head, args, nargs);
    if (strcmp(name, "compile:run") == 0)
        return rm_run_artifact(arena, head, args, nargs);
    if (strcmp(name, "compile:artifact-info") == 0)
        return rm_artifact_info(arena, head, args, nargs);
    if (strcmp(name, "compile:rule-program") == 0)
        return rm_compile_rule_program(arena, head, args, nargs);
    if (strcmp(name, "compile:rule-program-info") == 0)
        return rm_rule_program_info(arena, head, args, nargs);
    if (strcmp(name, "compile:rule-program-link") == 0)
        return rm_link_rule_program_block(arena, head, args, nargs);
    if (strcmp(name, "compile:rule-program-run") == 0)
        return rm_run_rule_program(arena, head, args, nargs);
    if (strcmp(name, "compile:rule-program-run-native") == 0)
        return rm_run_rule_program(arena, head, args, nargs);
    return NULL;
}
