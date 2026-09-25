#include "atom.h"
#include "match.h"
#include "term_canon.h"
#include "stats.h"
#include "term_universe.h"
#include "variant_shape.h"
#include "tests/test_runtime_stats_stubs.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned passed;
static unsigned failed;

#define CHECK(condition, message)                                              \
    do {                                                                       \
        if (condition) {                                                       \
            passed++;                                                          \
        } else {                                                               \
            failed++;                                                          \
            fprintf(stderr, "FAIL: %s\n", message);                            \
        }                                                                      \
    } while (0)

static VarId test_id(uint32_t ordinal) {
    return UINT64_C(0x10000000) + (VarId)ordinal * UINT64_C(0x10001);
}

static bool build_bindings(Arena *arena, uint32_t count, Bindings *out) {
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL))
        return false;
    for (uint32_t i = 0u; i < count; i++) {
        if (!bindings_builder_add_id_fresh(
                &builder, test_id(i), SYMBOL_ID_NONE,
                atom_int(arena, (int64_t)i))) {
            bindings_builder_free(&builder);
            return false;
        }
    }
    bindings_builder_take(&builder, out);
    return true;
}

static bool binding_is_int(Bindings *bindings, VarId id, int64_t expected) {
    Atom *value = bindings_lookup_value_id(bindings, id).skeleton;
    return value && value->kind == ATOM_GROUNDED &&
           value->ground.gkind == GV_INT &&
           value->ground.ival == expected;
}

static void test_owner_retain(void *owner) {
    (*(unsigned *)owner)++;
}

static void test_owner_release(void *owner) {
    (*(unsigned *)owner)--;
}

static void test_arena_retained_owners(void) {
    Arena owner;
    arena_init(&owner);
    unsigned outer = 0u, inner = 0u;
    CHECK(arena_retain_owner(&owner, &outer, test_owner_retain, test_owner_release) &&
              arena_retain_owner(&owner, &outer, test_owner_retain, test_owner_release) &&
              outer == 1u,
          "repeated syntax-owner borrows retain one owner per arena");
    ArenaMark mark = arena_mark(&owner);
    CHECK(arena_retain_owner(&owner, &inner, test_owner_retain, test_owner_release) &&
              arena_retain_owner(&owner, &outer, test_owner_retain, test_owner_release) &&
              outer == 1u && inner == 1u,
          "nested ownership retains a new plan without multiplying the existing owner");
    arena_reset(&owner, mark);
    CHECK(outer == 1u && inner == 0u &&
              arena_retain_owner(&owner, &inner, test_owner_retain, test_owner_release) &&
              inner == 1u,
          "reset releases only later owners and permits reacquisition");
    arena_free(&owner);
    CHECK(outer == 0u && inner == 0u,
          "arena teardown releases every remaining syntax owner exactly once");
}

static void test_sparse_frame_write_context_growth(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&identities);
    Atom *low = atom_var_with_id(arena, "sparse-low", var_epoch_id(100u, epoch));
    Atom *high = atom_var_with_id(arena, "sparse-high", var_epoch_id(200u, epoch));
    Atom *value = atom_expr2(arena, atom_symbol(arena, "SparseValue"), low);
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL)) {
        CHECK(false, "sparse frame context fixture initializes");
        return;
    }
    uint32_t mark = bindings_builder_save(&builder);
    bool added = bindings_builder_add_var_fresh(&builder, high, value);
    CHECK(added && bindings_lookup_value_id(&builder.current, high->var_id).skeleton == value &&
              !bindings_lookup_value_id(&builder.current, low->var_id).skeleton,
          "RHS context growth preserves the target variable of a sparse frame write");
    bool resolved = added && bindings_builder_add_var_fresh(&builder, low, atom_int(arena, 7));
    Atom *expected = atom_expr2(arena, atom_symbol(arena, "SparseValue"), atom_int(arena, 7));
    CHECK(resolved && atom_eq(bindings_apply(&builder.current, arena, high), expected),
          "a sparse frame write follows the subsequently bound RHS variable");
    bindings_builder_rollback(&builder, mark);
    CHECK(!bindings_lookup_value_id(&builder.current, high->var_id).skeleton &&
              !bindings_lookup_value_id(&builder.current, low->var_id).skeleton &&
              bindings_builder_add_var_fresh(&builder, high, atom_int(arena, 9)) &&
              binding_is_int(&builder.current, high->var_id, 9),
          "rollback restores the sparse schema and permits an independent sibling write");
    bindings_builder_free(&builder);
}

static void test_borrowed_root_identity(Arena *arena) {
    Atom *first = atom_var_with_id(arena, "same-root-name", test_id(9000u));
    Atom *second = atom_var_with_id(arena, "same-root-name", test_id(9001u));
    Atom *open = atom_var_with_id(arena, "open-root", test_id(9002u));
    Atom *value = atom_expr3(
        arena, atom_symbol(arena, "RootPair"), open, open);
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, NULL)) {
        CHECK(false, "borrowed-root fixture initializes its binding store");
        return;
    }
    bool ready = bindings_builder_add_var_fresh(&builder, first, second);
    uint32_t mark = bindings_builder_save(&builder);
    ready = ready && bindings_builder_add_var_fresh(&builder, second, value);
    CHECK(ready && first->var_id != second->var_id &&
              first->sym_id == second->sym_id &&
              !first->name_key && !second->name_key,
          "borrowed-root fixture separates logical identity from spelling");

    CettaGsltTermViewV1 view = bindings_term_view_v1(first, &builder.current);
    Atom *resolved = NULL;
    CHECK(cetta_gslt_term_view_resolve_root_v1(&view, first, &resolved) ==
              CETTA_GSLT_TERM_VIEW_OK_V1 && resolved == value,
          "borrowed roots follow aliases with equal spelling and distinct identities");
    CHECK(resolved && atom_eq(
              bindings_apply(&builder.current, arena, first),
              bindings_apply(&builder.current, arena, resolved)),
          "root resolution preserves correlated open-term forcing");
    CHECK(cetta_gslt_term_view_resolve_root_v1(&view, open, &resolved) ==
              CETTA_GSLT_TERM_VIEW_OK_V1 && resolved == open,
          "an unbound root preserves its exact variable identity");

    Atom *replacement = atom_int(arena, 17);
    bindings_builder_rollback(&builder, mark);
    CHECK(bindings_builder_add_var_fresh(&builder, second, replacement) &&
              cetta_gslt_term_view_resolve_root_v1(&view, first, &resolved) ==
                  CETTA_GSLT_TERM_VIEW_OK_V1 && resolved == replacement,
          "a borrowed root observes the current branch after rollback");
    /* Simulate an invalid externally rewritten store. Normal binding
     * insertion may already reject or normalize this alias cycle. */
    CHECK(bindings_prepare_logical_write(&builder.current),
          "cycle canary flattens before rewriting the store");
    for (uint32_t index = 0u; index < builder.current.len; index++) {
        if (builder.current.entries[index].var_id == second->var_id)
            builder.current.entries[index].value.skeleton = first;
    }
    bindings_invalidate_after_key_rewrite(&builder.current);
    CHECK(bindings_has_loop(&builder.current),
          "borrowed-root cycle canary creates a nontrivial alias cycle");
    resolved = value;
    CHECK(cetta_gslt_term_view_resolve_root_v1(&view, first, &resolved) ==
              CETTA_GSLT_TERM_VIEW_DEFER_V1 && resolved == NULL,
          "a cyclic root declines instead of exposing an unbound wildcard");
    bindings_builder_free(&builder);
}

static bool dense_slot_is(const Bindings *bindings, const BindingsActivationView *frame, uint32_t slot,
                          Atom *expected) {
    BindingValue value = binding_value_from_atom(NULL);
    bool present = false;
    return bindings_activation_view_read_slot(bindings, frame, slot, &value, &present) &&
        present == (expected != NULL) && value.skeleton == expected;
}

static bool lookup_frame_value(const Bindings *bindings, VarId id, BindingValue *value) {
    bool known = false;
    uint32_t row = 0u;
    if (!bindings_frame_index_test_lookup(bindings, id, &known, &row) || !known)
        return false;
    *value = bindings_lookup_value_id((Bindings *)bindings, id);
    return true;
}

static void test_dense_frame_indexed_suffix(Arena *arena) {
    VarId ids[] = {UINT64_C(80001), UINT64_C(80003), UINT64_C(80008)};
    Atom *variables[] = {
        atom_var_with_id(arena, "frame-prefix", ids[0]),
        atom_var_with_id(arena, "frame-present", ids[1]),
        atom_var_with_id(arena, "frame-missing", ids[2])
    };
    const uint32_t epoch = 113u;
    Bindings base;
    bool ready = build_bindings(arena, 256u, &base);
    BindingsBuilder builder;
    ready = ready && bindings_builder_init(&builder, &base);
    if (!ready) { CHECK(false, "indexed frame fixture allocation"); return; }
    bindings_free(&base);
    Atom *before = atom_int(arena, 11);
    Atom *after = atom_int(arena, 22);
    Atom *later = atom_int(arena, 33);
    ready = bindings_builder_register_contextual_frame(&builder, ids, 3u, epoch) &&
        bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[0], epoch),
            SYMBOL_ID_NONE, before);
    uint32_t begin = builder.current.len;
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    ready = ready && bindings_activation_view_prepare(
        &frame, &builder, ids, variables, 3u, epoch, begin) &&
        bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[1], epoch),
            SYMBOL_ID_NONE, after);
    CHECK(ready && dense_slot_is(&builder.current, &frame, 0u, NULL) &&
              dense_slot_is(&builder.current, &frame, 1u, after) && dense_slot_is(&builder.current, &frame, 2u, NULL),
          "activation observation excludes earlier writes and retains unbound slots");
    Bindings snapshot;
    bool cloned = ready && bindings_clone(&snapshot, &builder.current);
    uint32_t mark = bindings_builder_save(&builder);
    ready = cloned && bindings_builder_add_id_fresh(
        &builder, var_epoch_id(ids[2], epoch), SYMBOL_ID_NONE, later);
    bindings_lookup_index_test_clear(&builder.current);
    CHECK(ready && dense_slot_is(&builder.current, &frame, 2u, later) &&
              !bindings_lookup_value_id(&snapshot, var_epoch_id(ids[2], epoch)).skeleton,
          "an identity view observes copy-on-write slots without pointer refresh");
    bindings_builder_rollback(&builder, mark);
    CHECK(ready && dense_slot_is(&builder.current, &frame, 1u, after) && dense_slot_is(&builder.current, &frame, 2u, NULL),
          "rollback is immediately visible through the same activation identity");
    ready = ready && bindings_rewrite_value_id(&builder.current,
        var_epoch_id(ids[1], epoch), binding_value_from_atom(later));
    CHECK(ready && dense_slot_is(&builder.current, &frame, 1u, later) &&
              bindings_lookup_value_id(&snapshot, var_epoch_id(ids[1], epoch)).skeleton == after,
          "a rewritten slot changes the current image without altering a sibling");
    if (cloned)
        bindings_free(&snapshot);
    bindings_activation_view_free(&frame);
    CHECK(!bindings_activation_view_available(&frame, &builder.current),
          "a released activation view cannot resolve a slot");
    bindings_builder_free(&builder);
}

static void test_generation_checked_frame_handles(Arena *arena) {
    VarId source_id = UINT64_C(88001);
    VarId unrelated_source_id = UINT64_C(88002);
    Atom *source = atom_var_with_id(
        arena, "stable-frame-source", source_id);
    Atom *variables[] = {source};
    BindingsBuilder builder;
    bool ready = source && bindings_builder_init(&builder, NULL) &&
        bindings_builder_register_contextual_frame(
            &builder, &source_id, 1u, 300u);
    BindingsFrameRef frame_300 = {0};
    ready = ready && bindings_frame_ref_test_for_epoch(
        &builder.current, 300u, &frame_300);
    BindingsActivationView dense;
    bindings_activation_view_init(&dense);
    ready = ready && bindings_activation_view_prepare(
        &dense, &builder, &source_id, variables, 1u, 300u, 0u);

    CHECK(ready && bindings_frame_ref_is_valid(frame_300) &&
              dense.authority_ref.identity ==
                  frame_300.identity &&
              dense.authority_ref.identity == 300u,
          "prepared activation carries its stable generation-checked frame handle");

    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &unrelated_source_id, 1u, 250u);
    uint32_t mark = bindings_builder_save(&builder);
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &source_id, 1u, 200u);
    BindingsFrameRef frame_200 = {0};
    uint32_t resolved_epoch = 0u;
    CHECK(ready && bindings_frame_ref_test_for_epoch(
              &builder.current, 200u, &frame_200) &&
              bindings_frame_ref_test_resolves(
                  &builder.current, frame_300, &resolved_epoch) &&
              resolved_epoch == 300u &&
              bindings_activation_view_available(&dense, &builder.current),
          "inserting another frame preserves a live direct reference");

    BindingValue context_300 = binding_value_from_context(source, 300u);
    BindingValue context_200 = binding_value_from_context(source, 200u);
    ready = ready && match_binding_values_builder(
        context_300, context_200, &builder);
    BindingValue stored_context = ready
        ? bindings_lookup_value_id(
              &builder.current, var_epoch_id(source_id, 300u))
        : binding_value_from_atom(NULL);
    BindingsFrameRef stored_ref = binding_value_frame_ref(stored_context);
    BindingValue resolved_context = binding_value_from_atom(NULL);
    test_runtime_stats_reset_counters();
    bool resolved_direct = ready && bindings_resolve_value_exact(
        &builder.current, stored_context, &resolved_context);
    CHECK(resolved_direct &&
              stored_ref.identity ==
                  frame_200.identity &&
              stored_ref.identity == 200u &&
              resolved_context.skeleton == source &&
              resolved_context.epoch == 200u &&
              test_runtime_stats_counter(
                  CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_DIRECTORY_LOOKUP) == 0u,
          "stored contextual aliases dereference by stable frame handle without directory recovery");

    Bindings projected;
    bindings_init(&projected);
    BindingsEpochRoot projected_root = {
        .atom = source,
        .epoch = 300u,
        .variable_support = NULL,
    };
    bool projected_ok = ready &&
        bindings_project_reachable_with_epoch_roots(
            &builder.current, NULL, 0u,
            &projected_root, 1u, &projected);
    BindingValue projected_context = projected_ok
        ? bindings_lookup_value_id(
              &projected, var_epoch_id(source_id, 300u))
        : binding_value_from_atom(NULL);
    BindingsFrameRef projected_ref =
        binding_value_frame_ref(projected_context);
    resolved_epoch = 0u;
    CHECK(projected_ok &&
              bindings_frame_ref_is_valid(projected_ref) &&
              projected_ref.identity ==
                  stored_ref.identity &&
              bindings_frame_ref_test_resolves(
                  &projected, projected_ref, &resolved_epoch) &&
              resolved_epoch == 200u,
          "projection preserves a contextual identity in the destination frame table");
    bindings_free(&projected);

    Bindings cloned;
    bindings_init(&cloned);
    resolved_epoch = 0u;
    bool cloned_ok = ready && bindings_clone(&cloned, &builder.current);
    CHECK(cloned_ok && bindings_frame_ref_test_resolves(
              &cloned, frame_300, &resolved_epoch) &&
              resolved_epoch == 300u,
          "copy-on-write clone preserves the frame-handle namespace");
    bindings_free(&cloned);

    bindings_builder_rollback(&builder, mark);
    resolved_epoch = 0u;
    CHECK(bindings_frame_ref_test_resolves(
              &builder.current, frame_300, &resolved_epoch) &&
              resolved_epoch == 300u &&
              !bindings_frame_ref_test_resolves(
                  &builder.current, frame_200, &resolved_epoch) &&
              bindings_activation_view_available(&dense, &builder.current),
          "rollback retires the removed activation handle and preserves its older sibling");

    ready = bindings_builder_register_contextual_frame(
        &builder, &source_id, 1u, 400u);
    BindingsFrameRef frame_400 = {0};
    resolved_epoch = 0u;
    CHECK(ready && bindings_frame_ref_test_for_epoch(
              &builder.current, 400u, &frame_400) &&
              frame_400.identity !=
                  frame_200.identity &&
              frame_400.identity != frame_200.identity &&
              !bindings_frame_ref_test_resolves(
                  &builder.current, frame_200, &resolved_epoch) &&
              bindings_frame_ref_test_resolves(
                  &builder.current, frame_400, &resolved_epoch) &&
              resolved_epoch == 400u,
          "removed frame identity remains invalid after another activation is installed");

    bindings_activation_view_free(&dense);
    bindings_builder_free(&builder);
}

static void test_persistent_frame_index_partition(Arena *arena) {
    const uint32_t epoch = 701u;
    VarId ids[] = {UINT64_C(87001), UINT64_C(87003)};
    Atom *variables[] = {
        atom_var_with_id(arena, "persistent-frame-a", ids[0]),
        atom_var_with_id(arena, "persistent-frame-b", ids[1]),
    };
    VarId framed_a = var_epoch_id(ids[0], epoch);
    VarId framed_b = var_epoch_id(ids[1], epoch);
    VarId unframed = UINT64_C(990001);
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder parent;
    bool ready = bindings_builder_init(&parent, &empty);
    bindings_free(&empty);
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    ready = ready && bindings_activation_view_prepare(
        &frame, &parent, ids, variables, 2u, epoch, 0u);
    ready = ready && bindings_builder_add_id_fresh(
        &parent, framed_a, SYMBOL_ID_NONE, atom_int(arena, 11));
    ready = ready && bindings_builder_add_id_fresh(
        &parent, unframed, SYMBOL_ID_NONE, atom_int(arena, 44));
    for (uint32_t i = 0u; i < 64u && ready; i++) {
        ready = bindings_builder_add_id_fresh(
            &parent, unframed + 1u + i, SYMBOL_ID_NONE,
            atom_int(arena, i));
    }

    bool known_a = false;
    bool known_b = false;
    bool generic_a = true;
    bool generic_unframed = false;
    uint32_t entry_a = UINT32_MAX;
    uint32_t entry_b = 0u;
    const char *index_setting = getenv("CETTA_BINDINGS_LOOKUP_INDEX");
    bool generic_enabled =
        !(index_setting && index_setting[0] == '0');
    bool partition_ok = ready &&
        bindings_frame_index_test_lookup(
            &parent.current, framed_a, &known_a, &entry_a) &&
        bindings_frame_index_test_lookup(
            &parent.current, framed_b, &known_b, &entry_b) &&
        (!generic_enabled ||
         (bindings_lookup_index_test_generic_contains(
              &parent.current, framed_a, &generic_a) &&
          bindings_lookup_index_test_generic_contains(
              &parent.current, unframed, &generic_unframed)));
    CHECK(partition_ok && known_a && entry_a == UINT32_MAX &&
              known_b && entry_b == UINT32_MAX &&
              (!generic_enabled || (!generic_a && generic_unframed)),
          "registered frame slots and generic variables occupy disjoint lookup domains");

    Atom *framed_root = atom_var_with_id(
        arena, "persistent-frame-root", framed_a);
    Atom *projection_roots[] = {framed_root};
    Bindings projected;
    bool projected_ok = ready && bindings_project_reachable(
        &parent.current, projection_roots, 1u, &projected);
    BindingValue projected_value = projected_ok
        ? bindings_lookup_value_id(&projected, framed_a)
        : binding_value_from_atom(NULL);
    CHECK(projected_ok && projected_value.skeleton &&
              binding_value_equal(
                  projected_value,
                  bindings_lookup_value_id(
                      &parent.current, framed_a)),
          "reachable projection retains a frame-owned binding through the partitioned index");
    if (projected_ok)
        bindings_free(&projected);

    Bindings merged;
    bindings_init(&merged);
    bool merged_ok = ready && bindings_try_merge(
        &merged, &parent.current);
    bool merged_known_a = false;
    bool merged_known_b = false;
    bool merged_generic_a = true;
    uint32_t merged_entry_a = UINT32_MAX;
    uint32_t merged_entry_b = 0u;
    CHECK(merged_ok &&
              bindings_frame_index_test_lookup(
                  &merged, framed_a, &merged_known_a,
                  &merged_entry_a) &&
              bindings_frame_index_test_lookup(
                  &merged, framed_b, &merged_known_b,
                  &merged_entry_b) &&
              (!generic_enabled ||
               (bindings_lookup_index_test_generic_contains(
                    &merged, framed_a, &merged_generic_a) &&
                !merged_generic_a)) &&
              merged_known_a && merged_entry_a == UINT32_MAX &&
              merged_known_b && merged_entry_b == UINT32_MAX,
          "substitution merge carries bound and unbound contextual frame coordinates");
    bindings_free(&merged);

    Bindings empty_merge;
    bindings_init(&empty_merge);
    BindingsBuilder merged_builder;
    bool merged_builder_ready = bindings_builder_init(
        &merged_builder, &empty_merge);
    bindings_free(&empty_merge);
    merged_known_a = false;
    merged_entry_a = UINT32_MAX;
    CHECK(merged_builder_ready &&
              bindings_builder_try_merge(
                  &merged_builder, &parent.current) &&
              bindings_frame_index_test_lookup(
                  &merged_builder.current, framed_a,
                  &merged_known_a, &merged_entry_a) &&
              merged_known_a &&
              merged_entry_a == UINT32_MAX,
          "rollback-capable merge carries contextual frame coordinates");
    if (merged_builder_ready)
        bindings_builder_free(&merged_builder);

    Bindings factored;
    bool factored_initialized = bindings_clone(
        &factored, &parent.current);
    bool factored_ready = factored_initialized &&
        bindings_add_id(&factored, framed_b, SYMBOL_ID_NONE,
                        atom_int(arena, 22));
    bool did_factor = false;
    uint64_t factored_items = 0u;
    bool factored_known_a = false;
    bool factored_known_b = false;
    uint32_t factored_entry_a = 0u;
    uint32_t factored_entry_b = UINT32_MAX;
    bool factor_ok = factored_ready && bindings_factor_prefix(
        &factored, &parent.current, &did_factor, &factored_items);
    size_t factored_binding_count = 0u;
    CHECK(factor_ok && did_factor &&
              bindings_current_binding_count_test(
                  &factored, &factored_binding_count) &&
              factored_binding_count == 1u &&
              bindings_frame_index_test_lookup(
                  &factored, framed_a, &factored_known_a,
                  &factored_entry_a) &&
              bindings_frame_index_test_lookup(
                  &factored, framed_b, &factored_known_b,
                  &factored_entry_b) &&
              factored_known_a && factored_entry_a == UINT32_MAX &&
              factored_known_b && factored_entry_b == UINT32_MAX &&
              binding_is_int(&factored, framed_b, 22),
          "prefix factoring carries the contextual frame schema into its residual substitution");
    if (factored_initialized)
        bindings_free(&factored);

    BindingsBuilder child;
    bool child_initialized = ready && bindings_builder_clone(&child, &parent);
    bool child_ready = child_initialized;
    uint32_t child_mark = child_initialized
        ? bindings_builder_save(&child) : 0u;
    child_ready = child_ready && bindings_builder_add_id_fresh(
        &child, framed_b, SYMBOL_ID_NONE, atom_int(arena, 22));
    bool child_known = false;
    bool parent_known = false;
    uint32_t child_entry = UINT32_MAX;
    uint32_t parent_entry = 0u;
    bool cow_ok = child_ready &&
        bindings_frame_index_test_lookup(
            &child.current, framed_b, &child_known, &child_entry) &&
        bindings_frame_index_test_lookup(
            &parent.current, framed_b, &parent_known, &parent_entry);
    CHECK(cow_ok && child_known && child_entry == UINT32_MAX &&
              parent_known && parent_entry == UINT32_MAX,
          "a captured binding image detaches its frame coordinate on write");
    const void *parent_schema = NULL, *child_schema = NULL;
    const void *parent_slots = NULL, *child_slots = NULL;
    CHECK(cow_ok && bindings_frame_storage_test_identity(
              &parent.current, epoch, &parent_schema, &parent_slots) &&
          bindings_frame_storage_test_identity(
              &child.current, epoch, &child_schema, &child_slots) &&
          parent_schema == child_schema && parent_slots != child_slots,
          "a branch write shares the immutable schema while detaching only slot state");

    if (child_initialized)
        bindings_builder_rollback(&child, child_mark);
    child_known = false;
    child_entry = 0u;
    CHECK(child_ready && bindings_frame_index_test_lookup(
              &child.current, framed_b, &child_known, &child_entry) &&
              child_known && child_entry == UINT32_MAX,
          "rollback restores an unbound contextual frame slot");

    if (child_initialized)
        bindings_builder_free(&child);
    bindings_activation_view_free(&frame);
    bindings_builder_free(&parent);
}

static void test_frame_slot_is_value_authority(Arena *arena) {
    const uint32_t epoch = 717u;
    VarId source_id = UINT64_C(88941);
    VarId framed_id = var_epoch_id(source_id, epoch);
    Atom *authoritative = atom_int(arena, 41);
    Atom *history_decoy = atom_int(arena, 99);
    Bindings bindings;
    bindings_init(&bindings);
    bool ready = bindings_register_complete_contextual_frame(
        &bindings, &source_id, 1u, epoch);
    CHECK(ready && !bindings_has_bound_values(&bindings),
          "an unbound frame reports no authoritative values");
    ready = ready && bindings_add_id(
        &bindings, framed_id, SYMBOL_ID_NONE, authoritative);
    BindingValue observed = ready
        ? bindings_lookup_value_id(&bindings, framed_id)
        : binding_value_from_atom(NULL);
    CHECK(ready && observed.skeleton == authoritative &&
              bindings.len == 0u && bindings_has_bound_values(&bindings),
          "a framed lookup reads its authoritative slot without a chronological row");
    Atom *rewritten = atom_int(arena, 42);
    CHECK(ready && bindings_rewrite_value_id(
              &bindings, framed_id,
              binding_value_from_atom(rewritten)) &&
              bindings_lookup_value_id(
                  &bindings, framed_id).skeleton == rewritten,
          "logical value replacement addresses an authoritative frame slot");
    CHECK(!bindings_rewrite_value_id(
              &bindings, var_epoch_id(source_id + 1u, epoch),
              binding_value_from_atom(rewritten)),
          "logical value replacement refuses a missing frame coordinate");
    bindings_free(&bindings);

    Bindings late;
    bindings_init(&late);
    CHECK(bindings_add_id(
              &late, framed_id, SYMBOL_ID_NONE, history_decoy) &&
          late.len == 0u && bindings_register_complete_contextual_frame(
              &late, &source_id, 1u, epoch) &&
          bindings_lookup_value_id(&late, framed_id).skeleton == history_decoy,
          "a framed write establishes slot ownership before schema completion");
    bindings_free(&late);

    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL) &&
              bindings_register_complete_contextual_frame(
                  &builder.current, &source_id, 1u, epoch),
          "initialize an authoritative frame slot for rollback");
    uint32_t mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_id_fresh(
              &builder, framed_id, SYMBOL_ID_NONE, authoritative) &&
              builder.frame_undo_len == 1u &&
              bindings_lookup_value_id(
                  &builder.current, framed_id).skeleton == authoritative,
          "a frame write publishes its slot value and one inverse update");
    bindings_builder_rollback(&builder, mark);
    CHECK(bindings_lookup_value_id(
              &builder.current, framed_id).skeleton == NULL &&
              builder.frame_undo_len == 0u &&
              !bindings_has_bound_values(&builder.current),
          "rollback applies the inverse slot update without row reconstruction");
    bindings_builder_free(&builder);
}

static void test_frame_slot_promotion_lifetime(void) {
    Arena source;
    Arena owner;
    arena_init(&source);
    arena_init(&owner);
    const uint32_t epoch = 718u;
    VarId source_id = UINT64_C(88951);
    VarId framed_id = var_epoch_id(source_id, epoch);
    Atom *head = atom_symbol(&source, "promoted-frame-head");
    Atom *payload = atom_expr2(&source, head, atom_int(&source, 73));
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL) &&
        bindings_register_complete_contextual_frame(
            &builder.current, &source_id, 1u, epoch);
    uint32_t mark = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_add_id_fresh(
        &builder, framed_id, SYMBOL_ID_NONE, payload);
    CHECK(ready && bindings_builder_promote_atoms_to_arena(
              &builder, &owner),
          "promotion moves authoritative frame-slot values to their owner");
    arena_free(&source);
    BindingValue observed = ready
        ? bindings_lookup_value_id(&builder.current, framed_id)
        : binding_value_from_atom(NULL);
    CHECK(observed.skeleton && observed.skeleton->kind == ATOM_EXPR &&
              observed.skeleton->expr.len == 2u &&
              observed.skeleton->expr.elems[1]->kind == ATOM_GROUNDED &&
              observed.skeleton->expr.elems[1]->ground.gkind == GV_INT &&
              observed.skeleton->expr.elems[1]->ground.ival == 73,
          "the frame slot survives release of the syntax source arena");
    bindings_builder_rollback(&builder, mark);
    CHECK(!bindings_lookup_value_id(
              &builder.current, framed_id).skeleton,
          "promoted slot state remains rollback-capable");
    bindings_builder_free(&builder);
    arena_free(&owner);
}

static void test_manufactured_slot_lifecycle(void) {
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    Arena source, survivor;
    arena_init(&source);
    arena_init(&survivor);
    CettaFrameIdentity identity = cetta_frame_identity_scope_fresh(&identities);
    BindingsBuilder parent, sibling;
    Bindings projected;
    bindings_init(&projected);
    bool ready = bindings_builder_init(&parent, NULL);
    Atom *x = ready ? bindings_builder_new_variable(&parent, &source, identity) : NULL;
    Atom *y = x ? bindings_builder_new_variable(&parent, &source, identity) : NULL;
    uint32_t mark = bindings_builder_save(&parent);
    Atom *last = y;
    for (uint32_t i = 0u; last && i < 40u; i++)
        last = bindings_builder_new_variable(&parent, &source, identity);
    Atom *payload = last ? atom_expr3(&source,
        atom_symbol(&source, "ManufacturedPair"), y, last) : NULL;
    ready = payload && bindings_builder_add_var_fresh(&parent, x, payload) &&
        bindings_builder_add_var_fresh(&parent, y, atom_int(&source, 73)) &&
        bindings_builder_add_var_fresh(&parent, last, atom_int(&source, 91));
    bool sibling_ready = ready && bindings_builder_init(&sibling, &parent.current);
    CHECK(sibling_ready && parent.current.len == 0u &&
              var_base_id(last->var_id) == 42u,
          "manufactured variables extend one dense frame without binding rows");
    uint32_t seal_mark = bindings_builder_save(&parent);
    VarId first_slot = 1u;
    BindingsFrameSchema *sealed_schema = bindings_frame_schema_new(&first_slot, 1u);
    bool sealed = ready && sealed_schema && bindings_builder_register_complete_frame_schema(
        &parent, sealed_schema, identity);
    bindings_frame_schema_release(sealed_schema);
    bindings_builder_rollback(&parent, seal_mark);
    size_t after_seal_count = 0u;
    CHECK(sealed && bindings_current_binding_count(&parent.current, &after_seal_count) &&
              after_seal_count == 3u && binding_is_int(&parent.current, last->var_id, 91),
          "undoing inventory completion preserves every manufactured slot and its value count");
    VarId root_id = x ? x->var_id : VAR_ID_NONE;
    VarId last_id = last ? last->var_id : VAR_ID_NONE;
    bool projected_ok = sibling_ready && bindings_project_reachable(
        &sibling.current, &x, 1u, &projected) &&
        bindings_promote_logical_atoms_to_arena(&projected, &survivor);
    bindings_builder_rollback(&parent, mark);
    Atom *next = ready ? bindings_builder_new_variable(&parent, &source, identity) : NULL;
    CHECK(next && next->var_id != last_id && var_base_id(next->var_id) > 42u &&
              !bindings_has_bound_values(&parent.current) && sibling_ready &&
              binding_is_int(&sibling.current, last_id, 91),
          "rollback restores values without recycling identities held by a sibling");
    Atom *sibling_next = sibling_ready
        ? bindings_builder_new_variable(&sibling, &source, identity) : NULL;
    CHECK(next && sibling_next && sibling_next->var_id != next->var_id &&
              bindings_builder_add_var_fresh(&parent, next, atom_int(&source, 101)) &&
              !bindings_lookup_value_id(&sibling.current, next->var_id).skeleton,
          "branch-local slot growth cannot alias a sibling's manufactured variable");
    if (sibling_ready)
        bindings_builder_free(&sibling);
    bindings_builder_free(&parent);
    arena_free(&source);
    Atom *root = projected_ok
        ? atom_var_with_id(&survivor, "result", root_id) : NULL;
    Atom *observed = root ? bindings_apply(&projected, &survivor, root) : NULL;
    CHECK(observed && observed->kind == ATOM_EXPR && observed->expr.len == 3u &&
              observed->expr.elems[1]->kind == ATOM_GROUNDED &&
              observed->expr.elems[1]->ground.ival == 73 &&
              observed->expr.elems[2]->kind == ATOM_GROUNDED &&
              observed->expr.elems[2]->ground.ival == 91,
          "projected dynamic slots retain aliases and syntax after source release");
    bindings_free(&projected);
    arena_free(&survivor);
}

static void test_frame_index_cycle_bridge(Arena *arena) {
    const uint32_t epoch = 702u;
    VarId source_id = UINT64_C(88001);
    VarId framed_id = var_epoch_id(source_id, epoch);
    Atom *source = atom_var_with_id(arena, "cycle-frame-source", source_id);
    VarId ids[] = {source_id};
    Atom *variables[] = {source};
    Atom *framed = atom_var_with_id(
        arena, "cycle-frame-coordinate", framed_id);
    Atom *x = atom_var_with_id(arena, "cycle-generic-x", UINT64_C(88101));
    Atom *y = atom_var_with_id(arena, "cycle-generic-y", UINT64_C(88102));
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, &empty);
    bindings_free(&empty);
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    ready = ready && bindings_activation_view_prepare(
        &frame, &builder, ids, variables, 1u, epoch, 0u);
    ready = ready && bindings_builder_add_id_fresh(
        &builder, framed_id, SYMBOL_ID_NONE, y);
    ready = ready && bindings_builder_add_id_fresh(
        &builder, x->var_id, SYMBOL_ID_NONE, framed);
    for (uint32_t i = 0u; i < 64u && ready; i++) {
        ready = bindings_builder_add_id_fresh(
            &builder, UINT64_C(88200) + i, SYMBOL_ID_NONE,
            atom_int(arena, i));
    }
    uint32_t before = builder.current.len;
    bool accepted = ready && bindings_builder_add_id_fresh(
        &builder, y->var_id, SYMBOL_ID_NONE, x);
    Arena observed;
    arena_init(&observed);
    Atom *residual = accepted
        ? bindings_apply(&builder.current, &observed, x) : NULL;
    CHECK(!accepted && builder.current.len == before &&
              !bindings_has_loop(&builder.current) && !residual,
          "generic reachability follows a frame coordinate and refuses the closing edge");
    arena_free(&observed);
    bindings_activation_view_free(&frame);
    bindings_builder_free(&builder);
}

static void test_frame_only_merge_occurs_check(Arena *arena) {
    const uint32_t left_epoch = 721u;
    const uint32_t right_epoch = 722u;
    VarId left_source = UINT64_C(88961);
    VarId right_source = UINT64_C(88963);
    VarId left_id = var_epoch_id(left_source, left_epoch);
    VarId right_id = var_epoch_id(right_source, right_epoch);
    Atom *left_var = atom_var_with_id(
        arena, "merge-cycle-left", left_id);
    Atom *right_var = atom_var_with_id(
        arena, "merge-cycle-right", right_id);
    Atom *left_tail = atom_var_with_id(
        arena, "merge-cycle-left-tail", UINT64_C(88965));
    Atom *right_tail = atom_var_with_id(
        arena, "merge-cycle-right-tail", UINT64_C(88967));
    Atom *node = atom_symbol(arena, "MergeCycleNode");
    Atom *left_value = atom_expr3(
        arena, node, right_var, left_tail);
    Atom *right_value = atom_expr3(
        arena, node, left_var, right_tail);

    Bindings left;
    Bindings right;
    bindings_init(&left);
    bindings_init(&right);
    bool ready =
        bindings_register_complete_contextual_frame(
            &left, &left_source, 1u, left_epoch) &&
        bindings_register_complete_contextual_frame(
            &right, &right_source, 1u, right_epoch) &&
        bindings_add_id(
            &left, left_id, SYMBOL_ID_NONE, left_value) &&
        bindings_add_id(
            &right, right_id, SYMBOL_ID_NONE, right_value);
    CHECK(ready && !bindings_has_loop(&left) &&
              !bindings_has_loop(&right),
          "separate frame-only substitutions are independently acyclic");

    Bindings refused;
    bool refused_ready = bindings_clone(&refused, &left);
    CHECK(refused_ready && !bindings_try_merge_live(&refused, &right) &&
              binding_value_equal(
                  bindings_lookup_value_id(&refused, left_id),
                  binding_value_from_atom(left_value)) &&
              !bindings_lookup_value_id(&refused, right_id).skeleton &&
              !bindings_has_loop(&refused),
          "merging frame-only substitutions rejects a cross-context cycle transactionally");
    if (refused_ready)
        bindings_free(&refused);

    Bindings grounded_right;
    bindings_init(&grounded_right);
    bool grounded_ready =
        bindings_register_complete_contextual_frame(
            &grounded_right, &right_source, 1u, right_epoch) &&
        bindings_add_id(
            &grounded_right, right_id, SYMBOL_ID_NONE,
            atom_int(arena, 79));
    Bindings accepted;
    bool accepted_ready = bindings_clone(&accepted, &left);
    CHECK(grounded_ready && accepted_ready &&
              bindings_try_merge_live(&accepted, &grounded_right) &&
              binding_is_int(&accepted, right_id, 79) &&
              !bindings_has_loop(&accepted),
          "merging compatible frame-only substitutions preserves both slot authorities");
    if (accepted_ready)
        bindings_free(&accepted);
    bindings_free(&grounded_right);
    bindings_free(&right);
    bindings_free(&left);
}

static void test_frame_schema_branch_lifetime(Arena *arena) {
    const uint32_t epoch = 709u;
    VarId ids[] = {UINT64_C(88901), UINT64_C(88903)};
    Bindings parent, child;
    bindings_init(&parent);
    CHECK(bindings_register_contextual_frame(&parent, ids, 1u, epoch) &&
          bindings_add_id(&parent, var_epoch_id(ids[0], epoch), SYMBOL_ID_NONE,
                          atom_int(arena, 19)),
          "publish a partial frame inventory before capture");
    CHECK(bindings_clone(&child, &parent), "capture a frame inventory");
    const void *old_schema = NULL, *old_slots = NULL;
    const void *new_schema = NULL, *new_slots = NULL;
    CHECK(bindings_frame_storage_test_identity(&parent, epoch, &old_schema, &old_slots) &&
          bindings_register_contextual_frame(&child, ids, 2u, epoch) &&
          bindings_frame_storage_test_identity(&child, epoch, &new_schema, &new_slots) &&
          old_schema != new_schema,
          "extending an inventory publishes a new schema without changing a captured schema");
    bool known = true;
    uint32_t entry = 0u;
    CHECK(bindings_frame_index_test_lookup(&parent, var_epoch_id(ids[1], epoch),
                                          &known, &entry) && !known &&
          binding_is_int(&parent, var_epoch_id(ids[0], epoch), 19),
          "the captured parent retains its original inventory and values");
    bindings_free(&parent);
    CHECK(bindings_add_id(&child, var_epoch_id(ids[1], epoch), SYMBOL_ID_NONE,
                         atom_int(arena, 23)) &&
          binding_is_int(&child, var_epoch_id(ids[0], epoch), 19) &&
          binding_is_int(&child, var_epoch_id(ids[1], epoch), 23),
          "the child owns its expanded schema after the parent is released");
    bindings_free(&child);

    bindings_init(&parent);
    CHECK(bindings_register_contextual_frame(&parent, ids, 2u, epoch),
          "publish an unbound inventory shared with a future branch");
    BindingsBuilder branch;
    CHECK(bindings_builder_init(&branch, &parent), "capture an unbound frame in a builder");
    uint32_t mark = bindings_builder_save(&branch);
    CHECK(bindings_builder_add_id_fresh(&branch, var_epoch_id(ids[0], epoch),
                                       SYMBOL_ID_NONE, atom_int(arena, 29)),
          "branch write detaches shared slot storage");
    bindings_builder_rollback(&branch, mark);
    VarId replacement[] = {UINT64_C(88921), UINT64_C(88923)};
    CHECK(bindings_register_contextual_frame(&branch.current, replacement, 2u, epoch + 1u),
          "reuse retired slot storage for another activation");
    known = false;
    CHECK(bindings_frame_index_test_lookup(&parent, var_epoch_id(ids[1], epoch),
                                          &known, &entry) && known && entry == UINT32_MAX,
          "recycling child storage does not rewrite the parent's shared schema");
    bindings_builder_free(&branch);
    bindings_free(&parent);
}

static void test_builder_frame_registration_rollback(Arena *arena) {
    const uint32_t epoch = 710u;
    VarId ids[] = {UINT64_C(88911), UINT64_C(88913)};
    VarId third = UINT64_C(88915);
    VarId first_id = var_epoch_id(ids[0], epoch);
    VarId second_id = var_epoch_id(ids[1], epoch);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL);
    uint32_t origin = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &ids[0], 1u, epoch);
    uint32_t registered = ready ? bindings_builder_save(&builder) : 0u;
    bool first_known = false;
    uint32_t first_entry = 0u;
    bindings_builder_rollback(&builder, registered);
    CHECK(ready && registered > origin &&
              bindings_frame_index_test_lookup(
                  &builder.current, first_id,
                  &first_known, &first_entry) &&
              first_known && first_entry == UINT32_MAX,
          "a save after frame manufacture preserves that activation on rollback");

    ready = ready && bindings_builder_add_id_fresh(
        &builder, first_id, SYMBOL_ID_NONE, atom_int(arena, 43));
    uint32_t valued = ready ? bindings_builder_save(&builder) : 0u;
    BindingsFrameSchema *complete_schema =
        bindings_frame_schema_new(ids, 2u);
    ready = ready && complete_schema &&
        bindings_builder_register_complete_frame_schema(
            &builder, complete_schema, epoch) &&
        bindings_builder_add_id_fresh(
            &builder, second_id, SYMBOL_ID_NONE, atom_int(arena, 47));
    CHECK(ready && binding_is_int(&builder.current, first_id, 43) &&
              binding_is_int(&builder.current, second_id, 47),
          "a branch may extend and complete an activation frame with slot values");
    bindings_builder_rollback(&builder, valued);
    bool second_known = true;
    uint32_t second_entry = 0u;
    CHECK(ready && binding_is_int(&builder.current, first_id, 43) &&
              bindings_frame_index_test_lookup(
                  &builder.current, second_id,
                  &second_known, &second_entry) &&
              !second_known,
          "rollback removes a branch-local schema extension and preserves older slots");

    bool third_registered = ready &&
        bindings_builder_register_contextual_frame(
            &builder, &third, 1u, epoch);
    bool third_known = false;
    uint32_t third_entry = 0u;
    CHECK(third_registered &&
              bindings_frame_index_test_lookup(
                  &builder.current, var_epoch_id(third, epoch),
                  &third_known, &third_entry) &&
              third_known && third_entry == UINT32_MAX,
          "rollback restores an incomplete frame rather than retaining branch-local closure");
    bindings_builder_rollback(&builder, valued);

    bindings_builder_rollback(&builder, registered);
    first_known = false;
    first_entry = 0u;
    CHECK(bindings_frame_index_test_lookup(
              &builder.current, first_id, &first_known, &first_entry) &&
              first_known && first_entry == UINT32_MAX &&
              !bindings_lookup_value_id(
                  &builder.current, first_id).skeleton,
          "rolling back slot writes keeps the activation inventory alive");
    bindings_builder_rollback(&builder, origin);
    const void *schema_identity = NULL;
    const void *slot_identity = NULL;
    first_known = true;
    CHECK(bindings_frame_index_test_lookup(
              &builder.current, first_id, &first_known, &first_entry) &&
              !first_known &&
              !bindings_frame_storage_test_identity(
                  &builder.current, epoch,
                  &schema_identity, &slot_identity),
          "rolling back before frame manufacture removes the activation itself");
    bindings_frame_schema_release(complete_schema);
    bindings_builder_free(&builder);
}

static void test_frame_registration_compaction_rebase(Arena *arena) {
    const uint32_t epoch = 712u;
    VarId ids[] = {UINT64_C(88931), UINT64_C(88933)};
    VarId extension = UINT64_C(88935);
    VarId first_id = var_epoch_id(ids[0], epoch);
    VarId second_id = var_epoch_id(ids[1], epoch);
    VarId extension_id = var_epoch_id(extension, epoch);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL);
    uint32_t origin = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &ids[0], 1u, epoch);
    uint32_t registered = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_add_id_fresh(
        &builder, first_id, SYMBOL_ID_NONE, atom_int(arena, 53));
    uint32_t first_bound = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &ids[1], 1u, epoch) &&
        bindings_builder_add_id_fresh(
            &builder, second_id, SYMBOL_ID_NONE, atom_int(arena, 59));

    Atom *first_root = atom_var_with_id(arena, "compact-frame-first", first_id);
    Atom *second_root = atom_var_with_id(arena, "compact-frame-second", second_id);
    Atom *roots[] = {first_root, second_root};
    uint32_t marks[] = {origin, registered, first_bound};
    uint32_t old_registration_undo_len =
        builder.frame_registration_undo_len;
    bool compacted = ready && old_registration_undo_len > 0u &&
        bindings_builder_compact_reachable(
            &builder, roots, 2u, marks, 3u, NULL, NULL);
    CHECK(compacted && builder.frame_registration_undo_len == 0u &&
              !builder.frame_registration_save_barrier &&
              binding_is_int(&builder.current, first_id, 53) &&
              binding_is_int(&builder.current, second_id, 59),
          "compaction rebases frame registration history onto its projected directory");

    if (compacted)
        bindings_builder_rollback(&builder, marks[2]);
    bool second_known = false;
    uint32_t second_entry = 0u;
    CHECK(compacted && binding_is_int(&builder.current, first_id, 53) &&
              !bindings_lookup_value_id(
                  &builder.current, second_id).skeleton &&
              bindings_frame_index_test_lookup(
                  &builder.current, second_id,
                  &second_known, &second_entry) && second_known,
          "rollback after compaction restores values within the projected frame base");

    if (compacted)
        bindings_builder_rollback(&builder, marks[1]);
    uint32_t projected_base = bindings_builder_save(&builder);
    bool extended = compacted &&
        !bindings_lookup_value_id(&builder.current, first_id).skeleton &&
        bindings_builder_register_contextual_frame(
            &builder, &extension, 1u, epoch);
    bool extension_known = false;
    uint32_t extension_entry = 0u;
    CHECK(extended && builder.frame_registration_undo_len == 1u &&
              bindings_frame_index_test_lookup(
                  &builder.current, extension_id,
                  &extension_known, &extension_entry) && extension_known,
          "frame manufacture after compaction records history against the projected base");
    if (extended)
        bindings_builder_rollback(&builder, projected_base);
    extension_known = true;
    bool first_known = false;
    uint32_t first_entry = 0u;
    CHECK(extended && builder.frame_registration_undo_len == 0u &&
              bindings_frame_index_test_lookup(
                  &builder.current, first_id,
                  &first_known, &first_entry) && first_known &&
              bindings_frame_index_test_lookup(
                  &builder.current, extension_id,
                  &extension_known, &extension_entry) && !extension_known,
          "rollback removes post-compaction frame extensions without erasing the projected base");
    bindings_builder_free(&builder);
}

static void test_frame_registration_undo_promotion(void) {
    Arena source;
    Arena owner;
    arena_init(&source);
    arena_init(&owner);
    const uint32_t epoch = 714u;
    VarId ids[] = {UINT64_C(88925), UINT64_C(88927)};
    Atom *first_key = atom_expr2(
        &source, atom_symbol(&source, "RegistrationName"),
        atom_int(&source, 1));
    Atom *second_key = atom_expr2(
        &source, atom_symbol(&source, "RegistrationName"),
        atom_int(&source, 2));
    Atom *variables[] = {
        atom_var_with_name_key(&source, first_key, ids[0]),
        atom_var_with_name_key(&source, second_key, ids[1]),
    };
    BindingsFrameSchema *first_schema =
        bindings_frame_schema_new_presented(ids, variables, 1u);
    BindingsFrameSchema *full_schema =
        bindings_frame_schema_new_presented(ids, variables, 2u);
    BindingsBuilder builder;
    bool ready = first_schema && full_schema &&
        bindings_builder_init(&builder, NULL) &&
        bindings_builder_register_frame_schema(
            &builder, first_schema, epoch);
    uint32_t first_mark = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_frame_schema(
        &builder, full_schema, epoch) &&
        bindings_builder_promote_atoms_to_arena(&builder, &owner);
    CHECK(ready,
          "promotion owns both the live frame and its rollback inventory");
    bindings_frame_schema_release(first_schema);
    bindings_frame_schema_release(full_schema);
    arena_free(&source);
    if (ready)
        bindings_builder_rollback(&builder, first_mark);
    bool first_known = false;
    bool second_known = true;
    uint32_t entry = 0u;
    bool first_lookup = ready && bindings_frame_index_test_lookup(
        &builder.current, var_epoch_id(ids[0], epoch),
        &first_known, &entry);
    bool second_lookup = ready && bindings_frame_index_test_lookup(
        &builder.current, var_epoch_id(ids[1], epoch),
        &second_known, &entry);
    bool owner_closed = ready && bindings_logical_atoms_closed_for_arena(
        &builder.current, &owner);
    CHECK(ready && first_lookup && first_known && second_lookup &&
              !second_known && owner_closed,
          "a restored frame remains valid after releasing its syntax source arena");
    if (ready)
        bindings_builder_free(&builder);
    arena_free(&owner);
}

static void test_prepared_frame_schema_lifetime(Arena *arena) {
    VarId ids[] = {UINT64_C(88931), UINT64_C(88933)};
    VarId first = ids[0], second = ids[1];
    BindingsFrameSchema *schema = bindings_frame_schema_new(ids, 2u);
    CHECK(schema && bindings_frame_schema_len(schema) == 2u,
          "prepare an owned equation inventory");
    ids[0] = UINT64_C(99999);
    CHECK(schema && bindings_frame_schema_source_ids(schema)[0] == first,
          "a prepared schema owns its ids independently of the input buffer");
    Bindings bindings;
    bindings_init(&bindings);
    CHECK(bindings_register_complete_frame_schema(&bindings, schema, 711u) &&
          bindings_register_complete_frame_schema(&bindings, schema, 712u),
          "two activations retain one prepared equation schema");
    const void *first_schema = NULL, *first_slots = NULL;
    const void *second_schema = NULL, *second_slots = NULL;
    CHECK(bindings_frame_storage_test_identity(&bindings, 711u, &first_schema, &first_slots) &&
          bindings_frame_storage_test_identity(&bindings, 712u, &second_schema, &second_slots) &&
          first_schema == schema && second_schema == schema && first_slots != second_slots,
          "equation identities are shared while activation slot storage is independent");

    Atom *source_variables[] = {
        atom_var_with_id(arena, "prepared-frame-first", first),
        atom_var_with_id(arena, "prepared-frame-second", second),
    };
    BindingsBuilder prepared_builder;
    bool prepared_ready = bindings_builder_init(
        &prepared_builder, &bindings);
    BindingsActivationView prepared_frame;
    bindings_activation_view_init(&prepared_frame);
    test_runtime_stats_reset_counters();
    CHECK(prepared_ready && bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, schema,
              source_variables, 711u, 0u) &&
          prepared_frame.source_ids == bindings_frame_schema_source_ids(schema) &&
          test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 0u,
          "a prepared equation borrows its published frame without registering it again");
    VarId manufactured_id = UINT64_C(88937);
    Atom *manufactured_var = atom_var_with_id(
        arena, "prepared-frame-manufactured", manufactured_id);
    BindingsFrameSchema *manufactured_schema =
        bindings_frame_schema_new_presented(
            &manufactured_id, &manufactured_var, 1u);
    CHECK(manufactured_schema &&
          bindings_builder_register_frame_schema(
              &prepared_builder, manufactured_schema, 713u) &&
          bindings_activation_view_available(&prepared_frame, &prepared_builder.current) &&
          prepared_frame.source_ids == bindings_frame_schema_source_ids(schema),
          "another activation cannot invalidate an identity-based frame view");
    bindings_frame_schema_release(manufactured_schema);
    test_runtime_stats_reset_counters();
    CHECK(!bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, schema,
              NULL, 711u, 0u),
          "a nonempty prepared equation refuses a missing source-variable inventory");
    CHECK(bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, schema,
              source_variables, 714u, 0u) &&
          prepared_frame.source_ids == bindings_frame_schema_source_ids(schema) &&
          test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 1u &&
          test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_NEW) == 1u,
          "a missing prepared frame is published exactly once before borrowing");
    VarId different_ids[] = {first, second + UINT64_C(2)};
    BindingsFrameSchema *different_schema =
        bindings_frame_schema_new(different_ids, 2u);
    CHECK(different_schema &&
          !bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, different_schema,
              source_variables, 711u, 0u),
          "a published complete frame refuses a different schema for the same activation");
    bindings_frame_schema_release(different_schema);
    bindings_activation_view_free(&prepared_frame);
    if (prepared_ready)
        bindings_builder_free(&prepared_builder);

    bindings_frame_schema_release(schema);
    CHECK(bindings_add_id(&bindings, var_epoch_id(first, 711u), SYMBOL_ID_NONE,
                          atom_int(arena, 31)) &&
          bindings_add_id(&bindings, var_epoch_id(first, 712u), SYMBOL_ID_NONE,
                          atom_int(arena, 33)) &&
          binding_is_int(&bindings, var_epoch_id(first, 711u), 31) &&
          binding_is_int(&bindings, var_epoch_id(first, 712u), 33),
          "activations retain their schema after the preparing owner is released");
    Atom *full_frame_value = bindings_apply_epoch(
        &bindings, arena, source_variables[0], 711u);
    Atom *unbound_frame_value = bindings_apply_epoch(
        &bindings, arena, source_variables[1], 711u);
    CHECK(full_frame_value && full_frame_value->kind == ATOM_GROUNDED &&
          full_frame_value->ground.gkind == GV_INT &&
          full_frame_value->ground.ival == 31 &&
          unbound_frame_value && unbound_frame_value->kind == ATOM_VAR &&
          unbound_frame_value->var_id == var_epoch_id(second, 711u),
          "a full epoch view observes a frame-only value and preserves an unbound slot");
    bool known = false;
    uint32_t entry = 0u;
    CHECK(bindings_frame_index_test_lookup(&bindings, var_epoch_id(second, 711u),
                                          &known, &entry) && known && entry == UINT32_MAX,
          "an unbound slot survives release of its preparing owner");
    bindings_free(&bindings);

    VarId duplicate[] = {first, first};
    VarId qualified[] = {var_epoch_id(first, 711u)};
    CHECK(bindings_frame_schema_new(duplicate, 2u) == NULL &&
          bindings_frame_schema_new(qualified, 1u) == NULL &&
          bindings_frame_schema_new(ids, 2u) == NULL &&
          bindings_frame_schema_new(NULL, 1u) == NULL,
          "schema preparation refuses duplicate, qualified, unsorted and missing ids");
    schema = bindings_frame_schema_new(NULL, 0u);
    bindings_init(&bindings);
    CHECK(schema && bindings_frame_schema_len(schema) == 0u &&
          bindings_register_complete_frame_schema(&bindings, schema, 713u),
          "a ground equation has a valid empty schema");
    bindings_frame_schema_release(schema);
    bindings_free(&bindings);
}

static void test_presented_manufacture_frame_grows_until_closed(
        Arena *arena) {
    const uint32_t epoch = 715u;
    VarId ids[] = {UINT64_C(88941), UINT64_C(88943), UINT64_C(88945)};
    Atom *first_variable = atom_var_with_id(
        arena, "manufactured-first", ids[0]);
    Atom *second_variable = atom_var_with_id(
        arena, "manufactured-second", ids[1]);
    Atom *third_variable = atom_var_with_id(
        arena, "manufactured-third", ids[2]);
    BindingsFrameSchema *first_schema =
        bindings_frame_schema_new_presented(
            &ids[0], &first_variable, 1u);
    BindingsFrameSchema *second_schema =
        bindings_frame_schema_new_presented(
            &ids[1], &second_variable, 1u);
    BindingsFrameSchema *third_schema =
        bindings_frame_schema_new_presented(
            &ids[2], &third_variable, 1u);
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, &empty);
    bindings_free(&empty);
    CHECK(ready && first_schema && second_schema && third_schema &&
          bindings_builder_register_frame_schema(
              &builder, first_schema, epoch) &&
          bindings_builder_register_frame_schema(
              &builder, second_schema, epoch) &&
          bindings_builder_add_id_fresh(
              &builder, var_epoch_id(ids[0], epoch), SYMBOL_ID_NONE,
              atom_int(arena, 37)) &&
          bindings_builder_add_id_fresh(
              &builder, var_epoch_id(ids[1], epoch), SYMBOL_ID_NONE,
              atom_int(arena, 41)) &&
          binding_is_int(
              &builder.current, var_epoch_id(ids[0], epoch), 37) &&
          binding_is_int(
              &builder.current, var_epoch_id(ids[1], epoch), 41),
          "a dynamic activation merges presented manufactured slots into one frame");
    CHECK(ready &&
          bindings_register_complete_contextual_frame(
              &builder.current, ids, 2u, epoch) &&
          !bindings_builder_register_frame_schema(
              &builder, third_schema, epoch),
          "closing a manufactured frame rejects a later undeclared slot");
    bindings_frame_schema_release(first_schema);
    bindings_frame_schema_release(second_schema);
    bindings_frame_schema_release(third_schema);
    if (ready)
        bindings_builder_free(&builder);
}

static void test_complete_contextual_frame_is_closed(Arena *arena) {
    const uint32_t epoch = 703u;
    VarId source_ids[] = {UINT64_C(89001), UINT64_C(89003)};
    VarId extra_source_id = UINT64_C(89005);
    VarId first = var_epoch_id(source_ids[0], epoch);
    VarId second = var_epoch_id(source_ids[1], epoch);
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, &empty);
    bindings_free(&empty);
    bool admitted = ready &&
        bindings_register_complete_contextual_frame(
            &builder.current, source_ids, 2u, epoch);
    bool first_known = false;
    bool second_known = false;
    uint32_t first_entry = 0u;
    uint32_t second_entry = 0u;
    CHECK(admitted &&
              bindings_frame_index_test_lookup(
                  &builder.current, first, &first_known,
                  &first_entry) &&
              bindings_frame_index_test_lookup(
                  &builder.current, second, &second_known,
                  &second_entry) &&
              first_known && second_known &&
              first_entry == UINT32_MAX &&
              second_entry == UINT32_MAX &&
              bindings_builder_add_id_fresh(
                  &builder, first, SYMBOL_ID_NONE,
                  atom_int(arena, 703)) &&
              binding_is_int(&builder.current, first, 703),
          "a complete contextual frame owns every declared slot before binding");
    CHECK(!bindings_register_contextual_frame(
              &builder.current, &extra_source_id, 1u, epoch) &&
              bindings_lookup_value_id(
                  &builder.current,
                  var_epoch_id(extra_source_id, epoch)).skeleton == NULL &&
              binding_is_int(&builder.current, first, 703),
          "a complete contextual frame refuses undeclared coordinates without mutation");
    if (ready)
        bindings_builder_free(&builder);
}

static void test_lookup_domain_accounting(Arena *arena) {
    const uint32_t epoch = 703u;
    const VarId source_ids[] = {UINT64_C(88301), UINT64_C(88302)};
    const VarId framed_hit = var_epoch_id(source_ids[0], epoch);
    const VarId framed_miss = var_epoch_id(source_ids[1], epoch);
    const VarId generic_hit = UINT64_C(88311);
    const VarId generic_miss = UINT64_C(88312);
    const VarId unowned_contextual_miss =
        var_epoch_id(UINT64_C(88313), epoch + 1u);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL);
    test_runtime_stats_reset_counters();
    ready = ready && bindings_register_contextual_frame(
        &builder.current, source_ids, 1u, epoch);
    ready = ready && bindings_register_contextual_frame(
        &builder.current, source_ids, 1u, epoch);
    ready = ready && bindings_register_contextual_frame(
        &builder.current, source_ids, 2u, epoch);
    CHECK(ready && test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 3u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_NEW) == 1u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_KNOWN) == 1u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_EXTENDED) == 1u,
          "frame registration accounts new, known, and extended schemas");
    ready = ready && bindings_builder_add_id_fresh(
        &builder, framed_hit, SYMBOL_ID_NONE, atom_int(arena, 31));
    ready = ready && bindings_builder_add_id_fresh(
        &builder, generic_hit, SYMBOL_ID_NONE, atom_int(arena, 37));

    test_runtime_stats_reset_counters();
    BindingValue framed_value = ready
        ? bindings_lookup_value_id(&builder.current, framed_hit)
        : binding_value_from_atom(NULL);
    BindingValue absent_frame_value = ready
        ? bindings_lookup_value_id(&builder.current, framed_miss)
        : binding_value_from_atom(NULL);
    BindingValue generic_value = ready
        ? bindings_lookup_value_id(&builder.current, generic_hit)
        : binding_value_from_atom(NULL);
    BindingValue absent_generic_value = ready
        ? bindings_lookup_value_id(&builder.current, generic_miss)
        : binding_value_from_atom(NULL);
    BindingValue absent_unowned_contextual_value = ready
        ? bindings_lookup_value_id(
            &builder.current, unowned_contextual_miss)
        : binding_value_from_atom(NULL);

    CHECK(ready && framed_value.skeleton &&
              !absent_frame_value.skeleton && generic_value.skeleton &&
              !absent_generic_value.skeleton &&
              !absent_unowned_contextual_value.skeleton,
          "lookup-domain fixture distinguishes present and absent coordinates");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_FRAME_COORDINATE) == 3u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_GENERIC_MAP) == 2u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_PLAIN_IDENTITY) == 2u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP_UNOWNED_CONTEXTUAL) == 0u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_LOOKUP) == 5u,
          "every lookup is accounted to exactly one coordinate domain");
    bindings_builder_free(&builder);
}

static void test_dense_inventory_is_slot_read(Arena *arena) {
    VarId ids[] = {UINT64_C(91001), UINT64_C(91002), UINT64_C(91003)};
    Atom *variables[] = {
        atom_var_with_id(arena, "inv-a", ids[0]),
        atom_var_with_id(arena, "inv-b", ids[1]),
        atom_var_with_id(arena, "inv-c", ids[2])
    };
    const uint32_t epoch = 19u;
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL),
          "slot-read builder");
    Atom *one = atom_int(arena, 1);
    Atom *two = atom_int(arena, 2);
    CHECK(bindings_builder_add_id_fresh(
              &builder, var_epoch_id(ids[0], epoch - 1u),
              SYMBOL_ID_NONE, two),
          "outer-epoch prefix stays a trail binding");
    uint32_t first = builder.current.len;
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    CHECK(bindings_activation_view_prepare(
              &frame, &builder, ids, variables, 3u, epoch, first),
          "prepare the activation inventory");

    CHECK(bindings_activation_view_available(&frame, &builder.current),
          "prepared view resolves its activation");

    bindings_lookup_index_test_clear(&builder.current);
    VarId local_a = var_epoch_id(ids[0], epoch);
    VarId local_b = var_epoch_id(ids[1], epoch);
    uint32_t synced = 0u;
    BindingValue slot = binding_value_from_atom(NULL);
    CHECK(lookup_frame_value(
              &builder.current, local_a, &slot) && slot.skeleton == NULL &&
              !bindings_lookup_index_test_synced_len(
                  &builder.current, &synced),
          "unbound inventory slot is empty without rebuilding the index");
    CHECK(bindings_lookup_value_id(&builder.current, var_epoch_id(ids[0], epoch - 1u)).skeleton == two,
          "a different epoch of the same source still uses the trail");

    CHECK(bindings_builder_add_id_fresh(
              &builder, local_a, SYMBOL_ID_NONE, one),
          "bind an inventory variable");
    bindings_lookup_index_test_clear(&builder.current);
    slot = binding_value_from_atom(NULL);
    CHECK(lookup_frame_value(
              &builder.current, local_a, &slot) && slot.skeleton == one &&
              !bindings_lookup_index_test_synced_len(
                  &builder.current, &synced),
          "bound inventory lookup is the slot, not a search");

    uint32_t mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_id_fresh(
              &builder, local_b, SYMBOL_ID_NONE, two),
          "bind a second inventory variable");
    slot = binding_value_from_atom(NULL);
    CHECK(lookup_frame_value(
              &builder.current, local_b, &slot) && slot.skeleton == two,
          "second slot is live during the attempt");
    bindings_builder_rollback(&builder, mark);
    BindingValue after_b = binding_value_from_atom(NULL);
    BindingValue after_a = binding_value_from_atom(NULL);
    CHECK(lookup_frame_value(
              &builder.current, local_b, &after_b) && after_b.skeleton == NULL &&
              lookup_frame_value(
                  &builder.current, local_a, &after_a) && after_a.skeleton == one,
          "rollback restores slots; the prior suffix survives");

    Atom *head = atom_symbol(arena, "Pair");
    Atom *pat_x = atom_var_with_id(arena, "inv-b", local_b);
    Atom *pattern = atom_expr3(arena, head, pat_x, pat_x);
    Atom *query = atom_expr3(arena, head, one, one);
    CHECK(match_atoms_builder(query, pattern, &builder) &&
              bindings_lookup_value_id(&builder.current, local_b).skeleton == one,
          "repeated inventory variable unifies with the display live");

    bindings_activation_view_free(&frame);
    CHECK(!bindings_activation_view_available(&frame, &builder.current),
          "free invalidates the activation view");
    bindings_builder_free(&builder);
}

static void test_term_stability_summary(Arena *arena) {
    static const bool expected[GV_INTERNAL_TAG + 1u] = {
        [GV_INT] = true,
        [GV_FLOAT] = true,
        [GV_BOOL] = true,
        [GV_STRING] = true,
        [GV_BIGINT] = true,
        [GV_RATIONAL] = true,
    };
    bool classification_exact = true;
    for (unsigned raw = 0u; raw <= (unsigned)GV_INTERNAL_TAG; raw++) {
        GroundedKind kind = (GroundedKind)raw;
        Atom probe = {0};
        probe.kind = ATOM_GROUNDED;
        probe.ground.gkind = kind;
        if (atom_grounded_kind_is_term_stable(kind) != expected[raw] ||
            term_universe_atom_is_stable(&probe) != expected[raw])
            classification_exact = false;
    }
    CHECK(classification_exact,
          "term stability classifies every grounded kind exactly");

    Atom *stable = atom_expr2(
        arena, atom_symbol(arena, "StableSummary"), atom_int(arena, 37));
    StateCell state_cell = {0};
    Atom *unstable = atom_expr2(
        arena, atom_symbol(arena, "UnstableSummary"),
        atom_state(arena, &state_cell));
    Atom *empty = atom_expr(arena, NULL, 0u);
    Atom *incomplete = atom_expr_builder_begin(arena, 2u);
    incomplete->expr.elems[0] = atom_symbol(arena, "IncompleteSummary");
    incomplete->expr.elems[1] = NULL;
    incomplete = atom_expr_builder_finish(arena, incomplete);
    Atom *contextual = atom_expr2(
        arena, atom_symbol_id(arena, g_builtin_syms.native_handle),
        atom_string(arena, "resource"));
    bool propagation_exact = stable && unstable && empty && incomplete &&
        contextual &&
        (stable->flags & ATOM_FLAG_TERM_STABLE) != 0u &&
        term_universe_atom_is_stable(stable) &&
        (unstable->flags & ATOM_FLAG_TERM_STABLE) == 0u &&
        !term_universe_atom_is_stable(unstable) &&
        (empty->flags & ATOM_FLAG_TERM_STABLE) != 0u &&
        term_universe_atom_is_stable(empty) &&
        (incomplete->flags & ATOM_FLAG_TERM_STABLE) == 0u &&
        !term_universe_atom_is_stable(incomplete) &&
        (contextual->flags & ATOM_FLAG_TERM_STABLE) != 0u &&
        (contextual->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) == 0u &&
        term_universe_atom_is_stable(contextual);
    CHECK(propagation_exact,
          "term stability composes and unstable children poison parents");
}

static void test_internal_tag_structural_summary(Arena *arena) {
    Atom *plain = atom_expr3(
        arena, atom_symbol(arena, "PlainStructure"),
        atom_int(arena, 3), atom_symbol(arena, "leaf"));
    Atom *tag = atom_internal_tag(
        arena, CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
    Atom *tagged = atom_expr2(
        arena, atom_symbol(arena, "TaggedStructure"), tag);
    CHECK(plain && !atom_structural_may_have_internal_tag(plain),
          "constructor facts prove an ordinary expression has no internal tag");
    CHECK(tagged && atom_structural_may_have_internal_tag(tagged),
          "constructor facts propagate an internal tag through expressions");

    Atom unknown = plain ? *plain : (Atom){0};
    unknown.structural_facts = 0u;
    CHECK(atom_structural_may_have_internal_tag(&unknown),
          "missing structural facts conservatively retain the exact walk");
}

static Atom *published_decision_int(Arena *arena, int64_t value) {
    Atom *local = atom_int(arena, value);
    return arena && arena->hashcons
        ? hashcons_get(arena->hashcons, local) : local;
}

static Atom *published_decision_geometry(
        Arena *arena, unsigned geometry, bool mismatch) {
    Atom *value = atom_symbol(
        arena, mismatch ? "published-other" : "published-leaf");
    if (!value)
        return NULL;
    switch (geometry) {
    case 0u:
        for (unsigned depth = 0u; depth < 8u; depth++)
            value = atom_expr2(
                arena, atom_symbol(arena, "PublishedUnary"), value);
        return value;
    case 1u: {
        Atom *left_branch = atom_expr3(
            arena, atom_symbol(arena, "PublishedBranch"),
            published_decision_int(arena, 1), value);
        Atom *right_branch = atom_expr3(
            arena, atom_symbol(arena, "PublishedBranch"),
            published_decision_int(arena, 2),
            atom_symbol(arena, "published-anchor"));
        return atom_expr3(
            arena, atom_symbol(arena, "PublishedBalanced"),
            left_branch, right_branch);
    }
    case 2u:
        for (unsigned depth = 0u; depth < 6u; depth++)
            value = atom_expr3(
                arena, atom_symbol(arena, "PublishedLeftSpine"),
                value, published_decision_int(arena, (int64_t)depth));
        return value;
    case 3u:
        for (unsigned depth = 0u; depth < 6u; depth++)
            value = atom_expr3(
                arena, atom_symbol(arena, "PublishedRightSpine"),
                published_decision_int(arena, (int64_t)depth), value);
        return value;
    case 4u:
        for (unsigned depth = 0u; depth < 5u; depth++) {
            Atom *items[4] = {
                atom_symbol(arena, "PublishedTernary"),
                published_decision_int(arena, (int64_t)depth),
                value,
                atom_symbol(arena, (depth & 1u) ? "odd" : "even"),
            };
            value = atom_expr(arena, items, 4u);
        }
        return value;
    default:
        return NULL;
    }
}

static void test_epoch_identity_and_publication(Arena *ordinary_arena) {
    HashConsTable hashcons;
    hashcons_init(&hashcons);
    Arena shared_arena;
    arena_init(&shared_arena);
    arena_set_hashcons(&shared_arena, &hashcons);
    HashConsTable peer_hashcons;
    hashcons_init(&peer_hashcons);
    Arena peer_arena;
    arena_init(&peer_arena);
    arena_set_hashcons(&peer_arena, &peer_hashcons);

    Atom *head = atom_symbol(&shared_arena, "IdentityProbe");
    Atom *nan_left = hashcons_get(
        &hashcons, atom_float(&shared_arena, NAN));
    Atom *nan_right = hashcons_get(
        &hashcons, atom_float(&shared_arena, NAN));
    Atom *shared_nan = atom_expr2(&shared_arena, head, nan_left);
    Atom *rebuilt_nan = NULL;
    /* A second term over the same NaN, outside the interning arena. */
    rebuilt_nan = atom_expr2(ordinary_arena, head, nan_right);
    Atom *local_expression = atom_expr2(
        ordinary_arena, atom_symbol(ordinary_arena, "LocalPublication"),
        atom_int(ordinary_arena, 31));
    Atom *declined_publication = hashcons_get(
        &hashcons, local_expression);
    CHECK(local_expression && local_expression->arena_id != 0u &&
              declined_publication == local_expression,
          "hash-cons publication declines arena-local expression children");
    Atom *global_int_41 = hashcons_get(
        &hashcons, atom_int(&shared_arena, 41));
    Atom *global_int_43 = hashcons_get(
        &hashcons, atom_int(&shared_arena, 43));
    Atom *publishable = atom_expr2(
        ordinary_arena, head, global_int_41);
    Atom *published = hashcons_get(&hashcons, publishable);
    CHECK(publishable && published && published != publishable &&
              published->arena_id == 0u,
          "hash-cons publication admits expressions with global children");

    Atom malformed_var = {
        .kind = ATOM_VAR,
        .flags = ATOM_FLAG_HASH_STABLE |
                 ATOM_FLAG_HASHCONS_ELIGIBLE |
                 ATOM_FLAG_ARENA_CLOSED,
        .var_id = test_id(7990u),
        .sym_id = head->sym_id,
        .arena_id = ordinary_arena->identity,
    };
    CHECK(hashcons_get(&hashcons, &malformed_var) == &malformed_var,
          "hash-cons publication rejects a variable without HAS_VARS");

    StateCell malformed_state_cell = {0};
    Atom malformed_state = *atom_int(ordinary_arena, 47);
    malformed_state.ground.gkind = GV_STATE;
    malformed_state.ground.ptr = &malformed_state_cell;
    CHECK(hashcons_get(&hashcons, &malformed_state) == &malformed_state,
          "hash-cons publication rejects stale value flags on a state");

    Atom *stale_hash = atom_expr2(
        ordinary_arena, head, global_int_41);
    (void)atom_hash(stale_hash);
    stale_hash->expr.elems[1] = global_int_43;
    CHECK(hashcons_get(&hashcons, stale_hash) == stale_hash,
          "hash-cons publication rejects a stale structural hash");
    Atom *stale_structural_facts = atom_expr2(
        ordinary_arena, head, global_int_41);
    if (stale_structural_facts) {
        stale_structural_facts->structural_facts |=
            ATOM_STRUCTURAL_HAS_INTERNAL_TAG;
    }
    CHECK(stale_structural_facts &&
              hashcons_get(&hashcons, stale_structural_facts) ==
                  stale_structural_facts,
          "hash-cons publication rejects stale structural facts");
    BindingsBuilder values;
    bool values_ready = bindings_builder_init(&values, NULL);
    uint32_t values_mark = values_ready
        ? bindings_builder_save(&values) : 0u;
    test_runtime_stats_reset_counters();
    bool published_decisions_exact = values_ready;
    unsigned published_geometries_completed = 0u;
    for (unsigned geometry = 0u;
         geometry < 5u && published_decisions_exact; geometry++) {
        Atom *left = published_decision_geometry(
            &shared_arena, geometry, false);
        Atom *equal = published_decision_geometry(
            &peer_arena, geometry, false);
        Atom *unequal = published_decision_geometry(
            &peer_arena, geometry, true);
        bool nodes_certified = left && equal && unequal &&
            left != equal && left->arena_id == 0u &&
            equal->arena_id == 0u && unequal->arena_id == 0u;
        bool equal_match = nodes_certified &&
            match_atoms_epoch_view_builder(
                left, 3u, 0u, equal, &values, &shared_arena, 5u) &&
            bindings_builder_save(&values) == values_mark;
        bool unequal_rejected = equal_match &&
            !match_atoms_epoch_view_builder(
                left, 3u, 0u, unequal, &values, &shared_arena, 5u) &&
            bindings_builder_save(&values) == values_mark;
        published_decisions_exact = unequal_rejected;
        if (!published_decisions_exact) {
            fprintf(stderr,
                    "published geometry %u failed: certified=%u equal=%u unequal-rejected=%u arenas=%u/%u/%u\n",
                    geometry, nodes_certified ? 1u : 0u,
                    equal_match ? 1u : 0u, unequal_rejected ? 1u : 0u,
                    left ? left->arena_id : UINT32_MAX,
                    equal ? equal->arena_id : UINT32_MAX,
                    unequal ? unequal->arena_id : UINT32_MAX);
            if (left && left->kind == ATOM_EXPR) {
                for (CettaExprIndex index = 0u;
                     index < left->expr.len; index++) {
                    Atom *child = left->expr.elems[index];
                    fprintf(stderr,
                            "  left child %llu: arena=%u flags=0x%x kind=%d\n",
                            (unsigned long long)index,
                            child ? child->arena_id : UINT32_MAX,
                            child ? child->flags : 0u,
                            child ? (int)child->kind : -1);
                    if (child && child->kind == ATOM_EXPR) {
                        for (CettaExprIndex nested = 0u;
                             nested < child->expr.len; nested++) {
                            Atom *grandchild = child->expr.elems[nested];
                            fprintf(stderr,
                                    "    grandchild %llu: arena=%u flags=0x%x kind=%d\n",
                                    (unsigned long long)nested,
                                    grandchild ? grandchild->arena_id : UINT32_MAX,
                                    grandchild ? grandchild->flags : 0u,
                                    grandchild ? (int)grandchild->kind : -1);
                        }
                    }
                }
            }
        }
        if (published_decisions_exact)
            published_geometries_completed++;
    }
    uint64_t published_attempts = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_ATTEMPT);
    uint64_t published_equal = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_EQUAL);
    uint64_t published_unequal = test_runtime_stats_counter(
        CETTA_RUNTIME_COUNTER_MATCH_CLOSED_EXPRESSION_DECISION_UNEQUAL);
    if (!published_decisions_exact ||
        published_attempts != published_equal + published_unequal ||
        published_equal == 0u || published_unequal == 0u) {
        fprintf(stderr,
                "published decision receipt: geometries=%u attempts=%llu equal=%llu unequal=%llu\n",
                published_geometries_completed,
                (unsigned long long)published_attempts,
                (unsigned long long)published_equal,
                (unsigned long long)published_unequal);
    }
    /* The five explicit equal/unequal checks above establish the observable
       result.  Receipts are diagnostic: every recorded decision must have one
       outcome, and this mixed workload must exercise both outcome classes,
       without prescribing an implementation's internal visit count. */
    CHECK(published_decisions_exact &&
              published_attempts == published_equal + published_unequal &&
              published_equal > 0u && published_unequal > 0u,
          "published immutable DAGs preserve closed-expression decisions and receipt partitioning");
    bool shared_nan_matches = values_ready && shared_nan &&
        shared_nan->arena_id == 0u &&
        (shared_nan->flags & ATOM_FLAG_HASHCONS_ELIGIBLE) != 0u &&
        !atom_has_vars(shared_nan) &&
        match_atoms_epoch_view_builder(
            shared_nan, 11u, 0u, shared_nan,
            &values, &shared_arena, 13u) &&
        bindings_builder_save(&values) == values_mark;
    /* Floats intern by their bits, so the two NaN literals are one atom.
     * Whether a NaN equals itself is its lane's rule, and with no lane here
     * a term is itself; the epoch view settles a second term over the same
     * NaN exactly as equality does. */
    bool nan_interned = nan_left && nan_left == nan_right;
    bool rebuilt_nan_agrees = values_ready && rebuilt_nan &&
        rebuilt_nan != shared_nan &&
        match_atoms_epoch_view_builder(
            shared_nan, 11u, 0u, rebuilt_nan,
            &values, &shared_arena, 13u) ==
            atom_eq(shared_nan, rebuilt_nan) &&
        bindings_builder_save(&values) == values_mark;
    CHECK(shared_nan_matches,
          "global identity settles a shared variable-free NaN term");
    CHECK(nan_interned,
          "floats intern by their bits: two NaN literals are one atom");
    CHECK(rebuilt_nan_agrees,
          "epoch-view matching settles NaN terms as equality does");
    if (values_ready)
        bindings_builder_free(&values);

    Atom *epoch_var = atom_var_with_id(
        &shared_arena, "epoch-identity", test_id(7991u));
    Atom *epoch_term = atom_expr2(&shared_arena, head, epoch_var);
    BindingsBuilder epochs;
    bool epochs_ready = bindings_builder_init(&epochs, NULL);
    bool epochs_match = epochs_ready && epoch_term &&
        match_atoms_epoch_view_builder(
            epoch_term, 17u, 0u, epoch_term,
            &epochs, &shared_arena, 19u);
    BindingValue epoch_value = epochs_match
        ? bindings_lookup_value_id(
              &epochs.current, var_epoch_id(epoch_var->var_id, 17u))
        : binding_value_from_atom(NULL);
    CHECK(epochs_match &&
              bindings_has_bound_values(&epochs.current) &&
              epoch_value.skeleton &&
              epoch_value.skeleton->kind == ATOM_VAR &&
              binding_value_variable_id(epoch_value) ==
                  var_epoch_id(epoch_var->var_id, 19u),
          "shared variable-bearing terms preserve distinct activation epochs");
    if (epochs_ready)
        bindings_builder_free(&epochs);

    Atom *cycle = atom_expr2(
        ordinary_arena, atom_symbol(ordinary_arena, "IdentityCycle"),
        atom_symbol(ordinary_arena, "seed"));
    if (cycle)
        cycle->expr.elems[1] = cycle;
    BindingsBuilder cyclic;
    bool cyclic_ready = bindings_builder_init(&cyclic, NULL);
    uint32_t cyclic_mark = cyclic_ready
        ? bindings_builder_save(&cyclic) : 0u;
    CHECK(cyclic_ready && cycle && cycle->arena_id != 0u &&
              !match_atoms_epoch_view_builder(
                  cycle, 23u, 0u, cycle,
                  &cyclic, ordinary_arena, 29u) &&
              bindings_builder_save(&cyclic) == cyclic_mark,
          "epoch-view matching preserves fail-closed cyclic rejection");
    if (cyclic_ready)
        bindings_builder_free(&cyclic);

    arena_free(&shared_arena);
    hashcons_free(&hashcons);
    arena_free(&peer_arena);
    hashcons_free(&peer_hashcons);
}

static void test_incremental_occurs_large_frontier(Arena *arena) {
    enum { frontier_length = 512u };
    Atom *variables[frontier_length + 1u];
    Atom *node_head = atom_symbol(arena, "OccursFrontierNode");
    bool ready = node_head != NULL;
    for (uint32_t index = 0u;
         ready && index <= frontier_length; index++) {
        variables[index] = atom_var_with_id(
            arena, "occurs-frontier", test_id(8000u + index));
        ready = variables[index] != NULL;
    }

    BindingsBuilder builder;
    bool builder_ready = ready &&
        bindings_builder_init(&builder, NULL);
    ready = builder_ready;
    for (uint32_t index = 0u;
         ready && index < frontier_length; index++) {
        Atom *next = atom_expr2(
            arena, node_head, variables[index + 1u]);
        ready = next && bindings_builder_add_var_fresh(
            &builder, variables[index], next);
    }
    size_t frontier_binding_count = 0u;
    CHECK(ready && bindings_current_binding_count_test(
                       &builder.current, &frontier_binding_count) &&
              frontier_binding_count == frontier_length &&
              !bindings_has_loop(&builder.current),
          "large incremental occurs frontier remains acyclic");

    uint32_t cycle_mark = ready
        ? bindings_builder_save(&builder) : 0u;
    bool closing_refused = ready &&
        !bindings_builder_add_var_fresh(
            &builder, variables[frontier_length], variables[0]) &&
        !bindings_has_loop(&builder.current);
    CHECK(closing_refused,
          "large incremental occurs frontier refuses its closing edge");
    if (ready) {
        bindings_builder_rollback(&builder, cycle_mark);
        frontier_binding_count = 0u;
        CHECK(bindings_current_binding_count_test(
                  &builder.current, &frontier_binding_count) &&
                  frontier_binding_count == frontier_length &&
                  !bindings_has_loop(&builder.current),
              "large occurs-check rollback restores the acyclic frontier");
    }
    if (builder_ready)
        bindings_builder_free(&builder);
}

static void test_single_variable_support_summary(Arena *arena) {
    Atom *head = atom_symbol(arena, "SupportSummary");
    Atom *left = atom_var_with_id(
        arena, "support-left", test_id(8700u));
    Atom *right = atom_var_with_id(
        arena, "support-right", test_id(8701u));
    Atom *singleton = atom_expr3(
        arena, head, left,
        atom_expr3(arena, head, left, left));
    Atom *multiple = atom_expr3(arena, head, left, right);
    Atom *closed = atom_expr2(arena, head, atom_int(arena, 87));
    CHECK(singleton && atom_has_vars(singleton) &&
              atom_single_variable_id(singleton) == left->var_id,
          "nested repeated support records its exact single variable");
    CHECK(multiple && atom_has_vars(multiple) &&
              atom_single_variable_id(multiple) == VAR_ID_NONE,
          "multi-variable support declines the singleton fast path");
    CHECK(closed && !atom_has_vars(closed) &&
              atom_single_variable_id(closed) == VAR_ID_NONE,
          "closed support remains distinct from an open multi-variable term");

    Atom *draft = atom_expr_builder_begin(arena, 2u);
    if (draft) {
        draft->expr.elems[0] = head;
        draft->expr.elems[1] = left;
        draft = atom_expr_builder_finish(arena, draft);
    }
    CHECK(draft && atom_single_variable_id(draft) == left->var_id,
          "expression builders derive the same singleton support summary");
}

static void test_arena_symbol_cache_is_bounded(void) {
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_EVAL);

    SymbolId first_id = symbol_intern_cstr(
        g_symbols, "arena-symbol-cache-first");
    Atom *first = atom_symbol_id(&arena, first_id);
    size_t cache_bytes = arena.symbol_cache_bytes;
    Atom *first_again = atom_symbol_id(&arena, first_id);
    CHECK(first && first_again == first && cache_bytes > 0u,
          "arena symbol cache reuses an immediate exact symbol lookup");

    bool exact_after_churn = true;
    for (uint32_t i = 0u; i < 256u; i++) {
        char name[64];
        int written = snprintf(
            name, sizeof(name), "arena-symbol-cache-churn-%u", i);
        SymbolId id = written > 0 && (size_t)written < sizeof(name)
            ? symbol_intern_cstr(g_symbols, name) : SYMBOL_ID_NONE;
        Atom *atom = id != SYMBOL_ID_NONE
            ? atom_symbol_id(&arena, id) : NULL;
        if (!atom || atom->kind != ATOM_SYMBOL || atom->sym_id != id) {
            exact_after_churn = false;
            break;
        }
    }
    Atom *first_after_churn = atom_symbol_id(&arena, first_id);
    CHECK(exact_after_churn && first_after_churn &&
              first_after_churn->kind == ATOM_SYMBOL &&
              first_after_churn->sym_id == first_id &&
              arena.symbol_cache_bytes == cache_bytes,
          "arena symbol cache remains bounded and collisions are exact misses");

    arena_free(&arena);
}

typedef struct {
    Arena *destination;
} LogicalTransportTestContext;

static Atom *logical_transport_test_atom(void *raw_context, Atom *source) {
    LogicalTransportTestContext *context = raw_context;
    return context && context->destination && source
        ? atom_deep_copy(context->destination, source)
        : NULL;
}

static void test_logical_binding_transport(void) {
    Arena source_arena;
    Arena destination_arena;
    arena_init(&source_arena);
    arena_init(&destination_arena);

    VarId first_id = test_id(7000u);
    VarId constraint_left_id = test_id(7001u);
    VarId constraint_right_id = test_id(7002u);
    Atom *value = atom_expr2(
        &source_arena, atom_symbol(&source_arena, "transported"),
        atom_int(&source_arena, 41));
    Atom *constraint_left = atom_expr2(
        &source_arena, atom_symbol(&source_arena, "left"),
        atom_var_with_id(&source_arena, "transport-left",
                         constraint_left_id));
    Atom *constraint_right = atom_expr2(
        &source_arena, atom_symbol(&source_arena, "right"),
        atom_var_with_id(&source_arena, "transport-right",
                         constraint_right_id));

    Bindings source;
    bindings_init(&source);
    bool source_ready =
        bindings_add_id(&source, first_id, SYMBOL_ID_NONE, value) &&
        bindings_add_constraint(
            &source, constraint_left, constraint_right);
    LogicalTransportTestContext context = {
        .destination = &destination_arena,
    };
    Bindings transported;
    bool transported_ready = source_ready &&
        bindings_transport_logical(
            &transported, &source, logical_transport_test_atom, &context);
    CHECK(transported_ready && transported.len == source.len &&
              transported.eq_len == source.eq_len &&
              bindings_entry_at(&transported, 0)->var_id == first_id &&
              bindings_entry_at(&transported, 0)->value.skeleton !=
                  bindings_entry_at(&source, 0)->value.skeleton &&
              arena_owns_ptr(
                  &destination_arena,
                  bindings_entry_at(&transported, 0)->value.skeleton) &&
              arena_owns_ptr(
                  &destination_arena, transported.constraints[0].lhs.skeleton) &&
              arena_owns_ptr(
                  &destination_arena, transported.constraints[0].rhs.skeleton) &&
              atom_eq(bindings_entry_at(&transported, 0)->value.skeleton,
                      bindings_entry_at(&source, 0)->value.skeleton) &&
              atom_eq(transported.constraints[0].lhs.skeleton,
                      source.constraints[0].lhs.skeleton) &&
              atom_eq(transported.constraints[0].rhs.skeleton,
                      source.constraints[0].rhs.skeleton),
          "logical transport preserves ordered bindings and constraints in a new owner");

    const uint32_t frame_epoch = 733u;
    VarId frame_source_id = UINT64_C(7100);
    VarId framed_id = var_epoch_id(frame_source_id, frame_epoch);
    Atom *frame_variable = atom_var_with_id(
        &source_arena, "transport-frame-variable", frame_source_id);
    VarId frame_ids[] = {frame_source_id};
    Atom *frame_variables[] = {frame_variable};
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder frame_builder;
    bool frame_builder_initialized = bindings_builder_init(
        &frame_builder, &empty);
    bindings_free(&empty);
    bool frame_builder_ready = frame_builder_initialized;
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    frame_builder_ready = frame_builder_ready &&
        bindings_activation_view_prepare(
            &frame, &frame_builder, frame_ids, frame_variables,
            1u, frame_epoch, 0u) &&
        bindings_builder_add_id_fresh(
            &frame_builder, framed_id, SYMBOL_ID_NONE,
            atom_expr2(&source_arena,
                atom_symbol(&source_arena, "framed-transported"),
                atom_int(&source_arena, 73)));
    Bindings transported_frame;
    bool transported_frame_ready = frame_builder_ready &&
        bindings_transport_logical(
            &transported_frame, &frame_builder.current,
            logical_transport_test_atom, &context);
    bool transported_frame_known = false;
    uint32_t transported_frame_entry = UINT32_MAX;
    CHECK(transported_frame_ready &&
              bindings_frame_index_test_lookup(
                  &transported_frame, framed_id,
                  &transported_frame_known,
                  &transported_frame_entry) &&
              transported_frame_known &&
              transported_frame_entry == UINT32_MAX &&
              arena_owns_ptr(
                  &destination_arena,
                  bindings_lookup_value_id(
                      &transported_frame, framed_id).skeleton),
          "logical transport preserves contextual frame identity and its direct coordinate");
    if (transported_frame_ready)
        bindings_free(&transported_frame);
    bindings_activation_view_free(&frame);
    if (frame_builder_initialized)
        bindings_builder_free(&frame_builder);

    Bindings prime_source;
    Bindings refused;
    bindings_init(&prime_source);
    bindings_init(&refused);
    bool prime_ready = bindings_refresh_occurrence_token(&prime_source);
    CHECK(prime_ready &&
              !bindings_transport_logical(
                  &refused, &prime_source,
                  logical_transport_test_atom, &context) &&
              refused.len == 0u && refused.eq_len == 0u,
          "logical transport refuses orthogonal Prime occurrence state");

    bindings_free(&refused);
    bindings_free(&prime_source);
    if (transported_ready)
        bindings_free(&transported);
    bindings_free(&source);
    arena_free(&destination_arena);
    arena_free(&source_arena);
}

static bool ground_test_loop_oracle(Bindings *bindings) {
    uint8_t saved = bindings->cycle_state;
    bindings->cycle_state = 0u; /* Unknown: force the full-graph oracle. */
    bool result = bindings_has_loop(bindings);
    bindings->cycle_state = saved;
    return result;
}

static void test_closed_component_cache(void) {
    HashConsTable hc;
    hashcons_init(&hc);
    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, &hc);
    Atom *node = atom_symbol(&arena, "ClosedComponent");
    Atom *leaf = atom_symbol(&arena, "closed-leaf");
    Atom *x = atom_var_with_id(&arena, "closed-x", test_id(9100));
    Atom *y = atom_var_with_id(&arena, "closed-y", test_id(9101));
    Atom *p = atom_var_with_id(&arena, "closed-p", test_id(9102));
    Atom *q = atom_var_with_id(&arena, "closed-q", test_id(9103));
    Atom *z = atom_var_with_id(&arena, "closed-z", test_id(9104));
    Atom *open = atom_expr3(&arena, node, x, y);
    BindingsBuilder parent;
    bool ready = bindings_builder_init(&parent, NULL);
    for (uint32_t i=0; ready && i<128; i++)
        ready = bindings_builder_add_id_fresh(&parent, test_id(9200+i), SYMBOL_ID_NONE,
            atom_var_with_id(&arena, "unbound-pad", test_id(9600+i)));
    ready = ready && bindings_builder_add_var_fresh(&parent, p, open);
    ready = ready && bindings_builder_add_var_fresh(&parent, z, leaf) &&
        bindings_builder_add_id_fresh(&parent, test_id(9105), SYMBOL_ID_NONE,
            atom_expr3(&arena, node, p, z));
    CHECK(ready && (open->flags & ATOM_FLAG_HASH_STABLE) &&
          !bindings_has_loop(&parent.current) && !ground_test_loop_oracle(&parent.current),
          "open multi-support component remains acyclic without assuming it is closed");
    if (!ready) { bindings_builder_free(&parent); arena_free(&arena); hashcons_free(&hc); return; }
    uint32_t mark = bindings_builder_save(&parent);
    BindingsBuilder sibling;
    bool sibling_ready = bindings_builder_init(&sibling, &parent.current);
    ready = bindings_builder_add_var_fresh(&parent, x, leaf) &&
            bindings_builder_add_var_fresh(&parent, y, leaf) &&
            bindings_builder_add_var_fresh(&parent, q, atom_expr3(&arena, node, p, x));
    CHECK(ready && !bindings_has_loop(&parent.current) && !ground_test_loop_oracle(&parent.current),
          "closed branching component agrees with the independent full graph traversal");
    uint32_t closed_mark = bindings_builder_save(&parent);
    bool repeated = ready;
    for (uint32_t i=0; repeated && i<64; i++)
        repeated = bindings_builder_add_id_fresh(&parent, test_id(9900+i), SYMBOL_ID_NONE,
            atom_expr3(&arena, node, p, y));
    CHECK(repeated && !bindings_has_loop(&parent.current) && !ground_test_loop_oracle(&parent.current),
          "repeated references to a closed component preserve acyclicity");
    bindings_builder_rollback(&parent, closed_mark);
    CHECK(!bindings_has_loop(&parent.current) &&
          !ground_test_loop_oracle(&parent.current) &&
          bindings_lookup_value_id(&parent.current, x->var_id).skeleton == leaf &&
          bindings_lookup_value_id(&parent.current, y->var_id).skeleton == leaf,
          "rollback of unrelated suffix preserves the closed component and its bindings");
    bool reused_suffix = true;
    for (uint32_t round = 0u; reused_suffix && round < 64u; round++) {
        uint32_t suffix_mark = bindings_builder_save(&parent);
        for (uint32_t i = 0u; reused_suffix && i < 17u; i++)
            reused_suffix = bindings_builder_add_id_fresh(
                &parent, test_id(12000u + round * 17u + i), SYMBOL_ID_NONE,
                atom_expr3(&arena, node, p, y));
        reused_suffix = reused_suffix && !bindings_has_loop(&parent.current) &&
            !ground_test_loop_oracle(&parent.current);
        bindings_builder_rollback(&parent, suffix_mark);
    }
    CHECK(reused_suffix && !ground_test_loop_oracle(&parent.current),
          "repeated distinct suffixes preserve closedness through index-cluster relocation");
    bool sibling_refused = sibling_ready && !bindings_builder_add_var_fresh(&sibling, x, p);
    CHECK(sibling_refused && !bindings_has_loop(&sibling.current) && !ground_test_loop_oracle(&sibling.current) &&
          !bindings_has_loop(&parent.current),
          "closedness established in one branch does not admit a sibling's closing edge");
    bindings_builder_rollback(&parent, mark);
    CHECK(!bindings_has_loop(&parent.current) && !ground_test_loop_oracle(&parent.current),
          "rollback restores the original open acyclic component");
    bool after_rollback_refused = !bindings_builder_add_var_fresh(&parent, x, p);
    CHECK(after_rollback_refused && !bindings_has_loop(&parent.current) && !ground_test_loop_oracle(&parent.current),
          "rollback invalidates closedness, and the old open frontier's closing edge is refused");
    if (sibling_ready) bindings_builder_free(&sibling);
    bindings_builder_free(&parent);

    Arena scratch;
    arena_init(&scratch);
    Atom *mutable = atom_expr_builder_begin(&scratch, 3u);
    mutable->expr.elems[0] = node;
    mutable->expr.elems[1] = x;
    mutable->expr.elems[2] = y;
    mutable = atom_expr_builder_finish(&scratch, mutable);
    /* This raw ABI specimen deliberately withholds the immutability fact. */
    mutable->flags &= ~ATOM_FLAG_HASH_STABLE;
    ready = bindings_builder_init(&parent, NULL);
    for (uint32_t i=0; ready && i<128; i++)
        ready = bindings_builder_add_id_fresh(&parent, test_id(9200+i), SYMBOL_ID_NONE,
            atom_var_with_id(&arena, "unbound-pad", test_id(9600+i)));
    ready = ready && bindings_builder_add_var_fresh(&parent, x, leaf) &&
        bindings_builder_add_var_fresh(&parent, y, leaf) &&
        bindings_builder_add_var_fresh(&parent, p, mutable) &&
        bindings_builder_add_id_fresh(&parent, test_id(9106), SYMBOL_ID_NONE,
            atom_expr3(&arena, node, p, x));
    CHECK(ready && !(mutable->flags & ATOM_FLAG_HASH_STABLE) &&
          !bindings_has_loop(&parent.current) && !ground_test_loop_oracle(&parent.current),
          "multi-support data without a stability fact retains exact traversal");
    mutable->expr.elems[2] = q;
    mutable = atom_expr_builder_finish(&scratch, mutable);
    mutable->flags &= ~ATOM_FLAG_HASH_STABLE;
    bool changed_cycle_refused = ready && mutable &&
        !bindings_builder_add_var_fresh(&parent, q, atom_expr3(&arena, node, p, x));
    CHECK(changed_cycle_refused && !bindings_has_loop(&parent.current) &&
              !ground_test_loop_oracle(&parent.current),
          "changed uncertified component is traversed before accepting a new edge, and the edge is refused");
    bindings_builder_free(&parent);
    arena_free(&scratch);
    arena_free(&arena);
    hashcons_free(&hc);
}

static void test_unframed_context_import(Arena *arena) {
    const uint32_t epoch = 1701u;
    Atom *left = atom_var_with_id(
        arena, "context-import-left", test_id(14000u));
    Atom *right = atom_var_with_id(
        arena, "context-import-right", test_id(14001u));
    Atom *leaf = atom_int(arena, 1701);
    Atom *constraint_left_variable = atom_var_with_id(
        arena, "context-import-constraint-left", test_id(14002u));
    Atom *constraint_right_variable = atom_var_with_id(
        arena, "context-import-constraint-right", test_id(14003u));
    Atom *constraint_left = atom_expr2(
        arena, atom_symbol(arena, "ContextImportLeft"),
        constraint_left_variable);
    Atom *constraint_right = atom_expr2(
        arena, atom_symbol(arena, "ContextImportRight"),
        constraint_right_variable);
    Bindings imported;
    bindings_init(&imported);
    bool ready = left && right && leaf &&
        constraint_left && constraint_right &&
        bindings_add_var(&imported, left, right) &&
        bindings_add_var(&imported, right, leaf) &&
        bindings_add_constraint(
            &imported, constraint_left, constraint_right);
    size_t assignment_count = 0u;
    BindingValue left_value = binding_value_from_atom(NULL);
    BindingValue right_value = binding_value_from_atom(NULL);
    bool left_known = false;
    bool right_known = false;
    uint32_t left_entry = 0u;
    uint32_t right_entry = 0u;
    CettaVarMap inventory = {0};
    VarId contextual_left = var_epoch_id(1u, epoch);
    VarId contextual_right = var_epoch_id(2u, epoch);
    bool imported_ok = ready &&
        bindings_contextualize_unframed(&imported, arena, &inventory, epoch) &&
        bindings_current_binding_count(
            &imported, &assignment_count) &&
        bindings_frame_index_test_lookup(
            &imported, contextual_left, &left_known, &left_entry) &&
        bindings_frame_index_test_lookup(
            &imported, contextual_right, &right_known, &right_entry);
    if (imported_ok) {
        left_value = bindings_lookup_value_id(
            &imported, contextual_left);
        right_value = bindings_lookup_value_id(
            &imported, contextual_right);
    }
    CHECK(imported_ok && imported.len == 0u &&
              assignment_count == 2u &&
              left_known && left_entry == UINT32_MAX &&
              right_known && right_entry == UINT32_MAX &&
              binding_value_variable_id(left_value) == contextual_right &&
              right_value.skeleton == leaf &&
              !bindings_lookup_value_id(
                  &imported, left->var_id).skeleton,
          "context import replaces unframed rows with authoritative frame slots");
    Atom *resolved = imported_ok
        ? bindings_apply_value(
              &imported, arena,
              binding_value_from_context(
                  cetta_var_map_lookup(&inventory, left->var_id), epoch))
        : NULL;
    Atom *observed_constraint_left = imported_ok &&
            imported.eq_len == 1u
        ? bindings_apply_value(
              &imported, arena, imported.constraints[0].lhs)
        : NULL;
    CHECK(resolved == leaf && imported.eq_len == 1u &&
              observed_constraint_left &&
              observed_constraint_left->kind == ATOM_EXPR &&
              observed_constraint_left->expr.len == 2u &&
              observed_constraint_left->expr.elems[1]->kind == ATOM_VAR &&
              observed_constraint_left->expr.elems[1]->var_id ==
                  var_epoch_id(
                      cetta_var_map_lookup(&inventory,
                          constraint_left_variable->var_id)->var_id, epoch),
          "context import preserves aliases and qualifies connected constraints");
    CHECK(inventory.len == 4u &&
              cetta_var_map_lookup(&inventory, left->var_id)->var_id == 1u &&
              cetta_var_map_lookup(&inventory, right->var_id)->var_id == 2u,
          "context import compiles sparse authored identities into dense slots");
    cetta_var_map_free(&inventory);
    bindings_free(&imported);

    Atom *foreign = atom_var_with_id(
        arena, "context-import-foreign", var_epoch_id(1u, epoch + 1u));
    Atom *open = atom_expr3(arena, atom_symbol(arena, "ContextImportOpen"), foreign, right);
    Bindings mixed;
    bindings_init(&mixed);
    VarId foreign_slot = 1u;
    ready = bindings_register_contextual_frame(&mixed, &foreign_slot, 1u, epoch + 1u) &&
        bindings_add_var(&mixed, foreign, atom_int(arena, 71)) &&
        bindings_add_var(&mixed, left, open) && bindings_add_var(&mixed, right, atom_int(arena, 83));
    Atom *query = ready ? cetta_import_frame_syntax(arena,
        atom_expr3(arena, atom_symbol(arena, "ImportedQuery"), left, foreign),
        &inventory, epoch) : NULL;
    bool mixed_ok = query && bindings_contextualize_unframed(&mixed, arena, &inventory, epoch);
    Atom *answer = mixed_ok ? bindings_apply(&mixed, arena, query) : NULL;
    CHECK(answer && answer->kind == ATOM_EXPR && answer->expr.len == 3u &&
              answer->expr.elems[1]->kind == ATOM_EXPR &&
              answer->expr.elems[1]->expr.elems[1]->ground.ival == 71 &&
              answer->expr.elems[1]->expr.elems[2]->ground.ival == 83 &&
              answer->expr.elems[2]->ground.ival == 71 && inventory.len == 2u,
          "root and substitution import preserve existing frames while translating authored aliases");
    CHECK(mixed_ok && query->expr.elems[1]->var_id != query->expr.elems[2]->var_id &&
              query->expr.elems[2]->var_id == foreign->var_id,
          "equal slot numbers in different imported frames cannot capture one another");
    cetta_var_map_free(&inventory);
    bindings_free(&mixed);
}

static void test_dense_term_instantiation(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(source_owners);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&source_owners);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&source_owners);
    Atom *x = atom_var_with_id(arena, "same", var_epoch_id(71u, first));
    Atom *y = atom_var_with_id(arena, "same", var_epoch_id(71u, second));
    Atom *z = atom_var_with_id(arena, "last", UINT64_C(900003));
    Atom *terms[2] = {atom_expr3(arena, x, y, z), atom_expr2(arena, z, x)};
    CHECK(cetta_instantiate_frame_terms(arena, terms, 2u),
          "instantiate related terms in one owned frame");
    Atom *a = terms[0]->expr.elems[0];
    Atom *b = terms[0]->expr.elems[1];
    Atom *c = terms[0]->expr.elems[2];
    CettaFrameIdentity identity = var_epoch_suffix(a->var_id);
    uint32_t extent = 0u;
    CHECK(identity != first && identity != second &&
          cetta_frame_identity_slot_count(identity, &extent) && extent == 3u &&
          var_base_id(a->var_id) == 1u && var_base_id(b->var_id) == 2u &&
          var_base_id(c->var_id) == 3u &&
          var_epoch_suffix(b->var_id) == identity &&
          var_epoch_suffix(c->var_id) == identity,
          "fresh frame has dense slots even for sparse and previously qualified sources");
    CHECK(a->var_id != b->var_id &&
          terms[1]->expr.elems[0]->var_id == c->var_id &&
          terms[1]->expr.elems[1]->var_id == a->var_id,
          "shared variables remain shared without capturing equal slots in distinct source frames");
    Atom *other = cetta_instantiate_frame_syntax(arena, terms[0]);
    CHECK(other && other->expr.elems[0]->var_id != a->var_id &&
          other->expr.elems[0]->var_id != other->expr.elems[1]->var_id,
          "independent instantiation creates independent identities");
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL), "initialize dense instance bindings");
    Atom *eleven = atom_int(arena, 11);
    Atom *thirty = atom_int(arena, 30);
    CHECK(bindings_builder_add_var_fresh(&builder, c, thirty) &&
          bindings_builder_add_var_fresh(&builder, a, eleven) && builder.current.len == 0u &&
          bindings_lookup_value_id(&builder.current, c->var_id).skeleton == thirty &&
          bindings_lookup_value_id(&builder.current, a->var_id).skeleton == eleven,
          "out-of-order writes to instantiated slots have only slot authority");
    uint32_t mark = bindings_builder_save(&builder);
    CHECK(bindings_builder_add_var_fresh(&builder, b, eleven), "write remaining instance slot");
    bindings_builder_rollback(&builder, mark);
    Atom *hole = bindings_builder_new_variable(&builder, arena, identity);
    CHECK(!bindings_lookup_value_id(&builder.current, b->var_id).skeleton &&
          bindings_lookup_value_id(&builder.current, c->var_id).skeleton == thirty &&
          hole && var_base_id(hole->var_id) == 4u,
          "rollback restores slots while new variables stay beyond the shared inventory");
    bindings_builder_free(&builder);
}

static Atom *retain_transport_atom(void *context, Atom *atom) {
    (void)context;
    return atom;
}

static void test_saved_binding_value_lifetime(void) {
    Arena source, captured_arena, survivor;
    arena_init_detached(&source);
    arena_init_detached(&captured_arena);
    arena_init_detached(&survivor);
    Bindings env, later, restored;
    bindings_init(&env);
    bindings_init(&later);
    Atom *x = cetta_instantiate_frame_syntax(&source,
        atom_var_with_id(&source, "saved-x", test_id(9200u)));
    VarId saved_id = x->var_id;
    CHECK(bindings_add_var(&env, x, atom_int(&source, 17)),
          "saved substitution source binds its variable");
    Atom *saved = bindings_capture_value(&captured_arena, &env);
    CHECK(saved && saved->kind == ATOM_GROUNDED &&
              saved->ground.gkind == GV_BINDINGS &&
              atom_has_identity_grounded(saved),
          "saved substitutions are owned contextual values");
    CHECK(bindings_add_var(&later, x, atom_int(&source, 23)) &&
              bindings_apply(&later, &source, saved) == saved,
          "substitution cannot rewrite a saved binding domain");
    Atom *independent = bindings_capture_value(&survivor, &env);
    Atom *different = bindings_capture_value(&survivor, &later);
    CHECK(independent && atom_eq(saved, independent) &&
              different && !atom_eq(saved, different),
          "saved substitution equality compares environments, not allocation");
    Atom *copy = atom_deep_copy(&survivor, saved);
    CHECK(copy && atom_eq(copy, saved),
          "copying a saved substitution retains its identity");
    bindings_free(&later);
    bindings_free(&env);
    arena_free(&captured_arena);
    arena_free(&source);
    CHECK(bindings_from_atom(copy, &restored) &&
              binding_is_int(&restored, saved_id, 17),
          "saved substitution survives both source and first owner arenas");
    arena_free(&survivor);
    CHECK(binding_is_int(&restored, saved_id, 17),
          "restored version owns syntax after the final capsule arena is released");
    Arena observation;
    arena_init_detached(&observation);
    Atom *root = atom_var_with_id(&observation, "saved-x", saved_id);
    Bindings projected, merged, transported;
    CHECK(bindings_project_reachable(&restored, &root, 1u, &projected),
          "projection retains captured syntax ownership");
    bindings_init(&merged);
    CHECK(bindings_add_id(&merged, test_id(9201u), SYMBOL_ID_NONE,
                          atom_int(&observation, 18)) &&
          bindings_try_merge(&merged, &restored),
          "merging a saved version retains its owners");
    BindingsBuilder branch;
    CHECK(bindings_builder_init(&branch, NULL), "initialize owned-version rollback branch");
    uint32_t mark = bindings_builder_save(&branch);
    CHECK(bindings_builder_try_merge(&branch, &restored), "merge snapshot into speculative branch");
    BindingsBuilder fork;
    CHECK(bindings_builder_clone(&fork, &branch), "fork retains current and checkpoint owners");
    Atom *recaptured = bindings_capture_value(&observation, &restored);
    Bindings recaptured_version;
    CHECK(recaptured && bindings_from_atom(recaptured, &recaptured_version),
          "recapture closes a new image over its own syntax owner");
    CHECK(bindings_transport_logical(&transported, &restored, retain_transport_atom, NULL),
          "identity transport preserves syntax ownership without copying atoms");
    bindings_free(&restored);
    CHECK(binding_is_int(&transported, saved_id, 17) &&
          binding_is_int(&projected, saved_id, 17) &&
          binding_is_int(&merged, saved_id, 17) &&
          binding_is_int(&branch.current, saved_id, 17),
          "projection, merge and branch survive release of the restored source");
    bindings_builder_rollback(&branch, mark);
    CHECK(!bindings_lookup_value_id(&branch.current, saved_id).skeleton &&
          branch.current.owners == NULL &&
          binding_is_int(&fork.current, saved_id, 17),
          "rollback drops speculative ownership without invalidating a sibling version");
    uint32_t marks[] = {mark};
    CHECK(bindings_builder_compact_reachable(&fork, &root, 1u, marks, 1u, NULL, NULL) &&
          binding_is_int(&fork.current, saved_id, 17),
          "checkpoint compaction keeps the owned version's denotation");
    bindings_builder_rollback(&fork, marks[0]);
    CHECK(!bindings_lookup_value_id(&fork.current, saved_id).skeleton &&
          fork.current.owners == NULL,
          "rebased rollback releases the captured owner at its original boundary");
    bindings_builder_commit(&fork);
    bindings_builder_free(&branch);
    bindings_builder_free(&fork);
    bindings_free(&projected);
    bindings_free(&merged);
    bindings_free(&transported);
    arena_free(&observation);
    CHECK(binding_is_int(&recaptured_version, saved_id, 17),
          "recaptured image survives release of every ancestor and capsule arena");
    bindings_free(&recaptured_version);
}

int main(void) {
    CETTA_FRAME_IDENTITY_SCOPE(frame_identity_scope);
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    VarInternTable var_intern;
    var_intern_init(&var_intern);
    g_var_intern = &var_intern;

    Arena arena;
    arena_init(&arena);

    test_dense_term_instantiation(&arena);
    test_arena_retained_owners();
    test_saved_binding_value_lifetime();
    test_sparse_frame_write_context_growth(&arena);

    test_borrowed_root_identity(&arena);
    test_term_stability_summary(&arena);
    test_dense_frame_indexed_suffix(&arena);
    test_generation_checked_frame_handles(&arena);
    test_persistent_frame_index_partition(&arena);
    test_frame_slot_is_value_authority(&arena);
    test_frame_slot_promotion_lifetime();
    test_manufactured_slot_lifecycle();
    test_frame_index_cycle_bridge(&arena);
    test_frame_only_merge_occurs_check(&arena);
    test_frame_schema_branch_lifetime(&arena);
    test_builder_frame_registration_rollback(&arena);
    test_frame_registration_compaction_rebase(&arena);
    test_unframed_context_import(&arena);
    test_frame_registration_undo_promotion();
    test_prepared_frame_schema_lifetime(&arena);
    test_presented_manufacture_frame_grows_until_closed(&arena);
    test_complete_contextual_frame_is_closed(&arena);
    test_lookup_domain_accounting(&arena);
    test_dense_inventory_is_slot_read(&arena);
    test_closed_component_cache();
    test_internal_tag_structural_summary(&arena);
    test_epoch_identity_and_publication(&arena);
    test_single_variable_support_summary(&arena);
    test_incremental_occurs_large_frontier(&arena);
    test_arena_symbol_cache_is_bounded();
    test_logical_binding_transport();
    const char *lookup_index_setting =
        getenv("CETTA_BINDINGS_LOOKUP_INDEX");
    bool lookup_index_expected =
        !(lookup_index_setting && lookup_index_setting[0] == '0');

    Bindings base;
    CHECK(build_bindings(&arena, 64u, &base),
          "build a large environment");
    bool all_present = true;
    for (uint32_t i = 0u; i < 64u; i++) {
        uint32_t ordinal = (i * 37u) & 63u;
        if (!binding_is_int(
                &base, test_id(ordinal), (int64_t)ordinal)) {
            all_present = false;
            break;
        }
    }
    CHECK(all_present,
          "indexed lookup agrees with every authoritative entry");
    CHECK(!bindings_contains_private_variant_slots(&base),
          "ordinary bindings carry no private variant slots");

    SymbolId modern_spelling =
        symbol_intern_cstr(g_symbols, "modern-identity");
    Atom *modern_bound = atom_var_with_id(
        &arena, "modern-identity", test_id(5000u));
    Atom *modern_unbound = atom_var_with_id(
        &arena, "modern-identity", test_id(5001u));
    Bindings modern;
    bindings_init(&modern);
    Atom *modern_result = NULL;
    CHECK(bindings_add_var(
              &modern, modern_bound, atom_int(&arena, 5000)) &&
              (modern_result =
                   bindings_apply(&modern, &arena, modern_unbound)) ==
                  modern_unbound &&
              modern_result->sym_id == modern_spelling,
          "modern bindings never capture a different VarId by spelling");

    Atom *scoped_x = atom_var_with_id(&arena, "scoped-x", test_id(6001u));
    Atom *foreign_x = atom_var_with_id(&arena, "scoped-x", test_id(6002u));
    Atom *encoded = atom_expr3(&arena,
        atom_symbol_id(&arena, g_builtin_syms.bindings),
        atom_expr(&arena, (Atom *[]){atom_expr2(&arena, atom_symbol(&arena, "scoped-x"),
                                     atom_int(&arena, 6000))}, 1u),
        atom_expr(&arena, NULL, 0u));
    Bindings scoped, missing, ambiguous, conflicting;
    CHECK(bindings_from_atom_scoped(encoded, scoped_x, NULL, &scoped) &&
          binding_is_int(&scoped, scoped_x->var_id, 6000) &&
          bindings_apply(&scoped, &arena, foreign_x) == foreign_x,
          "textual restoration resolves once without capturing another identity");
    CHECK(!bindings_from_atom(encoded, &missing),
          "textual restoration without a receiving scope fails");
    CHECK(!bindings_from_atom_scoped(encoded, atom_expr2(&arena, scoped_x, foreign_x),
                                      NULL, &ambiguous),
          "two distinct same-spelling identities make textual restoration ambiguous");
    bindings_init(&conflicting);
    CHECK(bindings_add_var(&conflicting, scoped_x, atom_int(&arena, 9)) &&
          !bindings_try_merge(&conflicting, &scoped) &&
          binding_is_int(&conflicting, scoped_x->var_id, 9),
          "incompatible restoration fails without overriding the receiving binding");
    Bindings received;
    CHECK(bindings_from_atom_scoped(encoded, NULL, &conflicting, &received) &&
          binding_is_int(&received, scoped_x->var_id, 6000),
          "receiving environment supplies an explicit name inventory independent of values");
    bindings_free(&received);
    Atom *cyclic = atom_expr3(&arena, atom_symbol_id(&arena, g_builtin_syms.bindings),
        atom_expr(&arena, (Atom *[]){atom_expr2(&arena, atom_symbol(&arena, "scoped-x"),
                                    atom_expr2(&arena, atom_symbol(&arena, "f"), scoped_x))}, 1u),
        atom_expr(&arena, NULL, 0u));
    CHECK(!bindings_from_atom_scoped(cyclic, scoped_x, NULL, &received),
          "scoped decoding enforces finite-tree occurs checking");
    bindings_free(&scoped);
    bindings_free(&conflicting);

    VarId late_id = test_id(1000u);
    CHECK(bindings_lookup_value_id(&base, late_id).skeleton == NULL,
          "a genuinely absent variable remains absent");
    BindingsBuilder appended;
    bindings_builder_init_owned(&appended, &base);
    CHECK(bindings_builder_add_id_fresh(
              &appended, late_id, SYMBOL_ID_NONE,
              atom_int(&arena, 1000)) &&
              binding_is_int(&appended.current, late_id, 1000),
          "append invalidates a previously cached miss");
    bindings_builder_take(&appended, &base);

    Bindings clone;
    CHECK(bindings_clone(&clone, &base),
          "clone preserves indexed lookup");

    BindingsBuilder prepared;
    bool prepared_initialized =
        bindings_builder_init(&prepared, NULL);
    uint64_t prepared_growth = prepared_initialized
        ? prepared.growth_count : 0u;
    CHECK(prepared_initialized &&
              bindings_builder_prepare_fresh_entries(
                  &prepared, 20u) &&
              prepared.current.cap >= 20u &&
              prepared.trail_cap >= 20u &&
              prepared.current.len == 0u &&
              prepared.trail_len == 0u &&
              prepared.growth_count == prepared_growth,
          "fresh-entry preparation changes capacity but not logical state");
    Binding *prepared_entries = prepared_initialized
        ? prepared.current.entries : NULL;
    BindingsBuilderTrailEntry *prepared_trail = prepared_initialized
        ? prepared.trail : NULL;
    bool prepared_without_growth = prepared_initialized;
    for (uint32_t index = 0u;
         prepared_without_growth && index < 20u; index++) {
        prepared_without_growth = bindings_builder_add_id_fresh(
            &prepared, test_id(3000u + index), SYMBOL_ID_NONE,
            atom_int(&arena, (int64_t)index));
    }
    CHECK(prepared_without_growth &&
              prepared.current.entries == prepared_entries &&
              prepared.trail == prepared_trail &&
              prepared.current.len == 20u &&
              prepared.trail_len == 20u,
          "prepared activation entries avoid geometric storage growth");
    CHECK(!bindings_builder_prepare_fresh_entries(NULL, 1u),
          "fresh-entry preparation rejects a missing builder");
    if (prepared_initialized)
        bindings_builder_free(&prepared);

    BindingsBuilder branch;
    bindings_builder_init_owned(&branch, &clone);
    uint32_t root_mark = bindings_builder_save(&branch);
    VarId branch_a = test_id(2000u);
    VarId branch_b = test_id(2001u);
    bool branch_added =
        bindings_builder_add_id_fresh(
            &branch, branch_a, SYMBOL_ID_NONE, atom_int(&arena, 2000));
    uint64_t growth_after_first = branch.growth_count;
    bool duplicate_no_growth =
        bindings_builder_add_id_fresh(
            &branch, branch_a, SYMBOL_ID_NONE, atom_int(&arena, 2000)) &&
        branch.growth_count == growth_after_first;
    uint32_t branch_mark = bindings_builder_save(&branch);
    branch_added =
        branch_added &&
        bindings_builder_add_id_fresh(
            &branch, branch_b, SYMBOL_ID_NONE, atom_int(&arena, 2001));
    bool branch_visible =
        binding_is_int(&branch.current, branch_a, 2000) &&
        binding_is_int(&branch.current, branch_b, 2001);
    uint64_t rollback_before = branch.rollback_count;
    bindings_builder_rollback(&branch, branch_mark);
    bool inner_rolled_back =
        binding_is_int(&branch.current, branch_a, 2000) &&
        bindings_lookup_value_id(&branch.current, branch_b).skeleton == NULL &&
        branch.rollback_count == rollback_before + 1u;
    bindings_builder_rollback(&branch, root_mark);
    uint64_t rollback_after_restore = branch.rollback_count;
    bindings_builder_rollback(&branch, root_mark);
    bool root_rolled_back =
        bindings_lookup_value_id(&branch.current, branch_a).skeleton == NULL &&
        bindings_lookup_value_id(&branch.current, branch_b).skeleton == NULL &&
        bindings_lookup_value_id(&base, branch_a).skeleton == NULL &&
        bindings_lookup_value_id(&base, branch_b).skeleton == NULL &&
        binding_is_int(&branch.current, late_id, 1000) &&
        branch.current.private_entry_count == 0u &&
        branch.current.private_constraint_count == 0u &&
        branch.growth_count == growth_after_first + 1u &&
        rollback_after_restore == rollback_before + 2u &&
        branch.rollback_count == rollback_after_restore;
    CHECK(branch_added && duplicate_no_growth && branch_visible &&
              inner_rolled_back && root_rolled_back,
          "write and restore revisions distinguish rollback ABA exactly");
    bindings_builder_take(&branch, &clone);

    BindingsBuilder coalesced;
    bool coalesced_ready = bindings_builder_init(&coalesced, NULL);
    uint32_t coalesced_root = coalesced_ready
        ? bindings_builder_save(&coalesced) : 0u;
    bool coalesced_entered = coalesced_ready &&
        bindings_builder_begin_unobserved_write_region(&coalesced);
    VarId coalesced_a = test_id(2100u);
    VarId coalesced_b = test_id(2101u);
    VarId coalesced_c = test_id(2102u);
    VarId coalesced_d = test_id(2103u);
    VarId coalesced_e = test_id(2104u);
    VarId coalesced_f = test_id(2105u);
    bool coalesced_first_segment = coalesced_entered &&
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_a, SYMBOL_ID_NONE,
            atom_int(&arena, 2100)) &&
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_b, SYMBOL_ID_NONE,
            atom_int(&arena, 2101)) &&
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_c, SYMBOL_ID_NONE,
            atom_int(&arena, 2102)) &&
        coalesced.trail_len == coalesced_root + 1u &&
        binding_is_int(&coalesced.current, coalesced_a, 2100) &&
        binding_is_int(&coalesced.current, coalesced_b, 2101) &&
        binding_is_int(&coalesced.current, coalesced_c, 2102);
    CHECK(coalesced_first_segment,
          "one unobserved checkpoint covers three exact binding writes");
    CHECK(!bindings_builder_begin_unobserved_write_region(&coalesced),
          "unobserved checkpoint regions reject ambiguous nesting");

    uint32_t coalesced_middle = bindings_builder_save(&coalesced);
    bool coalesced_second_segment =
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_d, SYMBOL_ID_NONE,
            atom_int(&arena, 2103)) &&
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_e, SYMBOL_ID_NONE,
            atom_int(&arena, 2104)) &&
        coalesced.trail_len == coalesced_middle + 1u;
    CHECK(coalesced_second_segment,
          "an observed save starts one distinct checkpoint segment");
    bindings_builder_rollback(&coalesced, coalesced_middle);
    CHECK(binding_is_int(&coalesced.current, coalesced_a, 2100) &&
              binding_is_int(&coalesced.current, coalesced_b, 2101) &&
              binding_is_int(&coalesced.current, coalesced_c, 2102) &&
              bindings_lookup_value_id(&coalesced.current, coalesced_d).skeleton == NULL &&
              bindings_lookup_value_id(&coalesced.current, coalesced_e).skeleton == NULL,
          "observed middle rollback preserves only the earlier segment");
    CHECK(bindings_builder_add_id_fresh(
              &coalesced, coalesced_f, SYMBOL_ID_NONE,
              atom_int(&arena, 2105)) &&
              coalesced.trail_len == coalesced_middle + 1u,
          "a post-rollback write opens a fresh exact segment");

    BindingsBuilder coalesced_clone;
    bool coalesced_clone_ready =
        bindings_builder_clone(&coalesced_clone, &coalesced);
    Binding *coalesced_shared_entries = coalesced.current.entries;
    CHECK(coalesced_clone_ready &&
              coalesced_clone.current.entries ==
                  coalesced_shared_entries &&
              !coalesced_clone.unobserved_write_region_active &&
              !coalesced_clone.unobserved_write_region_has_checkpoint &&
              coalesced_clone.unobserved_write_region_entry_mark == 0u &&
              binding_is_int(
                  &coalesced_clone.current, coalesced_f, 2105),
          "a fork publishes the current meaning outside the private region");
    BindingsBuilder coalesced_clone2;
    bool coalesced_clone2_ready =
        bindings_builder_clone(&coalesced_clone2, &coalesced);
    CHECK(coalesced_clone2_ready &&
              coalesced_clone2.current.entries == coalesced_shared_entries &&
              coalesced_clone.current.entries == coalesced_shared_entries,
          "a later capture retains the same frozen image");
    bool coalesced_clone_detached = coalesced_clone_ready &&
        bindings_builder_add_id_fresh(
            &coalesced_clone, test_id(2199u), SYMBOL_ID_NONE,
            atom_int(&arena, 2199)) &&
        coalesced_clone.current.entries != coalesced_shared_entries &&
        coalesced_clone.current.shared_entries == coalesced_shared_entries &&
        coalesced.current.entries == coalesced_shared_entries &&
        coalesced_clone2_ready &&
        coalesced_clone2.current.entries == coalesced_shared_entries &&
        binding_is_int(
            &coalesced_clone.current, test_id(2199u), 2199) &&
        bindings_lookup_value_id(&coalesced.current, test_id(2199u)).skeleton == NULL &&
        bindings_lookup_value_id(&coalesced_clone2.current, test_id(2199u)).skeleton == NULL;
    CHECK(coalesced_clone_detached,
          "a write detaches one fork and preserves every shared sibling");
    if (coalesced_clone2_ready)
        bindings_builder_free(&coalesced_clone2);
    if (coalesced_clone_ready)
        bindings_builder_free(&coalesced_clone);

    bindings_builder_end_unobserved_write_region(&coalesced, true);
    bindings_builder_rollback(&coalesced, coalesced_root);
    CHECK(coalesced.trail_len == coalesced_root &&
              bindings_lookup_value_id(&coalesced.current, coalesced_a).skeleton == NULL &&
              bindings_lookup_value_id(&coalesced.current, coalesced_b).skeleton == NULL &&
              bindings_lookup_value_id(&coalesced.current, coalesced_c).skeleton == NULL &&
              bindings_lookup_value_id(&coalesced.current, coalesced_f).skeleton == NULL,
          "the entrance checkpoint restores the complete region exactly");
    bool rejected_region = coalesced_ready &&
        bindings_builder_begin_unobserved_write_region(&coalesced);
    bool rejected_region_writes = rejected_region &&
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_a, SYMBOL_ID_NONE,
            atom_int(&arena, 2110)) &&
        bindings_builder_add_id_fresh(
            &coalesced, coalesced_b, SYMBOL_ID_NONE,
            atom_int(&arena, 2111));
    if (rejected_region)
        bindings_builder_end_unobserved_write_region(&coalesced, false);
    CHECK(rejected_region_writes &&
              !coalesced.unobserved_write_region_active &&
              coalesced.unobserved_write_region_entry_mark == 0u &&
              coalesced.trail_len == coalesced_root &&
              bindings_lookup_value_id(&coalesced.current, coalesced_a).skeleton == NULL &&
              bindings_lookup_value_id(&coalesced.current, coalesced_b).skeleton == NULL,
          "a rejected unobserved region restores its captured entrance before exit");
    if (coalesced_ready)
        bindings_builder_free(&coalesced);

#ifdef CETTA_TEST_HOOKS
    Bindings lazy_index_base;
    CHECK(build_bindings(&arena, 60u, &lazy_index_base) &&
              binding_is_int(&lazy_index_base, test_id(0u), 0),
          "demand-synchronized index fixture starts fully indexed");
    uint32_t lazy_synced_len = UINT32_MAX;
    bool lazy_initially_synced =
        bindings_lookup_index_test_synced_len(
            &lazy_index_base, &lazy_synced_len) &&
        lazy_synced_len == lazy_index_base.len;
    BindingsBuilder lazy_index_branch;
    bindings_builder_init_owned(&lazy_index_branch, &lazy_index_base);
    uint32_t lazy_index_mark = bindings_builder_save(&lazy_index_branch);
    VarId lazy_index_id = test_id(6400u);
    bool lazy_append_lags =
        bindings_builder_add_id_fresh(
            &lazy_index_branch, lazy_index_id, SYMBOL_ID_NONE,
            atom_int(&arena, 6400)) &&
        bindings_lookup_index_test_synced_len(
            &lazy_index_branch.current, &lazy_synced_len) &&
        lazy_synced_len + 1u == lazy_index_branch.current.len;
    bindings_builder_rollback(&lazy_index_branch, lazy_index_mark);
    bool unobserved_rollback_restores =
        bindings_lookup_index_test_synced_len(
            &lazy_index_branch.current, &lazy_synced_len) &&
        lazy_synced_len == lazy_index_branch.current.len &&
        bindings_lookup_value_id(&lazy_index_branch.current, lazy_index_id).skeleton == NULL;
    CHECK(!lookup_index_expected ||
              (lazy_initially_synced && lazy_append_lags &&
               unobserved_rollback_restores),
          "unobserved append and suffix rollback avoid derived index work");

    lazy_index_mark = bindings_builder_save(&lazy_index_branch);
    bool cache_hit_stays_lazy =
        bindings_builder_add_id_fresh(
            &lazy_index_branch, lazy_index_id, SYMBOL_ID_NONE,
            atom_int(&arena, 6400)) &&
        bindings_lookup_index_test_synced_len(
            &lazy_index_branch.current, &lazy_synced_len) &&
        lazy_synced_len + 1u == lazy_index_branch.current.len &&
        binding_is_int(&lazy_index_branch.current, lazy_index_id, 6400) &&
        bindings_lookup_index_test_synced_len(
            &lazy_index_branch.current, &lazy_synced_len) &&
        lazy_synced_len + 1u == lazy_index_branch.current.len;
    bool uncached_lookup_synchronizes =
        binding_is_int(&lazy_index_branch.current, test_id(5u), 5) &&
        binding_is_int(&lazy_index_branch.current, test_id(6u), 6) &&
        binding_is_int(&lazy_index_branch.current, lazy_index_id, 6400) &&
        bindings_lookup_index_test_synced_len(
            &lazy_index_branch.current, &lazy_synced_len) &&
        lazy_synced_len == lazy_index_branch.current.len;
    CHECK(!lookup_index_expected ||
              (cache_hit_stays_lazy && uncached_lookup_synchronizes),
          "one-entry index catch-up waits for demand and preserves its binding");
    bindings_builder_rollback(&lazy_index_branch, lazy_index_mark);
    bindings_builder_free(&lazy_index_branch);

    Bindings shared_index_base;
    CHECK(build_bindings(&arena, 60u, &shared_index_base) &&
              binding_is_int(&shared_index_base, test_id(0u), 0),
          "shared index fixture starts fully indexed");
    BindingsBuilder shared_index_branch;
    bindings_builder_init(&shared_index_branch, &shared_index_base);
    uint32_t shared_index_mark =
        bindings_builder_save(&shared_index_branch);
    bool shared_append_stays_lazy =
        bindings_builder_add_id_fresh(
            &shared_index_branch, lazy_index_id, SYMBOL_ID_NONE,
            atom_int(&arena, 6400)) &&
        bindings_lookup_index_test_synced_len(
            &shared_index_branch.current, &lazy_synced_len) &&
        lazy_synced_len + 1u == shared_index_branch.current.len &&
        binding_is_int(&shared_index_branch.current, test_id(5u), 5) &&
        binding_is_int(&shared_index_branch.current, test_id(6u), 6) &&
        binding_is_int(&shared_index_branch.current, lazy_index_id, 6400) &&
        bindings_lookup_index_test_synced_len(
            &shared_index_base, &lazy_synced_len) &&
        lazy_synced_len == shared_index_base.len &&
        bindings_lookup_value_id(&shared_index_base, lazy_index_id).skeleton == NULL;
    bindings_builder_rollback(
        &shared_index_branch, shared_index_mark);
    CHECK(!lookup_index_expected || shared_append_stays_lazy,
          "demanded catch-up detaches before mutating a shared index");
    bindings_builder_free(&shared_index_branch);
    bindings_free(&shared_index_base);
#endif

    CHECK(sizeof(BindingsBuilderTrailEntry) <= 16u,
          "the hot rollback checkpoint excludes cold Prime payloads");
    BindingsBuilder sparse_effect_branch;
    CHECK(bindings_builder_init(&sparse_effect_branch, NULL),
          "sparse effect trail starts from an empty branch");
    uint32_t sparse_root_mark =
        bindings_builder_save(&sparse_effect_branch);
    bool sparse_effect_fixture =
        bindings_builder_add_id_fresh(
            &sparse_effect_branch, test_id(6500u), SYMBOL_ID_NONE,
            atom_int(&arena, 6500)) &&
        sparse_effect_branch.trail_len == 1u &&
        sparse_effect_branch.prime_trail_len == 0u &&
        !sparse_effect_branch.trail[0].prime_state_present;
    uint32_t sparse_plain_mark =
        bindings_builder_save(&sparse_effect_branch);
    PrimeNeedSnapshot sparse_need;
    PrimeNeedBranchState sparse_branch_state;
    prime_need_snapshot_init(&sparse_need);
    prime_need_branch_state_init(&sparse_branch_state);
    sparse_effect_fixture =
        sparse_effect_fixture &&
        prime_need_snapshot_begin(&sparse_need) &&
        prime_need_branch_state_begin(&arena, &sparse_branch_state);
    bindings_prime_set(
        &sparse_effect_branch.current, &sparse_need, &sparse_branch_state, 0u,
        NULL);
    sparse_effect_fixture =
        sparse_effect_fixture &&
        bindings_builder_add_id_fresh(
            &sparse_effect_branch, test_id(6501u), SYMBOL_ID_NONE,
            atom_int(&arena, 6501)) &&
        sparse_effect_branch.trail_len == 2u &&
        sparse_effect_branch.prime_trail_len == 1u &&
        sparse_effect_branch.trail[1].prime_state_present;
    bindings_prime_set(
        &sparse_effect_branch.current, NULL, NULL, 0u, NULL);
    uint32_t sparse_absent_mark =
        bindings_builder_save(&sparse_effect_branch);
    sparse_effect_fixture =
        sparse_effect_fixture &&
        bindings_builder_add_id_fresh(
            &sparse_effect_branch, test_id(6502u), SYMBOL_ID_NONE,
            atom_int(&arena, 6502)) &&
        sparse_effect_branch.prime_trail_len == 1u &&
        !sparse_effect_branch.trail[2].prime_state_present;

    BindingsBuilder sparse_effect_clone;
    bool sparse_clone_ready = bindings_builder_clone(
        &sparse_effect_clone, &sparse_effect_branch);
    Arena sparse_clone_owner;
    arena_init(&sparse_clone_owner);
    bool sparse_clone_exact = sparse_clone_ready &&
        bindings_builder_promote_atoms_to_arena(
            &sparse_effect_clone, &sparse_clone_owner) &&
        sparse_effect_clone.instance_id !=
            sparse_effect_branch.instance_id &&
        sparse_effect_clone.trail_len ==
            sparse_effect_branch.trail_len &&
        sparse_effect_clone.prime_trail_len ==
            sparse_effect_branch.prime_trail_len &&
        sparse_effect_clone.growth_count ==
            sparse_effect_branch.growth_count &&
        sparse_effect_clone.rollback_count ==
            sparse_effect_branch.rollback_count &&
        sparse_effect_clone.prime_trail[0].prime_need.session_id ==
            sparse_need.session_id &&
        sparse_effect_clone.prime_trail[0].branch_state.owner ==
            &sparse_clone_owner &&
        bindings_logical_atoms_closed_for_arena(
            &sparse_effect_clone.current, &sparse_clone_owner) &&
        binding_is_int(
            &sparse_effect_clone.current, test_id(6502u), 6502);
    if (sparse_clone_exact) {
        bindings_builder_rollback(
            &sparse_effect_clone, sparse_plain_mark);
        sparse_clone_exact =
            bindings_prime_present(&sparse_effect_clone.current) &&
            bindings_need_view(
                &sparse_effect_clone.current)->session_id ==
                    sparse_need.session_id &&
            binding_is_int(
                &sparse_effect_branch.current,
                test_id(6502u), 6502) &&
            !bindings_prime_present(
                &sparse_effect_branch.current);
    }
    if (sparse_clone_exact) {
        bindings_builder_rollback(
            &sparse_effect_clone, sparse_root_mark);
        sparse_clone_exact =
            sparse_effect_clone.current.len == 0u &&
            sparse_effect_branch.current.len == 3u;
    }
    CHECK(sparse_clone_exact,
          "builder clone preserves independent logical and Prime rollback history");
    if (sparse_clone_ready)
        bindings_builder_free(&sparse_effect_clone);
    arena_free(&sparse_clone_owner);

    BindingsBuilder invalid_effect_source = sparse_effect_branch;
    invalid_effect_source.prime_trail_len = 0u;
    BindingsBuilder refused_effect_clone;
    CHECK(!bindings_builder_clone(
              &refused_effect_clone, &invalid_effect_source),
          "builder clone rejects an effect mark outside the retained Prime trail");

    bindings_builder_rollback(
        &sparse_effect_branch, sparse_absent_mark);
    bool sparse_absent_restored =
        !bindings_prime_present(&sparse_effect_branch.current) &&
        sparse_effect_branch.prime_trail_len == 1u;
    bindings_builder_rollback(
        &sparse_effect_branch, sparse_plain_mark);
    bool sparse_present_restored =
        bindings_prime_present(&sparse_effect_branch.current) &&
        bindings_need_view(&sparse_effect_branch.current)->session_id ==
            sparse_need.session_id &&
        bindings_branch_state_view(&sparse_effect_branch.current)->session_id ==
            sparse_branch_state.session_id &&
        sparse_effect_branch.prime_trail_len == 0u;
    bindings_builder_rollback(
        &sparse_effect_branch, sparse_root_mark);
    CHECK(sparse_effect_fixture && sparse_absent_restored &&
              sparse_present_restored &&
              !bindings_prime_present(&sparse_effect_branch.current) &&
              sparse_effect_branch.prime_trail_len == 0u,
          "mixed effect checkpoints restore absent and present states exactly");
    bindings_builder_free(&sparse_effect_branch);

    BindingsBuilder sparse_compact_branch;
    CHECK(bindings_builder_init(&sparse_compact_branch, NULL),
          "sparse effect compaction starts from an empty branch");
    PrimeNeedSnapshot compact_need;
    prime_need_snapshot_init(&compact_need);
    bool sparse_compact_fixture =
        prime_need_snapshot_begin(&compact_need);
    bindings_prime_set(
        &sparse_compact_branch.current, &compact_need, NULL, 0u, NULL);
    Atom *sparse_compact_live = atom_var_with_id(
        &arena, "sparse-compact-live", test_id(6510u));
    sparse_compact_fixture =
        sparse_compact_fixture &&
        bindings_builder_add_var_fresh(
            &sparse_compact_branch, sparse_compact_live,
            atom_int(&arena, 6510));
    bindings_prime_set(
        &sparse_compact_branch.current, NULL, NULL, 0u, NULL);
    uint32_t sparse_compact_mark_a =
        bindings_builder_save(&sparse_compact_branch);
    sparse_compact_fixture =
        sparse_compact_fixture &&
        bindings_builder_add_id_fresh(
            &sparse_compact_branch, test_id(6511u), SYMBOL_ID_NONE,
            atom_int(&arena, 6511));
    PrimeNeedSnapshot compact_need_later;
    prime_need_snapshot_init(&compact_need_later);
    sparse_compact_fixture =
        sparse_compact_fixture &&
        prime_need_snapshot_begin(&compact_need_later);
    bindings_prime_set(
        &sparse_compact_branch.current, &compact_need_later, NULL, 0u,
        NULL);
    uint32_t sparse_compact_mark_b =
        bindings_builder_save(&sparse_compact_branch);
    sparse_compact_fixture =
        sparse_compact_fixture &&
        bindings_builder_add_id_fresh(
            &sparse_compact_branch, test_id(6512u), SYMBOL_ID_NONE,
            atom_int(&arena, 6512));
    uint32_t sparse_compact_marks[2] = {
        sparse_compact_mark_a, sparse_compact_mark_b,
    };
    Atom *sparse_compact_roots[1] = {sparse_compact_live};
    sparse_compact_fixture =
        sparse_compact_fixture &&
        bindings_builder_compact_reachable(
            &sparse_compact_branch,
            sparse_compact_roots, 1u,
            sparse_compact_marks, 2u, NULL, NULL) &&
        sparse_compact_branch.trail_len == 2u &&
        sparse_compact_branch.prime_trail_len == 1u;
    bindings_builder_rollback(
        &sparse_compact_branch, sparse_compact_marks[1]);
    bool sparse_compact_present =
        bindings_prime_present(&sparse_compact_branch.current) &&
        bindings_need_view(&sparse_compact_branch.current)->session_id ==
            compact_need_later.session_id;
    bindings_builder_rollback(
        &sparse_compact_branch, sparse_compact_marks[0]);
    CHECK(sparse_compact_fixture && sparse_compact_present &&
              !bindings_prime_present(&sparse_compact_branch.current) &&
              binding_is_int(
                  &sparse_compact_branch.current,
                  sparse_compact_live->var_id, 6510),
          "compaction remaps mixed sparse effect checkpoints transactionally");
    bindings_builder_free(&sparse_compact_branch);

    BindingsBuilder sparse_commit_branch;
    Bindings sparse_committed;
    bindings_init(&sparse_committed);
    CHECK(bindings_builder_init(&sparse_commit_branch, NULL),
          "sparse effect commit starts from an empty branch");
    PrimeNeedSnapshot committed_need;
    prime_need_snapshot_init(&committed_need);
    bool sparse_commit_fixture =
        prime_need_snapshot_begin(&committed_need);
    bindings_prime_set(
        &sparse_commit_branch.current, &committed_need, NULL, 0u, NULL);
    sparse_commit_fixture =
        sparse_commit_fixture &&
        bindings_builder_add_id_fresh(
            &sparse_commit_branch, test_id(6520u), SYMBOL_ID_NONE,
            atom_int(&arena, 6520)) &&
        sparse_commit_branch.trail_len == 1u &&
        sparse_commit_branch.prime_trail_len == 1u;
    bindings_builder_commit(&sparse_commit_branch);
    CHECK(sparse_commit_fixture &&
              sparse_commit_branch.trail_len == 0u &&
              sparse_commit_branch.prime_trail_len == 0u &&
              bindings_prime_present(&sparse_commit_branch.current) &&
              bindings_need_view(&sparse_commit_branch.current)->session_id ==
                  committed_need.session_id,
          "commit discards both rollback trails without changing current effects");
    bindings_builder_take(&sparse_commit_branch, &sparse_committed);
    CHECK(sparse_commit_branch.trail == NULL &&
              sparse_commit_branch.prime_trail == NULL &&
              bindings_prime_present(&sparse_committed) &&
              binding_is_int(&sparse_committed, test_id(6520u), 6520),
          "take transfers current effects while releasing both rollback trails");
    bindings_free(&sparse_committed);

    BindingsBuilder compacted_branch;
    CHECK(bindings_builder_init(&compacted_branch, NULL),
          "reachable-trail compaction starts from an empty branch");
    Atom *old_live = atom_var_with_id(
        &arena, "old-live", test_id(7000u));
    Atom *old_dead = atom_var_with_id(
        &arena, "old-dead", test_id(7001u));
    Atom *mid_live = atom_var_with_id(
        &arena, "mid-live", test_id(7002u));
    Atom *mid_dead = atom_var_with_id(
        &arena, "mid-dead", test_id(7003u));
    Atom *post_live = atom_var_with_id(
        &arena, "post-live", test_id(7004u));
    Atom *post_dead = atom_var_with_id(
        &arena, "post-dead", test_id(7005u));
    bool compact_fixture =
        bindings_builder_add_var_fresh(
            &compacted_branch, old_live, atom_int(&arena, 11)) &&
        bindings_builder_add_var_fresh(
            &compacted_branch, old_dead, atom_int(&arena, 12));
    uint32_t compact_mark_a =
        bindings_builder_save(&compacted_branch);
    compact_fixture =
        compact_fixture &&
        bindings_builder_add_var_fresh(
            &compacted_branch, mid_live, old_live) &&
        bindings_builder_add_var_fresh(
            &compacted_branch, mid_dead, atom_int(&arena, 22));
    uint32_t compact_mark_b =
        bindings_builder_save(&compacted_branch);
    compact_fixture =
        compact_fixture &&
        bindings_builder_add_var_fresh(
            &compacted_branch, post_live, mid_live) &&
        bindings_builder_add_var_fresh(
            &compacted_branch, post_dead, atom_int(&arena, 32));
    uint32_t invalid_mark =
        bindings_builder_save(&compacted_branch) + 1u;
    uint32_t invalid_entry_mark =
        compacted_branch.current.len + 1u;
    Atom *compact_roots[1] = {post_live};
    CHECK(compact_fixture &&
              !bindings_builder_compact_reachable(
                  &compacted_branch, compact_roots, 1u,
                  &invalid_mark, 1u, NULL, NULL) &&
              invalid_mark ==
                  bindings_builder_save(&compacted_branch) + 1u &&
              compacted_branch.current.len == 6u,
          "invalid compaction marks fail without mutating the branch");
    CHECK(!bindings_builder_compact_reachable_with_entry_marks(
              &compacted_branch, compact_roots, 1u,
              NULL, 0u, &invalid_entry_mark, 1u,
              NULL, NULL) &&
              invalid_entry_mark == 7u &&
              compacted_branch.current.len == 6u,
          "invalid entry-prefix marks fail transactionally");
    uint32_t compact_marks[2] = {
        compact_mark_a, compact_mark_b,
    };
    uint32_t compact_entry_marks[2] = {2u, 4u};
    uint64_t compact_discarded = 0u;
    uint64_t compact_trail_discarded = 0u;
    bool compact_ok = bindings_builder_compact_reachable_with_entry_marks(
        &compacted_branch, compact_roots, 1u,
        compact_marks, 2u, compact_entry_marks, 2u,
        &compact_discarded,
        &compact_trail_discarded);
    CHECK(compact_ok &&
              compacted_branch.current.len == 3u &&
              compacted_branch.trail_len == 2u &&
              compact_marks[0] == 0u &&
              compact_marks[1] == 1u &&
              compact_entry_marks[0] == 1u &&
              compact_entry_marks[1] == 2u &&
              compact_discarded == 3u &&
              compact_trail_discarded == 4u &&
              binding_is_int(
                  &compacted_branch.current,
                  old_live->var_id, 11) &&
              bindings_lookup_value_id(&compacted_branch.current, old_dead->var_id).skeleton == NULL &&
              bindings_lookup_value_id(&compacted_branch.current, post_live->var_id).skeleton == mid_live,
          "compaction retains the transitive live closure and remaps marks");
    bindings_builder_rollback(
        &compacted_branch, compact_marks[1]);
    bool compact_mid_rollback =
        compacted_branch.current.len == 2u &&
        bindings_lookup_value_id(&compacted_branch.current, post_live->var_id).skeleton == NULL &&
        bindings_lookup_value_id(&compacted_branch.current, mid_live->var_id).skeleton == old_live;
    bindings_builder_rollback(
        &compacted_branch, compact_marks[0]);
    CHECK(compact_mid_rollback &&
              compacted_branch.current.len == 1u &&
              binding_is_int(
                  &compacted_branch.current,
                  old_live->var_id, 11) &&
              bindings_lookup_value_id(&compacted_branch.current, mid_live->var_id).skeleton == NULL,
          "compacted nested marks preserve exact rollback states");
    bindings_builder_free(&compacted_branch);

    Atom *projection_root = atom_var_with_id(
        &arena, "projection-root", late_id);
    Atom *projection_roots[1] = {projection_root};
    uint32_t projection_marks[2] = {64u, 65u};
    Bindings projected;
    CHECK(bindings_project_reachable_with_entry_marks(
              &base, projection_roots, 1u,
              projection_marks, 2u, &projected) &&
              projected.len == 1u &&
              projection_marks[0] == 0u &&
              projection_marks[1] == 1u &&
              binding_is_int(&projected, late_id, 1000),
          "deterministic projection remaps activation entry boundaries");
    bindings_free(&projected);
    Atom *stable_projection_root = atom_expr2(
        &arena, atom_symbol(&arena, "StableProjection"), projection_root);
    Atom *stable_projection_roots[1] = {stable_projection_root};
    CHECK((stable_projection_root->flags & ATOM_FLAG_HASH_STABLE) != 0u &&
              bindings_project_reachable(
                  &base, stable_projection_roots, 1u, &projected) &&
              projected.len == 1u &&
              binding_is_int(&projected, late_id, 1000),
          "hash-stable projection uses the acyclic variable collector");
    bindings_free(&projected);

    Atom *stable_dag_head = atom_symbol(&arena, "StableProjectionDag");
    Atom *stable_dag_root = projection_root;
    for (uint32_t depth = 0u; depth < 64u; depth++)
        stable_dag_root = atom_expr3(
            &arena, stable_dag_head, stable_dag_root, stable_dag_root);
    Atom *stable_dag_roots[1] = {stable_dag_root};
    CHECK(stable_dag_root &&
              (stable_dag_root->flags & ATOM_FLAG_HASH_STABLE) != 0u &&
              bindings_project_reachable(
                  &base, stable_dag_roots, 1u, &projected) &&
              projected.len == 1u &&
              binding_is_int(&projected, late_id, 1000),
          "hash-stable projection visits a shared variable DAG linearly");
    bindings_free(&projected);

    /* Many roots share a non-singleton support DAG. Reusing a traversal
     * must retain the union contributed by earlier roots and both variables. */
    Atom *multi_support = atom_expr3(
        &arena, stable_dag_head, projection_root,
        atom_var_with_id(&arena, "other-live", test_id(0u)));
    Atom *overlapping_roots[64];
    for (size_t index = 0u; index < 64u; index++) {
        multi_support = atom_expr3(
            &arena, stable_dag_head, multi_support, multi_support);
        overlapping_roots[index] = multi_support;
    }
    CHECK(bindings_project_reachable(
              &base, overlapping_roots, 64u, &projected) &&
              projected.len == 2u &&
              binding_is_int(&projected, late_id, 1000) &&
              binding_is_int(&projected, test_id(0u), 0),
          "overlapping projection roots retain their complete support union");
    bindings_free(&projected);

    Bindings unmarked_projection_source;
    bindings_init(&unmarked_projection_source);
    bool unmarked_projection_fixture = build_bindings(
        &arena, 64u, &unmarked_projection_source);
#ifdef CETTA_TEST_HOOKS
    bindings_lookup_index_test_clear(&unmarked_projection_source);
#endif
    Atom *unmarked_projection_root = atom_var_with_id(
        &arena, "unmarked-projection-root", test_id(63u));
    Atom *unmarked_projection_roots[1] = {
        unmarked_projection_root,
    };
    CHECK(unmarked_projection_fixture &&
              unmarked_projection_source.lookup_index == NULL &&
              bindings_project_reachable_with_entry_marks(
                  &unmarked_projection_source,
                  unmarked_projection_roots, 1u,
                  NULL, 0u, &projected) &&
              projected.len == 1u &&
              binding_is_int(&projected, test_id(63u), 63) &&
              (!lookup_index_expected ||
               unmarked_projection_source.lookup_index != NULL),
          "zero entry marks preserve the sparse indexed projection path");
    bindings_free(&projected);
    bindings_free(&unmarked_projection_source);

    Bindings marked_projection_source;
    bindings_init(&marked_projection_source);
    bool marked_projection_fixture = build_bindings(
        &arena, 64u, &marked_projection_source);
#ifdef CETTA_TEST_HOOKS
    bindings_lookup_index_test_clear(&marked_projection_source);
#endif
    uint32_t marked_projection_boundary = 64u;
    CHECK(marked_projection_fixture &&
              marked_projection_source.lookup_index == NULL &&
              bindings_project_reachable_with_entry_marks(
                  &marked_projection_source,
                  unmarked_projection_roots, 1u,
                  &marked_projection_boundary, 1u, &projected) &&
              projected.len == 1u &&
              marked_projection_boundary == 1u &&
              marked_projection_source.lookup_index == NULL &&
              binding_is_int(&projected, test_id(63u), 63),
          "nonempty entry marks retain exact dense selection-map semantics");
    bindings_free(&projected);
    Atom *prefix_roots[3] = {
        atom_var_with_id(&arena, "prefix-first", test_id(0u)),
        atom_var_with_id(&arena, "prefix-middle", test_id(16u)),
        unmarked_projection_root,
    };
    uint32_t prefix_marks[6] = {64u, 0u, 16u, 64u, 17u, 1u};
    CHECK(bindings_project_reachable_with_entry_marks(
              &marked_projection_source, prefix_roots, 3u,
              prefix_marks, 6u, &projected) && projected.len == 3u &&
              prefix_marks[0] == 3u && prefix_marks[1] == 0u &&
              prefix_marks[2] == 1u && prefix_marks[3] == 3u &&
              prefix_marks[4] == 2u && prefix_marks[5] == 1u,
          "projection translates unordered duplicate and empty prefix marks");
    bindings_free(&projected);
    bool indexed_projection_ready = binding_is_int(
        &marked_projection_source, test_id(63u), 63);
    BindingsLookupIndex *borrowed_projection_index =
        marked_projection_source.lookup_index;
    uint32_t indexed_projection_mark = 64u;
    CHECK(indexed_projection_ready &&
              bindings_project_reachable_with_entry_marks(
                  &marked_projection_source, unmarked_projection_roots, 1u,
                  &indexed_projection_mark, 1u, &projected) &&
              projected.len == 1u && indexed_projection_mark == 1u &&
              binding_is_int(&projected, test_id(63u), 63) &&
              marked_projection_source.lookup_index == borrowed_projection_index,
          "marked projection borrows the synchronized source index unchanged");
    bindings_free(&projected);

    BindingsBuilder projection_tail;
    bool projection_tail_ready = bindings_builder_init(
        &projection_tail, &marked_projection_source);
    bool projection_tail_added = projection_tail_ready &&
        bindings_builder_add_id_fresh(
            &projection_tail, test_id(64u), SYMBOL_ID_NONE, atom_int(&arena, 64));
    Atom *projection_tail_root = atom_var_with_id(
        &arena, "projection-tail", test_id(64u));
    uint32_t projection_tail_mark = 65u;
    bool projection_tail_partial = true;
#ifdef CETTA_TEST_HOOKS
    uint32_t projection_synced_before = 0u;
    uint32_t projection_synced_after = 0u;
    if (lookup_index_expected)
        projection_tail_partial = projection_tail_added &&
            bindings_lookup_index_test_synced_len(
                &projection_tail.current, &projection_synced_before) &&
            projection_synced_before < projection_tail.current.len;
#endif
    bool projection_tail_ok = projection_tail_added && projection_tail_partial &&
        bindings_project_reachable_with_entry_marks(
            &projection_tail.current, &projection_tail_root, 1u,
            &projection_tail_mark, 1u, &projected);
#ifdef CETTA_TEST_HOOKS
    if (lookup_index_expected && projection_tail_ok)
        projection_tail_ok = bindings_lookup_index_test_synced_len(
            &projection_tail.current, &projection_synced_after) &&
            projection_synced_after == projection_synced_before;
#endif
    CHECK(projection_tail_ok && projected.len == 1u &&
              projection_tail_mark == 1u &&
              binding_is_int(&projected, test_id(64u), 64) &&
              marked_projection_source.len == 64u,
          "partial shared indexes fall back without losing an appended root");
    bindings_free(&projected);
    if (projection_tail_ready)
        bindings_builder_free(&projection_tail);
    bindings_free(&marked_projection_source);

    Atom *cyclic_projection_root = atom_expr_builder_begin(&arena, 2u);
    cyclic_projection_root->expr.elems[0] = cyclic_projection_root;
    cyclic_projection_root->expr.elems[1] = projection_root;
    cyclic_projection_root = atom_expr_builder_finish(
        &arena, cyclic_projection_root);
    Atom *cyclic_projection_roots[1] = {cyclic_projection_root};
    CHECK(cyclic_projection_root &&
              (cyclic_projection_root->flags & ATOM_FLAG_HASH_STABLE) == 0u &&
              !bindings_project_reachable(
                  &base, cyclic_projection_roots, 1u, &projected),
          "non-stable cyclic projection retains the guarded collector");
    Atom *mixed_cycle_roots[2] = {multi_support, cyclic_projection_root};
    CHECK(!bindings_project_reachable(
              &base, mixed_cycle_roots, 2u, &projected),
          "completed shared roots do not hide a later structural cycle");
    uint32_t invalid_projection_mark = 66u;
    CHECK(!bindings_project_reachable_with_entry_marks(
              &base, projection_roots, 1u,
              &invalid_projection_mark, 1u, &projected) &&
              invalid_projection_mark == 66u,
          "invalid projection entry boundary leaves its mark unchanged");

    Atom *epoch_local = atom_var_with_id(
        &arena, "epoch-local", test_id(7200u));
    Atom *epoch_outer = atom_var_with_id(
        &arena, "epoch-outer", test_id(7201u));
    VarId epoch_local_id =
        var_epoch_id(epoch_local->var_id, 17u);
    Bindings epoch_environment;
    bindings_init(&epoch_environment);
    bool epoch_fixture =
        bindings_add_id(
            &epoch_environment, test_id(7199u), SYMBOL_ID_NONE,
            atom_int(&arena, -1)) &&
        bindings_add_id(
            &epoch_environment, epoch_local_id,
            epoch_local->sym_id, epoch_outer) &&
        bindings_add_id(
            &epoch_environment, epoch_outer->var_id,
            epoch_outer->sym_id, atom_int(&arena, 77)) &&
        bindings_add_id(
            &epoch_environment, test_id(7202u), SYMBOL_ID_NONE,
            atom_int(&arena, -2));
    BindingsEpochRoot epoch_root = {
        .atom = epoch_local,
        .epoch = 17u,
    };
    uint32_t epoch_projection_marks[3] = {1u, 2u, 3u};
    Bindings epoch_projected;
    CHECK(epoch_fixture &&
              bindings_project_reachable_with_epoch_roots_and_entry_marks(
                  &epoch_environment, NULL, 0u,
                  &epoch_root, 1u,
                  epoch_projection_marks, 3u, &epoch_projected) &&
              epoch_projected.len == 1u &&
              epoch_projection_marks[0] == 0u &&
              epoch_projection_marks[1] == 1u &&
              epoch_projection_marks[2] == 1u &&
              bindings_lookup_value_id(&epoch_projected, epoch_local_id).skeleton == epoch_outer &&
              binding_is_int(
                  &epoch_projected, epoch_outer->var_id, 77) &&
              bindings_lookup_value_id(&epoch_projected, test_id(7199u)).skeleton == NULL,
          "epoch roots retain a lazy activation namespace and its transitive closure");
    bindings_free(&epoch_projected);

    Arena support_arena;
    TermUniverse support_universe;
    arena_init(&support_arena);
    term_universe_init(&support_universe);
    term_universe_set_persistent_arena(
        &support_universe, &support_arena);
    AtomId epoch_local_atom_id = term_universe_store_atom_id(
        &support_universe, NULL, epoch_local);
    Atom *canonical_epoch_local = term_universe_get_atom(
        &support_universe, epoch_local_atom_id);
    const CettaTermVariableSupport *epoch_support = NULL;
    BindingsEpochRoot summarized_epoch_root = {
        .atom = canonical_epoch_local,
        .epoch = 17u,
    };
    bool support_ready =
        epoch_local_atom_id != CETTA_ATOM_ID_NONE &&
        canonical_epoch_local != NULL &&
        canonical_epoch_local->kind == ATOM_VAR &&
        term_universe_variable_support(
            &support_universe, epoch_local_atom_id,
            &epoch_support) &&
        epoch_support != NULL;
    summarized_epoch_root.variable_support = epoch_support;
    BindingsEpochRoot traversed_epoch_root = {
        .atom = canonical_epoch_local,
        .epoch = 17u,
    };
    VarId canonical_epoch_id = support_ready
        ? var_epoch_id(canonical_epoch_local->var_id, 17u)
        : VAR_ID_NONE;
    Bindings canonical_epoch_environment;
    bindings_init(&canonical_epoch_environment);
    bool canonical_epoch_fixture =
        support_ready &&
        bindings_add_id(
            &canonical_epoch_environment, test_id(7199u),
            SYMBOL_ID_NONE, atom_int(&arena, -1)) &&
        bindings_add_id(
            &canonical_epoch_environment, canonical_epoch_id,
            canonical_epoch_local->sym_id, epoch_outer) &&
        bindings_add_id(
            &canonical_epoch_environment, epoch_outer->var_id,
            epoch_outer->sym_id, atom_int(&arena, 77)) &&
        bindings_add_id(
            &canonical_epoch_environment, test_id(7202u),
            SYMBOL_ID_NONE, atom_int(&arena, -2));
    uint32_t traversed_epoch_marks[3] = {1u, 2u, 3u};
    uint32_t summarized_epoch_marks[3] = {1u, 2u, 3u};
    Bindings traversed_epoch_projected;
    Bindings summarized_epoch_projected;
    bindings_init(&traversed_epoch_projected);
    bindings_init(&summarized_epoch_projected);
    CHECK(canonical_epoch_fixture &&
              bindings_project_reachable_with_epoch_roots_and_entry_marks(
                  &canonical_epoch_environment, NULL, 0u,
                  &traversed_epoch_root, 1u,
                  traversed_epoch_marks, 3u,
                  &traversed_epoch_projected) &&
              bindings_project_reachable_with_epoch_roots_and_entry_marks(
                  &canonical_epoch_environment, NULL, 0u,
                  &summarized_epoch_root, 1u,
                  summarized_epoch_marks, 3u,
                  &summarized_epoch_projected) &&
              bindings_eq(
                  &traversed_epoch_projected,
                  &summarized_epoch_projected) &&
              memcmp(traversed_epoch_marks,
                     summarized_epoch_marks,
                     sizeof(traversed_epoch_marks)) == 0,
          "intrinsic variable support is extensionally equal to epoch-root traversal");
    bindings_free(&traversed_epoch_projected);
    bindings_free(&summarized_epoch_projected);
    bindings_free(&canonical_epoch_environment);
    term_universe_free(&support_universe);
    arena_free(&support_arena);

    BindingsBuilder epoch_branch = {0};
    bool epoch_branch_initialized =
        bindings_builder_init(&epoch_branch, &epoch_environment);
    uint32_t epoch_entry_mark = 2u;
    uint64_t epoch_discarded = 0u;
    CHECK(epoch_branch_initialized &&
              bindings_builder_compact_reachable_with_epoch_roots_and_entry_marks(
                  &epoch_branch, NULL, 0u,
                  &epoch_root, 1u, NULL, 0u,
                  &epoch_entry_mark, 1u,
                  &epoch_discarded, NULL) &&
              epoch_branch.current.len == 1u &&
              epoch_entry_mark == 1u &&
              epoch_discarded == 2u &&
              bindings_lookup_value_id(&epoch_branch.current, epoch_local_id).skeleton == epoch_outer &&
              binding_is_int(
                  &epoch_branch.current, epoch_outer->var_id, 77),
          "epoch-root compaction preserves lazy activation meaning");
    if (epoch_branch_initialized)
        bindings_builder_free(&epoch_branch);

    BindingsEpochRoot invalid_epoch_root = {
        .atom = epoch_local,
        .epoch = 0u,
    };
    uint32_t invalid_epoch_mark = 4u;
    bindings_init(&epoch_projected);
    CHECK(!bindings_project_reachable_with_epoch_roots_and_entry_marks(
              &epoch_environment, NULL, 0u,
              &invalid_epoch_root, 1u,
              &invalid_epoch_mark, 1u, &epoch_projected) &&
              invalid_epoch_mark == 4u &&
              epoch_environment.len == 3u,
          "an invalid epoch root fails without changing its source environment");
    bindings_free(&epoch_projected);
    Atom *epoch_apply_result = bindings_apply_epoch(
        &epoch_environment, &arena, epoch_local, 17u);
    CHECK(epoch_apply_result &&
              epoch_apply_result->kind == ATOM_GROUNDED &&
              epoch_apply_result->ground.gkind == GV_INT &&
              epoch_apply_result->ground.ival == 77,
          "acyclic epoch application consumes the cached graph summary");
    bindings_free(&epoch_environment);

    {
        Atom *outer = atom_var_with_id(&arena, "activation-outer", test_id(7300u));
        Atom *source = atom_var_with_id(&arena, "activation-source", test_id(7302u));
        Atom *prefix = atom_var_with_id(&arena, "activation-prefix", test_id(7305u));
        Atom *value = atom_int(&arena, 7303);
        BindingsBuilder activation;
        CHECK(bindings_builder_init(&activation, NULL) &&
              bindings_builder_add_var_fresh(&activation, outer, value) &&
              bindings_builder_add_id_fresh(&activation, var_epoch_id(prefix->var_id, 29u),
                  prefix->sym_id, value), "activation prefix fixture");
        VarId ids[] = {source->var_id, prefix->var_id};
        Atom *variables[] = {source, prefix};
        BindingsActivationView view;
        bindings_activation_view_init(&view);
        CHECK(bindings_activation_view_prepare(&view, &activation, ids, variables, 2u, 29u, 1u) &&
              bindings_builder_add_id_fresh(&activation, var_epoch_id(source->var_id, 29u),
                  source->sym_id, outer), "activation writes use versioned slots");
        Atom *local = bindings_apply_epoch_since(&activation.current, &arena, source, 29u, 1u);
        Atom *full = bindings_apply_activation_view_then_all(&activation.current, &arena, source, &view);
        CHECK(local == outer && full == value,
              "activation substitution and outer alias resolution compose exactly");
        Atom *closed = NULL;
        CHECK(bindings_resolve_epoch_view_ground(&activation.current, source, 29u, 1u, &closed) &&
              closed == value, "direct activation observation follows a slot alias");
        CHECK(bindings_resolve_epoch_view_ground(&activation.current, prefix, 29u, 1u, &closed) &&
              !closed, "activation observation excludes the preceding slot version");
        CHECK(!bindings_apply_epoch_since(&activation.current, &arena, source, 29u, 2u) &&
              !bindings_resolve_epoch_view_ground(&activation.current, source, 29u, 2u, &closed),
              "an invalid activation boundary fails closed");
        Bindings retained;
        CHECK(bindings_clone(&retained, &activation.current) &&
              bindings_rewrite_value_id(&activation.current, var_epoch_id(source->var_id, 29u),
                  binding_value_from_atom(atom_int(&arena, 7305))),
              "a slot rewrite retains an independent branch image");
        Atom *rewritten = bindings_apply_activation_view_then_all(&activation.current, &arena, source, &view);
        CHECK(rewritten && atom_eq(rewritten, atom_int(&arena, 7305)) &&
              bindings_apply_epoch_then_all(&retained, &arena, source, 29u, 1u) == value,
              "a stable frame view observes the current slot without changing its retained version");
        CHECK(bindings_apply_activation_view_then_all(&retained, &arena, source, &view) == value,
              "the same activation view reads the explicitly supplied captured image");
        bindings_builder_free(&activation);
        CHECK(bindings_apply_activation_view_then_all(&retained, &arena, source, &view) == value,
              "captured activation observation survives its original builder");
        Bindings absent;
        bindings_init(&absent);
        CHECK(!bindings_activation_view_available(&view, &absent) &&
              !bindings_apply_activation_view_then_all(&absent, &arena, source, &view),
              "a descriptor cannot read an image that does not hold its frame");
        bindings_free(&absent);
        bindings_free(&retained);
        bindings_activation_view_free(&view);
    }

    {
        CettaFrameIdentity first_identity = 0u;
        CettaFrameIdentity next_identity = 0u;
        VarId id = UINT64_C(1);
        Atom *variable = atom_var_with_id(&arena, "recycled-view", id);
        Atom *variables[] = {variable};
        BindingsBuilder owner;
        BindingsActivationView old_view, next_view;
        bindings_activation_view_init(&old_view);
        bindings_activation_view_init(&next_view);
        CHECK(cetta_frame_identity_acquire(&first_identity) &&
              bindings_builder_init(&owner, NULL) &&
              bindings_activation_view_prepare(&old_view, &owner, &id, variables,
                                               1u, first_identity, 0u),
              "a view names an owned allocator identity");
        bindings_builder_free(&owner);
        cetta_frame_identity_release(first_identity);
        CHECK(cetta_frame_identity_acquire(&next_identity) &&
              cetta_frame_handle(first_identity) == cetta_frame_handle(next_identity) &&
              next_identity != first_identity &&
              bindings_builder_init(&owner, NULL) &&
              bindings_activation_view_prepare(&next_view, &owner, &id, variables,
                                               1u, next_identity, 0u),
              "released activation storage recycles with a new generation");
        BindingValue value;
        bool present = false;
        CHECK(!bindings_activation_view_available(&old_view, &owner.current) &&
              !bindings_activation_view_read_slot(&owner.current, &old_view, 0u,
                                                  &value, &present) &&
              bindings_activation_view_read_slot(&owner.current, &next_view, 0u,
                                                 &value, &present) && !present,
              "a recycled handle rejects an old view and accepts its new unbound slot");
        bindings_activation_view_free(&old_view);
        bindings_activation_view_free(&next_view);
        bindings_builder_free(&owner);
        cetta_frame_identity_release(next_identity);
    }

    Atom *dense_left = atom_var(&arena, "dense-left");
    Atom *dense_right = atom_var(&arena, "dense-right");
    Atom *dense_open = atom_var(&arena, "dense-open");
    Atom *dense_outer = atom_var(&arena, "dense-outer");
    Atom *dense_candidate = atom_var(&arena, "dense-candidate");
    Atom *dense_value = atom_expr2(
        &arena, atom_symbol(&arena, "DenseValue"),
        atom_int(&arena, 7500));
    Atom *dense_source = atom_expr3(
        &arena, atom_symbol(&arena, "DenseCall"), dense_left,
        atom_expr2(
            &arena, atom_symbol(&arena, "DenseTail"), dense_right));
    Atom *dense_query = atom_expr3(
        &arena, atom_symbol(&arena, "DenseCall"), dense_candidate,
        atom_expr2(
            &arena, atom_symbol(&arena, "DenseTail"), dense_candidate));
    VarId dense_ids[3] = {
        dense_left->var_id,
        dense_right->var_id,
        dense_open->var_id,
    };
    Atom *dense_variables[3] = {
        dense_left,
        dense_right,
        dense_open,
    };
    for (uint32_t left_index = 0u; left_index < 3u; left_index++) {
        for (uint32_t right_index = left_index + 1u;
             right_index < 3u; right_index++) {
            if (dense_ids[right_index] >= dense_ids[left_index])
                continue;
            VarId id_swap = dense_ids[left_index];
            dense_ids[left_index] = dense_ids[right_index];
            dense_ids[right_index] = id_swap;
            Atom *variable_swap = dense_variables[left_index];
            dense_variables[left_index] = dense_variables[right_index];
            dense_variables[right_index] = variable_swap;
        }
    }
    uint32_t dense_left_slot = UINT32_MAX;
    uint32_t dense_open_slot = UINT32_MAX;
    for (uint32_t index = 0u; index < 3u; index++) {
        if (dense_ids[index] == dense_left->var_id)
            dense_left_slot = index;
        if (dense_ids[index] == dense_open->var_id)
            dense_open_slot = index;
    }
    Bindings dense_environment;
    bindings_init(&dense_environment);
    bool dense_fixture =
        bindings_add_var(
            &dense_environment, dense_outer, dense_value);
    uint32_t dense_first_entry = dense_environment.len;
    BindingsBuilder dense_frame_builder;
    bool dense_frame_ready = dense_fixture && bindings_builder_init(
        &dense_frame_builder, &dense_environment);
    BindingsActivationView dense_frame;
    bindings_activation_view_init(&dense_frame);
    bool dense_prepared = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    dense_frame_ready = dense_prepared &&
        bindings_builder_add_id_fresh(
            &dense_frame_builder,
            var_epoch_id(dense_left->var_id, 67u),
            dense_left->sym_id, dense_outer) &&
        bindings_builder_add_id_fresh(
            &dense_frame_builder,
            var_epoch_id(dense_right->var_id, 67u),
            dense_right->sym_id, dense_value);
    dense_prepared = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    if (dense_prepared) {
        Bindings framed_environment;
        bindings_init(&framed_environment);
        dense_prepared = bindings_clone(
            &framed_environment, &dense_frame_builder.current);
        if (dense_prepared) {
            bindings_free(&dense_environment);
            bindings_move(&dense_environment, &framed_environment);
        } else {
            bindings_free(&framed_environment);
        }
    }
    Atom *dense_reference = dense_prepared
        ? bindings_apply_epoch_then_all(
              &dense_frame_builder.current, &arena, dense_source,
              67u, dense_first_entry)
        : NULL;
    Atom *dense_compiled = dense_prepared
        ? bindings_apply_activation_view_then_all(
              &dense_frame_builder.current, &arena, dense_source,
              &dense_frame)
        : NULL;
    CHECK(dense_reference && dense_compiled &&
              atom_eq(dense_reference, dense_compiled),
          "dense activation application equals the authoritative epoch view");

    Atom *dense_coord_a = atom_var_with_id(
        &arena, "dense-coord-a", UINT64_C(7600));
    Atom *dense_coord_b = atom_var_with_id(
        &arena, "dense-coord-b", UINT64_C(7601));
    Atom *dense_coord_c = atom_var_with_id(
        &arena, "dense-coord-c", UINT64_C(7602));
    VarId dense_contiguous_ids[3] = {
        dense_coord_a->var_id,
        dense_coord_b->var_id,
        dense_coord_c->var_id,
    };
    Atom *dense_contiguous_variables[3] = {
        dense_coord_a, dense_coord_b, dense_coord_c,
    };
    VarId dense_sparse_ids[2] = {
        dense_coord_a->var_id,
        dense_coord_c->var_id,
    };
    Atom *dense_sparse_variables[2] = {
        dense_coord_a, dense_coord_c,
    };
    BindingsActivationView dense_coordinate_frame;
    bindings_activation_view_init(&dense_coordinate_frame);
    bool dense_contiguous_prepared = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_coordinate_frame, &dense_frame_builder,
            dense_contiguous_ids, dense_contiguous_variables,
            3u, 67u, dense_first_entry);
    bool dense_contiguous_recognized = dense_contiguous_prepared &&
        dense_coordinate_frame.source_ids_contiguous &&
        dense_coordinate_frame.source_first_id == dense_coord_a->var_id;
    Atom *dense_coordinate_source = atom_expr3(
        &arena, atom_symbol(&arena, "DenseCoordinates"),
        dense_coord_a, dense_coord_c);
    Atom *dense_contiguous_value = dense_contiguous_recognized
        ? bindings_apply_activation_view_then_all(
              &dense_frame_builder.current, &arena,
              dense_coordinate_source, &dense_coordinate_frame)
        : NULL;
    Atom *dense_contiguous_reference = dense_contiguous_recognized
        ? bindings_apply_epoch_then_all(
              &dense_frame_builder.current, &arena,
              dense_coordinate_source, 67u, dense_first_entry)
        : NULL;
    bool dense_sparse_prepared = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_coordinate_frame, &dense_frame_builder,
            dense_sparse_ids, dense_sparse_variables,
            2u, 67u, dense_first_entry);
    Atom *dense_below = atom_var_with_id(
        &arena, "dense-coord-below", UINT64_C(7599));
    Atom *dense_above = atom_var_with_id(
        &arena, "dense-coord-above", UINT64_C(7603));
    Atom *dense_sparse_source = atom_expr3(
        &arena, atom_symbol(&arena, "DenseSparseCoordinates"),
        atom_expr3(
            &arena, atom_symbol(&arena, "DenseSparseMembers"),
            dense_coord_a, dense_coord_c),
        atom_expr3(
            &arena, atom_symbol(&arena, "DenseSparseBounds"),
            dense_below, dense_above));
    Atom *dense_sparse_value = dense_sparse_prepared
        ? bindings_apply_activation_view_then_all(
              &dense_frame_builder.current, &arena,
              dense_sparse_source, &dense_coordinate_frame)
        : NULL;
    Atom *dense_sparse_reference = dense_sparse_prepared
        ? bindings_apply_epoch_then_all(
              &dense_frame_builder.current, &arena,
              dense_sparse_source, 67u, dense_first_entry)
        : NULL;
    CHECK(dense_contiguous_recognized && dense_sparse_prepared &&
              dense_contiguous_value && dense_contiguous_reference &&
              atom_eq(
                  dense_contiguous_value, dense_contiguous_reference) &&
              !dense_coordinate_frame.source_ids_contiguous &&
              dense_sparse_value && dense_sparse_reference &&
              atom_eq(dense_sparse_value, dense_sparse_reference),
          "dense frames recognize contiguous coordinates and retain sparse fallback");
    bindings_activation_view_free(&dense_coordinate_frame);
    Atom *dense_slot_value = dense_prepared &&
            dense_left_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena, &dense_frame,
              dense_left,
              dense_left_slot)
        : NULL;
    Atom *dense_slot_open = dense_prepared &&
            dense_open_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena, &dense_frame,
              dense_open,
              dense_open_slot)
        : NULL;
    Atom *dense_slot_root = dense_prepared &&
            dense_left_slot != UINT32_MAX
        ? bindings_resolve_activation_view_slot_root(
              &dense_frame_builder.current, &arena, &dense_frame,
              dense_left,
              dense_left_slot)
        : NULL;
    CHECK(dense_slot_value == dense_value && dense_slot_open &&
              dense_slot_open->kind == ATOM_VAR &&
              dense_slot_open->var_id ==
                  var_epoch_id(dense_open->var_id, 67u) &&
              dense_slot_root == dense_value,
          "compiler-known activation slots resolve bound and open values exactly");

    Atom *dense_mismatched_slot = dense_prepared &&
            dense_left_slot != UINT32_MAX
        ? bindings_resolve_activation_view_slot_root(
              &dense_frame_builder.current, &arena, &dense_frame,
              dense_right, dense_left_slot)
        : NULL;
    CHECK(!dense_mismatched_slot,
          "a compiled slot declines when it names a different source variable");

    BindingsBuilder dense_reference_builder;
    BindingsBuilder dense_compiled_builder;
    bool dense_reference_ready = bindings_builder_init(
        &dense_reference_builder, &dense_environment);
    bool dense_compiled_ready = bindings_builder_init(
        &dense_compiled_builder, &dense_environment);
    Arena dense_reference_arena;
    Arena dense_compiled_arena;
    arena_init(&dense_reference_arena);
    arena_init(&dense_compiled_arena);
    BindingsActivationView dense_match_frame;
    bindings_activation_view_init(&dense_match_frame);
    bool dense_match_prepared = dense_compiled_ready &&
        bindings_activation_view_prepare(
            &dense_match_frame, &dense_compiled_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    Atom *dense_materialized = dense_reference_ready
        ? bindings_apply_epoch_then_all(
              &dense_reference_builder.current,
              &dense_reference_arena, dense_source,
              67u, dense_first_entry)
        : NULL;
    bool dense_reference_match = dense_materialized &&
        match_atoms_epoch_builder(
            dense_materialized, dense_query,
            &dense_reference_builder, &dense_reference_arena, 71u);
    bool dense_compiled_match = dense_match_prepared &&
        match_atoms_activation_view_builder(
            dense_source, &dense_match_frame, dense_query,
            &dense_compiled_builder, &dense_compiled_arena, 71u);
    CHECK(dense_reference_match && dense_compiled_match &&
              bindings_eq(
                  &dense_reference_builder.current,
                  &dense_compiled_builder.current),
          "dense activation matching equals materialize-then-match");

    uint32_t dense_refresh_mark = dense_frame_ready
        ? bindings_builder_save(&dense_frame_builder) : 0u;
    Atom *dense_open_value = atom_int(&arena, 7501);
    bool dense_refreshed = dense_frame_ready && dense_prepared &&
        bindings_builder_add_id_fresh(
            &dense_frame_builder,
            var_epoch_id(dense_open->var_id, 67u),
            dense_open->sym_id, dense_open_value) &&
        bindings_activation_view_available(
            &dense_frame, &dense_frame_builder.current);
    Atom *dense_refreshed_slot = dense_refreshed &&
            dense_open_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena,
              &dense_frame, dense_open, dense_open_slot)
        : NULL;
    CHECK(dense_refreshed &&
              dense_refreshed_slot == dense_open_value,
          "dense activation reads the current slot after an appended write");

    uint64_t dense_refresh_rollbacks = dense_frame_ready
        ? dense_frame_builder.rollback_count : 0u;
    if (dense_frame_ready) {
        bindings_builder_rollback(
            &dense_frame_builder, dense_refresh_mark);
    }
    Atom *dense_replacement_value = atom_int(&arena, 7502);
    bool dense_replaced_same_length = dense_frame_ready &&
        bindings_builder_add_id_fresh(
            &dense_frame_builder,
            var_epoch_id(dense_open->var_id, 67u),
            dense_open->sym_id, dense_replacement_value) &&
        bindings_activation_view_available(&dense_frame, &dense_frame_builder.current);
    Atom *dense_stale_consumer = dense_replaced_same_length
        ? bindings_resolve_activation_view_slot_root(
              &dense_frame_builder.current, &arena, &dense_frame,
              dense_open, dense_open_slot)
        : NULL;
    bool dense_aba_rejected = dense_replaced_same_length &&
        dense_stale_consumer == dense_replacement_value &&
        dense_frame_builder.rollback_count ==
            dense_refresh_rollbacks + 1u &&
        bindings_activation_view_available(
            &dense_frame, &dense_frame_builder.current);
    bool dense_replacement_rebuilt = dense_aba_rejected &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    Atom *dense_replacement_slot = dense_replacement_rebuilt &&
            dense_open_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena,
              &dense_frame, dense_open, dense_open_slot)
        : NULL;
    CHECK(dense_replacement_slot == dense_replacement_value,
          "equal-length rollback and reappend reads the new slot through the same identity");

    if (dense_frame_ready) {
        bindings_builder_rollback(
            &dense_frame_builder, dense_refresh_mark);
    }
    bool dense_shrink_rejected = dense_frame_ready &&
        bindings_activation_view_available(
            &dense_frame, &dense_frame_builder.current);
    bool dense_rebuilt = dense_shrink_rejected &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    Atom *dense_rebuilt_open = dense_rebuilt &&
            dense_open_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena,
              &dense_frame, dense_open, dense_open_slot)
        : NULL;
    CHECK(dense_rebuilt && dense_rebuilt_open &&
              dense_rebuilt_open->kind == ATOM_VAR &&
              dense_rebuilt_open->var_id ==
                  var_epoch_id(dense_open->var_id, 67u),
          "rollback exposes the restored unbound slot through its identity");

    const void *dense_schema_identity = NULL;
    const void *dense_slot_identity = NULL;
    bool dense_authority_reborrowed = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry) &&
        bindings_frame_storage_test_identity(
            &dense_frame_builder.current, 67u,
            &dense_schema_identity, &dense_slot_identity) &&
        dense_slot_identity != NULL;
    Atom *dense_reborrowed_open = dense_authority_reborrowed &&
            dense_open_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena,
              &dense_frame, dense_open, dense_open_slot)
        : NULL;
    CHECK(dense_authority_reborrowed &&
              dense_reborrowed_open &&
              dense_reborrowed_open->kind == ATOM_VAR &&
              dense_reborrowed_open->var_id ==
                  var_epoch_id(dense_open->var_id, 67u),
          "dense frame resolves authoritative slots without cached presence state");

    BindingsActivationView dense_saturated_frame;
    bindings_activation_view_init(&dense_saturated_frame);
    uint64_t dense_saved_growth = dense_frame_builder.growth_count;
    uint64_t dense_saved_rollbacks = dense_frame_builder.rollback_count;
    dense_frame_builder.growth_count = UINT64_MAX;
    dense_frame_builder.rollback_count = UINT64_MAX;
    bool dense_saturation_independent =
        bindings_activation_view_prepare(
            &dense_saturated_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    Atom *dense_saturation_reference = bindings_apply_epoch_then_all(
        &dense_frame_builder.current, &arena, dense_source,
        67u, dense_first_entry);
    CHECK(dense_saturation_independent && dense_saturation_reference &&
              atom_eq(dense_saturation_reference, dense_reference),
          "identity views do not depend on saturated growth counters");
    dense_frame_builder.growth_count = dense_saved_growth;
    dense_frame_builder.rollback_count = dense_saved_rollbacks;
    bindings_activation_view_free(&dense_saturated_frame);

    VarId dense_unsorted_ids[2] = {
        dense_ids[1], dense_ids[0],
    };
    Atom *dense_unsorted_variables[2] = {
        dense_variables[1], dense_variables[0],
    };
    BindingsActivationView dense_invalid_frame;
    bindings_activation_view_init(&dense_invalid_frame);
    CHECK(dense_frame_ready &&
              !bindings_activation_view_prepare(
                  &dense_invalid_frame, &dense_frame_builder,
                  dense_unsorted_ids, dense_unsorted_variables,
                  2u, 67u, dense_first_entry),
          "dense activation admission rejects an unsorted variable inventory");
    bindings_activation_view_free(&dense_invalid_frame);

    /* Reusing a builder address is irrelevant: the supplied captured image
     * still owns this frame. No descriptor stores values from the old builder. */
    BindingsBuilder dense_reinit_builder;
    BindingsActivationView dense_reinit_frame;
    bindings_activation_view_init(&dense_reinit_frame);
    bool dense_reinit_ready = bindings_builder_init(
        &dense_reinit_builder, &dense_environment);
    bool dense_reinit_prepared = dense_reinit_ready &&
        bindings_activation_view_prepare(
            &dense_reinit_frame, &dense_reinit_builder,
            dense_ids, dense_variables, 3u, 67u,
            dense_first_entry);
    uint64_t dense_old_instance = dense_reinit_builder.instance_id;
    if (dense_reinit_ready)
        bindings_builder_free(&dense_reinit_builder);
    bool dense_same_address_reinit = bindings_builder_init(
        &dense_reinit_builder, &dense_environment);
    Atom *dense_reinit_stale_slot =
        dense_reinit_prepared && dense_same_address_reinit &&
        dense_left_slot != UINT32_MAX
        ? bindings_resolve_activation_view_slot_root(
              &dense_reinit_builder.current, &arena, &dense_reinit_frame,
              dense_left, dense_left_slot)
        : NULL;
    CHECK(dense_reinit_prepared && dense_same_address_reinit &&
              dense_reinit_builder.instance_id != 0u &&
              dense_reinit_builder.instance_id != dense_old_instance &&
              bindings_activation_view_available(
                  &dense_reinit_frame, &dense_reinit_builder.current) &&
              dense_reinit_stale_slot == dense_value,
          "a retained frame image remains readable after builder reincarnation");
    bindings_activation_view_free(&dense_reinit_frame);
    if (dense_same_address_reinit)
        bindings_builder_free(&dense_reinit_builder);

    bindings_activation_view_free(&dense_match_frame);
    arena_free(&dense_compiled_arena);
    arena_free(&dense_reference_arena);
    if (dense_compiled_ready)
        bindings_builder_free(&dense_compiled_builder);
    if (dense_reference_ready)
        bindings_builder_free(&dense_reference_builder);
    bindings_activation_view_free(&dense_frame);
    if (dense_frame_ready)
        bindings_builder_free(&dense_frame_builder);
    bindings_free(&dense_environment);

    /* A parser-style reduction and a rule-machine-style repeated slot both
     * exercise the generic activation view.  The reference path first
     * materializes the complete left term; the compiled path resolves only
     * the nodes demanded by matching. */
    Atom *view_local = atom_var_with_id(
        &arena, "view-local", test_id(7400u));
    Atom *view_outer = atom_var_with_id(
        &arena, "view-outer", test_id(7401u));
    Atom *view_candidate = atom_var_with_id(
        &arena, "view-candidate", test_id(7402u));
    Atom *view_value = atom_expr2(
        &arena, atom_symbol(&arena, "ParserNode"),
        atom_int(&arena, 7403));
    Atom *view_tail = atom_expr2(
        &arena, atom_symbol(&arena, "RuleTail"), view_local);
    Atom *view_left = atom_expr3(
        &arena, atom_symbol(&arena, "Reduce"),
        view_local, view_tail);
    Atom *view_right_tail = atom_expr2(
        &arena, atom_symbol(&arena, "RuleTail"), view_candidate);
    Atom *view_right = atom_expr3(
        &arena, atom_symbol(&arena, "Reduce"),
        view_candidate, view_right_tail);
    Bindings view_base;
    bindings_init(&view_base);
    BindingsBuilder view_seed_builder;
    bool view_fixture = bindings_builder_init(&view_seed_builder, NULL) &&
        bindings_builder_add_var_fresh(
            &view_seed_builder, view_outer, view_value);
    uint32_t view_first_entry = view_seed_builder.current.len;
    VarId view_source_ids[] = {view_local->var_id};
    Atom *view_source_variables[] = {view_local};
    BindingsActivationView view_seed_frame;
    bindings_activation_view_init(&view_seed_frame);
    view_fixture = view_fixture && bindings_activation_view_prepare(
        &view_seed_frame, &view_seed_builder,
        view_source_ids, view_source_variables, 1u, 37u,
        view_first_entry);
    view_fixture = view_fixture && bindings_builder_add_id_fresh(
        &view_seed_builder, var_epoch_id(view_local->var_id, 37u),
        view_local->sym_id, view_outer);
    view_fixture = view_fixture && bindings_clone(
        &view_base, &view_seed_builder.current);
    bindings_activation_view_free(&view_seed_frame);
    bindings_builder_free(&view_seed_builder);
    BindingsBuilder view_reference;
    BindingsBuilder view_compiled;
    bool view_reference_ready = bindings_builder_init(
        &view_reference, &view_base);
    bool view_compiled_ready = bindings_builder_init(
        &view_compiled, &view_base);
    Arena view_reference_arena;
    Arena view_compiled_arena;
    arena_init(&view_reference_arena);
    arena_init(&view_compiled_arena);
    Atom *view_materialized = view_fixture && view_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_reference.current, &view_reference_arena,
              view_left, 37u, view_first_entry)
        : NULL;
    bool view_reference_match = view_materialized &&
        match_atoms_epoch_builder(
            view_materialized, view_right, &view_reference,
            &view_reference_arena, 41u);
    bool view_compiled_match = view_fixture && view_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_left, 37u, view_first_entry, view_right,
            &view_compiled, &view_compiled_arena, 41u);
    size_t view_reference_bytes =
        arena_accounted_live_bytes(&view_reference_arena);
    size_t view_compiled_bytes =
        arena_accounted_live_bytes(&view_compiled_arena);
    CHECK(view_reference_match && view_compiled_match &&
              bindings_eq(
                  &view_reference.current, &view_compiled.current),
          "activation-view matching equals materialize-then-match across repeated slots");
    CHECK(view_compiled_bytes < view_reference_bytes,
          "activation-view matching removes complete parser/rule term materialization");

    /* A relational-pattern UNIFY consumes an ordinary live right operand,
     * not another freshly standardized rule.  Its direct activation view is
     * exactly materialize-left-then-match, including current variable ids. */
    BindingsBuilder view_current_reference;
    BindingsBuilder view_current_compiled;
    bool view_current_reference_ready = bindings_builder_init(
        &view_current_reference, &view_base);
    bool view_current_compiled_ready = bindings_builder_init(
        &view_current_compiled, &view_base);
    Arena view_current_reference_arena;
    Arena view_current_compiled_arena;
    arena_init(&view_current_reference_arena);
    arena_init(&view_current_compiled_arena);
    Atom *view_current_materialized =
        view_current_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_current_reference.current,
              &view_current_reference_arena,
              view_left, 37u, view_first_entry)
        : NULL;
    bool view_current_reference_match =
        view_current_materialized &&
        match_atoms_builder(
            view_current_materialized, view_right,
            &view_current_reference);
    bool view_current_compiled_match =
        view_current_compiled_ready &&
        match_atoms_epoch_view_builder_current(
            view_left, 37u, view_first_entry, view_right,
            &view_current_compiled,
            &view_current_compiled_arena);
    CHECK(view_current_reference_match &&
              view_current_compiled_match &&
              bindings_eq(
                  &view_current_reference.current,
                  &view_current_compiled.current),
          "activation view against a live term equals materialize-then-match");

    BindingsBuilder view_open_reference;
    BindingsBuilder view_open_compiled;
    Bindings view_open_base;
    bindings_init(&view_open_base);
    BindingsBuilder view_open_seed;
    bool view_open_fixture = bindings_builder_init(
        &view_open_seed, NULL);
    uint32_t view_open_first_entry = 0u;
    BindingsActivationView view_open_seed_frame;
    bindings_activation_view_init(&view_open_seed_frame);
    view_open_fixture = view_open_fixture &&
        bindings_activation_view_prepare(
            &view_open_seed_frame, &view_open_seed,
            view_source_ids, view_source_variables, 1u, 43u,
            view_open_first_entry) &&
        bindings_builder_add_id_fresh(
            &view_open_seed,
            var_epoch_id(view_local->var_id, 43u),
            view_local->sym_id, view_outer) &&
        bindings_clone(&view_open_base, &view_open_seed.current);
    bindings_activation_view_free(&view_open_seed_frame);
    bindings_builder_free(&view_open_seed);
    bool view_open_reference_ready = bindings_builder_init(
        &view_open_reference, &view_open_base);
    bool view_open_compiled_ready = bindings_builder_init(
        &view_open_compiled, &view_open_base);
    Arena view_open_reference_arena;
    Arena view_open_compiled_arena;
    arena_init(&view_open_reference_arena);
    arena_init(&view_open_compiled_arena);
    Atom *view_open_right = atom_expr3(
        &arena, atom_symbol(&arena, "Reduce"),
        atom_int(&arena, 7404),
        atom_expr2(
            &arena, atom_symbol(&arena, "RuleTail"),
            atom_int(&arena, 7404)));
    Atom *view_open_materialized =
        view_open_fixture && view_open_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_open_reference.current,
              &view_open_reference_arena, view_left, 43u,
              view_open_first_entry)
        : NULL;
    bool view_open_reference_match = view_open_materialized &&
        match_atoms_epoch_builder(
            view_open_materialized, view_open_right,
            &view_open_reference, &view_open_reference_arena, 47u);
    bool view_open_compiled_match =
        view_open_fixture && view_open_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_left, 43u, view_open_first_entry, view_open_right,
            &view_open_compiled, &view_open_compiled_arena, 47u);
    CHECK(view_open_reference_match && view_open_compiled_match &&
              bindings_eq(
                  &view_open_reference.current,
                  &view_open_compiled.current),
          "activation view preserves outer-variable binding by the candidate");

    BindingsBuilder view_whole_reference;
    BindingsBuilder view_whole_compiled;
    bool view_whole_reference_ready = bindings_builder_init(
        &view_whole_reference, &view_base);
    bool view_whole_compiled_ready = bindings_builder_init(
        &view_whole_compiled, &view_base);
    Arena view_whole_reference_arena;
    Arena view_whole_compiled_arena;
    arena_init(&view_whole_reference_arena);
    arena_init(&view_whole_compiled_arena);
    Atom *view_whole_materialized = view_whole_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_whole_reference.current,
              &view_whole_reference_arena, view_left, 37u,
              view_first_entry)
        : NULL;
    bool view_whole_reference_match = view_whole_materialized &&
        match_atoms_epoch_builder(
            view_whole_materialized, view_candidate,
            &view_whole_reference, &view_whole_reference_arena, 53u);
    bool view_whole_compiled_match = view_whole_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_left, 37u, view_first_entry, view_candidate,
            &view_whole_compiled, &view_whole_compiled_arena, 53u);
    Atom *view_whole_key = atom_var_like(
        &arena, view_candidate,
        var_epoch_id(view_candidate->var_id, 53u));
    Atom *view_whole_reference_value = view_whole_key
        ? bindings_apply(
              &view_whole_reference.current,
              &view_whole_reference_arena, view_whole_key)
        : NULL;
    Atom *view_whole_compiled_value = view_whole_key
        ? bindings_apply(
              &view_whole_compiled.current,
              &view_whole_compiled_arena, view_whole_key)
        : NULL;
    CHECK(view_whole_reference_match && view_whole_compiled_match &&
              view_whole_reference_value &&
              view_whole_compiled_value &&
              atom_eq(
                  view_whole_reference_value,
                  view_whole_compiled_value),
          "activation view retains a demanded whole-term closure with the same observation");
    uint32_t invalid_view_mark = view_compiled_ready
        ? bindings_builder_save(&view_compiled) : 0u;
    CHECK(view_compiled_ready &&
              !match_atoms_epoch_view_builder(
                  view_left, 37u, view_compiled.current.len + 1u,
                  view_right, &view_compiled,
                  &view_compiled_arena, 59u) &&
              bindings_builder_save(&view_compiled) == invalid_view_mark,
          "activation-view matcher rejects an invalid frame boundary without mutation");

    /* Open activation matching is a branch transaction.  A repeated local
     * variable can acquire a binding before a later occurrence disagrees;
     * both the materialized reference and the direct view must reject, and
     * the caller's rollback must restore the exact entry state. */
    Atom *view_fail_left = atom_expr3(
        &arena, atom_symbol(&arena, "OpenPair"),
        view_local, view_local);
    Atom *view_fail_right = atom_expr3(
        &arena, atom_symbol(&arena, "OpenPair"),
        atom_int(&arena, 7405), atom_int(&arena, 7406));
    BindingsBuilder view_fail_reference;
    BindingsBuilder view_fail_compiled;
    bool view_fail_reference_ready = bindings_builder_init(
        &view_fail_reference, NULL);
    bool view_fail_compiled_ready = bindings_builder_init(
        &view_fail_compiled, NULL);
    Arena view_fail_reference_arena;
    Arena view_fail_compiled_arena;
    arena_init(&view_fail_reference_arena);
    arena_init(&view_fail_compiled_arena);
    uint32_t view_fail_reference_mark = view_fail_reference_ready
        ? bindings_builder_save(&view_fail_reference) : 0u;
    uint32_t view_fail_compiled_mark = view_fail_compiled_ready
        ? bindings_builder_save(&view_fail_compiled) : 0u;
    ArenaMark view_fail_reference_arena_mark =
        arena_mark(&view_fail_reference_arena);
    ArenaMark view_fail_compiled_arena_mark =
        arena_mark(&view_fail_compiled_arena);
    Atom *view_fail_materialized = view_fail_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_fail_reference.current, &view_fail_reference_arena,
              view_fail_left, 61u, 0u)
        : NULL;
    bool view_fail_reference_match = view_fail_materialized &&
        match_atoms_epoch_builder(
            view_fail_materialized, view_fail_right,
            &view_fail_reference, &view_fail_reference_arena, 67u);
    bool view_fail_compiled_match = view_fail_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_fail_left, 61u, 0u, view_fail_right,
            &view_fail_compiled, &view_fail_compiled_arena, 67u);
    if (view_fail_reference_ready) {
        bindings_builder_rollback(
            &view_fail_reference, view_fail_reference_mark);
        arena_reset(
            &view_fail_reference_arena,
            view_fail_reference_arena_mark);
    }
    if (view_fail_compiled_ready) {
        bindings_builder_rollback(
            &view_fail_compiled, view_fail_compiled_mark);
        arena_reset(
            &view_fail_compiled_arena,
            view_fail_compiled_arena_mark);
    }
    CHECK(!view_fail_reference_match && !view_fail_compiled_match &&
              view_fail_reference_ready && view_fail_compiled_ready &&
              bindings_eq(
                  &view_fail_reference.current,
                  &view_fail_compiled.current) &&
              view_fail_reference.current.len == 0u,
          "open activation mismatch rejects and transactionally restores both paths");

    BindingsBuilder view_current_fail_reference;
    BindingsBuilder view_current_fail_compiled;
    bool view_current_fail_reference_ready = bindings_builder_init(
        &view_current_fail_reference, NULL);
    bool view_current_fail_compiled_ready = bindings_builder_init(
        &view_current_fail_compiled, NULL);
    Arena view_current_fail_reference_arena;
    Arena view_current_fail_compiled_arena;
    arena_init(&view_current_fail_reference_arena);
    arena_init(&view_current_fail_compiled_arena);
    uint32_t view_current_fail_reference_mark =
        view_current_fail_reference_ready
        ? bindings_builder_save(&view_current_fail_reference) : 0u;
    uint32_t view_current_fail_compiled_mark =
        view_current_fail_compiled_ready
        ? bindings_builder_save(&view_current_fail_compiled) : 0u;
    Atom *view_current_fail_materialized =
        view_current_fail_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_current_fail_reference.current,
              &view_current_fail_reference_arena,
              view_fail_left, 61u, 0u)
        : NULL;
    bool view_current_fail_reference_match =
        view_current_fail_materialized &&
        match_atoms_builder(
            view_current_fail_materialized, view_fail_right,
            &view_current_fail_reference);
    bool view_current_fail_compiled_match =
        view_current_fail_compiled_ready &&
        match_atoms_epoch_view_builder_current(
            view_fail_left, 61u, 0u, view_fail_right,
            &view_current_fail_compiled,
            &view_current_fail_compiled_arena);
    if (view_current_fail_reference_ready)
        bindings_builder_rollback(
            &view_current_fail_reference,
            view_current_fail_reference_mark);
    if (view_current_fail_compiled_ready)
        bindings_builder_rollback(
            &view_current_fail_compiled,
            view_current_fail_compiled_mark);
    CHECK(!view_current_fail_reference_match &&
              !view_current_fail_compiled_match &&
              view_current_fail_reference_ready &&
              view_current_fail_compiled_ready &&
              bindings_eq(
                  &view_current_fail_reference.current,
                  &view_current_fail_compiled.current) &&
              view_current_fail_reference.current.len == 0u,
          "live-term activation mismatch agrees and rolls back exactly");

    /* A successful bidirectional walk may expose a cyclic substitution.  The
     * production branch rejects that result before publication, then rolls
     * back.  Exercise the same boundary on the direct and materialized paths. */
    Atom *view_cycle_candidate = atom_var_with_id(
        &arena, "view-cycle-candidate", test_id(7407u));
    Atom *view_cycle_left = atom_expr3(
        &arena, atom_symbol(&arena, "OpenCycle"),
        view_local, view_local);
    Atom *view_cycle_right = atom_expr3(
        &arena, atom_symbol(&arena, "OpenCycle"),
        atom_expr2(
            &arena, atom_symbol(&arena, "Wrap"),
            view_cycle_candidate),
        view_cycle_candidate);
    BindingsBuilder view_cycle_reference;
    BindingsBuilder view_cycle_compiled;
    bool view_cycle_reference_ready = bindings_builder_init(
        &view_cycle_reference, NULL);
    bool view_cycle_compiled_ready = bindings_builder_init(
        &view_cycle_compiled, NULL);
    Arena view_cycle_reference_arena;
    Arena view_cycle_compiled_arena;
    arena_init(&view_cycle_reference_arena);
    arena_init(&view_cycle_compiled_arena);
    uint32_t view_cycle_reference_mark = view_cycle_reference_ready
        ? bindings_builder_save(&view_cycle_reference) : 0u;
    uint32_t view_cycle_compiled_mark = view_cycle_compiled_ready
        ? bindings_builder_save(&view_cycle_compiled) : 0u;
    Atom *view_cycle_materialized = view_cycle_reference_ready
        ? bindings_apply_epoch_then_all(
              &view_cycle_reference.current,
              &view_cycle_reference_arena,
              view_cycle_left, 71u, 0u)
        : NULL;
    bool view_cycle_reference_match = view_cycle_materialized &&
        match_atoms_epoch_builder(
            view_cycle_materialized, view_cycle_right,
            &view_cycle_reference, &view_cycle_reference_arena, 73u);
    bool view_cycle_compiled_match = view_cycle_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_cycle_left, 71u, 0u, view_cycle_right,
            &view_cycle_compiled, &view_cycle_compiled_arena, 73u);
    bool view_cycle_reference_loop = view_cycle_reference_ready &&
        bindings_has_loop(&view_cycle_reference.current);
    bool view_cycle_compiled_loop = view_cycle_compiled_ready &&
        bindings_has_loop(&view_cycle_compiled.current);
    if (view_cycle_reference_ready)
        bindings_builder_rollback(
            &view_cycle_reference, view_cycle_reference_mark);
    if (view_cycle_compiled_ready)
        bindings_builder_rollback(
            &view_cycle_compiled, view_cycle_compiled_mark);
    CHECK(!view_cycle_reference_match && !view_cycle_compiled_match &&
              !view_cycle_reference_loop && !view_cycle_compiled_loop &&
              bindings_eq(
                  &view_cycle_reference.current,
                  &view_cycle_compiled.current) &&
              view_cycle_reference.current.len == 0u,
          "open activation cycle is refused at the bind by both matchers, leaving nothing to publish");

    arena_free(&view_cycle_compiled_arena);
    arena_free(&view_cycle_reference_arena);
    if (view_cycle_compiled_ready)
        bindings_builder_free(&view_cycle_compiled);
    if (view_cycle_reference_ready)
        bindings_builder_free(&view_cycle_reference);
    arena_free(&view_current_fail_compiled_arena);
    arena_free(&view_current_fail_reference_arena);
    if (view_current_fail_compiled_ready)
        bindings_builder_free(&view_current_fail_compiled);
    if (view_current_fail_reference_ready)
        bindings_builder_free(&view_current_fail_reference);
    arena_free(&view_fail_compiled_arena);
    arena_free(&view_fail_reference_arena);
    if (view_fail_compiled_ready)
        bindings_builder_free(&view_fail_compiled);
    if (view_fail_reference_ready)
        bindings_builder_free(&view_fail_reference);

    arena_free(&view_whole_compiled_arena);
    arena_free(&view_whole_reference_arena);
    if (view_whole_compiled_ready)
        bindings_builder_free(&view_whole_compiled);
    if (view_whole_reference_ready)
        bindings_builder_free(&view_whole_reference);
    arena_free(&view_open_compiled_arena);
    arena_free(&view_open_reference_arena);
    if (view_open_compiled_ready)
        bindings_builder_free(&view_open_compiled);
    if (view_open_reference_ready)
        bindings_builder_free(&view_open_reference);
    bindings_free(&view_open_base);
    arena_free(&view_current_compiled_arena);
    arena_free(&view_current_reference_arena);
    if (view_current_compiled_ready)
        bindings_builder_free(&view_current_compiled);
    if (view_current_reference_ready)
        bindings_builder_free(&view_current_reference);
    arena_free(&view_compiled_arena);
    arena_free(&view_reference_arena);
    if (view_compiled_ready)
        bindings_builder_free(&view_compiled);
    if (view_reference_ready)
        bindings_builder_free(&view_reference);
    bindings_free(&view_base);

    /* A direct equation activation frame gives standardized rule variables ownership of
     * otherwise unconstrained aliases.  This is the query-visible normal
     * form used by the isolated equation matcher, obtained without building
     * and projecting a temporary environment. */
    Atom *slot_outer = atom_var_with_id(
        &arena, "slot-outer", test_id(7450u));
    Atom *slot_rule = atom_var_with_id(
        &arena, "slot-rule", test_id(7451u));
    BindingsBuilder slot_alias;
    bool slot_alias_ready = bindings_builder_init(&slot_alias, NULL);
    uint32_t slot_epoch = 61u;
    bool slot_alias_matched = slot_alias_ready &&
        match_atoms_epoch_builder_rule_local(
            slot_outer, slot_rule, &slot_alias, &arena, slot_epoch);
    CHECK(slot_alias_matched &&
              bindings_lookup_value_id(&slot_alias.current, var_epoch_id(slot_rule->var_id, slot_epoch)).skeleton ==
                  slot_outer &&
              bindings_lookup_value_id(&slot_alias.current, slot_outer->var_id).skeleton == NULL,
          "equation-activation-frame variable aliases point from rule slots to caller variables");
    if (slot_alias_ready)
        bindings_builder_free(&slot_alias);

    Atom *slot_rule_left = atom_var_with_id(
        &arena, "slot-rule-left", test_id(7452u));
    Atom *slot_rule_right = atom_var_with_id(
        &arena, "slot-rule-right", test_id(7453u));
    Atom *slot_rule_pair = atom_expr3(
        &arena, atom_symbol(&arena, "SlotPair"),
        slot_rule_left, slot_rule_right);
    BindingsBuilder slot_structured;
    bool slot_structured_ready =
        bindings_builder_init(&slot_structured, NULL);
    bool slot_structured_matched = slot_structured_ready &&
        match_atoms_epoch_builder_rule_local(
            slot_outer, slot_rule_pair, &slot_structured,
            &arena, slot_epoch);
    BindingValue slot_structured_value = slot_structured_matched
        ? bindings_lookup_value_id(
              &slot_structured.current, slot_outer->var_id)
        : binding_value_from_atom(NULL);
    Atom *slot_structured_observed = binding_value_materialize(
        &arena, slot_structured_value);
    CHECK(slot_structured_observed &&
              slot_structured_observed->kind == ATOM_EXPR &&
              slot_structured_observed->expr.len == 3u &&
              var_epoch_suffix(
                  slot_structured_observed->expr.elems[1]->var_id) ==
                  slot_epoch &&
              var_epoch_suffix(
                  slot_structured_observed->expr.elems[2]->var_id) ==
                  slot_epoch,
          "equation-activation-frame matching retains a caller's structured rule value");
    if (slot_structured_ready)
        bindings_builder_free(&slot_structured);

    Atom *slot_outer_left = atom_var_with_id(
        &arena, "slot-outer-left", test_id(7454u));
    Atom *slot_outer_right = atom_var_with_id(
        &arena, "slot-outer-right", test_id(7455u));
    Atom *slot_call = atom_expr3(
        &arena, atom_symbol(&arena, "SlotRepeat"),
        slot_outer_left, slot_outer_right);
    Atom *slot_repeated_rule = atom_expr3(
        &arena, atom_symbol(&arena, "SlotRepeat"),
        slot_rule, slot_rule);
    BindingsBuilder slot_repeated;
    bool slot_repeated_ready = bindings_builder_init(&slot_repeated, NULL);
    bool slot_repeated_matched = slot_repeated_ready &&
        match_atoms_epoch_builder_rule_local(
            slot_call, slot_repeated_rule, &slot_repeated,
            &arena, slot_epoch);
    Atom *slot_left_value = slot_repeated_matched
        ? bindings_apply(
              &slot_repeated.current, &arena, slot_outer_left)
        : NULL;
    Atom *slot_right_value = slot_repeated_matched
        ? bindings_apply(
              &slot_repeated.current, &arena, slot_outer_right)
        : NULL;
    CHECK(slot_left_value && slot_right_value &&
              slot_left_value->kind == ATOM_VAR &&
              slot_right_value->kind == ATOM_VAR &&
              slot_left_value->var_id == slot_right_value->var_id &&
              !bindings_has_loop(&slot_repeated.current),
          "a repeated rule slot constrains distinct caller variables without a cycle");
    if (slot_repeated_ready)
        bindings_builder_free(&slot_repeated);

    Atom *slot_wrap = atom_expr2(
        &arena, atom_symbol(&arena, "SlotWrap"), slot_rule);
    Atom *slot_cycle_rule = atom_expr3(
        &arena, atom_symbol(&arena, "SlotCycle"),
        slot_rule, slot_wrap);
    Atom *slot_cycle_call = atom_expr3(
        &arena, atom_symbol(&arena, "SlotCycle"),
        slot_outer, slot_outer);
    BindingsBuilder slot_cycle;
    bool slot_cycle_ready = bindings_builder_init(&slot_cycle, NULL);
    CHECK(slot_cycle_ready &&
              !match_atoms_epoch_builder_rule_local(
                  slot_cycle_call, slot_cycle_rule,
                  &slot_cycle, &arena, slot_epoch) &&
              !bindings_has_loop(&slot_cycle.current),
          "equation-activation-frame orientation refuses a cyclic substitution at the bind");
    if (slot_cycle_ready)
        bindings_builder_free(&slot_cycle);

    uint32_t fresh_before_null = 0u;
    uint32_t fresh_after_null = 0u;
    CHECK(cetta_frame_identity_scope_try(&frame_identity_scope, &fresh_before_null) &&
              fresh_before_null != 0u &&
              !cetta_frame_identity_scope_try(&frame_identity_scope, NULL) &&
              cetta_frame_identity_scope_try(&frame_identity_scope, &fresh_after_null) &&
              fresh_after_null != fresh_before_null,
          "frame identity refusal does not consume or alias an identity");
    CettaFrameIdentity retired_identity = 0u;
    bool identity_reuse_ok = cetta_frame_identity_acquire(&retired_identity);
    uint32_t retired_handle = cetta_frame_handle(retired_identity);
    cetta_frame_identity_release(retired_identity);
    bool handle_retired = false;
    for (uint32_t generation = 0u;
         identity_reuse_ok && generation <= CETTA_FRAME_GENERATION_MAX;
         generation++) {
        CettaFrameIdentity next_identity = 0u;
        identity_reuse_ok = cetta_frame_identity_acquire(&next_identity) &&
            next_identity != retired_identity &&
            !cetta_frame_identity_retain(retired_identity);
        handle_retired = cetta_frame_handle(next_identity) != retired_handle;
        cetta_frame_identity_release(next_identity);
        if (handle_retired)
            break;
    }
    CHECK(identity_reuse_ok && handle_retired,
          "frame reuse rejects stale generations and retires before wrapping");

    Atom *epoch_cycle_source = atom_var_with_id(
        &arena, "epoch-cycle-source", test_id(7210u));
    Atom *epoch_cycle_bound = atom_var_with_id(
        &arena, "epoch-cycle-bound",
        var_epoch_id(epoch_cycle_source->var_id, 23u));
    Atom *epoch_cycle_tail = atom_var_with_id(
        &arena, "epoch-cycle-tail", test_id(7211u));
    Atom *epoch_cycle_payload = atom_expr2(
        &arena, atom_symbol(&arena, "EpochCycle"), epoch_cycle_bound);
    Bindings epoch_cycle;
    bindings_init(&epoch_cycle);
    Atom *epoch_applied = NULL;
    CHECK(bindings_add_var(&epoch_cycle, epoch_cycle_bound,
                           epoch_cycle_tail) &&
              !bindings_add_var(&epoch_cycle, epoch_cycle_tail,
                                epoch_cycle_payload) &&
              !bindings_has_loop(&epoch_cycle) &&
              (epoch_applied = bindings_apply_epoch(
                   &epoch_cycle, &arena, epoch_cycle_source, 23u)) != NULL &&
              epoch_applied->kind == ATOM_VAR &&
              epoch_applied->var_id == epoch_cycle_tail->var_id,
          "a closing epoch bind is refused and epoch application stops at the open tail");
    bindings_free(&epoch_cycle);

    bool original_unchanged =
        bindings_lookup_value_id(&base, branch_a).skeleton == NULL &&
        binding_is_int(&base, late_id, 1000);
    CHECK(original_unchanged,
          "copy-on-write index mutation leaves the source clone unchanged");

    Bindings removed;
    bool removed_ok = bindings_clone(&removed, &base);
    VarId removed_id = test_id(31u);
    removed_ok = removed_ok &&
                 bindings_remove_entry_at(&removed, 31u) &&
                 bindings_lookup_value_id(&removed, removed_id).skeleton == NULL &&
                 binding_is_int(&removed, test_id(30u), 30) &&
                 binding_is_int(&removed, test_id(32u), 32) &&
                 binding_is_int(&base, removed_id, 31);
    CHECK(removed_ok,
          "structural removal rebuilds derived lookup state without aliasing");

    Atom *cycle_x = atom_var(&arena, "incremental-cycle-x");
    Atom *cycle_y = atom_var(&arena, "incremental-cycle-y");
    Atom *cycle_payload = atom_expr2(
        &arena, atom_symbol(&arena, "Cycle"), cycle_x);
    Bindings cycle;
    bindings_init(&cycle);
    CHECK(bindings_add_var(&cycle, cycle_x, cycle_y) &&
              !bindings_has_loop(&cycle),
          "incremental occurs summary accepts an acyclic edge");
    CHECK(!bindings_add_var(&cycle, cycle_y, cycle_payload) &&
              !bindings_has_loop(&cycle),
          "incremental occurs summary refuses a transitive cycle at the bind");
    Atom *cyclic_result = bindings_apply(&cycle, &arena, cycle_x);
    CHECK(cyclic_result && cyclic_result->kind == ATOM_VAR &&
              cyclic_result->var_id == cycle_y->var_id,
          "application after a refused bind stops at the open variable");
    CHECK(cycle.len == 1u && !bindings_has_loop(&cycle),
          "the refused edge leaves the single acyclic edge in place");

    Atom *acyclic_tail = atom_var(&arena, "apply-summary-tail");
    Atom *acyclic_head = atom_var(&arena, "apply-summary-head");
    Atom *acyclic_leaf = atom_symbol(&arena, "ApplySummaryLeaf");
    Atom *acyclic_pair = atom_expr2(&arena, acyclic_head, acyclic_head);
    Bindings acyclic_apply;
    bindings_init(&acyclic_apply);
    Atom *acyclic_result = NULL;
    CHECK(bindings_add_var(&acyclic_apply, acyclic_head, acyclic_tail) &&
              bindings_add_var(&acyclic_apply, acyclic_tail, acyclic_leaf) &&
              !bindings_has_loop(&acyclic_apply) &&
              (acyclic_result = bindings_apply(
                   &acyclic_apply, &arena, acyclic_pair)) &&
              acyclic_result->kind == ATOM_EXPR &&
              acyclic_result->expr.len == 2u &&
              acyclic_result->expr.elems[0] == acyclic_leaf &&
              acyclic_result->expr.elems[1] == acyclic_leaf,
          "acyclicity summary erases only redundant path tracking");
    bindings_free(&acyclic_apply);

    Atom *memo_vars[96];
    Bindings indexed_memo_apply;
    bindings_init(&indexed_memo_apply);
    bool indexed_memo_built = true;
    for (uint32_t i = 0u; i < 96u; i++) {
        char name[32];
        snprintf(name, sizeof(name), "indexed-memo-%u", i);
        memo_vars[i] = atom_var(&arena, name);
    }
    for (uint32_t i = 0u; i < 96u; i++) {
        Atom *value = i + 1u < 96u ? memo_vars[i + 1u] : acyclic_leaf;
        indexed_memo_built = indexed_memo_built &&
            bindings_add_var(&indexed_memo_apply, memo_vars[i], value);
    }
    Atom *indexed_memo_pair = atom_expr2(
        &arena, memo_vars[0], memo_vars[0]);
    Atom *indexed_memo_result = indexed_memo_built
        ? bindings_apply(&indexed_memo_apply, &arena, indexed_memo_pair)
        : NULL;
    CHECK(indexed_memo_result &&
              indexed_memo_result->kind == ATOM_EXPR &&
              indexed_memo_result->expr.len == 2u &&
              indexed_memo_result->expr.elems[0] == acyclic_leaf &&
              indexed_memo_result->expr.elems[1] == acyclic_leaf,
          "indexed substitution memo preserves a long shared chain");
    bindings_free(&indexed_memo_apply);

    Bindings indexed_memo_cycle;
    bindings_init(&indexed_memo_cycle);
    bool indexed_cycle_built = true;
    for (uint32_t i = 0u; i + 1u < 96u; i++) {
        indexed_cycle_built = indexed_cycle_built &&
            bindings_add_var(
                &indexed_memo_cycle, memo_vars[i], memo_vars[i + 1u]);
    }
    Atom *indexed_cycle_tail = atom_expr2(
        &arena, atom_symbol(&arena, "IndexedCycle"), memo_vars[0]);
    bool indexed_cycle_refused = indexed_cycle_built &&
        !bindings_add_var(
            &indexed_memo_cycle, memo_vars[95u], indexed_cycle_tail);
    Atom *indexed_cycle_result = indexed_cycle_refused
        ? bindings_apply(&indexed_memo_cycle, &arena, memo_vars[0])
        : NULL;
    CHECK(indexed_cycle_result && indexed_cycle_result->kind == ATOM_VAR &&
              indexed_cycle_result->var_id == memo_vars[95u]->var_id &&
              !bindings_has_loop(&indexed_memo_cycle),
          "indexed substitution memo refuses the long closing edge and applies to the open tail");
    bindings_free(&indexed_memo_cycle);

    Atom *reach_rollback_predecessor = atom_var(
        &arena, "single-reach-rollback-predecessor");
    Atom *reach_rollback_terminal = atom_var(
        &arena, "single-reach-rollback-terminal");
    BindingsBuilder reach_rollback;
    bool reach_rollback_initialized =
        bindings_builder_init(&reach_rollback, NULL);
    bool reach_rollback_ready = reach_rollback_initialized;
    for (uint32_t i = 0u; reach_rollback_ready && i < 23u; i++) {
        reach_rollback_ready = bindings_builder_add_var_fresh(
            &reach_rollback, memo_vars[i], memo_vars[i + 1u]);
    }
    reach_rollback_ready = reach_rollback_ready &&
        bindings_builder_add_var_fresh(
            &reach_rollback, reach_rollback_predecessor,
            reach_rollback_terminal);
    uint32_t reach_rollback_mark =
        bindings_builder_save(&reach_rollback);
    bool first_suffix_acyclic = reach_rollback_ready &&
        bindings_builder_add_var_fresh(
            &reach_rollback, memo_vars[23u], acyclic_leaf) &&
        bindings_builder_add_var_fresh(
            &reach_rollback, reach_rollback_terminal,
            memo_vars[0u]) &&
        !bindings_has_loop(&reach_rollback.current);
    size_t reach_cache_support = 0u;
    size_t reach_cache_capacity = 0u;
    bool reach_cache_support_recorded = !lookup_index_expected ||
        (bindings_lookup_index_test_single_cache_support(
             &reach_rollback.current, &reach_cache_support,
             &reach_cache_capacity) &&
         reach_cache_support > 0u &&
         reach_cache_support < reach_cache_capacity);
    bindings_builder_rollback(
        &reach_rollback, reach_rollback_mark);
    bool reach_cache_support_cleared = !lookup_index_expected ||
        (bindings_lookup_index_test_single_cache_support(
             &reach_rollback.current, &reach_cache_support,
             &reach_cache_capacity) &&
         reach_cache_support == 0u);
    bool replacement_suffix_refused = first_suffix_acyclic &&
        bindings_builder_add_var_fresh(
            &reach_rollback, memo_vars[23u],
            reach_rollback_terminal) &&
        !bindings_builder_add_var_fresh(
            &reach_rollback, reach_rollback_terminal,
            memo_vars[0u]) &&
        !bindings_has_loop(&reach_rollback.current);
    CHECK(reach_cache_support_recorded &&
              reach_cache_support_cleared &&
              replacement_suffix_refused,
          "rollback invalidates exactly recorded single-support roots before suffix reuse");
    if (reach_rollback_initialized)
        bindings_builder_free(&reach_rollback);

    Atom *shared_reach_vars[18];
    Bindings shared_reach_base;
    bindings_init(&shared_reach_base);
    bool shared_reach_ready = true;
    for (uint32_t i = 0u; i < 18u; i++) {
        char name[40];
        snprintf(name, sizeof(name), "shared-reach-%u", i);
        shared_reach_vars[i] = atom_var(&arena, name);
        shared_reach_ready = shared_reach_ready &&
            shared_reach_vars[i] != NULL;
    }
    for (uint32_t i = 0u; shared_reach_ready && i + 1u < 18u; i++) {
        shared_reach_ready = bindings_add_var(
            &shared_reach_base,
            shared_reach_vars[i], shared_reach_vars[i + 1u]);
    }
    shared_reach_ready = shared_reach_ready &&
        bindings_lookup_value_id(&shared_reach_base, shared_reach_vars[0]->var_id).skeleton != NULL;
    Bindings shared_reach_cycle;
    Bindings shared_reach_safe;
    bindings_init(&shared_reach_cycle);
    bindings_init(&shared_reach_safe);
    bool shared_cycle_ready = shared_reach_ready &&
        bindings_clone(&shared_reach_cycle, &shared_reach_base);
    bool shared_safe_ready = shared_reach_ready &&
        bindings_clone(&shared_reach_safe, &shared_reach_base);
    bool shared_cycle_refused = shared_cycle_ready &&
        !bindings_add_var(
            &shared_reach_cycle,
            shared_reach_vars[17u], shared_reach_vars[0u]);
    Atom *shared_reach_outside = atom_var(
        &arena, "shared-reach-outside");
    shared_safe_ready = shared_safe_ready && shared_reach_outside &&
        bindings_add_var(
            &shared_reach_safe,
            shared_reach_vars[17u], shared_reach_outside);
    CHECK(shared_cycle_refused &&
              !bindings_has_loop(&shared_reach_cycle),
          "a read-only shared reachability index still refuses a closing cycle at the bind");
    CHECK(shared_safe_ready &&
              !bindings_has_loop(&shared_reach_safe) &&
              !bindings_has_loop(&shared_reach_base),
          "a read-only shared reachability index preserves acyclic siblings");
    bindings_free(&shared_reach_cycle);
    bindings_free(&shared_reach_safe);
    bindings_free(&shared_reach_base);

    BindingsBuilder cycle_branch;
    CHECK(bindings_builder_init(&cycle_branch, &cycle),
          "cycle rollback branch starts from the acyclic summary");
    uint32_t cycle_mark = bindings_builder_save(&cycle_branch);
    CHECK(!bindings_builder_add_var_fresh(
              &cycle_branch, cycle_y, cycle_payload) &&
              !bindings_has_loop(&cycle_branch.current),
          "speculative insertion of a closing edge is refused");
    bindings_builder_rollback(&cycle_branch, cycle_mark);
    CHECK(!bindings_has_loop(&cycle_branch.current) &&
              bindings_lookup_value_id(&cycle_branch.current, cycle_y->var_id).skeleton == NULL,
          "rollback restores the prior acyclicity summary");
    bindings_builder_free(&cycle_branch);
    bindings_free(&cycle);

    Bindings rewritten;
    bool rewrite_ok = bindings_clone(&rewritten, &base) &&
                      bindings_prepare_logical_write(&rewritten);
    VarId rewritten_old = rewrite_ok
        ? rewritten.entries[12u].var_id : VAR_ID_NONE;
    VarId rewritten_new = test_id(4000u);
    rewrite_ok = rewrite_ok &&
                 bindings_lookup_value_id(&rewritten, rewritten_old).skeleton != NULL;
    if (rewrite_ok)
        rewritten.entries[12u].var_id = rewritten_new;
    bindings_invalidate_after_key_rewrite(&rewritten);
    rewrite_ok = rewrite_ok &&
                 bindings_lookup_value_id(&rewritten, rewritten_old).skeleton == NULL &&
                 binding_is_int(&rewritten, rewritten_new, 12);
    CHECK(rewrite_ok,
          "key rewrites invalidate every derived lookup summary");
    rewritten.entries[12u].var_id = variant_shape_slot_id(12u);
    bindings_invalidate_after_key_rewrite(&rewritten);
    CHECK(bindings_contains_private_variant_slots(&rewritten),
          "key rewrite recomputes private-slot metadata");
    bindings_free(&rewritten);

    Bindings private_value;
    bindings_init(&private_value);
    Atom *private_var = atom_var_with_id(
        &arena, "private-slot", variant_shape_slot_id(41u));
    Atom *nested_private = atom_expr2(
        &arena, atom_symbol(&arena, "Nested"), private_var);
    CHECK(bindings_add_id(
              &private_value, test_id(4100u), SYMBOL_ID_NONE,
              nested_private) &&
              bindings_contains_private_variant_slots(&private_value),
          "nested private values update derived metadata");
    BindingsBuilder private_value_branch;
    CHECK(bindings_builder_init(
              &private_value_branch, &private_value),
          "private-entry rollback branch clones derived metadata");
    uint32_t private_value_mark =
        bindings_builder_save(&private_value_branch);
    bool private_value_rollback =
        bindings_builder_add_id_fresh(
            &private_value_branch, test_id(4101u), SYMBOL_ID_NONE,
            atom_int(&arena, 4101));
    bindings_builder_rollback(
        &private_value_branch, private_value_mark);
    CHECK(private_value_rollback &&
              private_value_branch.current.private_entry_count == 1u &&
              bindings_contains_private_variant_slots(
                  &private_value_branch.current),
          "rollback rebuilds nonzero private-entry metadata exactly");
    bindings_builder_free(&private_value_branch);
    CHECK(bindings_remove_entry_at(&private_value, 0u) &&
              !bindings_contains_private_variant_slots(&private_value),
          "entry removal decrements private-slot metadata");
    bindings_free(&private_value);

    {
        CETTA_FRAME_IDENTITY_SCOPE(identities);
        CettaFrameIdentity identity = cetta_frame_identity_scope_fresh(&identities);
        VarId key = var_epoch_id(1u, identity);
        BindingsBuilder slots;
        Bindings retained;
        CHECK(bindings_builder_init(&slots, NULL), "private slot fixture initialization");
        uint32_t mark = bindings_builder_save(&slots);
        CHECK(bindings_builder_add_id_fresh(&slots, key, SYMBOL_ID_NONE, nested_private) &&
              slots.current.len == 0u && bindings_contains_private_variant_slots(&slots.current) &&
              bindings_clone(&retained, &slots.current),
              "private payloads live in framed slots and survive branch retention");
        bindings_builder_rollback(&slots, mark);
        CHECK(!bindings_has_bound_values(&slots.current) &&
              !bindings_contains_private_variant_slots(&slots.current) &&
              bindings_contains_private_variant_slots(&retained),
              "rollback removes private slot state without altering a retained branch");
        CHECK(bindings_rewrite_value_id(&retained, key, binding_value_from_atom(atom_int(&arena, 9))) &&
              !bindings_contains_private_variant_slots(&retained),
              "rewriting the last private payload clears its slot summary");
        bindings_free(&retained);
        bindings_builder_free(&slots);

        Bindings imported;
        bindings_init(&imported);
        CHECK(bindings_add_id(&imported, test_id(4120u), SYMBOL_ID_NONE, atom_int(&arena, 1)) &&
              bindings_add_id(&imported, test_id(4121u), SYMBOL_ID_NONE, atom_int(&arena, 2)) &&
              bindings_prepare_logical_write(&imported), "prepare external key translation");
        imported.entries[0].var_id = key;
        imported.entries[1].var_id = key;
        CHECK(bindings_invalidate_after_key_rewrite(&imported) && imported.len == 0u &&
              binding_is_int(&imported, key, 2),
              "external key translation consumes framed rows into one newest slot value");
        CHECK(!bindings_add_id(&imported, key, SYMBOL_ID_NONE, atom_int(&arena, 3)) &&
              binding_is_int(&imported, key, 2),
              "ordinary framed insertion rejects an inconsistent duplicate");
        bindings_free(&imported);
    }

    Bindings private_constraint;
    bindings_init(&private_constraint);
    Atom *ordinary_var = atom_var(&arena, "ordinary-constraint-var");
    Atom *left_constraint = atom_expr2(
        &arena, atom_symbol(&arena, "Left"), private_var);
    Atom *right_constraint = atom_expr2(
        &arena, atom_symbol(&arena, "Right"), ordinary_var);
    CHECK(bindings_add_constraint(
              &private_constraint, left_constraint, right_constraint) &&
              bindings_contains_private_variant_slots(&private_constraint),
          "private variables inside constraints update derived metadata");
    BindingsBuilder private_constraint_branch;
    CHECK(bindings_builder_init(
              &private_constraint_branch, &private_constraint),
          "private-constraint rollback branch clones derived metadata");
    uint32_t private_constraint_mark =
        bindings_builder_save(&private_constraint_branch);
    bool private_constraint_rollback =
        bindings_builder_add_id_fresh(
            &private_constraint_branch, test_id(4200u), SYMBOL_ID_NONE,
            atom_int(&arena, 4200));
    bindings_builder_rollback(
        &private_constraint_branch, private_constraint_mark);
    CHECK(private_constraint_rollback &&
              private_constraint_branch.current.private_constraint_count ==
                  1u &&
              bindings_contains_private_variant_slots(
                  &private_constraint_branch.current),
          "rollback rebuilds nonzero private-constraint metadata exactly");
    BindingsBuilder private_constraint_clone;
    BindingConstraint *shared_constraints =
        private_constraint_branch.current.constraints;
    Atom *source_constraint_lhs =
        private_constraint_branch.current.constraints[0].lhs.skeleton;
    Arena constraint_clone_owner;
    arena_init(&constraint_clone_owner);
    bool private_constraint_clone_ready = bindings_builder_clone(
        &private_constraint_clone, &private_constraint_branch);
    bool private_constraint_detached =
        private_constraint_clone_ready &&
        private_constraint_clone.current.constraints == shared_constraints &&
        bindings_builder_promote_atoms_to_arena(
            &private_constraint_clone, &constraint_clone_owner) &&
        private_constraint_clone.current.constraints != shared_constraints &&
        private_constraint_branch.current.constraints == shared_constraints &&
        private_constraint_branch.current.constraints[0].lhs.skeleton ==
            source_constraint_lhs &&
        arena_owns_ptr(
            &constraint_clone_owner,
            private_constraint_clone.current.constraints[0].lhs.skeleton);
    CHECK(private_constraint_detached,
          "constraint promotion detaches one fork and preserves its sibling");
    if (private_constraint_clone_ready)
        bindings_builder_free(&private_constraint_clone);
    arena_free(&constraint_clone_owner);
    bindings_builder_free(&private_constraint_branch);
    bindings_free(&private_constraint);

    Arena promoted_arena;
    arena_init(&promoted_arena);
    Atom *shared_items[2] = {
        atom_int(&arena, 7),
        atom_int(&arena, 11),
    };
    Atom *shared_value = atom_expr(&arena, shared_items, 2u);
    Bindings shared;
    bindings_init(&shared);
    bool shared_built =
        bindings_add_id(
            &shared, test_id(3000u), SYMBOL_ID_NONE, shared_value) &&
        bindings_add_id(
            &shared, test_id(3001u), SYMBOL_ID_NONE, shared_value);
    CHECK(shared_built &&
              bindings_promote_logical_atoms_to_arena(
                  &shared, &promoted_arena) &&
              bindings_entry_at(&shared, 0)->value.skeleton ==
                  bindings_entry_at(&shared, 1)->value.skeleton &&
              arena_owns_ptr(&promoted_arena,
                             bindings_entry_at(&shared, 0)->value.skeleton),
          "one promotion session preserves shared DAG identity");
    Atom *promoted_once = bindings_entry_at(&shared, 0)->value.skeleton;
    CHECK(bindings_promote_logical_atoms_to_arena(
              &shared, &promoted_arena) &&
              bindings_entry_at(&shared, 0)->value.skeleton == promoted_once &&
              bindings_entry_at(&shared, 1)->value.skeleton == promoted_once,
          "promotion reuses a destination-owned graph");

    Bindings occurrence_left;
    Bindings occurrence_right;
    Bindings occurrence_clone;
    Bindings occurrence_join;
    bindings_init(&occurrence_left);
    bindings_init(&occurrence_right);
    bindings_init(&occurrence_clone);
    bindings_init(&occurrence_join);
    CHECK(bindings_refresh_occurrence_token(&occurrence_left) &&
              bindings_refresh_occurrence_token(&occurrence_right) &&
              bindings_occurrence_token(&occurrence_left) != 0u &&
              bindings_occurrence_token(&occurrence_right) != 0u &&
              bindings_occurrence_token(&occurrence_left) !=
                  bindings_occurrence_token(&occurrence_right) &&
              !bindings_eq(&occurrence_left, &occurrence_right),
          "distinct receipt-free derivations retain distinct occurrences");
    CHECK(bindings_clone(&occurrence_clone, &occurrence_left) &&
              bindings_occurrence_token(&occurrence_clone) ==
                  bindings_occurrence_token(&occurrence_left) &&
              bindings_eq(&occurrence_clone, &occurrence_left),
          "cloning preserves one exact occurrence identity");
    CHECK(bindings_clone_merge(
              &occurrence_join, &occurrence_left, &occurrence_right) &&
              bindings_occurrence_token(&occurrence_join) != 0u &&
              bindings_occurrence_token(&occurrence_join) !=
                  bindings_occurrence_token(&occurrence_left) &&
              bindings_occurrence_token(&occurrence_join) !=
                  bindings_occurrence_token(&occurrence_right),
          "joining distinct occurrences allocates one fresh union identity");
    BindingsBuilder occurrence_branch;
    bool occurrence_branch_ready = bindings_builder_init(
        &occurrence_branch, &occurrence_left);
    uint32_t occurrence_mark = occurrence_branch_ready
        ? bindings_builder_save(&occurrence_branch) : 0u;
    uint64_t occurrence_left_token =
        bindings_occurrence_token(&occurrence_left);
    bool occurrence_rollback_exact = occurrence_branch_ready &&
        bindings_builder_try_merge(
            &occurrence_branch, &occurrence_right) &&
        bindings_occurrence_token(&occurrence_branch.current) !=
            occurrence_left_token;
    if (occurrence_rollback_exact) {
        bindings_builder_rollback(&occurrence_branch, occurrence_mark);
        occurrence_rollback_exact =
            bindings_occurrence_token(&occurrence_branch.current) ==
                occurrence_left_token;
    }
    CHECK(occurrence_rollback_exact,
          "rollback restores the exact pre-branch occurrence identity");
    if (occurrence_branch_ready)
        bindings_builder_free(&occurrence_branch);
    bindings_free(&occurrence_join);
    bindings_free(&occurrence_clone);
    bindings_free(&occurrence_right);
    bindings_free(&occurrence_left);

    VarId fresh_id_before_null = VAR_ID_NONE;
    VarId fresh_id_after_null = VAR_ID_NONE;
    CHECK(fresh_var_id_try(&fresh_id_before_null) &&
              fresh_id_before_null != VAR_ID_NONE &&
              !fresh_var_id_try(NULL) &&
              fresh_var_id_try(&fresh_id_after_null) &&
              fresh_id_after_null == fresh_id_before_null + 1u,
          "fresh identity refusal does not consume or alias an identity");
#ifdef CETTA_TEST_HOOKS
    VarId penultimate_id = VAR_ID_NONE;
    VarId final_id = VAR_ID_NONE;
    VarId exhausted_id_sentinel = (VarId)7305u;
    fresh_var_id_test_reset((uint64_t)UINT32_MAX - 1u);
    CHECK(fresh_var_id_try(&penultimate_id) &&
              penultimate_id == (VarId)UINT32_MAX - 1u &&
              fresh_var_id_try(&final_id) &&
              final_id == (VarId)UINT32_MAX &&
              !fresh_var_id_try(&exhausted_id_sentinel) &&
              exhausted_id_sentinel == (VarId)7305u,
          "fresh identity allocation reaches the boundary then fails closed");
    fresh_var_id_test_reset(1u);
#endif

    printf("(BindingsLookupIndexSummary %u %u %u)\n",
           passed + failed, passed, failed);

    bindings_free(&shared);
    bindings_free(&modern);
    arena_free(&promoted_arena);
    bindings_free(&removed);
    bindings_free(&clone);
    bindings_free(&base);
    arena_free(&arena);
    bindings_thread_cache_free();
    g_var_intern = NULL;
    var_intern_free(&var_intern);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return failed == 0u ? 0 : 1;
}
