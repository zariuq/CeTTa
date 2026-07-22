#include "finite_horn_ground_term_v1.h"
#include "parser_pack_gll_v1.h"

#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char DIGEST_A[] =
    "0000000000000000000000000000000000000000000000000000000000000001";
static const char DIGEST_B[] =
    "0000000000000000000000000000000000000000000000000000000000000002";
static const char DIGEST_C[] =
    "0000000000000000000000000000000000000000000000000000000000000003";

typedef struct {
    uint32_t passed;
    uint32_t failed;
} TestCounts;

typedef struct {
    Atom *term;
    char *canonical;
} CanonicalTerm;

static bool expect(TestCounts *counts, bool condition, const char *label) {
    if (condition) {
        counts->passed++;
        return true;
    }
    counts->failed++;
    fprintf(stderr, "FAIL: %s\n", label);
    return false;
}

static Atom *app(Arena *arena,
                 const char *head,
                 size_t argument_len,
                 Atom **arguments) {
    Atom **items = malloc(sizeof(*items) * (argument_len + 1u));
    Atom *result;
    size_t index;
    if (!items)
        return NULL;
    items[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        items[index + 1u] = arguments[index];
    result = atom_expr(arena, items, argument_len + 1u);
    free(items);
    return result;
}

static Atom *unary(Arena *arena, const char *head, Atom *argument) {
    Atom *arguments[1] = {argument};
    return app(arena, head, 1u, arguments);
}

static Atom *binary(Arena *arena,
                    const char *head,
                    Atom *left,
                    Atom *right) {
    Atom *arguments[2] = {left, right};
    return app(arena, head, 2u, arguments);
}

static Atom *cp(Arena *arena, uint32_t scalar) {
    return unary(arena, "cp", atom_int(arena, scalar));
}

static Atom *items(Arena *arena, Atom **values, uint32_t len) {
    Atom *list = atom_symbol(arena, "pp-items-nil");
    while (len > 0u) {
        len--;
        list = binary(arena, "pp-items-cons", values[len], list);
    }
    return list;
}

static Atom *actions(Arena *arena, Atom **values, uint32_t len) {
    Atom *list = atom_symbol(arena, "pa-nil");
    while (len > 0u) {
        len--;
        list = binary(arena, "pa-cons", values[len], list);
    }
    return list;
}

static Atom *terminal_char(Arena *arena, uint32_t scalar) {
    return unary(arena, "pp-terminal",
                 unary(arena, "pp-terminal-char", cp(arena, scalar)));
}

static Atom *nonterminal(Arena *arena, Atom *state) {
    return unary(arena, "pp-nonterminal", state);
}

static Atom *slot(Arena *arena, uint32_t index) {
    Atom *peano = atom_symbol(arena, "q-zero");
    while (index > 0u) {
        peano = unary(arena, "q-succ", peano);
        index--;
    }
    return unary(arena, "pa-slot", peano);
}

static Atom *apply(Arena *arena,
                   const char *head,
                   Atom **arguments,
                   uint32_t argument_len) {
    return binary(arena, "pa-apply", atom_symbol(arena, head),
                  actions(arena, arguments, argument_len));
}

static Atom *production(Arena *arena,
                        const char *label,
                        Atom *state,
                        Atom **rhs,
                        uint32_t rhs_len,
                        Atom *action) {
    Atom *arguments[4] = {
        atom_symbol(arena, label), state, items(arena, rhs, rhs_len), action,
    };
    return app(arena, "pp-production", 4u, arguments);
}

static Atom *evidence_answer(Arena *arena,
                             PPABIV1EvidenceKind kind,
                             Atom *artifact) {
    const char *head = kind == PPABI_V1_EVIDENCE_PRODUCTION
        ? "compile-pack-production" : "compile-pack-class-clause";
    Atom *arguments[2] = {atom_symbol(arena, "owner"), artifact};
    return app(arena, head, 2u, arguments);
}

static Atom *evidence_certificate(Arena *arena, uint32_t index) {
    Atom *arguments[2] = {
        atom_int(arena, index), atom_symbol(arena, "evidence"),
    };
    return app(arena, "cert", 2u, arguments);
}

static char *render(Atom *term) {
    uint8_t *bytes = NULL;
    size_t len = 0u;
    if (!fh_ground_term_v1_render(term, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static int canonical_compare(const void *lhs, const void *rhs) {
    const CanonicalTerm *left = lhs;
    const CanonicalTerm *right = rhs;
    return strcmp(left->canonical, right->canonical);
}

static bool canonicalize_terms(Atom **terms, size_t len) {
    CanonicalTerm *entries = calloc(len ? len : 1u, sizeof(*entries));
    size_t index;
    if (!entries)
        return false;
    for (index = 0u; index < len; index++) {
        entries[index].term = terms[index];
        entries[index].canonical = render(terms[index]);
        if (!entries[index].canonical)
            goto fail;
    }
    if (len > 1u)
        qsort(entries, len, sizeof(*entries), canonical_compare);
    for (index = 0u; index < len; index++)
        terms[index] = entries[index].term;
    for (index = 0u; index < len; index++)
        free(entries[index].canonical);
    free(entries);
    return true;

fail:
    for (index = 0u; index < len; index++)
        free(entries[index].canonical);
    free(entries);
    return false;
}

static bool load_pack(Arena *arena,
                      Atom **productions,
                      size_t production_len,
                      Atom **classes,
                      size_t class_len,
                      PPABIV1Pack *pack,
                      char *error,
                      size_t error_size) {
    PPABIV1DerivationInput *roots;
    PPABIV1ProvenanceInput provenance;
    size_t root_len = production_len + class_len;
    size_t index;
    bool ok;

    if (!canonicalize_terms(productions, production_len) ||
        !canonicalize_terms(classes, class_len)) {
        return false;
    }
    roots = calloc(root_len ? root_len : 1u, sizeof(*roots));
    if (!roots)
        return false;
    for (index = 0u; index < production_len; index++) {
        roots[index].kind = PPABI_V1_EVIDENCE_PRODUCTION;
        roots[index].artifact = productions[index];
        roots[index].answer = evidence_answer(
            arena, roots[index].kind, productions[index]);
        roots[index].certificate = evidence_certificate(arena, (uint32_t)index);
    }
    for (index = 0u; index < class_len; index++) {
        size_t root_index = production_len + index;
        roots[root_index].kind = PPABI_V1_EVIDENCE_CLASS;
        roots[root_index].artifact = classes[index];
        roots[root_index].answer = evidence_answer(
            arena, roots[root_index].kind, classes[index]);
        roots[root_index].certificate = evidence_certificate(
            arena, (uint32_t)root_index);
    }
    provenance = (PPABIV1ProvenanceInput){
        .source_digest = DIGEST_A,
        .compiler_digest = DIGEST_B,
        .environment_digest = DIGEST_C,
        .derivations = roots,
        .derivation_len = root_len,
    };
    ppabi_v1_pack_init(pack);
    ok = ppabi_v1_pack_load(
        pack, productions, production_len, classes, class_len,
        &provenance, error, error_size);
    free(roots);
    return ok;
}

static bool result_has(PPNativeV1Result *result, const char *expected) {
    uint32_t index;
    for (index = 0u; index < result->semantic_result_len; index++) {
        char *actual = render(result->semantic_results[index]);
        bool equal = actual && strcmp(actual, expected) == 0;
        free(actual);
        if (equal)
            return true;
    }
    return false;
}

static bool results_equal(const PPNativeV1Result *left,
                          const PPNativeV1Result *right) {
    uint32_t index;

    if (!left || !right || left->outcome != right->outcome ||
        left->accepted != right->accepted ||
        left->canonical_forest_materialized !=
            right->canonical_forest_materialized ||
        left->semantic_result_len != right->semantic_result_len ||
        strcmp(left->forest_digest, right->forest_digest) != 0 ||
        strcmp(left->detail, right->detail) != 0 ||
        left->forest.node_len != right->forest.node_len ||
        left->forest.choice_len != right->forest.choice_len ||
        left->forest.root_len != right->forest.root_len ||
        left->forest.expected_terminal_len !=
            right->forest.expected_terminal_len ||
        left->forest.graph_node_len != right->forest.graph_node_len ||
        left->forest.stack_node_len != right->forest.stack_node_len ||
        left->forest.work_item_len != right->forest.work_item_len ||
        left->forest.source_pass_count !=
            right->forest.source_pass_count ||
        left->forest.decoded_byte_len != right->forest.decoded_byte_len) {
        return false;
    }
    for (index = 0u; index < left->semantic_result_len; index++) {
        char *left_text = render(left->semantic_results[index]);
        char *right_text = render(right->semantic_results[index]);
        bool equal = left_text && right_text &&
            strcmp(left_text, right_text) == 0;
        free(left_text);
        free(right_text);
        if (!equal)
            return false;
    }
    return true;
}

static void recursive_replay_gate(TestCounts *counts, Arena *arena) {
    Atom *state_arguments[1] = {atom_symbol(arena, "recursive")};
    Atom *state = app(arena, "pp-rel", 1u, state_arguments);
    Atom *a = terminal_char(arena, 97u);
    Atom *self = nonterminal(arena, state);
    Atom *grow_rhs[2] = {self, a};
    Atom *grow_args[2] = {slot(arena, 0u), slot(arena, 1u)};
    Atom *leaf_rhs[1] = {a};
    Atom *leaf_args[1] = {slot(arena, 0u)};
    Atom *productions[2] = {
        production(arena, "grow", state, grow_rhs, 2u,
                   apply(arena, "grow", grow_args, 2u)),
        production(arena, "leaf", state, leaf_rhs, 1u,
                   apply(arena, "leaf", leaf_args, 1u)),
    };
    PPABIV1Pack pack = {0};
    PPNativeV1Result result;
    static const uint8_t input[] = {'a', 'a', 'a'};
    char error[512] = {0};

    if (!load_pack(arena, productions, 2u, NULL, 0u,
                   &pack, error, sizeof(error))) {
        (void)expect(counts, false, error);
        return;
    }
    ppnative_v1_result_init(&result);
    (void)expect(
        counts,
        ppgll_v1_parse(&pack, state, input, sizeof(input), 10000u,
                       128u, 128u, &result, error, sizeof(error)),
        error[0] ? error : "recursive ParserPack GLL run");
    (void)expect(counts, result.outcome == PPNATIVE_V1_COMPLETED &&
                         result.accepted,
                 "recursive ParserPack accepted");
    (void)expect(counts, result.forest.root_len == 3u &&
                         result.semantic_result_len == 3u,
                 "all prefix roots and results retained");
    (void)expect(
        counts,
        result_has(&result,
                   "(result (leaf (cp 97)) (cons (cp 97) (cons (cp 97) nil)))") &&
        result_has(&result,
                   "(result (grow (leaf (cp 97)) (cp 97)) (cons (cp 97) nil))") &&
        result_has(&result,
                   "(result (grow (grow (leaf (cp 97)) (cp 97)) (cp 97)) nil)"),
        "recursive actions replay exactly");
    (void)expect(counts, result.canonical_forest != NULL &&
                         strlen(result.forest_digest) == 64u &&
                         result.forest.source_pass_count == 1u &&
                         result.forest.decoded_byte_len == sizeof(input),
                 "neutral forest digest and one-pass accounting");
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);
}

static void ambiguity_and_duplicate_slot_gate(TestCounts *counts,
                                               Arena *arena) {
    Atom *state = binary(arena, "pp-def", atom_symbol(arena, "scope"),
                         atom_symbol(arena, "ambiguous"));
    Atom *a = terminal_char(arena, 97u);
    Atom *rhs[1] = {a};
    Atom *left_args[1] = {slot(arena, 0u)};
    Atom *duplicate_args[2] = {slot(arena, 0u), slot(arena, 0u)};
    Atom *productions[2] = {
        production(arena, "left", state, rhs, 1u,
                   apply(arena, "left", left_args, 1u)),
        production(arena, "duplicate", state, rhs, 1u,
                   apply(arena, "duplicate", duplicate_args, 2u)),
    };
    PPABIV1Pack pack;
    PPNativeV1Result result;
    static const uint8_t input[] = {'a'};
    char error[512] = {0};
    uint32_t root;

    if (!load_pack(arena, productions, 2u, NULL, 0u,
                   &pack, error, sizeof(error))) {
        (void)expect(counts, false, error);
        return;
    }
    ppnative_v1_result_init(&result);
    (void)expect(
        counts,
        ppgll_v1_parse(&pack, state, input, sizeof(input), 1000u,
                       64u, 64u, &result, error, sizeof(error)),
        error[0] ? error : "ambiguous ParserPack GLL run");
    root = result.forest.root_len ? result.forest.roots[0] : UINT32_MAX;
    (void)expect(counts, root < result.forest.node_len &&
                         result.forest.nodes[root].choice_len == 2u &&
                         result.semantic_result_len == 2u,
                 "packed ambiguity retained through replay");
    (void)expect(
        counts,
        result_has(&result, "(result (left (cp 97)) nil)") &&
        result_has(&result,
                   "(result (duplicate (cp 97) (cp 97)) nil)"),
        "ordered duplicate semantic slots retained");
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);
}

static void deep_ambiguous_equality_gate(TestCounts *counts, Arena *arena) {
    enum { DEEP_INPUT_LEN = 5000 };
    Atom *root = unary(arena, "pp-def", atom_symbol(arena, "deep-root"));
    Atom *left = unary(arena, "pp-def", atom_symbol(arena, "deep-left"));
    Atom *right = unary(arena, "pp-def", atom_symbol(arena, "deep-right"));
    Atom *different = unary(
        arena, "pp-def", atom_symbol(arena, "deep-different"));
    Atom *a = terminal_char(arena, 97u);
    Atom *eof = unary(
        arena, "pp-terminal", atom_symbol(arena, "pp-terminal-eof"));
    Atom *left_self = nonterminal(arena, left);
    Atom *right_self = nonterminal(arena, right);
    Atom *different_self = nonterminal(arena, different);
    Atom *left_grow_rhs[2] = {left_self, a};
    Atom *right_grow_rhs[2] = {right_self, a};
    Atom *different_grow_rhs[2] = {different_self, a};
    Atom *leaf_rhs[1] = {a};
    Atom *grow_args[2] = {slot(arena, 0u), slot(arena, 1u)};
    Atom *leaf_args[1] = {slot(arena, 0u)};
    Atom *root_left_rhs[2] = {nonterminal(arena, left), eof};
    Atom *root_right_rhs[2] = {nonterminal(arena, right), eof};
    Atom *root_different_rhs[2] = {nonterminal(arena, different), eof};
    Atom *productions[9] = {
        production(arena, "deep-left-grow", left, left_grow_rhs, 2u,
                   apply(arena, "node", grow_args, 2u)),
        production(arena, "deep-left-leaf", left, leaf_rhs, 1u,
                   apply(arena, "leaf", leaf_args, 1u)),
        production(arena, "deep-right-grow", right, right_grow_rhs, 2u,
                   apply(arena, "node", grow_args, 2u)),
        production(arena, "deep-right-leaf", right, leaf_rhs, 1u,
                   apply(arena, "leaf", leaf_args, 1u)),
        production(arena, "deep-different-grow", different,
                   different_grow_rhs, 2u,
                   apply(arena, "node", grow_args, 2u)),
        production(arena, "deep-different-leaf", different,
                   leaf_rhs, 1u,
                   apply(arena, "other-leaf", leaf_args, 1u)),
        production(arena, "deep-root-left", root, root_left_rhs, 2u,
                   slot(arena, 0u)),
        production(arena, "deep-root-right", root, root_right_rhs, 2u,
                   slot(arena, 0u)),
        production(arena, "deep-root-different", root,
                   root_different_rhs, 2u, slot(arena, 0u)),
    };
    PPABIV1Pack pack = {0};
    PPNativeV1Result result;
    uint8_t *input = malloc(DEEP_INPUT_LEN);
    char error[512] = {0};
    uint32_t root_index;
    /* The original 1024-byte-per-scalar ceiling was calibrated with the
       40-byte Atom ABI.  Semantic replay results legitimately contain Atoms;
       scale that linear ceiling when the host Atom ABI grows, while the
       materialized/spare checks below independently reject retained canonical
       scratch. */
    size_t retained_result_limit =
        ((size_t)DEEP_INPUT_LEN * 1024u * sizeof(Atom)) / 40u;

    if (!input) {
        (void)expect(counts, false, "allocate deep ambiguous GLL input");
        return;
    }
    memset(input, 'a', DEEP_INPUT_LEN);
    if (!load_pack(arena, productions, 9u, NULL, 0u,
                   &pack, error, sizeof(error))) {
        (void)expect(counts, false, error);
        free(input);
        return;
    }
    ppnative_v1_result_init(&result);
    (void)expect(
        counts,
        ppgll_v1_parse(&pack, root, input, DEEP_INPUT_LEN,
                       5000000u, 64u, 16u,
                       &result, error, sizeof(error)),
        error[0] ? error : "deep ambiguous ParserPack GLL run");
    root_index = result.forest.root_len == 1u
        ? result.forest.roots[0] : UINT32_MAX;
    (void)expect(
        counts,
        result.outcome == PPNATIVE_V1_COMPLETED && result.accepted &&
        result.semantic_result_len == 2u &&
        root_index < result.forest.node_len &&
        result.forest.nodes[root_index].choice_len == 3u,
        "deep equal GLL alternatives deduplicate without merging inequality");
    (void)expect(
        counts,
        !result.canonical_forest_materialized &&
        result.arena.spare_bytes == 0u &&
        result.arena.live_bytes <= retained_result_limit,
        "large GLL forest canonical scratch is not retained by the result");
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);
    free(input);
}

static void class_and_eof_gate(TestCounts *counts, Arena *arena) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "unicode"));
    Atom *klass = atom_symbol(arena, "not-delimiter");
    Atom *class_item = unary(
        arena, "pp-terminal", unary(arena, "pp-terminal-class", klass));
    Atom *eof_item = unary(
        arena, "pp-terminal", atom_symbol(arena, "pp-terminal-eof"));
    Atom *rhs[2] = {class_item, eof_item};
    Atom *action_args[2] = {slot(arena, 0u), slot(arena, 1u)};
    Atom *productions[1] = {
        production(arena, "unicode", state, rhs, 2u,
                   apply(arena, "pair", action_args, 2u)),
    };
    Atom *excluded = binary(
        arena, "pp-points-cons", cp(arena, 34u),
        binary(arena, "pp-points-cons", cp(arena, 92u),
               atom_symbol(arena, "pp-points-nil")));
    Atom *classes[1] = {
        binary(arena, "pp-class-except", klass, excluded),
    };
    PPABIV1Pack pack;
    PPNativeV1Result result;
    static const uint8_t input[] = {0xceu, 0xbbu};
    static const uint32_t codepoints[] = {955u};
    static const uint32_t byte_offsets[] = {0u, 2u};
    const CettaLpNativeUtf8ScalarView view = {
        codepoints, byte_offsets, 1u, 2u, 0u, 0u,
    };
    char error[512] = {0};

    if (!load_pack(arena, productions, 1u, classes, 1u,
                   &pack, error, sizeof(error))) {
        (void)expect(counts, false, error);
        return;
    }
    ppnative_v1_result_init(&result);
    (void)expect(
        counts,
        ppgll_v1_parse(&pack, state, input, sizeof(input), 1000u,
                       64u, 64u, &result, error, sizeof(error)),
        error[0] ? error : "Unicode class ParserPack GLL run");
    (void)expect(counts, result.accepted &&
                         result.forest.scalar_len == 1u &&
                         result.forest.byte_offsets[1] == 2u,
                 "class matcher preserves scalar/byte spans");
    (void)expect(
        counts,
        result_has(&result, "(result (pair (cp 955) eof) nil)"),
        "class and zero-width EOF values replay");
    ppnative_v1_result_free(&result);

    error[0] = '\0';
    ppnative_v1_result_init(&result);
    (void)expect(
        counts,
        ppgll_v1_parse_scalar_view(
            &pack, state, &view, 1000u, 64u, 64u,
            &result, error, sizeof(error)),
        error[0] ? error : "borrowed ParserPack GLL run");
    (void)expect(
        counts,
        result.accepted && result.forest.source_pass_count == 0u &&
            result.forest.decoded_byte_len == 0u &&
            result_has(&result, "(result (pair (cp 955) eof) nil)"),
        "borrowed ParserPack GLL replays without another decode");
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);
}

static void typed_boundaries_gate(TestCounts *counts, Arena *arena) {
    Atom *state = atom_symbol(arena, "open");
    Atom *missing = atom_symbol(arena, "missing");
    Atom *rhs[1] = {nonterminal(arena, missing)};
    Atom *productions[1] = {
        production(arena, "open-production", state, rhs, 1u,
                   slot(arena, 0u)),
    };
    PPABIV1Pack pack;
    PPNativeV1Result result;
    static const uint8_t invalid_utf8[] = {0xc0u, 0x80u};
    char error[512] = {0};

    if (!load_pack(arena, productions, 1u, NULL, 0u,
                   &pack, error, sizeof(error))) {
        (void)expect(counts, false, error);
        return;
    }
    ppnative_v1_result_init(&result);
    (void)expect(
        counts,
        ppgll_v1_parse(&pack, state, NULL, 0u, 100u, 32u, 32u,
                       &result, error, sizeof(error)) &&
        result.outcome == PPNATIVE_V1_UNSUPPORTED_OPEN_PACK &&
        strstr(result.detail, "undefined") != NULL,
        "open ParserPack is a typed unsupported outcome");
    ppnative_v1_result_free(&result);
    ppabi_v1_pack_free(&pack);

    {
        Atom *closed_state = atom_symbol(arena, "closed");
        Atom *a = terminal_char(arena, 97u);
        Atom *closed_rhs[1] = {a};
        Atom *closed_productions[1] = {
            production(arena, "closed-production", closed_state,
                       closed_rhs, 1u, slot(arena, 0u)),
        };
        if (!load_pack(arena, closed_productions, 1u, NULL, 0u,
                       &pack, error, sizeof(error))) {
            (void)expect(counts, false, error);
            return;
        }
        ppnative_v1_result_init(&result);
        error[0] = '\0';
        (void)expect(
            counts,
            !ppgll_v1_parse(&pack, closed_state, invalid_utf8,
                            sizeof(invalid_utf8), 100u, 32u, 32u,
                            &result, error, sizeof(error)) &&
            strstr(error, "invalid UTF-8") != NULL,
            "invalid UTF-8 fails closed");
        ppnative_v1_result_free(&result);

        ppnative_v1_result_init(&result);
        error[0] = '\0';
        (void)expect(
            counts,
            ppgll_v1_parse(&pack, closed_state,
                           (const uint8_t *)"a", 1u, 1u, 32u, 32u,
                           &result, error, sizeof(error)) &&
            result.outcome == PPNATIVE_V1_RECOGNIZER_LIMIT,
            "descriptor exhaustion remains typed");
        ppnative_v1_result_free(&result);
        ppabi_v1_pack_free(&pack);
    }
}

static void prepared_reuse_gate(TestCounts *counts, Arena *arena) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "prepared"));
    Atom *a = terminal_char(arena, 97u);
    Atom *self = nonterminal(arena, state);
    Atom *grow_rhs[2] = {self, a};
    Atom *grow_args[2] = {slot(arena, 0u), slot(arena, 1u)};
    Atom *leaf_rhs[1] = {a};
    Atom *leaf_args[1] = {slot(arena, 0u)};
    Atom *productions[2] = {
        production(arena, "prepared-grow", state, grow_rhs, 2u,
                   apply(arena, "grow", grow_args, 2u)),
        production(arena, "prepared-leaf", state, leaf_rhs, 1u,
                   apply(arena, "leaf", leaf_args, 1u)),
    };
    static const uint8_t input_one[] = {'a'};
    static const uint8_t input_two[] = {'a', 'a'};
    PPABIV1Pack pack = {0};
    PPNativeV1Prepared prepared;
    PPNativeV1Prepared empty;
    PPNativeV1Result cold_one;
    PPNativeV1Result cold_two;
    PPNativeV1Result warm_one;
    PPNativeV1Result warm_two;
    char saved_digest_char;
    char error[512] = {0};

    ppnative_v1_prepared_init(&prepared);
    ppnative_v1_prepared_init(&empty);
    ppnative_v1_result_init(&cold_one);
    ppnative_v1_result_init(&cold_two);
    ppnative_v1_result_init(&warm_one);
    ppnative_v1_result_init(&warm_two);
    if (!load_pack(arena, productions, 2u, NULL, 0u,
                   &pack, error, sizeof(error))) {
        (void)expect(counts, false, error);
        goto done;
    }
    (void)expect(
        counts,
        ppgll_v1_parse(
            &pack, state, input_one, sizeof(input_one),
            10000u, 128u, 128u, &cold_one, error, sizeof(error)) &&
        ppgll_v1_parse(
            &pack, state, input_two, sizeof(input_two),
            10000u, 128u, 128u, &cold_two, error, sizeof(error)),
        error[0] ? error : "construct cold ParserPack GLL references");
    error[0] = '\0';
    (void)expect(
        counts,
        ppnative_v1_prepare(
            &prepared, &pack, state, error, sizeof(error)) &&
        ppnative_v1_prepared_table_build_count(&prepared) == 1u,
        error[0] ? error : "prepare ParserPack GLL and GLR tables once");
    error[0] = '\0';
    (void)expect(
        counts,
        ppnative_v1_prepare(
            &prepared, &pack, state, error, sizeof(error)) &&
        ppnative_v1_prepared_table_build_count(&prepared) == 1u,
        error[0] ? error : "identical ParserPack prepare is a cache hit");
    error[0] = '\0';
    (void)expect(
        counts,
        ppgll_v1_prepared_parse(
            &prepared, input_one, sizeof(input_one),
            10000u, 128u, 128u, &warm_one, error, sizeof(error)) &&
        ppgll_v1_prepared_parse(
            &prepared, input_two, sizeof(input_two),
            10000u, 128u, 128u, &warm_two, error, sizeof(error)),
        error[0] ? error : "reuse prepared ParserPack GLL tables");
    (void)expect(
        counts,
        results_equal(&cold_one, &warm_one) &&
        results_equal(&cold_two, &warm_two) &&
        ppnative_v1_prepared_table_build_count(&prepared) == 1u,
        "prepared ParserPack GLL results equal cold results exactly");
    error[0] = '\0';
    (void)expect(
        counts,
        !ppgll_v1_prepared_parse(
            &empty, input_one, sizeof(input_one),
            10000u, 128u, 128u, &warm_one, error, sizeof(error)) &&
        strstr(error, "not ready") != NULL,
        "unprepared ParserPack GLL fails closed");
    saved_digest_char = pack.pack_digest[0];
    pack.pack_digest[0] = saved_digest_char == 'f' ? 'e' : 'f';
    error[0] = '\0';
    (void)expect(
        counts,
        !ppgll_v1_prepared_parse(
            &prepared, input_one, sizeof(input_one),
            10000u, 128u, 128u, &warm_one, error, sizeof(error)) &&
        strstr(error, "stale") != NULL,
        "changed ParserPack digest invalidates prepared GLL tables");
    pack.pack_digest[0] = saved_digest_char;
    ppnative_v1_prepared_free(&prepared);
    error[0] = '\0';
    (void)expect(
        counts,
        !ppgll_v1_prepared_parse(
            &prepared, input_one, sizeof(input_one),
            10000u, 128u, 128u, &warm_one, error, sizeof(error)) &&
        strstr(error, "not ready") != NULL,
        "freed ParserPack GLL owner fails closed");
    ppabi_v1_pack_free(&pack);
    memset(&pack, 0, sizeof(pack));

done:
    ppnative_v1_prepared_free(&prepared);
    ppnative_v1_prepared_free(&empty);
    ppnative_v1_result_free(&cold_one);
    ppnative_v1_result_free(&cold_two);
    ppnative_v1_result_free(&warm_one);
    ppnative_v1_result_free(&warm_two);
    ppabi_v1_pack_free(&pack);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    TestCounts counts = {0};

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    recursive_replay_gate(&counts, &arena);
    ambiguity_and_duplicate_slot_gate(&counts, &arena);
    deep_ambiguous_equality_gate(&counts, &arena);
    class_and_eof_gate(&counts, &arena);
    typed_boundaries_gate(&counts, &arena);
    prepared_reuse_gate(&counts, &arena);

    printf("(ParserPackGLLV1NativeSummary %u %u)\n",
           counts.passed, counts.failed);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return counts.failed == 0u ? 0 : 1;
}
