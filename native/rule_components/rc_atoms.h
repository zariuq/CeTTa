/* rc_atoms.h - MeTTa atoms as module data: the "atoms" representation of a sort (rulec's
 * (rep SORT atoms INT STRING SYMBOL EXPRESSION)).
 *
 * A value of an atoms sort is
 *   - an application of one of the sort's constructors: a heap term tagged with the constructor (a
 *     nullary constructor: its static header);
 *   - an integer: an immediate when it lies in [-2^62, 2^62), else a byte string of its eight bytes
 *     (little-endian two's complement) tagged INT; one spelling per integer, so equal integers are
 *     equal values;
 *   - a string: a byte string tagged STRING;
 *   - a symbol that is no constructor of the sort: a byte string of its name tagged SYMBOL;
 *   - an expression that is no application of a constructor of the sort: a heap term tagged
 *     EXPRESSION whose children are all its elements, the head first.
 * A literal of the rules (a nullary constructor named by a decimal integer or a quoted string) is read
 * through the sort's view as that constructor and built as the integer or string it names.
 *
 * The run's source (rulec's (atoms-source SORT CONS CP NIL)): bytes the host sets with
 * rc_atoms_set_source, read as the list (CONS (CP n) ...) ending in NIL of their Unicode scalars
 * (UTF-8; a byte that is not UTF-8 read as U+DC80..U+DCFF, so every list reads back to its bytes).  A
 * cursor is a nonempty suffix of it: a word (position << 3 | 2) the collector neither moves nor scans;
 * the empty suffix is NIL itself.  The views read a cursor as its first cell, (CONS (CP n) rest), the
 * scalar and the rest decoded when they are read; equality reads it as the list it stands for.
 * Equality is structural (rc_atoms_equal); two values are equal exactly when they are the same atom.
 * Label: trusted (the representation is one to one on atoms; the views read it back). */
#ifndef RC_ATOMS_H
#define RC_ATOMS_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define RC_ATOMS_IMM(v) ((T *)(uintptr_t)(((uint64_t)(int64_t)(v) << 1) | 1u))
#define RC_ATOMS_INT(x) ((int64_t)((intptr_t)(x)) >> 1)
static inline bool rc_atoms_fits(int64_t v) { return v >= -(INT64_C(1) << 62) && v < (INT64_C(1) << 62); }

/* the integer V; INTC is the sort's INT constructor */
T *rc_atoms_int(uint16_t intc, int64_t v);
/* whether X is an integer, and its value */
static inline bool rc_atoms_int_of(const T *x, uint16_t intc, int64_t *v) {
    if (RC_IS_IMM(x)) { *v = RC_ATOMS_INT(x); return true; }
    if (!RC_IS_HEAP(x) || !(x->flags & RC_FLAG_BYTES) || x->tag != intc || x->n != 8) return false;
    uint64_t u = 0;
    for (int i = 7; i >= 0; i--) u = (u << 8) | rc_bytes(x)[i];
    *v = (int64_t)u;
    return true;
}
/* a byte string tagged TAG (STRING or SYMBOL) */
T *rc_atoms_bytes(uint16_t tag, const uint8_t *b, size_t n);
/* equality as atoms */
bool rc_atoms_equal(const T *a, const T *b);

/* ---- the run's source ---- */
typedef struct { const uint8_t *bytes; size_t len; } RcAtomsSource;
extern RC_HEAP_LOCAL RcAtomsSource rc_atoms_source;
/* the module's CONS, CP and NIL (set by its rc_components_init) */
void rc_atoms_source_ctors(uint16_t cons, uint16_t cp, uint16_t nil);
static inline void rc_atoms_set_source(const uint8_t *bytes, size_t len) { rc_atoms_source = (RcAtomsSource){bytes, len}; }
static inline bool rc_atoms_is_cursor(const T *x) { return ((uintptr_t)x & 7u) == 2u; }
static inline size_t rc_atoms_cursor_pos(const T *x) { return (size_t)((uintptr_t)x >> 3); }
/* the suffix of the source from byte POS (a scalar's start): a cursor, or NIL at the end */
T *rc_atoms_cursor(size_t pos);
/* a cursor's first scalar, as (CP n), and the rest */
T *rc_atoms_cursor_head(const T *x);
T *rc_atoms_cursor_tail(const T *x);
/* the scalar at byte POS of the source (POS < len) and the bytes it takes */
size_t rc_atoms_scalar_at(size_t pos, uint32_t *scalar);
#endif
