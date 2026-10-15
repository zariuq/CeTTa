#include "atom.h"
#include "parser.h"
#include "space.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { Atom *result; unsigned calls; bool accept; } Result;
static bool keep(Atom *atom, const Bindings *bindings, void *payload) {
    (void)bindings;
    Result *result = payload;
    result->calls++;
    result->result = atom;
    return result->accept;
}
static Atom *parse(Arena *arena, const char *source) {
    Atom **atoms;
    int count = parse_metta_text(source, arena, &atoms);
    assert(count == 1);
    Atom *result = atoms[0];
    free(atoms);
    return result;
}
int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    arena_init(&arena);
    Atom *equation = parse(&arena, "(= (same $x $x) (Pair $x $x))");
    Atom *query = parse(&arena, "(same A A)");
    Atom *different = parse(&arena, "(same A B)");
    Result result = {.accept = true};
    CettaCount count;
    assert(query_equation_visit_checked(equation, query, &arena, keep, &result,
        0u, &count) == CETTA_MATCH_INCOMPLETE && !count && !result.calls);
    assert(query_equation_visit_checked(equation, different, &arena, keep, &result,
        0u, &count) == CETTA_MATCH_INCOMPLETE && !count);
    assert(query_equation_visit_checked(equation, different, &arena, keep, &result,
        UINT64_MAX, &count) == CETTA_MATCH_REFUTED && !count);
    assert(query_equation_visit_checked(equation, query, &arena, keep, &result,
        UINT64_MAX, &count) == CETTA_MATCH_MATCHED && count == 1u && result.calls == 1u);
    assert(atom_eq(result.result, parse(&arena, "(Pair A A)")));
    result = (Result){.accept = false};
    assert(query_equation_visit_checked(equation, query, &arena, keep, &result,
        UINT64_MAX, &count) == CETTA_MATCH_INCOMPLETE && count == 1u);
    result = (Result){.accept = true};
    assert(query_equation_visit_checked(parse(&arena, "NotAnEquation"), query,
        &arena, keep, &result, UINT64_MAX, &count) == CETTA_MATCH_INCOMPLETE);
    assert(query_equation_visit_checked(equation, parse(&arena, "(else A A)"),
        &arena, keep, &result, UINT64_MAX, &count) == CETTA_MATCH_REFUTED);
    assert(query_equation_visit_checked(equation, parse(&arena, "(same A)"),
        &arena, keep, &result, UINT64_MAX, &count) == CETTA_MATCH_REFUTED);
    /* Original per-row API and checked traversal produce the same freshened
     * image; an actual mismatch is distinct from a refused finite budget. */
    Result old = {.accept = true};
    assert(query_equation_visit(equation, query, &arena, keep, &old) == 1u);
    assert(atom_eq(old.result, parse(&arena, "(Pair A A)")));
    arena_free(&arena);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: checked equation visit distinguishes witnessed match, rigid refutation and incomplete work");
    return 0;
}
