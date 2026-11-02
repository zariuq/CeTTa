#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "parser.h"
#include "petta_program.h"
#include "petta_search_machine.h"
#include "petta_semantics.h"
#include "symbol.h"

typedef struct {
    uint64_t purse, spent;
    size_t quantum, count;
    CettaSourceStep steps[512];
} Budget;

static bool permit(void *opaque, const CettaSourceStep *step) {
    Budget *budget = opaque;
    assert(step && step->source.root && step->debit == step->source.debit);
    if (!budget->quantum || step->debit > budget->purse)
        return false;
    budget->quantum--;
    budget->purse -= step->debit;
    budget->spent += step->debit;
    assert(budget->count < sizeof(budget->steps) / sizeof(*budget->steps));
    budget->steps[budget->count++] = *step;
    return true;
}

static Atom *parse(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *term = parse_sexpr(arena, text, &position);
    assert(term);
    return term;
}

static Atom *call(Arena *arena, unsigned variant) {
    unsigned arity = variant == 2u ? 66u : 2u;
    Atom *body = variant == 2u ? parse(arena, "(idx 0)")
        : parse(arena, "(Bundle (idx 1) (idx 0))");
    Atom *identity = petta_semantics_new_callable_identity(arena);
    assert(identity);
    for (unsigned parameter = 0u; parameter < arity; parameter++)
        body = petta_semantics_lambda_value(arena, identity, body);
    assert(body);
    Atom **elements = arena_alloc(arena, sizeof(*elements) * (arity + 1u));
    elements[0] = body;
    for (unsigned index = 0u; index < arity; index++)
        elements[index + 1u] = atom_int(arena, index == arity - 1u ? 9 : 7);
    if (variant == 1u)
        elements[1] = parse(arena, "(quote (+ 1 2))");
    return atom_expr(arena, elements, arity + 1u);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    const uint64_t allowances[] = {0u, 1u, 2u, 3u, 64u, 1024u};
    const char *wanted[] = {"(Bundle 7 9)", "(Bundle (+ 1 2) 9)", "9"};
    unsigned checks = 0u;
    for (unsigned variant = 0u; variant < 3u; variant++) {
        CettaSourceStep reference[512];
        size_t reference_count = 0u;
        for (unsigned purse = 6u; purse-- > 0u;)
        for (unsigned physical = 6u; physical-- > 0u;) {
            Arena arena;
            arena_init(&arena);
            Space space;
            space_init(&space);
            PettaProgram *program = petta_program_new();
            assert(program);
            Atom *query = call(&arena, variant);
            const PettaPlanNode *plan = petta_program_plan_current(program, query);
            assert(plan);
            Budget budget = {.purse = allowances[purse], .quantum = allowances[physical]};
            PettaMachineHost host = {.context = &budget,
                .forms_are_written_syntax = true, .permit_transition = permit};
            PettaMachine machine;
            assert(petta_machine_init_with_plan(&machine, &space, &arena,
                query, plan, NULL, &host));
            unsigned answers = 0u, slices = 0u;
            uint64_t supplied = budget.purse;
            bool replenished = false;
            bool complete = false;
            for (;;) {
                Atom *answer = NULL;
                Bindings environment;
                bindings_init(&environment);
                PettaMachineStep step = petta_machine_next(&machine, &answer, &environment);
                if (step == PETTA_MACHINE_STEP_SUSPENDED) {
                    bindings_free(&environment);
                    assert(slices++ < 2048u);
                    if (!budget.purse) {
                        assert(!replenished && budget.count <= reference_count);
                        for (size_t index = 0u; index < budget.count; index++)
                            assert(budget.steps[index].phase == reference[index].phase &&
                                budget.steps[index].debit == reference[index].debit);
                        assert(budget.spent == supplied);
                        budget.purse = 1024u;
                        supplied += 1024u;
                        replenished = true;
                    }
                    budget.quantum = allowances[physical] ? allowances[physical] : 1u;
                    continue;
                }
                if (step == PETTA_MACHINE_STEP_EXHAUSTED) {
                    bindings_free(&environment);
                    complete = true;
                    break;
                }
                if (step != PETTA_MACHINE_STEP_ANSWER ||
                    !atom_eq(answer, parse(&arena, wanted[variant]))) {
                    fprintf(stderr, "variant %u source %u physical %u step %d: ",
                        variant, purse, physical, step);
                    if (answer)
                        atom_print(answer, stderr);
                    fputc('\n', stderr);
                }
                assert(step == PETTA_MACHINE_STEP_ANSWER && !answers &&
                    atom_eq(answer, parse(&arena, wanted[variant])));
                answers++;
                bindings_free(&environment);
            }
            if (purse == 5u && physical == 5u) {
                assert(complete && answers == 1u);
                reference_count = budget.count;
                memcpy(reference, budget.steps, reference_count * sizeof(*reference));
            }
            assert(budget.count <= reference_count);
            for (size_t index = 0u; index < budget.count; index++)
                assert(budget.steps[index].phase == reference[index].phase &&
                    budget.steps[index].debit == reference[index].debit);
            assert(budget.spent + budget.purse == supplied);
            assert(complete && answers == 1u && budget.count == reference_count);
            petta_machine_destroy(&machine);
            petta_program_free(program);
            space_free(&space);
            arena_free(&arena);
            checks++;
        }
    }
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    assert(checks == 108u);
    printf("PASS: %u lambda-span source/physical budgets preserve values and split prefixes\n", checks);
    return 0;
}
