#include "weight_ledger.h"
#include "tests/test_runtime_stats_stubs.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks, failures;
#define CHECK(test, message) do { \
    checks++; \
    if (!(test)) { fprintf(stderr, "FAIL: %s\n", message); failures++; } \
} while (0)

static void fresh_read_storage(void) {
    Arena producer, consumer;
    arena_init(&producer);
    arena_init(&consumer);
    CettaWeightLedger empty = {0}, produced = {0};
    const uint64_t count = 128u;
    for (uint64_t i = 0u; i < count; i++)
        CHECK(cetta_weight_ledger_append(&producer, &produced, i + 1u, i + 1000u,
            atom_expr2(&producer, atom_symbol(&producer, "Coefficient"),
                atom_int(&producer, (int64_t)(i % 3u))), &produced),
            "fresh source factors include repeated and zero coefficient payloads");

    CettaWeightLedger world = {0}, selected = {0};
    CHECK(cetta_weight_ledger_handle(&producer, &produced, &empty, 400u,
        &world, &selected) && world.claim_count == count && selected.length == count,
        "one read claims the entire previously unclaimed production");
    CHECK(selected.last == produced.last && selected.last == world.last &&
        !selected.claims && !selected.claim_count && !produced.claims,
        "whole-source read shares owned immutable factors with separate claim maps");
    const CettaWeightClaim *claim = world.claims;
    for (uint64_t i = count; i > 0u; i--) {
        CHECK(claim && claim->occurrence == i && claim->handler == 400u,
            "fresh claims preserve the existing ordered map construction");
        claim = claim ? claim->previous : NULL;
    }
    CHECK(!claim, "fresh read introduces exactly one claim for every factor");
    CHECK(!cetta_weight_ledger_claims_conflict(&world, &world),
        "the same immutable claim map cannot conflict with itself");
    CettaWeightLedger extended = {0};
    CHECK(cetta_weight_ledger_append(&producer, &world, count + 1u, 2000u,
        atom_int(&producer, 7), &extended) && extended.claims == world.claims &&
        !cetta_weight_ledger_claims_conflict(&world, &extended) &&
        !cetta_weight_ledger_claims_conflict(&extended, &world),
        "fresh production preserves compatibility of its inherited shared claim map");
    CettaWeightLedger later = {0}, unit = {0};
    CHECK(cetta_weight_ledger_handle(&producer, &world, &empty, 500u, &later, &unit) &&
        !unit.length && cetta_weight_ledger_equal(&later, &world),
        "later observations cannot select any already claimed factor");

    CettaWeightLedger promoted = {0}, detached = {0};
    CHECK(cetta_weight_ledger_handle(&consumer, &produced, &empty, 600u,
        &promoted, &detached) && promoted.claim_count == count && detached.length == count,
        "a cross-arena fresh read promotes the complete factor payload once");
    CHECK(promoted.last != produced.last && promoted.last == detached.last &&
        detached.owner == &consumer && !detached.claims,
        "shared read image uses its destination arena rather than borrowing source storage");
    arena_free(&producer);
    const CettaWeightFactor **ordered = cetta_weight_ledger_ordered(&detached);
    CHECK(ordered != NULL, "detached whole-source factors outlive the producer arena");
    for (uint64_t i = 0u; ordered && i < count; i++)
        CHECK(ordered[i]->occurrence == i + 1u && ordered[i]->dependency == i + 1000u &&
            ordered[i]->value->kind == ATOM_EXPR && ordered[i]->value->expr.len == 2u &&
            ordered[i]->value->expr.elems[1]->ground.ival == (int64_t)(i % 3u) &&
            cetta_weight_ledger_handler(&promoted, i + 1u) == 600u,
            "promotion preserves every ordered factor, duplicate payload and actual handler");
    free(ordered);
    arena_free(&consumer);
}

static void suffix_import_cases(void) {
    Arena source, recipient_owner, survivor;
    arena_init(&source); arena_init(&recipient_owner); arena_init(&survivor);
    CettaWeightLedger empty = {0}, common = {0}, entrance = {0}, production = {0};
    CettaWeightLedger recipient = {0}, imported = {0};
    CHECK(cetta_weight_ledger_append(&source, &empty, 1u, 101u, atom_int(&source, 2), &common) &&
        cetta_weight_ledger_append(&source, &common, 2u, 102u, atom_int(&source, 3), &entrance) &&
        cetta_weight_ledger_append(&source, &entrance, 3u, 103u, atom_int(&source, 7), &production) &&
        cetta_weight_ledger_append(&source, &production, 4u, 104u, atom_int(&source, 7), &production),
        "a producer earns two equal but physically distinct factors after force entrance");
    CHECK(cetta_weight_ledger_append(&recipient_owner, &common, 5u, 105u,
        atom_int(&recipient_owner, 11), &recipient), "another reader has its own earlier choice");
    CHECK(cetta_weight_ledger_import_suffix(&survivor, &entrance, &production, &recipient, &imported) &&
        imported.length == 4u, "only the earned suffix joins the recipient's earlier world");
    const CettaWeightFactor **items = cetta_weight_ledger_ordered(&imported);
    CHECK(items && items[0]->occurrence == 1u && items[1]->occurrence == 5u &&
        items[2]->occurrence == 3u && items[3]->occurrence == 4u &&
        items[1]->value->ground.ival == 11 && items[2]->value->ground.ival == 7 &&
        items[3]->value->ground.ival == 7,
        "no earlier producer choice leaks and equal earned factors retain both identities");
    free(items);
    CettaWeightLedger again = {0};
    CHECK(cetta_weight_ledger_import_suffix(&survivor, &entrance, &production, &imported, &again) &&
        cetta_weight_ledger_equal(&again, &imported), "reimport charges no factor twice");
    CettaWeightLedger held = {0};
    CHECK(cetta_weight_ledger_append(&recipient_owner, &recipient, 3u, 103u,
        atom_int(&recipient_owner, 7), &held) &&
        cetta_weight_ledger_append(&recipient_owner, &held, 6u, 106u,
        atom_int(&recipient_owner, 13), &held) &&
        cetta_weight_ledger_import_suffix(&survivor, &entrance, &production, &held, &again),
        "an unrelated recipient factor can follow an already imported earned prefix");
    items = cetta_weight_ledger_ordered(&again);
    CHECK(items && again.length == 5u && items[2]->occurrence == 3u &&
        items[3]->occurrence == 6u && items[4]->occurrence == 4u,
        "recipient order stays intact before the remaining earned suffix");
    free(items);
    CettaWeightLedger refused = again, saved = again;
    CHECK(cetta_weight_ledger_append(&recipient_owner, &recipient, 4u, 104u,
        atom_int(&recipient_owner, 7), &held) &&
        !cetta_weight_ledger_import_suffix(&survivor, &entrance, &production, &held, &refused) &&
        refused.last == saved.last, "a hole in the shared earned prefix refuses without publishing");
    CHECK(cetta_weight_ledger_append(&recipient_owner, &held, 3u, 103u,
        atom_int(&recipient_owner, 7), &held) &&
        !cetta_weight_ledger_import_suffix(&survivor, &entrance, &production, &held, &refused) &&
        refused.last == saved.last, "reversed earned factors cannot impersonate a shared prefix");
    for (unsigned change = 0u; change < 2u; change++) {
        CHECK(cetta_weight_ledger_append(&recipient_owner, &recipient, 3u,
            change ? 999u : 103u, atom_int(&recipient_owner, change ? 7 : 8), &held) &&
            !cetta_weight_ledger_import_suffix(&survivor, &entrance, &production, &held, &refused) &&
            refused.last == saved.last, "changed payload or dependency cannot reuse an earned identity");
    }
    CettaWeightLedger wrong_entrance = {0};
    CHECK(cetta_weight_ledger_append(&source, &common, 2u, 902u, atom_int(&source, 3),
        &wrong_entrance) && !cetta_weight_ledger_import_suffix(&survivor, &wrong_entrance,
        &production, &recipient, &refused) && refused.last == saved.last,
        "the claimed entrance must be the actual production prefix");

    CettaWeightLedger handled = {0}, selected = {0}, claimed = {0};
    CHECK(cetta_weight_ledger_handle(&source, &production, &entrance, 910u, &handled, &selected) &&
        selected.length == 2u && cetta_weight_ledger_import_suffix(&survivor,
        &entrance, &handled, &recipient, &claimed) && claimed.claim_count == 2u &&
        cetta_weight_ledger_handler(&claimed, 3u) == 910u &&
        cetta_weight_ledger_handler(&claimed, 4u) == 910u &&
        !cetta_weight_ledger_handler(&claimed, 1u) && !cetta_weight_ledger_handler(&claimed, 5u),
        "new post-entrance claims travel with their original earned factors only");
    CHECK(cetta_weight_ledger_import_suffix(&survivor, &entrance, &handled, &claimed, &again) &&
        cetta_weight_ledger_equal(&again, &claimed), "the same handler claim is idempotent");
    CettaWeightLedger rival = {0};
    CHECK(cetta_weight_ledger_handle(&survivor, &imported, &recipient, 911u, &rival, &selected) &&
        !cetta_weight_ledger_import_suffix(&survivor, &entrance, &handled, &rival, &refused) &&
        refused.last == saved.last, "a competing handler cannot take the same earned factors");
    CettaWeightLedger all_claimed = {0};
    CHECK(cetta_weight_ledger_handle(&source, &production, &empty, 912u,
        &all_claimed, &selected) && !cetta_weight_ledger_import_suffix(&survivor,
        &entrance, &all_claimed, &recipient, &refused) && refused.last == saved.last,
        "a new claim cannot import an absent pre-entrance factor by accident");
    CHECK(cetta_weight_ledger_import_suffix(&survivor, &entrance, &all_claimed, &entrance, &again) &&
        again.claim_count == 4u && cetta_weight_ledger_handler(&again, 2u) == 912u,
        "a new pre-entrance claim needs the identical factor already present in the recipient");
    CettaWeightLedger claimed_entrance = {0}, inherited = {0};
    CHECK(cetta_weight_ledger_handle(&source, &entrance, &empty, 913u,
        &claimed_entrance, &selected) && cetta_weight_ledger_append(&source,
        &claimed_entrance, 3u, 103u, atom_int(&source, 7), &inherited) &&
        cetta_weight_ledger_import_suffix(&survivor, &claimed_entrance, &inherited, &recipient, &again) &&
        !again.claim_count && again.length == 3u,
        "inherited pre-entrance claims remain outside the imported production cut");
    arena_free(&source); arena_free(&recipient_owner);
    items = cetta_weight_ledger_ordered(&claimed);
    CHECK(items && items[0]->value->ground.ival == 2 && items[1]->value->ground.ival == 11 &&
        items[2]->dependency == 103u && items[3]->value->ground.ival == 7 &&
        cetta_weight_ledger_handler(&claimed, 4u) == 910u,
        "all imported payloads and claims outlive both source and recipient arenas");
    free(items); arena_free(&survivor);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    fresh_read_storage();
    suffix_import_cases();
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
