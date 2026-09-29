#define _GNU_SOURCE
#include "native/decimal_value_v1.h"

#include <locale.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unsigned integers as little-endian base 2^32 limbs; len has no zero
 * limb at the top, and zero has len 0. */
typedef struct {
    uint32_t *limbs;
    size_t len;
    size_t cap;
} DvNat;

static void dv_free(DvNat *n) {
    free(n->limbs);
    n->limbs = NULL;
    n->len = n->cap = 0u;
}

static bool dv_reserve(DvNat *n, size_t cap) {
    uint32_t *grown;
    if (cap <= n->cap)
        return true;
    grown = realloc(n->limbs, cap * sizeof(*grown));
    if (!grown)
        return false;
    memset(grown + n->cap, 0, (cap - n->cap) * sizeof(*grown));
    n->limbs = grown;
    n->cap = cap;
    return true;
}

static void dv_trim(DvNat *n) {
    while (n->len > 0u && n->limbs[n->len - 1u] == 0u)
        n->len--;
}

static bool dv_copy(DvNat *out, const DvNat *in) {
    if (!dv_reserve(out, in->len ? in->len : 1u))
        return false;
    if (in->len)
        memcpy(out->limbs, in->limbs, in->len * sizeof(*in->limbs));
    out->len = in->len;
    return true;
}

/* n = n * factor + addend */
static bool dv_mul_add_small(DvNat *n, uint32_t factor, uint32_t addend) {
    uint64_t carry = addend;
    for (size_t i = 0u; i < n->len; i++) {
        uint64_t v = (uint64_t)n->limbs[i] * factor + carry;
        n->limbs[i] = (uint32_t)v;
        carry = v >> 32;
    }
    if (carry) {
        if (!dv_reserve(n, n->len + 1u))
            return false;
        n->limbs[n->len++] = (uint32_t)carry;
    }
    return true;
}

/* n = n / divisor, returning the remainder */
static uint32_t dv_div_small(DvNat *n, uint32_t divisor) {
    uint64_t rem = 0u;
    for (size_t i = n->len; i > 0u; i--) {
        uint64_t v = (rem << 32) | n->limbs[i - 1u];
        n->limbs[i - 1u] = (uint32_t)(v / divisor);
        rem = v % divisor;
    }
    dv_trim(n);
    return (uint32_t)rem;
}

static bool dv_from_digits(DvNat *n, const char *digits, size_t len) {
    n->len = 0u;
    if (!dv_reserve(n, 1u))
        return false;
    for (size_t i = 0u; i < len; i++) {
        if (!dv_mul_add_small(n, 10u, (uint32_t)(digits[i] - '0')))
            return false;
    }
    dv_trim(n);
    return true;
}

static char *dv_to_digits(const DvNat *in) {
    DvNat work = {0};
    char *text;
    size_t cap;
    size_t len = 0u;
    if (in->len == 0u) {
        text = malloc(2u);
        if (text) {
            text[0] = '0';
            text[1] = '\0';
        }
        return text;
    }
    if (!dv_copy(&work, in))
        return NULL;
    cap = in->len * 10u + 2u;
    text = malloc(cap);
    if (!text) {
        dv_free(&work);
        return NULL;
    }
    while (work.len > 0u)
        text[len++] = (char)('0' + dv_div_small(&work, 10u));
    dv_free(&work);
    for (size_t i = 0u; i < len / 2u; i++) {
        char c = text[i];
        text[i] = text[len - 1u - i];
        text[len - 1u - i] = c;
    }
    text[len] = '\0';
    return text;
}

static int dv_compare(const DvNat *a, const DvNat *b) {
    if (a->len != b->len)
        return a->len < b->len ? -1 : 1;
    for (size_t i = a->len; i > 0u; i--) {
        if (a->limbs[i - 1u] != b->limbs[i - 1u])
            return a->limbs[i - 1u] < b->limbs[i - 1u] ? -1 : 1;
    }
    return 0;
}

/* a = a - b, where a >= b */
static void dv_sub(DvNat *a, const DvNat *b) {
    uint64_t borrow = 0u;
    for (size_t i = 0u; i < a->len; i++) {
        uint64_t sub = (i < b->len ? b->limbs[i] : 0u) + borrow;
        uint64_t cur = a->limbs[i];
        borrow = cur < sub;
        a->limbs[i] = (uint32_t)(cur - sub);
    }
    dv_trim(a);
}

static size_t dv_trailing_zero_bits(const DvNat *n) {
    size_t bits = 0u;
    for (size_t i = 0u; i < n->len; i++) {
        uint32_t limb = n->limbs[i];
        if (limb == 0u) {
            bits += 32u;
            continue;
        }
        while ((limb & 1u) == 0u) {
            limb >>= 1;
            bits++;
        }
        return bits;
    }
    return bits;
}

static void dv_shift_right(DvNat *n, size_t bits) {
    size_t words = bits / 32u;
    unsigned shift = (unsigned)(bits % 32u);
    if (words >= n->len) {
        n->len = 0u;
        return;
    }
    for (size_t i = 0u; i + words < n->len; i++) {
        uint64_t lo = n->limbs[i + words];
        uint64_t hi = i + words + 1u < n->len ? n->limbs[i + words + 1u] : 0u;
        n->limbs[i] = shift ? (uint32_t)((lo >> shift) | (hi << (32u - shift)))
                            : (uint32_t)lo;
    }
    n->len -= words;
    dv_trim(n);
}

static bool dv_shift_left(DvNat *n, size_t bits) {
    size_t words = bits / 32u;
    unsigned shift = (unsigned)(bits % 32u);
    size_t old_len = n->len;
    if (old_len == 0u)
        return true;
    if (!dv_reserve(n, old_len + words + 1u))
        return false;
    n->limbs[old_len + words] = 0u;
    for (size_t i = old_len; i > 0u; i--) {
        uint64_t v = (uint64_t)n->limbs[i - 1u] << shift;
        n->limbs[i - 1u + words + 1u] |= (uint32_t)(v >> 32);
        n->limbs[i - 1u + words] = (uint32_t)v;
    }
    for (size_t i = 0u; i < words; i++)
        n->limbs[i] = 0u;
    n->len = old_len + words + 1u;
    dv_trim(n);
    return true;
}

/* Binary greatest common divisor of two nonzero values. */
static bool dv_gcd(const DvNat *x, const DvNat *y, DvNat *out) {
    DvNat a = {0};
    DvNat b = {0};
    size_t shift;
    size_t za;
    size_t zb;
    bool ok = false;
    if (!dv_copy(&a, x) || !dv_copy(&b, y))
        goto done;
    za = dv_trailing_zero_bits(&a);
    zb = dv_trailing_zero_bits(&b);
    shift = za < zb ? za : zb;
    dv_shift_right(&a, za);
    while (b.len > 0u) {
        dv_shift_right(&b, dv_trailing_zero_bits(&b));
        if (dv_compare(&a, &b) > 0) {
            DvNat t = a;
            a = b;
            b = t;
        }
        dv_sub(&b, &a);
    }
    if (!dv_shift_left(&a, shift))
        goto done;
    dv_free(out);
    *out = a;
    a = (DvNat){0};
    ok = true;
done:
    dv_free(&a);
    dv_free(&b);
    return ok;
}

/* quotient = dividend / divisor by shift and subtract; divisor is nonzero */
static bool dv_div(const DvNat *dividend, const DvNat *divisor, DvNat *quotient) {
    DvNat rem = {0};
    size_t bits = dividend->len * 32u;
    quotient->len = 0u;
    if (!dv_reserve(quotient, dividend->len ? dividend->len : 1u) ||
        !dv_reserve(&rem, divisor->len + 1u)) {
        dv_free(&rem);
        return false;
    }
    memset(quotient->limbs, 0, quotient->cap * sizeof(*quotient->limbs));
    quotient->len = dividend->len;
    for (size_t bit = bits; bit > 0u; bit--) {
        size_t index = bit - 1u;
        uint32_t value = (dividend->limbs[index / 32u] >> (index % 32u)) & 1u;
        if (!dv_shift_left(&rem, 1u))
            goto fail;
        if (value) {
            if (rem.len == 0u) {
                rem.limbs[0] = 1u;
                rem.len = 1u;
            } else {
                rem.limbs[0] |= 1u;
            }
        }
        if (dv_compare(&rem, divisor) >= 0) {
            dv_sub(&rem, divisor);
            quotient->limbs[index / 32u] |= (uint32_t)1u << (index % 32u);
        }
    }
    dv_trim(quotient);
    dv_free(&rem);
    return true;
fail:
    dv_free(&rem);
    return false;
}

static bool dv_is_digit(char c) {
    return c >= '0' && c <= '9';
}

static char *dv_concat_sign(bool negative, char *digits) {
    size_t len;
    char *out;
    if (!digits || !negative)
        return digits;
    len = strlen(digits);
    out = malloc(len + 2u);
    if (out) {
        out[0] = '-';
        memcpy(out + 1u, digits, len + 1u);
    }
    free(digits);
    return out;
}

bool cetta_decimal_rational_value_v1(
    const char *text, char **numerator, char **denominator) {
    const char *p = text;
    const char *num;
    const char *den;
    size_t num_len = 0u;
    size_t den_len = 0u;
    bool negative = false;
    DvNat n = {0};
    DvNat d = {0};
    DvNat g = {0};
    DvNat nq = {0};
    DvNat dq = {0};
    char *num_text = NULL;
    char *den_text = NULL;
    bool ok = false;

    if (!text || !numerator || !denominator)
        return false;
    *numerator = *denominator = NULL;
    if (*p == '+' || *p == '-')
        negative = *p++ == '-';
    num = p;
    while (dv_is_digit(*p)) {
        p++;
        num_len++;
    }
    if (num_len == 0u || *p != '/')
        return false;
    den = ++p;
    while (dv_is_digit(*p)) {
        p++;
        den_len++;
    }
    if (den_len == 0u || *p != '\0')
        return false;
    if (!dv_from_digits(&n, num, num_len) || !dv_from_digits(&d, den, den_len))
        goto done;
    if (d.len == 0u)
        goto done;
    if (n.len == 0u) {
        num_text = malloc(2u);
        den_text = malloc(2u);
        if (!num_text || !den_text)
            goto done;
        strcpy(num_text, "0");
        strcpy(den_text, "1");
        ok = true;
        goto done;
    }
    if (!dv_gcd(&n, &d, &g) || !dv_div(&n, &g, &nq) || !dv_div(&d, &g, &dq))
        goto done;
    num_text = dv_concat_sign(negative, dv_to_digits(&nq));
    den_text = dv_to_digits(&dq);
    ok = num_text && den_text;
done:
    dv_free(&n);
    dv_free(&d);
    dv_free(&g);
    dv_free(&nq);
    dv_free(&dq);
    if (!ok) {
        free(num_text);
        free(den_text);
        return false;
    }
    *numerator = num_text;
    *denominator = den_text;
    return true;
}

/* A signed decimal text plus a small signed offset, as a signed decimal. */
static char *dv_add_offset(bool negative, const char *digits, size_t len,
                           int64_t offset) {
    /* Work on magnitudes: value = (negative ? -1 : 1) * digits + offset. */
    DvNat a = {0};
    DvNat b = {0};
    uint64_t magnitude = offset < 0 ? (uint64_t)(-(offset + 1)) + 1u
                                    : (uint64_t)offset;
    bool offset_negative = offset < 0;
    bool result_negative;
    char *text = NULL;
    if (!dv_from_digits(&a, digits, len) || !dv_reserve(&b, 2u))
        goto done;
    b.limbs[0] = (uint32_t)magnitude;
    b.limbs[1] = (uint32_t)(magnitude >> 32);
    b.len = 2u;
    dv_trim(&b);
    if (negative == offset_negative) {
        /* same sign: add magnitudes */
        uint64_t carry = 0u;
        size_t len_max = a.len > b.len ? a.len : b.len;
        if (!dv_reserve(&a, len_max + 1u))
            goto done;
        for (size_t i = 0u; i < len_max; i++) {
            uint64_t v = (uint64_t)(i < a.len ? a.limbs[i] : 0u) +
                         (i < b.len ? b.limbs[i] : 0u) + carry;
            a.limbs[i] = (uint32_t)v;
            carry = v >> 32;
        }
        a.len = len_max;
        if (carry)
            a.limbs[a.len++] = (uint32_t)carry;
        result_negative = negative;
        text = dv_to_digits(&a);
    } else if (dv_compare(&a, &b) >= 0) {
        dv_sub(&a, &b);
        result_negative = negative && a.len > 0u;
        text = dv_to_digits(&a);
    } else {
        dv_sub(&b, &a);
        result_negative = offset_negative;
        text = dv_to_digits(&b);
    }
    text = dv_concat_sign(result_negative, text);
done:
    dv_free(&a);
    dv_free(&b);
    return text;
}

static bool dv_append(char **buf, size_t *len, size_t *cap, const char *s, size_t n) {
    if (*len + n + 1u > *cap) {
        size_t next = (*cap ? *cap : 32u);
        char *grown;
        while (next < *len + n + 1u)
            next *= 2u;
        grown = realloc(*buf, next);
        if (!grown)
            return false;
        *buf = grown;
        *cap = next;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
    return true;
}

static bool dv_append_zeros(char **buf, size_t *len, size_t *cap, int64_t count) {
    static const char zeros[] = "0000000000000000";
    while (count > 0) {
        size_t n = count > 16 ? 16u : (size_t)count;
        if (!dv_append(buf, len, cap, zeros, n))
            return false;
        count -= (int64_t)n;
    }
    return true;
}

bool cetta_decimal_real_canonical_v1(const char *text, char **out) {
    const char *p = text;
    const char *int_digits;
    const char *frac_digits = NULL;
    const char *exp_digits = NULL;
    size_t int_len = 0u;
    size_t frac_len = 0u;
    size_t exp_len = 0u;
    bool negative = false;
    bool exp_negative = false;
    char *digits = NULL;
    size_t n = 0u;
    size_t trailing = 0u;
    char *buf = NULL;
    size_t len = 0u;
    size_t cap = 0u;
    bool exp_small;
    int64_t exp_value = 0;

    if (!text || !out)
        return false;
    *out = NULL;
    if (*p == '+' || *p == '-')
        negative = *p++ == '-';
    int_digits = p;
    while (dv_is_digit(*p)) {
        p++;
        int_len++;
    }
    if (int_len == 0u)
        return false;
    if (*p == '.') {
        frac_digits = ++p;
        while (dv_is_digit(*p)) {
            p++;
            frac_len++;
        }
        if (frac_len == 0u)
            return false;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-')
            exp_negative = *p++ == '-';
        exp_digits = p;
        while (dv_is_digit(*p)) {
            p++;
            exp_len++;
        }
        if (exp_len == 0u)
            return false;
    }
    if (*p != '\0' || (!frac_digits && !exp_digits))
        return false;

    /* The significant digits and the exponent of their last digit. */
    digits = malloc(int_len + frac_len + 1u);
    if (!digits)
        return false;
    for (size_t i = 0u; i < int_len; i++) {
        if (n > 0u || int_digits[i] != '0')
            digits[n++] = int_digits[i];
    }
    for (size_t i = 0u; i < frac_len; i++) {
        if (n > 0u || frac_digits[i] != '0')
            digits[n++] = frac_digits[i];
    }
    if (n == 0u) {
        free(digits);
        *out = malloc(4u);
        if (!*out)
            return false;
        strcpy(*out, "0.0");
        return true;
    }
    while (digits[n - 1u - trailing] == '0')
        trailing++;
    n -= trailing;
    digits[n] = '\0';

    /* point = n + exponent - frac_len + trailing: the value is
     * 0.DIGITS x 10^point. */
    {
        const char *e = exp_digits;
        size_t elen = exp_len;
        while (elen > 1u && *e == '0') {
            e++;
            elen--;
        }
        exp_small = !exp_digits || elen <= 17u;
        if (exp_digits && exp_small) {
            for (size_t i = 0u; i < elen; i++)
                exp_value = exp_value * 10 + (e[i] - '0');
            if (exp_negative)
                exp_value = -exp_value;
        }
        exp_digits = e;
        exp_len = elen;
    }
    if (negative && !dv_append(&buf, &len, &cap, "-", 1u))
        goto fail;
    if (exp_small) {
        int64_t point = (int64_t)n + exp_value - (int64_t)frac_len + (int64_t)trailing;
        if ((int64_t)n <= point && point <= 21) {
            if (!dv_append(&buf, &len, &cap, digits, n) ||
                !dv_append_zeros(&buf, &len, &cap, point - (int64_t)n) ||
                !dv_append(&buf, &len, &cap, ".0", 2u))
                goto fail;
        } else if (0 < point && point <= 21) {
            if (!dv_append(&buf, &len, &cap, digits, (size_t)point) ||
                !dv_append(&buf, &len, &cap, ".", 1u) ||
                !dv_append(&buf, &len, &cap, digits + point, n - (size_t)point))
                goto fail;
        } else if (-6 < point && point <= 0) {
            if (!dv_append(&buf, &len, &cap, "0.", 2u) ||
                !dv_append_zeros(&buf, &len, &cap, -point) ||
                !dv_append(&buf, &len, &cap, digits, n))
                goto fail;
        } else {
            char exponent[32];
            snprintf(exponent, sizeof(exponent), "E%lld", (long long)(point - 1));
            if (!dv_append(&buf, &len, &cap, digits, 1u) ||
                (n > 1u && (!dv_append(&buf, &len, &cap, ".", 1u) ||
                            !dv_append(&buf, &len, &cap, digits + 1u, n - 1u))) ||
                !dv_append(&buf, &len, &cap, exponent, strlen(exponent)))
                goto fail;
        }
    } else {
        /* An exponent of more than 17 digits puts the point far outside the
         * positional range: write the exponent point - 1 exactly. */
        int64_t offset = (int64_t)n - (int64_t)frac_len + (int64_t)trailing - 1;
        char *exponent = dv_add_offset(exp_negative, exp_digits, exp_len, offset);
        bool appended;
        if (!exponent)
            goto fail;
        appended = dv_append(&buf, &len, &cap, digits, 1u) &&
            (n == 1u || (dv_append(&buf, &len, &cap, ".", 1u) &&
                         dv_append(&buf, &len, &cap, digits + 1u, n - 1u))) &&
            dv_append(&buf, &len, &cap, "E", 1u) &&
            dv_append(&buf, &len, &cap, exponent, strlen(exponent));
        free(exponent);
        if (!appended)
            goto fail;
    }
    free(digits);
    *out = buf;
    return true;
fail:
    free(digits);
    free(buf);
    return false;
}

static pthread_once_t dv_locale_once = PTHREAD_ONCE_INIT;
static locale_t dv_locale;

static void dv_locale_init(void) {
    dv_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
}

bool cetta_decimal_real_double_v1(const char *text, double *out) {
    char *canonical = NULL;
    char *end = NULL;
    double value;
    bool zero;
    if (!out || !cetta_decimal_real_canonical_v1(text, &canonical))
        return false;
    zero = strcmp(canonical, "0.0") == 0;
    free(canonical);
    (void)pthread_once(&dv_locale_once, dv_locale_init);
    if (!dv_locale)
        return false;
    value = strtod_l(text, &end, dv_locale);
    if (!end || *end != '\0' || !isfinite(value) || (value == 0.0 && !zero))
        return false;
    *out = zero ? 0.0 : value;
    return true;
}

bool cetta_decimal_real_from_double_v1(double value, char **out) {
    char candidate[64];
    if (!out)
        return false;
    *out = NULL;
    if (!isfinite(value))
        return false;
    (void)pthread_once(&dv_locale_once, dv_locale_init);
    if (!dv_locale)
        return false;
    for (int precision = 0; precision <= 17; precision++) {
        char *end = NULL;
        int length = snprintf(candidate, sizeof(candidate), "%.*e", precision, value);
        if (length <= 0 || (size_t)length >= sizeof(candidate))
            return false;
        if (strtod_l(candidate, &end, dv_locale) == value && end && *end == '\0')
            return cetta_decimal_real_canonical_v1(candidate, out);
    }
    return false;
}
