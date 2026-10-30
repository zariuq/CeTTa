#include "eval.h"
#include "parser.h"
#include "petta_semantics.h"
#include "prepared_pure_machine.h"
#include "symbol.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Atom *read_atom(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *value = parse_sexpr(arena, text, &position);
    assert(value);
    return value;
}

static CettaPreparedPureProgram *compile(Space *space, Arena *source, const char *call) {
    return cetta_prepared_pure_program_compile_closed(space,
        read_atom(source, call), CETTA_GSLT_PURE_CALL_EAGER,
        atom_bool, atom_expr, cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL, true, true,
        (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()}, NULL);
}

typedef struct { Arena *arena; bool expected; unsigned count; } EqualityAnswer;

static bool equality_answer(Atom *answer, void *context) {
    EqualityAnswer *expected = context;
    assert(atom_eq(answer, atom_bool(expected->arena, expected->expected)));
    expected->count++;
    return true;
}

static void check_list_equality(Space *space, Arena *source, Arena *scratch,
                                Atom *left, Atom *right, bool expected) {
    Atom *call = atom_expr3(source, atom_symbol(source, "private-list-equality"), left, right);
    CettaMatchDecisionSemanticIdentity identity = {
        .compiler_identity = cetta_match_decision_compiler_identity()};
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed(
        space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL, true, true, identity, NULL);
    assert(program);
    Atom *result = NULL;
    assert(cetta_prepared_pure_program_execute_closed(program, scratch, 0u, &result));
    assert(atom_eq(result, atom_bool(source, expected)));
    cetta_prepared_pure_program_free(program);
    program = cetta_prepared_pure_program_compile_closed_answers(
        space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always,
        NULL, NULL, NULL, NULL, NULL, true, true, identity);
    assert(program);
    CettaPreparedPureAnswerLimits limits = {
        .max_transitions = 1024u, .max_scratch_bytes = 1024u * 1024u,
        .construct_allocation_bound = atom_expr_allocation_bound};
    EqualityAnswer answer = {source, expected, 0u};
    CettaPreparedPureAnswersResult returned = cetta_prepared_pure_program_visit_closed_answers(
        program, scratch, &limits, CETTA_PREPARED_PURE_UNMATCHED_DECLINES,
        equality_answer, &answer, NULL, NULL, NULL, NULL);
    assert(returned == CETTA_PREPARED_PURE_ANSWERS_COMPLETE && answer.count == 1u);
    cetta_prepared_pure_program_free(program);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols; g_hashcons = NULL;
    var_intern_init(&variables); g_var_intern = &variables;
    eval_set_library_context(NULL);
    Arena source, scratch;
    arena_init(&source); arena_init(&scratch);
    arena_set_hashcons(&source, NULL); arena_set_hashcons(&scratch, NULL);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &source);
    Space space;
    /* Initialization must not depend on calloc or a zeroed stack frame. */
    memset(&space, 0xa5, sizeof(space));
    space_init_with_universe(&space, &universe);
    assert(!space.declares_subtypes);
    const char *equations[] = {
        "(= (polynomial $x) (+ (* $x 0.5) 1.0))",
        "(= (negative-zero $x) (* (- $x 0.0) 1.0))",
        "(= (identity-boundary $x) $x)",
        "(= (call-boundary $x) (+ (identity-boundary $x) (* $x 0.5)))",
        "(= (equal-result $x) (== (+ $x 1.0) (* $x 2.0)))",
        "(= (allocate $n) (if (== $n 0) End (Box $n (allocate (- $n 1)))))",
        "(= (after-allocation $n $x) (let $derived (+ $x 0.25) (let $unused (allocate $n) (+ (* $derived 0.5) 1.0))))",
        "(= (private-list-equality $left $right) (== $left $right))",
    };
    for (size_t i=0u; i<sizeof(equations)/sizeof(*equations); i++)
        assert(space_admit_atom(&space, &source, read_atom(&source, equations[i])));
    const char *reference = getenv("CETTA_PREPARED_PURE_SCALAR_TREE_REFERENCE");
    bool fused = !(reference && *reference && strcmp(reference,"0"));
    CettaPreparedPureProgram *program = compile(&space, &source, "(polynomial 2.0)");
    assert(program && cetta_prepared_pure_program_scalar_tree_count(program)==(fused ? 1u : 0u));
    Atom *result = NULL;
    assert(cetta_prepared_pure_program_execute_closed(program, &scratch, 0u, &result));
    assert(result && result->kind==ATOM_GROUNDED && result->ground.gkind==GV_FLOAT && result->ground.fval==2.0);
    size_t start = scratch.live_bytes;
    for (size_t i=0u; i<1000u; i++) {
        Atom *call = atom_expr2(&source, atom_symbol(&source,"polynomial"), atom_float(&source,(double)i));
        assert(cetta_prepared_pure_program_rebind_closed_entry_call(program,call));
        assert(cetta_prepared_pure_program_execute_closed(program,&scratch,0u,&result));
        assert(result->ground.gkind==GV_FLOAT && result->ground.fval==(double)i*0.5+1.0);
    }
    size_t allocated = scratch.live_bytes-start;
    cetta_prepared_pure_program_free(program);
    program = compile(&space,&source,"(negative-zero -0.0)");
    assert(program && cetta_prepared_pure_program_execute_closed(program,&scratch,0u,&result));
    assert(result->ground.gkind==GV_FLOAT && result->ground.fval==0.0 && signbit(result->ground.fval));
    cetta_prepared_pure_program_free(program);
    program = compile(&space,&source,"(call-boundary 2.0)");
    assert(program && cetta_prepared_pure_program_scalar_tree_count(program)==0u);
    assert(cetta_prepared_pure_program_execute_closed(program,&scratch,0u,&result));
    assert(result->ground.gkind==GV_FLOAT && result->ground.fval==3.0);
    cetta_prepared_pure_program_free(program);
    program = compile(&space,&source,"(after-allocation 4000 1.0)");
    assert(program && cetta_prepared_pure_program_scalar_tree_count(program)==(fused ? 1u : 0u));
    assert(cetta_prepared_pure_program_execute_closed(program,&scratch,65536u,&result));
    assert(result->ground.gkind==GV_FLOAT && result->ground.fval==1.625);
    cetta_prepared_pure_program_free(program);
    Atom *a = atom_symbol(&source, "list-a"), *b = atom_symbol(&source, "list-b");
    Atom *cell = petta_semantics_open_cons_value(&source, a,
        petta_semantics_open_cons_value(&source, b, atom_unit(&source)));
    Atom *flat_values[] = {a, b};
    Atom *flat = atom_expr(&source, flat_values, 2u);
    Atom *wrap = atom_symbol(&source, "ListRecord");
    Atom *nested_cell = atom_expr2(&source, wrap, cell);
    Atom *nested_flat = atom_expr2(&source, wrap, flat);
    assert(!atom_value_eq(nested_cell, nested_flat));
    check_list_equality(&space, &source, &scratch, cell, flat, true);
    check_list_equality(&space, &source, &scratch, nested_cell, nested_flat, true);
    check_list_equality(&space, &source, &scratch, nested_flat, nested_cell, true);
    check_list_equality(&space, &source, &scratch,
        atom_expr3(&source, wrap, nested_cell, nested_cell),
        atom_expr3(&source, wrap, nested_flat, nested_flat), true);
    check_list_equality(&space, &source, &scratch, cell, atom_expr(&source, &a, 1u), false);
    check_list_equality(&space, &source, &scratch, nested_cell,
        atom_expr2(&source, wrap, atom_expr(&source, &a, 1u)), false);
    check_list_equality(&space, &source, &scratch, nested_cell,
        atom_expr2(&source, atom_symbol(&source, "OtherRecord"), flat), false);
    Atom *quote = atom_symbol(&source, "quote");
    check_list_equality(&space, &source, &scratch,
        atom_expr2(&source, quote, cell), atom_expr2(&source, quote, flat), false);
    check_list_equality(&space, &source, &scratch,
        atom_expr2(&source, quote, cell), atom_expr2(&source, quote, cell), true);
    Atom *improper = petta_semantics_open_cons_value(&source, a, b);
    Atom *authored = atom_expr3(&source, atom_symbol(&source, "cons"), a, b);
    check_list_equality(&space, &source, &scratch,
        atom_expr2(&source, wrap, improper), atom_expr2(&source, wrap, authored), true);
    printf("PreparedListEquality observations=20 nested-cells, shared-records, mismatches, opaque-quotes and improper-tails\n");
    program = compile(&space,&source,"(equal-result 1.0)");
    assert(program && cetta_prepared_pure_program_execute_closed(program,&scratch,0u,&result));
    assert(atom_eq(result,atom_bool(&source,true)));
    assert(space_admit_atom(&space,&source,read_atom(&source,"(= (new-revision) changed)")));
    assert(!cetta_prepared_pure_program_is_current(program));
    assert(!cetta_prepared_pure_program_execute_closed(program,&scratch,0u,&result));
    cetta_prepared_pure_program_free(program);
    printf("PreparedScalarTree mode=%s iterations=1000 allocated=%zu\n", fused ? "fused" : "reference", allocated);
    space_free(&space); term_universe_free(&universe);
    arena_free(&scratch); arena_free(&source);
    var_intern_free(&variables); g_var_intern=NULL;
    symbol_table_free(&symbols); g_symbols=NULL;
    return 0;
}
