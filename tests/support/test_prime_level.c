#include "prime_level.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

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
    void *context, uint64_t parameter,
    const CettaPrimeLevelNotationV1 **value_out) {
    const CettaPrimeLevelNotationV1 *const *values = context;
    if (!values || !value_out || parameter > 3u)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    *value_out = values[parameter];
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* The machine number `value` as a natural number. */
static const CettaPrimeLevelNaturalV1 *number(Arena *arena, uint64_t value) {
    const CettaPrimeLevelNaturalV1 *natural = NULL;
    CHECK(cetta_prime_level_natural_v1(arena, value, &natural) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "a machine number is a natural number");
    return natural;
}

/* The natural number written by `digits`, of which there may be any
 * number. */
static const CettaPrimeLevelNaturalV1 *decimal(
    Arena *arena, const char *digits) {
    const CettaPrimeLevelNaturalV1 *natural = NULL;
    CHECK(cetta_prime_level_natural_from_decimal_v1(
              arena, digits, &natural) == CETTA_PRIME_LEVEL_OK_V1,
          "decimal digits write a natural number");
    return natural;
}

static bool number_is(
    const CettaPrimeLevelNaturalV1 *natural, uint64_t value) {
    uint64_t held = ~value;
    return cetta_prime_level_natural_fits_uint64_v1(natural, &held) &&
           held == value;
}

static bool decimal_is(
    Arena *arena, const CettaPrimeLevelNaturalV1 *natural,
    const char *digits) {
    const char *written = NULL;
    return cetta_prime_level_natural_decimal_v1(arena, natural, &written) ==
               CETTA_PRIME_LEVEL_OK_V1 &&
           written && strcmp(written, digits) == 0;
}

/* The natural number `value` as a notation. */
static const CettaPrimeLevelNotationV1 *natural(
    Arena *arena, uint64_t value) {
    const CettaPrimeLevelNotationV1 *notation = NULL;
    CHECK(cetta_prime_level_notation_natural_v1(
              arena, number(arena, value), &notation) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "a natural number has a notation");
    return notation;
}

/* The natural number written by `digits` as a notation. */
static const CettaPrimeLevelNotationV1 *long_natural(
    Arena *arena, const char *digits) {
    const CettaPrimeLevelNotationV1 *notation = NULL;
    CHECK(cetta_prime_level_notation_natural_v1(
              arena, decimal(arena, digits), &notation) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "a long natural number has a notation");
    return notation;
}

/* omega^exponent * coefficient + remainder, which must be a normal form. */
static const CettaPrimeLevelNotationV1 *cantor(
    Arena *arena, const CettaPrimeLevelNotationV1 *exponent,
    uint64_t coefficient, const CettaPrimeLevelNotationV1 *remainder) {
    const CettaPrimeLevelNotationV1 *notation = NULL;
    CHECK(cetta_prime_level_notation_v1(
              arena, exponent, number(arena, coefficient), remainder,
              &notation) == CETTA_PRIME_LEVEL_OK_V1,
          "a Cantor normal form has a notation");
    return notation;
}

/* The same with a coefficient written by `digits`. */
static const CettaPrimeLevelNotationV1 *long_cantor(
    Arena *arena, const CettaPrimeLevelNotationV1 *exponent,
    const char *digits, const CettaPrimeLevelNotationV1 *remainder) {
    const CettaPrimeLevelNotationV1 *notation = NULL;
    CHECK(cetta_prime_level_notation_v1(
              arena, exponent, decimal(arena, digits), remainder,
              &notation) == CETTA_PRIME_LEVEL_OK_V1,
          "a Cantor normal form with a long coefficient has a notation");
    return notation;
}

/* The n-th level above all Cantor normal forms. */
static const CettaPrimeLevelNotationV1 *above(Arena *arena, uint64_t n) {
    const CettaPrimeLevelNotationV1 *notation = NULL;
    CHECK(cetta_prime_level_notation_above_v1(
              arena, number(arena, n), &notation) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation != NULL,
          "a level above the Cantor normal forms has a notation");
    return notation;
}

static const CettaPrimeLevelV1 *constant_level(
    Arena *arena, const CettaPrimeLevelNotationV1 *constant) {
    const CettaPrimeLevelV1 *level = NULL;
    CHECK(cetta_prime_level_constant_v1(arena, constant, &level) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "a notation is a constant level");
    return level;
}

/* The order of two notations, which must have one. */
static int order(
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right) {
    int result = 0;
    CHECK(cetta_prime_level_notation_compare_v1(left, right, &result) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "two notations have an order");
    return result;
}

static bool notation_is(
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right) {
    return order(left, right) == 0;
}

static bool level_equal(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right) {
    bool equal = false;
    CHECK(cetta_prime_level_equal_v1(left, right, &equal) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "the equality of two levels is decided");
    return equal;
}

static bool level_le(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right) {
    bool le = false;
    CHECK(cetta_prime_level_le_v1(left, right, &le) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "the order of two levels is decided");
    return le;
}

/* The notation `offset` successors above another. */
static const CettaPrimeLevelNotationV1 *raised(
    Arena *arena, const CettaPrimeLevelNotationV1 *notation,
    uint64_t offset) {
    const CettaPrimeLevelNotationV1 *shifted =
        (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_offset_v1(
              arena, notation, number(arena, offset), &shifted) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "a notation has the notation any number of successors above it");
    return shifted;
}

/* The least notation whose successor reaches another. */
static const CettaPrimeLevelNotationV1 *under_successor(
    Arena *arena, const CettaPrimeLevelNotationV1 *notation) {
    const CettaPrimeLevelNotationV1 *under =
        (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_under_successor_v1(
              arena, notation, &under) == CETTA_PRIME_LEVEL_OK_V1,
          "a notation has a least notation whose successor reaches it");
    return under;
}

/* The least notation that reaches another when raised by `offset`. */
static const CettaPrimeLevelNotationV1 *under_offset(
    Arena *arena, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *offset) {
    const CettaPrimeLevelNotationV1 *under =
        (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_under_offset_v1(
              arena, notation, offset, &under) == CETTA_PRIME_LEVEL_OK_V1,
          "a notation has a least notation that reaches it under an offset");
    return under;
}

/* Whether a notation is the natural number `value`. */
static bool natural_is(
    const CettaPrimeLevelNotationV1 *notation, uint64_t value) {
    const CettaPrimeLevelNaturalV1 *held = NULL;
    return cetta_prime_level_notation_natural_value_v1(notation, &held) &&
           number_is(held, value);
}

/* Whether a notation is the natural number written by `digits`. */
static bool long_natural_is(
    Arena *arena, const CettaPrimeLevelNotationV1 *notation,
    const char *digits) {
    const CettaPrimeLevelNaturalV1 *held = NULL;
    return cetta_prime_level_notation_natural_value_v1(notation, &held) &&
           decimal_is(arena, held, digits);
}

typedef CettaPrimeLevelStatusV1 (*NotationOperation)(
    Arena *, const CettaPrimeLevelNotationV1 *,
    const CettaPrimeLevelNotationV1 *, CettaPrimeLevelBudgetV1 *,
    const CettaPrimeLevelNotationV1 **);

/* The result of an operation on two notations, which must have one. */
static const CettaPrimeLevelNotationV1 *computed(
    Arena *arena, NotationOperation operation,
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right) {
    const CettaPrimeLevelNotationV1 *result = NULL;
    CHECK(operation(arena, left, right, NULL, &result) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "the arithmetic of notations has a result");
    return result;
}

static const CettaPrimeLevelNotationV1 *sum(
    Arena *arena, const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right) {
    return computed(arena, cetta_prime_level_notation_add_v1, left, right);
}

static const CettaPrimeLevelNotationV1 *product(
    Arena *arena, const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right) {
    return computed(arena, cetta_prime_level_notation_mul_v1, left, right);
}

static const CettaPrimeLevelNotationV1 *power(
    Arena *arena, const CettaPrimeLevelNotationV1 *base,
    const CettaPrimeLevelNotationV1 *exponent) {
    return computed(arena, cetta_prime_level_notation_pow_v1, base, exponent);
}

/* Whether an operation on two notations answers with `status` and leaves no
 * result. */
static bool answers(
    Arena *arena, NotationOperation operation,
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, CettaPrimeLevelStatusV1 status) {
    const CettaPrimeLevelNotationV1 *result =
        (const CettaPrimeLevelNotationV1 *)1;
    return operation(arena, left, right, NULL, &result) == status &&
           result == NULL;
}

/* An operation on two notations capped at a budget of `steps`: its status,
 * its result, and in `*spent_out` the steps it took. */
static CettaPrimeLevelStatusV1 capped(
    Arena *arena, NotationOperation operation,
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, uint64_t steps,
    const CettaPrimeLevelNotationV1 **result_out, uint64_t *spent_out) {
    CettaPrimeLevelBudgetV1 budget = {.limited = true, .remaining = steps};
    CettaPrimeLevelStatusV1 status =
        operation(arena, left, right, &budget, result_out);
    /* What a budget has left and what it has spent are its steps. */
    CHECK(budget.limited && budget.remaining <= steps &&
              budget.spent == steps - budget.remaining,
          "a budget keeps count of its steps");
    if (spent_out) *spent_out = budget.spent;
    return status;
}

static bool level_constant_is(
    const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelNotationV1 *constant, size_t parameter_count) {
    CettaPrimeLevelViewV1 view = {0};
    return cetta_prime_level_view_v1(level, &view) &&
           notation_is(view.constant, constant) &&
           view.parameter_count == parameter_count;
}

/* The number of terms of a Cantor normal form, those of its exponents not
 * counted. */
static size_t term_count(const CettaPrimeLevelNotationV1 *notation) {
    CettaPrimeLevelNotationTermV1 term = {0};
    size_t count = 0u;
    for (; cetta_prime_level_notation_term_v1(notation, &term);
         notation = term.remainder)
        count++;
    return count;
}

/* A fold that measures how deep the exponents of a notation nest: zero is
 * 0, and a term is one more than its exponent.  The count is carried as the
 * value itself.  A level above the Cantor normal forms ends it. */
static CettaPrimeLevelStatusV1 height_of_zero(
    void *context, const void *handle, void **value_out) {
    (void)context;
    (void)handle;
    *value_out = NULL;
    return CETTA_PRIME_LEVEL_OK_V1;
}

static CettaPrimeLevelStatusV1 height_of_above(
    void *context, const void *handle,
    const CettaPrimeLevelNotationReadTermV1 *read, void **value_out) {
    (void)context;
    (void)handle;
    (void)read;
    *value_out = NULL;
    return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
}

static CettaPrimeLevelStatusV1 height_of_term(
    void *context, const void *handle,
    const CettaPrimeLevelNotationReadTermV1 *read, void *exponent_value,
    void *remainder_value, void **value_out) {
    size_t *terms = context;
    (void)handle;
    (void)read;
    (void)remainder_value;
    (*terms)++;
    *value_out = (void *)((uintptr_t)exponent_value + 1u);
    return CETTA_PRIME_LEVEL_OK_V1;
}

static const CettaPrimeLevelNotationFoldV1 height_fold = {
    .zero = height_of_zero,
    .above = height_of_above,
    .term = height_of_term,
};

int main(void) {
    Arena arena;
    arena_init(&arena);

    const CettaPrimeLevelV1 *zero = constant_level(&arena, NULL);
    const CettaPrimeLevelV1 *one = constant_level(&arena, natural(&arena, 1u));
    const CettaPrimeLevelV1 *two = constant_level(&arena, natural(&arena, 2u));
    const CettaPrimeLevelV1 *five =
        constant_level(&arena, natural(&arena, 5u));
    const CettaPrimeLevelV1 *maximum_constant = NULL;
    CHECK(zero && one && two && five &&
              cetta_prime_level_maximum_v1(
                  &arena, one, two, &maximum_constant) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(maximum_constant, two),
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
              level_equal(absorbed, parameter_three_successor),
          "a dominated constant is absent from the admitted form");

    const CettaPrimeLevelV1 *left_order = NULL;
    const CettaPrimeLevelV1 *right_order = NULL;
    CHECK(cetta_prime_level_maximum_v1(
              &arena, two, parameter_zero, &left_order) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_successor_v1(
                  &arena, parameter_zero, &right_order) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_le(parameter_zero, right_order) &&
              !level_le(right_order, parameter_zero) &&
              !level_le(left_order, right_order) &&
              !level_le(parameter_zero, parameter_three),
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
              natural_is(substituted_view.constant, 5u) &&
              substituted_view.parameter_count == 1u &&
              substituted_view.parameters[0].parameter == 3u &&
              number_is(substituted_view.parameters[0].offset, 1u),
          "simultaneous substitution composes offsets and merges parameters");

    const CettaPrimeLevelNotationV1 *valuation[] = {
        natural(&arena, 7u), natural(&arena, 11u), natural(&arena, 13u),
        natural(&arena, 17u),
    };
    const CettaPrimeLevelNotationV1 *evaluated = NULL;
    CHECK(cetta_prime_level_evaluate_v1(
              &arena, substituted, valuation_fixture, (void *)valuation,
              &evaluated) == CETTA_PRIME_LEVEL_OK_V1 &&
              natural_is(evaluated, 18u),
          "canonical evaluation agrees with constant/parameter semantics");

    /* Natural numbers of any size.  A machine number and decimal digits
     * give the same number, and a number is read back as both. */
    static const char forty_digits[] =
        "1234567890123456789012345678901234567890";
    const CettaPrimeLevelNaturalV1 *forty = decimal(&arena, forty_digits);
    const CettaPrimeLevelNaturalV1 *largest_machine =
        number(&arena, UINT64_MAX);
    const CettaPrimeLevelNaturalV1 *refused_number =
        (const CettaPrimeLevelNaturalV1 *)1;
    CHECK(number(&arena, 0u) == NULL && decimal(&arena, "0") == NULL &&
              decimal(&arena, "0000") == NULL &&
              number_is(NULL, 0u) && decimal_is(&arena, NULL, "0"),
          "zero is the NULL natural, however it is written");
    CHECK(number_is(decimal(&arena, "000123"), 123u) &&
              decimal_is(&arena, number(&arena, 123u), "123") &&
              number_is(largest_machine, UINT64_MAX) &&
              decimal_is(&arena, largest_machine, "18446744073709551615") &&
              number_is(decimal(&arena, "18446744073709551615"), UINT64_MAX) &&
              decimal_is(
                  &arena, number(&arena, UINT64_C(1000000000)),
                  "1000000000") &&
              decimal_is(
                  &arena, number(&arena, UINT64_C(1000000000000000000)),
                  "1000000000000000000"),
          "a machine number and its decimal digits are the same number");
    CHECK(decimal_is(&arena, forty, forty_digits) &&
              !number_is(forty, UINT64_C(1234567890123456789)) &&
              !cetta_prime_level_natural_fits_uint64_v1(forty, NULL) &&
              !cetta_prime_level_natural_fits_uint64_v1(
                  decimal(&arena, "18446744073709551616"), NULL) &&
              !cetta_prime_level_natural_fits_uint64_v1(
                  decimal(&arena, "1000000000000000000000000000"), NULL) &&
              !cetta_prime_level_natural_fits_uint64_v1(
                  decimal(&arena, "1000000000000000000000000007"), NULL) &&
              !cetta_prime_level_natural_fits_uint64_v1(
                  decimal(&arena, "99999999999999999999"), NULL) &&
              !decimal_is(
                  &arena, forty, "123456789012345678901234567890123456789"),
          "a number of forty digits is held in full and is no machine "
          "number");
    CHECK(decimal_is(
              &arena, decimal(&arena, "1000000000000000000000000000"),
              "1000000000000000000000000000") &&
              decimal_is(
                  &arena, decimal(&arena, "1000000001000000000"),
                  "1000000001000000000") &&
              decimal_is(
                  &arena, decimal(&arena, "20000000000000000000000000001"),
                  "20000000000000000000000000001"),
          "digits that are zero inside a long number are kept");
    static const char *const not_numbers[] = {
        "", "-5", "+7", " 7", "7 ", "12a", "1.5", "0x10",
    };
    bool not_numbers_refused = true;
    for (size_t index = 0u;
         index < sizeof not_numbers / sizeof not_numbers[0]; index++) {
        refused_number = (const CettaPrimeLevelNaturalV1 *)1;
        if (cetta_prime_level_natural_from_decimal_v1(
                &arena, not_numbers[index], &refused_number) !=
                CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 ||
            refused_number != NULL)
            not_numbers_refused = false;
    }
    CHECK(not_numbers_refused &&
              cetta_prime_level_natural_from_decimal_v1(
                  &arena, NULL, &refused_number) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1,
          "text that is not decimal digits writes no natural number");

    /* The successor of the largest machine number is a level like any
     * other. */
    const CettaPrimeLevelV1 *largest =
        constant_level(&arena, natural(&arena, UINT64_MAX));
    const CettaPrimeLevelV1 *beyond_machine = NULL;
    CettaPrimeLevelViewV1 beyond_machine_view = {0};
    CHECK(largest &&
              cetta_prime_level_successor_v1(
                  &arena, largest, &beyond_machine) ==
                  CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_view_v1(
                  beyond_machine, &beyond_machine_view) &&
              long_natural_is(
                  &arena, beyond_machine_view.constant,
                  "18446744073709551616") &&
              level_le(largest, beyond_machine) &&
              !level_le(beyond_machine, largest) &&
              !level_equal(beyond_machine, largest),
          "the largest machine number has a successor");

    /* Notations below epsilon-zero: 7 < omega < omega + 1 < omega * 2 <
     * omega^2 < omega^omega. */
    const CettaPrimeLevelNotationV1 *n1 = natural(&arena, 1u);
    const CettaPrimeLevelNotationV1 *n2 = natural(&arena, 2u);
    const CettaPrimeLevelNotationV1 *n7 = natural(&arena, 7u);
    const CettaPrimeLevelNotationV1 *omega = cantor(&arena, n1, 1u, NULL);
    const CettaPrimeLevelNotationV1 *omega_plus_one =
        cantor(&arena, n1, 1u, n1);
    const CettaPrimeLevelNotationV1 *omega_times_two =
        cantor(&arena, n1, 2u, NULL);
    const CettaPrimeLevelNotationV1 *omega_squared =
        cantor(&arena, n2, 1u, NULL);
    const CettaPrimeLevelNotationV1 *omega_to_omega =
        cantor(&arena, omega, 1u, NULL);
    const CettaPrimeLevelNotationV1 *chain[] = {
        NULL, n1, n7, omega, omega_plus_one, omega_times_two,
        omega_squared, omega_to_omega,
    };
    bool chain_ordered = true;
    for (size_t left = 0u; left < sizeof chain / sizeof chain[0]; left++)
        for (size_t right = 0u; right < sizeof chain / sizeof chain[0];
             right++) {
            int between = order(chain[left], chain[right]);
            if ((left < right && between >= 0) ||
                (left == right && between != 0) ||
                (left > right && between <= 0))
                chain_ordered = false;
        }
    CHECK(chain_ordered,
          "notations compare by exponent, coefficient and remainder");
    CHECK(order(omega_squared,
                cantor(&arena, natural(&arena, 2u), 1u, NULL)) == 0,
          "equal notations built apart compare equal");
    CHECK(natural_is(NULL, 0u) && natural_is(n7, 7u) &&
              !natural_is(n7, 8u) &&
              !cetta_prime_level_notation_natural_value_v1(
                  omega_plus_one, NULL),
          "the natural numbers are the notations with exponent zero");

    /* Only normal forms are notations: 1 + omega, omega + omega and a zero
     * coefficient are refused. */
    const CettaPrimeLevelNotationV1 *refused =
        (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_v1(
              &arena, NULL, number(&arena, 1u), omega, &refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              refused == NULL,
          "one plus omega is not a normal form");
    CHECK(cetta_prime_level_notation_v1(
              &arena, n1, number(&arena, 1u), omega, &refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              cetta_prime_level_notation_v1(
                  &arena, n1, number(&arena, 1u), omega_squared, &refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1,
          "exponents of a normal form strictly decrease");
    CHECK(cetta_prime_level_notation_v1(
              &arena, n1, NULL, NULL, &refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1,
          "a coefficient of a normal form is positive");

    /* Successor and finite offset.  omega is a limit: its successor gains a
     * last term.  omega + 1 is a successor: its last term grows. */
    CHECK(notation_is(raised(&arena, omega, 1u), omega_plus_one),
          "the successor of a limit notation appends one");
    CHECK(notation_is(
              raised(&arena, omega_plus_one, 1u), cantor(&arena, n1, 1u, n2)),
          "the successor of a successor notation grows its last term");
    const CettaPrimeLevelNotationV1 *omega_two_plus_three =
        cantor(&arena, n1, 2u, natural(&arena, 3u));
    CHECK(notation_is(
              raised(&arena, omega_two_plus_three, 4u),
              cantor(&arena, n1, 2u, n7)) &&
              notation_is(
                  raised(&arena, omega_squared, 7u),
                  cantor(&arena, n2, 1u, n7)) &&
              notation_is(raised(&arena, NULL, 7u), n7) &&
              raised(&arena, omega_to_omega, 0u) == omega_to_omega,
          "a finite offset changes only the natural last term");
    const CettaPrimeLevelNotationV1 *long_sum = cantor(
        &arena, omega, 3u, cantor(&arena, n2, 1u, omega_plus_one));
    const CettaPrimeLevelNotationV1 *long_sum_successor = cantor(
        &arena, omega, 3u,
        cantor(&arena, n2, 1u, cantor(&arena, n1, 1u, n2)));
    CHECK(notation_is(raised(&arena, long_sum, 1u), long_sum_successor) &&
              order(long_sum, raised(&arena, long_sum, 1u)) < 0,
          "the successor keeps every term before the last");

    /* The least notation whose successor reaches a given one: a successor
     * notation steps back by one, and zero and the limits stay.  It undoes
     * the successor, and a notation is at most `level + 1` exactly when
     * this one is at most `level`. */
    CHECK(under_successor(&arena, NULL) == NULL &&
              under_successor(&arena, n1) == NULL &&
              notation_is(under_successor(&arena, n7), natural(&arena, 6u)),
          "under a natural number is the number before it, and zero stays");
    CHECK(under_successor(&arena, omega) == omega &&
              under_successor(&arena, omega_to_omega) == omega_to_omega,
          "a limit notation has nothing under it but itself");
    CHECK(notation_is(under_successor(&arena, omega_plus_one), omega) &&
              notation_is(
                  under_successor(&arena, omega_two_plus_three),
                  cantor(&arena, n1, 2u, n2)) &&
              notation_is(
                  under_successor(&arena, long_sum_successor), long_sum),
          "under a successor notation its last term shrinks or goes");
    bool under_is_adjoint = true;
    for (size_t left = 0u; left < sizeof chain / sizeof chain[0]; left++)
        for (size_t right = 0u; right < sizeof chain / sizeof chain[0];
             right++) {
            if ((order(chain[left], raised(&arena, chain[right], 1u)) <= 0) !=
                (order(under_successor(&arena, chain[left]),
                       chain[right]) <= 0))
                under_is_adjoint = false;
        }
    CHECK(under_is_adjoint,
          "a notation is at most level + 1 exactly when the one under it "
          "is at most level");

    /* Sum, product and power.  Among natural numbers they are those of the
     * numbers. */
    const CettaPrimeLevelNotationV1 *n3 = natural(&arena, 3u);
    const CettaPrimeLevelNotationV1 *n4 = natural(&arena, 4u);
    CHECK(notation_is(sum(&arena, n3, n4), n7) &&
              notation_is(product(&arena, n2, n3), natural(&arena, 6u)) &&
              notation_is(power(&arena, n2, n3), natural(&arena, 8u)) &&
              sum(&arena, NULL, NULL) == NULL &&
              product(&arena, n7, NULL) == NULL &&
              product(&arena, NULL, n7) == NULL,
          "among natural numbers the arithmetic is that of the numbers");
    CHECK(notation_is(power(&arena, NULL, NULL), n1) &&
              power(&arena, NULL, n7) == NULL &&
              power(&arena, NULL, omega) == NULL &&
              notation_is(power(&arena, n7, NULL), n1) &&
              notation_is(power(&arena, omega_to_omega, NULL), n1) &&
              notation_is(power(&arena, n1, omega_to_omega), n1),
          "a power of zero, to zero, and of one");

    /* Beyond the natural numbers the operations are not commutative: a
     * smaller left summand is absorbed, a natural left factor is absorbed by
     * a limit, and a natural base to the power omega is omega. */
    CHECK(notation_is(sum(&arena, n1, omega), omega) &&
              notation_is(sum(&arena, omega, n1), omega_plus_one) &&
              !notation_is(sum(&arena, omega, n1), sum(&arena, n1, omega)),
          "one plus omega is omega, and omega plus one is its successor");
    CHECK(notation_is(sum(&arena, omega, omega), omega_times_two) &&
              notation_is(sum(&arena, omega, omega_squared), omega_squared) &&
              notation_is(
                  sum(&arena, omega_squared, omega),
                  cantor(&arena, n2, 1u, omega)) &&
              notation_is(sum(&arena, omega_plus_one, NULL), omega_plus_one) &&
              notation_is(sum(&arena, NULL, omega_plus_one), omega_plus_one),
          "a sum merges equal exponents and absorbs smaller ones on the left");
    CHECK(notation_is(product(&arena, n2, omega), omega) &&
              notation_is(product(&arena, omega, n2), omega_times_two) &&
              !notation_is(
                  product(&arena, omega, n2), product(&arena, n2, omega)),
          "two times omega is omega, and omega times two is omega plus omega");
    CHECK(notation_is(
              product(&arena, omega_plus_one, n2),
              cantor(&arena, n1, 2u, n1)) &&
              notation_is(product(&arena, omega_plus_one, omega),
                          omega_squared) &&
              notation_is(
                  product(&arena, omega_plus_one, omega_plus_one),
                  cantor(&arena, n2, 1u, omega_plus_one)) &&
              notation_is(product(&arena, omega, omega), omega_squared) &&
              notation_is(product(&arena, omega_to_omega, n1), omega_to_omega),
          "a product shifts the leading exponent or multiplies its "
          "coefficient");
    CHECK(notation_is(power(&arena, n2, omega), omega) &&
              notation_is(power(&arena, omega, n2), omega_squared) &&
              !notation_is(power(&arena, omega, n2), power(&arena, n2, omega)),
          "two to the power omega is omega, and omega to the power two is "
          "omega times omega");
    CHECK(notation_is(power(&arena, omega, omega), omega_to_omega) &&
              notation_is(power(&arena, omega_squared, omega),
                          omega_to_omega) &&
              notation_is(power(&arena, omega, n1), omega) &&
              notation_is(
                  power(&arena, n2, cantor(&arena, n1, 2u, n3)),
                  cantor(&arena, n2, 8u, NULL)) &&
              notation_is(
                  power(&arena, omega, natural(&arena, 5000u)),
                  cantor(&arena, natural(&arena, 5000u), 1u, NULL)),
          "a power of omega and of a natural number");
    CHECK(notation_is(
              power(&arena, omega_plus_one, n2),
              cantor(&arena, n2, 1u, omega_plus_one)) &&
              notation_is(
                  power(&arena, omega_plus_one, n3),
                  cantor(&arena, n3, 1u,
                         cantor(&arena, n2, 1u, omega_plus_one))) &&
              notation_is(
                  power(&arena, cantor(&arena, n1, 2u, n3), n2),
                  cantor(&arena, n2, 2u,
                         cantor(&arena, n1, 6u, n3))) &&
              notation_is(
                  power(&arena, omega_plus_one, omega), omega_to_omega),
          "a power of a base with several terms");
    bool sum_laws = true;
    bool product_laws = true;
    bool power_laws = true;
    for (size_t x = 0u; x < sizeof chain / sizeof chain[0]; x++)
        for (size_t y = 0u; y < sizeof chain / sizeof chain[0]; y++) {
            const CettaPrimeLevelNotationV1 *both =
                sum(&arena, chain[x], chain[y]);
            if (order(chain[x], both) > 0 || order(chain[y], both) > 0)
                sum_laws = false;
            for (size_t z = 0u; z < sizeof chain / sizeof chain[0]; z++) {
                const CettaPrimeLevelNotationV1 *later =
                    sum(&arena, chain[y], chain[z]);
                if (!notation_is(sum(&arena, both, chain[z]),
                                 sum(&arena, chain[x], later)))
                    sum_laws = false;
                if (!notation_is(
                        product(&arena, chain[x], later),
                        sum(&arena, product(&arena, chain[x], chain[y]),
                            product(&arena, chain[x], chain[z]))) ||
                    !notation_is(
                        product(&arena,
                                product(&arena, chain[x], chain[y]), chain[z]),
                        product(&arena, chain[x],
                                product(&arena, chain[y], chain[z]))))
                    product_laws = false;
                if (!notation_is(
                        power(&arena, chain[x], later),
                        product(&arena, power(&arena, chain[x], chain[y]),
                                power(&arena, chain[x], chain[z]))))
                    power_laws = false;
            }
        }
    CHECK(sum_laws,
          "a sum is associative and at least each of its summands");
    CHECK(product_laws,
          "a product is associative and distributes over a sum on the right");
    CHECK(power_laws, "a power of a sum is the product of the powers");

    /* Numbers beyond the machine numbers.  A sum, a product and a power
     * that pass 2^63 and 2^64 are the numbers they are. */
    const CettaPrimeLevelNotationV1 *largest_numeral =
        natural(&arena, (uint64_t)INT64_MAX);
    CHECK(long_natural_is(
              &arena, sum(&arena, largest_numeral, n1),
              "9223372036854775808") &&
              long_natural_is(
                  &arena, product(&arena, largest_numeral, n2),
                  "18446744073709551614") &&
              long_natural_is(
                  &arena, power(&arena, n2, natural(&arena, 63u)),
                  "9223372036854775808") &&
              natural_is(
                  power(&arena, n2, natural(&arena, 62u)),
                  UINT64_C(1) << 62) &&
              notation_is(
                  sum(&arena,
                      cantor(&arena, n1, (uint64_t)INT64_MAX, NULL), omega),
                  long_cantor(&arena, n1, "9223372036854775808", NULL)),
          "a number one beyond the largest machine integer is that number");
    const CettaPrimeLevelNotationV1 *two_to_forty =
        natural(&arena, UINT64_C(1) << 40);
    CHECK(long_natural_is(
              &arena, product(&arena, two_to_forty, two_to_forty),
              "1208925819614629174706176") &&
              !long_natural_is(
                  &arena, product(&arena, two_to_forty, two_to_forty),
                  "1208925819614629174706175") &&
              !natural_is(product(&arena, two_to_forty, two_to_forty), 0u) &&
              long_natural_is(
                  &arena,
                  product(
                      &arena, long_natural(&arena, "12345678901234567890"),
                      long_natural(&arena, "98765432109876543210")),
                  "1219326311370217952237463801111263526900") &&
              long_natural_is(
                  &arena,
                  product(
                      &arena, long_natural(&arena, forty_digits),
                      long_natural(&arena, forty_digits)),
                  "15241578753238836750495351562566681945005334557625361987"
                  "87501905199875019052100"),
          "a product beyond 2^64 is the product of the numbers");
    CHECK(long_natural_is(
              &arena, power(&arena, n7, natural(&arena, 100u)),
              "32344765096247579913446477691002168108572031989046254009338"
              "95331391691459636928060001") &&
              long_natural_is(
                  &arena, power(&arena, n3, natural(&arena, 80u)),
                  "147808829414345923316083210206383297601") &&
              long_natural_is(
                  &arena, power(&arena, n2, natural(&arena, 200u)),
                  "16069380442589902755419620923411626025222029937827928353"
                  "01376") &&
              !long_natural_is(
                  &arena, power(&arena, n2, natural(&arena, 200u)),
                  "16069380442589902755419620923411626025222029937827928353"
                  "01377") &&
              notation_is(
                  power(&arena, n2, natural(&arena, 200u)),
                  product(
                      &arena, power(&arena, n2, natural(&arena, 100u)),
                      power(&arena, n2, natural(&arena, 100u)))) &&
              order(power(&arena, n2, natural(&arena, 200u)),
                    power(&arena, n3, natural(&arena, 200u))) < 0 &&
              order(power(&arena, n2, natural(&arena, 200u)), omega) < 0,
          "a power beyond 2^64 is the power of the numbers");
    CHECK(long_natural_is(
              &arena, raised(&arena, long_natural(
                                         &arena, "999999999999999999"), 1u),
              "1000000000000000000") &&
              long_natural_is(
                  &arena,
                  under_successor(
                      &arena, long_natural(&arena, "1000000000000000000")),
                  "999999999999999999") &&
              long_natural_is(
                  &arena,
                  under_successor(
                      &arena,
                      long_natural(&arena, "1000000000000000000000000000")),
                  "999999999999999999999999999") &&
              long_natural_is(
                  &arena,
                  raised(&arena, long_natural(
                                     &arena, "999999999999999999999999999"),
                         1u),
                  "1000000000000000000000000000"),
          "a successor and a predecessor carry through a long number");

    /* A coefficient of forty digits: the notation holds it in full, and the
     * order of two notations that differ only there is the order of the
     * numbers. */
    const CettaPrimeLevelNotationV1 *omega_times_forty =
        long_cantor(&arena, n1, forty_digits, NULL);
    const CettaPrimeLevelNotationV1 *omega_times_forty_and_one = long_cantor(
        &arena, n1, "1234567890123456789012345678901234567891", NULL);
    CettaPrimeLevelNotationTermV1 forty_term = {0};
    CHECK(cetta_prime_level_notation_term_v1(
              omega_times_forty, &forty_term) &&
              decimal_is(&arena, forty_term.coefficient, forty_digits) &&
              forty_term.remainder == NULL &&
              natural_is(forty_term.exponent, 1u),
          "a coefficient of forty digits is read back in full");
    CHECK(order(omega_times_forty, omega_times_forty_and_one) < 0 &&
              order(omega_times_forty_and_one, omega_times_forty) > 0 &&
              order(omega_times_forty,
                    long_cantor(&arena, n1, forty_digits, NULL)) == 0 &&
              order(long_natural(&arena, forty_digits), omega) < 0 &&
              order(omega_times_forty, omega_squared) < 0 &&
              order(cantor(&arena, n1, UINT64_MAX, NULL),
                    omega_times_forty) < 0 &&
              notation_is(
                  sum(&arena, omega_times_forty, omega),
                  omega_times_forty_and_one) &&
              notation_is(
                  product(&arena, omega, long_natural(&arena, forty_digits)),
                  omega_times_forty) &&
              notation_is(
                  product(&arena, omega_times_forty, n3),
                  long_cantor(
                      &arena, n1,
                      "3703703670370370367037037036703703703670", NULL)),
          "notations with long coefficients are ordered and computed with "
          "as the numbers are");
    /* Long numbers are ordered from their first digits on: of two numbers
     * of the same length the one that leads with the larger digit is the
     * larger, whatever their last digits are. */
    static const char leads_larger[] = "2000000000000000000000000001";
    static const char ends_larger[] = "1000000000000000000000000002";
    CHECK(order(long_natural(&arena, ends_larger),
                long_natural(&arena, leads_larger)) < 0 &&
              order(long_natural(&arena, leads_larger),
                    long_natural(&arena, ends_larger)) > 0 &&
              order(long_cantor(&arena, n1, ends_larger, NULL),
                    long_cantor(&arena, n1, leads_larger, NULL)) < 0 &&
              order(long_cantor(&arena, n1, leads_larger, NULL),
                    long_cantor(&arena, n1, ends_larger, NULL)) > 0 &&
              order(long_natural(&arena, "999999999999999999999999999"),
                    long_natural(&arena, ends_larger)) < 0 &&
              order(long_natural(&arena, "1000000000999999999"),
                    long_natural(&arena, "1000000001000000000")) < 0 &&
              order(long_natural(&arena, "1000000001000000000"),
                    long_natural(&arena, "1000000000999999999")) > 0,
          "long numbers are ordered from their first digits on");
    CHECK(!notation_is(omega_times_forty, omega_times_forty_and_one) &&
              !notation_is(
                  omega_times_forty,
                  long_cantor(
                      &arena, n1,
                      "123456789012345678901234567890123456789", NULL)) &&
              order(omega_times_forty, long_natural(&arena, forty_digits)) >
                  0,
          "a long coefficient is not the number of its first digits");

    /* A notation of any number of terms.  The power of omega + 1 to n has
     * n + 1 terms, n of them with an exponent of one term. */
    const CettaPrimeLevelNotationV1 *five_thousand =
        natural(&arena, 5000u);
    const CettaPrimeLevelNotationV1 *long_power =
        power(&arena, omega_plus_one, five_thousand);
    const CettaPrimeLevelNotationV1 *shorter_power =
        power(&arena, omega_plus_one, natural(&arena, 4999u));
    CHECK(term_count(long_power) == 5001u &&
              term_count(shorter_power) == 5000u,
          "the power of omega + 1 to 5000 has 5001 terms");
    CHECK(order(shorter_power, long_power) < 0 &&
              order(cantor(&arena, five_thousand, 1u, NULL), long_power) <
                  0 &&
              order(long_power,
                    cantor(&arena, natural(&arena, 5001u), 1u, NULL)) < 0 &&
              notation_is(
                  long_power,
                  product(&arena, shorter_power, omega_plus_one)) &&
              notation_is(
                  long_power,
                  power(&arena,
                        power(&arena, omega_plus_one, natural(&arena, 50u)),
                        natural(&arena, 100u))) &&
              notation_is(
                  product(&arena, long_power, long_power),
                  power(&arena, omega_plus_one, natural(&arena, 10000u))) &&
              order(long_power, raised(&arena, long_power, 1u)) < 0 &&
              notation_is(
                  under_successor(&arena, raised(&arena, long_power, 1u)),
                  long_power),
          "a power with more than four thousand terms is a notation like "
          "any other");
    CHECK(!notation_is(long_power, shorter_power) &&
              !notation_is(
                  long_power, cantor(&arena, five_thousand, 1u, NULL)) &&
              order(long_power, shorter_power) > 0,
          "a longer power is not a shorter one");
    const CettaPrimeLevelNotationV1 *many_terms = NULL;
    for (uint64_t exponent = 1u; exponent <= 6000u; exponent++)
        many_terms = cantor(&arena, natural(&arena, exponent), 1u, many_terms);
    CHECK(term_count(many_terms) == 6000u &&
              order(many_terms,
                    cantor(&arena, natural(&arena, 6001u), 1u, NULL)) < 0 &&
              order(many_terms, raised(&arena, many_terms, 1u)) < 0 &&
              term_count(raised(&arena, many_terms, 1u)) == 6001u,
          "a notation of six thousand terms takes further terms");

    /* A power no memory holds is reported as that, with no result: omega + 1
     * to the power n has n + 1 terms, and a natural number above one to the
     * power n has more than n binary digits. */
    const CettaPrimeLevelNotationV1 *beyond_memory =
        long_natural(&arena, "100000000000000000000000000000000000000");
    CHECK(answers(&arena, cetta_prime_level_notation_pow_v1, omega_plus_one,
                  beyond_memory, CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1) &&
              answers(&arena, cetta_prime_level_notation_pow_v1, n2,
                      beyond_memory, CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1) &&
              answers(&arena, cetta_prime_level_notation_pow_v1,
                      omega_plus_one, natural(&arena, UINT64_MAX),
                      CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1),
          "a power no memory holds is out of memory, not refused");
    CHECK(notation_is(
              power(&arena, omega, beyond_memory),
              long_cantor(
                  &arena, beyond_memory, "1", NULL)) &&
              notation_is(power(&arena, n1, beyond_memory), n1) &&
              power(&arena, NULL, beyond_memory) == NULL &&
              notation_is(
                  power(&arena, n2, sum(&arena, omega, n3)),
                  cantor(&arena, n1, 8u, NULL)),
          "a power to a long number that memory holds is computed");

    /* Levels with constants beyond the natural numbers. */
    const CettaPrimeLevelV1 *level_omega = constant_level(&arena, omega);
    const CettaPrimeLevelV1 *level_omega_plus_one =
        constant_level(&arena, omega_plus_one);
    const CettaPrimeLevelV1 *level_successor = NULL;
    const CettaPrimeLevelV1 *level_maximum = NULL;
    CHECK(cetta_prime_level_successor_v1(
              &arena, level_omega, &level_successor) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(level_successor, level_omega_plus_one) &&
              !level_equal(level_successor, level_omega),
          "the successor of the level omega is omega + 1");
    CHECK(cetta_prime_level_maximum_v1(
              &arena, level_omega, five, &level_maximum) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(level_maximum, level_omega) &&
              cetta_prime_level_maximum_v1(
                  &arena, five, level_omega_plus_one, &level_maximum) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(level_maximum, level_omega_plus_one),
          "the maximum of two constants is the larger notation");
    CHECK(level_le(five, level_omega) && level_le(level_omega, level_omega) &&
              level_le(level_omega, level_omega_plus_one) &&
              !level_le(level_omega_plus_one, level_omega) &&
              !level_le(level_omega, five),
          "constant levels are ordered as their notations");

    /* The absorption rule.  A natural constant not above the largest offset
     * is dropped.  A constant beyond the natural numbers lies above every
     * offset and stays. */
    const CettaPrimeLevelV1 *parameter_zero_plus_seven = NULL;
    const CettaPrimeLevelV1 *kept = NULL;
    const CettaPrimeLevelV1 *dropped = NULL;
    const CettaPrimeLevelV1 *seven = constant_level(&arena, n7);
    const CettaPrimeLevelV1 *eight =
        constant_level(&arena, natural(&arena, 8u));
    CHECK(cetta_prime_level_offset_v1(
              &arena, parameter_zero, number(&arena, 7u),
              &parameter_zero_plus_seven) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_maximum_v1(
                  &arena, seven, parameter_zero_plus_seven, &dropped) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(dropped, NULL, 1u) &&
              level_equal(dropped, parameter_zero_plus_seven),
          "a natural constant under the largest offset is dropped");
    CHECK(cetta_prime_level_maximum_v1(
              &arena, eight, parameter_zero_plus_seven, &kept) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(kept, natural(&arena, 8u), 1u) &&
              !level_equal(kept, parameter_zero_plus_seven),
          "a natural constant above the largest offset stays");
    CHECK(cetta_prime_level_maximum_v1(
              &arena, level_omega, parameter_zero_plus_seven, &kept) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(kept, omega, 1u) &&
              cetta_prime_level_successor_v1(&arena, kept, &kept) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(kept, omega_plus_one, 1u),
          "a constant beyond the natural numbers is never dropped");
    CHECK(level_le(seven, parameter_zero_plus_seven) &&
              !level_le(eight, parameter_zero_plus_seven) &&
              !level_le(level_omega, parameter_zero_plus_seven) &&
              level_le(parameter_zero_plus_seven, kept) &&
              level_le(level_omega_plus_one, kept) &&
              !level_le(kept, level_omega_plus_one),
          "order under every valuation reads constants at the zero valuation");

    /* An offset of forty digits.  The offset of a parameter is a natural
     * number of any size, and the rules above hold of it as of a small
     * one. */
    const CettaPrimeLevelV1 *parameter_zero_plus_forty = NULL;
    const CettaPrimeLevelV1 *parameter_zero_plus_forty_and_one = NULL;
    const CettaPrimeLevelV1 *long_absorbed = NULL;
    CettaPrimeLevelViewV1 long_offset_view = {0};
    CHECK(cetta_prime_level_offset_v1(
              &arena, parameter_zero, forty, &parameter_zero_plus_forty) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_successor_v1(
                  &arena, parameter_zero_plus_forty,
                  &parameter_zero_plus_forty_and_one) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_view_v1(
                  parameter_zero_plus_forty_and_one, &long_offset_view) &&
              long_offset_view.parameter_count == 1u &&
              decimal_is(
                  &arena, long_offset_view.parameters[0].offset,
                  "1234567890123456789012345678901234567891"),
          "an offset of forty digits has a successor");
    CHECK(level_le(parameter_zero_plus_forty,
                   parameter_zero_plus_forty_and_one) &&
              !level_le(parameter_zero_plus_forty_and_one,
                        parameter_zero_plus_forty) &&
              level_le(
                  parameter_zero_plus_seven, parameter_zero_plus_forty) &&
              !level_le(
                  parameter_zero_plus_forty, parameter_zero_plus_seven) &&
              !level_equal(parameter_zero_plus_forty,
                           parameter_zero_plus_forty_and_one) &&
              level_le(
                  constant_level(&arena, long_natural(&arena, forty_digits)),
                  parameter_zero_plus_forty) &&
              !level_le(
                  constant_level(
                      &arena,
                      long_natural(
                          &arena,
                          "1234567890123456789012345678901234567891")),
                  parameter_zero_plus_forty) &&
              !level_le(level_omega, parameter_zero_plus_forty),
          "levels with long offsets are ordered as the numbers are");
    CHECK(cetta_prime_level_maximum_v1(
              &arena,
              constant_level(&arena, long_natural(&arena, forty_digits)),
              parameter_zero_plus_forty, &long_absorbed) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(long_absorbed, NULL, 1u) &&
              cetta_prime_level_maximum_v1(
                  &arena,
                  constant_level(
                      &arena,
                      long_natural(
                          &arena,
                          "1234567890123456789012345678901234567891")),
                  parameter_zero_plus_forty, &long_absorbed) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(
                  long_absorbed,
                  long_natural(
                      &arena, "1234567890123456789012345678901234567891"),
                  1u),
          "a long natural constant is dropped exactly under a long offset "
          "that reaches it");

    /* The maximum of two offsets of one parameter is the larger offset. */
    const CettaPrimeLevelV1 *larger_offset = NULL;
    const CettaPrimeLevelV1 *larger_long_offset = NULL;
    CHECK(cetta_prime_level_maximum_v1(
              &arena, right_order, parameter_zero_plus_seven,
              &larger_offset) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(larger_offset, parameter_zero_plus_seven) &&
              cetta_prime_level_maximum_v1(
                  &arena, parameter_zero_plus_seven, right_order,
                  &larger_offset) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(larger_offset, parameter_zero_plus_seven) &&
              !level_equal(larger_offset, right_order),
          "the maximum of two offsets of a parameter is the larger one");
    CHECK(cetta_prime_level_maximum_v1(
              &arena, parameter_zero_plus_forty, parameter_zero_plus_seven,
              &larger_long_offset) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(larger_long_offset, parameter_zero_plus_forty) &&
              cetta_prime_level_maximum_v1(
                  &arena, parameter_zero_plus_forty,
                  parameter_zero_plus_forty_and_one, &larger_long_offset) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(
                  larger_long_offset, parameter_zero_plus_forty_and_one) &&
              !level_equal(larger_long_offset, parameter_zero_plus_forty),
          "the maximum of two long offsets of a parameter is the larger "
          "one");

    /* Substitution and evaluation with notations: in max(p0 + 1, p1), p0
     * becomes omega and p1 becomes max(5, p3). */
    SubstitutionFixture transfinite_substitution = {
        .parameter_zero = level_omega,
        .parameter_one = NULL,
    };
    CHECK(cetta_prime_level_maximum_v1(
              &arena, five, parameter_three,
              &transfinite_substitution.parameter_one) ==
              CETTA_PRIME_LEVEL_OK_V1,
          "substitution fixture levels construct");
    const CettaPrimeLevelV1 *transfinite_substituted = NULL;
    CettaPrimeLevelViewV1 transfinite_view = {0};
    CHECK(cetta_prime_level_substitute_v1(
              &arena, source, substitute_fixture, &transfinite_substitution,
              &transfinite_substituted) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_view_v1(
                  transfinite_substituted, &transfinite_view) &&
              notation_is(transfinite_view.constant, omega_plus_one) &&
              transfinite_view.parameter_count == 1u &&
              transfinite_view.parameters[0].parameter == 3u &&
              number_is(transfinite_view.parameters[0].offset, 0u),
          "substitution raises a notation by the offset of its parameter");
    const CettaPrimeLevelNotationV1 *transfinite_valuation[] = {
        omega, NULL, NULL, omega_squared,
    };
    CHECK(cetta_prime_level_evaluate_v1(
              &arena, source, valuation_fixture,
              (void *)transfinite_valuation, &evaluated) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(evaluated, omega_plus_one) &&
              cetta_prime_level_evaluate_v1(
                  &arena, transfinite_substituted, valuation_fixture,
                  (void *)transfinite_valuation, &evaluated) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(evaluated, omega_squared),
          "evaluation takes the largest notation among constant and atoms");

    /* A last term beyond the machine numbers grows like any other. */
    const CettaPrimeLevelNotationV1 *omega_plus_largest =
        cantor(&arena, n1, 1u, natural(&arena, UINT64_MAX));
    const CettaPrimeLevelV1 *level_beyond = NULL;
    CettaPrimeLevelViewV1 level_beyond_view = {0};
    CHECK(notation_is(
              raised(&arena, omega_plus_largest, 1u),
              cantor(&arena, n1, 1u,
                     long_natural(&arena, "18446744073709551616"))) &&
              order(omega_plus_largest,
                    raised(&arena, omega_plus_largest, 1u)) < 0 &&
              cetta_prime_level_successor_v1(
                  &arena, constant_level(&arena, omega_plus_largest),
                  &level_beyond) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_view_v1(level_beyond, &level_beyond_view) &&
              notation_is(
                  level_beyond_view.constant,
                  raised(&arena, omega_plus_largest, 1u)) &&
              !notation_is(level_beyond_view.constant, omega_plus_largest),
          "a last term at the largest machine number has a successor");

    /* Exponents nest to any depth, and nothing recurses on that depth.  A
     * tower of a hundred thousand omegas is compared with one built apart,
     * with one that differs only at its innermost exponent, and with its
     * successor. */
    enum { tower_omegas = 100000 };
    const CettaPrimeLevelNotationV1 *tower = n1;
    const CettaPrimeLevelNotationV1 *twin = natural(&arena, 1u);
    const CettaPrimeLevelNotationV1 *taller_within = n2;
    bool towers_built = true;
    for (unsigned height = 0u; height < tower_omegas; height++) {
        const CettaPrimeLevelNotationV1 *next[3] = {NULL, NULL, NULL};
        const CettaPrimeLevelNotationV1 *under[3] = {
            tower, twin, taller_within,
        };
        for (size_t index = 0u; index < 3u; index++)
            if (cetta_prime_level_notation_v1(
                    &arena, under[index], number(&arena, 1u), NULL,
                    &next[index]) != CETTA_PRIME_LEVEL_OK_V1)
                towers_built = false;
        tower = next[0];
        twin = next[1];
        taller_within = next[2];
    }
    CHECK(towers_built && tower && twin && taller_within && tower != twin,
          "a tower of a hundred thousand omegas is a notation");
    CHECK(order(tower, tower) == 0 && order(tower, twin) == 0 &&
              order(twin, tower) == 0,
          "a deep notation is equal to itself and to one built apart");
    CHECK(order(tower, taller_within) < 0 && order(taller_within, tower) > 0 &&
              order(tower, omega_to_omega) > 0 &&
              order(omega_to_omega, tower) < 0,
          "deep notations that differ at the innermost exponent are ordered "
          "by it");
    const CettaPrimeLevelNotationV1 *tower_successor =
        raised(&arena, tower, 1u);
    CHECK(order(tower, tower_successor) < 0 &&
              order(twin, tower_successor) < 0 &&
              order(tower_successor, twin) > 0 &&
              order(tower_successor, taller_within) < 0 &&
              notation_is(under_successor(&arena, tower_successor), twin) &&
              under_successor(&arena, tower) == tower,
          "a deep notation is below its successor, and is a limit");
    CHECK(notation_is(sum(&arena, tower, twin), product(&arena, tower, n2)) &&
              order(sum(&arena, tower, twin), tower_successor) > 0 &&
              notation_is(sum(&arena, twin, taller_within), taller_within) &&
              notation_is(sum(&arena, omega, tower), tower) &&
              !notation_is(sum(&arena, tower, omega), tower),
          "the arithmetic of deep notations compares their exponents");
    const CettaPrimeLevelNotationV1 *one_taller = NULL;
    CHECK(cetta_prime_level_notation_v1(
              &arena, tower, number(&arena, 1u), NULL, &one_taller) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(power(&arena, omega, twin), one_taller) &&
              order(tower, one_taller) < 0 &&
              order(taller_within, one_taller) < 0,
          "omega to the power of a deep notation is one omega taller");
    size_t tower_terms = 0u;
    void *tower_height = NULL;
    CHECK(cetta_prime_level_notation_fold_read_v1(
              cetta_prime_level_notation_read_v1, &height_fold, &tower_terms,
              tower, &tower_height) == CETTA_PRIME_LEVEL_OK_V1 &&
              (uintptr_t)tower_height == (uintptr_t)tower_omegas + 1u &&
              tower_terms == (size_t)tower_omegas + 1u,
          "a value is computed over a deep notation from its parts");
    size_t long_power_terms = 0u;
    void *long_power_height = NULL;
    CHECK(cetta_prime_level_notation_fold_read_v1(
              cetta_prime_level_notation_read_v1, &height_fold,
              &long_power_terms, long_power, &long_power_height) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              (uintptr_t)long_power_height == 2u &&
              long_power_terms == 5001u + 5000u,
          "a value is computed over a long notation from its parts");
    int unread = 7;
    void *unfolded = (void *)1;
    CHECK(cetta_prime_level_notation_compare_read_v1(
              NULL, cetta_prime_level_natural_order_v1, tower, twin,
              &unread) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              unread == 0 &&
              cetta_prime_level_notation_compare_read_v1(
                  cetta_prime_level_notation_read_v1, NULL, tower, twin,
                  &unread) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              cetta_prime_level_notation_fold_read_v1(
                  cetta_prime_level_notation_read_v1, NULL, NULL, tower,
                  &unfolded) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              unfolded == NULL,
          "a comparison or a computation without its parts is refused");

    /* The levels above every Cantor normal form: the level of the sort of
     * all sets, of its sort, and so on.  Every Cantor normal form is below
     * every one of them, and among themselves they are ordered by their
     * number. */
    const CettaPrimeLevelNotationV1 *above_zero = above(&arena, 0u);
    const CettaPrimeLevelNotationV1 *above_one = above(&arena, 1u);
    const CettaPrimeLevelNotationV1 *above_two = above(&arena, 2u);
    const CettaPrimeLevelNotationV1 *above_forty = NULL;
    CHECK(cetta_prime_level_notation_above_v1(
              &arena, forty, &above_forty) == CETTA_PRIME_LEVEL_OK_V1 &&
              above_forty != NULL,
          "the level a long number above the Cantor normal forms has a "
          "notation");
    const CettaPrimeLevelNotationV1 *below_all[] = {
        NULL, n1, n7, omega, omega_plus_one, omega_times_two, omega_squared,
        omega_to_omega, omega_times_forty, long_power, many_terms, tower,
        tower_successor, one_taller, long_natural(&arena, forty_digits),
    };
    const CettaPrimeLevelNotationV1 *above_all[] = {
        above_zero, above_one, above_two, above_forty,
    };
    bool cantor_below_above = true;
    for (size_t low = 0u; low < sizeof below_all / sizeof below_all[0]; low++)
        for (size_t high = 0u;
             high < sizeof above_all / sizeof above_all[0]; high++)
            if (order(below_all[low], above_all[high]) >= 0 ||
                order(above_all[high], below_all[low]) <= 0)
                cantor_below_above = false;
    CHECK(cantor_below_above,
          "every Cantor normal form is below every level above them");
    bool above_ordered = true;
    for (size_t left = 0u; left < sizeof above_all / sizeof above_all[0];
         left++)
        for (size_t right = 0u;
             right < sizeof above_all / sizeof above_all[0]; right++) {
            int between = order(above_all[left], above_all[right]);
            if ((left < right && between >= 0) ||
                (left == right && between != 0) ||
                (left > right && between <= 0))
                above_ordered = false;
        }
    CHECK(above_ordered && order(above_one, above(&arena, 1u)) == 0 &&
              order(above_zero, above(&arena, 0u)) == 0 &&
              !notation_is(above_zero, above_one) &&
              !notation_is(above_zero, NULL) &&
              !notation_is(above_one, n1),
          "the levels above the Cantor normal forms are ordered by their "
          "number");
    const CettaPrimeLevelNaturalV1 *above_number =
        (const CettaPrimeLevelNaturalV1 *)1;
    CettaPrimeLevelNotationTermV1 above_term = {0};
    CHECK(cetta_prime_level_notation_above_value_v1(
              above_zero, &above_number) &&
              above_number == NULL &&
              cetta_prime_level_notation_above_value_v1(
                  above_two, &above_number) &&
              number_is(above_number, 2u) &&
              cetta_prime_level_notation_above_value_v1(
                  above_forty, &above_number) &&
              decimal_is(&arena, above_number, forty_digits),
          "a level above the Cantor normal forms tells which one it is");
    CHECK(!cetta_prime_level_notation_above_value_v1(NULL, &above_number) &&
              !cetta_prime_level_notation_above_value_v1(n7, &above_number) &&
              !cetta_prime_level_notation_above_value_v1(
                  omega, &above_number) &&
              !cetta_prime_level_notation_above_value_v1(
                  tower, &above_number),
          "a Cantor normal form is no level above them");
    CHECK(!cetta_prime_level_notation_term_v1(above_zero, &above_term) &&
              !cetta_prime_level_notation_term_v1(above_two, &above_term) &&
              !cetta_prime_level_notation_natural_value_v1(above_zero, NULL) &&
              !cetta_prime_level_notation_natural_value_v1(above_two, NULL) &&
              cetta_prime_level_notation_term_v1(omega, &above_term),
          "a level above the Cantor normal forms has no terms and is no "
          "natural number");
    /* Successor and offset: the k-th successor of the n-th is the
     * (n + k)-th.  The 0-th is a limit. */
    CHECK(notation_is(raised(&arena, above_zero, 1u), above_one) &&
              notation_is(raised(&arena, above_one, 1u), above_two) &&
              notation_is(raised(&arena, above_two, 5u), above(&arena, 7u)) &&
              raised(&arena, above_two, 0u) == above_two &&
              order(above_zero, raised(&arena, above_zero, 1u)) < 0,
          "the successor of a level above the Cantor normal forms is the "
          "next one");
    const CettaPrimeLevelNotationV1 *above_long_shift = NULL;
    CHECK(cetta_prime_level_notation_offset_v1(
              &arena, above_one, forty, &above_long_shift) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_notation_above_value_v1(
                  above_long_shift, &above_number) &&
              decimal_is(
                  &arena, above_number,
                  "1234567890123456789012345678901234567891") &&
              order(above_forty, above_long_shift) < 0 &&
              notation_is(above_long_shift, raised(&arena, above_forty, 1u)),
          "an offset of forty digits above the Cantor normal forms adds to "
          "the number");
    CHECK(under_successor(&arena, above_zero) == above_zero &&
              notation_is(under_successor(&arena, above_one), above_zero) &&
              notation_is(under_successor(&arena, above_two), above_one) &&
              !notation_is(under_successor(&arena, above_two), above_two) &&
              !notation_is(under_successor(&arena, above_one), omega),
          "the 0-th level above the Cantor normal forms is a limit, and "
          "under the next one is the one before");
    const CettaPrimeLevelNotationV1 *mixed[] = {
        NULL, n7, omega, omega_plus_one, omega_to_omega, above_zero,
        above_one, above_two,
    };
    bool mixed_under_is_adjoint = true;
    for (size_t left = 0u; left < sizeof mixed / sizeof mixed[0]; left++)
        for (size_t right = 0u; right < sizeof mixed / sizeof mixed[0];
             right++)
            if ((order(mixed[left], raised(&arena, mixed[right], 1u)) <= 0) !=
                (order(under_successor(&arena, mixed[left]),
                       mixed[right]) <= 0))
                mixed_under_is_adjoint = false;
    CHECK(mixed_under_is_adjoint,
          "above the Cantor normal forms too a notation is at most "
          "level + 1 exactly when the one under it is at most level");
    /* Maximum, order and equality of levels go through the order of the
     * notations. */
    const CettaPrimeLevelV1 *level_above_zero =
        constant_level(&arena, above_zero);
    const CettaPrimeLevelV1 *level_above_one =
        constant_level(&arena, above_one);
    const CettaPrimeLevelV1 *level_tower = constant_level(&arena, tower);
    const CettaPrimeLevelV1 *above_maximum = NULL;
    const CettaPrimeLevelV1 *above_successor = NULL;
    CHECK(cetta_prime_level_maximum_v1(
              &arena, level_above_zero, level_omega, &above_maximum) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(above_maximum, level_above_zero) &&
              cetta_prime_level_maximum_v1(
                  &arena, level_tower, level_above_zero, &above_maximum) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(above_maximum, level_above_zero) &&
              cetta_prime_level_maximum_v1(
                  &arena, level_above_one, level_above_zero,
                  &above_maximum) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(above_maximum, level_above_one) &&
              !level_equal(above_maximum, level_above_zero),
          "the maximum with a level above the Cantor normal forms is the "
          "larger notation");
    CHECK(cetta_prime_level_successor_v1(
              &arena, level_above_zero, &above_successor) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(above_successor, level_above_one) &&
              !level_equal(above_successor, level_above_zero),
          "the successor of the level of the sort of all sets is the level "
          "of its sort");
    CHECK(level_le(five, level_above_zero) &&
              level_le(level_omega, level_above_zero) &&
              level_le(level_tower, level_above_zero) &&
              level_le(level_above_zero, level_above_zero) &&
              level_le(level_above_zero, level_above_one) &&
              !level_le(level_above_one, level_above_zero) &&
              !level_le(level_above_zero, level_omega) &&
              !level_le(level_above_zero, level_tower),
          "a level above the Cantor normal forms is above every written "
          "level and under none");

    /* A parameter stands for a level an author writes: a Cantor normal
     * form.  So a parameter, raised by any offset, is below every level
     * above the Cantor normal forms, and none of those is below it. */
    const CettaPrimeLevelV1 *level_above_forty =
        constant_level(&arena, above_forty);
    const CettaPrimeLevelV1 *parameters_raised[] = {
        parameter_zero, parameter_three, right_order,
        parameter_zero_plus_seven, parameter_zero_plus_forty,
        parameter_zero_plus_forty_and_one, source,
    };
    const CettaPrimeLevelV1 *levels_above[] = {
        level_above_zero, level_above_one, level_above_forty,
    };
    bool parameters_below = true;
    bool none_below_parameters = true;
    for (size_t raised_index = 0u;
         raised_index < sizeof parameters_raised / sizeof parameters_raised[0];
         raised_index++)
        for (size_t above_index = 0u;
             above_index < sizeof levels_above / sizeof levels_above[0];
             above_index++) {
            if (!level_le(parameters_raised[raised_index],
                          levels_above[above_index]))
                parameters_below = false;
            if (level_le(levels_above[above_index],
                         parameters_raised[raised_index]) ||
                level_equal(levels_above[above_index],
                            parameters_raised[raised_index]))
                none_below_parameters = false;
        }
    CHECK(parameters_below,
          "a parameter raised by any offset is below every level above the "
          "Cantor normal forms");
    CHECK(none_below_parameters,
          "a level above the Cantor normal forms is below no parameter, "
          "whatever its offset");
    /* A Cantor normal form, however long, is one a parameter may stand for
     * and one it may exceed: neither is below the other.  With the Cantor
     * normal form as a constant beside the parameter, both are below. */
    const CettaPrimeLevelV1 *level_long_power =
        constant_level(&arena, long_power);
    const CettaPrimeLevelV1 *long_with_parameter = NULL;
    const CettaPrimeLevelV1 *tower_with_parameter = NULL;
    CHECK(!level_le(parameter_zero, level_long_power) &&
              !level_le(level_long_power, parameter_zero) &&
              !level_le(parameter_zero_plus_forty, level_long_power) &&
              !level_le(level_long_power, parameter_zero_plus_forty) &&
              !level_le(parameter_zero, level_tower) &&
              !level_le(level_tower, parameter_zero) &&
              !level_le(parameter_zero, level_omega) &&
              !level_le(level_omega, parameter_zero),
          "a parameter and a Cantor normal form beyond the natural numbers "
          "are ordered neither way");
    CHECK(cetta_prime_level_maximum_v1(
              &arena, level_long_power, parameter_zero_plus_seven,
              &long_with_parameter) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_maximum_v1(
                  &arena, parameter_zero, level_tower,
                  &tower_with_parameter) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(long_with_parameter, long_power, 1u) &&
              level_constant_is(tower_with_parameter, tower, 1u) &&
              level_le(parameter_zero_plus_seven, long_with_parameter) &&
              level_le(parameter_zero, long_with_parameter) &&
              level_le(level_long_power, long_with_parameter) &&
              level_le(level_omega, long_with_parameter) &&
              !level_le(long_with_parameter, level_long_power) &&
              !level_le(long_with_parameter, parameter_zero_plus_seven) &&
              !level_le(parameter_zero_plus_forty, long_with_parameter) &&
              level_le(long_with_parameter, level_above_zero) &&
              !level_le(level_above_zero, long_with_parameter) &&
              level_le(tower_with_parameter, level_above_one) &&
              !level_le(level_above_one, tower_with_parameter),
          "a long Cantor normal form beside a parameter is above both and "
          "below the levels above the Cantor normal forms");

    /* The canonical form.  Under a constant above the Cantor normal forms a
     * parameter atom adds nothing and is absorbed: the maximum is the
     * constant.  The successor and an offset keep it so. */
    const CettaPrimeLevelV1 *absorbing = NULL;
    const CettaPrimeLevelV1 *absorbing_other = NULL;
    const CettaPrimeLevelV1 *absorbing_raised = NULL;
    CHECK(cetta_prime_level_maximum_v1(
              &arena, level_above_zero, parameter_zero_plus_seven,
              &absorbing) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(absorbing, above_zero, 0u) &&
              level_equal(absorbing, level_above_zero) &&
              cetta_prime_level_maximum_v1(
                  &arena, source, level_above_zero, &absorbing_other) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(absorbing_other, above_zero, 0u) &&
              level_equal(absorbing_other, absorbing) &&
              cetta_prime_level_maximum_v1(
                  &arena, parameter_zero_plus_forty, level_above_one,
                  &absorbing_other) == CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(absorbing_other, above_one, 0u) &&
              level_equal(absorbing_other, level_above_one) &&
              !level_equal(absorbing_other, absorbing),
          "a parameter atom is absorbed under a constant above the Cantor "
          "normal forms");
    CHECK(cetta_prime_level_successor_v1(
              &arena, absorbing, &absorbing_raised) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(absorbing_raised, above_one, 0u) &&
              level_equal(absorbing_raised, level_above_one) &&
              cetta_prime_level_offset_v1(
                  &arena, absorbing, forty, &absorbing_raised) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(absorbing_raised, above_forty, 0u) &&
              cetta_prime_level_maximum_v1(
                  &arena, absorbing_raised, absorbing, &absorbing_raised) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(absorbing_raised, level_above_forty),
          "the successor, an offset and a maximum of such a level have no "
          "parameter atoms either");
    CHECK(cetta_prime_level_maximum_v1(
              &arena, level_omega, parameter_zero_plus_seven, &kept) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(kept, omega, 1u) &&
              !level_equal(kept, level_omega) &&
              cetta_prime_level_maximum_v1(
                  &arena, level_tower, parameter_zero, &kept) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_constant_is(kept, tower, 1u) &&
              !level_equal(kept, level_tower),
          "under a Cantor normal form a parameter atom is kept");

    /* No parameter stands for a level above the Cantor normal forms: one is
     * no replacement of a parameter and no value of a valuation.  In
     * max(p0 + 1, p1), p0 is refused the level of the sort of all sets, and
     * so is p1; a maximum that hides such a level is refused as well. */
    SubstitutionFixture above_substitution = {
        .parameter_zero = level_above_zero,
        .parameter_one = transfinite_substitution.parameter_one,
    };
    const CettaPrimeLevelV1 *above_substituted =
        (const CettaPrimeLevelV1 *)1;
    CHECK(cetta_prime_level_substitute_v1(
              &arena, source, substitute_fixture, &above_substitution,
              &above_substituted) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              above_substituted == NULL,
          "a level above the Cantor normal forms replaces no parameter");
    above_substitution.parameter_zero = level_omega;
    above_substitution.parameter_one = absorbing;
    above_substituted = (const CettaPrimeLevelV1 *)1;
    CHECK(cetta_prime_level_substitute_v1(
              &arena, source, substitute_fixture, &above_substitution,
              &above_substituted) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              above_substituted == NULL,
          "a maximum with a level above the Cantor normal forms replaces no "
          "parameter");
    above_substitution.parameter_one = long_with_parameter;
    CettaPrimeLevelViewV1 long_view = {0};
    CHECK(cetta_prime_level_substitute_v1(
              &arena, source, substitute_fixture, &above_substitution,
              &above_substituted) == CETTA_PRIME_LEVEL_OK_V1 &&
              cetta_prime_level_view_v1(above_substituted, &long_view) &&
              notation_is(long_view.constant, long_power) &&
              long_view.parameter_count == 1u &&
              long_view.parameters[0].parameter == 0u &&
              number_is(long_view.parameters[0].offset, 7u) &&
              cetta_prime_level_substitute_v1(
                  &arena, level_above_one, substitute_fixture,
                  &above_substitution, &above_substituted) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              level_equal(above_substituted, level_above_one),
          "a long Cantor normal form replaces a parameter, and a level "
          "above the Cantor normal forms has no parameter to replace");
    const CettaPrimeLevelNotationV1 *above_valuation[] = {
        above_zero, omega, NULL, tower,
    };
    const CettaPrimeLevelNotationV1 *written_valuation[] = {
        long_power, tower, NULL, omega,
    };
    evaluated = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_evaluate_v1(
              &arena, source, valuation_fixture, (void *)above_valuation,
              &evaluated) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              evaluated == NULL,
          "a valuation assigns no level above the Cantor normal forms");
    above_valuation[0] = omega;
    above_valuation[1] = above_two;
    evaluated = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_evaluate_v1(
              &arena, source, valuation_fixture, (void *)above_valuation,
              &evaluated) == CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              evaluated == NULL &&
              cetta_prime_level_evaluate_v1(
                  &arena, parameter_three, valuation_fixture,
                  (void *)above_valuation, &evaluated) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(evaluated, tower),
          "a valuation is refused for the parameters a level has, and for "
          "no others");
    CHECK(cetta_prime_level_evaluate_v1(
              &arena, source, valuation_fixture, (void *)written_valuation,
              &evaluated) == CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(evaluated, tower) &&
              cetta_prime_level_evaluate_v1(
                  &arena, absorbing, valuation_fixture,
                  (void *)written_valuation, &evaluated) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(evaluated, above_zero) &&
              cetta_prime_level_evaluate_v1(
                  &arena, level_above_one, valuation_fixture,
                  (void *)above_valuation, &evaluated) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(evaluated, above_one),
          "a valuation in the Cantor normal forms evaluates every level, "
          "one above them to itself");
    /* The order with parameters, against valuations.  A level here is the
     * maximum of a constant and of the parameters 0 and 1, each absent or
     * raised by an offset up to two.  The values are a set of Cantor normal
     * forms that holds, for every such level, zero and the successor of what
     * the level shows without a parameter.  Over every pair of levels the
     * order holds exactly when no valuation of the two parameters in those
     * values puts the left level above the right one, and the canonical
     * forms are the same exactly when no valuation tells the levels apart. */
    const CettaPrimeLevelNotationV1 *small_constants[] = {
        NULL, n1, n2, n3, omega, omega_plus_one, omega_times_two, above_zero,
        above_one,
    };
    const CettaPrimeLevelNotationV1 *small_values[] = {
        NULL, n1, n2, n3, n4, natural(&arena, 5u), omega, omega_plus_one,
        raised(&arena, omega, 2u), raised(&arena, omega, 3u),
        omega_times_two, raised(&arena, omega_times_two, 1u),
    };
    enum {
        small_constant_count = sizeof small_constants /
                               sizeof small_constants[0],
        small_value_count = sizeof small_values / sizeof small_values[0],
        small_atoms = 4,
        small_level_count = small_constant_count * small_atoms * small_atoms,
        small_valuation_count = small_value_count * small_value_count
    };
    static const CettaPrimeLevelV1 *small_levels[small_level_count];
    static const CettaPrimeLevelNotationV1
        *small_level_values[small_level_count][small_valuation_count];
    bool small_levels_built = true;
    for (size_t index = 0u; index < small_level_count; index++) {
        /* Zero for an absent parameter, and one more than its offset. */
        size_t atoms[2] = {
            index / small_atoms % small_atoms, index % small_atoms,
        };
        const CettaPrimeLevelV1 *level = NULL;
        if (cetta_prime_level_constant_v1(
                &arena, small_constants[index / small_atoms / small_atoms],
                &level) != CETTA_PRIME_LEVEL_OK_V1)
            small_levels_built = false;
        for (uint64_t parameter = 0u; level && parameter < 2u; parameter++) {
            const CettaPrimeLevelV1 *atom = NULL;
            const CettaPrimeLevelNaturalV1 *offset = NULL;
            if (atoms[parameter] == 0u) continue;
            if (cetta_prime_level_natural_v1(
                    &arena, atoms[parameter] - 1u, &offset) !=
                    CETTA_PRIME_LEVEL_OK_V1 ||
                cetta_prime_level_parameter_v1(&arena, parameter, &atom) !=
                    CETTA_PRIME_LEVEL_OK_V1 ||
                cetta_prime_level_offset_v1(&arena, atom, offset, &atom) !=
                    CETTA_PRIME_LEVEL_OK_V1 ||
                cetta_prime_level_maximum_v1(&arena, level, atom, &level) !=
                    CETTA_PRIME_LEVEL_OK_V1)
                small_levels_built = false;
        }
        small_levels[index] = level;
        for (size_t valuation_index = 0u;
             level && valuation_index < small_valuation_count;
             valuation_index++) {
            const CettaPrimeLevelNotationV1 *held[4] = {
                small_values[valuation_index / small_value_count],
                small_values[valuation_index % small_value_count], NULL, NULL,
            };
            if (cetta_prime_level_evaluate_v1(
                    &arena, level, valuation_fixture, (void *)held,
                    &small_level_values[index][valuation_index]) !=
                CETTA_PRIME_LEVEL_OK_V1)
                small_levels_built = false;
        }
        if (!level) small_levels_built = false;
    }
    CHECK(small_levels_built,
          "small levels are built and have a value at every small valuation");
    size_t order_disagreements = 0u;
    size_t equality_disagreements = 0u;
    size_t ordered_pairs = 0u;
    size_t equal_pairs = 0u;
    size_t separated_off_zero = 0u;
    for (size_t left = 0u; small_levels_built && left < small_level_count;
         left++)
        for (size_t right = 0u; right < small_level_count; right++) {
            bool le = false;
            bool equal = false;
            bool below_everywhere = true;
            bool same_everywhere = true;
            bool below_at_zero = true;
            if (cetta_prime_level_le_v1(
                    small_levels[left], small_levels[right], &le) !=
                    CETTA_PRIME_LEVEL_OK_V1 ||
                cetta_prime_level_equal_v1(
                    small_levels[left], small_levels[right], &equal) !=
                    CETTA_PRIME_LEVEL_OK_V1)
                order_disagreements++;
            for (size_t valuation_index = 0u;
                 valuation_index < small_valuation_count;
                 valuation_index++) {
                int between = 0;
                if (cetta_prime_level_notation_compare_v1(
                        small_level_values[left][valuation_index],
                        small_level_values[right][valuation_index],
                        &between) != CETTA_PRIME_LEVEL_OK_V1)
                    order_disagreements++;
                if (between > 0) below_everywhere = false;
                if (between != 0) same_everywhere = false;
                if (between > 0 && valuation_index == 0u)
                    below_at_zero = false;
            }
            if (le != below_everywhere) {
                if (order_disagreements == 0u)
                    fprintf(
                        stderr,
                        "the order of the small levels %zu and %zu is %d, "
                        "and their values say %d\n",
                        left, right, (int)le, (int)below_everywhere);
                order_disagreements++;
            }
            if (equal != same_everywhere) {
                if (equality_disagreements == 0u)
                    fprintf(
                        stderr,
                        "the equality of the small levels %zu and %zu is "
                        "%d, and their values say %d\n",
                        left, right, (int)equal, (int)same_everywhere);
                equality_disagreements++;
            }
            if (le) ordered_pairs++;
            if (equal) equal_pairs++;
            if (!le && below_at_zero) separated_off_zero++;
        }
    CHECK(small_levels_built && order_disagreements == 0u,
          "a level is at most another exactly when it is at every valuation "
          "of the parameters in the Cantor normal forms");
    /* Where the order fails, one of the valuations the criterion names puts
     * the left level above the right one: every parameter at zero, or one
     * parameter of the left level at the successor of what the right level
     * shows without it and the other at zero. */
    size_t unseparated = 0u;
    size_t separated_by_zero = 0u;
    size_t separated_by_parameter = 0u;
    for (size_t left = 0u; small_levels_built && left < small_level_count;
         left++)
        for (size_t right = 0u; right < small_level_count; right++) {
            bool le = true;
            CettaPrimeLevelViewV1 left_view = {0};
            CettaPrimeLevelViewV1 right_view = {0};
            if (cetta_prime_level_le_v1(
                    small_levels[left], small_levels[right], &le) !=
                    CETTA_PRIME_LEVEL_OK_V1 ||
                le)
                continue;
            cetta_prime_level_view_v1(small_levels[left], &left_view);
            cetta_prime_level_view_v1(small_levels[right], &right_view);
            bool separated = false;
            /* The candidate at index 0 is the zero valuation; the one at
             * index i + 1 raises the i-th parameter of the left level. */
            for (size_t candidate = 0u;
                 !separated && candidate <= left_view.parameter_count;
                 candidate++) {
                const CettaPrimeLevelNotationV1 *held[4] = {
                    NULL, NULL, NULL, NULL,
                };
                if (candidate != 0u) {
                    uint64_t raised_parameter =
                        left_view.parameters[candidate - 1u].parameter;
                    const CettaPrimeLevelNotationV1 *shown =
                        right_view.constant;
                    for (size_t at = 0u; at < right_view.parameter_count;
                         at++) {
                        const CettaPrimeLevelNotationV1 *offset = NULL;
                        if (right_view.parameters[at].parameter ==
                            raised_parameter)
                            continue;
                        if (cetta_prime_level_notation_natural_v1(
                                &arena, right_view.parameters[at].offset,
                                &offset) != CETTA_PRIME_LEVEL_OK_V1)
                            unseparated++;
                        if (order(offset, shown) > 0) shown = offset;
                    }
                    /* A right level above the Cantor normal forms shows no
                     * Cantor normal form: only its constant decides. */
                    if (cetta_prime_level_notation_above_value_v1(
                            shown, NULL))
                        continue;
                    held[raised_parameter] = raised(&arena, shown, 1u);
                }
                const CettaPrimeLevelNotationV1 *left_value = NULL;
                const CettaPrimeLevelNotationV1 *right_value = NULL;
                if (cetta_prime_level_evaluate_v1(
                        &arena, small_levels[left], valuation_fixture,
                        (void *)held, &left_value) !=
                        CETTA_PRIME_LEVEL_OK_V1 ||
                    cetta_prime_level_evaluate_v1(
                        &arena, small_levels[right], valuation_fixture,
                        (void *)held, &right_value) !=
                        CETTA_PRIME_LEVEL_OK_V1)
                    unseparated++;
                else if (order(left_value, right_value) > 0) {
                    separated = true;
                    if (candidate == 0u) separated_by_zero++;
                    else separated_by_parameter++;
                }
            }
            if (!separated) unseparated++;
        }
    CHECK(small_levels_built && unseparated == 0u &&
              separated_by_zero != 0u && separated_by_parameter != 0u,
          "where the order fails, a valuation the criterion names puts the "
          "left level above the right one");
    CHECK(small_levels_built && equality_disagreements == 0u,
          "two levels have one canonical form exactly when they have the "
          "same value at every valuation in the Cantor normal forms");
    /* Among them are pairs in order, pairs out of order that only a
     * valuation other than zero separates, and distinct spellings of one
     * level, as max(above 0, p0) and above 0 are. */
    CHECK(ordered_pairs > (size_t)small_level_count &&
              ordered_pairs <
                  (size_t)small_level_count * small_level_count &&
              separated_off_zero != 0u &&
              equal_pairs > (size_t)small_level_count,
          "the small levels are ordered, unordered, and equal across "
          "spellings");

    /* The least notation that reaches a given one under an offset: the
     * natural last term loses the offset or goes, a limit stays, and above
     * the Cantor normal forms the number loses the offset down to the 0-th
     * level, which is a limit. */
    const CettaPrimeLevelNotationV1 *omega_plus_five =
        raised(&arena, omega, 5u);
    CHECK(notation_is(
              under_offset(&arena, omega_plus_five, number(&arena, 3u)),
              raised(&arena, omega, 2u)) &&
              notation_is(
                  under_offset(&arena, omega_plus_five, number(&arena, 5u)),
                  omega) &&
              notation_is(
                  under_offset(&arena, omega_plus_five, number(&arena, 9u)),
                  omega) &&
              notation_is(
                  under_offset(&arena, omega_plus_five, forty), omega) &&
              !notation_is(
                  under_offset(&arena, omega_plus_five, number(&arena, 3u)),
                  omega) &&
              under_offset(&arena, omega_plus_five, NULL) == omega_plus_five,
          "under an offset the last term of a successor notation shrinks by "
          "the offset or goes");
    CHECK(notation_is(under_offset(&arena, n7, number(&arena, 3u)), n4) &&
              under_offset(&arena, n7, number(&arena, 7u)) == NULL &&
              under_offset(&arena, n7, number(&arena, 100u)) == NULL &&
              under_offset(&arena, n7, forty) == NULL &&
              under_offset(&arena, NULL, forty) == NULL &&
              under_offset(&arena, omega, forty) == omega &&
              under_offset(&arena, tower, number(&arena, 2u)) == tower,
          "under an offset a natural number loses the offset down to zero, "
          "and zero and the limits stay");
    const CettaPrimeLevelNotationV1 *long_last = cantor(
        &arena, n1, 1u,
        long_natural(&arena, "1000000000000000000000000005"));
    CHECK(notation_is(
              under_offset(&arena, long_last, decimal(&arena, "7")),
              cantor(&arena, n1, 1u,
                     long_natural(&arena, "999999999999999999999999998"))) &&
              notation_is(
                  under_offset(
                      &arena, long_last,
                      decimal(&arena, "1000000000000000005")),
                  cantor(&arena, n1, 1u,
                         long_natural(
                             &arena, "999999999000000000000000000"))) &&
              notation_is(
                  under_offset(
                      &arena, long_last,
                      decimal(&arena, "1000000000000000000000000005")),
                  omega) &&
              notation_is(
                  under_offset(
                      &arena, long_last,
                      decimal(&arena, "1000000000000000000000000006")),
                  omega) &&
              notation_is(
                  under_offset(
                      &arena, long_last,
                      decimal(&arena, "1000000000000000000000000004")),
                  omega_plus_one),
          "a long last term loses a long offset through every digit");
    CHECK(notation_is(
              under_offset(&arena, above(&arena, 5u), number(&arena, 3u)),
              above_two) &&
              notation_is(
                  under_offset(&arena, above(&arena, 5u), number(&arena, 5u)),
                  above_zero) &&
              notation_is(
                  under_offset(&arena, above(&arena, 5u), forty),
                  above_zero) &&
              notation_is(
                  under_offset(&arena, above_forty, number(&arena, 1u)),
                  under_successor(&arena, above_forty)) &&
              under_offset(&arena, above_zero, forty) == above_zero &&
              !notation_is(
                  under_offset(&arena, above_one, forty), omega) &&
              !notation_is(
                  under_offset(&arena, above(&arena, 5u), number(&arena, 3u)),
                  above_one),
          "above the Cantor normal forms the number loses the offset down "
          "to the 0-th level, and no further");
    const CettaPrimeLevelNotationV1 *offset_mixed[] = {
        NULL, n1, n7, omega, omega_plus_one, omega_plus_five, long_last,
        omega_to_omega, above_zero, above_one, above_two, above_forty,
    };
    const CettaPrimeLevelNaturalV1 *offsets[] = {
        NULL, number(&arena, 1u), number(&arena, 2u), number(&arena, 6u),
        forty,
    };
    bool under_offset_is_adjoint = true;
    bool under_offset_iterates = true;
    for (size_t at = 0u; at < sizeof offsets / sizeof offsets[0]; at++)
        for (size_t left = 0u;
             left < sizeof offset_mixed / sizeof offset_mixed[0]; left++) {
            const CettaPrimeLevelNotationV1 *under =
                under_offset(&arena, offset_mixed[left], offsets[at]);
            for (size_t right = 0u;
                 right < sizeof offset_mixed / sizeof offset_mixed[0];
                 right++) {
                const CettaPrimeLevelNotationV1 *reached =
                    (const CettaPrimeLevelNotationV1 *)1;
                if (cetta_prime_level_notation_offset_v1(
                        &arena, offset_mixed[right], offsets[at],
                        &reached) != CETTA_PRIME_LEVEL_OK_V1 ||
                    (order(offset_mixed[left], reached) <= 0) !=
                        (order(under, offset_mixed[right]) <= 0))
                    under_offset_is_adjoint = false;
            }
            /* An offset is that many successors. */
            uint64_t steps = 0u;
            if (cetta_prime_level_natural_fits_uint64_v1(
                    offsets[at], &steps)) {
                const CettaPrimeLevelNotationV1 *stepped =
                    offset_mixed[left];
                for (uint64_t step = 0u; step < steps; step++)
                    stepped = under_successor(&arena, stepped);
                if (!notation_is(stepped, under))
                    under_offset_iterates = false;
            }
        }
    CHECK(under_offset_is_adjoint,
          "a notation is at most level + offset exactly when the one under "
          "it by that offset is at most level");
    CHECK(under_offset_iterates,
          "under an offset is under a successor that many times");
    const CettaPrimeLevelNotationV1 *under_refused =
        (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_under_offset_v1(
              NULL, omega, forty, &under_refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              under_refused == NULL &&
              cetta_prime_level_notation_under_offset_v1(
                  &arena, omega, forty, NULL) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1,
          "the notation under an offset is refused without its parts");

    /* The work of the sum, product and power counts against a budget.
     * Within its budget an operation gives what it gives without one, and
     * it takes steps; with one step fewer than it takes it is incomplete,
     * and so it is with no steps at all. */
    static const struct {
        NotationOperation operation;
        const char *what;
    } budgeted[] = {
        {cetta_prime_level_notation_add_v1, "sum"},
        {cetta_prime_level_notation_mul_v1, "product"},
        {cetta_prime_level_notation_pow_v1, "power"},
    };
    const CettaPrimeLevelNotationV1 *work_left[] = {
        omega_plus_one, long_sum, omega_two_plus_three, n7,
        long_natural(&arena, forty_digits), omega_to_omega,
    };
    const CettaPrimeLevelNotationV1 *work_right[] = {
        n7, omega_plus_one, long_sum, natural(&arena, 40u),
        long_natural(&arena, forty_digits), omega_two_plus_three,
    };
    bool budget_gives_same = true;
    bool budget_is_exact = true;
    for (size_t index = 0u;
         index < sizeof budgeted / sizeof budgeted[0]; index++)
        for (size_t left = 0u;
             left < sizeof work_left / sizeof work_left[0]; left++)
            for (size_t right = 0u;
                 right < sizeof work_right / sizeof work_right[0];
                 right++) {
                const CettaPrimeLevelNotationV1 *plain = NULL;
                const CettaPrimeLevelNotationV1 *counted = NULL;
                const CettaPrimeLevelNotationV1 *short_of = NULL;
                uint64_t spent = 0u;
                uint64_t spent_again = 0u;
                /* A power of a forty-digit number to a forty-digit number
                 * is no notation memory holds; the others all are. */
                CettaPrimeLevelStatusV1 status = budgeted[index].operation(
                    &arena, work_left[left], work_right[right], NULL,
                    &plain);
                if (status != CETTA_PRIME_LEVEL_OK_V1) {
                    if (status != CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1)
                        budget_gives_same = false;
                    continue;
                }
                if (capped(&arena, budgeted[index].operation,
                           work_left[left], work_right[right],
                           UINT64_C(1) << 40, &counted, &spent) !=
                        CETTA_PRIME_LEVEL_OK_V1 ||
                    !notation_is(counted, plain) || spent == 0u) {
                    fprintf(
                        stderr, "the %s of the notations %zu and %zu "
                        "differs within a budget\n",
                        budgeted[index].what, left, right);
                    budget_gives_same = false;
                    continue;
                }
                if (capped(&arena, budgeted[index].operation,
                           work_left[left], work_right[right], spent,
                           &counted, &spent_again) !=
                        CETTA_PRIME_LEVEL_OK_V1 ||
                    !notation_is(counted, plain) || spent_again != spent ||
                    capped(&arena, budgeted[index].operation,
                           work_left[left], work_right[right], spent - 1u,
                           &short_of, &spent_again) !=
                        CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 ||
                    short_of != NULL || spent_again != spent - 1u ||
                    capped(&arena, budgeted[index].operation,
                           work_left[left], work_right[right], 0u,
                           &short_of, &spent_again) !=
                        CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 ||
                    short_of != NULL || spent_again != 0u) {
                    fprintf(
                        stderr, "the %s of the notations %zu and %zu "
                        "does not take its %" PRIu64 " steps\n",
                        budgeted[index].what, left, right, spent);
                    budget_is_exact = false;
                }
            }
    CHECK(budget_gives_same,
          "within its budget an operation gives what it gives without one");
    CHECK(budget_is_exact,
          "an operation is complete with the steps it takes and incomplete "
          "with one fewer");
    /* The steps are those of the work: the sum of two small numbers reads
     * one term, compares one pair of exponents, adds one limb and builds one
     * term; the product of two numbers of forty digits, five limbs each,
     * reads one term, multiplies twenty-five pairs of limbs and builds one
     * term; and omega + 1 to the power five thousand takes fourteen steps
     * for each factor. */
    const CettaPrimeLevelNotationV1 *stepped_result = NULL;
    uint64_t stepped = 0u;
    CHECK(capped(&arena, cetta_prime_level_notation_add_v1, n3, n4, 100u,
                 &stepped_result, &stepped) == CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(stepped_result, n7) && stepped == 4u &&
              capped(&arena, cetta_prime_level_notation_mul_v1,
                     long_natural(&arena, forty_digits),
                     long_natural(&arena, forty_digits), 100u,
                     &stepped_result, &stepped) == CETTA_PRIME_LEVEL_OK_V1 &&
              stepped == 27u &&
              capped(&arena, cetta_prime_level_notation_pow_v1,
                     omega_plus_one, five_thousand, 70000u, &stepped_result,
                     &stepped) == CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(stepped_result, long_power) && stepped == 70000u,
          "the steps of an operation are those of its terms and limbs");

    /* A power that takes more steps than its budget has is incomplete at
     * once, and takes what the budget had: omega + 1 to the power of a
     * thousand million has that many terms and more, and two to the power
     * of two to the power forty has more than a hundred thousand million
     * digits. */
    const CettaPrimeLevelNotationV1 *unfinished =
        (const CettaPrimeLevelNotationV1 *)1;
    uint64_t unfinished_spent = 0u;
    CHECK(capped(&arena, cetta_prime_level_notation_pow_v1, omega_plus_one,
                 natural(&arena, UINT64_C(1000000000)), 100000u, &unfinished,
                 &unfinished_spent) ==
              CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && unfinished_spent == 100000u,
          "a power of a thousand million terms is incomplete within a "
          "hundred thousand steps");
    unfinished = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(capped(&arena, cetta_prime_level_notation_pow_v1, n2,
                 natural(&arena, UINT64_C(1) << 40), 1000000u, &unfinished,
                 &unfinished_spent) ==
              CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && unfinished_spent == 1000000u,
          "a natural power of a hundred thousand million digits is "
          "incomplete within a million steps");
    unfinished = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(capped(&arena, cetta_prime_level_notation_pow_v1, omega_plus_one,
                 five_thousand, 2000u, &unfinished, &unfinished_spent) ==
              CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && unfinished_spent == 2000u &&
              capped(&arena, cetta_prime_level_notation_pow_v1,
                     omega_plus_one, five_thousand, 20000u, &unfinished,
                     &unfinished_spent) ==
              CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && unfinished_spent == 20000u &&
              capped(&arena, cetta_prime_level_notation_pow_v1,
                     omega_plus_one, five_thousand, UINT64_C(1) << 40,
                     &unfinished, &unfinished_spent) ==
              CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(unfinished, long_power) &&
              unfinished_spent > 20000u,
          "a power of five thousand terms is incomplete within fewer steps "
          "than it has terms, and complete within more");
    /* A budget within another one takes its steps from both: the operation
     * is complete where both have the steps it takes, and incomplete where
     * either has one fewer.  A budget that is not limited, within a limited
     * one, leaves the count to that one. */
    static const struct {
        bool inner_limited;
        uint64_t inner_short;
        uint64_t outer_short;
    } nested[] = {
        {true, 0u, 0u}, {true, 1u, 0u}, {true, 0u, 1u}, {false, 0u, 0u},
        {false, 0u, 1u},
    };
    bool nested_as_expected = true;
    for (size_t index = 0u; index < sizeof nested / sizeof nested[0];
         index++) {
        const uint64_t needed = 70000u;
        CettaPrimeLevelBudgetV1 outer = {
            .limited = true,
            .remaining = needed + 5u - nested[index].outer_short * 6u,
        };
        CettaPrimeLevelBudgetV1 inner = {
            .limited = nested[index].inner_limited,
            .remaining = needed + 9u - nested[index].inner_short * 10u,
            .within = &outer,
        };
        const CettaPrimeLevelNotationV1 *nested_power =
            (const CettaPrimeLevelNotationV1 *)1;
        CettaPrimeLevelStatusV1 nested_status =
            cetta_prime_level_notation_pow_v1(
                &arena, omega_plus_one, five_thousand, &inner, &nested_power);
        bool fits = nested[index].inner_short == 0u &&
                    nested[index].outer_short == 0u;
        if (fits
                ? nested_status != CETTA_PRIME_LEVEL_OK_V1 ||
                      !notation_is(nested_power, long_power) ||
                      outer.spent != needed || outer.remaining != 5u ||
                      (inner.limited
                           ? inner.spent != needed || inner.remaining != 9u
                           : inner.spent != 0u)
                : nested_status != CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 ||
                      nested_power != NULL ||
                      (nested[index].outer_short != 0u
                           ? outer.remaining != 0u
                           : inner.remaining != 0u))
            nested_as_expected = false;
    }
    CHECK(nested_as_expected,
          "a budget within another one takes its steps from both");
    /* A power beyond the steps of the outer budget is incomplete before any
     * storage is had for it, whatever the inner budget has. */
    CettaPrimeLevelBudgetV1 small_outer = {
        .limited = true, .remaining = 1000000u,
    };
    CettaPrimeLevelBudgetV1 large_inner = {
        .limited = true, .remaining = UINT64_MAX, .within = &small_outer,
    };
    unfinished = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_pow_v1(
              &arena, n2, natural(&arena, UINT64_C(1) << 40), &large_inner,
              &unfinished) == CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && small_outer.remaining == 0u &&
              small_outer.spent == 1000000u &&
              large_inner.remaining != 0u,
          "a power beyond the outer budget is incomplete for lack of steps, "
          "not of memory");

    /* The default allowance of a computation without a budget: a limited
     * budget of that many steps, marked as the allowance, within no other.
     * It is told apart from a budget given explicitly when it ends a
     * computation, and only then. */
    CettaPrimeLevelBudgetV1 allowance = {.remaining = 3u};
    cetta_prime_level_budget_allowance_v1(&allowance);
    CettaPrimeLevelBudgetV1 explicit_budget = {
        .limited = true, .remaining = 100u,
    };
    CettaPrimeLevelBudgetV1 counts_nothing = {.within = &allowance};
    CettaPrimeLevelBudgetV1 counts_nothing_alone = {.remaining = 7u};
    CHECK(allowance.limited && allowance.allowance &&
              allowance.remaining == CETTA_PRIME_LEVEL_DEFAULT_ALLOWANCE_V1 &&
              allowance.spent == 0u && allowance.within == NULL &&
              cetta_prime_level_budget_limited_v1(&allowance) &&
              cetta_prime_level_budget_limited_v1(&counts_nothing) &&
              cetta_prime_level_budget_limited_v1(&explicit_budget) &&
              !cetta_prime_level_budget_limited_v1(&counts_nothing_alone) &&
              !cetta_prime_level_budget_limited_v1(NULL),
          "the default allowance is a limited budget, and a budget within it "
          "counts its work");
    unfinished = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_pow_v1(
              &arena, omega_plus_one, five_thousand, &counts_nothing,
              &unfinished) == CETTA_PRIME_LEVEL_OK_V1 &&
              notation_is(unfinished, long_power) &&
              allowance.spent == 70000u &&
              allowance.remaining ==
                  CETTA_PRIME_LEVEL_DEFAULT_ALLOWANCE_V1 - 70000u &&
              !cetta_prime_level_budget_allowance_spent_v1(&counts_nothing),
          "a power of five thousand terms fits the default allowance");
    unfinished = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_pow_v1(
              &arena, omega_plus_one, natural(&arena, UINT64_C(1000000000)),
              &counts_nothing, &unfinished) ==
              CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && allowance.remaining == 0u &&
              cetta_prime_level_budget_allowance_spent_v1(&counts_nothing) &&
              cetta_prime_level_budget_allowance_spent_v1(&allowance),
          "a power of a thousand million terms spends the default "
          "allowance");
    unfinished = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_pow_v1(
              &arena, omega_plus_one, natural(&arena, 50u), &explicit_budget,
              &unfinished) == CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1 &&
              unfinished == NULL && explicit_budget.remaining == 0u &&
              !cetta_prime_level_budget_allowance_spent_v1(&explicit_budget) &&
              !cetta_prime_level_budget_allowance_spent_v1(NULL),
          "a budget given explicitly that is spent is not the allowance");

    /* A budget that is not limited counts nothing, like no budget. */
    CettaPrimeLevelBudgetV1 unlimited = {.limited = false, .remaining = 3u};
    unfinished = NULL;
    CHECK(cetta_prime_level_notation_pow_v1(
              &arena, omega_plus_one, natural(&arena, 50u), &unlimited,
              &unfinished) == CETTA_PRIME_LEVEL_OK_V1 &&
              term_count(unfinished) == 51u && unlimited.remaining == 3u &&
              unlimited.spent == 0u,
          "a budget that is not limited counts no steps");

    /* No author writes these levels: they are no operands of the sum, the
     * product or the power, and no part of a Cantor normal form. */
    static const NotationOperation operations[] = {
        cetta_prime_level_notation_add_v1, cetta_prime_level_notation_mul_v1,
        cetta_prime_level_notation_pow_v1,
    };
    bool above_refused = true;
    for (size_t index = 0u;
         index < sizeof operations / sizeof operations[0]; index++)
        if (!answers(&arena, operations[index], above_zero, n1,
                     CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1) ||
            !answers(&arena, operations[index], n1, above_zero,
                     CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1) ||
            !answers(&arena, operations[index], above_two, above_one,
                     CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1) ||
            !answers(&arena, operations[index], NULL, above_one,
                     CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1) ||
            !answers(&arena, operations[index], omega, above_one,
                     CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1))
            above_refused = false;
    CHECK(above_refused,
          "a level above the Cantor normal forms is no operand of the sum, "
          "the product or the power");
    refused = (const CettaPrimeLevelNotationV1 *)1;
    CHECK(cetta_prime_level_notation_v1(
              &arena, above_zero, number(&arena, 1u), NULL, &refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              refused == NULL &&
              cetta_prime_level_notation_v1(
                  &arena, omega, number(&arena, 1u), above_zero, &refused) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              refused == NULL &&
              notation_is(sum(&arena, omega, n1), omega_plus_one),
          "a level above the Cantor normal forms is no exponent and no "
          "remainder");
    void *above_height = (void *)1;
    size_t above_terms = 0u;
    CHECK(cetta_prime_level_notation_fold_read_v1(
              cetta_prime_level_notation_read_v1, &height_fold, &above_terms,
              above_two, &above_height) ==
              CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1 &&
              above_height == NULL && above_terms == 0u,
          "a computation over a notation is ended by the status of a part");

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
