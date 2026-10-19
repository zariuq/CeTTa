#ifndef CETTA_UNICODE_CLASS_V1_H
#define CETTA_UNICODE_CLASS_V1_H

#include "lib_parse_native_grammar.h"

/* A domain is part of a set's meaning, including its complement. */
typedef enum {
    UNICODE_V1_BYTES = 0,
    UNICODE_V1_UTF16_UNITS,
    UNICODE_V1_UTF32_UNITS,
    UNICODE_V1_CODE_POINTS,
    UNICODE_V1_SCALARS
} UnicodeV1Domain;

typedef struct {
    const CettaLpNativeUnicodeRange *items;
    size_t len;
} UnicodeV1Ranges;

typedef struct {
    UnicodeV1Domain domain;
    CettaLpNativeUnicodeRange *items;
    size_t len;
} UnicodeV1Set;

typedef enum {
    UNICODE_V1_OK = 0,
    UNICODE_V1_INVALID_ARGUMENT,
    UNICODE_V1_OUT_OF_MEMORY
} UnicodeV1Outcome;

typedef enum {
    UNICODE_V1_UNION = 0,
    UNICODE_V1_INTERSECTION,
    UNICODE_V1_DIFFERENCE,
    UNICODE_V1_SYMMETRIC_DIFFERENCE
} UnicodeV1Operation;

/* Ordinary realloc contract; isolated failure controls can override it. */
void *unicode_v1_class_reallocate(void *previous, size_t bytes);

/* Owned destinations must start as {0}. Success replaces the destination;
 * failure leaves it unchanged. Inputs and destination may alias. Projection
 * intersects arbitrary well-ordered uint32 ranges with the selected domain. */
void unicode_v1_set_free(UnicodeV1Set *set);
bool unicode_v1_set_valid(const UnicodeV1Set *set);
bool unicode_v1_set_contains(const UnicodeV1Set *set, uint32_t value);
/* Exact equality of canonical values. The domain is part of the value:
 * equal membership in different universes is not typed class equality.
 * Invalid inputs leave *out unchanged. This operation allocates nothing. */
UnicodeV1Outcome unicode_v1_set_equal(const UnicodeV1Set *left,
    const UnicodeV1Set *right, bool *out);
UnicodeV1Outcome unicode_v1_set_project(UnicodeV1Domain domain,
    UnicodeV1Ranges ranges, UnicodeV1Set *out);
UnicodeV1Outcome unicode_v1_set_combine(UnicodeV1Operation operation,
    const UnicodeV1Set *left, const UnicodeV1Set *right, UnicodeV1Set *out);
UnicodeV1Outcome unicode_v1_set_complement(const UnicodeV1Set *set,
    UnicodeV1Set *out);

/* Data views are immutable and process-lifetime. Unicode properties use the
 * code-point universe; callers project to scalars or another explicit domain.
 * Names/values accept the UCD's ASCII aliases with loose matching. A missing
 * property is distinct from a known property whose projected set is empty. */
const char *unicode_v1_version(void);
/* Canonical property identity, even when no ranges for it are provided.
 * The borrowed result has process lifetime; unknown aliases return NULL. */
const char *unicode_v1_property_name(const char *name);
typedef struct {
    const char *property, *value;
    UnicodeV1Ranges ranges;
} UnicodeV1Property;
bool unicode_v1_resolve_property(const char *property, const char *value,
    UnicodeV1Property *out);
bool unicode_v1_property(const char *property, const char *value,
    UnicodeV1Ranges *out);
bool unicode_v1_character_class(const char *name, UnicodeV1Ranges *out);
/* Authored lexical classes compiled from the Unicode presentation. Digit
 * views retain their exact class, including holes; they allocate nothing. */
bool unicode_v1_lexical_class(const char *name, UnicodeV1Ranges *out);
typedef struct {
    const uint8_t *values;
    uint32_t first;
    size_t len;
} UnicodeV1DigitClass;
const UnicodeV1DigitClass *unicode_v1_digit_class(const char *name);
static inline bool unicode_v1_lexical_digit_value(
    const UnicodeV1DigitClass *digits, uint32_t codepoint, uint32_t *out) {
    if (!digits || !digits->values || !out || codepoint < digits->first ||
        (uint64_t)codepoint - digits->first >= digits->len)
        return false;
    uint8_t encoded = digits->values[codepoint - digits->first];
    if (!encoded)
        return false;
    *out = (uint32_t)encoded - 1u;
    return true;
}
size_t unicode_v1_property_count(void);
bool unicode_v1_property_at(size_t index, const char **property,
    const char **value, UnicodeV1Ranges *out);
bool unicode_v1_decimal_value(uint32_t codepoint, uint32_t *value,
    uint32_t *digit_set_zero);
/* Decimal values are independent per scalar. Digit-set identity is descriptive
 * only: callers may combine digits from different Unicode Nd blocks. */
UnicodeV1Ranges unicode_v1_decimal_ranges(void);

/* Nontrivial simple-fold classes include the queried scalar. An empty view
 * means the scalar folds only to itself. Full folding is a separate service. */
bool unicode_v1_simple_fold_class(uint32_t scalar,
    const uint32_t **members, size_t *len);
/* Enumerate nontrivial scalar equivalence members in scalar order. The
 * representative is the least member, suitable for an owned compiled map. */
size_t unicode_v1_simple_fold_count(void);
bool unicode_v1_simple_fold_at(size_t index, uint32_t *scalar, uint32_t *representative);
UnicodeV1Outcome unicode_v1_set_simple_fold(const UnicodeV1Set *set,
    UnicodeV1Set *out);

#endif
