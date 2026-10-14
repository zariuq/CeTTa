#include "atom.h"
#include "parser.h"
#include "petta_program.h"
#include "symbol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static Atom *parse(Arena *owner, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, owner, &forms);
    Atom *result = count == 1 ? forms[0] : NULL;
    free(forms);
    assert(result);
    return result;
}

int main(void) {
    SymbolTable symbols; VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables); g_symbols = &symbols; g_var_intern = &variables;
    Arena syntax, owner; arena_init_detached(&syntax); arena_init_detached(&owner);
    Space space; space_init(&space);
    PettaProgram *program = petta_program_new(); assert(program);
    assert(petta_program_synchronize_space(program, &space));
    Atom *call = parse(&syntax, "(owned-call $a $b)");
    const PettaPlanNode *data = petta_program_plan_current_in(program, &owner, call);
    assert(data && data->role == PETTA_PLAN_DATA && !data->relation_head_admitted);
    space_add(&space, parse(&syntax, "(= (owned-call $a $b) (pair $a $b))"));
    assert(petta_program_synchronize_space(program, &space));
    ArenaMark mark = arena_mark(&owner);
    for (unsigned i = 0; i < 20000; ++i) {
        const PettaPlanNode *plan = petta_program_plan_current_in(program, &owner, call);
        assert(plan && plan->role == PETTA_PLAN_STATIC_CALL && plan->relation_head_admitted);
        assert(plan->child_count == 3 && plan->children[1].role == PETTA_PLAN_VALUE);
        assert(!plan->deterministic_region && !plan->region_hole_program);
        assert(arena_accounted_live_bytes(&owner) > arena_mark_accounted_live_bytes(mark));
        arena_reset(&owner, mark);
        assert(arena_accounted_live_bytes(&owner) == arena_mark_accounted_live_bytes(mark));
    }
    const PettaPlanNode *kept = petta_program_plan_current_in(program, &owner, call);
    assert(!petta_program_plan_current_in(program, NULL, call));
    assert(!petta_program_plan_current_in(program, &owner, NULL));
    assert(!petta_program_plan_current_in(NULL, &owner, call));
    petta_program_free(program);
    space_free(&space); arena_free(&syntax);
    assert(kept->role == PETTA_PLAN_STATIC_CALL && kept->child_count == 3);
    assert(kept->children[0].role == PETTA_PLAN_VALUE);
    assert(kept->children[1].role == PETTA_PLAN_VALUE && kept->children[2].role == PETTA_PLAN_VALUE);
    assert(data->role == PETTA_PLAN_DATA);
    arena_free(&owner); var_intern_free(&variables); symbol_table_free(&symbols);
    g_var_intern = NULL; g_symbols = NULL;
    puts("PASS: caller-owned call plans reset exactly and survive program/source teardown");
    return 0;
}
