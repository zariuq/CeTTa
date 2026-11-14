/* rc_bytes.h - byte strings of a sort declared (rep SORT bytes NIL CONS BYTE NIBBLE): interned
 * byte strings (rc_intern.h) read through the module's spelling of a string as a list of bytes,
 * each byte two hexadecimal digits: NIL / CONS (BYTE hi lo) rest.  Generic: the constructor ids
 * are parameters (rc_bytes_init, called by the generated rc_components_init).
 *
 * Contract.
 *   - The view: a string of n > 0 bytes is CONS (BYTE hi lo) rest, where hi lo are the nibbles of
 *     its first byte and rest the string of its other bytes; the empty string is NIL.  Building
 *     CONS b s gives the string b followed by s's bytes.
 *   - rc_bytes_cmp(a, b): unsigned lexicographic order of the bytes, a proper prefix first; 0
 *     exactly when the strings are equal (then a == b, they are interned).
 *   - rc_bytes_snoc(a, s, n): the string a followed by the n bytes s.
 * Label: the order is proved equal to the nibble-by-nibble rules of a module's string order in
 * RuleModulePrimitives.lean (cmpStr_refines, nativeCmp_eq_memcmp) for modules with that order;
 * trusted: C memcmp compares unsigned bytes.
 */
#ifndef RC_BYTES_H
#define RC_BYTES_H

#include "rc_intern.h"

extern uint16_t rc_bytes_nil, rc_bytes_cons;
void rc_bytes_init(uint16_t nil, uint16_t cons, uint16_t byte, const uint16_t nib[16]);
T *rc_byte_term(unsigned v);                 /* BYTE hi lo, a static term */
unsigned rc_byte_value(T *b);                /* the byte of a BYTE hi lo term */
int rc_bytes_cmp(T *a, T *b);
T *rc_bytes_snoc(T *a, const uint8_t *s, size_t n);

static inline uint16_t RC_TAG_bytes(T *x) { return rc_ibytes_len(x) ? rc_bytes_cons : rc_bytes_nil; }
static inline T *RC_KID_bytes(T *x, int i) {
    return i == 0 ? rc_byte_term(rc_ibytes(x)[0]) : rc_intern_bytes(rc_ibytes(x) + 1, rc_ibytes_len(x) - 1);
}
#define RC_NUL_bytes(c) rc_intern_bytes(NULL, 0)
T *RC_MK2_bytes(uint16_t c, T *b, T *s);

#endif
