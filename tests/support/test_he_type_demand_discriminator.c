#include "atom.h"
#include "he_type_policy.h"
#include "parser.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* The references retain the pre-change constructor and decision rules. */
static Atom *reference_metatype(Arena *arena, Atom *atom) {
    switch (atom->kind) {
    case ATOM_SYMBOL: return atom_symbol_type(arena);
    case ATOM_VAR: return atom_variable_type(arena);
    case ATOM_GROUNDED: return atom_grounded_type(arena);
    case ATOM_EXPR:
        return atom_is_list(atom) || atom_is_list_rest(atom)
            ? atom_list_type(arena) : atom_expression_type(arena);
    }
    return atom_undefined_type(arena);
}

static HeTypeDemand reference_demand(const Atom *subject, Atom *expected,
                                     Atom *metatype) {
    if (atom_is_symbol_id(expected, g_builtin_syms.atom) ||
        atom_eq(expected, metatype) ||
        (atom_is_symbol_id(expected, g_builtin_syms.expression) &&
         atom_is_list(subject)) ||
        atom_is_symbol_id(metatype, g_builtin_syms.variable))
        return HE_TYPE_KEEP;
    if (subject->kind == ATOM_SYMBOL || subject->kind == ATOM_GROUNDED ||
        (subject->kind == ATOM_EXPR && subject->expr.len == 0u) ||
        atom_is_list_form(subject))
        return HE_TYPE_CAST;
    return HE_TYPE_INTERPRET;
}

static Atom *parse_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    assert(count == 1 && forms);
    Atom *atom = forms[0];
    free(forms);
    return atom;
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
    arena_init(&arena);
    Atom *elements[] = {atom_int(&arena, 7), atom_symbol(&arena, "held")};
    Atom *subjects[] = {
        atom_symbol(&arena, "source"), atom_var(&arena, "$x"),
        atom_int(&arena, 6), atom_float(&arena, 1.5),
        atom_bool(&arena, true), atom_string(&arena, "value"),
        atom_expr(&arena, NULL, 0u), parse_one(&arena, "(box $x 6)"),
        atom_list(&arena, NULL, 0u), atom_list(&arena, elements, 2u),
        atom_list_with_rest(&arena, elements, 2u, atom_var(&arena, "$tail")),
    };
    Atom *expected[] = {
        atom_atom_type(&arena), atom_symbol_type(&arena),
        atom_variable_type(&arena), atom_grounded_type(&arena),
        atom_expression_type(&arena), atom_list_type(&arena),
        atom_undefined_type(&arena), atom_symbol(&arena, "Number"),
        atom_symbol(&arena, "SpaceType"), atom_var(&arena, "$type"),
        parse_one(&arena, "(Grounded)"), atom_int(&arena, 6),
        atom_symbol_id(&arena, symbol_intern_bytes(g_symbols,
            (const uint8_t *)"Grounded\0suffix", 15u)),
    };
    size_t comparisons = 0u;
    for (size_t i = 0u; i < sizeof(subjects) / sizeof(*subjects); i++) {
        Atom *metatype = reference_metatype(&arena, subjects[i]);
        assert(metatype->kind == ATOM_SYMBOL);
        assert(atom_meta_type_id(subjects[i]) == metatype->sym_id);
        assert(atom_eq(get_meta_type(&arena, subjects[i]), metatype));
        for (size_t j = 0u; j < sizeof(expected) / sizeof(*expected); j++) {
            size_t before = arena_accounted_live_bytes(&arena);
            HeTypeDemand actual = he_type_source_demand(subjects[i], expected[j]);
            assert(arena_accounted_live_bytes(&arena) == before);
            assert(actual == reference_demand(subjects[i], expected[j], metatype));
            comparisons++;
            /* The public API still respects an explicitly supplied metatype. */
            for (size_t k = 0u; k < sizeof(expected) / sizeof(*expected); k++) {
                assert(he_type_demand(subjects[i], expected[j], expected[k]) ==
                    reference_demand(subjects[i], expected[j], expected[k]));
                comparisons++;
            }
        }
    }
    assert(he_type_source_demand(subjects[2], expected[3]) == HE_TYPE_KEEP);
    assert(he_type_source_demand(subjects[7], expected[6]) == HE_TYPE_INTERPRET);
    assert(he_type_source_demand(subjects[9], expected[4]) == HE_TYPE_KEEP);
    assert(he_type_source_demand(subjects[10], expected[4]) == HE_TYPE_CAST);
    printf("PASS: %zu independent demand comparisons; native lists and rest patterns preserved; source demand allocates no arena bytes\n", comparisons);
    arena_free(&arena);
    g_var_intern = NULL;
    var_intern_free(&variables);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}
