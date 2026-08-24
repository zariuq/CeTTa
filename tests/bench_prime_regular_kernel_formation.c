#define _POSIX_C_SOURCE 200809L

#include "parser.h"
#include "prime_semantics.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    Atom *result = count == 1 && forms ? forms[0] : NULL;
    free(forms);
    return result;
}

static bool verdict_has_status(Atom *verdict, const char *status) {
    return verdict && verdict->kind == ATOM_EXPR &&
           verdict->expr.len == 4u &&
           atom_is_symbol(verdict->expr.elems[0], "PrimeVerdict") &&
           atom_is_symbol(verdict->expr.elems[1], status);
}

static uint64_t elapsed_ns(struct timespec start, struct timespec end) {
    uint64_t seconds = (uint64_t)(end.tv_sec - start.tv_sec);
    int64_t nanoseconds = end.tv_nsec - start.tv_nsec;
    if (nanoseconds < 0) {
        seconds--;
        nanoseconds += 1000000000L;
    }
    return seconds * UINT64_C(1000000000) + (uint64_t)nanoseconds;
}

int main(int argc, char **argv) {
    const char *workload = argc > 1 ? argv[1] : "formed";
    uint64_t iterations = argc > 2
        ? (uint64_t)strtoull(argv[2], NULL, 10) : UINT64_C(20000);
    const char *judgment_text = NULL;
    const char *expected_status = NULL;
    if (strcmp(workload, "formed") == 0) {
        judgment_text = "(type:formed (Pi U0 U0))";
        expected_status = "Established";
    } else if (strcmp(workload, "scoped-formed") == 0) {
        judgment_text = "(type:of (PrimeScoped PrimeCtxNil (Pi U0 U0)))";
        expected_status = "Established";
    } else if (strcmp(workload, "top") == 0) {
        judgment_text = "(type:formed U1)";
        expected_status = "Refuted";
    } else if (strcmp(workload, "scoped-top") == 0) {
        judgment_text = "(type:of (PrimeScoped PrimeCtxNil U1))";
        expected_status = "Refuted";
    } else if (strcmp(workload, "control") == 0) {
        judgment_text = "(type:formed Number)";
        expected_status = "Established";
    } else {
        fprintf(stderr, "unknown workload: %s\n", workload);
        return 2;
    }
    if (argc > 3) expected_status = argv[3];
    if (iterations == 0u) {
        fputs("iterations must be positive\n", stderr);
        return 2;
    }

    Arena persistent;
    Arena scratch;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&persistent);
    arena_init(&scratch);
    arena_set_runtime_kind(
        &persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    Atom *judgment = parse_one(&persistent, judgment_text);
    if (!judgment) {
        fputs("benchmark judgment did not parse\n", stderr);
        return 2;
    }
    for (uint64_t index = 0u; index < 100u; index++) {
        ArenaMark mark = arena_mark(&scratch);
        Atom *verdict = prime_semantics_judge_typing_direct(
            &scratch, &space, judgment, false, 0u);
        if (!verdict_has_status(verdict, expected_status)) {
            fputs("benchmark warmup changed the expected verdict\n", stderr);
            return 1;
        }
        arena_reset(&scratch, mark);
    }

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (uint64_t index = 0u; index < iterations; index++) {
        ArenaMark mark = arena_mark(&scratch);
        Atom *verdict = prime_semantics_judge_typing_direct(
            &scratch, &space, judgment, false, 0u);
        if (!verdict_has_status(verdict, expected_status)) {
            fputs("benchmark iteration changed the expected verdict\n", stderr);
            return 1;
        }
        arena_reset(&scratch, mark);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    uint64_t nanoseconds = elapsed_ns(start, end);
    const char *lane = argc > 4 ? argv[4] : "default";
    printf("lane=%s workload=%s iterations=%" PRIu64
           " elapsed_ns=%" PRIu64 " ns_per=%.3f\n",
           lane, workload, iterations, nanoseconds,
           (double)nanoseconds / (double)iterations);

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    return 0;
}
