/* rc_memo.h - memoised judgments: the result of a judgment on given arguments, kept and reused.
 *
 * Contract.  rulec emits, for a judgment declared (memo NAME JUDGMENT ...), a lookup before its
 * compiled rules and an insertion after them.  Sound because a judgment is a function of its
 * arguments (its rules are deterministic: det or semidet, and a failure is kept too) and because
 * the arguments are values of interned sorts, immediates or static constants, compared by
 * identity: equal arguments are the same words (rc_intern.h).  rulec refuses a memo row whose
 * argument or result sort is not of that kind.  The tables are caches: two generations of at most
 * RC_MEMO_MAX entries each (when the current one is full the older is dropped; a hit in the older is
 * kept), cleared before every collection of the interning store and when it is freed.
 * Label: proved for the model (RuleModuleComponents.lean: memo_sound, memo2_sound for the two
 * generations); trusted: this C is the model, the tables are cleared before every collection, and
 * the front cache holds only pairs the tables answered or were given;
 * checked in check mode: every hit compared with the rules' result on the same arguments.
 */
#ifndef RC_MEMO_H
#define RC_MEMO_H

#include "../rule_runtime/rc_rt.h"

#define RC_MEMO_ARGS 4
#define RC_MEMO_MAX ((size_t)1 << 20)
/* A direct-mapped front cache of RC_MEMO_FRONT entries per memoised judgment, kept by the judgment's
 * generated wrapper: an entry holds arguments and the result the tables gave (or the rules gave and
 * the tables were given) for them, and is valid while rc_memo_gen is the generation it was made in;
 * rc_memo_clear starts a new generation, so no entry outlives the tables' contents. */
#ifndef RC_MEMO_FRONT_BITS
#define RC_MEMO_FRONT_BITS 8
#endif
#define RC_MEMO_FRONT ((size_t)1 << RC_MEMO_FRONT_BITS)
extern uint64_t rc_memo_gen;
static inline size_t rc_memo_front_slot(int n, T *const *a) {
    uint64_t h = 0x9e3779b97f4a7c15ull;
    for (int i = 0; i < n; i++) { h ^= (uint64_t)(uintptr_t)a[i]; h *= 0xff51afd7ed558ccdull; }
    return (size_t)(h >> (64 - RC_MEMO_FRONT_BITS));
}
bool rc_memo_get(uint16_t j, int n, T *const *args, T **out);
void rc_memo_put(uint16_t j, int n, T *const *args, T *result);
void rc_memo_clear(void);
typedef struct { uint64_t hits, misses, clears; size_t entries; } RcMemoStats;
RcMemoStats rc_memo_stats(void);

#endif
