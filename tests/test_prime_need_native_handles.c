#include "atom.h"
#include "prime_need.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { unsigned references; } HeldResource;
static unsigned retained, released, destroyed, checks, failures;

static void resource_retain(void *raw) {
    HeldResource *resource = raw;
    ++resource->references;
    ++retained;
}

static void resource_release(void *raw) {
    HeldResource *resource = raw;
    assert(resource->references);
    --resource->references;
    ++released;
    if (!resource->references) {
        ++destroyed;
        free(resource);
    }
}

#define CHECK(condition, name) do { \
    ++checks; \
    if (!(condition)) { \
        ++failures; \
        fprintf(stderr, "FAIL: %s\n", name); \
    } \
} while (0)

static bool history_resource_control(const PrimeNeedSnapshot *snapshot,
                                     uint64_t thunk_id, uint64_t occurrence,
                                     HeldResource *resource) {
    PrimeNeedHistoryIterator iterator;
    PrimeNeedHistoryView view;
    unsigned held = resource->references;
    unsigned acquisitions = retained, relinquishments = released;
    if (!prime_need_snapshot_history_begin(snapshot, &iterator))
        return false;
    const PrimeNeedCacheState states[] = {
        PRIME_NEED_CACHE_VALUE, PRIME_NEED_CACHE_EVALUATING, PRIME_NEED_CACHE_EMPTY,
    };
    uint64_t authority = 0u, storage = 0u;
    for (size_t i = 0u; i < 3u; ++i) {
        if (prime_need_snapshot_history_next(&iterator, &view) != PRIME_NEED_HISTORY_ENTRY ||
            view.session_id != snapshot->session_id || view.thunk_id != thunk_id ||
            view.depth != 3u - i || view.cell.cache_state != states[i] ||
            !atom_is_symbol(view.cell.origin, "resource-producer") ||
            view.cell.evaluator_id != (i == 1u ? 19u : 0u) ||
            view.cell.source_occurrence_id != occurrence ||
            view.cell.source_argument_index != 2u)
            return false;
        if (i == 0u) {
            authority = view.cell.authority_id;
            storage = view.cell.storage_key;
            if (!authority || !storage || !view.cell.cached ||
                view.cell.cached->kind != ATOM_EXPR || view.cell.cached->expr.len != 3u ||
                view.cell.cached->expr.elems[2]->ground.ival != 41)
                return false;
        } else if (view.cell.cached || view.cell.authority_id != authority ||
                   view.cell.storage_key != storage)
            return false;
    }
    return prime_need_snapshot_history_next(&iterator, &view) == PRIME_NEED_HISTORY_DONE &&
        resource->references == held && retained == acquisitions && released == relinquishments;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena vector_owner, source, survivor, other;
    arena_init(&vector_owner);
    arena_init(&source);
    arena_init(&survivor);
    arena_init(&other);

    HeldResource *resource = calloc(1u, sizeof(*resource));
    assert(resource);
    Atom *identifier = atom_native_handle_identifier(
        &source, 41, resource, resource_retain, resource_release);
    Atom *handle = atom_expr(&source, (Atom *[]){
        atom_symbol(&source, "NativeHandle"),
        atom_string(&source, "owned-resource-control"), identifier}, 3u);
    CHECK(resource->references == 1u && retained == 1u,
          "the source arena holds the resource once");
    CHECK(!atom_graph_is_closed_for_arena(&source, identifier) &&
          !atom_graph_is_closed_for_arena(&source, handle),
          "retained resources do not gain the shallow-copy certificate");
    CHECK(atom_graph_is_owner_closed_for_arena(&source, identifier) &&
          atom_graph_is_owner_closed_for_arena(&source, handle),
          "the holder and its structural carrier have lifetime evidence");
    CHECK(!atom_graph_is_owner_closed_for_arena(&other, handle),
          "ownership evidence remains indexed by its arena");

    int borrowed_identity;
    Atom *borrowed_leaf = atom_space(&source, &borrowed_identity);
    Atom *plain = atom_symbol(&source, "plain");
    Atom *foreign_head = atom_symbol(&other, "foreign-head");
    Atom *heads[] = {plain, handle, borrowed_leaf, foreign_head};
    for (unsigned h = 0u; h < 4u; ++h) {
        AtomExprHead summary;
        atom_expr_head_init(&summary, heads[h]);
        Atom *children[] = {heads[h], handle, plain, handle};
        for (CettaExprLen length = 1u; length <= 4u; ++length) {
            Atom *headed = atom_expr_headed(
                &source, &summary, children, length);
            CHECK(atom_graph_is_owner_closed_for_arena(&source, headed) ==
                      (h < 2u),
                  "cached-head construction folds the head's ownership too");
        }
    }
    Atom *owned_list = atom_expr2(&source, handle, plain);
    Atom *mixed_list = atom_expr(&source,
        (Atom *[]){borrowed_leaf, handle, plain}, 3u);
    CHECK(!atom_graph_has_owner_closure(mixed_list),
          "closure is conjunctive rather than inherited from one holder");
    Atom *owned_suffix = atom_expr_suffix(&source, mixed_list, 1u);
    CHECK(atom_graph_is_owner_closed_for_arena(&source, owned_suffix) &&
          !atom_graph_is_closed_for_arena(&source, owned_suffix),
          "a suffix dropping a borrow computes its own lifetime evidence");
    CHECK(atom_graph_is_owner_closed_for_arena(
              &source, atom_expr_suffix(&source, owned_list, 0u)),
          "an owned suffix retains the carrier certificate");
    CHECK(!atom_graph_has_owner_closure(
              atom_expr_prepend(&source, borrowed_leaf, owned_list)),
          "prepending a borrow does not inherit an owned tail's certificate");
    CHECK(atom_graph_is_owner_closed_for_arena(
              &source, atom_expr_prepend(&source, handle, owned_list)),
          "prepending a holder folds ownership by conjunction");
    CHECK(!atom_graph_has_owner_closure(
              atom_expr_view_rehome(&other, owned_list, owned_list->expr.elems)),
          "a relocated view cannot retain the old arena's evidence");
    Atom *older_vector = atom_expr2(&vector_owner, plain, handle);
    arena_set_older_generation(&source, &vector_owner);
    Atom *borrowed_vector_suffix = atom_expr_suffix(&source, older_vector, 1u);
    CHECK(!atom_graph_is_owner_closed_for_arena(&source, borrowed_vector_suffix),
          "a suffix cannot hide its older arena's element-vector storage");
    Atom *older_list = atom_expr(&vector_owner,
        (Atom *[]){plain, plain, handle, handle}, 4u);
    Atom *first_view = atom_expr_suffix(&source, older_list, 1u);
    Atom *second_view = atom_expr_suffix(&source, first_view, 1u);
    CHECK(second_view->expr.elems == older_list->expr.elems + 2 &&
          !atom_graph_is_owner_closed_for_arena(&source, first_view) &&
          !atom_graph_is_owner_closed_for_arena(&source, second_view),
          "a view of a view keeps borrowing the older vector uncertified");
    Atom *older_handle = atom_expr(&vector_owner, (Atom *[]){
        atom_symbol(&source, "NativeHandle"),
        atom_string(&source, "owned-resource-control"), identifier}, 3u);
    CHECK(!atom_graph_is_owner_closed_for_arena(
              &source, atom_expr_suffix(&source, older_handle, 1u)),
          "a refolded view of an older vector stays uncertified");
    Atom *older_closed = atom_expr2(&vector_owner,
        atom_internal_tag(&source, CETTA_INTERNAL_TAG_LIST),
        atom_symbol(&vector_owner, "older-symbol"));
    Atom *older_closed_view = atom_expr_suffix(&source, older_closed, 1u);
    CHECK((older_closed->flags & ATOM_FLAG_ARENA_CLOSED) != 0u &&
          !atom_graph_is_closed_for_arena(&source, older_closed_view) &&
          !atom_graph_has_owner_closure(older_closed_view) &&
          atom_settled_for_arena(&source, older_closed_view),
          "an older list's closure is not inherited, but its generation admits the view");
    arena_set_older_generation(&source, NULL);
    Atom *own_list = atom_expr(&source,
        (Atom *[]){plain, handle, plain, handle}, 4u);
    Atom *own_view = atom_expr_suffix(&source, own_list, 1u);
    CHECK(atom_graph_is_owner_closed_for_arena(&source, own_view) &&
          atom_graph_is_owner_closed_for_arena(
              &source, atom_expr_suffix(&source, own_view, 1u)) &&
          atom_graph_is_owner_closed_for_arena(
              &source, atom_expr_suffix(&source, handle, 1u)),
          "views of an owned list in its own arena keep the evidence");
    Atom *deep = handle;
    for (unsigned i = 0u; i < 10000u; ++i)
        deep = atom_expr2(&source, plain, deep);
    uint32_t minimum, maximum;
    CHECK(atom_graph_is_owner_closed_for_arena(&source, deep) &&
          atom_graph_arena_id_bounds(deep, &minimum, &maximum) &&
          minimum == source.identity && maximum == source.identity,
          "deep retained carriers have constant-depth ownership bounds");
    CHECK(!atom_graph_arena_id_bounds(mixed_list, &minimum, &maximum),
          "unknown external storage keeps arena bounds incomplete");

    PrimeNeedSnapshot base, origin_handle;
    prime_need_snapshot_init(&base);
    assert(prime_need_snapshot_begin(&base));
    uint64_t origin_id;
    assert(prime_need_snapshot_allocate(
        &source, &base, handle, &origin_handle, &origin_id));
    CHECK(origin_handle.closure_owner == &source &&
          prime_need_snapshot_owner_excludes_arena(&origin_handle, &other),
          "an owned resource in a delayed origin has its carrier owner");
    CHECK(!prime_need_snapshot_owner_excludes_arena(&origin_handle, &source),
          "a resource carrier never excludes its own arena");
    arena_set_older_generation(&source, &vector_owner);
    PrimeNeedSnapshot generational;
    uint64_t generational_id;
    CHECK(!prime_need_snapshot_allocate(&source, &base, plain,
                                        &generational, &generational_id),
          "an arena with an older generation owns no Need storage");
    arena_set_older_generation(&source, NULL);

    PrimeNeedSnapshot pending, evaluating, ready;
    uint64_t thunk_id;
    uint64_t occurrence = prime_need_fresh_source_occurrence();
    assert(prime_need_snapshot_allocate_source_argument(
        &source, &base, atom_symbol(&source, "resource-producer"),
        occurrence, 2u, PRIME_NEED_SOURCE_COMPUTATION, &pending, &thunk_id));
    assert(prime_need_snapshot_start_evaluation(
        &source, &pending, thunk_id, 19u, &evaluating));
    assert(prime_need_snapshot_resolve_value(
        &source, &evaluating, thunk_id, 19u, handle, &ready));
    CHECK(ready.closure_owner == &source &&
          prime_need_snapshot_owner_excludes_arena(&ready, &other),
          "a memoized resource is admitted by the carrier audit");

    PrimeNeedCellView before, after;
    assert(prime_need_snapshot_lookup(&ready, thunk_id, &before));
    CHECK(before.cache_state == PRIME_NEED_CACHE_VALUE &&
          before.cached == handle && before.source_occurrence_id == occurrence &&
          before.source_argument_index == 2u &&
          before.source_role == PRIME_NEED_SOURCE_COMPUTATION &&
          history_resource_control(&ready, thunk_id, occurrence, resource),
          "memoization retains the value and source occurrence");
    uint64_t session_id = ready.session_id;
    assert(prime_need_snapshot_promote_suffix(&survivor, &source, &ready));
    assert(prime_need_snapshot_lookup(&ready, thunk_id, &after));
    CHECK(ready.closure_owner == &survivor &&
          prime_need_snapshot_owner_excludes_arena(&ready, &source) &&
          prime_need_snapshot_excludes_arena(&ready, &source),
          "promotion certifies the survivor without weakening the audit");
    CHECK(ready.session_id == session_id &&
          after.cache_state == before.cache_state &&
          after.authority_id == before.authority_id &&
          after.evaluator_id == before.evaluator_id &&
          after.storage_key == before.storage_key &&
          after.import_key == before.import_key &&
          after.source_occurrence_id == before.source_occurrence_id &&
          after.source_argument_index == before.source_argument_index &&
          after.source_role == before.source_role &&
          history_resource_control(&ready, thunk_id, occurrence, resource),
          "transport preserves cell identity, role and occurrence");
    CHECK(after.cached != handle && after.cached->expr.elems[2] != identifier &&
          after.cached->expr.elems[2]->ground.ival == 41 &&
          resource->references == 2u && retained == 2u,
          "promotion keeps the opaque identity and acquires one holder");
    ArenaMark before_escape = arena_mark(&other);
    Atom *escaped = atom_int_copy(&other, identifier);
    CHECK(escaped && escaped != identifier && resource->references == 3u,
          "an escape to another arena acquires a holder there");
    arena_reset(&other, before_escape);
    CHECK(resource->references == 2u,
          "a reset to a mark releases the holders acquired after it");

    int borrowed_resource;
    Atom *borrowed = atom_space(&source, &borrowed_resource);
    PrimeNeedSnapshot unsafe;
    uint64_t unsafe_id;
    assert(prime_need_snapshot_allocate(&source, &base,
        atom_expr2(&source, handle, borrowed), &unsafe, &unsafe_id));
    CHECK(unsafe.closure_owner == NULL &&
          !prime_need_snapshot_owner_excludes_arena(&unsafe, &other),
          "an unrelated borrowed resource is not certified by a sibling handle");
    Atom *foreign_child = atom_symbol(&other, "foreign-child");
    CHECK(!atom_graph_has_owner_closure(
              atom_expr_prepend(&source, foreign_child, owned_list)),
          "optimized prepend does not hide a foreign-arena child");
    assert(prime_need_snapshot_allocate(&source, &base,
        atom_expr2(&source, handle, foreign_child), &unsafe, &unsafe_id));
    CHECK(unsafe.closure_owner == NULL &&
          !prime_need_snapshot_owner_excludes_arena(&unsafe, &other),
          "a retained handle does not hide a foreign-arena child");
    Atom *forged = atom_expr(&other, (Atom *[]){
        atom_symbol(&other, "NativeHandle"),
        atom_string(&other, "owned-resource-control"), atom_int(&other, 41)}, 3u);
    CHECK((forged->expr.elems[2]->structural_facts &
              ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) == 0u &&
          resource->references == 2u,
          "equal printed identifiers do not acquire a resource holder");

    unsigned released_before_reclaim = released;
    arena_free(&source);
    CHECK(resource->references == 1u &&
              released == released_before_reclaim + 1u && destroyed == 0u,
          "the survivor holder outlives the reclaimed source arena");
    assert(prime_need_snapshot_lookup(&ready, thunk_id, &after));
    CHECK(after.cache_state == PRIME_NEED_CACHE_VALUE &&
          after.cached->expr.elems[2]->ground.ival == 41 &&
          !atom_graph_is_closed_for_arena(&survivor, after.cached) &&
          history_resource_control(&ready, thunk_id, occurrence, resource),
          "the transported memo remains live without a shallow-copy licence");
    arena_free(&survivor);
    arena_free(&other);
    arena_free(&vector_owner);
    CHECK(retained == released && destroyed == 1u,
          "all holders release the resource exactly once");
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(PrimeNeedNativeHandleSummary %u %u %u)\n",
           checks, checks - failures, failures);
    return failures ? 1 : 0;
}
