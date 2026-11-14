/* rc_table.h - id tables as arrays updated in place, for a sort declared
 * (rep SORT array-table TABLE LEAF NODE NONE).
 *
 * Contract.  The module's tables are TABLE next trie; its rules for get, put (store at next, then
 * next + 1), set, next and the empty table are the trie recursions on binary numerals.  The array
 * refines them through the read-back (the trie whose nodes are the union of the digit paths of the
 * ids stored at least once; a stored id's own node holds its cell, every other node NONE):
 *   rc_table_get(t, n)     the cell stored at n, NONE where none was;
 *   rc_table_put(t, c)     stores c at next, next + 1;
 *   rc_table_set(t, n, c)  stores c at n;
 *   rc_table_next(t)       next;
 *   rc_table_empty()       the empty table (no array until its first store).
 * A table value names an array and its version; only the current version is accepted: an
 * operation on a value used before (an older version) stops the run, never a silent difference.
 * The cells are roots of the copying collector (rc_table_roots via rc_extra_roots_hook).
 * Label: proved for the model (RuleModuleInterning.lean: tableGet_readback, tablePut_readback,
 * tableSet_readback, tableNext_readback, emptyTable_readback) for modules whose table rules are
 * these recursions; trusted: this array code is the model NTable; checked at run time: no table
 * value is used after its update.
 */
#ifndef RC_TABLE_H
#define RC_TABLE_H

#include "../rule_runtime/rc_rt.h"

void rc_table_init(uint16_t table, uint16_t leaf, uint16_t node, uint16_t none);
uint16_t rc_table_ctor(void);
T *rc_table_get(T *tb, T *n);
T *rc_table_put(T *tb, T *c);
T *rc_table_set(T *tb, T *n, T *c);
T *rc_table_next(T *tb);
T *rc_table_empty(void);
void rc_table_reset(void);                              /* frees every array (a run's end) */
void rc_table_roots(int major, T *(*evac)(T *x));       /* rc_extra_roots_hook */
void rc_table_stats(uint64_t *arrays, uint64_t *cells);

/* the views: a table is an array (an immediate) or, as rules build it, module data */
T *rc_table_kid(T *x, int i);
T *rc_table_readback(T *x);
static inline uint16_t RC_TAG_table(T *x) { (void)x; return rc_table_ctor(); }
static inline T *RC_KID_table(T *x, int i) { return RC_IS_HEAP(x) ? x->k[i] : rc_table_kid(x, i); }
static inline T *RC_MK2_table(uint16_t c, T *next, T *trie) { return rc_mk2(c, next, trie); }
#define RC_READBACK_table(x) rc_table_readback(x)

#endif
