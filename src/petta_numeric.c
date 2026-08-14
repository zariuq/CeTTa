#define _GNU_SOURCE
#include "petta_numeric.h"

#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    PETTA_PARSED_NONE = 0,
    PETTA_PARSED_INTEGER,
    PETTA_PARSED_FLOAT,
    PETTA_PARSED_RATIONAL,
} PeTTaParsedNumberKind;

typedef struct {
    PeTTaParsedNumberKind kind;
    char *canonical;
    double floating;
} PeTTaParsedNumber;

static pthread_once_t petta_numeric_locale_once = PTHREAD_ONCE_INIT;
static locale_t petta_numeric_locale;

static void petta_numeric_locale_init(void) {
    petta_numeric_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
}

static int petta_digit_value(unsigned char byte) {
    if (byte >= (unsigned char)'0' && byte <= (unsigned char)'9')
        return (int)(byte - (unsigned char)'0');
    if (byte >= (unsigned char)'a' && byte <= (unsigned char)'z')
        return 10 + (int)(byte - (unsigned char)'a');
    if (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z')
        return 10 + (int)(byte - (unsigned char)'A');
    return -1;
}

static bool petta_scan_digits(
    const char *text, size_t length, size_t *position,
    unsigned base, bool separators) {
    size_t index = *position;
    bool saw_digit = false;
    bool previous_was_digit = false;
    while (index < length) {
        int digit = petta_digit_value((unsigned char)text[index]);
        if (digit >= 0 && (unsigned)digit < base) {
            saw_digit = true;
            previous_was_digit = true;
            index++;
            continue;
        }
        if (separators && text[index] == '_' && previous_was_digit &&
            index + 1u < length) {
            int next = petta_digit_value((unsigned char)text[index + 1u]);
            if (next >= 0 && (unsigned)next < base) {
                previous_was_digit = false;
                index++;
                continue;
            }
        }
        break;
    }
    *position = index;
    return saw_digit && previous_was_digit;
}

static char *petta_copy_without_separators(
    const char *text, size_t length) {
    char *copy = malloc(length + 1u);
    size_t output = 0u;
    if (!copy)
        return NULL;
    for (size_t index = 0u; index < length; index++) {
        if (text[index] != '_')
            copy[output++] = text[index];
    }
    copy[output] = '\0';
    return copy;
}

static char *petta_based_integer_text(
    const char *text, size_t length, size_t sign_end,
    size_t digit_start, unsigned base) {
    char *digits = petta_copy_without_separators(
        text + digit_start, length - digit_start);
    if (!digits)
        return NULL;
#if CETTA_BUILD_WITH_GMP
    mpz_t value;
    mpz_init(value);
    if (mpz_set_str(value, digits, (int)base) != 0) {
        mpz_clear(value);
        free(digits);
        return NULL;
    }
    if (sign_end && text[0] == '-')
        mpz_neg(value, value);
    size_t capacity = mpz_sizeinbase(value, 10) + 3u;
    char *canonical = malloc(capacity);
    if (canonical)
        (void)mpz_get_str(canonical, 10, value);
    mpz_clear(value);
    free(digits);
    return canonical;
#else
    uint64_t value = 0u;
    bool overflow = false;
    for (size_t index = 0u; digits[index]; index++) {
        unsigned digit = (unsigned)petta_digit_value(
            (unsigned char)digits[index]);
        if (value > (UINT64_MAX - digit) / base) {
            overflow = true;
            break;
        }
        value = value * base + digit;
    }
    free(digits);
    if (overflow)
        return NULL;
    char buffer[64];
    int written;
    if (sign_end && text[0] == '-') {
        if (value > (uint64_t)INT64_MAX + 1u)
            return NULL;
        int64_t signed_value = value == (uint64_t)INT64_MAX + 1u
            ? INT64_MIN : -(int64_t)value;
        written = snprintf(buffer, sizeof(buffer), "%" PRId64, signed_value);
    } else {
        if (value > (uint64_t)INT64_MAX)
            return NULL;
        written = snprintf(buffer, sizeof(buffer), "%" PRId64, (int64_t)value);
    }
    return written > 0 && (size_t)written < sizeof(buffer)
        ? strdup(buffer) : NULL;
#endif
}

static bool petta_utf8_codepoint(
    const char *text, size_t length, uint32_t *value) {
    const unsigned char *bytes = (const unsigned char *)text;
    uint32_t codepoint;
    size_t width;
    if (!text || length == 0u || !value)
        return false;
    if (length == 2u && bytes[0] == (unsigned char)'\\') {
        switch (bytes[1]) {
        case 'n': *value = '\n'; return true;
        case 'r': *value = '\r'; return true;
        case 't': *value = '\t'; return true;
        case '\\': *value = '\\'; return true;
        case '\'': *value = '\''; return true;
        case '"': *value = '"'; return true;
        default: return false;
        }
    }
    if (bytes[0] < 0x80u) {
        codepoint = bytes[0];
        width = 1u;
    } else if (bytes[0] >= 0xc2u && bytes[0] <= 0xdfu) {
        codepoint = bytes[0] & 0x1fu;
        width = 2u;
    } else if (bytes[0] >= 0xe0u && bytes[0] <= 0xefu) {
        codepoint = bytes[0] & 0x0fu;
        width = 3u;
    } else if (bytes[0] >= 0xf0u && bytes[0] <= 0xf4u) {
        codepoint = bytes[0] & 0x07u;
        width = 4u;
    } else {
        return false;
    }
    if (width != length)
        return false;
    for (size_t index = 1u; index < width; index++) {
        if ((bytes[index] & 0xc0u) != 0x80u)
            return false;
        codepoint = (codepoint << 6u) | (bytes[index] & 0x3fu);
    }
    if ((width == 2u && codepoint < 0x80u) ||
        (width == 3u && codepoint < 0x800u) ||
        (width == 4u && codepoint < 0x10000u) ||
        (codepoint >= 0xd800u && codepoint <= 0xdfffu) ||
        codepoint > 0x10ffffu) {
        return false;
    }
    *value = codepoint;
    return true;
}

static char *petta_codepoint_integer_text(
    const char *text, size_t length, size_t sign_end) {
    uint32_t codepoint;
    if (!petta_utf8_codepoint(
            text + sign_end + 2u, length - sign_end - 2u, &codepoint)) {
        return NULL;
    }
    int64_t signed_value = (int64_t)codepoint;
    if (sign_end && text[0] == '-')
        signed_value = -signed_value;
    char buffer[64];
    int written = snprintf(buffer, sizeof(buffer), "%" PRId64, signed_value);
    return written > 0 && (size_t)written < sizeof(buffer)
        ? strdup(buffer) : NULL;
}

static bool petta_parse_quoted_integer(
    const char *text, size_t length, size_t sign_end,
    PeTTaParsedNumber *parsed) {
    size_t separator = sign_end;
    uint32_t base = 0u;
    if (!petta_scan_digits(
            text, length, &separator, 10u, false) ||
        separator >= length || text[separator] != '\'') {
        return false;
    }
    for (size_t index = sign_end; index < separator; index++) {
        base = base * 10u + (uint32_t)(text[index] - '0');
        if (base > 36u)
            return false;
    }
    size_t digit_start = separator + 1u;
    if (base == 0u) {
        parsed->canonical = petta_codepoint_integer_text(
            text, length, sign_end);
    } else {
        if (base < 2u)
            return false;
        size_t end = digit_start;
        if (!petta_scan_digits(
                text, length, &end, base, true) || end != length) {
            return false;
        }
        parsed->canonical = petta_based_integer_text(
            text, length, sign_end, digit_start, base);
    }
    if (!parsed->canonical)
        return false;
    parsed->kind = PETTA_PARSED_INTEGER;
    return true;
}

static bool petta_parse_special_float(
    const char *text, size_t length, PeTTaParsedNumber *parsed) {
    bool negative = length > 0u && text[0] == '-';
    bool positive = length > 0u && text[0] == '+';
    const char *body = text + (negative || positive ? 1u : 0u);
    size_t body_length = length - (negative || positive ? 1u : 0u);
    if (body_length == 6u && memcmp(body, "1.0Inf", 6u) == 0) {
        parsed->kind = PETTA_PARSED_FLOAT;
        parsed->floating = negative ? -INFINITY : INFINITY;
        return true;
    }
    if (!positive && body_length == 6u &&
        memcmp(body, "1.5NaN", 6u) == 0) {
        parsed->kind = PETTA_PARSED_FLOAT;
        parsed->floating = NAN;
        return true;
    }
    return false;
}

static bool petta_parse_number(
    const uint8_t *bytes, size_t length,
    CettaPeTTaNumberGrammar grammar, PeTTaParsedNumber *parsed) {
    memset(parsed, 0, sizeof(*parsed));
    if (!bytes || length == 0u || memchr(bytes, '\0', length))
        return false;
    char *text = malloc(length + 1u);
    if (!text)
        return false;
    memcpy(text, bytes, length);
    text[length] = '\0';
    size_t sign_end = 0u;
    if (text[0] == '+' || text[0] == '-') {
        sign_end = 1u;
        if (length == 1u) {
            free(text);
            return false;
        }
    }

    if (grammar == CETTA_PETTA_NUMBER_ATOM_TEXT &&
        petta_parse_special_float(text, length, parsed)) {
        free(text);
        return true;
    }
    if (grammar == CETTA_PETTA_NUMBER_ATOM_TEXT &&
        petta_parse_quoted_integer(text, length, sign_end, parsed)) {
        free(text);
        return true;
    }
    if (grammar == CETTA_PETTA_NUMBER_ATOM_TEXT &&
        sign_end + 2u < length && text[sign_end] == '0') {
        unsigned base = 0u;
        switch (text[sign_end + 1u]) {
        case 'x': case 'X': base = 16u; break;
        case 'o': case 'O': base = 8u; break;
        case 'b': case 'B': base = 2u; break;
        default: break;
        }
        if (base != 0u) {
            size_t end = sign_end + 2u;
            if (petta_scan_digits(text, length, &end, base, true) &&
                end == length) {
                parsed->canonical = petta_based_integer_text(
                    text, length, sign_end, sign_end + 2u, base);
                if (parsed->canonical)
                    parsed->kind = PETTA_PARSED_INTEGER;
            }
            free(text);
            return parsed->kind != PETTA_PARSED_NONE;
        }
    }

    bool separators = grammar == CETTA_PETTA_NUMBER_ATOM_TEXT;
    size_t position = sign_end;
    if (!petta_scan_digits(
            text, length, &position, 10u, separators)) {
        free(text);
        return false;
    }
    if (grammar == CETTA_PETTA_NUMBER_ATOM_TEXT &&
        position < length && text[position] == 'r') {
        size_t denominator = position + 1u;
        if (petta_scan_digits(
                text, length, &denominator, 10u, true) &&
            denominator == length) {
            parsed->canonical = petta_copy_without_separators(text, length);
            if (parsed->canonical) {
                char *separator = strchr(parsed->canonical, 'r');
                if (separator)
                    *separator = '/';
                if (parsed->canonical[0] == '+')
                    memmove(
                        parsed->canonical, parsed->canonical + 1u,
                        strlen(parsed->canonical));
                parsed->kind = PETTA_PARSED_RATIONAL;
            }
        }
        free(text);
        return parsed->kind != PETTA_PARSED_NONE;
    }

    bool floating = false;
    if (position < length && text[position] == '.') {
        floating = true;
        position++;
        if (!petta_scan_digits(
                text, length, &position, 10u, separators)) {
            free(text);
            return false;
        }
    }
    if (position < length &&
        (text[position] == 'e' || text[position] == 'E')) {
        floating = true;
        position++;
        if (position < length &&
            (text[position] == '+' || text[position] == '-')) {
            position++;
        }
        if (!petta_scan_digits(
                text, length, &position, 10u, separators)) {
            free(text);
            return false;
        }
    }
    if (position != length) {
        free(text);
        return false;
    }

    parsed->canonical = petta_copy_without_separators(text, length);
    free(text);
    if (!parsed->canonical)
        return false;
    if (!floating) {
        parsed->kind = PETTA_PARSED_INTEGER;
        return true;
    }

    pthread_once(&petta_numeric_locale_once, petta_numeric_locale_init);
    if (!petta_numeric_locale) {
        free(parsed->canonical);
        parsed->canonical = NULL;
        return false;
    }
    char *end = NULL;
    errno = 0;
    parsed->floating = strtod_l(
        parsed->canonical, &end, petta_numeric_locale);
    if (!end || *end != '\0' || errno != 0 || !isfinite(parsed->floating)) {
        free(parsed->canonical);
        parsed->canonical = NULL;
        return false;
    }
    parsed->kind = PETTA_PARSED_FLOAT;
    return true;
}

static void petta_parsed_number_clear(PeTTaParsedNumber *parsed) {
    if (!parsed)
        return;
    free(parsed->canonical);
    parsed->canonical = NULL;
    parsed->kind = PETTA_PARSED_NONE;
}

Atom *cetta_petta_number_parse_atom(
    Arena *arena, const uint8_t *bytes, size_t length,
    CettaPeTTaNumberGrammar grammar) {
    PeTTaParsedNumber parsed;
    if (!arena || !petta_parse_number(bytes, length, grammar, &parsed))
        return NULL;
    Atom *result = NULL;
    switch (parsed.kind) {
    case PETTA_PARSED_INTEGER:
        result = atom_bigint(arena, parsed.canonical);
        break;
    case PETTA_PARSED_FLOAT:
        result = atom_float(arena, parsed.floating);
        break;
    case PETTA_PARSED_RATIONAL:
        result = atom_rational(arena, parsed.canonical);
        break;
    case PETTA_PARSED_NONE:
        break;
    }
    petta_parsed_number_clear(&parsed);
    return result;
}

AtomId cetta_petta_number_parse_id(
    TermUniverse *universe, const uint8_t *bytes, size_t length,
    CettaPeTTaNumberGrammar grammar) {
    PeTTaParsedNumber parsed;
    if (!universe || !petta_parse_number(bytes, length, grammar, &parsed))
        return CETTA_ATOM_ID_NONE;
    AtomId result = CETTA_ATOM_ID_NONE;
    switch (parsed.kind) {
    case PETTA_PARSED_INTEGER:
        result = tu_intern_bigint(universe, parsed.canonical);
        break;
    case PETTA_PARSED_FLOAT:
        result = tu_intern_float(universe, parsed.floating);
        break;
    case PETTA_PARSED_RATIONAL:
        result = tu_intern_rational(universe, parsed.canonical);
        break;
    case PETTA_PARSED_NONE:
        break;
    }
    petta_parsed_number_clear(&parsed);
    return result;
}
