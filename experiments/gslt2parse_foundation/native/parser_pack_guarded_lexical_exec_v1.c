#include "parser_pack_guarded_lexical_exec_v1.h"

#include "parser_pack_semantic_mask_binding_v1.h"

#include "native_sha256.h"

#include "symbol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PPGUARDED_LEX_EXEC_V1_GLL = 0,
    PPGUARDED_LEX_EXEC_V1_GLR = 1
} PPGuardedLexExecV1Backend;

typedef struct {
    const PPABIV1Pack *pack;
    const PPLexV1Plan *lexical_plan;
    const PPGuardedLexExecV1Plan *exec_plan;
    PPGuardedLexExecV1Backend backend;
    uint32_t replay_depth;
    uint32_t result_limit;
} PPGuardedLexCursorV1PreparedFallback;

typedef struct {
    uint32_t *starts;
    RSDFAV1NfaEdge *edges;
    RSDFAV1NfaAccept *accepts;
    RSDFAV1Nfa nfa;
} PPGuardedLexExecV1NfaUnion;

typedef struct {
    uint32_t *data;
    uint32_t len;
    uint32_t cap;
} PPGuardedLexExecV1U32Vec;

typedef struct {
    const CettaLpNativeUnicodeRange *ranges;
    uint32_t range_len;
    CettaLpNativeUnicodeRange singleton;
    uint32_t slr_terminal_index;
    uint32_t dfa_tag_index;
    bool eof;
} PPGuardedLexExecV1TokenDomain;

static int32_t ppguarded_lex_exec_v1_forest_root_find(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t state_id,
    uint32_t left,
    uint32_t right);

static bool ppguarded_lex_exec_v1_replay_value(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t root_node,
    uint32_t replay_depth,
    uint32_t result_limit,
    Arena *arena,
    Atom **value,
    uint32_t *value_len,
    PPNativeV1Outcome *outcome,
    char *error_buf,
    size_t error_buf_size);

static void ppguarded_lex_exec_v1_set_error(
    char *buf, size_t size, const char *format, ...) {
    va_list arguments;

    if (!buf || size == 0u)
        return;
    va_start(arguments, format);
    (void)vsnprintf(buf, size, format, arguments);
    va_end(arguments);
}

static bool ppguarded_lex_exec_v1_limits_valid(
    const PPGuardedLexExecV1Limits *limits) {
    return limits && limits->dfa_state_limit > 0u &&
        limits->dfa_transition_limit > 0u &&
        limits->scan_work_limit > 0u && limits->scan_token_limit > 0u &&
        limits->witness_work_limit > 0u && limits->parse_work_limit > 0u &&
        limits->replay_depth > 0u && limits->result_limit > 0u;
}

static bool ppguarded_lex_exec_v1_array_fits(
    uint32_t len, size_t element_size) {
    return len == 0u || element_size <= SIZE_MAX / (size_t)len;
}

static bool ppguarded_lex_exec_v1_source_receipt_valid(
    uint32_t source_pass_count,
    uint32_t decoded_byte_len,
    uint32_t input_byte_len) {
    return (source_pass_count == 0u && decoded_byte_len == 0u) ||
        (source_pass_count == 1u && decoded_byte_len == input_byte_len);
}

void ppguarded_lex_cursor_v1_dispatch_index_init(
    PPGuardedLexCursorV1DispatchIndex *index) {
    if (index)
        memset(index, 0, sizeof(*index));
}

void ppguarded_lex_cursor_v1_dispatch_index_free(
    PPGuardedLexCursorV1DispatchIndex *index) {
    if (!index)
        return;
    free(index->span_source_indices);
    free(index->span_offsets);
    free(index->scalar_source_indices);
    free(index->scalar_offsets);
    memset(index, 0, sizeof(*index));
}

static bool ppguarded_lex_cursor_v1_dispatch_program_shape_valid(
    const PPGuardedLexCursorV1Program *program) {
    size_t action_len;
    uint32_t source_index;
    uint32_t columns;

    if (!program || program->final_slr.summary.state_len == 0u ||
        program->final_slr.summary.state_len == UINT32_MAX ||
        program->final_slr.terminal_len == UINT32_MAX ||
        program->terminal_len > program->final_slr.terminal_len ||
        !program->final_slr.actions ||
        (program->terminal_len > 0u && !program->terminals)) {
        return false;
    }
    columns = program->final_slr.terminal_len + 1u;
    if ((size_t)program->final_slr.summary.state_len >
            SIZE_MAX / (size_t)columns) {
        return false;
    }
    action_len =
        (size_t)program->final_slr.summary.state_len * (size_t)columns;
    if (action_len > SIZE_MAX / sizeof(*program->final_slr.actions))
        return false;
    for (source_index = 0u; source_index < program->terminal_len;
         source_index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[source_index];

        if (terminal->slr_terminal_index >=
                program->final_slr.terminal_len ||
            terminal->kind >
                PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN) {
            return false;
        }
    }
    return true;
}

bool ppguarded_lex_cursor_v1_dispatch_index_validate(
    const PPGuardedLexCursorV1Program *program,
    const PPGuardedLexCursorV1DispatchIndex *index,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t columns;
    uint32_t state;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!ppguarded_lex_cursor_v1_dispatch_program_shape_valid(program) ||
        !index ||
        index->state_len != program->final_slr.summary.state_len ||
        !index->scalar_offsets || !index->span_offsets ||
        (index->scalar_candidate_len > 0u &&
         !index->scalar_source_indices) ||
        (index->span_candidate_len > 0u &&
         !index->span_source_indices) ||
        index->scalar_offsets[0] != 0u || index->span_offsets[0] != 0u ||
        index->scalar_offsets[index->state_len] !=
            index->scalar_candidate_len ||
        index->span_offsets[index->state_len] !=
            index->span_candidate_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad guarded lexical cursor dispatch index");
        return false;
    }
    columns = program->final_slr.terminal_len + 1u;
    for (state = 0u; state < index->state_len; state++) {
        uint32_t scalar = index->scalar_offsets[state];
        uint32_t scalar_end = index->scalar_offsets[state + 1u];
        uint32_t span = index->span_offsets[state];
        uint32_t span_end = index->span_offsets[state + 1u];
        uint32_t source_index;

        if (scalar > scalar_end ||
            scalar_end > index->scalar_candidate_len ||
            span > span_end || span_end > index->span_candidate_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical cursor dispatch offsets are malformed");
            return false;
        }
        for (source_index = 0u; source_index < program->terminal_len;
             source_index++) {
            const PPGuardedLexCursorV1Terminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &program->final_slr.actions[
                    (size_t)state * columns +
                    terminal->slr_terminal_index];
            uint32_t *candidate;
            uint32_t candidate_end;
            const uint32_t *source_indices;

            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
                continue;
            if (terminal->kind ==
                    PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR) {
                candidate = &scalar;
                candidate_end = scalar_end;
                source_indices = index->scalar_source_indices;
            } else {
                candidate = &span;
                candidate_end = span_end;
                source_indices = index->span_source_indices;
            }
            if (*candidate >= candidate_end ||
                source_indices[*candidate] != source_index) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "guarded lexical cursor dispatch index disagrees "
                    "with its SLR table");
                return false;
            }
            (*candidate)++;
        }
        if (scalar != scalar_end || span != span_end) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical cursor dispatch index has trailing "
                "candidates");
            return false;
        }
    }
    return true;
}

bool ppguarded_lex_cursor_v1_dispatch_index_build(
    const PPGuardedLexCursorV1Program *program,
    PPGuardedLexCursorV1DispatchIndex *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1DispatchIndex result;
    uint64_t scalar_candidate_len = 0u;
    uint64_t span_candidate_len = 0u;
    uint32_t scalar_write = 0u;
    uint32_t span_write = 0u;
    uint32_t columns;
    uint32_t state;

    ppguarded_lex_cursor_v1_dispatch_index_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out ||
        !ppguarded_lex_cursor_v1_dispatch_program_shape_valid(program)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad guarded lexical cursor dispatch-index build input");
        return false;
    }
    columns = program->final_slr.terminal_len + 1u;
    for (state = 0u; state < program->final_slr.summary.state_len;
         state++) {
        uint32_t source_index;

        for (source_index = 0u; source_index < program->terminal_len;
             source_index++) {
            const PPGuardedLexCursorV1Terminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &program->final_slr.actions[
                    (size_t)state * columns +
                    terminal->slr_terminal_index];

            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
                continue;
            if (terminal->kind ==
                    PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR) {
                scalar_candidate_len++;
            } else {
                span_candidate_len++;
            }
        }
    }
    if (scalar_candidate_len > UINT32_MAX ||
        span_candidate_len > UINT32_MAX ||
        (size_t)program->final_slr.summary.state_len + 1u >
            SIZE_MAX / sizeof(*result.scalar_offsets) ||
        (size_t)scalar_candidate_len >
            SIZE_MAX / sizeof(*result.scalar_source_indices) ||
        (size_t)span_candidate_len >
            SIZE_MAX / sizeof(*result.span_source_indices)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "guarded lexical cursor dispatch-index size overflow");
        return false;
    }
    result.state_len = program->final_slr.summary.state_len;
    result.scalar_offsets = calloc(
        (size_t)result.state_len + 1u,
        sizeof(*result.scalar_offsets));
    result.span_offsets = calloc(
        (size_t)result.state_len + 1u,
        sizeof(*result.span_offsets));
    result.scalar_source_indices = calloc(
        scalar_candidate_len ? (size_t)scalar_candidate_len : 1u,
        sizeof(*result.scalar_source_indices));
    result.span_source_indices = calloc(
        span_candidate_len ? (size_t)span_candidate_len : 1u,
        sizeof(*result.span_source_indices));
    if (!result.scalar_offsets || !result.span_offsets ||
        !result.scalar_source_indices || !result.span_source_indices) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate guarded lexical cursor dispatch index");
        goto fail;
    }
    for (state = 0u; state < result.state_len; state++) {
        uint32_t source_index;

        result.scalar_offsets[state] = scalar_write;
        result.span_offsets[state] = span_write;
        for (source_index = 0u; source_index < program->terminal_len;
             source_index++) {
            const PPGuardedLexCursorV1Terminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &program->final_slr.actions[
                    (size_t)state * columns +
                    terminal->slr_terminal_index];

            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
                continue;
            if (terminal->kind ==
                    PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR) {
                result.scalar_source_indices[scalar_write++] =
                    source_index;
            } else {
                result.span_source_indices[span_write++] = source_index;
            }
        }
    }
    result.scalar_offsets[result.state_len] = scalar_write;
    result.span_offsets[result.state_len] = span_write;
    result.scalar_candidate_len = scalar_write;
    result.span_candidate_len = span_write;
    if (scalar_write != (uint32_t)scalar_candidate_len ||
        span_write != (uint32_t)span_candidate_len ||
        !ppguarded_lex_cursor_v1_dispatch_index_validate(
            program, &result, error_buf, error_buf_size)) {
        goto fail;
    }
    ppguarded_lex_cursor_v1_dispatch_index_free(out);
    *out = result;
    return true;

fail:
    ppguarded_lex_cursor_v1_dispatch_index_free(&result);
    return false;
}

static void ppguarded_lex_exec_v1_sha_text(
    CettaNativeSha256 *sha, const char *text) {
    cetta_native_sha256_update(
        sha, (const uint8_t *)text, strlen(text));
}

static void ppguarded_lex_exec_v1_sha_u32(
    CettaNativeSha256 *sha, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value >> 24u),
        (uint8_t)(value >> 16u),
        (uint8_t)(value >> 8u),
        (uint8_t)value,
    };
    cetta_native_sha256_update(sha, bytes, sizeof(bytes));
}

static void ppguarded_lex_exec_v1_plan_digest(
    const PPGuardedLexExecV1Plan *plan, char out[65]) {
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppguarded_lex_exec_v1_sha_text(
        &sha, "ParserPackGuardedLexicalExecutionPlanV1\n");
    ppguarded_lex_exec_v1_sha_text(&sha, plan->base_pack_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, plan->lexical_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, plan->guard_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, plan->guarded_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_u32(&sha, plan->lexical_tag_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, plan->guarded_tag_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, plan->guard_tag_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, plan->dfa.state_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, plan->dfa.transition_len);
    for (index = 0u;
         index < plan->lexical_tag_len + plan->guarded_tag_len; index++) {
        ppguarded_lex_exec_v1_sha_u32(
            &sha, plan->projected_tag_terminal_ids[index]);
    }
    for (index = 0u; index < plan->guarded_tag_len; index++) {
        ppguarded_lex_exec_v1_sha_u32(
            &sha, plan->guard_local_for_guarded[index]);
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

static void ppguarded_lex_exec_v1_cursor_certificate_digest(
    const PPGuardedLexExecV1Plan *plan,
    const PPGuardedLexCursorV1Certificate *certificate,
    char out[65]) {
    CettaNativeSha256 sha;

    cetta_native_sha256_init(&sha);
    ppguarded_lex_exec_v1_sha_text(
        &sha, "ParserPackGuardedLexicalCursorCertificateV1\n");
    ppguarded_lex_exec_v1_sha_text(&sha, plan->execution_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->slr.state_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->slr.shift_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->slr.goto_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->slr.reduce_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->slr.accept_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->slr.conflict_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->reachable_nonterminal_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->reachable_production_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->active_terminal_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->scalar_terminal_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->ordinary_span_tag_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->guarded_span_tag_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->guard_lookahead_tag_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->zero_input_cycle_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->unmapped_terminal_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->duplicate_terminal_source_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->empty_token_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->nullable_token_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->non_prefix_free_token_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->unsupported_guard_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->boundary_crossing_len);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->token_prefix_overlap_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, certificate->failure_mask);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, certificate->eligible ? 1u : 0u);
    cetta_native_sha256_finish_hex(&sha, out);
}

static int32_t ppguarded_lex_exec_v1_u32_find(
    const PPGuardedLexExecV1U32Vec *values, uint32_t value) {
    uint32_t index;

    for (index = 0u; index < values->len; index++) {
        if (values->data[index] == value)
            return (int32_t)index;
    }
    return -1;
}

static bool ppguarded_lex_exec_v1_u32_add(
    PPGuardedLexExecV1U32Vec *values, uint32_t value) {
    uint32_t next_cap;
    uint32_t *next;

    if (ppguarded_lex_exec_v1_u32_find(values, value) >= 0)
        return true;
    if (values->len == values->cap) {
        next_cap = values->cap ? values->cap * 2u : 16u;
        if (next_cap < values->cap ||
            (size_t)next_cap > SIZE_MAX / sizeof(*values->data)) {
            return false;
        }
        next = realloc(
            values->data, sizeof(*values->data) * (size_t)next_cap);
        if (!next)
            return false;
        values->data = next;
        values->cap = next_cap;
    }
    values->data[values->len++] = value;
    return true;
}

static bool ppguarded_lex_exec_v1_reachability(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_state_id,
    PPGuardedLexExecV1U32Vec *nonterminals,
    PPGuardedLexExecV1U32Vec *terminals,
    bool **out_productions,
    uint32_t *out_production_len,
    char *error_buf,
    size_t error_buf_size) {
    bool *productions = NULL;
    uint32_t production_len = 0u;
    bool changed = true;
    uint32_t index;

    if (!grammar || !nonterminals || !terminals || !out_productions ||
        !out_production_len ||
        (grammar->production_len > 0u && !grammar->productions) ||
        !ppguarded_lex_exec_v1_u32_add(nonterminals, start_state_id)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to initialize cursor-certificate reachability");
        return false;
    }
    productions = calloc(
        grammar->production_len ? grammar->production_len : 1u,
        sizeof(*productions));
    if (!productions) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate cursor-certificate reachability");
        return false;
    }
    while (changed) {
        changed = false;
        for (index = 0u; index < grammar->production_len; index++) {
            const CettaLpNativeProduction *production =
                &grammar->productions[index];
            uint32_t rhs_index;
            if (ppguarded_lex_exec_v1_u32_find(
                    nonterminals, production->lhs) < 0) {
                continue;
            }
            if (!productions[index]) {
                productions[index] = true;
                production_len++;
            }
            for (rhs_index = 0u;
                 rhs_index < production->rhs_len; rhs_index++) {
                const CettaLpNativeSymbol *symbol =
                    &production->rhs[rhs_index];
                uint32_t before;
                if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    if (!ppguarded_lex_exec_v1_u32_add(
                            terminals, symbol->name)) {
                        goto allocation_error;
                    }
                    continue;
                }
                before = nonterminals->len;
                if (!ppguarded_lex_exec_v1_u32_add(
                        nonterminals, symbol->name)) {
                    goto allocation_error;
                }
                changed = changed || nonterminals->len != before;
            }
        }
    }
    *out_productions = productions;
    *out_production_len = production_len;
    return true;

allocation_error:
    free(productions);
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "failed to grow cursor-certificate reachability");
    return false;
}

static bool ppguarded_lex_exec_v1_zero_input_cycles(
    const CettaLpNativeGrammar *grammar,
    const PPGuardedLexExecV1U32Vec *nonterminals,
    const bool *reachable_productions,
    uint32_t *out,
    char *error_buf,
    size_t error_buf_size) {
    bool *nullable = NULL;
    bool *edges = NULL;
    size_t matrix_len;
    bool changed = true;
    uint32_t index;
    uint32_t middle;
    uint32_t left;
    uint32_t right;
    uint32_t cycles = 0u;
    bool ok = false;

    if (!grammar || !nonterminals || !reachable_productions || !out ||
        (nonterminals->len > 0u &&
         (size_t)nonterminals->len >
             SIZE_MAX / (size_t)nonterminals->len) ||
        ((size_t)nonterminals->len * nonterminals->len) >
            SIZE_MAX / sizeof(*edges)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor-certificate nullable analysis");
        return false;
    }
    matrix_len = (size_t)nonterminals->len * nonterminals->len;
    nullable = calloc(
        nonterminals->len ? nonterminals->len : 1u,
        sizeof(*nullable));
    edges = calloc(matrix_len ? matrix_len : 1u, sizeof(*edges));
    if (!nullable || !edges) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate cursor-certificate nullable analysis");
        goto done;
    }
    while (changed) {
        changed = false;
        for (index = 0u; index < grammar->production_len; index++) {
            const CettaLpNativeProduction *production;
            int32_t lhs;
            uint32_t rhs_index;
            bool all_nullable = true;
            if (!reachable_productions[index])
                continue;
            production = &grammar->productions[index];
            lhs = ppguarded_lex_exec_v1_u32_find(
                nonterminals, production->lhs);
            if (lhs < 0)
                goto malformed;
            for (rhs_index = 0u;
                 rhs_index < production->rhs_len; rhs_index++) {
                const CettaLpNativeSymbol *symbol =
                    &production->rhs[rhs_index];
                int32_t rhs;
                if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    all_nullable = false;
                    break;
                }
                rhs = ppguarded_lex_exec_v1_u32_find(
                    nonterminals, symbol->name);
                if (rhs < 0)
                    goto malformed;
                if (!nullable[rhs]) {
                    all_nullable = false;
                    break;
                }
            }
            if (all_nullable && !nullable[lhs]) {
                nullable[lhs] = true;
                changed = true;
            }
        }
    }
    for (index = 0u; index < grammar->production_len; index++) {
        const CettaLpNativeProduction *production;
        int32_t lhs;
        uint32_t rhs_index;
        bool zero_input = true;
        if (!reachable_productions[index])
            continue;
        production = &grammar->productions[index];
        lhs = ppguarded_lex_exec_v1_u32_find(
            nonterminals, production->lhs);
        if (lhs < 0)
            goto malformed;
        for (rhs_index = 0u;
             rhs_index < production->rhs_len; rhs_index++) {
            const CettaLpNativeSymbol *symbol =
                &production->rhs[rhs_index];
            int32_t rhs;
            if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                zero_input = false;
                break;
            }
            rhs = ppguarded_lex_exec_v1_u32_find(
                nonterminals, symbol->name);
            if (rhs < 0)
                goto malformed;
            if (!nullable[rhs]) {
                zero_input = false;
                break;
            }
        }
        if (!zero_input)
            continue;
        for (rhs_index = 0u;
             rhs_index < production->rhs_len; rhs_index++) {
            int32_t rhs = ppguarded_lex_exec_v1_u32_find(
                nonterminals, production->rhs[rhs_index].name);
            if (rhs < 0)
                goto malformed;
            edges[(size_t)lhs * nonterminals->len + (uint32_t)rhs] = true;
        }
    }
    for (middle = 0u; middle < nonterminals->len; middle++) {
        for (left = 0u; left < nonterminals->len; left++) {
            if (!edges[(size_t)left * nonterminals->len + middle])
                continue;
            for (right = 0u; right < nonterminals->len; right++) {
                if (edges[(size_t)middle * nonterminals->len + right]) {
                    edges[(size_t)left * nonterminals->len + right] = true;
                }
            }
        }
    }
    for (index = 0u; index < nonterminals->len; index++) {
        if (edges[(size_t)index * nonterminals->len + index])
            cycles++;
    }
    *out = cycles;
    ok = true;
    goto done;

malformed:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "reachable grammar escaped cursor-certificate closure");
done:
    free(edges);
    free(nullable);
    return ok;
}

static bool ppguarded_lex_exec_v1_token_domains_overlap(
    const PPGuardedLexExecV1Plan *plan,
    const PPGuardedLexExecV1TokenDomain *left,
    const PPGuardedLexExecV1TokenDomain *right,
    bool *out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t left_index;
    uint32_t right_index;

    if (!plan || !left || !right || !out) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor token-domain overlap arguments");
        return false;
    }
    if (left->dfa_tag_index != UINT32_MAX &&
        right->dfa_tag_index != UINT32_MAX) {
        return rsdfa_v1_plan_tags_prefix_overlap(
            &plan->dfa, left->dfa_tag_index, right->dfa_tag_index,
            out, error_buf, error_buf_size);
    }
    if (left->eof && right->eof) {
        *out = true;
        return true;
    }
    for (left_index = 0u; left_index < left->range_len; left_index++) {
        for (right_index = 0u;
             right_index < right->range_len; right_index++) {
            if (left->ranges[left_index].low <=
                    right->ranges[right_index].high &&
                right->ranges[right_index].low <=
                    left->ranges[left_index].high) {
                *out = true;
                return true;
            }
        }
    }
    *out = false;
    return true;
}

static bool ppguarded_lex_exec_v1_cursor_certificate_build(
    const CettaLpNativeGrammar *grammar,
    const PPGuardedLexExecV1Plan *plan,
    PPGuardedLexCursorV1Certificate *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1Certificate result = {0};
    PPGuardedLexExecV1U32Vec nonterminals = {0};
    PPGuardedLexExecV1U32Vec terminals = {0};
    CettaLpNativeSlrProgram slr_program;
    bool *reachable_productions = NULL;
    RSDFAV1TagLanguage *tag_languages = NULL;
    bool *tag_analyzed = NULL;
    PPGuardedLexExecV1TokenDomain *domains = NULL;
    uint32_t domain_len = 0u;
    uint32_t index;
    bool ok = false;

    cetta_lp_native_slr_program_init(&slr_program);
    if (!grammar || !plan || !out || !plan->dfa.impl ||
        !plan->final_slr.implementation ||
        plan->combined_tag_len != plan->dfa.tag_len ||
        plan->combined_tag_len == 0u) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor-certificate inputs");
        return false;
    }
    if (!cetta_lp_native_slr_prepared_summary(
            &plan->final_slr, &result.slr,
            error_buf, error_buf_size) ||
        !cetta_lp_native_slr_prepared_export_program(
            &plan->final_slr, &slr_program,
            error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_reachability(
            grammar, plan->start_state_id, &nonterminals, &terminals,
            &reachable_productions, &result.reachable_production_len,
            error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_zero_input_cycles(
            grammar, &nonterminals, reachable_productions,
            &result.zero_input_cycle_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    result.reachable_nonterminal_len = nonterminals.len;
    result.active_terminal_len = terminals.len;
    tag_languages = calloc(
        plan->combined_tag_len, sizeof(*tag_languages));
    tag_analyzed = calloc(
        plan->combined_tag_len, sizeof(*tag_analyzed));
    domains = calloc(
        terminals.len ? terminals.len : 1u, sizeof(*domains));
    if (!tag_languages || !tag_analyzed || !domains) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate cursor-certificate token analysis");
        goto done;
    }
    for (index = 0u; index < plan->combined_tag_len; index++)
        rsdfa_v1_tag_language_init(&tag_languages[index]);

    for (index = 0u; index < terminals.len; index++) {
        uint32_t terminal_id = terminals.data[index];
        uint32_t source_len = 0u;
        int32_t scalar_index = -1;
        int32_t tag_index = -1;
        int32_t slr_terminal_index = -1;
        uint32_t source_index;
        PPGuardedLexExecV1TokenDomain *domain = &domains[domain_len];

        domain->dfa_tag_index = UINT32_MAX;

        for (source_index = 0u;
             source_index < slr_program.terminal_len; source_index++) {
            if (slr_program.terminals[source_index] != terminal_id)
                continue;
            if (slr_terminal_index >= 0) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor certificate found a repeated SLR terminal");
                goto done;
            }
            slr_terminal_index = (int32_t)source_index;
        }
        if (slr_terminal_index < 0) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor certificate lost an active SLR terminal");
            goto done;
        }
        domain->slr_terminal_index = (uint32_t)slr_terminal_index;
        for (source_index = 0u;
             source_index < plan->scalar_terminals.len; source_index++) {
            if (plan->scalar_terminals.terminals[
                    source_index].terminal_id == terminal_id) {
                scalar_index = (int32_t)source_index;
                source_len++;
            }
        }
        for (source_index = 0u;
             source_index < plan->lexical_tag_len + plan->guarded_tag_len;
             source_index++) {
            if (plan->projected_tag_terminal_ids[source_index] ==
                    terminal_id) {
                tag_index = (int32_t)source_index;
                source_len++;
            }
        }
        if (source_len == 0u) {
            result.unmapped_terminal_len++;
            continue;
        }
        if (source_len != 1u) {
            result.duplicate_terminal_source_len++;
            continue;
        }
        if (scalar_index >= 0) {
            const CettaLpNativeUtf8Terminal *terminal =
                &plan->scalar_terminals.terminals[scalar_index];
            result.scalar_terminal_len++;
            switch (terminal->kind) {
            case CETTA_LP_NATIVE_UTF8_TERMINAL_ANY:
                domain->singleton = (CettaLpNativeUnicodeRange){
                    0u, UINT32_C(0x10ffff)};
                domain->ranges = &domain->singleton;
                domain->range_len = 1u;
                break;
            case CETTA_LP_NATIVE_UTF8_TERMINAL_EOF:
                domain->eof = true;
                break;
            case CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR:
                domain->singleton = (CettaLpNativeUnicodeRange){
                    terminal->scalar, terminal->scalar};
                domain->ranges = &domain->singleton;
                domain->range_len = 1u;
                break;
            case CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES:
                domain->ranges = terminal->ranges;
                domain->range_len = terminal->range_len;
                break;
            default:
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor certificate saw an unknown terminal kind");
                goto done;
            }
            if (!domain->eof && domain->range_len == 0u)
                result.empty_token_len++;
            domain_len++;
            continue;
        }
        if (tag_index < 0 ||
            (uint32_t)tag_index >=
                plan->lexical_tag_len + plan->guarded_tag_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor certificate lost a projected tag mapping");
            goto done;
        }
        if (!tag_analyzed[tag_index]) {
            if (!rsdfa_v1_plan_tag_language(
                    &plan->dfa, (uint32_t)tag_index,
                    &tag_languages[tag_index],
                    error_buf, error_buf_size)) {
                goto done;
            }
            tag_analyzed[tag_index] = true;
        }
        domain->ranges = tag_languages[tag_index].first_ranges;
        domain->range_len = tag_languages[tag_index].first_range_len;
        domain->dfa_tag_index = (uint32_t)tag_index;
        if ((uint32_t)tag_index < plan->lexical_tag_len) {
            const RSDFAV1TagLanguage *language =
                &tag_languages[tag_index];
            result.ordinary_span_tag_len++;
            domain->eof = language->accepts_eof_empty;
            if (language->is_empty)
                result.empty_token_len++;
            if (language->accepts_empty || language->accepts_eof_empty)
                result.nullable_token_len++;
            if (!language->prefix_free)
                result.non_prefix_free_token_len++;
        } else {
            uint32_t guarded_local =
                (uint32_t)tag_index - plan->lexical_tag_len;
            uint32_t guard_local;
            uint32_t guard_tag;
            const RSDFAV1TagLanguage *prefix =
                &tag_languages[tag_index];
            bool stops = false;

            result.guarded_span_tag_len++;
            if (prefix->is_empty)
                result.empty_token_len++;
            if (prefix->accepts_empty || prefix->accepts_eof_empty)
                result.nullable_token_len++;
            if (guarded_local >= plan->guarded_tag_len) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor certificate lost a guarded tag index");
                goto done;
            }
            guard_local = plan->guard_local_for_guarded[guarded_local];
            if (guard_local >= plan->guard_tag_len) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor certificate lost a guard-body index");
                goto done;
            }
            guard_tag = plan->lexical_tag_len + plan->guarded_tag_len +
                guard_local;
            if (!tag_analyzed[guard_tag]) {
                if (!rsdfa_v1_plan_tag_language(
                        &plan->dfa, guard_tag,
                        &tag_languages[guard_tag],
                        error_buf, error_buf_size)) {
                    goto done;
                }
                tag_analyzed[guard_tag] = true;
            }
            result.guard_lookahead_tag_len++;
            if (tag_languages[guard_tag].is_empty ||
                !tag_languages[guard_tag].one_scalar_or_eof) {
                result.unsupported_guard_len++;
            }
            if (!rsdfa_v1_plan_tag_stops_before(
                    &plan->dfa, (uint32_t)tag_index,
                    tag_languages[guard_tag].first_ranges,
                    tag_languages[guard_tag].first_range_len,
                    &stops, error_buf, error_buf_size)) {
                goto done;
            }
            if (!stops)
                result.boundary_crossing_len++;
        }
        if (!domain->eof && domain->range_len == 0u &&
            !tag_languages[tag_index].is_empty) {
            result.empty_token_len++;
        }
        domain_len++;
    }
    for (index = 0u; index < domain_len; index++) {
        uint32_t other;
        for (other = index + 1u; other < domain_len; other++) {
            uint32_t state;
            bool domains_overlap = false;
            bool coactive = false;
            if (!ppguarded_lex_exec_v1_token_domains_overlap(
                    plan, &domains[index], &domains[other],
                    &domains_overlap, error_buf, error_buf_size)) {
                goto done;
            }
            if (!domains_overlap) {
                continue;
            }
            for (state = 0u;
                 state < slr_program.summary.state_len; state++) {
                uint32_t columns = slr_program.terminal_len + 1u;
                const CettaLpNativeSlrProgramAction *left =
                    &slr_program.actions[
                        state * columns +
                        domains[index].slr_terminal_index];
                const CettaLpNativeSlrProgramAction *right =
                    &slr_program.actions[
                        state * columns +
                        domains[other].slr_terminal_index];
                if (left->kind != CETTA_LP_NATIVE_SLR_PROGRAM_ERROR &&
                    right->kind != CETTA_LP_NATIVE_SLR_PROGRAM_ERROR &&
                    (left->kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
                     right->kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
                     left->value != right->value)) {
                    coactive = true;
                    break;
                }
            }
            if (coactive)
                result.token_prefix_overlap_len++;
        }
    }
    if (result.slr.conflict_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_SLR_CONFLICT;
    if (result.slr.accept_len != 1u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_SLR_ACCEPT_SHAPE;
    if (result.zero_input_cycle_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_ZERO_INPUT_CYCLE;
    if (result.unmapped_terminal_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_UNMAPPED_TERMINAL;
    if (result.duplicate_terminal_source_len > 0u) {
        result.failure_mask |=
            PPGUARDED_LEX_CURSOR_V1_DUPLICATE_TERMINAL_SOURCE;
    }
    if (result.empty_token_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_EMPTY_TOKEN;
    if (result.nullable_token_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_NULLABLE_TOKEN;
    if (result.non_prefix_free_token_len > 0u) {
        result.failure_mask |=
            PPGUARDED_LEX_CURSOR_V1_NON_PREFIX_FREE_TOKEN;
    }
    if (result.unsupported_guard_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_UNSUPPORTED_GUARD;
    if (result.boundary_crossing_len > 0u)
        result.failure_mask |= PPGUARDED_LEX_CURSOR_V1_BOUNDARY_CROSSING;
    if (result.token_prefix_overlap_len > 0u) {
        result.failure_mask |=
            PPGUARDED_LEX_CURSOR_V1_TOKEN_PREFIX_OVERLAP;
    }
    result.eligible = result.failure_mask == 0u;
    ppguarded_lex_exec_v1_cursor_certificate_digest(
        plan, &result, result.digest);
    *out = result;
    ok = true;

done:
    if (tag_languages) {
        for (index = 0u; index < plan->combined_tag_len; index++)
            rsdfa_v1_tag_language_free(&tag_languages[index]);
    }
    free(domains);
    free(tag_analyzed);
    free(tag_languages);
    free(reachable_productions);
    cetta_lp_native_slr_program_free(&slr_program);
    free(terminals.data);
    free(nonterminals.data);
    return ok;
}

void ppguarded_lex_exec_v1_plan_init(PPGuardedLexExecV1Plan *plan) {
    if (!plan)
        return;
    memset(plan, 0, sizeof(*plan));
    rsdfa_v1_plan_init(&plan->dfa);
    cetta_lp_native_slr_prepared_init(&plan->final_slr);
    cetta_lp_native_gll_prepared_init(&plan->final_gll);
    cetta_lp_native_glr_prepared_init(&plan->final_glr);
    ppnative_v1_prepared_start_family_init(
        &plan->ordinary_witness_parsers);
    ppnative_v1_prepared_start_family_init(
        &plan->guard_witness_parsers);
    ppnative_v1_prepared_start_family_init(
        &plan->guarded_witness_parsers);
}

void ppguarded_lex_exec_v1_plan_free(PPGuardedLexExecV1Plan *plan) {
    if (!plan)
        return;
    free(plan->terminals);
    free(plan->projected_tag_terminal_ids);
    free(plan->guard_local_for_guarded);
    ppnative_v1_prepared_start_family_free(
        &plan->guarded_witness_parsers);
    ppnative_v1_prepared_start_family_free(
        &plan->guard_witness_parsers);
    ppnative_v1_prepared_start_family_free(
        &plan->ordinary_witness_parsers);
    cetta_lp_native_glr_prepared_free(&plan->final_glr);
    cetta_lp_native_gll_prepared_free(&plan->final_gll);
    cetta_lp_native_slr_prepared_free(&plan->final_slr);
    ppnative_v1_terminal_plan_free(&plan->scalar_terminals);
    rsdfa_v1_plan_free(&plan->dfa);
    memset(plan, 0, sizeof(*plan));
}

static void ppguarded_lex_exec_v1_union_free(
    PPGuardedLexExecV1NfaUnion *combined) {
    if (!combined)
        return;
    free(combined->starts);
    free(combined->edges);
    free(combined->accepts);
    memset(combined, 0, sizeof(*combined));
}

static bool ppguarded_lex_exec_v1_union_build(
    const RSNFAV1Plan *const *plans,
    uint32_t plan_len,
    PPGuardedLexExecV1NfaUnion *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexExecV1NfaUnion result = {0};
    uint32_t state_len = 0u;
    uint32_t start_len = 0u;
    uint32_t edge_len = 0u;
    uint32_t accept_len = 0u;
    uint32_t tag_len = 0u;
    uint32_t plan_index;
    uint32_t start_write = 0u;
    uint32_t edge_write = 0u;
    uint32_t accept_write = 0u;
    uint32_t state_offset = 0u;
    uint32_t tag_offset = 0u;
    bool ok = false;

    if (!plans || plan_len == 0u || !out) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size, "bad NFA union inputs");
        return false;
    }
    for (plan_index = 0u; plan_index < plan_len; plan_index++) {
        const RSDFAV1Nfa *nfa;
        if (!plans[plan_index])
            goto overflow;
        nfa = &plans[plan_index]->nfa;
        if (nfa->state_len > UINT32_MAX - state_len ||
            nfa->start_len > UINT32_MAX - start_len ||
            nfa->edge_len > UINT32_MAX - edge_len ||
            nfa->accept_len > UINT32_MAX - accept_len ||
            nfa->tag_len > UINT32_MAX - tag_len) {
            goto overflow;
        }
        state_len += nfa->state_len;
        start_len += nfa->start_len;
        edge_len += nfa->edge_len;
        accept_len += nfa->accept_len;
        tag_len += nfa->tag_len;
    }
    if (state_len == 0u || start_len == 0u ||
        accept_len == 0u || tag_len == 0u) {
        goto overflow;
    }
    result.starts = malloc(sizeof(*result.starts) * (size_t)start_len);
    result.edges = calloc(
        edge_len ? edge_len : 1u, sizeof(*result.edges));
    result.accepts = malloc(
        sizeof(*result.accepts) * (size_t)accept_len);
    if (!result.starts || !result.edges || !result.accepts)
        goto done;
    for (plan_index = 0u; plan_index < plan_len; plan_index++) {
        const RSDFAV1Nfa *nfa = &plans[plan_index]->nfa;
        uint32_t index;
        for (index = 0u; index < nfa->start_len; index++) {
            result.starts[start_write++] =
                state_offset + nfa->start_states[index];
        }
        for (index = 0u; index < nfa->edge_len; index++) {
            result.edges[edge_write] = nfa->edges[index];
            result.edges[edge_write].from += state_offset;
            result.edges[edge_write].to += state_offset;
            edge_write++;
        }
        for (index = 0u; index < nfa->accept_len; index++) {
            result.accepts[accept_write] = nfa->accepts[index];
            result.accepts[accept_write].state += state_offset;
            result.accepts[accept_write].tag += tag_offset;
            accept_write++;
        }
        state_offset += nfa->state_len;
        tag_offset += nfa->tag_len;
    }
    result.nfa = (RSDFAV1Nfa){
        .state_len = state_len,
        .start_states = result.starts,
        .start_len = start_len,
        .edges = result.edges,
        .edge_len = edge_len,
        .accepts = result.accepts,
        .accept_len = accept_len,
        .tag_len = tag_len,
    };
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;
    goto done;

overflow:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "regular-span NFA union is empty or overflows v1 indices");

done:
    ppguarded_lex_exec_v1_union_free(&result);
    return ok;
}

static int32_t ppguarded_lex_exec_v1_lexical_entry(
    const PPLexV1Plan *plan, uint32_t tag_index) {
    uint32_t index;
    for (index = 0u; index < plan->entry_len; index++) {
        if (plan->entries[index].tag_index == tag_index)
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppguarded_lex_exec_v1_guarded_entry(
    const PPGuardedLexV1Plan *plan, uint32_t tag_index) {
    uint32_t index;
    for (index = 0u; index < plan->entry_len; index++) {
        if (plan->entries[index].tag_index == tag_index)
            return (int32_t)index;
    }
    return -1;
}

static int32_t ppguarded_lex_exec_v1_tag_find(
    Atom *const *tags, uint32_t tag_len, const Atom *tag) {
    uint32_t index;
    int32_t found = -1;
    for (index = 0u; index < tag_len; index++) {
        if (!atom_eq(tags[index], (Atom *)tag))
            continue;
        if (found >= 0)
            return -1;
        found = (int32_t)index;
    }
    return found;
}

static bool ppguarded_lex_exec_v1_guarded_witness_family_prepare(
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    PPNativeV1PreparedStartFamily *family,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGrammar grammar;
    uint32_t *start_state_ids = NULL;
    uint32_t index;
    bool ok = false;

    cetta_lp_native_grammar_init(&grammar);
    if (!pack || !lexical_plan || !guard_plan || !guarded_plan ||
        !family || guarded_plan->entry_len == 0u ||
        !guarded_plan->entries ||
        (size_t)guarded_plan->entry_len >
            SIZE_MAX / sizeof(*start_state_ids)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad guarded witness family inputs");
        goto done;
    }
    start_state_ids = malloc(
        sizeof(*start_state_ids) * (size_t)guarded_plan->entry_len);
    if (!start_state_ids ||
        !ppguard_plan_v1_grammar_build(
            pack, lexical_plan, guard_plan, &grammar,
            error_buf, error_buf_size)) {
        goto done;
    }
    for (index = 0u; index < guarded_plan->entry_len; index++)
        start_state_ids[index] = guarded_plan->entries[index].state_id;
    if (!ppnative_v1_prepared_start_family_prepare(
            family, &grammar, guarded_plan->plan_digest,
            start_state_ids, guarded_plan->entry_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    ok = true;

done:
    free(start_state_ids);
    cetta_lp_native_grammar_free(&grammar);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to prepare guarded witness family");
    }
    return ok;
}

static bool ppguarded_lex_exec_v1_plan_matches(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan) {
    char digest[65];
    char cursor_digest[65];
    uint32_t index;

    if (!pack || !start_state || !lexical_plan || !guard_plan || !guarded_plan ||
        !exec_plan || !exec_plan->dfa.impl ||
        (exec_plan->terminal_len > 0u && !exec_plan->terminals) ||
        !exec_plan->final_slr.implementation ||
        !exec_plan->final_gll.implementation ||
        !exec_plan->final_glr.implementation ||
        !exec_plan->start_state ||
        !atom_eq(exec_plan->start_state, (Atom *)start_state) ||
        exec_plan->final_parser_table_build_count != 1u ||
        exec_plan->ordinary_witness_parser_family_build_count != 1u ||
        exec_plan->ordinary_witness_prepared_start_len == 0u ||
        exec_plan->ordinary_witness_prepared_start_len >
            lexical_plan->entry_len ||
        (guard_plan->entry_len > 0u
         ? (exec_plan->guard_witness_parser_family_build_count != 1u ||
            exec_plan->guard_witness_prepared_start_len == 0u ||
            exec_plan->guard_witness_prepared_start_len >
                guard_plan->entry_len)
         : (exec_plan->guard_witness_parser_family_build_count != 0u ||
            exec_plan->guard_witness_prepared_start_len != 0u)) ||
        (guarded_plan->entry_len > 0u
         ? (exec_plan->guarded_witness_parser_family_build_count != 1u ||
            exec_plan->guarded_witness_prepared_start_len == 0u ||
            exec_plan->guarded_witness_prepared_start_len >
                guarded_plan->entry_len)
         : (exec_plan->guarded_witness_parser_family_build_count != 0u ||
            exec_plan->guarded_witness_prepared_start_len != 0u)) ||
        ppnative_v1_prepared_start_family_build_count(
            &exec_plan->ordinary_witness_parsers) != 1u ||
        ppnative_v1_prepared_start_family_len(
            &exec_plan->ordinary_witness_parsers) !=
            exec_plan->ordinary_witness_prepared_start_len ||
        ppnative_v1_prepared_start_family_build_count(
            &exec_plan->guard_witness_parsers) !=
            exec_plan->guard_witness_parser_family_build_count ||
        ppnative_v1_prepared_start_family_len(
            &exec_plan->guard_witness_parsers) !=
            exec_plan->guard_witness_prepared_start_len ||
        ppnative_v1_prepared_start_family_build_count(
            &exec_plan->guarded_witness_parsers) !=
            exec_plan->guarded_witness_parser_family_build_count ||
        ppnative_v1_prepared_start_family_len(
            &exec_plan->guarded_witness_parsers) !=
            exec_plan->guarded_witness_prepared_start_len ||
        !exec_plan->projected_tag_terminal_ids ||
        (exec_plan->guarded_tag_len > 0u &&
         !exec_plan->guard_local_for_guarded) ||
        (exec_plan->guard_tag_len > 0u && !exec_plan->guard_tags) ||
        exec_plan->lexical_tag_len != lexical_plan->entry_len ||
        exec_plan->guarded_tag_len != guarded_plan->entry_len ||
        exec_plan->guard_tag_len != guard_plan->entry_len ||
        exec_plan->guarded_tag_len != exec_plan->guard_tag_len ||
        exec_plan->combined_tag_len != exec_plan->lexical_tag_len +
            exec_plan->guarded_tag_len + exec_plan->guard_tag_len ||
        exec_plan->dfa.tag_len != exec_plan->combined_tag_len ||
        exec_plan->terminal_len !=
            guard_plan->terminal_len + guarded_plan->entry_len ||
        strcmp(exec_plan->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(exec_plan->lexical_plan_digest,
               lexical_plan->plan_digest) != 0 ||
        strcmp(exec_plan->guard_plan_digest, guard_plan->plan_digest) != 0 ||
        strcmp(exec_plan->guarded_plan_digest,
               guarded_plan->plan_digest) != 0) {
        return false;
    }
    for (index = 0u; index < exec_plan->terminal_len; index++) {
        const PPNativeV1TerminalExtension *expected =
            index < guard_plan->terminal_len
            ? &guard_plan->terminals[index]
            : &guarded_plan->terminals[index - guard_plan->terminal_len];
        if (exec_plan->terminals[index].terminal_id !=
                pack->terminal_len + index ||
            exec_plan->terminals[index].terminal_id !=
                expected->terminal_id ||
            !atom_eq(exec_plan->terminals[index].identity,
                     expected->identity)) {
            return false;
        }
    }
    for (index = 0u; index < exec_plan->lexical_tag_len; index++) {
        if (exec_plan->projected_tag_terminal_ids[index] !=
            lexical_plan->tag_terminal_ids[index]) {
            return false;
        }
    }
    for (index = 0u; index < exec_plan->guarded_tag_len; index++) {
        int32_t entry_index = ppguarded_lex_exec_v1_guarded_entry(
            guarded_plan, index);
        uint32_t guard_entry;
        uint32_t guard_local = exec_plan->guard_local_for_guarded[index];

        if (entry_index < 0)
            return false;
        guard_entry = guarded_plan->entries[entry_index].guard_entry_index;
        if (exec_plan->projected_tag_terminal_ids[
                exec_plan->lexical_tag_len + index] !=
                guarded_plan->tag_terminal_ids[index] ||
            guard_local >= exec_plan->guard_tag_len ||
            guard_entry >= guard_plan->entry_len ||
            !atom_eq(exec_plan->guard_tags[guard_local],
                     guard_plan->entries[guard_entry].tag)) {
            return false;
        }
    }
    ppguarded_lex_exec_v1_plan_digest(exec_plan, digest);
    ppguarded_lex_exec_v1_cursor_certificate_digest(
        exec_plan, &exec_plan->cursor_certificate, cursor_digest);
    return strcmp(digest, exec_plan->execution_plan_digest) == 0 &&
        strcmp(cursor_digest, exec_plan->cursor_certificate.digest) == 0;
}

void ppguarded_lex_cursor_v1_program_init(
    PPGuardedLexCursorV1Program *program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
    rsdfa_v1_program_init(&program->dfa);
    cetta_lp_native_slr_program_init(&program->final_slr);
    pp_action_bytecode_v1_program_init(&program->actions);
}

static void ppguarded_lex_cursor_v1_value_program_init(
    PPGuardedLexCursorV1ValueProgram *program) {
    if (!program)
        return;
    memset(program, 0, sizeof(*program));
    cetta_lp_native_slr_program_init(&program->slr);
}

static void ppguarded_lex_cursor_v1_value_program_free(
    PPGuardedLexCursorV1ValueProgram *program) {
    if (!program)
        return;
    free(program->scalar_dispatch_shift_sources);
    free(program->scalar_dispatch_actions);
    free(program->scalar_dispatch_ranges);
    free(program->state_candidate_terminal_indices);
    free(program->state_candidate_offsets);
    free(program->production_lhs_indices);
    free(program->terminals);
    free(program->ranges);
    cetta_lp_native_slr_program_free(&program->slr);
    memset(program, 0, sizeof(*program));
}

void ppguarded_lex_cursor_v1_program_free(
    PPGuardedLexCursorV1Program *program) {
    uint32_t index;

    if (!program)
        return;
    for (index = 0u; index < program->value_program_len; index++) {
        ppguarded_lex_cursor_v1_value_program_free(
            &program->value_programs[index]);
    }
    free(program->value_programs);
    free(program->final_production_lhs_indices);
    free(program->terminals);
    free(program->ranges);
    pp_action_bytecode_v1_program_free(&program->actions);
    cetta_lp_native_slr_program_free(&program->final_slr);
    rsdfa_v1_program_free(&program->dfa);
    memset(program, 0, sizeof(*program));
}

static bool ppguarded_lex_cursor_v1_digest_valid(const char *digest) {
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

static bool ppguarded_lex_cursor_v1_range_valid(
    const CettaLpNativeUnicodeRange *range) {
    return range && range->low <= range->high &&
        range->high <= UINT32_C(0x10ffff) &&
        !(range->low <= UINT32_C(0xdfff) &&
          range->high >= UINT32_C(0xd800));
}

static void ppguarded_lex_cursor_v1_sha_slr_program(
    CettaNativeSha256 *sha,
    const CettaLpNativeSlrProgram *program) {
    uint32_t index;

    ppguarded_lex_exec_v1_sha_u32(sha, program->start_nonterminal);
    ppguarded_lex_exec_v1_sha_u32(sha, program->terminal_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->nonterminal_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->production_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->authored_production_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->rhs_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->action_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->goto_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->summary.state_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->summary.shift_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->summary.goto_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->summary.reduce_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->summary.accept_len);
    ppguarded_lex_exec_v1_sha_u32(sha, program->summary.conflict_len);
    for (index = 0u; index < program->terminal_len; index++)
        ppguarded_lex_exec_v1_sha_u32(sha, program->terminals[index]);
    for (index = 0u; index < program->nonterminal_len; index++)
        ppguarded_lex_exec_v1_sha_u32(sha, program->nonterminals[index]);
    for (index = 0u; index < program->production_len; index++) {
        const CettaLpNativeSlrProgramProduction *production =
            &program->productions[index];
        ppguarded_lex_exec_v1_sha_u32(sha, production->label);
        ppguarded_lex_exec_v1_sha_u32(sha, production->lhs);
        ppguarded_lex_exec_v1_sha_u32(sha, production->rhs_begin);
        ppguarded_lex_exec_v1_sha_u32(sha, production->rhs_len);
        ppguarded_lex_exec_v1_sha_u32(
            sha, production->authored ? 1u : 0u);
    }
    for (index = 0u; index < program->rhs_len; index++) {
        const CettaLpNativeSymbol *symbol = &program->rhs[index];
        ppguarded_lex_exec_v1_sha_u32(sha, (uint32_t)symbol->kind);
        ppguarded_lex_exec_v1_sha_u32(sha, symbol->name);
        ppguarded_lex_exec_v1_sha_u32(sha, symbol->scope);
    }
    for (index = 0u; index < program->action_len; index++) {
        const CettaLpNativeSlrProgramAction *action =
            &program->actions[index];
        ppguarded_lex_exec_v1_sha_u32(sha, (uint32_t)action->kind);
        ppguarded_lex_exec_v1_sha_u32(sha, (uint32_t)action->value);
    }
    for (index = 0u; index < program->goto_len; index++)
        ppguarded_lex_exec_v1_sha_u32(sha, program->gotos[index]);
}

static void ppguarded_lex_cursor_v1_program_digest(
    const PPGuardedLexCursorV1Program *program,
    char out[65]) {
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    ppguarded_lex_exec_v1_sha_text(
        &sha, "ParserPackGuardedLexicalCursorProgramV1\n");
    ppguarded_lex_exec_v1_sha_text(&sha, program->base_pack_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, program->lexical_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, program->guard_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, program->guarded_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, program->execution_plan_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    ppguarded_lex_exec_v1_sha_text(&sha, program->certificate_digest);
    ppguarded_lex_exec_v1_sha_text(&sha, "\n");

    ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.state_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.transition_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.accept_tag_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.eof_accept_tag_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.start_state);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.tag_len);
    for (index = 0u; index < program->dfa.state_len; index++) {
        const RSDFAV1ProgramState *state = &program->dfa.states[index];
        ppguarded_lex_exec_v1_sha_u32(&sha, state->transition_begin);
        ppguarded_lex_exec_v1_sha_u32(&sha, state->transition_len);
        ppguarded_lex_exec_v1_sha_u32(&sha, state->accept_begin);
        ppguarded_lex_exec_v1_sha_u32(&sha, state->accept_len);
        ppguarded_lex_exec_v1_sha_u32(&sha, state->eof_accept_begin);
        ppguarded_lex_exec_v1_sha_u32(&sha, state->eof_accept_len);
    }
    for (index = 0u; index < program->dfa.transition_len; index++) {
        const RSDFAV1ProgramTransition *transition =
            &program->dfa.transitions[index];
        ppguarded_lex_exec_v1_sha_u32(&sha, transition->low);
        ppguarded_lex_exec_v1_sha_u32(&sha, transition->high);
        ppguarded_lex_exec_v1_sha_u32(&sha, transition->target);
    }
    for (index = 0u; index < program->dfa.accept_tag_len; index++)
        ppguarded_lex_exec_v1_sha_u32(&sha, program->dfa.accept_tags[index]);
    for (index = 0u; index < program->dfa.eof_accept_tag_len; index++) {
        ppguarded_lex_exec_v1_sha_u32(
            &sha, program->dfa.eof_accept_tags[index]);
    }

    ppguarded_lex_cursor_v1_sha_slr_program(&sha, &program->final_slr);

    ppguarded_lex_exec_v1_sha_u32(&sha, program->terminal_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->range_len);
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->terminal_id);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->slr_terminal_index);
        ppguarded_lex_exec_v1_sha_u32(&sha, (uint32_t)terminal->kind);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, (uint32_t)terminal->scalar_kind);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->scalar);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->scalar_range_begin);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->scalar_range_len);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->dfa_tag);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->guard_dfa_tag);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->guard_range_begin);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->guard_range_len);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->guard_accepts_eof ? 1u : 0u);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->semantic_value_required ? 1u : 0u);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->semantic_start_state_id);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->semantic_program_index);
    }
    for (index = 0u; index < program->range_len; index++) {
        ppguarded_lex_exec_v1_sha_u32(&sha, program->ranges[index].low);
        ppguarded_lex_exec_v1_sha_u32(&sha, program->ranges[index].high);
    }
    ppguarded_lex_exec_v1_sha_u32(
        &sha, program->value_programs_complete ? 1u : 0u);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, program->value_program_conflict_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->value_program_len);
    for (index = 0u; index < program->value_program_len; index++) {
        ppguarded_lex_exec_v1_sha_text(
            &sha, program->value_programs[index].program_digest);
        ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    }
    if (program->actions_bound) {
        ppguarded_lex_exec_v1_sha_text(
            &sha, "ParserActionBindingV1\n");
        ppguarded_lex_exec_v1_sha_text(
            &sha, program->actions.program_digest);
        ppguarded_lex_exec_v1_sha_text(&sha, "\n");
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

static bool ppguarded_lex_cursor_v1_range_slice_valid(
    const PPGuardedLexCursorV1Program *program,
    uint32_t begin,
    uint32_t len,
    uint32_t expected_begin) {
    uint32_t index;

    if (begin != expected_begin || begin > program->range_len ||
        len > program->range_len - begin ||
        (len > 0u && !program->ranges)) {
        return false;
    }
    for (index = 0u; index < len; index++) {
        const CettaLpNativeUnicodeRange *range =
            &program->ranges[begin + index];
        if (!ppguarded_lex_cursor_v1_range_valid(range) ||
            (index > 0u &&
             program->ranges[begin + index - 1u].high >= range->low)) {
            return false;
        }
    }
    return true;
}

static bool ppguarded_lex_cursor_v1_slr_terminal_shifts(
    const CettaLpNativeSlrProgram *program,
    uint32_t terminal_index) {
    uint32_t columns;
    uint32_t state;

    if (!program || terminal_index >= program->terminal_len)
        return false;
    columns = program->terminal_len + 1u;
    for (state = 0u; state < program->summary.state_len; state++) {
        const CettaLpNativeSlrProgramAction *action =
            &program->actions[state * columns + terminal_index];
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT)
            return true;
    }
    return false;
}

static int32_t ppguarded_lex_cursor_v1_nonterminal_find(
    const CettaLpNativeSlrProgram *program,
    uint32_t nonterminal_id) {
    uint32_t index;

    for (index = 0u; index < program->nonterminal_len; index++) {
        if (program->nonterminals[index] == nonterminal_id)
            return (int32_t)index;
    }
    return -1;
}

static bool ppguarded_lex_cursor_v1_production_lhs_indices_build(
    const CettaLpNativeSlrProgram *program,
    uint32_t **out,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *indices = NULL;
    uint32_t index;

    if (out)
        *out = NULL;
    if (!program || !out || program->production_len == 0u ||
        (size_t)program->production_len > SIZE_MAX / sizeof(*indices)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad SLR production-column index inputs");
        return false;
    }
    indices = malloc(sizeof(*indices) * program->production_len);
    if (!indices) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate SLR production-column indexes");
        return false;
    }
    for (index = 0u; index < program->production_len; index++) {
        int32_t found = ppguarded_lex_cursor_v1_nonterminal_find(
            program, program->productions[index].lhs);
        if (found < 0) {
            free(indices);
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "SLR production lhs has no dense nonterminal column");
            return false;
        }
        indices[index] = (uint32_t)found;
    }
    *out = indices;
    return true;
}

static bool ppguarded_lex_cursor_v1_production_lhs_indices_validate(
    const CettaLpNativeSlrProgram *program,
    const uint32_t *indices) {
    uint32_t index;

    if (!program || (program->production_len > 0u && !indices))
        return false;
    for (index = 0u; index < program->production_len; index++) {
        uint32_t lhs_index = indices[index];
        if (lhs_index >= program->nonterminal_len ||
            program->nonterminals[lhs_index] !=
                program->productions[index].lhs) {
            return false;
        }
    }
    return true;
}

static void ppguarded_lex_cursor_v1_value_program_digest(
    const PPGuardedLexCursorV1ValueProgram *program,
    char out[65]) {
    static const char domain[] =
        "ParserPackGuardedLexicalCursorValueProgramV1\n";
    CettaNativeSha256 sha;
    uint32_t index;

    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(
        &sha, (const uint8_t *)domain, sizeof(domain) - 1u);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->start_state_id);
    ppguarded_lex_exec_v1_sha_u32(
        &sha, program->guarded ? 1u : 0u);
    ppguarded_lex_cursor_v1_sha_slr_program(&sha, &program->slr);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->terminal_len);
    ppguarded_lex_exec_v1_sha_u32(&sha, program->range_len);
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &program->terminals[index];
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->terminal_id);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->slr_terminal_index);
        ppguarded_lex_exec_v1_sha_u32(&sha, (uint32_t)terminal->kind);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, (uint32_t)terminal->scalar_kind);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->scalar);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->range_begin);
        ppguarded_lex_exec_v1_sha_u32(&sha, terminal->range_len);
        ppguarded_lex_exec_v1_sha_u32(
            &sha, terminal->guard_value_program_index);
    }
    for (index = 0u; index < program->range_len; index++) {
        ppguarded_lex_exec_v1_sha_u32(&sha, program->ranges[index].low);
        ppguarded_lex_exec_v1_sha_u32(&sha, program->ranges[index].high);
    }
    cetta_native_sha256_finish_hex(&sha, out);
}

static bool ppguarded_lex_cursor_v1_value_range_slice_valid(
    const PPGuardedLexCursorV1ValueProgram *program,
    uint32_t begin,
    uint32_t len,
    uint32_t expected_begin) {
    uint32_t index;

    if (begin != expected_begin || begin > program->range_len ||
        len > program->range_len - begin ||
        (len > 0u && !program->ranges)) {
        return false;
    }
    for (index = 0u; index < len; index++) {
        const CettaLpNativeUnicodeRange *range =
            &program->ranges[begin + index];
        if (!ppguarded_lex_cursor_v1_range_valid(range) ||
            (index > 0u &&
             program->ranges[begin + index - 1u].high >= range->low)) {
            return false;
        }
    }
    return true;
}

static bool ppguarded_lex_cursor_v1_range_contains(
    const CettaLpNativeUnicodeRange *ranges,
    uint32_t range_len,
    uint32_t scalar);

static bool ppguarded_lex_cursor_v1_value_terminal_matches_scalar(
    const PPGuardedLexCursorV1ValueProgram *program,
    const PPGuardedLexCursorV1ValueTerminal *terminal,
    uint32_t scalar);

static bool ppguarded_lex_cursor_v1_value_scalar_dispatch_cell(
    const PPGuardedLexCursorV1ValueProgram *program,
    uint32_t state,
    uint32_t scalar,
    CettaLpNativeSlrProgramAction *out_action,
    uint32_t *out_shift_source,
    char *error_buf,
    size_t error_buf_size);

static bool ppguarded_lex_cursor_v1_value_program_validate(
    const PPGuardedLexCursorV1Program *owner,
    uint32_t program_index,
    char *error_buf,
    size_t error_buf_size) {
    const PPGuardedLexCursorV1ValueProgram *program;
    char digest[65];
    uint32_t range_end = 0u;
    uint32_t index;
    uint32_t source_index = 0u;
    uint32_t state;

    if (!owner || program_index >= owner->value_program_len)
        return false;
    program = &owner->value_programs[program_index];
    if (!ppguarded_lex_cursor_v1_digest_valid(program->program_digest) ||
        !cetta_lp_native_slr_program_validate(
            &program->slr, error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_production_lhs_indices_validate(
            &program->slr, program->production_lhs_indices) ||
        program->start_state_id != program->slr.start_nonterminal ||
        program->terminal_len > program->slr.terminal_len ||
        (program->terminal_len > 0u && !program->terminals) ||
        (program->range_len > 0u && !program->ranges) ||
        !program->state_candidate_offsets ||
        (program->state_candidate_len > 0u &&
         !program->state_candidate_terminal_indices) ||
        program->scalar_dispatch_range_len == 0u ||
        !program->scalar_dispatch_ranges ||
        !program->scalar_dispatch_actions ||
        !program->scalar_dispatch_shift_sources) {
        return false;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &program->terminals[index];
        if (terminal->slr_terminal_index >= program->slr.terminal_len ||
            (index > 0u &&
             program->terminals[index - 1u].slr_terminal_index >=
                 terminal->slr_terminal_index) ||
            terminal->terminal_id !=
                program->slr.terminals[terminal->slr_terminal_index] ||
            !ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &program->slr, terminal->slr_terminal_index)) {
            return false;
        }
        if (terminal->kind ==
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
            if (terminal->scalar_kind >
                    CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES ||
                (terminal->scalar_kind !=
                     CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
                 terminal->scalar != 0u) ||
                terminal->guard_value_program_index != UINT32_MAX) {
                return false;
            }
            if (terminal->scalar_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
                if (terminal->range_len == 0u ||
                    !ppguarded_lex_cursor_v1_value_range_slice_valid(
                        program, terminal->range_begin,
                        terminal->range_len, range_end)) {
                    return false;
                }
                range_end += terminal->range_len;
            } else if (terminal->range_len != 0u ||
                       terminal->range_begin != range_end ||
                       (terminal->scalar_kind ==
                            CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
                        (terminal->scalar > UINT32_C(0x10ffff) ||
                         (terminal->scalar >= UINT32_C(0xd800) &&
                          terminal->scalar <= UINT32_C(0xdfff))))) {
                return false;
            }
            continue;
        }
        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_GUARD ||
            !program->guarded ||
            terminal->scalar_kind != CETTA_LP_NATIVE_UTF8_TERMINAL_ANY ||
            terminal->scalar != 0u ||
            terminal->range_begin != range_end ||
            terminal->range_len != 0u ||
            terminal->guard_value_program_index >= owner->value_program_len ||
            terminal->guard_value_program_index == program_index ||
            owner->value_programs[
                terminal->guard_value_program_index].guarded) {
            return false;
        }
    }
    for (index = 0u; index < program->slr.terminal_len; index++) {
        if (!ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &program->slr, index)) {
            continue;
        }
        if (source_index >= program->terminal_len ||
            program->terminals[source_index].slr_terminal_index != index) {
            return false;
        }
        source_index++;
    }
    if (source_index != program->terminal_len ||
        range_end != program->range_len) {
        return false;
    }
    if (program->state_candidate_offsets[0] != 0u ||
        program->state_candidate_offsets[
            program->slr.summary.state_len] !=
            program->state_candidate_len) {
        return false;
    }
    for (state = 0u; state < program->slr.summary.state_len; state++) {
        uint32_t candidate = program->state_candidate_offsets[state];
        uint32_t candidate_end =
            program->state_candidate_offsets[state + 1u];
        uint32_t columns = program->slr.terminal_len + 1u;

        if (candidate > candidate_end ||
            candidate_end > program->state_candidate_len) {
            return false;
        }
        for (source_index = 0u; source_index < program->terminal_len;
             source_index++) {
            const PPGuardedLexCursorV1ValueTerminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &program->slr.actions[
                    state * columns + terminal->slr_terminal_index];

            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
                continue;
            if (candidate >= candidate_end ||
                program->state_candidate_terminal_indices[candidate] !=
                    source_index) {
                return false;
            }
            candidate++;
        }
        if (candidate != candidate_end)
            return false;
    }
    if (program->scalar_dispatch_ranges[0].low != 0u ||
        program->scalar_dispatch_ranges[
            program->scalar_dispatch_range_len - 1u].high !=
            UINT32_C(0x10ffff)) {
        return false;
    }
    for (index = 0u; index < program->scalar_dispatch_range_len; index++) {
        const CettaLpNativeUnicodeRange *range =
            &program->scalar_dispatch_ranges[index];

        if (!ppguarded_lex_cursor_v1_range_valid(range) ||
            (index > 0u &&
             program->scalar_dispatch_ranges[index - 1u].high + 1u !=
                 range->low &&
             !(program->scalar_dispatch_ranges[index - 1u].high ==
                   UINT32_C(0xd7ff) &&
               range->low == UINT32_C(0xe000)))) {
            return false;
        }
    }
    for (source_index = 0u; source_index < program->terminal_len;
         source_index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &program->terminals[source_index];
        uint32_t local_index;
        bool found_low;
        bool found_high;

        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
            continue;
        }
        if (terminal->scalar_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR) {
            found_low = false;
            for (index = 0u;
                 index < program->scalar_dispatch_range_len; index++) {
                if (program->scalar_dispatch_ranges[index].low ==
                        terminal->scalar &&
                    program->scalar_dispatch_ranges[index].high ==
                        terminal->scalar) {
                    found_low = true;
                    break;
                }
            }
            if (!found_low)
                return false;
        } else if (terminal->scalar_kind ==
                       CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
            for (local_index = 0u;
                 local_index < terminal->range_len; local_index++) {
                const CettaLpNativeUnicodeRange *terminal_range =
                    &program->ranges[
                        terminal->range_begin + local_index];
                found_low = false;
                found_high = false;
                for (index = 0u;
                     index < program->scalar_dispatch_range_len; index++) {
                    found_low = found_low ||
                        program->scalar_dispatch_ranges[index].low ==
                            terminal_range->low;
                    found_high = found_high ||
                        program->scalar_dispatch_ranges[index].high ==
                            terminal_range->high;
                }
                if (!found_low || !found_high)
                    return false;
            }
        }
    }
    for (state = 0u; state < program->slr.summary.state_len; state++) {
        for (index = 0u; index < program->scalar_dispatch_range_len;
             index++) {
            CettaLpNativeSlrProgramAction expected;
            uint32_t expected_shift;
            size_t cell = (size_t)state *
                    program->scalar_dispatch_range_len +
                index;

            if (!ppguarded_lex_cursor_v1_value_scalar_dispatch_cell(
                    program, state,
                    program->scalar_dispatch_ranges[index].low,
                    &expected, &expected_shift,
                    error_buf, error_buf_size) ||
                program->scalar_dispatch_actions[cell].kind !=
                    expected.kind ||
                program->scalar_dispatch_actions[cell].value !=
                    expected.value ||
                program->scalar_dispatch_shift_sources[cell] !=
                    expected_shift) {
                return false;
            }
        }
    }
    ppguarded_lex_cursor_v1_value_program_digest(program, digest);
    return strcmp(digest, program->program_digest) == 0;
}

static int32_t ppguarded_lex_cursor_v1_terminal_source_index(
    const PPGuardedLexCursorV1Program *program,
    uint32_t terminal) {
    uint32_t index;

    if (!program)
        return -1;
    for (index = 0u; index < program->terminal_len; index++) {
        if (program->terminals[index].terminal_id == terminal)
            return (int32_t)index;
    }
    return -1;
}

/*
 * Compute the least semantic dependency closure from the final start value.
 * Action bytecode is a pure term algebra: PUSH_SLOT is its only operation
 * that observes a child value.  Consequently a terminal outside this closure
 * may be replaced by any well-formed neutral value without changing the
 * accepted start value.  Recognition and every reduction occurrence remain
 * untouched.
 */
static bool ppguarded_lex_cursor_v1_semantic_requirements(
    const PPGuardedLexCursorV1Program *program,
    bool *terminal_required,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrProgram *slr;
    bool *nonterminal_required = NULL;
    int32_t start_index;
    bool changed = true;
    uint32_t production_index;
    bool ok = false;

    if (!program || !program->actions_bound ||
        (program->terminal_len > 0u && !terminal_required)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor semantic-dependency inputs");
        return false;
    }
    slr = &program->final_slr;
    if (slr->nonterminal_len == 0u ||
        (size_t)slr->nonterminal_len >
            SIZE_MAX / sizeof(*nonterminal_required)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor semantic dependency has no nonterminal domain");
        return false;
    }
    memset(
        terminal_required, 0,
        sizeof(*terminal_required) * (size_t)program->terminal_len);
    nonterminal_required = calloc(
        slr->nonterminal_len, sizeof(*nonterminal_required));
    if (!nonterminal_required)
        return false;
    start_index = ppguarded_lex_cursor_v1_nonterminal_find(
        slr, slr->start_nonterminal);
    if (start_index < 0) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor semantic dependency cannot find the start nonterminal");
        goto done;
    }
    nonterminal_required[start_index] = true;
    while (changed) {
        changed = false;
        for (production_index = 0u;
             production_index < slr->production_len;
             production_index++) {
            const CettaLpNativeSlrProgramProduction *production =
                &slr->productions[production_index];
            const PPActionBytecodeV1Production *action;
            uint32_t instruction_index;
            uint32_t lhs_index;

            if (!production->authored)
                continue;
            if (!program->final_production_lhs_indices ||
                production->label >= program->actions.production_len) {
                goto malformed;
            }
            lhs_index =
                program->final_production_lhs_indices[production_index];
            if (lhs_index >= slr->nonterminal_len ||
                !nonterminal_required[lhs_index]) {
                continue;
            }
            action = &program->actions.productions[production->label];
            if (action->arity != production->rhs_len ||
                action->instruction_begin >
                    program->actions.instruction_len ||
                action->instruction_len >
                    program->actions.instruction_len -
                        action->instruction_begin ||
                production->rhs_begin > slr->rhs_len ||
                production->rhs_len >
                    slr->rhs_len - production->rhs_begin) {
                goto malformed;
            }
            for (instruction_index = 0u;
                 instruction_index < action->instruction_len;
                 instruction_index++) {
                const PPActionBytecodeV1Instruction *instruction =
                    &program->actions.instructions[
                        action->instruction_begin + instruction_index];
                const CettaLpNativeSymbol *symbol;

                if (instruction->kind !=
                        PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
                    continue;
                }
                if (instruction->operand >= production->rhs_len)
                    goto malformed;
                symbol = &slr->rhs[
                    production->rhs_begin + instruction->operand];
                if (symbol->kind == CETTA_LP_NATIVE_SYMBOL_TM) {
                    int32_t source_index =
                        ppguarded_lex_cursor_v1_terminal_source_index(
                            program, symbol->name);
                    if (source_index < 0)
                        goto malformed;
                    terminal_required[source_index] = true;
                } else {
                    int32_t nonterminal_index =
                        ppguarded_lex_cursor_v1_nonterminal_find(
                            slr, symbol->name);
                    if (nonterminal_index < 0)
                        goto malformed;
                    if (!nonterminal_required[nonterminal_index]) {
                        nonterminal_required[nonterminal_index] = true;
                        changed = true;
                    }
                }
            }
        }
    }
    ok = true;
    goto done;

malformed:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "cursor semantic dependency escaped its bound grammar/actions");

done:
    free(nonterminal_required);
    return ok;
}

static bool ppguarded_lex_cursor_v1_semantic_programs_complete(
    const PPGuardedLexCursorV1Program *program) {
    uint32_t index;

    if (!program)
        return false;
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];
        if (terminal->semantic_value_required &&
            terminal->kind != PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR &&
            terminal->semantic_program_index == UINT32_MAX) {
            return false;
        }
    }
    return true;
}

static bool ppguarded_lex_cursor_v1_semantic_requirements_apply(
    PPGuardedLexCursorV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    bool *required = NULL;
    uint32_t index;

    if (!program ||
        (size_t)program->terminal_len > SIZE_MAX / sizeof(*required)) {
        return false;
    }
    required = calloc(
        program->terminal_len ? program->terminal_len : 1u,
        sizeof(*required));
    if (!required ||
        !ppguarded_lex_cursor_v1_semantic_requirements(
            program, required, error_buf, error_buf_size)) {
        free(required);
        return false;
    }
    for (index = 0u; index < program->terminal_len; index++)
        program->terminals[index].semantic_value_required = required[index];
    program->value_programs_complete =
        ppguarded_lex_cursor_v1_semantic_programs_complete(program);
    free(required);
    return true;
}

static bool ppguarded_lex_cursor_v1_semantic_requirements_validate(
    const PPGuardedLexCursorV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    bool *required = NULL;
    uint32_t index;
    bool ok = false;

    if (!program ||
        (size_t)program->terminal_len > SIZE_MAX / sizeof(*required)) {
        return false;
    }
    if (!program->actions_bound) {
        for (index = 0u; index < program->terminal_len; index++) {
            if (!program->terminals[index].semantic_value_required)
                return false;
        }
        return program->value_programs_complete ==
            ppguarded_lex_cursor_v1_semantic_programs_complete(program);
    }
    required = calloc(
        program->terminal_len ? program->terminal_len : 1u,
        sizeof(*required));
    if (!required ||
        !ppguarded_lex_cursor_v1_semantic_requirements(
            program, required, error_buf, error_buf_size)) {
        goto done;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        if (program->terminals[index].semantic_value_required !=
                required[index]) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor semantic dependency flags do not reproduce");
            goto done;
        }
    }
    if (program->value_programs_complete !=
            ppguarded_lex_cursor_v1_semantic_programs_complete(program)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor semantic dependency coverage does not reproduce");
        goto done;
    }
    ok = true;

done:
    free(required);
    return ok;
}

bool ppguarded_lex_cursor_v1_program_validate(
    const PPGuardedLexCursorV1Program *program,
    char *error_buf,
    size_t error_buf_size) {
    char digest[65];
    uint32_t range_end = 0u;
    uint32_t index;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->base_pack_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->lexical_plan_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->guard_plan_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->guarded_plan_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->execution_plan_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->certificate_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->program_digest) ||
        !rsdfa_v1_program_validate(
            &program->dfa, error_buf, error_buf_size) ||
        !cetta_lp_native_slr_program_validate(
            &program->final_slr, error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_production_lhs_indices_validate(
            &program->final_slr,
            program->final_production_lhs_indices) ||
        program->terminal_len > program->final_slr.terminal_len ||
        (program->terminal_len > 0u && !program->terminals) ||
        (program->value_program_len > 0u && !program->value_programs) ||
        (program->range_len > 0u && !program->ranges)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size, "bad cursor program");
        }
        return false;
    }
    if (!program->actions_bound) {
        if (program->actions.production_len != 0u ||
            program->actions.instruction_len != 0u ||
            program->actions.productions || program->actions.instructions ||
            program->actions.base_pack_digest[0] != '\0' ||
            program->actions.compiler_digest[0] != '\0' ||
            program->actions.answer_set_digest[0] != '\0' ||
            program->actions.program_digest[0] != '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "unbound cursor program carries action storage");
            return false;
        }
    } else if (
        program->actions.production_len !=
            program->final_slr.authored_production_len ||
        (program->actions.production_len > 0u &&
         !program->actions.productions) ||
        (program->actions.instruction_len > 0u &&
         !program->actions.instructions) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->actions.base_pack_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->actions.compiler_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->actions.answer_set_digest) ||
        !ppguarded_lex_cursor_v1_digest_valid(
            program->actions.program_digest) ||
        strcmp(program->actions.base_pack_digest,
               program->base_pack_digest) != 0) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bound cursor action inventory is malformed");
        return false;
    }
    if (!ppguarded_lex_cursor_v1_semantic_requirements_validate(
            program, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor semantic dependencies are malformed");
        }
        return false;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];

        if (terminal->slr_terminal_index >=
                program->final_slr.terminal_len ||
            (index > 0u &&
             program->terminals[index - 1u].slr_terminal_index >=
                 terminal->slr_terminal_index) ||
            terminal->terminal_id != program->final_slr.terminals[
                terminal->slr_terminal_index] ||
            !ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &program->final_slr, terminal->slr_terminal_index)) {
            goto malformed_terminal;
        }
        if (terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR) {
            if (terminal->dfa_tag != UINT32_MAX ||
                terminal->guard_dfa_tag != UINT32_MAX ||
                terminal->guard_range_len != 0u ||
                terminal->guard_accepts_eof ||
                terminal->semantic_start_state_id != UINT32_MAX ||
                terminal->semantic_program_index != UINT32_MAX ||
                terminal->scalar_kind >
                    CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES ||
                (terminal->scalar_kind !=
                     CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
                 terminal->scalar != 0u)) {
                goto malformed_terminal;
            }
            if (terminal->scalar_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
                if (terminal->scalar_range_len == 0u ||
                    !ppguarded_lex_cursor_v1_range_slice_valid(
                        program, terminal->scalar_range_begin,
                        terminal->scalar_range_len, range_end)) {
                    goto malformed_terminal;
                }
                range_end += terminal->scalar_range_len;
            } else if (terminal->scalar_range_len != 0u ||
                       terminal->scalar_range_begin != range_end ||
                       (terminal->scalar_kind ==
                            CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR &&
                        (terminal->scalar > UINT32_C(0x10ffff) ||
                         (terminal->scalar >= UINT32_C(0xd800) &&
                          terminal->scalar <= UINT32_C(0xdfff))))) {
                goto malformed_terminal;
            }
            if (terminal->guard_range_begin != range_end)
                goto malformed_terminal;
            continue;
        }
        if (terminal->kind ==
                PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN) {
            if (terminal->dfa_tag >= program->dfa.tag_len ||
                terminal->guard_dfa_tag != UINT32_MAX ||
                terminal->scalar_kind !=
                    CETTA_LP_NATIVE_UTF8_TERMINAL_ANY ||
                terminal->scalar != 0u ||
                terminal->scalar_range_begin != range_end ||
                terminal->scalar_range_len != 0u ||
                terminal->guard_range_begin != range_end ||
                terminal->guard_range_len != 0u ||
                terminal->guard_accepts_eof ||
                terminal->semantic_start_state_id == UINT32_MAX ||
                (terminal->semantic_program_index != UINT32_MAX &&
                 (terminal->semantic_program_index >=
                      program->value_program_len ||
                  program->value_programs[
                      terminal->semantic_program_index].guarded ||
                  program->value_programs[
                      terminal->semantic_program_index].start_state_id !=
                      terminal->semantic_start_state_id)) ||
                (program->value_programs_complete &&
                 terminal->semantic_value_required &&
                 terminal->semantic_program_index == UINT32_MAX)) {
                goto malformed_terminal;
            }
            continue;
        }
        if (terminal->kind ==
                PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN) {
            if (terminal->dfa_tag >= program->dfa.tag_len ||
                terminal->guard_dfa_tag >= program->dfa.tag_len ||
                terminal->scalar_kind !=
                    CETTA_LP_NATIVE_UTF8_TERMINAL_ANY ||
                terminal->scalar != 0u ||
                terminal->scalar_range_begin != range_end ||
                terminal->scalar_range_len != 0u ||
                terminal->semantic_start_state_id == UINT32_MAX ||
                (terminal->semantic_program_index != UINT32_MAX &&
                 (terminal->semantic_program_index >=
                      program->value_program_len ||
                  !program->value_programs[
                      terminal->semantic_program_index].guarded ||
                  program->value_programs[
                      terminal->semantic_program_index].start_state_id !=
                      terminal->semantic_start_state_id)) ||
                (program->value_programs_complete &&
                 terminal->semantic_value_required &&
                 terminal->semantic_program_index == UINT32_MAX) ||
                (!terminal->guard_accepts_eof &&
                 terminal->guard_range_len == 0u) ||
                !ppguarded_lex_cursor_v1_range_slice_valid(
                    program, terminal->guard_range_begin,
                    terminal->guard_range_len, range_end)) {
                goto malformed_terminal;
            }
            range_end += terminal->guard_range_len;
            continue;
        }
        goto malformed_terminal;
    }
    {
        uint32_t source_index = 0u;
        uint32_t terminal_index;
        for (terminal_index = 0u;
             terminal_index < program->final_slr.terminal_len;
             terminal_index++) {
            if (!ppguarded_lex_cursor_v1_slr_terminal_shifts(
                    &program->final_slr, terminal_index)) {
                continue;
            }
            if (source_index >= program->terminal_len ||
                program->terminals[source_index].slr_terminal_index !=
                    terminal_index) {
                goto malformed_terminal;
            }
            source_index++;
        }
        if (source_index != program->terminal_len)
            goto malformed_terminal;
    }
    if (range_end != program->range_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program has trailing range storage");
        return false;
    }
    for (index = 0u; index < program->value_program_len; index++) {
        if (!ppguarded_lex_cursor_v1_value_program_validate(
                program, index, error_buf, error_buf_size)) {
            if (error_buf && error_buf_size > 0u &&
                error_buf[0] == '\0') {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor value program %u is malformed", index);
            }
            return false;
        }
    }
    ppguarded_lex_cursor_v1_program_digest(program, digest);
    if (strcmp(digest, program->program_digest) != 0) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program digest does not reproduce");
        return false;
    }
    return true;

malformed_terminal:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "cursor program terminal source %u is malformed "
        "(kind=%u required=%u semantic-start=%u semantic-program=%u)",
        index,
        index < program->terminal_len
            ? (uint32_t)program->terminals[index].kind
            : UINT32_MAX,
        index < program->terminal_len &&
            program->terminals[index].semantic_value_required
            ? 1u : 0u,
        index < program->terminal_len
            ? program->terminals[index].semantic_start_state_id
            : UINT32_MAX,
        index < program->terminal_len
            ? program->terminals[index].semantic_program_index
            : UINT32_MAX);
    return false;
}

static bool ppguarded_lex_cursor_v1_actions_match_slr(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGrammar grammar;
    bool *seen = NULL;
    uint32_t expected_len;
    uint32_t slr_index;
    bool ok = false;

    cetta_lp_native_grammar_init(&grammar);
    if (!program || !pack || !lexical_plan || !guard_plan || !guarded_plan ||
        guard_plan->production_len > UINT32_MAX - pack->production_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor action/SLR binding arguments");
        goto done;
    }
    expected_len = pack->production_len + guard_plan->production_len;
    if (!ppguarded_lex_v1_grammar_build(
            pack, lexical_plan, guard_plan, guarded_plan,
            &grammar, error_buf, error_buf_size)) {
        goto done;
    }
    if (grammar.production_len != expected_len ||
        program->actions.production_len != expected_len ||
        program->final_slr.authored_production_len != expected_len ||
        program->final_slr.production_len < expected_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor action and SLR production inventories differ");
        goto done;
    }
    seen = calloc(expected_len ? expected_len : 1u, sizeof(*seen));
    if (!seen) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to allocate cursor action/SLR binding check");
        goto done;
    }
    for (slr_index = 0u; slr_index < expected_len; slr_index++) {
        const CettaLpNativeSlrProgramProduction *slr_production =
            &program->final_slr.productions[slr_index];
        uint32_t production_id = slr_production->label;
        const PPActionBytecodeV1Production *action_production =
            production_id < expected_len
            ? &program->actions.productions[production_id] : NULL;
        const CettaLpNativeProduction *source_production =
            production_id < expected_len
            ? &grammar.productions[production_id] : NULL;
        uint32_t item_index;

        if (!slr_production->authored ||
            production_id >= expected_len || seen[production_id] ||
            source_production->label != production_id ||
            slr_production->lhs != source_production->lhs ||
            slr_production->rhs_len != source_production->rhs_len ||
            action_production->arity != source_production->rhs_len ||
            slr_production->rhs_begin > program->final_slr.rhs_len ||
            source_production->rhs_len > program->final_slr.rhs_len -
                slr_production->rhs_begin) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor SLR row %u does not bind one action production",
                slr_index);
            goto done;
        }
        seen[production_id] = true;
        for (item_index = 0u;
             item_index < source_production->rhs_len; item_index++) {
            const CettaLpNativeSymbol *slr_item =
                &program->final_slr.rhs[
                    slr_production->rhs_begin + item_index];
            const CettaLpNativeSymbol *source_item =
                &source_production->rhs[item_index];
            if (slr_item->kind != source_item->kind ||
                slr_item->name != source_item->name ||
                slr_item->scope != source_item->scope) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor SLR row %u/action production %u item %u differs "
                    "(%u/%u/%u versus %u/%u/%u)",
                    slr_index, production_id, item_index,
                    (uint32_t)slr_item->kind, slr_item->name,
                    slr_item->scope, (uint32_t)source_item->kind,
                    source_item->name, source_item->scope);
                goto done;
            }
        }
    }
    for (slr_index = 0u; slr_index < expected_len; slr_index++) {
        if (!seen[slr_index]) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor SLR omits one action production");
            goto done;
        }
    }
    for (slr_index = 0u; slr_index < program->value_program_len;
         slr_index++) {
        const CettaLpNativeSlrProgram *value_slr =
            &program->value_programs[slr_index].slr;
        uint32_t production_index;

        for (production_index = 0u;
             production_index < value_slr->production_len;
             production_index++) {
            const CettaLpNativeSlrProgramProduction *production =
                &value_slr->productions[production_index];
            if (!production->authored)
                continue;
            if (production->label >= expected_len ||
                program->actions.productions[
                    production->label].arity != production->rhs_len) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor value program %u production %u does not bind "
                    "one action production",
                    slr_index, production_index);
                goto done;
            }
        }
    }
    ok = true;

done:
    free(seen);
    cetta_lp_native_grammar_free(&grammar);
    return ok;
}

static bool ppguarded_lex_cursor_v1_action_program_validate_source(
    const PPActionBytecodeV1Program *actions,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    char *error_buf,
    size_t error_buf_size) {
    if (!guard_plan) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor action program has no guard-plan source");
        return false;
    }
    if (guard_plan->production_len == 0u) {
        return pp_action_bytecode_v1_program_validate(
            actions, pack, error_buf, error_buf_size);
    }
    return pp_action_bytecode_v1_program_validate_guard_extended(
        actions, pack, lexical_plan, guard_plan,
        error_buf, error_buf_size);
}

static bool ppguarded_lex_cursor_v1_action_program_build_source(
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
    if (!guard_plan ||
        !ppguard_plan_v1_validate(
            pack, lexical_plan, guard_plan,
            error_buf, error_buf_size)) {
        return false;
    }
    if (guard_plan->production_len == 0u) {
        return pp_action_bytecode_v1_program_build(
            pack, compiler_answers, compiler_answer_len,
            compiler_digest, answer_set_digest,
            out, error_buf, error_buf_size);
    }
    return pp_action_bytecode_v1_program_build_guard_extended(
        pack, lexical_plan, guard_plan,
        compiler_answers, compiler_answer_len,
        compiler_digest, answer_set_digest,
        out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_validate_bound(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    char *error_buf,
    size_t error_buf_size) {
    if (!program || !pack || !lexical_plan || !guard_plan || !guarded_plan ||
        !program->actions_bound) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program has no bound action program");
        return false;
    }
    if (strcmp(program->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(program->lexical_plan_digest,
               lexical_plan->plan_digest) != 0 ||
        strcmp(program->guard_plan_digest,
               guard_plan->plan_digest) != 0 ||
        strcmp(program->guarded_plan_digest,
               guarded_plan->plan_digest) != 0) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bound cursor provenance does not match its source plans");
        return false;
    }
    return ppguard_plan_v1_validate(
               pack, lexical_plan, guard_plan,
               error_buf, error_buf_size) &&
        ppguarded_lex_cursor_v1_action_program_validate_source(
            &program->actions, pack, lexical_plan, guard_plan,
            error_buf, error_buf_size) &&
        ppguarded_lex_cursor_v1_actions_match_slr(
            program, pack, lexical_plan, guard_plan, guarded_plan,
            error_buf, error_buf_size) &&
        ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_bind_actions(
    PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    Atom *const *compiler_answers,
    size_t compiler_answer_len,
    const char *compiler_digest,
    const char *answer_set_digest,
    char *error_buf,
    size_t error_buf_size) {
    PPActionBytecodeV1Program actions;
    bool *unbound_required = NULL;
    bool unbound_complete;
    char unbound_digest[65];
    uint32_t index;

    pp_action_bytecode_v1_program_init(&actions);
    if (!program || program->actions_bound ||
        !ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_action_program_build_source(
            pack, lexical_plan, guard_plan,
            compiler_answers, compiler_answer_len,
            compiler_digest, answer_set_digest,
            &actions, error_buf, error_buf_size)) {
        if (program && program->actions_bound) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor action program is already bound");
        }
        pp_action_bytecode_v1_program_free(&actions);
        return false;
    }
    if ((size_t)program->terminal_len >
            SIZE_MAX / sizeof(*unbound_required)) {
        pp_action_bytecode_v1_program_free(&actions);
        return false;
    }
    unbound_required = malloc(
        sizeof(*unbound_required) *
        (size_t)(program->terminal_len ? program->terminal_len : 1u));
    if (!unbound_required) {
        pp_action_bytecode_v1_program_free(&actions);
        return false;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        unbound_required[index] =
            program->terminals[index].semantic_value_required;
    }
    unbound_complete = program->value_programs_complete;
    memcpy(unbound_digest, program->program_digest, sizeof(unbound_digest));
    pp_action_bytecode_v1_program_free(&program->actions);
    program->actions = actions;
    memset(&actions, 0, sizeof(actions));
    program->actions_bound = true;
    if (!ppguarded_lex_cursor_v1_semantic_requirements_apply(
            program, error_buf, error_buf_size)) {
        goto rollback;
    }
    ppguarded_lex_cursor_v1_program_digest(
        program, program->program_digest);
    if (!ppguarded_lex_cursor_v1_program_validate_bound(
            program, pack, lexical_plan, guard_plan, guarded_plan,
            error_buf, error_buf_size)) {
        goto rollback;
    }
    free(unbound_required);
    return true;

rollback:
    pp_action_bytecode_v1_program_free(&program->actions);
    pp_action_bytecode_v1_program_init(&program->actions);
    program->actions_bound = false;
    for (index = 0u; index < program->terminal_len; index++) {
        program->terminals[index].semantic_value_required =
            unbound_required[index];
    }
    program->value_programs_complete = unbound_complete;
    memcpy(program->program_digest,
           unbound_digest, sizeof(unbound_digest));
    free(unbound_required);
    return false;
}

static bool ppguarded_lex_cursor_v1_append_ranges(
    PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUnicodeRange *ranges,
    uint32_t len,
    uint32_t *begin,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUnicodeRange *next;
    uint32_t index;

    if (!program || !begin || (len > 0u && !ranges) ||
        len > UINT32_MAX - program->range_len ||
        (size_t)(program->range_len + len) >
            SIZE_MAX / sizeof(*program->ranges)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program range append overflow");
        return false;
    }
    *begin = program->range_len;
    if (len == 0u)
        return true;
    for (index = 0u; index < len; index++) {
        if (!ppguarded_lex_cursor_v1_range_valid(&ranges[index])) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor program received an invalid Unicode range");
            return false;
        }
    }
    next = realloc(
        program->ranges,
        sizeof(*program->ranges) * (size_t)(program->range_len + len));
    if (!next) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to grow cursor program ranges");
        return false;
    }
    program->ranges = next;
    memcpy(&program->ranges[program->range_len], ranges,
           sizeof(*program->ranges) * len);
    program->range_len += len;
    return true;
}

static bool ppguarded_lex_cursor_v1_value_append_ranges(
    PPGuardedLexCursorV1ValueProgram *program,
    const CettaLpNativeUnicodeRange *ranges,
    uint32_t len,
    uint32_t *begin,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUnicodeRange *next;
    uint32_t index;

    if (!program || !begin || (len > 0u && !ranges) ||
        len > UINT32_MAX - program->range_len ||
        (size_t)(program->range_len + len) >
            SIZE_MAX / sizeof(*program->ranges)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor value-program range append overflow");
        return false;
    }
    *begin = program->range_len;
    if (len == 0u)
        return true;
    for (index = 0u; index < len; index++) {
        if (!ppguarded_lex_cursor_v1_range_valid(&ranges[index])) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor value program received an invalid Unicode range");
            return false;
        }
    }
    next = realloc(
        program->ranges,
        sizeof(*program->ranges) * (size_t)(program->range_len + len));
    if (!next)
        return false;
    program->ranges = next;
    memcpy(&program->ranges[program->range_len], ranges,
           sizeof(*program->ranges) * len);
    program->range_len += len;
    return true;
}

static int ppguarded_lex_cursor_v1_u32_compare(
    const void *left,
    const void *right) {
    uint32_t lhs = *(const uint32_t *)left;
    uint32_t rhs = *(const uint32_t *)right;
    return lhs < rhs ? -1 : lhs > rhs ? 1 : 0;
}

static bool ppguarded_lex_cursor_v1_expr_head(
    const Atom *atom,
    const char *head,
    CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == argument_len + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static int32_t ppguarded_lex_cursor_v1_guard_body_state(
    const PPABIV1Pack *pack,
    const Atom *body) {
    const Atom *name;
    uint32_t index;

    if (!pack || !ppguarded_lex_cursor_v1_expr_head(
            body, "sir-ref", 1u) ||
        body->expr.elems[1]->kind != ATOM_SYMBOL) {
        return -1;
    }
    name = body->expr.elems[1];
    for (index = 0u; index < pack->state_len; index++) {
        const Atom *identity = pack->states[index].identity;
        if (ppguarded_lex_cursor_v1_expr_head(
                identity, "pp-def", 1u) &&
            atom_eq(identity->expr.elems[1], (Atom *)name)) {
            return (int32_t)index;
        }
    }
    return -1;
}

static int32_t ppguarded_lex_cursor_v1_sorted_u32_find(
    const uint32_t *values,
    uint32_t len,
    uint32_t value) {
    uint32_t low = 0u;
    uint32_t high = len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (values[middle] < value)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < len && values[low] == value ? (int32_t)low : -1;
}

static bool ppguarded_lex_cursor_v1_value_dispatch_build(
    PPGuardedLexCursorV1ValueProgram *program,
    char *error_buf,
    size_t error_buf_size) {
    uint64_t candidate_len = 0u;
    uint32_t columns;
    uint32_t state;
    uint32_t source_index;
    uint32_t write = 0u;

    if (!program || program->slr.summary.state_len == UINT32_MAX ||
        (program->terminal_len > 0u && !program->terminals)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor value dispatch-program inputs");
        return false;
    }
    columns = program->slr.terminal_len + 1u;
    for (state = 0u; state < program->slr.summary.state_len; state++) {
        for (source_index = 0u; source_index < program->terminal_len;
             source_index++) {
            const PPGuardedLexCursorV1ValueTerminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &program->slr.actions[
                    state * columns + terminal->slr_terminal_index];

            if (action->kind != CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
                candidate_len++;
        }
    }
    if (candidate_len > UINT32_MAX ||
        (size_t)(program->slr.summary.state_len + 1u) >
            SIZE_MAX / sizeof(*program->state_candidate_offsets) ||
        (size_t)candidate_len >
            SIZE_MAX / sizeof(*program->state_candidate_terminal_indices)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor value dispatch-program size overflow");
        return false;
    }
    program->state_candidate_offsets = calloc(
        (size_t)program->slr.summary.state_len + 1u,
        sizeof(*program->state_candidate_offsets));
    program->state_candidate_terminal_indices = calloc(
        candidate_len ? (size_t)candidate_len : 1u,
        sizeof(*program->state_candidate_terminal_indices));
    if (!program->state_candidate_offsets ||
        !program->state_candidate_terminal_indices) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate cursor value dispatch program");
        return false;
    }
    for (state = 0u; state < program->slr.summary.state_len; state++) {
        program->state_candidate_offsets[state] = write;
        for (source_index = 0u; source_index < program->terminal_len;
             source_index++) {
            const PPGuardedLexCursorV1ValueTerminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &program->slr.actions[
                    state * columns + terminal->slr_terminal_index];

            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
                continue;
            program->state_candidate_terminal_indices[write++] =
                source_index;
        }
    }
    program->state_candidate_offsets[program->slr.summary.state_len] =
        write;
    program->state_candidate_len = write;
    return write == (uint32_t)candidate_len;
}

static bool ppguarded_lex_cursor_v1_value_terminal_matches_scalar(
    const PPGuardedLexCursorV1ValueProgram *program,
    const PPGuardedLexCursorV1ValueTerminal *terminal,
    uint32_t scalar) {
    if (!program || !terminal ||
        terminal->kind !=
            PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
        return false;
    }
    switch (terminal->scalar_kind) {
    case CETTA_LP_NATIVE_UTF8_TERMINAL_ANY:
        return true;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_EOF:
        return false;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR:
        return terminal->scalar == scalar;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES:
        return ppguarded_lex_cursor_v1_range_contains(
            &program->ranges[terminal->range_begin],
            terminal->range_len, scalar);
    }
    return false;
}

static bool ppguarded_lex_cursor_v1_value_scalar_dispatch_cell(
    const PPGuardedLexCursorV1ValueProgram *program,
    uint32_t state,
    uint32_t scalar,
    CettaLpNativeSlrProgramAction *out_action,
    uint32_t *out_shift_source,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrProgramAction selected = {
        .kind = CETTA_LP_NATIVE_SLR_PROGRAM_ERROR,
        .value = 0,
    };
    uint32_t shift_source = UINT32_MAX;
    uint32_t columns;
    uint32_t source_index;

    if (!program || state >= program->slr.summary.state_len ||
        scalar > UINT32_C(0x10ffff) || !out_action ||
        !out_shift_source) {
        return false;
    }
    columns = program->slr.terminal_len + 1u;
    for (source_index = 0u; source_index < program->terminal_len;
         source_index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &program->terminals[source_index];
        const CettaLpNativeSlrProgramAction *action;

        if (!ppguarded_lex_cursor_v1_value_terminal_matches_scalar(
                program, terminal, scalar)) {
            continue;
        }
        action = &program->slr.actions[
            state * columns + terminal->slr_terminal_index];
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR)
            continue;
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT ||
            (selected.kind != CETTA_LP_NATIVE_SLR_PROGRAM_ERROR &&
             (selected.kind != action->kind ||
              selected.value != action->value)) ||
            (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT &&
             shift_source != UINT32_MAX)) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor value scalar dispatch is not functional");
            return false;
        }
        selected = *action;
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT)
            shift_source = source_index;
    }
    *out_action = selected;
    *out_shift_source = shift_source;
    return true;
}

static bool ppguarded_lex_cursor_v1_value_scalar_dispatch_build(
    PPGuardedLexCursorV1ValueProgram *program,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t *boundaries = NULL;
    uint64_t boundary_cap = 4u;
    uint32_t boundary_len = 0u;
    uint32_t unique_len = 0u;
    uint32_t source_index;
    uint32_t range_index;
    uint32_t state;
    size_t cell_len;
    bool ok = false;

    if (!program || program->slr.summary.state_len == 0u)
        return false;
    for (source_index = 0u; source_index < program->terminal_len;
         source_index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &program->terminals[source_index];

        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
            continue;
        }
        if (terminal->scalar_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR) {
            boundary_cap += 2u;
        } else if (terminal->scalar_kind ==
                       CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
            boundary_cap += (uint64_t)terminal->range_len * 2u;
        }
    }
    if (boundary_cap > UINT32_MAX ||
        (size_t)boundary_cap > SIZE_MAX / sizeof(*boundaries)) {
        goto overflow;
    }
    boundaries = malloc(sizeof(*boundaries) * (size_t)boundary_cap);
    if (!boundaries)
        goto allocation;
    boundaries[boundary_len++] = 0u;
    boundaries[boundary_len++] = UINT32_C(0xd800);
    boundaries[boundary_len++] = UINT32_C(0xe000);
    boundaries[boundary_len++] = UINT32_C(0x110000);
    for (source_index = 0u; source_index < program->terminal_len;
         source_index++) {
        const PPGuardedLexCursorV1ValueTerminal *terminal =
            &program->terminals[source_index];
        uint32_t local_index;

        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
            continue;
        }
        if (terminal->scalar_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR) {
            boundaries[boundary_len++] = terminal->scalar;
            boundaries[boundary_len++] = terminal->scalar + 1u;
        } else if (terminal->scalar_kind ==
                       CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
            for (local_index = 0u; local_index < terminal->range_len;
                 local_index++) {
                const CettaLpNativeUnicodeRange *range =
                    &program->ranges[
                        terminal->range_begin + local_index];
                boundaries[boundary_len++] = range->low;
                boundaries[boundary_len++] = range->high + 1u;
            }
        }
    }
    if (boundary_len != (uint32_t)boundary_cap)
        goto overflow;
    qsort(boundaries, boundary_len, sizeof(*boundaries),
          ppguarded_lex_cursor_v1_u32_compare);
    for (source_index = 0u; source_index < boundary_len; source_index++) {
        if (unique_len == 0u ||
            boundaries[source_index] != boundaries[unique_len - 1u]) {
            boundaries[unique_len++] = boundaries[source_index];
        }
    }
    if (unique_len < 2u || boundaries[0] != 0u ||
        boundaries[unique_len - 1u] != UINT32_C(0x110000)) {
        goto overflow;
    }
    program->scalar_dispatch_range_len = 0u;
    for (range_index = 0u; range_index + 1u < unique_len;
         range_index++) {
        uint32_t low = boundaries[range_index];
        uint32_t high = boundaries[range_index + 1u] - 1u;
        if (!(low <= UINT32_C(0xdfff) &&
              high >= UINT32_C(0xd800))) {
            program->scalar_dispatch_range_len++;
        }
    }
    if (program->scalar_dispatch_range_len == 0u)
        goto overflow;
    program->scalar_dispatch_ranges = calloc(
        program->scalar_dispatch_range_len,
        sizeof(*program->scalar_dispatch_ranges));
    if ((size_t)program->slr.summary.state_len >
        SIZE_MAX / program->scalar_dispatch_range_len) {
        goto overflow;
    }
    cell_len = (size_t)program->slr.summary.state_len *
        program->scalar_dispatch_range_len;
    if (cell_len > SIZE_MAX / sizeof(*program->scalar_dispatch_actions) ||
        cell_len >
            SIZE_MAX / sizeof(*program->scalar_dispatch_shift_sources)) {
        goto overflow;
    }
    program->scalar_dispatch_actions = calloc(
        cell_len, sizeof(*program->scalar_dispatch_actions));
    program->scalar_dispatch_shift_sources = malloc(
        cell_len * sizeof(*program->scalar_dispatch_shift_sources));
    if (!program->scalar_dispatch_ranges ||
        !program->scalar_dispatch_actions ||
        !program->scalar_dispatch_shift_sources) {
        goto allocation;
    }
    source_index = 0u;
    for (range_index = 0u; range_index + 1u < unique_len;
         range_index++) {
        uint32_t low = boundaries[range_index];
        uint32_t high = boundaries[range_index + 1u] - 1u;
        if (low <= UINT32_C(0xdfff) &&
            high >= UINT32_C(0xd800)) {
            continue;
        }
        program->scalar_dispatch_ranges[source_index++] =
            (CettaLpNativeUnicodeRange){.low = low, .high = high};
    }
    if (source_index != program->scalar_dispatch_range_len)
        goto overflow;
    for (state = 0u; state < program->slr.summary.state_len; state++) {
        for (range_index = 0u;
             range_index < program->scalar_dispatch_range_len;
             range_index++) {
            size_t cell = (size_t)state *
                    program->scalar_dispatch_range_len +
                range_index;
            if (!ppguarded_lex_cursor_v1_value_scalar_dispatch_cell(
                    program, state,
                    program->scalar_dispatch_ranges[range_index].low,
                    &program->scalar_dispatch_actions[cell],
                    &program->scalar_dispatch_shift_sources[cell],
                    error_buf, error_buf_size)) {
                goto done;
            }
        }
    }
    ok = true;
    goto done;

overflow:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "cursor value scalar dispatch size overflow");
    goto done;
allocation:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "cannot allocate cursor value scalar dispatch");

done:
    free(boundaries);
    return ok;
}

static bool ppguarded_lex_cursor_v1_value_program_build(
    const CettaLpNativeGrammar *grammar,
    uint32_t start_state_id,
    const PPNativeV1TerminalPlan *scalar_terminals,
    const PPABIV1Pack *pack,
    const PPGuardPlanV1 *guard_plan,
    const uint32_t *base_start_ids,
    const uint32_t *base_program_indices,
    uint32_t base_start_len,
    bool *unsupported_dependency,
    PPGuardedLexCursorV1ValueProgram *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeSlrPrepared prepared;
    PPGuardedLexCursorV1ValueProgram result;
    uint32_t terminal_write = 0u;
    uint32_t index;
    char slr_error[256] = {0};
    bool ok = false;

    if (unsupported_dependency)
        *unsupported_dependency = false;
    cetta_lp_native_slr_prepared_init(&prepared);
    ppguarded_lex_cursor_v1_value_program_init(&result);
    result.start_state_id = start_state_id;
    result.guarded = guard_plan != NULL;
    if (!grammar || !scalar_terminals || !pack || !out ||
        (base_start_len > 0u &&
         (!base_start_ids || (guard_plan && !base_program_indices))) ||
        (guard_plan && !unsupported_dependency)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad value-program build inputs for start state %u",
            start_state_id);
        goto done;
    }
    if (!cetta_lp_native_slr_prepare(
            &prepared, grammar, start_state_id,
            slr_error, sizeof(slr_error)) ||
        !cetta_lp_native_slr_prepared_export_program(
            &prepared, &result.slr, slr_error, sizeof(slr_error)) ||
        !ppguarded_lex_cursor_v1_production_lhs_indices_build(
            &result.slr, &result.production_lhs_indices,
            slr_error, sizeof(slr_error))) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "start state %u (%s) has no deterministic value program: %s",
            start_state_id,
            start_state_id < pack->state_len
                ? pack->states[start_state_id].canonical : "unknown",
            slr_error[0] ? slr_error : "unknown SLR failure");
        goto done;
    }
    for (index = 0u; index < result.slr.terminal_len; index++) {
        if (ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &result.slr, index)) {
            result.terminal_len++;
        }
    }
    if ((size_t)result.terminal_len >
        SIZE_MAX / sizeof(*result.terminals)) {
        goto done;
    }
    result.terminals = calloc(
        result.terminal_len ? result.terminal_len : 1u,
        sizeof(*result.terminals));
    if (!result.terminals)
        goto done;
    for (index = 0u; index < result.slr.terminal_len; index++) {
        PPGuardedLexCursorV1ValueTerminal *target;
        uint32_t terminal_id;
        uint32_t source_len = 0u;
        int32_t scalar_index = -1;
        int32_t guard_index = -1;
        uint32_t source_index;

        if (!ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &result.slr, index)) {
            continue;
        }
        if (terminal_write >= result.terminal_len)
            goto done;
        target = &result.terminals[terminal_write++];
        terminal_id = result.slr.terminals[index];
        *target = (PPGuardedLexCursorV1ValueTerminal){
            .terminal_id = terminal_id,
            .slr_terminal_index = index,
            .scalar_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_ANY,
            .range_begin = result.range_len,
            .guard_value_program_index = UINT32_MAX,
        };
        for (source_index = 0u;
             source_index < scalar_terminals->len; source_index++) {
            if (scalar_terminals->terminals[source_index].terminal_id ==
                terminal_id) {
                scalar_index = (int32_t)source_index;
                source_len++;
            }
        }
        if (guard_plan) {
            for (source_index = 0u;
                 source_index < guard_plan->entry_len; source_index++) {
                if (guard_plan->entries[source_index].terminal_id ==
                    terminal_id) {
                    guard_index = (int32_t)source_index;
                    source_len++;
                }
            }
        }
        if (source_len != 1u) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "value program %u terminal %u has %u direct sources",
                start_state_id, terminal_id, source_len);
            goto done;
        }
        if (scalar_index >= 0) {
            const CettaLpNativeUtf8Terminal *source =
                &scalar_terminals->terminals[scalar_index];
            target->kind =
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR;
            target->scalar_kind = source->kind;
            target->scalar = source->kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR
                ? source->scalar : 0u;
            if (source->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
                if (!ppguarded_lex_cursor_v1_value_append_ranges(
                        &result, source->ranges, source->range_len,
                        &target->range_begin,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                target->range_len = source->range_len;
            }
            continue;
        }
        if (guard_index >= 0) {
            int32_t body_state =
                ppguarded_lex_cursor_v1_guard_body_state(
                    pack, guard_plan->entries[guard_index].body);
            int32_t base_start_index = body_state >= 0
                ? ppguarded_lex_cursor_v1_sorted_u32_find(
                      base_start_ids, base_start_len,
                      (uint32_t)body_state)
                : -1;
            if (base_start_index < 0) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "guard terminal %u has no base value program",
                    terminal_id);
                goto done;
            }
            if (base_program_indices[base_start_index] == UINT32_MAX) {
                *unsupported_dependency = true;
                goto done;
            }
            target->kind = PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_GUARD;
            target->guard_value_program_index =
                base_program_indices[base_start_index];
            continue;
        }
        goto done;
    }
    if (terminal_write != result.terminal_len)
        goto done;
    if (!ppguarded_lex_cursor_v1_value_dispatch_build(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    if (!ppguarded_lex_cursor_v1_value_scalar_dispatch_build(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    ppguarded_lex_cursor_v1_value_program_digest(
        &result, result.program_digest);
    ppguarded_lex_cursor_v1_value_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppguarded_lex_cursor_v1_value_program_free(&result);
    cetta_lp_native_slr_prepared_free(&prepared);
    return ok;
}

typedef enum {
    PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_ERROR = 0,
    PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_PARTIAL = 1,
    PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_BUILT = 2
} PPGuardedLexCursorV1ValueProgramsOutcome;

static PPGuardedLexCursorV1ValueProgramsOutcome
ppguarded_lex_cursor_v1_value_programs_build(
    PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPNativeV1TerminalPlan *scalar_terminals,
    uint32_t *conflict_len,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeGrammar base_grammar;
    CettaLpNativeGrammar guarded_grammar;
    uint32_t *base_start_ids = NULL;
    uint32_t *base_program_indices = NULL;
    uint32_t *base_conflicts = NULL;
    uint32_t *guarded_conflicts = NULL;
    uint32_t base_start_cap = 0u;
    uint32_t base_start_len = 0u;
    uint32_t guarded_len = 0u;
    uint32_t value_program_cap = 0u;
    uint32_t value_write = 0u;
    uint32_t index;
    PPGuardedLexCursorV1ValueProgramsOutcome outcome =
        PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_ERROR;

    cetta_lp_native_grammar_init(&base_grammar);
    cetta_lp_native_grammar_init(&guarded_grammar);
    if (!program || !pack || !lexical_plan || !guard_plan ||
        !scalar_terminals || !conflict_len || program->value_programs ||
        program->value_program_len != 0u ||
        program->terminal_len > UINT32_MAX - guard_plan->entry_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor value-program family inputs");
        goto done;
    }
    *conflict_len = 0u;
    base_start_cap = program->terminal_len + guard_plan->entry_len;
    if ((size_t)base_start_cap > SIZE_MAX / sizeof(*base_start_ids))
        goto done;
    base_start_ids = malloc(
        sizeof(*base_start_ids) * (size_t)(base_start_cap
                                           ? base_start_cap : 1u));
    base_program_indices = malloc(
        sizeof(*base_program_indices) * (size_t)(base_start_cap
                                                  ? base_start_cap : 1u));
    base_conflicts = calloc(
        base_start_cap ? base_start_cap : 1u, sizeof(*base_conflicts));
    guarded_conflicts = calloc(
        program->terminal_len ? program->terminal_len : 1u,
        sizeof(*guarded_conflicts));
    if (!base_start_ids || !base_program_indices || !base_conflicts ||
        !guarded_conflicts) {
        goto done;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];
        if (terminal->kind ==
                PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN) {
            base_start_ids[base_start_len++] =
                terminal->semantic_start_state_id;
        } else if (terminal->kind ==
                       PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN) {
            guarded_len++;
        }
    }
    for (index = 0u; index < guard_plan->entry_len; index++) {
        int32_t body_state = ppguarded_lex_cursor_v1_guard_body_state(
            pack, guard_plan->entries[index].body);
        if (body_state < 0 || base_start_len >= base_start_cap) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "positive guard %u has no closed reference body", index);
            goto done;
        }
        base_start_ids[base_start_len++] = (uint32_t)body_state;
    }
    if (base_start_len > 1u) {
        uint32_t unique_len = 0u;
        qsort(base_start_ids, base_start_len,
              sizeof(*base_start_ids),
              ppguarded_lex_cursor_v1_u32_compare);
        for (index = 0u; index < base_start_len; index++) {
            if (unique_len == 0u ||
                base_start_ids[index] != base_start_ids[unique_len - 1u]) {
                base_start_ids[unique_len++] = base_start_ids[index];
            }
        }
        base_start_len = unique_len;
    }
    if (base_start_len > UINT32_MAX - guarded_len ||
        (size_t)(base_start_len + guarded_len) >
            SIZE_MAX / sizeof(*program->value_programs) ||
        !ppnative_v1_grammar_build(pack, &base_grammar) ||
        !ppguard_plan_v1_grammar_build(
            pack, lexical_plan, guard_plan, &guarded_grammar,
            error_buf, error_buf_size)) {
        goto done;
    }
    for (index = 0u; index < base_start_len; index++) {
        CettaLpNativeSlrSummary summary;

        if (!cetta_lp_native_slr_summary(
                &base_grammar, base_start_ids[index], &summary,
                error_buf, error_buf_size)) {
            goto done;
        }
        if (summary.conflict_len > UINT32_MAX - *conflict_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor value-program conflict count overflow");
            goto done;
        }
        *conflict_len += summary.conflict_len;
        base_conflicts[index] = summary.conflict_len;
        base_program_indices[index] = UINT32_MAX;
        if (summary.conflict_len == 0u)
            value_program_cap++;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];
        CettaLpNativeSlrSummary summary;

        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN) {
            continue;
        }
        if (!cetta_lp_native_slr_summary(
                &guarded_grammar, terminal->semantic_start_state_id,
                &summary, error_buf, error_buf_size)) {
            goto done;
        }
        if (summary.conflict_len > UINT32_MAX - *conflict_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor value-program conflict count overflow");
            goto done;
        }
        *conflict_len += summary.conflict_len;
        guarded_conflicts[index] = summary.conflict_len;
        if (summary.conflict_len == 0u)
            value_program_cap++;
    }
    program->value_program_len = value_program_cap;
    if (program->value_program_len > 0u) {
        program->value_programs = calloc(
            program->value_program_len,
            sizeof(*program->value_programs));
        if (!program->value_programs)
            goto done;
    }
    for (index = 0u; index < program->value_program_len; index++) {
        ppguarded_lex_cursor_v1_value_program_init(
            &program->value_programs[index]);
    }
    for (index = 0u; index < base_start_len; index++) {
        bool unsupported_dependency = false;

        if (base_conflicts[index] != 0u)
            continue;
        if (!ppguarded_lex_cursor_v1_value_program_build(
                &base_grammar, base_start_ids[index], scalar_terminals,
                pack, NULL, base_start_ids, NULL, base_start_len,
                &unsupported_dependency,
                &program->value_programs[value_write],
                error_buf, error_buf_size)) {
            goto done;
        }
        base_program_indices[index] = value_write++;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        PPGuardedLexCursorV1Terminal *terminal = &program->terminals[index];
        int32_t base_index;
        bool unsupported_dependency = false;

        if (terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR)
            continue;
        if (terminal->kind ==
                PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN) {
            base_index = ppguarded_lex_cursor_v1_sorted_u32_find(
                base_start_ids, base_start_len,
                terminal->semantic_start_state_id);
            if (base_index < 0)
                goto done;
            terminal->semantic_program_index =
                base_program_indices[base_index];
            continue;
        }
        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN ||
            guarded_conflicts[index] != 0u) {
            continue;
        }
        if (value_write >= program->value_program_len ||
            !ppguarded_lex_cursor_v1_value_program_build(
                &guarded_grammar, terminal->semantic_start_state_id,
                scalar_terminals, pack, guard_plan,
                base_start_ids, base_program_indices, base_start_len,
                &unsupported_dependency,
                &program->value_programs[value_write],
                error_buf, error_buf_size)) {
            if (unsupported_dependency)
                continue;
            goto done;
        }
        terminal->semantic_program_index = value_write++;
    }
    program->value_program_len = value_write;
    outcome = *conflict_len == 0u && value_write == value_program_cap
        ? PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_BUILT
        : PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_PARTIAL;

done:
    free(guarded_conflicts);
    free(base_conflicts);
    free(base_program_indices);
    free(base_start_ids);
    cetta_lp_native_grammar_free(&guarded_grammar);
    cetta_lp_native_grammar_free(&base_grammar);
    if (outcome == PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_ERROR &&
        error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to build cursor value-program family");
    }
    return outcome;
}

bool ppguarded_lex_cursor_v1_program_build(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    PPGuardedLexCursorV1Program *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1Program result;
    PPGuardedLexCursorV1ValueProgramsOutcome value_programs_outcome;
    uint32_t terminal_write = 0u;
    uint32_t index;
    bool ok = false;

    ppguarded_lex_cursor_v1_program_init(&result);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out ||
        !ppguarded_lex_exec_v1_plan_matches(
            pack, start_state, lexical_plan, guard_plan,
            guarded_plan, exec_plan) ||
        !exec_plan->cursor_certificate.eligible ||
        exec_plan->cursor_certificate.failure_mask != 0u ||
        !rsdfa_v1_plan_export_program(
            &exec_plan->dfa, &result.dfa,
            error_buf, error_buf_size) ||
        !cetta_lp_native_slr_prepared_export_program(
            &exec_plan->final_slr, &result.final_slr,
            error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_production_lhs_indices_build(
            &result.final_slr, &result.final_production_lhs_indices,
            error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor program requires an eligible execution plan");
        }
        goto done;
    }
    for (index = 0u; index < result.final_slr.terminal_len; index++) {
        if (ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &result.final_slr, index)) {
            result.terminal_len++;
        }
    }
    if (result.terminal_len !=
        exec_plan->cursor_certificate.active_terminal_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program active SLR columns changed");
        goto done;
    }
    if ((size_t)result.terminal_len >
        SIZE_MAX / sizeof(*result.terminals)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program terminal count overflows allocation");
        goto done;
    }
    result.terminals = calloc(
        result.terminal_len ? result.terminal_len : 1u,
        sizeof(*result.terminals));
    if (!result.terminals)
        goto done;
    for (index = 0u; index < result.final_slr.terminal_len; index++) {
        PPGuardedLexCursorV1Terminal *target;
        uint32_t terminal_id;
        uint32_t source_len = 0u;
        int32_t scalar_index = -1;
        int32_t tag_index = -1;
        uint32_t source_index;

        if (!ppguarded_lex_cursor_v1_slr_terminal_shifts(
                &result.final_slr, index)) {
            continue;
        }
        if (terminal_write >= result.terminal_len)
            goto done;
        target = &result.terminals[terminal_write++];
        terminal_id = result.final_slr.terminals[index];
        *target = (PPGuardedLexCursorV1Terminal){
            .terminal_id = terminal_id,
            .slr_terminal_index = index,
            .dfa_tag = UINT32_MAX,
            .guard_dfa_tag = UINT32_MAX,
            .semantic_value_required = true,
            .semantic_start_state_id = UINT32_MAX,
            .semantic_program_index = UINT32_MAX,
        };
        for (source_index = 0u;
             source_index < exec_plan->scalar_terminals.len;
             source_index++) {
            if (exec_plan->scalar_terminals.terminals[
                    source_index].terminal_id == terminal_id) {
                scalar_index = (int32_t)source_index;
                source_len++;
            }
        }
        for (source_index = 0u;
             source_index <
                 exec_plan->lexical_tag_len + exec_plan->guarded_tag_len;
             source_index++) {
            if (exec_plan->projected_tag_terminal_ids[source_index] ==
                terminal_id) {
                tag_index = (int32_t)source_index;
                source_len++;
            }
        }
        if (source_len != 1u) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor program terminal %u has %u sources among %u SLR "
                "terminals and %u active terminals",
                terminal_id, source_len, result.final_slr.terminal_len,
                exec_plan->cursor_certificate.active_terminal_len);
            goto done;
        }
        if (scalar_index >= 0) {
            const CettaLpNativeUtf8Terminal *source =
                &exec_plan->scalar_terminals.terminals[scalar_index];
            target->kind = PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR;
            target->scalar_kind = source->kind;
            target->scalar = source->kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR
                ? source->scalar : 0u;
            if (source->kind == CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES) {
                if (!ppguarded_lex_cursor_v1_append_ranges(
                        &result, source->ranges, source->range_len,
                        &target->scalar_range_begin,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                target->scalar_range_len = source->range_len;
            } else {
                target->scalar_range_begin = result.range_len;
            }
            target->guard_range_begin = result.range_len;
            continue;
        }
        if (tag_index < 0)
            goto done;
        target->dfa_tag = (uint32_t)tag_index;
        target->scalar_range_begin = result.range_len;
        target->guard_range_begin = result.range_len;
        if ((uint32_t)tag_index < exec_plan->lexical_tag_len) {
            int32_t entry = ppguarded_lex_exec_v1_lexical_entry(
                lexical_plan, (uint32_t)tag_index);
            if (entry < 0)
                goto done;
            target->kind =
                PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN;
            target->semantic_start_state_id =
                lexical_plan->entries[entry].state_id;
            continue;
        }
        {
            uint32_t guarded_local =
                (uint32_t)tag_index - exec_plan->lexical_tag_len;
            uint32_t guard_local;
            uint32_t guard_tag;
            int32_t entry = ppguarded_lex_exec_v1_guarded_entry(
                guarded_plan, guarded_local);
            RSDFAV1TagLanguage language;

            rsdfa_v1_tag_language_init(&language);
            if (entry < 0 || guarded_local >= exec_plan->guarded_tag_len) {
                rsdfa_v1_tag_language_free(&language);
                goto done;
            }
            guard_local = exec_plan->guard_local_for_guarded[guarded_local];
            if (guard_local >= exec_plan->guard_tag_len) {
                rsdfa_v1_tag_language_free(&language);
                goto done;
            }
            guard_tag = exec_plan->lexical_tag_len +
                exec_plan->guarded_tag_len + guard_local;
            if (!rsdfa_v1_plan_tag_language(
                    &exec_plan->dfa, guard_tag, &language,
                    error_buf, error_buf_size) ||
                !language.one_scalar_or_eof || language.is_empty ||
                !ppguarded_lex_cursor_v1_append_ranges(
                    &result, language.first_ranges,
                    language.first_range_len,
                    &target->guard_range_begin,
                    error_buf, error_buf_size)) {
                rsdfa_v1_tag_language_free(&language);
                goto done;
            }
            target->kind =
                PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN;
            target->guard_dfa_tag = guard_tag;
            target->guard_range_len = language.first_range_len;
            target->guard_accepts_eof = language.accepts_eof_empty;
            target->semantic_start_state_id =
                guarded_plan->entries[entry].state_id;
            rsdfa_v1_tag_language_free(&language);
        }
    }
    if (terminal_write != result.terminal_len)
        goto done;
    value_programs_outcome =
        ppguarded_lex_cursor_v1_value_programs_build(
            &result, pack, lexical_plan, guard_plan,
            &exec_plan->scalar_terminals,
            &result.value_program_conflict_len,
            error_buf, error_buf_size);
    if (value_programs_outcome ==
            PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_ERROR) {
        goto done;
    }
    result.value_programs_complete = value_programs_outcome ==
        PPGUARDED_LEX_CURSOR_VALUE_PROGRAMS_BUILT;
    memcpy(result.base_pack_digest, exec_plan->base_pack_digest, 65u);
    memcpy(result.lexical_plan_digest, exec_plan->lexical_plan_digest, 65u);
    memcpy(result.guard_plan_digest, exec_plan->guard_plan_digest, 65u);
    memcpy(result.guarded_plan_digest, exec_plan->guarded_plan_digest, 65u);
    memcpy(result.execution_plan_digest,
           exec_plan->execution_plan_digest, 65u);
    memcpy(result.certificate_digest,
           exec_plan->cursor_certificate.digest, 65u);
    ppguarded_lex_cursor_v1_program_digest(
        &result, result.program_digest);
    if (!ppguarded_lex_cursor_v1_program_validate(
            &result, error_buf, error_buf_size)) {
        goto done;
    }
    ppguarded_lex_cursor_v1_program_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppguarded_lex_cursor_v1_program_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to build cursor program");
    }
    return ok;
}

typedef struct {
    bool present;
    bool work_limit;
    bool reselect_after_reduction;
    uint32_t source_index;
    uint32_t end_scalar;
    uint32_t end_byte;
    uint64_t dfa_work_item_len;
} PPGuardedLexCursorV1Lookahead;

typedef struct {
    bool valid;
    uint32_t cursor;
    RSDFAV1CursorScanResult scan;
} PPGuardedLexCursorV1DfaScanCache;

static bool ppguarded_lex_cursor_v1_range_contains(
    const CettaLpNativeUnicodeRange *ranges,
    uint32_t len,
    uint32_t scalar) {
    uint32_t low = 0u;
    uint32_t high = len;

    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (scalar < ranges[middle].low) {
            high = middle;
        } else if (scalar > ranges[middle].high) {
            low = middle + 1u;
        } else {
            return true;
        }
    }
    return false;
}

static bool ppguarded_lex_cursor_v1_scalar_matches(
    const PPGuardedLexCursorV1Program *program,
    const PPGuardedLexCursorV1Terminal *terminal,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t cursor) {
    if (terminal->kind != PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR)
        return false;
    switch (terminal->scalar_kind) {
    case CETTA_LP_NATIVE_UTF8_TERMINAL_ANY:
        return cursor < view->scalar_len;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_EOF:
        return cursor == view->scalar_len;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR:
        return cursor < view->scalar_len &&
            cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor) ==
                terminal->scalar;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES:
        return cursor < view->scalar_len &&
            ppguarded_lex_cursor_v1_range_contains(
                &program->ranges[terminal->scalar_range_begin],
                terminal->scalar_range_len,
                cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor));
    }
    return false;
}

static bool ppguarded_lex_cursor_v1_guard_matches(
    const PPGuardedLexCursorV1Program *program,
    const PPGuardedLexCursorV1Terminal *terminal,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t cursor) {
    if (cursor == view->scalar_len)
        return terminal->guard_accepts_eof;
    return ppguarded_lex_cursor_v1_range_contains(
        &program->ranges[terminal->guard_range_begin],
        terminal->guard_range_len,
        cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor));
}

static bool ppguarded_lex_cursor_v1_select(
    const PPGuardedLexCursorV1Program *program,
    const PPGuardedLexCursorV1DispatchIndex *dispatch_index,
    const RSDFAV1AsciiTransitionIndex *ascii_index,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t state,
    uint32_t cursor,
    uint64_t work_limit,
    RSDFAV1Token *dfa_tokens,
    PPGuardedLexCursorV1DfaScanCache *dfa_scan_cache,
    PPGuardedLexCursorV1Lookahead *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1Lookahead result = {0};
    CettaLpNativeSlrProgramAction selected_action = {
        .kind = CETTA_LP_NATIVE_SLR_PROGRAM_ERROR,
        .value = 0,
    };
    uint32_t match_len = 0u;
    uint32_t candidate;
    uint32_t candidate_end;

    if (!program || !dispatch_index ||
        state >= program->final_slr.summary.state_len ||
        state >= dispatch_index->state_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor selection escaped its parser states");
        return false;
    }
    candidate = dispatch_index->scalar_offsets[state];
    candidate_end = dispatch_index->scalar_offsets[state + 1u];
    for (; candidate < candidate_end; candidate++) {
        uint32_t source_index =
            dispatch_index->scalar_source_indices[candidate];
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[source_index];
        uint32_t columns = program->final_slr.terminal_len + 1u;
        const CettaLpNativeSlrProgramAction *action =
            &program->final_slr.actions[
                state * columns + terminal->slr_terminal_index];
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
            continue;
        }
        if (!ppguarded_lex_cursor_v1_scalar_matches(
                program, terminal, view, cursor)) {
            continue;
        }
        if (match_len > 0u &&
            (selected_action.kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
             action->kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
             selected_action.value != action->value)) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor scalar lookahead is not functionally determined");
            return false;
        }
        if (match_len == 0u) {
            result.present = true;
            result.source_index = source_index;
            result.end_scalar = terminal->scalar_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_EOF
                ? cursor : cursor + 1u;
            result.end_byte =
                cetta_lp_native_utf8_scalar_view_byte_offset(
                    view, result.end_scalar);
            selected_action = *action;
        }
        match_len++;
    }
    if (match_len > 0u) {
        result.reselect_after_reduction = match_len > 1u;
        *out = result;
        return true;
    }
    {
        RSDFAV1CursorScanResult scan;
        if (dfa_scan_cache && dfa_scan_cache->valid &&
            dfa_scan_cache->cursor == cursor) {
            scan = dfa_scan_cache->scan;
            if (scan.outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor DFA scan cache retained an incomplete result");
                return false;
            }
            if (scan.work_item_len > work_limit) {
                result.dfa_work_item_len = work_limit;
                result.work_limit = true;
                *out = result;
                return true;
            }
        } else {
            if (!rsdfa_v1_program_scan_cursor_longest_indexed_prevalidated(
                    &program->dfa, ascii_index, view, cursor, work_limit,
                    dfa_tokens, program->dfa.tag_len, &scan,
                    error_buf, error_buf_size)) {
                return false;
            }
            if (dfa_scan_cache &&
                scan.outcome == RSDFA_V1_CURSOR_SCAN_COMPLETED) {
                dfa_scan_cache->valid = true;
                dfa_scan_cache->cursor = cursor;
                dfa_scan_cache->scan = scan;
            }
        }
        result.dfa_work_item_len = scan.work_item_len;
        if (scan.outcome == RSDFA_V1_CURSOR_SCAN_WORK_LIMIT) {
            result.work_limit = true;
            *out = result;
            return true;
        }
        candidate = dispatch_index->span_offsets[state];
        candidate_end = dispatch_index->span_offsets[state + 1u];
        for (; candidate < candidate_end; candidate++) {
            uint32_t source_index =
                dispatch_index->span_source_indices[candidate];
            const PPGuardedLexCursorV1Terminal *terminal =
                &program->terminals[source_index];
            uint32_t columns = program->final_slr.terminal_len + 1u;
            const CettaLpNativeSlrProgramAction *action =
                &program->final_slr.actions[
                    state * columns + terminal->slr_terminal_index];
            uint32_t token_index;
            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
                continue;
            }
            for (token_index = 0u; token_index < scan.accept_len;
                 token_index++) {
                const RSDFAV1Token *token = &dfa_tokens[token_index];
                if (token->tag != terminal->dfa_tag)
                    continue;
                if (token->end_scalar <= cursor ||
                    (terminal->kind ==
                         PPGUARDED_LEX_CURSOR_TERMINAL_GUARDED_SPAN &&
                     !ppguarded_lex_cursor_v1_guard_matches(
                         program, terminal, view, token->end_scalar))) {
                    break;
                }
                if (match_len > 0u &&
                    (selected_action.kind !=
                         CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
                     action->kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
                     selected_action.value != action->value)) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "cursor span lookahead is not functionally determined");
                    return false;
                }
                if (match_len == 0u) {
                    result.present = true;
                    result.source_index = source_index;
                    result.end_scalar = token->end_scalar;
                    result.end_byte = token->end_byte;
                    selected_action = *action;
                }
                match_len++;
                break;
            }
        }
    }
    result.reselect_after_reduction = match_len > 1u;
    *out = result;
    return true;
}

static void ppguarded_lex_cursor_v1_trace_u64(
    CettaNativeSha256 *sha,
    uint64_t value) {
    ppguarded_lex_exec_v1_sha_u32(sha, (uint32_t)(value >> 32u));
    ppguarded_lex_exec_v1_sha_u32(sha, (uint32_t)value);
}

typedef struct {
    uint32_t left;
    uint32_t right;
    bool present;
} PPGuardedLexCursorV1SourceSpan;

static bool ppguarded_lex_cursor_v1_emit_event(
    const PPGuardedLexCursorV1EventSink *sink,
    const PPGuardedLexCursorV1Event *event,
    char *error_buf,
    size_t error_buf_size) {
    if (!sink)
        return true;
    if (!sink->emit ||
        !sink->emit(sink->context, event, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor occurrence consumer rejected an event");
        }
        return false;
    }
    return true;
}

void ppguarded_lex_cursor_v1_semantic_result_init(
    PPGuardedLexCursorV1SemanticResult *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    arena_init(&result->arena);
}

void ppguarded_lex_cursor_v1_semantic_result_free(
    PPGuardedLexCursorV1SemanticResult *result) {
    if (!result)
        return;
    arena_free(&result->arena);
    memset(result, 0, sizeof(*result));
}

void ppguarded_lex_cursor_v1_projected_result_init(
    PPGuardedLexCursorV1ProjectedResult *result) {
    if (result)
        memset(result, 0, sizeof(*result));
}

static bool ppguarded_lex_cursor_v1_semantic_lattice_matches_view(
    const CettaLpNativeUtf8Lattice *lattice,
    const CettaLpNativeUtf8ScalarView *view) {
    uint32_t index;

    if (!lattice || !view ||
        lattice->scalar_len != view->scalar_len ||
        lattice->input_byte_len != view->input_byte_len ||
        lattice->start_offset_len != view->scalar_len + 2u ||
        (view->scalar_len > 0u && !lattice->codepoints) ||
        !lattice->byte_offsets ||
        !cetta_lp_native_utf8_scalar_view_has_storage(view)) {
        return false;
    }
    for (index = 0u; index < view->scalar_len; index++) {
        if (lattice->codepoints[index] !=
                cetta_lp_native_utf8_scalar_view_scalar_at(view, index) ||
            lattice->byte_offsets[index] !=
                cetta_lp_native_utf8_scalar_view_byte_offset(view, index)) {
            return false;
        }
    }
    return lattice->byte_offsets[view->scalar_len] ==
        cetta_lp_native_utf8_scalar_view_byte_offset(
            view, view->scalar_len);
}

static Atom *ppguarded_lex_cursor_v1_scalar_value_kind(
    Arena *arena,
    CettaLpNativeUtf8TerminalKind scalar_kind,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right) {
    Atom **items;

    if (scalar_kind == CETTA_LP_NATIVE_UTF8_TERMINAL_EOF) {
        if (left != view->scalar_len || right != left)
            return NULL;
        return atom_symbol(arena, "eof");
    }
    if (left >= view->scalar_len || right != left + 1u)
        return NULL;
    items = arena_alloc(arena, sizeof(*items) * 2u);
    if (!items)
        return NULL;
    items[0] = atom_symbol(arena, "cp");
    items[1] = atom_int(
        arena,
        (int64_t)cetta_lp_native_utf8_scalar_view_scalar_at(view, left));
    if (!items[0] || !items[1])
        return NULL;
    return atom_expr(arena, items, 2u);
}

static Atom *ppguarded_lex_cursor_v1_span_value(
    Arena *arena,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint32_t terminal_id,
    uint32_t left,
    uint32_t right) {
    const CettaLpNativeUtf8LatticeEdge *match = NULL;
    uint32_t index;
    uint32_t end;

    if (!lattice || left > lattice->scalar_len ||
        left + 1u >= lattice->start_offset_len) {
        return NULL;
    }
    end = lattice->start_offsets[left + 1u];
    for (index = lattice->start_offsets[left]; index < end; index++) {
        const CettaLpNativeUtf8LatticeEdge *edge = &lattice->edges[index];
        if (edge->terminal_id != terminal_id ||
            edge->scalar_left != left || edge->scalar_right != right) {
            continue;
        }
        if (match)
            return NULL;
        match = edge;
    }
    if (!match ||
        match->value_kind != CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS ||
        match->value >= witness_len || !witness_values ||
        !witness_values[match->value]) {
        return NULL;
    }
    return atom_deep_copy(arena, witness_values[match->value]);
}

static Atom *ppguarded_lex_cursor_v1_wrap_result(
    Arena *arena,
    Atom *value) {
    Atom **items;

    if (!value)
        return NULL;
    items = arena_alloc(arena, sizeof(*items) * 3u);
    if (!items)
        return NULL;
    items[0] = atom_symbol(arena, "result");
    items[1] = value;
    items[2] = atom_symbol(arena, "nil");
    if (!items[0] || !items[2])
        return NULL;
    return atom_expr(arena, items, 3u);
}

static uint64_t ppguarded_lex_cursor_v1_work_total(
    const PPGuardedLexCursorV1Receipt *receipt,
    uint32_t terminal_value_len,
    uint64_t instruction_len,
    uint32_t value_program_run_len,
    uint32_t value_terminal_value_len,
    uint64_t value_instruction_len,
    uint64_t value_parser_work_item_len,
    uint64_t semantic_mask_work_item_len) {
    uint64_t total = receipt->dfa_work_item_len;

    if (UINT64_MAX - total < receipt->parser_work_item_len)
        return UINT64_MAX;
    total += receipt->parser_work_item_len;
    if (UINT64_MAX - total < terminal_value_len)
        return UINT64_MAX;
    total += terminal_value_len;
    if (UINT64_MAX - total < instruction_len)
        return UINT64_MAX;
    total += instruction_len;
    if (UINT64_MAX - total < value_program_run_len)
        return UINT64_MAX;
    total += value_program_run_len;
    if (UINT64_MAX - total < value_terminal_value_len)
        return UINT64_MAX;
    total += value_terminal_value_len;
    if (UINT64_MAX - total < value_instruction_len)
        return UINT64_MAX;
    total += value_instruction_len;
    if (UINT64_MAX - total < value_parser_work_item_len)
        return UINT64_MAX;
    total += value_parser_work_item_len;
    if (UINT64_MAX - total < semantic_mask_work_item_len)
        return UINT64_MAX;
    return total + semantic_mask_work_item_len;
}

static uint64_t ppguarded_lex_cursor_v1_run_work_total(
    const PPGuardedLexCursorV1Receipt *receipt,
    bool semantic_mode,
    uint32_t terminal_value_len,
    uint64_t instruction_len,
    uint32_t value_program_run_len,
    uint32_t value_terminal_value_len,
    uint64_t value_instruction_len,
    uint64_t value_parser_work_item_len,
    uint64_t semantic_mask_work_item_len,
    uint64_t semantic_occurrence_work_item_len) {
    uint64_t total;

    if (!semantic_mode) {
        total = receipt->dfa_work_item_len;
        if (UINT64_MAX - total < receipt->parser_work_item_len)
            return UINT64_MAX;
        total += receipt->parser_work_item_len;
    } else {
        total = ppguarded_lex_cursor_v1_work_total(
            receipt, terminal_value_len, instruction_len,
            value_program_run_len, value_terminal_value_len,
            value_instruction_len, value_parser_work_item_len,
            semantic_mask_work_item_len);
    }
    if (UINT64_MAX - total < semantic_occurrence_work_item_len)
        return UINT64_MAX;
    return total + semantic_occurrence_work_item_len;
}

typedef union {
    Atom *atom;
    PPAtomProjectionActionV1Value projection;
    PPGuardedLexCursorV1SourceSpan source_span;
} PPGuardedLexCursorV1SemanticValue;

typedef enum {
    PPGUARDED_LEX_CURSOR_SPAN_UNHANDLED = 0,
    PPGUARDED_LEX_CURSOR_SPAN_ACCEPTED = 1,
    PPGUARDED_LEX_CURSOR_SPAN_WORK_LIMIT = 2
} PPGuardedLexCursorV1SpanOutcome;

typedef bool (*PPGuardedLexCursorV1ScalarValueFn)(
    void *context,
    CettaLpNativeUtf8TerminalKind scalar_kind,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPGuardedLexCursorV1NeutralValueFn)(
    void *context,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPGuardedLexCursorV1ActionValueFn)(
    void *context,
    uint32_t production_id,
    const PPGuardedLexCursorV1SemanticValue *slots,
    uint32_t slot_len,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size);

typedef bool (*PPGuardedLexCursorV1SpanValueFn)(
    void *context,
    uint32_t terminal_index,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticValue *out,
    PPGuardedLexCursorV1SpanOutcome *outcome,
    uint64_t *work_item_len,
    char *error_buf,
    size_t error_buf_size);

typedef struct {
    void *context;
    Arena *atom_arena;
    PPGuardedLexCursorV1ScalarValueFn scalar;
    PPGuardedLexCursorV1NeutralValueFn neutral;
    PPGuardedLexCursorV1ActionValueFn action;
    PPGuardedLexCursorV1SpanValueFn span;
} PPGuardedLexCursorV1SemanticBackend;

typedef struct {
    const PPActionBytecodeV1Program *program;
    Arena *arena;
} PPGuardedLexCursorV1AtomBackend;

typedef struct {
    const PPAtomProjectionActionV1Program *program;
    const PPAtomProjectionV1Plan *projection;
    PPAtomProjectionActionV1Store *store;
    const PPGuardedLexCursorV1Program *cursor;
    const PPGuardedLexCursorV1SemanticMaskBinding *semantic_masks;
    uint32_t *mask_actions;
    uint32_t *retained_scalars;
    uint32_t scratch_capacity;
    uint32_t semantic_mask_run_len;
    uint32_t semantic_mask_input_scalar_len;
    uint32_t semantic_mask_retained_scalar_len;
    uint64_t semantic_mask_work_item_len;
} PPGuardedLexCursorV1ProjectionBackend;

static bool ppguarded_lex_cursor_v1_atom_scalar(
    void *raw,
    CettaLpNativeUtf8TerminalKind scalar_kind,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1AtomBackend *context = raw;

    out->atom = ppguarded_lex_cursor_v1_scalar_value_kind(
        context->arena, scalar_kind, view, left, right);
    if (!out->atom) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor scalar has no neutral semantic value");
        return false;
    }
    return true;
}

static bool ppguarded_lex_cursor_v1_atom_neutral(
    void *raw,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1AtomBackend *context = raw;

    out->atom = atom_symbol(context->arena, "unused");
    if (!out->atom) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate an unobserved cursor value");
        return false;
    }
    return true;
}

static bool ppguarded_lex_cursor_v1_atom_action(
    void *raw,
    uint32_t production_id,
    const PPGuardedLexCursorV1SemanticValue *slots,
    uint32_t slot_len,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1AtomBackend *context = raw;
    Atom *local[16] = {0};
    Atom **values = local;
    uint32_t index;
    bool owned = false;
    bool ok;

    if (slot_len > sizeof(local) / sizeof(local[0])) {
        values = malloc(sizeof(*values) * (size_t)slot_len);
        if (!values) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cannot allocate neutral action slots");
            return false;
        }
        owned = true;
    }
    for (index = 0u; index < slot_len; index++)
        values[index] = slots[index].atom;
    ok = pp_action_bytecode_v1_execute_prevalidated(
        context->program, production_id,
        values, slot_len, context->arena, &out->atom,
        error_buf, error_buf_size);
    if (owned)
        free(values);
    return ok;
}

static bool ppguarded_lex_cursor_v1_projection_scalar(
    void *raw,
    CettaLpNativeUtf8TerminalKind scalar_kind,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    (void)raw;

    if (scalar_kind == CETTA_LP_NATIVE_UTF8_TERMINAL_EOF) {
        if (left != view->scalar_len || right != left) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "compact EOF terminal has the wrong span");
            return false;
        }
        return ppatom_projection_action_v1_eof(
            &out->projection, error_buf, error_buf_size);
    }
    if (left >= view->scalar_len || right != left + 1u) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "compact scalar terminal has the wrong span");
        return false;
    }
    return ppatom_projection_action_v1_scalar(
        cetta_lp_native_utf8_scalar_view_scalar_at(view, left),
        &out->projection,
        error_buf, error_buf_size);
}

static bool ppguarded_lex_cursor_v1_projection_neutral(
    void *raw,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    (void)raw;

    return ppatom_projection_action_v1_eof(
        &out->projection, error_buf, error_buf_size);
}

static bool ppguarded_lex_cursor_v1_projection_action(
    void *raw,
    uint32_t production_id,
    const PPGuardedLexCursorV1SemanticValue *slots,
    uint32_t slot_len,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1ProjectionBackend *context = raw;
    const PPAtomProjectionActionV1Production *production;

    if (!context || !context->program ||
        production_id >= context->program->production_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "compact action escaped its prepared production inventory");
        return false;
    }
    production = &context->program->productions[production_id];
    if (production->execution_kind ==
            PPATOM_PROJECTION_ACTION_V1_EXECUTE_VALUE) {
        const PPAtomProjectionActionV1Instruction *instruction;

        if (slot_len != production->arity ||
            production->instruction_len != 1u ||
            production->instruction_begin >=
                context->program->instruction_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "prepared compact value action is malformed");
            return false;
        }
        instruction = &context->program->instructions[
            production->instruction_begin];
        if (instruction->kind == PPATOM_PROJECTION_ACTION_V1_PUSH_SLOT) {
            if (!slots || instruction->operand >= slot_len) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "prepared compact value slot is malformed");
                return false;
            }
            out->projection = slots[instruction->operand].projection;
            return true;
        }
        if (instruction->kind ==
                PPATOM_PROJECTION_ACTION_V1_PUSH_CONSTANT) {
            out->projection = (PPAtomProjectionActionV1Value){
                .kind = PPATOM_PROJECTION_ACTION_V1_VALUE_CONSTANT,
                .operand = production->instruction_begin,
            };
            return true;
        }
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "prepared compact value instruction is malformed");
        return false;
    }

    return ppatom_projection_action_v1_execute_specialized_view_prevalidated(
        context->program, production_id,
        slots, slot_len, sizeof(*slots),
        context->store, &out->projection,
        error_buf, error_buf_size);
}

static bool ppguarded_lex_cursor_v1_projection_reserve_mask_scratch(
    PPGuardedLexCursorV1ProjectionBackend *context,
    uint32_t needed,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t capacity;
    uint32_t *actions;
    uint32_t *scalars;

    if (needed <= context->scratch_capacity)
        return true;
    capacity = context->scratch_capacity ? context->scratch_capacity : 64u;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if ((size_t)capacity > SIZE_MAX / sizeof(*actions)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask scratch capacity overflow");
        return false;
    }
    actions = malloc(sizeof(*actions) * (size_t)capacity);
    scalars = malloc(sizeof(*scalars) * (size_t)capacity);
    if (!actions || !scalars) {
        free(actions);
        free(scalars);
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cannot allocate semantic-mask cursor scratch");
        return false;
    }
    free(context->mask_actions);
    free(context->retained_scalars);
    context->mask_actions = actions;
    context->retained_scalars = scalars;
    context->scratch_capacity = capacity;
    return true;
}

static bool ppguarded_lex_cursor_v1_projection_span(
    void *raw,
    uint32_t terminal_index,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticValue *out,
    PPGuardedLexCursorV1SpanOutcome *outcome,
    uint64_t *work_item_len,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1ProjectionBackend *context = raw;
    const PPGuardedLexCursorV1SemanticMaskBinding *binding;
    PPSemanticMaskDfaV1RunResult run;
    uint32_t tag;
    uint32_t span_len;
    uint32_t retained_len = 0u;
    uint32_t index;

    if (outcome)
        *outcome = PPGUARDED_LEX_CURSOR_SPAN_UNHANDLED;
    if (work_item_len)
        *work_item_len = 0u;
    if (!context || !context->cursor || !context->projection || !view ||
        !out || !outcome ||
        !work_item_len || terminal_index >= context->cursor->terminal_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-mask cursor span inputs");
        return false;
    }
    binding = context->semantic_masks;
    if (!binding)
        return true;
    if (terminal_index >= binding->terminal_len ||
        !binding->terminal_tags || !binding->text_bindings ||
        !binding->action_wrapper_indices ||
        left > right || right > view->scalar_len || work_limit == 0u) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask cursor span escaped its prepared binding");
        return false;
    }
    tag = binding->terminal_tags[terminal_index];
    if (tag == UINT32_MAX)
        return true;
    span_len = right - left;
    if (!ppguarded_lex_cursor_v1_projection_reserve_mask_scratch(
            context, span_len, error_buf, error_buf_size) ||
        !ppsemantic_mask_trace_dfa_v1_run_exact_prevalidated(
            &binding->transducer, view, left, right, tag, work_limit,
            context->mask_actions, context->scratch_capacity, &run,
            error_buf, error_buf_size)) {
        return false;
    }
    *work_item_len = run.work_item_len;
    if (UINT64_MAX - context->semantic_mask_work_item_len <
            run.work_item_len ||
        context->semantic_mask_run_len == UINT32_MAX ||
        UINT32_MAX - context->semantic_mask_input_scalar_len < span_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "semantic-mask cursor receipt overflow");
        return false;
    }
    context->semantic_mask_work_item_len += run.work_item_len;
    context->semantic_mask_run_len++;
    context->semantic_mask_input_scalar_len += span_len;
    if (run.outcome == PPSEMANTIC_MASK_DFA_V1_WORK_LIMIT) {
        *outcome = PPGUARDED_LEX_CURSOR_SPAN_WORK_LIMIT;
        return true;
    }
    if (run.outcome != PPSEMANTIC_MASK_DFA_V1_ACCEPTED ||
        run.action_len != span_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "selected cursor span disagrees with its semantic-mask root");
        return false;
    }
    for (index = 0u; index < span_len;) {
        uint32_t action_index = context->mask_actions[index];
        const PPSemanticMaskV1Action *action;

        if (action_index >= binding->mask.action_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask DFA emitted an unknown action");
            return false;
        }
        action = &binding->mask.actions[action_index];
        if (action->kind == PPSEMANTIC_MASK_V1_COPY) {
            context->retained_scalars[retained_len++] =
                cetta_lp_native_utf8_scalar_view_scalar_at(
                    view, left + index);
            index++;
        } else if (action->kind == PPSEMANTIC_MASK_V1_DROP) {
            index++;
        } else if (action->kind == PPSEMANTIC_MASK_V1_WRAPPER) {
            uint32_t wrapper_index =
                binding->action_wrapper_indices[action_index];
            uint32_t payload_begin = retained_len;
            uint32_t payload_len = 0u;
            uint32_t decoded;

            if (wrapper_index == UINT32_MAX) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "semantic-mask wrapper escaped its projection binding");
                return false;
            }
            do {
                context->retained_scalars[payload_begin + payload_len] =
                    cetta_lp_native_utf8_scalar_view_scalar_at(
                        view, left + index);
                payload_len++;
                index++;
                if (index >= span_len)
                    break;
                action_index = context->mask_actions[index];
            } while (
                action_index < binding->mask.action_len &&
                binding->mask.actions[action_index].kind ==
                    PPSEMANTIC_MASK_V1_WRAPPER &&
                binding->action_wrapper_indices[action_index] ==
                    wrapper_index);
            if (!ppatom_projection_v1_apply_wrapper_scalars_prevalidated(
                    context->projection, wrapper_index,
                    &context->retained_scalars[payload_begin], payload_len,
                    &decoded, error_buf, error_buf_size)) {
                return false;
            }
            context->retained_scalars[retained_len++] = decoded;
        } else {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask cursor selected an unknown action kind");
            return false;
        }
    }
    if (UINT32_MAX - context->semantic_mask_retained_scalar_len <
            retained_len ||
        !ppatom_projection_action_v1_text_node_prevalidated(
            context->program, &binding->text_bindings[terminal_index],
            context->retained_scalars, retained_len, context->store,
            &out->projection, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "semantic-mask retained-scalar receipt overflow");
        }
        return false;
    }
    context->semantic_mask_retained_scalar_len += retained_len;
    *outcome = PPGUARDED_LEX_CURSOR_SPAN_ACCEPTED;
    return true;
}

static bool ppguarded_lex_cursor_v1_finish_atom_result(
    PPGuardedLexCursorV1SemanticResult *result,
    PPGuardedLexCursorV1SemanticValue root,
    char *error_buf,
    size_t error_buf_size) {
    if (!result)
        return false;
    result->semantic_result = NULL;
    if (result->receipt.outcome != PPGUARDED_LEX_CURSOR_V1_ACCEPTED)
        return true;
    result->semantic_result = ppguarded_lex_cursor_v1_wrap_result(
        &result->arena, root.atom);
    if (!result->semantic_result) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cannot wrap accepted cursor semantic value");
        return false;
    }
    return true;
}

typedef enum {
    PPGUARDED_LEX_CURSOR_VALUE_REJECTED = 0,
    PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED = 1,
    PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT = 2
} PPGuardedLexCursorV1ValueOutcome;

typedef struct {
    const PPGuardedLexCursorV1Program *owner;
    const PPGuardedLexCursorV1SemanticBackend *backend;
    const CettaLpNativeUtf8ScalarView *view;
    const PPGuardedLexCursorV1PreparedFallback *prepared_fallback;
    Arena *arena;
    uint64_t work_limit;
    uint64_t base_work;
    uint32_t program_run_len;
    uint32_t terminal_value_len;
    uint32_t action_execution_len;
    uint64_t action_instruction_len;
    uint64_t parser_work_item_len;
    uint32_t prepared_fallback_run_len;
    uint64_t prepared_fallback_work_item_len;
} PPGuardedLexCursorV1ValueContext;

static uint64_t ppguarded_lex_cursor_v1_value_work_total(
    const PPGuardedLexCursorV1ValueContext *context) {
    uint64_t total = context->base_work;

#define PPGUARDED_LEX_CURSOR_ADD_WORK(value) \
    do { \
        uint64_t add_work = (uint64_t)(value); \
        if (UINT64_MAX - total < add_work) \
            return UINT64_MAX; \
        total += add_work; \
    } while (0)
    PPGUARDED_LEX_CURSOR_ADD_WORK(context->program_run_len);
    PPGUARDED_LEX_CURSOR_ADD_WORK(context->terminal_value_len);
    PPGUARDED_LEX_CURSOR_ADD_WORK(context->action_instruction_len);
    PPGUARDED_LEX_CURSOR_ADD_WORK(context->parser_work_item_len);
#undef PPGUARDED_LEX_CURSOR_ADD_WORK
    return total;
}

static bool ppguarded_lex_cursor_v1_value_scalar_matches(
    const PPGuardedLexCursorV1ValueProgram *program,
    const PPGuardedLexCursorV1ValueTerminal *terminal,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t cursor,
    uint32_t right,
    bool zero_width_shifted) {
    if (terminal->kind !=
            PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
        return false;
    }
    switch (terminal->scalar_kind) {
    case CETTA_LP_NATIVE_UTF8_TERMINAL_ANY:
        return cursor < right;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_EOF:
        return !zero_width_shifted && cursor == right &&
            cursor == view->scalar_len;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR:
        return cursor < right &&
            cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor) ==
                terminal->scalar;
    case CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES:
        return cursor < right &&
            ppguarded_lex_cursor_v1_range_contains(
                &program->ranges[terminal->range_begin],
                terminal->range_len,
                cetta_lp_native_utf8_scalar_view_scalar_at(view, cursor));
    }
    return false;
}

static int32_t ppguarded_lex_cursor_v1_value_scalar_dispatch_find(
    const PPGuardedLexCursorV1ValueProgram *program,
    uint32_t scalar) {
    uint32_t low = 0u;
    uint32_t high;

    if (!program || !program->scalar_dispatch_ranges ||
        program->scalar_dispatch_range_len == 0u)
        return -1;
    high = program->scalar_dispatch_range_len;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        const CettaLpNativeUnicodeRange *range =
            &program->scalar_dispatch_ranges[middle];

        if (scalar < range->low)
            high = middle;
        else if (scalar > range->high)
            low = middle + 1u;
        else
            return (int32_t)middle;
    }
    return -1;
}

static bool ppguarded_lex_cursor_v1_value_stack_grow(
    uint32_t **states,
    PPGuardedLexCursorV1SemanticValue **values,
    uint32_t state_len,
    uint32_t value_len,
    uint32_t *state_cap,
    uint32_t *local_states,
    PPGuardedLexCursorV1SemanticValue *local_values,
    char *error_buf,
    size_t error_buf_size) {
    uint32_t next_cap;
    uint32_t *next_states;
    PPGuardedLexCursorV1SemanticValue *next_values;

    if (!states || !*states || !values || !*values || !state_cap ||
        state_len > *state_cap || value_len >= *state_cap) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor value program stack state is malformed");
        return false;
    }
    next_cap = *state_cap * 2u;
    if (next_cap < *state_cap ||
        (size_t)next_cap > SIZE_MAX / sizeof(**states) ||
        (size_t)(next_cap - 1u) > SIZE_MAX / sizeof(**values)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor value program stack overflow");
        return false;
    }
    next_states = malloc(sizeof(*next_states) * next_cap);
    next_values = malloc(sizeof(*next_values) * (next_cap - 1u));
    if (!next_states || !next_values) {
        free(next_values);
        free(next_states);
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cannot grow cursor value program stack");
        return false;
    }
    memcpy(next_states, *states, sizeof(*next_states) * state_len);
    memcpy(next_values, *values, sizeof(*next_values) * value_len);
    if (*states != local_states)
        free(*states);
    if (*values != local_values)
        free(*values);
    *states = next_states;
    *values = next_values;
    *state_cap = next_cap;
    return true;
}

static bool ppguarded_lex_cursor_v1_value_program_run(
    PPGuardedLexCursorV1ValueContext *context,
    uint32_t program_index,
    uint32_t left,
    uint32_t right,
    PPGuardedLexCursorV1SemanticValue *out,
    PPGuardedLexCursorV1ValueOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    const PPGuardedLexCursorV1ValueProgram *program;
    const CettaLpNativeSlrProgram *slr;
    uint32_t local_states[32];
    PPGuardedLexCursorV1SemanticValue local_values[31];
    uint8_t local_terminal_matches[64] = {0};
    PPGuardedLexCursorV1SemanticValue
        local_terminal_guard_values[64] = {0};
    uint32_t *states = local_states;
    PPGuardedLexCursorV1SemanticValue *values = local_values;
    uint8_t *terminal_matches = local_terminal_matches;
    PPGuardedLexCursorV1SemanticValue *terminal_guard_values =
        local_terminal_guard_values;
    uint32_t state_len = 1u;
    uint32_t value_len = 0u;
    uint32_t state_cap = 32u;
    uint32_t cursor = left;
    uint32_t scalar_dispatch_index = UINT32_MAX;
    uint32_t columns;
    bool zero_width_shifted = false;
    bool ok = false;

    if (out)
        memset(out, 0, sizeof(*out));
    if (outcome)
        *outcome = PPGUARDED_LEX_CURSOR_VALUE_REJECTED;
    if (!context || !context->owner || !context->view ||
        !context->backend || !context->backend->scalar ||
        !context->backend->action || !out || !outcome ||
        program_index >= context->owner->value_program_len ||
        left > right || right > context->view->scalar_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad cursor value-program run arguments");
        return false;
    }
    if (ppguarded_lex_cursor_v1_value_work_total(context) >=
        context->work_limit) {
        *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
        return true;
    }
    context->program_run_len++;
    program = &context->owner->value_programs[program_index];
    slr = &program->slr;
    columns = slr->terminal_len + 1u;
    if (program->terminal_len > 64u) {
        terminal_matches = calloc(
            program->terminal_len, sizeof(*terminal_matches));
        terminal_guard_values = calloc(
            program->terminal_len, sizeof(*terminal_guard_values));
        if (!terminal_matches || !terminal_guard_values) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cannot allocate cursor value lookahead cache");
            goto done;
        }
    }
    states[0] = 0u;

    for (;;) {
        const CettaLpNativeSlrProgramAction *selected_action = NULL;
        const PPGuardedLexCursorV1ValueTerminal *shift_terminal = NULL;
        PPGuardedLexCursorV1SemanticValue shift_guard_value = {0};
        bool have_shift_guard_value = false;
        uint32_t state;
        uint32_t candidate_index;
        uint32_t candidate_end;
        uint32_t source_index;

        if (value_len + 1u != state_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor value semantic and state stacks diverged");
            goto done;
        }
        if (ppguarded_lex_cursor_v1_value_work_total(context) >=
            context->work_limit) {
            *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
            ok = true;
            goto done;
        }
        state = states[state_len - 1u];
        if (state >= slr->summary.state_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor value program escaped its SLR states");
            goto done;
        }
        if (cursor < right) {
            size_t cell;
            uint32_t shift_source;

            if (scalar_dispatch_index == UINT32_MAX) {
                int32_t found =
                    ppguarded_lex_cursor_v1_value_scalar_dispatch_find(
                        program,
                        cetta_lp_native_utf8_scalar_view_scalar_at(
                            context->view, cursor));
                if (found < 0) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "cursor value scalar escaped its dispatch ranges");
                    goto done;
                }
                scalar_dispatch_index = (uint32_t)found;
            }
            cell = (size_t)state * program->scalar_dispatch_range_len +
                scalar_dispatch_index;
            selected_action = &program->scalar_dispatch_actions[cell];
            shift_source =
                program->scalar_dispatch_shift_sources[cell];
            if (shift_source != UINT32_MAX) {
                if (shift_source >= program->terminal_len ||
                    selected_action->kind !=
                        CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT) {
                    goto malformed_action;
                }
                shift_terminal = &program->terminals[shift_source];
            } else if (selected_action->kind ==
                           CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT) {
                goto malformed_action;
            }
        } else {
            candidate_index = program->state_candidate_offsets[state];
            candidate_end = program->state_candidate_offsets[state + 1u];
            for (; candidate_index < candidate_end; candidate_index++) {
            source_index =
                program->state_candidate_terminal_indices[candidate_index];
            const PPGuardedLexCursorV1ValueTerminal *terminal =
                &program->terminals[source_index];
            const CettaLpNativeSlrProgramAction *action =
                &slr->actions[
                    state * columns + terminal->slr_terminal_index];
            PPGuardedLexCursorV1SemanticValue guard_value = {0};
            bool matches;

            if (terminal_matches[source_index] == 0u) {
                if (terminal->kind ==
                        PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
                    matches =
                        ppguarded_lex_cursor_v1_value_scalar_matches(
                            program, terminal, context->view,
                            cursor, right, zero_width_shifted);
                } else {
                    uint32_t guard_right;
                    PPGuardedLexCursorV1ValueOutcome guard_outcome;

                    matches = false;
                    if (!zero_width_shifted && cursor == right) {
                        guard_right = cursor < context->view->scalar_len
                            ? cursor + 1u : cursor;
                        if (!ppguarded_lex_cursor_v1_value_program_run(
                                context,
                                terminal->guard_value_program_index,
                                cursor, guard_right,
                                &guard_value, &guard_outcome,
                                error_buf, error_buf_size)) {
                            goto done;
                        }
                        if (guard_outcome ==
                                PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT) {
                            *outcome = guard_outcome;
                            ok = true;
                            goto done;
                        }
                        matches = guard_outcome ==
                            PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED;
                    }
                }
                terminal_matches[source_index] = matches ? 2u : 1u;
                terminal_guard_values[source_index] = guard_value;
            }
            if (terminal_matches[source_index] != 2u)
                continue;
            if (selected_action &&
                (selected_action->kind != action->kind ||
                 selected_action->value != action->value)) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor value lookahead has multiple SLR actions");
                goto done;
            }
            selected_action = action;
            if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT) {
                if (shift_terminal) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "cursor value program selected multiple terminals");
                    goto done;
                }
                shift_terminal = terminal;
                shift_guard_value =
                    terminal_guard_values[source_index];
                have_shift_guard_value = true;
            }
        }
        }
        if (!selected_action) {
            if (cursor != right) {
                *outcome = PPGUARDED_LEX_CURSOR_VALUE_REJECTED;
                ok = true;
                goto done;
            }
            selected_action = &slr->actions[
                state * columns + slr->terminal_len];
        }
        if (ppguarded_lex_cursor_v1_value_work_total(context) >=
            context->work_limit) {
            *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
            ok = true;
            goto done;
        }
        context->parser_work_item_len++;
        if (selected_action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
            *outcome = PPGUARDED_LEX_CURSOR_VALUE_REJECTED;
            ok = true;
            goto done;
        }
        if (selected_action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT) {
            if (shift_terminal || cursor != right || value_len != 1u) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor value program accepted without one value");
                goto done;
            }
            *out = values[0];
            *outcome = PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED;
            ok = true;
            goto done;
        }
        if (selected_action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE) {
            const CettaLpNativeSlrProgramProduction *production;
            const PPActionBytecodeV1Production *semantic_action;
            PPGuardedLexCursorV1SemanticValue reduced_value = {0};
            const PPGuardedLexCursorV1SemanticValue *slots;
            uint32_t lhs_index;
            uint32_t target;
            uint64_t work;

            if (selected_action->value < 0 ||
                (uint32_t)selected_action->value >=
                    slr->production_len) {
                goto malformed_action;
            }
            production = &slr->productions[selected_action->value];
            if (!production->authored ||
                production->label >=
                    context->owner->actions.production_len ||
                production->rhs_len >= state_len ||
                production->rhs_len > value_len) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor value reduction has no bound action");
                goto done;
            }
            semantic_action = &context->owner->actions.productions[
                production->label];
            work = ppguarded_lex_cursor_v1_value_work_total(context);
            if (work > context->work_limit ||
                semantic_action->instruction_len >
                    context->work_limit - work) {
                *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
                ok = true;
                goto done;
            }
            slots = production->rhs_len > 0u
                ? &values[value_len - production->rhs_len] : NULL;
            if (!context->backend->action(
                    context->backend->context, production->label,
                    slots, production->rhs_len, &reduced_value,
                    error_buf, error_buf_size)) {
                goto done;
            }
            value_len -= production->rhs_len;
            state_len -= production->rhs_len;
            lhs_index = program->production_lhs_indices[
                (uint32_t)selected_action->value];
            if (lhs_index >= slr->nonterminal_len ||
                slr->nonterminals[lhs_index] != production->lhs) {
                goto malformed_action;
            }
            target = slr->gotos[
                states[state_len - 1u] * slr->nonterminal_len +
                lhs_index];
            if (target == UINT32_MAX ||
                target >= slr->summary.state_len) {
                goto malformed_action;
            }
            if (state_len == state_cap) {
                if (!ppguarded_lex_cursor_v1_value_stack_grow(
                        &states, &values, state_len, value_len,
                        &state_cap, local_states, local_values,
                        error_buf, error_buf_size)) {
                    goto done;
                }
            }
            values[value_len++] = reduced_value;
            states[state_len++] = target;
            context->action_execution_len++;
            context->action_instruction_len +=
                semantic_action->instruction_len;
            continue;
        }
        if (selected_action->kind !=
                CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT ||
            !shift_terminal || selected_action->value < 0 ||
            (uint32_t)selected_action->value >=
                slr->summary.state_len) {
            goto malformed_action;
        }
        if (ppguarded_lex_cursor_v1_value_work_total(context) >=
            context->work_limit) {
            *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
            ok = true;
            goto done;
        }
        if (state_len == state_cap) {
            if (!ppguarded_lex_cursor_v1_value_stack_grow(
                    &states, &values, state_len, value_len,
                    &state_cap, local_states, local_values,
                    error_buf, error_buf_size)) {
                goto done;
            }
        }
        if (shift_terminal->kind ==
                PPGUARDED_LEX_CURSOR_VALUE_TERMINAL_SCALAR) {
            uint32_t end = shift_terminal->scalar_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_EOF
                ? cursor : cursor + 1u;
            if (!context->backend->scalar(
                    context->backend->context,
                    shift_terminal->scalar_kind,
                    context->view, cursor, end,
                    &values[value_len], error_buf, error_buf_size)) {
                goto done;
            }
            if (end == cursor)
                zero_width_shifted = true;
            cursor = end;
        } else {
            if (!have_shift_guard_value || cursor != right ||
                zero_width_shifted) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor guard has no singular semantic value");
                goto done;
            }
            values[value_len] = shift_guard_value;
            zero_width_shifted = true;
        }
        value_len++;
        states[state_len++] = (uint32_t)selected_action->value;
        context->terminal_value_len++;
        scalar_dispatch_index = UINT32_MAX;
        memset(terminal_matches, 0,
               sizeof(*terminal_matches) * program->terminal_len);
        memset(terminal_guard_values, 0,
               sizeof(*terminal_guard_values) * program->terminal_len);
        continue;

malformed_action:
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor value program encountered a malformed SLR action");
        goto done;
    }

done:
    if (terminal_guard_values != local_terminal_guard_values)
        free(terminal_guard_values);
    if (terminal_matches != local_terminal_matches)
        free(terminal_matches);
    if (values != local_values)
        free(values);
    if (states != local_states)
        free(states);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to execute cursor value program");
    }
    return ok;
}

typedef struct {
    const PPGuardedLexCursorV1EventSink *sink;
    const PPActionBytecodeV1Program *actions;
    const CettaLpNativeUtf8ScalarView *view;
    uint32_t owner_source_index;
    uint32_t current_scalar;
} PPGuardedLexCursorV1SpanOccurrenceBackend;

static bool ppguarded_lex_cursor_v1_span_occurrence_scalar(
    void *raw,
    CettaLpNativeUtf8TerminalKind scalar_kind,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t left,
    uint32_t right,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1SpanOccurrenceBackend *context = raw;

    (void)scalar_kind;
    if (!context || !view || context->view != view || !out ||
        left > right || right > view->scalar_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad source-span terminal value");
        return false;
    }
    out->source_span =
        (PPGuardedLexCursorV1SourceSpan){
            .left = left, .right = right, .present = true,
        };
    context->current_scalar = right;
    return true;
}

static bool ppguarded_lex_cursor_v1_span_occurrence_action(
    void *raw,
    uint32_t production_id,
    const PPGuardedLexCursorV1SemanticValue *slots,
    uint32_t slot_len,
    PPGuardedLexCursorV1SemanticValue *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1SpanOccurrenceBackend *context = raw;
    const PPActionBytecodeV1Production *production;
    PPGuardedLexCursorV1SourceSpan local_stack[16] = {0};
    PPGuardedLexCursorV1SourceSpan *stack = local_stack;
    PPGuardedLexCursorV1SourceSpan span = {0};
    PPGuardedLexCursorV1Event event;
    uint32_t index;
    uint32_t stack_len = 0u;
    bool stack_owned = false;
    bool ok = false;

    if (!context || !context->sink || !context->sink->emit ||
        !context->actions || !context->view || !out ||
        production_id >= context->actions->production_len ||
        (slot_len > 0u && !slots)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad source-span production value");
        return false;
    }
    production = &context->actions->productions[production_id];
    if (production->arity != slot_len ||
        production->max_stack_len == 0u ||
        production->instruction_begin >
            context->actions->instruction_len ||
        production->instruction_len >
            context->actions->instruction_len -
                production->instruction_begin) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "source-span action program changed shape");
        return false;
    }
    for (index = 0u; index < slot_len; index++) {
        const PPGuardedLexCursorV1SourceSpan current =
            slots[index].source_span;
        if (!current.present || current.left > current.right ||
            current.right > context->view->scalar_len ||
            (index > 0u &&
             slots[index - 1u].source_span.right > current.left)) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "source-span action received non-monotone slots");
            return false;
        }
    }
    if (production->max_stack_len >
            sizeof(local_stack) / sizeof(local_stack[0])) {
        if ((size_t)production->max_stack_len >
            SIZE_MAX / sizeof(*stack)) {
            goto malformed;
        }
        stack = calloc(production->max_stack_len, sizeof(*stack));
        if (!stack)
            goto done;
        stack_owned = true;
    }
    for (index = 0u; index < production->instruction_len; index++) {
        const PPActionBytecodeV1Instruction *instruction =
            &context->actions->instructions[
                production->instruction_begin + index];
        if (instruction->kind == PP_ACTION_BYTECODE_V1_PUSH_SLOT) {
            if (instruction->operand >= slot_len ||
                stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] =
                slots[instruction->operand].source_span;
            continue;
        }
        if (instruction->kind == PP_ACTION_BYTECODE_V1_PUSH_CONST) {
            if (stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = (PPGuardedLexCursorV1SourceSpan){
                .left = context->current_scalar,
                .right = context->current_scalar,
                .present = false,
            };
            continue;
        }
        if (instruction->kind == PP_ACTION_BYTECODE_V1_APPLY) {
            uint32_t argument_begin;
            uint32_t argument_index;
            PPGuardedLexCursorV1SourceSpan applied = {
                .left = context->current_scalar,
                .right = context->current_scalar,
                .present = false,
            };

            if (stack_len < instruction->operand)
                goto malformed;
            argument_begin = stack_len - instruction->operand;
            for (argument_index = argument_begin;
                 argument_index < stack_len; argument_index++) {
                PPGuardedLexCursorV1SourceSpan argument =
                    stack[argument_index];
                if (!argument.present)
                    continue;
                if (!applied.present) {
                    applied = argument;
                } else {
                    if (argument.left < applied.left)
                        applied.left = argument.left;
                    if (argument.right > applied.right)
                        applied.right = argument.right;
                }
            }
            stack_len = argument_begin;
            if (stack_len >= production->max_stack_len)
                goto malformed;
            stack[stack_len++] = applied;
            continue;
        }
        goto malformed;
    }
    if (stack_len != 1u)
        goto malformed;
    span = stack[0];
    if (!span.present)
        span = (PPGuardedLexCursorV1SourceSpan){
            .left = context->current_scalar,
            .right = context->current_scalar,
            .present = false,
        };
    out->source_span = span;
    event = (PPGuardedLexCursorV1Event){
        .kind = PPGUARDED_LEX_CURSOR_V1_EVENT_SEMANTIC_REDUCE,
        .symbol_id = UINT32_MAX,
        .source_index = context->owner_source_index,
        .production_index = UINT32_MAX,
        .production_label = production_id,
        .rhs_len = slot_len,
        .authored = true,
        .left_scalar = span.left,
        .right_scalar = span.right,
        .left_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
            context->view, span.left),
        .right_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
            context->view, span.right),
    };
    ok = ppguarded_lex_cursor_v1_emit_event(
        context->sink, &event, error_buf, error_buf_size);
    goto done;

malformed:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "source-span action bytecode became malformed");

done:
    if (stack_owned)
        free(stack);
    return ok;
}

static bool ppguarded_lex_cursor_v1_emit_semantic_occurrences(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t source_index,
    uint32_t left,
    uint32_t right,
    const PPGuardedLexCursorV1EventSink *sink,
    uint64_t work_limit,
    uint64_t *work_item_len,
    char *error_buf,
    size_t error_buf_size) {
    const PPGuardedLexCursorV1Terminal *terminal;
    PPGuardedLexCursorV1SpanOccurrenceBackend span_context;
    PPGuardedLexCursorV1SemanticBackend backend;
    PPGuardedLexCursorV1ValueContext value_context;
    PPGuardedLexCursorV1SemanticValue root = {0};
    PPGuardedLexCursorV1ValueOutcome outcome;
    uint64_t used;

    if (work_item_len)
        *work_item_len = 0u;
    if (!program || !view || source_index >= program->terminal_len ||
        !sink || !sink->emit || !sink->semantic_occurrences ||
        !work_item_len || work_limit == 0u || left > right ||
        right > view->scalar_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad semantic-occurrence expansion inputs");
        return false;
    }
    terminal = &program->terminals[source_index];
    if (terminal->semantic_program_index >= program->value_program_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "semantic occurrence has no deterministic value program");
        return false;
    }
    span_context = (PPGuardedLexCursorV1SpanOccurrenceBackend){
        .sink = sink,
        .actions = &program->actions,
        .view = view,
        .owner_source_index = source_index,
        .current_scalar = left,
    };
    backend = (PPGuardedLexCursorV1SemanticBackend){
        .context = &span_context,
        .atom_arena = NULL,
        .scalar = ppguarded_lex_cursor_v1_span_occurrence_scalar,
        .neutral = NULL,
        .action = ppguarded_lex_cursor_v1_span_occurrence_action,
        .span = NULL,
    };
    value_context = (PPGuardedLexCursorV1ValueContext){
        .owner = program,
        .backend = &backend,
        .view = view,
        .prepared_fallback = NULL,
        .arena = NULL,
        .work_limit = work_limit,
    };
    if (!ppguarded_lex_cursor_v1_value_program_run(
            &value_context, terminal->semantic_program_index,
            left, right, &root, &outcome,
            error_buf, error_buf_size)) {
        return false;
    }
    used = ppguarded_lex_cursor_v1_value_work_total(&value_context);
    *work_item_len = used;
    if (outcome == PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "semantic-occurrence expansion exhausted its work limit");
        return false;
    }
    if (outcome != PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED ||
        !root.source_span.present ||
        root.source_span.left < left ||
        root.source_span.right > right) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "semantic-occurrence expansion escaped its terminal span");
        return false;
    }
    return true;
}

static bool ppguarded_lex_cursor_v1_prepared_fallback_value(
    PPGuardedLexCursorV1ValueContext *context,
    const PPGuardedLexCursorV1Terminal *terminal,
    uint32_t left,
    uint32_t right,
    Atom **out,
    PPGuardedLexCursorV1ValueOutcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    const PPGuardedLexCursorV1PreparedFallback *fallback;
    const CettaLpNativeGllPrepared *gll = NULL;
    const CettaLpNativeGlrPrepared *glr = NULL;
    CettaLpNativeUtf8ScalarView token_view;
    CettaLpNativeUtf8Forest forest;
    uint32_t *byte_offsets = NULL;
    uint32_t scalar_len;
    uint32_t byte_base;
    uint32_t index;
    uint64_t used_work;
    uint64_t remaining_work;
    uint32_t parser_limit;
    int32_t root;
    Atom *value = NULL;
    uint32_t value_len = 0u;
    PPNativeV1Outcome replay_outcome = PPNATIVE_V1_COMPLETED;
    bool parsed;
    bool ok = false;

    if (out)
        *out = NULL;
    if (outcome)
        *outcome = PPGUARDED_LEX_CURSOR_VALUE_REJECTED;
    memset(&token_view, 0, sizeof(token_view));
    cetta_lp_native_utf8_forest_init(&forest);
    if (!context || !terminal || !out || !outcome ||
        !(fallback = context->prepared_fallback) ||
        !fallback->pack || !fallback->lexical_plan ||
        !fallback->exec_plan ||
        terminal->kind != PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN ||
        terminal->semantic_program_index != UINT32_MAX ||
        terminal->semantic_start_state_id >= fallback->pack->state_len ||
        left >= right || right > context->view->scalar_len ||
        fallback->replay_depth == 0u || fallback->result_limit == 0u) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad prepared cursor semantic fallback request");
        goto done;
    }
    used_work = ppguarded_lex_cursor_v1_value_work_total(context);
    if (used_work >= context->work_limit) {
        *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
        ok = true;
        goto done;
    }
    remaining_work = context->work_limit - used_work;
    parser_limit = remaining_work > UINT32_MAX
        ? UINT32_MAX : (uint32_t)remaining_work;
    if (parser_limit == 0u) {
        *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
        ok = true;
        goto done;
    }
    scalar_len = right - left;
    if (context->view->ascii_bytes) {
        token_view = (CettaLpNativeUtf8ScalarView){
            .codepoints = NULL,
            .byte_offsets = NULL,
            .scalar_len = scalar_len,
            .input_byte_len = scalar_len,
            .decoded_byte_len = 0u,
            .source_pass_count = 0u,
            .ascii_bytes = &context->view->ascii_bytes[left],
        };
    } else {
        if ((size_t)scalar_len + 1u >
                SIZE_MAX / sizeof(*byte_offsets)) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "prepared cursor semantic fallback span is too large");
            goto done;
        }
        byte_offsets = malloc(
            sizeof(*byte_offsets) * ((size_t)scalar_len + 1u));
        if (!byte_offsets)
            goto done;
        byte_base = cetta_lp_native_utf8_scalar_view_byte_offset(
            context->view, left);
        for (index = 0u; index <= scalar_len; index++) {
            uint32_t absolute =
                cetta_lp_native_utf8_scalar_view_byte_offset(
                    context->view, left + index);
            if (absolute < byte_base) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "prepared cursor semantic fallback offsets regress");
                goto done;
            }
            byte_offsets[index] = absolute - byte_base;
        }
        token_view = (CettaLpNativeUtf8ScalarView){
            .codepoints = &context->view->codepoints[left],
            .byte_offsets = byte_offsets,
            .scalar_len = scalar_len,
            .input_byte_len = byte_offsets[scalar_len],
            .decoded_byte_len = 0u,
            .source_pass_count = 0u,
            .ascii_bytes = NULL,
        };
    }
    if (!ppnative_v1_prepared_start_family_lookup(
            &fallback->exec_plan->ordinary_witness_parsers,
            fallback->lexical_plan->plan_digest,
            terminal->semantic_start_state_id,
            &gll, &glr, error_buf, error_buf_size)) {
        goto done;
    }
    if (fallback->backend == PPGUARDED_LEX_EXEC_V1_GLL) {
        parsed =
            cetta_lp_native_gll_prepared_parse_utf8_scalar_view_forest_complete(
                gll, fallback->exec_plan->scalar_terminals.terminals,
                fallback->exec_plan->scalar_terminals.len,
                &token_view, parser_limit, &forest,
                error_buf, error_buf_size);
    } else {
        parsed = cetta_lp_native_glr_prepared_parse_utf8_scalar_view_forest(
            glr, fallback->exec_plan->scalar_terminals.terminals,
            fallback->exec_plan->scalar_terminals.len,
            &token_view, parser_limit, &forest,
            error_buf, error_buf_size);
    }
    if (!parsed)
        goto done;
    if (context->prepared_fallback_run_len == UINT32_MAX ||
        UINT64_MAX - context->prepared_fallback_work_item_len <
            forest.work_item_len ||
        UINT64_MAX - context->parser_work_item_len <
            forest.work_item_len) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "prepared cursor semantic fallback work overflows");
        goto done;
    }
    context->prepared_fallback_run_len++;
    context->prepared_fallback_work_item_len += forest.work_item_len;
    context->parser_work_item_len += forest.work_item_len;
    if (forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        *outcome = PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT;
        ok = true;
        goto done;
    }
    root = ppguarded_lex_exec_v1_forest_root_find(
        &forest, terminal->semantic_start_state_id, 0u, scalar_len);
    if (root < 0 ||
        !ppguarded_lex_exec_v1_replay_value(
            fallback->pack,
            fallback->pack->states[
                terminal->semantic_start_state_id].identity,
            NULL, &forest, (uint32_t)root,
            fallback->replay_depth, fallback->result_limit,
            context->arena, &value, &value_len, &replay_outcome,
            error_buf, error_buf_size)) {
        if (root < 0 && error_buf && error_buf_size > 0u &&
            error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "selected cursor span has no exact prepared ParserPack root");
        }
        goto done;
    }
    if (replay_outcome != PPNATIVE_V1_COMPLETED ||
        value_len != 1u || !value) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "prepared cursor semantic fallback produced %u values "
            "with outcome %u",
            value_len, (uint32_t)replay_outcome);
        goto done;
    }
    *out = value;
    *outcome = PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED;
    ok = true;

done:
    free(byte_offsets);
    cetta_lp_native_utf8_forest_free(&forest);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to realize prepared cursor semantic fallback");
    }
    return ok;
}

static bool ppguarded_lex_cursor_v1_program_run_impl(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const CettaLpNativeUtf8Lattice *semantic_lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    const PPGuardedLexCursorV1PreparedFallback *prepared_fallback,
    const PPGuardedLexCursorV1SemanticBackend *semantic_backend,
    PPGuardedLexCursorV1SemanticValue *semantic_value,
    uint32_t *terminal_value_len,
    uint32_t *action_execution_len,
    uint64_t *action_instruction_len,
    uint32_t *value_program_run_len,
    uint32_t *value_terminal_value_len,
    uint32_t *value_action_execution_len,
    uint64_t *value_action_instruction_len,
    uint64_t *value_parser_work_item_len,
    uint32_t *prepared_fallback_run_len,
    uint64_t *prepared_fallback_work_item_len,
    const PPGuardedLexCursorV1EventSink *event_sink,
    bool exact_trace,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    const CettaLpNativeSlrProgram *slr;
    PPGuardedLexCursorV1Receipt result;
    PPGuardedLexCursorV1Lookahead lookahead = {0};
    PPGuardedLexCursorV1DfaScanCache dfa_scan_cache = {0};
    RSDFAV1Token *dfa_tokens = NULL;
    uint32_t *states = NULL;
    PPGuardedLexCursorV1SemanticValue *values = NULL;
    uint32_t *source_lefts = NULL;
    PPGuardedLexCursorV1DispatchIndex dispatch_index = {0};
    RSDFAV1AsciiTransitionIndex ascii_index = {0};
    const RSDFAV1AsciiTransitionIndex *ascii_index_ptr = NULL;
    uint32_t state_len = 1u;
    uint32_t value_len = 0u;
    uint32_t source_span_len = 0u;
    uint32_t state_cap = 64u;
    uint32_t cursor = 0u;
    uint32_t columns;
    bool have_lookahead = false;
    bool semantic_mode = semantic_backend != NULL;
    bool event_mode = event_sink != NULL;
    bool direct_semantic_mode = semantic_mode && !semantic_lattice;
    uint32_t semantic_terminal_values = 0u;
    uint32_t semantic_actions = 0u;
    uint64_t semantic_instructions = 0u;
    uint64_t semantic_mask_work_item_len = 0u;
    uint64_t semantic_occurrence_work_item_len = 0u;
    PPGuardedLexCursorV1ValueContext value_context = {0};
    CettaNativeSha256 trace;
    bool ok = false;

    memset(&result, 0, sizeof(result));
    if (semantic_value)
        memset(semantic_value, 0, sizeof(*semantic_value));
    if (terminal_value_len)
        *terminal_value_len = 0u;
    if (action_execution_len)
        *action_execution_len = 0u;
    if (action_instruction_len)
        *action_instruction_len = 0u;
    if (value_program_run_len)
        *value_program_run_len = 0u;
    if (value_terminal_value_len)
        *value_terminal_value_len = 0u;
    if (value_action_execution_len)
        *value_action_execution_len = 0u;
    if (value_action_instruction_len)
        *value_action_instruction_len = 0u;
    if (value_parser_work_item_len)
        *value_parser_work_item_len = 0u;
    if (prepared_fallback_run_len)
        *prepared_fallback_run_len = 0u;
    if (prepared_fallback_work_item_len)
        *prepared_fallback_work_item_len = 0u;
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!program || !view || !out || work_limit == 0u ||
        (event_mode &&
         (!event_sink->emit ||
          (event_sink->shift_event_live
               ? event_sink->shift_event_live_len != program->terminal_len
               : event_sink->shift_event_live_len != 0u) ||
          (event_sink->reduction_event_live
               ? event_sink->reduction_event_live_len !=
                     program->final_slr.production_len
               : event_sink->reduction_event_live_len != 0u))) ||
        !program->final_slr.actions || !program->final_slr.gotos ||
        (semantic_mode &&
         (!semantic_value || !semantic_backend->scalar ||
          !semantic_backend->neutral ||
          !semantic_backend->action ||
          !terminal_value_len || !action_execution_len ||
          !action_instruction_len || !value_program_run_len ||
          !value_terminal_value_len || !value_action_execution_len ||
          !value_action_instruction_len ||
          !value_parser_work_item_len || !prepared_fallback_run_len ||
          !prepared_fallback_work_item_len || !program->actions_bound ||
          (!direct_semantic_mode &&
           (!ppguarded_lex_cursor_v1_semantic_lattice_matches_view(
                semantic_lattice, view) ||
            (witness_len > 0u && !witness_values))) ||
          (direct_semantic_mode &&
           (witness_values || witness_len > 0u ||
            (!prepared_fallback &&
             (!program->value_programs_complete ||
              program->value_program_len == 0u ||
              !program->value_programs)) ||
            (prepared_fallback &&
             (!prepared_fallback->pack ||
              !prepared_fallback->lexical_plan ||
              !prepared_fallback->exec_plan ||
              prepared_fallback->replay_depth == 0u ||
              prepared_fallback->result_limit == 0u)))))) ||
        (!semantic_mode &&
         (semantic_lattice || witness_values || witness_len > 0u ||
          prepared_fallback ||
          semantic_value || terminal_value_len || action_execution_len ||
          action_instruction_len || value_program_run_len ||
          value_terminal_value_len || value_action_execution_len ||
          value_action_instruction_len || value_parser_work_item_len ||
          prepared_fallback_run_len ||
          prepared_fallback_work_item_len)) ||
        (size_t)program->dfa.tag_len >
            SIZE_MAX / sizeof(*dfa_tokens)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size, "bad cursor program run arguments");
        return false;
    }
    if (direct_semantic_mode) {
        value_context.owner = program;
        value_context.backend = semantic_backend;
        value_context.view = view;
        value_context.prepared_fallback = prepared_fallback;
        value_context.arena = semantic_backend->atom_arena;
        value_context.work_limit = work_limit;
    }
    if (!ppguarded_lex_cursor_v1_dispatch_index_build(
            program, &dispatch_index,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (view->ascii_bytes) {
        if (!rsdfa_v1_ascii_transition_index_build(
                &program->dfa, &ascii_index,
                error_buf, error_buf_size)) {
            goto done;
        }
        ascii_index_ptr = &ascii_index;
    }
    slr = &program->final_slr;
    columns = slr->terminal_len + 1u;
    states = malloc(sizeof(*states) * state_cap);
    if (semantic_mode)
        values = malloc(sizeof(*values) * (state_cap - 1u));
    if (event_mode) {
        source_lefts = malloc(
            sizeof(*source_lefts) * (state_cap - 1u));
    }
    dfa_tokens = malloc(
        sizeof(*dfa_tokens) *
        (size_t)(program->dfa.tag_len ? program->dfa.tag_len : 1u));
    if (!states || !dfa_tokens || (semantic_mode && !values) ||
        (event_mode && !source_lefts))
        goto done;
    states[0] = 0u;
    result.outcome = PPGUARDED_LEX_CURSOR_V1_REJECTED;
    result.input_byte_len = view->input_byte_len;
    result.input_scalar_len = view->scalar_len;
    result.source_pass_count = view->source_pass_count;
    result.max_stack_len = 1u;
    if (exact_trace) {
        cetta_native_sha256_init(&trace);
        ppguarded_lex_exec_v1_sha_text(
            &trace, "ParserPackGuardedLexicalCursorTraceV1\n");
        ppguarded_lex_exec_v1_sha_text(&trace, program->program_digest);
        ppguarded_lex_exec_v1_sha_u32(&trace, view->input_byte_len);
        ppguarded_lex_exec_v1_sha_u32(&trace, view->scalar_len);
    }

    for (;;) {
        const PPGuardedLexCursorV1Terminal *terminal = NULL;
        const CettaLpNativeSlrProgramAction *action;
        uint32_t state;
        uint32_t lookahead_column;
        bool stop_after_unit_chain = false;
        uint64_t total_work =
            ppguarded_lex_cursor_v1_run_work_total(
                &result, semantic_mode,
                semantic_terminal_values, semantic_instructions,
                value_context.program_run_len,
                value_context.terminal_value_len,
                value_context.action_instruction_len,
                value_context.parser_work_item_len,
                semantic_mask_work_item_len,
                semantic_occurrence_work_item_len);

        if (semantic_mode && value_len + 1u != state_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor semantic and state stacks diverged");
            goto done;
        }
        if (event_mode && source_span_len + 1u != state_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor occurrence and state stacks diverged");
            goto done;
        }

        if (total_work >= work_limit) {
            result.outcome = PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
            break;
        }
        state = states[state_len - 1u];
        if (state >= slr->summary.state_len) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor program stack escaped its SLR states");
            goto done;
        }
        if (!have_lookahead) {
            if (!ppguarded_lex_cursor_v1_select(
                    program, &dispatch_index, ascii_index_ptr,
                    view, state, cursor,
                    work_limit - total_work,
                    dfa_tokens, &dfa_scan_cache, &lookahead,
                    error_buf, error_buf_size)) {
                goto done;
            }
            result.cursor_scan_len++;
            result.dfa_work_item_len += lookahead.dfa_work_item_len;
            if (lookahead.work_limit) {
                result.outcome = PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                break;
            }
            have_lookahead = true;
        }
        if (lookahead.present) {
            terminal = &program->terminals[lookahead.source_index];
            lookahead_column = terminal->slr_terminal_index;
            action = &slr->actions[state * columns + lookahead_column];
            if (terminal->kind ==
                    PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR &&
                terminal->scalar_kind ==
                    CETTA_LP_NATIVE_UTF8_TERMINAL_EOF &&
                action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
                lookahead_column = slr->terminal_len;
                action = &slr->actions[
                    state * columns + lookahead_column];
                terminal = NULL;
            }
        } else if (cursor == view->scalar_len) {
            lookahead_column = slr->terminal_len;
            action = &slr->actions[state * columns + lookahead_column];
        } else {
            result.outcome = PPGUARDED_LEX_CURSOR_V1_REJECTED;
            break;
        }

        /*
         * A silent unit reduction A -> X preserves the value and occurrence
         * stacks and replaces only the top parser state.  When lookahead is
         * already functionally determined, execute a maximal such chain in
         * place.  Logical work, counters, trace entries, and work-limit cuts
         * remain exactly those of the unfused transition sequence.
         */
        for (;;) {
            const CettaLpNativeSlrProgramProduction *production;
            uint32_t lhs_index;
            uint32_t target;

            if (semantic_mode || lookahead.reselect_after_reduction ||
                action->kind != CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE ||
                action->value < 0 ||
                (uint32_t)action->value >= slr->production_len) {
                break;
            }
            production = &slr->productions[action->value];
            if (production->rhs_len != 1u ||
                production->rhs_len >= state_len ||
                (event_mode &&
                 (!event_sink->reduction_event_live ||
                  event_sink->reduction_event_live[
                      (uint32_t)action->value] != 0u))) {
                break;
            }

            result.parser_work_item_len++;
            if (exact_trace) {
                ppguarded_lex_exec_v1_sha_u32(&trace, state);
                ppguarded_lex_exec_v1_sha_u32(
                    &trace, lookahead_column);
                ppguarded_lex_exec_v1_sha_u32(
                    &trace, (uint32_t)action->kind);
                ppguarded_lex_exec_v1_sha_u32(
                    &trace, (uint32_t)action->value);
            }
            if (event_mode && source_span_len == 0u)
                goto malformed_action;
            lhs_index = program->final_production_lhs_indices[
                (uint32_t)action->value];
            if (lhs_index >= slr->nonterminal_len ||
                slr->nonterminals[lhs_index] != production->lhs) {
                goto malformed_action;
            }
            target = slr->gotos[
                states[state_len - 2u] * slr->nonterminal_len +
                lhs_index];
            if (target == UINT32_MAX ||
                target >= slr->summary.state_len) {
                goto malformed_action;
            }
            states[state_len - 1u] = target;
            result.reduce_len++;
            result.unit_reduce_len++;

            total_work =
                ppguarded_lex_cursor_v1_run_work_total(
                    &result, semantic_mode,
                    semantic_terminal_values,
                    semantic_instructions,
                    value_context.program_run_len,
                    value_context.terminal_value_len,
                    value_context.action_instruction_len,
                    value_context.parser_work_item_len,
                    semantic_mask_work_item_len,
                    semantic_occurrence_work_item_len);
            if (total_work >= work_limit) {
                result.outcome =
                    PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                stop_after_unit_chain = true;
                break;
            }

            state = target;
            if (lookahead.present) {
                terminal = &program->terminals[
                    lookahead.source_index];
                lookahead_column = terminal->slr_terminal_index;
                action = &slr->actions[
                    state * columns + lookahead_column];
                if (terminal->kind ==
                        PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR &&
                    terminal->scalar_kind ==
                        CETTA_LP_NATIVE_UTF8_TERMINAL_EOF &&
                    action->kind ==
                        CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
                    lookahead_column = slr->terminal_len;
                    action = &slr->actions[
                        state * columns + lookahead_column];
                    terminal = NULL;
                }
            } else if (cursor == view->scalar_len) {
                terminal = NULL;
                lookahead_column = slr->terminal_len;
                action = &slr->actions[
                    state * columns + lookahead_column];
            } else {
                result.outcome =
                    PPGUARDED_LEX_CURSOR_V1_REJECTED;
                stop_after_unit_chain = true;
                break;
            }
        }
        if (stop_after_unit_chain)
            break;
        result.parser_work_item_len++;
        if (exact_trace) {
            ppguarded_lex_exec_v1_sha_u32(&trace, state);
            ppguarded_lex_exec_v1_sha_u32(&trace, lookahead_column);
            ppguarded_lex_exec_v1_sha_u32(&trace, (uint32_t)action->kind);
            ppguarded_lex_exec_v1_sha_u32(
                &trace, (uint32_t)action->value);
        }

        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ERROR) {
            result.outcome = PPGUARDED_LEX_CURSOR_V1_REJECTED;
            break;
        }
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_ACCEPT) {
            PPGuardedLexCursorV1Event event;

            if (terminal || cursor != view->scalar_len) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor program accepted before parser EOF");
                goto done;
            }
            if (event_mode &&
                (source_span_len != 1u ||
                 source_lefts[0] != 0u)) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "accepted cursor occurrence has the wrong root span");
                goto done;
            }
            if (semantic_mode) {
                if (value_len != 1u) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "accepted cursor parse has no singular semantic value");
                    goto done;
                }
                *semantic_value = values[0];
            }
            if (event_mode) {
                event = (PPGuardedLexCursorV1Event){
                    .kind = PPGUARDED_LEX_CURSOR_V1_EVENT_ACCEPT,
                    .symbol_id = slr->start_nonterminal,
                    .source_index = UINT32_MAX,
                    .production_index = UINT32_MAX,
                    .production_label = UINT32_MAX,
                    .rhs_len = 1u,
                    .authored = false,
                    .left_scalar = 0u,
                    .right_scalar = cursor,
                    .left_byte = 0u,
                    .right_byte =
                        cetta_lp_native_utf8_scalar_view_byte_offset(
                            view, cursor),
                };
                if (!ppguarded_lex_cursor_v1_emit_event(
                        event_sink, &event, error_buf, error_buf_size)) {
                    goto done;
                }
            }
            result.outcome = PPGUARDED_LEX_CURSOR_V1_ACCEPTED;
            break;
        }
        if (action->kind == CETTA_LP_NATIVE_SLR_PROGRAM_REDUCE) {
            const CettaLpNativeSlrProgramProduction *production;
            PPGuardedLexCursorV1SemanticValue reduced_value = {0};
            PPGuardedLexCursorV1SourceSpan reduced_span = {0};
            uint32_t lhs_index;
            uint32_t target;
            if (action->value < 0 ||
                (uint32_t)action->value >= slr->production_len) {
                goto malformed_action;
            }
            production = &slr->productions[action->value];
            if (production->rhs_len >= state_len)
                goto malformed_action;
            if (event_mode) {
                if (production->rhs_len > source_span_len)
                    goto malformed_action;
                if (production->rhs_len == 0u) {
                    reduced_span.left = cursor;
                    reduced_span.right = cursor;
                } else {
                    reduced_span.left =
                        source_lefts[
                            source_span_len - production->rhs_len];
                    reduced_span.right = cursor;
                }
                source_span_len -= production->rhs_len;
            }
            if (semantic_mode) {
                const PPActionBytecodeV1Production *semantic_action;
                uint64_t current_work;
                const PPGuardedLexCursorV1SemanticValue *slots;

                if (!production->authored ||
                    production->label >= program->actions.production_len ||
                    production->rhs_len > value_len) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "cursor reduction has no bound semantic action");
                    goto done;
                }
                slots = production->rhs_len > 0u
                    ? &values[value_len - production->rhs_len] : NULL;
                semantic_action =
                    &program->actions.productions[production->label];
                current_work = ppguarded_lex_cursor_v1_work_total(
                    &result, semantic_terminal_values,
                    semantic_instructions,
                    value_context.program_run_len,
                    value_context.terminal_value_len,
                    value_context.action_instruction_len,
                    value_context.parser_work_item_len,
                    semantic_mask_work_item_len);
                if (current_work > work_limit ||
                    semantic_action->instruction_len >
                        work_limit - current_work) {
                    result.outcome = PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                    break;
                }
                if (!semantic_backend->action(
                        semantic_backend->context, production->label,
                        slots, production->rhs_len, &reduced_value,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                value_len -= production->rhs_len;
            }
            state_len -= production->rhs_len;
            lhs_index = program->final_production_lhs_indices[
                (uint32_t)action->value];
            if (lhs_index >= slr->nonterminal_len ||
                slr->nonterminals[lhs_index] != production->lhs) {
                goto malformed_action;
            }
            target = slr->gotos[
                states[state_len - 1u] * slr->nonterminal_len +
                lhs_index];
            if (target == UINT32_MAX || target >= slr->summary.state_len)
                goto malformed_action;
            if (state_len == state_cap) {
                uint32_t next_cap = state_cap * 2u;
                uint32_t *next;
                PPGuardedLexCursorV1SemanticValue *next_values = NULL;
                uint32_t *next_source_lefts = NULL;
                if (next_cap < state_cap ||
                    (size_t)next_cap > SIZE_MAX / sizeof(*states) ||
                    (semantic_mode &&
                     (size_t)(next_cap - 1u) >
                         SIZE_MAX / sizeof(*values)) ||
                    (event_mode &&
                     (size_t)(next_cap - 1u) >
                         SIZE_MAX / sizeof(*source_lefts))) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "cursor program state stack overflow");
                    goto done;
                }
                next = realloc(states, sizeof(*states) * next_cap);
                if (!next)
                    goto done;
                states = next;
                if (semantic_mode) {
                    next_values = realloc(
                        values, sizeof(*values) * (next_cap - 1u));
                    if (!next_values)
                        goto done;
                    values = next_values;
                }
                if (event_mode) {
                    next_source_lefts = realloc(
                        source_lefts,
                        sizeof(*source_lefts) * (next_cap - 1u));
                    if (!next_source_lefts)
                        goto done;
                    source_lefts = next_source_lefts;
                }
                state_cap = next_cap;
            }
            if (semantic_mode) {
                const PPActionBytecodeV1Production *semantic_action =
                    &program->actions.productions[production->label];
                values[value_len++] = reduced_value;
                semantic_actions++;
                semantic_instructions += semantic_action->instruction_len;
            }
            if (event_mode)
                source_lefts[source_span_len++] = reduced_span.left;
            states[state_len++] = target;
            if (state_len > result.max_stack_len)
                result.max_stack_len = state_len;
            result.reduce_len++;
            if (production->rhs_len == 0u)
                result.epsilon_reduce_len++;
            if (production->rhs_len == 1u)
                result.unit_reduce_len++;
            if (lookahead.reselect_after_reduction) {
                result.reselect_reduce_len++;
                if (production->rhs_len == 1u)
                    result.unit_reselect_reduce_len++;
            }
            if (event_mode &&
                (!event_sink->reduction_event_live ||
                 event_sink->reduction_event_live[
                     (uint32_t)action->value] != 0u)) {
                PPGuardedLexCursorV1Event event = {
                    .kind = PPGUARDED_LEX_CURSOR_V1_EVENT_REDUCE,
                    .symbol_id = production->lhs,
                    .source_index = UINT32_MAX,
                    .production_index = (uint32_t)action->value,
                    .production_label = production->label,
                    .rhs_len = production->rhs_len,
                    .authored = production->authored,
                    .left_scalar = reduced_span.left,
                    .right_scalar = reduced_span.right,
                    .left_byte =
                        cetta_lp_native_utf8_scalar_view_byte_offset(
                            view, reduced_span.left),
                    .right_byte =
                        cetta_lp_native_utf8_scalar_view_byte_offset(
                            view, reduced_span.right),
                };
                if (!ppguarded_lex_cursor_v1_emit_event(
                        event_sink, &event, error_buf, error_buf_size)) {
                    goto done;
                }
            }
            if (lookahead.reselect_after_reduction) {
                have_lookahead = false;
                memset(&lookahead, 0, sizeof(lookahead));
            }
            continue;
        }
        if (action->kind != CETTA_LP_NATIVE_SLR_PROGRAM_SHIFT || !terminal ||
            action->value < 0 ||
            (uint32_t)action->value >= slr->summary.state_len) {
            goto malformed_action;
        }
        if (state_len == state_cap) {
            uint32_t next_cap = state_cap * 2u;
            uint32_t *next;
            PPGuardedLexCursorV1SemanticValue *next_values = NULL;
            uint32_t *next_source_lefts = NULL;
            if (next_cap < state_cap ||
                (size_t)next_cap > SIZE_MAX / sizeof(*states) ||
                (semantic_mode &&
                 (size_t)(next_cap - 1u) > SIZE_MAX / sizeof(*values)) ||
                (event_mode &&
                 (size_t)(next_cap - 1u) >
                     SIZE_MAX / sizeof(*source_lefts))) {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "cursor program state stack overflow");
                goto done;
            }
            next = realloc(states, sizeof(*states) * next_cap);
            if (!next)
                goto done;
            states = next;
            if (semantic_mode) {
                next_values = realloc(
                    values, sizeof(*values) * (next_cap - 1u));
                if (!next_values)
                    goto done;
                values = next_values;
            }
            if (event_mode) {
                next_source_lefts = realloc(
                    source_lefts,
                    sizeof(*source_lefts) * (next_cap - 1u));
                if (!next_source_lefts)
                    goto done;
                source_lefts = next_source_lefts;
            }
            state_cap = next_cap;
        }
        if (semantic_mode) {
            PPGuardedLexCursorV1SemanticValue terminal_value = {0};
            bool neutral_value_ready = false;
            bool span_value_ready = false;
            uint64_t current_work = ppguarded_lex_cursor_v1_work_total(
                &result, semantic_terminal_values,
                semantic_instructions,
                value_context.program_run_len,
                value_context.terminal_value_len,
                value_context.action_instruction_len,
                value_context.parser_work_item_len,
                semantic_mask_work_item_len);

            if (current_work >= work_limit) {
                result.outcome = PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                break;
            }
            if (direct_semantic_mode &&
                !terminal->semantic_value_required) {
                if (!semantic_backend->neutral(
                        semantic_backend->context, &terminal_value,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                neutral_value_ready = true;
            }
            if (direct_semantic_mode &&
                terminal->semantic_value_required &&
                terminal->kind != PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR &&
                semantic_backend->span) {
                PPGuardedLexCursorV1SpanOutcome span_outcome;
                uint64_t span_work_item_len = 0u;

                if (!semantic_backend->span(
                        semantic_backend->context,
                        lookahead.source_index, view, cursor,
                        lookahead.end_scalar, work_limit - current_work,
                        &terminal_value, &span_outcome,
                        &span_work_item_len,
                        error_buf, error_buf_size) ||
                    UINT64_MAX - semantic_mask_work_item_len <
                        span_work_item_len) {
                    if (error_buf && error_buf_size > 0u &&
                        error_buf[0] == '\0') {
                        ppguarded_lex_exec_v1_set_error(
                            error_buf, error_buf_size,
                            "semantic-mask cursor work receipt overflow");
                    }
                    goto done;
                }
                semantic_mask_work_item_len += span_work_item_len;
                if (span_outcome ==
                        PPGUARDED_LEX_CURSOR_SPAN_WORK_LIMIT) {
                    result.outcome =
                        PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                    break;
                }
                if (span_outcome == PPGUARDED_LEX_CURSOR_SPAN_ACCEPTED)
                    span_value_ready = true;
                else if (span_outcome !=
                             PPGUARDED_LEX_CURSOR_SPAN_UNHANDLED) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "semantic-mask cursor returned an unknown outcome");
                    goto done;
                }
            }
            if (neutral_value_ready) {
                /* Its value is outside the start-result dependency closure. */
            } else if (terminal->kind ==
                    PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR) {
                if (!semantic_backend->scalar(
                        semantic_backend->context, terminal->scalar_kind,
                        view, cursor, lookahead.end_scalar,
                        &terminal_value, error_buf, error_buf_size)) {
                    goto done;
                }
            } else if (!direct_semantic_mode) {
                if (!semantic_backend->atom_arena ||
                    !(terminal_value.atom =
                        ppguarded_lex_cursor_v1_span_value(
                    semantic_backend->atom_arena, semantic_lattice,
                    witness_values, witness_len,
                    terminal->terminal_id,
                    cursor, lookahead.end_scalar))) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "selected cursor span has no witness semantic value");
                    goto done;
                }
            } else if (span_value_ready) {
                /* The compiler-derived mask produced the compact leaf. */
            } else if (terminal->semantic_program_index != UINT32_MAX) {
                PPGuardedLexCursorV1ValueOutcome value_outcome;

                value_context.base_work =
                    ppguarded_lex_cursor_v1_work_total(
                        &result, semantic_terminal_values,
                        semantic_instructions, 0u, 0u, 0u, 0u,
                        semantic_mask_work_item_len);
                if (!ppguarded_lex_cursor_v1_value_program_run(
                        &value_context, terminal->semantic_program_index,
                        cursor, lookahead.end_scalar,
                        &terminal_value, &value_outcome,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                if (value_outcome ==
                        PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT) {
                    result.outcome =
                        PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                    break;
                }
                if (value_outcome !=
                        PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "selected cursor span is rejected by its value "
                        "program");
                    goto done;
                }
            } else if (prepared_fallback) {
                PPGuardedLexCursorV1ValueOutcome value_outcome;

                value_context.base_work =
                    ppguarded_lex_cursor_v1_work_total(
                        &result, semantic_terminal_values,
                        semantic_instructions, 0u, 0u, 0u, 0u,
                        semantic_mask_work_item_len);
                if (!semantic_backend->atom_arena ||
                    !ppguarded_lex_cursor_v1_prepared_fallback_value(
                        &value_context, terminal,
                        cursor, lookahead.end_scalar,
                        &terminal_value.atom, &value_outcome,
                        error_buf, error_buf_size)) {
                    goto done;
                }
                if (value_outcome ==
                        PPGUARDED_LEX_CURSOR_VALUE_WORK_LIMIT) {
                    result.outcome =
                        PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                    break;
                }
                if (value_outcome !=
                        PPGUARDED_LEX_CURSOR_VALUE_ACCEPTED) {
                    ppguarded_lex_exec_v1_set_error(
                        error_buf, error_buf_size,
                        "selected cursor span is rejected by its prepared "
                        "semantic fallback");
                    goto done;
                }
            } else {
                ppguarded_lex_exec_v1_set_error(
                    error_buf, error_buf_size,
                    "selected cursor span has no generated or prepared "
                    "semantic program");
                goto done;
            }
            values[value_len++] = terminal_value;
            semantic_terminal_values++;
        }
        if (event_mode) {
            source_lefts[source_span_len++] = cursor;
        }
        states[state_len++] = (uint32_t)action->value;
        if (state_len > result.max_stack_len)
            result.max_stack_len = state_len;
        result.shift_len++;
        result.token_len++;
        if (terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR) {
            result.scalar_token_len++;
        } else if (terminal->kind ==
                       PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN) {
            result.ordinary_span_token_len++;
        } else {
            result.guarded_span_token_len++;
        }
        if (exact_trace) {
            ppguarded_lex_exec_v1_sha_u32(&trace, terminal->terminal_id);
            ppguarded_lex_exec_v1_sha_u32(&trace, cursor);
            ppguarded_lex_exec_v1_sha_u32(&trace, lookahead.end_scalar);
        }
        if (event_mode) {
            uint64_t occurrence_work = 0u;

            if (event_sink->semantic_occurrences &&
                (event_sink->semantic_occurrence_required
                    ? event_sink->semantic_occurrence_required(
                          event_sink->context, lookahead.source_index)
                    : terminal->semantic_value_required)) {
                bool projected = false;
                uint64_t current_work =
                    ppguarded_lex_cursor_v1_run_work_total(
                        &result, semantic_mode,
                        semantic_terminal_values,
                        semantic_instructions,
                        value_context.program_run_len,
                        value_context.terminal_value_len,
                        value_context.action_instruction_len,
                        value_context.parser_work_item_len,
                        semantic_mask_work_item_len,
                        semantic_occurrence_work_item_len);
                if (current_work >= work_limit) {
                    result.outcome =
                        PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                    break;
                }
                if (event_sink->semantic_occurrence_project) {
                    PPGuardedLexCursorV1SemanticOccurrenceProjection
                        projection = {0};
                    PPGuardedLexCursorV1Event projection_event;

                    if (!event_sink->semantic_occurrence_project(
                            event_sink->context, view,
                            lookahead.source_index, cursor,
                            lookahead.end_scalar,
                            work_limit - current_work, &projection,
                            error_buf, error_buf_size) ||
                        UINT64_MAX - semantic_occurrence_work_item_len <
                            projection.work_item_len) {
                        if (error_buf && error_buf_size > 0u &&
                            error_buf[0] == '\0') {
                            ppguarded_lex_exec_v1_set_error(
                                error_buf, error_buf_size,
                                "semantic-occurrence projection work "
                                "receipt overflow");
                        }
                        goto done;
                    }
                    semantic_occurrence_work_item_len +=
                        projection.work_item_len;
                    if (projection.outcome ==
                            PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_WORK_LIMIT) {
                        result.outcome =
                            PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT;
                        break;
                    }
                    if (projection.outcome ==
                            PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_ACCEPTED) {
                        if (projection.production_label >=
                                program->actions.production_len ||
                            projection.left_scalar < cursor ||
                            projection.left_scalar >
                                projection.right_scalar ||
                            projection.right_scalar >
                                lookahead.end_scalar) {
                            ppguarded_lex_exec_v1_set_error(
                                error_buf, error_buf_size,
                                "semantic-occurrence projection escaped "
                                "its selected terminal");
                            goto done;
                        }
                        projection_event =
                            (PPGuardedLexCursorV1Event){
                                .kind =
                                    PPGUARDED_LEX_CURSOR_V1_EVENT_SEMANTIC_REDUCE,
                                .symbol_id = UINT32_MAX,
                                .source_index = lookahead.source_index,
                                .production_index = UINT32_MAX,
                                .production_label =
                                    projection.production_label,
                                .rhs_len = program->actions.productions[
                                    projection.production_label].arity,
                                .authored = true,
                                .left_scalar =
                                    projection.left_scalar,
                                .right_scalar =
                                    projection.right_scalar,
                                .left_byte =
                                    cetta_lp_native_utf8_scalar_view_byte_offset(
                                        view, projection.left_scalar),
                                .right_byte =
                                    cetta_lp_native_utf8_scalar_view_byte_offset(
                                        view, projection.right_scalar),
                            };
                        if (!ppguarded_lex_cursor_v1_emit_event(
                                event_sink, &projection_event,
                                error_buf, error_buf_size)) {
                            goto done;
                        }
                        projected = true;
                    } else if (projection.outcome !=
                            PPGUARDED_LEX_CURSOR_V1_OCCURRENCE_PROJECTION_UNHANDLED) {
                        ppguarded_lex_exec_v1_set_error(
                            error_buf, error_buf_size,
                            "semantic-occurrence projection returned an "
                            "unknown outcome");
                        goto done;
                    }
                }
                current_work =
                    ppguarded_lex_cursor_v1_run_work_total(
                        &result, semantic_mode,
                        semantic_terminal_values,
                        semantic_instructions,
                        value_context.program_run_len,
                        value_context.terminal_value_len,
                        value_context.action_instruction_len,
                        value_context.parser_work_item_len,
                        semantic_mask_work_item_len,
                        semantic_occurrence_work_item_len);
                if (!projected &&
                    (current_work >= work_limit ||
                     !ppguarded_lex_cursor_v1_emit_semantic_occurrences(
                         program, view, lookahead.source_index,
                         cursor, lookahead.end_scalar, event_sink,
                         work_limit - current_work, &occurrence_work,
                         error_buf, error_buf_size) ||
                     UINT64_MAX - semantic_occurrence_work_item_len <
                         occurrence_work)) {
                    if (error_buf && error_buf_size > 0u &&
                        error_buf[0] == '\0') {
                        ppguarded_lex_exec_v1_set_error(
                            error_buf, error_buf_size,
                            "semantic-occurrence work receipt overflow");
                    }
                    goto done;
                }
                if (!projected)
                    semantic_occurrence_work_item_len += occurrence_work;
            }
            if (!event_sink->shift_event_live ||
                event_sink->shift_event_live[
                    lookahead.source_index] != 0u) {
                PPGuardedLexCursorV1Event event = {
                    .kind = PPGUARDED_LEX_CURSOR_V1_EVENT_SHIFT,
                    .symbol_id = terminal->terminal_id,
                    .source_index = lookahead.source_index,
                    .production_index = UINT32_MAX,
                    .production_label = UINT32_MAX,
                    .rhs_len = 0u,
                    .authored = false,
                    .left_scalar = cursor,
                    .right_scalar = lookahead.end_scalar,
                    .left_byte =
                        cetta_lp_native_utf8_scalar_view_byte_offset(
                            view, cursor),
                    .right_byte =
                        cetta_lp_native_utf8_scalar_view_byte_offset(
                            view, lookahead.end_scalar),
                };
                if (!ppguarded_lex_cursor_v1_emit_event(
                        event_sink, &event,
                        error_buf, error_buf_size)) {
                    goto done;
                }
            }
        }
        cursor = lookahead.end_scalar;
        dfa_scan_cache.valid = false;
        have_lookahead = false;
        memset(&lookahead, 0, sizeof(lookahead));
        continue;

malformed_action:
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor program encountered a malformed SLR action");
        goto done;
    }
    result.farthest_scalar = cursor;
    result.farthest_byte = cetta_lp_native_utf8_scalar_view_byte_offset(
        view, cursor);
    if (exact_trace) {
        ppguarded_lex_exec_v1_sha_u32(&trace, (uint32_t)result.outcome);
        ppguarded_lex_exec_v1_sha_u32(&trace, result.farthest_scalar);
        ppguarded_lex_cursor_v1_trace_u64(
            &trace, result.dfa_work_item_len);
        ppguarded_lex_cursor_v1_trace_u64(
            &trace, result.parser_work_item_len);
        cetta_native_sha256_finish_hex(&trace, result.trace_digest);
    }
    if (semantic_mode) {
        *terminal_value_len = semantic_terminal_values;
        *action_execution_len = semantic_actions;
        *action_instruction_len = semantic_instructions;
        *value_program_run_len = value_context.program_run_len;
        *value_terminal_value_len = value_context.terminal_value_len;
        *value_action_execution_len =
            value_context.action_execution_len;
        *value_action_instruction_len =
            value_context.action_instruction_len;
        *value_parser_work_item_len =
            value_context.parser_work_item_len;
        *prepared_fallback_run_len =
            value_context.prepared_fallback_run_len;
        *prepared_fallback_work_item_len =
            value_context.prepared_fallback_work_item_len;
        if (result.outcome != PPGUARDED_LEX_CURSOR_V1_ACCEPTED)
            memset(semantic_value, 0, sizeof(*semantic_value));
    }
    *out = result;
    ok = true;

done:
    rsdfa_v1_ascii_transition_index_free(&ascii_index);
    ppguarded_lex_cursor_v1_dispatch_index_free(&dispatch_index);
    free(source_lefts);
    free(values);
    free(states);
    free(dfa_tokens);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to execute cursor program");
    }
    return ok;
}

bool ppguarded_lex_cursor_v1_program_run_scalar_view_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppguarded_lex_cursor_v1_program_run_impl(
        program, view, NULL, NULL, 0u, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL,
        true, work_limit, out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_scalar_view(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        return false;
    }
    return ppguarded_lex_cursor_v1_program_run_impl(
        program, view, NULL, NULL, 0u, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL,
        true, work_limit, out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_events_scalar_view_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    if (observation > PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "cursor occurrence run has an unknown observation mode");
        return false;
    }
    return ppguarded_lex_cursor_v1_program_run_impl(
        program, view, NULL, NULL, 0u, NULL,
        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
        NULL, NULL, sink,
        observation == PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
        work_limit, out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_events_scalar_view(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        return false;
    }
    return
        ppguarded_lex_cursor_v1_program_run_events_scalar_view_prevalidated(
            program, view, sink, observation, work_limit, out,
            error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_events_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    bool ok;

    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, input_bytes, input_byte_len,
            error_buf, error_buf_size)) {
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return false;
    }
    ok =
        ppguarded_lex_cursor_v1_program_run_events_scalar_view_prevalidated(
            program, &buffer.view, sink, observation, work_limit, out,
            error_buf, error_buf_size);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return ok;
}

bool ppguarded_lex_cursor_v1_program_run_events_bytes(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const PPGuardedLexCursorV1EventSink *sink,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size)) {
        return false;
    }
    return ppguarded_lex_cursor_v1_program_run_events_bytes_prevalidated(
        program, input_bytes, input_byte_len, sink, observation,
        work_limit, out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_bytes(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1Receipt *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    bool ok;

    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, input_bytes, input_byte_len,
            error_buf, error_buf_size)) {
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return false;
    }
    ok = ppguarded_lex_cursor_v1_program_run_scalar_view(
        program, &buffer.view, work_limit, out,
        error_buf, error_buf_size);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return ok;
}

bool ppguarded_lex_cursor_v1_program_run_semantic_lattice_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const CettaLpNativeUtf8Lattice *semantic_lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1SemanticResult result;
    PPGuardedLexCursorV1AtomBackend atom_context;
    PPGuardedLexCursorV1SemanticBackend semantic_backend;
    PPGuardedLexCursorV1SemanticValue root = {0};

    ppguarded_lex_cursor_v1_semantic_result_init(&result);
    atom_context = (PPGuardedLexCursorV1AtomBackend){
        .program = program ? &program->actions : NULL,
        .arena = &result.arena,
    };
    semantic_backend = (PPGuardedLexCursorV1SemanticBackend){
        .context = &atom_context,
        .atom_arena = &result.arena,
        .scalar = ppguarded_lex_cursor_v1_atom_scalar,
        .neutral = ppguarded_lex_cursor_v1_atom_neutral,
        .action = ppguarded_lex_cursor_v1_atom_action,
    };
    if (!out ||
        !ppguarded_lex_cursor_v1_program_run_impl(
            program, view, semantic_lattice,
            witness_values, witness_len, NULL, &semantic_backend,
            &root, &result.terminal_value_len,
            &result.action_execution_len, &result.action_instruction_len,
            &result.value_program_run_len,
            &result.value_terminal_value_len,
            &result.value_action_execution_len,
            &result.value_action_instruction_len,
            &result.value_parser_work_item_len,
            &result.prepared_fallback_run_len,
            &result.prepared_fallback_work_item_len,
            NULL, true, work_limit, &result.receipt,
            error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_finish_atom_result(
            &result, root, error_buf, error_buf_size)) {
        ppguarded_lex_cursor_v1_semantic_result_free(&result);
        return false;
    }
    ppguarded_lex_cursor_v1_semantic_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    return true;
}

bool ppguarded_lex_cursor_v1_program_run_semantic_lattice(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    const CettaLpNativeUtf8Lattice *semantic_lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !program->actions_bound ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size) ||
        !cetta_lp_native_utf8_lattice_validate(
            semantic_lattice, error_buf, error_buf_size)) {
        if (program && !program->actions_bound && error_buf &&
            error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor semantic execution requires bound actions");
        }
        return false;
    }
    return ppguarded_lex_cursor_v1_program_run_semantic_lattice_prevalidated(
        program, view, semantic_lattice, witness_values, witness_len,
        work_limit, out, error_buf, error_buf_size);
}

static bool ppguarded_lex_cursor_v1_program_run_semantic_direct_mode(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    bool exact_trace,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1SemanticResult result;
    PPGuardedLexCursorV1AtomBackend atom_context;
    PPGuardedLexCursorV1SemanticBackend semantic_backend;
    PPGuardedLexCursorV1SemanticValue root = {0};

    ppguarded_lex_cursor_v1_semantic_result_init(&result);
    atom_context = (PPGuardedLexCursorV1AtomBackend){
        .program = program ? &program->actions : NULL,
        .arena = &result.arena,
    };
    semantic_backend = (PPGuardedLexCursorV1SemanticBackend){
        .context = &atom_context,
        .atom_arena = &result.arena,
        .scalar = ppguarded_lex_cursor_v1_atom_scalar,
        .neutral = ppguarded_lex_cursor_v1_atom_neutral,
        .action = ppguarded_lex_cursor_v1_atom_action,
    };
    if (!out ||
        !ppguarded_lex_cursor_v1_program_run_impl(
            program, view, NULL, NULL, 0u, NULL, &semantic_backend,
            &root, &result.terminal_value_len,
            &result.action_execution_len, &result.action_instruction_len,
            &result.value_program_run_len,
            &result.value_terminal_value_len,
            &result.value_action_execution_len,
            &result.value_action_instruction_len,
            &result.value_parser_work_item_len,
            &result.prepared_fallback_run_len,
            &result.prepared_fallback_work_item_len,
            NULL, exact_trace, work_limit, &result.receipt,
            error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_finish_atom_result(
            &result, root, error_buf, error_buf_size)) {
        ppguarded_lex_cursor_v1_semantic_result_free(&result);
        return false;
    }
    ppguarded_lex_cursor_v1_semantic_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    return true;
}

bool ppguarded_lex_cursor_v1_program_run_semantic_direct_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppguarded_lex_cursor_v1_program_run_semantic_direct_mode(
        program, view, true, work_limit, out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_semantic_direct(
    const PPGuardedLexCursorV1Program *program,
    const CettaLpNativeUtf8ScalarView *view,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguarded_lex_cursor_v1_program_validate(
            program, error_buf, error_buf_size) ||
        !program->actions_bound ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        if (program && !program->actions_bound && error_buf &&
            error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "cursor semantic execution requires bound actions");
        }
        return false;
    }
    return ppguarded_lex_cursor_v1_program_run_semantic_direct_prevalidated(
        program, view, work_limit, out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_semantic_direct_bytes(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    bool ok;

    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, input_bytes, input_byte_len,
            error_buf, error_buf_size)) {
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return false;
    }
    ok = ppguarded_lex_cursor_v1_program_run_semantic_direct(
        program, &buffer.view, work_limit, out,
        error_buf, error_buf_size);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return ok;
}

bool ppguarded_lex_cursor_v1_program_run_semantic_direct_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    bool ok;

    if (observation > PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "unknown cursor observation policy");
        return false;
    }
    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, input_bytes, input_byte_len,
            error_buf, error_buf_size)) {
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return false;
    }
    ok = ppguarded_lex_cursor_v1_program_run_semantic_direct_mode(
        program, &buffer.view,
        observation == PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
        work_limit, out, error_buf, error_buf_size);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return ok;
}

bool ppguarded_lex_cursor_v1_program_run_projected_direct_bytes_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPAtomProjectionActionV1Program *projection_actions,
    const PPAtomProjectionV1Plan *projection,
    const PPGuardedLexCursorV1SemanticMaskBinding *semantic_masks,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PPGuardedLexCursorV1Observation observation,
    uint64_t work_limit,
    Arena *output_arena,
    uint32_t projection_depth_limit,
    PPGuardedLexCursorV1ProjectedResult *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    PPAtomProjectionV1ProvenanceView provenance;
    PPAtomProjectionActionV1Store store;
    PPGuardedLexCursorV1ProjectionBackend projection_context = {0};
    PPGuardedLexCursorV1SemanticBackend semantic_backend;
    PPGuardedLexCursorV1SemanticValue root = {0};
    PPGuardedLexCursorV1ProjectedResult result;
    uint32_t prepared_fallback_run_len = 0u;
    uint64_t prepared_fallback_work_item_len = 0u;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    ppguarded_lex_cursor_v1_projected_result_init(&result);
    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    ppatom_projection_action_v1_store_init(&store);
    if (!program || !projection_actions || !projection || !output_arena ||
        !out || observation > PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE ||
        work_limit == 0u || projection_depth_limit == 0u ||
        input_byte_len > UINT32_MAX ||
        (input_byte_len > 0u && !input_bytes) ||
        !program->actions_bound || !program->value_programs_complete ||
        program->value_program_len == 0u || !program->value_programs ||
        projection_actions->production_len !=
            program->actions.production_len ||
        strcmp(projection_actions->action_program_digest,
               program->actions.program_digest) != 0 ||
        !ppatom_projection_v1_plan_provenance(
            projection, &provenance, error_buf, error_buf_size) ||
        strcmp(projection_actions->projection_plan_digest,
               provenance.plan_digest) != 0 ||
        (semantic_masks &&
         (semantic_masks->terminal_len != program->terminal_len ||
          semantic_masks->bound_terminal_len == 0u ||
          !semantic_masks->terminal_tags ||
          !semantic_masks->text_bindings ||
          semantic_masks->mask.root_link_len !=
              semantic_masks->bound_terminal_len ||
          !semantic_masks->action_wrapper_indices ||
          semantic_masks->transducer.action_len !=
              semantic_masks->mask.action_len ||
          strcmp(semantic_masks->cursor_program_digest,
                 program->program_digest) != 0 ||
          strcmp(semantic_masks->projection_action_program_digest,
                 projection_actions->program_digest) != 0))) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "bad compact cursor projection inputs");
        }
        goto done;
    }
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, input_bytes, input_byte_len,
            error_buf, error_buf_size)) {
        goto done;
    }
    projection_context = (PPGuardedLexCursorV1ProjectionBackend){
        .program = projection_actions,
        .projection = projection,
        .store = &store,
        .cursor = program,
        .semantic_masks = semantic_masks,
    };
    semantic_backend = (PPGuardedLexCursorV1SemanticBackend){
        .context = &projection_context,
        .atom_arena = NULL,
        .scalar = ppguarded_lex_cursor_v1_projection_scalar,
        .neutral = ppguarded_lex_cursor_v1_projection_neutral,
        .action = ppguarded_lex_cursor_v1_projection_action,
        .span = semantic_masks
            ? ppguarded_lex_cursor_v1_projection_span : NULL,
    };
    if (!ppguarded_lex_cursor_v1_program_run_impl(
            program, &buffer.view, NULL, NULL, 0u, NULL,
            &semantic_backend, &root,
            &result.terminal_value_len,
            &result.action_execution_len,
            &result.action_instruction_len,
            &result.value_program_run_len,
            &result.value_terminal_value_len,
            &result.value_action_execution_len,
            &result.value_action_instruction_len,
            &result.value_parser_work_item_len,
            &prepared_fallback_run_len,
            &prepared_fallback_work_item_len,
            NULL,
            observation == PPGUARDED_LEX_CURSOR_V1_EXACT_TRACE,
            work_limit, &result.receipt,
            error_buf, error_buf_size)) {
        goto done;
    }
    result.semantic_mask_run_len =
        projection_context.semantic_mask_run_len;
    result.semantic_mask_input_scalar_len =
        projection_context.semantic_mask_input_scalar_len;
    result.semantic_mask_retained_scalar_len =
        projection_context.semantic_mask_retained_scalar_len;
    result.semantic_mask_work_item_len =
        projection_context.semantic_mask_work_item_len;
    if (prepared_fallback_run_len != 0u ||
        prepared_fallback_work_item_len != 0u) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "compact cursor unexpectedly used a prepared fallback");
        goto done;
    }
    if (result.receipt.outcome == PPGUARDED_LEX_CURSOR_V1_ACCEPTED) {
        if (!ppatom_projection_action_v1_project_document(
                projection_actions, &store, root.projection,
                projection, output_arena, projection_depth_limit,
                &result.atoms, &result.atom_len,
                error_buf, error_buf_size)) {
            goto done;
        }
    } else if (result.receipt.outcome !=
                   PPGUARDED_LEX_CURSOR_V1_REJECTED &&
               result.receipt.outcome !=
                   PPGUARDED_LEX_CURSOR_V1_WORK_LIMIT) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "compact cursor produced an unknown outcome");
        goto done;
    }
    *out = result;
    ok = true;

done:
    free(projection_context.mask_actions);
    free(projection_context.retained_scalars);
    ppatom_projection_action_v1_store_free(&store);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to execute compact cursor projection");
    }
    return ok;
}

bool ppguarded_lex_cursor_v1_program_run_semantic_hybrid_prevalidated(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    PPGuardedLexCursorV1PreparedBackend backend,
    uint32_t replay_depth,
    uint32_t result_limit,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexCursorV1PreparedFallback fallback;
    PPGuardedLexCursorV1SemanticResult result;
    PPGuardedLexCursorV1AtomBackend atom_context;
    PPGuardedLexCursorV1SemanticBackend semantic_backend;
    PPGuardedLexCursorV1SemanticValue root = {0};
    uint32_t index;

    ppguarded_lex_cursor_v1_semantic_result_init(&result);
    atom_context = (PPGuardedLexCursorV1AtomBackend){
        .program = program ? &program->actions : NULL,
        .arena = &result.arena,
    };
    semantic_backend = (PPGuardedLexCursorV1SemanticBackend){
        .context = &atom_context,
        .atom_arena = &result.arena,
        .scalar = ppguarded_lex_cursor_v1_atom_scalar,
        .neutral = ppguarded_lex_cursor_v1_atom_neutral,
        .action = ppguarded_lex_cursor_v1_atom_action,
    };
    if (!program || !pack || !lexical_plan || !exec_plan || !view || !out ||
        backend > PPGUARDED_LEX_CURSOR_V1_PREPARED_GLR ||
        replay_depth == 0u || result_limit == 0u || work_limit == 0u ||
        !program->actions_bound ||
        strcmp(program->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(program->lexical_plan_digest,
               lexical_plan->plan_digest) != 0 ||
        strcmp(program->execution_plan_digest,
               exec_plan->execution_plan_digest) != 0 ||
        strcmp(exec_plan->base_pack_digest, pack->pack_digest) != 0 ||
        strcmp(exec_plan->lexical_plan_digest,
               lexical_plan->plan_digest) != 0) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad hybrid cursor semantic execution inputs");
        ppguarded_lex_cursor_v1_semantic_result_free(&result);
        return false;
    }
    for (index = 0u; index < program->terminal_len; index++) {
        const PPGuardedLexCursorV1Terminal *terminal =
            &program->terminals[index];
        if (terminal->kind == PPGUARDED_LEX_CURSOR_TERMINAL_SCALAR ||
            terminal->semantic_program_index != UINT32_MAX) {
            continue;
        }
        if (terminal->kind !=
                PPGUARDED_LEX_CURSOR_TERMINAL_ORDINARY_SPAN) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "hybrid cursor v1 cannot fallback a guarded span");
            ppguarded_lex_cursor_v1_semantic_result_free(&result);
            return false;
        }
    }
    fallback = (PPGuardedLexCursorV1PreparedFallback){
        .pack = pack,
        .lexical_plan = lexical_plan,
        .exec_plan = exec_plan,
        .backend = backend == PPGUARDED_LEX_CURSOR_V1_PREPARED_GLL
            ? PPGUARDED_LEX_EXEC_V1_GLL : PPGUARDED_LEX_EXEC_V1_GLR,
        .replay_depth = replay_depth,
        .result_limit = result_limit,
    };
    if (!ppguarded_lex_cursor_v1_program_run_impl(
            program, view, NULL, NULL, 0u, &fallback, &semantic_backend,
            &root, &result.terminal_value_len,
            &result.action_execution_len, &result.action_instruction_len,
            &result.value_program_run_len,
            &result.value_terminal_value_len,
            &result.value_action_execution_len,
            &result.value_action_instruction_len,
            &result.value_parser_work_item_len,
            &result.prepared_fallback_run_len,
            &result.prepared_fallback_work_item_len,
            NULL, true, work_limit, &result.receipt,
            error_buf, error_buf_size) ||
        !ppguarded_lex_cursor_v1_finish_atom_result(
            &result, root, error_buf, error_buf_size)) {
        ppguarded_lex_cursor_v1_semantic_result_free(&result);
        return false;
    }
    ppguarded_lex_cursor_v1_semantic_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    return true;
}

bool ppguarded_lex_cursor_v1_program_run_semantic_hybrid(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    PPGuardedLexCursorV1PreparedBackend backend,
    uint32_t replay_depth,
    uint32_t result_limit,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!ppguarded_lex_exec_v1_plan_matches(
            pack, start_state, lexical_plan, guard_plan,
            guarded_plan, exec_plan) ||
        !ppguarded_lex_cursor_v1_program_validate_bound(
            program, pack, lexical_plan, guard_plan, guarded_plan,
            error_buf, error_buf_size) ||
        !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "hybrid cursor semantic sources are stale or malformed");
        }
        return false;
    }
    return ppguarded_lex_cursor_v1_program_run_semantic_hybrid_prevalidated(
        program, pack, lexical_plan, exec_plan, view, backend,
        replay_depth, result_limit, work_limit,
        out, error_buf, error_buf_size);
}

bool ppguarded_lex_cursor_v1_program_run_semantic_hybrid_bytes(
    const PPGuardedLexCursorV1Program *program,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    PPGuardedLexCursorV1PreparedBackend backend,
    uint32_t replay_depth,
    uint32_t result_limit,
    uint64_t work_limit,
    PPGuardedLexCursorV1SemanticResult *out,
    char *error_buf,
    size_t error_buf_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    bool ok;

    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, input_bytes, input_byte_len,
            error_buf, error_buf_size)) {
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return false;
    }
    ok = ppguarded_lex_cursor_v1_program_run_semantic_hybrid(
        program, pack, start_state, lexical_plan, guard_plan, guarded_plan,
        exec_plan, &buffer.view, backend, replay_depth, result_limit,
        work_limit, out, error_buf, error_buf_size);
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return ok;
}

bool ppguarded_lex_exec_v1_plan_build(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const RSNFAV1Plan *lexical_nfa,
    const RSNFAV1Plan *guard_nfa,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Plan *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexExecV1Plan result;
    PPGuardedLexExecV1NfaUnion combined = {0};
    const RSNFAV1Plan *parts[3];
    CettaLpNativeGrammar validation;
    PPNativeV1ForestExtension extension;
    RSDFAV1BuildOutcome build_outcome = RSDFA_V1_BUILD_COMPLETED;
    bool *used_guards = NULL;
    int32_t start_state_id;
    uint32_t part_len = 1u;
    uint32_t index;
    bool ok = false;

    ppguarded_lex_exec_v1_plan_init(&result);
    cetta_lp_native_grammar_init(&validation);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state || !lexical_plan || !guard_plan || !guarded_plan ||
        !lexical_nfa || !guard_nfa || !out ||
        !ppguarded_lex_exec_v1_limits_valid(limits) ||
        lexical_nfa->nfa.tag_len == 0u ||
        lexical_nfa->nfa.tag_len != lexical_plan->entry_len ||
        guarded_plan->entry_len != guarded_plan->prefix_nfa.nfa.tag_len ||
        guard_nfa->nfa.tag_len != guard_plan->entry_len ||
        guarded_plan->entry_len != guard_plan->entry_len ||
        lexical_plan->entry_len >
            UINT32_MAX - guarded_plan->entry_len ||
        lexical_plan->entry_len + guarded_plan->entry_len >
            UINT32_MAX - guard_plan->entry_len ||
        guard_plan->terminal_len >
            UINT32_MAX - guarded_plan->entry_len ||
        !pplex_v1_grammar_build(
            pack, lexical_plan, &validation,
            error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "bad guarded lexical execution plan inputs");
        }
        goto done;
    }
    cetta_lp_native_grammar_free(&validation);
    if (!ppguard_plan_v1_grammar_build(
            pack, lexical_plan, guard_plan, &validation,
            error_buf, error_buf_size)) {
        goto done;
    }
    cetta_lp_native_grammar_free(&validation);
    if (!ppguarded_lex_v1_grammar_build(
            pack, lexical_plan, guard_plan, guarded_plan,
            &validation, error_buf, error_buf_size)) {
        goto done;
    }
    cetta_lp_native_grammar_free(&validation);
    if (guard_plan->entry_len > 0u) {
        used_guards = calloc(
            guard_plan->entry_len, sizeof(*used_guards));
    }
    result.projected_tag_terminal_ids = malloc(
        sizeof(*result.projected_tag_terminal_ids) *
        (size_t)(lexical_plan->entry_len + guarded_plan->entry_len));
    if (guarded_plan->entry_len > 0u) {
        result.guard_local_for_guarded = malloc(
            sizeof(*result.guard_local_for_guarded) *
            (size_t)guarded_plan->entry_len);
    }
    result.terminal_len = guard_plan->terminal_len + guarded_plan->entry_len;
    if (result.terminal_len > 0u) {
        result.terminals = malloc(
            sizeof(*result.terminals) * (size_t)result.terminal_len);
    }
    if ((guard_plan->entry_len > 0u && !used_guards) ||
        !result.projected_tag_terminal_ids ||
        (guarded_plan->entry_len > 0u &&
         !result.guard_local_for_guarded) ||
        (result.terminal_len > 0u && !result.terminals) ||
        !ppnative_v1_terminal_plan_build(
            pack, &result.scalar_terminals,
            error_buf, error_buf_size)) {
        goto done;
    }
    for (index = 0u; index < lexical_plan->entry_len; index++) {
        int32_t entry = ppguarded_lex_exec_v1_lexical_entry(
            lexical_plan, index);
        if (entry < 0 ||
            !atom_eq(lexical_plan->entries[entry].tag,
                     lexical_nfa->tags[index])) {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "ordinary lexical NFA tags do not match their plan");
            goto done;
        }
        result.projected_tag_terminal_ids[index] =
            lexical_plan->tag_terminal_ids[index];
    }
    for (index = 0u; index < guarded_plan->entry_len; index++) {
        int32_t entry_index = ppguarded_lex_exec_v1_guarded_entry(
            guarded_plan, index);
        const PPGuardedLexV1Entry *entry;
        int32_t guard_local;
        if (entry_index < 0)
            goto mapping_error;
        entry = &guarded_plan->entries[entry_index];
        if (entry->guard_entry_index >= guard_plan->entry_len ||
            used_guards[entry->guard_entry_index] ||
            !atom_eq(entry->tag,
                     guarded_plan->prefix_nfa.tags[index])) {
            goto mapping_error;
        }
        guard_local = ppguarded_lex_exec_v1_tag_find(
            guard_nfa->tags, guard_nfa->nfa.tag_len,
            guard_plan->entries[entry->guard_entry_index].tag);
        if (guard_local < 0)
            goto mapping_error;
        used_guards[entry->guard_entry_index] = true;
        result.guard_local_for_guarded[index] = (uint32_t)guard_local;
        result.projected_tag_terminal_ids[
            lexical_plan->entry_len + index] =
            guarded_plan->tag_terminal_ids[index];
    }
    for (index = 0u; index < guard_plan->entry_len; index++) {
        if (!used_guards[index])
            goto mapping_error;
    }
    for (index = 0u; index < result.terminal_len; index++) {
        result.terminals[index] = index < guard_plan->terminal_len
            ? guard_plan->terminals[index]
            : guarded_plan->terminals[index - guard_plan->terminal_len];
    }
    parts[0] = lexical_nfa;
    if (guarded_plan->entry_len > 0u) {
        parts[part_len++] = &guarded_plan->prefix_nfa;
        parts[part_len++] = guard_nfa;
    }
    if (!ppguarded_lex_exec_v1_union_build(
            parts, part_len, &combined, error_buf, error_buf_size) ||
        !rsdfa_v1_plan_build(
            &combined.nfa, limits->dfa_state_limit,
            limits->dfa_transition_limit, &result.dfa, &build_outcome,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (build_outcome != RSDFA_V1_BUILD_COMPLETED) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "guarded lexical DFA hit its construction limit");
        goto done;
    }
    result.guard_tags = guard_nfa->tags;
    result.lexical_tag_len = lexical_plan->entry_len;
    result.guarded_tag_len = guarded_plan->entry_len;
    result.guard_tag_len = guard_plan->entry_len;
    result.combined_tag_len = result.lexical_tag_len +
        result.guarded_tag_len + result.guard_tag_len;
    extension = (PPNativeV1ForestExtension){
        .states = guard_plan->states,
        .state_len = guard_plan->state_len,
        .terminals = result.terminals,
        .terminal_len = result.terminal_len,
        .productions = guard_plan->productions,
        .production_len = guard_plan->production_len,
    };
    start_state_id = ppnative_v1_state_find_extended(
        pack, &extension, start_state);
    if (start_state_id < 0 ||
        !ppnative_v1_start_is_closed_extended(
            pack, start_state, &extension,
            error_buf, error_buf_size) ||
        !ppguarded_lex_v1_grammar_build(
            pack, lexical_plan, guard_plan, guarded_plan,
            &validation, error_buf, error_buf_size) ||
        !cetta_lp_native_slr_prepare(
            &result.final_slr, &validation,
            (uint32_t)start_state_id,
            error_buf, error_buf_size) ||
        !cetta_lp_native_gll_prepare(
            &result.final_gll, &validation,
            (uint32_t)start_state_id,
            error_buf, error_buf_size) ||
        !cetta_lp_native_glr_prepare(
            &result.final_glr, &validation,
            (uint32_t)start_state_id,
            error_buf, error_buf_size) ||
        !pplex_v1_witness_family_prepare(
            pack, lexical_plan, &result.ordinary_witness_parsers,
            error_buf, error_buf_size) ||
        (guard_plan->entry_len > 0u &&
         !ppguard_ref_v1_witness_family_prepare(
             pack, guard_plan, &result.guard_witness_parsers,
             error_buf, error_buf_size)) ||
        (guarded_plan->entry_len > 0u &&
         !ppguarded_lex_exec_v1_guarded_witness_family_prepare(
             pack, lexical_plan, guard_plan, guarded_plan,
             &result.guarded_witness_parsers,
             error_buf, error_buf_size))) {
        goto done;
    }
    result.start_state = (Atom *)start_state;
    result.start_state_id = (uint32_t)start_state_id;
    result.final_parser_table_build_count = 1u;
    result.ordinary_witness_parser_family_build_count =
        ppnative_v1_prepared_start_family_build_count(
            &result.ordinary_witness_parsers);
    result.ordinary_witness_prepared_start_len =
        ppnative_v1_prepared_start_family_len(
            &result.ordinary_witness_parsers);
    result.guard_witness_parser_family_build_count =
        ppnative_v1_prepared_start_family_build_count(
            &result.guard_witness_parsers);
    result.guard_witness_prepared_start_len =
        ppnative_v1_prepared_start_family_len(
            &result.guard_witness_parsers);
    result.guarded_witness_parser_family_build_count =
        ppnative_v1_prepared_start_family_build_count(
            &result.guarded_witness_parsers);
    result.guarded_witness_prepared_start_len =
        ppnative_v1_prepared_start_family_len(
            &result.guarded_witness_parsers);
    memcpy(result.base_pack_digest, pack->pack_digest, 65u);
    memcpy(result.lexical_plan_digest, lexical_plan->plan_digest, 65u);
    memcpy(result.guard_plan_digest, guard_plan->plan_digest, 65u);
    memcpy(result.guarded_plan_digest, guarded_plan->plan_digest, 65u);
    ppguarded_lex_exec_v1_plan_digest(
        &result, result.execution_plan_digest);
    if (!ppguarded_lex_exec_v1_cursor_certificate_build(
            &validation, &result, &result.cursor_certificate,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (!ppguarded_lex_exec_v1_plan_matches(
            pack, start_state, lexical_plan, guard_plan,
            guarded_plan, &result)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "guarded lexical execution plan failed self-validation");
        goto done;
    }
    ppguarded_lex_exec_v1_plan_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;
    goto done;

mapping_error:
    ppguarded_lex_exec_v1_set_error(
        error_buf, error_buf_size,
        "guarded prefix and positive-guard tags are not bijective");

done:
    free(used_guards);
    ppguarded_lex_exec_v1_union_free(&combined);
    cetta_lp_native_grammar_free(&validation);
    ppguarded_lex_exec_v1_plan_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to build guarded lexical execution plan");
    }
    return ok;
}

static void ppguarded_lex_exec_v1_witness_init(
    PPGuardedLexWitnessV1 *table) {
    if (!table)
        return;
    memset(table, 0, sizeof(*table));
    arena_init(&table->arena);
    arena_set_hashcons(&table->arena, NULL);
    table->outcome = PPLEX_V1_WITNESS_COMPLETED;
}

static void ppguarded_lex_exec_v1_witness_free(
    PPGuardedLexWitnessV1 *table) {
    if (!table)
        return;
    free(table->values);
    arena_free(&table->arena);
    memset(table, 0, sizeof(*table));
}

void ppguarded_lex_exec_v1_result_init(
    PPGuardedLexExecV1Result *result) {
    if (!result)
        return;
    memset(result, 0, sizeof(*result));
    rsdfa_v1_scan_result_init(&result->scan);
    rsdfa_v1_parser_lattice_init(&result->ordinary_lattice);
    rsdfa_v1_parser_lattice_init(&result->projected_lattice);
    pplex_v1_witness_table_init(&result->ordinary_gll_witness);
    pplex_v1_witness_table_init(&result->ordinary_glr_witness);
    ppguard_relation_v1_init(&result->gll_relation);
    ppguard_relation_v1_init(&result->glr_relation);
    ppguard_relation_v1_init(&result->gll_projected_relation);
    ppguard_relation_v1_init(&result->glr_projected_relation);
    ppguarded_lex_exec_v1_witness_init(&result->guarded_gll_witness);
    ppguarded_lex_exec_v1_witness_init(&result->guarded_glr_witness);
    ppnative_v1_result_init(&result->slr);
    ppnative_v1_result_init(&result->gll);
    ppnative_v1_result_init(&result->glr);
}

void ppguarded_lex_exec_v1_result_free(
    PPGuardedLexExecV1Result *result) {
    if (!result)
        return;
    ppnative_v1_result_free(&result->glr);
    ppnative_v1_result_free(&result->gll);
    ppnative_v1_result_free(&result->slr);
    ppguard_relation_v1_free(&result->glr_projected_relation);
    ppguard_relation_v1_free(&result->gll_projected_relation);
    free(result->glr_witness_values);
    free(result->gll_witness_values);
    ppguarded_lex_exec_v1_witness_free(
        &result->guarded_glr_witness);
    ppguarded_lex_exec_v1_witness_free(
        &result->guarded_gll_witness);
    ppguard_relation_v1_free(&result->glr_relation);
    ppguard_relation_v1_free(&result->gll_relation);
    pplex_v1_witness_table_free(&result->ordinary_glr_witness);
    pplex_v1_witness_table_free(&result->ordinary_gll_witness);
    rsdfa_v1_parser_lattice_free(&result->projected_lattice);
    rsdfa_v1_parser_lattice_free(&result->ordinary_lattice);
    free(result->projected_tokens);
    free(result->guarded_tokens);
    free(result->guard_tokens);
    free(result->ordinary_tokens);
    rsdfa_v1_scan_result_free(&result->scan);
    memset(result, 0, sizeof(*result));
}

static bool ppguarded_lex_exec_v1_rebind_projected_owners(
    PPGuardRelationV1 *relation,
    const Arena *ordinary_witness_owner,
    const Arena *guarded_witness_owner,
    const Arena *guard_relation_owner) {
    if (!relation ||
        relation->atom_storage != PPGUARD_RELATION_V1_ATOMS_BORROWED ||
        relation->borrowed_atom_owner_len != 3u ||
        !relation->borrowed_atom_owners) {
        return false;
    }
    relation->borrowed_atom_owners[0] = ordinary_witness_owner;
    relation->borrowed_atom_owners[1] = guarded_witness_owner;
    relation->borrowed_atom_owners[2] = guard_relation_owner;
    return true;
}

static bool ppguarded_lex_exec_v1_rebind_result_owners(
    PPGuardedLexExecV1Result *result) {
    return result &&
        ppguarded_lex_exec_v1_rebind_projected_owners(
            &result->gll_projected_relation,
            &result->ordinary_gll_witness.arena,
            &result->guarded_gll_witness.arena,
            &result->gll_relation.arena) &&
        ppguarded_lex_exec_v1_rebind_projected_owners(
            &result->glr_projected_relation,
            &result->ordinary_glr_witness.arena,
            &result->guarded_glr_witness.arena,
            &result->glr_relation.arena);
}

static bool ppguarded_lex_exec_v1_expr_head(
    const Atom *term, const char *head, CettaExprLen argument_len) {
    return term && term->kind == ATOM_EXPR &&
        term->expr.len == argument_len + 1u &&
        atom_is_symbol(term->expr.elems[0], head);
}

static int32_t ppguarded_lex_exec_v1_forest_root_find(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t state_id,
    uint32_t left,
    uint32_t right) {
    uint32_t index;
    int32_t found = -1;

    for (index = 0u; index < forest->root_len; index++) {
        uint32_t node_index = forest->roots[index];
        const CettaLpNativeUtf8ForestNode *node;
        if (node_index >= forest->node_len)
            return -1;
        node = &forest->nodes[node_index];
        if (node->kind != CETTA_LP_NATIVE_UTF8_FOREST_SYMBOL ||
            node->symbol_id != state_id || node->scalar_left != left ||
            node->scalar_right != right) {
            continue;
        }
        if (found >= 0)
            return -1;
        found = (int32_t)node_index;
    }
    return found;
}

static bool ppguarded_lex_exec_v1_replay_value(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPNativeV1ForestExtension *extension,
    const CettaLpNativeUtf8Forest *forest,
    uint32_t root_node,
    uint32_t replay_depth,
    uint32_t result_limit,
    Arena *arena,
    Atom **value,
    uint32_t *value_len,
    PPNativeV1Outcome *outcome,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result replay;
    uint32_t selected_root = root_node;
    bool ok = false;

    *value = NULL;
    *value_len = 0u;
    *outcome = PPNATIVE_V1_COMPLETED;
    ppnative_v1_result_init(&replay);
    replay.forest = *forest;
    replay.forest.roots = &selected_root;
    replay.forest.root_len = 1u;
    if (!ppnative_v1_finish_extended(
            &replay, pack, start_state, extension,
            replay_depth, result_limit,
            error_buf, error_buf_size)) {
        goto done;
    }
    *outcome = replay.outcome;
    *value_len = replay.semantic_result_len;
    if (replay.outcome != PPNATIVE_V1_COMPLETED ||
        replay.semantic_result_len != 1u) {
        ok = true;
        goto done;
    }
    if (!ppguarded_lex_exec_v1_expr_head(
            replay.semantic_results[0], "result", 2u)) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "guarded lexical root replay produced a malformed result");
        goto done;
    }
    *value = atom_deep_copy(
        arena, replay.semantic_results[0]->expr.elems[1]);
    if (!*value)
        goto done;
    ok = true;

done:
    memset(&replay.forest, 0, sizeof(replay.forest));
    ppnative_v1_result_free(&replay);
    return ok;
}

static bool ppguarded_lex_exec_v1_witness_build(
    PPGuardedLexExecV1Backend backend,
    const PPABIV1Pack *pack,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const RSDFAV1Token *tokens,
    uint32_t token_len,
    const PPGuardRelationV1 *relation,
    const PPNativeV1PreparedStartFamily *prepared_family,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexWitnessV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexWitnessV1 result;
    PPNativeV1ForestExtension extension;
    uint32_t token_index;
    bool ok = false;

    ppguarded_lex_exec_v1_witness_init(&result);
    if (!pack || !lexical_plan || !guard_plan || !guarded_plan ||
        (token_len > 0u && !tokens) || !relation || !prepared_family ||
        !limits || !out || !ppguard_relation_v1_validate(
            relation, error_buf, error_buf_size)) {
        goto done;
    }
    result.values = calloc(
        token_len ? token_len : 1u, sizeof(*result.values));
    if (!result.values)
        goto done;
    result.len = token_len;
    result.source_pass_count = relation->lattice.source_pass_count;
    extension = (PPNativeV1ForestExtension){
        .states = guard_plan->states,
        .state_len = guard_plan->state_len,
        .terminals = guard_plan->terminals,
        .terminal_len = guard_plan->terminal_len,
        .productions = guard_plan->productions,
        .production_len = guard_plan->production_len,
        .witness_values = relation->witness_values,
        .witness_len = relation->witness_len,
    };
    for (token_index = 0u; token_index < token_len; token_index++) {
        const RSDFAV1Token *token = &tokens[token_index];
        CettaLpNativeUtf8Forest forest;
        RSDFAV1ParserLattice token_lattice;
        uint32_t token_scalar_len;
        int32_t entry_index;
        int32_t root;
        Atom *value = NULL;
        uint32_t value_len = 0u;
        PPNativeV1Outcome replay_outcome = PPNATIVE_V1_COMPLETED;
        bool parsed;

        cetta_lp_native_utf8_forest_init(&forest);
        rsdfa_v1_parser_lattice_init(&token_lattice);
        entry_index = ppguarded_lex_exec_v1_guarded_entry(
            guarded_plan, token->tag);
        if (entry_index < 0) {
            cetta_lp_native_utf8_forest_free(&forest);
            rsdfa_v1_parser_lattice_free(&token_lattice);
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded token has no projection entry");
            goto done;
        }
        token_scalar_len = token->end_scalar - token->start_scalar;
        if (!rsdfa_v1_parser_lattice_slice_prevalidated(
                &relation->lattice,
                token->start_scalar, token->end_scalar,
                &token_lattice, error_buf, error_buf_size)) {
            cetta_lp_native_utf8_forest_free(&forest);
            rsdfa_v1_parser_lattice_free(&token_lattice);
            goto done;
        }
        if (backend == PPGUARDED_LEX_EXEC_V1_GLL) {
            const CettaLpNativeGllPrepared *gll = NULL;
            const CettaLpNativeGlrPrepared *glr = NULL;
            parsed = ppnative_v1_prepared_start_family_lookup(
                    prepared_family, guarded_plan->plan_digest,
                    guarded_plan->entries[entry_index].state_id,
                    &gll, &glr, error_buf, error_buf_size) &&
                cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
                    gll, &token_lattice.lattice, 0u,
                    limits->witness_work_limit, &forest,
                    error_buf, error_buf_size);
        } else {
            const CettaLpNativeGllPrepared *gll = NULL;
            const CettaLpNativeGlrPrepared *glr = NULL;
            parsed = ppnative_v1_prepared_start_family_lookup(
                    prepared_family, guarded_plan->plan_digest,
                    guarded_plan->entries[entry_index].state_id,
                    &gll, &glr, error_buf, error_buf_size) &&
                cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
                    glr, &token_lattice.lattice, 0u,
                    limits->witness_work_limit, &forest,
                    error_buf, error_buf_size);
        }
        rsdfa_v1_parser_lattice_free(&token_lattice);
        if (!parsed) {
            cetta_lp_native_utf8_forest_free(&forest);
            goto done;
        }
        result.parser_runs++;
        if (UINT64_MAX - result.work_item_len < forest.work_item_len) {
            cetta_lp_native_utf8_forest_free(&forest);
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded witness work count overflow");
            goto done;
        }
        result.work_item_len += forest.work_item_len;
        if (forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
            result.outcome = PPLEX_V1_WITNESS_RECOGNIZER_LIMIT;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "%s guarded witness work limit %u at token %u",
                backend == PPGUARDED_LEX_EXEC_V1_GLL ? "GLL" : "GLR",
                limits->witness_work_limit, token_index);
            cetta_lp_native_utf8_forest_free(&forest);
            goto finish;
        }
        root = ppguarded_lex_exec_v1_forest_root_find(
            &forest, guarded_plan->entries[entry_index].state_id,
            0u, token_scalar_len);
        if (root < 0) {
            result.outcome = PPLEX_V1_WITNESS_RECOGNITION_MISMATCH;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "guarded DFA token %u has no exact ParserPack root",
                token_index);
            cetta_lp_native_utf8_forest_free(&forest);
            goto finish;
        }
        if (!ppguarded_lex_exec_v1_replay_value(
                pack,
                pack->states[
                    guarded_plan->entries[entry_index].state_id].identity,
                &extension, &forest, (uint32_t)root,
                limits->replay_depth, limits->result_limit,
                &result.arena, &value, &value_len, &replay_outcome,
                error_buf, error_buf_size)) {
            cetta_lp_native_utf8_forest_free(&forest);
            goto done;
        }
        cetta_lp_native_utf8_forest_free(&forest);
        if (replay_outcome == PPNATIVE_V1_REPLAY_DEPTH) {
            result.outcome = PPLEX_V1_WITNESS_REPLAY_DEPTH;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "guarded witness replay depth %u at token %u",
                limits->replay_depth, token_index);
            goto finish;
        }
        if (replay_outcome == PPNATIVE_V1_RESULT_LIMIT) {
            result.outcome = PPLEX_V1_WITNESS_RESULT_LIMIT;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "guarded witness result limit %u at token %u",
                limits->result_limit, token_index);
            goto finish;
        }
        if (replay_outcome != PPNATIVE_V1_COMPLETED ||
            value_len != 1u || !value) {
            result.outcome = PPLEX_V1_WITNESS_NONFUNCTIONAL;
            (void)snprintf(
                result.detail, sizeof(result.detail),
                "guarded token %u has %u semantic values",
                token_index, value_len);
            goto finish;
        }
        result.values[token_index] = value;
    }

finish:
    ppguarded_lex_exec_v1_witness_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppguarded_lex_exec_v1_witness_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to realize guarded lexical witnesses");
    }
    return ok;
}

static bool ppguarded_lex_exec_v1_witness_equal(
    Atom *const *left,
    Atom *const *right,
    uint32_t len) {
    uint32_t index;
    if (len > 0u && (!left || !right))
        return false;
    for (index = 0u; index < len; index++) {
        if (!atom_eq(left[index], right[index]))
            return false;
    }
    return true;
}

static bool ppguarded_lex_exec_v1_relation_accepts(
    const PPGuardRelationV1 *relation,
    uint32_t terminal_id,
    uint32_t scalar_left,
    uint32_t byte_left) {
    uint32_t index;
    for (index = 0u; index < relation->evidence_len; index++) {
        const PPGuardRelationV1Evidence *evidence =
            &relation->evidence[index];
        if (evidence->guard_terminal_id == terminal_id &&
            evidence->scalar_left == scalar_left &&
            evidence->byte_left == byte_left) {
            return true;
        }
    }
    return false;
}

static bool ppguarded_lex_exec_v1_projected_relation_build(
    const CettaLpNativeUtf8Lattice *projected_lattice,
    Atom *const *projected_values,
    uint32_t projected_value_len,
    const PPGuardRelationV1 *guard_relation,
    const PPGuardPlanV1 *guard_plan,
    const Arena *const *atom_owners,
    uint32_t atom_owner_len,
    PPGuardRelationV1 *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardRelationV1Match *matches = NULL;
    uint32_t index;
    bool ok = false;

    if (!projected_lattice ||
        (projected_value_len > 0u && !projected_values) ||
        !guard_relation || !guard_plan ||
        (guard_plan->entry_len > 0u &&
         !guard_plan->guard_terminal_ids) ||
        !atom_owners ||
        atom_owner_len == 0u || !out ||
        !ppguard_relation_v1_validate(
            guard_relation, error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_array_fits(
            guard_relation->evidence_len, sizeof(*matches))) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "bad projected positive-guard relation inputs");
        }
        goto done;
    }
    if (guard_relation->evidence_len > 0u) {
        matches = malloc(
            sizeof(*matches) * (size_t)guard_relation->evidence_len);
        if (!matches)
            goto done;
    }
    for (index = 0u; index < guard_relation->evidence_len; index++) {
        const PPGuardRelationV1Evidence *evidence =
            &guard_relation->evidence[index];
        matches[index] = (PPGuardRelationV1Match){
            .guard_terminal_id = evidence->guard_terminal_id,
            .scalar_left = evidence->scalar_left,
            .body_scalar_right = evidence->body_scalar_right,
            .byte_left = evidence->byte_left,
            .body_byte_right = evidence->body_byte_right,
            .value = evidence->value,
            .proof = evidence->proof,
        };
    }
    if (!ppguard_relation_v1_build_borrowed_atoms(
            projected_lattice, projected_values, projected_value_len,
            guard_plan->guard_terminal_ids, guard_plan->entry_len,
            matches, guard_relation->evidence_len,
            atom_owners, atom_owner_len,
            guard_relation->source_digest, out,
            error_buf, error_buf_size)) {
        goto done;
    }
    ok = true;

done:
    free(matches);
    return ok;
}

static bool ppguarded_lex_exec_v1_token_equal(
    const RSDFAV1Token *left, const RSDFAV1Token *right) {
    return left->tag == right->tag &&
        left->start_scalar == right->start_scalar &&
        left->end_scalar == right->end_scalar &&
        left->start_byte == right->start_byte &&
        left->end_byte == right->end_byte;
}

static bool ppguarded_lex_exec_v1_results_equal(
    const PPNativeV1Result *left,
    const PPNativeV1Result *right) {
    uint32_t index;
    if (left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        if (!atom_eq(left->semantic_results[index],
                     right->semantic_results[index])) {
            return false;
        }
    }
    return true;
}

static bool ppguarded_lex_exec_v1_slr_shadow_parse(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    const PPGuardedLexExecV1Limits *limits,
    CettaLpNativeSlrLatticeOutcome *slr_outcome,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result result;
    PPNativeV1ForestExtension extension;
    int32_t start_id;
    bool ok = false;

    ppnative_v1_result_init(&result);
    extension = (PPNativeV1ForestExtension){
        .states = guard_plan->states,
        .state_len = guard_plan->state_len,
        .terminals = exec_plan->terminals,
        .terminal_len = exec_plan->terminal_len,
        .productions = guard_plan->productions,
        .production_len = guard_plan->production_len,
        .witness_values = witness_values,
        .witness_len = witness_len,
    };
    if (!lattice || !slr_outcome ||
        (witness_len > 0u && !witness_values)) {
        goto done;
    }
    *slr_outcome = CETTA_LP_NATIVE_SLR_LATTICE_NEEDS_GENERAL;
    start_id = ppnative_v1_state_find_extended(
        pack, &extension, start_state);
    if (start_id < 0 || (uint32_t)start_id != exec_plan->start_state_id ||
        !ppnative_v1_start_is_closed_extended(
            pack, start_state, &extension,
            error_buf, error_buf_size)) {
        result.outcome = PPNATIVE_V1_UNSUPPORTED_OPEN_PACK;
        goto finish;
    }
    if (!cetta_lp_native_slr_prepared_parse_utf8_lattice_forest(
            &exec_plan->final_slr, lattice, 0u,
            limits->parse_work_limit, slr_outcome, &result.forest,
            error_buf, error_buf_size)) {
        goto done;
    }
    if (*slr_outcome == CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED) {
        result.accepted = true;
        if (!ppnative_v1_finish_extended(
                &result, pack, start_state, &extension,
                limits->replay_depth, limits->result_limit,
                error_buf, error_buf_size)) {
            goto done;
        }
    }

finish:
    ppnative_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppnative_v1_result_free(&result);
    return ok;
}

static bool ppguarded_lex_exec_v1_final_parse(
    PPGuardedLexExecV1Backend backend,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8Lattice *lattice,
    Atom *const *witness_values,
    uint32_t witness_len,
    const PPGuardedLexExecV1Limits *limits,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result result;
    PPNativeV1ForestExtension extension;
    int32_t start_id;
    uint32_t root_index;
    bool ok = false;

    ppnative_v1_result_init(&result);
    extension = (PPNativeV1ForestExtension){
        .states = guard_plan->states,
        .state_len = guard_plan->state_len,
        .terminals = exec_plan->terminals,
        .terminal_len = exec_plan->terminal_len,
        .productions = guard_plan->productions,
        .production_len = guard_plan->production_len,
        .witness_values = witness_values,
        .witness_len = witness_len,
    };
    if (!lattice || (witness_len > 0u && !witness_values)) {
        goto done;
    }
    start_id = ppnative_v1_state_find_extended(
        pack, &extension, start_state);
    if (start_id < 0 || (uint32_t)start_id != exec_plan->start_state_id ||
        !ppnative_v1_start_is_closed_extended(
            pack, start_state, &extension,
            error_buf, error_buf_size)) {
        result.outcome = PPNATIVE_V1_UNSUPPORTED_OPEN_PACK;
        goto finish;
    }
    if (backend == PPGUARDED_LEX_EXEC_V1_GLL) {
        if (!cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete(
                &exec_plan->final_gll, lattice, 0u,
                limits->parse_work_limit, &result.forest,
                error_buf, error_buf_size)) {
            goto done;
        }
    } else if (!cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from(
                   &exec_plan->final_glr, lattice, 0u,
                   limits->parse_work_limit, &result.forest,
                   error_buf, error_buf_size)) {
        goto done;
    }
    if (result.forest.outcome ==
        CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        result.outcome = PPNATIVE_V1_RECOGNIZER_LIMIT;
        (void)snprintf(
            result.detail, sizeof(result.detail),
            "%s guarded lexical parse work limit %u",
            backend == PPGUARDED_LEX_EXEC_V1_GLL ? "GLL" : "GLR",
            limits->parse_work_limit);
        goto finish;
    }
    for (root_index = 0u;
         root_index < result.forest.root_len; root_index++) {
        uint32_t node_index = result.forest.roots[root_index];
        if (node_index < result.forest.node_len &&
            result.forest.nodes[node_index].scalar_left == 0u &&
            result.forest.nodes[node_index].scalar_right ==
                result.forest.scalar_len) {
            result.accepted = true;
            break;
        }
    }
    if (!ppnative_v1_finish_extended(
            &result, pack, start_state, &extension,
            limits->replay_depth, limits->result_limit,
            error_buf, error_buf_size)) {
        goto done;
    }

finish:
    ppnative_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    ppnative_v1_result_free(&result);
    return ok;
}

static bool ppguarded_lex_exec_v1_run_impl(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input,
    size_t input_len,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPGuardedLexExecV1Result result;
    RSDFAV1ScanResult ordinary_scan;
    RSDFAV1ScanResult guard_scan;
    RSDFAV1ScanResult projected_scan;
    uint32_t guarded_offset;
    uint32_t guard_offset;
    uint32_t ordinary_write = 0u;
    uint32_t guard_write = 0u;
    uint32_t guarded_write = 0u;
    uint32_t projected_write = 0u;
    uint32_t token_index;
    const Arena *gll_projected_atom_owners[3];
    const Arena *glr_projected_atom_owners[3];
    bool scanned;
    bool ok = false;

    ppguarded_lex_exec_v1_result_init(&result);
    memset(&ordinary_scan, 0, sizeof(ordinary_scan));
    memset(&guard_scan, 0, sizeof(guard_scan));
    memset(&projected_scan, 0, sizeof(projected_scan));
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state || !lexical_plan || !guard_plan ||
        !guarded_plan || !exec_plan || !limits || !out ||
        (!view && input_len > 0u && !input) ||
        (view && (input || input_len > 0u)) ||
        !ppguarded_lex_exec_v1_limits_valid(limits) ||
        !ppguarded_lex_exec_v1_plan_matches(
            pack, start_state, lexical_plan, guard_plan,
            guarded_plan, exec_plan) ||
        (view && !cetta_lp_native_utf8_scalar_view_validate(
            view, error_buf, error_buf_size))) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "bad guarded lexical execution inputs");
        goto done;
    }
    guarded_offset = exec_plan->lexical_tag_len;
    guard_offset = guarded_offset + exec_plan->guarded_tag_len;
    scanned = view
        ? rsdfa_v1_scan_scalar_view(
              &exec_plan->dfa, view, limits->scan_work_limit,
              limits->scan_token_limit, &result.scan,
              error_buf, error_buf_size)
        : rsdfa_v1_scan(
              &exec_plan->dfa, input, input_len,
              limits->scan_work_limit, limits->scan_token_limit,
              &result.scan, error_buf, error_buf_size);
    if (!scanned ||
        result.scan.outcome != RSDFA_V1_SCAN_COMPLETED ||
        result.scan.tag_len != exec_plan->combined_tag_len ||
        result.scan.source_pass_count !=
            (view ? view->source_pass_count : 1u) ||
        !ppguarded_lex_exec_v1_source_receipt_valid(
            result.scan.source_pass_count,
            result.scan.decoded_byte_len,
            result.scan.input_byte_len)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "combined guarded lexical DFA did not complete");
        }
        goto done;
    }
    for (token_index = 0u;
         token_index < result.scan.token_len; token_index++) {
        uint32_t tag = result.scan.tokens[token_index].tag;
        if (tag < guarded_offset) {
            result.ordinary_token_len++;
        } else if (tag < guard_offset) {
            result.guarded_candidate_len++;
        } else if (tag < exec_plan->combined_tag_len) {
            result.guard_token_len++;
        } else {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "combined DFA emitted an invalid tag");
            goto done;
        }
    }
    result.ordinary_tokens = malloc(
        sizeof(*result.ordinary_tokens) *
        (size_t)(result.ordinary_token_len
                 ? result.ordinary_token_len : 1u));
    result.guard_tokens = malloc(
        sizeof(*result.guard_tokens) *
        (size_t)(result.guard_token_len ? result.guard_token_len : 1u));
    if (!result.ordinary_tokens || !result.guard_tokens)
        goto done;
    for (token_index = 0u;
         token_index < result.scan.token_len; token_index++) {
        RSDFAV1Token token = result.scan.tokens[token_index];
        if (token.tag < guarded_offset) {
            result.ordinary_tokens[ordinary_write++] = token;
        } else if (token.tag >= guard_offset) {
            token.tag -= guard_offset;
            result.guard_tokens[guard_write++] = token;
        }
    }
    if (ordinary_write != result.ordinary_token_len ||
        guard_write != result.guard_token_len) {
        goto done;
    }
    ordinary_scan = result.scan;
    ordinary_scan.tokens = result.ordinary_tokens;
    ordinary_scan.token_len = result.ordinary_token_len;
    ordinary_scan.tag_len = exec_plan->lexical_tag_len;
    guard_scan = result.scan;
    guard_scan.tokens = result.guard_tokens;
    guard_scan.token_len = result.guard_token_len;
    guard_scan.tag_len = exec_plan->guard_tag_len;
    if (!pplex_v1_witness_table_build_gll_prepared(
            pack, lexical_plan, &ordinary_scan,
            &exec_plan->ordinary_witness_parsers,
            &exec_plan->scalar_terminals,
            limits->witness_work_limit, limits->replay_depth,
            limits->result_limit, &result.ordinary_gll_witness,
            error_buf, error_buf_size) ||
        !pplex_v1_witness_table_build_glr_prepared(
            pack, lexical_plan, &ordinary_scan,
            &exec_plan->ordinary_witness_parsers,
            &exec_plan->scalar_terminals,
            limits->witness_work_limit, limits->replay_depth,
            limits->result_limit, &result.ordinary_glr_witness,
            error_buf, error_buf_size) ||
        result.ordinary_gll_witness.outcome !=
            PPLEX_V1_WITNESS_COMPLETED ||
        result.ordinary_glr_witness.outcome !=
            PPLEX_V1_WITNESS_COMPLETED ||
        result.ordinary_gll_witness.parser_runs !=
            result.ordinary_glr_witness.parser_runs ||
        !ppguarded_lex_exec_v1_witness_equal(
            result.ordinary_gll_witness.values,
            result.ordinary_glr_witness.values,
            result.ordinary_token_len) ||
        !rsdfa_v1_composite_parser_lattice_build(
            &ordinary_scan, exec_plan->scalar_terminals.terminals,
            exec_plan->scalar_terminals.len,
            lexical_plan->tag_terminal_ids,
            exec_plan->lexical_tag_len, &result.ordinary_lattice,
            error_buf, error_buf_size)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "ordinary lexical witnesses did not agree");
        }
        goto done;
    }
    if (!ppguard_ref_v1_relation_build_gll_prepared(
            pack, guard_plan, exec_plan->guard_tags,
            exec_plan->guard_tag_len, &guard_scan,
            &result.ordinary_lattice.lattice,
            result.ordinary_gll_witness.values,
            result.ordinary_gll_witness.len,
            &exec_plan->guard_witness_parsers,
            limits->witness_work_limit, limits->replay_depth,
            limits->result_limit, &result.gll_relation,
            &result.gll_guard_witness, error_buf, error_buf_size) ||
        !ppguard_ref_v1_relation_build_glr_prepared(
            pack, guard_plan, exec_plan->guard_tags,
            exec_plan->guard_tag_len, &guard_scan,
            &result.ordinary_lattice.lattice,
            result.ordinary_glr_witness.values,
            result.ordinary_glr_witness.len,
            &exec_plan->guard_witness_parsers,
            limits->witness_work_limit, limits->replay_depth,
            limits->result_limit, &result.glr_relation,
            &result.glr_guard_witness, error_buf, error_buf_size) ||
        strcmp(result.gll_relation.relation_digest,
               result.glr_relation.relation_digest) != 0 ||
        result.gll_guard_witness.parser_run_len !=
            result.glr_guard_witness.parser_run_len ||
        result.gll_guard_witness.match_len !=
            result.glr_guard_witness.match_len) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guard relations did not agree");
        }
        goto done;
    }
    result.guarded_tokens = malloc(
        sizeof(*result.guarded_tokens) *
        (size_t)(result.guarded_candidate_len
                 ? result.guarded_candidate_len : 1u));
    result.projected_tokens = malloc(
        sizeof(*result.projected_tokens) *
        (size_t)((result.ordinary_token_len +
                  result.guarded_candidate_len)
                 ? (result.ordinary_token_len +
                    result.guarded_candidate_len)
                 : 1u));
    if (!result.guarded_tokens || !result.projected_tokens)
        goto done;
    for (token_index = 0u;
         token_index < result.scan.token_len; token_index++) {
        RSDFAV1Token token = result.scan.tokens[token_index];
        if (token.tag < guarded_offset) {
            result.projected_tokens[projected_write++] = token;
        } else if (token.tag < guard_offset) {
            uint32_t local = token.tag - guarded_offset;
            int32_t entry_index = ppguarded_lex_exec_v1_guarded_entry(
                guarded_plan, local);
            uint32_t guard_entry;
            uint32_t guard_terminal;
            if (entry_index < 0)
                goto done;
            guard_entry = guarded_plan->entries[entry_index].guard_entry_index;
            guard_terminal = guard_plan->entries[guard_entry].terminal_id;
            if (!ppguarded_lex_exec_v1_relation_accepts(
                    &result.gll_relation, guard_terminal,
                    token.end_scalar, token.end_byte)) {
                continue;
            }
            result.projected_tokens[projected_write++] = token;
            token.tag = local;
            result.guarded_tokens[guarded_write++] = token;
        }
    }
    result.guarded_token_len = guarded_write;
    result.projected_token_len = projected_write;
    if (!ppguarded_lex_exec_v1_witness_build(
            PPGUARDED_LEX_EXEC_V1_GLL, pack, lexical_plan,
            guard_plan, guarded_plan, result.guarded_tokens,
            result.guarded_token_len, &result.gll_relation,
            &exec_plan->guarded_witness_parsers, limits,
            &result.guarded_gll_witness,
            error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_witness_build(
            PPGUARDED_LEX_EXEC_V1_GLR, pack, lexical_plan,
            guard_plan, guarded_plan, result.guarded_tokens,
            result.guarded_token_len, &result.glr_relation,
            &exec_plan->guarded_witness_parsers, limits,
            &result.guarded_glr_witness,
            error_buf, error_buf_size) ||
        result.guarded_gll_witness.outcome !=
            PPLEX_V1_WITNESS_COMPLETED ||
        result.guarded_glr_witness.outcome !=
            PPLEX_V1_WITNESS_COMPLETED ||
        result.guarded_gll_witness.parser_runs !=
            result.guarded_glr_witness.parser_runs ||
        !ppguarded_lex_exec_v1_witness_equal(
            result.guarded_gll_witness.values,
            result.guarded_glr_witness.values,
            result.guarded_token_len)) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical witnesses did not agree");
        }
        goto done;
    }
    projected_scan = result.scan;
    projected_scan.tokens = result.projected_tokens;
    projected_scan.token_len = result.projected_token_len;
    projected_scan.tag_len =
        exec_plan->lexical_tag_len + exec_plan->guarded_tag_len;
    if (!rsdfa_v1_composite_parser_lattice_build(
            &projected_scan, exec_plan->scalar_terminals.terminals,
            exec_plan->scalar_terminals.len,
            exec_plan->projected_tag_terminal_ids,
            projected_scan.tag_len, &result.projected_lattice,
            error_buf, error_buf_size)) {
        goto done;
    }
    result.witness_len = result.projected_token_len;
    result.gll_witness_values = calloc(
        result.witness_len ? result.witness_len : 1u,
        sizeof(*result.gll_witness_values));
    result.glr_witness_values = calloc(
        result.witness_len ? result.witness_len : 1u,
        sizeof(*result.glr_witness_values));
    if (!result.gll_witness_values || !result.glr_witness_values)
        goto done;
    ordinary_write = 0u;
    guarded_write = 0u;
    for (token_index = 0u;
         token_index < result.projected_token_len; token_index++) {
        const RSDFAV1Token *token = &result.projected_tokens[token_index];
        if (token->tag < guarded_offset) {
            if (ordinary_write >= result.ordinary_token_len ||
                !ppguarded_lex_exec_v1_token_equal(
                    token, &result.ordinary_tokens[ordinary_write])) {
                goto done;
            }
            result.gll_witness_values[token_index] =
                result.ordinary_gll_witness.values[ordinary_write];
            result.glr_witness_values[token_index] =
                result.ordinary_glr_witness.values[ordinary_write];
            ordinary_write++;
        } else {
            RSDFAV1Token local = *token;
            local.tag -= guarded_offset;
            if (guarded_write >= result.guarded_token_len ||
                !ppguarded_lex_exec_v1_token_equal(
                    &local, &result.guarded_tokens[guarded_write])) {
                goto done;
            }
            result.gll_witness_values[token_index] =
                result.guarded_gll_witness.values[guarded_write];
            result.glr_witness_values[token_index] =
                result.guarded_glr_witness.values[guarded_write];
            guarded_write++;
        }
    }
    gll_projected_atom_owners[0] =
        &result.ordinary_gll_witness.arena;
    gll_projected_atom_owners[1] =
        &result.guarded_gll_witness.arena;
    gll_projected_atom_owners[2] = &result.gll_relation.arena;
    glr_projected_atom_owners[0] =
        &result.ordinary_glr_witness.arena;
    glr_projected_atom_owners[1] =
        &result.guarded_glr_witness.arena;
    glr_projected_atom_owners[2] = &result.glr_relation.arena;
    if (ordinary_write != result.ordinary_token_len ||
        guarded_write != result.guarded_token_len ||
        !ppguarded_lex_exec_v1_projected_relation_build(
            &result.projected_lattice.lattice,
            result.gll_witness_values, result.witness_len,
            &result.gll_relation, guard_plan,
            gll_projected_atom_owners, 3u,
            &result.gll_projected_relation,
            error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_projected_relation_build(
            &result.projected_lattice.lattice,
            result.glr_witness_values, result.witness_len,
            &result.glr_relation, guard_plan,
            glr_projected_atom_owners, 3u,
            &result.glr_projected_relation,
            error_buf, error_buf_size) ||
        strcmp(result.gll_projected_relation.relation_digest,
               result.glr_projected_relation.relation_digest) != 0) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "projected guarded lexical relations did not agree");
        }
        goto done;
    }
    if (!ppguarded_lex_exec_v1_slr_shadow_parse(
            pack, start_state, guard_plan, exec_plan,
            &result.gll_projected_relation.lattice,
            result.gll_projected_relation.witness_values,
            result.gll_projected_relation.witness_len,
            limits, &result.receipt.slr_outcome, &result.slr,
            error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_final_parse(
            PPGUARDED_LEX_EXEC_V1_GLL, pack, start_state,
            guard_plan, exec_plan,
            &result.gll_projected_relation.lattice,
            result.gll_projected_relation.witness_values,
            result.gll_projected_relation.witness_len,
            limits, &result.gll, error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_final_parse(
            PPGUARDED_LEX_EXEC_V1_GLR, pack, start_state,
            guard_plan, exec_plan,
            &result.glr_projected_relation.lattice,
            result.glr_projected_relation.witness_values,
            result.glr_projected_relation.witness_len,
            limits, &result.glr, error_buf, error_buf_size) ||
        !ppguarded_lex_exec_v1_results_equal(
            &result.gll, &result.glr) ||
        (result.receipt.slr_outcome ==
             CETTA_LP_NATIVE_SLR_LATTICE_ACCEPTED &&
         !ppguarded_lex_exec_v1_results_equal(
             &result.slr, &result.gll))) {
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "SLR/GLL/GLR guarded lexical parses did not agree");
        }
        goto done;
    }
    memcpy(result.receipt.execution_plan_digest,
           exec_plan->execution_plan_digest, 65u);
    memcpy(result.receipt.guard_relation_digest,
           result.gll_relation.relation_digest, 65u);
    memcpy(result.receipt.projected_relation_digest,
           result.gll_projected_relation.relation_digest, 65u);
    memcpy(result.receipt.forest_digest, result.gll.forest_digest, 65u);
    result.receipt.source_pass_count = result.scan.source_pass_count;
    result.receipt.dfa_scan_count = 1u;
    result.receipt.input_byte_len = result.scan.input_byte_len;
    result.receipt.input_scalar_len = result.scan.input_scalar_len;
    result.receipt.combined_token_len = result.scan.token_len;
    result.receipt.ordinary_token_len = result.ordinary_token_len;
    result.receipt.guard_body_token_len = result.guard_token_len;
    result.receipt.guarded_candidate_len = result.guarded_candidate_len;
    result.receipt.guarded_token_len = result.guarded_token_len;
    result.receipt.projected_token_len = result.projected_token_len;
    result.receipt.ordinary_witness_runs =
        result.ordinary_gll_witness.parser_runs;
    result.receipt.guard_witness_runs =
        result.gll_guard_witness.parser_run_len;
    result.receipt.guarded_witness_runs =
        result.guarded_gll_witness.parser_runs;
    result.receipt.ordinary_witness_parser_family_build_count =
        exec_plan->ordinary_witness_parser_family_build_count;
    result.receipt.ordinary_witness_prepared_start_len =
        exec_plan->ordinary_witness_prepared_start_len;
    result.receipt.guard_witness_parser_family_build_count =
        exec_plan->guard_witness_parser_family_build_count;
    result.receipt.guard_witness_prepared_start_len =
        exec_plan->guard_witness_prepared_start_len;
    result.receipt.guarded_witness_parser_family_build_count =
        exec_plan->guarded_witness_parser_family_build_count;
    result.receipt.guarded_witness_prepared_start_len =
        exec_plan->guarded_witness_prepared_start_len;
    result.receipt.final_parser_table_build_count =
        exec_plan->final_parser_table_build_count;
    result.receipt.slr_parse_work_item_len =
        result.slr.forest.work_item_len;
    result.receipt.final_parse_work_item_len =
        (uint64_t)result.gll.forest.work_item_len +
        (uint64_t)result.glr.forest.work_item_len;
    result.receipt.outcome = result.gll.outcome;
    result.receipt.accepted = result.gll.accepted;
    ppguarded_lex_exec_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    if (!ppguarded_lex_exec_v1_rebind_result_owners(out) ||
        !ppguard_relation_v1_validate(
            &out->gll_projected_relation, error_buf, error_buf_size) ||
        !ppguard_relation_v1_validate(
            &out->glr_projected_relation, error_buf, error_buf_size)) {
        ppguarded_lex_exec_v1_result_free(out);
        if (error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
            ppguarded_lex_exec_v1_set_error(
                error_buf, error_buf_size,
                "guarded lexical result retained stale Atom owners");
        }
        goto done;
    }
    ok = true;

done:
    ppguarded_lex_exec_v1_result_free(&result);
    if (!ok && error_buf && error_buf_size > 0u && error_buf[0] == '\0') {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "failed to execute guarded lexical ParserPack");
    }
    return ok;
}

bool ppguarded_lex_exec_v1_run_bytes(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const uint8_t *input,
    size_t input_len,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppguarded_lex_exec_v1_run_impl(
        pack, start_state, lexical_plan, guard_plan, guarded_plan,
        exec_plan, input, input_len, NULL, limits, out,
        error_buf, error_buf_size);
}

bool ppguarded_lex_exec_v1_run(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const PPLexV1Plan *lexical_plan,
    const PPGuardPlanV1 *guard_plan,
    const PPGuardedLexV1Plan *guarded_plan,
    const PPGuardedLexExecV1Plan *exec_plan,
    const CettaLpNativeUtf8ScalarView *view,
    const PPGuardedLexExecV1Limits *limits,
    PPGuardedLexExecV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    if (!view) {
        ppguarded_lex_exec_v1_set_error(
            error_buf, error_buf_size,
            "missing guarded lexical scalar view");
        return false;
    }
    return ppguarded_lex_exec_v1_run_impl(
        pack, start_state, lexical_plan, guard_plan, guarded_plan,
        exec_plan, NULL, 0u, view, limits, out,
        error_buf, error_buf_size);
}
