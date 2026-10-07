#include "rule_binding.h"
#include "term_canon.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <stdio.h>

static Atom *call(Arena *arena, Atom *argument) {
    return atom_expr2(arena, atom_symbol(arena, "f"), argument);
}

static CettaRuleDescriptor rule(Arena *arena, const char *marker,
                               Atom *head, Atom *body) {
    Atom *source = atom_expr3(arena, atom_symbol(arena, marker), head, body);
    CettaRuleDescriptor descriptor;
    assert(cetta_rule_view(source, CETTA_RULE_SYNTAX_ORDINARY |
                                   CETTA_RULE_SYNTAX_DIRECTIONAL, &descriptor));
    return descriptor;
}

static void admission(Arena *arena) {
    Atom *head = call(arena, atom_symbol(arena, "a"));
    Atom *body = atom_symbol(arena, "body");
    CettaRuleDescriptor descriptor = rule(arena, "=%", head, body);
    assert(descriptor.binding == CETTA_RULE_BIND_HEAD);
    assert(!cetta_rule_view(descriptor.source, CETTA_RULE_SYNTAX_ORDINARY, &descriptor));
    assert(descriptor.binding == CETTA_RULE_BIND_HEAD && descriptor.body == body);
    Atom *reverse = atom_expr3(arena, atom_symbol(arena, "%="), head, body);
    assert(!cetta_rule_view(reverse, ~0u, &descriptor));
    Atom *quoted = atom_expr2(arena, atom_symbol(arena, "quote"), descriptor.source);
    assert(!cetta_rule_view(quoted, ~0u, &descriptor));
    Atom *extra = atom_expr2(arena, descriptor.source, body);
    assert(!cetta_rule_view(extra, ~0u, &descriptor));
}

static void directional_and_ordinary(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    Atom *a = atom_symbol(arena, "a"), *q = atom_var(arena, "q");
    Atom *sentinel = atom_symbol(arena, "unchanged");
    Atom *body = sentinel;
    Bindings environment;
    bindings_init(&environment);
    Atom *saved = atom_var(arena, "saved");
    assert(bindings_add_var(&environment, saved, sentinel));
    CettaRuleDescriptor descriptor = rule(arena, "=%", call(arena, a), a);
    assert(cetta_rule_prepare(&descriptor, call(arena, q),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &body) == CETTA_TERM_MATCH_MISMATCH);
    assert(body == sentinel);
    assert(bindings_apply(&environment, arena, q)->var_id == q->var_id);
    assert(bindings_apply(&environment, arena, saved) == sentinel);
    descriptor = rule(arena, "=", call(arena, a), a);
    assert(cetta_rule_prepare(&descriptor, call(arena, q),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &body) == CETTA_TERM_MATCH_OK);
    assert(atom_eq(body, a) && atom_eq(bindings_apply(&environment, arena, q), a));
    /* Existing caller bindings are resolved before protection is chosen. */
    descriptor.binding = CETTA_RULE_BIND_HEAD;
    assert(cetta_rule_prepare(&descriptor, call(arena, q),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &body) == CETTA_TERM_MATCH_OK);
    assert(atom_eq(body, a));
    bindings_free(&environment);
}

static void aliases_and_body(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    Atom *x = atom_var(arena, "x");
    Atom *u = atom_var(arena, "same"), *v = atom_var(arena, "same");
    Atom *head = call(arena, atom_expr2(arena, x, x));
    Atom *payload = atom_expr3(arena, atom_symbol(arena, "unify"), x, atom_int(arena, 7));
    CettaRuleDescriptor descriptor = rule(arena, "=%", head, payload);
    Bindings environment;
    bindings_init(&environment);
    Atom *body = NULL;
    assert(cetta_rule_prepare(&descriptor, call(arena, atom_expr2(arena, u, v)),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &body) == CETTA_TERM_MATCH_MISMATCH);
    assert(!body && !bindings_has_bound_values(&environment));
    assert(cetta_rule_prepare(&descriptor, call(arena, atom_expr2(arena, u, u)),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &body) == CETTA_TERM_MATCH_OK);
    assert(body->expr.elems[1]->var_id == u->var_id);
    assert(bindings_apply(&environment, arena, u)->var_id == u->var_id);
    /* Head preparation only constructs the body. Executing ordinary body
     * unification afterwards may deliberately bind the caller. */
    assert(match_atoms(body->expr.elems[1], body->expr.elems[2], &environment, arena));
    assert(atom_eq(bindings_apply(&environment, arena, u), atom_int(arena, 7)));
    assert(bindings_apply(&environment, arena, v)->var_id == v->var_id);
    bindings_free(&environment);
}

static void activation_identity(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    Atom *x = atom_var(arena, "x"), *private = atom_var(arena, "private");
    Atom *shared = atom_expr2(arena, x, private);
    CettaRuleDescriptor descriptor = rule(arena, "=%", call(arena, x),
                                          atom_expr2(arena, shared, shared));
    Atom *first = NULL, *second = NULL;
    Bindings environment;
    bindings_init(&environment);
    /* Even a caller carrying the author's original identity is opened apart. */
    assert(cetta_rule_prepare(&descriptor, call(arena, x),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &first) == CETTA_TERM_MATCH_OK);
    assert(cetta_rule_prepare(&descriptor, call(arena, x),
        cetta_frame_identity_scope_fresh(&frames), arena, &environment,
        atom_eq, &second) == CETTA_TERM_MATCH_OK);
    assert(first->expr.elems[0] == first->expr.elems[1]);
    assert(second->expr.elems[0] == second->expr.elems[1]);
    assert(first->expr.elems[0]->expr.elems[0]->var_id == x->var_id);
    assert(first->expr.elems[0]->expr.elems[1]->var_id != private->var_id);
    assert(first->expr.elems[0]->expr.elems[1]->var_id !=
           second->expr.elems[0]->expr.elems[1]->var_id);
    Atom *old = second;
    assert(cetta_rule_prepare(&descriptor, call(arena, x), 0u, arena,
        &environment, atom_eq, &second) == CETTA_TERM_MATCH_INVALID);
    assert(second == old);
    bindings_free(&environment);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
    admission(&arena);
    directional_and_ordinary(&arena);
    aliases_and_body(&arena);
    activation_identity(&arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: rule admission, head policy, rollback, body freedom and activation identities");
    return 0;
}
