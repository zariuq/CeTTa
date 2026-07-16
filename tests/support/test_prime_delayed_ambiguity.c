#include "atom.h"
#include "eval.h"
#include "he_typing.h"
#include "parser.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>

static const char *normalize_status_name(CettaHeNormalizeStatus status) {
    switch (status) {
    case CETTA_HE_NORMALIZE_COMPLETE: return "complete";
    case CETTA_HE_NORMALIZE_RESOURCE: return "resource";
    case CETTA_HE_NORMALIZE_DEPTH: return "depth";
    case CETTA_HE_NORMALIZE_AMBIGUOUS: return "ambiguous";
    case CETTA_HE_NORMALIZE_NO_RESULT: return "no-result";
    case CETTA_HE_NORMALIZE_INADMISSIBLE: return "inadmissible";
    case CETTA_HE_NORMALIZE_PROVISIONAL: return "provisional";
    }
    return "invalid";
}

int main(void) {
    static const char *definitions =
        "(= (delayed-tag $n) "
        "   (if (== $n 0) TagB (delayed-tag (- $n 1)))) "
        "(= (delayed-source $x) "
        "   (superpose (TagA (delayed-tag 600)))) "
        "(type-level-function delayed-source)";

    int rc = 1;
    Arena persistent;
    Arena scratch;
    Arena result_arena;
    TermUniverse universe;
    Space space;
    SymbolTable symbols;
    VarInternTable var_intern;
    Atom **forms = NULL;
    Atom **queries = NULL;

    arena_init(&persistent);
    arena_set_runtime_kind(&persistent, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&scratch);
    arena_init(&result_arena);
    arena_set_runtime_kind(&result_arena, CETTA_ARENA_RUNTIME_KIND_EVAL);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    space_init_with_universe(&space, &universe);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&var_intern);
    g_symbols = &symbols;
    g_var_intern = &var_intern;
    eval_set_library_context(NULL);

    int form_count = parse_metta_text(definitions, &scratch, &forms);
    if (form_count != 3 || !forms) {
        fprintf(stderr, "definition parse failed: %d forms\n", form_count);
        goto cleanup;
    }
    for (int i = 0; i < form_count; i++) space_add(&space, forms[i]);

    int query_count = parse_metta_text("(delayed-source q)", &scratch,
                                       &queries);
    if (query_count != 1 || !queries) {
        fprintf(stderr, "query parse failed: %d forms\n", query_count);
        goto cleanup;
    }

    CettaHeTypingBudget low_budget;
    CettaHeTypingBudget high_budget;
    Atom *low_value = NULL;
    Atom *high_value = NULL;
    he_typing_budget_init(&low_budget, 520);
    he_typing_budget_init(&high_budget, 20000);

    CettaHeNormalizeStatus low = he_typing_normalize_type_status_budgeted(
        &result_arena, &space, queries[0], &low_budget, &low_value);
    CettaHeNormalizeStatus high = he_typing_normalize_type_status_budgeted(
        &result_arena, &space, queries[0], &high_budget, &high_value);

    printf("low=%s high=%s\n", normalize_status_name(low),
           normalize_status_name(high));
    if (low != CETTA_HE_NORMALIZE_RESOURCE ||
        high != CETTA_HE_NORMALIZE_AMBIGUOUS) {
        char *low_text = low_value
            ? atom_to_string(&scratch, low_value) : NULL;
        char *high_text = high_value
            ? atom_to_string(&scratch, high_value) : NULL;
        fprintf(stderr, "unexpected values: low=%s high=%s\n",
                low_text ? low_text : "<null>",
                high_text ? high_text : "<null>");
        goto cleanup;
    }

    rc = 0;

cleanup:
    free(queries);
    free(forms);
    eval_set_library_context(NULL);
    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&var_intern);
    symbol_table_free(&symbols);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&result_arena);
    arena_free(&scratch);
    arena_free(&persistent);
    return rc;
}
