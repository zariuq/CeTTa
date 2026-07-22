#include "parser_pack_semantic_mask_binding_v1.h"

#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ppsemantic_binding_v1_set_error(
    char *buf,
    size_t size,
    const char *format,
    ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppsemantic_binding_v1_digest_valid(const char *digest) {
    uint32_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static void ppsemantic_binding_v1_sha_text(
    CettaNativeSha256 *sha,
    const char *text) {
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, strlen(text));
    cetta_native_sha256_update(sha, (const uint8_t *)"\n", 1u);
}

static void ppsemantic_binding_v1_sha_u32(
    CettaNativeSha256 *sha,
    uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppsemantic_binding_v1_digest(
    const PPGuardedLexCursorV1SemanticMaskBinding *binding,
    char out[65]) {
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppsemantic_binding_v1_sha_text(
        &sha, "ParserPackSemanticMaskBindingV1");
    ppsemantic_binding_v1_sha_text(&sha, binding->source_digest);
    ppsemantic_binding_v1_sha_text(&sha, binding->compiler_digest);
    ppsemantic_binding_v1_sha_text(&sha, binding->answer_set_digest);
    ppsemantic_binding_v1_sha_text(&sha, binding->cursor_program_digest);
    ppsemantic_binding_v1_sha_text(
        &sha, binding->projection_action_program_digest);
    ppsemantic_binding_v1_sha_u32(&sha, binding->terminal_len);
    ppsemantic_binding_v1_sha_u32(&sha, binding->bound_terminal_len);
    ppsemantic_binding_v1_sha_u32(&sha, binding->mask.root_link_len);
    ppsemantic_binding_v1_sha_u32(
        &sha, binding->transducer.dfa.state_len);
    ppsemantic_binding_v1_sha_u32(
        &sha, binding->transducer.dfa.transition_len);
    ppsemantic_binding_v1_sha_u32(
        &sha, binding->transducer.transition_action_len);
    ppsemantic_binding_v1_sha_u32(
        &sha, binding->transducer.action_local_transition_len);
    ppsemantic_binding_v1_sha_u32(
        &sha, binding->transducer.backstep_len);
    ppsemantic_binding_v1_sha_u32(
        &sha, binding->transducer.functionality_pair_len);
    for (index = 0u; index < binding->mask.root_link_len; index++) {
        const PPSemanticMaskV1RootLink *link =
            &binding->mask.root_links[index];
        ppsemantic_binding_v1_sha_text(
            &sha, link->definition_canonical);
        ppsemantic_binding_v1_sha_text(&sha, link->label_canonical);
        ppsemantic_binding_v1_sha_u32(&sha, link->tag);
    }
    for (index = 0u; index < binding->terminal_len; index++) {
        const PPAtomProjectionActionV1TextBinding *text =
            &binding->text_bindings[index];
        ppsemantic_binding_v1_sha_u32(
            &sha, binding->terminal_tags[index]);
        ppsemantic_binding_v1_sha_u32(&sha, text->rule_dense_id);
        ppsemantic_binding_v1_sha_u32(
            &sha, text->rule_constant_instruction);
        ppsemantic_binding_v1_sha_u32(&sha, text->node_instruction);
    }
    for (index = 0u; index < binding->mask.action_len; index++) {
        ppsemantic_binding_v1_sha_u32(
            &sha, (uint32_t)binding->mask.actions[index].kind);
        ppsemantic_binding_v1_sha_u32(
            &sha, binding->action_wrapper_indices[index]);
    }
    for (index = 0u;
         index < binding->transducer.transition_action_len; index++) {
        ppsemantic_binding_v1_sha_u32(
            &sha, binding->transducer.transition_actions[index]);
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

void ppguarded_lex_cursor_v1_semantic_mask_binding_init(
    PPGuardedLexCursorV1SemanticMaskBinding *binding) {
    if (!binding)
        return;
    memset(binding, 0, sizeof(*binding));
    ppsemantic_mask_nfa_v1_plan_init(&binding->mask);
    ppsemantic_mask_trace_dfa_v1_program_init(&binding->transducer);
}

void ppguarded_lex_cursor_v1_semantic_mask_binding_free(
    PPGuardedLexCursorV1SemanticMaskBinding *binding) {
    if (!binding)
        return;
    ppsemantic_mask_trace_dfa_v1_program_free(&binding->transducer);
    ppsemantic_mask_nfa_v1_plan_free(&binding->mask);
    free(binding->terminal_tags);
    free(binding->text_bindings);
    free(binding->action_wrapper_indices);
    memset(binding, 0, sizeof(*binding));
}

static int32_t ppsemantic_binding_v1_link_for_tag(
    const PPGuardedLexCursorV1SemanticMaskBinding *binding,
    uint32_t tag) {
    uint32_t index;

    for (index = 0u; index < binding->mask.root_link_len; index++) {
        if (binding->mask.root_links[index].tag == tag)
            return (int32_t)index;
    }
    return -1;
}

static bool ppsemantic_binding_v1_state_is_definition(
    const PPABIV1Pack *pack,
    uint32_t state_id,
    const Atom *definition) {
    Atom *identity;

    if (!pack || state_id >= pack->state_len || !definition)
        return false;
    identity = pack->states[state_id].identity;
    return identity && identity->kind == ATOM_EXPR &&
        identity->expr.len == 2u &&
        atom_is_symbol(identity->expr.elems[0], "pp-def") &&
        atom_eq(identity->expr.elems[1], (Atom *)definition);
}

bool ppguarded_lex_cursor_v1_semantic_mask_binding_validate(
    const PPGuardedLexCursorV1SemanticMaskBinding *binding,
    const PPGuardedLexCursorV1Program *cursor,
    const PPABIV1Pack *pack,
    const PPAtomProjectionActionV1Program *projection_actions,
    const PPAtomProjectionV1Plan *projection,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t index;
    uint32_t bound = 0u;
    char digest[65];

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!binding || !cursor || !pack || !projection_actions || !projection ||
        !binding->terminal_tags || !binding->text_bindings ||
        !binding->action_wrapper_indices ||
        binding->terminal_len != cursor->terminal_len ||
        binding->bound_terminal_len == 0u ||
        binding->mask.root_link_len != binding->bound_terminal_len ||
        binding->mask.root_link_len != binding->mask.erased.nfa.tag_len ||
        binding->transducer.dfa.tag_len !=
            binding->mask.erased.nfa.tag_len ||
        binding->transducer.action_len != binding->mask.action_len ||
        strcmp(binding->source_digest, pack->source_digest) != 0 ||
        strcmp(binding->cursor_program_digest, cursor->program_digest) != 0 ||
        strcmp(binding->projection_action_program_digest,
               projection_actions->program_digest) != 0 ||
        !ppsemantic_binding_v1_digest_valid(binding->compiler_digest) ||
        !ppsemantic_binding_v1_digest_valid(binding->answer_set_digest) ||
        !ppsemantic_mask_trace_dfa_v1_program_validate(
            &binding->transducer, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsemantic_binding_v1_set_error(
                error_buf, error_buf_size,
                "bad semantic-mask cursor binding");
        }
        return false;
    }
    for (index = 0u; index < binding->mask.action_len; index++) {
        const PPSemanticMaskV1Action *action =
            &binding->mask.actions[index];
        uint32_t wrapper_index = binding->action_wrapper_indices[index];
        if (action->kind == PPSEMANTIC_MASK_V1_WRAPPER) {
            PPAtomProjectionV1WrapperView wrapper;
            if (wrapper_index == UINT32_MAX ||
                !ppatom_projection_v1_plan_wrapper(
                    projection, wrapper_index, &wrapper,
                    error_buf, error_buf_size) ||
                !action->wrapper_label ||
                !atom_is_symbol(action->wrapper_label, wrapper.label)) {
                if (error_buf && error_buf_size > 0u &&
                    error_buf[0] == '\0') {
                    ppsemantic_binding_v1_set_error(
                        error_buf, error_buf_size,
                        "semantic-mask wrapper lost its projection binding");
                }
                return false;
            }
        } else if (wrapper_index != UINT32_MAX) {
            ppsemantic_binding_v1_set_error(
                error_buf, error_buf_size,
                "non-wrapper semantic action retained a wrapper binding");
            return false;
        }
    }
    for (index = 0u; index < binding->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &cursor->terminals[index];
        const PPAtomProjectionActionV1TextBinding *text =
            &binding->text_bindings[index];
        uint32_t tag = binding->terminal_tags[index];
        int32_t link_index;
        PPAtomProjectionActionV1TextBinding expected;
        const PPSemanticMaskV1RootLink *link;
        const PPAtomProjectionActionV1Instruction *constant;

        if (tag == UINT32_MAX) {
            if (text->rule_dense_id != UINT32_MAX ||
                text->rule_constant_instruction != UINT32_MAX ||
                text->node_instruction != UINT32_MAX) {
                ppsemantic_binding_v1_set_error(
                    error_buf, error_buf_size,
                    "unbound cursor terminal retained a text binding");
                return false;
            }
            continue;
        }
        if (tag >= binding->transducer.dfa.tag_len ||
            terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR ||
            terminal->semantic_start_state_id >= pack->state_len ||
            (link_index = ppsemantic_binding_v1_link_for_tag(
                 binding, tag)) < 0) {
            ppsemantic_binding_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask cursor terminal mapping is malformed");
            return false;
        }
        link = &binding->mask.root_links[(uint32_t)link_index];
        if (!ppsemantic_binding_v1_state_is_definition(
                pack, terminal->semantic_start_state_id,
                link->definition) ||
            !ppatom_projection_action_v1_text_binding(
                projection_actions, text->rule_dense_id, &expected,
                error_buf, error_buf_size) ||
            expected.rule_dense_id != text->rule_dense_id ||
            expected.rule_constant_instruction !=
                text->rule_constant_instruction ||
            expected.node_instruction != text->node_instruction ||
            text->rule_constant_instruction >=
                projection_actions->instruction_len) {
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
                ppsemantic_binding_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask cursor binding lost its source identity");
            }
            return false;
        }
        constant = &projection_actions->instructions[
            text->rule_constant_instruction];
        if (!constant->term || !atom_eq(constant->term, link->label)) {
            ppsemantic_binding_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask projection rule differs from its root label");
            return false;
        }
        bound++;
    }
    if (bound != binding->bound_terminal_len) {
        ppsemantic_binding_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask cursor binding count changed");
        return false;
    }
    ppsemantic_binding_v1_digest(binding, digest);
    if (strcmp(digest, binding->binding_digest) != 0) {
        ppsemantic_binding_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask cursor binding digest changed");
        return false;
    }
    return true;
}

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
    size_t error_buf_size) {
    PPGuardedLexCursorV1SemanticMaskBinding candidate;
    RSDFAV1BuildOutcome outcome = RSDFA_V1_BUILD_STATE_LIMIT;
    uint32_t rule_len = 0u;
    uint32_t wrapper_len = 0u;
    uint64_t functionality_work_limit;
    uint32_t index;
    bool ok = false;

    ppguarded_lex_cursor_v1_semantic_mask_binding_init(&candidate);
    functionality_work_limit =
        (uint64_t)transition_limit * UINT64_C(16);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!binding || !cursor || !pack || !projection_actions || !projection ||
        !answer_terms || answer_len == 0u ||
        !ppsemantic_binding_v1_digest_valid(compiler_digest) ||
        !ppsemantic_binding_v1_digest_valid(answer_set_digest) ||
        state_limit == 0u || transition_limit == 0u ||
        !ppatom_projection_v1_plan_rule_count(
            projection, &rule_len, error_buf, error_buf_size) ||
        !ppatom_projection_v1_plan_wrapper_count(
            projection, &wrapper_len, error_buf, error_buf_size) ||
        !ppsemantic_mask_nfa_v1_plan_load(
            pack, answer_terms, answer_len, &candidate.mask,
            error_buf, error_buf_size) ||
        candidate.mask.root_link_len == 0u ||
        !ppsemantic_mask_trace_dfa_v1_program_build(
            &candidate.mask, state_limit, transition_limit,
            state_limit, functionality_work_limit,
            &candidate.transducer, &outcome,
            error_buf, error_buf_size) ||
        outcome != RSDFA_V1_BUILD_COMPLETED ||
        cursor->terminal_len == 0u || !cursor->terminals ||
        (size_t)cursor->terminal_len >
            SIZE_MAX / sizeof(*candidate.terminal_tags) ||
        (size_t)cursor->terminal_len >
            SIZE_MAX / sizeof(*candidate.text_bindings) ||
        (size_t)candidate.mask.action_len >
            SIZE_MAX / sizeof(*candidate.action_wrapper_indices)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppsemantic_binding_v1_set_error(
                error_buf, error_buf_size,
                "bad semantic-mask binding inputs");
        }
        goto done;
    }
    candidate.terminal_len = cursor->terminal_len;
    candidate.terminal_tags = malloc(
        sizeof(*candidate.terminal_tags) * candidate.terminal_len);
    candidate.text_bindings = malloc(
        sizeof(*candidate.text_bindings) * candidate.terminal_len);
    candidate.action_wrapper_indices = malloc(
        sizeof(*candidate.action_wrapper_indices) *
        candidate.mask.action_len);
    if (!candidate.terminal_tags || !candidate.text_bindings ||
        !candidate.action_wrapper_indices) {
        ppsemantic_binding_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate semantic-mask terminal binding");
        goto done;
    }
    for (index = 0u; index < candidate.terminal_len; index++) {
        candidate.terminal_tags[index] = UINT32_MAX;
        candidate.text_bindings[index] =
            (PPAtomProjectionActionV1TextBinding){
                .rule_dense_id = UINT32_MAX,
                .rule_constant_instruction = UINT32_MAX,
                .node_instruction = UINT32_MAX,
            };
    }
    for (index = 0u; index < candidate.mask.action_len; index++) {
        const PPSemanticMaskV1Action *action =
            &candidate.mask.actions[index];
        uint32_t wrapper_index;
        uint32_t matches = 0u;

        candidate.action_wrapper_indices[index] = UINT32_MAX;
        if (action->kind != PPSEMANTIC_MASK_V1_WRAPPER)
            continue;
        for (wrapper_index = 0u; wrapper_index < wrapper_len;
             wrapper_index++) {
            PPAtomProjectionV1WrapperView wrapper;
            if (!ppatom_projection_v1_plan_wrapper(
                    projection, wrapper_index, &wrapper,
                    error_buf, error_buf_size)) {
                goto done;
            }
            if (action->wrapper_label &&
                atom_is_symbol(action->wrapper_label, wrapper.label)) {
                candidate.action_wrapper_indices[index] = wrapper_index;
                matches++;
            }
        }
        if (matches != 1u) {
            ppsemantic_binding_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask wrapper has %u projection bindings",
                matches);
            goto done;
        }
    }
    for (index = 0u; index < candidate.mask.root_link_len; index++) {
        const PPSemanticMaskV1RootLink *link =
            &candidate.mask.root_links[index];
        uint32_t terminal_index = UINT32_MAX;
        uint32_t rule_index = UINT32_MAX;
        uint32_t terminal_matches = 0u;
        uint32_t rule_matches = 0u;
        uint32_t scan;

        for (scan = 0u; scan < cursor->terminal_len; scan++) {
            const PPGuardedLexCursorV1Terminal *terminal =
                &cursor->terminals[scan];
            if (terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR ||
                !ppsemantic_binding_v1_state_is_definition(
                    pack, terminal->semantic_start_state_id,
                    link->definition)) {
                continue;
            }
            terminal_index = scan;
            terminal_matches++;
        }
        for (scan = 0u; scan < rule_len; scan++) {
            PPAtomProjectionV1RuleView rule;
            if (!ppatom_projection_v1_plan_rule(
                    projection, scan, &rule,
                    error_buf, error_buf_size)) {
                goto done;
            }
            if (!atom_is_symbol(link->label, rule.source_label))
                continue;
            rule_index = scan;
            rule_matches++;
        }
        if (terminal_matches != 1u || rule_matches != 1u ||
            terminal_index == UINT32_MAX || rule_index == UINT32_MAX ||
            candidate.terminal_tags[terminal_index] != UINT32_MAX ||
            !ppatom_projection_action_v1_text_binding(
                projection_actions, rule_index,
                &candidate.text_bindings[terminal_index],
                error_buf, error_buf_size)) {
            if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
                ppsemantic_binding_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask root link %s -> %s has %u cursor "
                    "terminals and %u projection rules",
                    link->definition_canonical, link->label_canonical,
                    terminal_matches, rule_matches);
            }
            goto done;
        }
        candidate.terminal_tags[terminal_index] = link->tag;
        candidate.bound_terminal_len++;
    }
    (void)snprintf(
        candidate.source_digest, 65u, "%s", pack->source_digest);
    (void)snprintf(
        candidate.compiler_digest, 65u, "%s", compiler_digest);
    (void)snprintf(
        candidate.answer_set_digest, 65u, "%s", answer_set_digest);
    (void)snprintf(
        candidate.cursor_program_digest, 65u, "%s", cursor->program_digest);
    (void)snprintf(
        candidate.projection_action_program_digest, 65u, "%s",
        projection_actions->program_digest);
    ppsemantic_binding_v1_digest(&candidate, candidate.binding_digest);
    if (!ppguarded_lex_cursor_v1_semantic_mask_binding_validate(
            &candidate, cursor, pack, projection_actions,
            projection,
            error_buf, error_buf_size)) {
        goto done;
    }
    ppguarded_lex_cursor_v1_semantic_mask_binding_free(binding);
    *binding = candidate;
    memset(&candidate, 0, sizeof(candidate));
    ok = true;

done:
    ppguarded_lex_cursor_v1_semantic_mask_binding_free(&candidate);
    return ok;
}
