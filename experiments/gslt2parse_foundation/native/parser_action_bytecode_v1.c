#include "parser_action_bytecode_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    PPActionBytecodeV1Instruction *data;
    uint32_t len;
    uint32_t cap;
} PPActionBytecodeV1ExpectedVec;

typedef enum {
    PP_ACTION_BYTECODE_V1_FRAME_ACTION = 0,
    PP_ACTION_BYTECODE_V1_FRAME_APPLY = 1
} PPActionBytecodeV1FrameKind;

typedef struct {
    PPActionBytecodeV1FrameKind kind;
    Atom *term;
    uint32_t arity;
} PPActionBytecodeV1Frame;

typedef struct {
    PPActionBytecodeV1Frame *data;
    uint32_t len;
    uint32_t cap;
} PPActionBytecodeV1FrameVec;

typedef struct {
    const PPABIV1Pack *pack;
    const PPGuardPlanV1 *guard_plan;
} PPActionBytecodeV1Source;

static bool pp_action_bytecode_v1_error(char *error_buf,
                                        size_t error_buf_size,
                                        const char *format,
                                        ...) {
    va_list arguments;

    if (error_buf && error_buf_size > 0u) {
        va_start(arguments, format);
        (void)vsnprintf(error_buf, error_buf_size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool pp_action_bytecode_v1_expr_head(const Atom *atom,
                                            const char *head,
                                            CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static bool pp_action_bytecode_v1_digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || digest[64] != '\0')
        return false;
    for (index = 0u; index < 64u; index++) {
        char ch = digest[index];
        if (!((ch >= '0' && ch <= '9') ||
              (ch >= 'a' && ch <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool pp_action_bytecode_v1_qindex(const Atom *term, uint32_t *out) {
    uint32_t value = 0u;

    while (!atom_is_symbol((Atom *)term, "q-zero")) {
        if (!pp_action_bytecode_v1_expr_head(term, "q-succ", 1u) ||
            value == UINT32_MAX) {
            return false;
        }
        value++;
        term = term->expr.elems[1];
    }
    *out = value;
    return true;
}

static bool pp_action_bytecode_v1_renderable(const Atom *term) {
    uint8_t *canonical = NULL;
    size_t canonical_len = 0u;
    bool ok = fh_ground_term_v1_render(
        term, &canonical, &canonical_len, NULL, 0u);

    (void)canonical_len;
    free(canonical);
    return ok;
}

static bool pp_action_bytecode_v1_expected_push(
    PPActionBytecodeV1ExpectedVec *vec,
    PPActionBytecodeV1Instruction instruction) {
    PPActionBytecodeV1Instruction *next;
    uint32_t cap;

    if (vec->len == UINT32_MAX)
        return false;
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 16u;
        if (cap < vec->cap ||
            (size_t)cap > SIZE_MAX / sizeof(*vec->data)) {
            return false;
        }
        next = realloc(vec->data, sizeof(*vec->data) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = instruction;
    return true;
}

static bool pp_action_bytecode_v1_frame_push(
    PPActionBytecodeV1FrameVec *vec,
    PPActionBytecodeV1Frame frame) {
    PPActionBytecodeV1Frame *next;
    uint32_t cap;

    if (vec->len == UINT32_MAX)
        return false;
    if (vec->len == vec->cap) {
        cap = vec->cap ? vec->cap * 2u : 16u;
        if (cap < vec->cap ||
            (size_t)cap > SIZE_MAX / sizeof(*vec->data)) {
            return false;
        }
        next = realloc(vec->data, sizeof(*vec->data) * (size_t)cap);
        if (!next)
            return false;
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len++] = frame;
    return true;
}

static bool pp_action_bytecode_v1_flatten_expected(
    Atom *action,
    uint32_t production_arity,
    PPActionBytecodeV1ExpectedVec *out,
    char *error_buf,
    size_t error_buf_size) {
    PPActionBytecodeV1FrameVec frames = {0};
    bool ok = false;

    memset(out, 0, sizeof(*out));
    if (!action || !pp_action_bytecode_v1_renderable(action) ||
        !pp_action_bytecode_v1_frame_push(
            &frames,
            (PPActionBytecodeV1Frame){
                .kind = PP_ACTION_BYTECODE_V1_FRAME_ACTION,
                .term = action,
            })) {
        pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "parser action is not a finite ground term");
        goto done;
    }
    while (frames.len > 0u) {
        PPActionBytecodeV1Frame frame = frames.data[--frames.len];
        Atom *term = frame.term;
        uint32_t operand;

        if (frame.kind == PP_ACTION_BYTECODE_V1_FRAME_APPLY) {
            if (!pp_action_bytecode_v1_expected_push(
                    out,
                    (PPActionBytecodeV1Instruction){
                        .kind = PP_ACTION_BYTECODE_V1_APPLY,
                        .operand = frame.arity,
                        .term = term,
                    })) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "parser action is too large to flatten");
                goto done;
            }
            continue;
        }
        if (pp_action_bytecode_v1_expr_head(term, "pa-slot", 1u)) {
            if (!pp_action_bytecode_v1_qindex(
                    term->expr.elems[1], &operand) ||
                operand >= production_arity ||
                !pp_action_bytecode_v1_expected_push(
                    out,
                    (PPActionBytecodeV1Instruction){
                        .kind = PP_ACTION_BYTECODE_V1_PUSH_SLOT,
                        .operand = operand,
                    })) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "parser action has an invalid slot");
                goto done;
            }
            continue;
        }
        if (pp_action_bytecode_v1_expr_head(term, "pa-const", 1u)) {
            Atom *value = term->expr.elems[1];
            if (!pp_action_bytecode_v1_renderable(value) ||
                !pp_action_bytecode_v1_expected_push(
                    out,
                    (PPActionBytecodeV1Instruction){
                        .kind = PP_ACTION_BYTECODE_V1_PUSH_CONST,
                        .term = value,
                    })) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "parser action has an invalid constant");
                goto done;
            }
            continue;
        }
        if (pp_action_bytecode_v1_expr_head(term, "pa-apply", 2u) &&
            term->expr.elems[1]->kind == ATOM_SYMBOL) {
            Atom *head = term->expr.elems[1];
            Atom *list = term->expr.elems[2];
            Atom **arguments = NULL;
            uint32_t argument_len = 0u;
            uint32_t argument_cap = 0u;
            bool list_ok = true;

            while (!atom_is_symbol(list, "pa-nil")) {
                Atom **next;
                uint32_t cap;
                if (!pp_action_bytecode_v1_expr_head(
                        list, "pa-cons", 2u) ||
                    argument_len == UINT32_MAX) {
                    list_ok = false;
                    break;
                }
                if (argument_len == argument_cap) {
                    cap = argument_cap ? argument_cap * 2u : 4u;
                    if (cap < argument_cap ||
                        (size_t)cap > SIZE_MAX / sizeof(*arguments)) {
                        list_ok = false;
                        break;
                    }
                    next = realloc(
                        arguments, sizeof(*arguments) * (size_t)cap);
                    if (!next) {
                        list_ok = false;
                        break;
                    }
                    arguments = next;
                    argument_cap = cap;
                }
                arguments[argument_len++] = list->expr.elems[1];
                list = list->expr.elems[2];
            }
            if (!list_ok ||
                !pp_action_bytecode_v1_frame_push(
                    &frames,
                    (PPActionBytecodeV1Frame){
                        .kind = PP_ACTION_BYTECODE_V1_FRAME_APPLY,
                        .term = head,
                        .arity = argument_len,
                    })) {
                free(arguments);
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "parser action application list is malformed or too large");
                goto done;
            }
            while (argument_len > 0u) {
                if (!pp_action_bytecode_v1_frame_push(
                        &frames,
                        (PPActionBytecodeV1Frame){
                            .kind = PP_ACTION_BYTECODE_V1_FRAME_ACTION,
                            .term = arguments[--argument_len],
                        })) {
                    free(arguments);
                    pp_action_bytecode_v1_error(
                        error_buf, error_buf_size,
                        "parser action traversal is too large");
                    goto done;
                }
            }
            free(arguments);
            continue;
        }
        pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "parser action lies outside the bytecode fragment");
        goto done;
    }
    if (out->len == 0u) {
        pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "parser action compiled to an empty instruction sequence");
        goto done;
    }
    ok = true;

done:
    free(frames.data);
    if (!ok) {
        free(out->data);
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

static bool pp_action_bytecode_v1_instruction_equal(
    const PPActionBytecodeV1Instruction *left,
    const PPActionBytecodeV1Instruction *right) {
    if (left->kind != right->kind || left->operand != right->operand)
        return false;
    if (left->kind == PP_ACTION_BYTECODE_V1_PUSH_SLOT)
        return left->term == NULL && right->term == NULL;
    return left->term && right->term && atom_eq(left->term, right->term);
}

static bool pp_action_bytecode_v1_sha_u32(CettaNativeSha256 *sha,
                                          uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
    return true;
}

static void pp_action_bytecode_v1_sha_u64(CettaNativeSha256 *sha,
                                          uint64_t value) {
    uint8_t bytes[8];
    uint32_t index;
    for (index = 0u; index < 8u; index++)
        bytes[index] = (uint8_t)(value >> ((7u - index) * 8u));
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static bool pp_action_bytecode_v1_program_digest(
    const PPActionBytecodeV1Program *program,
    char out[65]) {
    static const uint8_t domain[] = "ParserActionBytecodeProgramV1\n";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)program->base_pack_digest, 64u);
    cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)program->compiler_digest, 64u);
    cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)program->answer_set_digest, 64u);
    cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
    pp_action_bytecode_v1_sha_u32(&sha, program->production_len);
    pp_action_bytecode_v1_sha_u32(&sha, program->instruction_len);
    for (index = 0u; index < program->production_len; index++) {
        const PPActionBytecodeV1Production *production =
            &program->productions[index];
        pp_action_bytecode_v1_sha_u32(
            &sha, production->instruction_begin);
        pp_action_bytecode_v1_sha_u32(
            &sha, production->instruction_len);
        pp_action_bytecode_v1_sha_u32(&sha, production->arity);
        pp_action_bytecode_v1_sha_u32(
            &sha, production->max_stack_len);
    }
    for (index = 0u; index < program->instruction_len; index++) {
        const PPActionBytecodeV1Instruction *instruction =
            &program->instructions[index];
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        pp_action_bytecode_v1_sha_u32(&sha, (uint32_t)instruction->kind);
        pp_action_bytecode_v1_sha_u32(&sha, instruction->operand);
        if (instruction->kind != PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
            if (!fh_ground_term_v1_render(
                    instruction->term, &canonical, &canonical_len,
                    NULL, 0u)) {
                free(canonical);
                return false;
            }
        }
        pp_action_bytecode_v1_sha_u64(&sha, (uint64_t)canonical_len);
        if (canonical_len > 0u)
            cetta_native_sha256_update(&sha, canonical, canonical_len);
        free(canonical);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

void pp_action_bytecode_v1_program_init(PPActionBytecodeV1Program *program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
    arena_init(&program->arena);
}

void pp_action_bytecode_v1_program_free(PPActionBytecodeV1Program *program) {
    if (!program)
        return;
    free(program->productions);
    free(program->instructions);
    arena_free(&program->arena);
    memset(program, 0, sizeof(*program));
}

static bool pp_action_bytecode_v1_answer_set_digest(
    Atom *const *answers,
    size_t answer_len,
    char out[65],
    char *error_buf,
    size_t error_buf_size) {
    static const uint8_t domain[] = "FiniteHornAnswerSetV1\n";
    CettaNativeSha256 sha;
    uint8_t *previous = NULL;
    size_t previous_len = 0u;
    size_t index;
    bool ok = false;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    for (index = 0u; index < answer_len; index++) {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        int ordering;

        if (!fh_ground_term_v1_render(
                answers[index], &canonical, &canonical_len,
                error_buf, error_buf_size)) {
            free(canonical);
            goto done;
        }
        ordering = previous
            ? memcmp(previous, canonical,
                     previous_len < canonical_len
                         ? previous_len : canonical_len)
            : -1;
        if (previous &&
            (ordering > 0 ||
             (ordering == 0 && previous_len >= canonical_len))) {
            free(canonical);
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "action compiler answers are not strictly ordered");
            goto done;
        }
        cetta_native_sha256_update(&sha, canonical, canonical_len);
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
        free(previous);
        previous = canonical;
        previous_len = canonical_len;
    }
    cetta_native_sha256_finish_hex(&sha, out);
    ok = true;

done:
    free(previous);
    return ok;
}

static bool pp_action_bytecode_v1_production_has_owner(
    const PPABIV1Pack *pack,
    uint32_t production_id,
    Atom *owner) {
    const PPABIV1Production *production = &pack->productions[production_id];
    uint32_t evidence_index;

    if (production->evidence_begin > pack->derivation_len ||
        production->evidence_len >
            pack->derivation_len - production->evidence_begin) {
        return false;
    }
    for (evidence_index = 0u;
         evidence_index < production->evidence_len;
         evidence_index++) {
        const PPABIV1Derivation *derivation = &pack->derivations[
            production->evidence_begin + evidence_index];
        Atom *answer = derivation->answer;
        if (derivation->kind == PPABI_V1_EVIDENCE_PRODUCTION &&
            derivation->artifact_index == production_id &&
            pp_action_bytecode_v1_expr_head(
                answer, "compile-pack-production", 2u) &&
            atom_eq(answer->expr.elems[1], owner) &&
            atom_eq(answer->expr.elems[2], production->identity)) {
            return true;
        }
    }
    return false;
}

static uint32_t pp_action_bytecode_v1_source_len(
    const PPActionBytecodeV1Source *source) {
    return source->pack->production_len +
        (source->guard_plan ? source->guard_plan->production_len : 0u);
}

static bool pp_action_bytecode_v1_source_production(
    const PPActionBytecodeV1Source *source,
    uint32_t production_id,
    Atom **identity,
    Atom **action,
    uint32_t *arity) {
    if (production_id < source->pack->production_len) {
        const PPABIV1Production *production =
            &source->pack->productions[production_id];
        *identity = production->identity;
        *action = production->action;
        *arity = production->item_len;
        return true;
    }
    if (source->guard_plan) {
        uint32_t local_id = production_id - source->pack->production_len;
        if (local_id < source->guard_plan->production_len) {
            const PPNativeV1ProductionExtension *production =
                &source->guard_plan->productions[local_id];
            if (production->production_id != production_id)
                return false;
            *identity = production->identity;
            *action = production->action;
            *arity = production->item_len;
            return true;
        }
    }
    return false;
}

static bool pp_action_bytecode_v1_guard_source_valid(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan) {
    uint32_t index;

    if (!pack || !guard_plan || !guard_plan->entries ||
        !guard_plan->productions || guard_plan->entry_len == 0u ||
        guard_plan->entry_len != guard_plan->production_len ||
        guard_plan->production_len > UINT32_MAX - pack->production_len ||
        !pp_action_bytecode_v1_digest_valid(pack->pack_digest) ||
        !pp_action_bytecode_v1_digest_valid(guard_plan->base_pack_digest) ||
        !pp_action_bytecode_v1_digest_valid(guard_plan->plan_digest) ||
        strcmp(pack->pack_digest, guard_plan->base_pack_digest) != 0) {
        return false;
    }
    if (guard_plan->has_lexical_plan) {
        if (!lexical_plan ||
            !pp_action_bytecode_v1_digest_valid(
                guard_plan->lexical_plan_digest) ||
            strcmp(lexical_plan->plan_digest,
                   guard_plan->lexical_plan_digest) != 0) {
            return false;
        }
    } else if (lexical_plan || guard_plan->lexical_plan_digest[0] != '\0') {
        return false;
    }
    for (index = 0u; index < guard_plan->production_len; index++) {
        const PPGuardPlanV1Entry *entry = &guard_plan->entries[index];
        const PPNativeV1ProductionExtension *production =
            &guard_plan->productions[index];
        if (!entry->owner || !entry->production ||
            production->production_id != pack->production_len + index ||
            entry->production_id != production->production_id ||
            !atom_eq(entry->production, production->identity) ||
            !pp_action_bytecode_v1_expr_head(
                production->identity, "pp-production", 4u) ||
            !atom_eq(production->action,
                     production->identity->expr.elems[4])) {
            return false;
        }
    }
    return true;
}

static bool pp_action_bytecode_v1_find_production(
    const PPActionBytecodeV1Source *source,
    Atom *owner,
    Atom *label,
    uint32_t arity,
    Atom *action,
    uint32_t *out) {
    uint32_t match = UINT32_MAX;
    uint32_t index;

    for (index = 0u; index < source->pack->production_len; index++) {
        const PPABIV1Production *production =
            &source->pack->productions[index];
        Atom *identity = production->identity;
        if (production->item_len != arity ||
            !atom_eq(production->action, action) ||
            !pp_action_bytecode_v1_expr_head(
                identity, "pp-production", 4u) ||
            !atom_eq(identity->expr.elems[1], label) ||
            !pp_action_bytecode_v1_production_has_owner(
                source->pack, index, owner)) {
            continue;
        }
        if (match != UINT32_MAX)
            return false;
        match = index;
    }
    if (source->guard_plan) {
        for (index = 0u;
             index < source->guard_plan->production_len;
             index++) {
            const PPNativeV1ProductionExtension *production =
                &source->guard_plan->productions[index];
            const PPGuardPlanV1Entry *entry =
                &source->guard_plan->entries[index];
            Atom *identity = production->identity;
            if (production->production_id !=
                    source->pack->production_len + index ||
                entry->production_id != production->production_id ||
                production->item_len != arity ||
                !atom_eq(production->action, action) ||
                !atom_eq(entry->owner, owner) ||
                !atom_eq(entry->production, identity) ||
                !pp_action_bytecode_v1_expr_head(
                    identity, "pp-production", 4u) ||
                !atom_eq(identity->expr.elems[1], label)) {
                continue;
            }
            if (match != UINT32_MAX)
                return false;
            match = production->production_id;
        }
    }
    if (match == UINT32_MAX)
        return false;
    *out = match;
    return true;
}

static bool pp_action_bytecode_v1_answer_fields(
    Atom *answer,
    const char *relation,
    Atom **owner,
    Atom **label,
    uint32_t *arity,
    Atom **action,
    Atom **code) {
    return pp_action_bytecode_v1_expr_head(
               answer, relation, 5u) &&
        pp_action_bytecode_v1_qindex(answer->expr.elems[3], arity) &&
        ((*owner = answer->expr.elems[1]),
         (*label = answer->expr.elems[2]),
         (*action = answer->expr.elems[4]),
         (*code = answer->expr.elems[5]),
         true);
}

static bool pp_action_bytecode_v1_instruction_append(
    PPActionBytecodeV1Program *program,
    uint32_t *capacity,
    PPActionBytecodeV1Instruction instruction) {
    PPActionBytecodeV1Instruction *next;
    uint32_t cap;

    if (program->instruction_len == UINT32_MAX)
        return false;
    if (program->instruction_len == *capacity) {
        cap = *capacity ? *capacity * 2u : 256u;
        if (cap < *capacity ||
            (size_t)cap > SIZE_MAX / sizeof(*program->instructions)) {
            return false;
        }
        next = realloc(
            program->instructions,
            sizeof(*program->instructions) * (size_t)cap);
        if (!next)
            return false;
        program->instructions = next;
        *capacity = cap;
    }
    program->instructions[program->instruction_len++] = instruction;
    return true;
}

static bool pp_action_bytecode_v1_compile_code(
    PPActionBytecodeV1Program *program,
    uint32_t *instruction_capacity,
    Atom *action,
    Atom *code,
    uint32_t production_arity,
    PPActionBytecodeV1Production *production,
    char *error_buf,
    size_t error_buf_size) {
    PPActionBytecodeV1ExpectedVec expected = {0};
    Atom *list = code;
    uint32_t expected_index = 0u;
    uint32_t stack_len = 0u;
    bool ok = false;

    if (!pp_action_bytecode_v1_flatten_expected(
            action, production_arity, &expected,
            error_buf, error_buf_size) ||
        !pp_action_bytecode_v1_renderable(code)) {
        goto done;
    }
    production->instruction_begin = program->instruction_len;
    production->arity = production_arity;
    while (!atom_is_symbol(list, "pbc-nil")) {
        PPActionBytecodeV1Instruction instruction = {0};
        Atom *encoded;

        if (!pp_action_bytecode_v1_expr_head(list, "pbc-cons", 2u)) {
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "action bytecode list is malformed");
            goto done;
        }
        encoded = list->expr.elems[1];
        if (pp_action_bytecode_v1_expr_head(
                encoded, "pbc-push-slot", 1u)) {
            instruction.kind = PP_ACTION_BYTECODE_V1_PUSH_SLOT;
            if (!pp_action_bytecode_v1_qindex(
                    encoded->expr.elems[1], &instruction.operand) ||
                instruction.operand >= production_arity) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "action bytecode slot lies outside its production");
                goto done;
            }
        } else if (pp_action_bytecode_v1_expr_head(
                       encoded, "pbc-push-const", 1u)) {
            instruction.kind = PP_ACTION_BYTECODE_V1_PUSH_CONST;
            instruction.term = encoded->expr.elems[1];
            if (!pp_action_bytecode_v1_renderable(instruction.term)) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "action bytecode constant is not ground");
                goto done;
            }
        } else if (pp_action_bytecode_v1_expr_head(
                       encoded, "pbc-apply", 2u)) {
            instruction.kind = PP_ACTION_BYTECODE_V1_APPLY;
            instruction.term = encoded->expr.elems[1];
            if (instruction.term->kind != ATOM_SYMBOL ||
                !pp_action_bytecode_v1_qindex(
                    encoded->expr.elems[2], &instruction.operand)) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "action bytecode application is malformed");
                goto done;
            }
        } else {
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "unknown action bytecode instruction");
            goto done;
        }
        if (expected_index >= expected.len ||
            !pp_action_bytecode_v1_instruction_equal(
                &instruction, &expected.data[expected_index])) {
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "action bytecode does not certify its source action");
            goto done;
        }
        expected_index++;
        if (instruction.kind == PP_ACTION_BYTECODE_V1_APPLY) {
            if (stack_len < instruction.operand) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "action bytecode application underflows its stack");
                goto done;
            }
            stack_len = stack_len - instruction.operand + 1u;
        } else {
            if (stack_len == UINT32_MAX) {
                pp_action_bytecode_v1_error(
                    error_buf, error_buf_size,
                    "action bytecode stack height overflows");
                goto done;
            }
            stack_len++;
        }
        if (stack_len > production->max_stack_len)
            production->max_stack_len = stack_len;
        if (instruction.kind != PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
            instruction.term = atom_deep_copy(
                &program->arena, instruction.term);
            if (!instruction.term)
                goto done;
        }
        if (!pp_action_bytecode_v1_instruction_append(
                program, instruction_capacity, instruction)) {
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "failed to grow action bytecode program");
            goto done;
        }
        list = list->expr.elems[2];
    }
    production->instruction_len =
        program->instruction_len - production->instruction_begin;
    if (expected_index != expected.len ||
        production->instruction_len == 0u || stack_len != 1u ||
        production->max_stack_len == 0u) {
        pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "action bytecode is incomplete or does not leave one value");
        goto done;
    }
    ok = true;

done:
    free(expected.data);
    return ok;
}

static bool pp_action_bytecode_v1_program_validate_source(
    const PPActionBytecodeV1Program *program,
    const PPActionBytecodeV1Source *source,
    char *error_buf,
    size_t error_buf_size);

static bool pp_action_bytecode_v1_program_build_source(
    const PPActionBytecodeV1Source *source,
    const char *relation,
    Atom *const *compiler_answers,
    size_t compiler_answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    PPActionBytecodeV1Program *out,
    char *error_buf,
    size_t error_buf_size) {
    PPActionBytecodeV1Program result;
    Atom **answers_by_production = NULL;
    uint32_t instruction_capacity = 0u;
    char reproduced_answer_digest[65] = {0};
    size_t answer_index;
    uint32_t source_len;
    uint32_t production_id;
    bool ok = false;

    pp_action_bytecode_v1_program_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!source || !source->pack || !relation || !out ||
        !compiler_answers ||
        (source->guard_plan &&
         source->guard_plan->production_len >
             UINT32_MAX - source->pack->production_len)) {
        pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "bad action bytecode production source");
        goto done;
    }
    source_len = pp_action_bytecode_v1_source_len(source);
    if (compiler_answer_len != source_len ||
        compiler_answer_len > UINT32_MAX ||
        !pp_action_bytecode_v1_digest_valid(source->pack->pack_digest) ||
        !pp_action_bytecode_v1_digest_valid(compiler_digest) ||
        !pp_action_bytecode_v1_digest_valid(answer_set_digest) ||
        !pp_action_bytecode_v1_answer_set_digest(
            compiler_answers, compiler_answer_len,
            reproduced_answer_digest, error_buf, error_buf_size) ||
        strcmp(reproduced_answer_digest, answer_set_digest) != 0 ||
        (size_t)source_len >
            SIZE_MAX / sizeof(*answers_by_production)) {
        if (!error_buf || error_buf[0] == '\0') {
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "bad or stale action bytecode compiler inputs");
        }
        goto done;
    }
    answers_by_production = calloc(
        source_len ? source_len : 1u,
        sizeof(*answers_by_production));
    result.productions = calloc(
        source_len ? source_len : 1u,
        sizeof(*result.productions));
    if (!answers_by_production || !result.productions)
        goto done;
    result.production_len = source_len;
    for (answer_index = 0u;
         answer_index < compiler_answer_len;
         answer_index++) {
        Atom *owner = NULL;
        Atom *label = NULL;
        Atom *action = NULL;
        Atom *code = NULL;
        uint32_t arity;

        if (!pp_action_bytecode_v1_answer_fields(
                compiler_answers[answer_index],
                relation,
                &owner, &label, &arity, &action, &code) ||
            !pp_action_bytecode_v1_find_production(
                source, owner, label, arity, action, &production_id) ||
            answers_by_production[production_id] != NULL) {
            pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "action compiler answer does not uniquely bind a production");
            goto done;
        }
        answers_by_production[production_id] = compiler_answers[answer_index];
    }
    for (production_id = 0u;
         production_id < source_len;
         production_id++) {
        Atom *owner = NULL;
        Atom *label = NULL;
        Atom *action = NULL;
        Atom *code = NULL;
        uint32_t arity;

        if (!answers_by_production[production_id] ||
            !pp_action_bytecode_v1_answer_fields(
                answers_by_production[production_id],
                relation,
                &owner, &label, &arity, &action, &code) ||
            !pp_action_bytecode_v1_compile_code(
                &result, &instruction_capacity,
                action, code, arity, &result.productions[production_id],
                error_buf, error_buf_size)) {
            (void)owner;
            (void)label;
            goto done;
        }
    }
    memcpy(result.base_pack_digest, source->pack->pack_digest, 65u);
    memcpy(result.compiler_digest, compiler_digest, 65u);
    memcpy(result.answer_set_digest, answer_set_digest, 65u);
    if (!pp_action_bytecode_v1_program_digest(
            &result, result.program_digest) ||
        !pp_action_bytecode_v1_program_validate_source(
            &result, source, error_buf, error_buf_size)) {
        goto done;
    }
    pp_action_bytecode_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    free(answers_by_production);
    pp_action_bytecode_v1_program_free(&result);
    return ok;
}

static bool pp_action_bytecode_v1_program_validate_source(
    const PPActionBytecodeV1Program *program,
    const PPActionBytecodeV1Source *source,
    char *error_buf,
    size_t error_buf_size) {
    char digest[65] = {0};
    uint32_t expected_begin = 0u;
    uint32_t source_len;
    uint32_t production_id;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !source || !source->pack ||
        (source->guard_plan &&
         source->guard_plan->production_len >
             UINT32_MAX - source->pack->production_len)) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "bad action bytecode production source");
    }
    source_len = pp_action_bytecode_v1_source_len(source);
    if (program->production_len != source_len ||
        (program->production_len > 0u && !program->productions) ||
        (program->instruction_len > 0u && !program->instructions) ||
        !pp_action_bytecode_v1_digest_valid(program->base_pack_digest) ||
        !pp_action_bytecode_v1_digest_valid(program->compiler_digest) ||
        !pp_action_bytecode_v1_digest_valid(program->answer_set_digest) ||
        !pp_action_bytecode_v1_digest_valid(program->program_digest) ||
        !pp_action_bytecode_v1_digest_valid(source->pack->pack_digest) ||
        strcmp(program->base_pack_digest,
               source->pack->pack_digest) != 0) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "bad action bytecode program or ParserPack binding");
    }
    for (production_id = 0u;
         production_id < program->production_len;
         production_id++) {
        const PPActionBytecodeV1Production *production =
            &program->productions[production_id];
        Atom *source_identity = NULL;
        Atom *source_action = NULL;
        uint32_t source_arity = 0u;
        PPActionBytecodeV1ExpectedVec expected = {0};
        uint32_t stack_len = 0u;
        uint32_t max_stack_len = 0u;
        uint32_t local_index;
        bool valid = true;

        if (production->instruction_begin != expected_begin ||
            production->instruction_len == 0u ||
            production->instruction_begin > program->instruction_len ||
            production->instruction_len >
                program->instruction_len - production->instruction_begin ||
            !pp_action_bytecode_v1_source_production(
                source, production_id,
                &source_identity, &source_action, &source_arity) ||
            !source_identity || production->arity != source_arity ||
            !pp_action_bytecode_v1_flatten_expected(
                source_action,
                production->arity, &expected,
                error_buf, error_buf_size) ||
            expected.len != production->instruction_len) {
            free(expected.data);
            return pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "action bytecode production slice is malformed");
        }
        for (local_index = 0u;
             local_index < production->instruction_len;
             local_index++) {
            const PPActionBytecodeV1Instruction *instruction =
                &program->instructions[
                    production->instruction_begin + local_index];
            if (!pp_action_bytecode_v1_instruction_equal(
                    instruction, &expected.data[local_index])) {
                valid = false;
                break;
            }
            if (instruction->kind == PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
                if (instruction->operand >= production->arity ||
                    stack_len == UINT32_MAX) {
                    valid = false;
                    break;
                }
                stack_len++;
            } else if (instruction->kind ==
                           PP_ACTION_BYTECODE_V1_PUSH_CONST) {
                if (instruction->operand != 0u || !instruction->term ||
                    !pp_action_bytecode_v1_renderable(instruction->term) ||
                    stack_len == UINT32_MAX) {
                    valid = false;
                    break;
                }
                stack_len++;
            } else if (instruction->kind == PP_ACTION_BYTECODE_V1_APPLY) {
                if (!instruction->term ||
                    instruction->term->kind != ATOM_SYMBOL ||
                    stack_len < instruction->operand) {
                    valid = false;
                    break;
                }
                stack_len = stack_len - instruction->operand + 1u;
            } else {
                valid = false;
                break;
            }
            if (stack_len > max_stack_len)
                max_stack_len = stack_len;
        }
        free(expected.data);
        if (!valid || stack_len != 1u ||
            max_stack_len != production->max_stack_len) {
            return pp_action_bytecode_v1_error(
                error_buf, error_buf_size,
                "action bytecode fails stack or source-action validation");
        }
        expected_begin += production->instruction_len;
    }
    if (expected_begin != program->instruction_len ||
        !pp_action_bytecode_v1_program_digest(program, digest) ||
        strcmp(digest, program->program_digest) != 0) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "action bytecode has trailing storage or a stale digest");
    }
    return true;
}

bool pp_action_bytecode_v1_program_build(
    const PPABIV1Pack *pack,
    Atom *const *compiler_answers,
    size_t compiler_answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    PPActionBytecodeV1Program *out,
    char *error_buf,
    size_t error_buf_size) {
    const PPActionBytecodeV1Source source = {
        .pack = pack,
    };
    return pp_action_bytecode_v1_program_build_source(
        &source, "compile-pack-action-program",
        compiler_answers, compiler_answer_len,
        compiler_digest, answer_set_digest,
        out, error_buf, error_buf_size);
}

bool pp_action_bytecode_v1_program_validate(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    char *error_buf,
    size_t error_buf_size) {
    const PPActionBytecodeV1Source source = {
        .pack = pack,
    };
    return pp_action_bytecode_v1_program_validate_source(
        program, &source, error_buf, error_buf_size);
}

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
    size_t error_buf_size) {
    const PPActionBytecodeV1Source source = {
        .pack = pack,
        .guard_plan = guard_plan,
    };
    if (!pp_action_bytecode_v1_guard_source_valid(
            pack, lexical_plan, guard_plan)) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "bad positive-guard action production source");
    }
    return pp_action_bytecode_v1_program_build_source(
        &source, "compile-guard-extended-action-program",
        compiler_answers, compiler_answer_len,
        compiler_digest, answer_set_digest,
        out, error_buf, error_buf_size);
}

bool pp_action_bytecode_v1_program_validate_guard_extended(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    char *error_buf,
    size_t error_buf_size) {
    const PPActionBytecodeV1Source source = {
        .pack = pack,
        .guard_plan = guard_plan,
    };
    if (!pp_action_bytecode_v1_guard_source_valid(
            pack, lexical_plan, guard_plan)) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "bad positive-guard action production source");
    }
    return pp_action_bytecode_v1_program_validate_source(
        program, &source, error_buf, error_buf_size);
}

bool pp_action_bytecode_v1_execute_prevalidated(
    const PPActionBytecodeV1Program *program,
    uint32_t production_id,
    Atom *const *slots,
    uint32_t slot_len,
    Arena *result_arena,
    Atom **out,
    char *error_buf,
    size_t error_buf_size) {
    const PPActionBytecodeV1Production *production;
    Atom *local_stack[16];
    Atom **stack = NULL;
    bool stack_owned = false;
    uint32_t stack_len = 0u;
    uint32_t local_index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out)
        *out = NULL;
    if (!program || !result_arena || !out ||
        production_id >= program->production_len ||
        (slot_len > 0u && !slots)) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "bad action bytecode execution arguments");
    }
    production = &program->productions[production_id];
    if (slot_len != production->arity ||
        production->max_stack_len == 0u ||
        (size_t)production->max_stack_len >
            SIZE_MAX / sizeof(*stack)) {
        return pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "action bytecode execution arity changed");
    }
    if (production->max_stack_len <=
            sizeof(local_stack) / sizeof(local_stack[0])) {
        stack = local_stack;
    } else {
        stack = malloc(
            sizeof(*stack) * (size_t)production->max_stack_len);
        if (!stack)
            goto done;
        stack_owned = true;
    }
    for (local_index = 0u;
         local_index < production->instruction_len;
         local_index++) {
        const PPActionBytecodeV1Instruction *instruction =
            &program->instructions[
                production->instruction_begin + local_index];
        if (instruction->kind == PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
            if (instruction->operand >= slot_len ||
                stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = slots[instruction->operand];
        } else if (instruction->kind ==
                       PP_ACTION_BYTECODE_V1_PUSH_CONST) {
            if (!instruction->term ||
                stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = atom_deep_copy(
                result_arena, instruction->term);
        } else if (instruction->kind == PP_ACTION_BYTECODE_V1_APPLY) {
            Atom **items;
            uint32_t argument_begin;
            uint32_t argument_index;
            if (!instruction->term ||
                instruction->term->kind != ATOM_SYMBOL ||
                stack_len < instruction->operand)
                goto malformed;
            argument_begin = stack_len - instruction->operand;
            items = arena_alloc(
                result_arena,
                sizeof(*items) * ((size_t)instruction->operand + 1u));
            items[0] = atom_deep_copy(result_arena, instruction->term);
            for (argument_index = 0u;
                 argument_index < instruction->operand;
                 argument_index++) {
                items[argument_index + 1u] =
                    stack[argument_begin + argument_index];
            }
            stack_len = argument_begin;
            stack[stack_len++] = atom_expr(
                result_arena, items,
                (CettaExprLen)instruction->operand + 1u);
        } else {
            goto malformed;
        }
    }
    if (stack_len != 1u)
        goto malformed;
    *out = stack[0];
    ok = true;
    goto done;

malformed:
    pp_action_bytecode_v1_error(
        error_buf, error_buf_size,
        "prevalidated action bytecode became malformed");

done:
    if (stack_owned)
        free(stack);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        pp_action_bytecode_v1_error(
            error_buf, error_buf_size,
            "failed to execute action bytecode");
    }
    return ok;
}

bool pp_action_bytecode_v1_execute(
    const PPActionBytecodeV1Program *program,
    const PPABIV1Pack *pack,
    uint32_t production_id,
    Atom *const *slots,
    uint32_t slot_len,
    Arena *result_arena,
    Atom **out,
    char *error_buf,
    size_t error_buf_size) {
    return pp_action_bytecode_v1_program_validate(
               program, pack, error_buf, error_buf_size) &&
        pp_action_bytecode_v1_execute_prevalidated(
            program, production_id, slots, slot_len,
            result_arena, out, error_buf, error_buf_size);
}

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
    size_t error_buf_size) {
    return pp_action_bytecode_v1_program_validate_guard_extended(
               program, pack, lexical_plan, guard_plan,
               error_buf, error_buf_size) &&
        pp_action_bytecode_v1_execute_prevalidated(
            program, production_id, slots, slot_len,
            result_arena, out, error_buf, error_buf_size);
}
