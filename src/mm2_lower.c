#include "mm2_lower.h"

#include "symbol.h"

typedef enum {
    MM2_CTX_GENERAL = 0,
    MM2_CTX_PATTERN,
    MM2_CTX_TEMPLATE
} Mm2LowerContext;

typedef struct {
    SymbolId surf_exec;
    SymbolId surf_btm;
    SymbolId surf_act;
    SymbolId surf_sink_seq;
    SymbolId surf_neq;
    SymbolId surf_z3;

    SymbolId ir_exec;
    SymbolId ir_pattern_and;
    SymbolId ir_pattern_btm;
    SymbolId ir_pattern_act;
    SymbolId ir_guard_eq;
    SymbolId ir_guard_neq;
    SymbolId ir_sink_seq;
    SymbolId ir_sink_add;
    SymbolId ir_sink_remove;
    SymbolId ir_sink_act;
    SymbolId ir_sink_z3;
} Mm2LowerSyms;

static Mm2LowerSyms mm2_lower_syms(void) {
    Mm2LowerSyms syms;
    syms.surf_exec = symbol_intern_cstr(g_symbols, "exec");
    syms.surf_btm = symbol_intern_cstr(g_symbols, "BTM");
    syms.surf_act = symbol_intern_cstr(g_symbols, "ACT");
    syms.surf_sink_seq = symbol_intern_cstr(g_symbols, "O");
    syms.surf_neq = symbol_intern_cstr(g_symbols, "!=");
    syms.surf_z3 = symbol_intern_cstr(g_symbols, "z3");

    syms.ir_exec = symbol_intern_cstr(g_symbols, "mm2_exec");
    syms.ir_pattern_and = symbol_intern_cstr(g_symbols, "mm2_pattern_and");
    syms.ir_pattern_btm = symbol_intern_cstr(g_symbols, "mm2_pattern_btm");
    syms.ir_pattern_act = symbol_intern_cstr(g_symbols, "mm2_pattern_act");
    syms.ir_guard_eq = symbol_intern_cstr(g_symbols, "mm2_guard_eq");
    syms.ir_guard_neq = symbol_intern_cstr(g_symbols, "mm2_guard_neq");
    syms.ir_sink_seq = symbol_intern_cstr(g_symbols, "mm2_sink_seq");
    syms.ir_sink_add = symbol_intern_cstr(g_symbols, "mm2_sink_add");
    syms.ir_sink_remove = symbol_intern_cstr(g_symbols, "mm2_sink_remove");
    syms.ir_sink_act = symbol_intern_cstr(g_symbols, "mm2_sink_act");
    syms.ir_sink_z3 = symbol_intern_cstr(g_symbols, "mm2_sink_z3");
    return syms;
}

bool cetta_mm2_atom_is_exec_rule(Atom *atom) {
    Mm2LowerSyms syms = mm2_lower_syms();
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return false;
    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL) return false;
    return atom_is_symbol_id(head, syms.surf_exec) ||
           atom_is_symbol_id(head, syms.ir_exec);
}

bool cetta_mm2_atoms_have_top_level_eval(Atom **atoms, int n) {
    if (!atoms || n <= 0) return false;
    for (int i = 0; i < n; i++) {
        if (atom_is_symbol_id(atoms[i], g_builtin_syms.bang)) {
            return true;
        }
    }
    return false;
}

static Atom *mm2_raise_atom_impl(Arena *a, Atom *atom, const Mm2LowerSyms *syms);

static Atom *mm2_raise_expr_with_head(Arena *a, Atom *atom, SymbolId head_id,
                                      const Mm2LowerSyms *syms) {
    bool changed = false;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = atom_symbol_id(a, head_id);
    if (elems[0] != atom->expr.elems[0]) changed = true;
    for (uint32_t i = 1; i < atom->expr.len; i++) {
        elems[i] = mm2_raise_atom_impl(a, atom->expr.elems[i], syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_raise_plain_expr(Arena *a, Atom *atom, const Mm2LowerSyms *syms) {
    bool changed = false;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        elems[i] = mm2_raise_atom_impl(a, atom->expr.elems[i], syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_raise_atom_impl(Arena *a, Atom *atom, const Mm2LowerSyms *syms) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return atom;

    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL) {
        return mm2_raise_plain_expr(a, atom, syms);
    }
    if (atom_is_symbol_id(head, g_builtin_syms.quote)) {
        return atom;
    }

    if (atom_is_symbol_id(head, syms->ir_exec)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_exec, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_and)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.comma, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_btm)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_btm, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_act) ||
        atom_is_symbol_id(head, syms->ir_sink_act)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_act, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_eq)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.op_eq, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_neq)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_neq, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_seq)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_sink_seq, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_add)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.op_plus, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_remove)) {
        return mm2_raise_expr_with_head(a, atom, g_builtin_syms.op_minus, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_z3)) {
        return mm2_raise_expr_with_head(a, atom, syms->surf_z3, syms);
    }

    return mm2_raise_plain_expr(a, atom, syms);
}

static Atom *mm2_lower_atom(Arena *a, Atom *atom, Mm2LowerContext ctx,
                            const Mm2LowerSyms *syms);

static Atom *mm2_lower_expr_with_head(Arena *a, Atom *atom, Atom *new_head,
                                      uint32_t start_idx,
                                      Mm2LowerContext child_ctx,
                                      const Mm2LowerSyms *syms) {
    bool changed = (new_head != atom->expr.elems[0]);
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = new_head;
    for (uint32_t i = 1; i < atom->expr.len; i++) {
        Mm2LowerContext next_ctx = (i >= start_idx) ? child_ctx : MM2_CTX_GENERAL;
        elems[i] = mm2_lower_atom(a, atom->expr.elems[i], next_ctx, syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_lower_expr_mixed(Arena *a, Atom *atom, Atom *new_head,
                                  const Mm2LowerContext *child_ctxs,
                                  const Mm2LowerSyms *syms) {
    bool changed = (new_head != atom->expr.elems[0]);
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    elems[0] = new_head;
    for (uint32_t i = 1; i < atom->expr.len; i++) {
        elems[i] = mm2_lower_atom(a, atom->expr.elems[i], child_ctxs[i - 1], syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_lower_plain_expr(Arena *a, Atom *atom, const Mm2LowerSyms *syms) {
    bool changed = false;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * atom->expr.len);
    for (uint32_t i = 0; i < atom->expr.len; i++) {
        Mm2LowerContext next_ctx = (i == 0) ? MM2_CTX_GENERAL : MM2_CTX_GENERAL;
        elems[i] = mm2_lower_atom(a, atom->expr.elems[i], next_ctx, syms);
        if (elems[i] != atom->expr.elems[i]) changed = true;
    }
    if (!changed) return atom;
    return atom_expr_shared(a, elems, atom->expr.len);
}

static Atom *mm2_lower_atom(Arena *a, Atom *atom, Mm2LowerContext ctx,
                            const Mm2LowerSyms *syms) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len == 0) return atom;

    Atom *head = atom->expr.elems[0];
    if (head->kind != ATOM_SYMBOL) {
        return mm2_lower_plain_expr(a, atom, syms);
    }
    if (atom_is_symbol_id(head, g_builtin_syms.quote)) {
        return atom;
    }

    if (atom_is_symbol_id(head, syms->surf_exec) && atom->expr.len == 4) {
        Mm2LowerContext child_ctxs[3] = {
            MM2_CTX_GENERAL, MM2_CTX_PATTERN, MM2_CTX_TEMPLATE
        };
        return mm2_lower_expr_mixed(a, atom, atom_symbol_id(a, syms->ir_exec),
                                    child_ctxs, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_exec) && atom->expr.len == 4) {
        Mm2LowerContext child_ctxs[3] = {
            MM2_CTX_GENERAL, MM2_CTX_PATTERN, MM2_CTX_TEMPLATE
        };
        return mm2_lower_expr_mixed(a, atom, head, child_ctxs, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.comma) &&
        atom->expr.len >= 3 &&
        (ctx == MM2_CTX_PATTERN || ctx == MM2_CTX_GENERAL)) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_pattern_and),
                                        1, MM2_CTX_PATTERN, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_and) && atom->expr.len >= 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_PATTERN, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_btm) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_pattern_btm),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_pattern_btm) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_act) && atom->expr.len == 3) {
        SymbolId target = (ctx == MM2_CTX_TEMPLATE) ? syms->ir_sink_act : syms->ir_pattern_act;
        return mm2_lower_expr_with_head(a, atom, atom_symbol_id(a, target),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if ((atom_is_symbol_id(head, syms->ir_pattern_act) ||
         atom_is_symbol_id(head, syms->ir_sink_act)) &&
        atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.op_eq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_guard_eq),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_eq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_neq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_guard_neq),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_guard_neq) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_sink_seq) &&
        atom->expr.len >= 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_seq),
                                        1, MM2_CTX_TEMPLATE, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_seq) && atom->expr.len >= 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_TEMPLATE, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.op_plus) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_add),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_add) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, g_builtin_syms.op_minus) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_remove),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_remove) && atom->expr.len == 2) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    if (atom_is_symbol_id(head, syms->surf_z3) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom,
                                        atom_symbol_id(a, syms->ir_sink_z3),
                                        1, MM2_CTX_GENERAL, syms);
    }
    if (atom_is_symbol_id(head, syms->ir_sink_z3) && atom->expr.len == 3) {
        return mm2_lower_expr_with_head(a, atom, head, 1, MM2_CTX_GENERAL, syms);
    }

    return mm2_lower_plain_expr(a, atom, syms);
}

void cetta_mm2_lower_atoms(Arena *a, Atom **atoms, int n) {
    if (!a || !atoms || n <= 0) return;
    Mm2LowerSyms syms = mm2_lower_syms();
    for (int i = 0; i < n; i++) {
        atoms[i] = mm2_lower_atom(a, atoms[i], MM2_CTX_GENERAL, &syms);
    }
}

Atom *cetta_mm2_raise_atom(Arena *a, Atom *atom) {
    if (!a || !atom) return atom;
    Mm2LowerSyms syms = mm2_lower_syms();
    return mm2_raise_atom_impl(a, atom, &syms);
}

char *cetta_mm2_atom_to_surface_string(Arena *a, Atom *atom) {
    Atom *raised = cetta_mm2_raise_atom(a, atom);
    return atom_to_string(a, raised);
}
