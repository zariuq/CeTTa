#include "space.h"

#include <assert.h>
#include <stdio.h>

static Atom *equation(Arena *arena, const char *marker, Atom *argument, int value) {
    return atom_expr3(arena, atom_symbol(arena, marker),
        atom_expr2(arena, atom_symbol(arena, "f"), argument), atom_int(arena, value));
}

typedef struct {
    Arena *arena;
    Atom *variable;
    int values[16];
    bool bound[16];
    unsigned length;
} Answers;

static bool observe(Atom *body, const Bindings *bindings, void *raw) {
    Answers *answers = raw;
    assert(answers->length < 16u && body->kind == ATOM_GROUNDED && body->ground.gkind == GV_INT);
    Atom *query = bindings_apply_if_vars(bindings, answers->arena, answers->variable);
    answers->values[answers->length] = (int)body->ground.ival;
    answers->bound[answers->length++] = query->kind != ATOM_VAR;
    return true;
}

static Answers run(Space *space, Arena *arena, Atom *argument, unsigned forms) {
    SpaceEquationCursor cursor;
    SymbolId head = atom_symbol(arena, "f")->sym_id;
    assert(space_rule_cursor_init(space, head, forms, &cursor));
    Answers answers = {.arena = arena, .variable = argument};
    Atom *query = atom_expr2(arena, atom_symbol(arena, "f"), argument);
    SpaceEquationOccurrenceId id;
    SpaceEquationCursorStep step;
    while ((step = space_equation_cursor_next(&cursor, &id)) == SPACE_EQUATION_CURSOR_ITEM) {
        SpaceEquationOccurrence occurrence;
        CettaRuleDescriptor rule;
        assert(space_rule_occurrence_resolve(id, forms, &occurrence));
        assert(cetta_rule_view(occurrence.equation, forms, &rule));
        if (rule.binding == CETTA_RULE_BIND_HEAD)
            assert(!space_equation_occurrence_resolve(id, &occurrence));
        unsigned before = answers.length;
        CettaCount emitted = 42u;
        CettaTermMatchStatus status = query_rule_visit(&rule, query, arena,
                                                       observe, &answers, &emitted);
        assert(status == CETTA_TERM_MATCH_OK || status == CETTA_TERM_MATCH_MISMATCH);
        assert(emitted == answers.length - before);
    }
    assert(step == SPACE_EQUATION_CURSOR_END);
    return answers;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena arena;
    arena_init_detached(&arena);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &arena);
    Space space, dependency;
    space_init_with_universe(&space, &universe);
    space_init_with_universe(&dependency, &universe);
    Atom *a = atom_symbol(&arena, "a"), *x = atom_var(&arena, "x");
    Atom *q = atom_var(&arena, "q");
    Atom *directional = equation(&arena, "=%", a, 2);
    SymbolId head = atom_symbol(&arena, "f")->sym_id;
    space_add(&space, equation(&arena, "=", a, 1));
    assert(space_length64(&space) == 1u);
    assert(!space_directional_rules_may_match_known_head(&space, head));
    assert(space_directional_index_proves_absent(&space));
    SpaceProgramToken before = space_program_token(&space);
    space_begin_secondary_index_deferral(&space);
    space_add(&space, directional);
    assert(!space_directional_index_proves_absent(&space));
    assert(space_directional_rules_may_match_known_head(&space, head));
    space_end_secondary_index_deferral(&space);
    assert(!space_program_token_is_current(before));
    space_add(&space, directional); /* Separate, equal rule occurrences. */
    space_add(&space, equation(&arena, "=%", x, 3));
    space_add(&space, equation(&arena, "=", a, 4));
    space_add(&space, atom_expr2(&arena, atom_symbol(&arena, "quote"), directional));
    space_add(&dependency, equation(&arena, "=%", a, 5));
    assert(space_add_dependency(&space, &dependency));
    const unsigned both = CETTA_RULE_SYNTAX_ORDINARY | CETTA_RULE_SYNTAX_DIRECTIONAL;
    Answers ordinary = run(&space, &arena, q, CETTA_RULE_SYNTAX_ORDINARY);
    assert(ordinary.length == 2u && ordinary.values[0] == 1 && ordinary.values[1] == 4);
    assert(ordinary.bound[0] && ordinary.bound[1]);
    Answers open = run(&space, &arena, q, both);
    assert(open.length == 3u && open.values[0] == 1 && open.values[1] == 3 && open.values[2] == 4);
    assert(open.bound[0] && !open.bound[1] && open.bound[2]);
    Answers ground = run(&space, &arena, a, both);
    assert(ground.length == 6u);
    const int values[] = {1, 2, 2, 3, 4, 5};
    for (unsigned i = 0u; i < 6u; ++i) assert(ground.values[i] == values[i]);

    SpaceEquationCursor cursor;
    assert(space_rule_cursor_init(&space, head, both, &cursor));
    SpaceEquationOccurrenceId id;
    assert(space_equation_cursor_next(&cursor, &id) == SPACE_EQUATION_CURSOR_ITEM);
    space_add(&space, equation(&arena, "=%", a, 6));
    unsigned remaining = 0u;
    while (space_equation_cursor_next(&cursor, &id) == SPACE_EQUATION_CURSOR_ITEM) ++remaining;
    assert(remaining == 5u); /* Append does not enter this captured prefix. */
    assert(space_rule_cursor_init(&space, head, both, &cursor));
    before = space_program_token(&space);
    assert(space_remove(&space, directional));
    assert(!space_program_token_is_current(before));
    assert(space_equation_cursor_next(&cursor, &id) == SPACE_EQUATION_CURSOR_INVALIDATED);

    Space overlay;
    space_init_overlay(&overlay, &space);
    space_add(&overlay, equation(&arena, "=%", a, 7));
    Answers over = run(&overlay, &arena, a, both);
    /* Removal preserves the base's documented swap order; overlay-local
     * occurrences precede separately imported dependency occurrences. */
    const int overlay_values[] = {1, 6, 2, 3, 4, 7, 5};
    assert(over.length == sizeof(overlay_values) / sizeof(overlay_values[0]));
    for (unsigned i = 0u; i < over.length; ++i)
        assert(over.values[i] == overlay_values[i]);
    space_free(&overlay);

    /* A clean absence result must not hide later local or imported rules. */
    Space local;
    space_init_with_universe(&local, &universe);
    assert(!space_directional_rules_may_match_known_head(&local, head));
    assert(space_directional_index_proves_absent(&local));
    space_add(&local, directional);
    assert(space_directional_rules_may_match_known_head(&local, head));
    assert(space_remove(&local, directional));
    assert(!space_directional_rules_may_match_known_head(&local, head));
    assert(space_directional_index_proves_absent(&local));
    assert(space_add_dependency(&local, &dependency));
    assert(!space_directional_index_proves_absent(&local));
    assert(space_directional_rules_may_match_known_head(&local, head));
    space_init_overlay(&overlay, &local);
    assert(!space_directional_index_proves_absent(&overlay));
    assert(space_directional_rules_may_match_known_head(&overlay, head));
    space_free(&overlay);
    space_free(&local);
    space_free(&space);
    space_free(&dependency);
    term_universe_free(&universe);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: mixed rule occurrences, admission, caller protection, imports, overlays and invalidation");
    return 0;
}
