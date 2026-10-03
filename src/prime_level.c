#include "prime_level.h"

#include <stdlib.h>
#include <string.h>

/* A natural number in base 10^9, least significant limb first.  The last
 * limb in use is not zero, so a number has one representation; zero has no
 * limbs and is the NULL natural. */
#define PRIME_LEVEL_LIMB_BASE UINT32_C(1000000000)
#define PRIME_LEVEL_LIMB_DIGITS 9u

struct CettaPrimeLevelNaturalV1 {
    size_t count;
    const uint32_t *limbs;
};

struct CettaPrimeLevelNotationV1 {
    /* omega^exponent * number + remainder.  Where `above` is set the
     * notation is the level `number` above every such sum, and has neither
     * exponent nor remainder. */
    const CettaPrimeLevelNotationV1 *exponent;
    const CettaPrimeLevelNaturalV1 *number;
    const CettaPrimeLevelNotationV1 *remainder;
    bool above;
};

struct CettaPrimeLevelV1 {
    const CettaPrimeLevelNotationV1 *constant;
    size_t parameter_count;
    CettaPrimeLevelParameterOffsetV1 parameters[];
};

static const uint32_t prime_level_one_limbs[1] = {1u};
static const CettaPrimeLevelNaturalV1 prime_level_one = {
    .count = 1u,
    .limbs = prime_level_one_limbs,
};

void cetta_prime_level_budget_allowance_v1(CettaPrimeLevelBudgetV1 *budget) {
    if (!budget) return;
    *budget = (CettaPrimeLevelBudgetV1){
        .limited = true,
        .remaining = CETTA_PRIME_LEVEL_DEFAULT_ALLOWANCE_V1,
        .allowance = true,
    };
}

bool cetta_prime_level_budget_limited_v1(
    const CettaPrimeLevelBudgetV1 *budget) {
    for (; budget; budget = budget->within)
        if (budget->limited) return true;
    return false;
}

bool cetta_prime_level_budget_allowance_spent_v1(
    const CettaPrimeLevelBudgetV1 *budget) {
    for (; budget; budget = budget->within)
        if (budget->allowance && budget->limited && budget->remaining == 0u)
            return true;
    return false;
}

/* Whether a budget, and every budget it stands within, has `amount` steps
 * left. */
static bool prime_level_affords(
    const CettaPrimeLevelBudgetV1 *budget, uint64_t amount) {
    for (; budget; budget = budget->within)
        if (budget->limited && budget->remaining < amount) return false;
    return true;
}

/* Take `amount` steps of a budget and of every budget it stands within.
 * Where one of them has fewer left, what it has is taken, and it is
 * exhausted. */
static bool prime_level_spend(
    CettaPrimeLevelBudgetV1 *budget, uint64_t amount) {
    bool enough = true;
    for (; budget; budget = budget->within) {
        if (!budget->limited) continue;
        uint64_t taken = amount;
        if (taken > budget->remaining) {
            taken = budget->remaining;
            enough = false;
        }
        budget->remaining -= taken;
        budget->spent = budget->spent > UINT64_MAX - taken
            ? UINT64_MAX : budget->spent + taken;
    }
    return enough;
}

/* Take one step for every pair of `left` things and `right` things. */
static bool prime_level_spend_pairs(
    CettaPrimeLevelBudgetV1 *budget, size_t left, size_t right) {
    return prime_level_spend(
        budget, left != 0u && right > UINT64_MAX / left
                    ? UINT64_MAX : (uint64_t)left * right);
}

/* Room in the arena for a natural of up to `count` limbs. */
static CettaPrimeLevelStatusV1 prime_level_natural_room(
    Arena *owner, size_t count, CettaPrimeLevelNaturalV1 **natural_out,
    uint32_t **limbs_out) {
    if (count >
        (SIZE_MAX - sizeof(CettaPrimeLevelNaturalV1) - 7u) / sizeof(uint32_t))
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    CettaPrimeLevelNaturalV1 *natural = arena_alloc(
        owner, sizeof(*natural) + count * sizeof(uint32_t));
    *natural_out = natural;
    *limbs_out = (uint32_t *)(natural + 1);
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* The natural whose limbs were written into that room: limbs that are zero
 * at the top are not part of it, and none left is zero. */
static const CettaPrimeLevelNaturalV1 *prime_level_natural_close(
    CettaPrimeLevelNaturalV1 *natural, const uint32_t *limbs, size_t count) {
    while (count > 0u && limbs[count - 1u] == 0u) count--;
    natural->count = count;
    natural->limbs = limbs;
    return count == 0u ? NULL : natural;
}

static CettaPrimeLevelStatusV1 prime_level_natural_copy(
    Arena *owner, const uint32_t *limbs, size_t count,
    const CettaPrimeLevelNaturalV1 **natural_out) {
    CettaPrimeLevelNaturalV1 *natural = NULL;
    uint32_t *room = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_natural_room(owner, count, &natural, &room);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    if (count != 0u) memcpy(room, limbs, count * sizeof(*limbs));
    *natural_out = prime_level_natural_close(natural, room, count);
    return CETTA_PRIME_LEVEL_OK_V1;
}

static int prime_level_natural_compare(
    const CettaPrimeLevelNaturalV1 *left,
    const CettaPrimeLevelNaturalV1 *right) {
    size_t left_count = left ? left->count : 0u;
    size_t right_count = right ? right->count : 0u;
    if (left_count != right_count) return left_count < right_count ? -1 : 1;
    for (size_t index = left_count; index > 0u; index--)
        if (left->limbs[index - 1u] != right->limbs[index - 1u])
            return left->limbs[index - 1u] < right->limbs[index - 1u]
                ? -1 : 1;
    return 0;
}

static CettaPrimeLevelStatusV1 prime_level_natural_add(
    Arena *owner, const CettaPrimeLevelNaturalV1 *left,
    const CettaPrimeLevelNaturalV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNaturalV1 **sum_out) {
    if (!left || !right) {
        *sum_out = left ? left : right;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    size_t longer = left->count > right->count ? left->count : right->count;
    if (!prime_level_spend(budget, longer))
        return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    CettaPrimeLevelNaturalV1 *natural = NULL;
    uint32_t *limbs = NULL;
    /* A count of limbs is a count of objects in memory, so one more is a
     * number. */
    CettaPrimeLevelStatusV1 status =
        prime_level_natural_room(owner, longer + 1u, &natural, &limbs);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    uint32_t carry = 0u;
    for (size_t index = 0u; index < longer; index++) {
        uint32_t total = carry;
        if (index < left->count) total += left->limbs[index];
        if (index < right->count) total += right->limbs[index];
        carry = total >= PRIME_LEVEL_LIMB_BASE;
        limbs[index] = carry ? total - PRIME_LEVEL_LIMB_BASE : total;
    }
    limbs[longer] = carry;
    *sum_out = prime_level_natural_close(natural, limbs, longer + 1u);
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* The product of two limb sequences, in `product`, which has room for the
 * limbs of both.  The count returned leaves out limbs that are zero at the
 * top. */
static size_t prime_level_limbs_multiply(
    const uint32_t *left, size_t left_count, const uint32_t *right,
    size_t right_count, uint32_t *product) {
    size_t count = left_count + right_count;
    memset(product, 0, count * sizeof(*product));
    for (size_t i = 0u; i < left_count; i++) {
        uint64_t carry = 0u;
        for (size_t j = 0u; j < right_count; j++) {
            uint64_t total = (uint64_t)left[i] * right[j] + product[i + j] +
                             carry;
            product[i + j] = (uint32_t)(total % PRIME_LEVEL_LIMB_BASE);
            carry = total / PRIME_LEVEL_LIMB_BASE;
        }
        product[i + right_count] = (uint32_t)carry;
    }
    while (count > 0u && product[count - 1u] == 0u) count--;
    return count;
}

static CettaPrimeLevelStatusV1 prime_level_natural_multiply(
    Arena *owner, const CettaPrimeLevelNaturalV1 *left,
    const CettaPrimeLevelNaturalV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNaturalV1 **product_out) {
    *product_out = NULL;
    if (!left || !right) return CETTA_PRIME_LEVEL_OK_V1;
    if (left->count > SIZE_MAX - right->count)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    if (!prime_level_spend_pairs(budget, left->count, right->count))
        return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    CettaPrimeLevelNaturalV1 *natural = NULL;
    uint32_t *limbs = NULL;
    CettaPrimeLevelStatusV1 status = prime_level_natural_room(
        owner, left->count + right->count, &natural, &limbs);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    size_t count = prime_level_limbs_multiply(
        left->limbs, left->count, right->limbs, right->count, limbs);
    *product_out = prime_level_natural_close(natural, limbs, count);
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* The difference of two natural numbers, the left one not the smaller. */
static CettaPrimeLevelStatusV1 prime_level_natural_subtract(
    Arena *owner, const CettaPrimeLevelNaturalV1 *left,
    const CettaPrimeLevelNaturalV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNaturalV1 **difference_out) {
    if (!right) {
        *difference_out = left;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    if (!prime_level_spend(budget, left->count))
        return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    CettaPrimeLevelNaturalV1 *difference = NULL;
    uint32_t *limbs = NULL;
    CettaPrimeLevelStatusV1 status = prime_level_natural_room(
        owner, left->count, &difference, &limbs);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    uint32_t borrow = 0u;
    for (size_t index = 0u; index < left->count; index++) {
        uint32_t limb = left->limbs[index];
        uint32_t taken =
            borrow + (index < right->count ? right->limbs[index] : 0u);
        borrow = limb < taken;
        limbs[index] = borrow ? limb + PRIME_LEVEL_LIMB_BASE - taken
                              : limb - taken;
    }
    *difference_out =
        prime_level_natural_close(difference, limbs, left->count);
    return CETTA_PRIME_LEVEL_OK_V1;
}

static unsigned prime_level_limb_digits(uint32_t limb) {
    unsigned digits = 1u;
    while (limb >= 10u) {
        limb /= 10u;
        digits++;
    }
    return digits;
}

/* A size is a machine number, so a count of objects in memory is one. */
_Static_assert(SIZE_MAX <= UINT64_MAX, "a size is a machine number");

/* A natural base of at least two to a natural power.  The power is computed
 * by squaring in working storage of its own, which holds every power on the
 * way: a base of d digits to the power k has at most d * k digits.  Where
 * d * k is no size the power alone fills more than a tenth of all addresses,
 * and there is no memory for it. */
static CettaPrimeLevelStatusV1 prime_level_natural_power(
    Arena *owner, const CettaPrimeLevelNaturalV1 *base,
    const CettaPrimeLevelNaturalV1 *exponent, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNaturalV1 **power_out) {
    *power_out = NULL;
    uint64_t steps = 0u;
    if (!exponent) {
        *power_out = &prime_level_one;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    if (base->count >
        (SIZE_MAX - PRIME_LEVEL_LIMB_DIGITS) / PRIME_LEVEL_LIMB_DIGITS)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    size_t digits = (base->count - 1u) * PRIME_LEVEL_LIMB_DIGITS +
                    prime_level_limb_digits(base->limbs[base->count - 1u]);
    if (!cetta_prime_level_natural_fits_uint64_v1(exponent, &steps) ||
        steps > SIZE_MAX / digits)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    /* The limbs of d * k digits, and three more for a product on the way.
     * Twice that many limbs are a size: they are eight ninths of d * k
     * bytes, and twenty-four. */
    size_t room = digits * (size_t)steps / PRIME_LEVEL_LIMB_DIGITS + 3u;
    /* The power has more than (d - 1) * k digits, and more than three
     * tenths of k, the base being two at the least; and the product that
     * gives its limbs takes a step for each of them but one.  A budget with
     * fewer steps than that is spent before any storage is had. */
    uint64_t least = (uint64_t)(digits - 1u) * steps / PRIME_LEVEL_LIMB_DIGITS;
    if (least < steps / 30u) least = steps / 30u;
    if (least != 0u && !prime_level_affords(budget, least - 1u)) {
        (void)prime_level_spend(budget, least);
        return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    }
    uint32_t *storage = malloc(2u * room * sizeof(*storage));
    if (!storage) return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    uint32_t *power = storage;
    uint32_t *next = storage + room;
    size_t count = 1u;
    power[0] = 1u;
    CettaPrimeLevelStatusV1 status = CETTA_PRIME_LEVEL_OK_V1;
    uint64_t bit = UINT64_C(1) << 63;
    while ((steps & bit) == 0u) bit >>= 1;
    for (; bit != 0u; bit >>= 1) {
        if (!prime_level_spend_pairs(budget, count, count)) {
            status = CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
            break;
        }
        size_t squared =
            prime_level_limbs_multiply(power, count, power, count, next);
        uint32_t *swap = power;
        power = next;
        next = swap;
        count = squared;
        if ((steps & bit) == 0u) continue;
        if (!prime_level_spend_pairs(budget, count, base->count)) {
            status = CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
            break;
        }
        size_t multiplied = prime_level_limbs_multiply(
            power, count, base->limbs, base->count, next);
        swap = power;
        power = next;
        next = swap;
        count = multiplied;
    }
    if (status == CETTA_PRIME_LEVEL_OK_V1)
        status = prime_level_natural_copy(owner, power, count, power_out);
    free(storage);
    return status;
}

CettaPrimeLevelStatusV1 cetta_prime_level_natural_v1(
    Arena *owner, uint64_t value,
    const CettaPrimeLevelNaturalV1 **natural_out) {
    if (natural_out) *natural_out = NULL;
    if (!owner || !natural_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (value == 1u) {
        *natural_out = &prime_level_one;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    uint32_t limbs[3] = {
        (uint32_t)(value % PRIME_LEVEL_LIMB_BASE),
        (uint32_t)(value / PRIME_LEVEL_LIMB_BASE % PRIME_LEVEL_LIMB_BASE),
        (uint32_t)(value / PRIME_LEVEL_LIMB_BASE / PRIME_LEVEL_LIMB_BASE),
    };
    size_t count = 3u;
    while (count > 0u && limbs[count - 1u] == 0u) count--;
    if (count == 0u) return CETTA_PRIME_LEVEL_OK_V1;
    return prime_level_natural_copy(owner, limbs, count, natural_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_natural_from_decimal_v1(
    Arena *owner, const char *digits,
    const CettaPrimeLevelNaturalV1 **natural_out) {
    if (natural_out) *natural_out = NULL;
    if (!owner || !digits || !natural_out || !*digits)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    size_t length = 0u;
    for (; digits[length]; length++)
        if (digits[length] < '0' || digits[length] > '9')
            return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    while (length > 0u && *digits == '0') {
        digits++;
        length--;
    }
    if (length == 0u) return CETTA_PRIME_LEVEL_OK_V1;
    /* Fewer limbs than digits, and the digits are in memory. */
    size_t count = (length - 1u) / PRIME_LEVEL_LIMB_DIGITS + 1u;
    CettaPrimeLevelNaturalV1 *natural = NULL;
    uint32_t *limbs = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_natural_room(owner, count, &natural, &limbs);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    for (size_t index = 0u; index < count; index++) {
        size_t end = length - index * PRIME_LEVEL_LIMB_DIGITS;
        size_t start =
            end > PRIME_LEVEL_LIMB_DIGITS ? end - PRIME_LEVEL_LIMB_DIGITS : 0u;
        uint32_t limb = 0u;
        for (size_t at = start; at < end; at++)
            limb = limb * 10u + (uint32_t)(digits[at] - '0');
        limbs[index] = limb;
    }
    *natural_out = prime_level_natural_close(natural, limbs, count);
    return CETTA_PRIME_LEVEL_OK_V1;
}

bool cetta_prime_level_natural_fits_uint64_v1(
    const CettaPrimeLevelNaturalV1 *natural, uint64_t *value_out) {
    static const uint64_t two_limbs =
        (uint64_t)PRIME_LEVEL_LIMB_BASE * PRIME_LEVEL_LIMB_BASE;
    uint64_t low = 0u;
    uint64_t high = 0u;
    if (natural) {
        if (natural->count > 3u) return false;
        low = natural->limbs[0];
        if (natural->count > 1u)
            low += (uint64_t)natural->limbs[1] * PRIME_LEVEL_LIMB_BASE;
        if (natural->count > 2u) high = natural->limbs[2];
        if (high > UINT64_MAX / two_limbs) return false;
        high *= two_limbs;
        if (high > UINT64_MAX - low) return false;
    }
    if (value_out) *value_out = high + low;
    return true;
}

CettaPrimeLevelStatusV1 cetta_prime_level_natural_decimal_v1(
    Arena *owner, const CettaPrimeLevelNaturalV1 *natural,
    const char **digits_out) {
    if (digits_out) *digits_out = NULL;
    if (!owner || !digits_out) return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (!natural) {
        *digits_out = "0";
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    uint32_t top = natural->limbs[natural->count - 1u];
    size_t top_digits = prime_level_limb_digits(top);
    if (natural->count - 1u >
        (SIZE_MAX - top_digits - 8u) / PRIME_LEVEL_LIMB_DIGITS)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    size_t length =
        (natural->count - 1u) * PRIME_LEVEL_LIMB_DIGITS + top_digits;
    char *text = arena_alloc(owner, length + 1u);
    for (size_t at = top_digits; at > 0u; at--) {
        text[at - 1u] = (char)('0' + top % 10u);
        top /= 10u;
    }
    size_t written = top_digits;
    for (size_t index = natural->count - 1u; index > 0u; index--) {
        uint32_t limb = natural->limbs[index - 1u];
        for (size_t at = PRIME_LEVEL_LIMB_DIGITS; at > 0u; at--) {
            text[written + at - 1u] = (char)('0' + limb % 10u);
            limb /= 10u;
        }
        written += PRIME_LEVEL_LIMB_DIGITS;
    }
    text[length] = '\0';
    *digits_out = text;
    return CETTA_PRIME_LEVEL_OK_V1;
}

int cetta_prime_level_natural_order_v1(const void *left, const void *right) {
    return prime_level_natural_compare(left, right);
}

bool cetta_prime_level_notation_read_v1(
    const void *handle, CettaPrimeLevelNotationReadTermV1 *term_out) {
    const CettaPrimeLevelNotationV1 *notation = handle;
    if (!notation) return false;
    *term_out = (CettaPrimeLevelNotationReadTermV1){
        .above = notation->above,
        .exponent = notation->exponent,
        .number = notation->number,
        .remainder = notation->remainder,
    };
    return true;
}

/* A work stack starts in storage of its caller and moves to the heap when a
 * notation nests deeper than that storage holds. */
#define PRIME_LEVEL_STACK_START 16u

static bool prime_level_stack_room(
    void **frames, size_t *capacity, size_t count, size_t frame_size,
    void *start) {
    if (count < *capacity) return true;
    if (*capacity > SIZE_MAX / 2u / frame_size) return false;
    size_t grown = *capacity * 2u;
    void *moved = *frames == start
        ? malloc(grown * frame_size)
        : realloc(*frames, grown * frame_size);
    if (!moved) return false;
    if (*frames == start) memcpy(moved, start, count * frame_size);
    *frames = moved;
    *capacity = grown;
    return true;
}

typedef struct {
    const void *left;
    const void *right;
} PrimeLevelComparedTerms;

/* The order of two notations, and in `*pairs_out`, where it is given, how
 * many pairs of notations were compared to find it. */
static CettaPrimeLevelStatusV1 prime_level_compare_read(
    CettaPrimeLevelNotationReaderV1 reader,
    CettaPrimeLevelNumberOrderV1 number_order, const void *left,
    const void *right, int *order_out, uint64_t *pairs_out) {
    if (order_out) *order_out = 0;
    if (!reader || !number_order || !order_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    /* The pairs of terms whose exponents are being compared, outermost
     * first.  A pair goes on with its coefficients and remainders once its
     * exponents are found equal. */
    PrimeLevelComparedTerms start[PRIME_LEVEL_STACK_START];
    void *frames = start;
    size_t capacity = PRIME_LEVEL_STACK_START;
    size_t depth = 0u;
    CettaPrimeLevelStatusV1 status = CETTA_PRIME_LEVEL_OK_V1;
    int order = 0;
    uint64_t pairs = 0u;
    for (;;) {
        CettaPrimeLevelNotationReadTermV1 left_term = {0};
        CettaPrimeLevelNotationReadTermV1 right_term = {0};
        if (pairs != UINT64_MAX) pairs++;
        if (left != right) {
            bool left_positive = reader(left, &left_term);
            bool right_positive = reader(right, &right_term);
            if (!left_positive || !right_positive) {
                order = left_positive ? 1 : right_positive ? -1 : 0;
            } else if (left_term.above || right_term.above) {
                order = !left_term.above ? -1
                    : !right_term.above ? 1
                    : number_order(left_term.number, right_term.number);
            } else {
                if (!prime_level_stack_room(
                        &frames, &capacity, depth,
                        sizeof(PrimeLevelComparedTerms), start)) {
                    status = CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
                    break;
                }
                ((PrimeLevelComparedTerms *)frames)[depth++] =
                    (PrimeLevelComparedTerms){.left = left, .right = right};
                left = left_term.exponent;
                right = right_term.exponent;
                continue;
            }
            if (order != 0) break;
        }
        /* These two are equal.  They are the notations compared, or the
         * exponents of the innermost pair of terms. */
        if (depth == 0u) break;
        PrimeLevelComparedTerms terms =
            ((PrimeLevelComparedTerms *)frames)[--depth];
        reader(terms.left, &left_term);
        reader(terms.right, &right_term);
        order = number_order(left_term.number, right_term.number);
        if (order != 0) break;
        left = left_term.remainder;
        right = right_term.remainder;
    }
    if (frames != start) free(frames);
    if (status == CETTA_PRIME_LEVEL_OK_V1) *order_out = order;
    if (pairs_out) *pairs_out = pairs;
    return status;
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_compare_read_v1(
    CettaPrimeLevelNotationReaderV1 reader,
    CettaPrimeLevelNumberOrderV1 number_order, const void *left,
    const void *right, int *order_out) {
    return prime_level_compare_read(
        reader, number_order, left, right, order_out, NULL);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_compare_v1(
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, int *order_out) {
    return cetta_prime_level_notation_compare_read_v1(
        cetta_prime_level_notation_read_v1,
        cetta_prime_level_natural_order_v1, left, right, order_out);
}

/* The order of two notations, with one step of the budget for every pair of
 * notations compared. */
static CettaPrimeLevelStatusV1 prime_level_notation_order(
    const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, CettaPrimeLevelBudgetV1 *budget,
    int *order_out) {
    uint64_t pairs = 0u;
    CettaPrimeLevelStatusV1 status = prime_level_compare_read(
        cetta_prime_level_notation_read_v1,
        cetta_prime_level_natural_order_v1, left, right, order_out, &pairs);
    if (status == CETTA_PRIME_LEVEL_OK_V1 &&
        !prime_level_spend(budget, pairs))
        status = CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    return status;
}

typedef struct {
    const void *handle;
    /* The value of the exponent, once the remainder is being computed. */
    void *exponent_value;
    bool exponent_done;
} PrimeLevelFoldedTerm;

CettaPrimeLevelStatusV1 cetta_prime_level_notation_fold_read_v1(
    CettaPrimeLevelNotationReaderV1 reader,
    const CettaPrimeLevelNotationFoldV1 *fold, void *context,
    const void *handle, void **value_out) {
    if (value_out) *value_out = NULL;
    if (!reader || !fold || !fold->zero || !fold->above || !fold->term ||
        !value_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    /* The terms whose value waits for the value of a part, outermost
     * first. */
    PrimeLevelFoldedTerm start[PRIME_LEVEL_STACK_START];
    void *frames = start;
    size_t capacity = PRIME_LEVEL_STACK_START;
    size_t depth = 0u;
    CettaPrimeLevelStatusV1 status = CETTA_PRIME_LEVEL_OK_V1;
    void *value = NULL;
    for (;;) {
        CettaPrimeLevelNotationReadTermV1 read = {0};
        bool positive = reader(handle, &read);
        if (positive && !read.above) {
            if (!prime_level_stack_room(
                    &frames, &capacity, depth, sizeof(PrimeLevelFoldedTerm),
                    start)) {
                status = CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
                break;
            }
            ((PrimeLevelFoldedTerm *)frames)[depth++] =
                (PrimeLevelFoldedTerm){.handle = handle};
            handle = read.exponent;
            continue;
        }
        status = positive ? fold->above(context, handle, &read, &value)
                          : fold->zero(context, handle, &value);
        /* Hand the value to the terms that wait for it: a term whose
         * exponent it is goes on with its remainder, and a term whose
         * remainder it is has its own value. */
        bool waiting = false;
        while (status == CETTA_PRIME_LEVEL_OK_V1 && depth > 0u) {
            PrimeLevelFoldedTerm *term =
                &((PrimeLevelFoldedTerm *)frames)[depth - 1u];
            reader(term->handle, &read);
            if (!term->exponent_done) {
                term->exponent_value = value;
                term->exponent_done = true;
                handle = read.remainder;
                waiting = true;
                break;
            }
            status = fold->term(
                context, term->handle, &read, term->exponent_value, value,
                &value);
            depth--;
        }
        if (!waiting) break;
    }
    if (frames != start) free(frames);
    if (status == CETTA_PRIME_LEVEL_OK_V1) *value_out = value;
    return status;
}

static bool prime_level_notation_is_above(
    const CettaPrimeLevelNotationV1 *notation) {
    return notation && notation->above;
}

/* The notation omega^exponent * coefficient + remainder, built with the steps
 * of a budget: one for the term, and those of the comparison that finds the
 * remainder below omega^exponent. */
static CettaPrimeLevelStatusV1 prime_level_notation_term(
    Arena *owner, const CettaPrimeLevelNotationV1 *exponent,
    const CettaPrimeLevelNaturalV1 *coefficient,
    const CettaPrimeLevelNotationV1 *remainder,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **notation_out) {
    *notation_out = NULL;
    if (!coefficient || prime_level_notation_is_above(exponent) ||
        prime_level_notation_is_above(remainder))
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (!prime_level_spend(budget, 1u))
        return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    /* A nonzero remainder is below omega^exponent exactly when its own
     * leading exponent is below the exponent. */
    if (remainder) {
        int order = 0;
        CettaPrimeLevelStatusV1 status = prime_level_notation_order(
            remainder->exponent, exponent, budget, &order);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        if (order >= 0) return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    }
    CettaPrimeLevelNotationV1 *notation =
        arena_alloc(owner, sizeof(*notation));
    *notation = (CettaPrimeLevelNotationV1){
        .exponent = exponent,
        .number = coefficient,
        .remainder = remainder,
    };
    *notation_out = notation;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *exponent,
    const CettaPrimeLevelNaturalV1 *coefficient,
    const CettaPrimeLevelNotationV1 *remainder,
    const CettaPrimeLevelNotationV1 **notation_out) {
    if (notation_out) *notation_out = NULL;
    if (!owner || !notation_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    return prime_level_notation_term(
        owner, exponent, coefficient, remainder, NULL, notation_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_natural_v1(
    Arena *owner, const CettaPrimeLevelNaturalV1 *value,
    const CettaPrimeLevelNotationV1 **notation_out) {
    if (notation_out) *notation_out = NULL;
    if (!owner || !notation_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (!value) return CETTA_PRIME_LEVEL_OK_V1;
    return cetta_prime_level_notation_v1(
        owner, NULL, value, NULL, notation_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_above_v1(
    Arena *owner, const CettaPrimeLevelNaturalV1 *n,
    const CettaPrimeLevelNotationV1 **notation_out) {
    if (notation_out) *notation_out = NULL;
    if (!owner || !notation_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    CettaPrimeLevelNotationV1 *notation =
        arena_alloc(owner, sizeof(*notation));
    *notation = (CettaPrimeLevelNotationV1){
        .number = n,
        .above = true,
    };
    *notation_out = notation;
    return CETTA_PRIME_LEVEL_OK_V1;
}

bool cetta_prime_level_notation_above_value_v1(
    const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 **n_out) {
    if (!prime_level_notation_is_above(notation)) return false;
    if (n_out) *n_out = notation->number;
    return true;
}

bool cetta_prime_level_notation_term_v1(
    const CettaPrimeLevelNotationV1 *notation,
    CettaPrimeLevelNotationTermV1 *term_out) {
    if (!notation || notation->above || !term_out) return false;
    *term_out = (CettaPrimeLevelNotationTermV1){
        .exponent = notation->exponent,
        .coefficient = notation->number,
        .remainder = notation->remainder,
    };
    return true;
}

bool cetta_prime_level_notation_natural_value_v1(
    const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 **value_out) {
    /* In normal form a term with exponent zero has no remainder. */
    if (notation && (notation->above || notation->exponent)) return false;
    if (value_out) *value_out = notation ? notation->number : NULL;
    return true;
}

/* The natural last term of a Cantor normal form: the term with exponent
 * zero, which in normal form is the last one, or zero when there is none. */
static const CettaPrimeLevelNaturalV1 *prime_level_notation_natural_part(
    const CettaPrimeLevelNotationV1 *notation) {
    while (notation && notation->exponent) notation = notation->remainder;
    return notation ? notation->number : NULL;
}

/* The Cantor normal form with its natural last term replaced by `natural`:
 * the terms before it are kept. */
static CettaPrimeLevelStatusV1 prime_level_notation_with_natural_part(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *natural,
    const CettaPrimeLevelNotationV1 **replaced_out) {
    const CettaPrimeLevelNotationV1 *last = NULL;
    CettaPrimeLevelStatusV1 status = cetta_prime_level_notation_natural_v1(
        owner, natural, &last);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    const CettaPrimeLevelNotationV1 *first = last;
    const CettaPrimeLevelNotationV1 **link = &first;
    for (const CettaPrimeLevelNotationV1 *term = notation;
         term && term->exponent; term = term->remainder) {
        CettaPrimeLevelNotationV1 *copy = arena_alloc(owner, sizeof(*copy));
        *copy = *term;
        copy->remainder = last;
        *link = copy;
        link = &copy->remainder;
    }
    *replaced_out = first;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_offset_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *offset,
    const CettaPrimeLevelNotationV1 **shifted_out) {
    if (shifted_out) *shifted_out = NULL;
    if (!owner || !shifted_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (!offset) {
        *shifted_out = notation;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    const CettaPrimeLevelNaturalV1 *raised = NULL;
    CettaPrimeLevelStatusV1 status = CETTA_PRIME_LEVEL_OK_V1;
    if (prime_level_notation_is_above(notation)) {
        status = prime_level_natural_add(
            owner, notation->number, offset, NULL, &raised);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        return cetta_prime_level_notation_above_v1(owner, raised, shifted_out);
    }
    /* The natural last term, if there is one, is the only term that
     * changes; a notation without one gains the offset as its last term. */
    status = prime_level_natural_add(
        owner, prime_level_notation_natural_part(notation), offset, NULL,
        &raised);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    return prime_level_notation_with_natural_part(
        owner, notation, raised, shifted_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_under_offset_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *offset,
    const CettaPrimeLevelNotationV1 **under_out) {
    if (under_out) *under_out = NULL;
    if (!owner || !under_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    bool above = prime_level_notation_is_above(notation);
    /* Zero and the limit notations have no natural last term and nothing
     * under a successor but themselves; a successor notation loses the
     * offset from that term, or all of the term.  Above the Cantor normal
     * forms the 0-th level is the limit. */
    const CettaPrimeLevelNaturalV1 *natural =
        above ? notation->number : prime_level_notation_natural_part(notation);
    if (!natural || !offset) {
        *under_out = notation;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    const CettaPrimeLevelNaturalV1 *lowered = NULL;
    if (prime_level_natural_compare(natural, offset) > 0) {
        CettaPrimeLevelStatusV1 status = prime_level_natural_subtract(
            owner, natural, offset, NULL, &lowered);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    return above
        ? cetta_prime_level_notation_above_v1(owner, lowered, under_out)
        : prime_level_notation_with_natural_part(
              owner, notation, lowered, under_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_under_successor_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNotationV1 **under_out) {
    return cetta_prime_level_notation_under_offset_v1(
        owner, notation, &prime_level_one, under_out);
}

/* The terms of a Cantor normal form, first to last: one step for each. */
static CettaPrimeLevelStatusV1 prime_level_notation_terms(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 ***terms_out, size_t *count_out) {
    size_t count = 0u;
    for (const CettaPrimeLevelNotationV1 *term = notation; term;
         term = term->remainder)
        count++;
    *terms_out = NULL;
    *count_out = count;
    if (count == 0u) return CETTA_PRIME_LEVEL_OK_V1;
    if (!prime_level_spend(budget, count))
        return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
    if (count > SIZE_MAX / sizeof(**terms_out) - 1u)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    const CettaPrimeLevelNotationV1 **terms =
        arena_alloc(owner, sizeof(*terms) * count);
    size_t at = 0u;
    for (const CettaPrimeLevelNotationV1 *term = notation; term;
         term = term->remainder)
        terms[at++] = term;
    *terms_out = terms;
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* `omega^exponent * coefficient` before a notation (`Cnf.addAux`): a larger
 * leading term of the notation absorbs it, an equal one takes its
 * coefficient, and a smaller one follows it. */
static CettaPrimeLevelStatusV1 prime_level_notation_attach(
    Arena *owner, const CettaPrimeLevelNotationV1 *exponent,
    const CettaPrimeLevelNaturalV1 *coefficient,
    const CettaPrimeLevelNotationV1 *rest, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **attached_out) {
    if (!rest)
        return prime_level_notation_term(
            owner, exponent, coefficient, NULL, budget, attached_out);
    int order = 0;
    CettaPrimeLevelStatusV1 status = prime_level_notation_order(
        exponent, rest->exponent, budget, &order);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    if (order < 0) {
        *attached_out = rest;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    if (order == 0) {
        const CettaPrimeLevelNaturalV1 *merged = NULL;
        status = prime_level_natural_add(
            owner, coefficient, rest->number, budget, &merged);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        return prime_level_notation_term(
            owner, exponent, merged, rest->remainder, budget, attached_out);
    }
    return prime_level_notation_term(
        owner, exponent, coefficient, rest, budget, attached_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_add_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **sum_out) {
    if (sum_out) *sum_out = NULL;
    if (!owner || !sum_out || prime_level_notation_is_above(left) ||
        prime_level_notation_is_above(right))
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    /* The terms of the left notation are attached to the right one from the
     * last term back (`Cnf.add`). */
    size_t count = 0u;
    const CettaPrimeLevelNotationV1 **terms = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_notation_terms(owner, left, budget, &terms, &count);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    const CettaPrimeLevelNotationV1 *total = right;
    for (size_t i = count; i > 0u; i--) {
        status = prime_level_notation_attach(
            owner, terms[i - 1u]->exponent, terms[i - 1u]->number, total,
            budget, &total);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    *sum_out = total;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_mul_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *left,
    const CettaPrimeLevelNotationV1 *right, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **product_out) {
    if (product_out) *product_out = NULL;
    if (!owner || !product_out || prime_level_notation_is_above(left) ||
        prime_level_notation_is_above(right))
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (!left || !right) return CETTA_PRIME_LEVEL_OK_V1;
    /* From the last term of the right factor back (`Cnf.mul`): its natural
     * term multiplies the leading coefficient of the left factor and keeps
     * what follows that coefficient; a term with a positive exponent shifts
     * the leading exponent of the left factor. */
    size_t count = 0u;
    const CettaPrimeLevelNotationV1 **terms = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_notation_terms(owner, right, budget, &terms, &count);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    const CettaPrimeLevelNotationV1 *total = NULL;
    for (size_t i = count; i > 0u; i--) {
        const CettaPrimeLevelNotationV1 *term = terms[i - 1u];
        if (!term->exponent) {
            const CettaPrimeLevelNaturalV1 *coefficient = NULL;
            status = prime_level_natural_multiply(
                owner, left->number, term->number, budget, &coefficient);
            if (status == CETTA_PRIME_LEVEL_OK_V1)
                status = prime_level_notation_term(
                    owner, left->exponent, coefficient, left->remainder,
                    budget, &total);
        } else {
            const CettaPrimeLevelNotationV1 *exponent = NULL;
            status = cetta_prime_level_notation_add_v1(
                owner, left->exponent, term->exponent, budget, &exponent);
            if (status == CETTA_PRIME_LEVEL_OK_V1)
                status = prime_level_notation_term(
                    owner, exponent, term->number, total, budget, &total);
        }
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    *product_out = total;
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* One subtracted from the left (`Cnf.subOne`): a natural number loses one,
 * and an infinite notation is unchanged, since 1 + e is e. */
static CettaPrimeLevelStatusV1 prime_level_notation_sub_one(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **less_out) {
    if (!notation || notation->exponent) {
        *less_out = notation;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    const CettaPrimeLevelNaturalV1 *less = NULL;
    CettaPrimeLevelStatusV1 status = prime_level_natural_subtract(
        owner, notation->number, &prime_level_one, budget, &less);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    return cetta_prime_level_notation_natural_v1(owner, less, less_out);
}

/* A notation as `quotient + natural` with omega dividing the quotient
 * (`Cnf.split`), or as `omega * quotient + natural` when `from_left`
 * (`Cnf.split'`): there each exponent of the quotient is one less from the
 * left. */
static CettaPrimeLevelStatusV1 prime_level_notation_split(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation, bool from_left,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **quotient_out,
    const CettaPrimeLevelNaturalV1 **natural_out) {
    *quotient_out = NULL;
    *natural_out = prime_level_notation_natural_part(notation);
    if (!from_left && !*natural_out) {
        *quotient_out = notation;
        return CETTA_PRIME_LEVEL_OK_V1;
    }
    size_t count = 0u;
    const CettaPrimeLevelNotationV1 **terms = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_notation_terms(owner, notation, budget, &terms, &count);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    const CettaPrimeLevelNotationV1 *total = NULL;
    for (size_t i = count; i > 0u; i--) {
        const CettaPrimeLevelNotationV1 *exponent = terms[i - 1u]->exponent;
        if (!exponent) continue;
        if (from_left)
            status = prime_level_notation_sub_one(
                owner, exponent, budget, &exponent);
        if (status == CETTA_PRIME_LEVEL_OK_V1)
            status = prime_level_notation_term(
                owner, exponent, terms[i - 1u]->number, total, budget,
                &total);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    *quotient_out = total;
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* `omega^shift * notation` (`Cnf.scale`). */
static CettaPrimeLevelStatusV1 prime_level_notation_scale(
    Arena *owner, const CettaPrimeLevelNotationV1 *shift,
    const CettaPrimeLevelNotationV1 *notation,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **scaled_out) {
    *scaled_out = NULL;
    size_t count = 0u;
    const CettaPrimeLevelNotationV1 **terms = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_notation_terms(owner, notation, budget, &terms, &count);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    const CettaPrimeLevelNotationV1 *total = NULL;
    for (size_t i = count; i > 0u; i--) {
        const CettaPrimeLevelNotationV1 *exponent = NULL;
        status = cetta_prime_level_notation_add_v1(
            owner, shift, terms[i - 1u]->exponent, budget, &exponent);
        if (status == CETTA_PRIME_LEVEL_OK_V1)
            status = prime_level_notation_term(
                owner, exponent, terms[i - 1u]->number, total, budget,
                &total);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    *scaled_out = total;
    return CETTA_PRIME_LEVEL_OK_V1;
}

/* `notation * factor` for a natural factor (`Cnf.mulNat`). */
static CettaPrimeLevelStatusV1 prime_level_notation_times(
    Arena *owner, const CettaPrimeLevelNotationV1 *notation,
    const CettaPrimeLevelNaturalV1 *factor, CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **product_out) {
    *product_out = NULL;
    if (!notation || !factor) return CETTA_PRIME_LEVEL_OK_V1;
    const CettaPrimeLevelNaturalV1 *coefficient = NULL;
    CettaPrimeLevelStatusV1 status = prime_level_natural_multiply(
        owner, notation->number, factor, budget, &coefficient);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    return prime_level_notation_term(
        owner, notation->exponent, coefficient, notation->remainder, budget,
        product_out);
}

/* `omega^(leading + first * factor) * notation`: one summand of a power
 * whose base is infinite. */
static CettaPrimeLevelStatusV1 prime_level_notation_power_summand(
    Arena *owner, const CettaPrimeLevelNotationV1 *leading,
    const CettaPrimeLevelNotationV1 *first,
    const CettaPrimeLevelNaturalV1 *factor,
    const CettaPrimeLevelNotationV1 *notation,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **summand_out) {
    const CettaPrimeLevelNotationV1 *shift = NULL;
    CettaPrimeLevelStatusV1 status =
        prime_level_notation_times(owner, first, factor, budget, &shift);
    if (status == CETTA_PRIME_LEVEL_OK_V1)
        status = cetta_prime_level_notation_add_v1(
            owner, leading, shift, budget, &shift);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    return prime_level_notation_scale(
        owner, shift, notation, budget, summand_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_notation_pow_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *base,
    const CettaPrimeLevelNotationV1 *exponent,
    CettaPrimeLevelBudgetV1 *budget,
    const CettaPrimeLevelNotationV1 **power_out) {
    if (power_out) *power_out = NULL;
    if (!owner || !power_out || prime_level_notation_is_above(base) ||
        prime_level_notation_is_above(exponent))
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    const CettaPrimeLevelNotationV1 *infinite = NULL;
    const CettaPrimeLevelNaturalV1 *finite = NULL;
    CettaPrimeLevelStatusV1 status = prime_level_notation_split(
        owner, base, false, budget, &infinite, &finite);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    if (!infinite) {
        /* A natural base (`Cnf.opowAux2`): zero to the power zero is one and
         * to any other power zero; one to any power is one; a larger number
         * to the power `omega * q + k` is `omega^q * base^k`. */
        if (!finite)
            return exponent ? CETTA_PRIME_LEVEL_OK_V1
                            : cetta_prime_level_notation_natural_v1(
                                  owner, &prime_level_one, power_out);
        if (prime_level_natural_compare(finite, &prime_level_one) == 0)
            return cetta_prime_level_notation_natural_v1(
                owner, &prime_level_one, power_out);
        const CettaPrimeLevelNotationV1 *quotient = NULL;
        const CettaPrimeLevelNaturalV1 *steps = NULL;
        const CettaPrimeLevelNaturalV1 *coefficient = NULL;
        status = prime_level_notation_split(
            owner, exponent, true, budget, &quotient, &steps);
        if (status == CETTA_PRIME_LEVEL_OK_V1)
            status = prime_level_natural_power(
                owner, finite, steps, budget, &coefficient);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        return prime_level_notation_term(
            owner, quotient, coefficient, NULL, budget, power_out);
    }
    /* An infinite base `omega^first * c + ... + finite` to the power
     * `q + k` with omega dividing `q` (`Cnf.opowAux2`): the leading exponent
     * of the power is `first * q`, and each of the `k` further factors puts
     * the terms of the base in front of what the factors before it gave
     * (`Cnf.opowAux`). */
    const CettaPrimeLevelNotationV1 *quotient = NULL;
    const CettaPrimeLevelNaturalV1 *steps = NULL;
    status = prime_level_notation_split(
        owner, exponent, false, budget, &quotient, &steps);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    const CettaPrimeLevelNotationV1 *first = infinite->exponent;
    const CettaPrimeLevelNotationV1 *leading = NULL;
    status = cetta_prime_level_notation_mul_v1(
        owner, first, quotient, budget, &leading);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    if (!steps)
        return prime_level_notation_term(
            owner, leading, &prime_level_one, NULL, budget, power_out);
    const CettaPrimeLevelNaturalV1 *last_step = NULL;
    status = prime_level_natural_subtract(
        owner, steps, &prime_level_one, budget, &last_step);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    /* `Cnf.opowAux leading first (infinite * finite) (steps - 1) finite`,
     * from its first case up.  With a zero natural part of the base it is
     * zero; otherwise every step puts at least one more term into the
     * power, and terms are objects in memory, so a number of steps that
     * exceeds the number of terms memory can address has no power here. */
    const CettaPrimeLevelNotationV1 *total = NULL;
    if (finite) {
        uint64_t further = 0u;
        if (!cetta_prime_level_natural_fits_uint64_v1(last_step, &further) ||
            further >= SIZE_MAX / sizeof(CettaPrimeLevelNotationV1))
            return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
        /* Each of those terms takes a step at the least, so a budget with
         * fewer steps than there are further factors is spent at once. */
        if (!prime_level_affords(budget, further)) {
            (void)prime_level_spend(budget, further);
            return CETTA_PRIME_LEVEL_BUDGET_EXHAUSTED_V1;
        }
        status = prime_level_notation_term(
            owner, leading, finite, NULL, budget, &total);
        /* The terms of the base times its natural part stand in the power
         * from the second factor on. */
        const CettaPrimeLevelNotationV1 *lower = NULL;
        if (status == CETTA_PRIME_LEVEL_OK_V1 && further != 0u)
            status = prime_level_notation_times(
                owner, infinite, finite, budget, &lower);
        for (uint64_t step = 0u;
             status == CETTA_PRIME_LEVEL_OK_V1 && step < further; step++) {
            const CettaPrimeLevelNaturalV1 *factor = NULL;
            const CettaPrimeLevelNotationV1 *summand = NULL;
            status = cetta_prime_level_natural_v1(owner, step, &factor);
            if (status == CETTA_PRIME_LEVEL_OK_V1)
                status = prime_level_notation_power_summand(
                    owner, leading, first, factor, lower, budget, &summand);
            if (status == CETTA_PRIME_LEVEL_OK_V1)
                status = cetta_prime_level_notation_add_v1(
                    owner, summand, total, budget, &total);
        }
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    const CettaPrimeLevelNotationV1 *summand = NULL;
    status = prime_level_notation_power_summand(
        owner, leading, first, last_step, infinite, budget, &summand);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    return cetta_prime_level_notation_add_v1(
        owner, summand, total, budget, power_out);
}

static bool prime_level_size(
    size_t parameter_count, size_t *size_out) {
    if (!size_out ||
        parameter_count >
            (SIZE_MAX - sizeof(CettaPrimeLevelV1)) /
                sizeof(CettaPrimeLevelParameterOffsetV1)) {
        return false;
    }
    size_t size = sizeof(CettaPrimeLevelV1) +
        parameter_count * sizeof(CettaPrimeLevelParameterOffsetV1);
    if (size > SIZE_MAX - 7u) return false;
    *size_out = size;
    return true;
}

static CettaPrimeLevelStatusV1 prime_level_allocate(
    Arena *owner, const CettaPrimeLevelNotationV1 *constant,
    const CettaPrimeLevelParameterOffsetV1 *parameters,
    size_t parameter_count, const CettaPrimeLevelV1 **level_out) {
    if (level_out) *level_out = NULL;
    if (!owner || !level_out || (parameter_count != 0u && !parameters))
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    size_t size = 0u;
    if (!prime_level_size(parameter_count, &size))
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    CettaPrimeLevelV1 *level = arena_alloc(owner, size);
    level->constant = constant;
    level->parameter_count = parameter_count;
    if (parameter_count != 0u)
        memcpy(
            level->parameters, parameters,
            parameter_count * sizeof(*parameters));
    *level_out = level;
    return CETTA_PRIME_LEVEL_OK_V1;
}

static const CettaPrimeLevelNaturalV1 *prime_level_sup_offsets(
    const CettaPrimeLevelParameterOffsetV1 *parameters,
    size_t parameter_count) {
    const CettaPrimeLevelNaturalV1 *supremum = NULL;
    for (size_t index = 0u; index < parameter_count; index++)
        if (prime_level_natural_compare(
                parameters[index].offset, supremum) > 0)
            supremum = parameters[index].offset;
    return supremum;
}

/* The canonical form of a constant with parameter atoms: the constant that
 * stays, and in `*parameter_count` the atoms that stay.  A parameter stands
 * for a Cantor normal form, so under a constant above the Cantor normal
 * forms no atom adds anything and none stays.  A constant that is a natural
 * number not above the largest offset adds nothing under any valuation and
 * is dropped.  A Cantor normal form beyond the natural numbers lies above
 * every offset and stays, with the atoms. */
static const CettaPrimeLevelNotationV1 *prime_level_canonical(
    const CettaPrimeLevelNotationV1 *constant,
    const CettaPrimeLevelParameterOffsetV1 *parameters,
    size_t *parameter_count) {
    if (prime_level_notation_is_above(constant)) {
        *parameter_count = 0u;
        return constant;
    }
    const CettaPrimeLevelNaturalV1 *natural = NULL;
    return cetta_prime_level_notation_natural_value_v1(constant, &natural) &&
           prime_level_natural_compare(
               natural,
               prime_level_sup_offsets(parameters, *parameter_count)) <= 0
        ? NULL
        : constant;
}

CettaPrimeLevelStatusV1 cetta_prime_level_constant_v1(
    Arena *owner, const CettaPrimeLevelNotationV1 *constant,
    const CettaPrimeLevelV1 **level_out) {
    return prime_level_allocate(owner, constant, NULL, 0u, level_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_parameter_v1(
    Arena *owner, uint64_t parameter, const CettaPrimeLevelV1 **level_out) {
    CettaPrimeLevelParameterOffsetV1 atom = {
        .parameter = parameter,
        .offset = NULL,
    };
    return prime_level_allocate(owner, NULL, &atom, 1u, level_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_offset_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelNaturalV1 *offset,
    const CettaPrimeLevelV1 **shifted_out) {
    if (shifted_out) *shifted_out = NULL;
    if (!owner || !level || !shifted_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    const CettaPrimeLevelNotationV1 *constant = NULL;
    CettaPrimeLevelStatusV1 status = cetta_prime_level_notation_offset_v1(
        owner, level->constant, offset, &constant);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    size_t size = 0u;
    if (!prime_level_size(level->parameter_count, &size))
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    CettaPrimeLevelV1 *shifted = arena_alloc(owner, size);
    shifted->parameter_count = level->parameter_count;
    for (size_t index = 0u; index < level->parameter_count; index++) {
        shifted->parameters[index].parameter =
            level->parameters[index].parameter;
        status = prime_level_natural_add(
            owner, level->parameters[index].offset, offset, NULL,
            &shifted->parameters[index].offset);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    }
    shifted->constant = prime_level_canonical(
        constant, shifted->parameters, &shifted->parameter_count);
    *shifted_out = shifted;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_successor_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    const CettaPrimeLevelV1 **successor_out) {
    return cetta_prime_level_offset_v1(
        owner, level, &prime_level_one, successor_out);
}

CettaPrimeLevelStatusV1 cetta_prime_level_maximum_v1(
    Arena *owner, const CettaPrimeLevelV1 *left,
    const CettaPrimeLevelV1 *right,
    const CettaPrimeLevelV1 **maximum_out) {
    if (maximum_out) *maximum_out = NULL;
    if (!owner || !left || !right || !maximum_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (left->parameter_count > SIZE_MAX - right->parameter_count)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    size_t capacity = left->parameter_count + right->parameter_count;
    size_t size = 0u;
    if (!prime_level_size(capacity, &size))
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    int order = 0;
    CettaPrimeLevelStatusV1 status = cetta_prime_level_notation_compare_v1(
        left->constant, right->constant, &order);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    CettaPrimeLevelV1 *maximum = arena_alloc(owner, size);
    size_t left_index = 0u;
    size_t right_index = 0u;
    size_t output_index = 0u;
    while (left_index < left->parameter_count ||
           right_index < right->parameter_count) {
        if (right_index == right->parameter_count ||
            (left_index < left->parameter_count &&
             left->parameters[left_index].parameter <
                 right->parameters[right_index].parameter)) {
            maximum->parameters[output_index++] =
                left->parameters[left_index++];
        } else if (left_index == left->parameter_count ||
                   right->parameters[right_index].parameter <
                       left->parameters[left_index].parameter) {
            maximum->parameters[output_index++] =
                right->parameters[right_index++];
        } else {
            const CettaPrimeLevelNaturalV1 *left_offset =
                left->parameters[left_index].offset;
            const CettaPrimeLevelNaturalV1 *right_offset =
                right->parameters[right_index].offset;
            maximum->parameters[output_index++] =
                (CettaPrimeLevelParameterOffsetV1){
                    .parameter = left->parameters[left_index].parameter,
                    .offset = prime_level_natural_compare(
                                  left_offset, right_offset) > 0
                        ? left_offset
                        : right_offset,
                };
            left_index++;
            right_index++;
        }
    }
    maximum->parameter_count = output_index;
    maximum->constant = prime_level_canonical(
        order > 0 ? left->constant : right->constant, maximum->parameters,
        &maximum->parameter_count);
    *maximum_out = maximum;
    return CETTA_PRIME_LEVEL_OK_V1;
}

static int prime_level_parameter_compare(
    const void *left_pointer, const void *right_pointer) {
    const CettaPrimeLevelParameterOffsetV1 *left = left_pointer;
    const CettaPrimeLevelParameterOffsetV1 *right = right_pointer;
    if (left->parameter < right->parameter) return -1;
    if (left->parameter > right->parameter) return 1;
    return 0;
}

CettaPrimeLevelStatusV1 cetta_prime_level_substitute_v1(
    Arena *owner, const CettaPrimeLevelV1 *source,
    CettaPrimeLevelSubstitutionV1 substitution, void *context,
    const CettaPrimeLevelV1 **result_out) {
    if (result_out) *result_out = NULL;
    if (!owner || !source || !substitution || !result_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (source->parameter_count == 0u)
        return prime_level_allocate(
            owner, source->constant, NULL, 0u, result_out);
    if (source->parameter_count >
        SIZE_MAX / sizeof(const CettaPrimeLevelV1 *)) {
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    }
    const CettaPrimeLevelV1 **replacements = malloc(
        source->parameter_count * sizeof(*replacements));
    if (!replacements)
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    size_t capacity = 0u;
    CettaPrimeLevelStatusV1 status = CETTA_PRIME_LEVEL_OK_V1;
    for (size_t index = 0u; index < source->parameter_count; index++) {
        replacements[index] = NULL;
        status = substitution(
            context, source->parameters[index].parameter,
            &replacements[index]);
        /* No parameter stands for a level above the Cantor normal forms. */
        if (status != CETTA_PRIME_LEVEL_OK_V1 || !replacements[index] ||
            prime_level_notation_is_above(replacements[index]->constant)) {
            if (status == CETTA_PRIME_LEVEL_OK_V1)
                status = CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
            free(replacements);
            return status;
        }
        if (capacity > SIZE_MAX - replacements[index]->parameter_count) {
            free(replacements);
            return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
        }
        capacity += replacements[index]->parameter_count;
    }
    if (capacity > SIZE_MAX / sizeof(CettaPrimeLevelParameterOffsetV1)) {
        free(replacements);
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    }
    CettaPrimeLevelParameterOffsetV1 *parameters = capacity == 0u
        ? NULL
        : malloc(capacity * sizeof(*parameters));
    if (capacity != 0u && !parameters) {
        free(replacements);
        return CETTA_PRIME_LEVEL_OUT_OF_MEMORY_V1;
    }
    size_t parameter_count = 0u;
    const CettaPrimeLevelNotationV1 *constant = source->constant;
    for (size_t source_index = 0u;
         source_index < source->parameter_count; source_index++) {
        const CettaPrimeLevelV1 *replacement = replacements[source_index];
        const CettaPrimeLevelNaturalV1 *offset =
            source->parameters[source_index].offset;
        const CettaPrimeLevelNotationV1 *shifted_constant = NULL;
        int order = 0;
        status = cetta_prime_level_notation_offset_v1(
            owner, replacement->constant, offset, &shifted_constant);
        if (status == CETTA_PRIME_LEVEL_OK_V1)
            status = cetta_prime_level_notation_compare_v1(
                shifted_constant, constant, &order);
        if (status != CETTA_PRIME_LEVEL_OK_V1) goto cleanup;
        if (order > 0) constant = shifted_constant;
        for (size_t replacement_index = 0u;
             replacement_index < replacement->parameter_count;
             replacement_index++) {
            CettaPrimeLevelParameterOffsetV1 atom =
                replacement->parameters[replacement_index];
            status = prime_level_natural_add(
                owner, atom.offset, offset, NULL, &atom.offset);
            if (status != CETTA_PRIME_LEVEL_OK_V1) goto cleanup;
            parameters[parameter_count++] = atom;
        }
    }
    if (parameter_count > 1u)
        qsort(
            parameters, parameter_count, sizeof(*parameters),
            prime_level_parameter_compare);
    size_t distinct_count = 0u;
    for (size_t index = 0u; index < parameter_count; index++) {
        if (distinct_count != 0u &&
            parameters[distinct_count - 1u].parameter ==
                parameters[index].parameter) {
            if (prime_level_natural_compare(
                    parameters[index].offset,
                    parameters[distinct_count - 1u].offset) > 0) {
                parameters[distinct_count - 1u].offset =
                    parameters[index].offset;
            }
        } else {
            parameters[distinct_count++] = parameters[index];
        }
    }
    constant = prime_level_canonical(constant, parameters, &distinct_count);
    status = prime_level_allocate(
        owner, constant, parameters, distinct_count, result_out);

cleanup:
    free(parameters);
    free(replacements);
    return status;
}

CettaPrimeLevelStatusV1 cetta_prime_level_equal_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right,
    bool *equal_out) {
    if (equal_out) *equal_out = false;
    if (!left || !right || !equal_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    if (left->parameter_count != right->parameter_count)
        return CETTA_PRIME_LEVEL_OK_V1;
    for (size_t index = 0u; index < left->parameter_count; index++)
        if (left->parameters[index].parameter !=
                right->parameters[index].parameter ||
            prime_level_natural_compare(
                left->parameters[index].offset,
                right->parameters[index].offset) != 0) {
            return CETTA_PRIME_LEVEL_OK_V1;
        }
    int order = 0;
    CettaPrimeLevelStatusV1 status = cetta_prime_level_notation_compare_v1(
        left->constant, right->constant, &order);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    *equal_out = order == 0;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_le_v1(
    const CettaPrimeLevelV1 *left, const CettaPrimeLevelV1 *right,
    bool *le_out) {
    if (le_out) *le_out = false;
    if (!left || !right || !le_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    /* A right level whose constant is above the Cantor normal forms is that
     * constant, and every parameter of the left level, raised by its offset,
     * is below it: the two constants decide. */
    if (!prime_level_notation_is_above(right->constant)) {
        size_t right_index = 0u;
        for (size_t left_index = 0u;
             left_index < left->parameter_count; left_index++) {
            while (right_index < right->parameter_count &&
                   right->parameters[right_index].parameter <
                       left->parameters[left_index].parameter) {
                right_index++;
            }
            if (right_index == right->parameter_count ||
                right->parameters[right_index].parameter !=
                    left->parameters[left_index].parameter ||
                prime_level_natural_compare(
                    left->parameters[left_index].offset,
                    right->parameters[right_index].offset) > 0) {
                return CETTA_PRIME_LEVEL_OK_V1;
            }
        }
        /* At the zero valuation the right side shows its constant and its
         * largest offset; the left constant must lie under one of the
         * two. */
        const CettaPrimeLevelNaturalV1 *left_natural = NULL;
        if (cetta_prime_level_notation_natural_value_v1(
                left->constant, &left_natural) &&
            prime_level_natural_compare(
                left_natural,
                prime_level_sup_offsets(
                    right->parameters, right->parameter_count)) <= 0) {
            *le_out = true;
            return CETTA_PRIME_LEVEL_OK_V1;
        }
    }
    int order = 0;
    CettaPrimeLevelStatusV1 status = cetta_prime_level_notation_compare_v1(
        left->constant, right->constant, &order);
    if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
    *le_out = order <= 0;
    return CETTA_PRIME_LEVEL_OK_V1;
}

CettaPrimeLevelStatusV1 cetta_prime_level_evaluate_v1(
    Arena *owner, const CettaPrimeLevelV1 *level,
    CettaPrimeLevelValuationV1 valuation, void *context,
    const CettaPrimeLevelNotationV1 **value_out) {
    if (value_out) *value_out = NULL;
    if (!owner || !level || !valuation || !value_out)
        return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
    const CettaPrimeLevelNotationV1 *value = level->constant;
    for (size_t index = 0u; index < level->parameter_count; index++) {
        const CettaPrimeLevelNotationV1 *parameter_value = NULL;
        CettaPrimeLevelStatusV1 status = valuation(
            context, level->parameters[index].parameter,
            &parameter_value);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        /* No parameter stands for a level above the Cantor normal forms. */
        if (prime_level_notation_is_above(parameter_value))
            return CETTA_PRIME_LEVEL_INVALID_ARGUMENT_V1;
        const CettaPrimeLevelNotationV1 *atom_value = NULL;
        int order = 0;
        status = cetta_prime_level_notation_offset_v1(
            owner, parameter_value, level->parameters[index].offset,
            &atom_value);
        if (status == CETTA_PRIME_LEVEL_OK_V1)
            status = cetta_prime_level_notation_compare_v1(
                atom_value, value, &order);
        if (status != CETTA_PRIME_LEVEL_OK_V1) return status;
        if (order > 0) value = atom_value;
    }
    *value_out = value;
    return CETTA_PRIME_LEVEL_OK_V1;
}

bool cetta_prime_level_view_v1(
    const CettaPrimeLevelV1 *level, CettaPrimeLevelViewV1 *view_out) {
    if (!level || !view_out) return false;
    *view_out = (CettaPrimeLevelViewV1){
        .constant = level->constant,
        .parameters = level->parameters,
        .parameter_count = level->parameter_count,
    };
    return true;
}
