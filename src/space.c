#include "space.h"
#include <stdlib.h>
#include <string.h>

void space_init(Space *s) {
    s->atoms = NULL;
    s->len = 0;
    s->cap = 0;
}

void space_add(Space *s, Atom *atom) {
    if (s->len >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->atoms = realloc(s->atoms, sizeof(Atom *) * s->cap);
    }
    s->atoms[s->len++] = atom;
}

/* ── Query Results ──────────────────────────────────────────────────────── */

void query_results_init(QueryResults *qr) {
    qr->items = NULL;
    qr->len = 0;
    qr->cap = 0;
}

void query_results_push(QueryResults *qr, Atom *result, Bindings *b) {
    if (qr->len >= qr->cap) {
        qr->cap = qr->cap ? qr->cap * 2 : 8;
        qr->items = realloc(qr->items, sizeof(QueryResult) * qr->cap);
    }
    qr->items[qr->len].result = result;
    qr->items[qr->len].bindings = *b;
    qr->len++;
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

static bool is_equation(Atom *a, Atom **lhs_out, Atom **rhs_out) {
    if (a->kind != ATOM_EXPR) return false;
    if (a->expr.len != 3) return false;
    if (!atom_is_symbol(a->expr.elems[0], "=")) return false;
    *lhs_out = a->expr.elems[1];
    *rhs_out = a->expr.elems[2];
    return true;
}

/* ── Type Lookup ─────────────────────────────────────────────────────────── */

Atom *get_grounded_type(Arena *a, Atom *atom) {
    if (atom->kind != ATOM_GROUNDED) return atom_undefined_type(a);
    switch (atom->ground.gkind) {
    case GV_INT:    return atom_symbol(a, "Number");
    case GV_FLOAT:  return atom_symbol(a, "Number");
    case GV_BOOL:   return atom_symbol(a, "Bool");
    case GV_STRING: return atom_symbol(a, "String");
    }
    return atom_undefined_type(a);
}

/* Scan space for (: atom type) annotations */
static uint32_t get_annotated_types(Space *s, Arena *a, Atom *atom,
                                    Atom ***out_types) {
    Atom **types = NULL;
    uint32_t count = 0, cap = 0;
    for (uint32_t i = 0; i < s->len; i++) {
        Atom *sa = s->atoms[i];
        if (sa->kind != ATOM_EXPR || sa->expr.len != 3) continue;
        if (!atom_is_symbol(sa->expr.elems[0], ":")) continue;
        if (!atom_eq(sa->expr.elems[1], atom)) continue;
        /* Found (: atom type) — freshen type variables */
        if (count >= cap) {
            cap = cap ? cap * 2 : 4;
            types = realloc(types, sizeof(Atom *) * cap);
        }
        types[count++] = rename_vars(a, sa->expr.elems[2], fresh_var_suffix());
    }
    *out_types = types;
    return count;
}

uint32_t get_atom_types(Space *s, Arena *a, Atom *atom,
                        Atom ***out_types) {
    uint32_t count = 0;
    Atom **types = NULL;

    switch (atom->kind) {
    case ATOM_VAR:
        /* Variables have no type → %Undefined% */
        break;
    case ATOM_GROUNDED: {
        Atom *ty = get_grounded_type(a, atom);
        if (!atom_is_symbol(ty, "%Undefined%")) {
            types = malloc(sizeof(Atom *));
            types[0] = ty;
            count = 1;
        }
        break;
    }
    case ATOM_SYMBOL:
        count = get_annotated_types(s, a, atom, &types);
        break;
    case ATOM_EXPR:
        count = get_annotated_types(s, a, atom, &types);
        /* Also try to infer type from operator's function type */
        if (count == 0 && atom->expr.len >= 2) {
            Atom *op = atom->expr.elems[0];
            Atom **op_types = NULL;
            uint32_t nop = get_annotated_types(s, a, op, &op_types);
            for (uint32_t oi = 0; oi < nop; oi++) {
                Atom *ft = op_types[oi];
                /* Check if it's a function type (-> ...) with matching arity */
                if (ft->kind == ATOM_EXPR && ft->expr.len >= 3 &&
                    atom_is_symbol(ft->expr.elems[0], "->") &&
                    ft->expr.len - 2 == atom->expr.len - 1) {
                    /* Return type is the last element of (-> ... ret) */
                    Atom *ret_type = ft->expr.elems[ft->expr.len - 1];
                    /* Freshen type vars and try to unify args to get concrete ret type */
                    uint32_t tsuf = fresh_var_suffix();
                    Atom *fresh_ft = rename_vars(a, ft, tsuf);
                    Atom *fresh_ret = fresh_ft->expr.elems[fresh_ft->expr.len - 1];
                    Bindings tb;
                    bindings_init(&tb);
                    bool all_ok = true;
                    for (uint32_t ai = 0; ai < atom->expr.len - 1 && all_ok; ai++) {
                        /* Apply accumulated bindings to resolve type vars from earlier args */
                        Atom *arg_type_decl = bindings_apply(&tb, a, fresh_ft->expr.elems[ai + 1]);
                        Atom **atypes = NULL;
                        uint32_t nat = get_atom_types(s, a, atom->expr.elems[ai + 1], &atypes);
                        bool found = false;
                        for (uint32_t ti = 0; ti < nat; ti++) {
                            if (match_types(arg_type_decl, atypes[ti], &tb)) {
                                found = true;
                                break;
                            }
                        }
                        free(atypes);
                        if (!found) all_ok = false;
                    }
                    if (all_ok) {
                        /* Apply accumulated type bindings to return type */
                        Atom *concrete_ret = bindings_apply(&tb, a, fresh_ret);
                        if (count >= 1) {
                            types = realloc(types, sizeof(Atom *) * (count + 1));
                        } else {
                            types = malloc(sizeof(Atom *));
                        }
                        types[count++] = concrete_ret;
                    }
                    (void)ret_type;
                }
            }
            free(op_types);
        }
        break;
    }

    /* If no types found, return [%Undefined%] */
    if (count == 0) {
        types = malloc(sizeof(Atom *));
        types[0] = atom_undefined_type(a);
        count = 1;
    }
    *out_types = types;
    return count;
}

/* ── Equation Query ─────────────────────────────────────────────────────── */

void query_equations(Space *s, Atom *query, Arena *a, QueryResults *out) {
    for (uint32_t i = 0; i < s->len; i++) {
        Atom *lhs, *rhs;
        if (!is_equation(s->atoms[i], &lhs, &rhs)) continue;

        /* Standardization apart: rename equation variables to fresh names
           so they don't collide with query variables (à la Vampire/Prolog).
           HE spec metta.md lines 199-207: variables in different scopes
           are different even if they have the same name. */
        uint32_t suffix = fresh_var_suffix();
        Atom *rlhs = rename_vars(a, lhs, suffix);
        Atom *rrhs = rename_vars(a, rhs, suffix);

        Bindings b;
        bindings_init(&b);
        if (match_atoms(rlhs, query, &b)) {
            Atom *result = bindings_apply(&b, a, rrhs);
            query_results_push(out, result, &b);
        }
    }
}
