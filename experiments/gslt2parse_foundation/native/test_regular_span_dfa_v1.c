#include "regular_span_dfa_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "regular-span DFA gate failed: %s\n", message); \
            return 1; \
        } \
    } while (0)

static bool has_token(const RSDFAV1ScanResult *result,
                      uint32_t tag,
                      uint32_t start_scalar,
                      uint32_t end_scalar,
                      uint32_t start_byte,
                      uint32_t end_byte) {
    uint32_t index;
    for (index = 0u; index < result->token_len; index++) {
        const RSDFAV1Token *token = &result->tokens[index];
        if (token->tag == tag &&
            token->start_scalar == start_scalar &&
            token->end_scalar == end_scalar &&
            token->start_byte == start_byte &&
            token->end_byte == end_byte) {
            return true;
        }
    }
    return false;
}

static uint32_t token_witness(const RSDFAV1ScanResult *result,
                              uint32_t tag,
                              uint32_t start_scalar,
                              uint32_t end_scalar) {
    uint32_t index;
    for (index = 0u; index < result->token_len; index++) {
        const RSDFAV1Token *token = &result->tokens[index];
        if (token->tag == tag &&
            token->start_scalar == start_scalar &&
            token->end_scalar == end_scalar) {
            return index;
        }
    }
    return UINT32_MAX;
}

static bool same_token(const RSDFAV1Token *left,
                       const RSDFAV1Token *right) {
    return left->tag == right->tag &&
           left->start_scalar == right->start_scalar &&
           left->end_scalar == right->end_scalar &&
           left->start_byte == right->start_byte &&
           left->end_byte == right->end_byte;
}

static bool same_recognition(const RSDFAV1ScanResult *left,
                             const RSDFAV1ScanResult *right) {
    uint32_t index;

    if (left->outcome != right->outcome ||
        left->token_len != right->token_len ||
        left->tag_len != right->tag_len ||
        left->input_byte_len != right->input_byte_len ||
        left->input_scalar_len != right->input_scalar_len ||
        left->work_item_len != right->work_item_len) {
        return false;
    }
    for (index = 0u; index < left->input_scalar_len; index++) {
        if (left->codepoints[index] != right->codepoints[index] ||
            left->byte_offsets[index] != right->byte_offsets[index]) {
            return false;
        }
    }
    if (left->byte_offsets[left->input_scalar_len] !=
        right->byte_offsets[right->input_scalar_len]) {
        return false;
    }
    for (index = 0u; index < left->token_len; index++) {
        const RSDFAV1Token *left_token = &left->tokens[index];
        const RSDFAV1Token *right_token = &right->tokens[index];
        if (left_token->tag != right_token->tag ||
            left_token->start_scalar != right_token->start_scalar ||
            left_token->end_scalar != right_token->end_scalar ||
            left_token->start_byte != right_token->start_byte ||
            left_token->end_byte != right_token->end_byte) {
            return false;
        }
    }
    return true;
}

static bool cursor_is_longest_projection(
    const RSDFAV1ScanResult *complete,
    uint32_t start_scalar,
    const RSDFAV1Token *cursor_tokens,
    const RSDFAV1CursorScanResult *cursor) {
    uint32_t tag;
    uint32_t write = 0u;

    if (!complete || !cursor ||
        cursor->outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED ||
        cursor->start_scalar != start_scalar) {
        return false;
    }
    for (tag = 0u; tag < complete->tag_len; tag++) {
        const RSDFAV1Token *longest = NULL;
        uint32_t index;
        for (index = 0u; index < complete->token_len; index++) {
            const RSDFAV1Token *candidate = &complete->tokens[index];
            if (candidate->tag == tag &&
                candidate->start_scalar == start_scalar &&
                (!longest ||
                 candidate->end_scalar > longest->end_scalar)) {
                longest = candidate;
            }
        }
        if (!longest)
            continue;
        if (write >= cursor->accept_len ||
            !same_token(longest, &cursor_tokens[write])) {
            return false;
        }
        write++;
    }
    return write == cursor->accept_len;
}

static bool same_parser_lattice(const RSDFAV1ParserLattice *left,
                                const RSDFAV1ParserLattice *right) {
    uint32_t index;

    if (left->lattice.terminal_len != right->lattice.terminal_len ||
        left->lattice.edge_len != right->lattice.edge_len ||
        left->lattice.start_offset_len !=
            right->lattice.start_offset_len ||
        left->lattice.scalar_len != right->lattice.scalar_len ||
        left->lattice.input_byte_len != right->lattice.input_byte_len ||
        left->lattice.decoded_byte_len !=
            right->lattice.decoded_byte_len ||
        left->lattice.source_pass_count !=
            right->lattice.source_pass_count) {
        return false;
    }
    for (index = 0u; index < left->lattice.terminal_len; index++) {
        if (left->lattice.terminal_ids[index] !=
            right->lattice.terminal_ids[index]) {
            return false;
        }
    }
    for (index = 0u; index < left->lattice.edge_len; index++) {
        const CettaLpNativeUtf8LatticeEdge *left_edge =
            &left->lattice.edges[index];
        const CettaLpNativeUtf8LatticeEdge *right_edge =
            &right->lattice.edges[index];
        if (left_edge->terminal_id != right_edge->terminal_id ||
            left_edge->scalar_left != right_edge->scalar_left ||
            left_edge->scalar_right != right_edge->scalar_right ||
            left_edge->byte_left != right_edge->byte_left ||
            left_edge->byte_right != right_edge->byte_right ||
            left_edge->value_kind != right_edge->value_kind ||
            left_edge->value != right_edge->value) {
            return false;
        }
    }
    for (index = 0u; index < left->lattice.scalar_len; index++) {
        if (left->lattice.codepoints[index] !=
            right->lattice.codepoints[index]) {
            return false;
        }
    }
    for (index = 0u; index <= left->lattice.scalar_len; index++) {
        if (left->lattice.byte_offsets[index] !=
            right->lattice.byte_offsets[index]) {
            return false;
        }
    }
    for (index = 0u; index < left->lattice.start_offset_len; index++) {
        if (left->lattice.start_offsets[index] !=
            right->lattice.start_offsets[index]) {
            return false;
        }
    }
    return true;
}

static bool forest_has_root_right(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t right) {
    uint32_t index;
    for (index = 0u; index < forest->root_len; index++) {
        uint32_t node_index = forest->roots[index];
        if (node_index < forest->node_len &&
            forest->nodes[node_index].scalar_left == 0u &&
            forest->nodes[node_index].scalar_right == right) {
            return true;
        }
    }
    return false;
}

static bool forest_has_witness(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t terminal_id,
    uint32_t right,
    uint32_t witness) {
    uint32_t index;
    for (index = 0u; index < forest->node_len; index++) {
        const CettaLpNativeUtf8ForestNode *node = &forest->nodes[index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM &&
            node->symbol_id == terminal_id &&
            node->scalar_left == 0u &&
            node->scalar_right == right &&
            node->terminal_value_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
            node->terminal_witness_id == witness) {
            return true;
        }
    }
    return false;
}

static bool forest_has_scalar(
    const CettaLpNativeUtf8Forest *forest,
    uint32_t terminal_id,
    uint32_t left,
    uint32_t right,
    uint32_t scalar) {
    uint32_t index;
    for (index = 0u; index < forest->node_len; index++) {
        const CettaLpNativeUtf8ForestNode *node = &forest->nodes[index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM &&
            node->symbol_id == terminal_id &&
            node->scalar_left == left &&
            node->scalar_right == right &&
            node->terminal_value_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR &&
            node->terminal_scalar == scalar) {
            return true;
        }
    }
    return false;
}

int main(void) {
    static const CettaLpNativeUnicodeRange a_range[] = {{97u, 97u}};
    static const CettaLpNativeUnicodeRange lambda_range[] = {{955u, 955u}};
    static const uint32_t starts[] = {0u, 2u, 4u, 6u, 9u};
    static const RSDFAV1NfaEdge edges[] = {
        {0u, 1u, RSDFA_V1_NFA_RANGES, a_range, 1u},
        {1u, 1u, RSDFA_V1_NFA_RANGES, a_range, 1u},
        {2u, 3u, RSDFA_V1_NFA_RANGES, a_range, 1u},
        {4u, 5u, RSDFA_V1_NFA_EPSILON, NULL, 0u},
        {6u, 7u, RSDFA_V1_NFA_RANGES, a_range, 1u},
        {7u, 8u, RSDFA_V1_NFA_EOF, NULL, 0u},
        {9u, 10u, RSDFA_V1_NFA_RANGES, lambda_range, 1u},
    };
    static const uint32_t functional_annotations[] = {
        7u, 7u, 7u, UINT32_MAX, 7u, UINT32_MAX, 10u,
    };
    static const RSDFAV1NfaAccept accepts[] = {
        {1u, 0u}, {3u, 1u}, {5u, 2u}, {8u, 3u}, {10u, 4u},
    };
    static const uint8_t input[] = {'a', 'a', 0xceu, 0xbbu, 'a'};
    static const uint8_t invalid_utf8[] = {0xc0u, 0x80u};
    RSDFAV1Nfa nfa = {
        .state_len = 11u,
        .start_states = starts,
        .start_len = 5u,
        .edges = edges,
        .edge_len = 7u,
        .accepts = accepts,
        .accept_len = 5u,
        .tag_len = 5u,
    };
    RSDFAV1Plan plan;
    RSDFAV1Program program;
    RSDFAV1InclusionResult inclusion;
    RSDFAV1FunctionalityResult functionality;
    RSDFAV1ScanResult result;
    RSDFAV1ParserLattice parser_lattice;
    RSDFAV1BuildOutcome build_outcome;
    char error[512] = {0};
    uint32_t checks = 0u;

    rsdfa_v1_plan_init(&plan);
    rsdfa_v1_program_init(&program);
    rsdfa_v1_inclusion_result_init(&inclusion);
    rsdfa_v1_functionality_result_init(&functionality);
    rsdfa_v1_scan_result_init(&result);
    rsdfa_v1_parser_lattice_init(&parser_lattice);
    CHECK(rsdfa_v1_plan_build(
              &nfa, 128u, 1024u, &plan, &build_outcome,
              error, sizeof(error)),
          error);
    CHECK(build_outcome == RSDFA_V1_BUILD_COMPLETED,
          "bounded DFA construction did not complete");
    CHECK(plan.state_len > 0u && plan.transition_len > 0u,
          "DFA construction produced no consuming machine");
    checks++;

    CHECK(rsdfa_v1_plan_export_program(
              &plan, &program, error, sizeof(error)) &&
          rsdfa_v1_program_validate(&program, error, sizeof(error)) &&
          program.state_len == plan.state_len &&
          program.transition_len == plan.transition_len &&
          program.tag_len == plan.tag_len,
          error);
    checks++;
    {
        RSDFAV1Plan annotated;
        uint32_t *annotations = NULL;
        uint32_t annotation_len = 0u;
        uint32_t conflicting_annotations[7];
        uint32_t malformed_annotations[7];
        uint32_t index;

        rsdfa_v1_plan_init(&annotated);
        CHECK(rsdfa_v1_plan_build_annotated(
                  &nfa, functional_annotations, UINT32_MAX,
                  128u, 1024u, &annotated, &build_outcome,
                  error, sizeof(error)) &&
              build_outcome == RSDFA_V1_BUILD_COMPLETED &&
              annotated.state_len == plan.state_len &&
              annotated.transition_len == plan.transition_len &&
              rsdfa_v1_plan_export_transition_annotations(
                  &annotated, &annotations, &annotation_len,
                  error, sizeof(error)) &&
              annotation_len == annotated.transition_len,
              error);
        for (index = 0u; index < annotation_len; index++) {
            CHECK(annotations[index] == 7u || annotations[index] == 10u,
                  "annotated DFA invented an edge action");
        }
        free(annotations);
        memcpy(conflicting_annotations, functional_annotations,
               sizeof(conflicting_annotations));
        conflicting_annotations[2] = 8u;
        CHECK(rsdfa_v1_plan_build_annotated(
                  &nfa, conflicting_annotations, UINT32_MAX,
                  128u, 1024u, &annotated, &build_outcome,
                  error, sizeof(error)) &&
              build_outcome == RSDFA_V1_BUILD_ANNOTATION_CONFLICT &&
              annotated.impl == NULL,
              "nonlocal NFA edge actions were determinized locally");
        memcpy(malformed_annotations, functional_annotations,
               sizeof(malformed_annotations));
        malformed_annotations[3] = 7u;
        CHECK(!rsdfa_v1_plan_build_annotated(
                  &nfa, malformed_annotations, UINT32_MAX,
                  128u, 1024u, &annotated, &build_outcome,
                  error, sizeof(error)),
              "zero-width edge retained a consuming annotation");
        rsdfa_v1_plan_free(&annotated);
        checks += 4u;
    }
    {
        static const CettaLpNativeUnicodeRange x_range[] = {{120u, 120u}};
        static const CettaLpNativeUnicodeRange y_range[] = {{121u, 121u}};
        static const uint32_t delayed_start[] = {0u};
        static const RSDFAV1NfaAccept delayed_accept[] = {{5u, 0u}};
        static const RSDFAV1NfaEdge delayed_edges[] = {
            {0u, 1u, RSDFA_V1_NFA_EPSILON, NULL, 0u},
            {0u, 2u, RSDFA_V1_NFA_EPSILON, NULL, 0u},
            {1u, 3u, RSDFA_V1_NFA_RANGES, a_range, 1u},
            {2u, 4u, RSDFA_V1_NFA_RANGES, a_range, 1u},
            {3u, 5u, RSDFA_V1_NFA_RANGES, x_range, 1u},
            {4u, 5u, RSDFA_V1_NFA_RANGES, y_range, 1u},
        };
        static const RSDFAV1NfaEdge ambiguous_edges[] = {
            {0u, 1u, RSDFA_V1_NFA_EPSILON, NULL, 0u},
            {0u, 2u, RSDFA_V1_NFA_EPSILON, NULL, 0u},
            {1u, 3u, RSDFA_V1_NFA_RANGES, a_range, 1u},
            {2u, 4u, RSDFA_V1_NFA_RANGES, a_range, 1u},
            {3u, 5u, RSDFA_V1_NFA_RANGES, x_range, 1u},
            {4u, 5u, RSDFA_V1_NFA_RANGES, x_range, 1u},
        };
        static const uint32_t delayed_annotations[] = {
            UINT32_MAX, UINT32_MAX, 10u, 20u, 30u, 30u,
        };
        RSDFAV1Nfa delayed = {
            .state_len = 6u,
            .start_states = delayed_start,
            .start_len = 1u,
            .edges = delayed_edges,
            .edge_len = 6u,
            .accepts = delayed_accept,
            .accept_len = 1u,
            .tag_len = 1u,
        };
        RSDFAV1Nfa ambiguous = delayed;
        RSDFAV1Plan local;

        ambiguous.edges = ambiguous_edges;
        rsdfa_v1_plan_init(&local);
        CHECK(rsdfa_v1_plan_build_annotated(
                  &delayed, delayed_annotations, UINT32_MAX,
                  32u, 128u, &local, &build_outcome,
                  error, sizeof(error)) &&
              build_outcome == RSDFA_V1_BUILD_ANNOTATION_CONFLICT &&
              rsdfa_v1_annotated_nfa_functionality(
                  &delayed, delayed_annotations, UINT32_MAX, 0u,
                  128u, 4096u, &functionality,
                  error, sizeof(error)) &&
              functionality.outcome ==
                  RSDFA_V1_FUNCTIONALITY_COMPLETED &&
              functionality.functional &&
              functionality.counterexample_len == 0u,
              "future-disambiguated annotations were called nonfunctional");
        CHECK(rsdfa_v1_annotated_nfa_functionality(
                  &ambiguous, delayed_annotations, UINT32_MAX, 0u,
                  128u, 4096u, &functionality,
                  error, sizeof(error)) &&
              functionality.outcome ==
                  RSDFA_V1_FUNCTIONALITY_COMPLETED &&
              !functionality.functional &&
              functionality.counterexample_len == 2u &&
              functionality.counterexample[0] == 97u &&
              functionality.counterexample[1] == 120u &&
              functionality.first_left_annotation == 10u &&
              functionality.first_right_annotation == 20u,
              "ambiguous annotations lacked their shortest counterexample");
        CHECK(rsdfa_v1_annotated_nfa_functionality(
                  &delayed, delayed_annotations, UINT32_MAX, 0u,
                  1u, 4096u, &functionality,
                  error, sizeof(error)) &&
              functionality.outcome ==
                  RSDFA_V1_FUNCTIONALITY_PAIR_LIMIT &&
              !functionality.functional,
              "functionality pair limit was reported as a conclusion");
        CHECK(!rsdfa_v1_annotated_nfa_functionality(
                  &nfa, functional_annotations, UINT32_MAX, 0u,
                  128u, 4096u, &functionality,
                  error, sizeof(error)),
              "EOF-annotated functionality silently changed v1 semantics");
        rsdfa_v1_plan_free(&local);
        checks += 4u;
    }
    {
        uint32_t saved_target = program.transitions[0].target;
        program.transitions[0].target = program.state_len;
        CHECK(!rsdfa_v1_program_validate(
                  &program, error, sizeof(error)),
              "exported DFA program accepted an invalid target");
        program.transitions[0].target = saved_target;
        CHECK(rsdfa_v1_program_validate(
                  &program, error, sizeof(error)),
              "restored DFA program did not validate");
        checks += 2u;
    }

    CHECK(rsdfa_v1_program_tag_inclusion(
              &program, 1u, &program, 0u, 128u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_COMPLETED &&
          inclusion.included && inclusion.counterexample == NULL &&
          inclusion.counterexample_len == 0u &&
          inclusion.explored_pair_len > 0u,
          "single-scalar language was not included in its repetition");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &program, 0u, &program, 1u, 128u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_COMPLETED &&
          !inclusion.included && inclusion.counterexample_len == 2u &&
          inclusion.counterexample[0] == 97u &&
          inclusion.counterexample[1] == 97u,
          "language non-inclusion did not return its shortest witness");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &program, 4u, &program, 0u, 128u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_COMPLETED &&
          !inclusion.included && inclusion.counterexample_len == 1u &&
          inclusion.counterexample[0] == 955u,
          "Unicode non-inclusion witness changed");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &program, 2u, &program, 1u, 128u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_COMPLETED &&
          !inclusion.included && inclusion.counterexample == NULL &&
          inclusion.counterexample_len == 0u,
          "empty-string non-inclusion witness changed");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &program, 3u, &program, 1u, 128u,
              &inclusion, error, sizeof(error)) && inclusion.included &&
          rsdfa_v1_program_tag_inclusion(
              &program, 1u, &program, 3u, 128u,
              &inclusion, error, sizeof(error)) && inclusion.included,
          "EOF-closed and ordinary complete-input languages diverged");
    CHECK(rsdfa_v1_program_tag_inclusion(
              &program, 0u, &program, 1u, 1u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_PAIR_LIMIT &&
          !inclusion.included && inclusion.counterexample == NULL &&
          inclusion.explored_pair_len == 1u,
          "DFA inclusion pair limit was not explicit");
    CHECK(!rsdfa_v1_program_tag_inclusion(
              &program, 5u, &program, 0u, 128u,
              &inclusion, error, sizeof(error)) &&
          inclusion.outcome == RSDFA_V1_INCLUSION_PAIR_LIMIT,
          "out-of-range inclusion tag did not fail atomically");
    checks += 7u;

    {
        RSDFAV1TagLanguage languages[5];
        bool stops = false;
        uint32_t index;

        for (index = 0u; index < 5u; index++) {
            rsdfa_v1_tag_language_init(&languages[index]);
            CHECK(rsdfa_v1_plan_tag_language(
                      &plan, index, &languages[index],
                      error, sizeof(error)),
                  error);
        }
        CHECK(!languages[0].is_empty && !languages[0].accepts_empty &&
                  !languages[0].accepts_eof_empty &&
              !languages[0].prefix_free &&
              !languages[0].one_scalar_or_eof &&
              languages[0].first_range_len == 1u &&
              languages[0].first_ranges[0].low == 97u &&
              languages[0].first_ranges[0].high == 97u,
              "repeating tag language was misclassified");
        CHECK(!languages[1].is_empty && !languages[1].accepts_empty &&
              languages[1].prefix_free &&
              languages[1].one_scalar_or_eof &&
              languages[1].first_range_len == 1u &&
              languages[1].first_ranges[0].low == 97u,
              "single-scalar tag language was misclassified");
        CHECK(!languages[2].is_empty && languages[2].accepts_empty &&
              !languages[2].accepts_eof_empty &&
              languages[2].prefix_free &&
              !languages[2].one_scalar_or_eof &&
              languages[2].first_range_len == 0u,
              "ordinary nullable tag language was misclassified");
        CHECK(!languages[3].is_empty && !languages[3].accepts_empty &&
              !languages[3].accepts_eof_empty &&
              languages[3].prefix_free &&
              !languages[3].one_scalar_or_eof &&
              languages[3].first_range_len == 1u &&
              languages[3].first_ranges[0].low == 97u,
              "consuming EOF-guarded language was treated as lookahead");
        CHECK(!languages[4].is_empty && !languages[4].accepts_empty &&
              languages[4].prefix_free &&
              languages[4].one_scalar_or_eof &&
              languages[4].first_range_len == 1u &&
              languages[4].first_ranges[0].low == 955u,
              "Unicode single-scalar language was misclassified");
        CHECK(rsdfa_v1_plan_tag_stops_before(
                  &plan, 0u, a_range, 1u, &stops,
                  error, sizeof(error)) && !stops,
              "repeating tag was falsely certified boundary-stopped");
        CHECK(rsdfa_v1_plan_tag_stops_before(
                  &plan, 0u, lambda_range, 1u, &stops,
                  error, sizeof(error)) && stops,
              "disjoint boundary was not certified stopping");
        CHECK(rsdfa_v1_plan_tag_stops_before(
                  &plan, 1u, a_range, 1u, &stops,
                  error, sizeof(error)) && stops,
              "dead transitions from other tags polluted local stopping");
        CHECK(!rsdfa_v1_plan_tag_language(
                  &plan, 5u, &languages[0], error, sizeof(error)),
              "out-of-range tag language did not fail closed");
        for (index = 0u; index < 5u; index++)
            rsdfa_v1_tag_language_free(&languages[index]);
        checks += 9u;
    }

    {
        static const uint32_t empty_starts[] = {0u};
        static const RSDFAV1NfaAccept unreachable_accept[] = {{1u, 0u}};
        const RSDFAV1Nfa empty_nfa = {
            .state_len = 2u,
            .start_states = empty_starts,
            .start_len = 1u,
            .edges = NULL,
            .edge_len = 0u,
            .accepts = unreachable_accept,
            .accept_len = 1u,
            .tag_len = 1u,
        };
        RSDFAV1Plan empty_plan;
        RSDFAV1TagLanguage empty_language;

        rsdfa_v1_plan_init(&empty_plan);
        rsdfa_v1_tag_language_init(&empty_language);
        CHECK(rsdfa_v1_plan_build(
                  &empty_nfa, 8u, 8u, &empty_plan, &build_outcome,
                  error, sizeof(error)) &&
              build_outcome == RSDFA_V1_BUILD_COMPLETED &&
              rsdfa_v1_plan_tag_language(
                  &empty_plan, 0u, &empty_language,
                  error, sizeof(error)) &&
              empty_language.is_empty &&
              !empty_language.accepts_empty &&
              !empty_language.accepts_eof_empty &&
              empty_language.first_range_len == 0u,
              "unreachable tag language was not reported as empty");
        rsdfa_v1_tag_language_free(&empty_language);
        rsdfa_v1_plan_free(&empty_plan);
        checks++;
    }

    CHECK(rsdfa_v1_scan(
              &plan, input, sizeof(input), 10000u, 1000u,
              &result, error, sizeof(error)),
          error);
    CHECK(result.outcome == RSDFA_V1_SCAN_COMPLETED,
          "Unicode DFA scan did not complete");
    CHECK(result.input_byte_len == 5u &&
          result.input_scalar_len == 4u &&
          result.decoded_byte_len == 5u &&
          result.tag_len == 5u &&
          result.source_pass_count == 1u,
          "Unicode DFA scan accounting changed");
    CHECK(result.codepoints[0] == 97u &&
          result.codepoints[1] == 97u &&
          result.codepoints[2] == 955u &&
          result.codepoints[3] == 97u &&
          result.byte_offsets[0] == 0u &&
          result.byte_offsets[1] == 1u &&
          result.byte_offsets[2] == 2u &&
          result.byte_offsets[3] == 4u &&
          result.byte_offsets[4] == 5u,
          "DFA did not retain its single decoded source index");
    CHECK(result.token_len == 14u,
          "tagged token lattice lost or invented spans");
    CHECK(has_token(&result, 0u, 0u, 1u, 0u, 1u) &&
          has_token(&result, 1u, 0u, 1u, 0u, 1u),
          "ambiguous tags at one span were narrowed");
    CHECK(has_token(&result, 0u, 0u, 2u, 0u, 2u),
          "repeated regular span was not retained");
    CHECK(has_token(&result, 2u, 2u, 2u, 2u, 2u),
          "nullable span was not retained");
    CHECK(has_token(&result, 4u, 2u, 3u, 2u, 4u),
          "Unicode scalar and byte spans diverged");
    CHECK(has_token(&result, 3u, 3u, 4u, 4u, 5u),
          "EOF-tagged span was not retained");
    checks += 8u;

    {
        static const uint32_t codepoints[] = {97u, 97u, 955u, 97u};
        static const uint32_t byte_offsets[] = {0u, 1u, 2u, 4u, 5u};
        static const uint32_t corrupt_offsets[] = {0u, 1u, 3u, 4u, 5u};
        static const uint32_t tag_terminal_ids[] = {
            50u, 40u, 30u, 20u, 10u,
        };
        const CettaLpNativeUtf8ScalarView view = {
            codepoints, byte_offsets, 4u, 5u, 0u, 0u,
        };
        CettaLpNativeUtf8ScalarView corrupt_view;
        RSDFAV1ScanResult view_result;
        RSDFAV1ParserLattice view_lattice;
        RSDFAV1Token cursor_tokens[5];
        RSDFAV1Token program_tokens[5];
        RSDFAV1CursorScanResult cursor_result;
        RSDFAV1CursorScanResult program_result;
        uint32_t cursor;

        rsdfa_v1_scan_result_init(&view_result);
        rsdfa_v1_parser_lattice_init(&view_lattice);
        CHECK(rsdfa_v1_scan_scalar_view(
                  &plan, &view, 10000u, 1000u,
                  &view_result, error, sizeof(error)),
              error);
        CHECK(view_result.outcome == RSDFA_V1_SCAN_COMPLETED &&
              view_result.decoded_byte_len == 0u &&
              view_result.source_pass_count == 0u,
              "borrowed DFA scan performed source work");
        CHECK(same_recognition(&result, &view_result),
              "borrowed DFA scan changed the tagged span relation");
        for (cursor = 0u; cursor <= view.scalar_len; cursor++) {
            CHECK(rsdfa_v1_scan_cursor_longest_prevalidated(
                      &plan, &view, cursor, 100u,
                      cursor_tokens, 5u, &cursor_result,
                      error, sizeof(error)),
                  error);
            CHECK(cursor_is_longest_projection(
                      &view_result, cursor,
                      cursor_tokens, &cursor_result),
                  "cursor scan changed the longest recognition projection");
            CHECK(rsdfa_v1_program_scan_cursor_longest_prevalidated(
                      &program, &view, cursor, 100u,
                      program_tokens, 5u, &program_result,
                      error, sizeof(error)) &&
                  program_result.outcome == cursor_result.outcome &&
                  program_result.start_scalar == cursor_result.start_scalar &&
                  program_result.stop_scalar == cursor_result.stop_scalar &&
                  program_result.start_byte == cursor_result.start_byte &&
                  program_result.stop_byte == cursor_result.stop_byte &&
                  program_result.accept_len == cursor_result.accept_len &&
                  program_result.reached_eof == cursor_result.reached_eof &&
                  program_result.work_item_len ==
                      cursor_result.work_item_len,
                  "exported DFA program changed its cursor receipt");
            for (uint32_t token = 0u;
                 token < cursor_result.accept_len; token++) {
                CHECK(same_token(
                          &cursor_tokens[token], &program_tokens[token]),
                      "exported DFA program changed a cursor token");
            }
            checks++;
        }
        CHECK(rsdfa_v1_scan_cursor_longest(
                  &plan, &view, 0u, 100u,
                  cursor_tokens, 5u, &cursor_result,
                  error, sizeof(error)) &&
              cursor_is_longest_projection(
                  &view_result, 0u,
                  cursor_tokens, &cursor_result),
              "checked cursor scan differs from its prevalidated form");
        checks++;
        CHECK(rsdfa_v1_program_scan_cursor_longest(
                  &program, &view, 0u, 100u,
                  program_tokens, 5u, &program_result,
                  error, sizeof(error)) &&
              cursor_is_longest_projection(
                  &view_result, 0u, program_tokens, &program_result),
              "checked exported DFA program changed recognition");
        checks++;
        CHECK(rsdfa_v1_parser_lattice_build(
                  &view_result, tag_terminal_ids, 5u,
                  &view_lattice, error, sizeof(error)),
              error);
        CHECK(view_lattice.lattice.source_pass_count == 0u &&
              view_lattice.lattice.decoded_byte_len == 0u &&
              cetta_lp_native_utf8_lattice_validate(
                  &view_lattice.lattice, error, sizeof(error)),
              "borrowed DFA lattice lost its zero-pass receipt");

        corrupt_view = view;
        corrupt_view.decoded_byte_len = 5u;
        CHECK(!rsdfa_v1_scan_scalar_view(
                  &plan, &corrupt_view, 10000u, 1000u,
                  &view_result, error, sizeof(error)),
              "DFA accepted a zero-pass view claiming decoded bytes");
        CHECK(!rsdfa_v1_scan_cursor_longest(
                  &plan, &corrupt_view, 0u, 100u,
                  cursor_tokens, 5u, &cursor_result,
                  error, sizeof(error)),
              "cursor DFA accepted a corrupt decode receipt");
        corrupt_view = view;
        corrupt_view.source_pass_count = 2u;
        CHECK(!rsdfa_v1_scan_scalar_view(
                  &plan, &corrupt_view, 10000u, 1000u,
                  &view_result, error, sizeof(error)),
              "DFA accepted a multi-pass scalar-view receipt");
        corrupt_view = view;
        corrupt_view.byte_offsets = corrupt_offsets;
        CHECK(!rsdfa_v1_scan_scalar_view(
                  &plan, &corrupt_view, 10000u, 1000u,
                  &view_result, error, sizeof(error)),
              "DFA accepted a corrupt scalar-view byte index");
        checks += 9u;
        rsdfa_v1_parser_lattice_free(&view_lattice);
        rsdfa_v1_scan_result_free(&view_result);
    }

    {
        uint32_t long_codepoints[1024];
        uint32_t long_offsets[1025];
        CettaLpNativeUtf8ScalarView long_view;
        RSDFAV1Token cursor_tokens[5];
        RSDFAV1CursorScanResult cursor_result;
        uint32_t index;

        for (index = 0u; index < 1024u; index++) {
            long_codepoints[index] = 97u;
            long_offsets[index] = index;
        }
        long_offsets[1024] = 1024u;
        long_view = (CettaLpNativeUtf8ScalarView){
            long_codepoints, long_offsets, 1024u, 1024u, 0u, 0u,
        };
        CHECK(rsdfa_v1_scan_cursor_longest(
                  &plan, &long_view, 0u, 1024u,
                  cursor_tokens, 5u, &cursor_result,
                  error, sizeof(error)),
              error);
        CHECK(cursor_result.outcome ==
                  RSDFA_V1_CURSOR_SCAN_COMPLETED &&
              cursor_result.reached_eof &&
              cursor_result.stop_scalar == 1024u &&
              cursor_result.stop_byte == 1024u &&
              cursor_result.work_item_len == 1024u &&
              cursor_result.accept_len == 3u &&
              cursor_tokens[0].tag == 0u &&
              cursor_tokens[0].end_scalar == 1024u &&
              cursor_tokens[1].tag == 1u &&
              cursor_tokens[1].end_scalar == 1u &&
              cursor_tokens[2].tag == 2u &&
              cursor_tokens[2].end_scalar == 0u,
              "one-cursor DFA did not retain bounded longest accepts");
        CHECK(rsdfa_v1_scan_cursor_longest_prevalidated(
                  &plan, &long_view, 0u, 1u,
                  cursor_tokens, 5u, &cursor_result,
                  error, sizeof(error)) &&
              cursor_result.outcome ==
                  RSDFA_V1_CURSOR_SCAN_WORK_LIMIT &&
              !cursor_result.reached_eof &&
              cursor_result.stop_scalar == 1u &&
              cursor_result.work_item_len == 1u,
              "one-cursor DFA work limit is not explicit");
        CHECK(!rsdfa_v1_scan_cursor_longest_prevalidated(
                  &plan, &long_view, 0u, 1024u,
                  cursor_tokens, 4u, &cursor_result,
                  error, sizeof(error)) &&
              !rsdfa_v1_scan_cursor_longest_prevalidated(
                  &plan, &long_view, 1025u, 1024u,
                  cursor_tokens, 5u, &cursor_result,
                  error, sizeof(error)),
              "one-cursor DFA accepted an undersized buffer or bad cursor");
        checks += 4u;
    }

    {
        static const uint32_t tag_terminal_ids[] = {
            50u, 40u, 30u, 20u, 10u,
        };
        uint32_t witness_one = token_witness(&result, 0u, 0u, 1u);
        uint32_t witness_two = token_witness(&result, 0u, 0u, 2u);
        CettaLpNativeGrammar grammar;
        CettaLpNativeUtf8Forest forest;
        uint32_t edge_index;

        CHECK(rsdfa_v1_parser_lattice_build(
                  &result, tag_terminal_ids, 5u,
                  &parser_lattice, error, sizeof(error)),
              error);
        CHECK(parser_lattice.lattice.terminal_len == 5u &&
              parser_lattice.lattice.edge_len == result.token_len &&
              parser_lattice.lattice.source_pass_count == 1u &&
              parser_lattice.lattice.codepoints == result.codepoints &&
              parser_lattice.lattice.byte_offsets == result.byte_offsets &&
              parser_lattice.lattice.terminal_ids[0] == 10u &&
              parser_lattice.lattice.terminal_ids[4] == 50u,
              "DFA parser lattice changed ownership or terminal identity");
        for (edge_index = 0u;
             edge_index < parser_lattice.lattice.edge_len; edge_index++) {
            const CettaLpNativeUtf8LatticeEdge *edge =
                &parser_lattice.lattice.edges[edge_index];
            const RSDFAV1Token *token;
            CHECK(edge->value < result.token_len,
                  "DFA parser witness is outside the scan result");
            token = &result.tokens[edge->value];
            CHECK(edge->value_kind ==
                      CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
                  edge->terminal_id == tag_terminal_ids[token->tag] &&
                  edge->scalar_left == token->start_scalar &&
                  edge->scalar_right == token->end_scalar,
                  "DFA parser edge lost its recognition witness");
        }
        CHECK(witness_one != UINT32_MAX && witness_two != UINT32_MAX,
              "DFA witness lookup failed");

        cetta_lp_native_grammar_init(&grammar);
        grammar.productions = calloc(1u, sizeof(*grammar.productions));
        CHECK(grammar.productions != NULL,
              "failed to allocate lattice parser grammar");
        grammar.production_len = 1u;
        grammar.productions[0].label = 0u;
        grammar.productions[0].lhs = 77u;
        grammar.productions[0].rhs =
            calloc(1u, sizeof(*grammar.productions[0].rhs));
        CHECK(grammar.productions[0].rhs != NULL,
              "failed to allocate lattice parser production");
        grammar.productions[0].rhs_len = 1u;
        grammar.productions[0].rhs[0] = (CettaLpNativeSymbol){
            CETTA_LP_NATIVE_SYMBOL_TM, 50u, 0u,
        };

        cetta_lp_native_utf8_forest_init(&forest);
        CHECK(cetta_lp_native_gll_parse_utf8_lattice_forest(
                  &grammar, 77u, &parser_lattice.lattice, 10000u,
                  &forest, error, sizeof(error)),
              error);
        CHECK(forest.root_len == 2u &&
              forest_has_root_right(&forest, 1u) &&
              forest_has_root_right(&forest, 2u) &&
              forest_has_witness(&forest, 50u, 1u, witness_one) &&
              forest_has_witness(&forest, 50u, 2u, witness_two),
              "GLL did not consume the exact DFA witness relation");
        cetta_lp_native_utf8_forest_free(&forest);

        CHECK(cetta_lp_native_glr_parse_utf8_lattice_forest(
                  &grammar, 77u, &parser_lattice.lattice, 10000u,
                  &forest, error, sizeof(error)),
              error);
        CHECK(forest.root_len == 2u &&
              forest_has_root_right(&forest, 1u) &&
              forest_has_root_right(&forest, 2u) &&
              forest_has_witness(&forest, 50u, 1u, witness_one) &&
              forest_has_witness(&forest, 50u, 2u, witness_two),
              "GLR did not consume the exact DFA witness relation");
        cetta_lp_native_utf8_forest_free(&forest);

        CHECK(!rsdfa_v1_parser_lattice_build(
                  &result, tag_terminal_ids, 4u,
                  &parser_lattice, error, sizeof(error)),
              "mismatched tag mapping did not fail closed");
        rsdfa_v1_parser_lattice_free(&parser_lattice);

        {
            static const CettaLpNativeUnicodeRange a_ranges[] = {
                {97u, 97u},
            };
            CettaLpNativeUtf8Terminal base_terminals[] = {
                {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR,
                 955u, NULL, 0u},
                {1u, CETTA_LP_NATIVE_UTF8_TERMINAL_EOF,
                 0u, NULL, 0u},
                {2u, CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES,
                 0u, a_ranges, 1u},
                {3u, CETTA_LP_NATIVE_UTF8_TERMINAL_ANY,
                 0u, NULL, 0u},
            };
            CettaLpNativeSymbol *mixed_rhs;
            RSDFAV1ParserLattice slice;
            RSDFAV1ParserLattice prevalidated_slice;
            uint32_t inner_witness =
                token_witness(&result, 0u, 1u, 2u);
            bool inner_witness_found = false;
            bool invented_eof = false;
            bool true_eof_found = false;

            rsdfa_v1_parser_lattice_init(&slice);
            rsdfa_v1_parser_lattice_init(&prevalidated_slice);

            CHECK(rsdfa_v1_composite_parser_lattice_build(
                      &result, base_terminals, 4u,
                      tag_terminal_ids, 5u,
                      &parser_lattice, error, sizeof(error)),
                  error);
            CHECK(parser_lattice.lattice.terminal_len == 9u &&
                  parser_lattice.lattice.edge_len ==
                      result.token_len + 9u &&
                  parser_lattice.lattice.source_pass_count == 1u,
                  "composite lattice lost scalar or span edges");
            CHECK(inner_witness != UINT32_MAX &&
                  rsdfa_v1_parser_lattice_slice(
                      &parser_lattice.lattice, 1u, 3u,
                      &slice, error, sizeof(error)),
                  error);
            CHECK(cetta_lp_native_utf8_lattice_validate(
                      &parser_lattice.lattice, error, sizeof(error)) &&
                  rsdfa_v1_parser_lattice_slice_prevalidated(
                      &parser_lattice.lattice, 1u, 3u,
                      &prevalidated_slice, error, sizeof(error)) &&
                  same_parser_lattice(&slice, &prevalidated_slice),
                  "prevalidated lattice slice changed normalization");
            rsdfa_v1_parser_lattice_free(&prevalidated_slice);
            CHECK(slice.lattice.scalar_len == 2u &&
                  slice.lattice.input_byte_len == 3u &&
                  slice.lattice.decoded_byte_len == 0u &&
                  slice.lattice.source_pass_count == 0u &&
                  slice.lattice.codepoints[0] == 97u &&
                  slice.lattice.codepoints[1] == 955u &&
                  slice.lattice.byte_offsets ==
                      slice.owned_byte_offsets &&
                  slice.lattice.byte_offsets[0] == 0u &&
                  slice.lattice.byte_offsets[1] == 1u &&
                  slice.lattice.byte_offsets[2] == 3u,
                  "parser lattice slice did not normalize its carrier");
            for (edge_index = 0u;
                 edge_index < slice.lattice.edge_len; edge_index++) {
                const CettaLpNativeUtf8LatticeEdge *slice_edge =
                    &slice.lattice.edges[edge_index];
                if (slice_edge->terminal_id == 50u &&
                    slice_edge->scalar_left == 0u &&
                    slice_edge->scalar_right == 1u &&
                    slice_edge->value_kind ==
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
                    slice_edge->value == inner_witness) {
                    inner_witness_found = true;
                }
                if (slice_edge->terminal_id == 1u &&
                    slice_edge->value_kind ==
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF) {
                    invented_eof = true;
                }
            }
            CHECK(inner_witness_found && !invented_eof,
                  "parser lattice slice lost a witness or invented EOF");
            CHECK(rsdfa_v1_parser_lattice_slice(
                      &parser_lattice.lattice, 3u, 4u,
                      &slice, error, sizeof(error)),
                  error);
            for (edge_index = 0u;
                 edge_index < slice.lattice.edge_len; edge_index++) {
                const CettaLpNativeUtf8LatticeEdge *slice_edge =
                    &slice.lattice.edges[edge_index];
                if (slice_edge->terminal_id == 1u &&
                    slice_edge->scalar_left == 1u &&
                    slice_edge->scalar_right == 1u &&
                    slice_edge->value_kind ==
                        CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF) {
                    true_eof_found = true;
                }
            }
            CHECK(true_eof_found,
                  "parser lattice slice erased a true source EOF");
            CHECK(!rsdfa_v1_parser_lattice_slice(
                      &parser_lattice.lattice, 3u, 2u,
                      &slice, error, sizeof(error)),
                  "reversed parser lattice slice did not fail closed");
            {
                uint32_t saved_offset =
                    parser_lattice.lattice.start_offsets[1u];
                parser_lattice.start_offsets[1u] =
                    parser_lattice.lattice.edge_len + 1u;
                CHECK(!cetta_lp_native_utf8_lattice_validate(
                          &parser_lattice.lattice, error, sizeof(error)) &&
                      !rsdfa_v1_parser_lattice_slice(
                          &parser_lattice.lattice, 1u, 3u,
                          &slice, error, sizeof(error)),
                      "unchecked lattice slice did not reject corruption");
                parser_lattice.start_offsets[1u] = saved_offset;
            }
            rsdfa_v1_parser_lattice_free(&slice);
            mixed_rhs = realloc(
                grammar.productions[0].rhs,
                2u * sizeof(*grammar.productions[0].rhs));
            CHECK(mixed_rhs != NULL,
                  "failed to grow mixed lattice production");
            grammar.productions[0].rhs = mixed_rhs;
            grammar.productions[0].rhs_len = 2u;
            grammar.productions[0].rhs[1] = (CettaLpNativeSymbol){
                CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u,
            };

            CHECK(cetta_lp_native_gll_parse_utf8_lattice_forest(
                      &grammar, 77u, &parser_lattice.lattice, 10000u,
                      &forest, error, sizeof(error)),
                  error);
            CHECK(forest.root_len == 1u &&
                  forest_has_root_right(&forest, 3u) &&
                  forest_has_witness(
                      &forest, 50u, 2u, witness_two) &&
                  forest_has_scalar(&forest, 0u, 2u, 3u, 955u),
                  "GLL did not compose span and scalar lattice edges");
            cetta_lp_native_utf8_forest_free(&forest);

            CHECK(cetta_lp_native_glr_parse_utf8_lattice_forest(
                      &grammar, 77u, &parser_lattice.lattice, 10000u,
                      &forest, error, sizeof(error)),
                  error);
            CHECK(forest.root_len == 1u &&
                  forest_has_root_right(&forest, 3u) &&
                  forest_has_witness(
                      &forest, 50u, 2u, witness_two) &&
                  forest_has_scalar(&forest, 0u, 2u, 3u, 955u),
                  "GLR did not compose span and scalar lattice edges");
            cetta_lp_native_utf8_forest_free(&forest);

            base_terminals[0].terminal_id = 50u;
            CHECK(!rsdfa_v1_composite_parser_lattice_build(
                      &result, base_terminals, 4u,
                      tag_terminal_ids, 5u,
                      &parser_lattice, error, sizeof(error)),
                  "span/scalar terminal collision did not fail closed");
            rsdfa_v1_parser_lattice_free(&parser_lattice);
            checks += 15u;
        }
        cetta_lp_native_grammar_free(&grammar);
        checks += 6u + 2u * result.token_len;
    }

    CHECK(rsdfa_v1_scan(
              &plan, input, sizeof(input), 1u, 1000u,
              &result, error, sizeof(error)),
          "work-limit scan failed structurally");
    CHECK(result.outcome == RSDFA_V1_SCAN_WORK_LIMIT,
          "DFA work limit is not explicit");
    checks++;

    CHECK(rsdfa_v1_scan(
              &plan, input, sizeof(input), 10000u, 1u,
              &result, error, sizeof(error)),
          "token-limit scan failed structurally");
    CHECK(result.outcome == RSDFA_V1_SCAN_TOKEN_LIMIT,
          "DFA token limit is not explicit");
    checks++;

    CHECK(!rsdfa_v1_scan(
              &plan, invalid_utf8, sizeof(invalid_utf8), 100u, 100u,
              &result, error, sizeof(error)),
          "invalid UTF-8 did not fail closed");
    checks++;

    {
        RSDFAV1Plan limited;
        rsdfa_v1_plan_init(&limited);
        CHECK(rsdfa_v1_plan_build(
                  &nfa, 1u, 1024u, &limited, &build_outcome,
                  error, sizeof(error)),
              "DFA state limit failed structurally");
        CHECK(build_outcome == RSDFA_V1_BUILD_STATE_LIMIT,
              "DFA state limit is not explicit");
        CHECK(limited.impl == NULL,
              "resource-limited DFA escaped as a partial plan");
        rsdfa_v1_plan_free(&limited);
        checks++;
    }

    {
        static const CettaLpNativeUnicodeRange surrogate_range[] = {
            {0xd800u, 0xd800u},
        };
        RSDFAV1NfaEdge invalid_edge = {
            0u, 1u, RSDFA_V1_NFA_RANGES, surrogate_range, 1u,
        };
        RSDFAV1Nfa invalid_nfa = nfa;
        RSDFAV1Plan invalid_plan;
        invalid_nfa.edges = &invalid_edge;
        invalid_nfa.edge_len = 1u;
        rsdfa_v1_plan_init(&invalid_plan);
        CHECK(!rsdfa_v1_plan_build(
                  &invalid_nfa, 128u, 1024u,
                  &invalid_plan, &build_outcome,
                  error, sizeof(error)),
              "surrogate NFA range did not fail closed");
        rsdfa_v1_plan_free(&invalid_plan);
        checks++;
    }

    rsdfa_v1_scan_result_free(&result);
    rsdfa_v1_parser_lattice_free(&parser_lattice);
    rsdfa_v1_inclusion_result_free(&inclusion);
    rsdfa_v1_functionality_result_free(&functionality);
    rsdfa_v1_program_free(&program);
    rsdfa_v1_plan_free(&plan);
    printf("(RegularSpanDFAV1NativeSummary %u 0)\n", checks);
    return 0;
}
