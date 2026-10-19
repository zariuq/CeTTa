#include "native/unicode_class_v1.h"
#include "src/utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #test); \
    exit(1); } } while (0)

static int allocation_countdown = -1;

void *unicode_v1_class_reallocate(void *previous, size_t bytes) {
    if (allocation_countdown == 0)
        return NULL;
    if (allocation_countdown > 0)
        allocation_countdown--;
    return realloc(previous, bytes);
}

static UnicodeV1Set points(UnicodeV1Domain domain,
                           const CettaLpNativeUnicodeRange *ranges, size_t len) {
    UnicodeV1Set result = {0};
    CHECK(unicode_v1_set_project(domain, (UnicodeV1Ranges){ranges, len}, &result) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_valid(&result));
    return result;
}

static void test_domains(void) {
    CettaLpNativeUnicodeRange full[] = {{0u, UINT32_MAX}};
    for (int kind = UNICODE_V1_BYTES; kind <= UNICODE_V1_SCALARS; kind++) {
        UnicodeV1Set empty = {(UnicodeV1Domain)kind, NULL, 0u};
        UnicodeV1Set universe = {0}, again = {0};
        CHECK(unicode_v1_set_complement(&empty, &universe) == UNICODE_V1_OK);
        CHECK(unicode_v1_set_valid(&universe));
        UnicodeV1Set projected = points((UnicodeV1Domain)kind, full, 1u);
        CHECK(universe.len == projected.len);
        CHECK(memcmp(universe.items, projected.items, universe.len * sizeof(*universe.items)) == 0);
        CHECK(unicode_v1_set_complement(&universe, &again) == UNICODE_V1_OK);
        CHECK(again.domain == (UnicodeV1Domain)kind && again.len == 0u);
        CHECK(unicode_v1_set_contains(&universe, 0u));
        CHECK(unicode_v1_set_contains(&universe, 0xd800u) ==
              (kind != UNICODE_V1_BYTES && kind != UNICODE_V1_SCALARS));
        CHECK(unicode_v1_set_contains(&universe, UINT32_MAX) == (kind == UNICODE_V1_UTF32_UNITS));
        unicode_v1_set_free(&universe);
        unicode_v1_set_free(&again);
        unicode_v1_set_free(&projected);
    }
    CettaLpNativeUnicodeRange raw[] = {{100u, 105u}, {9u, 20u}, {0u, 10u}, {0xd7ffu, 0xe000u}};
    UnicodeV1Set scalars = points(UNICODE_V1_SCALARS, raw, 4u);
    CHECK(scalars.len == 4u && scalars.items[0].low == 0u && scalars.items[0].high == 20u);
    CHECK(!unicode_v1_set_contains(&scalars, 0xd800u));
    UnicodeV1Set bytes = points(UNICODE_V1_BYTES, raw, 4u);
    UnicodeV1Set result = {0};
    CHECK(unicode_v1_set_combine(UNICODE_V1_UNION, &scalars, &bytes, &result) == UNICODE_V1_INVALID_ARGUMENT);
    CettaLpNativeUnicodeRange reversed[] = {{8u, 7u}};
    CHECK(unicode_v1_set_project(UNICODE_V1_SCALARS, (UnicodeV1Ranges){reversed, 1u}, &result) == UNICODE_V1_INVALID_ARGUMENT);
    unicode_v1_set_free(&scalars);
    unicode_v1_set_free(&bytes);
}

static uint32_t random_state = 0x9254d783u;
static uint32_t next_random(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static void test_boolean_algebra(void) {
    for (size_t trial = 0u; trial < 2000u; trial++) {
        CettaLpNativeUnicodeRange a[20], b[20];
        bool left[256] = {0}, right[256] = {0};
        for (size_t i = 0u; i < 20u; i++) {
            uint32_t alow = next_random() & 255u, ahigh = next_random() & 255u;
            uint32_t blow = next_random() & 255u, bhigh = next_random() & 255u;
            a[i] = (CettaLpNativeUnicodeRange){alow < ahigh ? alow : ahigh, alow > ahigh ? alow : ahigh};
            b[i] = (CettaLpNativeUnicodeRange){blow < bhigh ? blow : bhigh, blow > bhigh ? blow : bhigh};
            if (trial % 3u != 0u) {
                a[i].high = a[i].low + (next_random() % 5u);
                b[i].high = b[i].low + (next_random() % 5u);
                if (a[i].high > 255u) a[i].high = 255u;
                if (b[i].high > 255u) b[i].high = 255u;
            }
            for (uint32_t cp = a[i].low; cp <= a[i].high; cp++) left[cp] = true;
            for (uint32_t cp = b[i].low; cp <= b[i].high; cp++) right[cp] = true;
        }
        UnicodeV1Set aa = points(UNICODE_V1_BYTES, a, 20u);
        UnicodeV1Set bb = points(UNICODE_V1_BYTES, b, 20u);
        bool same = true, expected_same = true;
        for (uint32_t cp = 0u; cp < 256u; cp++)
            expected_same &= left[cp] == right[cp];
        CHECK(unicode_v1_set_equal(&aa, &bb, &same) == UNICODE_V1_OK);
        CHECK(same == expected_same);
        CettaLpNativeUnicodeRange reversed[20];
        for (size_t i = 0u; i < 20u; i++) reversed[i] = a[19u - i];
        UnicodeV1Set reordered = points(UNICODE_V1_BYTES, reversed, 20u);
        CHECK(unicode_v1_set_equal(&aa, &reordered, &same) == UNICODE_V1_OK && same);
        unicode_v1_set_free(&reordered);
        for (int operation = UNICODE_V1_UNION; operation <= UNICODE_V1_SYMMETRIC_DIFFERENCE; operation++) {
            UnicodeV1Set result = {0};
            CHECK(unicode_v1_set_combine((UnicodeV1Operation)operation, &aa, &bb, &result) == UNICODE_V1_OK);
            CHECK(unicode_v1_set_valid(&result));
            for (uint32_t cp = 0u; cp < 256u; cp++) {
                bool expected = operation == UNICODE_V1_UNION ? left[cp] || right[cp] :
                    operation == UNICODE_V1_INTERSECTION ? left[cp] && right[cp] :
                    operation == UNICODE_V1_DIFFERENCE ? left[cp] && !right[cp] : left[cp] != right[cp];
                CHECK(unicode_v1_set_contains(&result, cp) == expected);
            }
            unicode_v1_set_free(&result);
        }
        CHECK(unicode_v1_set_complement(&aa, &aa) == UNICODE_V1_OK);
        for (uint32_t cp = 0u; cp < 256u; cp++) CHECK(unicode_v1_set_contains(&aa, cp) == !left[cp]);
        CHECK(unicode_v1_set_combine(UNICODE_V1_UNION, &aa, &bb, &aa) == UNICODE_V1_OK);
        for (uint32_t cp = 0u; cp < 256u; cp++) CHECK(unicode_v1_set_contains(&aa, cp) == (!left[cp] || right[cp]));
        unicode_v1_set_free(&aa);
        unicode_v1_set_free(&bb);
    }
    CettaLpNativeUnicodeRange last[] = {{UINT32_MAX, UINT32_MAX}};
    UnicodeV1Set set = points(UNICODE_V1_UTF32_UNITS, last, 1u), complement = {0};
    CHECK(unicode_v1_set_complement(&set, &complement) == UNICODE_V1_OK);
    CHECK(complement.len == 1u && complement.items[0].high == UINT32_MAX - 1u);
    CHECK(unicode_v1_set_combine(UNICODE_V1_UNION, &set, &complement, &set) == UNICODE_V1_OK);
    CHECK(set.len == 1u && set.items[0].high == UINT32_MAX);
    unicode_v1_set_free(&set);
    unicode_v1_set_free(&complement);
}

static void test_canonical_equality(void) {
    const CettaLpNativeUnicodeRange overlapping[] = {{8u, 12u}, {0u, 3u}, {3u, 8u}};
    const CettaLpNativeUnicodeRange contiguous[] = {{0u, 12u}};
    const CettaLpNativeUnicodeRange gap[] = {{0u, 3u}, {5u, 12u}};
    const CettaLpNativeUnicodeRange shorter[] = {{0u, 11u}};
    UnicodeV1Set aa = points(UNICODE_V1_BYTES, overlapping, 3u);
    UnicodeV1Set bb = points(UNICODE_V1_BYTES, contiguous, 1u);
    UnicodeV1Set cc = points(UNICODE_V1_BYTES, gap, 2u);
    UnicodeV1Set dd = points(UNICODE_V1_BYTES, shorter, 1u);
    bool same = false;
    CHECK(unicode_v1_set_equal(&aa, &bb, &same) == UNICODE_V1_OK && same);
    CHECK(unicode_v1_set_equal(&aa, &cc, &same) == UNICODE_V1_OK && !same);
    CHECK(unicode_v1_set_equal(&aa, &dd, &same) == UNICODE_V1_OK && !same);
    CHECK(unicode_v1_set_equal(&aa, &aa, &same) == UNICODE_V1_OK && same);

    UnicodeV1Set other_domain = points(UNICODE_V1_SCALARS, contiguous, 1u);
    CHECK(unicode_v1_set_equal(&aa, &other_domain, &same) == UNICODE_V1_OK && !same);
    UnicodeV1Set empty_byte = {UNICODE_V1_BYTES, NULL, 0u};
    UnicodeV1Set empty_scalar = {UNICODE_V1_SCALARS, NULL, 0u};
    CHECK(unicode_v1_set_equal(&empty_byte, &empty_scalar, &same) == UNICODE_V1_OK && !same);
    CHECK(unicode_v1_set_equal(&empty_byte, &empty_byte, &same) == UNICODE_V1_OK && same);

    CettaLpNativeUnicodeRange touching[] = {{0u, 3u}, {4u, 5u}};
    UnicodeV1Set invalid = {UNICODE_V1_BYTES, touching, 2u};
    same = true;
    CHECK(unicode_v1_set_equal(&aa, &invalid, &same) == UNICODE_V1_INVALID_ARGUMENT && same);
    UnicodeV1Set oversized = {UNICODE_V1_BYTES, touching, SIZE_MAX};
    CHECK(unicode_v1_set_equal(&aa, &oversized, &same) == UNICODE_V1_INVALID_ARGUMENT && same);
    CHECK(unicode_v1_set_equal(NULL, &bb, &same) == UNICODE_V1_INVALID_ARGUMENT && same);
    CHECK(unicode_v1_set_equal(&aa, &bb, NULL) == UNICODE_V1_INVALID_ARGUMENT);

    /* Equality allocates nothing. Construction retains its own allocation
     * and failure-atomicity contract, including for idempotent operations. */
    CettaLpNativeUnicodeRange *owned_aa = aa.items, *owned_bb = bb.items;
    allocation_countdown = 0;
    CHECK(unicode_v1_set_equal(&aa, &bb, &same) == UNICODE_V1_OK && same);
    CHECK(unicode_v1_set_combine(UNICODE_V1_UNION, &aa, &bb, &aa) == UNICODE_V1_OUT_OF_MEMORY);
    CHECK(unicode_v1_set_combine(UNICODE_V1_INTERSECTION, &aa, &bb, &bb) == UNICODE_V1_OUT_OF_MEMORY);
    CHECK(aa.items == owned_aa && bb.items == owned_bb && aa.items != bb.items);
    CHECK(unicode_v1_set_combine(UNICODE_V1_UNION, &aa, &bb, &cc) == UNICODE_V1_OUT_OF_MEMORY);
    CHECK(cc.len == 2u && cc.items[1].low == 5u);
    CHECK(unicode_v1_set_combine(UNICODE_V1_INTERSECTION, &aa, &cc, &aa) == UNICODE_V1_OUT_OF_MEMORY);
    CHECK(unicode_v1_set_combine(UNICODE_V1_INTERSECTION, &aa, &dd, &aa) == UNICODE_V1_OUT_OF_MEMORY);
    CHECK(aa.items == owned_aa && aa.items[0].high == 12u);
    allocation_countdown = -1;
    CHECK(unicode_v1_set_combine(UNICODE_V1_UNION, &aa, &bb, &aa) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_equal(&aa, &bb, &same) == UNICODE_V1_OK && same);
    CHECK(aa.items != bb.items);
    unicode_v1_set_free(&aa);
    unicode_v1_set_free(&bb);
    unicode_v1_set_free(&cc);
    unicode_v1_set_free(&dd);
    unicode_v1_set_free(&other_domain);
}

static void test_properties_and_folding(void) {
    CHECK(strcmp(unicode_v1_version(), "16.0.0") == 0);
    CHECK(strcmp(unicode_v1_property_name("General-Category"), "gc") == 0);
    CHECK(strcmp(unicode_v1_property_name("ISO_Comment"), "isc") == 0);
    CHECK(!unicode_v1_property_name(NULL));
    CHECK(!unicode_v1_property_name("not-a-property"));
    /* Regex-specific Is prefixes and non-ASCII deletion do not change the
     * generic provider's alias policy. Its whitespace policy is unchanged. */
    CHECK(!unicode_v1_property_name("IsAlphabetic"));
    CHECK(!unicode_v1_property_name("Alphabeticλ"));
    CHECK(strcmp(unicode_v1_property_name("White\tSpace"), "WSpace") == 0);
    UnicodeV1Ranges ranges;
    CHECK(unicode_v1_property("General-Category", "Decimal_Number", &ranges));
    UnicodeV1Set digits = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&digits, 0x663u));
    CHECK(unicode_v1_set_contains(&digits, 0xff11u));
    CHECK(unicode_v1_set_contains(&digits, 0x1d7d9u));
    CHECK(!unicode_v1_set_contains(&digits, 0xb2u));
    uint32_t value = 999u, zero = 999u;
    CHECK(unicode_v1_decimal_value(0x663u, &value, &zero) && value == 3u && zero == 0x660u);
    CHECK(!unicode_v1_decimal_value(0xb2u, &value, &zero));
    CHECK(unicode_v1_property("Script", "Greek", &ranges));
    UnicodeV1Set greek = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&greek, 0x3bbu) && !unicode_v1_set_contains(&greek, 'x'));
    CHECK(unicode_v1_character_class("Greek", &ranges));
    CHECK(unicode_v1_character_class("Sc", &ranges));
    UnicodeV1Set currency = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&currency, '$'));
    CHECK(unicode_v1_property("gc", "Cs", &ranges));
    UnicodeV1Set surrogates = points(UNICODE_V1_CODE_POINTS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&surrogates, 0xd800u));
    CHECK(unicode_v1_set_project(UNICODE_V1_SCALARS, ranges, &surrogates) == UNICODE_V1_OK);
    CHECK(surrogates.len == 0u);
    CHECK(!unicode_v1_property("not-a-property", "Y", &ranges));
    CHECK(!unicode_v1_property("Greek", "Y", &ranges));
    CHECK(unicode_v1_property("White_Space", NULL, &ranges));
    UnicodeV1Set space = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&space, 0xa0u));
    CHECK(!unicode_v1_set_contains(&space, 0x1cu));
    CHECK(!unicode_v1_set_contains(&space, 0x200bu));
    CHECK(unicode_v1_property("Script", "Unknown", &ranges));
    UnicodeV1Set unknown = points(UNICODE_V1_CODE_POINTS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&unknown, 0x378u));
    CHECK(!unicode_v1_set_contains(&unknown, 0x3bbu));
    CHECK(unicode_v1_property("Script", "Katakana_Or_Hiragana", &ranges) && !ranges.len);
    CHECK(unicode_v1_property("GCB", "E_Base", &ranges) && !ranges.len);
    CHECK(unicode_v1_property("GCB", "Other", &ranges));
    UnicodeV1Set other_break = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&other_break, 'a'));
    unicode_v1_set_free(&other_break);
    const uint32_t *members;
    size_t len;
    CHECK(unicode_v1_simple_fold_class(0x3c2u, &members, &len) && len == 3u);
    CHECK(members[0] == 0x3a3u && members[1] == 0x3c2u && members[2] == 0x3c3u);
    CettaLpNativeUnicodeRange sigma_range[] = {{0x3c2u, 0x3c2u}};
    UnicodeV1Set sigma = points(UNICODE_V1_SCALARS, sigma_range, 1u);
    CHECK(unicode_v1_set_simple_fold(&sigma, &sigma) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_contains(&sigma, 0x3a3u) && unicode_v1_set_contains(&sigma, 0x3c3u));
    CHECK(!unicode_v1_simple_fold_class(0xd800u, &members, &len));
    unicode_v1_set_free(&digits); unicode_v1_set_free(&greek); unicode_v1_set_free(&currency);
    unicode_v1_set_free(&surrogates); unicode_v1_set_free(&space);
    unicode_v1_set_free(&unknown); unicode_v1_set_free(&sigma);
    for (size_t index = 0u; index < unicode_v1_property_count(); index++) {
        const char *p, *v;
        CHECK(unicode_v1_property_at(index, &p, &v, &ranges));
        UnicodeV1Set set = points(UNICODE_V1_CODE_POINTS, ranges.items, ranges.len);
        CHECK(unicode_v1_set_valid(&set));
        UnicodeV1Ranges resolved;
        CHECK(unicode_v1_property(p, v, &resolved));
        CHECK(resolved.len == ranges.len);
        CHECK(resolved.items == ranges.items);
        unicode_v1_set_free(&set);
    }
}

static void test_lexical_clients(void) {
    UnicodeV1Ranges scalar_ranges, decimal_ranges, hex_ranges;
    CHECK(unicode_v1_lexical_class("UnicodeScalar", &scalar_ranges));
    CHECK(unicode_v1_lexical_class("AsciiDecimal", &decimal_ranges));
    CHECK(unicode_v1_lexical_class("AsciiHex", &hex_ranges));
    CHECK(!unicode_v1_lexical_class("absent", &hex_ranges));
    UnicodeV1Set scalars = points(UNICODE_V1_CODE_POINTS, scalar_ranges.items, scalar_ranges.len);
    UnicodeV1Set decimal = points(UNICODE_V1_CODE_POINTS, decimal_ranges.items, decimal_ranges.len);
    UnicodeV1Set hex = points(UNICODE_V1_CODE_POINTS, hex_ranges.items, hex_ranges.len);
    /* Reconstruct the authored claims by a separate native operation path. */
    CettaLpNativeUnicodeRange explicit_hex[] = {{48u, 57u}, {65u, 70u}, {97u, 102u}};
    UnicodeV1Set expanded_hex = points(UNICODE_V1_CODE_POINTS, explicit_hex, 3u);
    bool same = false;
    CHECK(unicode_v1_set_equal(&hex, &expanded_hex, &same) == UNICODE_V1_OK && same);
    CettaLpNativeUnicodeRange full_points[] = {{0u, 0x10ffffu}};
    CettaLpNativeUnicodeRange surrogate_points[] = {{0xd800u, 0xdfffu}};
    UnicodeV1Set projected = points(UNICODE_V1_CODE_POINTS, full_points, 1u);
    UnicodeV1Set surrogates = points(UNICODE_V1_CODE_POINTS, surrogate_points, 1u);
    CHECK(unicode_v1_set_combine(UNICODE_V1_DIFFERENCE, &projected, &surrogates, &projected) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_equal(&scalars, &projected, &same) == UNICODE_V1_OK && same);
    UnicodeV1Ranges nd;
    CHECK(unicode_v1_property("gc", "Nd", &nd));
    UnicodeV1Set digit_class = points(UNICODE_V1_SCALARS, nd.items, nd.len);
    UnicodeV1Set involution = {0};
    CHECK(unicode_v1_set_complement(&digit_class, &involution) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_complement(&involution, &involution) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_equal(&digit_class, &involution, &same) == UNICODE_V1_OK && same);
    const UnicodeV1DigitClass *decimal_values = unicode_v1_digit_class("AsciiDecimal");
    const UnicodeV1DigitClass *hex_values = unicode_v1_digit_class("AsciiHex");
    CHECK(decimal_values && hex_values && !unicode_v1_digit_class("absent"));
    for (uint32_t cp = 0u; cp <= 0x10ffffu; cp++) {
        bool scalar = cp < 0xd800u || cp > 0xdfffu;
        bool is_decimal = cp >= '0' && cp <= '9';
        bool is_hex = is_decimal || (cp >= 'A' && cp <= 'F') || (cp >= 'a' && cp <= 'f');
        CHECK(unicode_v1_set_contains(&scalars, cp) == scalar);
        CHECK(unicode_v1_set_contains(&decimal, cp) == is_decimal);
        CHECK(unicode_v1_set_contains(&hex, cp) == is_hex);
        uint32_t digit = UINT32_MAX;
        CHECK(unicode_v1_lexical_digit_value(decimal_values, cp, &digit) == is_decimal);
        CHECK(digit == (is_decimal ? cp - '0' : UINT32_MAX));
        digit = UINT32_MAX;
        CHECK(unicode_v1_lexical_digit_value(hex_values, cp, &digit) == is_hex);
        uint32_t expected = is_decimal ? cp - '0' : cp >= 'a' && cp <= 'f' ? cp - 'a' + 10u :
            cp >= 'A' && cp <= 'F' ? cp - 'A' + 10u : UINT32_MAX;
        CHECK(digit == expected);
        if (scalar) {
            uint8_t bytes[4];
            size_t encoded = cetta_utf8_encode(cp, bytes);
            uint32_t decoded = UINT32_MAX, width = 0u;
            CHECK(cetta_utf8_decode_one(bytes, encoded, 0u, &decoded, &width));
            CHECK(decoded == cp && width == encoded);
        }
    }
    static const uint8_t malformed[][4] = {
        {0x80u}, {0xc0u, 0x80u}, {0xe0u, 0x80u, 0x80u}, {0xedu, 0xa0u, 0x80u},
        {0xf0u, 0x80u, 0x80u, 0x80u}, {0xf4u, 0x90u, 0x80u, 0x80u}, {0xf5u, 0x80u, 0x80u, 0x80u},
    };
    const size_t lengths[] = {1u, 2u, 3u, 3u, 4u, 4u, 4u};
    for (size_t index = 0u; index < sizeof(lengths) / sizeof(lengths[0]); index++) {
        uint32_t scalar, width;
        CHECK(!cetta_utf8_decode_one(malformed[index], lengths[index], 0u, &scalar, &width));
    }
    uint32_t digit = UINT32_MAX;
    CHECK(!unicode_v1_lexical_digit_value(NULL, '0', &digit) && digit == UINT32_MAX);
    CHECK(!unicode_v1_lexical_digit_value(hex_values, UINT32_MAX, &digit) && digit == UINT32_MAX);
    unicode_v1_set_free(&scalars);
    unicode_v1_set_free(&decimal);
    unicode_v1_set_free(&hex);
    unicode_v1_set_free(&expanded_hex);
    unicode_v1_set_free(&projected);
    unicode_v1_set_free(&surrogates);
    unicode_v1_set_free(&digit_class);
    unicode_v1_set_free(&involution);
}

static void test_allocation_atomicity(void) {
    CettaLpNativeUnicodeRange input[70];
    for (uint32_t i = 0u; i < 70u; i++) input[i] = (CettaLpNativeUnicodeRange){i * 3u, i * 3u};
    CettaLpNativeUnicodeRange old[] = {{5u, 7u}};
    UnicodeV1Set original = points(UNICODE_V1_BYTES, old, 1u);
    CettaLpNativeUnicodeRange *identity = original.items;
    for (int failure = 0; failure < 5; failure++) {
        allocation_countdown = failure;
        CHECK(unicode_v1_set_project(UNICODE_V1_SCALARS, (UnicodeV1Ranges){input, 70u}, &original) == UNICODE_V1_OUT_OF_MEMORY);
        CHECK(original.items == identity && original.domain == UNICODE_V1_BYTES && original.len == 1u);
        CHECK(original.items[0].low == 5u && original.items[0].high == 7u);
    }
    allocation_countdown = -1;
    CHECK(unicode_v1_set_project(UNICODE_V1_SCALARS, (UnicodeV1Ranges){input, 70u}, &original) == UNICODE_V1_OK);
    identity = original.items;
    allocation_countdown = 0;
    CHECK(unicode_v1_set_complement(&original, &original) == UNICODE_V1_OUT_OF_MEMORY);
    CHECK(original.items == identity && original.len == 70u);
    allocation_countdown = -1;
    unicode_v1_set_free(&original);
}

static void test_regex_classes_and_mixed_numerals(void) {
    const uint32_t mixed[] = {'1', 0x662u, 0x969u, 0xff14u, 0x1d7ddu};
    uint32_t number = 0u;
    for (size_t i = 0u; i < sizeof(mixed) / sizeof(mixed[0]); i++) {
        uint32_t value = UINT32_MAX;
        CHECK(unicode_v1_decimal_value(mixed[i], &value, NULL));
        number = number * 10u + value;
    }
    CHECK(number == 12345u);
    const uint32_t another[] = {0x1e950u, '2', 0x6f7u};
    number = 0u;
    for (size_t i = 0u; i < sizeof(another) / sizeof(another[0]); i++) {
        uint32_t value;
        CHECK(unicode_v1_decimal_value(another[i], &value, NULL));
        number = number * 10u + value;
    }
    CHECK(number == 27u);
    uint32_t unchanged = 99u;
    CHECK(!unicode_v1_decimal_value(0xb2u, &unchanged, NULL) && unchanged == 99u);
    CHECK(!unicode_v1_decimal_value(0x2163u, &unchanged, NULL) && unchanged == 99u);

    UnicodeV1Ranges ranges;
    CHECK(unicode_v1_lexical_class("RegexWord", &ranges));
    UnicodeV1Set word = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&word, 0x3bbu));
    CHECK(unicode_v1_set_contains(&word, 0x301u));
    CHECK(unicode_v1_set_contains(&word, 0x10400u));
    CHECK(unicode_v1_set_contains(&word, 0x200cu));
    CHECK(unicode_v1_set_contains(&word, '_'));
    CHECK(!unicode_v1_set_contains(&word, ' '));
    CHECK(!unicode_v1_set_contains(&word, '-'));
    CHECK(!unicode_v1_set_contains(&word, 0x1f600u));
    CHECK(!unicode_v1_set_contains(&word, 0xd800u));
    CHECK(unicode_v1_lexical_class("RegexWhitespace", &ranges));
    UnicodeV1Set whitespace = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    CHECK(unicode_v1_set_contains(&whitespace, 0x202fu));
    CHECK(unicode_v1_set_contains(&whitespace, 0x3000u));
    CHECK(!unicode_v1_set_contains(&whitespace, 0x200bu));
    CHECK(unicode_v1_lexical_class("UnicodeDecimal", &ranges));
    UnicodeV1Set decimal = points(UNICODE_V1_SCALARS, ranges.items, ranges.len);
    size_t digits = 0u;
    for (uint32_t cp = 0u; cp <= 0x10ffffu; cp++) {
        uint32_t value;
        bool is_decimal = unicode_v1_decimal_value(cp, &value, NULL);
        CHECK(is_decimal == unicode_v1_set_contains(&decimal, cp));
        digits += is_decimal;
    }
    CHECK(digits == 760u);

    CettaLpNativeUnicodeRange a[] = {{'a', 'a'}};
    UnicodeV1Set positive = points(UNICODE_V1_SCALARS, a, 1u);
    UnicodeV1Set folded = {0}, negative = {0}, wrong_order = {0};
    CHECK(unicode_v1_set_simple_fold(&positive, &folded) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_complement(&folded, &negative) == UNICODE_V1_OK);
    CHECK(!unicode_v1_set_contains(&negative, 'a'));
    CHECK(!unicode_v1_set_contains(&negative, 'A'));
    CHECK(unicode_v1_set_contains(&negative, 'b'));
    CHECK(unicode_v1_set_complement(&positive, &wrong_order) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_simple_fold(&wrong_order, &wrong_order) == UNICODE_V1_OK);
    CHECK(unicode_v1_set_contains(&wrong_order, 'a'));
    CHECK(unicode_v1_set_contains(&wrong_order, 'A'));
    const uint32_t *members;
    size_t count;
    CHECK(unicode_v1_simple_fold_class(0x10400u, &members, &count));
    CHECK(count == 2u && members[0] == 0x10400u && members[1] == 0x10428u);
    CHECK(unicode_v1_simple_fold_class(0x212au, &members, &count));
    CHECK(count == 3u && members[0] == 'K' && members[1] == 'k' && members[2] == 0x212au);
    unicode_v1_set_free(&word); unicode_v1_set_free(&whitespace);
    unicode_v1_set_free(&decimal); unicode_v1_set_free(&positive);
    unicode_v1_set_free(&folded); unicode_v1_set_free(&negative);
    unicode_v1_set_free(&wrong_order);
}

static int dump_properties(void) {
    for (size_t index = 0u; index < unicode_v1_property_count(); index++) {
        const char *p, *v;
        UnicodeV1Ranges ranges;
        CHECK(unicode_v1_property_at(index, &p, &v, &ranges));
        printf("property\t%s\t%s\n", p, v);
        for (size_t i = 0u; i < ranges.len; i++) printf("range\t%u\t%u\n", ranges.items[i].low, ranges.items[i].high);
        puts("end");
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--dump-properties") == 0) return dump_properties();
    if (argc == 2 && strcmp(argv[1], "--dump-digits") == 0) {
        for (uint32_t cp = 0u; cp <= 0x10ffffu; cp++) {
            uint32_t value, zero;
            if (unicode_v1_decimal_value(cp, &value, &zero)) printf("digit\t%u\t%u\t%u\n", cp, value, zero);
        }
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--dump-folds") == 0) {
        for (uint32_t cp = 0u; cp <= 0x10ffffu; cp++) {
            const uint32_t *members;
            size_t len;
            if (!unicode_v1_simple_fold_class(cp, &members, &len) || !len) continue;
            printf("fold\t%u", cp);
            for (size_t i = 0u; i < len; i++) printf("\t%u", members[i]);
            putchar('\n');
        }
        return 0;
    }
    CHECK(argc == 1);
    test_domains(); test_boolean_algebra(); test_canonical_equality();
    test_properties_and_folding(); test_allocation_atomicity();
    test_lexical_clients();
    test_regex_classes_and_mixed_numerals();
    printf("PASS: Unicode %s class algebra, domains, aliases, digits, folding and allocation atomicity\n", unicode_v1_version());
    return 0;
}
