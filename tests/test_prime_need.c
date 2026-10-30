#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "atom.h"
#include "prime_need.h"
#include "symbol.h"

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

static bool same_cell_metadata(const PrimeNeedCellView *left,
                               const PrimeNeedCellView *right) {
    if (left->cache_state != right->cache_state ||
        left->update_id != right->update_id ||
        left->origin != right->origin || left->cached != right->cached ||
        left->authority_id != right->authority_id ||
        left->evaluator_id != right->evaluator_id ||
        left->storage_key != right->storage_key ||
        left->import_key != right->import_key ||
        left->source_occurrence_id != right->source_occurrence_id ||
        left->source_argument_index != right->source_argument_index)
        return false;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    if (left->capture_known != right->capture_known ||
        left->capture_var_ids != right->capture_var_ids ||
        left->capture_var_count != right->capture_var_count)
        return false;
    for (size_t index = 0u; index < left->capture_var_count; index++)
        if (!var_id_eq(left->capture_var_ids[index], right->capture_var_ids[index]))
            return false;
#endif
    return true;
}

static bool same_snapshot_metadata(const PrimeNeedSnapshot *left,
                                   const PrimeNeedSnapshot *right) {
    if (left->top != right->top || left->session_id != right->session_id ||
        left->max_storage_key != right->max_storage_key ||
        left->owner != right->owner || left->closure_owner != right->closure_owner)
        return false;
#if CETTA_PRIME_NEED_HEAP_INDEX
    if (left->heap_index != right->heap_index || left->lineage_index != right->lineage_index)
        return false;
#endif
    return true;
}

static void capability_record_controls(Arena *arena,
                                       const PrimeNeedSnapshot *snapshot,
                                       const PrimeNeedSnapshot *absent,
                                       uint64_t thunk_id) {
    PrimeNeedSnapshot before = *snapshot;
    PrimeNeedCellView original = {0}, observed = {0};
    Atom *ref = prime_need_ref(arena, snapshot, thunk_id);
    const CettaPrimeNeedCapability *record = atom_prime_need_capability_value(ref);
    uint64_t session = 0u, thunk = 0u, authority = 0u;
    uint32_t rights = 0u;
    bool ready = ref && record && prime_need_snapshot_lookup(snapshot, thunk_id, &original);
    CHECK(ready && ref->kind == ATOM_GROUNDED &&
              ref->ground.gkind == GV_PRIME_NEED_CAPABILITY &&
              prime_need_ref_parse_rights(ref, &session, &thunk, &authority, &rights) &&
              session == record->session_id && session == snapshot->session_id &&
              thunk == record->thunk_id && thunk == thunk_id &&
              authority == record->authority_id && authority == original.authority_id &&
              rights == record->rights && rights == CETTA_PRIME_NEED_RIGHT_ALL,
          "borrowed capability readback retains all original identity and rights fields");
    if (!ready)
        return;

    Atom wrong_kind = *ref;
    Atom wrong_ground = *ref;
    wrong_kind.kind = ATOM_SYMBOL;
    wrong_ground.ground.gkind = GV_INT;
    CHECK(!atom_prime_need_capability_value(&wrong_kind) &&
              !atom_prime_need_capability_value(&wrong_ground) &&
              !prime_need_ref_parse_rights(&wrong_kind, NULL, NULL, NULL, NULL) &&
              !prime_need_ref_parse_rights(&wrong_ground, NULL, NULL, NULL, NULL),
          "capability payload bits under another Atom or grounded tag grant no identity");

    for (unsigned malformed = 0u; malformed < 6u; malformed++) {
        CettaPrimeNeedCapability damaged = *record;
        Atom raw = *ref;
        raw.ground.prime_need_capability = &damaged;
        switch (malformed) {
        case 0u: damaged.session_id = 0u; break;
        case 1u: damaged.thunk_id = 0u; break;
        case 2u: damaged.authority_id = 0u; break;
        case 3u: damaged.rights = 0u; break;
        case 4u: damaged.rights = CETTA_PRIME_NEED_RIGHT_ALL | 4u; break;
        default: damaged.rights = CETTA_PRIME_NEED_RIGHT_ALL | UINT32_C(0x80000000); break;
        }
        session = UINT64_MAX;
        thunk = UINT64_MAX;
        authority = UINT64_MAX;
        rights = UINT32_MAX;
        CHECK(!prime_need_ref_parse_rights(&raw, &session, &thunk, &authority, &rights) &&
                  session == UINT64_MAX && thunk == UINT64_MAX &&
                  authority == UINT64_MAX && rights == UINT32_MAX &&
                  !prime_need_ref_belongs_to_with_rights(
                      &raw, snapshot, CETTA_PRIME_NEED_RIGHT_INSPECT, NULL),
              "malformed native capability record leaves parse outputs and membership ungranted");
    }

    CHECK(!prime_need_ref_restrict(arena, ref, snapshot, 0u) &&
              !prime_need_ref_belongs_to_with_rights(ref, snapshot, 0u, NULL),
          "zero rights are refused both as attenuation and as a membership request");
    CHECK(!prime_need_ref_restrict(arena, ref, snapshot, CETTA_PRIME_NEED_RIGHT_ALL | 4u) &&
              !prime_need_ref_belongs_to_with_rights(
                  ref, snapshot, CETTA_PRIME_NEED_RIGHT_ALL | 4u, NULL),
          "unknown requested rights cannot be masked into valid capability authority");

    Atom *inspect = prime_need_ref_restrict(arena, ref, snapshot, CETTA_PRIME_NEED_RIGHT_INSPECT);
    Atom *force = prime_need_ref_restrict(arena, ref, snapshot, CETTA_PRIME_NEED_RIGHT_FORCE);
    Atom *inspect_again = prime_need_ref_restrict(
        arena, inspect, snapshot, CETTA_PRIME_NEED_RIGHT_INSPECT);
    const CettaPrimeNeedCapability *again = atom_prime_need_capability_value(inspect_again);
    CHECK(inspect && inspect_again && again &&
              again->session_id == record->session_id && again->thunk_id == record->thunk_id &&
              again->authority_id == record->authority_id &&
              again->rights == CETTA_PRIME_NEED_RIGHT_INSPECT &&
              prime_need_ref_belongs_to_with_rights(
                  inspect_again, snapshot, CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
              !prime_need_ref_belongs_to_with_rights(
                  inspect_again, snapshot, CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
          "repeated attenuation retains suspension identity without restoring force");
    CHECK(force && !prime_need_ref_restrict(
                  arena, force, snapshot, CETTA_PRIME_NEED_RIGHT_INSPECT) &&
              !prime_need_ref_restrict(arena, force, snapshot, CETTA_PRIME_NEED_RIGHT_ALL) &&
              !prime_need_ref_restrict(arena, inspect, snapshot, CETTA_PRIME_NEED_RIGHT_FORCE),
          "neither one-right capability can acquire the other right through restriction");

    thunk = UINT64_MAX;
    CHECK(absent->session_id == snapshot->session_id &&
              !prime_need_ref_belongs_to_with_rights(
                  inspect, absent, CETTA_PRIME_NEED_RIGHT_INSPECT, &thunk) &&
              thunk == UINT64_MAX && !prime_need_ref_restrict(
                  arena, ref, absent, CETTA_PRIME_NEED_RIGHT_INSPECT),
          "a retained same-session snapshot without the cell refuses membership and attenuation");

    bool inert = true;
    for (unsigned index = 0u; index < 8u; index++) {
        inert = inert && prime_need_ref_parse_rights(
            inspect, &session, &thunk, &authority, &rights) &&
            prime_need_ref_belongs_to_with_rights(
                inspect, snapshot, CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
            !prime_need_ref_belongs_to_with_rights(
                inspect, snapshot, CETTA_PRIME_NEED_RIGHT_FORCE, NULL);
    }
    CHECK(inert && same_snapshot_metadata(&before, snapshot) &&
              prime_need_snapshot_lookup(snapshot, thunk_id, &observed) &&
              same_cell_metadata(&original, &observed) &&
              observed.cache_state == PRIME_NEED_CACHE_EMPTY &&
              observed.source_occurrence_id != 0u && observed.source_argument_index == 2u,
          "repeated inspection preserves original producer, occurrence metadata, cache and snapshot");
}

static bool history_readback_controls(const PrimeNeedSnapshot *empty,
                                     const PrimeNeedSnapshot *allocated,
                                     const PrimeNeedSnapshot *evaluating,
                                     const PrimeNeedSnapshot *completed,
                                     uint64_t thunk_id, Atom *origin,
                                     Atom *value, uint64_t evaluator,
                                     PrimeNeedCacheState completed_state) {
    PrimeNeedHistoryIterator iterator;
    PrimeNeedHistoryView view = {0};
    const PrimeNeedFrame *top = completed->top;
    if (!prime_need_snapshot_history_begin(empty, &iterator) ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_DONE ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_DONE ||
        !prime_need_snapshot_history_begin(completed, &iterator))
        return false;
    const PrimeNeedCacheState states[] = {
        completed_state, PRIME_NEED_CACHE_EVALUATING, PRIME_NEED_CACHE_EMPTY,
    };
    uint64_t serials[3];
    for (size_t i = 0; i < 3u; i++) {
        if (prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_ENTRY ||
            view.session_id != completed->session_id || view.thunk_id != thunk_id ||
            view.depth != 3u - i || view.cell.origin != origin ||
            view.cell.cache_state != states[i] || view.cell.authority_id == 0u ||
            view.cell.evaluator_id != (i == 1u ? evaluator : 0u) ||
            view.cell.cached != (i == 0u ? value : NULL))
            return false;
        serials[i] = view.serial;
    }
    if (!(serials[0] > serials[1] && serials[1] > serials[2]) ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_DONE ||
        completed->top != top ||
        !prime_need_snapshot_history_begin(allocated, &iterator) ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_ENTRY ||
        view.serial != serials[2] ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_DONE ||
        !prime_need_snapshot_history_begin(evaluating, &iterator) ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_ENTRY ||
        view.serial != serials[1])
        return false;
    PrimeNeedHistoryView sentinel = view;
    iterator.remaining++;
    if (prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_INVALID ||
        prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_INVALID ||
        view.serial != sentinel.serial || view.cell.origin != sentinel.cell.origin)
        return false;
    PrimeNeedSnapshot foreign = *completed;
    foreign.session_id++;
    return !prime_need_snapshot_history_begin(&foreign, &iterator) &&
        prime_need_snapshot_history_next(&iterator, &view) == PRIME_NEED_HISTORY_INVALID &&
        view.serial == sentinel.serial && view.cell.origin == sentinel.cell.origin &&
        !prime_need_snapshot_history_begin(NULL, &iterator) &&
        !prime_need_snapshot_history_begin(completed, NULL);
}

static bool history_transport_controls(const PrimeNeedSnapshot *before,
                                       const PrimeNeedSnapshot *after) {
    PrimeNeedHistoryIterator left, right;
    PrimeNeedHistoryView a, b;
    if (!prime_need_snapshot_history_begin(before, &left) ||
        !prime_need_snapshot_history_begin(after, &right))
        return false;
    for (;;) {
        PrimeNeedHistoryStep first = prime_need_snapshot_history_next(&left, &a);
        PrimeNeedHistoryStep second = prime_need_snapshot_history_next(&right, &b);
        if (first != second || first == PRIME_NEED_HISTORY_INVALID)
            return false;
        if (first == PRIME_NEED_HISTORY_DONE)
            return true;
        if (a.session_id != b.session_id || a.serial != b.serial ||
            a.thunk_id != b.thunk_id || a.depth != b.depth ||
            a.cell.cache_state != b.cell.cache_state ||
            a.cell.authority_id != b.cell.authority_id ||
            a.cell.evaluator_id != b.cell.evaluator_id ||
            a.cell.storage_key != b.cell.storage_key ||
            a.cell.import_key != b.cell.import_key ||
            a.cell.source_occurrence_id != b.cell.source_occurrence_id ||
            a.cell.source_argument_index != b.cell.source_argument_index ||
            !atom_eq(a.cell.origin, b.cell.origin) ||
            ((a.cell.cached == NULL) != (b.cell.cached == NULL)) ||
            (a.cell.cached && !atom_eq(a.cell.cached, b.cell.cached)))
            return false;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        if (a.cell.capture_known != b.cell.capture_known ||
            a.cell.capture_var_count != b.cell.capture_var_count)
            return false;
        for (size_t i = 0; i < a.cell.capture_var_count; i++)
            if (!var_id_eq(a.cell.capture_var_ids[i], b.cell.capture_var_ids[i]))
                return false;
#endif
    }
}

static bool history_latest(const PrimeNeedSnapshot *snapshot, uint64_t thunk,
                           PrimeNeedHistoryView *out) {
    PrimeNeedHistoryIterator iterator;
    PrimeNeedHistoryView row;
    if (!out || !prime_need_snapshot_history_begin(snapshot, &iterator))
        return false;
    for (;;) {
        PrimeNeedHistoryStep step = prime_need_snapshot_history_next(&iterator, &row);
        if (step != PRIME_NEED_HISTORY_ENTRY)
            return false;
        if (row.thunk_id == thunk) {
            *out = row;
            return true;
        }
    }
}

static bool history_cell_matches_lookup(const PrimeNeedSnapshot *snapshot,
                                       uint64_t thunk) {
    PrimeNeedHistoryView row;
    PrimeNeedCellView cell;
    if (!history_latest(snapshot, thunk, &row) ||
        !prime_need_snapshot_lookup(snapshot, thunk, &cell) ||
        row.session_id != snapshot->session_id ||
        row.cell.cache_state != cell.cache_state ||
        row.cell.origin != cell.origin || row.cell.cached != cell.cached ||
        row.cell.authority_id != cell.authority_id ||
        row.cell.evaluator_id != cell.evaluator_id ||
        row.cell.storage_key != cell.storage_key || row.cell.import_key != cell.import_key ||
        row.cell.source_occurrence_id != cell.source_occurrence_id ||
        row.cell.source_argument_index != cell.source_argument_index)
        return false;
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    if (row.cell.capture_known != cell.capture_known ||
        row.cell.capture_var_ids != cell.capture_var_ids ||
        row.cell.capture_var_count != cell.capture_var_count)
        return false;
#endif
    return true;
}

static void interleaved_history_controls(Arena *arena, Arena *destination,
                                        Arena *temporary) {
    PrimeNeedSnapshot stages[9] = {0};
    uint64_t numbered = 0u;
    const uint64_t named = PRIME_NEED_NAMED_CELL_BIT | UINT64_C(701);
    Atom *numbered_origin = atom_expr2(arena, atom_symbol(arena, "HistoryOrigin"),
                                      atom_int(arena, 1));
    Atom *named_origin = atom_expr2(arena, atom_symbol(arena, "HistoryOrigin"),
                                   atom_int(arena, 1));
    Atom *value = atom_expr2(arena, atom_symbol(arena, "HistoryValue"), atom_int(arena, 7));
    Atom *fault = atom_symbol(arena, "HistoryFault");
    bool ready = numbered_origin && named_origin && value && fault &&
        numbered_origin != named_origin && atom_eq(numbered_origin, named_origin) &&
        prime_need_snapshot_begin(&stages[0]) &&
        prime_need_snapshot_allocate(arena, &stages[0], numbered_origin, &stages[1], &numbered) &&
        prime_need_snapshot_allocate_named(arena, &stages[1], named_origin, named, &stages[2]) &&
        prime_need_snapshot_start_evaluation(arena, &stages[2], numbered, 23u, &stages[3]) &&
        prime_need_snapshot_start_evaluation(arena, &stages[3], named, 29u, &stages[4]) &&
        prime_need_snapshot_retry_evaluation(arena, &stages[4], numbered, 23u, &stages[5]) &&
        prime_need_snapshot_resolve_stable_fault(arena, &stages[5], named, 29u, fault, &stages[6]) &&
        prime_need_snapshot_start_evaluation(arena, &stages[6], numbered, 31u, &stages[7]) &&
        prime_need_snapshot_resolve_value(arena, &stages[7], numbered, 31u, value, &stages[8]);
    CHECK(ready, "interleaved numbered and named cells retain retry and stable-fault histories");

    const uint64_t cells[] = {numbered, numbered, named, numbered, named, numbered, named, numbered};
    const PrimeNeedCacheState states[] = {PRIME_NEED_CACHE_VALUE, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_STABLE_FAULT, PRIME_NEED_CACHE_EMPTY, PRIME_NEED_CACHE_EVALUATING,
        PRIME_NEED_CACHE_EVALUATING, PRIME_NEED_CACHE_EMPTY, PRIME_NEED_CACHE_EMPTY};
    PrimeNeedHistoryIterator iterator;
    PrimeNeedHistoryView row = {0};
    bool ordered = ready && prime_need_snapshot_history_begin(&stages[8], &iterator);
    uint64_t previous_serial = UINT64_MAX;
    for (size_t i = 0u; ordered && i < 8u; i++) {
        ordered = prime_need_snapshot_history_next(&iterator, &row) == PRIME_NEED_HISTORY_ENTRY &&
            row.session_id == stages[8].session_id && row.depth == 8u - i &&
            row.thunk_id == cells[i] && row.cell.cache_state == states[i] &&
            row.serial != 0u && row.serial < previous_serial &&
            row.cell.origin == (cells[i] == numbered ? numbered_origin : named_origin);
        previous_serial = row.serial;
    }
    CHECK(ordered && prime_need_snapshot_history_next(&iterator, &row) == PRIME_NEED_HISTORY_DONE,
          "complete history preserves every interleaved update and distinct equal producer nodes");
    PrimeNeedHistoryView untouched = {.serial = UINT64_MAX};
    CHECK(ready && history_cell_matches_lookup(&stages[8], numbered) &&
              history_cell_matches_lookup(&stages[8], named) &&
              !history_latest(&stages[8], 0u, &untouched) && untouched.serial == UINT64_MAX,
          "first matching history records agree with indexed and named latest-cell lookup");
    PrimeNeedHistoryView earlier, later;
    CHECK(ready && history_latest(&stages[4], numbered, &earlier) &&
              history_latest(&stages[8], numbered, &later) && earlier.serial != later.serial &&
              earlier.cell.cache_state == PRIME_NEED_CACHE_EVALUATING &&
              later.cell.cache_state == PRIME_NEED_CACHE_VALUE &&
              history_cell_matches_lookup(&stages[4], named),
          "saved prefixes retain their own latest evaluations after a descendant completes");
    PrimeNeedSnapshot image = stages[8];
    CHECK(ready && prime_need_snapshot_promote(destination, &image) &&
              history_transport_controls(&stages[8], &image) &&
              history_cell_matches_lookup(&image, numbered) &&
              history_cell_matches_lookup(&image, named),
          "promotion preserves the complete interleaved history and latest-cell observations");
    PrimeNeedSnapshot sibling = {0};
    Atom *equal_value = atom_deep_copy(arena, value);
    CHECK(ready && equal_value && atom_eq(value, equal_value) &&
              prime_need_snapshot_resolve_value(arena, &stages[7], numbered, 31u,
                                                equal_value, &sibling) &&
              history_latest(&sibling, numbered, &earlier) &&
              history_latest(&stages[8], numbered, &later) && earlier.serial != later.serial &&
              atom_eq(earlier.cell.cached, later.cell.cached) &&
              !history_transport_controls(&sibling, &stages[8]) &&
              !prime_need_snapshot_is_ancestor(&sibling, &stages[8]) &&
              !prime_need_snapshot_is_ancestor(&stages[8], &sibling),
          "equal sibling results retain different producer updates and incomparable histories");

    PrimeNeedSnapshot suffix = {0}, suffix_image = {0};
    PrimeNeedHistoryView suffix_top = {0}, suffix_prefix = {0};
    bool suffix_ready = ready &&
        prime_need_snapshot_start_evaluation(temporary, &stages[1], numbered, 41u, &suffix);
    if (suffix_ready)
        suffix_image = suffix;
    CHECK(suffix_ready &&
              prime_need_snapshot_promote_suffix(destination, temporary, &suffix_image) &&
              history_transport_controls(&suffix, &suffix_image) &&
              prime_need_snapshot_excludes_arena(&suffix_image, temporary) &&
              !prime_need_snapshot_excludes_arena(&suffix_image, arena) &&
              prime_need_snapshot_history_begin(&suffix_image, &iterator) &&
              prime_need_snapshot_history_next(&iterator, &suffix_top) == PRIME_NEED_HISTORY_ENTRY &&
              prime_need_snapshot_history_next(&iterator, &suffix_prefix) == PRIME_NEED_HISTORY_ENTRY &&
              prime_need_snapshot_history_next(&iterator, &row) == PRIME_NEED_HISTORY_DONE &&
              suffix_top.cell.origin != suffix_prefix.cell.origin &&
              suffix_prefix.cell.origin == numbered_origin &&
              atom_eq(suffix_top.cell.origin, suffix_prefix.cell.origin),
          "suffix promotion preserves origin content across copied and retained addresses");

    PrimeNeedSnapshot named_left = {0}, named_right = {0};
    PrimeNeedHistoryView named_left_row = {0}, named_right_row = {0};
    CHECK(ready &&
              prime_need_snapshot_allocate_named(arena, &stages[0], numbered_origin,
                                                 named, &named_left) &&
              prime_need_snapshot_allocate_named(arena, &stages[0], named_origin,
                                                 named, &named_right) &&
              history_latest(&named_left, named, &named_left_row) &&
              history_latest(&named_right, named, &named_right_row) &&
              named_left_row.session_id == named_right_row.session_id &&
              named_left_row.thunk_id == named_right_row.thunk_id &&
              named_left_row.cell.authority_id != named_right_row.cell.authority_id &&
              atom_eq(named_left_row.cell.origin, named_right_row.cell.origin) &&
              !prime_need_snapshot_is_ancestor(&named_left, &named_right) &&
              !prime_need_snapshot_is_ancestor(&named_right, &named_left),
          "sibling named allocations share a lookup key without sharing allocation authority");
    Atom *named_left_ref = prime_need_ref(arena, &named_left, named);
    Atom *named_right_ref = prime_need_ref(arena, &named_right, named);
    Atom *named_left_inspect = prime_need_ref_restrict(
        arena, named_left_ref, &named_left, CETTA_PRIME_NEED_RIGHT_INSPECT);
    uint64_t untouched_thunk = UINT64_MAX;
    CHECK(ready && named_left_ref && named_right_ref && named_left_inspect &&
              prime_need_ref_belongs_to_with_rights(
                  named_left_ref, &named_left, CETTA_PRIME_NEED_RIGHT_ALL, NULL) &&
              prime_need_ref_belongs_to_with_rights(
                  named_right_ref, &named_right, CETTA_PRIME_NEED_RIGHT_ALL, NULL) &&
              !prime_need_ref_belongs_to_with_rights(
                  named_left_inspect, &named_right, CETTA_PRIME_NEED_RIGHT_INSPECT,
                  &untouched_thunk) && untouched_thunk == UINT64_MAX &&
              !prime_need_ref_restrict(
                  arena, named_left_ref, &named_right, CETTA_PRIME_NEED_RIGHT_INSPECT),
          "same-key equal-origin sibling snapshots cannot borrow another allocation's capability");
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static void check_receipt_graph_producers(void) {
    PrimeNeedReceiptGraph empty = {0};
    bool unchanged = true;
    CHECK(prime_need_receipt_graph_producers(&empty, NULL) &&
              prime_need_receipt_graph_producers(&empty, &unchanged) && unchanged,
          "an empty graph needs no producer index or output storage");
    PrimeNeedReceiptGraph missing = {.length = 1u};
    PrimeNeedReceiptGraph too_large = {.nodes = (PrimeNeedReceiptGraphNode *)&missing,
        .length = SIZE_MAX};
    CHECK(!prime_need_receipt_graph_producers(NULL, &unchanged) &&
              !prime_need_receipt_graph_producers(&missing, &unchanged) &&
              !prime_need_receipt_graph_producers(&too_large, &unchanged) && unchanged,
          "invalid producer views leave the caller's flags untouched");
    PrimeNeedReceiptGraphNode nodes[10] = {0};
    for (size_t i = 0u; i < 10u; i++) {
        nodes[i].identity_id = i + 1u;
        nodes[i].has_event = true;
        nodes[i].event = (PrimeNeedReceiptEvent){
            .need_session_id = 7u, .thunk_id = 11u, .producer_update_id = 17u};
    }
    nodes[0].event.producer_completed = true;
    nodes[2].event.need_session_id = 8u;
    nodes[3].event.thunk_id = 12u;
    nodes[4].has_event = false;
    nodes[4].event.producer_completed = true;
    nodes[5].event.producer_update_id = 0u;
    nodes[6].event.producer_update_id = 19u;
    nodes[7].event = (PrimeNeedReceiptEvent){.need_session_id = UINT64_MAX,
        .thunk_id = UINT64_MAX - 1u, .producer_update_id = UINT64_MAX,
        .producer_completed = true};
    nodes[8].event = nodes[7].event;
    nodes[8].event.producer_completed = false;
    nodes[9].has_event = false;
    PrimeNeedReceiptGraphNode original[10];
    memcpy(original, nodes, sizeof(nodes));
    bool flags[10] = {0};
    const bool expected[10] = {true, true, false, false, false,
                              false, false, true, true, false};
    PrimeNeedReceiptGraph graph = {.nodes = nodes, .length = 10u};
    CHECK(prime_need_receipt_graph_producers(&graph, flags) &&
              memcmp(flags, expected, sizeof(flags)) == 0 &&
              memcmp(nodes, original, sizeof(nodes)) == 0,
          "producer presence keeps session/cell/update identities and the original node order");
    nodes[0].event.producer_completed = false;
    nodes[7].event.producer_completed = false;
    bool all_external = prime_need_receipt_graph_producers(&graph, flags);
    for (size_t i = 0u; i < 10u; i++) all_external &= !flags[i];
    CHECK(all_external,
          "a graph of deliveries cannot invent retained production from matching identities");

    const size_t count = 8192u;
    PrimeNeedReceiptGraphNode *many = calloc(count * 2u, sizeof(*many));
    bool *many_flags = calloc(count * 2u, sizeof(*many_flags));
    CHECK(many && many_flags, "large producer view allocates its temporary test storage");
    if (many && many_flags) {
        for (size_t i = 0u; i < count; i++) {
            many[i].identity_id = i + 1u;
            many[i].has_event = true;
            many[i].event = (PrimeNeedReceiptEvent){
                .need_session_id = UINT64_MAX - i * 31u,
                .thunk_id = UINT64_MAX - i * 127u,
                .producer_update_id = UINT64_MAX - i * 257u,
                .producer_completed = true};
        }
        for (size_t i = 0u; i < count; i++) {
            many[count + i] = many[count - i - 1u];
            many[count + i].identity_id = count + i + 1u;
            many[count + i].event.producer_completed = false;
        }
        many[count].event.need_session_id = 0u;
        many[count + 1u].event.thunk_id = 0u;
        many[count + 2u].event.producer_update_id = 0u;
        many[count + 3u].has_event = false;
        many[count + 4u].event.producer_update_id = 1u;
        PrimeNeedReceiptGraph large = {.nodes = many, .length = count * 2u};
        bool matches = prime_need_receipt_graph_producers(&large, many_flags);
        for (size_t i = 0u; i < count * 2u; i++)
            matches &= many_flags[i] == !(i >= count && i < count + 5u) &&
                many[i].identity_id == i + 1u;
        CHECK(matches,
              "large reversed deliveries find exact producers without sorting the occurrence graph");
    } else CHECK(false, "large producer view could not run");
    free(many_flags);
    free(many);
}

static bool recording_root_count(const PrimeNeedReceipt *root, void *raw) {
    if (!root->top) return false;
    PrimeNeedReceiptGraph graph = {0};
    PrimeNeedReceiptGraphStatus status = prime_need_receipt_graph_collect(root, &graph);
    prime_need_receipt_graph_free(&graph);
    if (status != PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        status != PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE) return false;
    (*(size_t *)raw)++;
    return true;
}

static void check_nested_recording_publication(void) {
    Arena input, destination;
    arena_init_detached(&input);
    arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){24u, 196608u, 3u, 1u});
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *session = NULL;
    PrimeNeedReceipt parent = {0}, child = {0};
    bool ready = group && prime_need_recording_reserve(
        group, NULL, 8u, 65536u, &parent);
    CHECK(ready, "nested history reserves its parent's allocation inventory");
    if (!ready) goto done;
    PrimeNeedRecordingScope scope;
    prime_need_recording_enter(&scope, &parent);
    ready = prime_need_recording_inherit(&child);
    prime_need_recording_leave(&scope);
    CHECK(ready, "nested history inherits a separate bounded slice");
    if (!ready) goto done;
    CHECK(prime_need_receipt_observe_cell(&input, &parent, 31u, 1u,
              atom_int(&input, 7), &parent) &&
          prime_need_receipt_observe_cell(&input, &child, 31u, 2u,
              atom_int(&input, 7), &child),
          "equal parent and child payloads have distinct event occurrences");
    const PrimeNeedReceiptFrame *child_root = child.top;
    prime_need_recording_enter(&scope, &parent);
    prime_need_recording_publish(&child);
    PrimeNeedRecordingStatus once = prime_need_recording_status(group);
    prime_need_recording_publish(&child);
    PrimeNeedRecordingStatus twice = prime_need_recording_status(group);
    prime_need_recording_leave(&scope);
    CHECK(child.top == child_root && once.nodes == twice.nodes &&
              once.payload_charge == twice.payload_charge && !twice.incomplete,
          "nested publication is idempotent and retains the source receipt");
    size_t roots = 0u;
    CHECK(prime_need_recording_roots(&parent, recording_root_count, &roots) && roots == 1u,
          "parent root inventory exposes the joined nested history");
    copy = prime_need_recording_copy_new(group);
    CHECK(copy && prime_need_recording_copy_pair_count(copy) == 2u,
          "published history retains two valid allocation domains");
    if (!copy) goto done;
    AtomDeepCopyArenaPair pairs[3] = {{&input, &destination}};
    session = atom_deep_copy_session_new(&destination);
    ready = session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
        prime_need_recording_copy_pair(copy, 1u, &pairs[2]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 3u) &&
        prime_need_recording_copy_prepare(copy, session);
    CHECK(ready, "nested publication remains eligible for complete recording copy");
    if (!ready) goto done;
    PrimeNeedReceipt copied_parent = {0}, copied_child = {0};
    CHECK(prime_need_recording_copy_receipt(copy, &parent, &copied_parent) &&
          prime_need_recording_copy_receipt(copy, &child, &copied_child) &&
          copied_child.top && copied_child.top != child_root,
          "parent and child carriers relocate through the same owner map");
    roots = 0u;
    CHECK(prime_need_recording_roots(&copied_parent, recording_root_count, &roots) && roots == 1u,
          "copied parent preserves its nested publication root inventory");
done:
    atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    prime_need_recording_release(group);
    arena_free(&destination);
    arena_free(&input);

    arena_init_detached(&input);
    group = prime_need_recording_new((PrimeNeedRecordingLimits){2u, 8192u, 2u, 0u});
    parent = (PrimeNeedReceipt){0};
    child = (PrimeNeedReceipt){0};
    ready = group && prime_need_recording_reserve(group, NULL, 0u, 0u, &parent) &&
        prime_need_recording_reserve(group, NULL, 2u, 8192u, &child) &&
        prime_need_receipt_observe_cell(&input, &child, 31u, 3u,
            atom_int(&input, 7), &child);
    CHECK(ready, "publication refusal control retains an actual child event");
    if (ready) {
        prime_need_recording_enter(&scope, &parent);
        prime_need_recording_publish(&child);
        prime_need_recording_leave(&scope);
        roots = 0u;
        CHECK(!prime_need_recording_status(group).incomplete &&
              prime_need_recording_status(group).nodes == 1u &&
              prime_need_recording_roots(&parent, recording_root_count, &roots) && roots == 1u,
              "an empty parent adopts a valid child root without allocating a join");
        PrimeNeedReceipt other = child;
        other.top = NULL;
        CHECK(prime_need_receipt_observe_cell(&input, &other, 31u, 4u,
            atom_int(&input, 8), &child),
            "a second independent child root requires a real publication join");
        child_root = child.top;
        prime_need_recording_enter(&scope, &parent);
        prime_need_recording_publish(&child);
        prime_need_recording_leave(&scope);
        roots = 0u;
        CHECK(prime_need_recording_status(group).incomplete &&
              child.top == child_root &&
              prime_need_recording_roots(&child, recording_root_count, &roots) && roots == 1u,
              "refused publication reports omission without dropping child evidence");
    }
    prime_need_recording_release(group);
    arena_free(&input);
}

static void check_adopted_recording_copy(void) {
    Arena input, destination;
    arena_init_detached(&input);
    arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 32768u, 3u, 0u});
    PrimeNeedReceipt parent = {0}, child = {0}, outer = {0};
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *session = NULL;
    bool ready = group && prime_need_recording_reserve(group, NULL, 1u, 8192u, &parent) &&
        prime_need_recording_reserve(group, NULL, 1u, 8192u, &outer) &&
        prime_need_recording_reserve(group, NULL, 2u, 16384u, &child) &&
        prime_need_receipt_observe_cell(&input, &child, 31u, 5u,
            atom_int(&input, 9), &child);
    CHECK(ready, "adopted history has an independently allocated child event");
    if (!ready) goto done;
    PrimeNeedRecordingScope scope;
    prime_need_recording_enter(&scope, &parent);
    prime_need_recording_publish(&child);
    prime_need_recording_publish(&child);
    prime_need_recording_leave(&scope);
    size_t roots = 0u;
    CHECK(prime_need_recording_status(group).nodes == 1u &&
          prime_need_recording_roots(&parent, recording_root_count, &roots) && roots == 1u,
          "first publication retains a valid root with no extra event or node");
    prime_need_recording_enter(&scope, &outer);
    prime_need_recording_publish(&parent);
    prime_need_recording_publish(&parent);
    prime_need_recording_leave(&scope);
    roots = 0u;
    CHECK(prime_need_recording_status(group).nodes == 1u &&
          prime_need_recording_roots(&outer, recording_root_count, &roots) && roots == 1u,
          "an adopted root publishes through a second empty parent without duplication");
    parent.top = child.top;
    CHECK(prime_need_receipt_observe_cell(&input, &parent, 31u, 6u,
            atom_int(&input, 10), &parent),
          "a local parent event can extend its adopted causal root");
    roots = 0u;
    CHECK(prime_need_recording_roots(&parent, recording_root_count, &roots) && roots == 1u,
          "a local descendant exposes its adopted predecessor exactly once");
    prime_need_recording_enter(&scope, &outer);
    prime_need_recording_publish(&parent);
    prime_need_recording_publish(&parent);
    prime_need_recording_leave(&scope);
    copy = prime_need_recording_copy_new(group);
    CHECK(copy && prime_need_recording_copy_pair_count(copy) == 3u,
          "an adopted root retains both allocation owners for transport");
    if (!copy) goto done;
    AtomDeepCopyArenaPair pairs[4] = {{&input, &destination}};
    session = atom_deep_copy_session_new(&destination);
    ready = session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
        prime_need_recording_copy_pair(copy, 1u, &pairs[2]) &&
        prime_need_recording_copy_pair(copy, 2u, &pairs[3]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 4u) &&
        prime_need_recording_copy_prepare(copy, session);
    CHECK(ready, "complete owner transport relocates an adopted publication root");
    if (!ready) goto done;
    PrimeNeedReceipt image = {0};
    roots = 0u;
    CHECK(prime_need_recording_copy_receipt(copy, &outer, &image) &&
          prime_need_recording_roots(&image, recording_root_count, &roots) && roots == 1u,
          "copied outer parent exposes the same valid adopted graph");
done:
    atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    prime_need_recording_release(group);
    arena_free(&destination);
    arena_free(&input);
}

static void check_growing_recording_grants(void) {
    Arena input, destination;
    arena_init_detached(&input);
    arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 16384u, 2u, 0u});
    PrimeNeedReceipt parent = {0}, child = {0};
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *session = NULL;
    bool ready = group && prime_need_recording_reserve_growing(
        group, NULL, 1u, 4096u, &parent);
    CHECK(ready, "growing recording reserves an explicit initial grant");
    if (!ready) goto done;
    for (uint64_t i = 0u; i < 3u; i++)
        ready &= prime_need_receipt_observe_cell(&input, &parent, 31u, i + 1u,
            atom_int(&input, 7), &parent);
    PrimeNeedRecordingStatus grown = prime_need_recording_status(group);
    CHECK(ready && grown.nodes == 3u && grown.reserved_nodes == 3u &&
          grown.reserved_payload_bytes == 12288u && !grown.incomplete,
          "continuing recording pays for each further grant within the group limit");
    PrimeNeedRecordingScope scope;
    prime_need_recording_enter(&scope, &parent);
    ready = prime_need_recording_inherit(&child);
    prime_need_recording_leave(&scope);
    CHECK(ready && prime_need_recording_status(group).reserved_nodes == 4u,
          "inheritance takes the original quantum rather than the parent's expanded limit");
    if (!ready) goto done;
    const PrimeNeedReceiptFrame *saved = parent.top;
    CHECK(prime_need_receipt_observe_cell(&input, &parent, 31u, 5u,
              atom_int(&input, 9), &parent) && parent.top == saved &&
          prime_need_recording_status(group).nodes == 3u &&
          prime_need_recording_status(group).omitted == 1u,
          "growth exhaustion retains the prefix and exposes the omitted event");
    copy = prime_need_recording_copy_new(group);
    session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[3] = {{&input, &destination}};
    CHECK(copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
          prime_need_recording_copy_pair(copy, 1u, &pairs[2]) &&
          atom_deep_copy_session_set_arena_map(session, pairs, 3u) &&
          prime_need_recording_copy_prepare(copy, session),
          "growing reservations and omissions survive complete owner transport");
done:
    atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    prime_need_recording_release(group);
    arena_free(&destination);
    arena_free(&input);
}

static void check_adoption_invalidates_current_copy(void) {
    Arena input, destination;
    arena_init_detached(&input);
    arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){1u, 8192u, 2u, 0u});
    PrimeNeedReceipt parent = {0}, child = {0};
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *session = NULL;
    bool ready = group && prime_need_recording_reserve(group, NULL, 0u, 0u, &parent) &&
        prime_need_recording_reserve(group, NULL, 1u, 8192u, &child) &&
        prime_need_receipt_observe_cell(&input, &child, 31u, 7u,
            atom_int(&input, 11), &child);
    CHECK(ready, "metadata-only adoption control has a real retained child event");
    if (!ready) goto done;
    copy = prime_need_recording_copy_new(group);
    session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[3] = {{&input, &destination}};
    ready = copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
        prime_need_recording_copy_pair(copy, 1u, &pairs[2]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 3u) &&
        prime_need_recording_copy_enable(session) &&
        prime_need_recording_copy_prepare(copy, session);
    CHECK(ready, "metadata-only adoption control prepares a complete recording copy");
    if (!ready) goto done;
    CHECK(prime_need_recording_copy_lookup(session, group) == copy,
          "the prepared image initially authenticates current correspondence");
    PrimeNeedRecordingStatus before = prime_need_recording_status(group);
    PrimeNeedRecordingScope scope;
    prime_need_recording_enter(&scope, &parent);
    prime_need_recording_publish(&child);
    prime_need_recording_leave(&scope);
    PrimeNeedRecordingStatus after = prime_need_recording_status(group);
    CHECK(before.nodes == after.nodes && before.payload_charge == after.payload_charge,
          "first-root adoption changes correspondence without allocating a node");
    CHECK(prime_need_recording_copy_lookup(session, group) == NULL,
          "metadata-only publication invalidates the old currentness capability");
    PrimeNeedReceipt old_parent = {0};
    size_t roots = 0u;
    CHECK(prime_need_recording_copy_receipt(copy, &parent, &old_parent) &&
          prime_need_recording_roots(&old_parent, recording_root_count, &roots) && roots == 0u,
          "a captured snapshot remains readable without claiming current correspondence");
done:
    atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    prime_need_recording_release(group);
    arena_free(&destination);
    arena_free(&input);
}

static void check_recording_copy(void) {
    Arena input, holder, image_holder, unheld, image_unheld, later, refused;
    Arena *arenas[] = {&input, &holder, &image_holder, &unheld, &image_unheld, &later, &refused};
    for (size_t i = 0u; i < 7u; i++) arena_init_detached(arenas[i]);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){16u, 524288u, 4u, 2u});
    PrimeNeedRecording *image_group = NULL;
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *session = NULL;
    PrimeNeedReceipt root = {0}, empty = {0}, equal = {0}, left = {0}, right = {0}, legacy = {0};
    Atom *value = atom_expr2(&input, atom_symbol(&input, "Value"), atom_int(&input, 7));
    bool seeded = group && prime_need_recording_reserve(group, NULL, 4u, 131072u, &root);
    empty = root;
    seeded = seeded && prime_need_receipt_observe_cell(&input, &root, 31u, 1u, value, &root) &&
        prime_need_receipt_observe_cell(&input, &empty, 31u, 1u, value, &equal) &&
        prime_need_recording_reserve(group, &root, 4u, 131072u, &left) &&
        prime_need_recording_reserve(group, &root, 4u, 131072u, &right);
    PrimeNeedReceipt left_seed = left;
    seeded = seeded &&
        prime_need_receipt_observe_cell(&input, &left, 31u, 2u, value, &left) &&
        prime_need_receipt_observe_cell(&input, &right, 31u, 3u, value, &right);
    PrimeNeedReceipt joined = left;
    seeded = seeded && prime_need_receipt_merge(&joined, &right) &&
        prime_need_recording_promote(&holder, &joined) &&
        prime_need_receipt_begin(&unheld, &legacy) &&
        prime_need_receipt_observe_cell(&unheld, &legacy, 31u, 9u, value, &legacy);
    PrimeNeedReceipt unrecorded = {.session_id = root.session_id, .owner = root.owner};
    seeded = seeded && prime_need_receipt_observe_cell(
        root.owner, &unrecorded, 31u, 10u, value, &unrecorded);
    CHECK(seeded, "recording copy source has a shared forest and an actual holder");
    if (!seeded) goto done;
    PrimeNeedReceiptEvent root_event = {0}, equal_event = {0};
    CHECK(prime_need_receipt_event_at(&root, 0u, &root_event) &&
        prime_need_receipt_event_at(&equal, 0u, &equal_event) &&
        root.top != equal.top && root_event.after != equal_event.after &&
        atom_eq(root_event.after, equal_event.after),
        "recording source premises distinguish equal payload and frame identities");
    size_t roots = 0u;
    CHECK(!empty.top && prime_need_recording_roots(&empty, recording_root_count, &roots) && roots == 2u,
        "an empty wrapper retains both actual roots of its slice inventory");
    PrimeNeedRecordingScope scope = {0};
    prime_need_recording_enter(&scope, &empty);
    CHECK(prime_need_recording_copy_new(group) == NULL,
        "copying an actively entered recording owner refuses");
    prime_need_recording_leave(&scope);
    copy = prime_need_recording_copy_new(group);
    CHECK(copy && prime_need_recording_copy_pair_count(copy) == 3u &&
        prime_need_recording_copy_take(copy) == NULL,
        "recording copy stages three private slices without publishing an image");
    if (!copy) goto done;
    AtomDeepCopyArenaPair pairs[5] = {{&holder, &image_holder}, {&unheld, &image_unheld}};
    bool pairs_ok = true;
    for (size_t i = 0u; i < 3u; i++) pairs_ok &= prime_need_recording_copy_pair(copy, i, &pairs[i + 2u]);
    AtomDeepCopyArenaPair unchanged = pairs[0];
    CHECK(pairs_ok && !prime_need_recording_copy_pair(copy, 3u, &unchanged) &&
        unchanged.source == pairs[0].source && unchanged.destination == pairs[0].destination,
        "recording pair enumeration preserves output on an invalid index");
    session = atom_deep_copy_session_new(&image_holder);
    CHECK(session && atom_deep_copy_session_set_arena_map(session, pairs, 5u),
        "recording and outside holders share one injective region map");
    if (!session) goto done;
    Atom *outside = atom_deep_copy_session_copy(session, root_event.after);
    bool prepared = prime_need_recording_copy_prepare(copy, session);
    CHECK(prepared && prime_need_recording_copy_prepare(copy, session),
        "the full recording forest prepares once and repeats idempotently");
    if (!prepared) goto done;
    PrimeNeedReceipt copied_root = {0}, copied_equal = {0}, copied_empty = {0}, copied_join = {0};
    CHECK(prime_need_recording_copy_receipt(copy, &root, &copied_root) &&
        prime_need_recording_copy_receipt(copy, &equal, &copied_equal) &&
        prime_need_recording_copy_receipt(copy, &empty, &copied_empty) &&
        prime_need_recording_copy_receipt(copy, &joined, &copied_join),
        "all recorded carriers map into the same prepared image");
    PrimeNeedReceipt repeat = {0};
    CHECK(prime_need_recording_copy_receipt(copy, &root, &repeat) &&
        repeat.top == copied_root.top && copied_root.top != copied_equal.top && copied_root.top != root.top,
        "recording copy preserves shared and equal-distinct physical frame identities");
    PrimeNeedReceipt copied_seed = {0};
    CHECK(left_seed.top == root.top && left_seed.owner != root.owner &&
        prime_need_recording_copy_receipt(copy, &left_seed, &copied_seed) &&
        copied_seed.top == copied_root.top && copied_seed.owner != copied_root.owner,
        "a valid seed retains a different writable slice and the same group-owned prefix");
    PrimeNeedReceipt wrong_slice = root, unchanged_root = copied_root;
    wrong_slice.recording = left.recording;
    CHECK(!prime_need_recording_copy_receipt(copy, &wrong_slice, &unchanged_root) &&
        unchanged_root.top == copied_root.top && unchanged_root.owner == copied_root.owner,
        "a correct owner paired with the wrong slice tag refuses without publication");
    wrong_slice = root;
    wrong_slice.owner = left.owner;
    CHECK(!prime_need_recording_copy_receipt(copy, &wrong_slice, &unchanged_root) &&
        unchanged_root.top == copied_root.top && unchanged_root.recording == copied_root.recording,
        "a correct slice paired with the wrong allocation owner refuses without publication");
    PrimeNeedReceipt legacy_image = legacy;
    CHECK(prime_need_branch_state_copy_with_session(&image_holder, session, &legacy_image) &&
        legacy_image.top != legacy.top && legacy_image.session_id != root.session_id,
        "the same enclosing session can also transport an independent semantic receipt");
    PrimeNeedReceipt mispaired = root, preserved = copied_root;
    mispaired.top = legacy.top;
    CHECK(!prime_need_recording_copy_receipt(copy, &mispaired, &preserved) &&
        preserved.top == copied_root.top && preserved.recording == copied_root.recording,
        "a memoized foreign frame cannot be relabelled as this recording group's root");
    PrimeNeedReceipt unrecorded_image = unrecorded;
    CHECK(prime_need_branch_state_copy_with_session(&image_holder, session, &unrecorded_image) &&
        unrecorded_image.top != unrecorded.top && unrecorded_image.session_id == copied_root.session_id &&
        unrecorded_image.owner == copied_root.owner && !unrecorded_image.recording,
        "an ordinary frame can share the true slice owner and logical ID without joining its recording inventory");
    mispaired = root; mispaired.top = unrecorded.top;
    preserved = copied_root;
    CHECK(!prime_need_recording_copy_receipt(copy, &mispaired, &preserved) &&
        preserved.top == copied_root.top && preserved.recording == copied_root.recording,
        "a correct mapped owner and ID cannot replace explicit recording-inventory membership");
    PrimeNeedReceiptEvent mapped_event = {0}, mapped_equal = {0};
    CHECK(prime_need_receipt_event_at(&copied_root, 0u, &mapped_event) &&
        prime_need_receipt_event_at(&copied_equal, 0u, &mapped_equal) &&
        mapped_event.after == outside && mapped_event.after != mapped_equal.after &&
        mapped_event.after != root_event.after,
        "recording payloads reuse the enclosing memo without coalescing equal source nodes");
    roots = 0u;
    CHECK(!copied_empty.top && prime_need_recording_roots(&copied_empty, recording_root_count, &roots) &&
        roots == 2u, "empty-wrapper transport preserves the whole allocation forest");
    PrimeNeedReceiptGraph before = {0}, after = {0};
    bool graphs = prime_need_receipt_graph_collect(&joined, &before) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        prime_need_receipt_graph_collect(&copied_join, &after) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        before.length == after.length && before.session_id == after.session_id &&
        before.root_id == after.root_id;
    for (size_t i = 0u; graphs && i < before.length; i++) {
        const PrimeNeedReceiptGraphNode *a = &before.nodes[i], *b = &after.nodes[i];
        graphs = a->identity_id == b->identity_id && a->left_id == b->left_id &&
            a->right_id == b->right_id && a->has_event == b->has_event &&
            a->event.event_id == b->event.event_id && a->event.kind == b->event.kind &&
            a->event.thunk_id == b->event.thunk_id &&
            (a->event.after == b->event.after ||
             (a->event.after && b->event.after && atom_eq(a->event.after, b->event.after)));
    }
    CHECK(graphs && before.length == 4u, "recording graph identities and shared causal edges survive exactly");
    prime_need_receipt_graph_free(&before); prime_need_receipt_graph_free(&after);
    CHECK(!prime_need_recording_copy_restore_holder(copy, &unheld, &image_unheld),
        "a mapped carrier without an actual source hold cannot manufacture quota credit");
    CHECK(prime_need_recording_copy_restore_holder(copy, &holder, &image_holder) &&
        prime_need_recording_copy_restore_holder(copy, &holder, &image_holder),
        "an actual holder restores once without spending the logical link twice");
    image_group = prime_need_recording_copy_take(copy);
    CHECK(image_group && image_group != group && !prime_need_recording_copy_take(copy) &&
        !arena_retains_owner(&image_unheld, image_group, prime_need_recording_release),
        "prepared recording ownership transfers exactly once");
    if (!image_group) goto done;
    PrimeNeedRecordingStatus a = prime_need_recording_status(group), b = prime_need_recording_status(image_group);
    CHECK(a.nodes == b.nodes && a.payload_charge == b.payload_charge &&
        a.reserved_nodes == b.reserved_nodes && a.reserved_payload_bytes == b.reserved_payload_bytes &&
        a.owner_links == b.owner_links && b.owner_links == 1u && !b.incomplete,
        "recording preparation retains the paid prefix and remaining logical quotas");
    CettaStorageCensus source_bytes = {0}, image_bytes = {0}, together = {0};
    prime_need_recording_storage(group, &source_bytes); cetta_storage_finish(&source_bytes);
    prime_need_recording_storage(image_group, &image_bytes); cetta_storage_finish(&image_bytes);
    arena_storage_census(&holder, &together); arena_storage_census(&image_holder, &together);
    cetta_storage_finish(&together);
    CHECK(!together.incomplete && together.retained == source_bytes.retained + image_bytes.retained +
        2u * arena_retained_owner_allocation_bytes() && image_bytes.retained <= b.retained_bound,
        "both live copies and both physical owner links are counted separately");
    cetta_storage_census_free(&source_bytes); cetta_storage_census_free(&image_bytes);
    cetta_storage_census_free(&together);
    CHECK(prime_need_recording_promote(&image_holder, &copied_join) &&
        prime_need_recording_status(image_group).owner_links == 1u,
        "future promotion recognizes the restored holder instead of charging it twice");
    PrimeNeedReceipt image_zero = {0}, source_zero = {0};
    CHECK(prime_need_recording_reserve(image_group, NULL, 0u, 0u, &image_zero) &&
        prime_need_recording_reserve(group, NULL, 0u, 0u, &source_zero) &&
        image_zero.session_id == source_zero.session_id,
        "source and image retain independent equal remaining slice reservations");
    CHECK(prime_need_receipt_observe_cell(&input, &image_zero, 31u, 9u, value, &image_zero) &&
        prime_need_recording_status(image_group).omitted == 1u &&
        prime_need_recording_status(group).omitted == 0u,
        "image exhaustion cannot mutate the source recording budget");
    PrimeNeedRecording *fresh = prime_need_recording_new((PrimeNeedRecordingLimits){0u, 0u, 1u, 0u});
    PrimeNeedReceipt fresh_receipt = {0};
    CHECK(fresh && prime_need_recording_reserve(fresh, NULL, 0u, 0u, &fresh_receipt) &&
        fresh_receipt.session_id == legacy.session_id + 1u,
        "copy preparation does not allocate a replacement recording-session identity");
    prime_need_recording_release(fresh);
    atom_deep_copy_session_free(session); session = NULL;
    prime_need_recording_copy_free(copy); copy = NULL;
    arena_free(&holder); prime_need_recording_release(group); group = NULL;
    arena_free(&input);
    CHECK(prime_need_receipt_graph_collect(&copied_join, &after) == PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE &&
        after.incomplete && after.length == 4u &&
        atom_is_symbol(after.nodes[0].event.after->expr.elems[0], "Value"),
        "source retirement preserves the whole copied graph and its subsequent omission status");
    prime_need_receipt_graph_free(&after);
    PrimeNeedReceipt held = copied_join, declined = copied_join;
    CHECK(prime_need_recording_promote(&later, &held) && held.top == copied_join.top &&
        prime_need_recording_status(image_group).owner_links == 2u,
        "a genuinely new holder spends the one remaining logical owner slot");
    CHECK(prime_need_recording_promote(&refused, &declined) && !declined.recording &&
        prime_need_recording_status(image_group).owner_links == 2u &&
        prime_need_recording_status(image_group).incomplete,
        "exhausted copied owner quota omits evidence without consuming semantic data");
done:
    atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    prime_need_recording_release(group); prime_need_recording_release(image_group);
    for (size_t i = 0u; i < 7u; i++) arena_free(arenas[i]);
}

static void check_recording_copy_owner_domain(void) {
    Arena input, first_destination, second_destination;
    Arena *arenas[] = {&input, &first_destination, &second_destination};
    for (size_t i = 0u; i < 3u; i++) arena_init_detached(arenas[i]);
    PrimeNeedRecording *original = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 65536u, 1u, 0u});
    PrimeNeedRecording *independent = NULL;
    PrimeNeedRecordingCopy *first = NULL, *left_copy = NULL, *right_copy = NULL;
    AtomDeepCopySession *first_session = NULL, *both_session = NULL;
    PrimeNeedReceipt left = {0}, right = {0}, left_image = {0}, right_image = {0};
    Atom *value = atom_int(&input, 7);
    bool seeded = original && prime_need_recording_reserve(original, NULL, 4u, 65536u, &left) &&
        prime_need_receipt_observe_cell(&input, &left, 51u, 1u, value, &left);
    CHECK(seeded, "owner-domain control begins with an actual recorded production");
    if (!seeded) goto done;
    first = prime_need_recording_copy_new(original);
    first_session = atom_deep_copy_session_new(&first_destination);
    AtomDeepCopyArenaPair first_pairs[2] = {{&input, &first_destination}, {0}};
    bool copied = first && first_session && prime_need_recording_copy_pair(first, 0u, &first_pairs[1]) &&
        atom_deep_copy_session_set_arena_map(first_session, first_pairs, 2u) &&
        prime_need_recording_copy_prepare(first, first_session) &&
        prime_need_recording_copy_receipt(first, &left, &right);
    if (copied) independent = prime_need_recording_copy_take(first);
    CHECK(copied && independent && independent != original && left.top != right.top &&
        left.owner != right.owner && left.recording != right.recording &&
        left.session_id == right.session_id,
        "a real copy produces distinct owners retaining the exact captured logical identities");
    if (!copied || !independent) goto done;
    atom_deep_copy_session_free(first_session); first_session = NULL;
    prime_need_recording_copy_free(first); first = NULL;
    left_copy = prime_need_recording_copy_new(original);
    right_copy = prime_need_recording_copy_new(independent);
    both_session = atom_deep_copy_session_new(&second_destination);
    AtomDeepCopyArenaPair both_pairs[3] = {{&input, &second_destination}, {0}, {0}};
    bool prepared = left_copy && right_copy && both_session &&
        prime_need_recording_copy_pair(left_copy, 0u, &both_pairs[1]) &&
        prime_need_recording_copy_pair(right_copy, 0u, &both_pairs[2]) &&
        atom_deep_copy_session_set_arena_map(both_session, both_pairs, 3u) &&
        prime_need_recording_copy_prepare(left_copy, both_session) &&
        prime_need_recording_copy_prepare(right_copy, both_session);
    CHECK(prepared, "two independent recording inventories prepare through one enclosing copy session");
    if (!prepared) goto done;
    CHECK(prime_need_recording_copy_receipt(left_copy, &left, &left_image) &&
        prime_need_recording_copy_receipt(right_copy, &right, &right_image) &&
        left_image.top != right_image.top && left_image.owner != right_image.owner &&
        left_image.session_id == right_image.session_id,
        "the common memo preserves both physical ownership domains without renaming logical IDs");
    PrimeNeedReceipt crossed = left, preserved = left_image;
    crossed.top = right.top;
    CHECK(!prime_need_recording_copy_receipt(left_copy, &crossed, &preserved) &&
        preserved.top == left_image.top && preserved.owner == left_image.owner,
        "a tagged root from another equal-ID inventory cannot acquire the left owner's authority");
    crossed = right; preserved = right_image; crossed.top = left.top;
    CHECK(!prime_need_recording_copy_receipt(right_copy, &crossed, &preserved) &&
        preserved.top == right_image.top && preserved.owner == right_image.owner,
        "the right owner likewise refuses a root tagged by another recording copy");
    PrimeNeedReceiptGraph a = {0}, b = {0}, c = {0}, d = {0};
    bool intact = prime_need_receipt_graph_collect(&left, &a) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        prime_need_receipt_graph_collect(&right, &b) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        prime_need_receipt_graph_collect(&left_image, &c) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        prime_need_receipt_graph_collect(&right_image, &d) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        a.event_count == 1u && b.event_count == 1u && c.event_count == 1u && d.event_count == 1u &&
        a.root_id == b.root_id && b.root_id == c.root_id && c.root_id == d.root_id;
    CHECK(intact, "ownership refusals retain all four complete graphs and captured occurrence identities");
    prime_need_receipt_graph_free(&a); prime_need_receipt_graph_free(&b);
    prime_need_receipt_graph_free(&c); prime_need_receipt_graph_free(&d);
    CHECK(prime_need_receipt_observe_cell(&input, &left, 51u, 2u, value, &left) &&
        prime_need_recording_status(original).nodes == 2u &&
        prime_need_recording_status(independent).nodes == 1u,
        "the original remains independently runnable after a crossed-owner refusal");
done:
    atom_deep_copy_session_free(first_session); atom_deep_copy_session_free(both_session);
    prime_need_recording_copy_free(first); prime_need_recording_copy_free(left_copy);
    prime_need_recording_copy_free(right_copy);
    prime_need_recording_release(original); prime_need_recording_release(independent);
    for (size_t i = 0u; i < 3u; i++) arena_free(arenas[i]);
}

static void check_recording_copy_forgetting(void) {
    Arena input, destination, holder, wrong;
    Arena *arenas[] = {&input, &destination, &holder, &wrong};
    for (size_t i = 0u; i < 4u; i++) arena_init_detached(arenas[i]);
    bool input_alive = true, holder_alive = true;
    AtomDeepCopySession *session = NULL, *retaining = NULL, *duplicating = NULL;
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 131072u, 1u, 2u});
    PrimeNeedReceipt receipt = {0}, empty = {0}, retained = {0};
    PrimeNeedSnapshot origin = {0}, allocated = {0}, active = {0}, ready = {0};
    PrimeNeedBranchState branch = {0}, written = {0};
    StateCell cell = {0};
    uint64_t thunk = 0u;
    Atom *value = atom_expr2(&input, atom_symbol(&input, "Kept"), atom_int(&input, 7));
    bool seeded = group && prime_need_recording_reserve(group, NULL, 4u, 131072u, &receipt);
    empty = receipt;
    seeded = seeded && prime_need_receipt_observe_cell(&input, &receipt, 71u, 1u, value, &receipt) &&
        prime_need_snapshot_begin(&origin) &&
        prime_need_snapshot_allocate(&input, &origin, value, &allocated, &thunk) &&
        prime_need_snapshot_start_evaluation(&input, &allocated, thunk, 91u, &active) &&
        prime_need_snapshot_resolve_ready_value(&input, &active, thunk, 91u, value, &ready) &&
        prime_need_branch_state_begin(&input, &branch) &&
        prime_need_branch_state_write(&input, &branch, &cell,
            atom_int(&input, 1), atom_int(&input, 2), &written);
    CHECK(seeded, "forgetting source has actual recorded data, a completed Need cell and a state overlay");
    if (!seeded) goto done;
    Atom *reference = prime_need_ref(&input, &ready, thunk);
    Atom *inspection = prime_need_ref_restrict(&input, reference, &ready,
        CETTA_PRIME_NEED_RIGHT_INSPECT);
    session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pair = {&input, &destination};
    CHECK(session && atom_deep_copy_session_set_arena_map(session, &pair, 1u) &&
        !prime_need_recording_copy_forget(NULL) &&
        prime_need_recording_copy_forget(session) && prime_need_recording_copy_forget(session),
        "an explicit erasing copy policy is idempotent and rejects a missing session");
    if (!session) goto done;
    CHECK(!prime_need_recording_copy_enable(session),
        "an erasing session cannot later become a duplicating session");
    PrimeNeedReceipt erased = receipt, erased_empty = empty;
    CHECK(prime_need_receipt_copy_with_session(&destination, session, &erased) &&
        prime_need_receipt_copy_with_session(&destination, session, &erased_empty) &&
        !erased.top && !erased.recording && !erased.owner && !erased.session_id &&
        !erased_empty.top && !erased_empty.recording && !erased_empty.owner,
        "projection erases both a recorded graph and its empty but owner-bearing slice");
    CHECK(!arena_retains_owner(&destination, group, prime_need_recording_release) &&
        !prime_need_recording_status(group).owner_links && receipt.top && receipt.recording,
        "an erased destination adds no recording hold and leaves the source observer intact");
    PrimeNeedReceipt refused = receipt;
    CHECK(!prime_need_receipt_copy_with_session(&wrong, session, &refused) &&
        refused.top == receipt.top && refused.recording == receipt.recording,
        "wrong destination refusal cannot erase the supplied carrier");
    PrimeNeedSnapshot copied_need = ready;
    PrimeNeedBranchState copied_branch = written;
    Atom *copied_inspection = atom_deep_copy_session_copy(session, inspection);
    bool transported = copied_inspection &&
        prime_need_snapshot_copy_with_session(&destination, session, &copied_need) &&
        prime_need_branch_state_copy_with_session(&destination, session, &copied_branch);
    CHECK(transported && copied_branch.top && copied_need.top &&
        copied_branch.session_id == written.session_id && copied_need.session_id == ready.session_id,
        "erasing observer evidence preserves the operational state and complete Need world");
    if (!transported) goto done;
    CHECK(prime_need_ref_belongs_to_with_rights(copied_inspection, &copied_need,
            CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
        !prime_need_ref_belongs_to_with_rights(copied_inspection, &copied_need,
            CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
        "recording erasure neither loses inspection rights nor creates force permission");
    duplicating = atom_deep_copy_session_new(&wrong);
    CHECK(duplicating && prime_need_recording_copy_enable(duplicating) &&
        !prime_need_recording_copy_forget(duplicating),
        "a duplicating session cannot switch to erasure");
    retaining = atom_deep_copy_session_new(&holder);
    retained = receipt;
    CHECK(retaining && prime_need_receipt_copy_with_session(&holder, retaining, &retained) &&
        arena_retains_owner(&holder, group, prime_need_recording_release),
        "default copying still supplies a separate retained observer");
    CHECK(!prime_need_recording_copy_forget(retaining) &&
        !prime_need_recording_copy_enable(retaining),
        "a session cannot change policy after retaining its first recording carrier");
    atom_deep_copy_session_free(session); session = NULL;
    atom_deep_copy_session_free(retaining); retaining = NULL;
    atom_deep_copy_session_free(duplicating); duplicating = NULL;
    arena_free(&input); input_alive = false;
    prime_need_recording_release(group); group = NULL;
    PrimeNeedReceiptEvent event = {0};
    CHECK(prime_need_receipt_event_at(&retained, 0u, &event) &&
        atom_eq(event.after, atom_expr2(&destination, atom_symbol(&destination, "Kept"),
            atom_int(&destination, 7))),
        "an outside observer retains its graph after the original owner and source arena end");
    arena_free(&holder); holder_alive = false;
    PrimeNeedCellView observed;
    Atom *state_value = NULL;
    CHECK(prime_need_snapshot_lookup(&copied_need, thunk, &observed) &&
        observed.cache_state == PRIME_NEED_CACHE_VALUE &&
        observed.value_role == PRIME_NEED_VALUE_READY &&
        atom_eq(observed.cached, atom_expr2(&destination, atom_symbol(&destination, "Kept"),
            atom_int(&destination, 7))) &&
        prime_need_branch_state_value(&copied_branch, &cell, &state_value) &&
        atom_eq(state_value, atom_int(&destination, 2)) &&
        prime_need_ref_belongs_to_with_rights(copied_inspection, &copied_need,
            CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
        !prime_need_ref_belongs_to_with_rights(copied_inspection, &copied_need,
            CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
        "semantic state and restricted authority outlive the final recording holder");
done:
    atom_deep_copy_session_free(session);
    atom_deep_copy_session_free(retaining);
    atom_deep_copy_session_free(duplicating);
    prime_need_recording_release(group);
    if (holder_alive) arena_free(&holder);
    arena_free(&destination);
    arena_free(&wrong);
    if (input_alive) arena_free(&input);
}

static void check_recording_carrier_copy(void) {
    Arena input, holder, image_holder;
    arena_init_detached(&input);
    arena_init_detached(&holder);
    arena_init_detached(&image_holder);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){1u, 65536u, 1u, 1u});
    PrimeNeedRecordingCopy *copy = NULL;
    PrimeNeedRecording *image = NULL;
    AtomDeepCopySession *session = NULL;
    PrimeNeedReceipt root = {0}, carried = {0}, mapped = {0};
    PrimeNeedReceiptGraph graph = {0};
    bool demoted = false;
    bool ready = group && prime_need_recording_reserve(group, NULL, 1u, 65536u, &root) &&
        prime_need_receipt_observe_cell(&input, &root, 71u, 1u, atom_int(&input, 7), &root);
    CHECK(ready, "carrier-copy control records one actual event");
    if (!ready) goto done;
    carried = root;
    CHECK(prime_need_recording_promote_carrier(&holder, &carried) &&
        arena_retains_owner(&holder, group, prime_need_recording_carrier_release) &&
        !arena_retains_owner(&holder, group, prime_need_recording_release),
        "execution carrier owns metadata without a reader lease");
    copy = prime_need_recording_copy_new(group);
    session = atom_deep_copy_session_new(&image_holder);
    AtomDeepCopyArenaPair pairs[2] = {{&holder, &image_holder}, {0}};
    ready = copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 2u) &&
        prime_need_recording_copy_enable(session) &&
        prime_need_recording_copy_prepare(copy, session);
    CHECK(ready, "carrier copy prepares one physical recording image");
    if (!ready) goto done;
    CHECK(prime_need_recording_copy_restore_holder(copy, &holder, &image_holder),
        "the actual carrier holder restores through the common owner map");
    mapped = carried;
    CHECK(prime_need_receipt_copy_carrier_with_session(&image_holder, session, &mapped),
        "a restored metadata holder admits the copied carrier");
    image = prime_need_recording_copy_take(copy);
    CHECK(image && arena_retains_owner(&image_holder, image, prime_need_recording_carrier_release) &&
        !arena_retains_owner(&image_holder, image, prime_need_recording_release) &&
        prime_need_recording_status(image).owner_links == 1u &&
        prime_need_receipt_graph_collect(&mapped, &graph) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        graph.length == 1u,
        "copy preserves the carrier role, its paid owner link and recorded occurrence");
    prime_need_receipt_graph_free(&graph);
    if (!image) goto done;
    prime_need_recording_demote(image);
    demoted = true;
    CHECK(!prime_need_receipt_present(&mapped) && prime_need_receipt_present(&root) &&
        !prime_need_recording_status(image).payload_charge,
        "forgetting the copy releases its payload without revoking the original recorder");
    CHECK(prime_need_recording_promote(&image_holder, &mapped) && !mapped.recording,
        "a copied carrier cannot resurrect forgotten payload");
done:
    prime_need_receipt_graph_free(&graph);
    atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    arena_free(&image_holder);
    if (demoted) prime_need_recording_carrier_release(image);
    else prime_need_recording_release(image);
    arena_free(&holder);
    prime_need_recording_release(group);
    arena_free(&input);
}

static void check_recording_copy_mode(void) {
    Arena input, destination, holder, image_holder, other, other_image, external;
    Arena *arenas[] = {&input, &destination, &holder, &image_holder,
                      &other, &other_image, &external};
    for (size_t i = 0u; i < 7u; i++) arena_init_detached(arenas[i]);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){8u, 131072u, 2u, 3u});
    PrimeNeedRecording *foreign = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 65536u, 1u, 1u});
    PrimeNeedRecording *image_group = NULL;
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *ordinary = NULL, *session = NULL;
    PrimeNeedReceipt root = {0}, foreign_root = {0}, semantic = {0}, mapped = {0};
    Atom *value = atom_expr2(&input, atom_symbol(&input, "Kept"), atom_int(&input, 7));
    bool seeded = group && foreign &&
        prime_need_recording_reserve(group, NULL, 4u, 65536u, &root) &&
        prime_need_receipt_observe_cell(&input, &root, 71u, 1u, value, &root) &&
        prime_need_recording_reserve(foreign, NULL, 4u, 65536u, &foreign_root) &&
        prime_need_receipt_observe_cell(&input, &foreign_root, 72u, 1u, value, &foreign_root) &&
        prime_need_receipt_begin(&input, &semantic) &&
        prime_need_receipt_observe_cell(&input, &semantic, 73u, 1u, value, &semantic);
    CHECK(seeded, "copy-mode control has recorded and ordinary semantic receipts");
    if (!seeded) goto done;
    ordinary = atom_deep_copy_session_new(&holder);
    PrimeNeedReceipt retained = root;
    CHECK(ordinary && prime_need_receipt_copy_with_session(&holder, ordinary, &retained) &&
        retained.top == root.top && retained.recording == root.recording &&
        arena_retains_owner(&holder, group, prime_need_recording_release) &&
        prime_need_recording_status(group).owner_links == 1u,
        "ordinary receipt copying retains its actual recording owner and pays one link");
    copy = prime_need_recording_copy_new(group);
    session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[5] = {{&input, &destination}, {&holder, &image_holder},
        {&other, &other_image}, {&external, &external}, {0}};
    bool ready = copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[4]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 5u) &&
        prime_need_recording_copy_enable(session) && prime_need_recording_copy_enable(session);
    CHECK(ready, "explicit recording-copy mode is idempotent on the captured common session");
    if (!ready) goto done;
    PrimeNeedReceipt refused = root;
    CHECK(!prime_need_receipt_copy_with_session(&destination, session, &refused) &&
        refused.top == root.top && refused.recording == root.recording &&
        !arena_retains_owner(&destination, group, prime_need_recording_release) &&
        prime_need_recording_status(group).owner_links == 1u,
        "enabled copy mode refuses an unprepared group without retaining its source");
    ready = prime_need_recording_copy_prepare(copy, session);
    CHECK(ready, "recorded payload preparation precedes carrier publication");
    if (!ready) goto done;
    CHECK(!prime_need_receipt_copy_with_session(&destination, session, &refused) &&
        refused.top == root.top,
        "a complete payload map alone supplies no destination enclosing lifetime");
    CHECK(!prime_need_recording_copy_enclosing_owner(copy, &input, &other_image) &&
        !prime_need_recording_copy_enclosing_owner(copy, &external, &external),
        "wrong and borrowed identity pairs cannot grant enclosing ownership");
    CHECK(prime_need_recording_copy_enclosing_owner(copy, &input, &destination) &&
        prime_need_recording_copy_enclosing_owner(copy, &input, &destination) &&
        !arena_retains_owner(&input, group, prime_need_recording_release),
        "a direct enclosing group owner registers its private pair without a paid arena link");
    static const char other_kind;
    CHECK(atom_deep_copy_session_object_original(session, copy, &destination, sizeof(Arena)) == &input &&
        !atom_deep_copy_session_object_original(session, copy, &destination, 1u) &&
        !atom_deep_copy_session_object_original(session, &other_kind, &destination, sizeof(Arena)) &&
        !atom_deep_copy_session_object_original(session, copy, &other_image, sizeof(Arena)),
        "the common inverse table distinguishes physical owner, kind and extent");
    mapped = root;
    CHECK(prime_need_receipt_copy_with_session(&destination, session, &mapped) &&
        mapped.top != root.top && mapped.recording != root.recording &&
        mapped.owner == pairs[4].destination && mapped.session_id == root.session_id,
        "an explicitly owned carrier receives the exact prepared recording image");
    PrimeNeedReceipt semantic_image = semantic;
    CHECK(prime_need_receipt_copy_with_session(&destination, session, &semantic_image) &&
        !semantic_image.recording && semantic_image.top != semantic.top &&
        semantic_image.session_id == semantic.session_id,
        "the same mode keeps ordinary semantic receipt graph copying intact");
    refused = foreign_root;
    CHECK(!prime_need_receipt_copy_with_session(&destination, session, &refused) &&
        refused.top == foreign_root.top && refused.recording == foreign_root.recording &&
        prime_need_recording_status(foreign).owner_links == 0u,
        "another unregistered recording owner refuses instead of using ordinary retention");
    AtomDeepCopyDestinationScope scope = {0};
    bool entered = atom_deep_copy_destination_enter(session, &other_image, &scope);
    refused = root;
    CHECK(entered && !prime_need_receipt_copy_with_session(&other_image, session, &refused) &&
        refused.top == root.top && atom_deep_copy_destination_leave(&scope),
        "a different valid destination still needs its own enclosing or retained ownership");
    (void)atom_deep_copy_destination_leave(&scope);
    entered = atom_deep_copy_destination_enter(session, &image_holder, &scope);
    refused = root;
    CHECK(entered && !prime_need_receipt_copy_with_session(&image_holder, session, &refused) &&
        prime_need_recording_copy_restore_holder(copy, &holder, &image_holder) &&
        prime_need_receipt_copy_with_session(&image_holder, session, &refused) &&
        refused.top == mapped.top && atom_deep_copy_destination_leave(&scope),
        "an actual paid holder restores its distinct physical link before carrier copying");
    (void)atom_deep_copy_destination_leave(&scope);
    image_group = prime_need_recording_copy_take(copy);
    CHECK(image_group && !arena_retains_owner(&destination, image_group, prime_need_recording_release) &&
        arena_retains_owner(&image_holder, image_group, prime_need_recording_release) &&
        prime_need_recording_status(image_group).owner_links == 1u,
        "TAKE supplies the direct owner while restored links retain their original logical charge");
    refused = root;
    CHECK(!prime_need_receipt_copy_with_session(&destination, session, &refused) &&
        refused.top == root.top,
        "carrier copies finish before the prepared image leaves its copy capability");
    retained = root;
    CHECK(prime_need_receipt_promote_with_session(&destination, session, &retained) &&
        retained.top == root.top && retained.recording == root.recording &&
        arena_retains_owner(&destination, group, prime_need_recording_release) &&
        !arena_retains_owner(&destination, image_group, prime_need_recording_release) &&
        prime_need_recording_status(group).owner_links == 2u &&
        prime_need_recording_status(image_group).owner_links == 1u,
        "ordinary promotion in a copy-enabled session still retains the original owner");
    retained = root;
    CHECK(prime_need_receipt_copy_with_session(&holder, ordinary, &retained) &&
        retained.top == root.top && retained.recording == root.recording &&
        prime_need_recording_status(group).owner_links == 2u,
        "a separate ordinary session keeps its existing recording ownership");
    atom_deep_copy_session_free(ordinary); ordinary = NULL;
    atom_deep_copy_session_free(session); session = NULL;
    prime_need_recording_copy_free(copy); copy = NULL;
    arena_free(&holder); arena_free(&destination);
    prime_need_recording_release(group); group = NULL;
    arena_free(&input);
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_graph_collect(&mapped, &graph) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        graph.event_count == 1u && atom_is_symbol(graph.nodes[0].event.after->expr.elems[0], "Kept"),
        "the taken enclosing owner keeps copied payload alive after all original holders retire");
    prime_need_receipt_graph_free(&graph);
done:
    atom_deep_copy_session_free(ordinary); atom_deep_copy_session_free(session);
    prime_need_recording_copy_free(copy);
    prime_need_recording_release(group); prime_need_recording_release(image_group);
    prime_need_recording_release(foreign);
    for (size_t i = 0u; i < 7u; i++) arena_free(arenas[i]);
}

static void check_recording_copy_mode_invalidation(bool reset_destination) {
    Arena input, destination;
    arena_init_detached(&input); arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 65536u, 1u, 1u});
    PrimeNeedRecordingCopy *copy = NULL;
    AtomDeepCopySession *session = NULL;
    PrimeNeedReceipt source = {0};
    Atom *value = atom_int(&input, 7);
    bool ready = group && prime_need_recording_reserve(group, NULL, 4u, 65536u, &source) &&
        prime_need_receipt_observe_cell(&input, &source, 74u, 1u, value, &source);
    copy = ready ? prime_need_recording_copy_new(group) : NULL;
    session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[2] = {{&input, &destination}, {0}};
    ready = copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 2u) &&
        prime_need_recording_copy_prepare(copy, session) &&
        prime_need_recording_copy_enclosing_owner(copy, &input, &destination) &&
        prime_need_recording_copy_enable(session);
    CHECK(ready, "invalidation control starts with a fully prepared owned copy mode");
    if (!ready) goto done;
    PrimeNeedReceipt image = source;
    CHECK(prime_need_receipt_copy_with_session(&destination, session, &image) &&
        image.top != source.top,
        "the unchanged captured group and owner admit carrier transport");
    PrimeNeedReceipt captured = source;
    if (reset_destination) arena_reset(&destination, arena_mark(&destination));
    else ready = prime_need_receipt_observe_cell(&input, &source, 74u, 2u, value, &source);
    PrimeNeedRecordingStatus before = prime_need_recording_status(group);
    PrimeNeedReceipt refused = captured;
    CHECK(ready && !prime_need_receipt_copy_with_session(&destination, session, &refused) &&
        refused.top == captured.top && refused.recording == captured.recording &&
        !prime_need_recording_copy_enclosing_owner(copy, &input, &destination),
        "changed capture or reset destination refuses without publishing another receipt");
    PrimeNeedRecordingStatus after = prime_need_recording_status(group);
    CHECK(before.nodes == after.nodes && before.payload_charge == after.payload_charge &&
        before.owner_links == after.owner_links && before.omitted == after.omitted &&
        !arena_retains_owner(&destination, group, prime_need_recording_release),
        "invalidated copy mode never falls back to spending or retaining the original owner");
    if (reset_destination)
        CHECK(!atom_deep_copy_session_object_original(session, copy, &destination, sizeof(Arena)),
            "inverse metadata lookup rejects reset allocation authority");
    else
        CHECK(atom_deep_copy_session_object_original(session, copy, &destination, sizeof(Arena)) == &input,
            "a valid physical map cannot hide changed recording progress");
    atom_deep_copy_session_free(session); session = NULL;
    prime_need_recording_copy_free(copy); copy = NULL;
    CHECK(prime_need_receipt_observe_cell(&input, &source, 74u, 3u, value, &source) &&
        prime_need_recording_status(group).nodes == before.nodes + 1u,
        "the original remains runnable after invalidated preparation is discarded");
done:
    atom_deep_copy_session_free(session); prime_need_recording_copy_free(copy);
    prime_need_recording_release(group); arena_free(&input); arena_free(&destination);
}

static void check_recording_copy_refusal(unsigned mode) {
    Arena input, destination;
    arena_init_detached(&input); arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 65536u, 1u, 0u});
    PrimeNeedReceipt source = {0}, output = {0};
    Atom *value = atom_int(&input, 7);
    bool seeded = group && prime_need_recording_reserve(group, NULL, 4u, 65536u, &source) &&
        prime_need_receipt_observe_cell(&input, &source, 41u, 1u, value, &source);
    PrimeNeedRecordingCopy *copy = seeded ? prime_need_recording_copy_new(group) : NULL;
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[2] = {{&input, &destination}, {0}};
    bool mapped = copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]);
    Arena *staged_destination = pairs[1].destination;
    if (mode == 1u) pairs[1].destination = (Arena *)pairs[1].source;
    mapped = mapped && atom_deep_copy_session_set_arena_map(session, pairs, mode == 0u ? 1u : 2u);
    CHECK(mapped, "recording refusal has an actual source and the declared candidate region map");
    if (!mapped) goto done;
    if (mode == 2u) arena_reset(staged_destination, arena_mark(staged_destination));
    bool changed = mode != 3u || prime_need_receipt_observe_cell(
        &input, &source, 41u, 2u, value, &source);
    PrimeNeedRecordingStatus before = prime_need_recording_status(group);
    output = source;
    CHECK(changed && !prime_need_recording_copy_prepare(copy, session) &&
        !prime_need_recording_copy_take(copy),
        "missing, borrowed, reset and changed recording authorities refuse preparation");
    PrimeNeedRecordingStatus after = prime_need_recording_status(group);
    CHECK(!prime_need_recording_copy_receipt(copy, &source, &output) &&
        output.top == source.top && output.owner == source.owner && output.recording == source.recording &&
        before.nodes == after.nodes && before.payload_charge == after.payload_charge &&
        before.omitted == after.omitted && before.owner_links == after.owner_links,
        "refusal preserves the original receipt, spent quota and output slot");
    CHECK(prime_need_receipt_observe_cell(&input, &source, 41u, 3u, value, &source) &&
        prime_need_recording_status(group).nodes == before.nodes + 1u,
        "an original recording remains runnable after copy refusal");
    atom_deep_copy_session_free(session); session = NULL;
    prime_need_recording_copy_free(copy); copy = NULL;
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_graph_collect(&source, &graph) == PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        graph.event_count == (mode == 3u ? 3u : 2u),
        "discarding failed preparation leaves the full source history valid");
    prime_need_receipt_graph_free(&graph);
done:
    atom_deep_copy_session_free(session); prime_need_recording_copy_free(copy);
    prime_need_recording_release(group);
    arena_free(&input); arena_free(&destination);
}

static void check_recording_copy_zero(void) {
    Arena source_arena, destination;
    arena_init_detached(&source_arena); arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new((PrimeNeedRecordingLimits){0u, 0u, 1u, 0u});
    PrimeNeedReceipt source = {0}, output = {0};
    bool seeded = group && prime_need_recording_reserve(group, NULL, 0u, 0u, &source) &&
        prime_need_receipt_observe_cell(&source_arena, &source, 41u, 1u,
            atom_int(&source_arena, 7), &source);
    PrimeNeedRecordingCopy *copy = seeded ? prime_need_recording_copy_new(group) : NULL;
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    AtomDeepCopyArenaPair pairs[2] = {{&source_arena, &destination}, {0}};
    bool prepared = copy && session && prime_need_recording_copy_pair(copy, 0u, &pairs[1]) &&
        atom_deep_copy_session_set_arena_map(session, pairs, 2u) &&
        prime_need_recording_copy_prepare(copy, session);
    CHECK(prepared && prime_need_recording_copy_receipt(copy, &source, &output) &&
        !output.top && output.recording != source.recording && output.session_id == source.session_id,
        "enabled capacity zero copies a real retained slice with the same identity");
    PrimeNeedRecording *image = prime_need_recording_copy_take(copy);
    PrimeNeedRecordingStatus status = prime_need_recording_status(image);
    CHECK(image && status.nodes == 0u && status.payload_charge == 0u &&
        status.incomplete && status.omitted == 1u,
        "empty recorded payload preserves its omitted prefix instead of becoming complete");
    atom_deep_copy_session_free(session); prime_need_recording_copy_free(copy);
    prime_need_recording_release(group); group = NULL;
    PrimeNeedReceiptGraph graph = {0};
    CHECK(image && prime_need_receipt_graph_collect(&output, &graph) == PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE &&
        graph.length == 0u && graph.incomplete,
        "the empty incomplete image remains independently owned after source retirement");
    prime_need_receipt_graph_free(&graph); prime_need_recording_release(image);
    arena_free(&source_arena); arena_free(&destination);
}

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool start;
} RecordingStart;

typedef struct {
    RecordingStart *start;
    PrimeNeedReceipt receipt;
    PrimeNeedReceipt captured;
    Atom *value;
    Arena destination;
    bool ok;
} RecordingWorker;

static void *recording_worker(void *raw) {
    RecordingWorker *worker = raw;
    pthread_mutex_lock(&worker->start->mutex);
    while (!worker->start->start)
        pthread_cond_wait(&worker->start->changed, &worker->start->mutex);
    pthread_mutex_unlock(&worker->start->mutex);
    PrimeNeedReceipt allocation = worker->receipt;
    PrimeNeedRecordingScope scope;
    prime_need_recording_enter(&scope, &allocation);
    worker->receipt = worker->captured;
    worker->ok = true;
    for (uint64_t i = 0u; i < 2u; i++)
        worker->ok &= prime_need_receipt_observe_cell(&worker->destination,
            &worker->receipt, 31u, 100u + i, worker->value, &worker->receipt);
    worker->ok &= prime_need_receipt_promote(
        &worker->destination, &worker->receipt);
    prime_need_recording_leave(&scope);
    return NULL;
}

static void check_recording_private_slices(void) {
    Arena source;
    arena_init_detached(&source);
    Atom *value = atom_int(&source, 7);
    for (unsigned i = 0u; i < 8u; i++) {
        Atom *children[] = {value, atom_int(&source, (int64_t)i)};
        value = atom_expr(&source, children, 2u);
    }
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){8u, 196608u, 3u, 2u});
    PrimeNeedReceipt parent = {0};
    RecordingStart start = {PTHREAD_MUTEX_INITIALIZER,
                            PTHREAD_COND_INITIALIZER, false};
    RecordingWorker workers[2] = {{.start = &start, .value = value},
                                  {.start = &start, .value = value}};
    for (unsigned i = 0u; i < 2u; i++)
        arena_init_detached(&workers[i].destination);
    bool reserved = group &&
        prime_need_recording_reserve(group, NULL, 4u, 65536u, &parent) &&
        prime_need_receipt_observe_cell(&source, &parent, 31u, 1u,
                                       atom_int(&source, 9), &parent) &&
        prime_need_recording_reserve(group, &parent, 2u, 65536u,
                                    &workers[0].receipt) &&
        prime_need_recording_reserve(group, &parent, 2u, 65536u,
                                    &workers[1].receipt);
    CHECK(reserved, "coordinator reserves bounded private worker slices before dispatch");
    if (!reserved) goto done;
    workers[0].captured = parent;
    workers[1].captured = parent;
    pthread_t threads[2];
    unsigned started = 0u;
    while (started < 2u &&
           pthread_create(&threads[started], NULL, recording_worker,
                          &workers[started]) == 0)
        started++;
    pthread_mutex_lock(&start.mutex);
    start.start = true;
    pthread_cond_broadcast(&start.changed);
    pthread_mutex_unlock(&start.mutex);
    for (unsigned i = 0u; i < started; i++)
        pthread_join(threads[i], NULL);
    CHECK(started == 2u && workers[0].ok && workers[1].ok,
          "private slices copy immutable shared inputs and publish owned roots concurrently");
    if (started != 2u) goto done;
    PrimeNeedReceiptGraph left = {0}, right = {0};
    CHECK(prime_need_receipt_graph_collect(&workers[0].receipt, &left) ==
              PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              prime_need_receipt_graph_collect(&workers[1].receipt, &right) ==
              PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              left.event_count == 3u && right.event_count == 3u &&
              left.nodes[0].identity_id == right.nodes[0].identity_id &&
              left.nodes[1].identity_id != right.nodes[1].identity_id,
          "worker histories share one prefix and keep equal deliveries as distinct occurrences");
    prime_need_receipt_graph_free(&left);
    prime_need_receipt_graph_free(&right);
    CHECK(prime_need_receipt_merge(&parent, &workers[0].receipt) &&
              prime_need_receipt_merge(&parent, &workers[1].receipt) &&
              prime_need_receipt_graph_collect(&parent, &left) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              left.length == 7u && left.event_count == 5u,
          "coordinator joins worker packets after their private writes stop");
    prime_need_receipt_graph_free(&left);
    CettaStorageCensus storage = {0};
    arena_storage_census(&workers[0].destination, &storage);
    arena_storage_census(&workers[1].destination, &storage);
    cetta_storage_finish(&storage);
    PrimeNeedRecordingStatus status = prime_need_recording_status(group);
    CHECK(!storage.incomplete && !status.incomplete && status.nodes == 7u &&
              storage.retained <= status.retained_bound &&
              status.payload_charge <= status.reserved_payload_bytes &&
              status.owner_links == 2u,
          "concurrent recording obeys the combined transitive payload and owner-link bounds");
    cetta_storage_census_free(&storage);
    arena_free(&source);
    prime_need_recording_release(group);
    group = NULL;
    arena_free(&workers[0].destination);
    CHECK(prime_need_receipt_graph_collect(&parent, &left) ==
              PRIME_NEED_RECEIPT_GRAPH_COMPLETE && left.event_count == 5u &&
              left.nodes[0].event.after->ground.ival == 9 &&
              left.nodes[1].event.after->kind == ATOM_EXPR,
          "last worker destination retains the full joined graph after source and sibling retirement");
    prime_need_receipt_graph_free(&left);
done:
    prime_need_recording_release(group);
    for (unsigned i = 0u; i < 2u; i++)
        arena_free(&workers[i].destination);
    arena_free(&source);
    pthread_cond_destroy(&start.changed);
    pthread_mutex_destroy(&start.mutex);
}

typedef struct {
    PrimeNeedReceipt roots[4];
    size_t count;
} RecordingRoots;

static bool recording_root(const PrimeNeedReceipt *root, void *raw) {
    RecordingRoots *roots = raw;
    if (roots->count == 4u) return false;
    roots->roots[roots->count++] = *root;
    return true;
}

static void check_recording_scopes(void) {
    Arena arena;
    arena_init_detached(&arena);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u, 16384u, 2u, 0u});
    PrimeNeedReceipt parent = {0}, slice = {0}, left = {0}, right = {0};
    CHECK(group && prime_need_recording_reserve(group, NULL, 1u, 4096u, &parent) &&
        prime_need_receipt_observe_cell(&arena, &parent, 41u, 1u,
            atom_int(&arena, 2), &parent) &&
        prime_need_recording_reserve(group, &parent, 3u, 12288u, &slice),
        "recording scopes reserve private authority independently of captured roots");
    if (!group || !slice.recording) goto done;
    PrimeNeedRecordingScope outer, inner;
    prime_need_recording_enter(&outer, &slice);
    CHECK(prime_need_receipt_observe_cell(&arena, &parent, 41u, 2u,
            atom_int(&arena, 3), &left) && left.recording == slice.recording &&
            left.owner == slice.owner && parent.recording != left.recording,
        "a captured parent root cannot redirect append into its old writer slice");
    CHECK(prime_need_receipt_observe_cell(&arena, &parent, 41u, 2u,
            atom_int(&arena, 3), &right) && left.top != right.top,
        "equal branch results have distinct recorded terminal roots");
    RecordingRoots roots = {0};
    CHECK(prime_need_recording_roots(&slice, recording_root, &roots) &&
            roots.count == 2u && roots.roots[0].top == left.top &&
            roots.roots[1].top == right.top,
        "slice forest preserves branch roots in allocation order without joining worlds");
    PrimeNeedReceipt missing = {0}, omitted = left;
    prime_need_recording_enter(&inner, &missing);
    PrimeNeedReceipt suppressed_merge = left;
    CHECK(prime_need_receipt_merge(&suppressed_merge, &right) &&
            !suppressed_merge.top && !suppressed_merge.recording &&
            prime_need_recording_status(group).nodes == 3u,
        "a refused nested observer suppresses merging without spending its parent's quota");
    CHECK(prime_need_receipt_observe_cell(&arena, &left, 41u, 3u,
            atom_int(&arena, 4), &omitted) && !omitted.top && !omitted.recording &&
            prime_need_recording_status(group).nodes == 3u,
        "failed nested recording reservation cannot fall back to unbounded copying");
    prime_need_recording_leave(&inner);
    CHECK(g_prime_need_recording_scope == &slice &&
            prime_need_receipt_observe_cell(&arena, &left, 41u, 4u,
                atom_int(&arena, 5), &left),
        "nested recording scope restores the prior private authority");
    roots = (RecordingRoots){0};
    CHECK(prime_need_recording_roots(&slice, recording_root, &roots) &&
            roots.count == 2u && roots.roots[0].top == right.top &&
            roots.roots[1].top == left.top,
        "terminal root inventory removes only a root extended within its private slice");
    prime_need_recording_leave(&outer);
    prime_need_recording_leave(&outer);
    CHECK(!g_prime_need_recording_scope,
        "recording scope restoration is idempotent after normal or early exit");
    PrimeNeedReceipt retained = left;
    size_t nodes = prime_need_recording_status(group).nodes;
    CHECK(prime_need_receipt_merge(&retained, &parent) &&
            retained.top == left.top && retained.recording == left.recording &&
            prime_need_recording_status(group).nodes == nodes,
        "ordinary merging preserves a carried recorded ancestor after observer exit");
done:
    prime_need_recording_release(group);
    arena_free(&arena);
}

static void check_recording_refusal_boundaries(void) {
    Arena source, destination;
    arena_init_detached(&source);
    arena_init_detached(&destination);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){3u, 12288u, 3u, 2u});
    PrimeNeedRecording *other = prime_need_recording_new(
        (PrimeNeedRecordingLimits){1u, 4096u, 1u, 0u});
    PrimeNeedReceipt first = {0}, foreign = {0}, output = {0}, legacy = {0};
    CHECK(group && other &&
              prime_need_recording_reserve(group, NULL, 1u, 4096u, &first) &&
              prime_need_recording_reserve(other, NULL, 1u, 4096u, &foreign),
          "independent recording owners start with distinct authority");
    if (!group || !other) goto done;
    output = first;
    CHECK(!prime_need_recording_reserve(group, &foreign, 1u, 4096u, &output) &&
              output.recording == first.recording && output.top == first.top &&
              output.session_id == first.session_id && output.owner == first.owner,
          "foreign seed refusal leaves the exact output and reservation available");
    CHECK(prime_need_receipt_begin(&source, &legacy) &&
              !prime_need_recording_reserve(group, &legacy, 1u, 4096u, &output) &&
              output.recording == first.recording,
          "an earlier legacy recording needs an explicit capture boundary");
    CHECK(!prime_need_recording_reserve(group, &first, 3u, 4096u, &output) &&
              !prime_need_recording_reserve(group, &first, 1u, 12288u, &output) &&
              prime_need_recording_status(group).reserved_nodes == 1u &&
              prime_need_recording_status(group).reserved_payload_bytes == 4096u,
          "refused grants do not consume node or byte reservations");
    CHECK(prime_need_receipt_observe_cell(&source, &first, 31u, 1u,
              atom_int(&source, 5), &first),
          "the admitted final recording slot retains its event");
    const PrimeNeedReceiptFrame *saved = first.top;
    size_t charged = prime_need_recording_status(group).payload_charge;
    CHECK(prime_need_receipt_observe_cell(&source, &first, 31u, 2u,
              atom_int(&source, 6), &first) &&
              prime_need_receipt_observe_cell(&source, &first, 31u, 3u,
              atom_int(&source, 7), &first) && first.top == saved &&
              prime_need_recording_status(group).nodes == 1u &&
              prime_need_recording_status(group).payload_charge == charged &&
              prime_need_recording_status(group).omitted == 2u,
          "capacity exhaustion is sticky and repeated omissions allocate no payload");
    PrimeNeedReceipt cyclic = {0};
    Atom cycle = {.kind = ATOM_EXPR, .expr.len = 1u};
    Atom *edge = &cycle;
    cycle.expr.elems = &edge;
    CHECK(prime_need_recording_reserve(group, NULL, 1u, 4096u, &cyclic) &&
              prime_need_receipt_observe_cell(&source, &cyclic, 31u, 4u,
                  &cycle, &cyclic) && !cyclic.top &&
              prime_need_recording_status(group).payload_charge == charged,
          "cyclic payload admission stops at its byte bound before canonical copying");
    PrimeNeedReceipt opaque = {0};
    Atom resource = {.kind = ATOM_GROUNDED, .ground.gkind = GV_SPACE};
    CHECK(prime_need_recording_reserve(group, NULL, 1u, 4096u, &opaque) &&
              prime_need_receipt_observe_cell(&source, &opaque, 31u, 5u,
                  &resource, &opaque) && !opaque.top &&
              prime_need_recording_status(group).payload_charge == charged,
          "an opaque resource is omitted without pinning a semantic owner");
    CHECK(prime_need_receipt_merge(&first, &foreign) && first.top == saved &&
              !foreign.top && prime_need_recording_status(group).omitted == 5u,
          "a foreign recording merge preserves both roots and remains incomplete");
    ArenaMark mark = arena_mark(&destination);
    PrimeNeedReceipt held = first;
    CHECK(prime_need_receipt_promote(&destination, &held) &&
              prime_need_receipt_promote(&destination, &held) &&
              prime_need_recording_status(group).owner_links == 1u,
          "nested reuse of one arena owns exactly one recording link");
    arena_reset(&destination, mark);
    CHECK(!prime_need_receipt_promote(&destination, &held) &&
              (held = first, prime_need_receipt_promote(&destination, &held)) &&
              prime_need_recording_status(group).owner_links == 2u,
          "rollback revokes the reader and fresh borrowing readmits a reserved owner link");
    CHECK(prime_need_recording_new((PrimeNeedRecordingLimits){0u, SIZE_MAX, 1u, 0u}) == NULL &&
              prime_need_recording_new((PrimeNeedRecordingLimits){0u, 0u, SIZE_MAX, 0u}) == NULL &&
              prime_need_recording_new((PrimeNeedRecordingLimits){0u, 0u, 1u, SIZE_MAX}) == NULL,
          "recording metadata and payload bound overflow is refused before allocation");
    Arena empty_owner;
    arena_init_detached(&empty_owner);
    PrimeNeedRecording *empty_group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){0u, 0u, 1u, 1u});
    PrimeNeedReceipt empty = {0};
    bool empty_owned = empty_group &&
        prime_need_recording_reserve(empty_group, NULL, 0u, 0u, &empty) &&
        prime_need_receipt_promote(&empty_owner, &empty);
    CHECK(empty_owned, "an enabled empty graph still owns its recording metadata");
    CettaStorageCensus empty_storage = {0};
    arena_storage_census(&empty_owner, &empty_storage);
    cetta_storage_finish(&empty_storage);
    PrimeNeedRecordingStatus empty_status = prime_need_recording_status(empty_group);
    CHECK(empty_owned && !empty_storage.incomplete && !empty_status.incomplete &&
              empty_status.payload_charge == 0u &&
              empty_storage.retained == empty_status.metadata_bound &&
              empty_storage.retained == empty_status.retained_bound,
          "zero-payload bound includes exact group and destination-link metadata");
    cetta_storage_census_free(&empty_storage);
    prime_need_recording_release(empty_group);
    arena_free(&empty_owner);
done:
    prime_need_recording_release(group);
    prime_need_recording_release(other);
    arena_free(&destination);
    arena_free(&source);
}

static void check_bounded_recording(void) {
    Arena source, first, second, refused;
    arena_init_detached(&source);
    arena_init_detached(&first);
    arena_init_detached(&second);
    arena_init_detached(&refused);
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){8u, 65536u, 4u, 2u});
    CHECK(group != NULL, "bounded recording has an independently owned allocation group");
    if (!group) goto done;
    PrimeNeedReceipt root = {0}, left = {0}, right = {0}, zero = {0};
    CHECK(prime_need_recording_reserve(group, NULL, 3u, 24000u, &root),
          "coordinator reserves the root recording slice");
    CHECK(prime_need_receipt_observe_cell(&source, &root, 31u, 1u,
              atom_int(&source, 7), &root) &&
              prime_need_recording_reserve(group, &root, 2u, 16000u, &left) &&
              prime_need_recording_reserve(group, &root, 2u, 16000u, &right),
          "private slices share the immutable prefix without copying it");
    CHECK(prime_need_receipt_observe_cell(&source, &left, 31u, 2u,
              atom_int(&source, 3), &left) &&
              prime_need_receipt_observe_cell(&source, &right, 31u, 3u,
              atom_int(&source, 6), &right),
          "each private slice records its own delivery");
    PrimeNeedReceipt joined = left;
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_merge(&joined, &right) &&
              prime_need_receipt_graph_collect(&joined, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              graph.length == 4u && graph.event_count == 3u,
          "bounded recording uses the existing shared-prefix join DAG");
    prime_need_receipt_graph_free(&graph);
    PrimeNeedReceipt a = joined, b = joined;
    CHECK(prime_need_receipt_promote(&first, &a) &&
              prime_need_receipt_promote(&second, &b) &&
              a.top == joined.top && b.top == joined.top,
          "recording promotion retains the same frame addresses");
    CHECK(prime_need_receipt_promote(&first, &a) &&
              prime_need_recording_status(group).owner_links == 2u,
          "repeated promotion to one destination creates no new owner link");
    CettaStorageCensus payload = {0}, owners = {0};
    prime_need_recording_storage(group, &payload);
    cetta_storage_finish(&payload);
    arena_storage_census(&first, &owners);
    arena_storage_census(&second, &owners);
    cetta_storage_finish(&owners);
    PrimeNeedRecordingStatus status = prime_need_recording_status(group);
    CHECK(!payload.incomplete && !owners.incomplete &&
              owners.retained == payload.retained +
                  2u * arena_retained_owner_allocation_bytes() &&
              owners.retained <= status.retained_bound,
          "census counts one shared graph and two separate destination links");
    cetta_storage_census_free(&payload);
    cetta_storage_census_free(&owners);
    CHECK(prime_need_recording_reserve(group, &left, 0u, 0u, &zero),
          "enabled zero-capacity recording is a retained slice");
    const PrimeNeedReceiptFrame *left_root = zero.top, *right_root = right.top;
    CHECK(prime_need_receipt_merge(&zero, &right) && zero.top == left_root &&
              right.top == right_root &&
              prime_need_recording_status(group).omitted == 1u,
          "merge refusal preserves both roots and records no semantic refusal");
    CHECK(prime_need_receipt_graph_collect(&zero, &graph) ==
              PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE && graph.event_count == 2u &&
              prime_need_receipt_graph_validate(&graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE,
          "a valid retained prefix cannot claim complete recording");
    prime_need_receipt_graph_free(&graph);
    PrimeNeedReceipt c = joined;
    CHECK(prime_need_receipt_promote(&refused, &c) &&
              !prime_need_receipt_present(&c) && joined.top == a.top &&
              prime_need_recording_status(group).omitted == 2u,
          "owner-link refusal drops only the optional destination evidence");
    arena_free(&source);
    prime_need_recording_release(group);
    group = NULL;
    arena_free(&first);
    CHECK(prime_need_receipt_graph_collect(&b, &graph) ==
              PRIME_NEED_RECEIPT_GRAPH_INCOMPLETE && graph.event_count == 3u &&
              graph.nodes[0].event.after->ground.ival == 7,
          "a retained destination keeps bounded payload after source and first owner retire");
    prime_need_receipt_graph_free(&graph);
done:
    prime_need_recording_release(group);
    arena_free(&source);
    arena_free(&first);
    arena_free(&second);
    arena_free(&refused);

    Arena semantic;
    arena_init_detached(&semantic);
    PrimeNeedSnapshot origin = {0}, cell = {0}, running = {0}, x = {0}, y = {0};
    uint64_t id = 0u;
    CHECK(prime_need_snapshot_begin(&origin) &&
              prime_need_snapshot_allocate(&semantic, &origin,
                  atom_symbol(&semantic, "recorded-choice"), &cell, &id) &&
              prime_need_snapshot_start_evaluation(&semantic, &cell, id, 11u, &running) &&
              prime_need_snapshot_resolve_value(&semantic, &running, id, 11u,
                  atom_int(&semantic, 3), &x) &&
              prime_need_snapshot_resolve_value(&semantic, &running, id, 11u,
                  atom_int(&semantic, 6), &y),
          "semantic alternatives own incompatible completed caches");
    PrimeNeedSnapshot tried = x;
    CHECK(!prime_need_snapshot_merge(&tried, &y),
          "semantic cache conflict is decided without recording");
    group = prime_need_recording_new((PrimeNeedRecordingLimits){0u, 0u, 2u, 0u});
    PrimeNeedReceipt rx = {0}, ry = {0};
    CHECK(group && prime_need_recording_reserve(group, NULL, 0u, 0u, &rx) &&
              prime_need_recording_reserve(group, NULL, 0u, 0u, &ry) &&
              prime_need_receipt_observe_cached_cell(&semantic, &rx, &x, id,
                  true, false, atom_int(&semantic, 3), &rx) &&
              prime_need_receipt_observe_cached_cell(&semantic, &ry, &y, id,
                  true, false, atom_int(&semantic, 6), &ry),
          "zero-capacity histories retain omissions for both cached outcomes");
    tried = x;
    CHECK(prime_need_receipt_compatible(&rx, &ry) &&
              !prime_need_snapshot_merge(&tried, &y) &&
              prime_need_recording_status(group).nodes == 0u,
          "omitted observations neither authorize nor reject incompatible semantic worlds");
    prime_need_recording_release(group);

    group = prime_need_recording_new((PrimeNeedRecordingLimits){2u, 4096u, 1u, 0u});
    PrimeNeedReceipt large = {0};
    char bytes[8192];
    memset(bytes, 'x', sizeof(bytes));
    CHECK(group && prime_need_recording_reserve(group, NULL, 2u, 4096u, &large) &&
              prime_need_receipt_observe_cell(&semantic, &large, 31u, 9u,
                  atom_string_n(&semantic, bytes, sizeof(bytes)), &large) &&
              prime_need_recording_status(group).nodes == 0u &&
              prime_need_recording_status(group).payload_charge == 0u &&
              prime_need_recording_status(group).incomplete,
          "oversized payload is refused before copying or retaining its source");
    prime_need_recording_release(group);
    arena_free(&semantic);
}

static void check_cache_production_identity(Arena *arena, Arena *promoted_arena) {
    PrimeNeedSnapshot root = {0}, cell = {0}, running = {0};
    PrimeNeedSnapshot left = {0}, right = {0}, faulted = {0};
    uint64_t thunk_id = 0u, left_id = 0u, right_id = 0u, fault_id = 0u;
    Atom *origin = atom_symbol(arena, "production-origin");
    Atom *value = atom_int(arena, 3);
    CHECK(prime_need_snapshot_begin(&root) &&
              prime_need_snapshot_allocate(arena, &root, origin, &cell, &thunk_id),
          "production identity starts from an actual allocated cell");
    uint64_t unchanged = UINT64_MAX;
    CHECK(!prime_need_snapshot_cached_update_id(&cell, thunk_id, &unchanged) &&
              unchanged == UINT64_MAX,
          "empty cell does not invent a production or change the refused destination");
    CHECK(prime_need_snapshot_start_evaluation(arena, &cell, thunk_id, 51u, &running) &&
              !prime_need_snapshot_cached_update_id(&running, thunk_id, &unchanged) &&
              unchanged == UINT64_MAX,
          "in-flight producer is not a completed update");
    CHECK(prime_need_snapshot_resolve_value(arena, &running, thunk_id, 51u, value, &left) &&
              prime_need_snapshot_resolve_value(arena, &running, thunk_id, 51u, value, &right) &&
              prime_need_snapshot_resolve_stable_fault(
                  arena, &running, thunk_id, 51u, origin, &faulted) &&
              prime_need_snapshot_cached_update_id(&left, thunk_id, &left_id) &&
              prime_need_snapshot_cached_update_id(&right, thunk_id, &right_id) &&
              prime_need_snapshot_cached_update_id(&faulted, thunk_id, &fault_id),
          "value and stable-fault returns have actual completed update identities");
    PrimeNeedCellView left_cell, right_cell;
    CHECK(left_id != 0u && left_id != right_id && left_id != fault_id &&
              prime_need_snapshot_lookup(&left, thunk_id, &left_cell) &&
              prime_need_snapshot_lookup(&right, thunk_id, &right_cell) &&
              left_cell.storage_key == right_cell.storage_key &&
              atom_eq(left_cell.cached, right_cell.cached),
          "equal alternative values at the same cell are different productions");
    PrimeNeedSnapshot promoted = left;
    uint64_t promoted_id = 0u;
    CHECK(prime_need_snapshot_promote(promoted_arena, &promoted) &&
              prime_need_snapshot_cached_update_id(&promoted, thunk_id, &promoted_id) &&
              promoted_id == left_id,
          "whole snapshot promotion preserves the cache update identity");
    Arena young;
    arena_init(&young);
    PrimeNeedSnapshot young_return = {0};
    uint64_t young_id = 0u;
    bool suffix_ok = prime_need_snapshot_resolve_value(
        &young, &running, thunk_id, 51u, value, &young_return) &&
        prime_need_snapshot_cached_update_id(&young_return, thunk_id, &young_id) &&
        prime_need_snapshot_promote_suffix(promoted_arena, &young, &young_return);
    arena_free(&young);
    CHECK(suffix_ok &&
              prime_need_snapshot_cached_update_id(&young_return, thunk_id, &promoted_id) &&
              young_id == promoted_id && young_id != left_id,
          "suffix promotion retains production identity after source arena retirement");
    PrimeNeedSnapshot retried = {0}, rerunning = {0}, returned = {0};
    uint64_t returned_id = 0u;
    CHECK(prime_need_snapshot_retry_evaluation(arena, &running, thunk_id, 51u, &retried) &&
              !prime_need_snapshot_cached_update_id(&retried, thunk_id, &unchanged) &&
              unchanged == UINT64_MAX &&
              prime_need_snapshot_start_evaluation(arena, &retried, thunk_id, 52u, &rerunning) &&
              prime_need_snapshot_resolve_value(arena, &rerunning, thunk_id, 52u, value, &returned) &&
              prime_need_snapshot_cached_update_id(&returned, thunk_id, &returned_id) &&
              returned_id != left_id,
          "retry has no completed producer and its later completion is fresh");
    PrimeNeedSnapshot rejected = running;
    CHECK(!prime_need_snapshot_resolve_value(arena, &running, thunk_id, 999u, value, &rejected) &&
              rejected.top == running.top &&
              !prime_need_snapshot_cached_update_id(&rejected, thunk_id, &unchanged),
          "wrong evaluating owner cannot manufacture a producer identity");

    PrimeNeedReceipt receipt = {0};
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_begin(arena, &receipt) &&
              prime_need_receipt_observe_cached_cell(
                  arena, &receipt, &left, thunk_id, true, false, value, &receipt) &&
              prime_need_receipt_observe_cached_cell(
                  arena, &receipt, &left, thunk_id, false, false, value, &receipt) &&
              prime_need_receipt_graph_collect(&receipt, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 2u,
          "one production and two deliveries use the existing receipt path");
    CHECK(graph.length == 2u && graph.nodes[0].event.producer_completed &&
              !graph.nodes[1].event.producer_completed &&
              graph.nodes[0].event.producer_update_id == left_id &&
              graph.nodes[1].event.producer_update_id == left_id &&
              graph.nodes[0].identity_id != graph.nodes[1].identity_id,
          "cache delivery names its actual producer without repeating production");
    PrimeNeedReceipt saved_receipt = receipt;
    CHECK(!prime_need_receipt_observe_cached_cell(
              arena, &receipt, &running, thunk_id, true, false, value, &receipt) &&
              receipt.top == saved_receipt.top,
          "in-flight state refuses a claimed completed delivery transactionally");
    PrimeNeedReceipt late = {0};
    CHECK(prime_need_receipt_begin(arena, &late) &&
              prime_need_receipt_observe_cached_cell(
                  arena, &late, &left, thunk_id, false, false, value, &late) &&
              prime_need_receipt_graph_collect(&late, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 1u &&
              !graph.nodes[0].event.producer_completed &&
              graph.nodes[0].event.producer_update_id == left_id,
          "late recording retains an external producer reference without inventing a firing");
    graph.nodes[0].event.producer_update_id = 0u;
    graph.nodes[0].event.producer_completed = true;
    CHECK(prime_need_receipt_graph_validate(&graph) == PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "a completion without a named cache update is structurally invalid");
    CHECK(prime_need_receipt_promote(promoted_arena, &receipt) &&
              prime_need_receipt_graph_collect(&receipt, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 2u &&
              graph.nodes[0].event.producer_update_id == left_id &&
              graph.nodes[0].event.producer_completed &&
              !graph.nodes[1].event.producer_completed,
          "receipt promotion preserves production versus delivery evidence");
    prime_need_receipt_graph_free(&graph);
}

static void check_recording_admission(void) {
    Arena source, retained;
    arena_init_detached(&source);
    arena_init_detached(&retained);
    Atom *coefficient = atom_int(&source, 2);
    Atom *verdicts[] = {atom_symbol(&source, "True"),
        atom_symbol(&source, "False"), atom_symbol(&source, "Maybe")};
    PrimeNeedReceiptEventKind kinds[] = {PRIME_NEED_RECEIPT_ADMISSION_TRUE,
        PRIME_NEED_RECEIPT_ADMISSION_FALSE, PRIME_NEED_RECEIPT_ADMISSION_PENDING};
    PrimeNeedRecording *group = prime_need_recording_new(
        (PrimeNeedRecordingLimits){3u, 65536u, 1u, 1u});
    PrimeNeedReceipt receipt = {0};
    CHECK(group && prime_need_recording_reserve(group, NULL, 3u, 65536u, &receipt),
          "admission recording reserves its own bounded payload owner");
    for (size_t i = 0u; i < 3u; i++)
        CHECK(prime_need_receipt_admission(&source, &receipt, kinds[i], 71u,
                  coefficient, verdicts[i], &receipt),
              "each actual admission role retains a separate verdict occurrence");
    PrimeNeedReceipt unchanged = receipt;
    CHECK(!prime_need_receipt_admission(&source, &receipt,
              PRIME_NEED_RECEIPT_RESAMPLE, 71u, coefficient, verdicts[0], &receipt) &&
              receipt.top == unchanged.top,
          "a non-admission role refuses without replacing the retained graph");
    CHECK(prime_need_receipt_promote(&retained, &receipt),
          "admission payloads transfer an owner link before source retirement");
    prime_need_recording_release(group);
    arena_free(&source);
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_graph_collect(&receipt, &graph) ==
              PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.event_count == 3u,
          "all examined verdicts survive source and initial recorder retirement");
    if (graph.length == 3u) {
        for (size_t i = 0u; i < 3u; i++)
            CHECK(graph.nodes[i].event.kind == kinds[i] &&
                      graph.nodes[i].event.source_occurrence_id == 71u &&
                      atom_eq(graph.nodes[i].event.before, atom_int(&retained, 2)) &&
                      atom_eq(graph.nodes[i].event.after, atom_symbol(&retained,
                          i == 0u ? "True" : i == 1u ? "False" : "Maybe")) &&
                      !graph.nodes[i].event.producer_completed &&
                      !graph.nodes[i].event.producer_update_id,
                  "guard observations preserve coefficient and origin without inventing a production");
    } else CHECK(false, "admission graph keeps three chronological nodes");
    prime_need_receipt_graph_free(&graph);
    arena_free(&retained);

    arena_init_detached(&source);
    coefficient = atom_int(&source, 2);
    Atom *no = atom_symbol(&source, "False");
    group = prime_need_recording_new((PrimeNeedRecordingLimits){0u, 0u, 1u, 0u});
    CHECK(group && prime_need_recording_reserve(group, NULL, 0u, 0u, &receipt) &&
              prime_need_receipt_admission(&source, &receipt,
                  PRIME_NEED_RECEIPT_ADMISSION_FALSE, 71u, coefficient, no, &receipt) &&
              !receipt.top && prime_need_recording_status(group).incomplete &&
              prime_need_recording_status(group).omitted == 1u,
          "missing guard evidence remains incomplete instead of certifying no examined verdict");
    prime_need_recording_release(group);
    arena_free(&source);
}

static void check_receipt_graph(Arena *arena, Arena *promoted_arena,
                                Atom *value) {
    PrimeNeedReceipt receipt = {0};
    PrimeNeedReceiptGraph graph = {0};
    CHECK(prime_need_receipt_graph_collect(&receipt, &graph) ==
              PRIME_NEED_RECEIPT_GRAPH_ABSENT && graph.length == 0u &&
              graph.session_id == 0u,
          "graph absence is an explicit observation status");
    CHECK(prime_need_receipt_begin(arena, &receipt) &&
              prime_need_receipt_graph_collect(&receipt, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              graph.session_id == receipt.session_id &&
              graph.length == 0u && graph.root_id == 0u,
          "captured empty graph is complete and distinct from absence");
    CHECK(prime_need_receipt_observe_cell(
              arena, &receipt, 91u, 3u, value, &receipt),
          "graph producer support is constructed by the receipt service");
    PrimeNeedReceipt left;
    PrimeNeedReceipt right;
    CHECK(prime_need_receipt_use_equation_ids(
              arena, &receipt, 17u, 2u, &left) &&
              prime_need_receipt_use_equation_ids(
                  arena, &receipt, 17u, 2u, &right),
          "equal rule uses are distinct receipt occurrences");
    PrimeNeedReceipt joined = left;
    CHECK(prime_need_receipt_merge(&joined, &right) &&
              prime_need_receipt_graph_collect(&joined, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              graph.length == 4u && graph.event_count == 3u,
          "graph exposes common support once, both uses and the join");
    if (graph.length != 4u) {
        prime_need_receipt_graph_free(&graph);
        return;
    }
    CHECK(graph.nodes[0].left_id == 0u && graph.nodes[0].right_id == 0u &&
              graph.nodes[0].has_event &&
              graph.nodes[1].left_id == graph.nodes[0].identity_id &&
              graph.nodes[2].left_id == graph.nodes[0].identity_id &&
              graph.nodes[1].identity_id != graph.nodes[2].identity_id &&
              graph.nodes[1].event.rule_occurrence_id == 2u &&
              graph.nodes[2].event.rule_occurrence_id == 2u,
          "shared predecessor and equal firing multiplicity survive projection");
    CHECK(!graph.nodes[3].has_event &&
              graph.nodes[3].left_id == graph.nodes[1].identity_id &&
              graph.nodes[3].right_id == graph.nodes[2].identity_id &&
              graph.root_id == graph.nodes[3].identity_id,
          "join identity and both exact predecessor edges remain visible");

    PrimeNeedReceiptGraphNode nodes[4];
    memcpy(nodes, graph.nodes, sizeof(nodes));
    PrimeNeedReceiptGraph changed = graph;
    changed.nodes = nodes;
    nodes[2].identity_id = nodes[1].identity_id;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "duplicate node identity cannot certify a complete graph");
    memcpy(nodes, graph.nodes, sizeof(nodes));
    nodes[1].left_id = UINT64_MAX;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "missing predecessor refuses graph completion");
    nodes[1].left_id = graph.root_id;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "backward chronological edge refuses a causal cycle");
    memcpy(nodes, graph.nodes, sizeof(nodes));
    nodes[2].event.event_id = nodes[1].event.event_id;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "event identity cannot be substituted independently of its node");
    memcpy(nodes, graph.nodes, sizeof(nodes));
    changed.event_count = 2u;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "incorrect event total cannot certify completion");
    changed.event_count = graph.event_count;
    changed.root_id = nodes[1].identity_id;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "a partial root cannot claim all retained nodes");
    changed.root_id = graph.root_id;
    nodes[1].left_id = 0u;
    nodes[2].left_id = 0u;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID,
          "orphaned retained support cannot masquerade as a rooted graph");
    nodes[2].left_id = graph.nodes[0].identity_id;
    CHECK(prime_need_receipt_graph_validate(&changed) ==
              PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              nodes[1].left_id != graph.nodes[1].left_id,
          "structural validity alone does not authenticate a removed sharing edge");
    PrimeNeedReceiptGraphNode *retained_nodes = graph.nodes;
    uint64_t retained_root = graph.root_id;
    PrimeNeedReceipt wrong_session = joined;
    wrong_session.session_id++;
    CHECK(prime_need_receipt_graph_collect(&wrong_session, &graph) ==
              PRIME_NEED_RECEIPT_GRAPH_INVALID &&
              graph.nodes == retained_nodes && graph.length == 4u &&
              graph.root_id == retained_root,
          "invalid capture preserves the entire prior graph view");

    PrimeNeedReceipt promoted = joined;
    PrimeNeedReceiptGraph promoted_graph = {0};
    CHECK(prime_need_receipt_promote(promoted_arena, &promoted) &&
              prime_need_receipt_graph_collect(&promoted, &promoted_graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              promoted_graph.length == graph.length &&
              promoted_graph.root_id == graph.root_id &&
              promoted_graph.session_id == graph.session_id,
          "promotion retains the graph session and root occurrence");
    bool same_edges = promoted_graph.length == graph.length;
    for (size_t i = 0u; same_edges && i < graph.length; i++) {
        const PrimeNeedReceiptGraphNode *a = &graph.nodes[i];
        const PrimeNeedReceiptGraphNode *b = &promoted_graph.nodes[i];
        same_edges = a->identity_id == b->identity_id &&
                     a->left_id == b->left_id && a->right_id == b->right_id &&
                     a->has_event == b->has_event;
    }
    CHECK(same_edges && atom_eq(promoted_graph.nodes[0].event.after, value),
          "promoted graph preserves every edge and borrowed value meaning");
    prime_need_receipt_graph_free(&promoted_graph);

    PrimeNeedReceipt copied_base = receipt;
    PrimeNeedReceipt copied_right;
    PrimeNeedReceipt copied_join = left;
    CHECK(prime_need_receipt_promote(promoted_arena, &copied_base) &&
              prime_need_receipt_use_equation_ids(
                  promoted_arena, &copied_base, 17u, 2u, &copied_right) &&
              prime_need_receipt_merge(&copied_join, &copied_right) &&
              prime_need_receipt_graph_collect(&copied_join, &graph) ==
                  PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
              graph.length == 4u && graph.event_count == 3u,
          "two storage copies of inherited support remain one graph occurrence");
    prime_need_receipt_graph_free(&graph);
    CHECK(graph.nodes == NULL && graph.length == 0u && graph.session_id == 0u,
          "graph release clears its owned view without releasing receipt payloads");
    prime_need_receipt_graph_free(&graph);
    CHECK(prime_need_receipt_graph_collect(&copied_join, &graph) ==
              PRIME_NEED_RECEIPT_GRAPH_COMPLETE && graph.length == 4u,
          "view release and repeated collection preserve the original receipt");
    prime_need_receipt_graph_free(&graph);
}
#endif

static void check_branch_shared_prefix_compatibility(void) {
    const unsigned lengths[] = {0u, 1u, 2u, 17u};
    for (size_t n = 0u; n < sizeof(lengths) / sizeof(*lengths); n++) {
        for (unsigned kind = 0u; kind < 5u; kind++) {
            for (unsigned mode = 0u; mode < 3u; mode++) {
                Arena source, destination;
                arena_init_detached(&source);
                arena_init_detached(&destination);
                StateCell state = {0}, a = {0}, b = {0};
                PrimeNeedBranchState common = {0}, left = {0}, right = {0};
                bool made = prime_need_branch_state_begin(&source, &common);
                unsigned writes = lengths[n];
                for (unsigned i = 0u; made && i < writes; i++)
                    made = prime_need_branch_state_write(&source, &common,
                        &state, atom_int(&source, i), atom_int(&source, i + 1u), &common);
                Atom *value = atom_int(&source, writes);
                Atom *next = atom_int(&source, writes + 1u);
                if (kind == 0u || kind == 1u) {
                    made = made && prime_need_branch_state_read(&source, &common,
                        kind ? &state : &a, value, &left) &&
                        prime_need_branch_state_read(&source, &common,
                            kind ? &state : &b, value, &right);
                } else {
                    made = made && prime_need_branch_state_write(&source, &common,
                        kind == 4u ? &a : &state, value, next, &left);
                    made = made && (kind == 3u
                        ? prime_need_branch_state_read(&source, &common, &state, value, &right)
                        : prime_need_branch_state_write(&source, &common,
                            kind == 4u ? &b : &state, value, next, &right));
                }
                CHECK(made && left.top != right.top &&
                          prime_need_branch_state_is_ancestor(&common, &left) &&
                          prime_need_branch_state_is_ancestor(&common, &right),
                      "distinct branch extensions inherit the same actual write prefix");
                if (mode == 1u)
                    CHECK(prime_need_branch_state_promote(&destination, &right),
                          "a separately promoted branch retains its logical occurrences");
                if (mode == 2u) {
                    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
                    AtomDeepCopyArenaPair pair = {&source, &destination};
                    CHECK(session && atom_deep_copy_session_set_arena_map(session, &pair, 1u) &&
                              prime_need_branch_state_copy_with_session(&destination, session, &left) &&
                              prime_need_branch_state_copy_with_session(&destination, session, &right),
                          "owned branch copies share the complete retained prefix graph");
                    atom_deep_copy_session_free(session);
                    arena_free(&source);
                }
                PrimeNeedBranchState old_left = left, old_right = right, joined = left;
                bool expected = kind == 0u || kind == 1u || kind == 4u;
                bool merged = prime_need_branch_state_merge(&joined, &right);
                CHECK(prime_need_branch_state_compatible(&left, &right) == expected &&
                          prime_need_branch_state_compatible(&right, &left) == expected &&
                          merged == expected,
                      "only branch-specific hazards conflict, including equal-payload independent writes");
                CHECK(left.top == old_left.top && left.owner == old_left.owner &&
                          left.session_id == old_left.session_id &&
                          right.top == old_right.top && right.owner == old_right.owner &&
                          right.session_id == old_right.session_id,
                      "compatibility and merge preserve both original branch carriers");
                CHECK(merged
                          ? prime_need_branch_state_is_ancestor(&left, &joined) &&
                            prime_need_branch_state_is_ancestor(&right, &joined)
                          : joined.top == left.top && joined.owner == left.owner &&
                            joined.session_id == left.session_id,
                      "accepted joins retain both parents and refused joins retain the original root");
                if (expected && (writes || kind == 1u)) {
                    Atom *actual = NULL;
                    CHECK(prime_need_branch_state_value(&joined, &state, &actual) &&
                              atom_eq(actual, atom_int(&destination, writes)),
                          "joining extensions retains the final common-prefix state value");
                }
                if (mode != 2u) arena_free(&source);
                arena_free(&destination);
            }
        }
    }
}

/* A returned value may name the completed producer it directly forwards.
 * The same lookup postcondition holds for numbered and named cells. */
static void check_forwarded_production(Arena *arena, Arena *promoted_arena) {
    for (unsigned named = 0u; named < 2u; named++) {
        PrimeNeedSnapshot root = {0}, source, evaluating, produced, allocated, running;
        PrimeNeedSnapshot forwarded, ordinary;
        uint64_t producer = 0u, consumer = 0u;
        uint64_t evaluator = prime_need_fresh_evaluator_id();
        Atom *origin = atom_symbol(arena, "forwarded-source");
        Atom *value = atom_expr2(arena, atom_symbol(arena, "Returned"), atom_int(arena, 7));
        CHECK(prime_need_snapshot_begin(&root), "forwarded producer starts a fresh world");
        bool ready;
        if (named) {
            producer = PRIME_NEED_NAMED_CELL_BIT | UINT64_C(1001);
            ready = prime_need_snapshot_allocate_named(arena, &root, origin, producer, &source);
        } else {
            ready = prime_need_snapshot_allocate(arena, &root, origin, &source, &producer);
        }
        CHECK(ready && prime_need_snapshot_start_evaluation(arena, &source, producer,
                  evaluator, &evaluating) &&
              prime_need_snapshot_resolve_value(arena, &evaluating, producer,
                  evaluator, value, &produced), "both named and numbered producers complete");
        CHECK(prime_need_snapshot_allocate(arena, &produced, origin, &allocated, &consumer) &&
              prime_need_snapshot_start_evaluation(arena, &allocated, consumer,
                  evaluator, &running) &&
              prime_need_snapshot_resolve_forwarded_value(arena, &running, consumer,
                  evaluator, atom_deep_copy(arena, value), producer, &forwarded),
              "a direct return retains the actual completed source update");
        PrimeNeedCellView cell, selected;
        CHECK(prime_need_snapshot_lookup(&forwarded, consumer, &cell) &&
              cell.value_role == PRIME_NEED_VALUE_DEMAND_RESULT &&
              cell.value_producer_thunk == producer && cell.value_producer_update &&
              prime_need_snapshot_forwarded_value_source(&forwarded, consumer, &selected) &&
              atom_eq(selected.cached, cell.cached),
              "the lookup returns the selected source without changing the return role");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
        uint64_t update = 0u;
        CHECK(prime_need_snapshot_cached_update_id(&forwarded, producer, &update) &&
              update == cell.value_producer_update,
              "indexed and named-log lookup return the same exact frame update");
#endif
        PrimeNeedCellView untouched;
        memset(&untouched, 0xA7, sizeof(untouched));
        PrimeNeedCellView before = untouched;
        CHECK(!prime_need_snapshot_forwarded_value_source(&forwarded, producer, &untouched) &&
              !memcmp(&before, &untouched, sizeof(before)) &&
              !prime_need_snapshot_forwarded_value_source(&running, consumer, &untouched) &&
              !memcmp(&before, &untouched, sizeof(before)),
              "absent forwarding and unfinished returns leave the output untouched");
        CHECK(prime_need_snapshot_resolve_value(arena, &running, consumer,
                  evaluator, value, &ordinary) &&
              !prime_need_snapshot_forwarded_value_source(&ordinary, consumer, &untouched) &&
              !memcmp(&before, &untouched, sizeof(before)),
              "equal value syntax does not invent a forwarded producer");
        PrimeNeedSnapshot refused = ordinary;
        CHECK(!prime_need_snapshot_resolve_forwarded_value(arena, &running, consumer,
                  evaluator + 1u, value, producer, &refused) &&
              !memcmp(&refused, &ordinary, sizeof(refused)),
              "forwarding retains the evaluating-owner check and refused destination");
        PrimeNeedSnapshot image = forwarded;
        CHECK(prime_need_snapshot_promote(promoted_arena, &image) &&
              prime_need_snapshot_forwarded_value_source(&image, consumer, &selected) &&
              atom_eq(selected.cached, value),
              "snapshot promotion retains forwarded production lookup");
    }
}

typedef struct {
    StateCell *cells[3];
    Atom *values[3];
    unsigned calls, refuse_at;
} StateImportView;

static CettaMatchCompletion state_import_initial(void *raw, StateCell *cell, Atom **out) {
    StateImportView *view = raw;
    view->calls++;
    if (view->refuse_at && view->calls == view->refuse_at) return CETTA_MATCH_INCOMPLETE;
    for (size_t i = 0u; i < 3u; i++) if (cell == view->cells[i]) {
        *out = view->values[i]; return CETTA_MATCH_MATCHED;
    }
    return CETTA_MATCH_INCOMPLETE;
}

static bool state_import_number(const PrimeNeedBranchState *state, StateCell *cell, int64_t expected) {
    Atom *value = NULL;
    return prime_need_branch_state_lookup_checked(state, cell, &value) == CETTA_MATCH_MATCHED &&
        value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT &&
        value->ground.ival == expected;
}

static void check_state_production_import(void) {
    Arena source, receiving, persistent;
    arena_init_detached(&source); arena_init_detached(&receiving); arena_init_detached(&persistent);
    StateCell a = {.value=atom_int(&persistent,0)}, b = {.value=atom_int(&persistent,0)},
              c = {.value=atom_int(&persistent,0)};
    StateImportView view = {.cells={&a,&b,&c}, .values={a.value,b.value,c.value}};
    PrimeNeedBranchState common = {0}, entrance = {0}, left = {0}, right = {0},
        production = {0}, recipient = {0}, imported = {0};
    bool ready = prime_need_branch_state_begin(&source,&common) &&
        prime_need_branch_state_write(&source,&common,&a,a.value,atom_int(&source,1),&entrance) &&
        prime_need_branch_state_write(&source,&common,&a,a.value,atom_int(&source,2),&recipient) &&
        prime_need_branch_state_write(&source,&entrance,&b,b.value,atom_int(&source,1),&left) &&
        prime_need_branch_state_write(&source,&entrance,&c,c.value,atom_int(&source,1),&right);
    CHECK(ready, "State producer and recipient retain distinct earlier a writes");
    if (!ready) goto done;
    PrimeNeedBranchState no_state_result={0};
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&entrance,
        &recipient,state_import_initial,&view,&no_state_result)==CETTA_MATCH_MATCHED &&
        no_state_result.top==recipient.top && state_import_number(&no_state_result,&a,2),
        "a row with no post-entrance State event preserves a different recipient branch");
    PrimeNeedBranchState absent_recipient={0};
    no_state_result=recipient;
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&entrance,
        &absent_recipient,state_import_initial,&view,&no_state_result)==CETTA_MATCH_MATCHED &&
        !no_state_result.top && !no_state_result.session_id && !no_state_result.owner,
        "a row with no post-entrance State event preserves an absent recipient world");
    PrimeNeedBranchState foreign_root={0},foreign_recipient={0},foreign_result={0};
    CHECK(prime_need_branch_state_begin(&source,&foreign_root) &&
        prime_need_branch_state_write(&source,&foreign_root,&a,a.value,atom_int(&source,4),
            &foreign_recipient) && foreign_recipient.session_id!=common.session_id &&
        prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&entrance,
            &foreign_recipient,state_import_initial,&view,&foreign_result)==CETTA_MATCH_MATCHED &&
        foreign_result.top==foreign_recipient.top && state_import_number(&foreign_result,&a,4),
        "a row with no post-entrance State event preserves a foreign recipient session");
    production = left;
    CHECK(prime_need_branch_state_merge(&production,&right),
        "the produced suffix has independent b and c branches joined above the entrance");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&production,&recipient,
        state_import_initial,&view,&imported) == CETTA_MATCH_MATCHED,
        "qualified State graft transports the actual post-entrance diamond");
    CHECK(state_import_number(&imported,&a,2) && state_import_number(&imported,&b,1) &&
        state_import_number(&imported,&c,1), "producer a=1 cannot overwrite recipient a=2");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    /* Operational State grafts preserve canonical events under new physical
     * node identities. The current receipt graph has a stricter identity
     * invariant, so treating this carrier as a recorded receipt is invalid. */
    PrimeNeedReceiptGraph state_as_receipt = {0};
    CHECK(prime_need_receipt_graph_collect(&imported,&state_as_receipt) ==
        PRIME_NEED_RECEIPT_GRAPH_INVALID,
        "a rebased operational State DAG is not yet a receipt-graph transport");
    prime_need_receipt_graph_free(&state_as_receipt);
#endif
    PrimeNeedBranchState absent = {0}, absent_result = {0}, malformed = {.owner=&source};
    CHECK(prime_need_branch_state_import_suffix(&receiving,&absent,&absent,&absent,&absent,
        state_import_initial,&view,&absent_result) == CETTA_MATCH_MATCHED &&
        !absent_result.top && !absent_result.session_id && !absent_result.owner,
        "a producer without State preserves the absent receiver world");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&absent,&absent,&absent,&recipient,
        state_import_initial,&view,&absent_result) == CETTA_MATCH_MATCHED &&
        absent_result.top == recipient.top && state_import_number(&absent_result,&a,2),
        "a producer without State preserves the receiver's unrelated writes");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&malformed,&absent,&absent,&recipient,
        state_import_initial,&view,&absent_result) == CETTA_MATCH_INCOMPLETE &&
        absent_result.top == recipient.top,
        "an uninitialized owner is not an absent State world");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&absent,&absent,&left,&recipient,
        state_import_initial,&view,&absent_result) == CETTA_MATCH_INCOMPLETE &&
        absent_result.top == recipient.top,
        "an absent entrance cannot authorize a produced State write");
    CHECK(prime_need_branch_state_is_ancestor(&recipient,&imported) &&
        !prime_need_branch_state_is_ancestor(&entrance,&imported),
        "the imported world extends the recipient, not the unrelated producer prefix");
    PrimeNeedBranchState partial = {0}, completed = {0};
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&left,&recipient,
        state_import_initial,&view,&partial) == CETTA_MATCH_MATCHED &&
        prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&production,&partial,
        state_import_initial,&view,&completed) == CETTA_MATCH_MATCHED &&
        prime_need_branch_state_equal(&completed,&imported),
        "a held branch of the source diamond permits delivery of its independent missing branch");
    PrimeNeedBranchState second_partial = {0};
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&left,&recipient,
        state_import_initial,&view,&second_partial) == CETTA_MATCH_MATCHED &&
        partial.top && second_partial.top && partial.top != second_partial.top &&
        prime_need_branch_state_is_ancestor(&recipient,&partial) &&
        prime_need_branch_state_is_ancestor(&recipient,&second_partial) &&
        prime_need_branch_state_equal(&partial,&second_partial),
        "independent grafts realize the same event over one recipient using distinct roots");
    PrimeNeedBranchState joined_partials = partial;
    CHECK(prime_need_branch_state_merge(&joined_partials,&second_partial) &&
        prime_need_branch_state_equal(&joined_partials,&partial) &&
        state_import_number(&joined_partials,&b,1),
        "joining those realizations retains one event and one State write");
    PrimeNeedBranchState after = {0}, repeated = {0};
    CHECK(prime_need_branch_state_write(&receiving,&completed,&b,atom_int(&receiving,1),
        atom_int(&receiving,2),&after), "recipient can write after consuming the source");
    view.calls = 0u;
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&production,&after,
        state_import_initial,&view,&repeated) == CETTA_MATCH_MATCHED && repeated.top == after.top &&
        !view.calls && state_import_number(&repeated,&b,2),
        "already-held delivery neither rechecks old reads nor rolls back later own writes");
    PrimeNeedBranchState refused = recipient;
    view.calls = 0u; view.refuse_at = 2u;
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&production,&recipient,
        state_import_initial,&view,&refused) == CETTA_MATCH_INCOMPLETE && view.calls == 2u &&
        refused.top == recipient.top, "refused second preparation publishes none of the first prepared write");
    view.refuse_at = 0u;
    PrimeNeedBranchState reads_a = {0};
    CHECK(prime_need_branch_state_read(&source,&entrance,&a,atom_int(&source,1),&reads_a) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&reads_a,&recipient,
        state_import_initial,&view,&refused) == CETTA_MATCH_REFUTED && refused.top == recipient.top,
        "an actual read of earlier a is a dependency, unlike an unrelated earlier a write");
    PrimeNeedBranchState aba = {0}, rival = {0};
    CHECK(prime_need_branch_state_write(&source,&recipient,&b,b.value,atom_int(&source,9),&aba) &&
        prime_need_branch_state_write(&source,&aba,&b,atom_int(&source,9),b.value,&aba) &&
        state_import_number(&aba,&b,0), "a rival State history returns to the same value through ABA");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&left,&aba,
        state_import_initial,&view,&refused) == CETTA_MATCH_REFUTED && refused.top == recipient.top,
        "equal final values do not erase competing writes");
    CHECK(prime_need_branch_state_same_writes_checked(&recipient,&aba,&b)==CETTA_MATCH_REFUTED,
        "input qualification preserves ABA write occurrences even at equal current values");
    CHECK(prime_need_branch_state_same_writes_checked(&entrance,&recipient,&b)==CETTA_MATCH_MATCHED,
        "input qualification ignores writes to unrelated State cells");
    CHECK(prime_need_branch_state_write(&source,&recipient,&b,b.value,atom_int(&source,1),&rival) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&left,&rival,
        state_import_initial,&view,&refused) == CETTA_MATCH_REFUTED,
        "distinct equal-valued writes remain distinct effect occurrences");
    PrimeNeedBranchState ordered = {0}, successor_only = {0};
    CHECK(prime_need_branch_state_write(&source,&left,&c,c.value,atom_int(&source,3),&ordered) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&left,&ordered,&recipient,
        state_import_initial,&view,&successor_only) == CETTA_MATCH_MATCHED,
        "a separately qualified c production can be observed without importing its caller's b");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&ordered,&successor_only,
        state_import_initial,&view,&refused) == CETTA_MATCH_REFUTED && refused.top == recipient.top,
        "that c alone is not a causally closed delivered prefix of the larger b-then-c production");
    Atom *changed = NULL;
    CHECK(prime_need_branch_state_lookup(&partial,&b,&changed) && changed && changed->ground.ival == 1,
        "the imported canonical write has a separately copied payload");
    if (changed) {
        changed->ground.ival = 99;
        CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&left,&partial,
            state_import_initial,&view,&refused) == CETTA_MATCH_REFUTED && refused.top == recipient.top,
            "the same event ID cannot authenticate a changed payload");
        changed->ground.ival = 1;
    }
    /* A physical-depth counterexample: e* is deeper than f, but f follows
     * the original e. Original/grafted aliases are one semantic event. */
    PrimeNeedBranchState original_e = {0}, original_f = {0}, deep = common, graft = {0};
    CHECK(prime_need_branch_state_write(&source,&common,&b,b.value,atom_int(&source,1),&original_e) &&
        prime_need_branch_state_write(&source,&original_e,&b,atom_int(&source,1),
            atom_int(&source,2),&original_f), "canonical e then f has final value 2");
    for (int64_t i = 0; i < 100; i++)
        ready = ready && prime_need_branch_state_write(&source,&deep,&a,
            atom_int(&source,i),atom_int(&source,i+1),&deep);
    CHECK(ready && prime_need_branch_state_import_suffix(&receiving,&common,&common,&original_e,&deep,
        state_import_initial,&view,&graft) == CETTA_MATCH_MATCHED && state_import_number(&graft,&b,1),
        "the same e can have a deeper physical record in an unrelated receiving context");
    CHECK(prime_need_branch_state_same_writes_checked(&original_e,&graft,&b)==CETTA_MATCH_MATCHED,
        "input qualification identifies original and grafted canonical writes");
    CHECK(prime_need_branch_state_same_writes_checked(&original_e,&original_f,&b)==CETTA_MATCH_REFUTED,
        "input qualification notices a later canonical write");
    CHECK(prime_need_branch_state_same_writes_checked(&original_e,&rival,&b)==CETTA_MATCH_REFUTED,
        "distinct equal-valued writes do not identify source inputs");
    PrimeNeedBranchState joined = graft;
    CHECK(prime_need_branch_state_merge(&joined,&original_f) && state_import_number(&joined,&b,2),
        "canonical causality prevents deep e* from shadowing its later original f");
    PrimeNeedBranchState full = {0};
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&common,&original_f,&graft,
        state_import_initial,&view,&full) == CETTA_MATCH_MATCHED && state_import_number(&full,&b,2) &&
        prime_need_branch_state_equal(&joined,&full),
        "a generic join and suffix completion retain the same canonical events without counting e twice");
    CHECK(prime_need_branch_state_same_writes_checked(&joined,&full,&b)==CETTA_MATCH_MATCHED,
        "input write comparison retains canonical order across join and suffix representations");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceiptEvent original_write={0};bool held=false;
    CHECK(prime_need_receipt_event_at(&original_e,0u,&original_write) &&
        original_write.kind==PRIME_NEED_RECEIPT_WRITE_STATE,
        "State entrance-membership control names one canonical write occurrence");
    CHECK(prime_need_branch_state_holds_write_checked(&original_e,&b,
            original_write.event_id,&held)==CETTA_MATCH_MATCHED && held &&
        prime_need_branch_state_holds_write_checked(&graft,&b,
            original_write.event_id,&held)==CETTA_MATCH_MATCHED && held &&
        prime_need_branch_state_holds_write_checked(&original_f,&b,
            original_write.event_id,&held)==CETTA_MATCH_MATCHED && held,
        "canonical State write membership survives later writes and physical graft aliases");
    held=true;
    CHECK(prime_need_branch_state_holds_write_checked(&common,&b,
            original_write.event_id,&held)==CETTA_MATCH_MATCHED && !held &&
        prime_need_branch_state_holds_write_checked(&original_e,&a,
            original_write.event_id,&held)==CETTA_MATCH_REFUTED && !held,
        "missing writes report false while a mismatched cell cannot reuse the occurrence identity");
#endif
    Atom *version_value = NULL;
    uint64_t e_write = 0u, f_write = 0u, selected_write = 0u;
    CHECK(prime_need_branch_state_lookup_version_checked(&original_e,&b,&version_value,&e_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==1 && e_write,
        "a State value identifies its actual supplying write");
    CHECK(prime_need_branch_state_lookup_version_checked(&original_f,&b,&version_value,&f_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==2 && f_write && f_write!=e_write,
        "a later canonical write supplies a distinct State version");
    CHECK(prime_need_branch_state_lookup_version_checked(&graft,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==1 && selected_write==e_write,
        "a deeper graft retains the original write identity, not its physical copy identity");
    CHECK(prime_need_branch_state_lookup_version_checked(&joined,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==2 && selected_write==f_write,
        "canonical ancestry selects f despite the deeper receiving record of e");
    CHECK(prime_need_branch_state_lookup_version_checked(&full,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==2 && selected_write==f_write,
        "suffix completion and generic join expose the same supplying occurrence");
    CHECK(prime_need_branch_state_lookup_version_checked(&rival,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==1 && selected_write && selected_write!=e_write,
        "independent equal-valued writes cannot share a causal anchor");
    version_value = b.value; selected_write = f_write;
    CHECK(prime_need_branch_state_lookup_version_checked(&common,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && !version_value && !selected_write,
        "an untouched logical State does not read a physical value or invent a write");
    CHECK(prime_need_branch_state_lookup_version_checked(&absent,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && !version_value && !selected_write,
        "an absent State has no logical value or write occurrence");
    version_value = b.value; selected_write = f_write;
    CHECK(prime_need_branch_state_lookup_version_checked(&malformed,&b,&version_value,&selected_write)==
        CETTA_MATCH_INCOMPLETE && version_value==b.value && selected_write==f_write,
        "an uninitialized State leaves the coherent output pair untouched");
    CHECK(prime_need_branch_state_lookup_version_checked(&full,&b,&version_value,NULL)==
        CETTA_MATCH_INCOMPLETE && version_value==b.value && selected_write==f_write &&
        prime_need_branch_state_lookup_version_checked(&full,&b,NULL,&selected_write)==
        CETTA_MATCH_INCOMPLETE && selected_write==f_write,
        "an incomplete observation destination publishes neither output");
    PrimeNeedBranchState inconsistent_read = {0};
    CHECK(prime_need_branch_state_read(&source,&original_f,&b,atom_int(&source,99),&inconsistent_read),
        "the raw State witness constructor can expose an inconsistent recorded read");
    CHECK(prime_need_branch_state_lookup_version_checked(&inconsistent_read,&b,
        &version_value,&selected_write)==CETTA_MATCH_REFUTED && version_value==b.value &&
        selected_write==f_write,
        "a read value cannot claim a supplying write with a different payload");
    PrimeNeedBranchState independent_c = {0}, e_on_f = {0}, f_on_e = {0};
    CHECK(prime_need_branch_state_write(&source,&common,&c,c.value,atom_int(&source,1),&independent_c) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&common,&original_e,&independent_c,
            state_import_initial,&view,&e_on_f) == CETTA_MATCH_MATCHED &&
        prime_need_branch_state_import_suffix(&receiving,&common,&common,&independent_c,&original_e,
            state_import_initial,&view,&f_on_e) == CETTA_MATCH_MATCHED &&
        prime_need_branch_state_merge(&e_on_f,&f_on_e),
        "crossed grafts of independent State cells compose");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&common,&original_e,&e_on_f,
        state_import_initial,&view,&repeated) == CETTA_MATCH_MATCHED && repeated.top == e_on_f.top &&
        state_import_number(&repeated,&b,1) && state_import_number(&repeated,&c,1),
        "independent cross-cell transport edges do not defeat all-held idempotence");
    PrimeNeedBranchState read_e = {0}, read_f = {0};
    CHECK(prime_need_branch_state_read(&source,&common,&b,b.value,&read_e) &&
        prime_need_branch_state_read(&source,&common,&b,b.value,&read_f) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&common,&read_e,&read_f,
            state_import_initial,&view,&e_on_f) == CETTA_MATCH_MATCHED &&
        prime_need_branch_state_import_suffix(&receiving,&common,&common,&read_f,&read_e,
            state_import_initial,&view,&f_on_e) == CETTA_MATCH_MATCHED &&
        prime_need_branch_state_merge(&e_on_f,&f_on_e) && state_import_number(&e_on_f,&b,0),
        "equal read-only frontiers commute without dropping either canonical observation");
    CHECK(prime_need_branch_state_equal(&e_on_f,&f_on_e) &&
        !prime_need_branch_state_equal(&e_on_f,&read_e) && !prime_need_branch_state_equal(&e_on_f,&read_f),
        "a read-only component retains both distinct canonical observations");
    CHECK(prime_need_branch_state_same_writes_checked(&read_e,&e_on_f,&b)==CETTA_MATCH_MATCHED,
        "equal commuting read-only additions preserve the selected write input");
    CHECK(prime_need_branch_state_lookup_version_checked(&e_on_f,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && atom_eq(version_value,b.value) && !selected_write,
        "a commuting read frontier reports its selected initial value without a fictitious write");
    CHECK(prime_need_branch_state_import_suffix(&receiving,&common,&common,&read_e,&e_on_f,
        state_import_initial,&view,&repeated) == CETTA_MATCH_MATCHED && repeated.top == e_on_f.top,
        "an already-held read remains idempotent inside a commuting read component");
    for (int64_t value = 0; value <= 1; value++) {
        PrimeNeedBranchState write = {0}, unmerged = e_on_f;
        CHECK(prime_need_branch_state_write(&source,&read_e,&b,b.value,atom_int(&source,value),&write) &&
            !prime_need_branch_state_compatible(&e_on_f,&write) &&
            !prime_need_branch_state_merge(&unmerged,&write) && unmerged.top == e_on_f.top,
            "passive read transport cannot order an otherwise conflicting independent write");
    }
    CHECK(prime_need_branch_state_write(&receiving,&e_on_f,&b,b.value,atom_int(&receiving,2),&after) &&
        state_import_number(&after,&b,2), "an explicitly later write wins after the joined read frontier");
    CHECK(prime_need_branch_state_lookup_version_checked(&after,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==2 && selected_write,
        "a write after joined reads becomes the supplying occurrence");
    Atom *zero = b.value;
    view.values[1] = atom_int(&persistent,7);
    PrimeNeedBranchState selected = {0}, selected_result = {0};
    CHECK(prime_need_branch_state_write(&source,&common,&b,view.values[1],atom_int(&source,8),&selected) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&common,&selected,&common,
        state_import_initial,&view,&selected_result) == CETTA_MATCH_MATCHED &&
        b.value == zero && state_import_number(&selected_result,&b,8),
        "initial observation uses the selected captured view instead of the physical cell field");
    view.values[1] = zero;
    PrimeNeedBranchState foreign = {0};
    CHECK(prime_need_branch_state_begin(&receiving,&foreign) &&
        prime_need_branch_state_import_suffix(&receiving,&common,&entrance,&production,&foreign,
        state_import_initial,&view,&refused) == CETTA_MATCH_INCOMPLETE && refused.top == recipient.top,
        "an unrelated session is unavailable transport, not an input inequality");
    CHECK(prime_need_branch_state_same_writes_checked(&common,&foreign,&b)==CETTA_MATCH_REFUTED,
        "write agreement never identifies independent State sessions");
    PrimeNeedBranchState retained = full;
    CHECK(prime_need_branch_state_promote(&persistent,&retained),
        "canonical grafts use the existing whole-DAG ownership transport");
    arena_free(&source); arena_init_detached(&source);
    arena_free(&receiving); arena_init_detached(&receiving);
    CHECK(state_import_number(&retained,&a,100) && state_import_number(&retained,&b,2),
        "promoted grafts survive both original physical history owners retiring");
    CHECK(prime_need_branch_state_lookup_version_checked(&retained,&b,&version_value,&selected_write)==
        CETTA_MATCH_MATCHED && version_value && version_value->ground.ival==2 && selected_write==f_write,
        "the supplying write survives promotion and retirement of original and receiving arenas");
done:
    arena_free(&source); arena_free(&receiving); arena_free(&persistent);
}

static void check_production_view_import(void) {
    Arena source, receiving, persistent;
    arena_init_detached(&source);
    arena_init_detached(&receiving);
    arena_init_detached(&persistent);
    PrimeNeedSnapshot root = {0}, base = {0}, next = {0}, running = {0},
        produced = {0}, recipient = {0}, imported = {0};
    prime_need_snapshot_init(&root);
    uint64_t a = 0u, b = 0u, c = 0u;
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    Atom *value = atom_int(&source, 17);
    bool ready = prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&source, &root, atom_symbol(&source, "a"), &next, &a) &&
        prime_need_snapshot_allocate(&source, &next, atom_symbol(&source, "b"), &base, &b) &&
        prime_need_snapshot_allocate(&source, &base, atom_symbol(&source, "c"), &next, &c);
    base = next;
    CHECK(ready, "production-view source has three independently addressable cells");
    PrimeNeedReadCapture *capture = prime_need_read_capture_new(&base, &base);
    CHECK(capture != NULL, "production captures its actual entrance Need view");
    if (!ready || !capture) goto cleanup;
    PrimeNeedReadScope scope;
    prime_need_read_scope_enter(&scope, capture);
    PrimeNeedCellView cell;
    uint64_t missing = PRIME_NEED_NAMED_CELL_BIT | UINT64_C(591233);
    CHECK(!prime_need_snapshot_lookup(&base, missing, &cell),
          "a missing named cell is a real negative dependency");
    CHECK(prime_need_snapshot_start_evaluation(&source, &base, a, evaluator, &running) &&
          prime_need_snapshot_resolve_value(&source, &running, a, evaluator, value, &produced),
          "EMPTY, own EVALUATING and completed VALUE form one production");
    prime_need_read_scope_leave(&scope);
    CHECK(prime_need_read_capture_complete(capture), "own cache transitions keep capture complete");
    CHECK(prime_need_snapshot_start_evaluation(&source, &base, b, evaluator, &running) &&
          prime_need_snapshot_resolve_value(&source, &running, b, evaluator,
              atom_int(&source, 29), &recipient), "the recipient independently completes b");
    CHECK(!prime_need_snapshot_is_ancestor(&produced, &recipient) &&
          !prime_need_snapshot_is_ancestor(&recipient, &produced),
          "independent production and reader have different history spines");
    PrimeNeedSnapshot refused = recipient;
    CHECK(!prime_need_snapshot_merge(&refused, &produced) &&
          same_snapshot_metadata(&refused, &recipient), "ordinary sibling merge remains refused");
    CHECK(prime_need_snapshot_import_production(&receiving, capture, &produced, a,
              &recipient, &imported), "qualified import carries a beside the reader's unrelated b");
    PrimeNeedCellView earned = {0}, delivered = {0}, retained = {0};
    CHECK(prime_need_snapshot_lookup(&produced, a, &earned) &&
          prime_need_snapshot_lookup(&imported, a, &delivered) &&
          prime_need_snapshot_lookup(&imported, b, &retained) &&
          delivered.update_id == earned.update_id && delivered.cached == value &&
          atom_eq(retained.cached, atom_int(&source, 29)),
          "import preserves canonical production identity and the recipient's own value");
    CHECK(prime_need_snapshot_is_ancestor(&recipient, &imported) &&
          !prime_need_snapshot_is_ancestor(&produced, &imported) &&
          imported.max_storage_key >= recipient.max_storage_key &&
          imported.max_storage_key >= produced.max_storage_key,
          "fresh administrative frames retain ancestry and never lower the storage watermark");
    PrimeNeedSnapshot repeated;
    CHECK(prime_need_snapshot_import_production(&receiving, capture, &produced, a,
              &imported, &repeated) && same_snapshot_metadata(&repeated, &imported),
          "delivering the same earned view again does not create another history update");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    uint64_t source_update = 0u, received_update = 0u;
    CHECK(prime_need_snapshot_cached_update_id(&produced, a, &source_update) &&
          prime_need_snapshot_cached_update_id(&imported, a, &received_update) &&
          source_update == received_update, "optional observation sees the canonical production token");
#endif
    PrimeNeedSnapshot other;
    CHECK(prime_need_snapshot_start_evaluation(&source, &base, a, evaluator, &running) &&
          prime_need_snapshot_resolve_value(&source, &running, a, evaluator, value, &other),
          "another equal-valued completion is a distinct physical production");
    refused = imported;
    CHECK(!prime_need_snapshot_import_production(&receiving, capture, &other, a,
              &imported, &refused) && same_snapshot_metadata(&refused, &imported),
          "equal payloads cannot replace a selected production");
    CHECK(prime_need_snapshot_allocate_named(&source, &recipient,
              atom_symbol(&source, "appeared"), missing, &other),
          "a formerly missing named cell appears");
    refused = recipient;
    CHECK(!prime_need_snapshot_import_production(&receiving, capture, &produced, a,
              &other, &refused) && same_snapshot_metadata(&refused, &recipient),
          "negative lookup invalidation refuses without publishing any destination");

    PrimeNeedReadCapture *frozen = prime_need_read_capture_copy(capture);
    prime_need_read_scope_enter(&scope, capture);
    CHECK(prime_need_snapshot_lookup(&base, b, &cell), "later work observes another entrance cell");
    prime_need_read_scope_leave(&scope);
    refused = recipient;
    CHECK(frozen && prime_need_snapshot_import_production(&receiving, frozen, &produced,
              a, &recipient, &next) &&
          !prime_need_snapshot_import_production(&receiving, capture, &produced,
              a, &recipient, &refused) && same_snapshot_metadata(&refused, &recipient),
          "a frozen row keeps its earned support while later reads constrain later rows");

    PrimeNeedReadCapture *outer = prime_need_read_capture_new(&base, &base);
    CHECK(outer != NULL, "a containing production owns separate support");
    if (outer && frozen) {
        prime_need_read_scope_enter(&scope, outer);
        bool built = prime_need_snapshot_import_production(&receiving, frozen, &produced,
            a, &base, &next) &&
            prime_need_snapshot_start_evaluation(&receiving, &next, c, evaluator, &running) &&
            prime_need_snapshot_resolve_value(&receiving, &running, c, evaluator,
                atom_int(&source, 31), &next);
        prime_need_read_scope_leave(&scope);
        refused = recipient;
        CHECK(built && prime_need_read_capture_complete(outer) &&
              !prime_need_snapshot_import_production(&receiving, outer, &next, c,
                  &other, &refused) && same_snapshot_metadata(&refused, &recipient),
              "nested reuse inherits negative dependencies even without repeating the lookup");
    }
    prime_need_read_capture_free(outer);

    PrimeNeedSnapshot foreign;
    prime_need_snapshot_init(&foreign);
    CHECK(prime_need_snapshot_begin(&foreign) &&
          prime_need_snapshot_allocate_ready_value(&source, &foreign, value, &foreign, &c),
          "a foreign session supplies a successful lookup discriminator");
    prime_need_read_scope_enter(&scope, capture);
    CHECK(prime_need_snapshot_lookup(&foreign, c, &cell) && cell.cached == value,
          "unrepresentable evidence never changes the source's actual lookup result");
    prime_need_read_scope_leave(&scope);
    CHECK(!prime_need_read_capture_complete(capture), "foreign-session reads make reuse evidence incomplete");
    refused = recipient;
    CHECK(!prime_need_snapshot_import_production(&receiving, capture, &produced, a,
              &recipient, &refused) && same_snapshot_metadata(&refused, &recipient),
          "incomplete dependency evidence cannot authorize an import");
    CHECK(prime_need_snapshot_promote(&persistent, &imported),
          "the existing promotion owner captures the complete imported view");
    uint64_t preserved_update = earned.update_id;
    prime_need_read_capture_free(frozen);
    prime_need_read_capture_free(capture);
    capture = NULL;
    arena_free(&receiving);
    arena_free(&source);
    CHECK(prime_need_snapshot_lookup(&imported, a, &cell) &&
          cell.update_id == preserved_update && atom_eq(cell.cached, atom_int(&persistent, 17)) &&
          prime_need_snapshot_lookup(&imported, b, &cell) &&
          atom_eq(cell.cached, atom_int(&persistent, 29)),
          "imported cells and production identity survive both original arena retirements");
    arena_free(&persistent);
    return;
cleanup:
    prime_need_read_capture_free(capture);
    arena_free(&persistent);
    arena_free(&receiving);
    arena_free(&source);
}

static void check_production_view_write_cut(void) {
    Arena arena;
    arena_init_detached(&arena);
    PrimeNeedSnapshot root={0}, base={0}, running={0}, left={0}, right={0},
        produced={0}, imported={0}, outer_result={0};
    uint64_t a=0u,b=0u,c=0u;
    uint64_t evaluator=prime_need_fresh_evaluator_id();
    Atom *one=atom_int(&arena,1),*two=atom_int(&arena,2),*answer=atom_int(&arena,23);
    bool ready=prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&arena,&root,atom_symbol(&arena,"choice-a"),&base,&a) &&
        prime_need_snapshot_allocate(&arena,&base,atom_symbol(&arena,"choice-b"),&base,&b) &&
        prime_need_snapshot_allocate(&arena,&base,atom_symbol(&arena,"choice-c"),&base,&c) &&
        prime_need_snapshot_start_evaluation(&arena,&base,a,evaluator,&running) &&
        prime_need_snapshot_resolve_value(&arena,&running,a,evaluator,one,&left) &&
        prime_need_snapshot_start_evaluation(&arena,&base,a,evaluator,&running) &&
        prime_need_snapshot_resolve_value(&arena,&running,a,evaluator,two,&right);
    CHECK(ready,"reader worlds share admitted cells but select different a productions before forcing b");
    if (!ready) goto done;
    PrimeNeedReadCapture *capture=prime_need_read_capture_new(&base,&left);
    PrimeNeedReadCapture *bad=prime_need_read_capture_new(&right,&left);
    CHECK(capture && !bad,"a force write-cut must actually extend its admitted common context");
    prime_need_read_capture_free(bad);
    if (!capture) goto done;
    PrimeNeedReadScope scope;
    prime_need_read_scope_enter(&scope,capture);
    ready=prime_need_snapshot_start_evaluation(&arena,&left,b,evaluator,&running) &&
        prime_need_snapshot_resolve_value(&arena,&running,b,evaluator,answer,&produced);
    prime_need_read_scope_leave(&scope);
    CHECK(ready && prime_need_read_capture_complete(capture),
          "b completes after a=1 without reading a");
    PrimeNeedCellView held,earned;
    CHECK(ready && prime_need_snapshot_import_production(&arena,capture,&produced,b,&right,&imported) &&
          prime_need_snapshot_lookup(&imported,a,&held) && held.cached==two &&
          prime_need_snapshot_lookup(&produced,b,&earned) &&
          prime_need_snapshot_lookup(&imported,b,&held) && held.cached==answer &&
          held.update_id==earned.update_id,
          "independent b imports into a=2 without copying the producer's earlier a=1 choice");
    CHECK(prime_need_snapshot_is_ancestor(&right,&imported) &&
          !prime_need_snapshot_is_ancestor(&left,&imported),
          "transport retains recipient ancestry instead of identifying sibling worlds");
    PrimeNeedSnapshot refused=right;
    CHECK(!prime_need_snapshot_merge(&refused,&produced) && same_snapshot_metadata(&refused,&right),
          "the common snapshot merge does not gain permission to merge siblings");

    PrimeNeedReadCapture *frozen=prime_need_read_capture_copy(capture);
    prime_need_read_scope_enter(&scope,capture);
    CHECK(prime_need_snapshot_lookup(&left,a,&held) && held.cached==one,
          "later source work explicitly observes its a=1 prefix");
    prime_need_read_scope_leave(&scope);
    CHECK(!prime_need_snapshot_import_production(&arena,capture,&produced,b,&right,&refused) &&
          same_snapshot_metadata(&refused,&right),
          "an actually read caller choice prevents reuse in the conflicting world");
    CHECK(frozen && prime_need_snapshot_import_production(&arena,frozen,&produced,b,&right,&imported),
          "the frozen earlier completion retains its smaller actual read support");

    PrimeNeedReadCapture *outer=prime_need_read_capture_new(&base,&right);
    if (outer && frozen) {
        prime_need_read_scope_enter(&scope,outer);
        ready=prime_need_snapshot_import_production(&arena,frozen,&produced,b,&right,&imported) &&
            prime_need_snapshot_start_evaluation(&arena,&imported,c,evaluator,&running) &&
            prime_need_snapshot_resolve_forwarded_value(&arena,&running,c,evaluator,
                answer,b,&outer_result);
        prime_need_read_scope_leave(&scope);
        CHECK(ready && prime_need_read_capture_complete(outer) &&
              prime_need_snapshot_import_production(&arena,outer,&outer_result,c,&left,&imported) &&
              prime_need_snapshot_lookup(&imported,a,&held) && held.cached==one &&
              prime_need_snapshot_forwarded_value_source(&imported,c,&held) &&
              held.update_id==earned.update_id,
              "nested imported support and forwarded provenance cross only the independent prefix");
    } else CHECK(false,"nested write-cut capture allocation");
    prime_need_read_capture_free(outer);
    prime_need_read_capture_free(frozen);
    prime_need_read_capture_free(capture);
done:
    arena_free(&arena);
}

static void check_production_view_dependencies(void) {
    Arena arena;
    arena_init_detached(&arena);
    PrimeNeedSnapshot root = {0}, base = {0}, running = {0}, produced = {0},
        recipient = {0}, imported = {0}, next = {0};
    uint64_t a = 0u, b = 0u, child = 0u;
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    Atom *origin = atom_symbol(&arena, "stored-source");
    Atom *value = atom_int(&arena, 23);
    bool ready = prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(&arena, &root, origin, &next, &a) &&
        prime_need_snapshot_allocate(&arena, &next, origin, &base, &b);
    CHECK(ready, "descriptor controls allocate the shared entrance");
    if (!ready) goto cleanup;
    PrimeNeedReadCapture *capture = prime_need_read_capture_new(&base, &base);
    PrimeNeedReadCapture *outer = prime_need_read_capture_new(&base, &base);
    CHECK(capture && outer, "nested Need captures have distinct owners");
    if (!capture || !outer) {
        prime_need_read_capture_free(capture);
        prime_need_read_capture_free(outer);
        goto cleanup;
    }
    PrimeNeedReadScope parent, inner;
    prime_need_read_scope_enter(&parent, outer);
    prime_need_read_scope_enter(&inner, capture);
    uint64_t descriptor = UINT64_C(10000000), found = 777u;
    CHECK(!prime_need_snapshot_find_imported_cell(&base, descriptor, origin, &found) && found == 777u,
          "a missing stored descriptor preserves its caller output while earning absence");
    CHECK(prime_need_snapshot_start_evaluation(&arena, &base, a, evaluator, &running) &&
          prime_need_snapshot_resolve_value(&arena, &running, a, evaluator, value, &produced),
          "the descriptor-reading production completes");
    prime_need_read_scope_leave(&inner);
    prime_need_read_scope_leave(&parent);
    CHECK(prime_need_snapshot_allocate_persisted(&arena, &base, origin, descriptor,
              &recipient, &child), "a previously absent descriptor becomes present");
    PrimeNeedSnapshot refused = base;
    CHECK(!prime_need_snapshot_import_production(&arena, capture, &produced, a,
              &recipient, &refused) && same_snapshot_metadata(&refused, &base),
          "stored-query absence detects a new cell even though its thunk was not known");
    CHECK(!prime_need_snapshot_import_production(&arena, outer, &produced, a,
              &recipient, &refused) && same_snapshot_metadata(&refused, &base),
          "the outer scope receives nested positive and negative Need observations");
    CHECK(prime_need_snapshot_allocate_persisted(&arena, &base,
              atom_symbol(&arena, "different-stored-source"), descriptor, &recipient, &child) &&
          prime_need_snapshot_import_production(&arena, capture, &produced, a,
              &recipient, &imported),
          "another origin under the same import key does not invalidate a distinct query");
    prime_need_read_capture_free(capture);
    prime_need_read_capture_free(outer);

    PrimeNeedSnapshot left={0},right={0},right_running={0},right_result={0};
    ready=prime_need_snapshot_start_evaluation(&arena,&base,a,evaluator,&running) &&
        prime_need_snapshot_resolve_value(&arena,&running,a,evaluator,value,&left) &&
        prime_need_snapshot_start_evaluation(&arena,&base,a,evaluator,&running) &&
        prime_need_snapshot_resolve_value(&arena,&running,a,evaluator,atom_int(&arena,41),&right);
    outer=ready ? prime_need_read_capture_new(&base,&left) : NULL;
    capture=ready ? prime_need_read_capture_new(&base,&right) : NULL;
    CHECK(outer && capture,"sibling resident controls capture distinct requesting worlds");
    if(outer && capture) {
        prime_need_read_scope_enter(&parent,outer);
        prime_need_read_scope_enter_resident(&inner,capture);
        ready=prime_need_snapshot_start_evaluation(&arena,&right,b,evaluator,&right_running) &&
            prime_need_snapshot_resolve_value(&arena,&right_running,b,evaluator,atom_int(&arena,99),&right_result);
        prime_need_read_scope_leave(&inner);
        CHECK(ready && prime_need_read_capture_complete(capture) && prime_need_read_capture_complete(outer),
            "a sibling resident records its physical reads without poisoning the recipient capture");
        CHECK(ready && prime_need_snapshot_import_production(&arena,capture,&right_result,b,&left,&imported) &&
            prime_need_read_capture_complete(outer),"qualified import propagates support in the recipient's actual world");
        prime_need_read_scope_leave(&parent);
        prime_need_read_scope_enter(&parent,outer);
        PrimeNeedCellView observed;
        bool read=prime_need_snapshot_lookup(&right,a,&observed);
        prime_need_read_scope_leave(&parent);
        CHECK(read && !prime_need_read_capture_complete(outer),
            "an ordinary unqualified sibling read still invalidates the capture");
    }
    prime_need_read_capture_free(capture);prime_need_read_capture_free(outer);

    capture = prime_need_read_capture_new(&base, &base);
    CHECK(capture != NULL, "persisted child production captures its entrance");
    if (!capture) goto cleanup;
    prime_need_read_scope_enter(&inner, capture);
    uint64_t shared_key = base.max_storage_key + UINT64_C(10000000);
    ready = prime_need_snapshot_start_evaluation(&arena, &base, a, evaluator, &running) &&
        prime_need_snapshot_allocate_persisted(&arena, &running, origin, shared_key, &next, &child) &&
        prime_need_snapshot_resolve_value(&arena, &next, a, evaluator, value, &produced);
    prime_need_read_scope_leave(&inner);
    uint64_t other_child = 0u;
    CHECK(ready && prime_need_snapshot_allocate_persisted(&arena, &base,
              atom_symbol(&arena, "other-persisted-child"), shared_key, &recipient, &other_child) &&
          child != other_child, "independent branches can allocate one requested key for different cells");
    refused = base;
    CHECK(!prime_need_snapshot_import_production(&arena, capture, &produced, a,
              &recipient, &refused) && same_snapshot_metadata(&refused, &base),
          "different thunk identities do not license a physical storage-key collision");
    prime_need_read_capture_free(capture);

    capture = prime_need_read_capture_new(&base, &base);
    CHECK(capture != NULL, "forwarding retains a production-local read capture");
    if (!capture) goto cleanup;
    prime_need_read_scope_enter(&inner, capture);
    ready = prime_need_snapshot_start_evaluation(&arena, &base, a, evaluator, &running) &&
        prime_need_snapshot_allocate(&arena, &running, origin, &next, &child) &&
        prime_need_snapshot_start_evaluation(&arena, &next, child, evaluator, &running) &&
        prime_need_snapshot_resolve_value(&arena, &running, child, evaluator, value, &next) &&
        prime_need_snapshot_resolve_forwarded_value(&arena, &next, a, evaluator,
            value, child, &produced);
    prime_need_read_scope_leave(&inner);
    CHECK(ready && prime_need_read_capture_complete(capture),
          "a source can allocate and complete a child then forward its value");
    CHECK(prime_need_snapshot_start_evaluation(&arena, &base, b, evaluator, &running) &&
          prime_need_snapshot_resolve_value(&arena, &running, b, evaluator,
              atom_int(&arena, 41), &recipient) &&
          prime_need_snapshot_import_production(&arena, capture, &produced, a,
              &recipient, &imported), "the complete forwarded view imports beside independent work");
    PrimeNeedCellView cell, source;
    CHECK(prime_need_snapshot_lookup(&imported, a, &cell) &&
          cell.value_producer_thunk == child &&
          prime_need_snapshot_forwarded_value_source(&imported, a, &source) &&
          source.update_id == cell.value_producer_update && atom_eq(source.cached, value),
          "forwarded provenance survives fresh recipient history records");
    prime_need_read_capture_free(capture);

    capture = prime_need_read_capture_new(&base, &base);
    CHECK(capture != NULL, "unfinished-descendant controls capture their entrance");
    if (!capture) goto cleanup;
    prime_need_read_scope_enter(&inner, capture);
    ready = prime_need_snapshot_start_evaluation(&arena, &base, a, evaluator, &running) &&
        prime_need_snapshot_allocate(&arena, &running, origin, &next, &child) &&
        prime_need_snapshot_start_evaluation(&arena, &next, child, evaluator, &running) &&
        prime_need_snapshot_resolve_value(&arena, &running, a, evaluator, value, &produced);
    prime_need_read_scope_leave(&inner);
    refused = recipient;
    CHECK(ready && !prime_need_snapshot_import_production(&arena, capture, &produced, a,
              &recipient, &refused) && same_snapshot_metadata(&refused, &recipient),
          "an introduced live evaluator cannot be imported as a completed cache view");
    prime_need_read_capture_free(capture);

    capture = prime_need_read_capture_new(&base, &base);
    CHECK(capture != NULL, "stable-fault controls capture their entrance");
    if (!capture) goto cleanup;
    prime_need_read_scope_enter(&inner, capture);
    Atom *fault = atom_expr3(&arena, atom_symbol(&arena, "Error"), origin,
                             atom_symbol(&arena, "stable-fault"));
    ready = prime_need_snapshot_start_evaluation(&arena, &base, a, evaluator, &running) &&
        prime_need_snapshot_resolve_stable_fault(&arena, &running, a, evaluator, fault, &produced);
    prime_need_read_scope_leave(&inner);
    CHECK(ready && prime_need_snapshot_import_production(&arena, capture, &produced, a,
              &recipient, &imported) && prime_need_snapshot_lookup(&imported, a, &cell) &&
          cell.cache_state == PRIME_NEED_CACHE_STABLE_FAULT && cell.cached == fault,
          "an earned stable fault remains a fault when delivered into another reader");
    prime_need_read_capture_free(capture);
cleanup:
    arena_free(&arena);
}

/* Compare lineage queries with independently retained parent indices, then
 * repeat after transporting every prefix and destroying the source arena. */
static void check_snapshot_prefix_transport(void) {
    enum { COUNT = 257 };
    PrimeNeedSnapshot source[COUNT], copied[COUNT];
    size_t parent[COUNT] = {0};
    Arena input, output;
    arena_init_detached(&input);
    arena_init_detached(&output);
    AtomDeepCopySession *session = NULL;
    bool input_alive = true;
    prime_need_snapshot_init(&source[0]);
    bool admitted = prime_need_snapshot_begin(&source[0]);
    for (size_t i = 1u; admitted && i < COUNT; i++) {
        parent[i] = i <= 128u ? i - 1u :
            (size_t)(((uint64_t)i * UINT64_C(2654435761)) >> 16u) % i;
        uint64_t cell = 0u;
        admitted = prime_need_snapshot_allocate(&input, &source[parent[i]],
            atom_int(&input, (int64_t)i), &source[i], &cell);
    }
    CHECK(admitted, "lineage forest retains its independent parent inventory");
    if (!admitted) goto done;
    bool exact = true;
    for (size_t a = 0u; a < COUNT; a++) {
        for (size_t b = 0u; b < COUNT; b++) {
            size_t walk = b;
            while (walk && walk != a) walk = parent[walk];
            if (prime_need_snapshot_is_ancestor(&source[a], &source[b]) != (walk == a))
                exact = false;
        }
    }
    CHECK(exact, "lineage queries preserve all ancestor and sibling distinctions");
    session = atom_deep_copy_session_new(&output);
    AtomDeepCopyArenaPair pair = {&input, &output};
    admitted = session && atom_deep_copy_session_set_arena_map(session, &pair, 1u);
    for (size_t i = 0u; admitted && i < COUNT; i++) {
        copied[i] = source[i];
        admitted = prime_need_snapshot_copy_with_session(&output, session, &copied[i]);
    }
    CHECK(admitted, "complete lineage copies share their transported prefixes");
    if (!admitted) goto done;
    atom_deep_copy_session_free(session);
    session = NULL;
    arena_free(&input);
    input_alive = false;
    exact = true;
    for (size_t a = 0u; a < COUNT; a++) {
        for (size_t b = 0u; b < COUNT; b++) {
            size_t walk = b;
            while (walk && walk != a) walk = parent[walk];
            if (prime_need_snapshot_is_ancestor(&copied[a], &copied[b]) != (walk == a))
                exact = false;
        }
    }
    CHECK(exact, "copied lineage queries survive destruction of every source frame");
done:
    atom_deep_copy_session_free(session);
    arena_free(&output);
    if (input_alive) arena_free(&input);
}

static void check_private_need_suffix(Arena *arena, Arena *promoted_arena) {
    PrimeNeedSnapshot root = {0}, base = {0}, running = {0}, completed = {0};
    PrimeNeedSnapshot local = {0}, local_running = {0}, local_done = {0};
    uint64_t inherited = 0u, fresh = 0u;
    uint64_t evaluator = prime_need_fresh_evaluator_id();
    Atom *origin = atom_symbol(arena, "private-suffix-choice");
    Atom *value = atom_symbol(arena, "chosen-value");
    bool ready = prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(arena, &root, origin, &base, &inherited);
    CHECK(ready, "private-suffix control starts with a caller-owned cell");
    if (!ready) return;
    CHECK(prime_need_snapshot_suffix_is_private(&base, &base),
          "an unchanged heap has no caller-cell update to retain");
    CHECK(prime_need_snapshot_start_evaluation(arena, &base, inherited,
              evaluator, &running) &&
              !prime_need_snapshot_suffix_is_private(&base, &running),
          "starting an inherited cell is not private temporary work");
    PrimeNeedCellView before = {0}, after = {0};
    CHECK(prime_need_snapshot_resolve_value(arena, &running, inherited,
              evaluator, value, &completed) &&
              !prime_need_snapshot_suffix_is_private(&base, &completed) &&
              prime_need_snapshot_lookup(&base, inherited, &before) &&
              prime_need_snapshot_lookup(&completed, inherited, &after) &&
              before.cache_state == PRIME_NEED_CACHE_EMPTY &&
              after.cache_state == PRIME_NEED_CACHE_VALUE && after.cached == value,
          "a completed caller choice survives even when its returned value is scalar");
    CHECK(prime_need_snapshot_allocate(arena, &base, origin, &local, &fresh) &&
              prime_need_snapshot_start_evaluation(arena, &local, fresh,
                  evaluator, &local_running) &&
              prime_need_snapshot_resolve_value(arena, &local_running, fresh,
                  evaluator, value, &local_done) &&
              prime_need_snapshot_suffix_is_private(&base, &local_done),
          "a wholly local cell may be allocated and forced in a private suffix");
    PrimeNeedSnapshot promoted = local_done;
    CHECK(prime_need_snapshot_promote(promoted_arena, &promoted) &&
              prime_need_snapshot_suffix_is_private(&base, &promoted),
          "private suffix admission follows history identity across promotion");
    CHECK(!prime_need_snapshot_suffix_is_private(&completed, &local_done),
          "a sibling history cannot be discarded as an extension");
    PrimeNeedSnapshot named = {0};
    CHECK(prime_need_snapshot_allocate_named(arena, &base, origin,
              PRIME_NEED_NAMED_CELL_BIT | UINT64_C(1002), &named) &&
              !prime_need_snapshot_suffix_is_private(&base, &named),
          "a newly demanded named cell retains its stable public identity");
    PrimeNeedSnapshot imported = {0};
    uint64_t imported_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted(arena, &base, origin,
              local_done.max_storage_key + 1u, &imported, &imported_id) &&
              !prime_need_snapshot_suffix_is_private(&base, &imported),
          "an imported cell is retained even above the caller's old watermark");
    PrimeNeedSnapshot watermark = base;
    watermark.max_storage_key = local_done.max_storage_key;
    CHECK(!prime_need_snapshot_suffix_is_private(&watermark, &local_done),
          "a retained watermark conservatively prevents reclassifying old keys");
    PrimeNeedSnapshot absent = {0}, foreign = {0};
    CHECK(prime_need_snapshot_begin(&foreign) &&
              !prime_need_snapshot_suffix_is_private(&absent, &local_done) &&
              !prime_need_snapshot_suffix_is_private(&foreign, &local_done) &&
              !prime_need_snapshot_suffix_is_private(NULL, &local_done) &&
              !prime_need_snapshot_suffix_is_private(&base, NULL),
          "absent and foreign worlds supply no suffix-discard permission");
    promoted = completed;
    CHECK(prime_need_snapshot_promote(promoted_arena, &promoted) &&
              !prime_need_snapshot_suffix_is_private(&base, &promoted),
          "relocating a caller-cell update does not make it private");
}

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
static void check_qualified_receipt_join(void) {
    Arena common_owner,left_owner,right_owner,output_owner,payload_owner;
    arena_init_detached(&common_owner);arena_init_detached(&left_owner);
    arena_init_detached(&right_owner);arena_init_detached(&output_owner);
    arena_init_detached(&payload_owner);
    Atom *zero=atom_int(&payload_owner,0),*one=atom_int(&payload_owner,1);
    StateCell cell={.value=zero};
    PrimeNeedReceipt root={0},left={0},right={0},joined={0};
    bool ready=prime_need_receipt_begin(&common_owner,&root) &&
        prime_need_receipt_read_state(&common_owner,&root,&cell,zero,&left) &&
        prime_need_receipt_write_state(&common_owner,&root,&cell,zero,one,&right);
    CHECK(ready,"qualified receipt join has sibling read and write evidence");
    CHECK(ready && prime_need_receipt_promote(&left_owner,&left) &&
        prime_need_receipt_promote(&right_owner,&right),
        "qualified receipt inputs retain independent physical owners");
    PrimeNeedReceipt ordinary=left;
    CHECK(!prime_need_receipt_merge(&ordinary,&right) && ordinary.top==left.top,
        "an unqualified receipt merge still rejects incomparable State effects");
    joined=left;
    CHECK(prime_need_receipt_join_qualified(&output_owner,&joined,&right)==
        CETTA_MATCH_MATCHED && joined.owner==&output_owner,
        "operational qualification admits a causal evidence join in the destination owner");
    PrimeNeedReceiptGraph graph={0};
    CHECK(prime_need_receipt_graph_collect(&joined,&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        graph.length==3u && graph.event_count==2u && !graph.nodes[2].has_event &&
        graph.nodes[2].left_id && graph.nodes[2].right_id &&
        graph.nodes[2].left_id!=graph.nodes[2].right_id,
        "qualified join retains both exact event occurrences under one fresh causal node");
    uint64_t joined_identity=graph.root_id;
    prime_need_receipt_graph_free(&graph);
    CHECK(prime_need_receipt_join_qualified(&output_owner,&joined,&right)==
        CETTA_MATCH_MATCHED &&
        prime_need_receipt_graph_collect(&joined,&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        graph.root_id==joined_identity && graph.event_count==2u,
        "reimporting held evidence is idempotent across promoted aliases");
    const PrimeNeedReceiptFrame *joined_top=joined.top;
    PrimeNeedReceipt foreign={0};
    CHECK(prime_need_receipt_begin(&payload_owner,&foreign) &&
        prime_need_receipt_observe_cell(&payload_owner,&foreign,77u,91u,one,&foreign),
        "foreign receipt session supplies real evidence");
    CHECK(prime_need_receipt_join_qualified(&output_owner,&joined,&foreign)==
        CETTA_MATCH_REFUTED && joined.top==joined_top,
        "qualified joining never identifies independent receipt sessions");
    PrimeNeedSnapshot need_root={0},need_empty={0},need_running={0},need_done={0};
    uint64_t need_cell=0u,need_evaluator=prime_need_fresh_evaluator_id(),need_update=0u;
    bool need_ready=prime_need_snapshot_begin(&need_root) &&
        prime_need_snapshot_allocate(&common_owner,&need_root,one,&need_empty,&need_cell) &&
        prime_need_snapshot_start_evaluation(&common_owner,&need_empty,need_cell,
            need_evaluator,&need_running) &&
        prime_need_snapshot_resolve_value(&common_owner,&need_running,need_cell,
            need_evaluator,one,&need_done) &&
        prime_need_snapshot_cached_update_id(&need_done,need_cell,&need_update);
    PrimeNeedReceipt need_catalogue={0};
    CHECK(need_ready && prime_need_receipt_observe_cached_cell(&common_owner,&root,
        &need_done,need_cell,true,false,one,&need_catalogue),
        "cached anchor catalogue contains one actual completed update");
    PrimeNeedReceipt need_anchored=root;bool found=false;
    CHECK(prime_need_receipt_join_cached_anchor_qualified(&output_owner,&need_anchored,
        &need_catalogue,need_done.session_id,need_cell,need_update,&found)==
        CETTA_MATCH_MATCHED && found && prime_need_receipt_event_count(&need_anchored)==1u,
        "cached observation selects the exact producer-completion prefix");
    const PrimeNeedReceiptFrame *need_top=need_anchored.top;found=true;
    found=false;
    CHECK(prime_need_receipt_join_cached_anchor_qualified(&output_owner,&need_anchored,
        &root,need_done.session_id,need_cell,need_update,&found)==CETTA_MATCH_MATCHED &&
        found && need_anchored.top==need_top,
        "an already-held producer completion needs no entrance-catalogue copy");
    found=true;
    CHECK(prime_need_receipt_join_cached_anchor_qualified(&output_owner,&need_anchored,
        &need_catalogue,need_done.session_id,need_cell,need_update+1u,&found)==
        CETTA_MATCH_INCOMPLETE && found && need_anchored.top==need_top,
        "missing plain cached evidence refuses without changing output or found flag");
    PrimeNeedBranchState operation_root={0},operation={0};
    PrimeNeedReceipt state_catalogue={0};PrimeNeedReceiptEvent state_event={0};
    bool state_ready=prime_need_branch_state_begin(&common_owner,&operation_root) &&
        prime_need_branch_state_write(&common_owner,&operation_root,&cell,zero,one,&operation) &&
        prime_need_receipt_event_at(&operation,0u,&state_event) && state_event.event_id &&
        prime_need_receipt_record_state(&common_owner,&root,&operation,&state_catalogue);
    CHECK(state_ready,"State anchor catalogue records one exact operational write");
    PrimeNeedReceipt state_anchored=root;found=false;
    CHECK(state_ready && prime_need_receipt_join_state_anchor_qualified(&output_owner,
        &state_anchored,&state_catalogue,state_event.event_id,&found)==CETTA_MATCH_MATCHED &&
        found && prime_need_receipt_event_count(&state_anchored)==1u,
        "State observation selects its canonical supplying-write prefix");
    prime_need_receipt_graph_free(&graph);
    arena_free(&common_owner);arena_free(&left_owner);arena_free(&right_owner);
    CHECK(prime_need_receipt_graph_collect(&joined,&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
        graph.length==3u,"qualified receipt output survives every shorter-lived source owner");
    prime_need_receipt_graph_free(&graph);
    CHECK(prime_need_receipt_join_qualified(NULL,&joined,NULL)==CETTA_MATCH_INCOMPLETE &&
        joined.top==joined_top,"missing destination ownership refuses without changing the receipt");
    PrimeNeedRecording *group=prime_need_recording_new(
        (PrimeNeedRecordingLimits){8u,65536u,3u,4u});
    PrimeNeedReceipt recorded_left={0},recorded_right={0};
    bool recorded=group && prime_need_recording_reserve(group,NULL,4u,16384u,&recorded_left) &&
        prime_need_recording_reserve(group,NULL,4u,16384u,&recorded_right) &&
        prime_need_receipt_observe_cell(&payload_owner,&recorded_left,81u,1u,zero,&recorded_left) &&
        prime_need_receipt_observe_cell(&payload_owner,&recorded_right,81u,2u,one,&recorded_right);
    CHECK(recorded,"qualified receipt recording control has two bounded branches");
    PrimeNeedRecordingScope scope;
    if(recorded) prime_need_recording_enter(&scope,&recorded_left);
    CettaMatchCompletion recorded_join=recorded
        ? prime_need_receipt_join_qualified(&output_owner,&recorded_left,&recorded_right)
        : CETTA_MATCH_INCOMPLETE;
    if(recorded) prime_need_recording_leave(&scope);
    CHECK(recorded_join==CETTA_MATCH_MATCHED,
        "qualified join reuses the active bounded recording owner");
    PrimeNeedReceiptGraph recorded_graph={0};
    CHECK(recorded_join==CETTA_MATCH_MATCHED &&
        prime_need_receipt_graph_collect(&recorded_left,&recorded_graph)==
            PRIME_NEED_RECEIPT_GRAPH_COMPLETE && recorded_graph.event_count==2u,
        "bounded qualified join retains both admitted row histories");
    prime_need_receipt_graph_free(&recorded_graph);
    PrimeNeedRecording *foreign_group=prime_need_recording_new(
        (PrimeNeedRecordingLimits){4u,32768u,1u,2u});
    PrimeNeedReceipt foreign_recording={0};
    bool foreign_ready=foreign_group && prime_need_recording_reserve(
        foreign_group,NULL,4u,16384u,&foreign_recording) &&
        prime_need_receipt_observe_cell(&payload_owner,&foreign_recording,82u,1u,one,
            &foreign_recording);
    CHECK(foreign_ready,"foreign bounded history supplies real evidence");
    if(foreign_ready) {
        prime_need_recording_enter(&scope,&recorded_left);
        recorded_join=prime_need_receipt_join_qualified(
            &output_owner,&recorded_left,&foreign_recording);
        prime_need_recording_leave(&scope);
    }
    PrimeNeedRecordingStatus recording_status=prime_need_recording_status(group);
    CHECK(foreign_ready && recorded_join==CETTA_MATCH_MATCHED &&
        recording_status.incomplete && recording_status.omitted>0u,
        "untransportable foreign optional history becomes explicit omission");
    if(foreign_group)prime_need_recording_release(foreign_group);
    if(group)prime_need_recording_release(group);
    arena_free(&payload_owner);arena_free(&output_owner);
}

static void check_state_receipt_operation_identity(void) {
    Arena values,operations,observations,copied;
    arena_init(&values);arena_init(&operations);arena_init(&observations);arena_init(&copied);
    Atom *zero=atom_int(&values,0),*one=atom_int(&values,1);
    StateCell cell={.value=zero,.content_type=atom_symbol(&values,"Number")};
    PrimeNeedBranchState states[6]={{0}};
    PrimeNeedReceipt receipts[6]={{0}};
    uint64_t ids[5]={0};
    bool ready=prime_need_branch_state_begin(&operations,&states[0]) &&
        prime_need_receipt_begin(&observations,&receipts[0]);
    CHECK(ready,"begin distinct operational and observer State carriers");
    for(size_t i=0u;ready && i<5u;i++) {
        Atom *before=i==1u?one:zero,*after=i==0u?one:zero;
        ready=i==4u
            ? prime_need_branch_state_read(&operations,&states[i],&cell,zero,&states[i+1u])
            : prime_need_branch_state_write(&operations,&states[i],&cell,before,after,&states[i+1u]);
        CHECK(ready,"construct an actual State occurrence including ABA and equal writes");
        if(!ready)break;
        PrimeNeedReceiptEvent operation={0},observation={0};
        ready=prime_need_receipt_event_at(&states[i+1u],i,&operation);
        CHECK(ready && operation.event_id && !operation.state_event_id,
            "operational State identity exists independently of observation");
        const PrimeNeedReceiptFrame *unchanged=states[i+1u].top;
        ready=ready && prime_need_receipt_record_state(&observations,&receipts[i],
            &states[i+1u],&receipts[i+1u]) &&
            prime_need_receipt_event_at(&receipts[i+1u],i,&observation);
        CHECK(ready,"record the operational occurrence through the common receipt owner");
        if(!ready)break;
        ids[i]=operation.event_id;
        Atom *version_value=NULL;uint64_t write_id=0u;
        CHECK(prime_need_branch_state_lookup_version_checked(&states[i+1u],&cell,
            &version_value,&write_id)==CETTA_MATCH_MATCHED && version_value==operation.after &&
            write_id==ids[i==4u?3u:i],
            "State observation names the exact supplying write through ABA, equal writes and later reads");
        CHECK(observation.state_event_id==operation.event_id &&
            observation.event_id>observation.state_event_id && observation.kind==operation.kind &&
            observation.state_cell==&cell && atom_eq(observation.after,operation.after) &&
            ((!observation.before && !operation.before) ||
                (observation.before && operation.before && atom_eq(observation.before,operation.before))),
            "observer identity is fresh while its State link and payload name the exact operation");
        CHECK(states[i+1u].top==unchanged && cell.value==zero,
            "recording neither reruns the operation nor mutates the operational carrier or cell");
        for(size_t j=0u;j<i;j++)CHECK(ids[j]!=ids[i],
            "ABA and equal-valued writes retain distinct operational identities");
    }
    if(ready) {
        PrimeNeedReceiptGraph graph={0};
        CHECK(prime_need_receipt_graph_collect(&receipts[5],&graph)==PRIME_NEED_RECEIPT_GRAPH_COMPLETE &&
            graph.event_count==5u,"correlated State observations form a complete diagnostic graph");
        for(size_t i=0u;i<graph.length;i++)if(graph.nodes[i].has_event) {
            PrimeNeedReceiptEvent saved=graph.nodes[i].event;
            graph.nodes[i].event.state_event_id=saved.event_id;
            CHECK(prime_need_receipt_graph_validate(&graph)==PRIME_NEED_RECEIPT_GRAPH_INVALID,
                "a State observation cannot precede its claimed operation");
            graph.nodes[i].event=saved;graph.nodes[i].event.kind=PRIME_NEED_RECEIPT_INSPECT_ORIGIN;
            CHECK(prime_need_receipt_graph_validate(&graph)==PRIME_NEED_RECEIPT_GRAPH_INVALID,
                "non-State event kinds cannot carry an operational State link");
            graph.nodes[i].event=saved;graph.nodes[i].event.state_cell=NULL;
            CHECK(prime_need_receipt_graph_validate(&graph)==PRIME_NEED_RECEIPT_GRAPH_INVALID,
                "a correlated State event retains its resource identity");
            graph.nodes[i].event=saved;
        }
        prime_need_receipt_graph_free(&graph);
        PrimeNeedReceipt preserved=receipts[5],empty={0},not_state={0};
        CHECK(!prime_need_receipt_record_state(&observations,&receipts[0],&empty,&preserved) &&
            preserved.top==receipts[5].top,"absent operational input leaves the owned receipt unchanged");
        CHECK(prime_need_receipt_use_equation_ids(&observations,&receipts[0],1u,1u,&not_state) &&
            !prime_need_receipt_record_state(&observations,&receipts[0],&not_state,&preserved) &&
            preserved.top==receipts[5].top,"a non-State operation cannot masquerade as a State witness");
        Atom *untouched=one;uint64_t untouched_write=UINT64_MAX;
        CHECK(prime_need_branch_state_lookup_version_checked(&not_state,&cell,&untouched,
            &untouched_write)==CETTA_MATCH_INCOMPLETE && untouched==one && untouched_write==UINT64_MAX,
            "an equation receipt is not an operational State version");
        PrimeNeedRecording *group=prime_need_recording_new((PrimeNeedRecordingLimits){8u,16384u,1u,4u});
        PrimeNeedReceipt bounded={0},omitted={0};
        bool reserved=group && prime_need_recording_reserve(group,NULL,4u,8192u,&bounded);
        CHECK(reserved,"reserve a bounded observer without changing State execution");
        if(reserved) {
            CHECK(prime_need_branch_state_lookup_version_checked(&bounded,&cell,&untouched,
                &untouched_write)==CETTA_MATCH_INCOMPLETE && untouched==one && untouched_write==UINT64_MAX,
                "a bounded receipt slice cannot supply an operational State version");
            PrimeNeedRecordingScope scope;
            prime_need_recording_enter(&scope,&bounded);
            CHECK(prime_need_receipt_record_state(&observations,&bounded,&states[5],&omitted),
                "State correlation follows the recorder's existing omission policy");
            prime_need_recording_leave(&scope);
            PrimeNeedRecordingStatus status=prime_need_recording_status(group);
            CHECK(status.incomplete && status.omitted>0u,
                "a bounded omitted State observation never claims complete history");
        }
        if(group)prime_need_recording_release(group);
        PrimeNeedBranchState state_copy=states[5];
        PrimeNeedReceipt receipt_copy=receipts[5];
        bool promoted=prime_need_branch_state_promote(&copied,&state_copy) &&
            prime_need_receipt_promote(&copied,&receipt_copy);
        CHECK(promoted && prime_need_receipt_equal(&receipt_copy,&receipts[5]),
            "promotion preserves exact operational State links and observer identities");
        if(promoted) {
            arena_free(&operations);arena_init(&operations);
            arena_free(&observations);arena_init(&observations);
            for(size_t i=0u;i<5u;i++) {
                PrimeNeedReceiptEvent observed={0};
                CHECK(prime_need_receipt_event_at(&receipt_copy,i,&observed) && observed.state_event_id==ids[i],
                    "promoted correlation survives retirement of both source arenas");
            }
            PrimeNeedReceipt later={0};PrimeNeedReceiptEvent event={0};
            CHECK(prime_need_receipt_record_state(&copied,&receipt_copy,&state_copy,&later) &&
                prime_need_receipt_event_at(&later,5u,&event) && event.state_event_id==ids[4],
                "an arena-promoted operational frame retains its canonical State identity");
        }
    }
    arena_free(&copied);arena_free(&observations);arena_free(&operations);arena_free(&values);
}
#endif

typedef struct {size_t references;unsigned identity;} ReaderCopyProbe;
static size_t reader_copy_live;
static bool reader_copy_probe_retain(void *raw) {
    ReaderCopyProbe *probe=raw;
    if(!probe||!probe->references||probe->references==SIZE_MAX)return false;
    probe->references++;return true;
}
static void reader_copy_probe_release(void *raw) {
    ReaderCopyProbe *probe=raw;
    if(probe&&!--probe->references){assert(reader_copy_live);reader_copy_live--;free(probe);}
}
static bool reader_copy_probe_payloads(const void *raw,AtomTreePredicate atoms,
        bool (*cells)(const StateCell *,void *),void *context) {
    (void)raw;(void)atoms;(void)cells;(void)context;return true;
}
static const char reader_copy_probe_kind;
static const PrimeNeedReaderValueOps reader_copy_probe_ops={
    .copy_kind=&reader_copy_probe_kind,.copy_size=sizeof(ReaderCopyProbe),
    .retain=reader_copy_probe_retain,.release=reader_copy_probe_release,
    .payloads=reader_copy_probe_payloads};
typedef struct {
    ReaderCopyProbe *source[2],*image[2];
    bool refuse;
    size_t calls, fail_at;
} ReaderCopyFixture;
static CettaMatchCompletion reader_copy_probe_map(void *raw,
        const PrimeNeedReaderValue *source,PrimeNeedReaderValue *image) {
    ReaderCopyFixture *copy=raw;
    copy->calls++;
    if(copy->refuse || (copy->fail_at && copy->calls==copy->fail_at) || source->operations!=&reader_copy_probe_ops) {
        *image=*source; /* Borrowed scratch output: failure grants no ownership. */
        return CETTA_MATCH_INCOMPLETE;
    }
    size_t index=source->value==copy->source[0]?0u:
        source->value==copy->source[1]?1u:SIZE_MAX;
    if(index==SIZE_MAX)return CETTA_MATCH_INCOMPLETE;
    copy->image[index]=calloc(1u,sizeof(*copy->image[index]));
    if(!copy->image[index])return CETTA_MATCH_INCOMPLETE;
    reader_copy_live++;
    *copy->image[index]=(ReaderCopyProbe){.references=1u,
        .identity=copy->source[index]->identity};
    *image=(PrimeNeedReaderValue){.value=copy->image[index],
        .operations=&reader_copy_probe_ops};
    return CETTA_MATCH_MATCHED;
}
static void check_reader_context_copy(void) {
    ReaderCopyFixture copy={0};
    for(size_t i=0u;i<2u;i++) {
        copy.source[i]=calloc(1u,sizeof(*copy.source[i]));
        if(copy.source[i])reader_copy_live++;
        CHECK(copy.source[i]!=NULL,"reader-copy source probe allocation succeeds");
        if(copy.source[i])*copy.source[i]=(ReaderCopyProbe){.references=1u,
            .identity=(unsigned)i+1u};
    }
    if(!copy.source[0]||!copy.source[1]) {
        reader_copy_probe_release(copy.source[0]);
        reader_copy_probe_release(copy.source[1]);return;
    }
    PrimeNeedReaderContext *base=NULL,*descendant=NULL,*tombstone=NULL;
    PrimeNeedReaderValue first={.value=copy.source[0],.operations=&reader_copy_probe_ops};
    PrimeNeedReaderValue second={.value=copy.source[1],.operations=&reader_copy_probe_ops};
    CHECK(prime_need_reader_context_update(NULL,11u,&first,&base)==CETTA_MATCH_MATCHED &&
          prime_need_reader_context_update(base,17u,&second,&descendant)==CETTA_MATCH_MATCHED &&
          prime_need_reader_context_update(descendant,11u,NULL,&tombstone)==CETTA_MATCH_MATCHED,
        "reader-copy fixture builds ancestry and a tombstone");
    reader_copy_probe_release(copy.source[0]);
    reader_copy_probe_release(copy.source[1]);
    Arena arena;arena_init_detached(&arena);
    AtomDeepCopySession *session=atom_deep_copy_session_new(&arena);
    PrimeNeedReaderContext *base_image=NULL,*descendant_image=NULL,*again=NULL,*tombstone_image=NULL;
    CettaMatchCompletion status=session?prime_need_reader_context_prepare_copy_with_session(
        base,session,reader_copy_probe_map,&copy,&base_image):CETTA_MATCH_INCOMPLETE;
    status=status==CETTA_MATCH_MATCHED?prime_need_reader_context_prepare_copy_with_session(
        descendant,session,reader_copy_probe_map,&copy,&descendant_image):status;
    status=status==CETTA_MATCH_MATCHED?prime_need_reader_context_prepare_copy_with_session(
        descendant,session,reader_copy_probe_map,&copy,&again):status;
    status=status==CETTA_MATCH_MATCHED?prime_need_reader_context_prepare_copy_with_session(
        tombstone,session,reader_copy_probe_map,&copy,&tombstone_image):status;
    PrimeNeedReaderValue observed={0};
    CHECK(status==CETTA_MATCH_MATCHED&&base_image&&descendant_image&&again==descendant_image&&
          prime_need_reader_context_lookup(descendant_image,11u,&observed)&&
          observed.value==copy.image[0]&&observed.value!=copy.source[0]&&
          !prime_need_reader_context_lookup(tombstone_image,11u,&observed)&&
          prime_need_reader_context_lookup(tombstone_image,17u,&observed)&&
          observed.value==copy.image[1],
        "reader context copy preserves mapped values, repeated identity and tombstones");
    PrimeNeedReaderContext *merged=NULL;
    CHECK(prime_need_reader_context_merge(base_image,descendant_image,&merged)==CETTA_MATCH_MATCHED&&
          merged==descendant_image,
        "reader context copy preserves ancestry used by later merges");
    CHECK(copy.calls==2u,"the session memoizes owners without callback-side caching");
    size_t live_before=reader_copy_live;
    ReaderCopyFixture mid={.source={copy.source[0],copy.source[1]},.fail_at=2u};
    Arena mid_arena;arena_init_detached(&mid_arena);
    AtomDeepCopySession *mid_session=atom_deep_copy_session_new(&mid_arena);
    PrimeNeedReaderContext *mid_out=base_image;
    prime_need_reader_context_retain(mid_out);
    bool failed=mid_session && prime_need_reader_context_prepare_copy_with_session(
        descendant,mid_session,reader_copy_probe_map,&mid,&mid_out)==CETTA_MATCH_INCOMPLETE;
    bool held=failed && mid_out==base_image && mid.calls==2u &&
        reader_copy_live==live_before+1u;
    CHECK(held,"a later preparation failure retains earlier mapped owners until disposal");
    CHECK(held && prime_need_reader_context_prepare_copy_with_session(
        descendant,mid_session,reader_copy_probe_map,&mid,&mid_out)==CETTA_MATCH_INCOMPLETE &&
        mid.calls==2u,"a failed preparation cannot publish on retry");
    prime_need_reader_context_release(mid_out);
    atom_deep_copy_session_free(mid_session);arena_free(&mid_arena);
    CHECK(reader_copy_live==live_before,"failed copy disposal releases every prepared owner");
    ReaderCopyFixture refusing=copy;refusing.refuse=true;
    Arena refused_arena;arena_init_detached(&refused_arena);
    AtomDeepCopySession *refused=atom_deep_copy_session_new(&refused_arena);
    PrimeNeedReaderContext *unchanged=base_image;
    prime_need_reader_context_retain(unchanged);
    CHECK(refused&&prime_need_reader_context_prepare_copy_with_session(descendant,refused,
              reader_copy_probe_map,&refusing,&unchanged)==CETTA_MATCH_INCOMPLETE&&
          unchanged==base_image,
        "reader context copy refusal leaves the destination owner unchanged");
    prime_need_reader_context_release(unchanged);
    atom_deep_copy_session_free(refused);arena_free(&refused_arena);
    PrimeNeedReaderContext *prior_image=base_image;
    prime_need_reader_context_retain(prior_image);
    CHECK(prime_need_reader_context_prepare_copy_with_session(base,session,NULL,NULL,
        &prior_image)==CETTA_MATCH_INCOMPLETE && prior_image==base_image,
        "early preparation refusal preserves the previous output");
    CHECK(!prime_need_reader_context_copy_lookup(base,session) &&
        prime_need_reader_context_copy_with_session(base,&arena,session,&prior_image)==CETTA_MATCH_INCOMPLETE &&
        prior_image==base_image,"a mixed success/failure session refuses further reader publication");
    prime_need_reader_context_release(prior_image);
    prime_need_reader_context_release(merged);
    prime_need_reader_context_release(tombstone_image);
    prime_need_reader_context_release(again);
    prime_need_reader_context_release(descendant_image);
    prime_need_reader_context_release(base_image);
    atom_deep_copy_session_free(session);arena_free(&arena);
    prime_need_reader_context_release(tombstone);
    prime_need_reader_context_release(descendant);
    prime_need_reader_context_release(base);
    CHECK(reader_copy_live==0u,"all source and image probe owners retire after session cleanup");
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    Arena branch_arena;
    Arena empty_suffix_arena;
    Arena snapshot_branch_arena;
    Arena promoted_arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    arena_init(&branch_arena);
    arena_init(&empty_suffix_arena);
    arena_init(&snapshot_branch_arena);
    arena_init(&promoted_arena);

    check_reader_context_copy();
    interleaved_history_controls(&arena, &promoted_arena, &branch_arena);
    check_production_view_import();
    check_state_production_import();
    check_production_view_write_cut();
    check_production_view_dependencies();
    check_snapshot_prefix_transport();
    check_branch_shared_prefix_compatibility();
    check_forwarded_production(&arena, &promoted_arena);
    check_private_need_suffix(&arena, &promoted_arena);

    PrimeNeedBranchState identity_carrier;
    prime_need_branch_state_init(&identity_carrier);
    PrimeNeedArenaAudit *identity_audit =
        prime_need_arena_audit_new(&branch_arena);
    CHECK(prime_need_branch_state_begin(
              &branch_arena, &identity_carrier) && identity_audit &&
              prime_need_arena_audit_branch_state(
                  identity_audit, &identity_carrier),
          "event-free branch-state identity owns no arena root");
    prime_need_arena_audit_free(identity_audit);
    uint64_t identity_session = identity_carrier.session_id;
    CHECK(prime_need_branch_state_promote(
              &promoted_arena, &identity_carrier) &&
              identity_carrier.owner == &promoted_arena &&
              identity_carrier.session_id == identity_session &&
              identity_carrier.top == NULL,
          "event-free branch-state identity rehomes without allocation");
    StateCell audit_cell = {
        .value = atom_int(&arena, 0),
        .content_type = atom_symbol(&arena, "Number"),
    };
    PrimeNeedBranchState event_base;
    PrimeNeedBranchState eventful_carrier;
    prime_need_branch_state_init(&event_base);
    PrimeNeedArenaAudit *eventful_audit =
        prime_need_arena_audit_new(&branch_arena);
    CHECK(prime_need_branch_state_begin(&branch_arena, &event_base) &&
              prime_need_branch_state_write(
              &branch_arena, &event_base, &audit_cell,
              audit_cell.value, atom_int(&arena, 1),
              &eventful_carrier) && eventful_audit &&
              !prime_need_arena_audit_branch_state(
                  eventful_audit, &eventful_carrier),
          "event-bearing branch state retains its exact owner arena");
    prime_need_arena_audit_free(eventful_audit);

    PrimeNeedBranchState suffix_identity;
    prime_need_branch_state_init(&suffix_identity);
    CHECK(prime_need_branch_state_begin(
              &empty_suffix_arena, &suffix_identity) &&
              prime_need_branch_state_promote_suffix(
                  &promoted_arena, &empty_suffix_arena,
                  &suffix_identity) &&
              suffix_identity.owner == &promoted_arena &&
              suffix_identity.top == NULL,
          "empty suffix promotion rehomes future allocation authority");
    arena_free(&empty_suffix_arena);
    arena_init(&empty_suffix_arena);
    Atom *reused_region_marker = atom_symbol(
        &empty_suffix_arena, "reused-region");
    PrimeNeedBranchState suffix_first_event;
    CHECK(reused_region_marker &&
              prime_need_branch_state_write(
                  &empty_suffix_arena, &suffix_identity, &audit_cell,
                  audit_cell.value, atom_int(&arena, 2),
                  &suffix_first_event) &&
              suffix_first_event.owner == &promoted_arena &&
              prime_need_branch_state_has_events(&suffix_first_event) &&
              prime_need_branch_state_excludes_arena(
                  &suffix_first_event, &empty_suffix_arena) &&
              !prime_need_branch_state_excludes_arena(
                  &suffix_first_event, &promoted_arena),
          "first event after region reuse allocates in promoted owner");

    PrimeNeedSnapshot root;
    prime_need_snapshot_init(&root);
    CHECK(!prime_need_snapshot_present(&root), "empty snapshot is absent");
    CHECK(prime_need_snapshot_begin(&root), "snapshot session begins");
    CHECK(prime_need_snapshot_present(&root), "begun snapshot is present");

    PrimeNeedSnapshot thunk;
    uint64_t thunk_id = 0u;
    Atom *term = atom_symbol(&arena, "delayed");
    CHECK(prime_need_snapshot_allocate(&arena, &root, term, &thunk,
                                       &thunk_id),
          "allocate extends the root");
    CHECK(!prime_need_snapshot_excludes_arena(&thunk, &arena) &&
              prime_need_snapshot_excludes_arena(
                  &thunk, &promoted_arena),
          "snapshot ownership audit distinguishes its owner arena");
    CHECK(prime_need_snapshot_is_ancestor(&root, &thunk),
          "root precedes allocation");
    PrimeNeedCellView cell;
    CHECK(prime_need_snapshot_lookup(&thunk, thunk_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EMPTY &&
              cell.origin == term && cell.cached == NULL &&
              cell.authority_id != 0u && cell.evaluator_id == 0u,
          "allocation stores an immutable origin and empty cache");
    Atom *branch_term = atom_symbol(
        &snapshot_branch_arena, "branch-delayed");
    PrimeNeedSnapshot branch_thunk;
    uint64_t branch_thunk_id = 0u;
    CHECK(prime_need_snapshot_allocate(
              &snapshot_branch_arena, &thunk, branch_term,
              &branch_thunk, &branch_thunk_id) &&
              branch_thunk.owner == &snapshot_branch_arena &&
              branch_thunk.closure_owner == NULL &&
              !prime_need_snapshot_excludes_arena(
                  &branch_thunk, &arena) &&
              !prime_need_snapshot_excludes_arena(
                  &branch_thunk, &snapshot_branch_arena),
          "snapshot branch suffix retains its longer-lived prefix");
    PrimeNeedSnapshot branch_before_promotion = branch_thunk;
    CHECK(prime_need_snapshot_promote_suffix(
              &arena, &snapshot_branch_arena, &branch_thunk) &&
              branch_thunk.owner == &arena &&
              branch_thunk.closure_owner == &arena &&
              prime_need_snapshot_excludes_arena(
                  &branch_thunk, &snapshot_branch_arena) &&
              history_transport_controls(&branch_before_promotion, &branch_thunk),
          "snapshot suffix promotion closes directly into its stable parent");
    arena_free(&snapshot_branch_arena);
    PrimeNeedCellView branch_cell;
    CHECK(prime_need_snapshot_lookup(
              &branch_thunk, branch_thunk_id, &branch_cell) &&
              branch_cell.origin && atom_is_symbol(
                  branch_cell.origin, "branch-delayed") &&
              prime_need_snapshot_is_ancestor(&thunk, &branch_thunk),
          "promoted snapshot survives reclamation of its branch arena");
    PrimeNeedSnapshot promoted_thunk = thunk;
    CHECK(prime_need_snapshot_promote(
              &promoted_arena, &promoted_thunk) &&
              prime_need_snapshot_excludes_arena(
                  &promoted_thunk, &arena) &&
              !prime_need_snapshot_excludes_arena(
                  &promoted_thunk, &promoted_arena),
          "snapshot promotion removes every source-arena frame");
    uint64_t storage_key = cell.storage_key;
    PrimeNeedSnapshot rehydrated;
    uint64_t rehydrated_id = 0u;
    CHECK(storage_key != 0u &&
              prime_need_snapshot_allocate_persisted(
                  &arena, &thunk, term, storage_key,
                  &rehydrated, &rehydrated_id) &&
              rehydrated_id == thunk_id && rehydrated.top == thunk.top,
          "rehydration reuses one cell for one stored graph key");
    Atom *different_origin = atom_symbol(&arena, "different-origin");
    PrimeNeedSnapshot instantiated;
    uint64_t instantiated_id = 0u;
    PrimeNeedCellView instantiated_cell;
    CHECK(prime_need_snapshot_allocate_persisted(
              &arena, &thunk, different_origin, storage_key,
              &instantiated, &instantiated_id) &&
              instantiated_id != thunk_id &&
              prime_need_snapshot_lookup(
                  &instantiated, instantiated_id, &instantiated_cell) &&
              instantiated_cell.storage_key != storage_key &&
              instantiated_cell.origin == different_origin,
          "substitution under one stored key allocates a fresh graph node");
    PrimeNeedSnapshot instantiated_again;
    uint64_t instantiated_again_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted(
              &arena, &instantiated, different_origin, storage_key,
              &instantiated_again, &instantiated_again_id) &&
              instantiated_again_id == instantiated_id &&
              instantiated_again.top == instantiated.top,
          "one imported key and instantiated origin reuse one graph node");
    PrimeNeedSnapshot original_again;
    uint64_t original_again_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted(
              &arena, &instantiated, term, storage_key,
              &original_again, &original_again_id) &&
              original_again_id == thunk_id &&
              original_again.top == instantiated.top,
          "the original stored key and origin still recover the original node");
    PrimeNeedSnapshot second_thunk;
    uint64_t second_thunk_id = 0u;
    PrimeNeedCellView second_cell;
    CHECK(prime_need_snapshot_allocate(
              &arena, &thunk, term, &second_thunk, &second_thunk_id) &&
              prime_need_snapshot_lookup(
                  &second_thunk, second_thunk_id, &second_cell) &&
              second_cell.storage_key != storage_key,
          "two ordinary delays remain distinct graph nodes");
    uint64_t source_occurrence = prime_need_fresh_source_occurrence();
    uint64_t next_source_occurrence = prime_need_fresh_source_occurrence();
    CHECK(source_occurrence != 0u &&
              next_source_occurrence != 0u &&
              source_occurrence != next_source_occurrence,
          "dynamic source occurrences have distinct nonzero identities");
    PrimeNeedSnapshot source_argument;
    uint64_t source_argument_id = 0u;
    PrimeNeedCellView source_argument_cell;
    CHECK(prime_need_snapshot_allocate_source_argument(
              &arena, &thunk, term, source_occurrence, 2u,
              PRIME_NEED_SOURCE_INFERRED,
              &source_argument, &source_argument_id) &&
              prime_need_snapshot_lookup(
                  &source_argument, source_argument_id,
                  &source_argument_cell) &&
              source_argument_cell.source_occurrence_id ==
                  source_occurrence &&
              source_argument_cell.source_argument_index == 2u &&
              source_argument_cell.source_role == PRIME_NEED_SOURCE_INFERRED,
          "a source argument cell retains application and position identity");
    CHECK(source_argument_cell.value_role ==
                  PRIME_NEED_VALUE_DEMAND_RESULT &&
              source_argument_cell.cache_state == PRIME_NEED_CACHE_EMPTY,
          "a source argument cell is a delayed computation");

    Atom *ready_value = atom_expr2(
        &arena, atom_symbol(&arena, "Box"), atom_int(&arena, 7));
    PrimeNeedSnapshot ready;
    uint64_t ready_id = 0u;
    PrimeNeedCellView ready_cell;
    CHECK(prime_need_snapshot_allocate_ready_value(
              &arena, &source_argument, ready_value,
              &ready, &ready_id) &&
              prime_need_snapshot_lookup(
                  &ready, ready_id, &ready_cell) &&
              ready_cell.value_role == PRIME_NEED_VALUE_READY &&
              ready_cell.cache_state == PRIME_NEED_CACHE_VALUE &&
              ready_cell.origin == ready_value &&
              ready_cell.cached == ready_value &&
              prime_need_snapshot_is_ancestor(&source_argument, &ready),
          "a ready value is a cell born resolved to itself");
    PrimeNeedSnapshot ready_restarted;
    CHECK(!prime_need_snapshot_start_evaluation(
              &arena, &ready, ready_id, 17u, &ready_restarted),
          "a ready value cannot be demanded into evaluation again");
    PrimeNeedSnapshot ready_promoted = ready;
    PrimeNeedCellView ready_promoted_cell;
    CHECK(prime_need_snapshot_promote(&promoted_arena, &ready_promoted) &&
              prime_need_snapshot_lookup(
                  &ready_promoted, ready_id,
                  &ready_promoted_cell) &&
              ready_promoted_cell.value_role ==
                  PRIME_NEED_VALUE_READY &&
              ready_promoted_cell.cache_state ==
                  PRIME_NEED_CACHE_VALUE &&
              atom_eq(ready_promoted_cell.cached, ready_value),
          "promotion keeps a ready value ready");
    PrimeNeedSnapshot answering;
    PrimeNeedSnapshot answered;
    PrimeNeedCellView answered_cell;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &source_argument, source_argument_id, 19u,
              &answering) &&
              prime_need_snapshot_resolve_ready_value(
                  &arena, &answering, source_argument_id, 19u,
                  ready_value, &answered) &&
              prime_need_snapshot_lookup(
                  &answered, source_argument_id, &answered_cell) &&
              answered_cell.value_role == PRIME_NEED_VALUE_READY &&
              answered_cell.cache_state == PRIME_NEED_CACHE_VALUE &&
              answered_cell.cached == ready_value &&
              answered_cell.origin == term,
          "a computation answered by a ready value becomes ready");
    PrimeNeedSnapshot answered_ordinary;
    PrimeNeedCellView ordinary_cell;
    CHECK(prime_need_snapshot_resolve_value(
              &arena, &answering, source_argument_id, 19u,
              ready_value, &answered_ordinary) &&
              prime_need_snapshot_lookup(
                  &answered_ordinary, source_argument_id, &ordinary_cell) &&
              ordinary_cell.value_role ==
                  PRIME_NEED_VALUE_DEMAND_RESULT,
          "an ordinary answer leaves its cell a computation");

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId capture_ids[] = {var_id_from_base(202u), var_id_from_base(101u), var_id_from_base(202u)};
    PrimeNeedSnapshot captured;
    uint64_t captured_id = 0u;
    PrimeNeedCellView captured_cell;
    CHECK(prime_need_snapshot_allocate_closure(
              &arena, &source_argument, term,
              capture_ids, 3u, &captured, &captured_id) &&
              prime_need_snapshot_lookup(
                  &captured, captured_id, &captured_cell) &&
              captured_cell.capture_known &&
              captured_cell.capture_var_count == 2u &&
              var_id_eq(captured_cell.capture_var_ids[0], var_id_from_base(101u)) &&
              var_id_eq(captured_cell.capture_var_ids[1], var_id_from_base(202u)),
          "closure allocation stores a canonical exact capture set");
    PrimeNeedSnapshot captured_evaluating;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &captured, captured_id, 13u,
              &captured_evaluating) &&
              prime_need_snapshot_lookup(
                  &captured_evaluating, captured_id,
                  &captured_cell) &&
              captured_cell.capture_known &&
              captured_cell.capture_var_count == 2u &&
              var_id_eq(captured_cell.capture_var_ids[0], var_id_from_base(101u)) &&
              var_id_eq(captured_cell.capture_var_ids[1], var_id_from_base(202u)),
          "cache transitions preserve exact closure captures");
    PrimeNeedSnapshot empty_capture;
    uint64_t empty_capture_id = 0u;
    PrimeNeedCellView empty_capture_cell;
    CHECK(prime_need_snapshot_allocate_closure(
              &arena, &captured, term, NULL, 0u,
              &empty_capture, &empty_capture_id) &&
              prime_need_snapshot_lookup(
                  &empty_capture, empty_capture_id,
                  &empty_capture_cell) &&
              empty_capture_cell.capture_known &&
              empty_capture_cell.capture_var_count == 0u &&
              empty_capture_cell.capture_var_ids == NULL,
          "an exact empty capture is distinct from unavailable metadata");
    PrimeNeedCellView legacy_capture_cell;
    CHECK(prime_need_snapshot_lookup(
              &thunk, thunk_id, &legacy_capture_cell) &&
              !legacy_capture_cell.capture_known &&
              legacy_capture_cell.capture_var_count == 0u,
          "legacy allocation remains an explicit fail-closed fallback");
    PrimeNeedSnapshot captured_persisted;
    uint64_t captured_persisted_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted_closure(
              &arena, &source_argument, term, UINT64_C(8128),
              capture_ids, 3u, &captured_persisted,
              &captured_persisted_id),
          "persisted closure allocation admits an exact capture set");
    VarId same_capture_ids[] = {var_id_from_base(101u), var_id_from_base(202u)};
    PrimeNeedSnapshot captured_reused;
    uint64_t captured_reused_id = 0u;
    CHECK(prime_need_snapshot_allocate_persisted_closure(
              &arena, &captured_persisted, term, UINT64_C(8128),
              same_capture_ids, 2u, &captured_reused,
              &captured_reused_id) &&
              captured_reused_id == captured_persisted_id &&
              captured_reused.top == captured_persisted.top,
          "persisted closure identity accepts the same capture set");
    VarId conflicting_capture_id = var_id_from_base(303u);
    CHECK(!prime_need_snapshot_allocate_persisted_closure(
              &arena, &captured_persisted, term, UINT64_C(8128),
              &conflicting_capture_id, 1u, &captured_reused,
              &captured_reused_id),
          "persisted closure identity rejects conflicting capture metadata");
#endif

    Atom *ref = prime_need_ref(&arena, &thunk, thunk_id);
    uint64_t parsed_id = 0u;
    uint64_t parsed_authority = 0u;
    CHECK(ref && prime_need_ref_parse(ref, NULL, &parsed_id,
                                     &parsed_authority) &&
              prime_need_ref_belongs_to(ref, &thunk, &parsed_id) &&
              parsed_id == thunk_id && parsed_authority == cell.authority_id,
          "opaque capability round-trips within its session");
    Atom *ordinary_graph = atom_expr2(
        &arena, atom_int(&arena, 1), atom_int(&arena, 2));
    CHECK(ordinary_graph && !atom_has_registry_refs(ordinary_graph),
          "ordinary immutable data proves it contains no private reference");
    Atom *capability_graph = atom_expr2(
        &arena, atom_symbol(&arena, "box"), ref);
    CHECK(capability_graph && atom_has_registry_refs(ref) &&
              atom_has_registry_refs(capability_graph),
          "private-reference reachability propagates through its container");
    PrimeNeedSnapshot other_root;
    prime_need_snapshot_init(&other_root);
    CHECK(prime_need_snapshot_begin(&other_root) &&
              !prime_need_ref_belongs_to(ref, &other_root, NULL),
          "capability cannot cross a session boundary");
    Atom *source_forgery = atom_expr3(
        &arena, atom_symbol(&arena, "__prime_need_ref_v1"),
        atom_int(&arena, (int64_t)thunk.session_id),
        atom_int(&arena, (int64_t)thunk_id));
    CHECK(!prime_need_ref_parse(source_forgery, NULL, NULL, NULL) &&
              !prime_need_ref_belongs_to(source_forgery, &thunk, NULL),
          "source-shaped sequential IDs cannot forge a capability");
    Atom *wrong_authority = atom_prime_need_capability(
        &arena, thunk.session_id, thunk_id, cell.authority_id + 1u);
    CHECK(wrong_authority &&
              !prime_need_ref_belongs_to(wrong_authority, &thunk, NULL),
          "authority mismatch fails closed");

    uint32_t parsed_rights = 0u;
    CHECK(prime_need_ref_parse_rights(
              ref, NULL, NULL, NULL, &parsed_rights) &&
              parsed_rights == CETTA_PRIME_NEED_RIGHT_ALL,
          "new suspensions begin with force and inspect rights");
    Atom *inspect_ref = prime_need_ref_restrict(
        &arena, ref, &thunk, CETTA_PRIME_NEED_RIGHT_INSPECT);
    Atom *force_ref = prime_need_ref_restrict(
        &arena, ref, &thunk, CETTA_PRIME_NEED_RIGHT_FORCE);
    CHECK(inspect_ref &&
              prime_need_ref_belongs_to(inspect_ref, &thunk, NULL) &&
              prime_need_ref_belongs_to_with_rights(
                  inspect_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
              !prime_need_ref_belongs_to_with_rights(
                  inspect_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
          "inspect-only attenuation preserves identity but removes force");
    CHECK(force_ref &&
              prime_need_ref_belongs_to(force_ref, &thunk, NULL) &&
              prime_need_ref_belongs_to_with_rights(
                  force_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_FORCE, NULL) &&
              !prime_need_ref_belongs_to_with_rights(
                  force_ref, &thunk,
                  CETTA_PRIME_NEED_RIGHT_INSPECT, NULL),
          "force-only attenuation preserves identity but removes inspection");
    CHECK(!prime_need_ref_restrict(
              &arena, inspect_ref, &thunk,
              CETTA_PRIME_NEED_RIGHT_ALL),
          "attenuated rights cannot be restored");
    Atom *inspect_copy = atom_deep_copy(&arena, inspect_ref);
    CHECK(inspect_copy && atom_eq(inspect_copy, inspect_ref) &&
              !atom_eq(inspect_copy, ref) &&
              !prime_need_ref_belongs_to_with_rights(
                  inspect_copy, &thunk,
                  CETTA_PRIME_NEED_RIGHT_FORCE, NULL),
          "copying preserves an attenuated rights set");

    capability_record_controls(&arena, &source_argument, &root, source_argument_id);

    PrimeNeedSnapshot evaluating;
    const uint64_t evaluator_id = 17u;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &thunk, thunk_id, evaluator_id, &evaluating),
          "force enters the evaluating state");
    CHECK(prime_need_snapshot_lookup(&evaluating, thunk_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EVALUATING &&
              cell.origin == term && cell.cached == NULL &&
              cell.evaluator_id == evaluator_id,
          "evaluation preserves origin and records its owner");
    PrimeNeedSnapshot invalid;
    CHECK(!prime_need_snapshot_start_evaluation(
              &arena, &evaluating, thunk_id, evaluator_id, &invalid),
          "an evaluating cell cannot be entered twice");
    CHECK(!prime_need_snapshot_resolve_value(
              &arena, &evaluating, thunk_id, evaluator_id + 1u,
              atom_symbol(&arena, "wrong-owner"), &invalid),
          "a different evaluator cannot resolve the demand");

    {
    /* Equal syntax can have distinct demand/value roles. Neither a role nor
     * a copied cache grants a different evaluator permission to complete. */
    Atom *ready_value = atom_expr3(&arena, atom_symbol(&arena, "+"),
                                  atom_int(&arena, 1), atom_int(&arena, 2));
    PrimeNeedSnapshot ready, ordinary;
    CHECK(prime_need_snapshot_resolve_ready_value(&arena, &evaluating,
              thunk_id, evaluator_id, ready_value, &ready) &&
          prime_need_snapshot_resolve_value(&arena, &evaluating,
              thunk_id, evaluator_id, ready_value, &ordinary),
          "the same source has distinct produced-value and demand-result updates");
    PrimeNeedCellView ready_cell = {0}, ordinary_cell = {0};
    CHECK(prime_need_snapshot_lookup(&ready, thunk_id, &ready_cell) &&
          prime_need_snapshot_lookup(&ordinary, thunk_id, &ordinary_cell) &&
          ready_cell.cache_state == PRIME_NEED_CACHE_VALUE &&
          ready_cell.value_role == PRIME_NEED_VALUE_READY &&
          ordinary_cell.value_role == PRIME_NEED_VALUE_DEMAND_RESULT &&
          ready_cell.cached == ordinary_cell.cached &&
          ready_cell.origin == term && ordinary_cell.origin == term,
          "ready syntax is an immutable update property, not pointer classification");
    uint64_t ready_update = ready_cell.update_id;
    CHECK(ready_update && ordinary_cell.update_id &&
          ready_update != ordinary_cell.update_id &&
          !prime_need_snapshot_is_ancestor(&ready, &ordinary) &&
          !prime_need_snapshot_is_ancestor(&ordinary, &ready),
          "sibling equal-value productions keep different update identities");
    PrimeNeedSnapshot role_refused = evaluating;
    CHECK(!prime_need_snapshot_resolve_ready_value(&arena, &evaluating,
              thunk_id, evaluator_id + 1u, ready_value, &role_refused) &&
          role_refused.top == evaluating.top &&
          !prime_need_snapshot_resolve_ready_value(&arena, &ready,
              thunk_id, evaluator_id, ready_value, &role_refused) &&
          role_refused.top == evaluating.top,
          "wrong owner and repeated ready completion refuse without publication");
    PrimeNeedSnapshot ready_image = ready;
    CHECK(prime_need_snapshot_promote(&promoted_arena, &ready_image) &&
          prime_need_snapshot_lookup(&ready_image, thunk_id, &ready_cell) &&
          ready_cell.value_role == PRIME_NEED_VALUE_READY &&
          atom_eq(ready_cell.cached, ready_value) &&
          ready_cell.update_id == ready_update,
          "full promotion preserves ready role, content, and update identity");
    Arena role_suffix, role_image;
    arena_init_detached(&role_suffix);
    arena_init_detached(&role_image);
    PrimeNeedSnapshot suffix_ready;
    CHECK(prime_need_snapshot_resolve_ready_value(&role_suffix, &evaluating,
              thunk_id, evaluator_id, ready_value, &suffix_ready) &&
          prime_need_snapshot_promote_suffix(&role_image, &role_suffix, &suffix_ready) &&
          prime_need_snapshot_lookup(&suffix_ready, thunk_id, &ready_cell) &&
          ready_cell.value_role == PRIME_NEED_VALUE_READY &&
          prime_need_snapshot_excludes_arena(&suffix_ready, &role_suffix),
          "suffix promotion retains the value role after retiring its source region");
    arena_free(&role_suffix);
    CHECK(prime_need_snapshot_lookup(&suffix_ready, thunk_id, &ready_cell) &&
          ready_cell.value_role == PRIME_NEED_VALUE_READY &&
          atom_eq(ready_cell.cached, ready_value),
          "the promoted ready cache remains usable after source-region teardown");
    arena_free(&role_image);

    }

    PrimeNeedSnapshot faulted;
    Atom *fault_value = atom_symbol(&arena, "fault");
    CHECK(prime_need_snapshot_resolve_stable_fault(
              &arena, &evaluating, thunk_id, evaluator_id,
              fault_value, &faulted),
          "a certified stable fault is memoized");
    PrimeNeedCellView repeated_fault;
    CHECK(prime_need_snapshot_lookup(&faulted, thunk_id, &cell) &&
              prime_need_snapshot_lookup(&faulted, thunk_id,
                                         &repeated_fault) &&
              cell.cache_state == PRIME_NEED_CACHE_STABLE_FAULT &&
              repeated_fault.cache_state ==
                  PRIME_NEED_CACHE_STABLE_FAULT &&
              cell.cached == repeated_fault.cached,
          "repeated fault lookup returns the cached receipt");
    CHECK(cell.origin == term && cell.cached == fault_value &&
              history_readback_controls(&root, &thunk, &evaluating,
                  &faulted, thunk_id, term, fault_value, evaluator_id,
                  PRIME_NEED_CACHE_STABLE_FAULT),
          "stable-fault caching does not overwrite the origin");

    PrimeNeedSnapshot left_evaluating;
    PrimeNeedSnapshot right_evaluating;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &thunk, thunk_id, 21u, &left_evaluating) &&
              prime_need_snapshot_start_evaluation(
                  &arena, &thunk, thunk_id, 22u, &right_evaluating),
          "sibling evaluations may refine the same empty ancestor");
    PrimeNeedSnapshot left;
    PrimeNeedSnapshot right;
    Atom *left_value = atom_symbol(&arena, "left");
    Atom *right_value = atom_symbol(&arena, "right");
    CHECK(prime_need_snapshot_resolve_value(
              &arena, &left_evaluating, thunk_id, 21u, left_value, &left),
          "left branch memoizes its value");
    CHECK(prime_need_snapshot_resolve_value(
              &arena, &right_evaluating, thunk_id, 22u,
              right_value, &right),
          "right branch memoizes its value");
    CHECK(!prime_need_snapshot_is_ancestor(&left, &right) &&
              !prime_need_snapshot_is_ancestor(&right, &left) &&
              history_readback_controls(&root, &thunk, &left_evaluating,
                &left, thunk_id, term, left_value, 21u, PRIME_NEED_CACHE_VALUE) &&
              history_readback_controls(&root, &thunk, &right_evaluating,
                &right, thunk_id, term, right_value, 22u, PRIME_NEED_CACHE_VALUE) &&
              !history_transport_controls(&left, &right),
          "incomparable siblings retain exact independent inert histories");
    CHECK(prime_need_ref_belongs_to_with_rights(
              inspect_ref, &left, CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
              prime_need_ref_belongs_to_with_rights(
                  inspect_ref, &right, CETTA_PRIME_NEED_RIGHT_INSPECT, NULL) &&
              prime_need_snapshot_lookup(&left, thunk_id, &cell) && cell.cached == left_value &&
              prime_need_snapshot_lookup(&right, thunk_id, &cell) && cell.cached == right_value &&
              !atom_eq(left_value, right_value),
          "an inherited capability selects each retained sibling's own cache without merging worlds");
    PrimeNeedSnapshot rejected_merge = left;
    CHECK(!prime_need_snapshot_merge(&rejected_merge, &right),
          "sibling heaps cannot merge");
    CHECK(prime_need_snapshot_lookup(&rejected_merge, thunk_id, &cell) &&
              cell.cached == left_value && cell.origin == term,
          "failed merge leaves destination unchanged");

    PrimeNeedSnapshot ancestor_merge = thunk;
    CHECK(prime_need_snapshot_merge(&ancestor_merge, &left) &&
              prime_need_snapshot_lookup(&ancestor_merge, thunk_id, &cell) &&
              cell.cached == left_value,
          "ancestor merge selects the descendant");
    CHECK(prime_need_snapshot_merge(&ancestor_merge, &thunk) &&
              prime_need_snapshot_lookup(&ancestor_merge, thunk_id, &cell) &&
              cell.cached == left_value,
          "merging an ancestor cannot rewind a branch");

    PrimeNeedSnapshot retrying;
    PrimeNeedSnapshot retried;
    CHECK(prime_need_snapshot_start_evaluation(
              &arena, &thunk, thunk_id, 31u, &retrying) &&
              prime_need_snapshot_retry_evaluation(
                  &arena, &retrying, thunk_id, 31u, &retried),
          "uncertified faults return the cell to a retryable state");
    CHECK(prime_need_snapshot_lookup(&retried, thunk_id, &cell) &&
              cell.cache_state == PRIME_NEED_CACHE_EMPTY &&
              cell.origin == term && cell.cached == NULL &&
              history_readback_controls(&root, &thunk, &retrying,
                  &retried, thunk_id, term, NULL, 31u, PRIME_NEED_CACHE_EMPTY),
          "retry preserves the immutable origin");

    PrimeNeedSnapshot promoted = left;
    CHECK(prime_need_snapshot_promote(&promoted_arena, &promoted),
          "snapshot promotes across arena ownership");
    CHECK(prime_need_snapshot_is_ancestor(&thunk, &promoted) &&
              prime_need_snapshot_lookup(&promoted, thunk_id, &cell) &&
              atom_is_symbol(cell.origin, "delayed") &&
              atom_is_symbol(cell.cached, "left") &&
              prime_need_ref_belongs_to(ref, &promoted, NULL) &&
              history_transport_controls(&left, &promoted),
          "promotion preserves lineage, origin, cache, and authority");
    CHECK(!prime_need_snapshot_start_evaluation(
              &arena, &left, thunk_id + 1u, 41u, &invalid),
          "unknown thunk transition fails closed");
    CHECK(!prime_need_snapshot_resolve_stable_fault(
              &arena, &thunk, thunk_id, 41u, fault_value, &invalid),
          "resolution cannot skip the evaluating state");

#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    PrimeNeedSnapshot captured_promoted = captured_evaluating;
    CHECK(prime_need_snapshot_promote(
              &promoted_arena, &captured_promoted) &&
              prime_need_snapshot_lookup(
                  &captured_promoted, captured_id,
                  &captured_cell) &&
              captured_cell.capture_known &&
              captured_cell.capture_var_count == 2u &&
              var_id_eq(captured_cell.capture_var_ids[0], var_id_from_base(101u)) &&
              var_id_eq(captured_cell.capture_var_ids[1], var_id_from_base(202u)) &&
              history_transport_controls(&captured_evaluating, &captured_promoted),
          "promotion preserves exact closure captures");
    PrimeNeedHistoryIterator captured_history;
    PrimeNeedHistoryView captured_top = {0}, captured_previous = {0};
    CHECK(prime_need_snapshot_history_begin(&captured_promoted, &captured_history) &&
              prime_need_snapshot_history_next(&captured_history, &captured_top) == PRIME_NEED_HISTORY_ENTRY &&
              prime_need_snapshot_history_next(&captured_history, &captured_previous) == PRIME_NEED_HISTORY_ENTRY &&
              captured_top.thunk_id == captured_id && captured_previous.thunk_id == captured_id &&
              captured_top.cell.capture_known && captured_previous.cell.capture_known &&
              captured_top.cell.capture_var_count == 2u &&
              captured_previous.cell.capture_var_count == 2u &&
              captured_top.cell.capture_var_ids != captured_previous.cell.capture_var_ids &&
              var_id_eq(captured_top.cell.capture_var_ids[0], captured_previous.cell.capture_var_ids[0]) &&
              var_id_eq(captured_top.cell.capture_var_ids[1], captured_previous.cell.capture_var_ids[1]),
          "promoted historical updates preserve capture contents in separate arrays");
#endif

#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    check_receipt_graph_producers();
    check_cache_production_identity(&arena, &promoted_arena);
    check_bounded_recording();
    check_recording_refusal_boundaries();
    check_recording_private_slices();
    check_recording_scopes();
    check_recording_admission();
    check_recording_copy();
    check_nested_recording_publication();
    check_adopted_recording_copy();
    check_adoption_invalidates_current_copy();
    check_growing_recording_grants();
    check_recording_copy_owner_domain();
    check_recording_copy_mode();
    check_recording_carrier_copy();
    check_recording_copy_forgetting();
    check_recording_copy_mode_invalidation(false);
    check_recording_copy_mode_invalidation(true);
    for (unsigned mode = 0u; mode < 4u; mode++) check_recording_copy_refusal(mode);
    check_recording_copy_zero();
    check_receipt_graph(&arena, &promoted_arena, left_value);
    PrimeNeedReceipt receipt_root;
    prime_need_receipt_init(&receipt_root);
    CHECK(!prime_need_receipt_present(&receipt_root),
          "empty dependency receipt is absent");
    CHECK(prime_need_receipt_begin(&arena, &receipt_root) &&
              prime_need_receipt_present(&receipt_root) &&
              prime_need_receipt_event_count(&receipt_root) == 0u,
          "an evaluation episode has an explicit empty receipt root");
    CHECK(prime_need_receipt_excludes_arena(
              &receipt_root, &arena) &&
              prime_need_receipt_excludes_arena(
                  &receipt_root, &promoted_arena),
          "event-free receipt root excludes every arena");
    PrimeNeedReceipt empty_promoted = receipt_root;
    CHECK(prime_need_receipt_promote(&promoted_arena, &empty_promoted) &&
              prime_need_receipt_equal(&empty_promoted, &receipt_root),
          "the empty receipt root promotes without inventing an event");
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, thunk_id,
              left_value, &receipt_root) &&
              prime_need_receipt_present(&receipt_root),
          "a cell observation begins a causal receipt");
    PrimeNeedReceiptEvent receipt_event;
    CHECK(prime_need_receipt_event_count(&receipt_root) == 1u &&
              prime_need_receipt_event_at(
                  &receipt_root, 0u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_OBSERVE_CELL &&
              receipt_event.need_session_id == thunk.session_id &&
              receipt_event.thunk_id == thunk_id &&
              atom_eq(receipt_event.after, left_value),
          "cell receipt exposes semantic identity and observed outcome");

    PrimeNeedReceipt inspected_origin;
    CHECK(prime_need_receipt_inspect_origin(
              &arena, &receipt_root, thunk.session_id, thunk_id,
              term, &inspected_origin),
          "origin inspection appends a distinct receipt event");
    CHECK(prime_need_receipt_event_count(&inspected_origin) == 2u &&
              prime_need_receipt_event_at(
                  &inspected_origin, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_INSPECT_ORIGIN &&
              atom_eq(receipt_event.after, term),
          "inspection receipts identify the cell and disclosed origin");

    Atom *equation = atom_expr3(
        &arena, atom_symbol(&arena, "="), term, left_value);
    PrimeNeedReceipt used_equation;
    CHECK(prime_need_receipt_use_equation(
              &arena, &receipt_root, source_occurrence, 17u,
              equation, left_value, &used_equation) &&
              prime_need_receipt_event_at(
                  &used_equation, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_USE_EQUATION &&
              receipt_event.source_occurrence_id == source_occurrence &&
              receipt_event.rule_occurrence_id == 17u &&
              atom_eq(receipt_event.before, equation) &&
              atom_eq(receipt_event.after, left_value),
          "equation use retains dynamic call and admitted rule occurrence");
    PrimeNeedReceipt used_equation_ids;
    CHECK(prime_need_receipt_use_equation_ids(
              &arena, &receipt_root, source_occurrence, 18u,
              &used_equation_ids) &&
              prime_need_receipt_event_at(
                  &used_equation_ids, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_USE_EQUATION &&
              receipt_event.source_occurrence_id == source_occurrence &&
              receipt_event.rule_occurrence_id == 18u &&
              receipt_event.before == NULL &&
              receipt_event.after == NULL &&
              !prime_need_receipt_equal(
                  &used_equation_ids, &used_equation),
          "compact equation use keeps causal identity without display payload");
    PrimeNeedReceipt inherited_owner_equation;
    CHECK(prime_need_receipt_use_equation_ids(
              &branch_arena, &receipt_root, source_occurrence, 19u,
              &inherited_owner_equation) &&
              inherited_owner_equation.owner == &arena,
          "ordinary receipt extension inherits the causal base lifetime");
    PrimeNeedReceipt branch_local_equation;
    CHECK(prime_need_receipt_use_equation_ids_branch_local(
              &branch_arena, &receipt_root, source_occurrence, 20u,
              &branch_local_equation) &&
              branch_local_equation.owner == &branch_arena &&
              prime_need_receipt_event_count(&branch_local_equation) == 2u &&
              !prime_need_receipt_excludes_arena(
                  &branch_local_equation, &arena) &&
              !prime_need_receipt_excludes_arena(
                  &branch_local_equation, &branch_arena),
          "branch-local extension retains its older base across arenas");
    PrimeNeedReceipt remote_branch_equation;
    PrimeNeedReceipt cross_arena_join = branch_local_equation;
    PrimeNeedArenaAudit *cross_arena_audit =
        prime_need_arena_audit_new(&promoted_arena);
    CHECK(prime_need_receipt_use_equation_ids_branch_local(
              &promoted_arena, &receipt_root, source_occurrence, 21u,
              &remote_branch_equation) &&
              prime_need_receipt_merge(
                  &cross_arena_join, &remote_branch_equation) &&
              cross_arena_join.owner == &branch_arena &&
              cross_arena_audit &&
              !prime_need_arena_audit_receipt(
                  cross_arena_audit, &cross_arena_join),
          "exact audit rejects a stable-owned join with forbidden descendant");
    prime_need_arena_audit_free(cross_arena_audit);
    PrimeNeedReceipt promoted_branch_local = branch_local_equation;
    CHECK(prime_need_receipt_promote(
              &promoted_arena, &promoted_branch_local) &&
              prime_need_receipt_event_count(&promoted_branch_local) == 2u &&
              prime_need_receipt_excludes_arena(
                  &promoted_branch_local, &arena) &&
              prime_need_receipt_excludes_arena(
                  &promoted_branch_local, &branch_arena),
          "promotion closes a branch-local receipt into its destination");
    PrimeNeedReceipt suffix_promoted_branch_local =
        branch_local_equation;
    CHECK(prime_need_receipt_promote_suffix(
              &arena, &branch_arena,
              &suffix_promoted_branch_local) &&
              suffix_promoted_branch_local.owner == &arena &&
              suffix_promoted_branch_local.top !=
                  branch_local_equation.top &&
              prime_need_receipt_event_count(
                  &suffix_promoted_branch_local) == 2u &&
              prime_need_receipt_excludes_arena(
                  &suffix_promoted_branch_local, &branch_arena) &&
              !prime_need_receipt_excludes_arena(
                  &suffix_promoted_branch_local, &arena),
          "receipt suffix promotion retains its stable parent without copying it");
    PrimeNeedReceipt resampled;
    CHECK(prime_need_receipt_resample(
              &arena, &receipt_root, term, &resampled) &&
              prime_need_receipt_event_at(
                  &resampled, 1u, &receipt_event) &&
              receipt_event.kind == PRIME_NEED_RECEIPT_RESAMPLE &&
              atom_eq(receipt_event.before, term),
          "resampling records one fresh evaluation occurrence");

    PrimeNeedReceipt observation_left;
    PrimeNeedReceipt observation_right;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &observation_left),
          "left sibling records one cell outcome");
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              right_value, &observation_right),
          "right sibling records a different cell outcome");
    CHECK(!prime_need_receipt_compatible(
              &observation_left, &observation_right),
          "different outcomes for one Need cell conflict");
    PrimeNeedReceipt observation_rejected = observation_left;
    CHECK(!prime_need_receipt_merge(
              &observation_rejected, &observation_right) &&
              prime_need_receipt_equal(
                  &observation_rejected, &observation_left),
          "conflicting receipt merge fails without changing its input");

    PrimeNeedReceipt same_observation_left;
    PrimeNeedReceipt same_observation_right;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &same_observation_left) &&
          prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &same_observation_right),
          "siblings may independently observe the same cell outcome");
    PrimeNeedReceipt same_observation_left_deep;
    PrimeNeedReceipt same_observation_left_deeper;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &same_observation_left, thunk.session_id,
              second_thunk_id + 10u, left_value,
              &same_observation_left_deep) &&
              prime_need_receipt_observe_cell(
                  &arena, &same_observation_left_deep,
                  thunk.session_id, second_thunk_id + 11u,
                  left_value, &same_observation_left_deeper) &&
              prime_need_receipt_compatible(
                  &same_observation_left,
                  &same_observation_right) &&
              !prime_need_receipt_is_ancestor(
                  &same_observation_left, &same_observation_right) &&
              !prime_need_receipt_is_ancestor(
                  &same_observation_right, &same_observation_left) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_left,
                  &same_observation_left_deeper) &&
              !prime_need_receipt_is_ancestor(
                  &same_observation_right,
                  &same_observation_left_deeper),
          "exact reachability accepts a deep ancestor and rejects "
          "a shallower disconnected sibling");
    PrimeNeedReceipt same_observation_join = same_observation_left;
    CHECK(prime_need_receipt_merge(
              &same_observation_join, &same_observation_right) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_left, &same_observation_join) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_right, &same_observation_join) &&
              prime_need_receipt_is_ancestor(
                  &same_observation_join, &same_observation_join),
          "equal cell observations have a native least joined receipt");
    CHECK(prime_need_receipt_event_count(&same_observation_join) == 3u,
          "native receipts retain equal observation occurrences before the functional quotient");
    PrimeNeedReceiptEvent joined_event_zero;
    PrimeNeedReceiptEvent joined_event_one;
    PrimeNeedReceiptEvent joined_event_two;
    CHECK(prime_need_receipt_event_at(
              &same_observation_join, 0u, &joined_event_zero) &&
              prime_need_receipt_event_at(
                  &same_observation_join, 1u, &joined_event_one) &&
              prime_need_receipt_event_at(
                  &same_observation_join, 2u, &joined_event_two) &&
              joined_event_zero.event_id != 0u &&
              joined_event_one.event_id != 0u &&
              joined_event_two.event_id != 0u &&
              joined_event_zero.event_id != joined_event_one.event_id &&
              joined_event_zero.event_id != joined_event_two.event_id &&
              joined_event_one.event_id != joined_event_two.event_id,
          "equal receipt payloads retain distinct nonzero occurrence IDs");

    PrimeNeedReceipt independent_left;
    PrimeNeedReceipt independent_right;
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id, second_thunk_id,
              left_value, &independent_left),
          "first independent observation extends the common support");
    CHECK(prime_need_receipt_observe_cell(
              &arena, &receipt_root, thunk.session_id,
              second_thunk_id + 1u, right_value, &independent_right),
          "second independent observation extends the common support");
    CHECK(prime_need_receipt_compatible(
              &independent_left, &independent_right),
          "observations of different cells are compatible");
    PrimeNeedReceipt independent_join = independent_left;
    CHECK(prime_need_receipt_merge(
              &independent_join, &independent_right),
          "compatible receipts have a native join");
    CHECK(prime_need_receipt_is_ancestor(
              &independent_left, &independent_join) &&
              prime_need_receipt_is_ancestor(
                  &independent_right, &independent_join),
          "the receipt join contains both causal branches");
    CHECK(prime_need_receipt_event_count(&independent_join) == 3u,
          "the join is the occurrence-preserving union of common support");

    check_qualified_receipt_join();
    check_state_receipt_operation_identity();

    StateCell state = {
        .value = atom_int(&arena, 0),
        .content_type = atom_symbol(&arena, "Number"),
    };
    PrimeNeedReceipt state_read_left;
    PrimeNeedReceipt state_read_right;
    CHECK(prime_need_receipt_read_state(
              &arena, &receipt_root, &state, state.value,
              &state_read_left),
          "a state read is an explicit receipt event");
    CHECK(prime_need_receipt_read_state(
              &arena, &receipt_root, &state, state.value,
              &state_read_right),
          "a sibling can read the same parent state");
    CHECK(prime_need_receipt_compatible(
              &state_read_left, &state_read_right),
          "equal sibling reads are compatible");
    PrimeNeedReceipt read_join = state_read_left;
    CHECK(prime_need_receipt_merge(&read_join, &state_read_right),
          "equal sibling reads have a least joined receipt");

    PrimeNeedReceipt state_write_left;
    PrimeNeedReceipt state_write_right;
    Atom *one = atom_int(&arena, 1);
    Atom *two = atom_int(&arena, 2);
    CHECK(prime_need_receipt_write_state(
              &arena, &receipt_root, &state, state.value, one,
              &state_write_left),
          "a state write records before and after values");
    CHECK(prime_need_receipt_write_state(
              &arena, &receipt_root, &state, state.value, two,
              &state_write_right),
          "a sibling write is a distinct effect occurrence");
    CHECK(!prime_need_receipt_compatible(
              &state_write_left, &state_write_right),
          "sibling writes do not merge without an admitted effect law");

    PrimeNeedReceipt sequential_write;
    CHECK(prime_need_receipt_write_state(
              &arena, &state_read_left, &state, state.value, one,
              &sequential_write),
          "a causally later write extends its branch read");
    Atom *effective_state = NULL;
    CHECK(prime_need_receipt_state_value(
              &sequential_write, &state, &effective_state) &&
              atom_eq(effective_state, one),
          "branch lookup observes the latest causal state value");
    PrimeNeedReceipt promoted_receipt = independent_join;
    CHECK(prime_need_receipt_promote(
              &promoted_arena, &promoted_receipt) &&
              prime_need_receipt_equal(
                  &promoted_receipt, &independent_join) &&
              prime_need_receipt_is_ancestor(
                  &independent_join, &promoted_receipt) &&
              prime_need_receipt_is_ancestor(
                  &promoted_receipt, &independent_join) &&
              prime_need_receipt_excludes_arena(
                  &promoted_receipt, &arena) &&
              !prime_need_receipt_excludes_arena(
                  &promoted_receipt, &promoted_arena),
          "receipt promotion preserves arena-independent causal identity");
    const PrimeNeedReceiptFrame *same_owner_top = promoted_receipt.top;
    CHECK(prime_need_receipt_promote(
              &promoted_arena, &promoted_receipt) &&
              promoted_receipt.top == same_owner_top,
          "same-owner promotion preserves the persistent receipt DAG");
#endif

    Atom *context_x = atom_symbol(&arena, "x");
    Atom *context_y = atom_symbol(&arena, "y");
    Atom *context_one = atom_int(&arena, 1);
    Atom *context_two = atom_int(&arena, 2);
    Atom *context_x_one = atom_prime_context_bind(
        &arena, NULL, context_x, context_one);
    const CettaPrimeContext *context_x_one_value =
        atom_prime_context_value(context_x_one);
    CHECK(context_x_one_value &&
              atom_prime_context_depth(context_x_one_value) == 1u,
          "context binding allocates one immutable chain frame");
    CHECK(atom_eq(atom_prime_context_lookup(
                      context_x_one_value, atom_symbol(&arena, "x")),
                  context_one) &&
              !atom_prime_context_lookup(
                  context_x_one_value, atom_symbol(&arena, "missing")),
          "context lookup is structural and absence does not invent a value");
    Atom *context_xy = atom_prime_context_bind(
        &arena, context_x_one_value, context_y, context_two);
    const CettaPrimeContext *context_xy_value =
        atom_prime_context_value(context_xy);
    CHECK(context_xy_value &&
              atom_prime_context_depth(context_xy_value) == 2u &&
              atom_eq(atom_prime_context_lookup(context_xy_value, context_x),
                      context_one) &&
              atom_eq(atom_prime_context_lookup(context_xy_value, context_y),
                      context_two),
          "context extension shares its parent and preserves older bindings");
    Atom *context_shadow = atom_prime_context_bind(
        &arena, context_xy_value, context_x, context_two);
    const CettaPrimeContext *context_shadow_value =
        atom_prime_context_value(context_shadow);
    CHECK(context_shadow_value &&
              atom_eq(atom_prime_context_lookup(
                          context_shadow_value, context_x),
                      context_two) &&
              atom_eq(atom_prime_context_lookup(
                          context_x_one_value, context_x),
                      context_one),
          "newest context binding shadows without mutating its parent");
    Atom *context_copy = atom_deep_copy(&promoted_arena, context_shadow);
    CHECK(context_copy && atom_eq(context_copy, context_shadow),
          "context promotion preserves the semantic chain");
    CHECK(!atom_eq(context_shadow, context_xy),
          "different context extensions remain distinguishable values");

    printf("(PrimeNeedAlgebraSummary %u %u %u)\n",
           checks, checks - failures, failures);
    arena_free(&promoted_arena);
    arena_free(&empty_suffix_arena);
    arena_free(&branch_arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u ? 0 : 1;
}
