#include "regular_span_nfa_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "regular-span NFA gate failed: %s\n", message); \
            return 1; \
        } \
    } while (0)

static Atom *parse_term(Arena *arena, const char *source) {
    Atom *term = NULL;
    char error[512] = {0};

    if (!fh_ground_term_v1_parse(
            arena, (const uint8_t *)source, strlen(source),
            &term, error, sizeof(error))) {
        fprintf(stderr, "regular-span NFA fixture is invalid: %s\n", error);
        return NULL;
    }
    return term;
}

static bool has_token(const RSNFAV1Plan *nfa,
                      const RSDFAV1ScanResult *result,
                      const char *tag,
                      uint32_t start_scalar,
                      uint32_t end_scalar,
                      uint32_t start_byte,
                      uint32_t end_byte) {
    uint32_t index;

    for (index = 0u; index < result->token_len; index++) {
        const RSDFAV1Token *token = &result->tokens[index];
        if (token->tag < nfa->nfa.tag_len &&
            strcmp(nfa->tag_canonical[token->tag], tag) == 0 &&
            token->start_scalar == start_scalar &&
            token->end_scalar == end_scalar &&
            token->start_byte == start_byte &&
            token->end_byte == end_byte) {
            return true;
        }
    }
    return false;
}

static bool expect_rejected(const PPABIV1Pack *pack,
                            Atom *const *terms,
                            size_t term_len,
                            const char *label) {
    RSNFAV1Plan plan;
    char error[512] = {0};
    bool admitted;

    rsnfa_v1_plan_init(&plan);
    admitted = rsnfa_v1_plan_load(
        pack, terms, term_len, &plan, error, sizeof(error));
    rsnfa_v1_plan_free(&plan);
    if (admitted || error[0] == '\0') {
        fprintf(stderr,
                "regular-span NFA gate accepted %s without a diagnostic\n",
                label);
        return false;
    }
    return true;
}

int main(void) {
    static const char *positive_sources[] = {
        "(compile-span-nfa alpha (nfa-accept alpha-out alpha))",
        "(compile-span-nfa alpha (nfa-class alpha-in letters alpha-out))",
        "(compile-span-nfa alpha (nfa-start alpha-in))",
        "(compile-span-nfa any-tag (nfa-accept any-out any-tag))",
        "(compile-span-nfa any-tag (nfa-any any-in any-out))",
        "(compile-span-nfa any-tag (nfa-start any-in))",
        "(compile-span-nfa lambda-eof (nfa-accept lambda-done lambda-eof))",
        "(compile-span-nfa lambda-eof (nfa-char lambda-in (cp 955) lambda-read))",
        "(compile-span-nfa lambda-eof (nfa-eof lambda-read lambda-done))",
        "(compile-span-nfa lambda-eof (nfa-start lambda-in))",
        "(compile-span-nfa nullable (nfa-accept nullable-out nullable))",
        "(compile-span-nfa nullable (nfa-epsilon nullable-in nullable-out))",
        "(compile-span-nfa nullable (nfa-start nullable-in))",
    };
    static const uint8_t input[] = {'a', 0xceu, 0xbbu};
    SymbolTable symbols;
    Arena arena;
    PPABIV1Pack class_pack;
    RSNFAV1Plan nfa_plan;
    RSDFAV1Plan dfa_plan;
    RSDFAV1ScanResult result;
    RSDFAV1BuildOutcome build_outcome;
    Atom *positive[sizeof(positive_sources) / sizeof(positive_sources[0])];
    char error[512] = {0};
    uint32_t checks = 0u;
    size_t index;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    ppabi_v1_pack_init(&class_pack);
    rsnfa_v1_plan_init(&nfa_plan);
    rsdfa_v1_plan_init(&dfa_plan);
    rsdfa_v1_scan_result_init(&result);

    class_pack.class_clauses = calloc(
        1u, sizeof(*class_pack.class_clauses));
    CHECK(class_pack.class_clauses != NULL,
          "failed to allocate class fixture");
    class_pack.class_clause_len = 1u;
    class_pack.class_clauses[0].class_identity =
        atom_symbol(&arena, "letters");
    class_pack.class_clauses[0].kind = PPABI_V1_CLASS_POINT;
    class_pack.class_clauses[0].point = 97u;

    for (index = 0u;
         index < sizeof(positive_sources) / sizeof(positive_sources[0]);
         index++) {
        positive[index] = parse_term(&arena, positive_sources[index]);
        CHECK(positive[index] != NULL, "failed to parse positive fixture");
    }
    CHECK(rsnfa_v1_plan_load(
              &class_pack, positive,
              sizeof(positive) / sizeof(positive[0]),
              &nfa_plan, error, sizeof(error)),
          error);
    CHECK(nfa_plan.nfa.tag_len == 4u &&
          nfa_plan.nfa.state_len == 9u &&
          nfa_plan.nfa.start_len == 4u &&
          nfa_plan.nfa.edge_len == 5u &&
          nfa_plan.nfa.accept_len == 4u,
          "canonical NFA dimensions changed");
    checks += 2u;

    CHECK(rsdfa_v1_plan_build(
              &nfa_plan.nfa, 128u, 1024u,
              &dfa_plan, &build_outcome,
              error, sizeof(error)),
          error);
    CHECK(build_outcome == RSDFA_V1_BUILD_COMPLETED,
          "adapted NFA did not determinize");
    CHECK(rsdfa_v1_scan(
              &dfa_plan, input, sizeof(input), 1000u, 100u,
              &result, error, sizeof(error)),
          error);
    if (result.outcome != RSDFA_V1_SCAN_COMPLETED ||
        result.source_pass_count != 1u || result.token_len != 7u) {
        fprintf(stderr,
                "adapted NFA receipt: outcome=%u source-passes=%u "
                "tokens=%u scalars=%u bytes=%u decoded=%u work=%llu\n",
                (unsigned)result.outcome, result.source_pass_count,
                result.token_len, result.input_scalar_len,
                result.input_byte_len, result.decoded_byte_len,
                (unsigned long long)result.work_item_len);
    }
    CHECK(result.outcome == RSDFA_V1_SCAN_COMPLETED &&
          result.source_pass_count == 1u &&
          result.token_len == 7u,
          "adapted NFA token lattice changed");
    CHECK(has_token(&nfa_plan, &result, "alpha", 0u, 1u, 0u, 1u),
          "class-derived token is absent");
    CHECK(has_token(
              &nfa_plan, &result, "lambda-eof", 1u, 2u, 1u, 3u),
          "Unicode EOF-derived token is absent");
    CHECK(has_token(&nfa_plan, &result, "nullable", 2u, 2u, 3u, 3u),
          "nullable token is absent");
    CHECK(has_token(&nfa_plan, &result, "any-tag", 1u, 2u, 1u, 3u),
          "Unicode any-token is absent");
    checks += 7u;

    {
        Atom *duplicate[] = {positive[0], positive[0]};
        CHECK(expect_rejected(
                  &class_pack, duplicate, 2u, "duplicate answer"),
              "duplicate-answer boundary");
        checks++;
    }
    {
        Atom *terms[] = {
            parse_term(&arena, "(compile-span-nfa bad (nfa-start bad-in))"),
            parse_term(
                &arena,
                "(compile-span-nfa bad (nfa-accept bad-in other-tag))"),
        };
        CHECK(terms[0] && terms[1] && expect_rejected(
                  &class_pack, terms, 2u, "mismatched accept tag"),
              "accept-tag boundary");
        checks++;
    }
    {
        PPABIV1Pack empty_pack;
        Atom *terms[] = {positive[0], positive[1], positive[2]};
        ppabi_v1_pack_init(&empty_pack);
        CHECK(expect_rejected(
                  &empty_pack, terms, 3u, "undefined class"),
              "undefined-class boundary");
        ppabi_v1_pack_free(&empty_pack);
        checks++;
    }
    {
        Atom *terms[] = {
            parse_term(
                &arena,
                "(compile-span-nfa open (nfa-char open-in (cp 97) open-out))"),
            parse_term(&arena, "(compile-span-nfa open (nfa-start open-in))"),
        };
        CHECK(terms[0] && terms[1] && expect_rejected(
                  &class_pack, terms, 2u, "missing accept"),
              "missing-endpoint boundary");
        checks++;
    }
    {
        Atom *terms[] = {
            parse_term(
                &arena,
                "(compile-span-nfa scalar (nfa-accept scalar-out scalar))"),
            parse_term(
                &arena,
                "(compile-span-nfa scalar (nfa-char scalar-in (cp 55296) scalar-out))"),
            parse_term(
                &arena,
                "(compile-span-nfa scalar (nfa-start scalar-in))"),
        };
        CHECK(terms[0] && terms[1] && terms[2] && expect_rejected(
                  &class_pack, terms, 3u, "surrogate character"),
              "Unicode-scalar boundary");
        checks++;
    }

    rsdfa_v1_scan_result_free(&result);
    rsdfa_v1_plan_free(&dfa_plan);
    rsnfa_v1_plan_free(&nfa_plan);
    ppabi_v1_pack_free(&class_pack);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(RegularSpanNFAV1NativeSummary %u 0)\n", checks);
    return 0;
}
