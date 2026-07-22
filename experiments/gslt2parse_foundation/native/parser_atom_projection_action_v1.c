#include "parser_atom_projection_action_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_atom_projection_events_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t instruction_index;
    PPAtomProjectionActionV1Value left;
    PPAtomProjectionActionV1Value right;
} PPAtomProjectionActionV1Node;

typedef struct {
    uint32_t scalar_begin;
    uint32_t scalar_len;
} PPAtomProjectionActionV1Text;

typedef struct {
    PPAtomProjectionActionV1Node *nodes;
    PPAtomProjectionActionV1Text *texts;
    uint32_t *text_scalars;
    uint32_t node_len;
    uint32_t node_cap;
    uint32_t text_len;
    uint32_t text_cap;
    uint32_t text_scalar_len;
    uint32_t text_scalar_cap;
} PPAtomProjectionActionV1StoreImpl;

typedef enum {
    PPATOM_PROJECTION_ACTION_V1_ABSTRACT_ANY = 0,
    PPATOM_PROJECTION_ACTION_V1_ABSTRACT_SYMBOL = 1,
    PPATOM_PROJECTION_ACTION_V1_ABSTRACT_OTHER = 2,
    PPATOM_PROJECTION_ACTION_V1_ABSTRACT_COMPOSITE = 3
} PPAtomProjectionActionV1Abstract;

static bool ppa_action_v1_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (buf && size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(buf, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool ppa_action_v1_digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        char byte = digest[index];
        if (!((byte >= '0' && byte <= '9') ||
              (byte >= 'a' && byte <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool ppa_action_v1_array_fits(
    uint32_t len, size_t element_size) {
    return len == 0u || element_size <= SIZE_MAX / (size_t)len;
}

static void ppa_action_v1_sha_u32(
    CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppa_action_v1_sha_u64(
    CettaNativeSha256 *sha, uint64_t value) {
    uint8_t bytes[8];
    uint32_t index;

    for (index = 0u; index < 8u; index++)
        bytes[index] = (uint8_t)(value >> ((7u - index) * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static bool ppa_action_v1_program_digest(
    const PPAtomProjectionActionV1Program *program,
    char out[65]) {
    static const uint8_t domain[] = "ParserAtomProjectionActionProgramV1\n";
    CettaNativeSha256 sha;
    uint32_t index;

    if (!program ||
        !ppa_action_v1_digest_valid(program->action_program_digest) ||
        !ppa_action_v1_digest_valid(program->projection_plan_digest)) {
        return false;
    }
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)program->action_program_digest, 64u);
    cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)program->projection_plan_digest, 64u);
    cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    ppa_action_v1_sha_u32(&sha, program->production_len);
    ppa_action_v1_sha_u32(&sha, program->instruction_len);
    {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!program->codepoint_label ||
            !fh_ground_term_v1_render(
                program->codepoint_label, &canonical, &canonical_len,
                NULL, 0u)) {
            free(canonical);
            return false;
        }
        ppa_action_v1_sha_u64(&sha, (uint64_t)canonical_len);
        cetta_native_sha256_update(&sha, canonical, canonical_len);
        free(canonical);
    }
    for (index = 0u; index < program->production_len; index++) {
        const PPAtomProjectionActionV1Production *production =
            &program->productions[index];
        ppa_action_v1_sha_u32(&sha, production->instruction_begin);
        ppa_action_v1_sha_u32(&sha, production->instruction_len);
        ppa_action_v1_sha_u32(&sha, production->arity);
        ppa_action_v1_sha_u32(&sha, production->max_stack_len);
    }
    for (index = 0u; index < program->instruction_len; index++) {
        const PPAtomProjectionActionV1Instruction *instruction =
            &program->instructions[index];
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;

        ppa_action_v1_sha_u32(&sha, (uint32_t)instruction->kind);
        ppa_action_v1_sha_u32(
            &sha, (uint32_t)instruction->constant_kind);
        ppa_action_v1_sha_u32(&sha, instruction->operand);
        ppa_action_v1_sha_u32(&sha, instruction->dense_id);
        if (instruction->term &&
            !fh_ground_term_v1_render(
                instruction->term, &canonical, &canonical_len,
                NULL, 0u)) {
            free(canonical);
            return false;
        }
        ppa_action_v1_sha_u64(&sha, (uint64_t)canonical_len);
        if (canonical_len > 0u)
            cetta_native_sha256_update(&sha, canonical, canonical_len);
        free(canonical);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

static bool ppa_action_v1_constant_classify(
    const PPAtomProjectionV1Plan *projection,
    const PPAtomProjectionV1CarrierView *carriers,
    Atom *term,
    PPAtomProjectionActionV1ConstantKind *kind,
    uint32_t *dense_id,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t count;
    uint32_t index;

    *kind = PPATOM_PROJECTION_ACTION_V1_CONSTANT_OPAQUE;
    *dense_id = 0u;
    if (!term)
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action constant is absent");
    if (atom_is_symbol(term, carriers->nil_label)) {
        *kind = PPATOM_PROJECTION_ACTION_V1_CONSTANT_NIL;
        return true;
    }
    if (atom_is_symbol(term, carriers->document_label)) {
        *kind = PPATOM_PROJECTION_ACTION_V1_CONSTANT_DOCUMENT;
        return true;
    }
    if (atom_is_symbol(term, carriers->expression_label)) {
        *kind = PPATOM_PROJECTION_ACTION_V1_CONSTANT_EXPRESSION;
        return true;
    }
    if (!ppatom_projection_v1_plan_rule_count(
            projection, &count, error_buf, error_buf_size)) {
        return false;
    }
    for (index = 0u; index < count; index++) {
        PPAtomProjectionV1RuleView rule;
        if (!ppatom_projection_v1_plan_rule(
                projection, index, &rule,
                error_buf, error_buf_size)) {
            return false;
        }
        if (atom_is_symbol(term, rule.source_label)) {
            *kind = PPATOM_PROJECTION_ACTION_V1_CONSTANT_RULE;
            *dense_id = index;
            return true;
        }
    }
    if (!ppatom_projection_v1_plan_wrapper_count(
            projection, &count, error_buf, error_buf_size)) {
        return false;
    }
    for (index = 0u; index < count; index++) {
        PPAtomProjectionV1WrapperView wrapper;
        if (!ppatom_projection_v1_plan_wrapper(
                projection, index, &wrapper,
                error_buf, error_buf_size)) {
            return false;
        }
        if (atom_is_symbol(term, wrapper.label)) {
            *kind = PPATOM_PROJECTION_ACTION_V1_CONSTANT_WRAPPER;
            *dense_id = index;
            return true;
        }
    }
    return true;
}

static bool ppa_action_v1_instruction_compile(
    const PPActionBytecodeV1Instruction *source,
    const PPAtomProjectionV1Plan *projection,
    const PPAtomProjectionV1CarrierView *carriers,
    Arena *arena,
    PPAtomProjectionActionV1Instruction *out,
    char *error_buf,
    size_t error_buf_size) {
    memset(out, 0, sizeof(*out));
    if (source->kind == PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
        if (source->term)
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "projection slot instruction retained a term");
        out->kind = PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT;
        out->operand = source->operand;
        return true;
    }
    if (!source->term)
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action instruction lost its term");
    out->term = atom_deep_copy(arena, source->term);
    if (!out->term)
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "cannot copy projection action term");
    if (source->kind == PP_ACTION_BYTECODE_V1_PUSH_CONST) {
        out->kind = PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT;
        return ppa_action_v1_constant_classify(
            projection, carriers, source->term,
            &out->constant_kind, &out->dense_id,
            error_buf, error_buf_size);
    }
    if (source->kind != PP_ACTION_BYTECODE_V1_APPLY ||
        source->operand != 2u || source->term->kind != ATOM_SYMBOL) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action application is outside the binary carrier algebra");
    }
    out->operand = 2u;
    if (atom_is_symbol(source->term, carriers->pair_label))
        out->kind = PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR;
    else if (atom_is_symbol(source->term, carriers->cons_label))
        out->kind = PPATOM_PROJECTION_ACTION_V1_APPLY_CONS;
    else if (atom_is_symbol(source->term, carriers->node_label))
        out->kind = PPATOM_PROJECTION_ACTION_V1_APPLY_NODE;
    else
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action application uses an unclassified carrier");
    return true;
}

static bool ppa_action_v1_production_types(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1Production *production,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionActionV1Abstract local[16];
    PPAtomProjectionActionV1Abstract *stack = local;
    uint32_t stack_len = 0u;
    uint32_t index;
    bool owned = false;
    bool ok = false;

    if (production->max_stack_len == 0u)
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action has an empty stack bound");
    if (production->max_stack_len >
            sizeof(local) / sizeof(local[0])) {
        stack = malloc(sizeof(*stack) * production->max_stack_len);
        if (!stack)
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "cannot allocate projection action type stack");
        owned = true;
    }
    for (index = 0u; index < production->instruction_len; index++) {
        const PPAtomProjectionActionV1Instruction *instruction =
            &program->instructions[production->instruction_begin + index];
        if (instruction->kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT) {
            if (instruction->operand >= production->arity ||
                stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = PPATOM_PROJECTION_ACTION_V1_ABSTRACT_ANY;
        } else if (instruction->kind ==
                       PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT) {
            if (stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = instruction->term->kind == ATOM_SYMBOL
                ? PPATOM_PROJECTION_ACTION_V1_ABSTRACT_SYMBOL
                : PPATOM_PROJECTION_ACTION_V1_ABSTRACT_OTHER;
        } else {
            uint32_t begin;
            if (stack_len < 2u)
                goto malformed;
            begin = stack_len - 2u;
            if (instruction->kind ==
                    PPATOM_PROJECTION_ACTION_V1_APPLY_NODE &&
                stack[begin] !=
                    PPATOM_PROJECTION_ACTION_V1_ABSTRACT_SYMBOL) {
                ppa_action_v1_error(
                    error_buf, error_buf_size,
                    "projection node label is not a static symbol");
                goto done;
            }
            stack_len = begin;
            stack[stack_len++] =
                PPATOM_PROJECTION_ACTION_V1_ABSTRACT_COMPOSITE;
        }
    }
    if (stack_len != 1u)
        goto malformed;
    ok = true;
    goto done;

malformed:
    ppa_action_v1_error(
        error_buf, error_buf_size,
        "projection action stack shape is malformed");
done:
    if (owned)
        free(stack);
    return ok;
}

static PPAtomProjectionActionV1ExecutionKind
ppa_action_v1_execution_classify(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1Production *production) {
    const PPAtomProjectionActionV1Instruction *instructions;

    if (!program || !production ||
        production->instruction_begin > program->instruction_len ||
        production->instruction_len >
            program->instruction_len - production->instruction_begin) {
        return PPATOM_PROJECTION_ACTION_V1_EXECUTE_INTERPRETED;
    }
    instructions = &program->instructions[production->instruction_begin];
    if (production->instruction_len == 1u &&
        (instructions[0].kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT ||
         instructions[0].kind ==
             PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT)) {
        return PPATOM_PROJECTION_ACTION_V1_EXECUTE_VALUE;
    }
    if (production->instruction_len == 3u &&
        (instructions[0].kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT ||
         instructions[0].kind ==
             PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT) &&
        (instructions[1].kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT ||
         instructions[1].kind ==
             PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT) &&
        instructions[2].kind >= PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR &&
        instructions[2].kind <= PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
        return PPATOM_PROJECTION_ACTION_V1_EXECUTE_BINARY;
    }
    return PPATOM_PROJECTION_ACTION_V1_EXECUTE_INTERPRETED;
}

void ppatom_projection_action_v1_program_init(
    PPAtomProjectionActionV1Program *program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
    arena_init(&program->arena);
}

void ppatom_projection_action_v1_program_free(
    PPAtomProjectionActionV1Program *program) {
    if (!program)
        return;
    free(program->productions);
    free(program->instructions);
    arena_free(&program->arena);
    memset(program, 0, sizeof(*program));
}

bool ppatom_projection_action_v1_program_build_prevalidated(
    PPAtomProjectionActionV1Program *program,
    const PPActionBytecodeV1Program *actions,
    const PPAtomProjectionV1Plan *projection,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionActionV1Program candidate;
    PPAtomProjectionV1CarrierView carriers;
    PPAtomProjectionV1ProvenanceView provenance;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    ppatom_projection_action_v1_program_init(&candidate);
    if (!program || !actions || !projection ||
        !actions->productions || !actions->instructions ||
        actions->production_len == 0u || actions->instruction_len == 0u ||
        !ppa_action_v1_digest_valid(actions->program_digest) ||
        !ppatom_projection_v1_plan_carriers(
            projection, &carriers, error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_provenance(
            projection, &provenance, error_buf, error_buf_size) ||
        !ppa_action_v1_digest_valid(provenance.plan_digest)) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "bad projection action program inputs");
        goto done;
    }
    if (!ppa_action_v1_array_fits(
            actions->production_len, sizeof(*candidate.productions)) ||
        !ppa_action_v1_array_fits(
            actions->instruction_len, sizeof(*candidate.instructions))) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action program is too large");
        goto done;
    }
    candidate.productions = calloc(
        actions->production_len, sizeof(*candidate.productions));
    candidate.instructions = calloc(
        actions->instruction_len, sizeof(*candidate.instructions));
    if (!candidate.productions || !candidate.instructions) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "cannot allocate projection action program");
        goto done;
    }
    candidate.production_len = actions->production_len;
    candidate.instruction_len = actions->instruction_len;
    candidate.codepoint_label = atom_symbol(
        &candidate.arena, carriers.codepoint_label);
    if (!candidate.codepoint_label) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "cannot copy projection codepoint carrier");
        goto done;
    }
    (void)snprintf(candidate.action_program_digest, 65u, "%s",
                   actions->program_digest);
    (void)snprintf(candidate.projection_plan_digest, 65u, "%s",
                   provenance.plan_digest);
    for (index = 0u; index < actions->production_len; index++) {
        const PPActionBytecodeV1Production *source =
            &actions->productions[index];
        PPAtomProjectionActionV1Production *target =
            &candidate.productions[index];
        if (source->instruction_begin > actions->instruction_len ||
            source->instruction_len >
                actions->instruction_len - source->instruction_begin ||
            source->max_stack_len == 0u) {
            ppa_action_v1_error(
                error_buf, error_buf_size,
                "source action production range is malformed");
            goto done;
        }
        *target = (PPAtomProjectionActionV1Production){
            .instruction_begin = source->instruction_begin,
            .instruction_len = source->instruction_len,
            .arity = source->arity,
            .max_stack_len = source->max_stack_len,
        };
    }
    for (index = 0u; index < actions->instruction_len; index++) {
        if (!ppa_action_v1_instruction_compile(
                &actions->instructions[index], projection, &carriers,
                &candidate.arena, &candidate.instructions[index],
                error_buf, error_buf_size)) {
            goto done;
        }
    }
    for (index = 0u; index < candidate.production_len; index++) {
        if (!ppa_action_v1_production_types(
                &candidate, &candidate.productions[index],
                error_buf, error_buf_size)) {
            goto done;
        }
        candidate.productions[index].execution_kind =
            ppa_action_v1_execution_classify(
                &candidate, &candidate.productions[index]);
    }
    if (!ppa_action_v1_program_digest(
            &candidate, candidate.program_digest)) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "cannot digest projection action program");
        goto done;
    }
    ppatom_projection_action_v1_program_free(program);
    *program = candidate;
    memset(&candidate, 0, sizeof(candidate));
    ok = true;

done:
    ppatom_projection_action_v1_program_free(&candidate);
    return ok;
}

bool ppatom_projection_action_v1_program_validate(
    const PPAtomProjectionActionV1Program *program,
    const PPActionBytecodeV1Program *actions,
    const PPAtomProjectionV1Plan *projection,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionActionV1Program expected;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    ppatom_projection_action_v1_program_init(&expected);
    if (!program || !actions || !projection ||
        !ppatom_projection_action_v1_program_build_prevalidated(
            &expected, actions, projection,
            error_buf, error_buf_size) ||
        program->production_len != expected.production_len ||
        program->instruction_len != expected.instruction_len ||
        strcmp(program->action_program_digest,
               expected.action_program_digest) != 0 ||
        strcmp(program->projection_plan_digest,
               expected.projection_plan_digest) != 0 ||
        strcmp(program->program_digest, expected.program_digest) != 0 ||
        !program->codepoint_label ||
        !atom_eq(program->codepoint_label, expected.codepoint_label)) {
        goto rejected;
    }
    for (index = 0u; index < program->production_len; index++) {
        const PPAtomProjectionActionV1Production *left =
            &program->productions[index];
        const PPAtomProjectionActionV1Production *right =
            &expected.productions[index];
        if (left->instruction_begin != right->instruction_begin ||
            left->instruction_len != right->instruction_len ||
            left->arity != right->arity ||
            left->max_stack_len != right->max_stack_len ||
            left->execution_kind != right->execution_kind) {
            goto rejected;
        }
    }
    for (index = 0u; index < program->instruction_len; index++) {
        const PPAtomProjectionActionV1Instruction *left =
            &program->instructions[index];
        const PPAtomProjectionActionV1Instruction *right =
            &expected.instructions[index];
        if (left->kind != right->kind ||
            left->constant_kind != right->constant_kind ||
            left->operand != right->operand ||
            left->dense_id != right->dense_id ||
            (!!left->term != !!right->term) ||
            (left->term && !atom_eq(left->term, right->term))) {
            goto rejected;
        }
    }
    {
        char digest[65];
        if (!ppa_action_v1_program_digest(program, digest) ||
            strcmp(digest, program->program_digest) != 0) {
            goto rejected;
        }
    }
    ok = true;
    goto done;

rejected:
    if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action program does not match its sources");
    }
done:
    ppatom_projection_action_v1_program_free(&expected);
    return ok;
}

void ppatom_projection_action_v1_store_init(
    PPAtomProjectionActionV1Store *store) {
    if (store)
        store->implementation = NULL;
}

void ppatom_projection_action_v1_store_free(
    PPAtomProjectionActionV1Store *store) {
    PPAtomProjectionActionV1StoreImpl *impl;

    if (!store)
        return;
    impl = store->implementation;
    if (impl) {
        free(impl->text_scalars);
        free(impl->texts);
        free(impl->nodes);
        free(impl);
    }
    store->implementation = NULL;
}

void ppatom_projection_action_v1_store_reset(
    PPAtomProjectionActionV1Store *store) {
    PPAtomProjectionActionV1StoreImpl *impl;

    if (!store || !(impl = store->implementation))
        return;
    impl->node_len = 0u;
    impl->text_len = 0u;
    impl->text_scalar_len = 0u;
}

bool ppatom_projection_action_v1_scalar(
    uint32_t scalar,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || scalar > UINT32_C(0x10ffff) ||
        (scalar >= UINT32_C(0xd800) &&
         scalar <= UINT32_C(0xdfff))) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action scalar is not a Unicode scalar value");
    }
    *out = (PPAtomProjectionActionV1Value){
        .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_SCALAR,
        .operand = scalar,
    };
    return true;
}

bool ppatom_projection_action_v1_eof(
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out)
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection action EOF output is absent");
    *out = (PPAtomProjectionActionV1Value){
        .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_EOF,
        .operand = 0u,
    };
    return true;
}

bool ppatom_projection_action_v1_text_binding(
    const PPAtomProjectionActionV1Program *program,
    uint32_t rule_dense_id,
    PPAtomProjectionActionV1TextBinding *out,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionActionV1TextBinding result = {
        .rule_dense_id = rule_dense_id,
        .rule_constant_instruction = UINT32_MAX,
        .node_instruction = UINT32_MAX,
    };
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        memset(out, 0, sizeof(*out));
    if (!program || !program->instructions || !out) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "bad compact text binding arguments");
    }
    for (index = 0u; index < program->instruction_len; index++) {
        const PPAtomProjectionActionV1Instruction *instruction =
            &program->instructions[index];
        if (instruction->kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT &&
            instruction->constant_kind ==
                PPATOM_PROJECTION_ACTION_V1_CONSTANT_RULE &&
            instruction->dense_id == rule_dense_id &&
            result.rule_constant_instruction == UINT32_MAX) {
            result.rule_constant_instruction = index;
        }
        if (instruction->kind ==
                PPATOM_PROJECTION_ACTION_V1_APPLY_NODE &&
            result.node_instruction == UINT32_MAX) {
            result.node_instruction = index;
        }
    }
    if (result.rule_constant_instruction == UINT32_MAX ||
        result.node_instruction == UINT32_MAX) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "projection rule has no compact text-node carrier");
    }
    *out = result;
    return true;
}

static bool ppa_action_v1_store_text_grow(
    PPAtomProjectionActionV1StoreImpl *store,
    uint32_t scalar_add) {
    PPAtomProjectionActionV1Text *next_texts;
    uint32_t *next_scalars;
    uint32_t text_cap;
    uint32_t scalar_cap;
    uint32_t scalar_needed;

    if (!store || store->text_len == UINT32_MAX ||
        scalar_add > UINT32_MAX - store->text_scalar_len) {
        return false;
    }
    scalar_needed = store->text_scalar_len + scalar_add;
    if (store->text_len == store->text_cap) {
        text_cap = store->text_cap ? store->text_cap * 2u : 256u;
        if (text_cap < store->text_cap ||
            !ppa_action_v1_array_fits(
                text_cap, sizeof(*store->texts))) {
            return false;
        }
        next_texts = realloc(
            store->texts, sizeof(*store->texts) * text_cap);
        if (!next_texts)
            return false;
        store->texts = next_texts;
        store->text_cap = text_cap;
    }
    if (scalar_needed > store->text_scalar_cap) {
        scalar_cap = store->text_scalar_cap
            ? store->text_scalar_cap : 1024u;
        while (scalar_cap < scalar_needed) {
            uint32_t next = scalar_cap * 2u;
            if (next < scalar_cap) {
                scalar_cap = scalar_needed;
                break;
            }
            scalar_cap = next;
        }
        if (!ppa_action_v1_array_fits(
                scalar_cap, sizeof(*store->text_scalars))) {
            return false;
        }
        next_scalars = realloc(
            store->text_scalars,
            sizeof(*store->text_scalars) * scalar_cap);
        if (!next_scalars)
            return false;
        store->text_scalars = next_scalars;
        store->text_scalar_cap = scalar_cap;
    }
    return true;
}

static bool ppa_action_v1_store_append(
    PPAtomProjectionActionV1StoreImpl *store,
    PPAtomProjectionActionV1Node node,
    uint32_t *out);

bool ppatom_projection_action_v1_text_node_prevalidated(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1TextBinding *binding,
    const uint32_t *scalars,
    uint32_t scalar_len,
    PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size) {
    PPAtomProjectionActionV1StoreImpl *impl;
    PPAtomProjectionActionV1Text text;
    PPAtomProjectionActionV1Node node;
    uint32_t node_mark;
    uint32_t text_mark;
    uint32_t scalar_mark;
    uint32_t node_index;
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        memset(out, 0, sizeof(*out));
    if (!program || !binding || !store || !out ||
        (scalar_len > 0u && !scalars) ||
        binding->rule_constant_instruction >= program->instruction_len ||
        binding->node_instruction >= program->instruction_len ||
        program->instructions[binding->rule_constant_instruction].kind !=
            PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT ||
        program->instructions[binding->rule_constant_instruction]
                .constant_kind !=
            PPATOM_PROJECTION_ACTION_V1_CONSTANT_RULE ||
        program->instructions[binding->rule_constant_instruction].dense_id !=
            binding->rule_dense_id ||
        program->instructions[binding->node_instruction].kind !=
            PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "bad compact text-node binding or input");
    }
    for (index = 0u; index < scalar_len; index++) {
        if (scalars[index] > UINT32_C(0x10ffff) ||
            (scalars[index] >= UINT32_C(0xd800) &&
             scalars[index] <= UINT32_C(0xdfff))) {
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "compact text contains a non-Unicode scalar");
        }
    }
    if (!store->implementation) {
        store->implementation = calloc(1u, sizeof(*impl));
        if (!store->implementation) {
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "cannot allocate compact projection action store");
        }
    }
    impl = store->implementation;
    node_mark = impl->node_len;
    text_mark = impl->text_len;
    scalar_mark = impl->text_scalar_len;
    if (!ppa_action_v1_store_text_grow(impl, scalar_len))
        goto allocation_failure;
    if (scalar_len > 0u) {
        memcpy(
            &impl->text_scalars[impl->text_scalar_len], scalars,
            sizeof(*scalars) * (size_t)scalar_len);
    }
    text = (PPAtomProjectionActionV1Text){
        .scalar_begin = impl->text_scalar_len,
        .scalar_len = scalar_len,
    };
    impl->text_scalar_len += scalar_len;
    impl->texts[impl->text_len++] = text;
    node = (PPAtomProjectionActionV1Node){
        .instruction_index = binding->node_instruction,
        .left = {
            .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT,
            .operand = binding->rule_constant_instruction,
        },
        .right = {
            .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_TEXT,
            .operand = text_mark,
        },
    };
    if (!ppa_action_v1_store_append(impl, node, &node_index))
        goto allocation_failure;
    *out = (PPAtomProjectionActionV1Value){
        .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE,
        .operand = node_index,
    };
    return true;

allocation_failure:
    impl->node_len = node_mark;
    impl->text_len = text_mark;
    impl->text_scalar_len = scalar_mark;
    return ppa_action_v1_error(
        error_buf, error_buf_size,
        "cannot retain compact projection text");
}

static bool ppa_action_v1_value_valid(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1StoreImpl *store,
    PPAtomProjectionActionV1Value value) {
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_SCALAR) {
        return value.operand <= UINT32_C(0x10ffff) &&
            !(value.operand >= UINT32_C(0xd800) &&
              value.operand <= UINT32_C(0xdfff));
    }
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT) {
        return value.operand < program->instruction_len &&
            program->instructions[value.operand].kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT;
    }
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_EOF)
        return value.operand == 0u;
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_TEXT) {
        const PPAtomProjectionActionV1Text *text;
        if (!store || value.operand >= store->text_len)
            return false;
        text = &store->texts[value.operand];
        return text->scalar_begin <= store->text_scalar_len &&
            text->scalar_len <=
                store->text_scalar_len - text->scalar_begin;
    }
    return value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE &&
        store && value.operand < store->node_len;
}

static bool ppa_action_v1_store_append(
    PPAtomProjectionActionV1StoreImpl *store,
    PPAtomProjectionActionV1Node node,
    uint32_t *out) {
    PPAtomProjectionActionV1Node *next;
    uint32_t cap;

    if (store->node_len == UINT32_MAX)
        return false;
    if (store->node_len == store->node_cap) {
        cap = store->node_cap ? store->node_cap * 2u : 256u;
        if (cap < store->node_cap ||
            !ppa_action_v1_array_fits(cap, sizeof(*store->nodes))) {
            return false;
        }
        next = realloc(store->nodes, sizeof(*store->nodes) * cap);
        if (!next)
            return false;
        store->nodes = next;
        store->node_cap = cap;
    }
    *out = store->node_len;
    store->nodes[store->node_len++] = node;
    return true;
}

bool ppatom_projection_action_v1_execute_prevalidated(
    const PPAtomProjectionActionV1Program *program,
    uint32_t production_id,
    const PPAtomProjectionActionV1Value *slots,
    uint32_t slot_len,
    PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomProjectionActionV1Production *production;
    PPAtomProjectionActionV1StoreImpl *impl;
    PPAtomProjectionActionV1Value local[16];
    PPAtomProjectionActionV1Value *stack = local;
    uint32_t stack_len = 0u;
    uint32_t node_mark;
    uint32_t index;
    bool owned = false;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        memset(out, 0, sizeof(*out));
    if (!program || !program->productions || !program->instructions ||
        !store || !out || production_id >= program->production_len ||
        (slot_len > 0u && !slots)) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "bad compact projection action execution arguments");
    }
    if (!store->implementation) {
        store->implementation = calloc(1u, sizeof(*impl));
        if (!store->implementation)
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "cannot allocate compact projection action store");
    }
    impl = store->implementation;
    node_mark = impl->node_len;
    production = &program->productions[production_id];
    if (slot_len != production->arity ||
        production->max_stack_len == 0u) {
        goto malformed;
    }
    for (index = 0u; index < slot_len; index++) {
        if (!ppa_action_v1_value_valid(program, impl, slots[index]))
            goto malformed;
    }
    if (production->max_stack_len >
            sizeof(local) / sizeof(local[0])) {
        stack = malloc(sizeof(*stack) * production->max_stack_len);
        if (!stack)
            goto done;
        owned = true;
    }
    for (index = 0u; index < production->instruction_len; index++) {
        uint32_t instruction_index =
            production->instruction_begin + index;
        const PPAtomProjectionActionV1Instruction *instruction =
            &program->instructions[instruction_index];

        if (instruction->kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT) {
            if (instruction->operand >= slot_len ||
                stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = slots[instruction->operand];
        } else if (instruction->kind ==
                       PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT) {
            if (stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = (PPAtomProjectionActionV1Value){
                .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT,
                .operand = instruction_index,
            };
        } else {
            PPAtomProjectionActionV1Node node;
            uint32_t begin;
            uint32_t node_index;
            if (stack_len < 2u)
                goto malformed;
            begin = stack_len - 2u;
            if (instruction->kind ==
                    PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
                PPAtomProjectionActionV1Value label = stack[begin];
                const PPAtomProjectionActionV1Instruction *label_source;
                if (label.kind !=
                        PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT ||
                    label.operand >= program->instruction_len ||
                    (label_source = &program->instructions[label.operand])
                            ->kind !=
                        PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT ||
                    !label_source->term ||
                    label_source->term->kind != ATOM_SYMBOL) {
                    goto malformed;
                }
            }
            node = (PPAtomProjectionActionV1Node){
                .instruction_index = instruction_index,
                .left = stack[begin],
                .right = stack[begin + 1u],
            };
            if (!ppa_action_v1_store_append(impl, node, &node_index))
                goto done;
            stack_len = begin;
            stack[stack_len++] = (PPAtomProjectionActionV1Value){
                .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE,
                .operand = node_index,
            };
        }
    }
    if (stack_len != 1u)
        goto malformed;
    *out = stack[0];
    ok = true;
    goto done;

malformed:
    ppa_action_v1_error(
        error_buf, error_buf_size,
        "compact projection action program or values are malformed");
done:
    if (!ok)
        impl->node_len = node_mark;
    if (owned)
        free(stack);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "failed to execute compact projection action");
    }
    return ok;
}

static bool ppa_action_v1_view_slot(
    const void *slots,
    uint32_t slot_len,
    size_t slot_stride,
    uint32_t index,
    PPAtomProjectionActionV1Value *out) {
    if (!slots || !out || index >= slot_len ||
        slot_stride < sizeof(*out) ||
        (size_t)index > SIZE_MAX / slot_stride) {
        return false;
    }
    memcpy(
        out, (const uint8_t *)slots + (size_t)index * slot_stride,
        sizeof(*out));
    return true;
}

static bool ppa_action_v1_direct_push(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1StoreImpl *store,
    uint32_t instruction_index,
    const void *slots,
    uint32_t slot_len,
    size_t slot_stride,
    PPAtomProjectionActionV1Value *out) {
    const PPAtomProjectionActionV1Instruction *instruction;

    if (!program || instruction_index >= program->instruction_len || !out)
        return false;
    instruction = &program->instructions[instruction_index];
    if (instruction->kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT) {
        return ppa_action_v1_view_slot(
                slots, slot_len, slot_stride, instruction->operand, out) &&
            ppa_action_v1_value_valid(program, store, *out);
    }
    if (instruction->kind == PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT) {
        *out = (PPAtomProjectionActionV1Value){
            .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT,
            .operand = instruction_index,
        };
        return true;
    }
    return false;
}

bool ppatom_projection_action_v1_execute_specialized_view_prevalidated(
    const PPAtomProjectionActionV1Program *program,
    uint32_t production_id,
    const void *slots,
    uint32_t slot_len,
    size_t slot_stride,
    PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomProjectionActionV1Production *production;
    const PPAtomProjectionActionV1Instruction *instructions;
    PPAtomProjectionActionV1StoreImpl *impl;

    if (!program || !program->productions || !program->instructions ||
        !store || !out || production_id >= program->production_len ||
        (slot_len > 0u &&
         (!slots || slot_stride < sizeof(PPAtomProjectionActionV1Value) ||
          (size_t)(slot_len - 1u) > SIZE_MAX / slot_stride))) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "bad specialized projection action execution arguments");
    }
    production = &program->productions[production_id];
    if (slot_len != production->arity ||
        production->instruction_begin > program->instruction_len ||
        production->instruction_len >
            program->instruction_len - production->instruction_begin) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "specialized projection action shape is malformed");
    }
    instructions = &program->instructions[production->instruction_begin];
    impl = store->implementation;
    if (production->execution_kind ==
            PPATOM_PROJECTION_ACTION_V1_EXECUTE_VALUE) {
        if (production->instruction_len != 1u ||
            !ppa_action_v1_direct_push(
                program, impl, production->instruction_begin,
                slots, slot_len, slot_stride, out)) {
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "specialized projection value action is malformed");
        }
        return true;
    }
    if (production->execution_kind ==
            PPATOM_PROJECTION_ACTION_V1_EXECUTE_BINARY) {
        PPAtomProjectionActionV1Value left;
        PPAtomProjectionActionV1Value right;
        PPAtomProjectionActionV1Node node;
        uint32_t node_mark;
        uint32_t node_index;
        uint32_t apply_index = production->instruction_begin + 2u;

        if (production->instruction_len != 3u ||
            instructions[2].kind < PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR ||
            instructions[2].kind > PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "specialized binary projection action is malformed");
        }
        if (!impl) {
            store->implementation = calloc(1u, sizeof(*impl));
            if (!store->implementation) {
                return ppa_action_v1_error(
                    error_buf, error_buf_size,
                    "cannot allocate specialized projection action store");
            }
            impl = store->implementation;
        }
        node_mark = impl->node_len;
        if (!ppa_action_v1_direct_push(
                program, impl, production->instruction_begin,
                slots, slot_len, slot_stride, &left) ||
            !ppa_action_v1_direct_push(
                program, impl, production->instruction_begin + 1u,
                slots, slot_len, slot_stride, &right) ||
            (instructions[2].kind == PPATOM_PROJECTION_ACTION_V1_APPLY_NODE &&
             (left.kind != PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT ||
              left.operand >= program->instruction_len ||
              program->instructions[left.operand].kind !=
                  PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT ||
              !program->instructions[left.operand].term ||
              program->instructions[left.operand].term->kind !=
                  ATOM_SYMBOL))) {
            impl->node_len = node_mark;
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "specialized binary projection values are malformed");
        }
        node = (PPAtomProjectionActionV1Node){
            .instruction_index = apply_index,
            .left = left,
            .right = right,
        };
        if (!ppa_action_v1_store_append(impl, node, &node_index)) {
            impl->node_len = node_mark;
            return ppa_action_v1_error(
                error_buf, error_buf_size,
                "cannot append specialized projection action node");
        }
        *out = (PPAtomProjectionActionV1Value){
            .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE,
            .operand = node_index,
        };
        return true;
    }
    if (production->execution_kind ==
            PPATOM_PROJECTION_ACTION_V1_EXECUTE_INTERPRETED) {
        PPAtomProjectionActionV1Value local[16] = {0};
        PPAtomProjectionActionV1Value *values = local;
        uint32_t index;
        bool owned = false;
        bool ok;

        if (slot_len > sizeof(local) / sizeof(local[0])) {
            values = malloc(sizeof(*values) * (size_t)slot_len);
            if (!values) {
                return ppa_action_v1_error(
                    error_buf, error_buf_size,
                    "cannot allocate interpreted projection action view");
            }
            owned = true;
        }
        for (index = 0u; index < slot_len; index++) {
            if (!ppa_action_v1_view_slot(
                    slots, slot_len, slot_stride, index, &values[index])) {
                if (owned)
                    free(values);
                return ppa_action_v1_error(
                    error_buf, error_buf_size,
                    "interpreted projection action view is malformed");
            }
        }
        ok = ppatom_projection_action_v1_execute_prevalidated(
            program, production_id, values, slot_len, store, out,
            error_buf, error_buf_size);
        if (owned)
            free(values);
        return ok;
    }
    return ppa_action_v1_error(
        error_buf, error_buf_size,
        "unknown specialized projection execution kind");
}

static Atom *ppa_action_v1_materialize(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1StoreImpl *store,
    PPAtomProjectionActionV1Value value,
    Arena *arena,
    uint32_t depth) {
    const PPAtomProjectionActionV1Instruction *instruction;
    const PPAtomProjectionActionV1Node *node;
    Atom **items;

    if (depth == 0u ||
        !ppa_action_v1_value_valid(program, store, value)) {
        return NULL;
    }
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT) {
        instruction = &program->instructions[value.operand];
        return atom_deep_copy(arena, instruction->term);
    }
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_EOF)
        return atom_symbol(arena, "eof");
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_SCALAR) {
        items = arena_alloc(arena, sizeof(*items) * 2u);
        if (!items)
            return NULL;
        items[0] = atom_deep_copy(arena, program->codepoint_label);
        items[1] = atom_int(arena, (int64_t)value.operand);
        if (!items[0] || !items[1])
            return NULL;
        return atom_expr(arena, items, 2u);
    }
    if (value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_TEXT)
        return NULL;
    node = &store->nodes[value.operand];
    if (node->instruction_index >= program->instruction_len)
        return NULL;
    instruction = &program->instructions[node->instruction_index];
    if (instruction->kind != PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR &&
        instruction->kind != PPATOM_PROJECTION_ACTION_V1_APPLY_CONS &&
        instruction->kind != PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
        return NULL;
    }
    items = arena_alloc(arena, sizeof(*items) * 3u);
    if (!items)
        return NULL;
    items[0] = atom_deep_copy(arena, instruction->term);
    items[1] = ppa_action_v1_materialize(
        program, store, node->left, arena, depth - 1u);
    items[2] = ppa_action_v1_materialize(
        program, store, node->right, arena, depth - 1u);
    if (!items[0] || !items[1] || !items[2])
        return NULL;
    return atom_expr(arena, items, 3u);
}

bool ppatom_projection_action_v1_value_to_atom(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1Store *store,
    PPAtomProjectionActionV1Value value,
    Arena *output_arena,
    uint32_t depth_limit,
    Atom **out,
    char *error_buf,
    size_t error_buf_size) {
    const PPAtomProjectionActionV1StoreImpl *impl;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        *out = NULL;
    impl = store ? store->implementation : NULL;
    if (!program || !output_arena || !out || depth_limit == 0u ||
        ((value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE ||
          value.kind == PPATOM_PROJECTION_ACTION_V1_VALUE_TEXT) &&
         !impl)) {
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "bad compact projection materialization arguments");
    }
    *out = ppa_action_v1_materialize(
        program, impl, value, output_arena, depth_limit);
    if (!*out)
        return ppa_action_v1_error(
            error_buf, error_buf_size,
            "cannot materialize compact projection value");
    return true;
}

typedef enum {
    PPA_ACTION_V1_TASK_NODE = 0,
    PPA_ACTION_V1_TASK_NODE_LIST = 1,
    PPA_ACTION_V1_TASK_TEXT = 2,
    PPA_ACTION_V1_TASK_EVENT = 3
} PPAtomProjectionActionV1TaskKind;

typedef struct {
    PPAtomProjectionActionV1TaskKind kind;
    PPAtomProjectionActionV1Value value;
    PPAtomProjectionEventsV1Event event;
    uint32_t depth;
    bool allow_wrappers;
} PPAtomProjectionActionV1Task;

typedef struct {
    PPAtomProjectionActionV1Task *data;
    uint32_t len;
    uint32_t cap;
} PPAtomProjectionActionV1TaskVec;

static bool ppa_action_v1_task_push(
    PPAtomProjectionActionV1TaskVec *tasks,
    PPAtomProjectionActionV1Task task) {
    PPAtomProjectionActionV1Task *next;
    uint32_t cap;

    if (tasks->len == UINT32_MAX)
        return false;
    if (tasks->len == tasks->cap) {
        cap = tasks->cap ? tasks->cap * 2u : 64u;
        if (cap < tasks->cap ||
            !ppa_action_v1_array_fits(cap, sizeof(*tasks->data))) {
            return false;
        }
        next = realloc(tasks->data, sizeof(*tasks->data) * cap);
        if (!next)
            return false;
        tasks->data = next;
        tasks->cap = cap;
    }
    tasks->data[tasks->len++] = task;
    return true;
}

static bool ppa_action_v1_task_value(
    PPAtomProjectionActionV1TaskVec *tasks,
    PPAtomProjectionActionV1TaskKind kind,
    PPAtomProjectionActionV1Value value,
    uint32_t depth,
    bool allow_wrappers) {
    return ppa_action_v1_task_push(
        tasks,
        (PPAtomProjectionActionV1Task){
            .kind = kind,
            .value = value,
            .depth = depth,
            .allow_wrappers = allow_wrappers,
        });
}

static bool ppa_action_v1_task_event(
    PPAtomProjectionActionV1TaskVec *tasks,
    PPAtomProjectionEventsV1Kind kind,
    uint32_t operand) {
    return ppa_action_v1_task_push(
        tasks,
        (PPAtomProjectionActionV1Task){
            .kind = PPA_ACTION_V1_TASK_EVENT,
            .event = {kind, operand},
        });
}

static const PPAtomProjectionActionV1Instruction *
ppa_action_v1_constant_instruction(
    const PPAtomProjectionActionV1Program *program,
    PPAtomProjectionActionV1Value value) {
    const PPAtomProjectionActionV1Instruction *instruction;

    if (value.kind != PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT ||
        value.operand >= program->instruction_len)
        return NULL;
    instruction = &program->instructions[value.operand];
    return instruction->kind ==
            PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT
        ? instruction : NULL;
}

static const PPAtomProjectionActionV1Node *ppa_action_v1_node(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionActionV1StoreImpl *store,
    PPAtomProjectionActionV1Value value,
    PPAtomProjectionActionV1InstructionKind *kind) {
    const PPAtomProjectionActionV1Node *node;
    const PPAtomProjectionActionV1Instruction *instruction;

    if (!store ||
        value.kind != PPATOM_PROJECTION_ACTION_V1_VALUE_COMPOSITE ||
        value.operand >= store->node_len)
        return NULL;
    node = &store->nodes[value.operand];
    if (node->instruction_index >= program->instruction_len)
        return NULL;
    instruction = &program->instructions[node->instruction_index];
    if (instruction->kind != PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR &&
        instruction->kind != PPATOM_PROJECTION_ACTION_V1_APPLY_CONS &&
        instruction->kind != PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
        return NULL;
    }
    *kind = instruction->kind;
    return node;
}

static bool ppa_action_v1_is_nil(
    const PPAtomProjectionActionV1Program *program,
    PPAtomProjectionActionV1Value value) {
    const PPAtomProjectionActionV1Instruction *instruction =
        ppa_action_v1_constant_instruction(program, value);
    return instruction && instruction->constant_kind ==
        PPATOM_PROJECTION_ACTION_V1_CONSTANT_NIL;
}

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
    size_t error_buf_size) {
    const PPAtomProjectionActionV1StoreImpl *impl;
    PPAtomProjectionV1ProvenanceView provenance;
    PPAtomProjectionActionV1InstructionKind root_kind;
    const PPAtomProjectionActionV1Node *root;
    const PPAtomProjectionActionV1Instruction *root_label;
    PPAtomProjectionActionV1TaskVec tasks = {0};
    PPAtomProjectionEventsV1Builder builder;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_atoms)
        *out_atoms = NULL;
    if (out_len)
        *out_len = 0u;
    impl = store ? store->implementation : NULL;
    ppatom_projection_events_v1_builder_init(&builder);
    if (!program || !impl || !projection || !output_arena ||
        !out_atoms || !out_len || depth_limit == 0u ||
        !ppatom_projection_v1_plan_provenance(
            projection, &provenance, error_buf, error_buf_size) ||
        strcmp(program->projection_plan_digest,
               provenance.plan_digest) != 0 ||
        !(root = ppa_action_v1_node(
              program, impl, document, &root_kind)) ||
        root_kind != PPATOM_PROJECTION_ACTION_V1_APPLY_NODE ||
        !(root_label = ppa_action_v1_constant_instruction(
              program, root->left)) ||
        root_label->constant_kind !=
            PPATOM_PROJECTION_ACTION_V1_CONSTANT_DOCUMENT) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "compact projection root is not its bound document");
        goto done;
    }
    if (!ppatom_projection_events_v1_builder_bind(
            &builder, projection, output_arena, depth_limit,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (!ppa_action_v1_task_event(
            &tasks, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_END, 0u) ||
        !ppa_action_v1_task_value(
            &tasks, PPA_ACTION_V1_TASK_NODE_LIST,
            root->right, 1u, false) ||
        !ppa_action_v1_task_event(
            &tasks, PPATOM_PROJECTION_EVENTS_V1_DOCUMENT_BEGIN, 0u)) {
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "cannot allocate compact projection task stack");
        goto done;
    }
    while (tasks.len > 0u) {
        PPAtomProjectionActionV1Task task = tasks.data[--tasks.len];
        PPAtomProjectionActionV1InstructionKind node_kind;
        const PPAtomProjectionActionV1Node *node;
        const PPAtomProjectionActionV1Instruction *label;

        if (task.kind == PPA_ACTION_V1_TASK_EVENT) {
            if (!ppatom_projection_events_v1_builder_emit(
                    &builder, task.event,
                    error_buf, error_buf_size)) {
                goto done;
            }
            continue;
        }
        if (task.depth > depth_limit) {
            ppa_action_v1_error(
                error_buf, error_buf_size,
                "compact projection exceeded its depth limit");
            goto done;
        }
        if (task.kind == PPA_ACTION_V1_TASK_NODE_LIST) {
            if (ppa_action_v1_is_nil(program, task.value))
                continue;
            node = ppa_action_v1_node(
                program, impl, task.value, &node_kind);
            if (!node ||
                (node_kind != PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR &&
                 node_kind != PPATOM_PROJECTION_ACTION_V1_APPLY_CONS) ||
                !ppa_action_v1_task_value(
                    &tasks, PPA_ACTION_V1_TASK_NODE_LIST,
                    node->right, task.depth, false) ||
                !ppa_action_v1_task_value(
                    &tasks, PPA_ACTION_V1_TASK_NODE,
                    node->left, task.depth + 1u, false)) {
                ppa_action_v1_error(
                    error_buf, error_buf_size,
                    "compact projection node list is malformed");
                goto done;
            }
            continue;
        }
        if (task.kind == PPA_ACTION_V1_TASK_NODE) {
            node = ppa_action_v1_node(
                program, impl, task.value, &node_kind);
            if (!node ||
                node_kind != PPATOM_PROJECTION_ACTION_V1_APPLY_NODE ||
                !(label = ppa_action_v1_constant_instruction(
                      program, node->left))) {
                ppa_action_v1_error(
                    error_buf, error_buf_size,
                    "compact projection expected a semantic node");
                goto done;
            }
            if (label->constant_kind ==
                    PPATOM_PROJECTION_ACTION_V1_CONSTANT_EXPRESSION) {
                if (!ppa_action_v1_task_event(
                        &tasks,
                        PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_END, 0u) ||
                    !ppa_action_v1_task_value(
                        &tasks, PPA_ACTION_V1_TASK_NODE_LIST,
                        node->right, task.depth + 1u, false) ||
                    !ppa_action_v1_task_event(
                        &tasks,
                        PPATOM_PROJECTION_EVENTS_V1_EXPRESSION_BEGIN, 0u)) {
                    goto allocation_failure;
                }
                continue;
            }
            if (label->constant_kind ==
                    PPATOM_PROJECTION_ACTION_V1_CONSTANT_RULE) {
                PPAtomProjectionV1RuleView rule;
                if (!ppatom_projection_v1_plan_rule(
                        projection, label->dense_id, &rule,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                if (!ppa_action_v1_task_event(
                        &tasks, PPATOM_PROJECTION_EVENTS_V1_LEAF_END,
                        label->dense_id) ||
                    !ppa_action_v1_task_value(
                        &tasks, PPA_ACTION_V1_TASK_TEXT,
                        node->right, task.depth + 1u,
                        rule.text_mode != PPATOM_PROJECTION_V1_TEXT_RAW) ||
                    !ppa_action_v1_task_event(
                        &tasks, PPATOM_PROJECTION_EVENTS_V1_LEAF_BEGIN,
                        label->dense_id)) {
                    goto allocation_failure;
                }
                continue;
            }
            ppa_action_v1_error(
                error_buf, error_buf_size,
                "compact semantic node has no projection rule");
            goto done;
        }
        if (task.kind == PPA_ACTION_V1_TASK_TEXT) {
            if (ppa_action_v1_is_nil(program, task.value))
                continue;
            if (task.value.kind ==
                    PPATOM_PROJECTION_ACTION_V1_VALUE_TEXT) {
                const PPAtomProjectionActionV1Text *text;
                uint32_t scalar_index;
                if (!impl || task.value.operand >= impl->text_len) {
                    ppa_action_v1_error(
                        error_buf, error_buf_size,
                        "compact retained text handle is stale");
                    goto done;
                }
                text = &impl->texts[task.value.operand];
                if (text->scalar_begin > impl->text_scalar_len ||
                    text->scalar_len >
                        impl->text_scalar_len - text->scalar_begin) {
                    ppa_action_v1_error(
                        error_buf, error_buf_size,
                        "compact retained text span is malformed");
                    goto done;
                }
                for (scalar_index = 0u;
                     scalar_index < text->scalar_len;
                     scalar_index++) {
                    if (!ppatom_projection_events_v1_builder_emit(
                            &builder,
                            (PPAtomProjectionEventsV1Event){
                                PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                                impl->text_scalars[
                                    text->scalar_begin + scalar_index],
                            },
                            error_buf, error_buf_size)) {
                        goto done;
                    }
                }
                continue;
            }
            if (task.value.kind ==
                    PPATOM_PROJECTION_ACTION_V1_VALUE_SCALAR) {
                if (!ppatom_projection_events_v1_builder_emit(
                        &builder,
                        (PPAtomProjectionEventsV1Event){
                            PPATOM_PROJECTION_EVENTS_V1_SCALAR,
                            task.value.operand,
                        },
                        error_buf, error_buf_size)) {
                    goto done;
                }
                continue;
            }
            node = ppa_action_v1_node(
                program, impl, task.value, &node_kind);
            if (node &&
                (node_kind == PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR ||
                 node_kind == PPATOM_PROJECTION_ACTION_V1_APPLY_CONS)) {
                if (!ppa_action_v1_task_value(
                        &tasks, PPA_ACTION_V1_TASK_TEXT,
                        node->right, task.depth,
                        task.allow_wrappers) ||
                    !ppa_action_v1_task_value(
                        &tasks, PPA_ACTION_V1_TASK_TEXT,
                        node->left, task.depth,
                        task.allow_wrappers)) {
                    goto allocation_failure;
                }
                continue;
            }
            if (task.allow_wrappers && node &&
                node_kind == PPATOM_PROJECTION_ACTION_V1_APPLY_NODE &&
                (label = ppa_action_v1_constant_instruction(
                     program, node->left)) &&
                label->constant_kind ==
                    PPATOM_PROJECTION_ACTION_V1_CONSTANT_WRAPPER) {
                if (!ppa_action_v1_task_event(
                        &tasks, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_END,
                        label->dense_id) ||
                    !ppa_action_v1_task_value(
                        &tasks, PPA_ACTION_V1_TASK_TEXT,
                        node->right, task.depth + 1u, false) ||
                    !ppa_action_v1_task_event(
                        &tasks, PPATOM_PROJECTION_EVENTS_V1_WRAPPER_BEGIN,
                        label->dense_id)) {
                    goto allocation_failure;
                }
                continue;
            }
            ppa_action_v1_error(
                error_buf, error_buf_size,
                "compact semantic text spine is malformed");
            goto done;
        }
        ppa_action_v1_error(
            error_buf, error_buf_size,
            "compact projection task kind is invalid");
        goto done;
    }
    if (!ppatom_projection_events_v1_builder_finish(
            &builder, out_atoms, out_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    ok = true;
    goto done;

allocation_failure:
    ppa_action_v1_error(
        error_buf, error_buf_size,
        "cannot allocate compact projection task stack");
done:
    free(tasks.data);
    ppatom_projection_events_v1_builder_free(&builder);
    return ok;
}
