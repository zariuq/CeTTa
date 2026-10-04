#ifndef CETTA_EFFECT_POLICY_H
#define CETTA_EFFECT_POLICY_H

#include "atom.h"
#include "symbol.h"

/* The weak query lets standalone pure-kernel tests omit the evaluator. A
 * production evaluator supplies it from the active, host-owned session. */
extern bool eval_speculative_active(void) __attribute__((weak));
extern void eval_note_effect_denied(void) __attribute__((weak));

static inline bool cetta_speculative_active(void) {
    return eval_speculative_active && eval_speculative_active();
}

static inline bool cetta_speculative_op_allowed(SymbolId id) {
    return (symbol_flags(g_symbols,id) & CETTA_SYMBOL_FLAG_SPECULATIVE_OP)!=0;
}

static inline Atom *cetta_effect_denied(Arena *arena) {
    if (eval_note_effect_denied) eval_note_effect_denied();
    /* Do not echo the attempted arguments: they may contain private input. */
    return atom_error(arena,atom_symbol(arena,"speculative-evaluation"),
                      atom_symbol(arena,"EffectNotAllowed"));
}

#endif
