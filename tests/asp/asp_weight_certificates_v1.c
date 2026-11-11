/* The weighted certificate checker: a definition's atom is supported by its
 * definition, so no loop step may hold it (Mettapedia's WeightCertificate
 * refuses one too); the solver's own refutations with weight steps are
 * accepted, their truncations refused. */
#include "asp_cdnl_v1.h"

#include <stdio.h>
#include <stdlib.h>

static int failures;

static void expect(const char *what, int got, int want) {
    if (got != want) {
        printf("FAIL: %s: %d, expected %d\n", what, got, want);
        failures++;
    }
}

int main(void) {
    /* W := 0 <= {}: always true, so {W} is the stable model */
    int32_t w = 42;
    AspWeightDef always = {.atom = w, .bound = 0};
    int32_t model[1] = {w};
    expect("{W} is stable", asp_model_check_weighted(NULL, 0, &always, 1, model, 1), 1);
    int32_t c2[1] = {-1}, c3[1] = {1};
    uint32_t atoms[1] = {0}, hints[2] = {2, 3};
    AspStep loop_over_def[3] = {
        {.kind = ASP_STEP_LOOP, .id = 2, .clause = c2, .clause_len = 1,
         .atoms = atoms, .atoms_len = 1, .atom = 0},
        {.kind = ASP_STEP_WEIGHT, .id = 3, .clause = c3, .clause_len = 1, .def = 0},
        {.kind = ASP_STEP_RUP, .id = 4, .clause = NULL, .clause_len = 0,
         .hints = hints, .hints_len = 2},
    };
    expect("a loop step over a definition's atom is refused",
           asp_certificate_check_weighted(NULL, 0, &always, 1, loop_over_def, 3), 0);

    /* choices a and b; W := 1 <= a + b, V := 1 <= not a + not b; neither W
     * nor V: no stable model, refuted with weight steps */
    int32_t a = 1, b = 2, wa = 3, va = 4, out_a = 5, out_b = 6, bad1 = 7, bad2 = 8;
    int32_t pos_ab[2] = {a, b};
    uint64_t ones[2] = {1u, 1u};
    AspWeightDef defs[2] = {
        {.atom = wa, .bound = 1, .pos = pos_ab, .pos_w = ones, .pos_len = 2},
        {.atom = va, .bound = 1, .neg = pos_ab, .neg_w = ones, .neg_len = 2},
    };
    int32_t na[1] = {out_a}, nb[1] = {out_b}, pa[1] = {a}, pb[1] = {b};
    int32_t pw[1] = {wa}, pv[1] = {va}, nbad1[1] = {bad1}, nbad2[1] = {bad2};
    AspRule rules[6] = {
        {.head = a, .neg = na, .neg_len = 1},
        {.head = out_a, .neg = pa, .neg_len = 1},
        {.head = b, .neg = nb, .neg_len = 1},
        {.head = out_b, .neg = pb, .neg_len = 1},
        {.head = bad1, .pos = pw, .pos_len = 1, .neg = nbad1, .neg_len = 1},
        {.head = bad2, .pos = pv, .pos_len = 1, .neg = nbad2, .neg_len = 1},
    };
    AspAnswer answer = asp_cdnl_solve_weighted(rules, 6, defs, 2, ASP_SEARCH_FAST,
                                               NULL, NULL);
    expect("the program has no stable model", answer.kind, ASP_ANSWER_UNSAT);
    int weight_steps = 0;
    for (uint32_t i = 0u; i < answer.steps_len; i++)
        weight_steps += answer.steps[i].kind == ASP_STEP_WEIGHT;
    expect("its refutation holds weight steps", weight_steps > 0, 1);
    expect("its refutation is accepted",
           asp_certificate_check_weighted(rules, 6, defs, 2, answer.steps,
                                          answer.steps_len), 1);
    if (answer.steps_len)
        expect("its truncation is refused",
               asp_certificate_check_weighted(rules, 6, defs, 2, answer.steps,
                                              answer.steps_len - 1u), 0);
    asp_answer_free(&answer);
    if (failures)
        return 1;
    printf("PASS: the weighted certificate checker refuses a loop step over a definition's atom and accepts the solver's own weighted refutation, not its truncation\n");
    return 0;
}
