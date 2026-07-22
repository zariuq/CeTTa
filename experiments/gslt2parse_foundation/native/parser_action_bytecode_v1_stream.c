#include "finite_horn_answer_stream_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"
#include "parser_action_bytecode_v1.h"
#include "parser_atom_projection_action_v1.h"
#include "parser_atom_projection_events_v1.h"
#include "parser_atom_projection_v1.h"
#include "parser_pack_abi_stream_v1.h"
#include "parser_pack_native_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool expr_head(const Atom *atom,
                      const char *head,
                      CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static Atom *slot_value(Arena *arena, uint32_t scalar) {
    Atom **items = arena_alloc(arena, sizeof(*items) * 2u);
    items[0] = atom_symbol(arena, "cp");
    items[1] = atom_int(arena, (int64_t)scalar);
    return atom_expr(arena, items, 2u);
}

static Atom *find_instruction(FHAnswerStreamV1 *answers,
                              const char *instruction_head) {
    size_t answer_index;

    for (answer_index = 0u;
         answer_index < answers->len;
         answer_index++) {
        Atom *answer = answers->terms[answer_index];
        Atom *list;
        if (!expr_head(answer, "compile-pack-action-program", 5u))
            return NULL;
        list = answer->expr.elems[5];
        while (!atom_is_symbol(list, "pbc-nil")) {
            Atom *instruction;
            if (!expr_head(list, "pbc-cons", 2u))
                return NULL;
            instruction = list->expr.elems[1];
            if (instruction->kind == ATOM_EXPR &&
                instruction->expr.len > 0u &&
                atom_is_symbol(
                    instruction->expr.elems[0], instruction_head)) {
                return instruction;
            }
            list = list->expr.elems[2];
        }
    }
    return NULL;
}

static bool answer_set_digest(Atom *const *terms,
                              size_t term_len,
                              char out[65]) {
    static const uint8_t domain[] = "FiniteHornAnswerSetV1\n";
    CettaNativeSha256 sha;
    size_t index;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, domain, sizeof(domain) - 1u);
    for (index = 0u; index < term_len; index++) {
        uint8_t *canonical = NULL;
        size_t canonical_len = 0u;
        if (!fh_ground_term_v1_render(
                terms[index], &canonical, &canonical_len, NULL, 0u)) {
            free(canonical);
            return false;
        }
        cetta_native_sha256_update(&sha, canonical, canonical_len);
        cetta_native_sha256_update(&sha, (const uint8_t *)"\n", 1u);
        free(canonical);
    }
    cetta_native_sha256_finish_hex(&sha, out);
    return true;
}

static bool build_rejected(const PPABIV1Pack *pack,
                           Atom *const *terms,
                           size_t term_len,
                           const char *compiler_digest,
                           const char *answer_digest) {
    PPActionBytecodeV1Program program;
    char error[512] = {0};
    bool rejected;

    pp_action_bytecode_v1_program_init(&program);
    rejected = !pp_action_bytecode_v1_program_build(
        pack, terms, term_len, compiler_digest, answer_digest,
        &program, error, sizeof(error));
    pp_action_bytecode_v1_program_free(&program);
    return rejected;
}

static int32_t find_constant_production(
    const PPAtomProjectionActionV1Program *program,
    PPAtomProjectionActionV1ConstantKind kind,
    uint32_t dense_id) {
    uint32_t production_id;

    for (production_id = 0u;
         production_id < program->production_len;
         production_id++) {
        const PPAtomProjectionActionV1Production *production =
            &program->productions[production_id];
        const PPAtomProjectionActionV1Instruction *instruction;
        if (production->arity != 0u || production->instruction_len != 1u)
            continue;
        instruction = &program->instructions[production->instruction_begin];
        if (instruction->kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT &&
            instruction->constant_kind == kind &&
            instruction->dense_id == dense_id) {
            return (int32_t)production_id;
        }
    }
    return -1;
}

static int32_t find_binary_production(
    const PPAtomProjectionActionV1Program *program,
    PPAtomProjectionActionV1InstructionKind kind) {
    uint32_t production_id;

    for (production_id = 0u;
         production_id < program->production_len;
         production_id++) {
        const PPAtomProjectionActionV1Production *production =
            &program->productions[production_id];
        const PPAtomProjectionActionV1Instruction *instructions;
        if (production->arity != 2u || production->instruction_len != 3u)
            continue;
        instructions = &program->instructions[production->instruction_begin];
        if (instructions[0].kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT &&
            instructions[0].operand == 0u &&
            instructions[1].kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT &&
            instructions[1].operand == 1u &&
            instructions[2].kind == kind) {
            return (int32_t)production_id;
        }
    }
    return -1;
}

static int32_t find_node_production(
    const PPAtomProjectionActionV1Program *program,
    PPAtomProjectionActionV1ConstantKind kind,
    uint32_t dense_id) {
    uint32_t production_id;

    for (production_id = 0u;
         production_id < program->production_len;
         production_id++) {
        const PPAtomProjectionActionV1Production *production =
            &program->productions[production_id];
        const PPAtomProjectionActionV1Instruction *instructions;
        if (production->arity != 1u || production->instruction_len != 3u)
            continue;
        instructions = &program->instructions[production->instruction_begin];
        if (instructions[0].kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT &&
            instructions[0].constant_kind == kind &&
            instructions[0].dense_id == dense_id &&
            instructions[1].kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT &&
            instructions[1].operand == 0u &&
            instructions[2].kind ==
                PPATOM_PROJECTION_ACTION_V1_APPLY_NODE) {
            return (int32_t)production_id;
        }
    }
    return -1;
}

static Atom *wrap_semantic_result(Arena *arena, Atom *document) {
    Atom **items = arena_alloc(arena, sizeof(*items) * 3u);
    if (!items)
        return NULL;
    items[0] = atom_symbol(arena, "result");
    items[1] = document;
    items[2] = atom_symbol(arena, "nil");
    if (!items[0] || !items[2])
        return NULL;
    return atom_expr(arena, items, 3u);
}

static bool projection_document_gate(
    const PPAtomProjectionActionV1Program *program,
    const PPAtomProjectionV1Plan *projection,
    PPAtomProjectionActionV1Store *store,
    Arena *semantic_arena,
    Arena *compact_output_arena,
    Arena *reference_output_arena,
    char *error,
    size_t error_size) {
    PPAtomProjectionActionV1Value scalar;
    PPAtomProjectionActionV1Value nil;
    PPAtomProjectionActionV1Value text;
    PPAtomProjectionActionV1Value leaf;
    PPAtomProjectionActionV1Value document_list;
    PPAtomProjectionActionV1Value document;
    PPAtomProjectionActionV1Value retained_leaf;
    PPAtomProjectionActionV1Value retained_document_list;
    PPAtomProjectionActionV1Value retained_document;
    PPAtomProjectionActionV1TextBinding retained_binding;
    PPAtomProjectionActionV1Value slots[2];
    const uint32_t retained_scalars[] = {97u};
    Atom *neutral_document = NULL;
    Atom *semantic_result = NULL;
    Atom **compact_atoms = NULL;
    Atom **reference_atoms = NULL;
    Atom **retained_atoms = NULL;
    uint32_t compact_len = 0u;
    uint32_t reference_len = 0u;
    uint32_t retained_len = 0u;
    uint32_t rule_len;
    uint32_t rule_id;
    uint32_t atom_index;
    int32_t nil_production;
    int32_t cons_production;
    int32_t leaf_production;
    int32_t document_production;

    if (!ppatom_projection_v1_plan_rule_count(
            projection, &rule_len, error, error_size)) {
        return false;
    }
    for (rule_id = 0u; rule_id < rule_len; rule_id++) {
        PPAtomProjectionV1RuleView rule;
        if (!ppatom_projection_v1_plan_rule(
                projection, rule_id, &rule, error, error_size)) {
            return false;
        }
        if (rule.kind == PPATOM_PROJECTION_V1_RULE_TEXT)
            break;
    }
    if (rule_id == rule_len)
        return false;
    nil_production = find_constant_production(
        program, PPATOM_PROJECTION_ACTION_V1_CONSTANT_NIL, 0u);
    cons_production = find_binary_production(
        program, PPATOM_PROJECTION_ACTION_V1_APPLY_CONS);
    leaf_production = find_node_production(
        program, PPATOM_PROJECTION_ACTION_V1_CONSTANT_RULE, rule_id);
    document_production = find_node_production(
        program, PPATOM_PROJECTION_ACTION_V1_CONSTANT_DOCUMENT, 0u);
    if (nil_production < 0 || cons_production < 0 ||
        leaf_production < 0 || document_production < 0 ||
        !ppatom_projection_action_v1_scalar(
            97u, &scalar, error, error_size) ||
        !ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)nil_production, NULL, 0u,
            store, &nil, error, error_size)) {
        return false;
    }
    slots[0] = scalar;
    slots[1] = nil;
    if (!ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)cons_production, slots, 2u,
            store, &text, error, error_size)) {
        return false;
    }
    slots[0] = text;
    if (!ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)leaf_production, slots, 1u,
            store, &leaf, error, error_size)) {
        return false;
    }
    slots[0] = leaf;
    slots[1] = nil;
    if (!ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)cons_production, slots, 2u,
            store, &document_list, error, error_size)) {
        return false;
    }
    slots[0] = document_list;
    if (!ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)document_production, slots, 1u,
            store, &document, error, error_size) ||
        !ppatom_projection_action_v1_project_document(
            program, store, document, projection,
            compact_output_arena, 64u,
            &compact_atoms, &compact_len, error, error_size) ||
        !ppatom_projection_action_v1_value_to_atom(
            program, store, document, semantic_arena, 64u,
            &neutral_document, error, error_size) ||
        !(semantic_result = wrap_semantic_result(
              semantic_arena, neutral_document)) ||
        !ppatom_projection_events_v1_project_document(
            projection, semantic_result, reference_output_arena, 64u,
            &reference_atoms, &reference_len, error, error_size) ||
        compact_len != reference_len) {
        return false;
    }
    for (atom_index = 0u; atom_index < compact_len; atom_index++) {
        if (!atom_eq(
                compact_atoms[atom_index], reference_atoms[atom_index]))
            return false;
    }
    if (!ppatom_projection_action_v1_text_binding(
            program, rule_id, &retained_binding, error, error_size) ||
        !ppatom_projection_action_v1_text_node_prevalidated(
            program, &retained_binding,
            retained_scalars,
            (uint32_t)(sizeof(retained_scalars) /
                       sizeof(retained_scalars[0])),
            store, &retained_leaf, error, error_size)) {
        return false;
    }
    slots[0] = retained_leaf;
    slots[1] = nil;
    if (!ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)cons_production, slots, 2u,
            store, &retained_document_list, error, error_size)) {
        return false;
    }
    slots[0] = retained_document_list;
    if (!ppatom_projection_action_v1_execute_prevalidated(
            program, (uint32_t)document_production, slots, 1u,
            store, &retained_document, error, error_size) ||
        !ppatom_projection_action_v1_project_document(
            program, store, retained_document, projection,
            compact_output_arena, 64u,
            &retained_atoms, &retained_len, error, error_size) ||
        retained_len != reference_len) {
        return false;
    }
    for (atom_index = 0u; atom_index < retained_len; atom_index++) {
        if (!atom_eq(
                retained_atoms[atom_index], reference_atoms[atom_index]))
            return false;
    }
    {
        PPAtomProjectionActionV1TextBinding corrupted = retained_binding;
        PPAtomProjectionActionV1Value rejected_value;
        corrupted.node_instruction = corrupted.rule_constant_instruction;
        if (ppatom_projection_action_v1_text_node_prevalidated(
                program, &corrupted,
                retained_scalars,
                (uint32_t)(sizeof(retained_scalars) /
                           sizeof(retained_scalars[0])),
                store, &rejected_value, error, error_size)) {
            return false;
        }
        if (error && error_size > 0u)
            error[0] = '\0';
    }
    if (ppatom_projection_action_v1_project_document(
            program, store, leaf, projection,
            compact_output_arena, 64u,
            &compact_atoms, &compact_len, error, error_size)) {
        return false;
    }
    if (error && error_size > 0u)
        error[0] = '\0';
    return true;
}

static bool run(const char *abi_path,
                const char *answer_path,
                const char *compiler_digest,
                const char *projection_path) {
    PPABIV1Wire wire;
    PPABIV1Pack pack;
    FHAnswerStreamV1 answers;
    PPActionBytecodeV1Program program;
    FHAnswerStreamV1 projection_answers;
    PPAtomProjectionV1Plan projection;
    PPAtomProjectionActionV1Program projection_actions;
    PPAtomProjectionActionV1Store projection_store;
    PPAtomProjectionActionV1Store projection_specialized_store;
    Arena slot_arena;
    Arena tree_arena;
    Arena bytecode_arena;
    Arena projection_semantic_arena;
    Arena projection_output_arena;
    Arena projection_reference_arena;
    uint32_t tree_bytecode_agreements = 0u;
    uint32_t projection_action_agreements = 0u;
    uint32_t projection_specialized_agreements = 0u;
    uint32_t projection_action_mutations = 0u;
    uint32_t projection_document_agreements = 0u;
    uint32_t projection_pair_len = 0u;
    uint32_t projection_cons_len = 0u;
    uint32_t projection_node_len = 0u;
    uint32_t projection_interpreted_production_len = 0u;
    uint32_t projection_value_production_len = 0u;
    uint32_t projection_binary_production_len = 0u;
    uint32_t mutation_count = 0u;
    uint32_t push_slot_len = 0u;
    uint32_t push_const_len = 0u;
    uint32_t apply_len = 0u;
    uint32_t max_stack_len = 0u;
    char error[512] = {0};
    uint32_t production_id;
    bool ok = false;

    ppabi_v1_wire_init(&wire);
    ppabi_v1_pack_init(&pack);
    fh_answer_stream_v1_init(&answers);
    pp_action_bytecode_v1_program_init(&program);
    fh_answer_stream_v1_init(&projection_answers);
    ppatom_projection_v1_plan_init(&projection);
    ppatom_projection_action_v1_program_init(&projection_actions);
    ppatom_projection_action_v1_store_init(&projection_store);
    ppatom_projection_action_v1_store_init(&projection_specialized_store);
    arena_init(&slot_arena);
    arena_init(&tree_arena);
    arena_init(&bytecode_arena);
    arena_init(&projection_semantic_arena);
    arena_init(&projection_output_arena);
    arena_init(&projection_reference_arena);
    if (!ppabi_v1_wire_read(&wire, abi_path, error, sizeof(error)) ||
        !ppabi_v1_wire_load_pack(&wire, &pack, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &answers, answer_path, error, sizeof(error)) ||
        !pp_action_bytecode_v1_program_build(
            &pack, answers.terms, answers.len,
            compiler_digest, answers.digest,
            &program, error, sizeof(error)) ||
        !fh_answer_stream_v1_read(
            &projection_answers, projection_path,
            error, sizeof(error)) ||
        projection_answers.len != 1u ||
        !ppatom_projection_v1_plan_load(
            &projection, projection_answers.terms[0],
            error, sizeof(error)) ||
        !ppatom_projection_action_v1_program_build_prevalidated(
            &projection_actions, &program, &projection,
            error, sizeof(error)) ||
        !ppatom_projection_action_v1_program_validate(
            &projection_actions, &program, &projection,
            error, sizeof(error))) {
        goto rejected;
    }
    for (production_id = 0u;
         production_id < program.production_len;
         production_id++) {
        const PPActionBytecodeV1Production *production =
            &program.productions[production_id];
        Atom **slots = NULL;
        Atom *tree_value = NULL;
        Atom *bytecode_value = NULL;
        Atom *projection_value = NULL;
        Atom *projection_specialized_value = NULL;
        PPAtomProjectionActionV1Value *projection_slots = NULL;
        PPAtomProjectionActionV1Value projected = {0};
        PPAtomProjectionActionV1Value specialized = {0};
        uint32_t slot_id;

        if (production->arity > 0u) {
            slots = malloc(
                sizeof(*slots) * (size_t)production->arity);
            projection_slots = malloc(
                sizeof(*projection_slots) * (size_t)production->arity);
            if (!slots || !projection_slots) {
                free(projection_slots);
                free(slots);
                goto rejected;
            }
        }
        for (slot_id = 0u; slot_id < production->arity; slot_id++) {
            uint32_t scalar = UINT32_C(0x100) +
                ((production_id + slot_id) % UINT32_C(0x10000));
            slots[slot_id] = slot_value(&slot_arena, scalar);
            if (!slots[slot_id] ||
                !ppatom_projection_action_v1_scalar(
                    scalar, &projection_slots[slot_id],
                    error, sizeof(error))) {
                free(projection_slots);
                free(slots);
                goto rejected;
            }
        }
        if (!ppnative_v1_apply_action_term(
                &tree_arena, pack.productions[production_id].action,
                slots, production->arity, &tree_value,
                error, sizeof(error)) ||
            !pp_action_bytecode_v1_execute_prevalidated(
                &program, production_id, slots, production->arity,
                &bytecode_arena, &bytecode_value,
                error, sizeof(error)) ||
            !ppatom_projection_action_v1_execute_prevalidated(
                &projection_actions, production_id,
                projection_slots, production->arity,
                &projection_store, &projected,
                error, sizeof(error)) ||
            !ppatom_projection_action_v1_execute_specialized_view_prevalidated(
                &projection_actions, production_id,
                projection_slots, production->arity,
                sizeof(*projection_slots), &projection_specialized_store,
                &specialized, error, sizeof(error)) ||
            !ppatom_projection_action_v1_value_to_atom(
                &projection_actions, &projection_store, projected,
                &bytecode_arena, 64u, &projection_value,
                error, sizeof(error)) ||
            !ppatom_projection_action_v1_value_to_atom(
                &projection_actions, &projection_specialized_store,
                specialized, &bytecode_arena, 64u,
                &projection_specialized_value,
                error, sizeof(error)) ||
            !atom_eq(tree_value, bytecode_value) ||
            !atom_eq(tree_value, projection_value) ||
            !atom_eq(tree_value, projection_specialized_value)) {
            free(projection_slots);
            free(slots);
            (void)snprintf(
                error, sizeof(error),
                "action interpreter disagreement at production %u",
                production_id);
            goto rejected;
        }
        ppatom_projection_action_v1_store_reset(&projection_store);
        ppatom_projection_action_v1_store_reset(
            &projection_specialized_store);
        free(projection_slots);
        free(slots);
        tree_bytecode_agreements++;
        projection_action_agreements++;
        projection_specialized_agreements++;
        if (production->max_stack_len > max_stack_len)
            max_stack_len = production->max_stack_len;
    }
    {
        uint32_t production_index;
        for (production_index = 0u;
             production_index < projection_actions.production_len;
             production_index++) {
            switch (projection_actions.productions[
                        production_index].execution_kind) {
            case PPATOM_PROJECTION_ACTION_V1_EXECUTE_INTERPRETED:
                projection_interpreted_production_len++;
                break;
            case PPATOM_PROJECTION_ACTION_V1_EXECUTE_VALUE:
                projection_value_production_len++;
                break;
            case PPATOM_PROJECTION_ACTION_V1_EXECUTE_BINARY:
                projection_binary_production_len++;
                break;
            }
        }
    }
    {
        uint32_t instruction_index;
        for (instruction_index = 0u;
             instruction_index < program.instruction_len;
             instruction_index++) {
            switch (program.instructions[instruction_index].kind) {
            case PP_ACTION_BYTECODE_V1_PUSH_SLOT:
                push_slot_len++;
                break;
            case PP_ACTION_BYTECODE_V1_PUSH_CONST:
                push_const_len++;
                break;
            case PP_ACTION_BYTECODE_V1_APPLY:
                apply_len++;
                break;
            }
        }
    }
    {
        uint32_t instruction_index;
        for (instruction_index = 0u;
             instruction_index < projection_actions.instruction_len;
             instruction_index++) {
            switch (projection_actions.instructions[instruction_index].kind) {
            case PPATOM_PROJECTION_ACTION_V1_APPLY_PAIR:
                projection_pair_len++;
                break;
            case PPATOM_PROJECTION_ACTION_V1_APPLY_CONS:
                projection_cons_len++;
                break;
            case PPATOM_PROJECTION_ACTION_V1_APPLY_NODE:
                projection_node_len++;
                break;
            default:
                break;
            }
        }
    }

    {
        Atom *slot = find_instruction(&answers, "pbc-push-slot");
        Atom *saved;
        char digest[65] = {0};
        if (!slot || !expr_head(slot, "pbc-push-slot", 1u))
            goto rejected;
        saved = slot->expr.elems[1];
        slot->expr.elems[1] = atom_symbol(&answers.arena, "q-invalid");
        if (!answer_set_digest(answers.terms, answers.len, digest) ||
            !build_rejected(
                &pack, answers.terms, answers.len,
                compiler_digest, digest)) {
            slot->expr.elems[1] = saved;
            (void)snprintf(error, sizeof(error),
                           "invalid-slot mutation survived");
            goto rejected;
        }
        slot->expr.elems[1] = saved;
        mutation_count++;
    }
    {
        Atom *apply = find_instruction(&answers, "pbc-apply");
        Atom *saved;
        char digest[65] = {0};
        if (!apply || !expr_head(apply, "pbc-apply", 2u))
            goto rejected;
        saved = apply->expr.elems[1];
        apply->expr.elems[1] = atom_int(&answers.arena, 7);
        if (!answer_set_digest(answers.terms, answers.len, digest) ||
            !build_rejected(
                &pack, answers.terms, answers.len,
                compiler_digest, digest)) {
            apply->expr.elems[1] = saved;
            (void)snprintf(error, sizeof(error),
                           "non-symbol-head mutation survived");
            goto rejected;
        }
        apply->expr.elems[1] = saved;
        mutation_count++;
    }
    {
        Atom *apply = find_instruction(&answers, "pbc-apply");
        Atom *saved;
        char digest[65] = {0};
        if (!apply || !expr_head(apply, "pbc-apply", 2u))
            goto rejected;
        saved = apply->expr.elems[2];
        apply->expr.elems[2] = atom_symbol(&answers.arena, "q-invalid");
        if (!answer_set_digest(answers.terms, answers.len, digest) ||
            !build_rejected(
                &pack, answers.terms, answers.len,
                compiler_digest, digest)) {
            apply->expr.elems[2] = saved;
            (void)snprintf(error, sizeof(error),
                           "invalid-apply-arity mutation survived");
            goto rejected;
        }
        apply->expr.elems[2] = saved;
        mutation_count++;
    }
    {
        Atom *first = answers.terms[0];
        size_t other_index = 1u;
        Atom *saved;
        char digest[65] = {0};
        if (!expr_head(first, "compile-pack-action-program", 5u))
            goto rejected;
        while (other_index < answers.len &&
               atom_eq(first->expr.elems[5],
                       answers.terms[other_index]->expr.elems[5])) {
            other_index++;
        }
        if (other_index >= answers.len ||
            !expr_head(answers.terms[other_index],
                       "compile-pack-action-program", 5u)) {
            goto rejected;
        }
        saved = first->expr.elems[5];
        first->expr.elems[5] = answers.terms[other_index]->expr.elems[5];
        if (!answer_set_digest(answers.terms, answers.len, digest) ||
            !build_rejected(
                &pack, answers.terms, answers.len,
                compiler_digest, digest)) {
            first->expr.elems[5] = saved;
            (void)snprintf(error, sizeof(error),
                           "swapped-code mutation survived");
            goto rejected;
        }
        first->expr.elems[5] = saved;
        mutation_count++;
    }
    if (answers.len == 0u ||
        !build_rejected(
            &pack, answers.terms, answers.len - 1u,
            compiler_digest, answers.digest)) {
        (void)snprintf(error, sizeof(error),
                       "missing-answer mutation survived");
        goto rejected;
    }
    mutation_count++;
    {
        Atom *saved;
        if (answers.len < 2u)
            goto rejected;
        saved = answers.terms[1];
        answers.terms[1] = answers.terms[0];
        if (!build_rejected(
                &pack, answers.terms, answers.len,
                compiler_digest, answers.digest)) {
            answers.terms[1] = saved;
            (void)snprintf(error, sizeof(error),
                           "duplicate-answer mutation survived");
            goto rejected;
        }
        answers.terms[1] = saved;
        mutation_count++;
    }
    {
        PPActionBytecodeV1Instruction *instruction = &program.instructions[0];
        PPActionBytecodeV1Instruction saved = *instruction;
        instruction->kind = (PPActionBytecodeV1InstructionKind)99;
        if (pp_action_bytecode_v1_program_validate(
                &program, &pack, error, sizeof(error))) {
            *instruction = saved;
            (void)snprintf(error, sizeof(error),
                           "corrupted-program mutation survived");
            goto rejected;
        }
        *instruction = saved;
        if (!pp_action_bytecode_v1_program_validate(
                &program, &pack, error, sizeof(error))) {
            goto rejected;
        }
        mutation_count++;
    }
    {
        PPAtomProjectionActionV1Production *production =
            &projection_actions.productions[0];
        PPAtomProjectionActionV1ExecutionKind saved_kind =
            production->execution_kind;
        production->execution_kind =
            (PPAtomProjectionActionV1ExecutionKind)99;
        if (ppatom_projection_action_v1_program_validate(
                &projection_actions, &program, &projection,
                error, sizeof(error))) {
            production->execution_kind = saved_kind;
            (void)snprintf(
                error, sizeof(error),
                "corrupted specialized action kind survived");
            goto rejected;
        }
        production->execution_kind = saved_kind;
        if (!ppatom_projection_action_v1_program_validate(
                &projection_actions, &program, &projection,
                error, sizeof(error))) {
            goto rejected;
        }
        projection_action_mutations++;
    }
    {
        PPAtomProjectionActionV1Instruction *instruction =
            &projection_actions.instructions[0];
        PPAtomProjectionActionV1Instruction saved = *instruction;
        instruction->kind =
            (PPAtomProjectionActionV1InstructionKind)99;
        if (ppatom_projection_action_v1_program_validate(
                &projection_actions, &program, &projection,
                error, sizeof(error))) {
            *instruction = saved;
            (void)snprintf(error, sizeof(error),
                           "corrupted projection action survived");
            goto rejected;
        }
        *instruction = saved;
        if (!ppatom_projection_action_v1_program_validate(
                &projection_actions, &program, &projection,
                error, sizeof(error))) {
            goto rejected;
        }
        projection_action_mutations++;
    }
    {
        uint32_t instruction_index;
        PPActionBytecodeV1Instruction *apply = NULL;
        Atom *saved;
        for (instruction_index = 0u;
             instruction_index < program.instruction_len;
             instruction_index++) {
            if (program.instructions[instruction_index].kind ==
                    PP_ACTION_BYTECODE_V1_APPLY) {
                apply = &program.instructions[instruction_index];
                break;
            }
        }
        if (!apply || !apply->term)
            goto rejected;
        saved = apply->term;
        apply->term = atom_symbol(&program.arena, "unclassified-carrier");
        {
            PPAtomProjectionActionV1Program rejected_program;
            bool rejected_build;
            ppatom_projection_action_v1_program_init(&rejected_program);
            rejected_build =
                !ppatom_projection_action_v1_program_build_prevalidated(
                    &rejected_program, &program, &projection,
                    error, sizeof(error));
            ppatom_projection_action_v1_program_free(&rejected_program);
            apply->term = saved;
            if (!rejected_build) {
                (void)snprintf(error, sizeof(error),
                               "unclassified carrier survived projection build");
                goto rejected;
            }
        }
        projection_action_mutations++;
    }
    {
        PPAtomProjectionActionV1Value invalid_scalar;
        if (ppatom_projection_action_v1_scalar(
                UINT32_C(0xd800), &invalid_scalar,
                error, sizeof(error))) {
            (void)snprintf(error, sizeof(error),
                           "surrogate compact scalar survived");
            goto rejected;
        }
        projection_action_mutations++;
    }
    if (!projection_document_gate(
            &projection_actions, &projection, &projection_store,
            &projection_semantic_arena, &projection_output_arena,
            &projection_reference_arena, error, sizeof(error))) {
        if (error[0] == '\0') {
            (void)snprintf(error, sizeof(error),
                           "compact document projection disagreed");
        }
        goto rejected;
    }
    projection_document_agreements++;

    printf("parser-action-bytecode-v1\n");
    printf("base-pack-digest\t%s\n", program.base_pack_digest);
    printf("compiler-digest\t%s\n", program.compiler_digest);
    printf("answer-set-digest\t%s\n", program.answer_set_digest);
    printf("program-digest\t%s\n", program.program_digest);
    printf("productions\t%u\n", program.production_len);
    printf("instructions\t%u\n", program.instruction_len);
    printf("push-slots\t%u\n", push_slot_len);
    printf("push-constants\t%u\n", push_const_len);
    printf("applications\t%u\n", apply_len);
    printf("max-stack\t%u\n", max_stack_len);
    printf("tree-bytecode-agreements\t%u\n",
           tree_bytecode_agreements);
    printf("projection-action-program-digest\t%s\n",
           projection_actions.program_digest);
    printf("projection-action-agreements\t%u\n",
           projection_action_agreements);
    printf("projection-specialized-agreements\t%u\n",
           projection_specialized_agreements);
    printf("projection-interpreted-productions\t%u\n",
           projection_interpreted_production_len);
    printf("projection-value-productions\t%u\n",
           projection_value_production_len);
    printf("projection-binary-productions\t%u\n",
           projection_binary_production_len);
    printf("projection-pair-applications\t%u\n", projection_pair_len);
    printf("projection-cons-applications\t%u\n", projection_cons_len);
    printf("projection-node-applications\t%u\n", projection_node_len);
    printf("projection-action-mutations-killed\t%u\n",
           projection_action_mutations);
    printf("projection-document-agreements\t%u\n",
           projection_document_agreements);
    printf("mutations-killed\t%u\n", mutation_count);
    printf("end\n");
    ok = true;
    goto done;

rejected:
    fprintf(stderr, "parser action bytecode rejected: %s\n",
            error[0] ? error : "unknown rejection");

done:
    arena_free(&projection_reference_arena);
    arena_free(&projection_output_arena);
    arena_free(&projection_semantic_arena);
    ppatom_projection_action_v1_store_free(
        &projection_specialized_store);
    ppatom_projection_action_v1_store_free(&projection_store);
    ppatom_projection_action_v1_program_free(&projection_actions);
    ppatom_projection_v1_plan_free(&projection);
    fh_answer_stream_v1_free(&projection_answers);
    arena_free(&bytecode_arena);
    arena_free(&tree_arena);
    arena_free(&slot_arena);
    pp_action_bytecode_v1_program_free(&program);
    fh_answer_stream_v1_free(&answers);
    ppabi_v1_pack_free(&pack);
    ppabi_v1_wire_free(&wire);
    return ok;
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    bool ok;

    if (argc != 5) {
        fprintf(stderr,
            "usage: parser_action_bytecode_v1_stream "
            "ABI ACTION_ANSWERS ACTION_COMPILER_SHA256 PROJECTION_PLAN\n");
        return 1;
    }
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    ok = run(argv[1], argv[2], argv[3], argv[4]);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return ok ? 0 : 1;
}
