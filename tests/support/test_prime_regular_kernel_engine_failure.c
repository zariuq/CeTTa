#include "parser.h"
#include "prime_semantics.h"
#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
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

    Atom *judgment = parse_one(
        &persistent,
        "(type:eq "
        " (App (Lam (Pi U0 U0) (idx 0)) (Lam U0 (idx 0)))"
        " (Lam U0 (idx 0)))");
    cetta_runtime_stats_reset();
    cetta_runtime_stats_enable();
    Atom *verdict = judgment
        ? prime_semantics_judge_typing_direct(
              &scratch, &space, judgment, false, 0u)
        : NULL;
    CettaRuntimeStats stats;
    cetta_runtime_stats_snapshot(&stats);
    cetta_runtime_stats_disable();
    char *printed = verdict ? atom_to_string(&scratch, verdict) : NULL;

    bool ok = verdict_has_status(verdict, "Undetermined") && printed &&
              strstr(printed, "beta-substitution-failed") &&
              stats.counters[
                  CETTA_RUNTIME_COUNTER_PRIME_REGULAR_KERNEL_CONVERSION_ADMISSION_ENGINE_FAILURE] ==
                  1u &&
              stats.counters[
                  CETTA_RUNTIME_COUNTER_PRIME_LEGACY_HE_CONVERSION] == 0u &&
              stats.counters[
                  CETTA_RUNTIME_COUNTER_PRIME_CONVERSION_CERTIFICATE_CONSTRUCTION] ==
                  0u;
    if (!ok) {
        fprintf(stderr,
                "FAIL: native engine failure was hidden, retried, or certified\n");
        if (printed) fprintf(stderr, "verdict: %s\n", printed);
    }

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    return ok ? 0 : 1;
}
