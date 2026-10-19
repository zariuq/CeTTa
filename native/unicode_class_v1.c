#include "unicode_class_v1.h"

#include <stdlib.h>
#include <string.h>

#include "../langdef/unicode/generated/unicode_ucd16_v1.generated.h"

typedef struct {
    CettaLpNativeUnicodeRange *items;
    size_t len;
    size_t cap;
} UnicodeV1Buffer;

/* An isolated allocation-failure control may override this ordinary realloc
 * boundary. Production has no mutable allocator state here. */
__attribute__((weak)) void *unicode_v1_class_reallocate(void *previous, size_t bytes) {
    return realloc(previous, bytes);
}

static size_t domain_ranges(UnicodeV1Domain domain,
                           CettaLpNativeUnicodeRange ranges[2]) {
    if (domain < UNICODE_V1_BYTES || domain > UNICODE_V1_SCALARS)
        return 0u;
    UnicodeV1Ranges source = unicode_v1_ucd_domains[domain];
    memcpy(ranges, source.items, source.len * sizeof(*ranges));
    return source.len;
}

static UnicodeV1Outcome buffer_push(UnicodeV1Buffer *buffer,
                                   uint32_t low, uint32_t high) {
    if (buffer->len == buffer->cap) {
        if (buffer->cap > SIZE_MAX / 2u / sizeof(*buffer->items))
            return UNICODE_V1_OUT_OF_MEMORY;
        size_t cap = buffer->cap ? buffer->cap * 2u : 8u;
        void *items = unicode_v1_class_reallocate(buffer->items,
                                                 cap * sizeof(*buffer->items));
        if (!items)
            return UNICODE_V1_OUT_OF_MEMORY;
        buffer->items = items;
        buffer->cap = cap;
    }
    buffer->items[buffer->len++] = (CettaLpNativeUnicodeRange){low, high};
    return UNICODE_V1_OK;
}

static int range_compare(const void *left, const void *right) {
    const CettaLpNativeUnicodeRange *a = left;
    const CettaLpNativeUnicodeRange *b = right;
    return a->low < b->low ? -1 : a->low > b->low ? 1 :
           a->high < b->high ? -1 : a->high > b->high;
}

static void buffer_normalize(UnicodeV1Buffer *buffer) {
    if (buffer->len < 2u)
        return;
    qsort(buffer->items, buffer->len, sizeof(*buffer->items), range_compare);
    size_t write = 0u;
    for (size_t read = 0u; read < buffer->len; read++) {
        CettaLpNativeUnicodeRange current = buffer->items[read];
        if (write && (uint64_t)current.low <= (uint64_t)buffer->items[write - 1u].high + 1u) {
            if (current.high > buffer->items[write - 1u].high)
                buffer->items[write - 1u].high = current.high;
        } else {
            buffer->items[write++] = current;
        }
    }
    buffer->len = write;
}

static void buffer_publish(UnicodeV1Buffer *buffer, UnicodeV1Domain domain,
                           UnicodeV1Set *out) {
    free(out->items);
    if (!buffer->len) {
        free(buffer->items);
        buffer->items = NULL;
    }
    *out = (UnicodeV1Set){domain, buffer->items, buffer->len};
    *buffer = (UnicodeV1Buffer){0};
}

void unicode_v1_set_free(UnicodeV1Set *set) {
    if (set) {
        free(set->items);
        *set = (UnicodeV1Set){0};
    }
}

bool unicode_v1_set_valid(const UnicodeV1Set *set) {
    CettaLpNativeUnicodeRange universe[2];
    if (!set || (set->len && !set->items) ||
        set->len > SIZE_MAX / sizeof(*set->items))
        return false;
    size_t count = domain_ranges(set->domain, universe);
    if (!count)
        return false;
    for (size_t i = 0u; i < set->len; i++) {
        CettaLpNativeUnicodeRange range = set->items[i];
        if (range.low > range.high || (i &&
            (uint64_t)set->items[i - 1u].high + 1u >= range.low))
            return false;
        bool contained = false;
        for (size_t j = 0u; j < count; j++)
            contained |= range.low >= universe[j].low && range.high <= universe[j].high;
        if (!contained)
            return false;
    }
    return true;
}

bool unicode_v1_set_contains(const UnicodeV1Set *set, uint32_t value) {
    if (!set || (set->len && !set->items) ||
        set->len > SIZE_MAX / sizeof(*set->items))
        return false;
    size_t low = 0u, high = set->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (value < set->items[middle].low)
            high = middle;
        else if (value > set->items[middle].high)
            low = middle + 1u;
        else
            return true;
    }
    return false;
}

UnicodeV1Outcome unicode_v1_set_project(UnicodeV1Domain domain,
                                       UnicodeV1Ranges ranges,
                                       UnicodeV1Set *out) {
    CettaLpNativeUnicodeRange universe[2];
    size_t count = domain_ranges(domain, universe);
    if (!out || !count || (ranges.len && !ranges.items) ||
        ranges.len > SIZE_MAX / sizeof(*ranges.items))
        return UNICODE_V1_INVALID_ARGUMENT;
    UnicodeV1Buffer buffer = {0};
    for (size_t i = 0u; i < ranges.len; i++) {
        CettaLpNativeUnicodeRange range = ranges.items[i];
        if (range.low > range.high) {
            free(buffer.items);
            return UNICODE_V1_INVALID_ARGUMENT;
        }
        for (size_t j = 0u; j < count; j++) {
            uint32_t low = range.low > universe[j].low ? range.low : universe[j].low;
            uint32_t high = range.high < universe[j].high ? range.high : universe[j].high;
            if (low <= high) {
                UnicodeV1Outcome outcome = buffer_push(&buffer, low, high);
                if (outcome != UNICODE_V1_OK) {
                    free(buffer.items);
                    return outcome;
                }
            }
        }
    }
    buffer_normalize(&buffer);
    buffer_publish(&buffer, domain, out);
    return UNICODE_V1_OK;
}

static UnicodeV1Outcome buffer_copy(UnicodeV1Buffer *out, const UnicodeV1Set *set) {
    for (size_t i = 0u; i < set->len; i++) {
        UnicodeV1Outcome outcome = buffer_push(out, set->items[i].low, set->items[i].high);
        if (outcome != UNICODE_V1_OK)
            return outcome;
    }
    return UNICODE_V1_OK;
}

static UnicodeV1Outcome buffer_difference(UnicodeV1Buffer *out,
                                         const UnicodeV1Set *left,
                                         const UnicodeV1Set *right) {
    size_t cursor = 0u;
    for (size_t i = 0u; i < left->len; i++) {
        uint64_t position = left->items[i].low;
        uint32_t end = left->items[i].high;
        while (cursor < right->len && right->items[cursor].high < position)
            cursor++;
        size_t j = cursor;
        while (j < right->len && right->items[j].low <= end) {
            if (position < right->items[j].low) {
                UnicodeV1Outcome outcome = buffer_push(out, (uint32_t)position,
                                                       right->items[j].low - 1u);
                if (outcome != UNICODE_V1_OK)
                    return outcome;
            }
            uint64_t next = (uint64_t)right->items[j].high + 1u;
            if (next > position)
                position = next;
            if (position > end)
                break;
            j++;
        }
        if (position <= end) {
            UnicodeV1Outcome outcome = buffer_push(out, (uint32_t)position, end);
            if (outcome != UNICODE_V1_OK)
                return outcome;
        }
    }
    return UNICODE_V1_OK;
}

UnicodeV1Outcome unicode_v1_set_combine(UnicodeV1Operation operation,
                                       const UnicodeV1Set *left,
                                       const UnicodeV1Set *right,
                                       UnicodeV1Set *out) {
    if (!out || !unicode_v1_set_valid(left) || !unicode_v1_set_valid(right) ||
        left->domain != right->domain || operation > UNICODE_V1_SYMMETRIC_DIFFERENCE ||
        operation < UNICODE_V1_UNION)
        return UNICODE_V1_INVALID_ARGUMENT;
    UnicodeV1Buffer buffer = {0};
    UnicodeV1Outcome outcome = UNICODE_V1_OK;
    if (operation == UNICODE_V1_UNION) {
        outcome = buffer_copy(&buffer, left);
        if (outcome == UNICODE_V1_OK)
            outcome = buffer_copy(&buffer, right);
    } else if (operation == UNICODE_V1_INTERSECTION) {
        size_t i = 0u, j = 0u;
        while (outcome == UNICODE_V1_OK && i < left->len && j < right->len) {
            uint32_t low = left->items[i].low > right->items[j].low
                ? left->items[i].low : right->items[j].low;
            uint32_t high = left->items[i].high < right->items[j].high
                ? left->items[i].high : right->items[j].high;
            if (low <= high)
                outcome = buffer_push(&buffer, low, high);
            if (left->items[i].high < right->items[j].high)
                i++;
            else
                j++;
        }
    } else {
        outcome = buffer_difference(&buffer, left, right);
        if (outcome == UNICODE_V1_OK && operation == UNICODE_V1_SYMMETRIC_DIFFERENCE)
            outcome = buffer_difference(&buffer, right, left);
    }
    if (outcome != UNICODE_V1_OK) {
        free(buffer.items);
        return outcome;
    }
    buffer_normalize(&buffer);
    buffer_publish(&buffer, left->domain, out);
    return UNICODE_V1_OK;
}

UnicodeV1Outcome unicode_v1_set_complement(const UnicodeV1Set *set,
                                          UnicodeV1Set *out) {
    if (!out || !unicode_v1_set_valid(set))
        return UNICODE_V1_INVALID_ARGUMENT;
    CettaLpNativeUnicodeRange ranges[2];
    size_t len = domain_ranges(set->domain, ranges);
    UnicodeV1Set universe = {set->domain, ranges, len};
    return unicode_v1_set_combine(UNICODE_V1_DIFFERENCE, &universe, set, out);
}

static bool loose_name(const char *name, char out[128]) {
    if (!name)
        return false;
    size_t position = 0u;
    while (*name) {
        unsigned char c = (unsigned char)*name++;
        if (c == '_' || c == '-' || c == ' ' || (c >= 9u && c <= 13u))
            continue;
        if (c >= 0x80u || position == 127u)
            return false;
        out[position++] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    out[position] = '\0';
    return position != 0u;
}

static const char *property_alias(const char *name) {
    size_t low = 0u, high = unicode_v1_ucd_property_alias_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int order = strcmp(name, unicode_v1_ucd_property_aliases[middle].name);
        if (order < 0)
            high = middle;
        else if (order > 0)
            low = middle + 1u;
        else
            return unicode_v1_ucd_property_aliases[middle].canonical;
    }
    return NULL;
}

static const char *value_alias(const char *property, const char *name) {
    size_t low = 0u, high = unicode_v1_ucd_value_alias_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const UnicodeV1ValueAlias *row = &unicode_v1_ucd_value_aliases[middle];
        int order = strcmp(property, row->property);
        if (!order)
            order = strcmp(name, row->name);
        if (order < 0)
            high = middle;
        else if (order > 0)
            low = middle + 1u;
        else
            return row->canonical;
    }
    return NULL;
}

static bool property_row(const char *property, const char *value,
                         UnicodeV1Ranges *out) {
    size_t low = 0u, high = unicode_v1_ucd_property_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const UnicodeV1PropertyRow *row = &unicode_v1_ucd_properties[middle];
        int order = strcmp(property, row->property);
        if (!order)
            order = strcmp(value, row->value);
        if (order < 0)
            high = middle;
        else if (order > 0)
            low = middle + 1u;
        else {
            *out = (UnicodeV1Ranges){unicode_v1_ucd_ranges + row->begin, row->len};
            return true;
        }
    }
    return false;
}

const char *unicode_v1_version(void) { return unicode_v1_ucd_version; }

const char *unicode_v1_property_name(const char *name) {
    char normalized[128];
    return loose_name(name, normalized) ? property_alias(normalized) : NULL;
}

bool unicode_v1_lexical_class(const char *name, UnicodeV1Ranges *out) {
    if (!name || !out)
        return false;
    for (size_t index = 0u; index < unicode_v1_ucd_lexical_class_len; index++) {
        if (!strcmp(name, unicode_v1_ucd_lexical_classes[index].name)) {
            *out = unicode_v1_ucd_lexical_classes[index].ranges;
            return true;
        }
    }
    return false;
}

const UnicodeV1DigitClass *unicode_v1_digit_class(const char *name) {
    if (!name)
        return NULL;
    for (size_t index = 0u; index < unicode_v1_ucd_digit_class_len; index++) {
        if (!strcmp(name, unicode_v1_ucd_digit_classes[index].name))
            return &unicode_v1_ucd_digit_classes[index].digits;
    }
    return NULL;
}

bool unicode_v1_resolve_property(const char *property, const char *value,
                                 UnicodeV1Property *out) {
    char value_name[128];
    if (!out)
        return false;
    const char *p = unicode_v1_property_name(property);
    if (!p)
        return false;
    const char *v = "Y";
    if (value) {
        if (!loose_name(value, value_name))
            return false;
        v = value_alias(p, value_name);
        if (!v) {
            if (!strcmp(value_name, "y") || !strcmp(value_name, "yes") ||
                !strcmp(value_name, "t") || !strcmp(value_name, "true"))
                v = "Y";
            else if (!strcmp(value_name, "n") || !strcmp(value_name, "no") ||
                     !strcmp(value_name, "f") || !strcmp(value_name, "false"))
                v = "N";
            else
                return false;
        }
    }
    UnicodeV1Ranges ranges;
    if (!property_row(p, v, &ranges))
        return false;
    *out = (UnicodeV1Property){p, v, ranges};
    return true;
}

bool unicode_v1_property(const char *property, const char *value,
                         UnicodeV1Ranges *out) {
    UnicodeV1Property resolved;
    if (!out || !unicode_v1_resolve_property(property, value, &resolved))
        return false;
    *out = resolved.ranges;
    return true;
}

bool unicode_v1_character_class(const char *name, UnicodeV1Ranges *out) {
    char normalized[128];
    if (!out || !loose_name(name, normalized))
        return false;
    const char *category = value_alias("gc", normalized);
    if (category && property_row("gc", category, out))
        return true;
    if (unicode_v1_property(name, NULL, out))
        return true;
    const char *script = value_alias("sc", normalized);
    return script && property_row("sc", script, out);
}

size_t unicode_v1_property_count(void) { return unicode_v1_ucd_property_len; }

bool unicode_v1_property_at(size_t index, const char **property,
                            const char **value, UnicodeV1Ranges *out) {
    if (index >= unicode_v1_ucd_property_len || !property || !value || !out)
        return false;
    const UnicodeV1PropertyRow *row = &unicode_v1_ucd_properties[index];
    *property = row->property;
    *value = row->value;
    *out = (UnicodeV1Ranges){unicode_v1_ucd_ranges + row->begin, row->len};
    return true;
}

size_t unicode_v1_simple_fold_count(void) { return unicode_v1_ucd_fold_index_len; }
bool unicode_v1_simple_fold_at(size_t index, uint32_t *scalar_, uint32_t *representative) {
    if (index >= unicode_v1_ucd_fold_index_len || !scalar_ || !representative) return false;
    const UnicodeV1FoldRow *row = &unicode_v1_ucd_fold_index[index];
    if (!row->len) return false;
    *scalar_ = row->scalar; *representative = unicode_v1_ucd_fold_members[row->begin];
    return true;
}

bool unicode_v1_decimal_value(uint32_t codepoint, uint32_t *value,
                              uint32_t *digit_set_zero) {
    if (!value)
        return false;
    size_t low = 0u, high = unicode_v1_ucd_decimal_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        uint32_t zero = unicode_v1_ucd_decimal_zeroes[middle];
        if (codepoint < zero)
            high = middle;
        else if ((uint64_t)codepoint >= (uint64_t)zero + 10u)
            low = middle + 1u;
        else {
            *value = codepoint - zero;
            if (digit_set_zero)
                *digit_set_zero = zero;
            return true;
        }
    }
    return false;
}

UnicodeV1Ranges unicode_v1_decimal_ranges(void) {
    UnicodeV1Ranges ranges = {0};
    (void)property_row("gc", "Nd", &ranges);
    return ranges;
}

bool unicode_v1_simple_fold_class(uint32_t scalar,
                                 const uint32_t **members, size_t *len) {
    if (!members || !len || scalar > 0x10ffffu ||
        (scalar >= 0xd800u && scalar <= 0xdfffu))
        return false;
    size_t low = 0u, high = unicode_v1_ucd_fold_index_len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        const UnicodeV1FoldRow *row = &unicode_v1_ucd_fold_index[middle];
        if (scalar < row->scalar)
            high = middle;
        else if (scalar > row->scalar)
            low = middle + 1u;
        else {
            *members = unicode_v1_ucd_fold_members + row->begin;
            *len = row->len;
            return true;
        }
    }
    *members = NULL;
    *len = 0u;
    return true;
}

UnicodeV1Outcome unicode_v1_set_simple_fold(const UnicodeV1Set *set,
                                           UnicodeV1Set *out) {
    if (!out || !unicode_v1_set_valid(set) || set->domain != UNICODE_V1_SCALARS)
        return UNICODE_V1_INVALID_ARGUMENT;
    UnicodeV1Buffer buffer = {0};
    UnicodeV1Outcome outcome = buffer_copy(&buffer, set);
    for (size_t i = 0u; outcome == UNICODE_V1_OK && i < unicode_v1_ucd_fold_index_len; i++) {
        const UnicodeV1FoldRow *row = &unicode_v1_ucd_fold_index[i];
        if (!unicode_v1_set_contains(set, row->scalar))
            continue;
        for (size_t j = 0u; outcome == UNICODE_V1_OK && j < row->len; j++) {
            uint32_t scalar = unicode_v1_ucd_fold_members[row->begin + j];
            outcome = buffer_push(&buffer, scalar, scalar);
        }
    }
    if (outcome != UNICODE_V1_OK) {
        free(buffer.items);
        return outcome;
    }
    buffer_normalize(&buffer);
    buffer_publish(&buffer, set->domain, out);
    return UNICODE_V1_OK;
}
