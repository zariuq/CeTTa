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

static void maybe_emit_wire_snapshot(
    TestCounts *counts,
    const uint8_t *wire,
    size_t wire_size) {
    const char *output_path = getenv("CETTA_NATIVE_FOREST_WIRE_OUT");
    FILE *output;
    bool written;

    if (!output_path || output_path[0] == '\0')
        return;
    output = fopen(output_path, "wb");
    if (!expect(counts, output != NULL,
                "open GLR forest wire qualification output")) {
        return;
    }
    written = fwrite(wire, 1u, wire_size, output) == wire_size;
    if (fclose(output) != 0)
        written = false;
    (void)expect(counts, written,
                 "emit GLR forest wire qualification output");
}

static void qualify_wire_snapshot(
    TestCounts *counts,
    const CettaLpNativeUtf8Forest *forest) {
    char error[256] = {0};
    size_t wire_size = 0u;
    size_t first_written = 0u;
    size_t second_written = 0u;
    size_t expected_size;
    uint8_t *first;
    uint8_t *second;

    if (!expect(
            counts,
            cetta_lp_native_utf8_forest_wire_size(
                forest, &wire_size, error, sizeof(error)),
            error[0] ? error : "size canonical GLR forest wire")) {
        return;
    }
    expected_size = 4u + 4u *
        (14u + 14u * (size_t)forest->node_len +
         6u * (size_t)forest->choice_len +
         (size_t)forest->root_len +
         (size_t)forest->expected_terminal_len +
         2u * (size_t)forest->scalar_len + 1u);
    (void)expect(counts, wire_size == expected_size,
                 "GLR forest wire has the versioned canonical extent");
    first = malloc(wire_size);
    second = malloc(wire_size);
    if (!expect(counts, first && second,
                "allocate GLR forest wire qualification buffers")) {
        free(second);
        free(first);
        return;
    }
    error[0] = '\0';
    if (expect(
        counts,
        cetta_lp_native_utf8_forest_wire_write(
            forest, first, wire_size, &first_written,
            error, sizeof(error)) &&
            first_written == wire_size &&
            memcmp(first, "CNF1", 4u) == 0,
        error[0] ? error : "write canonical GLR forest wire")) {
        maybe_emit_wire_snapshot(counts, first, wire_size);
    }
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_forest_wire_write(
            forest, second, wire_size, &second_written,
            error, sizeof(error)) &&
            second_written == wire_size &&
            memcmp(first, second, wire_size) == 0,
        error[0] ? error : "GLR forest wire is deterministic");
    memset(second, 0x5a, wire_size);
    second_written = wire_size;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_utf8_forest_wire_write(
            forest, second, wire_size - 1u, &second_written,
            error, sizeof(error)) &&
            second_written == 0u && second[0] == 0x5au,
        "undersized GLR forest wire output is fail-atomic");
    free(second);
    free(first);
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

static void unicode_single_pass_gate(TestCounts *counts) {
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
        (void)expect(counts, false, "build Unicode GLR grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, terminals, 3u, input, sizeof(input),
            10000u, &forest, error, sizeof(error)),
        error[0] ? error : "parse Unicode GLR input");
    (void)expect(counts,
                 forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
                 forest.source_pass_count == 1u &&
                 forest.decoded_byte_len == sizeof(input),
                 "GLR source bytes traversed once");
    (void)expect(counts, forest.scalar_len == 2u &&
                         forest.byte_offsets[0] == 0u &&
                         forest.byte_offsets[1] == 2u &&
                         forest.byte_offsets[2] == 3u,
                 "GLR Unicode scalar and byte index");
    (void)expect(counts, forest.root_len == 1u &&
                         root_with_right(&forest, 2u) >= 0,
                 "GLR full Unicode root");
    (void)expect(counts,
                 forest_has_terminal(&forest, 0u, 0u, 1u, 0u, 2u, false) &&
                 forest_has_terminal(&forest, 1u, 1u, 2u, 2u, 3u, false) &&
                 forest_has_terminal(&forest, 2u, 2u, 2u, 3u, 3u, true),
                 "GLR terminal scalar and byte spans");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void borrowed_scalar_view_and_receipts_gate(TestCounts *counts) {
    enum { STATE = 9u };
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
    static const uint32_t codepoints[] = {UINT32_C(0x03bb), 0x21u};
    static const uint32_t byte_offsets[] = {0u, 2u, 3u};
    static const uint32_t corrupt_offsets[] = {0u, 1u, 3u};
    const CettaLpNativeUtf8ScalarView view = {
        codepoints, byte_offsets, 2u, 3u, 0u, 0u,
    };
    CettaLpNativeUtf8ScalarView corrupt_view;
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build borrowed-view GLR grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &view, 10000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse borrowed scalar view with GLR");
    (void)expect(
        counts,
        forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.source_pass_count == 0u &&
            forest.decoded_byte_len == 0u,
        "borrowed GLR view performs no source decode");
    (void)expect(
        counts,
        forest.scalar_len == 2u &&
            forest.codepoints[0] == UINT32_C(0x03bb) &&
            forest.codepoints[1] == 0x21u &&
            forest.byte_offsets[0] == 0u &&
            forest.byte_offsets[1] == 2u &&
            forest.byte_offsets[2] == 3u,
        "borrowed GLR view preserves scalar index");
    (void)expect(
        counts,
        forest.root_len == 1u && root_with_right(&forest, 2u) >= 0 &&
            forest_has_terminal(&forest, 0u, 0u, 1u, 0u, 2u, false) &&
            forest_has_terminal(&forest, 1u, 1u, 2u, 2u, 3u, false) &&
            forest_has_terminal(&forest, 2u, 2u, 2u, 3u, 3u, true),
        "borrowed GLR view preserves forest spans");
    cetta_lp_native_utf8_forest_free(&forest);

    corrupt_view = view;
    corrupt_view.decoded_byte_len = 3u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &corrupt_view, 10000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "scalar view") != NULL,
        "GLR rejects a zero-pass view claiming decoded bytes");
    cetta_lp_native_utf8_forest_free(&forest);

    corrupt_view = view;
    corrupt_view.source_pass_count = 2u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &corrupt_view, 10000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "scalar view") != NULL,
        "GLR rejects a multi-pass scalar-view receipt");
    cetta_lp_native_utf8_forest_free(&forest);

    corrupt_view = view;
    corrupt_view.byte_offsets = corrupt_offsets;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_scalar_view_forest(
            &grammar, STATE, terminals, 3u, &corrupt_view, 10000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "scalar view index") != NULL,
        "GLR rejects a corrupt scalar-view byte index");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void left_recursion_and_ambiguity_gate(TestCounts *counts) {
    enum { STATE = 11u };
    static const uint32_t lhs[] = {STATE, STATE, STATE};
    static const uint32_t offsets[] = {0u, 2u, 3u, 4u};
    static const CettaLpNativeSymbol rhs[] = {
        {CETTA_LP_NATIVE_SYMBOL_HL, STATE, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
        {CETTA_LP_NATIVE_SYMBOL_TM, 0u, 0u},
    };
    static const CettaLpNativeUtf8Terminal terminals[] = {
        {0u, CETTA_LP_NATIVE_UTF8_TERMINAL_SCALAR, 97u, NULL, 0u},
    };
    static const uint8_t input[] = {'a', 'a', 'a'};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};
    int32_t first_root;

    if (!grammar_build(&grammar, 3u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build recursive GLR grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, terminals, 1u, input, sizeof(input),
            100000u, &forest, error, sizeof(error)),
        error[0] ? error : "parse recursive GLR input");
    (void)expect(counts, forest.root_len == 3u &&
                         root_with_right(&forest, 1u) >= 0 &&
                         root_with_right(&forest, 2u) >= 0 &&
                         root_with_right(&forest, 3u) >= 0,
                 "GLR retains every recursive prefix root");
    first_root = root_with_right(&forest, 1u);
    (void)expect(counts,
                 first_root >= 0 &&
                 forest.nodes[first_root].choice_len == 2u,
                 "GLR packs duplicate leaf ambiguity");
    (void)expect(counts,
                 forest_kind_count(
                     &forest, CETTA_LP_NATIVE_UTF8_FOREST_INTERMEDIATE) > 0u &&
                 root_with_right(&forest, 3u) >= 0,
                 "GLR builds recursive intermediate forest nodes");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_forest_validate(
            &forest, error, sizeof(error)),
        error[0] ? error : "validate exported GLR forest contract");
    qualify_wire_snapshot(counts, &forest);
    if (forest.choice_len > 0u) {
        uint32_t saved_byte_pivot = forest.choices[0].byte_pivot;
        forest.choices[0].byte_pivot = saved_byte_pivot + 1u;
        error[0] = '\0';
        (void)expect(
            counts,
            !cetta_lp_native_utf8_forest_validate(
                &forest, error, sizeof(error)) &&
                strstr(error, "choice reference") != NULL,
            "GLR forest validator rejects a corrupted byte pivot");
        forest.choices[0].byte_pivot = saved_byte_pivot;
    }
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void epsilon_invalid_and_limit_gate(TestCounts *counts) {
    enum { STATE = 19u };
    static const uint32_t lhs[] = {STATE};
    static const uint32_t offsets[] = {0u, 0u};
    static const uint8_t invalid_utf8[] = {0xc0u, 0x80u};
    CettaLpNativeGrammar grammar;
    CettaLpNativeUtf8Forest forest;
    char error[256] = {0};

    if (!grammar_build(&grammar, 1u, lhs, offsets, NULL)) {
        (void)expect(counts, false, "build epsilon GLR grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, NULL, 0u, NULL, 0u, 100u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse epsilon GLR input");
    (void)expect(counts, forest.root_len == 1u &&
                         root_with_right(&forest, 0u) >= 0 &&
                         forest_kind_count(
                             &forest,
                             CETTA_LP_NATIVE_UTF8_FOREST_EPSILON) == 1u,
                 "GLR epsilon forest");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, NULL, 0u, invalid_utf8,
            sizeof(invalid_utf8), 100u, &forest, error, sizeof(error)) &&
            strstr(error, "invalid UTF-8") != NULL,
        "GLR malformed UTF-8 rejected after epsilon recognition");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, NULL, 0u, NULL, 0u, 1u,
            &forest, error, sizeof(error)) &&
            forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT &&
            forest.work_item_len == 1u,
        error[0] ? error : "GLR resource exhaustion is typed");
    (void)expect(counts, forest.source_pass_count == 1u &&
                         forest.decoded_byte_len == 0u,
                 "GLR resource path preserves one-pass accounting");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_forest_validate(
            &forest, error, sizeof(error)),
        error[0] ? error : "validate typed GLR resource result");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static bool deterministic_star_receipt(uint32_t scalar_len,
                                       uint32_t *work_item_len,
                                       uint32_t *graph_node_len,
                                       uint32_t *stack_node_len,
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

    if (!work_item_len || !graph_node_len || !stack_node_len ||
        scalar_len == 0u) {
        return false;
    }
    input = malloc(scalar_len);
    if (!input || !grammar_build(&grammar, 3u, lhs, offsets, rhs))
        goto done;
    memset(input, 'a', scalar_len);
    cetta_lp_native_utf8_forest_init(&forest);
    if (!cetta_lp_native_glr_parse_utf8_forest(
            &grammar, START, terminals, 2u, input, scalar_len,
            1000000u, &forest, error, error_size) ||
        forest.outcome != CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED ||
        forest.root_len != 1u ||
        root_with_right(&forest, scalar_len) < 0) {
        cetta_lp_native_utf8_forest_free(&forest);
        cetta_lp_native_grammar_free(&grammar);
        goto done;
    }
    *work_item_len = forest.work_item_len;
    *graph_node_len = forest.graph_node_len;
    *stack_node_len = forest.stack_node_len;
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
    ok = true;

done:
    free(input);
    return ok;
}

static void deterministic_star_scale_gate(TestCounts *counts) {
    uint32_t small_work = 0u;
    uint32_t large_work = 0u;
    uint32_t small_graph = 0u;
    uint32_t large_graph = 0u;
    uint32_t small_stack = 0u;
    uint32_t large_stack = 0u;
    char error[256] = {0};
    char detail[224];

    if (!deterministic_star_receipt(
            64u, &small_work, &small_graph, &small_stack,
            error, sizeof(error)) ||
        !deterministic_star_receipt(
            128u, &large_work, &large_graph, &large_stack,
            error, sizeof(error))) {
        (void)expect(
            counts, false,
            error[0] ? error : "run deterministic GLR scale canary");
        return;
    }
    (void)snprintf(
        detail, sizeof(detail),
        "deterministic GLR stack stays subquadratic "
        "(work %u -> %u, graph %u -> %u, stack %u -> %u)",
        small_work, large_work, small_graph, large_graph,
        small_stack, large_stack);
    (void)expect(
        counts,
        small_stack > 0u && large_stack > small_stack &&
            large_work <= small_work * 3u &&
            large_graph <= small_graph * 3u &&
            large_stack <= small_stack * 3u,
        detail);
}

static void lattice_witnesses_and_corruption_gate(TestCounts *counts) {
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
        (void)expect(counts, false, "build GLR lattice grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_lattice_forest(
            &grammar, STATE, &lattice, 10000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse GLR witness lattice");
    (void)expect(counts,
                 forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
                 forest.source_pass_count == 1u &&
                 forest.scalar_len == 2u &&
                 forest.decoded_byte_len == 3u,
                 "GLR lattice preserves decoded one-pass receipt");
    (void)expect(counts,
                 forest.root_len == 3u &&
                 root_with_right(&forest, 0u) >= 0 &&
                 root_with_right(&forest, 1u) >= 0 &&
                 root_with_right(&forest, 2u) >= 0,
                 "GLR lattice retains every matched extent");
    (void)expect(counts,
                 forest_has_witness(&forest, 0u, 0u, 0u, 0u, 0u, 6u) &&
                 forest_has_witness(&forest, 0u, 0u, 1u, 0u, 2u, 7u) &&
                 forest_has_witness(&forest, 0u, 0u, 2u, 0u, 3u, 8u) &&
                 forest_has_witness(&forest, 0u, 0u, 2u, 0u, 3u, 9u) &&
                 forest_kind_count(
                     &forest, CETTA_LP_NATIVE_UTF8_FOREST_TERM) == 4u,
                 "GLR lattice preserves distinct lexical witnesses");
    full_root = root_with_right(&forest, 2u);
    (void)expect(counts,
                 full_root >= 0 &&
                 forest.nodes[full_root].choice_len == 2u,
                 "GLR packs equal-span witness ambiguity");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_forest_validate(
            &forest, error, sizeof(error)),
        error[0] ? error : "validate ambiguous GLR witness forest");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_lattice_forest_from(
            &grammar, STATE, &lattice, 1u, 10000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse GLR lattice from nonzero scalar");
    (void)expect(
        counts,
        forest.root_len == 1u &&
            forest.roots[0] < forest.node_len &&
            forest.nodes[forest.roots[0]].scalar_left == 1u &&
            forest.nodes[forest.roots[0]].scalar_right == 2u &&
            forest_has_witness(
                &forest, 0u, 1u, 2u, 2u, 3u, 10u),
        "GLR lattice start scalar retains absolute source spans");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_lattice_forest_from(
            &grammar, STATE, &lattice, 3u, 10000u,
            &forest, error, sizeof(error)),
        "GLR rejects a lattice start beyond the source extent");
    cetta_lp_native_utf8_forest_free(&forest);

    memcpy(corrupt_edges, edges, sizeof(corrupt_edges));
    corrupt_edges[0].scalar_left = 1u;
    corrupt_lattice.edges = corrupt_edges;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_lattice_forest(
            &grammar, STATE, &corrupt_lattice, 10000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "lattice") != NULL,
        "GLR rejects corrupt lattice position grouping");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void zero_width_witness_sequence_gate(TestCounts *counts) {
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
        (void)expect(counts, false, "build zero-width GLR grammar");
        return;
    }
    cetta_lp_native_utf8_forest_init(&forest);
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_lattice_forest(
            &grammar, STATE, &lattice, 10000u,
            &forest, error, sizeof(error)),
        error[0] ? error : "parse zero-width GLR witness sequence");
    (void)expect(
        counts,
        forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.root_len == 1u && root_with_right(&forest, 2u) >= 0 &&
            forest_has_witness(&forest, 1u, 1u, 1u, 1u, 1u, 42u),
        "GLR composes a zero-width witness between consuming terminals");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_utf8_forest_validate(
            &forest, error, sizeof(error)),
        error[0] ? error : "validate zero-width GLR witness forest");
    cetta_lp_native_utf8_forest_free(&forest);

    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_lattice_forest(
            &grammar, STATE, &missing_lattice, 10000u,
            &forest, error, sizeof(error)) &&
            forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED &&
            forest.root_len == 0u,
        error[0] ? error : "GLR rejects a sequence with no guard witness");
    cetta_lp_native_utf8_forest_free(&forest);

    memcpy(corrupt_edges, edges, sizeof(corrupt_edges));
    corrupt_edges[1].scalar_right = 0u;
    corrupt_edges[1].byte_right = 0u;
    corrupt_lattice.edges = corrupt_edges;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_lattice_forest(
            &grammar, STATE, &corrupt_lattice, 10000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "lattice edge") != NULL,
        "GLR rejects a backward witness edge");
    cetta_lp_native_utf8_forest_free(&forest);

    memcpy(corrupt_edges, edges, sizeof(corrupt_edges));
    corrupt_edges[1].byte_right = 2u;
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_parse_utf8_lattice_forest(
            &grammar, STATE, &corrupt_lattice, 10000u,
            &forest, error, sizeof(error)) &&
            strstr(error, "lattice edge") != NULL,
        "GLR rejects a zero-width witness with a nonzero byte extent");
    cetta_lp_native_utf8_forest_free(&forest);
    cetta_lp_native_grammar_free(&grammar);
}

static void prepared_reuse_and_lifecycle_gate(TestCounts *counts) {
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
    CettaLpNativeGlrPrepared prepared;
    CettaLpNativeGlrPrepared empty;
    CettaLpNativeUtf8Forest cold_ascii;
    CettaLpNativeUtf8Forest cold_unicode;
    CettaLpNativeUtf8Forest warm_ascii;
    CettaLpNativeUtf8Forest warm_unicode;
    char error[256] = {0};

    cetta_lp_native_glr_prepared_init(&prepared);
    cetta_lp_native_glr_prepared_init(&empty);
    cetta_lp_native_utf8_forest_init(&cold_ascii);
    cetta_lp_native_utf8_forest_init(&cold_unicode);
    cetta_lp_native_utf8_forest_init(&warm_ascii);
    cetta_lp_native_utf8_forest_init(&warm_unicode);
    if (!grammar_build(&grammar, 1u, lhs, offsets, rhs)) {
        (void)expect(counts, false, "build prepared GLR grammar");
        goto done;
    }
    (void)expect(
        counts,
        cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &cold_ascii, error, sizeof(error)) &&
        cetta_lp_native_glr_parse_utf8_forest(
            &grammar, STATE, terminals, 2u,
            unicode_input, sizeof(unicode_input), 1000u,
            &cold_unicode, error, sizeof(error)),
        error[0] ? error : "construct cold GLR reference forests");
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_glr_prepare(
            &prepared, &grammar, STATE, error, sizeof(error)),
        error[0] ? error : "prepare reusable GLR tables");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_prepare(
            &prepared, &grammar, STATE + 1u, error, sizeof(error)) &&
        strstr(error, "start nonterminal") != NULL,
        "failed GLR re-prepare preserves the prior owner");
    cetta_lp_native_grammar_free(&grammar);
    memset(&grammar, 0, sizeof(grammar));
    error[0] = '\0';
    (void)expect(
        counts,
        cetta_lp_native_glr_prepared_parse_utf8_forest(
            &prepared, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &warm_ascii, error, sizeof(error)) &&
        cetta_lp_native_glr_prepared_parse_utf8_forest(
            &prepared, terminals, 2u,
            unicode_input, sizeof(unicode_input), 1000u,
            &warm_unicode, error, sizeof(error)),
        error[0] ? error : "reuse prepared GLR tables after grammar free");
    (void)expect(
        counts,
        forest_equal(&cold_ascii, &warm_ascii) &&
        forest_equal(&cold_unicode, &warm_unicode),
        "prepared GLR forests equal cold forests exactly");
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_prepared_parse_utf8_forest(
            &empty, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &warm_ascii, error, sizeof(error)) &&
        strstr(error, "prepared") != NULL,
        "unprepared GLR owner fails closed");
    cetta_lp_native_glr_prepared_free(&prepared);
    error[0] = '\0';
    (void)expect(
        counts,
        !cetta_lp_native_glr_prepared_parse_utf8_forest(
            &prepared, terminals, 2u,
            ascii_input, sizeof(ascii_input), 1000u,
            &warm_ascii, error, sizeof(error)) &&
        strstr(error, "prepared") != NULL,
        "freed GLR owner fails closed");

done:
    cetta_lp_native_glr_prepared_free(&prepared);
    cetta_lp_native_glr_prepared_free(&empty);
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

    unicode_single_pass_gate(&counts);
    borrowed_scalar_view_and_receipts_gate(&counts);
    left_recursion_and_ambiguity_gate(&counts);
    epsilon_invalid_and_limit_gate(&counts);
    deterministic_star_scale_gate(&counts);
    lattice_witnesses_and_corruption_gate(&counts);
    zero_width_witness_sequence_gate(&counts);
    prepared_reuse_and_lifecycle_gate(&counts);

    printf("(NativeGLRUtf8ForestSummary %u %u)\n",
           counts.passed, counts.failed);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
