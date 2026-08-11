#include "lib_parse_native_grammar.h"

#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

static bool expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
    return false;
}

static bool forest_node_equal(
    const CettaLpNativeUtf8ForestNode *lhs,
    const CettaLpNativeUtf8ForestNode *rhs) {
    return lhs->kind == rhs->kind &&
        lhs->symbol_id == rhs->symbol_id &&
        lhs->production_index == rhs->production_index &&
        lhs->dot == rhs->dot &&
        lhs->scalar_left == rhs->scalar_left &&
        lhs->scalar_right == rhs->scalar_right &&
        lhs->byte_left == rhs->byte_left &&
        lhs->byte_right == rhs->byte_right &&
        lhs->terminal_is_eof == rhs->terminal_is_eof &&
        lhs->terminal_scalar == rhs->terminal_scalar &&
        lhs->terminal_value_kind == rhs->terminal_value_kind &&
        lhs->terminal_witness_id == rhs->terminal_witness_id &&
        lhs->choice_begin == rhs->choice_begin &&
        lhs->choice_len == rhs->choice_len;
}

static bool forest_equal(const CettaLpNativeUtf8Forest *lhs,
                         const CettaLpNativeUtf8Forest *rhs) {
    uint32_t index;

    if (!lhs || !rhs || lhs->outcome != rhs->outcome ||
        lhs->node_len != rhs->node_len ||
        lhs->choice_len != rhs->choice_len ||
        lhs->root_len != rhs->root_len ||
        lhs->expected_terminal_len != rhs->expected_terminal_len ||
        lhs->scalar_len != rhs->scalar_len ||
        lhs->input_byte_len != rhs->input_byte_len ||
        lhs->farthest_scalar != rhs->farthest_scalar ||
        lhs->farthest_byte != rhs->farthest_byte ||
        lhs->graph_node_len != rhs->graph_node_len ||
        lhs->stack_node_len != rhs->stack_node_len ||
        lhs->work_item_len != rhs->work_item_len ||
        lhs->decoded_byte_len != rhs->decoded_byte_len ||
        lhs->source_pass_count != rhs->source_pass_count ||
        !lhs->byte_offsets || !rhs->byte_offsets) {
        return false;
    }
    for (index = 0u; index < lhs->node_len; index++) {
        if (!forest_node_equal(&lhs->nodes[index], &rhs->nodes[index]))
            return false;
    }
    if ((lhs->choice_len > 0u &&
         memcmp(lhs->choices, rhs->choices,
                sizeof(*lhs->choices) * lhs->choice_len) != 0) ||
        (lhs->root_len > 0u &&
         memcmp(lhs->roots, rhs->roots,
                sizeof(*lhs->roots) * lhs->root_len) != 0) ||
        (lhs->expected_terminal_len > 0u &&
         memcmp(lhs->expected_terminal_ids, rhs->expected_terminal_ids,
                sizeof(*lhs->expected_terminal_ids) *
                    lhs->expected_terminal_len) != 0) ||
        (lhs->scalar_len > 0u &&
         memcmp(lhs->codepoints, rhs->codepoints,
                sizeof(*lhs->codepoints) * lhs->scalar_len) != 0) ||
        memcmp(lhs->byte_offsets, rhs->byte_offsets,
               sizeof(*lhs->byte_offsets) * (lhs->scalar_len + 1u)) != 0) {
        return false;
    }
    return true;
}

static bool grammar_build(CettaLpNativeGrammar *grammar,
                          uint32_t production_len,
                          const uint32_t *lhs,
                          const uint32_t *rhs_offsets,
                          const CettaLpNativeSymbol *rhs) {
    uint32_t index;

    cetta_lp_native_grammar_init(grammar);
    grammar->productions = calloc(production_len, sizeof(*grammar->productions));
    if (!grammar->productions)
        return false;
    grammar->production_len = production_len;
    for (index = 0u; index < production_len; index++) {
        uint32_t begin = rhs_offsets[index];
        uint32_t end = rhs_offsets[index + 1u];
        uint32_t len = end - begin;
        grammar->productions[index].label = index;
        grammar->productions[index].lhs = lhs[index];
        grammar->productions[index].rhs_len = len;
        if (len > 0u) {
            grammar->productions[index].rhs =
                malloc(sizeof(*grammar->productions[index].rhs) * len);
            if (!grammar->productions[index].rhs)
                return false;
            memcpy(grammar->productions[index].rhs, rhs + begin,
                   sizeof(*grammar->productions[index].rhs) * len);
        }
    }
    return true;
}

static uint32_t forest_kind_count(const CettaLpNativeUtf8Forest *forest,
                                  CettaLpNativeUtf8ForestNodeKind kind) {
    uint32_t count = 0u;
    uint32_t index;
    for (index = 0u; index < forest->node_len; index++) {
        if (forest->nodes[index].kind == kind)
            count++;
    }
    return count;
}

static bool forest_has_terminal(const CettaLpNativeUtf8Forest *forest,
                                uint32_t terminal_id,
                                uint32_t scalar_left,
                                uint32_t scalar_right,
                                uint32_t byte_left,
                                uint32_t byte_right,
                                bool is_eof) {
    uint32_t index;
    for (index = 0u; index < forest->node_len; index++) {
        const CettaLpNativeUtf8ForestNode *node = &forest->nodes[index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM &&
            node->symbol_id == terminal_id &&
            node->scalar_left == scalar_left &&
            node->scalar_right == scalar_right &&
            node->byte_left == byte_left &&
            node->byte_right == byte_right &&
            node->terminal_is_eof == is_eof) {
            return true;
        }
    }
    return false;
}

static bool root_has_extent(const CettaLpNativeUtf8Forest *forest,
                            uint32_t left,
                            uint32_t right) {
    uint32_t index;
    for (index = 0u; index < forest->root_len; index++) {
        uint32_t node_idx = forest->roots[index];
        if (node_idx < forest->node_len &&
            forest->nodes[node_idx].scalar_left == left &&
            forest->nodes[node_idx].scalar_right == right) {
            return true;
        }
    }
    return false;
}

static void test_owned_scalar_buffer(TestCounts *counts) {
    static const uint8_t valid[] = {'a', 0u, 0xceu, 0xbbu};
    static const uint8_t ascii[] = {'a', 0u, '!'};
    static const uint8_t invalid[] = {0xc0u, 0xafu};
    CettaLpNativeUtf8ScalarBuffer buffer;
    CettaLpNativeUtf8ScalarView corrupt_ascii;
    const uint32_t *saved_codepoints;
    const uint32_t *saved_offsets;
    char error[256] = {0};

    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    (void)expect(
        counts,
        cetta_lp_native_utf8_scalar_buffer_decode(
            &buffer, valid, sizeof(valid), error, sizeof(error)) &&
        buffer.view.codepoints == buffer.codepoints &&
        buffer.view.byte_offsets == buffer.byte_offsets &&
        buffer.view.scalar_len == 3u &&
        buffer.view.input_byte_len == 4u &&
        buffer.view.decoded_byte_len == 4u &&
        buffer.view.source_pass_count == 1u &&
        buffer.codepoints[0] == (uint32_t)'a' &&
        buffer.codepoints[1] == 0u &&
        buffer.codepoints[2] == UINT32_C(0x03bb) &&
        buffer.byte_offsets[0] == 0u &&
        buffer.byte_offsets[1] == 1u &&
        buffer.byte_offsets[2] == 2u &&
        buffer.byte_offsets[3] == 4u,
        error[0] ? error : "owned scalar buffer decodes one source pass");
    saved_codepoints = buffer.codepoints;
    saved_offsets = buffer.byte_offsets;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_utf8_scalar_buffer_decode(
            &buffer, invalid, sizeof(invalid), error, sizeof(error)) &&
        strstr(error, "invalid UTF-8") != NULL &&
        buffer.codepoints == saved_codepoints &&
        buffer.byte_offsets == saved_offsets &&
        buffer.view.scalar_len == 3u,
        "failed scalar decode preserves the previous owner");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, ascii, sizeof(ascii), error, sizeof(error)) &&
        !buffer.codepoints && !buffer.byte_offsets &&
        buffer.view.ascii_bytes == ascii &&
        buffer.view.scalar_len == sizeof(ascii) &&
        buffer.view.input_byte_len == sizeof(ascii) &&
        buffer.view.decoded_byte_len == sizeof(ascii) &&
        buffer.view.source_pass_count == 1u &&
        cetta_lp_native_utf8_scalar_view_scalar_at(&buffer.view, 0u) ==
            (uint32_t)'a' &&
        cetta_lp_native_utf8_scalar_view_scalar_at(&buffer.view, 1u) == 0u &&
        cetta_lp_native_utf8_scalar_view_byte_offset(&buffer.view, 3u) == 3u &&
        cetta_lp_native_utf8_scalar_view_validate(
            &buffer.view, error, sizeof(error)),
        error[0] ? error : "ASCII scalar source uses an identity view");
    corrupt_ascii = buffer.view;
    corrupt_ascii.ascii_bytes = invalid;
    corrupt_ascii.scalar_len = 2u;
    corrupt_ascii.input_byte_len = 2u;
    corrupt_ascii.decoded_byte_len = 2u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_utf8_scalar_view_validate(
            &corrupt_ascii, error, sizeof(error)) &&
        strstr(error, "non-ASCII") != NULL,
        "ASCII identity view rejects a high-bit byte");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, valid, sizeof(valid), error, sizeof(error)) &&
        buffer.codepoints && buffer.byte_offsets &&
        !buffer.view.ascii_bytes && buffer.view.scalar_len == 3u &&
        buffer.codepoints[2] == UINT32_C(0x03bb),
        error[0] ? error : "non-ASCII scalar source uses validated decode");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_scalar_buffer_decode(
            &buffer, NULL, 0u, error, sizeof(error)) &&
        buffer.view.scalar_len == 0u &&
        buffer.view.input_byte_len == 0u &&
        buffer.view.decoded_byte_len == 0u &&
        buffer.view.source_pass_count == 1u &&
        buffer.byte_offsets && buffer.byte_offsets[0] == 0u,
        error[0] ? error : "owned scalar buffer replaces with empty input");
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
}

static int32_t root_with_right(const CettaLpNativeUtf8Forest *forest,
                               uint32_t right) {
    uint32_t index;
    for (index = 0u; index < forest->root_len; index++) {
        uint32_t node_index = forest->roots[index];
        if (node_index < forest->node_len &&
            forest->nodes[node_index].scalar_left == 0u &&
            forest->nodes[node_index].scalar_right == right) {
            return (int32_t)node_index;
        }
    }
    return -1;
}

static bool forest_has_witness(const CettaLpNativeUtf8Forest *forest,
                               uint32_t terminal_id,
                               uint32_t scalar_left,
                               uint32_t scalar_right,
                               uint32_t byte_left,
                               uint32_t byte_right,
                               uint32_t witness_id) {
    uint32_t index;
    for (index = 0u; index < forest->node_len; index++) {
        const CettaLpNativeUtf8ForestNode *node = &forest->nodes[index];
        if (node->kind == CETTA_LP_NATIVE_UTF8_FOREST_TERM &&
            node->symbol_id == terminal_id &&
            node->scalar_left == scalar_left &&
            node->scalar_right == scalar_right &&
            node->byte_left == byte_left &&
            node->byte_right == byte_right &&
            node->terminal_value_kind ==
                CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS &&
            node->terminal_witness_id == witness_id) {
            return true;
        }
    }
    return false;
}

static void test_unicode_single_pass(TestCounts *counts) {
    enum { STATE = 7u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 3u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 1u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 2u, 0u},
    };
    static const CettaLpNativeUnicodeRange lambda_range[] = {
        {UINT32_C(0x03bb), UINT32_C(0x03bb)},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES, 0u,
         lambda_range, 1u},
        {1u, CETTA_LP_NATIVE_UTF8_TERMINAL_ANY, 0u, NULL, 0u},
        {2u, CETTA_LP_NATIVE_UTF8_TERMINAL_EOF, 0u, NULL, 0u},
    };
    static const uint8_t input[] = {0xceu, 0xbbu, 0x21u};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build Unicode grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest(
            &grammar, STATE, terminals, 3u, input, sizeof(input),
            1000u, &forest, error, sizeof(error)),
        error[0] ? error : "parse Unicode input");
    (void)expect(counts, forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED,
                 "Unicode parse completed");
    (void)expect(counts, forest.source_pass_count == 1u &&
                         forest.decoded_byte_len == sizeof(input),
                 "source bytes traversed once");
    (void)expect(counts, forest.scalar_len == 2u &&
                         forest.byte_offsets[0] == 0u &&
                         forest.byte_offsets[1] == 2u &&
                         forest.byte_offsets[2] == 3u,
                 "Unicode scalar and byte index");
    (void)expect(counts, forest.root_len == 1u &&
                         root_has_extent(&forest, 0u, 2u),
                 "full Unicode root");
    (void)expect(counts,
                 forest_has_terminal(&forest, 0u, 0u, 1u, 0u, 2u, false) &&
                 forest_has_terminal(&forest, 1u, 1u, 2u, 2u, 3u, false) &&
                 forest_has_terminal(&forest, 2u, 2u, 2u, 3u, 3u, true),
                 "terminal scalar and byte spans");
    (void)expect(counts,
                 forest_kind_count(&forest,
                                   CETTA_LP_NATIVE_UTF8_FOREST_TERM) == 3u,
                 "reachable terminal count");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_borrowed_scalar_view_and_receipts(TestCounts *counts) {
    enum { STATE = 9u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 3u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 1u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 2u, 0u},
    };
    static const CettaLpNativeUnicodeRange first_range[] = {
        {(uint32_t)'x', (uint32_t)'x'},
        {UINT32_C(0x03bb), UINT32_C(0x03bb)},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_RANGES, 0u,
         first_range, 2u},
        {1u, CETTA_LP_NATIVE_UTF8_TERMINAL_ANY, 0u, NULL, 0u},
        {2u, CETTA_LP_NATIVE_UTF8_TERMINAL_EOF, 0u, NULL, 0u},
    };
    static const uint32_t codepoints[] = {UINT32_C(0x03bb), 0x21u};
    static const uint32_t byte_offsets[] = {0u, 2u, 3u};
    static const uint32_t corrupt_offsets[] = {0u, 1u, 3u};
    static const uint8_t ascii_input[] = {'x', '!'};
    const CettaLpNativeUtf8ScalarView view = {
        codepoints, byte_offsets, 2u, 3u, 0u, 0u,
    };
    CettaLpNativeUtf8ScalarView corrupt_view;
    CettaLpNativeUtf8ScalarBuffer ascii_buffer;
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build borrowed-view GLL grammar");
        return;
    }
    cetta_lp_native_utf8_scalar_buffer_init(&ascii_buffer);
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &view, 1000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse borrowed scalar view with GLL");
    (void)expect(
        counts,
        forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.source_pass_count == 0u &&
            forest.decoded_byte_len == 0u,
        "borrowed GLL view performs no source decode");
    (void)expect(
        counts,
        forest.scalar_len == 2u &&
            forest.codepoints[0] == UINT32_C(0x03bb) &&
            forest.codepoints[1] == 0x21u &&
            forest.byte_offsets[0] == 0u &&
            forest.byte_offsets[1] == 2u &&
            forest.byte_offsets[2] == 3u,
        "borrowed GLL view preserves scalar index");
    (void)expect(
        counts,
        forest.root_len == 1u && root_has_extent(&forest, 0u, 2u) &&
            forest_has_terminal(&forest, 0u, 0u, 1u, 0u, 2u, false) &&
            forest_has_terminal(&forest, 1u, 1u, 2u, 2u, 3u, false) &&
            forest_has_terminal(&forest, 2u, 2u, 2u, 3u, 3u, true),
        "borrowed GLL view preserves forest spans");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_scalar_buffer_prepare(
            &ascii_buffer, ascii_input, sizeof(ascii_input),
            error, sizeof(error)) &&
        ascii_buffer.view.ascii_bytes == ascii_input,
        error[0] ? error : "prepare ASCII identity scalar view");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &ascii_buffer.view, 1000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse ASCII identity scalar view with GLL");
    (void)expect(
        counts,
        forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.source_pass_count == 1u &&
            forest.decoded_byte_len == 2u &&
            forest.scalar_len == 2u &&
            forest.codepoints[0] == (uint32_t)'x' &&
            forest.codepoints[1] == (uint32_t)'!' &&
            forest.byte_offsets[0] == 0u &&
            forest.byte_offsets[1] == 1u &&
            forest.byte_offsets[2] == 2u &&
            forest.root_len == 1u && root_has_extent(&forest, 0u, 2u),
        "GLL identity view exports the same normalized forest boundary");
    cetta_lp_native_utf8_forest_free(&forest);

    corrupt_view = view;
    corrupt_view.decoded_byte_len = 3u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &corrupt_view, 1000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "scalar view") != NULL,
        "GLL rejects a zero-pass view claiming decoded bytes");
    cetta_lp_native_utf8_forest_free(&forest);

    corrupt_view = view;
    corrupt_view.source_pass_count = 2u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &corrupt_view, 1000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "scalar view") != NULL,
        "GLL rejects a multi-pass scalar-view receipt");
    cetta_lp_native_utf8_forest_free(&forest);

    corrupt_view = view;
    corrupt_view.byte_offsets = corrupt_offsets;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &corrupt_view, 1000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "scalar view index") != NULL,
        "GLL rejects a corrupt scalar-view byte index");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_utf8_scalar_buffer_free(&ascii_buffer);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_ambiguity_and_prefix(TestCounts *counts) {
    enum { STATE = 11u };
    static const uint32_t lhs[] = {STATE, STATE};
    static const uint32_t offsets[] = {0u, 1u, 2u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, 97u, NULL, 0u},
    };
    static const uint8_t input[] = {'a', 'a'};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};
    uint32_t root;

    if (!grammar_build(&grammar, 2u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build ambiguity grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest(
            &grammar, STATE, terminals, 1u, input, sizeof(input),
            1000u, &forest, error, sizeof(error)),
        error[0] ? error : "parse ambiguous prefix");
    (void)expect(counts, forest.root_len == 1u &&
                         root_has_extent(&forest, 0u, 1u),
                 "prefix root retained on full rejection");
    root = forest.root_len ? forest.roots[0] : UINT32_MAX;
    (void)expect(counts, root < forest.node_len &&
                         forest.nodes[root].choice_len == 2u,
                 "ambiguity retained as packed choices");
    (void)expect(counts, forest.scalar_len == 2u &&
                         forest.source_pass_count == 1u,
                 "rejected suffix still decoded once");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_epsilon_and_fail_closed(TestCounts *counts) {
    enum { STATE = 19u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 0u};
    static const uint8_t invalid_utf8[] = {0xc0u, 0x80u};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 1u, lhs, offsets, NULL)) {
        (void)expect(counts, false, "build epsilon grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest(
            &grammar, STATE, NULL, 0u, NULL, 0u, 100u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse epsilon input");
    (void)expect(counts, forest.root_len == 1u &&
                         root_has_extent(&forest, 0u, 0u) &&
                         forest_kind_count(
                             &forest,
                             CETTA_LP_NATIVE_UTF8_FOREST_EPSILON) == 1u,
                 "epsilon forest");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_forest(
            &grammar, STATE, NULL, 0u, invalid_utf8,
            sizeof(invalid_utf8), 100u, &forest, error, sizeof(error)) &&
            strstr(error, "invalid UTF-8") != NULL,
        "malformed UTF-8 rejected even after epsilon recognition");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_complete_prediction_preserves_prefix_api(TestCounts *counts) {
    enum { START = 23u };
    static const uint32_t lhs[] = {START, START};
    static const uint32_t offsets[] = {0u, 2u, 2u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_HL, START, 0u},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, 97u, NULL, 0u},
    };
    static const uint8_t input[] = {'a'};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest prefixes;
    CettaLpNativeUtf8Forest complete;
    char error[256] = {0};

    if (!grammar_build(&grammar, 2u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build prefix-policy grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&prefixes);
    cetta_lp_native_utf8_forest_init(&complete);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest(
            &grammar, START, terminals, 1u, input, sizeof(input),
            1000u, &prefixes, error, sizeof(error)) &&
            root_has_extent(&prefixes, 0u, 0u) &&
            root_has_extent(&prefixes, 0u, 1u),
        error[0] ? error : "prefix GLL retains every start-state root");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest_complete(
            &grammar, START, terminals, 1u, input, sizeof(input),
            1000u, &complete, error, sizeof(error)) &&
            root_with_right(&complete, 0u) < 0 &&
            root_with_right(&complete, 1u) >= 0,
        error[0] ? error : "complete GLL prunes only incomplete roots");
    cetta_lp_native_utf8_forest_free(&complete);
    cetta_lp_native_utf8_forest_free(&prefixes);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_descriptor_limit(TestCounts *counts) {
    enum { STATE = 23u };
    static const uint32_t lhs[] = {STATE, STATE};
    static const uint32_t offsets[] = {0u, 1u, 2u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, 97u, NULL, 0u},
    };
    static const uint8_t input[] = {'a'};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 2u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build descriptor-limit grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest(
            &grammar, STATE, terminals, 1u, input, sizeof(input),
            1u, &forest, error, sizeof(error)),
        error[0] ? error : "run descriptor-limited parser");
    (void)expect(counts,
                 forest.outcome ==
                     CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT &&
                 forest.work_item_len == 2u,
                 "descriptor exhaustion is typed");
    (void)expect(counts, forest.decoded_byte_len == sizeof(input) &&
                         forest.source_pass_count == 1u,
                 "resource path retains one-pass input accounting");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static bool deterministic_star_receipt(uint32_t scalar_len,
                                       uint32_t *work_item_len,
                                       uint32_t *graph_node_len,
                                       char *error,
                                       size_t error_size) {
    enum { START = 31u, LIST = 32u };
    static const uint32_t lhs[] = {START, LIST, LIST};
    static const uint32_t offsets[] = {0u, 2u, 4u, 4u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, LIST, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 1u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_HL, LIST, 0u},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, 97u, NULL, 0u},
        {1u, CETTA_LP_NATIVE_UTF8_TERMINAL_EOF, 0u, NULL, 0u},
    };
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    uint8_t *input = NULL;
    bool ok = false;

    if (!work_item_len || !graph_node_len || scalar_len == 0u)
        return false;
    input = malloc(scalar_len);
    if (!input || !grammar_build(&grammar, 3u, lhs, offsets, rhs))
        goto done;
    memset(input, 'a', scalar_len);
    cetta_lp_native_utf8_forest_init(&forest);
    if (!cetta_lp_native_gll_parse_utf8_forest_complete(
            &grammar, START, terminals, 2u, input, scalar_len,
            1000000u, &forest, error, error_size) ||
        forest.outcome != CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED ||
        forest.root_len != 1u ||
        !root_has_extent(&forest, 0u, scalar_len)) {
        cetta_lp_native_utf8_forest_free(&forest);
        cetta_lp_native_grammar_free(&grammar);
        goto done;
    }
    *work_item_len = forest.work_item_len;
    *graph_node_len = forest.graph_node_len;
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
    ok = true;

done:
    free(input);
    return ok;
}

static void test_deterministic_star_scale(TestCounts *counts) {
    uint32_t small_work = 0u;
    uint32_t large_work = 0u;
    uint32_t small_graph = 0u;
    uint32_t large_graph = 0u;
    char error[256] = {0};
    char detail[192];

    if (!deterministic_star_receipt(
            64u, &small_work, &small_graph, error, sizeof(error)) ||
        !deterministic_star_receipt(
            128u, &large_work, &large_graph, error, sizeof(error))) {
        (void)expect(
            counts, false,
            error[0] ? error : "run deterministic star scale canary");
        return;
    }
    (void)snprintf(
        detail, sizeof(detail),
        "deterministic star stays subquadratic "
        "(work %u -> %u, graph %u -> %u)",
        small_work, large_work, small_graph, large_graph);
    (void)expect(
        counts,
        large_work <= small_work * 3u &&
            large_graph <= small_graph * 3u,
        detail);
}

static void test_lattice_witnesses_and_corruption(TestCounts *counts) {
    enum { STATE = 29u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 1u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
    };
    static const uint32_t terminal_ids[] = {0u};
    static const uint32_t codepoints[] = {UINT32_C(0x03bb), 97u};
    static const uint32_t byte_offsets[] = {0u, 2u, 3u};
    static const uint32_t start_offsets[] = {0u, 4u, 5u, 5u};
    static const CettaLpNativeUtf8LatticeEdge edges[] = {
        {0u, 0u, 0u, 0u, 0u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, 6u},
        {0u, 0u, 1u, 0u, 2u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, 7u},
        {0u, 0u, 2u, 0u, 3u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, 8u},
        {0u, 0u, 2u, 0u, 3u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, 9u},
        {0u, 1u, 2u, 2u, 3u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, 10u},
    };
    const CettaLpNativeUtf8Lattice lattice = {
        terminal_ids, 1u, edges, 5u, start_offsets, 4u,
        codepoints, byte_offsets, 2u, 3u, 3u, 1u,
    };
    CettaLpNativeUtf8Lattice corrupt_lattice = lattice;
    CettaLpNativeUtf8LatticeEdge corrupt_edges[5];
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};
    int32_t full_root;

    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build GLL lattice grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_lattice_forest(
            &grammar, STATE, &lattice, 1000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse GLL witness lattice");
    (void)expect(counts,
                 forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
                 forest.source_pass_count == 1u &&
                 forest.scalar_len == 2u &&
                 forest.decoded_byte_len == 3u,
                 "GLL lattice preserves decoded one-pass receipt");
    (void)expect(counts,
                 forest.root_len == 3u &&
                 root_with_right(&forest, 0u) >= 0 &&
                 root_with_right(&forest, 1u) >= 0 &&
                 root_with_right(&forest, 2u) >= 0,
                 "GLL lattice retains every matched extent");
    (void)expect(counts,
                 forest_has_witness(&forest, 0u, 0u, 0u, 0u, 0u, 6u) &&
                 forest_has_witness(&forest, 0u, 0u, 1u, 0u, 2u, 7u) &&
                 forest_has_witness(&forest, 0u, 0u, 2u, 0u, 3u, 8u) &&
                 forest_has_witness(&forest, 0u, 0u, 2u, 0u, 3u, 9u) &&
                 forest_kind_count(
                     &forest, CETTA_LP_NATIVE_UTF8_FOREST_TERM) == 4u,
                 "GLL lattice preserves distinct lexical witnesses");
    full_root = root_with_right(&forest, 2u);
    (void)expect(counts,
                 full_root >= 0 &&
                 forest.nodes[full_root].choice_len == 2u,
                 "GLL packs equal-span witness ambiguity");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_lattice_forest_from(
            &grammar, STATE, &lattice, 1u, 1000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse GLL lattice from nonzero scalar");
    (void)expect(
        counts,
        forest.root_len == 1u && root_has_extent(&forest, 1u, 2u) &&
            forest_has_witness(
                &forest, 0u, 1u, 2u, 2u, 3u, 10u),
        "GLL lattice start scalar retains absolute source spans");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_lattice_forest_from(
            &grammar, STATE, &lattice, 3u, 1000u,
            &forest, error, sizeof(error)),
        "GLL rejects a lattice start beyond the source extent");
    cetta_lp_native_utf8_forest_free(&forest);

    memcpy(corrupt_edges, edges, sizeof(corrupt_edges));
    corrupt_edges[0].scalar_left = 1u;
    corrupt_lattice.edges = corrupt_edges;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_lattice_forest(
            &grammar, STATE, &corrupt_lattice, 1000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "lattice") != NULL,
        "GLL rejects corrupt lattice position grouping");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_zero_width_witness_sequence(TestCounts *counts) {
    enum { STATE = 31u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 3u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 1u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 2u, 0u},
    };
    static const uint32_t terminal_ids[] = {0u, 1u, 2u};
    static const uint32_t codepoints[] = {97u, 98u};
    static const uint32_t byte_offsets[] = {0u, 1u, 2u};
    static const uint32_t start_offsets[] = {0u, 1u, 3u, 3u};
    static const CettaLpNativeUtf8LatticeEdge edges[] = {
        {0u, 0u, 1u, 0u, 1u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 97u},
        {1u, 1u, 1u, 1u, 1u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, 42u},
        {2u, 1u, 2u, 1u, 2u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 98u},
    };
    static const uint32_t missing_start_offsets[] = {0u, 1u, 2u, 2u};
    static const CettaLpNativeUtf8LatticeEdge missing_edges[] = {
        {0u, 0u, 1u, 0u, 1u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 97u},
        {2u, 1u, 2u, 1u, 2u,
         CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_SCALAR, 98u},
    };
    const CettaLpNativeUtf8Lattice lattice = {
        terminal_ids, 3u, edges, 3u, start_offsets, 4u,
        codepoints, byte_offsets, 2u, 2u, 2u, 1u,
    };
    const CettaLpNativeUtf8Lattice missing_lattice = {
        terminal_ids, 3u, missing_edges, 2u, missing_start_offsets, 4u,
        codepoints, byte_offsets, 2u, 2u, 2u, 1u,
    };
    CettaLpNativeUtf8Lattice corrupt_lattice = lattice;
    CettaLpNativeUtf8LatticeEdge corrupt_edges[3];
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build zero-width GLL grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_lattice_forest(
            &grammar, STATE, &lattice, 1000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse zero-width GLL witness sequence");
    (void)expect(
        counts,
        forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.root_len == 1u && root_has_extent(&forest, 0u, 2u) &&
            forest_has_witness(&forest, 1u, 1u, 1u, 1u, 1u, 42u),
        "GLL composes a zero-width witness between consuming terminals");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_lattice_forest(
            &grammar, STATE, &missing_lattice, 1000u,
            &forest, error, sizeof(error)) &&
            forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.root_len == 0u,
        error[0] ? error : "GLL rejects a sequence with no guard witness");
    cetta_lp_native_utf8_forest_free(&forest);

    memcpy(corrupt_edges, edges, sizeof(corrupt_edges));
    corrupt_edges[1].scalar_right = 0u;
    corrupt_edges[1].byte_right = 0u;
    corrupt_lattice.edges = corrupt_edges;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_lattice_forest(
            &grammar, STATE, &corrupt_lattice, 1000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "lattice edge") != NULL,
        "GLL rejects a backward witness edge");
    cetta_lp_native_utf8_forest_free(&forest);

    memcpy(corrupt_edges, edges, sizeof(corrupt_edges));
    corrupt_edges[1].byte_right = 2u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_parse_utf8_lattice_forest(
            &grammar, STATE, &corrupt_lattice, 1000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "lattice edge") != NULL,
        "GLL rejects a zero-width witness with a nonzero byte extent");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void test_prepared_reuse_and_lifecycle(TestCounts *counts) {
    enum { STATE = 41u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 2u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 1u, 0u},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_ANY, 0u, NULL, 0u},
        {1u, CETTA_LP_NATIVE_UTF8_TERMINAL_EOF, 0u, NULL, 0u},
    };
    static const uint8_t ascii_input[] = {'a'};
    static const uint8_t unicode_input[] = {0xceu, 0xbbu};
    CettaLpNativeGrammar grammar;
    CettaLpNativeGllPrepared prepared;
    CettaLpNativeGllPrepared empty;
    CettaLpNativeUtf8Forest cold_ascii;
    CettaLpNativeUtf8Forest cold_unicode;
    CettaLpNativeUtf8Forest warm_ascii;
    CettaLpNativeUtf8Forest warm_unicode;
    char error[256] = {0};

    cetta_lp_native_gll_prepared_init(&prepared);
    cetta_lp_native_gll_prepared_init(&empty);
    cetta_lp_native_utf8_forest_init(&cold_ascii);
    cetta_lp_native_utf8_forest_init(&cold_unicode);
    cetta_lp_native_utf8_forest_init(&warm_ascii);
    cetta_lp_native_utf8_forest_init(&warm_unicode);
    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build prepared GLL grammar");
        goto done;
    }
    (void)expect(
        counts,
        cetta_lp_native_gll_parse_utf8_forest_complete(
            &grammar, STATE, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &cold_ascii, error, sizeof(error)) &&
        cetta_lp_native_gll_parse_utf8_forest_complete(
            &grammar, STATE, terminals, 2u,
            unicode_input, sizeof(unicode_input), 1000u,
            &cold_unicode, error, sizeof(error)),
        error[0] ? error : "construct cold GLL reference forests");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_gll_prepare(
            &prepared, &grammar, STATE, error, sizeof(error)),
        error[0] ? error : "prepare reusable GLL tables");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_prepare(
            &prepared, &grammar, STATE + 1u, error, sizeof(error)) &&
        strstr(error, "start nonterminal") != NULL,
        "failed GLL re-prepare preserves the prior owner");
    cetta_lp_native_grammar_free(&grammar);
    memset(&grammar, 0, sizeof(grammar));
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
            &prepared, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &warm_ascii, error, sizeof(error)) &&
        cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
            &prepared, terminals, 2u,
            unicode_input, sizeof(unicode_input), 1000u,
            &warm_unicode, error, sizeof(error)),
        error[0] ? error : "reuse prepared GLL tables after grammar free");
    (void)expect(
        counts,
        forest_equal(&cold_ascii, &warm_ascii) &&
        forest_equal(&cold_unicode, &warm_unicode),
        "prepared GLL forests equal cold forests exactly");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
            &empty, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &warm_ascii, error, sizeof(error)) &&
        strstr(error, "prepared") != NULL,
        "unprepared GLL owner fails closed");
    cetta_lp_native_gll_prepared_free(&prepared);
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_gll_prepared_parse_utf8_forest_complete(
            &prepared, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &warm_ascii, error, sizeof(error)) &&
        strstr(error, "prepared") != NULL,
        "freed GLL owner fails closed");

done:
    cetta_lp_native_gll_prepared_free(&prepared);
    cetta_lp_native_gll_prepared_free(&empty);
    cetta_lp_native_utf8_forest_free(&cold_ascii);
    cetta_lp_native_utf8_forest_free(&cold_unicode);
    cetta_lp_native_utf8_forest_free(&warm_ascii);
    cetta_lp_native_utf8_forest_free(&warm_unicode);
    cetta_lp_native_grammar_free(&grammar);
}

int main(void) {
    SymbolTable symbols;
    TestCounts counts = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    test_unicode_single_pass(&counts);
    test_owned_scalar_buffer(&counts);
    test_borrowed_scalar_view_and_receipts(&counts);
    test_ambiguity_and_prefix(&counts);
    test_epsilon_and_fail_closed(&counts);
    test_complete_prediction_preserves_prefix_api(&counts);
    test_descriptor_limit(&counts);
    test_deterministic_star_scale(&counts);
    test_lattice_witnesses_and_corruption(&counts);
    test_zero_width_witness_sequence(&counts);
    test_prepared_reuse_and_lifecycle(&counts);

    printf("(NativeGLLUtf8ForestSummary %u %u)\n",
           counts.passed, counts.failed);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
