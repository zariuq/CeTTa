#include "prime_level.h"

#include <inttypes.h>
#include <stdio.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                             \
    do {                                                                    \
        checks++;                                                           \
        if (!(condition)) {                                                 \
            fprintf(stderr, "FAIL: %s\n", (label));                       \
            failures++;                                                     \
        }                                                                   \
    } while (0)

typedef struct {
    const CettaPrimeLevelV1 *parameter_zero;
    const CettaPrimeLevelV1 *parameter_one;
} SubstitutionFixture;

static CettaPrimeLevelStatusV1 substitute_fixture(
    void *context, uint64_t parameter,
    const CettaPrimeLevelV1 **replacement_out) {
    SubstitutionFixture *fixture = context;
    if (!fixture || !replacement_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (parameter == 0u) {
        *replacement_out = fixture->parameter_zero;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    if (parameter == 1u) {
        *replacement_out = fixture->parameter_one;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    *replacement_out = NULL;
    return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
}

static CettaPrimeLevelStatusV1 valuation_fixture(
    void *context, uint64_t parameter, uint64_t *value_out) {
    const uint64_t *values = context;
    if (!values || !value_out || parameter > 3u)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    *value_out = values[parameter];
    return CETTA_PRIME_LEVEL_OK_V1;
}

int main(void) {
    Arena arena;
    arena_init(&arena);

    const CettaPrimeLevelV1 *zero = NULL;
    const CettaPrimeLevelV1 *one = NULL;
    const CettaPrimeLevelV1 *two = NULL;
    const CettaPrimeLevelV1 *five = NULL;
    const CettaPrimeLevelV1 *maximum_constant = NULL;
    CHECK(cetta_prime_level_constant_v1(&arena, 0u, &zero) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_constant_v1(&arena, 1u, &one) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_constant_v1(&arena, 2u, &two) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_constant_v1(&arena, 5u, &five) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_maximum_v1(
                  &arena, one, two, &maximum_constant) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_equal_v1(maximum_constant, two),
          "constant maximum normalizes canonically");

    const CettaPrimeLevelV1 *parameter_zero = NULL;
    const CettaPrimeLevelV1 *parameter_three = NULL;
    const CettaPrimeLevelV1 *parameter_three_successor = NULL;
    const CettaPrimeLevelV1 *absorbed = NULL;
    CHECK(cetta_prime_level_parameter_v1(
              &arena, 0u, &parameter_zero) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_parameter_v1(
                  &arena, 3u, &parameter_three) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_successor_v1(
                  &arena, parameter_three,
                  &parameter_three_successor) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_maximum_v1(
                  &arena, one, parameter_three_successor, &absorbed) ==
                  CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_equal_v1(
                  absorbed, parameter_three_successor),
          "a dominated constant is absent from the admitted form");

    const CettaPrimeLevelV1 *left_order = NULL;
    const CettaPrimeLevelV1 *right_order = NULL;
    CHECK(cetta_prime_level_maximum_v1(
              &arena, two, parameter_zero, &left_order) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_successor_v1(
                  &arena, parameter_zero, &right_order) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_le_v1(parameter_zero, right_order) &&
              !cetta_prime_level_le_v1(right_order, parameter_zero) &&
              !cetta_prime_level_le_v1(left_order, right_order) &&
              !cetta_prime_level_le_v1(parameter_zero, parameter_three),
          "semantic order distinguishes constants, offsets, and parameters");

    const CettaPrimeLevelV1 *source_zero = NULL;
    const CettaPrimeLevelV1 *source_one = NULL;
    const CettaPrimeLevelV1 *source_one_successor = NULL;
    const CettaPrimeLevelV1 *source = NULL;
    const CettaPrimeLevelV1 *theta_zero = NULL;
    CHECK(cetta_prime_level_parameter_v1(
              &arena, 0u, &source_zero) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_parameter_v1(
                  &arena, 1u, &source_one) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_successor_v1(
                  &arena, source_zero, &source_one_successor) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_maximum_v1(
                  &arena, source_one_successor, source_one, &source) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_maximum_v1(
                  &arena, two, parameter_three, &theta_zero) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "substitution fixture levels construct");
    SubstitutionFixture substitution = {
        .parameter_zero = theta_zero,
        .parameter_one = five,
    };
    const CettaPrimeLevelV1 *substituted = NULL;
    CettaPrimeLevelViewV1 substituted_view = {0};
    CHECK(cetta_prime_level_substitute_v1(
              &arena, source, substitute_fixture, &substitution,
              &substituted) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_view_v1(substituted, &substituted_view) &&
              substituted_view.constant == 5u &&
              substituted_view.parameter_count == 1u &&
              substituted_view.parameters[0].parameter == 3u &&
              substituted_view.parameters[0].offset == 1u,
          "simultaneous substitution composes offsets and merges parameters");

    const uint64_t valuation[] = {7u, 11u, 13u, 17u};
    uint64_t evaluated = 0u;
    CHECK(cetta_prime_level_evaluate_v1(
              substituted, valuation_fixture, (void *)valuation,
              &evaluated) == CETTA_PRIME_LEVEL_OK_V1 &&
              evaluated == 18u,
          "canonical evaluation agrees with constant/parameter semantics");

    const CettaPrimeLevelV1 *overflow = (const CettaPrimeLevelV1 *)1;
    const CettaPrimeLevelV1 *largest = NULL;
    CHECK(cetta_prime_level_constant_v1(
              &arena, UINT64_MAX, &largest) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_successor_v1(
                  &arena, largest, &overflow) ==
                  CETTA_PRIME_LEVEL_REPRESENTATION_LIMIT_V1 &&
              overflow == NULL,
          "finite representation exhaustion is not a false level decision");

    arena_free(&arena);
    if (failures != 0u) {
        fprintf(
            stderr, "PrimeLevelSummary checks=%u failures=%u\n",
            checks, failures);
        return 1;
    }
    printf("(PrimeLevelSummary checks=%u failures=0)\n", checks);
    return 0;
}
