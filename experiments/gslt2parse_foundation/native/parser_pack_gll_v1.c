#include "parser_pack_gll_v1.h"

#include <stdio.h>
#include <string.h>

static bool ppgll_v1_parse_input(
    const PPNativeV1Prepared *prepared,
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    bool materialize_semantics,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    PPNativeV1Result result;
    PPNativeV1TerminalPlan terminals = {0};
    CettaLpNativeGrammar grammar;
    PPNativeV1PreparedView prepared_view = {0};
    int32_t start_id;
    char closure_error[512] = {0};
    uint32_t root_index;
    bool ok = false;

    ppnative_v1_result_init(&result);
    cetta_lp_native_grammar_init(&grammar);
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if ((!prepared && (!pack || !start_state)) ||
        !out || descriptor_limit == 0u ||
        (materialize_semantics &&
         (replay_depth == 0u || result_limit == 0u)) ||
        (view && (input_bytes || input_byte_len != 0u)) ||
        (!view && input_byte_len > 0u && !input_bytes)) {
        if (error_buf && error_buf_size > 0u) {
            (void)snprintf(error_buf, error_buf_size,
                           "bad ParserPack GLL arguments");
        }
        goto done;
    }
    if (prepared) {
        if (!ppnative_v1_prepared_view(
                prepared, &prepared_view,
                error_buf, error_buf_size)) {
            goto done;
        }
        pack = prepared_view.pack;
        start_state = prepared_view.start_state;
        start_id = (int32_t)prepared_view.start_state_id;
    } else {
        start_id = ppnative_v1_state_find(pack, start_state);
        if (start_id < 0 || !ppabi_v1_pack_start_is_closed(
                                pack, start_state, closure_error,
                                sizeof(closure_error))) {
            result.outcome = PPNATIVE_V1_UNSUPPORTED_OPEN_PACK;
            (void)snprintf(result.detail, sizeof(result.detail), "%s",
                           closure_error[0] ? closure_error
                                            : "start state is absent");
            goto finish;
        }
        if (!ppnative_v1_terminal_plan_build(
                pack, &terminals, error_buf, error_buf_size) ||
            !ppnative_v1_grammar_build(pack, &grammar)) {
            if (!error_buf || error_buf[0] == '\0') {
                if (error_buf && error_buf_size > 0u) {
                    (void)snprintf(
                        error_buf, error_buf_size,
                        "failed to adapt ParserPack recognition data");
                }
            }
            goto done;
        }
    }
    if (!(prepared
              ? (view
                    ? cetta_lp_native_gll_prepared_parse_utf8_scalar_view_forest_complete(
                          prepared_view.gll,
                          prepared_view.terminal_plan->terminals,
                          prepared_view.terminal_plan->len,
                          view, descriptor_limit, &result.forest,
                          error_buf, error_buf_size)
                    : cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
                          prepared_view.gll,
                          prepared_view.terminal_plan->terminals,
                          prepared_view.terminal_plan->len,
                          input_bytes, input_byte_len,
                          descriptor_limit, &result.forest,
                          error_buf, error_buf_size))
              : (view
                    ? cetta_lp_native_gll_parse_utf8_scalar_view_forest_complete(
                          &grammar, (uint32_t)start_id,
                          terminals.terminals, terminals.len,
                          view, descriptor_limit, &result.forest,
                          error_buf, error_buf_size)
                    : cetta_lp_native_gll_parse_utf8_forest_complete(
                          &grammar, (uint32_t)start_id,
                          terminals.terminals, terminals.len,
                          input_bytes, input_byte_len,
                          descriptor_limit, &result.forest,
                          error_buf, error_buf_size)))) {
        goto done;
    }
    if (result.forest.outcome ==
        CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        result.outcome = PPNATIVE_V1_RECOGNIZER_LIMIT;
        (void)snprintf(result.detail, sizeof(result.detail),
                       "GLL descriptor limit %u", descriptor_limit);
        goto finish;
    }
    for (root_index = 0u; root_index < result.forest.root_len; root_index++) {
        uint32_t node_index = result.forest.roots[root_index];
        if (node_index < result.forest.node_len &&
            result.forest.nodes[node_index].scalar_left == 0u &&
            result.forest.nodes[node_index].scalar_right ==
                result.forest.scalar_len) {
            result.accepted = true;
            break;
        }
    }
    if (materialize_semantics &&
        !ppnative_v1_finish(
            &result, pack, start_state, replay_depth, result_limit,
            error_buf, error_buf_size)) {
        goto done;
    }

finish:
    ppnative_v1_result_free(out);
    *out = result;
    memset(&result, 0, sizeof(result));
    ok = true;

done:
    cetta_lp_native_grammar_free(&grammar);
    ppnative_v1_terminal_plan_free(&terminals);
    ppnative_v1_result_free(&result);
    return ok;
}

bool ppgll_v1_parse(const PPABIV1Pack *pack,
                    const Atom *start_state,
                    const uint8_t *input_bytes,
                    size_t input_byte_len,
                    uint32_t descriptor_limit,
                    uint32_t replay_depth,
                    uint32_t result_limit,
                    PPNativeV1Result *out,
                    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        NULL, pack, start_state, input_bytes, input_byte_len, NULL,
        descriptor_limit, replay_depth, result_limit, true,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_parse_scalar_view(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        NULL, pack, start_state, NULL, 0u, view,
        descriptor_limit, replay_depth, result_limit, true,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_recognize(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        NULL, pack, start_state, input_bytes, input_byte_len, NULL,
        descriptor_limit, 0u, 0u, false,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_recognize_scalar_view(
    const PPABIV1Pack *pack,
    const Atom *start_state,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        NULL, pack, start_state, NULL, 0u, view,
        descriptor_limit, 0u, 0u, false,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_prepared_parse(
    const PPNativeV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        prepared, NULL, NULL, input_bytes, input_byte_len, NULL,
        descriptor_limit, replay_depth, result_limit, true,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_prepared_parse_scalar_view(
    const PPNativeV1Prepared *prepared,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    uint32_t replay_depth,
    uint32_t result_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        prepared, NULL, NULL, NULL, 0u, view,
        descriptor_limit, replay_depth, result_limit, true,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_prepared_recognize(
    const PPNativeV1Prepared *prepared,
    const uint8_t *input_bytes,
    size_t input_byte_len,
    uint32_t descriptor_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        prepared, NULL, NULL, input_bytes, input_byte_len, NULL,
        descriptor_limit, 0u, 0u, false,
        out, error_buf, error_buf_size);
}

bool ppgll_v1_prepared_recognize_scalar_view(
    const PPNativeV1Prepared *prepared,
    const CettaLpNativeUtf8ScalarView *view,
    uint32_t descriptor_limit,
    PPNativeV1Result *out,
    char *error_buf,
    size_t error_buf_size) {
    return ppgll_v1_parse_input(
        prepared, NULL, NULL, NULL, 0u, view,
        descriptor_limit, 0u, 0u, false,
        out, error_buf, error_buf_size);
}
