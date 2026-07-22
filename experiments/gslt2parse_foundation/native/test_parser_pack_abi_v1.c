#include "parser_pack_abi_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s\n", (message)); \
            return false; \
        } \
    } while (0)

static const char DIGEST_A[] =
    "0000000000000000000000000000000000000000000000000000000000000001";
static const char DIGEST_B[] =
    "0000000000000000000000000000000000000000000000000000000000000002";
static const char DIGEST_C[] =
    "0000000000000000000000000000000000000000000000000000000000000003";
static const char PACK_DIGEST[] =
    "0ed7e672d22c4712475b16955ed8a47cc1c624161fd1d30969820e40791cab1d";

static Atom *app(Arena *arena, const char *head,
                 size_t argument_len, Atom **arguments) {
    Atom **elements = (Atom **)malloc(
        (argument_len + 1u) * sizeof(*elements));
    Atom *result;
    size_t index;

    if (!elements)
        return NULL;
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr(arena, elements, argument_len + 1u);
    free(elements);
    return result;
}

static Atom *unary(Arena *arena, const char *head, Atom *argument) {
    Atom *arguments[1] = {argument};
    return app(arena, head, 1u, arguments);
}

static Atom *binary(Arena *arena, const char *head,
                    Atom *left, Atom *right) {
    Atom *arguments[2] = {left, right};
    return app(arena, head, 2u, arguments);
}

static Atom *cp(Arena *arena, uint32_t value) {
    return unary(arena, "cp", atom_int(arena, (int64_t)value));
}

static Atom *items_cons(Arena *arena, Atom *item, Atom *more) {
    return binary(arena, "pp-items-cons", item, more);
}

static Atom *pa_cons(Arena *arena, Atom *action, Atom *more) {
    return binary(arena, "pa-cons", action, more);
}

static Atom *make_production(Arena *arena,
                             Atom *label,
                             Atom *state,
                             Atom *items,
                             Atom *action) {
    Atom *arguments[4] = {label, state, items, action};
    return app(arena, "pp-production", 4u, arguments);
}

static Atom *make_answer(Arena *arena,
                         PPABIV1EvidenceKind kind,
                         Atom *artifact) {
    const char *relation = kind == PPABI_V1_EVIDENCE_PRODUCTION
        ? "compile-pack-production" : "compile-pack-class-clause";
    Atom *arguments[2] = {atom_symbol(arena, "owner"), artifact};
    return app(arena, relation, 2u, arguments);
}

static Atom *make_certificate(Arena *arena, const char *rule) {
    Atom *children = atom_expr(arena, NULL, 0u);
    Atom *arguments[2] = {atom_symbol(arena, rule), children};
    return app(arena, "cert", 2u, arguments);
}

static PPABIV1ProvenanceInput provenance(
    const PPABIV1DerivationInput *derivations,
    size_t derivation_len) {
    PPABIV1ProvenanceInput input = {
        .source_digest = DIGEST_A,
        .compiler_digest = DIGEST_B,
        .environment_digest = DIGEST_C,
        .derivations = derivations,
        .derivation_len = derivation_len,
    };
    return input;
}

static bool codec_rejects(Arena *arena,
                          const uint8_t *bytes,
                          size_t len,
                          const char *label) {
    Atom *term = NULL;
    char error[512] = {0};

    if (fh_ground_term_v1_parse(
            arena, bytes, len, &term, error, sizeof(error)) ||
        term != NULL || error[0] == '\0') {
        fprintf(stderr, "FAIL: codec accepted %s\n", label);
        return false;
    }
    return true;
}

static bool ground_term_codec_gate(Arena *arena, size_t *passed) {
    enum { DEEP_CODEC_DEPTH = 8192 };
    static const uint8_t canonical[] =
        "(wire \"λ"
        "\\n\\b\\f\\t\\r"
        "\\\""
        "\\\\"
        "\\u001f"
        "\" -9223372036854775809 ())";
    static const uint8_t leading_zero[] = "(wire 02)";
    static const uint8_t escaped_scalar[] = "\"\\u03bb\"";
    static const uint8_t embedded_nul[] = "\"\\u0000\"";
    static const uint8_t unpaired_surrogate[] = "\"\\ud800\"";
    static const uint8_t invalid_utf8[] = {0x22u, 0xc0u, 0x80u, 0x22u};
    Atom *term = NULL;
    uint8_t *rendered = NULL;
    uint8_t *rerendered = NULL;
    size_t rendered_len = 0u;
    size_t rerendered_len = 0u;
    size_t depth;
    char error[512] = {0};

    CHECK(fh_ground_term_v1_parse(
              arena, canonical, sizeof(canonical) - 1u,
              &term, error, sizeof(error)),
          error);
    CHECK(fh_ground_term_v1_render(
              term, &rendered, &rendered_len, error, sizeof(error)),
          error);
    CHECK(rendered_len == sizeof(canonical) - 1u &&
          memcmp(rendered, canonical, rendered_len) == 0,
          "canonical finite-Horn codec changed bytes");
    free(rendered);
    (*passed)++;

    CHECK(codec_rejects(
              arena, leading_zero, sizeof(leading_zero) - 1u,
              "noncanonical integer"),
          "leading-zero codec gate");
    CHECK(codec_rejects(
              arena, escaped_scalar, sizeof(escaped_scalar) - 1u,
              "noncanonical escaped scalar"),
          "escaped-scalar codec gate");
    CHECK(codec_rejects(
              arena, embedded_nul, sizeof(embedded_nul) - 1u,
              "embedded U+0000"),
          "NUL codec boundary");
    CHECK(codec_rejects(
              arena, unpaired_surrogate, sizeof(unpaired_surrogate) - 1u,
              "unpaired surrogate"),
          "surrogate codec gate");
    CHECK(codec_rejects(
              arena, invalid_utf8, sizeof(invalid_utf8),
              "invalid UTF-8"),
          "UTF-8 codec gate");
    (*passed) += 5u;

    rendered = NULL;
    rendered_len = 0u;
    CHECK(!fh_ground_term_v1_render(
              atom_symbol(arena, "2"), &rendered, &rendered_len,
              error, sizeof(error)) && rendered == NULL,
          "integer-shaped symbol crossed canonical boundary");
    CHECK(!fh_ground_term_v1_render(
              atom_symbol(arena, "?x"), &rendered, &rendered_len,
              error, sizeof(error)) && rendered == NULL,
          "variable-shaped symbol crossed canonical boundary");
    (*passed) += 2u;

    term = atom_symbol(arena, "leaf");
    for (depth = 0u; depth < DEEP_CODEC_DEPTH; depth++)
        term = unary(arena, "node", term);
    CHECK(fh_ground_term_v1_render(
              term, &rendered, &rendered_len, error, sizeof(error)),
          error);
    term = NULL;
    CHECK(fh_ground_term_v1_parse(
              arena, rendered, rendered_len,
              &term, error, sizeof(error)),
          error);
    CHECK(fh_ground_term_v1_render(
              term, &rerendered, &rerendered_len, error, sizeof(error)),
          error);
    CHECK(rerendered_len == rendered_len &&
          memcmp(rerendered, rendered, rendered_len) == 0,
          "deep finite-Horn codec changed canonical bytes");
    free(rerendered);
    rerendered = NULL;
    (*passed)++;

    CHECK(rendered_len > 0u && codec_rejects(
              arena, rendered, rendered_len - 1u,
              "deep unclosed expression"),
          "deep unclosed codec gate");
    free(rendered);
    rendered = NULL;
    rendered_len = 0u;
    (*passed)++;

    {
        Atom *cycle = atom_expr2(
            arena, atom_symbol(arena, "cycle"),
            atom_symbol(arena, "placeholder"));
        cycle->expr.elems[1] = cycle;
        CHECK(!fh_ground_term_v1_render(
                  cycle, &rendered, &rendered_len,
                  error, sizeof(error)) &&
              rendered == NULL && strstr(error, "cyclic") != NULL,
              "cyclic finite-Horn term was not rejected");
    }
    (*passed)++;
    return true;
}

static bool expect_rejected(Atom *const *productions,
                            size_t production_len,
                            Atom *const *classes,
                            size_t class_len,
                            const PPABIV1ProvenanceInput *input,
                            const char *label) {
    PPABIV1Pack pack;
    char error[512] = {0};
    bool admitted;

    ppabi_v1_pack_init(&pack);
    admitted = ppabi_v1_pack_load(
        &pack, productions, production_len, classes, class_len,
        input, error, sizeof(error));
    ppabi_v1_pack_free(&pack);
    if (admitted || error[0] == '\0') {
        fprintf(stderr, "FAIL: %s was not rejected with a diagnostic\n", label);
        return false;
    }
    return true;
}

static bool main_positive_gate(Arena *arena, size_t *passed) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "root"));
    Atom *label = binary(arena, "pp-label", state,
                         atom_symbol(arena, "root-production"));
    Atom *klass = atom_symbol(arena, "unicode-class");
    Atom *class_matcher = unary(arena, "pp-terminal-class", klass);
    Atom *class_item = unary(
        arena, "pp-terminal", class_matcher);
    Atom *eof_item = unary(
        arena, "pp-terminal", atom_symbol(arena, "pp-terminal-eof"));
    Atom *items = items_cons(
        arena, class_item,
        items_cons(arena, eof_item, atom_symbol(arena, "pp-items-nil")));
    Atom *slot_zero = unary(
        arena, "pa-slot", atom_symbol(arena, "q-zero"));
    Atom *constant = unary(
        arena, "pa-const", atom_string(arena, "λ\n"));
    Atom *action_arguments = pa_cons(
        arena, slot_zero,
        pa_cons(arena, constant, atom_symbol(arena, "pa-nil")));
    Atom *action = binary(
        arena, "pa-apply", atom_symbol(arena, "pair"), action_arguments);
    Atom *production = make_production(arena, label, state, items, action);
    Atom *points = binary(
        arena, "pp-points-cons", cp(arena, 34u),
        binary(arena, "pp-points-cons", cp(arena, 92u),
               atom_symbol(arena, "pp-points-nil")));
    Atom *class_clause =
        binary(arena, "pp-class-except", klass, points);
    Atom *productions[1] = {production};
    Atom *classes[1] = {class_clause};
    PPABIV1DerivationInput roots[2] = {
        {
            .kind = PPABI_V1_EVIDENCE_PRODUCTION,
            .artifact = production,
            .answer = make_answer(
                arena, PPABI_V1_EVIDENCE_PRODUCTION, production),
            .certificate = make_certificate(arena, "compile-production"),
        },
        {
            .kind = PPABI_V1_EVIDENCE_CLASS,
            .artifact = class_clause,
            .answer = make_answer(
                arena, PPABI_V1_EVIDENCE_CLASS, class_clause),
            .certificate = make_certificate(arena, "compile-class"),
        },
    };
    PPABIV1ProvenanceInput input = provenance(roots, 2u);
    PPABIV1Pack pack;
    char error[512] = {0};

    ppabi_v1_pack_init(&pack);
    CHECK(ppabi_v1_pack_load(
              &pack, productions, 1u, classes, 1u,
              &input, error, sizeof(error)),
          error);
    CHECK(pack.production_len == 1u, "production count changed");
    CHECK(pack.class_clause_len == 1u, "class count changed");
    CHECK(pack.state_len == 1u, "state count changed");
    CHECK(pack.terminal_len == 2u, "terminal count changed");
    CHECK(pack.derivation_len == 2u, "derivation-root count changed");
    CHECK(pack.productions[0].item_len == 2u,
          "recognition projection item count changed");
    CHECK(atom_eq(pack.productions[0].action, action),
          "semantic action was not retained separately");
    CHECK(strcmp(pack.source_digest, DIGEST_A) == 0 &&
          strcmp(pack.compiler_digest, DIGEST_B) == 0 &&
          strcmp(pack.environment_digest, DIGEST_C) == 0,
          "provenance digests changed");
    CHECK(ppabi_v1_pack_start_is_closed(
              &pack, state, error, sizeof(error)),
          error);
    CHECK(strcmp(pack.pack_digest, PACK_DIGEST) == 0,
          "canonical ParserPack digest changed");
    ppabi_v1_pack_free(&pack);
    (*passed)++;
    return true;
}

static bool ordering_gate(Arena *arena, size_t *passed) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "order-root"));
    Atom *label = binary(arena, "pp-label", state,
                         atom_symbol(arena, "order-production"));
    Atom *any_item = unary(
        arena, "pp-terminal", atom_symbol(arena, "pp-terminal-any"));
    Atom *items = items_cons(
        arena, any_item, atom_symbol(arena, "pp-items-nil"));
    Atom *action = unary(
        arena, "pa-slot", atom_symbol(arena, "q-zero"));
    Atom *production = make_production(arena, label, state, items, action);
    Atom *class_ten = binary(
        arena, "pp-class-point", atom_symbol(arena, "digits"), cp(arena, 10u));
    Atom *class_two = binary(
        arena, "pp-class-point", atom_symbol(arena, "digits"), cp(arena, 2u));
    Atom *productions[1] = {production};
    Atom *canonical_classes[2] = {class_ten, class_two};
    Atom *reversed_classes[2] = {class_two, class_ten};
    PPABIV1DerivationInput roots[3] = {
        {
            .kind = PPABI_V1_EVIDENCE_PRODUCTION,
            .artifact = production,
            .answer = make_answer(
                arena, PPABI_V1_EVIDENCE_PRODUCTION, production),
            .certificate = make_certificate(arena, "order-production"),
        },
        {
            .kind = PPABI_V1_EVIDENCE_CLASS,
            .artifact = class_ten,
            .answer = make_answer(
                arena, PPABI_V1_EVIDENCE_CLASS, class_ten),
            .certificate = make_certificate(arena, "class-ten"),
        },
        {
            .kind = PPABI_V1_EVIDENCE_CLASS,
            .artifact = class_two,
            .answer = make_answer(
                arena, PPABI_V1_EVIDENCE_CLASS, class_two),
            .certificate = make_certificate(arena, "class-two"),
        },
    };
    PPABIV1ProvenanceInput input = provenance(roots, 3u);
    PPABIV1Pack pack;
    char error[512] = {0};

    ppabi_v1_pack_init(&pack);
    CHECK(ppabi_v1_pack_load(
              &pack, productions, 1u, canonical_classes, 2u,
              &input, error, sizeof(error)),
          error);
    ppabi_v1_pack_free(&pack);
    CHECK(expect_rejected(
              productions, 1u, reversed_classes, 2u, &input,
              "host-term-order class stream"),
          "reversed class stream");
    (*passed) += 2u;
    return true;
}

static bool corruption_gates(Arena *arena, size_t *passed) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "corrupt-root"));
    Atom *label = binary(arena, "pp-label", state,
                         atom_symbol(arena, "corrupt-production"));
    Atom *any_item = unary(
        arena, "pp-terminal", atom_symbol(arena, "pp-terminal-any"));
    Atom *items = items_cons(
        arena, any_item, atom_symbol(arena, "pp-items-nil"));
    Atom *action = unary(
        arena, "pa-slot", atom_symbol(arena, "q-zero"));
    Atom *production = make_production(arena, label, state, items, action);
    Atom *productions[1] = {production};
    Atom *duplicates[2] = {production, production};
    PPABIV1DerivationInput root = {
        .kind = PPABI_V1_EVIDENCE_PRODUCTION,
        .artifact = production,
        .answer = make_answer(
            arena, PPABI_V1_EVIDENCE_PRODUCTION, production),
        .certificate = make_certificate(arena, "corrupt-production"),
    };
    PPABIV1ProvenanceInput input = provenance(&root, 1u);
    Atom *bad_slot = unary(
        arena, "pa-slot",
        unary(arena, "q-succ", atom_symbol(arena, "q-zero")));
    Atom *bad_production =
        make_production(arena, label, state, items, bad_slot);
    Atom *bad_productions[1] = {bad_production};
    PPABIV1DerivationInput bad_root = {
        .kind = PPABI_V1_EVIDENCE_PRODUCTION,
        .artifact = bad_production,
        .answer = make_answer(
            arena, PPABI_V1_EVIDENCE_PRODUCTION, bad_production),
        .certificate = make_certificate(arena, "bad-slot"),
    };
    PPABIV1ProvenanceInput bad_input = provenance(&bad_root, 1u);
    PPABIV1ProvenanceInput missing_evidence = provenance(NULL, 0u);
    PPABIV1ProvenanceInput bad_digest = input;

    CHECK(expect_rejected(
              duplicates, 2u, NULL, 0u, &input,
              "duplicate production stream"),
          "duplicate stream gate");
    CHECK(expect_rejected(
              bad_productions, 1u, NULL, 0u, &bad_input,
              "out-of-range action slot"),
          "bad action gate");
    CHECK(expect_rejected(
              productions, 1u, NULL, 0u, &missing_evidence,
              "missing derivation evidence"),
          "missing evidence gate");
    bad_digest.source_digest = "ABC";
    CHECK(expect_rejected(
              productions, 1u, NULL, 0u, &bad_digest,
              "invalid provenance digest"),
          "digest gate");
    (*passed) += 4u;
    return true;
}

static bool undefined_class_gate(Arena *arena, size_t *passed) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "class-root"));
    Atom *label = binary(arena, "pp-label", state,
                         atom_symbol(arena, "class-production"));
    Atom *matcher = unary(
        arena, "pp-terminal-class", atom_symbol(arena, "missing-class"));
    Atom *item = unary(arena, "pp-terminal", matcher);
    Atom *items = items_cons(
        arena, item, atom_symbol(arena, "pp-items-nil"));
    Atom *action = unary(
        arena, "pa-slot", atom_symbol(arena, "q-zero"));
    Atom *production = make_production(arena, label, state, items, action);
    Atom *productions[1] = {production};
    PPABIV1DerivationInput root = {
        .kind = PPABI_V1_EVIDENCE_PRODUCTION,
        .artifact = production,
        .answer = make_answer(
            arena, PPABI_V1_EVIDENCE_PRODUCTION, production),
        .certificate = make_certificate(arena, "missing-class"),
    };
    PPABIV1ProvenanceInput input = provenance(&root, 1u);

    CHECK(expect_rejected(
              productions, 1u, NULL, 0u, &input,
              "undefined terminal class"),
          "undefined class gate");
    (*passed)++;
    return true;
}

static bool open_state_gate(Arena *arena, size_t *passed) {
    Atom *state = unary(arena, "pp-def", atom_symbol(arena, "open-root"));
    Atom *missing = binary(
        arena, "pp-sub", state, atom_symbol(arena, "tail"));
    Atom *label = binary(arena, "pp-label", state,
                         atom_symbol(arena, "open-production"));
    Atom *item = unary(arena, "pp-nonterminal", missing);
    Atom *items = items_cons(
        arena, item, atom_symbol(arena, "pp-items-nil"));
    Atom *action = unary(
        arena, "pa-slot", atom_symbol(arena, "q-zero"));
    Atom *production = make_production(arena, label, state, items, action);
    Atom *productions[1] = {production};
    PPABIV1DerivationInput root = {
        .kind = PPABI_V1_EVIDENCE_PRODUCTION,
        .artifact = production,
        .answer = make_answer(
            arena, PPABI_V1_EVIDENCE_PRODUCTION, production),
        .certificate = make_certificate(arena, "open-production"),
    };
    PPABIV1ProvenanceInput input = provenance(&root, 1u);
    PPABIV1Pack pack;
    char error[512] = {0};

    ppabi_v1_pack_init(&pack);
    CHECK(ppabi_v1_pack_load(
              &pack, productions, 1u, NULL, 0u,
              &input, error, sizeof(error)),
          error);
    CHECK(!ppabi_v1_pack_start_is_closed(
              &pack, state, error, sizeof(error)) &&
          error[0] != '\0',
          "reachable undefined state was not rejected");
    ppabi_v1_pack_free(&pack);
    (*passed)++;
    return true;
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    size_t passed = 0u;
    bool ok;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);

    ok = ground_term_codec_gate(&arena, &passed) &&
         main_positive_gate(&arena, &passed) &&
         ordering_gate(&arena, &passed) &&
         corruption_gates(&arena, &passed) &&
         undefined_class_gate(&arena, &passed) &&
         open_state_gate(&arena, &passed);

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    if (!ok)
        return 1;
    printf("(ParserPackABIV1NativeSummary %zu 0)\n", passed);
    return 0;
}
