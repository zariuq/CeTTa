#include "unicode_class_v1.h"

/* Class comparison is separate from construction and matching. Comparison
 * never allocates or normalizes an input; callers supply canonical values. */
UnicodeV1Outcome unicode_v1_set_equal(const UnicodeV1Set *left,
                                     const UnicodeV1Set *right, bool *out) {
    if (!out || !left || !right ||
        left->len > SIZE_MAX / sizeof(*left->items) ||
        right->len > SIZE_MAX / sizeof(*right->items) ||
        !unicode_v1_set_valid(left) || !unicode_v1_set_valid(right))
        return UNICODE_V1_INVALID_ARGUMENT;
    bool same = left->domain == right->domain && left->len == right->len;
    for (size_t i = 0u; same && i < left->len; i++)
        same = left->items[i].low == right->items[i].low &&
               left->items[i].high == right->items[i].high;
    *out = same;
    return UNICODE_V1_OK;
}
