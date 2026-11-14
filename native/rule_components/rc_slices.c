/* rc_slices.c - byte strings as slices of the run's input (see rc_slices.h for the contract). */
#include "rc_slices.h"

#include <stdlib.h>
#include <string.h>

const uint8_t *rc_sl_buf;
size_t rc_sl_len;

T *rc_bytes_input(const uint8_t *b, size_t n) {
    if (n > RC_SL_MAX) return (T *)0;
    rc_sl_buf = b;
    rc_sl_len = n;
    return rc_sl_make(0, n);
}

T *rc_sl_cons_big(unsigned b, T *s) {
    size_t n = rc_sl_length(s);
    uint8_t small[256], *t = n + 1 <= sizeof small ? small : malloc(n + 1);
    if (!t) rc_refinement_fail("out of memory (a byte string)");
    t[0] = (uint8_t)b;
    if (n) memcpy(t + 1, rc_sl_data(s), n);
    T *r = rc_intern_bytes(t, n + 1);
    if (t != small) free(t);
    return r;
}

T *rc_sl_take_big(T *x, size_t n) { return rc_intern_bytes(rc_ibytes(x), n); }
T *rc_sl_drop_big(T *x, size_t n) { return rc_intern_bytes(rc_ibytes(x) + n, rc_ibytes_len(x) - n); }

bool rc_sl_equal(T *x, T *y) {
    size_t n = rc_sl_length(x);
    return n == rc_sl_length(y) && (n == 0 || !memcmp(rc_sl_data(x), rc_sl_data(y), n));
}
