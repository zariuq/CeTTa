/* rc_slices.h - byte strings for a sort declared (rep SORT slices NIL CONS), whose CONS holds a byte of
 * a sort declared (rep BYTE word CTOR BIT0 BIT1) with eight bits.  A value is either a slice of the
 * run's input (an immediate: start << 31 | end, both below 2^31, of the buffer given to
 * rc_bytes_input) or an interned byte string (rc_intern.h).  Reading the input byte by byte moves a
 * slice; nothing is copied.
 *
 * Contract.
 *   - The view: a value whose content is empty is NIL; otherwise CONS (first byte) (the rest).  The
 *     rest of a slice is a slice; the rest of an interned string is the interned string of its
 *     remaining bytes.
 *   - Building CONS b s gives a value whose content is b followed by the content of s: the slice one
 *     byte longer when s is a slice that the input's byte b precedes, else an interned string.  NIL
 *     is the empty slice.
 *   - Values of the sort are equal as module data exactly when their contents are equal (two values
 *     of equal content may differ as words), so the sort is never compared by identity: rulec refuses
 *     it in an (equal) block, a memo key and an interned node.
 *   - A slice is valid while the input it was cut from is the run's input: a value of the sort never
 *     outlives the call that read the input (a host keeps no handle on it).
 *   - rc_sl_take(x, n) and rc_sl_drop(x, n), for n at most the length: the first n bytes, and the
 *     bytes after them (slices of a slice).
 * Label: the blocks over these values are proved equal to the rules that define them in the Lean
 * files of the module that binds them; trusted: this C keeps the contents as stated.
 */
#ifndef RC_SLICES_H
#define RC_SLICES_H

#include "rc_intern.h"

extern const uint8_t *rc_sl_buf;
extern size_t rc_sl_len;

#define RC_SL_MAX ((size_t)0x7fffffff)
/* the run's input: every slice is cut from it; NULL when the input is 2^31 bytes or longer */
T *rc_bytes_input(const uint8_t *b, size_t n);

static inline size_t rc_sl_start(T *x) { return (size_t)(RC_IMM_VAL(x) >> 31); }
static inline size_t rc_sl_end(T *x) { return (size_t)(RC_IMM_VAL(x) & RC_SL_MAX); }
static inline T *rc_sl_make(size_t start, size_t end) { return RC_IMM(((uint64_t)start << 31) | (uint64_t)end); }
static inline size_t rc_sl_length(T *x) { return RC_IS_IMM(x) ? rc_sl_end(x) - rc_sl_start(x) : rc_ibytes_len(x); }
static inline const uint8_t *rc_sl_data(T *x) { return RC_IS_IMM(x) ? rc_sl_buf + rc_sl_start(x) : rc_ibytes(x); }
static inline bool rc_sl_empty(T *x) { return rc_sl_length(x) == 0; }
static inline unsigned rc_sl_first(T *x) { return rc_sl_data(x)[0]; }
static inline T *rc_sl_rest(T *x) {
    if (RC_IS_IMM(x)) return rc_sl_make(rc_sl_start(x) + 1, rc_sl_end(x));
    return rc_intern_bytes(rc_ibytes(x) + 1, rc_ibytes_len(x) - 1);
}
T *rc_sl_cons_big(unsigned b, T *s);
T *rc_sl_take_big(T *x, size_t n);
T *rc_sl_drop_big(T *x, size_t n);
bool rc_sl_equal(T *x, T *y);
/* the slice cases inline, the interned ones in rc_slices.c */
static inline T *rc_sl_cons(unsigned b, T *s) {
    if (RC_IS_IMM(s)) {
        size_t st = rc_sl_start(s);
        if (st > 0 && rc_sl_buf[st - 1] == (uint8_t)b) return rc_sl_make(st - 1, rc_sl_end(s));
    }
    return rc_sl_cons_big(b, s);
}
static inline T *rc_sl_take(T *x, size_t n) { return RC_IS_IMM(x) ? rc_sl_make(rc_sl_start(x), rc_sl_start(x) + n) : rc_sl_take_big(x, n); }
static inline T *rc_sl_drop(T *x, size_t n) { return RC_IS_IMM(x) ? rc_sl_make(rc_sl_start(x) + n, rc_sl_end(x)) : rc_sl_drop_big(x, n); }

#endif
