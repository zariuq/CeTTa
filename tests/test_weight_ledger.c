#include "weight_ledger.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;
#define CHECK(test, message) do { \
    checks++; \
    if (!(test)) { fprintf(stderr, "FAIL: %s\n", message); failures++; } \
} while (0)

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    Arena source, survivor;
    arena_init(&source);
    arena_init(&survivor);
    CettaWeightLedger empty = {0}, shared = {0}, left = {0}, right = {0};
    CHECK(cetta_weight_ledger_append(&source, &empty, 1u, 10u,
        atom_int(&source, 0), &shared), "zero is a retained factor");
    CHECK(cetta_weight_ledger_append(&source, &shared, 2u, 20u,
        atom_int(&source, 3), &left), "left execution extends shared prefix");
    CHECK(cetta_weight_ledger_append(&source, &shared, 3u, 30u,
        atom_int(&source, 3), &right), "equal value has distinct physical factor");
    CettaWeightLedger merged = left;
    CHECK(cetta_weight_ledger_merge(&merged, &right) && merged.length == 3u,
          "sharing counted once, equal fresh coefficients counted twice");
    const CettaWeightFactor **ordered = cetta_weight_ledger_ordered(&merged);
    CHECK(ordered && ordered[0]->occurrence == 1u &&
          ordered[1]->occurrence == 2u && ordered[2]->occurrence == 3u,
          "logical merge order is preserved");
    free(ordered);
    CettaWeightLedger reverse = right;
    CHECK(cetta_weight_ledger_merge(&reverse, &left) &&
          !cetta_weight_ledger_equal(&reverse, &merged),
          "noncommutative merge retains its source order");
    CettaWeightLedger saved = merged;
    CHECK(!cetta_weight_ledger_merge(&merged, &reverse) &&
          cetta_weight_ledger_equal(&saved, &merged),
          "incompatible ordering refuses without changing destination");
    CHECK(cetta_weight_ledger_merge(&merged, &left) &&
          cetta_weight_ledger_equal(&saved, &merged),
          "ancestor reuse contributes no second factor");
    CHECK(!cetta_weight_ledger_append(&source, &merged, 2u, 20u,
          atom_int(&source, 3), &merged) &&
          cetta_weight_ledger_equal(&saved, &merged),
          "append cannot disguise duplicate execution as sharing");
    CettaWeightLedger conflict = {0};
    CHECK(cetta_weight_ledger_append(&source, &empty, 1u, 10u,
          atom_int(&source, 1), &conflict) &&
          !cetta_weight_ledger_merge(&merged, &conflict),
          "same occurrence with conflicting value is refused");
    CHECK(cetta_weight_ledger_append(&source, &empty, 1u, 99u,
          atom_int(&source, 0), &conflict) &&
          !cetta_weight_ledger_merge(&merged, &conflict),
          "same occurrence with conflicting dependency is refused");

    CettaWeightLedger handled = {0}, selected = {0};
    CHECK(cetta_weight_ledger_handle(&source, &merged, &empty, 100u,
          &handled, &selected) && selected.length == 3u && handled.length == 3u &&
          handled.claim_count == 3u && cetta_weight_ledger_handler(&handled, 1u) == 100u,
          "read claims its ordered factors without deleting zero or production history");
    CHECK(cetta_weight_ledger_equal(&selected, &merged) &&
          !cetta_weight_ledger_equal(&handled, &merged),
          "selected factors and returned handled world have distinct roles");
    CettaWeightLedger old_cache = merged;
    CHECK(cetta_weight_ledger_merge(&old_cache, &handled) &&
          cetta_weight_ledger_equal(&old_cache, &handled) &&
          cetta_weight_ledger_merge(&handled, &merged) &&
          cetta_weight_ledger_equal(&old_cache, &handled),
          "old cached world merges cannot resurrect handled contributions in either order");
    CettaWeightLedger again = {0}, no_factors = {0};
    CHECK(cetta_weight_ledger_handle(&source, &handled, &empty, 200u,
          &again, &no_factors) && !no_factors.length &&
          cetta_weight_ledger_equal(&again, &handled),
          "later read observes a unit from an already handled shared producer");
    CettaWeightLedger rival = {0}, rival_factors = {0};
    CHECK(cetta_weight_ledger_handle(&source, &merged, &empty, 200u,
          &rival, &rival_factors) && !cetta_weight_ledger_merge(&handled, &rival) &&
          cetta_weight_ledger_equal(&handled, &old_cache),
          "incomparable handler claims conflict without changing the destination");
    CettaWeightLedger suffix_world = {0}, suffix = {0};
    CHECK(cetta_weight_ledger_handle(&source, &left, &shared, 300u,
          &suffix_world, &suffix) && suffix.length == 1u &&
          suffix.last->occurrence == 2u &&
          !cetta_weight_ledger_handler(&suffix_world, 1u),
          "incoming shared prefix remains available to its enclosing observation");
    CHECK(cetta_weight_ledger_append(&source, &handled, 4u, 40u,
          atom_int(&source, 7), &again) &&
          cetta_weight_ledger_handle(&source, &again, &empty, 400u,
              &again, &no_factors) && no_factors.length == 1u &&
          no_factors.last->value->ground.ival == 7,
          "callback production after claiming the source remains an outer contribution");
    CettaWeightLedger refusal_saved = again, selection_saved = no_factors;
    CHECK(!cetta_weight_ledger_handle(&source, &left, &right, 500u,
          &again, &no_factors) && cetta_weight_ledger_equal(&again, &refusal_saved) &&
          cetta_weight_ledger_equal(&no_factors, &selection_saved),
          "wrong captured prefix refuses without changing either result");
    CHECK(!cetta_weight_ledger_handle(&source, &merged, &empty, 0u,
          &again, &no_factors) && cetta_weight_ledger_equal(&again, &refusal_saved),
          "exhausted handler identity cannot authorize a read");
    CHECK(!cetta_weight_ledger_handle(&source, &merged, &handled, 500u,
          &again, &no_factors) && cetta_weight_ledger_equal(&again, &refusal_saved),
          "a captured handled prefix cannot be replaced by an older unclaimed world");
    CHECK(cetta_weight_ledger_extends(&handled, &merged) &&
          !cetta_weight_ledger_extends(&merged, &handled) &&
          cetta_weight_ledger_extends(&again, &handled) &&
          !cetta_weight_ledger_extends(&left, &right),
          "captured authority checks ordered factors and inherited claims");
    CHECK(cetta_weight_ledger_claims_conflict(&handled, &rival) &&
          cetta_weight_ledger_claims_conflict(&rival, &handled) &&
          !cetta_weight_ledger_claims_conflict(&handled, &old_cache) &&
          !cetta_weight_ledger_claims_conflict(&handled, &merged),
          "ownership refusal distinguishes rival handlers from older or identical claims");
    CettaWeightLedger retained_claims = handled;
    CHECK(cetta_weight_ledger_promote(&survivor, &retained_claims) &&
          cetta_weight_ledger_equal(&retained_claims, &handled),
          "arena transfer retains full production and handler ownership");
    CettaWeightLedger promoted = merged;
    CHECK(cetta_weight_ledger_promote(&survivor, &promoted) &&
          cetta_weight_ledger_equal(&promoted, &merged),
          "promotion preserves occurrences and semantic values");
    arena_free(&source);
    CHECK(cetta_weight_ledger_handler(&retained_claims, 2u) == 100u &&
          retained_claims.last->value->ground.ival == 3,
          "handled ownership and coefficient values outlive their original arena");
    ordered = cetta_weight_ledger_ordered(&promoted);
    CHECK(ordered && ordered[0]->value->ground.ival == 0 &&
          ordered[1]->value->ground.ival == 3 &&
          ordered[2]->value->ground.ival == 3,
          "promoted coefficients outlive source arena");
    free(ordered);
    arena_free(&survivor);
    symbol_table_free(&symbols);
    printf("Weight ledger: %u passed, %u failed\n", checks - failures, failures);
    return failures ? 1 : 0;
}
