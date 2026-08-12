#include "../../src/gslt_literal_hole_program_v1.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t checks;
static uint32_t failures;

static void expect_true(int condition, const char *message) {
    checks++;
    if (!condition) {
        failures++;
        fprintf(stderr, "FAIL: %s\n", message);
    }
}

typedef struct {
    const uint32_t *items[3];
    uint32_t lengths[3];
} LookupFixture;

static bool lookup(void *context, uint32_t hole,
                   const uint32_t **items_out, uint32_t *len_out) {
    const LookupFixture *fixture = context;
    if (!fixture || !items_out || !len_out || hole >= 3u)
        return false;
    *items_out = fixture->items[hole];
    *len_out = fixture->lengths[hole];
    return true;
}

int main(void) {
    CettaGsltLiteralHoleProgramV1 program;
    CettaGsltLiteralHoleProgramV1 unmerged;
    CettaGsltLiteralHeadProgramV1 headed;
    CettaGsltLiteralHeadProgramV1 leading_hole;
    const uint32_t first[] = {20u, 21u};
    const uint32_t second[] = {30u};
    const uint32_t expected[] = {
        10u, 11u, 12u, 20u, 21u, 13u, 30u, 14u, 14u,
    };
    const uint32_t mismatch[] = {
        10u, 11u, 99u, 20u, 21u, 13u, 30u, 14u, 14u,
    };
    uint32_t output[9] = {0};
    uint32_t measured = 0u;
    LookupFixture fixture = {
        .items = {first, second, NULL},
        .lengths = {2u, 1u, 0u},
    };
    uint32_t unused_literal_items[] = {9u};
    CettaGsltLiteralHolePartV1 inline_part[] = {{8u, 1u}};
    CettaGsltLiteralHoleProgramV1 unused_literal_pool = {
        .parts = inline_part,
        .part_len = 1u,
        .part_cap = 1u,
        .literal_items = unused_literal_items,
        .literal_len = 1u,
        .literal_cap = 1u,
        .cell_len = 1u,
    };
    uint32_t overlapping_literal_items[] = {4u, 5u};
    CettaGsltLiteralHolePartV1 overlapping_parts[] = {
        {0u, 2u}, {0u, 2u},
    };
    CettaGsltLiteralHoleProgramV1 overlapping_runs = {
        .parts = overlapping_parts,
        .part_len = 2u,
        .part_cap = 2u,
        .literal_items = overlapping_literal_items,
        .literal_len = 2u,
        .literal_cap = 2u,
        .cell_len = 4u,
    };

    cetta_gslt_literal_hole_program_init_v1(&program);
    cetta_gslt_literal_hole_program_init_v1(&unmerged);
    cetta_gslt_literal_head_program_init_v1(&headed);
    cetta_gslt_literal_head_program_init_v1(&leading_hole);
    expect_true(cetta_gslt_literal_hole_program_validate_v1(&program),
                "empty program is valid");
    expect_true(cetta_gslt_literal_hole_program_append_literal_v1(
                    &program, 10u, true) &&
                    cetta_gslt_literal_hole_program_append_literal_v1(
                        &program, 11u, true) &&
                    cetta_gslt_literal_hole_program_append_literal_v1(
                        &program, 12u, true),
                "adjacent literals append");
    expect_true(program.part_len == 1u && program.literal_len == 3u &&
                    program.parts[0].literal_len == 3u,
                "adjacent literals form one run");
    expect_true(cetta_gslt_literal_hole_program_append_hole_v1(
                    &program, 0u) &&
                    cetta_gslt_literal_hole_program_append_literal_v1(
                        &program, 13u, true) &&
                    cetta_gslt_literal_hole_program_append_hole_v1(
                        &program, 1u) &&
                    cetta_gslt_literal_hole_program_append_literal_v1(
                        &program, 14u, true) &&
                    cetta_gslt_literal_hole_program_append_literal_v1(
                        &program, 14u, true),
                "holes and duplicate literal occurrences append");
    expect_true(cetta_gslt_literal_hole_program_validate_v1(&program),
                "mixed program validates");
    expect_true(program.cell_len == 8u && program.part_len == 5u,
                "coalescing reduces dispatch parts");
    expect_true(cetta_gslt_literal_hole_program_measure_v1(
                    &program, lookup, &fixture, &measured) &&
                    measured == 9u,
                "instantiation length is exact");
    expect_true(cetta_gslt_literal_hole_program_write_v1(
                    &program, lookup, &fixture, output, measured) &&
                    memcmp(output, expected, sizeof(expected)) == 0,
                "proof-like instantiation is exact");
    expect_true(cetta_gslt_literal_hole_program_match_v1(
                    &program, lookup, &fixture, expected, 9u),
                "proof-like matching accepts the exact expansion");
    expect_true(!cetta_gslt_literal_hole_program_match_v1(
                    &program, lookup, &fixture, mismatch, 9u),
                "literal mismatch is rejected");
    expect_true(!cetta_gslt_literal_hole_program_match_v1(
                    &program, lookup, &fixture, expected, 8u),
                "short input is rejected");
    expect_true(!cetta_gslt_literal_hole_program_measure_v1(
                    &program, lookup, NULL, &measured),
                "missing environment is rejected");
    expect_true(!cetta_gslt_literal_hole_program_write_v1(
                    &program, lookup, &fixture, output, 8u),
                "short output is rejected");

    expect_true(cetta_gslt_literal_hole_program_append_literal_v1(
                    &unmerged, 7u, false) &&
                    cetta_gslt_literal_hole_program_append_literal_v1(
                        &unmerged, 8u, false) &&
                    cetta_gslt_literal_hole_program_append_hole_v1(
                        &unmerged, 2u),
                "parser-like unmerged cells append");
    expect_true(unmerged.part_len == 3u && unmerged.literal_len == 0u &&
                    cetta_gslt_literal_hole_program_validate_v1(&unmerged),
                "unadmitted representation retains cell dispatch");
    expect_true(!cetta_gslt_literal_hole_program_validate_v1(
                    &unused_literal_pool),
                "unused literal-pool cells fail validation");
    expect_true(!cetta_gslt_literal_hole_program_validate_v1(
                    &overlapping_runs),
                "overlapping literal runs fail validation");
    {
        const uint32_t parser_expected[] = {7u, 8u};
        expect_true(cetta_gslt_literal_hole_program_match_v1(
                        &unmerged, lookup, &fixture,
                        parser_expected, 2u),
                    "empty parser-like reference is preserved");
    }

    expect_true(!cetta_gslt_literal_hole_program_append_hole_v1(
                    NULL, 0u) &&
                    !cetta_gslt_literal_hole_program_append_literal_v1(
                        NULL, 0u, true),
                "null builders fail closed");

    {
        const uint32_t headed_expected[] = {
            42u, 10u, 11u, 20u, 21u,
        };
        const uint32_t headed_mismatch[] = {
            41u, 10u, 11u, 20u, 21u,
        };
        uint32_t headed_output[5] = {0};
        expect_true(cetta_gslt_literal_head_program_set_head_v1(
                        &headed, 42u) &&
                        cetta_gslt_literal_head_program_append_literal_v1(
                            &headed, 10u, true) &&
                        cetta_gslt_literal_head_program_append_literal_v1(
                            &headed, 11u, true) &&
                        cetta_gslt_literal_head_program_append_hole_v1(
                            &headed, 0u) &&
                        cetta_gslt_literal_head_program_validate_v1(
                            &headed),
                    "fixed literal head and residual program validate");
        expect_true(headed.has_head &&
                        headed.residual.cell_len == 3u &&
                        headed.residual.part_len == 2u,
                    "fixed head is absent from residual dispatch");
        expect_true(cetta_gslt_literal_head_program_measure_v1(
                        &headed, lookup, &fixture, &measured) &&
                        measured == 5u &&
                        cetta_gslt_literal_head_program_write_v1(
                            &headed, lookup, &fixture,
                            headed_output, measured) &&
                        memcmp(headed_output, headed_expected,
                               sizeof(headed_expected)) == 0,
                    "headed instantiation is exact");
        expect_true(cetta_gslt_literal_head_program_match_v1(
                        &headed, lookup, &fixture,
                        headed_expected, 5u) &&
                        !cetta_gslt_literal_head_program_match_v1(
                            &headed, lookup, &fixture,
                            headed_mismatch, 5u) &&
                        !cetta_gslt_literal_head_program_match_v1(
                            &headed, lookup, &fixture, NULL, 0u),
                    "headed matching checks the separated literal");
        expect_true(!cetta_gslt_literal_head_program_set_head_v1(
                        &headed, 99u),
                    "a separated head cannot be rebound");
    }

    {
        const uint32_t parser_ref[] = {20u, 21u, 7u};
        expect_true(cetta_gslt_literal_head_program_append_hole_v1(
                        &leading_hole, 0u) &&
                        cetta_gslt_literal_head_program_append_literal_v1(
                            &leading_hole, 7u, false) &&
                        !cetta_gslt_literal_head_program_set_head_v1(
                            &leading_hole, 20u) &&
                        cetta_gslt_literal_head_program_validate_v1(
                            &leading_hole) &&
                        cetta_gslt_literal_head_program_match_v1(
                            &leading_hole, lookup, &fixture,
                            parser_ref, 3u),
                    "leading hole stays on the exact unspecialized path");
    }

    cetta_gslt_literal_hole_program_free_v1(&program);
    cetta_gslt_literal_hole_program_free_v1(&unmerged);
    cetta_gslt_literal_head_program_free_v1(&headed);
    cetta_gslt_literal_head_program_free_v1(&leading_hole);
    expect_true(program.parts == NULL && program.literal_items == NULL &&
                    program.part_len == 0u && program.literal_len == 0u,
                "free restores empty state");

    printf("GsltLiteralHoleProgramV1Summary checks=%u failures=%u\n",
           checks, failures);
    return failures == 0u ? 0 : 1;
}
