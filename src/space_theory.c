#include "space_theory.h"

bool space_theory_atomic_clock_guard_recheck(
        SpaceTheoryAtomicClockGuard *guard) {
    if (!guard)
        return false;
    uint64_t clock = space_global_program_epoch();
    if (!space_theory_token_is_current(guard->token))
        return false;
    atomic_store_explicit(&guard->program_clock, clock, memory_order_relaxed);
    return true;
}

SpaceTheoryRowKind space_theory_classify_row(
        const Atom *row, SymbolId *head_out, bool *compound_subject_out) {
    if (head_out)
        *head_out = SYMBOL_ID_NONE;
    if (compound_subject_out)
        *compound_subject_out = false;
    if (!row || row->kind != ATOM_EXPR || row->expr.len < 3u ||
        !row->expr.elems[0] || row->expr.elems[0]->kind != ATOM_SYMBOL)
        return SPACE_THEORY_ROW_DATA;

    SymbolId form = row->expr.elems[0]->sym_id;
    if (space_theory_is_alias_head(form))
        return SPACE_THEORY_ROW_EQUATIONAL_ALIAS;
    if (row->expr.len != 3u)
        return SPACE_THEORY_ROW_DATA;
    const Atom *subject = row->expr.elems[1];
    if (form == g_builtin_syms.equals || form == g_builtin_syms.equals_percent) {
        if (head_out && subject && subject->kind == ATOM_EXPR &&
            subject->expr.len > 0u && subject->expr.elems[0] &&
            subject->expr.elems[0]->kind == ATOM_SYMBOL) {
            *head_out = subject->expr.elems[0]->sym_id;
        }
        return form == g_builtin_syms.equals
            ? SPACE_THEORY_ROW_EQUATION
            : SPACE_THEORY_ROW_DIRECTIONAL_RULE;
    }
    if (form == g_builtin_syms.colon) {
        if (subject) {
            if (subject->kind == ATOM_SYMBOL) {
                if (head_out)
                    *head_out = subject->sym_id;
            } else if (subject->kind == ATOM_EXPR) {
                if (compound_subject_out)
                    *compound_subject_out = true;
                if (head_out && subject->expr.len > 0u &&
                    subject->expr.elems[0] &&
                    subject->expr.elems[0]->kind == ATOM_SYMBOL) {
                    *head_out = subject->expr.elems[0]->sym_id;
                }
            }
        }
        return SPACE_THEORY_ROW_TYPE_DECLARATION;
    }
    if (form == g_builtin_syms.subtype)
        return SPACE_THEORY_ROW_SUBTYPE_DECLARATION;
    return SPACE_THEORY_ROW_DATA;
}

bool space_theory_overlay_chain_tokens_current(
        const Space *space, const SpaceTheoryToken *views, size_t count) {
    if (!views || count == 0u)
        return false;
    for (size_t i = 0u; i < count; i++) {
        if (!space)
            return false;
        SpaceTheoryToken old = views[i];
        if (old.space != space ||
            old.instance_id != space_instance_id(space) ||
            old.equation_revision != space_equation_revision(space) ||
            old.declaration_revision != space_declaration_revision(space)) {
            return false;
        }
        space = space->overlay_base;
    }
    return !space;
}
