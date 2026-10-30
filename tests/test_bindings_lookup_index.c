#include "atom.h"
#include "match.h"
#include "binding/slot_store_internal.h"
#include "term_canon.h"
#include "stats.h"
#include "term_universe.h"
#include "variant_shape.h"
#include "binding/slot_store_internal.h"
#include "tests/test_runtime_stats_stubs.h"

#include <math.h>
#include <pthread.h>
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
    return var_id_from_base(UINT64_C(0x10000000) +
                            (uint64_t)ordinal * UINT64_C(0x10001));
}

/* Exercise both logical merge routes without an evaluator linked in. Scope
 * selection, old receipt data and rollback belong to the common product. */
static void test_prime_occurrence_product(Arena *arena, unsigned mode, bool live) {
    Bindings source, destination, before, refused;
    bindings_init(&source); bindings_init(&destination);
    bindings_init(&before); bindings_init(&refused);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedRecording *group = NULL;
    PrimeNeedRecordingScope outer = {0}, inner = {0};
    PrimeNeedReceipt receipt = {0}, empty_slice = {0};
    bool demoted = false;
#else
    (void)mode;
#endif
    PrimeNeedSnapshot initial = {0}, allocated;
    uint64_t cell_id = 0u;
    Atom *origin = atom_int(arena, 7);
    StateCell *cell = arena_alloc(arena, sizeof(*cell));
    *cell = (StateCell){.value = atom_int(arena, 11)};
    PrimeNeedBranchState branch = {0};
    CettaWeightLedger weights = {0};
    bool prepared = prime_need_snapshot_begin(&initial) &&
        prime_need_snapshot_allocate(arena, &initial, origin, &allocated, &cell_id) &&
        prime_need_branch_state_write(arena, &branch, cell, cell->value,
            atom_int(arena, 19), &branch) &&
        cetta_weight_ledger_append(arena, &weights, 201u, 203u,
            atom_int(arena, 17), &weights) &&
        bindings_add_id(&destination, test_id(7700u), SYMBOL_ID_NONE, atom_int(arena, 14)) &&
        bindings_refresh_occurrence_token(&source) &&
        bindings_refresh_occurrence_token(&destination);
    CHECK(prepared, "complete occurrence-product merge fixture");
    if (!prepared) goto done;
    *bindings_need_mut(&source) = allocated;
    *bindings_branch_state_mut(&source) = branch;
    *bindings_weight_mut(&source) = weights;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (mode) {
        group = prime_need_recording_new((PrimeNeedRecordingLimits){16u, 65536u, 1u, 16u});
        prepared = group && prime_need_recording_reserve(group, NULL, 16u, 65536u, &receipt) &&
            prime_need_receipt_observe_cell(arena, &receipt, allocated.session_id,
                cell_id, origin, &receipt);
        CHECK(prepared, "recorded source context exists before merge");
        if (!prepared) goto done;
        *bindings_receipt_mut(&source) = receipt;
        if (mode == 1u || mode == 3u) prime_need_recording_enter(&outer, &receipt);
        if (mode == 3u) prime_need_recording_enter(&inner, &empty_slice);
        if (mode == 4u) {
            *bindings_receipt_mut(&destination) = receipt;
            prime_need_recording_demote(group);
            demoted = true;
        }
    }
#endif
    uint64_t left = bindings_occurrence_token(&destination);
    uint64_t right = bindings_occurrence_token(&source);
    bool merged = live ? bindings_try_merge_live(&destination, &source)
                       : bindings_try_merge(&destination, &source);
    PrimeNeedCellView view;
    Atom *state_value = NULL;
    const CettaWeightLedger *actual_weights = bindings_weight_view(&destination);
    CHECK(merged && prime_need_snapshot_lookup(bindings_need_view(&destination), cell_id, &view) &&
        view.origin == origin && prime_need_branch_state_value(
            bindings_branch_state_view(&destination), cell, &state_value) &&
        atom_eq(state_value, atom_int(arena, 19)) && atom_eq(cell->value, atom_int(arena, 11)) &&
        actual_weights->length == 1u && actual_weights->last->occurrence == 201u &&
        atom_eq(actual_weights->last->value, atom_int(arena, 17)) &&
        bindings_occurrence_token(&destination) != 0u &&
        bindings_occurrence_token(&destination) != left &&
        bindings_occurrence_token(&destination) != right,
        "merge retains Need, shadow state, weights and exact union identity");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    CHECK(prime_need_receipt_present(bindings_receipt_view(&destination)) ==
        (mode == 1u || mode == 2u),
        "scope refusal and erasure stay distinct from carried history with recording off");
    if (mode == 3u) {
        prime_need_recording_leave(&inner);
        merged = live ? bindings_try_merge_live(&destination, &source)
                      : bindings_try_merge(&destination, &source);
        CHECK(merged && prime_need_receipt_present(bindings_receipt_view(&destination)),
            "leaving refused inner scope restores the standalone merge operation");
    }
#endif
    prepared = bindings_clone(&before, &destination) &&
        prime_need_snapshot_begin(bindings_need_mut(&refused));
    CHECK(prepared, "refused world merge fixture");
    if (!prepared) goto done;
    merged = live ? bindings_try_merge_live(&destination, &refused)
                  : bindings_try_merge(&destination, &refused);
    CHECK(!merged && bindings_eq(&destination, &before),
        "world refusal preserves the complete existing occurrence product");
    bindings_free(&refused); bindings_init(&refused);
    prepared = bindings_clone(&refused, &source) &&
        bindings_add_id(&refused, test_id(7700u), SYMBOL_ID_NONE, atom_int(arena, 15));
    CHECK(prepared, "logical refusal after product merge fixture");
    if (!prepared) goto done;
    merged = live ? bindings_try_merge_live(&destination, &refused)
                  : bindings_try_merge(&destination, &refused);
    CHECK(!merged && bindings_eq(&destination, &before),
        "logical refusal rolls back all merged product components");
done:
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    prime_need_recording_leave(&inner);
    prime_need_recording_leave(&outer);
#endif
    bindings_free(&refused); bindings_free(&before);
    bindings_free(&destination); bindings_free(&source);
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (group) {
        if (demoted) prime_need_recording_carrier_release(group);
        else prime_need_recording_release(group);
    }
#endif
}


static void test_variable_inventory(Arena *arena) {
    CettaVarMap map = {0};
    Atom *values[512];
    bool ok = true;
    for (uint32_t i = 0u; i < 512u; i++) {
        values[i] = atom_var_with_id(arena, "same-spelling", test_id(i + 4000u));
        ok = ok && values[i] && cetta_var_map_add(&map, test_id(i), values[i]);
    }
    for (uint32_t i = 0u; i < 512u; i++)
        ok = ok && cetta_var_map_lookup(&map, test_id(i)) == values[i] &&
            var_id_eq(map.items[i].source_id, test_id(i));
    CHECK(ok && map.len == 512u,
          "indexed inventory retains distinct identities and first-appearance order");
    CHECK(cetta_var_map_add(&map, test_id(17u), values[17]) &&
              !cetta_var_map_add(&map, test_id(17u), values[18]) &&
              map.len == 512u &&
              cetta_var_map_lookup(&map, test_id(17u)) == values[17],
          "indexed map preserves identical insertion and rejects a changed payload");
    CettaVarMap copy = {0};
    ok = cetta_var_map_clone(&copy, &map);
    for (uint32_t i = 0u; i < 512u; i++)
        ok = ok && cetta_var_map_lookup(&copy, test_id(i)) == values[i];
    cetta_var_map_free(&map);
    CHECK(ok && cetta_var_map_lookup(&copy, test_id(511u)) == values[511],
          "cloned inventories own independent indexes and retain every payload");

    copy.len = 300u;
    CHECK(cetta_var_map_lookup(&copy, test_id(511u)) == NULL,
          "truncated inventory refuses an unconfirmed stale position");
    ok = true;
    for (uint32_t i = 300u; i < 512u; i++)
        ok = ok && cetta_var_map_add(&copy, test_id(i + 1000u), values[i]);
    for (uint32_t i = 300u; i < 512u; i++)
        ok = ok && !cetta_var_map_lookup(&copy, test_id(i)) &&
            cetta_var_map_lookup(&copy, test_id(i + 1000u)) == values[i];
    CHECK(ok, "refilled inventory checks stale keys against the current records");
    copy.len = 0u;
    ok = true;
    for (uint32_t i = 0u; i < 512u; i++)
        ok = ok && cetta_var_map_add(&copy, test_id(511u - i), values[i]);
    for (uint32_t i = 0u; i < 512u; i++)
        ok = ok && cetta_var_map_lookup(&copy, test_id(511u - i)) == values[i];
    CHECK(ok, "empty-and-refill rebuilds the inventory in its new appearance order");
    cetta_var_map_free(&copy);
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

static void check_binding_batch(Arena *arena, const Bindings *base,
                                Atom **variables, Atom **values,
                                uint32_t count, bool success,
                                const char *message) {
    Bindings reference, batch, owned;
    bool ready = bindings_clone(&reference, base) && bindings_clone(&batch, base) &&
        bindings_clone(&owned, base);
    CHECK(ready, "batch fixture clones its input");
    if (!ready)
        return;
    bool reference_ok = true;
    for (uint32_t i = 0u; i < count && reference_ok; i++)
        reference_ok = bindings_add_var(&reference, variables[i], values[i]);
    bool batch_ok = bindings_add_vars(&batch, variables, values, count);
    bool owned_ok = true;
    for (uint32_t i = 0u; i < count && owned_ok; i++)
        owned_ok = bindings_add_var_owned(&owned, variables[i], values[i]);
    bool equal = reference_ok == success && batch_ok == reference_ok &&
        owned_ok == reference_ok;
    if (batch_ok) {
        equal = equal && bindings_eq(&reference, &batch) &&
            bindings_eq(&reference, &owned) &&
            bindings_occurrence_token(&owned) == bindings_occurrence_token(base) &&
            bindings_occurrence_token(&batch) == bindings_occurrence_token(base);
        for (uint32_t i = 0u; i < count && equal; i++) {
            Atom *left = bindings_apply(&reference, arena, variables[i]);
            Atom *right = bindings_apply(&batch, arena, variables[i]);
            equal = left && right && atom_eq(left, right);
        }
    } else {
        equal = equal && bindings_eq(&batch, (Bindings *)base) &&
            bindings_occurrence_token(&batch) == bindings_occurrence_token(base);
        CHECK(!bindings_has_bound_values(&owned) && owned.eq_len == 0u &&
                  owned.owners == NULL && owned.frame_index == NULL &&
                  !bindings_prime_present(&owned),
              "failed owned construction releases its unpublished image");
    }
    CHECK(equal, message);
    bindings_free(&reference);
    bindings_free(&batch);
    bindings_free(&owned);
}

static void test_binding_batch(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&identities);
    Atom *x = atom_var_with_id(arena, "batch-x", var_epoch_id(var_id_from_base(200u), epoch));
    Atom *y = atom_var_with_id(arena, "batch-y", var_epoch_id(var_id_from_base(100u), epoch));
    Atom *z = atom_var_with_id(arena, "batch-z", fresh_var_id());
    Atom *seven = atom_int(arena, 7);
    Atom *eight = atom_int(arena, 8);
    Atom *tag = atom_symbol(arena, "Batch");
    Bindings base;
    bindings_init(&base);
    CHECK(bindings_refresh_occurrence_token(&base), "batch keeps orthogonal occurrence metadata");
    Atom *vars[] = {x, y, z};
    Atom *vals[] = {y, seven, atom_expr3(arena, tag, x, y)};
    check_binding_batch(arena, &base, vars, vals, 3u, true,
                        "bulk publication preserves cross-frame aliases and metadata");
    Atom *refine_vars[] = {x, x};
    Atom *refine_vals[] = {atom_expr2(arena, tag, y), atom_expr2(arena, tag, seven)};
    check_binding_batch(arena, &base, refine_vars, refine_vals, 2u, true,
                        "bulk repeated writes refine earlier bindings by unification");
    Atom *bad_vals[] = {seven, eight};
    check_binding_batch(arena, &base, refine_vars, bad_vals, 2u, false,
                        "a later conflicting write discards the whole private image");
    Atom *cycle_vars[] = {x, y};
    Atom *cycle_vals[] = {atom_expr2(arena, tag, y), x};
    check_binding_batch(arena, &base, cycle_vars, cycle_vals, 2u, false,
                        "bulk publication rejects indirect occurs cycles without publishing a prefix");
    Atom *reverse_vals[] = {y, x};
    check_binding_batch(arena, &base, cycle_vars, reverse_vals, 2u, true,
                        "bulk reverse aliases retain the sequential no-op rule");
    VarId named_id = fresh_var_id();
    Atom *key_vars[] = {
        atom_var_with_name_key(arena, atom_expr2(arena, tag, seven), named_id),
        atom_var_with_name_key(arena, atom_expr2(arena, tag, eight), named_id),
    };
    Atom *key_vals[] = {seven, seven};
    check_binding_batch(arena, &base, key_vars, key_vals, 2u, false,
                        "equal identifiers with incompatible structural names are rejected");
    CHECK(bindings_add_constraint(&base, atom_expr2(arena, tag, z),
                                  atom_expr2(arena, tag, seven)),
          "batch constraint fixture is admitted");
    Atom *constraint_vars[] = {z};
    Atom *constraint_vals[] = {seven};
    check_binding_batch(arena, &base, constraint_vars, constraint_vals, 1u, true,
                        "bulk writes retain sequential constraint normalization");
    Bindings owned_constraint;
    bool constraint_ready = bindings_clone(&owned_constraint, &base);
    CHECK(constraint_ready &&
              bindings_add_constraint_owned(&owned_constraint, z, seven) &&
              atom_eq(bindings_apply(&owned_constraint, arena, z), seven),
          "owned constraint construction preserves normal unification");
    CHECK(constraint_ready &&
              !bindings_add_constraint_owned(&owned_constraint, z, eight) &&
              !bindings_has_bound_values(&owned_constraint) &&
              owned_constraint.eq_len == 0u && owned_constraint.owners == NULL,
          "conflicting owned constraint discards the unpublished result");
    CHECK(base.eq_len == 1u && bindings_occurrence_token(&base) != 0u,
          "failed private construction preserves its live sibling and occurrence");
    if (constraint_ready)
        bindings_free(&owned_constraint);
    CHECK(bindings_add_vars(&base, NULL, NULL, 0u), "empty publication is an identity");
    bindings_free(&base);
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
    Atom *low = atom_var_with_id(arena, "sparse-low", var_epoch_id(var_id_from_base(100u), epoch));
    Atom *high = atom_var_with_id(arena, "sparse-high", var_epoch_id(var_id_from_base(200u), epoch));
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

static void test_resolved_frame_write_boundaries(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&identities);
    VarId ids[] = {var_id_from_base(101u), var_id_from_base(103u), var_id_from_base(107u)};
    Atom *variables[] = {
        atom_var_with_id(arena, "focus-a", ids[0]),
        atom_var_with_id(arena, "focus-b", ids[1]),
        atom_var_with_id(arena, "focus-c", ids[2])
    };
    BindingsFrameSchema *schema = bindings_frame_schema_new_presented(
        ids, variables, 3u);
    Bindings base;
    bindings_init(&base);
    CHECK(schema && bindings_register_complete_frame_schema(&base, schema, epoch),
          "resolved write fixture registers one presented inventory");
    bindings_frame_schema_release(schema);
    BindingsBuilder left, right;
    bool left_ready = bindings_builder_init(&left, &base);
    bool right_ready = bindings_builder_init(&right, &base);
    CHECK(left_ready && right_ready, "two builders share the initial frame");
    if (!left_ready || !right_ready) {
        if (left_ready) bindings_builder_free(&left);
        if (right_ready) bindings_builder_free(&right);
        bindings_free(&base);
        return;
    }
    Atom *a = atom_var_with_id(arena, "focus-a", var_epoch_id(ids[0], epoch));
    Atom *b = atom_var_with_id(arena, "focus-b", var_epoch_id(ids[1], epoch));
    Atom *c = atom_var_with_id(arena, "focus-c", var_epoch_id(ids[2], epoch));
    test_runtime_stats_reset_counters();
    CHECK(bindings_builder_add_var_fresh(&left, a, atom_int(arena, 11)) &&
              binding_is_int(&left.current, a->var_id, 11) &&
              !bindings_lookup_value_id(&right.current, a->var_id).skeleton &&
              !bindings_lookup_value_id(&base, a->var_id).skeleton,
          "a resolved write detaches without modifying either shared owner");
    CHECK(test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_DIRECTORY_DETACH) == 1u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_DETACH) == 1u,
          "one shared write detaches the directory and frame exactly once");
    CHECK(bindings_builder_add_var_fresh(&left, b, a) &&
              atom_eq(bindings_apply(&left.current, arena, b), atom_int(arena, 11)) &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_DIRECTORY_DETACH) == 1u &&
              test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_STORAGE_DETACH) == 1u,
          "the next private write retains alias meaning without another detach");
    uint32_t mark = bindings_builder_save(&left);
    Bindings captured;
    CHECK(bindings_clone(&captured, &left.current),
          "a captured image shares the frame before a later write");
    CHECK(bindings_builder_add_var_fresh(&left, c, atom_int(arena, 13)) &&
              !bindings_lookup_value_id(&captured, c->var_id).skeleton &&
              !bindings_builder_add_var_fresh(&left, a, atom_int(arena, 99)) &&
              binding_is_int(&left.current, a->var_id, 11),
          "capture and a conflicting bind preserve the prior slot authority");
    bindings_builder_rollback(&left, mark);
    CHECK(!bindings_lookup_value_id(&left.current, c->var_id).skeleton &&
              bindings_builder_add_var_fresh(&right, a, atom_int(arena, 17)) &&
              binding_is_int(&right.current, a->var_id, 17) &&
              binding_is_int(&captured, a->var_id, 11),
          "rollback and a sibling write keep the captured world independent");
    bindings_builder_free(&left);
    bindings_builder_free(&right);
    bindings_free(&base);
    CHECK(binding_is_int(&captured, a->var_id, 11) &&
              atom_eq(bindings_apply(&captured, arena, b), atom_int(arena, 11)),
          "the captured frame and alias survive both builders and their parent");
    bindings_free(&captured);
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
    CHECK(ready && !var_id_eq(first->var_id, second->var_id) &&
              atom_symbols_equal(first, second) &&
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
        if (var_id_eq(builder.current.entries[index].var_id, second->var_id))
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

static bool find_frame_state(const Bindings *bindings, CettaFrameIdentity epoch,
                             BindingsFrameStateView *view) {
    BindingsFrameStateIterator iterator = {.bindings = bindings};
    while (bindings_frame_state_next(&iterator, view))
        if (cetta_frame_identity_eq(view->ref.identity, epoch))
            return true;
    return false;
}

static bool frame_state_slots_are(const Bindings *bindings, CettaFrameIdentity epoch,
                                  const VarId *ids, Atom *first, Atom *second, Atom *third) {
    BindingsFrameStateView view;
    if (!find_frame_state(bindings, epoch, &view) || view.slot_count != 3u ||
        view.schema_slot_count != 3u)
        return false;
    Atom *expected[] = {first, second, third};
    for (uint32_t i = 0u; i < 3u; ++i) {
        BindingsFrameSlotStateView slot;
        if (!bindings_frame_state_read_slot(&view, i, &slot) ||
            !var_id_eq(slot.source_id, ids[i]) ||
            !var_id_eq(slot.var_id, var_epoch_id(ids[i], epoch)) ||
            !slot.declared_in_schema ||
            slot.value.skeleton != expected[i] ||
            (expected[i] && slot.write_version == 0u))
            return false;
    }
    const VarId sentinel_id = var_id_from_base(UINT64_MAX);
    BindingsFrameSlotStateView unchanged = {.source_id = sentinel_id};
    BindingsFrameStateView wrong_generation = view;
    /* Change the generation while preserving the complete 32-bit handle. */
    wrong_generation.ref.identity.word ^= UINT64_C(1) << 32;
    return !bindings_frame_state_read_slot(&view, 3u, &unchanged) &&
        var_id_eq(unchanged.source_id, sentinel_id) &&
        !bindings_frame_state_read_slot(&wrong_generation, 0u, &unchanged) &&
        var_id_eq(unchanged.source_id, sentinel_id) &&
        !bindings_frame_state_read_slot(NULL, 0u, &unchanged) &&
        var_id_eq(unchanged.source_id, sentinel_id);
}

/* Exercise direct promotion, builder promotion and the retained capsule used
 * by native reply/continuation boundaries against the same joined world. */
static void test_joint_world_promotion(unsigned mode) {
    Arena source, survivor;
    arena_init(&source);
    arena_init(&survivor);
    Atom *payload = atom_expr2(&source, atom_symbol(&source, "SharedWorldPayload"),
                              atom_int(&source, 7));
    Atom *independent = atom_expr2(&source, atom_symbol(&source, "SharedWorldPayload"),
                                  atom_int(&source, 7));
    Bindings carrier;
    bindings_init(&carrier);
    StateCell state = {.value = payload};
    PrimeNeedBranchState *branch = bindings_branch_state_mut(&carrier);
    Atom *captured = NULL;
    bool ready = payload && branch && prime_need_branch_state_begin(&source, branch) &&
        prime_need_branch_state_write(&source, branch, &state, payload, payload, branch) &&
        prime_need_branch_state_value(branch, &state, &captured);
    /* State evidence snapshots its input. Share that actual captured node
     * with the logical/Need roots, rather than assuming input pointer reuse. */
    if (ready)
        payload = captured;
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    if (ready)
        *bindings_receipt_mut(&carrier) = *branch;
#endif
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    const CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&identities);
    VarId ids[] = {var_id_from_base(42u), var_id_from_base(43u)};
    ready = ready && independent && payload != independent && atom_eq(payload, independent) &&
        bindings_register_contextual_frame(&carrier, ids, 2u, epoch) &&
        bindings_add_id(&carrier, var_epoch_id(ids[0], epoch), SYMBOL_ID_NONE, payload) &&
        bindings_add_id(&carrier, var_epoch_id(ids[1], epoch), SYMBOL_ID_NONE, independent);
    PrimeNeedSnapshot *need = bindings_need_mut(&carrier);
    uint64_t thunk = 0u;
    ready = ready && need && prime_need_snapshot_begin(need) &&
        prime_need_snapshot_allocate(&source, need, payload, need, &thunk) &&
        prime_need_snapshot_start_evaluation(&source, need, thunk, 17u, need) &&
        prime_need_snapshot_resolve_value(&source, need, thunk, 17u, payload, need);
    uint64_t need_session = need ? need->session_id : 0u;
    Arena split_owner;
    arena_init(&split_owner);
    Bindings split;
    bool split_cloned = ready && bindings_clone(&split, &carrier);
    bool split_promoted = split_cloned &&
        bindings_promote_logical_atoms_to_arena(&split, &split_owner) &&
        prime_need_snapshot_promote(&split_owner, bindings_need_mut(&split)) &&
        prime_need_branch_state_promote(&split_owner, bindings_branch_state_mut(&split));
    BindingsFrameStateView split_frame;
    BindingsFrameSlotStateView split_slot;
    PrimeNeedCellView split_memo;
    CHECK(split_promoted && find_frame_state(&split, epoch, &split_frame) &&
              bindings_frame_state_read_slot(&split_frame, 0u, &split_slot) &&
              prime_need_snapshot_lookup(bindings_need_view(&split), thunk, &split_memo) &&
              split_slot.value.skeleton != split_memo.cached &&
              atom_eq(split_slot.value.skeleton, split_memo.cached),
          "separate physical-copy episodes split a shared node despite equal values");
    if (split_cloned)
        bindings_free(&split);
    arena_free(&split_owner);
    bool promoted = false;
    if (mode == 2u) {
        Arena capsule;
        arena_init_detached(&capsule);
        Atom *saved = ready ? bindings_capture_value(&capsule, &carrier) : NULL;
        Bindings restored;
        promoted = saved && bindings_restore_captured_value(saved, &restored);
        if (promoted) {
            bindings_free(&carrier);
            carrier = restored;
        }
        arena_free(&capsule);
        CHECK(promoted && carrier.owners != NULL,
              "restored joined world retains its capsule after the capsule arena is released");
        Arena observation;
        arena_init_detached(&observation);
        Atom *visible = promoted ? bindings_to_atom(&observation, &carrier) : NULL;
        Bindings structural;
        bindings_init(&structural);
        bool decoded = visible && bindings_from_atom(visible, &structural);
        CHECK(decoded && !bindings_prime_present(&structural) &&
                  bindings_prime_present(&carrier),
              "the structural binding codec is not a retained operational world");
        bindings_free(&structural);
        arena_free(&observation);
    } else if (mode == 1u) {
        BindingsBuilder builder;
        bool initialized = ready && bindings_builder_init(&builder, &carrier);
        promoted = initialized && bindings_builder_promote_atoms_to_arena(&builder, &survivor);
        if (promoted) {
            bindings_free(&carrier);
            bindings_builder_take(&builder, &carrier);
        } else if (initialized) {
            bindings_builder_free(&builder);
        }
    } else {
        promoted = ready && bindings_promote_atoms_to_arena(&carrier, &survivor);
    }
    CHECK(promoted,
          "joint world promotion accepts contextual bindings, Need history and state evidence");
    BindingsFrameStateView frame;
    BindingsFrameSlotStateView first, second;
    PrimeNeedCellView memo;
    Atom *branch_value = NULL;
    bool read = promoted && find_frame_state(&carrier, epoch, &frame) &&
        bindings_frame_state_read_slot(&frame, 0u, &first) &&
        bindings_frame_state_read_slot(&frame, 1u, &second) &&
        prime_need_snapshot_lookup(bindings_need_view(&carrier), thunk, &memo) &&
        prime_need_branch_state_value(bindings_branch_state_view(&carrier), &state, &branch_value);
    CHECK(read && first.value.skeleton == memo.origin && memo.origin == memo.cached &&
              memo.cached == branch_value && first.value.skeleton != payload &&
              second.value.skeleton != first.value.skeleton &&
              atom_eq(second.value.skeleton, first.value.skeleton),
          "one world transport keeps shared payload identity and independent equal nodes distinct");
    PrimeNeedHistoryIterator history;
    PrimeNeedHistoryView record;
    bool history_preserved = read &&
        prime_need_snapshot_history_begin(bindings_need_view(&carrier), &history);
    for (unsigned i = 0u; history_preserved && i < 3u; ++i)
        history_preserved = prime_need_snapshot_history_next(&history, &record) ==
            PRIME_NEED_HISTORY_ENTRY && record.cell.origin == first.value.skeleton;
    CHECK(history_preserved && prime_need_snapshot_history_next(&history, &record) ==
              PRIME_NEED_HISTORY_DONE && bindings_need_view(&carrier)->session_id == need_session,
          "transport retains one producer across its exact immutable history");
#if CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS
    PrimeNeedReceiptEvent observed;
    CHECK(read && prime_need_receipt_event_at(bindings_receipt_view(&carrier), 0u, &observed) &&
              observed.after == first.value.skeleton,
          "receipt payload shares the transported operational and logical node");
#endif
    if (read)
        state.value = first.value.skeleton;
    arena_free(&source);
    CHECK(read && first.value.skeleton &&
              (mode == 2u || arena_owns_ptr(&survivor, first.value.skeleton)) &&
              prime_need_snapshot_excludes_arena(bindings_need_view(&carrier), &source),
          "the joint retained world survives source-arena reclamation");
    bindings_free(&carrier);
    arena_free(&survivor);
}

static void test_saved_contextual_frame_refinement(void) {
    Arena source, capsule, observation;
    arena_init_detached(&source);
    arena_init_detached(&capsule);
    arena_init_detached(&observation);
    Bindings env, restored, sibling;
    bindings_init(&env);
    bindings_init(&restored);
    bindings_init(&sibling);
    VarId ids[] = {var_id_from_base(51u), var_id_from_base(52u), var_id_from_base(53u)};
    CettaFrameIdentityScope identities = {0};
    CettaFrameIdentity target_epoch = CETTA_FRAME_IDENTITY_NONE, source_epoch = CETTA_FRAME_IDENTITY_NONE, empty_epoch = CETTA_FRAME_IDENTITY_NONE;
    Atom *raw = atom_var_with_id(&source, "late-field", ids[1]);
    bool ready = raw &&
        cetta_frame_identity_scope_try(&identities, &target_epoch) &&
        cetta_frame_identity_scope_try(&identities, &source_epoch) &&
        cetta_frame_identity_scope_try(&identities, &empty_epoch) &&
        bindings_register_contextual_frame(&env, ids, 3u, target_epoch) &&
        bindings_register_contextual_frame(&env, ids, 3u, source_epoch) &&
        bindings_register_contextual_frame(&env, ids, 3u, empty_epoch) &&
        bindings_add_id(&env, var_epoch_id(ids[0], target_epoch), SYMBOL_ID_NONE, raw) &&
        bindings_rewrite_value_id(&env, var_epoch_id(ids[0], target_epoch),
                                 binding_value_from_context(raw, source_epoch));
    Atom *saved = ready ? bindings_capture_value(&capsule, &env) : NULL;
    ready = saved && bindings_restore_captured_value(saved, &restored) &&
        bindings_clone(&sibling, &restored);
    bindings_free(&env);
    arena_free(&capsule);
    arena_free(&source);
    cetta_frame_identity_scope_clear(&identities);
    BindingValue held = bindings_lookup_value_id(&restored, var_epoch_id(ids[0], target_epoch));
    BindingsFrameStateView frame;
    CHECK(ready && held.skeleton && held.skeleton->kind == ATOM_VAR &&
              var_id_eq(held.skeleton->var_id, ids[1]) &&
              cetta_frame_identity_eq(binding_value_epoch(held), source_epoch) &&
              binding_value_kind(held) == BINDING_VALUE_CONTEXTUAL &&
              find_frame_state(&restored, empty_epoch, &frame) && frame.slot_count == 3u &&
              frame.owns_identity &&
              frame_state_slots_are(&restored, empty_epoch, ids, NULL, NULL, NULL),
          "a retained contextual world keeps raw syntax, its lexical frame and wholly unbound inventories");
    Atom *seven = atom_int(&observation, 7);
    Atom *eleven = atom_int(&observation, 11);
    ready = ready &&
        bindings_add_id(&restored, var_epoch_id(ids[1], source_epoch), SYMBOL_ID_NONE, seven) &&
        bindings_add_id(&sibling, var_epoch_id(ids[1], source_epoch), SYMBOL_ID_NONE, eleven);
    Atom *left = ready ? bindings_apply_value(&restored, &observation, held) : NULL;
    Atom *right = ready ? bindings_apply_value(&sibling, &observation, held) : NULL;
    Atom *without_context = ready
        ? bindings_apply_value(&restored, &observation, binding_value_from_atom(held.skeleton))
        : NULL;
    CHECK(ready && left == seven && right == eleven &&
              bindings_lookup_value_id(&restored, var_epoch_id(ids[0], target_epoch)).skeleton ==
                  held.skeleton &&
              bindings_lookup_value_id(&sibling, var_epoch_id(ids[0], target_epoch)).skeleton ==
                  held.skeleton,
          "one retained raw payload observes each branch's later refinement without capturing its substitution");
    CHECK(ready && without_context && without_context->kind == ATOM_VAR &&
              var_id_eq(without_context->var_id, ids[1]) && !atom_eq(without_context, left),
          "discarding the lexical frame loses a later contextual binding");
    bindings_free(&restored);
    bindings_free(&sibling);
    arena_free(&observation);
    bool target_live = cetta_frame_identity_retain(target_epoch);
    bool source_live = cetta_frame_identity_retain(source_epoch);
    bool empty_live = cetta_frame_identity_retain(empty_epoch);
    CHECK(!target_live && !source_live && !empty_live,
          "releasing every retained contextual image ends all three lexical authorities");
    if (target_live) cetta_frame_identity_release(target_epoch);
    if (source_live) cetta_frame_identity_release(source_epoch);
    if (empty_live) cetta_frame_identity_release(empty_epoch);
}

static void test_dense_frame_indexed_suffix(Arena *arena) {
    VarId ids[] = {var_id_from_base(UINT64_C(80001)), var_id_from_base(UINT64_C(80003)), var_id_from_base(UINT64_C(80008))};
    Atom *variables[] = {
        atom_var_with_id(arena, "frame-prefix", ids[0]),
        atom_var_with_id(arena, "frame-present", ids[1]),
        atom_var_with_id(arena, "frame-missing", ids[2])
    };
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    const CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&identities);
    Bindings base;
    bool ready = build_bindings(arena, 256u, &base);
    BindingsBuilder builder;
    ready = ready && bindings_builder_init(&builder, &base);
    if (!ready) { CHECK(false, "indexed frame fixture allocation"); return; }
    bindings_free(&base);
    Atom *before = atom_int(arena, 11);
    Atom *after = atom_int(arena, 22);
    Atom *later = atom_int(arena, 33);
    ready = bindings_builder_register_contextual_frame(&builder, ids, 3u, epoch);
    uint32_t frame_count = UINT32_MAX;
    CHECK(ready && bindings_frame_state_count(&builder.current, &frame_count) &&
              frame_count == 1u,
          "frame count includes an entirely unbound registered inventory");
    CHECK(ready && frame_state_slots_are(&builder.current, epoch, ids, NULL, NULL, NULL),
          "current frame readback retains a registered inventory with no assignments");
    ready = ready && bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[0], epoch),
            SYMBOL_ID_NONE, before);
    uint32_t begin = builder.current.len;
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    ready = ready && bindings_activation_view_prepare(
        &frame, &builder, ids, variables, 3u, epoch, begin) &&
        bindings_builder_add_id_fresh(&builder, var_epoch_id(ids[1], epoch),
            SYMBOL_ID_NONE, after);
    CHECK(ready && dense_slot_is(&builder.current, &frame, 0u, NULL) &&
              dense_slot_is(&builder.current, &frame, 1u, after) && dense_slot_is(&builder.current, &frame, 2u, NULL) &&
              frame_state_slots_are(&builder.current, epoch, ids, before, after, NULL),
          "activation observation excludes earlier writes and retains unbound slots");
    BindingsFrameStateView stale;
    CHECK(find_frame_state(&builder.current, epoch, &stale) &&
              stale.has_activation_write_boundary && stale.activation_write_boundary != 0u,
          "current frame readback retains the activation boundary separately from its values");
    Bindings snapshot;
    bool cloned = ready && bindings_clone(&snapshot, &builder.current);
    uint32_t snapshot_count = UINT32_MAX;
    CHECK(cloned && bindings_frame_state_count(&snapshot, &snapshot_count) &&
              snapshot_count == frame_count,
          "capturing an image retains the registered frame count");
    uint32_t mark = bindings_builder_save(&builder);
    ready = cloned && bindings_builder_add_id_fresh(
        &builder, var_epoch_id(ids[2], epoch), SYMBOL_ID_NONE, later);
    bindings_lookup_index_test_clear(&builder.current);
    CHECK(ready && dense_slot_is(&builder.current, &frame, 2u, later) &&
              !bindings_lookup_value_id(&snapshot, var_epoch_id(ids[2], epoch)).skeleton &&
              frame_state_slots_are(&builder.current, epoch, ids, before, after, later) &&
              frame_state_slots_are(&snapshot, epoch, ids, before, after, NULL),
          "an identity view observes copy-on-write slots without pointer refresh");
    const VarId sentinel_id = var_id_from_base(UINT64_MAX);
    BindingsFrameSlotStateView untouched = {.source_id = sentinel_id};
    CHECK(!bindings_frame_state_read_slot(&stale, 0u, &untouched) &&
              var_id_eq(untouched.source_id, sentinel_id),
          "a borrowed frame readback rejects a changed branch image");
    bindings_builder_rollback(&builder, mark);
    CHECK(ready && dense_slot_is(&builder.current, &frame, 1u, after) && dense_slot_is(&builder.current, &frame, 2u, NULL),
          "rollback is immediately visible through the same activation identity");
    CHECK(ready && frame_state_slots_are(&builder.current, epoch, ids, before, after, NULL),
          "fresh current frame readback sees the exact restored slot state");
    ready = ready && bindings_rewrite_value_id(&builder.current,
        var_epoch_id(ids[1], epoch), binding_value_from_atom(later));
    CHECK(ready && dense_slot_is(&builder.current, &frame, 1u, later) &&
              bindings_lookup_value_id(&snapshot, var_epoch_id(ids[1], epoch)).skeleton == after &&
              frame_state_slots_are(&builder.current, epoch, ids, before, later, NULL) &&
              frame_state_slots_are(&snapshot, epoch, ids, before, after, NULL),
          "a rewritten slot changes the current image without altering a sibling");
    if (cloned)
        bindings_free(&snapshot);
    bindings_activation_view_free(&frame);
    CHECK(!bindings_activation_view_available(&frame, &builder.current),
          "a released activation view cannot resolve a slot");
    bindings_builder_free(&builder);

    Bindings grown;
    bindings_init(&grown);
    VarId first_source = var_id_from_base(1u);
    const CettaFrameIdentity positional_epoch = cetta_frame_identity_scope_fresh(&identities);
    bool grown_ready = bindings_register_contextual_frame(
        &grown, &first_source, 1u, positional_epoch) &&
        bindings_frame_index_grow_slots(&grown, positional_epoch, 3u);
    BindingsFrameStateView positional;
    BindingsFrameSlotStateView implicit;
    uint32_t symbols_before = atomic_load_explicit(&g_symbols->entry_len, memory_order_relaxed);
    CHECK(grown_ready && find_frame_state(&grown, positional_epoch, &positional) &&
              positional.slot_count == 3u && positional.schema_slot_count == 1u &&
              bindings_frame_state_read_slot(&positional, 2u, &implicit) &&
              var_id_eq(implicit.source_id, var_id_from_base(3u)) &&
              var_id_eq(implicit.var_id, var_epoch_id(var_id_from_base(3u), positional_epoch)) &&
              !implicit.declared_in_schema && implicit.spelling == SYMBOL_ID_NONE &&
              !implicit.name_key && !implicit.value.skeleton && implicit.write_version == 0u &&
              atomic_load_explicit(&g_symbols->entry_len, memory_order_relaxed) == symbols_before &&
              grown.prime_ext == NULL,
          "implicit positional readback does not intern a name or materialize occurrence state");
    bindings_free(&grown);
    bindings_init(&grown);
    frame_count = UINT32_MAX;
    CHECK(bindings_frame_state_count(&grown, &frame_count) && frame_count == 0u,
          "an empty frame directory has a successful zero count");
    BindingsFrameDirectoryStateView directory_view;
    CHECK(bindings_frame_state_directory(&grown, &directory_view) &&
              directory_view.bindings == &grown && directory_view.directory == NULL &&
              directory_view.frame_count == 0u && directory_view.schema_revision == 0u &&
              directory_view.write_clock == 0u,
          "an absent directory has an explicit borrowed zero state");
    directory_view.frame_count = 41u;
    CHECK(!bindings_frame_state_directory(NULL, &directory_view) &&
              directory_view.frame_count == 41u &&
              !bindings_frame_state_directory(&grown, NULL),
          "refused directory inspection leaves its output unchanged");
    frame_count = 37u;
    CHECK(!bindings_frame_state_count(NULL, &frame_count) && frame_count == 37u &&
              !bindings_frame_state_count(&grown, NULL),
          "refused count inspection leaves its output unchanged");
    BindingsFrameStateIterator empty = {.bindings = &grown};
    BindingsFrameStateView sentinel = {.ref = {.identity = {UINT64_MAX}}};
    CHECK(!bindings_frame_state_next(&empty, &sentinel) &&
              !bindings_frame_state_next(&empty, &sentinel) &&
              sentinel.ref.identity.word == UINT64_MAX,
          "empty frame directory readback terminates without changing its output");
    VarId sources[] = {var_id_from_base(9u), var_id_from_base(27u)};
    const CettaFrameIdentity first_epoch = cetta_frame_identity_scope_fresh(&identities);
    const CettaFrameIdentity second_epoch = cetta_frame_identity_scope_fresh(&identities);
    bool counted = bindings_register_contextual_frame(&grown, sources, 2u, first_epoch) &&
        bindings_register_contextual_frame(&grown, sources, 2u, second_epoch) &&
        bindings_frame_state_count(&grown, &frame_count);
    BindingsFrameStateIterator all = {.bindings = &grown};
    uint32_t rows = 0u;
    while (bindings_frame_state_next(&all, &sentinel))
        rows++;
    CHECK(counted && frame_count == 2u && rows == frame_count &&
              bindings_frame_state_count(&grown, &snapshot_count) &&
              snapshot_count == frame_count,
          "complete readback enumerates every advertised frame, including unbound ones");
    CHECK(counted && bindings_frame_state_directory(&grown, &directory_view) &&
              directory_view.directory == grown.frame_index &&
              directory_view.frame_count == frame_count &&
              directory_view.schema_revision == grown.frame_index->schema_revision &&
              directory_view.write_clock == grown.frame_index->write_clock,
          "directory readback samples count and lineage independently of bound slots");
    if (counted) {
        /* Deliberately invalid metadata distinguishes refusal from exhaustion.
         * Restore the identity before releasing this test-owned directory. */
        CettaFrameIdentity saved_identity = grown.frame_index->frames[0].epoch;
        grown.frame_index->frames[0].epoch = CETTA_FRAME_IDENTITY_NONE;
        BindingsFrameStateIterator refused = {.bindings = &grown};
        sentinel.ref.identity = (CettaFrameIdentity){UINT64_MAX};
        CHECK(bindings_frame_state_count(&grown, &frame_count) && frame_count == 2u &&
                  !bindings_frame_state_next(&refused, &sentinel) &&
                  refused.frame == 0u && sentinel.ref.identity.word == UINT64_MAX,
              "iterator refusal cannot certify completion of an advertised inventory");
        grown.frame_index->frames[0].epoch = saved_identity;
    }
    bindings_free(&grown);
}

static void test_generation_checked_frame_handles(Arena *arena) {
    Atom qualified_variable = {
        .kind = ATOM_VAR,
        .var_id = {9u, {99u}},
    };
    const CettaFrameIdentity first_context = {FrameIdentity_pack(5u, 2u)};
    const CettaFrameIdentity second_context = {FrameIdentity_pack(6u, 3u)};
    BindingValue materialized = binding_value_from_atom(&qualified_variable);
    binding_value_set_epoch(&materialized, first_context);
    CHECK(var_id_eq(binding_value_variable_id(materialized), qualified_variable.var_id) &&
              cetta_frame_identity_eq(
                  var_epoch_suffix(binding_value_variable_id(materialized)),
                  (CettaFrameIdentity){99u}),
          "materialized variable inspection retains its complete existing identifier");
    BindingValue contextual = binding_value_from_context(
        &qualified_variable, first_context);
    BindingValue persistent = binding_value_from_persistent_context(
        &qualified_variable, second_context);
    CHECK(var_id_eq(binding_value_variable_id(contextual), (VarId){9u, first_context}) &&
              var_id_eq(binding_value_variable_id(persistent),
                        (VarId){9u, second_context}) &&
              contextual.skeleton == persistent.skeleton,
          "one variable pointer keeps distinct contextual epochs and replaces the previous suffix");
    Atom nonvariable = {.kind = ATOM_SYMBOL};
    CHECK(var_id_is_none(binding_value_variable_id(binding_value_from_atom(NULL))) &&
              var_id_is_none(binding_value_variable_id(binding_value_from_context(
                  &nonvariable, first_context))),
          "variable inspection refuses null and nonvariable skeletons");
    Atom zero_base = {.kind = ATOM_VAR, .var_id = {0u, {99u}}};
    VarId before_fresh = fresh_var_id();
    VarId allocated = binding_value_variable_id(
        binding_value_from_context(&zero_base, first_context));
    VarId after_fresh = fresh_var_id();
    CHECK(var_base_id(allocated) == var_base_id(before_fresh) + 1u &&
              var_id_eq(after_fresh, var_id_from_base(var_base_id(allocated) + 1u)) &&
              cetta_frame_identity_eq(var_epoch_suffix(allocated), first_context) &&
              var_id_eq(zero_base.var_id, (VarId){0u, {99u}}),
          "zero-base contextual qualification consumes a fresh identity and leaves the skeleton intact");
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    const CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&identities);
    const CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&identities);
    /* Preserve the alias orientation without assuming allocator reuse order. */
    const CettaFrameIdentity epoch_300 = first.word > second.word ? first : second;
    const CettaFrameIdentity epoch_200 = first.word > second.word ? second : first;
    const CettaFrameIdentity epoch_250 = cetta_frame_identity_scope_fresh(&identities);
    const CettaFrameIdentity epoch_400 = cetta_frame_identity_scope_fresh(&identities);
    VarId source_id = var_id_from_base(UINT64_C(88001));
    VarId unrelated_source_id = var_id_from_base(UINT64_C(88002));
    Atom *source = atom_var_with_id(
        arena, "stable-frame-source", source_id);
    Atom *variables[] = {source};
    BindingsBuilder builder;
    bool ready = source && bindings_builder_init(&builder, NULL) &&
        bindings_builder_register_contextual_frame(
            &builder, &source_id, 1u, epoch_300);
    BindingsFrameRef frame_300 = {0};
    ready = ready && bindings_frame_ref_test_for_epoch(
        &builder.current, epoch_300, &frame_300);
    BindingsActivationView dense;
    bindings_activation_view_init(&dense);
    ready = ready && bindings_activation_view_prepare(
        &dense, &builder, &source_id, variables, 1u, epoch_300, 0u);

    CHECK(ready && bindings_frame_ref_is_valid(frame_300) &&
              cetta_frame_identity_eq(dense.authority_ref.identity, frame_300.identity) &&
              dense.authority_ref.identity.word == epoch_300.word,
          "prepared activation carries its stable generation-checked frame handle");

    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &unrelated_source_id, 1u, epoch_250);
    uint32_t mark = bindings_builder_save(&builder);
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &source_id, 1u, epoch_200);
    BindingsFrameRef frame_200 = {CETTA_FRAME_IDENTITY_NONE};
    CettaFrameIdentity resolved_epoch = CETTA_FRAME_IDENTITY_NONE;
    CHECK(ready && bindings_frame_ref_test_for_epoch(
              &builder.current, epoch_200, &frame_200) &&
              bindings_frame_ref_test_resolves(
                  &builder.current, frame_300, &resolved_epoch) &&
              resolved_epoch.word == epoch_300.word &&
              bindings_activation_view_available(&dense, &builder.current),
          "inserting another frame preserves a live direct reference");

    BindingValue context_300 = binding_value_from_context(source, epoch_300);
    BindingValue context_200 = binding_value_from_context(source, epoch_200);
    ready = ready && match_binding_values_builder(
        context_300, context_200, &builder, arena);
    BindingValue stored_context = ready
        ? bindings_lookup_value_id(
              &builder.current, var_epoch_id(source_id, epoch_300))
        : binding_value_from_atom(NULL);
    BindingsFrameRef stored_ref = binding_value_frame_ref(stored_context);
    BindingsFrameStateView contextual_state;
    BindingsFrameSlotStateView contextual_slot;
    CHECK(ready && find_frame_state(&builder.current, epoch_300, &contextual_state) &&
              bindings_frame_state_read_slot(&contextual_state, 0u, &contextual_slot) &&
              binding_value_kind(contextual_slot.value) == binding_value_kind(stored_context) &&
              binding_value_epoch(contextual_slot.value).word == epoch_200.word &&
              contextual_slot.value.skeleton == source,
          "current frame readback keeps contextual syntax and its lexical identity separate");
    BindingValue resolved_context = binding_value_from_atom(NULL);
    test_runtime_stats_reset_counters();
    bool resolved_direct = ready && bindings_resolve_value_exact(
        &builder.current, stored_context, &resolved_context);
    CHECK(resolved_direct &&
              cetta_frame_identity_eq(stored_ref.identity, frame_200.identity) &&
              stored_ref.identity.word == epoch_200.word &&
              resolved_context.skeleton == source &&
              binding_value_epoch(resolved_context).word == epoch_200.word &&
              test_runtime_stats_counter(
                  CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_DIRECTORY_LOOKUP) == 0u,
          "stored contextual aliases dereference by stable frame handle without directory recovery");

    Bindings projected;
    bindings_init(&projected);
    BindingsEpochRoot projected_root = {
        .atom = source,
        .epoch = epoch_300,
        .variable_support = NULL,
    };
    bool projected_ok = ready &&
        bindings_project_reachable_with_epoch_roots(
            &builder.current, NULL, 0u,
            &projected_root, 1u, &projected);
    BindingValue projected_context = projected_ok
        ? bindings_lookup_value_id(
              &projected, var_epoch_id(source_id, epoch_300))
        : binding_value_from_atom(NULL);
    BindingsFrameRef projected_ref =
        binding_value_frame_ref(projected_context);
    resolved_epoch = CETTA_FRAME_IDENTITY_NONE;
    CHECK(projected_ok &&
              bindings_frame_ref_is_valid(projected_ref) &&
              cetta_frame_identity_eq(projected_ref.identity, stored_ref.identity) &&
              bindings_frame_ref_test_resolves(
                  &projected, projected_ref, &resolved_epoch) &&
              resolved_epoch.word == epoch_200.word,
          "projection preserves a contextual identity in the destination frame table");
    bindings_free(&projected);

    Bindings cloned;
    bindings_init(&cloned);
    resolved_epoch = CETTA_FRAME_IDENTITY_NONE;
    bool cloned_ok = ready && bindings_clone(&cloned, &builder.current);
    CHECK(cloned_ok && bindings_frame_ref_test_resolves(
              &cloned, frame_300, &resolved_epoch) &&
              resolved_epoch.word == epoch_300.word,
          "copy-on-write clone preserves the frame-handle namespace");
    bindings_free(&cloned);

    bindings_builder_rollback(&builder, mark);
    resolved_epoch = CETTA_FRAME_IDENTITY_NONE;
    CHECK(bindings_frame_ref_test_resolves(
              &builder.current, frame_300, &resolved_epoch) &&
              resolved_epoch.word == epoch_300.word &&
              !bindings_frame_ref_test_resolves(
                  &builder.current, frame_200, &resolved_epoch) &&
              bindings_activation_view_available(&dense, &builder.current),
          "rollback removes one activation from this branch and preserves its older sibling");

    ready = bindings_builder_register_contextual_frame(
        &builder, &source_id, 1u, epoch_400);
    BindingsFrameRef frame_400 = {0};
    resolved_epoch = CETTA_FRAME_IDENTITY_NONE;
    CHECK(ready && bindings_frame_ref_test_for_epoch(
              &builder.current, epoch_400, &frame_400) &&
              !cetta_frame_identity_eq(frame_400.identity, frame_200.identity) &&
              !bindings_frame_ref_test_resolves(
                  &builder.current, frame_200, &resolved_epoch) &&
              bindings_frame_ref_test_resolves(
                  &builder.current, frame_400, &resolved_epoch) &&
              resolved_epoch.word == epoch_400.word,
          "removed frame identity remains invalid after another activation is installed");

    BindingsFrameRef wrong_generation = frame_300;
    wrong_generation.identity.word ^= UINT64_C(1) << 32;
    CHECK(!bindings_frame_ref_test_resolves(
              &builder.current, wrong_generation, &resolved_epoch) &&
              bindings_activation_view_available(&dense, &builder.current),
          "a forged generation cannot reuse a live handle or invalidate its real owner");
    bindings_activation_view_free(&dense);
    bindings_builder_free(&builder);
}

static void test_persistent_frame_index_partition(Arena *arena) {
    const uint32_t epoch = 701u;
    VarId ids[] = {var_id_from_base(UINT64_C(87001)), var_id_from_base(UINT64_C(87003))};
    Atom *variables[] = {
        atom_var_with_id(arena, "persistent-frame-a", ids[0]),
        atom_var_with_id(arena, "persistent-frame-b", ids[1]),
    };
    VarId framed_a = var_epoch_id(ids[0], (CettaFrameIdentity){epoch});
    VarId framed_b = var_epoch_id(ids[1], (CettaFrameIdentity){epoch});
    VarId unframed = var_id_from_base(UINT64_C(990001));
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder parent;
    bool ready = bindings_builder_init(&parent, &empty);
    bindings_free(&empty);
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    ready = ready && bindings_activation_view_prepare(
        &frame, &parent, ids, variables, 2u, (CettaFrameIdentity){epoch}, 0u);
    ready = ready && bindings_builder_add_id_fresh(
        &parent, framed_a, SYMBOL_ID_NONE, atom_int(arena, 11));
    ready = ready && bindings_builder_add_id_fresh(
        &parent, unframed, SYMBOL_ID_NONE, atom_int(arena, 44));
    for (uint32_t i = 0u; i < 64u && ready; i++) {
        ready = bindings_builder_add_id_fresh(
            &parent, var_id_from_base(unframed.base + 1u + i), SYMBOL_ID_NONE,
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
              &parent.current, (CettaFrameIdentity){epoch}, &parent_schema, &parent_slots) &&
          bindings_frame_storage_test_identity(
              &child.current, (CettaFrameIdentity){epoch}, &child_schema, &child_slots) &&
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
    VarId source_id = var_id_from_base(UINT64_C(88941));
    VarId framed_id = var_epoch_id(source_id, (CettaFrameIdentity){epoch});
    Atom *authoritative = atom_int(arena, 41);
    Atom *history_decoy = atom_int(arena, 99);
    Bindings bindings;
    bindings_init(&bindings);
    bool ready = bindings_register_complete_contextual_frame(
        &bindings, &source_id, 1u, (CettaFrameIdentity){epoch});
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
              &bindings, var_epoch_id(var_id_from_base(source_id.base + 1u), (CettaFrameIdentity){epoch}),
              binding_value_from_atom(rewritten)),
          "logical value replacement refuses a missing frame coordinate");
    bindings_free(&bindings);

    Bindings late;
    bindings_init(&late);
    CHECK(bindings_add_id(
              &late, framed_id, SYMBOL_ID_NONE, history_decoy) &&
          late.len == 0u && bindings_register_complete_contextual_frame(
              &late, &source_id, 1u, (CettaFrameIdentity){epoch}) &&
          bindings_lookup_value_id(&late, framed_id).skeleton == history_decoy,
          "a framed write establishes slot ownership before schema completion");
    bindings_free(&late);

    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL) &&
              bindings_register_complete_contextual_frame(
                  &builder.current, &source_id, 1u, (CettaFrameIdentity){epoch}),
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

static void test_shared_carrier_promotion(void) {
    Arena source, destination;
    arena_init(&source);
    arena_init(&destination);
    Atom *value = atom_expr2(&source, atom_symbol(&source, "payload"),
                            atom_int(&source, 17));
    PrimeNeedSnapshot root = {0}, allocated, running, completed;
    uint64_t cell_id = 0u;
    CHECK(prime_need_snapshot_begin(&root) &&
          prime_need_snapshot_allocate_persisted(&source, &root, value, 31u,
              &allocated, &cell_id) &&
          prime_need_snapshot_start_evaluation(&source, &allocated, cell_id,
              83u, &running) &&
          prime_need_snapshot_resolve_value(&source, &running, cell_id, 83u,
              value, &completed), "construct completed shared cell");
    PrimeNeedSnapshot elided;
    uint64_t dead_cell;
    CHECK(prime_need_snapshot_allocate_persisted(&source, &completed, value, 901u,
              &elided, &dead_cell), "reserve a key in an elided suffix");
    completed.max_storage_key = elided.max_storage_key;
    PrimeNeedCellView source_cell = {0};
    CHECK(prime_need_snapshot_lookup(&completed, cell_id, &source_cell) &&
          source_cell.origin == value && source_cell.cached == value,
          "source Need origin and cached value have the asserted alias");
    PrimeNeedSnapshot copied = completed;
    CHECK(prime_need_snapshot_promote(&destination, &copied), "promote cell");
    PrimeNeedCellView cell = {0};
    CHECK(prime_need_snapshot_lookup(&copied, cell_id, &cell), "lookup copy");
    CHECK(copied.max_storage_key == 901u, "promotion retains elided-key watermark");
    CHECK(cell.origin == cell.cached, "promotion retains origin/cache alias");
    CHECK(prime_need_snapshot_is_ancestor(&completed, &copied), "copied serial lineage");

    Bindings bindings;
    bindings_init(&bindings);
    CHECK(bindings_add_id(&bindings, var_id_from_base(419u), SYMBOL_ID_NONE, value), "bind same payload");
    *bindings_need_mut(&bindings) = completed;
    StateCell state = {0};
    PrimeNeedBranchState branch = {0};
    CHECK(prime_need_branch_state_begin(&source, &branch) &&
          prime_need_branch_state_write(&source, &branch, &state, value, value,
              bindings_branch_state_mut(&bindings)), "same payload in branch state");
    CettaWeightLedger empty = {0}, factors, handled, selected;
    CHECK(cetta_weight_ledger_append(&source, &empty, 701u, 801u, value, &factors) &&
          cetta_weight_ledger_append(&source, &factors, 702u, 802u, value, &factors) &&
          cetta_weight_ledger_handle(&source, &factors, &empty, 901u,
              &handled, &selected), "same payload in two claimed factors");
    *bindings_weight_mut(&bindings) = handled;
    Atom *source_branch = NULL;
    CHECK(prime_need_branch_state_value(bindings_branch_state_view(&bindings),
              &state, &source_branch) && source_branch != value &&
          handled.last->value != value &&
          handled.last->value != handled.last->previous->value,
          "fixture distinguishes copied equal payloads before transport");
    CHECK(bindings_add_id(&bindings, var_id_from_base(421u), SYMBOL_ID_NONE, source_branch) &&
          bindings_add_id(&bindings, var_id_from_base(422u), SYMBOL_ID_NONE, handled.last->previous->value) &&
          bindings_add_id(&bindings, var_id_from_base(423u), SYMBOL_ID_NONE, handled.last->value),
          "capture actual stored branch and factor roots as shared logical values");
    CHECK(bindings_lookup_value_id(&bindings, var_id_from_base(419u)).skeleton == source_cell.origin &&
          bindings_lookup_value_id(&bindings, var_id_from_base(421u)).skeleton == source_branch &&
          bindings_lookup_value_id(&bindings, var_id_from_base(422u)).skeleton == handled.last->previous->value &&
          bindings_lookup_value_id(&bindings, var_id_from_base(423u)).skeleton == handled.last->value,
          "source logical skeletons alias each actual stored carrier root");
    CHECK(atom_eq(value, source_branch) && atom_eq(value, handled.last->value) &&
          atom_eq(value, handled.last->previous->value) &&
          source_branch != handled.last->value &&
          source_branch != handled.last->previous->value,
          "equal source carrier payloads retain four distinct root addresses");
    CHECK(bindings_refresh_occurrence_token(&bindings), "name the source occurrence");
    uint64_t occurrence = bindings_occurrence_token(&bindings);
    BindingsBuilder original, transported;
    CHECK(bindings_builder_init(&original, &bindings), "capture builder before promotion");
    uint32_t mark = bindings_builder_save(&original);
    CHECK(bindings_builder_add_id_fresh(&original, var_id_from_base(420u), SYMBOL_ID_NONE, value) &&
          original.prime_trail_len == 1u,
          "capture the complete Prime world in rollback state");
    CHECK(cetta_weight_ledger_append(&source, bindings_weight_view(&original.current),
          703u, 803u, value, bindings_weight_mut(&original.current)),
          "current world extends its captured coefficient prefix");
    CHECK(bindings_builder_clone(&transported, &original), "clone the private rollback image");
    Arena packet, wrong;
    arena_init(&packet);
    arena_init(&wrong);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&packet);
    Atom *external = atom_deep_copy_session_copy(session, value);
    Atom *external_branch = atom_deep_copy_session_copy(session, source_branch);
    Atom *external_factor_first = atom_deep_copy_session_copy(session, handled.last->previous->value);
    Atom *external_factor_last = atom_deep_copy_session_copy(session, handled.last->value);
    const PrimeNeedFrame *before_top = bindings_need_view(&transported.current)->top;
    CHECK(!bindings_builder_promote_atoms_with_session(&transported, &wrong, session) &&
          bindings_need_view(&transported.current)->top == before_top &&
          bindings_lookup_value_id(&transported.current, var_id_from_base(419u)).skeleton == value,
          "wrong destination refuses before mutating the private builder");
    PrimeNeedSnapshot wrong_snapshot = completed;
    PrimeNeedBranchState wrong_branch = *bindings_branch_state_view(&bindings);
    CettaWeightLedger wrong_weights = handled;
    CHECK(!prime_need_snapshot_promote_with_session(&wrong, session, &wrong_snapshot) &&
          wrong_snapshot.top == completed.top &&
          !prime_need_branch_state_promote_with_session(&wrong, session, &wrong_branch) &&
          wrong_branch.top == bindings_branch_state_view(&bindings)->top &&
          !cetta_weight_ledger_promote_with_session(&wrong, session, &wrong_weights) &&
          wrong_weights.last == handled.last,
          "each carrier checks the shared session destination");
    CHECK(!prime_need_snapshot_promote_suffix_with_session(&wrong, &source, session,
              &wrong_snapshot) &&
          !prime_need_branch_state_promote_suffix_with_session(&wrong, &source, session,
              &wrong_branch), "suffix promotion checks the same destination authority");
    CHECK(bindings_builder_promote_atoms_with_session(&transported, &packet, session),
          "promote current and every rollback carrier through the enclosing copy");
    CHECK(external != external_branch && external != external_factor_first &&
          external != external_factor_last && external_branch != external_factor_first &&
          external_branch != external_factor_last &&
          external_factor_first != external_factor_last &&
          atom_eq(external, external_branch) && atom_eq(external, external_factor_first) &&
          atom_eq(external, external_factor_last),
          "shared transport does not coalesce equal source-distinct carrier roots");
    CHECK(bindings_lookup_value_id(&transported.current, var_id_from_base(419u)).skeleton == external &&
          bindings_weight_view(&transported.current)->last->previous->value == external_factor_last &&
          transported.prime_trail[0].weights.last->value == external_factor_last &&
          transported.prime_trail[0].weights.last->previous->value == external_factor_first &&
          transported.prime_trail[0].prime_need.max_storage_key == 901u,
          "external, current and rollback roots share one payload image and watermark");
    CHECK(prime_need_snapshot_lookup(&transported.prime_trail[0].prime_need, cell_id,
              &cell) && cell.origin == external && cell.cached == external &&
          prime_need_snapshot_is_ancestor(&completed,
              &transported.prime_trail[0].prime_need),
          "rollback Need image retains payload aliases and exact serial ancestry");
    CHECK(bindings_lookup_value_id(&original.current, var_id_from_base(419u)).skeleton == value &&
          original.prime_trail[0].prime_need.top == completed.top,
          "preparing a copy leaves the original builder and its trail unchanged");
    atom_deep_copy_session_free(session);
    bindings_builder_free(&original);
    CHECK(bindings_promote_atoms_to_arena(&bindings, &destination), "promote complete bindings");
    Atom *logical = bindings_lookup_value_id(&bindings, var_id_from_base(419u)).skeleton;
    Atom *state_value = NULL;
    CHECK(prime_need_snapshot_lookup(bindings_need_view(&bindings), cell_id, &cell) &&
          prime_need_branch_state_value(bindings_branch_state_view(&bindings),
              &state, &state_value), "read copied carriers");
    CHECK(logical == cell.origin && logical == cell.cached,
          "bindings/Need promotion shares one Atom image");
    CHECK(bindings_lookup_value_id(&bindings, var_id_from_base(421u)).skeleton == state_value,
          "bindings/branch promotion shares one Atom image");
    const CettaWeightLedger *weights = bindings_weight_view(&bindings);
    CHECK(bindings_lookup_value_id(&bindings, var_id_from_base(423u)).skeleton == weights->last->value &&
          bindings_lookup_value_id(&bindings, var_id_from_base(422u)).skeleton == weights->last->previous->value,
          "bindings/factor promotion shares one Atom image");
    CHECK(logical != state_value && logical != weights->last->value &&
          logical != weights->last->previous->value &&
          state_value != weights->last->value &&
          state_value != weights->last->previous->value &&
          weights->last->value != weights->last->previous->value &&
          atom_eq(logical, state_value) && atom_eq(logical, weights->last->value) &&
          atom_eq(logical, weights->last->previous->value),
          "complete binding promotion preserves equal-but-distinct carrier roots");
    CHECK(weights->claims->occurrence == handled.claims->occurrence &&
          weights->claims->previous->occurrence == handled.claims->previous->occurrence,
          "promotion retains claim-chain order");
    arena_free(&source);
    CHECK(logical->kind == ATOM_EXPR && logical->expr.elems[1]->ground.ival == 17 &&
          cetta_weight_ledger_handler(weights, 701u) == 901u &&
          cetta_weight_ledger_handler(weights, 702u) == 901u,
          "payload and claims survive original arena retirement");
    CHECK(bindings_occurrence_token(&bindings) == occurrence &&
          bindings_need_view(&bindings)->max_storage_key == 901u,
          "ordinary full promotion preserves the occurrence and allocation watermark");
    bindings_builder_rollback(&transported, mark);
    Atom *restored_state = NULL;
    CHECK(!bindings_lookup_value_id(&transported.current, var_id_from_base(420u)).skeleton &&
          bindings_lookup_value_id(&transported.current, var_id_from_base(419u)).skeleton == external &&
          bindings_occurrence_token(&transported.current) == occurrence &&
          bindings_weight_view(&transported.current)->length == 2u &&
          cetta_weight_ledger_handler(bindings_weight_view(&transported.current), 701u) == 901u &&
          prime_need_snapshot_lookup(bindings_need_view(&transported.current), cell_id,
              &cell) && cell.origin == external && cell.cached == external &&
          prime_need_branch_state_value(bindings_branch_state_view(&transported.current),
              &state, &restored_state) && restored_state == external_branch &&
          bindings_weight_view(&transported.current)->last->value == external_factor_last,
          "rollback after source retirement restores the complete shared world");
    CHECK(external->kind == ATOM_EXPR && external->expr.elems[1]->ground.ival == 17 &&
          bindings_need_view(&transported.current)->max_storage_key == 901u,
          "retired-source rollback keeps an owned payload and its key reservation");
    bindings_builder_free(&transported);
    arena_free(&packet);
    arena_free(&wrong);
    bindings_free(&bindings);
    arena_free(&destination);
}

static void test_frame_slot_promotion_lifetime(void) {
    Arena source;
    Arena owner;
    arena_init(&source);
    arena_init(&owner);
    const uint32_t epoch = 718u;
    VarId source_id = var_id_from_base(UINT64_C(88951));
    VarId framed_id = var_epoch_id(source_id, (CettaFrameIdentity){epoch});
    Atom *head = atom_symbol(&source, "promoted-frame-head");
    Atom *payload = atom_expr2(&source, head, atom_int(&source, 73));
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL) &&
        bindings_register_complete_contextual_frame(
            &builder.current, &source_id, 1u, (CettaFrameIdentity){epoch});
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
    VarId first_slot = var_id_from_base(1u);
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
    CHECK(next && !var_id_eq(next->var_id, last_id) && var_base_id(next->var_id) > 42u &&
              !bindings_has_bound_values(&parent.current) && sibling_ready &&
              binding_is_int(&sibling.current, last_id, 91),
          "rollback restores values without recycling identities held by a sibling");
    Atom *sibling_next = sibling_ready
        ? bindings_builder_new_variable(&sibling, &source, identity) : NULL;
    CHECK(next && sibling_next && !var_id_eq(sibling_next->var_id, next->var_id) &&
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

static void test_manufactured_slot_presentation(void) {
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    Arena source, survivor;
    arena_init(&source);
    arena_init(&survivor);
    CettaFrameIdentity identity = cetta_frame_identity_scope_fresh(&identities);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL), "initialize presentation test");
    Atom *first = bindings_builder_new_variable(&builder, &source, identity);
    Atom *second = bindings_builder_new_variable(&builder, &source, identity);
    Atom *key = atom_expr2(&source, atom_symbol(&source, "GrownSlotName"),
                          atom_int(&source, 2));
    Atom *presented = atom_var_with_name_key(&source, key, second->var_id);
    Bindings before;
    CHECK(bindings_clone(&before, &builder.current), "capture frame before presentation");
    uint32_t mark = bindings_builder_save(&builder);
    CHECK(first && bindings_builder_add_var_fresh(&builder, presented, atom_int(&source, 91)),
          "named variables bind in grown manufactured slots");
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
        builder.current.frame_index, var_epoch_suffix(second->var_id));
    const BindingsFrameIndexEntry *old = bindings_frame_index_find_frame_const(
        before.frame_index, var_epoch_suffix(second->var_id));
    CHECK(frame && frame->schema->len == 1u && frame->slot_len == 2u &&
          frame->schema->cap >= 2u && bindings_frame_slot_name_key(frame, 1u) == key &&
          bindings_frame_slot_name_key(old, 1u) == NULL,
          "presentation growth preserves the source inventory and captured branches");
    Atom *conflict_key = atom_expr2(&source, atom_symbol(&source, "GrownSlotName"),
                                   atom_int(&source, 3));
    Atom *conflict = atom_var_with_name_key(&source, conflict_key, second->var_id);
    CHECK(!bindings_builder_add_var_fresh(&builder, conflict, atom_int(&source, 91)),
          "grown-slot presentation rejects conflicting names for the same identity");
    Bindings saved;
    CHECK(bindings_clone(&saved, &builder.current) &&
          bindings_promote_logical_atoms_to_arena(&saved, &survivor),
          "promotion copies grown-slot presentation into the surviving arena");
    bindings_builder_rollback(&builder, mark);
    CHECK(!bindings_lookup_value_id(&builder.current, second->var_id).skeleton &&
          binding_is_int(&saved, second->var_id, 91),
          "rollback restores values without changing captured identities");
    VarId second_id = second->var_id;
    bindings_free(&before);
    bindings_builder_free(&builder);
    arena_free(&source);
    frame = bindings_frame_index_find_frame_const(saved.frame_index, var_epoch_suffix(second_id));
    Atom *owned_key = frame ? bindings_frame_slot_name_key(frame, 1u) : NULL;
    CHECK(owned_key && owned_key->kind == ATOM_EXPR && owned_key->expr.len == 2u &&
          owned_key->expr.elems[1]->ground.ival == 2 &&
          bindings_logical_atoms_closed_for_arena(&saved, &survivor),
          "grown-slot name metadata survives retirement of its original arena");
    bindings_free(&saved);
    arena_free(&survivor);
}

static void test_manufactured_presentation_reuses_private_capacity(void) {
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    Arena arena;
    arena_init(&arena);
    CettaFrameIdentity identity = cetta_frame_identity_scope_fresh(&identities);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL), "initialize private presentation test");
    Atom *variables[4];
    for (uint32_t i = 0u; i < 4u; i++)
        variables[i] = bindings_builder_new_variable(&builder, &arena, identity);
    CHECK(variables[2] && variables[3] &&
          bindings_builder_add_var_fresh(&builder, variables[2], atom_int(&arena, 3)),
          "populate geometrically grown presentation");
    const BindingsFrameIndexEntry *frame = bindings_frame_index_find_frame_const(
        builder.current.frame_index, var_epoch_suffix(variables[2]->var_id));
    const BindingsFrameSchema *private_schema = frame ? frame->schema : NULL;
    CHECK(private_schema && private_schema->cap >= 4u &&
          bindings_builder_add_var_fresh(&builder, variables[3], atom_int(&arena, 4)),
          "populate another variable within private capacity");
    frame = bindings_frame_index_find_frame_const(
        builder.current.frame_index, var_epoch_suffix(variables[3]->var_id));
    CHECK(frame && frame->schema == private_schema &&
          binding_is_int(&builder.current, variables[2]->var_id, 3) &&
          binding_is_int(&builder.current, variables[3]->var_id, 4),
          "private presentation update avoids a whole-schema copy");
    bindings_builder_free(&builder);
    arena_free(&arena);
}

static void test_frame_index_cycle_bridge(Arena *arena) {
    const uint32_t epoch = 702u;
    VarId source_id = var_id_from_base(UINT64_C(88001));
    VarId framed_id = var_epoch_id(source_id, (CettaFrameIdentity){epoch});
    Atom *source = atom_var_with_id(arena, "cycle-frame-source", source_id);
    VarId ids[] = {source_id};
    Atom *variables[] = {source};
    Atom *framed = atom_var_with_id(
        arena, "cycle-frame-coordinate", framed_id);
    Atom *x = atom_var_with_id(arena, "cycle-generic-x", var_id_from_base(UINT64_C(88101)));
    Atom *y = atom_var_with_id(arena, "cycle-generic-y", var_id_from_base(UINT64_C(88102)));
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, &empty);
    bindings_free(&empty);
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    ready = ready && bindings_activation_view_prepare(
        &frame, &builder, ids, variables, 1u, (CettaFrameIdentity){epoch}, 0u);
    ready = ready && bindings_builder_add_id_fresh(
        &builder, framed_id, SYMBOL_ID_NONE, y);
    ready = ready && bindings_builder_add_id_fresh(
        &builder, x->var_id, SYMBOL_ID_NONE, framed);
    for (uint32_t i = 0u; i < 64u && ready; i++) {
        ready = bindings_builder_add_id_fresh(
            &builder, var_id_from_base(UINT64_C(88200) + i), SYMBOL_ID_NONE,
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
    VarId left_source = var_id_from_base(UINT64_C(88961));
    VarId right_source = var_id_from_base(UINT64_C(88963));
    VarId left_id = var_epoch_id(left_source, (CettaFrameIdentity){left_epoch});
    VarId right_id = var_epoch_id(right_source, (CettaFrameIdentity){right_epoch});
    Atom *left_var = atom_var_with_id(
        arena, "merge-cycle-left", left_id);
    Atom *right_var = atom_var_with_id(
        arena, "merge-cycle-right", right_id);
    Atom *left_tail = atom_var_with_id(
        arena, "merge-cycle-left-tail", var_id_from_base(UINT64_C(88965)));
    Atom *right_tail = atom_var_with_id(
        arena, "merge-cycle-right-tail", var_id_from_base(UINT64_C(88967)));
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
            &left, &left_source, 1u, (CettaFrameIdentity){left_epoch}) &&
        bindings_register_complete_contextual_frame(
            &right, &right_source, 1u, (CettaFrameIdentity){right_epoch}) &&
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
            &grounded_right, &right_source, 1u, (CettaFrameIdentity){right_epoch}) &&
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
    VarId ids[] = {var_id_from_base(UINT64_C(88901)), var_id_from_base(UINT64_C(88903))};
    Bindings parent, child;
    bindings_init(&parent);
    CHECK(bindings_register_contextual_frame(&parent, ids, 1u, (CettaFrameIdentity){epoch}) &&
          bindings_add_id(&parent, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}), SYMBOL_ID_NONE,
                          atom_int(arena, 19)),
          "publish a partial frame inventory before capture");
    CHECK(bindings_clone(&child, &parent), "capture a frame inventory");
    const void *old_schema = NULL, *old_slots = NULL;
    const void *new_schema = NULL, *new_slots = NULL;
    CHECK(bindings_frame_storage_test_identity(&parent, (CettaFrameIdentity){epoch}, &old_schema, &old_slots) &&
          bindings_register_contextual_frame(&child, ids, 2u, (CettaFrameIdentity){epoch}) &&
          bindings_frame_storage_test_identity(&child, (CettaFrameIdentity){epoch}, &new_schema, &new_slots) &&
          old_schema != new_schema,
          "extending an inventory publishes a new schema without changing a captured schema");
    bool known = true;
    uint32_t entry = 0u;
    CHECK(bindings_frame_index_test_lookup(&parent, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}),
                                          &known, &entry) && !known &&
          binding_is_int(&parent, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}), 19),
          "the captured parent retains its original inventory and values");
    bindings_free(&parent);
    CHECK(bindings_add_id(&child, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}), SYMBOL_ID_NONE,
                         atom_int(arena, 23)) &&
          binding_is_int(&child, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}), 19) &&
          binding_is_int(&child, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}), 23),
          "the child owns its expanded schema after the parent is released");
    bindings_free(&child);

    bindings_init(&parent);
    CHECK(bindings_register_contextual_frame(&parent, ids, 2u, (CettaFrameIdentity){epoch}),
          "publish an unbound inventory shared with a future branch");
    BindingsBuilder branch;
    CHECK(bindings_builder_init(&branch, &parent), "capture an unbound frame in a builder");
    uint32_t mark = bindings_builder_save(&branch);
    CHECK(bindings_builder_add_id_fresh(&branch, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}),
                                       SYMBOL_ID_NONE, atom_int(arena, 29)),
          "branch write detaches shared slot storage");
    bindings_builder_rollback(&branch, mark);
    VarId replacement[] = {var_id_from_base(UINT64_C(88921)), var_id_from_base(UINT64_C(88923))};
    CHECK(bindings_register_contextual_frame(&branch.current, replacement, 2u, (CettaFrameIdentity){epoch + 1u}),
          "reuse retired slot storage for another activation");
    known = false;
    CHECK(bindings_frame_index_test_lookup(&parent, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}),
                                          &known, &entry) && known && entry == UINT32_MAX,
          "recycling child storage does not rewrite the parent's shared schema");
    bindings_builder_free(&branch);
    bindings_free(&parent);
}

static void test_builder_frame_registration_rollback(Arena *arena) {
    const uint32_t epoch = 710u;
    VarId ids[] = {var_id_from_base(UINT64_C(88911)), var_id_from_base(UINT64_C(88913))};
    VarId third = var_id_from_base(UINT64_C(88915));
    VarId first_id = var_epoch_id(ids[0], (CettaFrameIdentity){epoch});
    VarId second_id = var_epoch_id(ids[1], (CettaFrameIdentity){epoch});
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL);
    uint32_t origin = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &ids[0], 1u, (CettaFrameIdentity){epoch});
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
            &builder, complete_schema, (CettaFrameIdentity){epoch}) &&
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
            &builder, &third, 1u, (CettaFrameIdentity){epoch});
    bool third_known = false;
    uint32_t third_entry = 0u;
    CHECK(third_registered &&
              bindings_frame_index_test_lookup(
                  &builder.current, var_epoch_id(third, (CettaFrameIdentity){epoch}),
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
                  &builder.current, (CettaFrameIdentity){epoch},
                  &schema_identity, &slot_identity),
          "rolling back before frame manufacture removes the activation itself");
    bindings_frame_schema_release(complete_schema);
    bindings_builder_free(&builder);
}

static void test_frame_registration_compaction_rebase(Arena *arena) {
    const uint32_t epoch = 712u;
    VarId ids[] = {var_id_from_base(UINT64_C(88931)), var_id_from_base(UINT64_C(88933))};
    VarId extension = var_id_from_base(UINT64_C(88935));
    VarId first_id = var_epoch_id(ids[0], (CettaFrameIdentity){epoch});
    VarId second_id = var_epoch_id(ids[1], (CettaFrameIdentity){epoch});
    VarId extension_id = var_epoch_id(extension, (CettaFrameIdentity){epoch});
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL);
    uint32_t origin = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &ids[0], 1u, (CettaFrameIdentity){epoch});
    uint32_t registered = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_add_id_fresh(
        &builder, first_id, SYMBOL_ID_NONE, atom_int(arena, 53));
    uint32_t first_bound = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_contextual_frame(
        &builder, &ids[1], 1u, (CettaFrameIdentity){epoch}) &&
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
            &builder, &extension, 1u, (CettaFrameIdentity){epoch});
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

/* A snapshot of the current ground value owes no rollback to an older RHS.
 * Builder compaction with a live checkpoint does: its native inverse record
 * can restore a contextual RHS whose frame is unbound in the current image. */
static void test_contextual_previous_rhs_compaction_rollback(Arena *arena) {
    VarId ids[] = {var_id_from_base(71u), var_id_from_base(72u), var_id_from_base(73u)};
    CettaFrameIdentityScope identities = {0};
    CettaFrameIdentity target_epoch = CETTA_FRAME_IDENTITY_NONE, source_epoch = CETTA_FRAME_IDENTITY_NONE, unrelated_epoch = CETTA_FRAME_IDENTITY_NONE;
    Bindings base, snapshot;
    bindings_init(&base);
    bindings_init(&snapshot);
    Atom *raw = atom_var_with_id(arena, "checkpoint-context-field", ids[1]);
    Atom *ground = atom_int(arena, 83);
    bool ready = raw && ground &&
        cetta_frame_identity_scope_try(&identities, &target_epoch) &&
        cetta_frame_identity_scope_try(&identities, &source_epoch) &&
        cetta_frame_identity_scope_try(&identities, &unrelated_epoch) &&
        bindings_register_complete_contextual_frame(&base, ids, 3u, target_epoch) &&
        bindings_register_complete_contextual_frame(&base, ids, 3u, source_epoch) &&
        bindings_register_complete_contextual_frame(&base, ids, 3u, unrelated_epoch);
    VarId target_id = var_epoch_id(ids[0], target_epoch);
    Atom *root = atom_var_with_id(arena, "checkpoint-context-root", target_id);
    ready = ready && root && bindings_add_id(&base, target_id, SYMBOL_ID_NONE, raw) &&
        bindings_rewrite_value_id(&base, target_id,
                                 binding_value_from_context(raw, source_epoch));
    BindingsBuilder builder, baseline;
    bool builder_ready = ready && bindings_builder_init(&builder, &base);
    BindingsFrameStateView target_view;
    BindingsFrameSlotStateView previous;
    ready = builder_ready && find_frame_state(&builder.current, target_epoch, &target_view) &&
        bindings_frame_state_read_slot(&target_view, 0u, &previous) &&
        previous.value.skeleton == raw && binding_value_is_contextual(previous.value) &&
        cetta_frame_identity_eq(binding_value_frame_ref(previous.value).identity, source_epoch) &&
        bindings_builder_prepare_fresh_entries(&builder, 1u);
    uint32_t checkpoint = ready ? bindings_builder_save(&builder) : UINT32_MAX;
    uint32_t write_mark = ready ? bindings_builder_history_mark(&builder) : UINT32_MAX;
    uint64_t written_version = 0u;
    /* Ordinary unification fills an alias target rather than replacing an
     * occupied slot. Arrange the replacement with the real native slot writer
     * and its exact inverse data; rollback and compaction remain the runtime's. */
    ready = ready && checkpoint != UINT32_MAX && write_mark == checkpoint &&
        builder.frame_undo_len < builder.frame_undo_cap &&
        bindings_frame_index_write_slot_ref(&builder.current, target_view.ref, 0u,
            binding_value_from_atom(ground), &written_version, NULL);
    if (ready) {
        builder.frame_undo[builder.frame_undo_len++] = (BindingsFrameUndoEntry){
            .previous_value = previous.value,
            .previous_write_version = previous.write_version,
            .written_write_version = written_version,
            .frame_ref = target_view.ref,
            .source_id = var_base_id(ids[0]),
            .slot = 0u,
            .trail_mark = write_mark,
        };
        builder.growth_count++;
    }
    CHECK(ready && binding_is_int(&builder.current, target_id, 83) &&
              written_version > previous.write_version,
          "contextual checkpoint fixture records a real replacement and its exact native inverse");

    bool baseline_ready = ready && bindings_builder_clone(&baseline, &builder);
    if (baseline_ready)
        bindings_builder_rollback(&baseline, checkpoint);
    BindingValue restored = binding_value_from_atom(NULL);
    CHECK(baseline_ready && lookup_frame_value(&baseline.current, target_id, &restored) &&
              restored.skeleton == previous.value.skeleton &&
              binding_value_kind(restored) == binding_value_kind(previous.value) &&
              cetta_frame_identity_eq(binding_value_epoch(restored), binding_value_epoch(previous.value)) &&
              cetta_frame_identity_eq(binding_value_frame_ref(restored).identity, source_epoch) &&
              frame_state_slots_are(&baseline.current, source_epoch, ids, NULL, NULL, NULL),
          "native rollback without compaction restores the contextual RHS and its unbound frame authority");

    BindingsFrameStateView omitted;
    bool projected = ready && bindings_project_reachable(&builder.current, &root, 1u, &snapshot);
    CHECK(projected && binding_is_int(&snapshot, target_id, 83) &&
              !find_frame_state(&snapshot, source_epoch, &omitted) &&
              !find_frame_state(&snapshot, unrelated_epoch, &omitted),
          "current-value snapshot projection owes no pre-capture rollback and drops unreachable frames");

    uint32_t marks[] = {checkpoint};
    bool compacted = ready && bindings_builder_compact_reachable(
        &builder, &root, 1u, marks, 1u, NULL, NULL);
    CHECK(compacted && binding_is_int(&builder.current, target_id, 83) &&
              frame_state_slots_are(&builder.current, source_epoch, ids, NULL, NULL, NULL) &&
              !find_frame_state(&builder.current, unrelated_epoch, &omitted),
          "live-checkpoint compaction retains contextual previous-RHS authority without retaining unrelated frames");
    if (compacted)
        bindings_builder_rollback(&builder, marks[0]);
    restored = binding_value_from_atom(NULL);
    CHECK(compacted && lookup_frame_value(&builder.current, target_id, &restored) &&
              restored.skeleton == previous.value.skeleton &&
              binding_value_kind(restored) == binding_value_kind(previous.value) &&
              cetta_frame_identity_eq(binding_value_epoch(restored), binding_value_epoch(previous.value)) &&
              cetta_frame_identity_eq(binding_value_frame_ref(restored).identity, source_epoch) &&
              frame_state_slots_are(&builder.current, source_epoch, ids, NULL, NULL, NULL),
          "compacted rollback restores the exact contextual RHS with its complete unbound frame inventory");
    if (baseline_ready)
        bindings_builder_free(&baseline);
    if (builder_ready)
        bindings_builder_free(&builder);
    bindings_free(&snapshot);
    bindings_free(&base);
    cetta_frame_identity_scope_clear(&identities);
}

static void test_all_unbound_logical_merge_authority(Arena *arena) {
    VarId ids[] = {var_id_from_base(81u), var_id_from_base(82u), var_id_from_base(83u)};
    CettaFrameIdentityScope identities = {0};
    CettaFrameIdentity source_epoch = CETTA_FRAME_IDENTITY_NONE, destination_epoch = CETTA_FRAME_IDENTITY_NONE;
    bool identities_ready = cetta_frame_identity_scope_try(&identities, &source_epoch) &&
        cetta_frame_identity_scope_try(&identities, &destination_epoch);
    for (unsigned live = 0u; live < 2u; live++) {
        for (unsigned mode = 0u; mode < 3u; mode++) {
            Bindings source, destination;
            bindings_init(&source);
            bindings_init(&destination);
            bool ready = identities_ready;
            if (mode != 1u)
                ready = ready && bindings_register_complete_contextual_frame(
                    &source, ids, 3u, source_epoch);
            else
                ready = ready && bindings_add_id(&source, test_id(9400u),
                    SYMBOL_ID_NONE, atom_int(arena, 89));
            if (mode != 0u)
                ready = ready && bindings_register_complete_contextual_frame(
                    &destination, ids, 3u, destination_epoch);
            else
                ready = ready && bindings_add_id(&destination, test_id(9401u),
                    SYMBOL_ID_NONE, atom_int(arena, 97));
            CHECK(ready && (mode == 1u || bindings_logically_empty(&source)) &&
                      (mode == 0u || bindings_logically_empty(&destination)),
                  "logical-merge fixture distinguishes all-unbound authority from assigned values");
            bool merged = ready && (live
                ? bindings_try_merge_live(&destination, &source)
                : bindings_try_merge(&destination, &source));
            CHECK(merged &&
                      (mode == 1u || frame_state_slots_are(&destination, source_epoch,
                                                         ids, NULL, NULL, NULL)) &&
                      (mode == 0u || frame_state_slots_are(&destination, destination_epoch,
                                                         ids, NULL, NULL, NULL)) &&
                      (mode != 1u || binding_is_int(&destination, test_id(9400u), 89)) &&
                      (mode != 0u || binding_is_int(&destination, test_id(9401u), 97)),
                  live
                      ? "live logical merge retains unbound source and destination frame authority"
                      : "logical merge retains unbound source and destination frame authority");
            CHECK(ready && (mode == 1u || frame_state_slots_are(&source, source_epoch,
                                                             ids, NULL, NULL, NULL)) &&
                      (mode != 1u || binding_is_int(&source, test_id(9400u), 89)),
                  "logical merge leaves source authority and assignments unchanged");
            bindings_free(&destination);
            bindings_free(&source);
        }
    }
    cetta_frame_identity_scope_clear(&identities);
}

/* Native rollback consumes the slot writes but preserves the directory clock
 * and activation boundary. Logical admission keeps that lineage; importing a
 * bound value is a new receiving write, unlike copying an exact image. */
static void test_logical_merge_frame_lineage(Arena *arena) {
    VarId ids[] = {var_id_from_base(91u), var_id_from_base(92u), var_id_from_base(93u)};
    Atom *variables[] = {
        atom_var_with_id(arena, "merge-lineage-first", ids[0]),
        atom_var_with_id(arena, "merge-lineage-second", ids[1]),
        atom_var_with_id(arena, "merge-lineage-third", ids[2]),
    };
    CettaFrameIdentityScope identities = {0};
    CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
    Bindings source;
    bindings_init(&source);
    bool ready = variables[0] && variables[1] && variables[2] &&
        cetta_frame_identity_scope_try(&identities, &epoch) &&
        bindings_register_complete_contextual_frame(&source, ids, 3u, epoch);
    BindingsBuilder source_builder;
    bool source_builder_ready = ready && bindings_builder_init(&source_builder, &source);
    uint32_t mark = source_builder_ready ? bindings_builder_save(&source_builder) : UINT32_MAX;
    BindingsFrameStateView written;
    ready = source_builder_ready && mark != UINT32_MAX &&
        bindings_builder_add_id_fresh(&source_builder, var_epoch_id(ids[0], epoch),
            SYMBOL_ID_NONE, atom_int(arena, 101)) &&
        find_frame_state(&source_builder.current, epoch, &written) &&
        bindings_frame_index_mark_activation_boundary_ref(&source_builder.current,
            written.ref, written.write_clock) &&
        bindings_builder_add_id_fresh(&source_builder, var_epoch_id(ids[2], epoch),
            SYMBOL_ID_NONE, atom_int(arena, 103));
    if (ready) {
        bindings_builder_rollback(&source_builder, mark);
        bindings_free(&source);
        bindings_builder_take(&source_builder, &source);
        source_builder_ready = false;
    }
    BindingsFrameStateView source_before;
    ready = ready && find_frame_state(&source, epoch, &source_before) &&
        source_before.has_activation_write_boundary &&
        source_before.activation_write_boundary > 0u &&
        source_before.write_clock > source_before.activation_write_boundary &&
        frame_state_slots_are(&source, epoch, ids, NULL, NULL, NULL);
    CHECK(ready && bindings_logically_empty(&source),
          "real rollback leaves an all-unbound frame with a nonzero clock and activation boundary");
    if (!ready) {
        if (source_builder_ready)
            bindings_builder_free(&source_builder);
        bindings_free(&source);
        cetta_frame_identity_scope_clear(&identities);
        return;
    }

    for (unsigned route = 0u; route < 3u; route++) {
        for (unsigned mode = 0u; mode < 3u; mode++) {
            Bindings incoming, destination, copied;
            bindings_init(&incoming);
            bindings_init(&destination);
            bindings_init(&copied);
            Atom *imported = atom_int(arena, 107);
            bool case_ready = imported && bindings_clone(&incoming, &source);
            if (mode == 1u) {
                BindingsFrameStateView child;
                case_ready = case_ready && find_frame_state(&incoming, epoch, &child) &&
                    bindings_frame_index_mark_activation_boundary_ref(&incoming,
                        child.ref, child.write_clock) &&
                    bindings_clone(&destination, &source);
            } else if (mode == 2u) {
                case_ready = case_ready && bindings_add_id(&incoming,
                    var_epoch_id(ids[1], epoch), SYMBOL_ID_NONE, imported);
            }
            /* A real receiving assignment keeps the live route on logical
             * merge rather than its exact-image clone shortcut. */
            case_ready = case_ready && bindings_add_id(&destination,
                test_id(9500u + route * 3u + mode), SYMBOL_ID_NONE, atom_int(arena, 109));
            BindingsFrameStateView incoming_before = {0};
            BindingsFrameSlotStateView incoming_slots[3] = {0};
            case_ready = case_ready && find_frame_state(&incoming, epoch, &incoming_before);
            for (uint32_t slot = 0u; slot < 3u && case_ready; slot++)
                case_ready = bindings_frame_state_read_slot(&incoming_before, slot,
                    &incoming_slots[slot]);
            CHECK(case_ready && incoming_before.write_clock > 0u &&
                      incoming_before.has_activation_write_boundary &&
                      (mode == 2u || bindings_logically_empty(&incoming)) &&
                      (mode != 1u || incoming_before.activation_write_boundary !=
                          source_before.activation_write_boundary),
                  "merge-lineage fixture supplies genuine source time and a distinct child boundary");

            if (mode == 2u) {
                BindingsFrameStateView copy_view;
                BindingsFrameSlotStateView copy_slot;
                bool copy_ready = case_ready && bindings_clone(&copied, &incoming);
                CHECK(copy_ready && find_frame_state(&copied, epoch, &copy_view) &&
                          bindings_frame_state_read_slot(&copy_view, 1u, &copy_slot) &&
                          copy_view.write_clock == incoming_before.write_clock &&
                          copy_view.activation_write_boundary == incoming_before.activation_write_boundary &&
                          copy_slot.value.skeleton == imported &&
                          copy_slot.write_version == incoming_slots[1].write_version,
                      "exact image copy preserves the imported value's original write version");
            }

            BindingsBuilder receiver;
            bool receiver_ready = false;
            bool merged = false;
            if (route == 2u) {
                receiver_ready = case_ready && bindings_builder_init(&receiver, &destination);
                merged = receiver_ready && bindings_builder_try_merge(&receiver, &incoming);
            } else {
                merged = case_ready && (route == 0u
                    ? bindings_try_merge(&destination, &incoming)
                    : bindings_try_merge_live(&destination, &incoming));
                receiver_ready = merged && bindings_builder_init(&receiver, &destination);
            }
            BindingsFrameStateView received;
            BindingsFrameSlotStateView received_slots[3];
            bool admitted = merged && receiver_ready &&
                find_frame_state(&receiver.current, epoch, &received);
            uint64_t expected_boundary = mode == 1u
                ? source_before.activation_write_boundary : incoming_before.activation_write_boundary;
            admitted = admitted && received.has_activation_write_boundary &&
                received.activation_write_boundary == expected_boundary &&
                received.write_clock >= incoming_before.write_clock &&
                received.schema_complete && received.slot_count == 3u;
            for (uint32_t slot = 0u; slot < 3u && admitted; slot++) {
                admitted = bindings_frame_state_read_slot(&received, slot, &received_slots[slot]) &&
                    var_id_eq(received_slots[slot].source_id, incoming_slots[slot].source_id) &&
                    received_slots[slot].value.skeleton == incoming_slots[slot].value.skeleton;
                if (admitted && !incoming_slots[slot].value.skeleton)
                    admitted = received_slots[slot].write_version == incoming_slots[slot].write_version;
            }
            CHECK(admitted,
                  mode == 1u
                      ? "logical merge preserves the receiving activation boundary instead of overwriting it with a child boundary"
                      : "new frame admission preserves source boundary and unbound versions and advances the receiving frontier");
            if (mode == 2u) {
                CHECK(admitted && received_slots[1].write_version > incoming_before.write_clock &&
                          received_slots[1].write_version > incoming_slots[1].write_version,
                      "logical merge restamps a bound imported value as a receiving write");
            }

            BindingsActivationView activation;
            bindings_activation_view_init(&activation);
            Atom *later = atom_int(arena, 113);
            bool later_ready = admitted && later && bindings_activation_view_prepare(&activation,
                &receiver, ids, variables, 3u, epoch, 0u) &&
                activation.first_write_version == expected_boundary &&
                bindings_builder_add_id_fresh(&receiver, var_epoch_id(ids[0], epoch),
                    SYMBOL_ID_NONE, later);
            BindingsFrameStateView after;
            BindingsFrameSlotStateView later_slot;
            CHECK(later_ready && find_frame_state(&receiver.current, epoch, &after) &&
                      bindings_frame_state_read_slot(&after, 0u, &later_slot) &&
                      later_slot.write_version > incoming_before.write_clock &&
                      dense_slot_is(&receiver.current, &activation, 0u, later),
                  "a later receiving write lies beyond the source frontier and is visible to its activation");

            BindingsFrameStateView source_after, incoming_after;
            bool unchanged = find_frame_state(&source, epoch, &source_after) &&
                source_after.write_clock == source_before.write_clock &&
                source_after.has_activation_write_boundary == source_before.has_activation_write_boundary &&
                source_after.activation_write_boundary == source_before.activation_write_boundary &&
                frame_state_slots_are(&source, epoch, ids, NULL, NULL, NULL) &&
                case_ready && find_frame_state(&incoming, epoch, &incoming_after) &&
                incoming_after.write_clock == incoming_before.write_clock &&
                incoming_after.has_activation_write_boundary == incoming_before.has_activation_write_boundary &&
                incoming_after.activation_write_boundary == incoming_before.activation_write_boundary;
            for (uint32_t slot = 0u; slot < 3u && unchanged; slot++) {
                BindingsFrameSlotStateView current;
                unchanged = bindings_frame_state_read_slot(&incoming_after, slot, &current) &&
                    current.value.skeleton == incoming_slots[slot].value.skeleton &&
                    current.write_version == incoming_slots[slot].write_version;
            }
            CHECK(unchanged,
                  "logical merge and receiving writes leave both source images and their lineage metadata unchanged");
            bindings_activation_view_free(&activation);
            if (receiver_ready)
                bindings_builder_free(&receiver);
            bindings_free(&copied);
            bindings_free(&destination);
            bindings_free(&incoming);
        }
    }
    bindings_free(&source);
    cetta_frame_identity_scope_clear(&identities);
}

static void test_frame_registration_undo_promotion(void) {
    Arena source;
    Arena owner;
    arena_init(&source);
    arena_init(&owner);
    const uint32_t epoch = 714u;
    VarId ids[] = {var_id_from_base(UINT64_C(88925)), var_id_from_base(UINT64_C(88927))};
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
            &builder, first_schema, (CettaFrameIdentity){epoch});
    uint32_t first_mark = ready ? bindings_builder_save(&builder) : 0u;
    ready = ready && bindings_builder_register_frame_schema(
        &builder, full_schema, (CettaFrameIdentity){epoch}) &&
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
        &builder.current, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}),
        &first_known, &entry);
    bool second_lookup = ready && bindings_frame_index_test_lookup(
        &builder.current, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}),
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

static void test_frame_schema_transport_lifetime(void) {
    Arena source, destination;
    arena_init(&source);
    arena_init(&destination);
    VarId id = var_id_from_base(UINT64_C(88929));
    Atom *key = atom_expr2(&source, atom_symbol(&source, "TransportName"),
                          atom_int(&source, 7));
    Atom *variable = atom_var_with_name_key(&source, key, id);
    BindingsFrameSchema *schema = bindings_frame_schema_new_presented(&id, &variable, 1u);
    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    BindingsFrameSchema *image = bindings_frame_schema_copy_with_session(schema, session);
    CHECK(image && image != schema &&
          bindings_frame_schema_copy_lookup(schema, session) == image &&
          bindings_frame_schema_copy_lookup(image, session) == image,
          "schema transport reuses source mappings and image fixed points");
    BindingsFrameSchema *again = bindings_frame_schema_copy_with_session(image, session);
    CHECK(again == image && !atom_deep_copy_session_clone(session),
          "schema image reuse retains one reference and refuses session cloning");
    bindings_frame_schema_release(again);
    bindings_frame_schema_release(schema);
    bindings_frame_schema_release(image);
    BindingsFrameSchema *held = bindings_frame_schema_copy_with_session(schema, session);
    CHECK(held == image && bindings_frame_schema_len(held) == 1u &&
          var_id_eq(bindings_frame_schema_source_ids(held)[0], id),
          "session holds both endpoints after outside schema references retire");
    atom_deep_copy_session_free(session);
    arena_free(&source);
    CHECK(bindings_frame_schema_len(held) == 1u &&
          var_id_eq(bindings_frame_schema_source_ids(held)[0], id),
          "the returned owned schema survives session and source retirement");
    bindings_frame_schema_release(held);
    arena_free(&destination);
}

static void test_prepared_frame_schema_lifetime(Arena *arena) {
    VarId ids[] = {var_id_from_base(UINT64_C(88931)), var_id_from_base(UINT64_C(88933))};
    VarId first = ids[0], second = ids[1];
    BindingsFrameSchema *schema = bindings_frame_schema_new(ids, 2u);
    CHECK(schema && bindings_frame_schema_len(schema) == 2u,
          "prepare an owned equation inventory");
    ids[0] = var_id_from_base(UINT64_C(99999));
    CHECK(schema && var_id_eq(bindings_frame_schema_source_ids(schema)[0], first),
          "a prepared schema owns its ids independently of the input buffer");
    Bindings bindings;
    bindings_init(&bindings);
    CHECK(bindings_register_complete_frame_schema(&bindings, schema, (CettaFrameIdentity){711u}) &&
          bindings_register_complete_frame_schema(&bindings, schema, (CettaFrameIdentity){712u}),
          "two activations retain one prepared equation schema");
    const void *first_schema = NULL, *first_slots = NULL;
    const void *second_schema = NULL, *second_slots = NULL;
    CHECK(bindings_frame_storage_test_identity(&bindings, (CettaFrameIdentity){711u}, &first_schema, &first_slots) &&
          bindings_frame_storage_test_identity(&bindings, (CettaFrameIdentity){712u}, &second_schema, &second_slots) &&
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
              source_variables, (CettaFrameIdentity){711u}, 0u) &&
          prepared_frame.source_ids == bindings_frame_schema_source_ids(schema) &&
          test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 0u,
          "a prepared equation borrows its published frame without registering it again");
    VarId manufactured_id = var_id_from_base(UINT64_C(88937));
    Atom *manufactured_var = atom_var_with_id(
        arena, "prepared-frame-manufactured", manufactured_id);
    BindingsFrameSchema *manufactured_schema =
        bindings_frame_schema_new_presented(
            &manufactured_id, &manufactured_var, 1u);
    CHECK(manufactured_schema &&
          bindings_builder_register_frame_schema(
              &prepared_builder, manufactured_schema, (CettaFrameIdentity){713u}) &&
          bindings_activation_view_available(&prepared_frame, &prepared_builder.current) &&
          prepared_frame.source_ids == bindings_frame_schema_source_ids(schema),
          "another activation cannot invalidate an identity-based frame view");
    bindings_frame_schema_release(manufactured_schema);
    test_runtime_stats_reset_counters();
    CHECK(!bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, schema,
              NULL, (CettaFrameIdentity){711u}, 0u),
          "a nonempty prepared equation refuses a missing source-variable inventory");
    CHECK(bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, schema,
              source_variables, (CettaFrameIdentity){714u}, 0u) &&
          prepared_frame.source_ids == bindings_frame_schema_source_ids(schema) &&
          test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION) == 1u &&
          test_runtime_stats_counter(
              CETTA_RUNTIME_COUNTER_BINDINGS_FRAME_REGISTRATION_NEW) == 1u,
          "a missing prepared frame is published exactly once before borrowing");
    VarId different_ids[] = {first, var_id_from_base(second.base + UINT64_C(2))};
    BindingsFrameSchema *different_schema =
        bindings_frame_schema_new(different_ids, 2u);
    CHECK(different_schema &&
          !bindings_activation_view_prepare_schema(
              &prepared_frame, &prepared_builder, different_schema,
              source_variables, (CettaFrameIdentity){711u}, 0u),
          "a published complete frame refuses a different schema for the same activation");
    bindings_frame_schema_release(different_schema);
    bindings_activation_view_free(&prepared_frame);
    if (prepared_ready)
        bindings_builder_free(&prepared_builder);

    bindings_frame_schema_release(schema);
    CHECK(bindings_add_id(&bindings, var_epoch_id(first, (CettaFrameIdentity){711u}), SYMBOL_ID_NONE,
                          atom_int(arena, 31)) &&
          bindings_add_id(&bindings, var_epoch_id(first, (CettaFrameIdentity){712u}), SYMBOL_ID_NONE,
                          atom_int(arena, 33)) &&
          binding_is_int(&bindings, var_epoch_id(first, (CettaFrameIdentity){711u}), 31) &&
          binding_is_int(&bindings, var_epoch_id(first, (CettaFrameIdentity){712u}), 33),
          "activations retain their schema after the preparing owner is released");
    Atom *full_frame_value = bindings_apply_epoch(
        &bindings, arena, source_variables[0], (CettaFrameIdentity){711u});
    Atom *unbound_frame_value = bindings_apply_epoch(
        &bindings, arena, source_variables[1], (CettaFrameIdentity){711u});
    CHECK(full_frame_value && full_frame_value->kind == ATOM_GROUNDED &&
          full_frame_value->ground.gkind == GV_INT &&
          full_frame_value->ground.ival == 31 &&
          unbound_frame_value && unbound_frame_value->kind == ATOM_VAR &&
          var_id_eq(unbound_frame_value->var_id, var_epoch_id(second, (CettaFrameIdentity){711u})),
          "a full epoch view observes a frame-only value and preserves an unbound slot");
    bool known = false;
    uint32_t entry = 0u;
    CHECK(bindings_frame_index_test_lookup(&bindings, var_epoch_id(second, (CettaFrameIdentity){711u}),
                                          &known, &entry) && known && entry == UINT32_MAX,
          "an unbound slot survives release of its preparing owner");
    bindings_free(&bindings);

    VarId duplicate[] = {first, first};
    VarId qualified[] = {var_epoch_id(first, (CettaFrameIdentity){711u})};
    CHECK(bindings_frame_schema_new(duplicate, 2u) == NULL &&
          bindings_frame_schema_new(qualified, 1u) == NULL &&
          bindings_frame_schema_new(ids, 2u) == NULL &&
          bindings_frame_schema_new(NULL, 1u) == NULL,
          "schema preparation refuses duplicate, qualified, unsorted and missing ids");
    schema = bindings_frame_schema_new(NULL, 0u);
    bindings_init(&bindings);
    CHECK(schema && bindings_frame_schema_len(schema) == 0u &&
          bindings_register_complete_frame_schema(&bindings, schema, (CettaFrameIdentity){713u}),
          "a ground equation has a valid empty schema");
    bindings_frame_schema_release(schema);
    bindings_free(&bindings);
}

static void test_presented_manufacture_frame_grows_until_closed(
        Arena *arena) {
    const uint32_t epoch = 715u;
    VarId ids[] = {var_id_from_base(UINT64_C(88941)), var_id_from_base(UINT64_C(88943)), var_id_from_base(UINT64_C(88945))};
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
              &builder, first_schema, (CettaFrameIdentity){epoch}) &&
          bindings_builder_register_frame_schema(
              &builder, second_schema, (CettaFrameIdentity){epoch}) &&
          bindings_builder_add_id_fresh(
              &builder, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}), SYMBOL_ID_NONE,
              atom_int(arena, 37)) &&
          bindings_builder_add_id_fresh(
              &builder, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}), SYMBOL_ID_NONE,
              atom_int(arena, 41)) &&
          binding_is_int(
              &builder.current, var_epoch_id(ids[0], (CettaFrameIdentity){epoch}), 37) &&
          binding_is_int(
              &builder.current, var_epoch_id(ids[1], (CettaFrameIdentity){epoch}), 41),
          "a dynamic activation merges presented manufactured slots into one frame");
    CHECK(ready &&
          bindings_register_complete_contextual_frame(
              &builder.current, ids, 2u, (CettaFrameIdentity){epoch}) &&
          !bindings_builder_register_frame_schema(
              &builder, third_schema, (CettaFrameIdentity){epoch}),
          "closing a manufactured frame rejects a later undeclared slot");
    bindings_frame_schema_release(first_schema);
    bindings_frame_schema_release(second_schema);
    bindings_frame_schema_release(third_schema);
    if (ready)
        bindings_builder_free(&builder);
}

static void test_complete_contextual_frame_is_closed(Arena *arena) {
    const uint32_t epoch = 703u;
    VarId source_ids[] = {var_id_from_base(UINT64_C(89001)), var_id_from_base(UINT64_C(89003))};
    VarId extra_source_id = var_id_from_base(UINT64_C(89005));
    VarId first = var_epoch_id(source_ids[0], (CettaFrameIdentity){epoch});
    VarId second = var_epoch_id(source_ids[1], (CettaFrameIdentity){epoch});
    Bindings empty;
    bindings_init(&empty);
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, &empty);
    bindings_free(&empty);
    bool admitted = ready &&
        bindings_register_complete_contextual_frame(
            &builder.current, source_ids, 2u, (CettaFrameIdentity){epoch});
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
              &builder.current, &extra_source_id, 1u, (CettaFrameIdentity){epoch}) &&
              bindings_lookup_value_id(
                  &builder.current,
                  var_epoch_id(extra_source_id, (CettaFrameIdentity){epoch})).skeleton == NULL &&
              binding_is_int(&builder.current, first, 703),
          "a complete contextual frame refuses undeclared coordinates without mutation");
    if (ready)
        bindings_builder_free(&builder);
}

static void test_lookup_domain_accounting(Arena *arena) {
    const uint32_t epoch = 703u;
    const VarId source_ids[] = {var_id_from_base(UINT64_C(88301)), var_id_from_base(UINT64_C(88302))};
    const VarId framed_hit = var_epoch_id(source_ids[0], (CettaFrameIdentity){epoch});
    const VarId framed_miss = var_epoch_id(source_ids[1], (CettaFrameIdentity){epoch});
    const VarId generic_hit = var_id_from_base(UINT64_C(88311));
    const VarId generic_miss = var_id_from_base(UINT64_C(88312));
    const VarId unowned_contextual_miss =
        var_epoch_id(var_id_from_base(UINT64_C(88313)), (CettaFrameIdentity){epoch + 1u});
    BindingsBuilder builder;
    bool ready = bindings_builder_init(&builder, NULL);
    test_runtime_stats_reset_counters();
    ready = ready && bindings_register_contextual_frame(
        &builder.current, source_ids, 1u, (CettaFrameIdentity){epoch});
    ready = ready && bindings_register_contextual_frame(
        &builder.current, source_ids, 1u, (CettaFrameIdentity){epoch});
    ready = ready && bindings_register_contextual_frame(
        &builder.current, source_ids, 2u, (CettaFrameIdentity){epoch});
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
    VarId ids[] = {var_id_from_base(UINT64_C(91001)), var_id_from_base(UINT64_C(91002)), var_id_from_base(UINT64_C(91003))};
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
              &builder, var_epoch_id(ids[0], (CettaFrameIdentity){epoch - 1u}),
              SYMBOL_ID_NONE, two),
          "outer-epoch prefix stays a trail binding");
    uint32_t first = builder.current.len;
    BindingsActivationView frame;
    bindings_activation_view_init(&frame);
    CHECK(bindings_activation_view_prepare(
              &frame, &builder, ids, variables, 3u, (CettaFrameIdentity){epoch}, first),
          "prepare the activation inventory");

    CHECK(bindings_activation_view_available(&frame, &builder.current),
          "prepared view resolves its activation");

    bindings_lookup_index_test_clear(&builder.current);
    VarId local_a = var_epoch_id(ids[0], (CettaFrameIdentity){epoch});
    VarId local_b = var_epoch_id(ids[1], (CettaFrameIdentity){epoch});
    uint32_t synced = 0u;
    BindingValue slot = binding_value_from_atom(NULL);
    CHECK(lookup_frame_value(
              &builder.current, local_a, &slot) && slot.skeleton == NULL &&
              !bindings_lookup_index_test_synced_len(
                  &builder.current, &synced),
          "unbound inventory slot is empty without rebuilding the index");
    CHECK(bindings_lookup_value_id(&builder.current, var_epoch_id(ids[0], (CettaFrameIdentity){epoch - 1u})).skeleton == two,
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
    CHECK(match_atoms_builder(query, pattern, &builder, arena) &&
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
    /* Matching compares variable identity, but a retained atom also carries
     * its authored name. Interning must not turn that name into an alpha key. */
    VarId shown_id = test_id(7989u);
    Atom *unnamed = atom_var_with_spelling(
        &shared_arena, SYMBOL_ID_NONE, shown_id);
    Atom *named = atom_var_with_id(&shared_arena, "retained", shown_id);
    CHECK(unnamed != named && atom_eq(unnamed, named) &&
              atom_symbol_current_id(named) == symbol_intern_cstr(g_symbols, "retained"),
          "hash-consing preserves names without changing variable identity");
    CHECK(atom_var_with_id(&shared_arena, "retained", shown_id) == named,
          "hash-consing still shares an identical named variable");
    Atom *unnamed_parent = atom_expr2(&shared_arena, head, unnamed);
    Atom *named_parent = atom_expr2(&shared_arena, head, named);
    CHECK(unnamed_parent != named_parent &&
              atom_eq(unnamed_parent, named_parent) &&
              named_parent->expr.elems[1] == named,
          "hash-consing preserves variable names inside expressions");
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
        .symbol_number = atom_symbol_current_id(head),
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
                left, (CettaFrameIdentity){3u}, 0u, equal, &values, &shared_arena, (CettaFrameIdentity){5u}) &&
            bindings_builder_save(&values) == values_mark;
        bool unequal_rejected = equal_match &&
            !match_atoms_epoch_view_builder(
                left, (CettaFrameIdentity){3u}, 0u, unequal, &values, &shared_arena, (CettaFrameIdentity){5u}) &&
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
            shared_nan, (CettaFrameIdentity){11u}, 0u, shared_nan,
            &values, &shared_arena, (CettaFrameIdentity){13u}) &&
        bindings_builder_save(&values) == values_mark;
    /* Floats intern by their bits, so the two NaN literals are one atom.
     * Whether a NaN equals itself is its lane's rule, and with no lane here
     * a term is itself; the epoch view settles a second term over the same
     * NaN exactly as equality does. */
    bool nan_interned = nan_left && nan_left == nan_right;
    bool rebuilt_nan_agrees = values_ready && rebuilt_nan &&
        rebuilt_nan != shared_nan &&
        match_atoms_epoch_view_builder(
            shared_nan, (CettaFrameIdentity){11u}, 0u, rebuilt_nan,
            &values, &shared_arena, (CettaFrameIdentity){13u}) ==
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
            epoch_term, (CettaFrameIdentity){17u}, 0u, epoch_term,
            &epochs, &shared_arena, (CettaFrameIdentity){19u});
    BindingValue epoch_value = epochs_match
        ? bindings_lookup_value_id(
              &epochs.current, var_epoch_id(epoch_var->var_id, (CettaFrameIdentity){17u}))
        : binding_value_from_atom(NULL);
    CHECK(epochs_match &&
              bindings_has_bound_values(&epochs.current) &&
              epoch_value.skeleton &&
              epoch_value.skeleton->kind == ATOM_VAR &&
              var_id_eq(binding_value_variable_id(epoch_value), var_epoch_id(epoch_var->var_id, (CettaFrameIdentity){19u})),
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
                  cycle, (CettaFrameIdentity){23u}, 0u, cycle,
                  &cyclic, ordinary_arena, (CettaFrameIdentity){29u}) &&
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
              var_id_eq(atom_single_variable_id(singleton), left->var_id),
          "nested repeated support records its exact single variable");
    CHECK(multiple && atom_has_vars(multiple) &&
              var_id_is_none(atom_single_variable_id(multiple)),
          "multi-variable support declines the singleton fast path");
    CHECK(closed && !atom_has_vars(closed) &&
              var_id_is_none(atom_single_variable_id(closed)),
          "closed support remains distinct from an open multi-variable term");

    Atom *draft = atom_expr_builder_begin(arena, 2u);
    if (draft) {
        draft->expr.elems[0] = head;
        draft->expr.elems[1] = left;
        draft = atom_expr_builder_finish(arena, draft);
    }
    CHECK(draft && var_id_eq(atom_single_variable_id(draft), left->var_id),
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
    size_t cache_bytes = arena.persistent_bytes;
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
        if (!atom || atom->kind != ATOM_SYMBOL || atom_symbol_current_id(atom) != id) {
            exact_after_churn = false;
            break;
        }
    }
    Atom *first_after_churn = atom_symbol_id(&arena, first_id);
    CHECK(exact_after_churn && first_after_churn &&
              first_after_churn->kind == ATOM_SYMBOL &&
              first_after_churn->symbol_number == first_id &&
              arena.persistent_bytes == cache_bytes,
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
              var_id_eq(bindings_entry_at(&transported, 0)->var_id, first_id) &&
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
    VarId frame_source_id = var_id_from_base(UINT64_C(7100));
    VarId framed_id = var_epoch_id(frame_source_id, (CettaFrameIdentity){frame_epoch});
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
            1u, (CettaFrameIdentity){frame_epoch}, 0u) &&
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
    VarId contextual_left = var_epoch_id(var_id_from_base(1u), (CettaFrameIdentity){epoch});
    VarId contextual_right = var_epoch_id(var_id_from_base(2u), (CettaFrameIdentity){epoch});
    bool imported_ok = ready &&
        bindings_contextualize_unframed(&imported, arena, &inventory, (CettaFrameIdentity){epoch}) &&
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
              var_id_eq(binding_value_variable_id(left_value), contextual_right) &&
              right_value.skeleton == leaf &&
              !bindings_lookup_value_id(
                  &imported, left->var_id).skeleton,
          "context import replaces unframed rows with authoritative frame slots");
    Atom *resolved = imported_ok
        ? bindings_apply_value(
              &imported, arena,
              binding_value_from_context(
                  cetta_var_map_lookup(&inventory, left->var_id), (CettaFrameIdentity){epoch}))
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
              var_id_eq(observed_constraint_left->expr.elems[1]->var_id, var_epoch_id(
                      cetta_var_map_lookup(&inventory,
                          constraint_left_variable->var_id)->var_id, (CettaFrameIdentity){epoch})),
          "context import preserves aliases and qualifies connected constraints");
    CHECK(inventory.len == 4u &&
              var_id_eq(cetta_var_map_lookup(&inventory, left->var_id)->var_id, var_id_from_base(1u)) &&
              var_id_eq(cetta_var_map_lookup(&inventory, right->var_id)->var_id, var_id_from_base(2u)),
          "context import compiles sparse authored identities into dense slots");
    cetta_var_map_free(&inventory);
    bindings_free(&imported);

    Atom *foreign = atom_var_with_id(
        arena, "context-import-foreign", var_epoch_id(var_id_from_base(1u), (CettaFrameIdentity){epoch + 1u}));
    Atom *open = atom_expr3(arena, atom_symbol(arena, "ContextImportOpen"), foreign, right);
    Bindings mixed;
    bindings_init(&mixed);
    VarId foreign_slot = var_id_from_base(1u);
    ready = bindings_register_contextual_frame(&mixed, &foreign_slot, 1u, (CettaFrameIdentity){epoch + 1u}) &&
        bindings_add_var(&mixed, foreign, atom_int(arena, 71)) &&
        bindings_add_var(&mixed, left, open) && bindings_add_var(&mixed, right, atom_int(arena, 83));
    Atom *query = ready ? cetta_import_frame_syntax(arena,
        atom_expr3(arena, atom_symbol(arena, "ImportedQuery"), left, foreign),
        &inventory, (CettaFrameIdentity){epoch}) : NULL;
    bool mixed_ok = query && bindings_contextualize_unframed(&mixed, arena, &inventory, (CettaFrameIdentity){epoch});
    Atom *answer = mixed_ok ? bindings_apply(&mixed, arena, query) : NULL;
    CHECK(answer && answer->kind == ATOM_EXPR && answer->expr.len == 3u &&
              answer->expr.elems[1]->kind == ATOM_EXPR &&
              answer->expr.elems[1]->expr.elems[1]->ground.ival == 71 &&
              answer->expr.elems[1]->expr.elems[2]->ground.ival == 83 &&
              answer->expr.elems[2]->ground.ival == 71 && inventory.len == 2u,
          "root and substitution import preserve existing frames while translating authored aliases");
    CHECK(mixed_ok && !var_id_eq(query->expr.elems[1]->var_id, query->expr.elems[2]->var_id) &&
              var_id_eq(query->expr.elems[2]->var_id, foreign->var_id),
          "equal slot numbers in different imported frames cannot capture one another");
    cetta_var_map_free(&inventory);
    bindings_free(&mixed);
}

static void test_dense_term_instantiation(Arena *arena) {
    CETTA_FRAME_IDENTITY_SCOPE(source_owners);
    CettaFrameIdentity first = cetta_frame_identity_scope_fresh(&source_owners);
    CettaFrameIdentity second = cetta_frame_identity_scope_fresh(&source_owners);
    Atom *x = atom_var_with_id(arena, "same", var_epoch_id(var_id_from_base(71u), first));
    Atom *y = atom_var_with_id(arena, "same", var_epoch_id(var_id_from_base(71u), second));
    Atom *z = atom_var_with_id(arena, "last", var_id_from_base(UINT64_C(900003)));
    Atom *shared = atom_expr2(arena, x, y);
    Atom *aliases[2] = {atom_expr2(arena, shared, z), atom_expr2(arena, z, shared)};
    CHECK(cetta_instantiate_frame_terms(arena, aliases, 2u) &&
          aliases[0]->expr.elems[0] == aliases[1]->expr.elems[1],
          "one freshening session preserves shared subgraphs across roots");
    Atom *original = atom_expr2(arena, x, z);
    Atom *incomplete[2] = {original, NULL};
    ArenaMark failure_origin = arena_mark(arena);
    CHECK(!cetta_instantiate_frame_terms(arena, incomplete, 2u) &&
          incomplete[0] == original && incomplete[1] == NULL &&
          arena_at_mark(arena, failure_origin),
          "failed multi-root freshening neither publishes a partial root nor retains its storage");
    Atom *terms[2] = {atom_expr3(arena, x, y, z), atom_expr2(arena, z, x)};
    CHECK(cetta_instantiate_frame_terms(arena, terms, 2u),
          "instantiate related terms in one owned frame");
    Atom *a = terms[0]->expr.elems[0];
    Atom *b = terms[0]->expr.elems[1];
    Atom *c = terms[0]->expr.elems[2];
    CettaFrameIdentity identity = var_epoch_suffix(a->var_id);
    uint32_t extent = 0u;
    CHECK(!cetta_frame_identity_eq(identity, first) && !cetta_frame_identity_eq(identity, second) &&
          cetta_frame_identity_slot_count(identity, &extent) && extent == 3u &&
          var_base_id(a->var_id) == 1u && var_base_id(b->var_id) == 2u &&
          var_base_id(c->var_id) == 3u &&
          cetta_frame_identity_eq(var_epoch_suffix(b->var_id), identity) &&
          cetta_frame_identity_eq(var_epoch_suffix(c->var_id), identity),
          "fresh frame has dense slots even for sparse and previously qualified sources");
    CHECK(!var_id_eq(a->var_id, b->var_id) &&
          var_id_eq(terms[1]->expr.elems[0]->var_id, c->var_id) &&
          var_id_eq(terms[1]->expr.elems[1]->var_id, a->var_id),
          "shared variables remain shared without capturing equal slots in distinct source frames");
    Atom *other = cetta_instantiate_frame_syntax(arena, terms[0]);
    CHECK(other && !var_id_eq(other->expr.elems[0]->var_id, a->var_id) &&
          !var_id_eq(other->expr.elems[0]->var_id, other->expr.elems[1]->var_id),
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

typedef struct {
    const Atom *saved;
    const Bindings *base;
    VarId saved_id;
    Bindings survivor;
    bool ok;
} CapturedBindingsWorker;

static void *captured_bindings_worker(void *raw) {
    CapturedBindingsWorker *worker = raw;
    Arena copies;
    arena_init_detached(&copies);
    Atom *copy = atom_deep_copy(&copies, (Atom *)worker->saved);
    worker->ok = copy != NULL;
    for (unsigned i = 0u; worker->ok && i < 1200u; i++) {
        Bindings restored, sibling;
        bindings_init(&restored);
        bindings_init(&sibling);
        worker->ok = bindings_restore_captured_value(copy, &restored) &&
            bindings_clone(&sibling, worker->base) &&
            binding_is_int(&restored, worker->saved_id, 17) &&
            binding_is_int(&sibling, worker->saved_id, 17);
        bindings_free(&restored);
        bindings_free(&sibling);
    }
    worker->ok = worker->ok &&
        bindings_restore_captured_value(copy, &worker->survivor);
    arena_free(&copies);
    return NULL;
}

static void test_saved_binding_value_concurrent_lifetime(void) {
    Arena source, capsules;
    arena_init_detached(&source);
    arena_init_detached(&capsules);
    Bindings env, base;
    bindings_init(&env);
    bindings_init(&base);
    VarId saved_id = test_id(9240u);
    bool ready = bindings_add_id(&env, saved_id, SYMBOL_ID_NONE,
                                 atom_int(&source, 17));
    Atom *saved = ready ? bindings_capture_value(&capsules, &env) : NULL;
    ready = saved && bindings_restore_captured_value(saved, &base);
    CHECK(ready, "concurrent saved bindings capture an owned source version");
    bindings_free(&env);
    arena_free(&source);

    enum { WORKERS = 4 };
    pthread_t threads[WORKERS];
    CapturedBindingsWorker workers[WORKERS] = {0};
    unsigned started = 0u;
    while (ready && started < WORKERS) {
        workers[started].saved = saved;
        workers[started].base = &base;
        workers[started].saved_id = saved_id;
        bindings_init(&workers[started].survivor);
        if (pthread_create(&threads[started], NULL,
                           captured_bindings_worker, &workers[started]) != 0)
            break;
        started++;
    }
    bool joined = started == WORKERS;
    for (unsigned i = 0u; i < started; i++)
        joined = (pthread_join(threads[i], NULL) == 0) && joined;
    CHECK(joined, "concurrent saved binding readers all finish");
    bool intact = ready && binding_is_int(&base, saved_id, 17);
    for (unsigned i = 0u; i < started; i++)
        intact = workers[i].ok && intact;
    CHECK(intact, "parallel capsule and version retains preserve one immutable binding image");
    bindings_free(&base);
    arena_free(&capsules);
    bool survived = started == WORKERS;
    for (unsigned i = 0u; i < started; i++) {
        survived = binding_is_int(&workers[i].survivor, saved_id, 17) && survived;
        bindings_free(&workers[i].survivor);
    }
    CHECK(survived, "worker-restored versions outlive every source and capsule owner");
}

static void check_capture_agreement(Arena *arena) {
    Bindings left, right, alias, framed_left, framed_right, copied;
    bindings_init(&left); bindings_init(&right); bindings_init(&alias);
    bindings_init(&framed_left); bindings_init(&framed_right); bindings_init(&copied);
    StateCell a = {.value = atom_int(arena, 1)}, b = {.value = atom_int(arena, 1)};
    VarId variable = fresh_var_id();
    Atom *first = atom_expr2(arena, atom_symbol(arena, "Captured"), atom_state(arena, &a));
    Atom *second = atom_expr2(arena, atom_symbol(arena, "Captured"), atom_state(arena, &b));
    CHECK(bindings_add_id(&left, variable, SYMBOL_ID_NONE, first) &&
          bindings_add_id(&right, variable, SYMBOL_ID_NONE, second) &&
          bindings_add_id(&alias, variable, SYMBOL_ID_NONE, atom_deep_copy(arena, first)),
          "capture controls bind same-named values containing actual mutable handles");
    CHECK(bindings_eq(&left, &right) && !bindings_capture_equal(arena, &left, &right) &&
          bindings_capture_equal(arena, &left, &alias),
          "captured input agreement distinguishes resource replacement from wrapper copying");
    CHECK(bindings_capture_equal_checked(arena,&left,&right)==CETTA_MATCH_REFUTED &&
          bindings_capture_equal_checked(arena,&left,&alias)==CETTA_MATCH_MATCHED,
          "checked captured inputs distinguish replacement from an identical held image");
    bindings_free(&left); bindings_free(&right);
    bindings_init(&left); bindings_init(&right);
    Atom *unknown = atom_var_with_id(arena, "$capture-open", fresh_var_id());
    CHECK(bindings_add_constraint(&left, first, unknown) &&
          bindings_add_constraint(&right, second, unknown) &&
          !bindings_capture_equal(arena, &left, &right),
          "delayed equality constraints use the same resource-aware capture relation");
    CETTA_FRAME_IDENTITY_SCOPE(identities);
    CettaFrameIdentity first_frame = cetta_frame_identity_scope_fresh(&identities);
    CettaFrameIdentity second_frame = cetta_frame_identity_scope_fresh(&identities);
    VarId slot = var_id_from_base(1u);
    CHECK(bindings_register_contextual_frame(&framed_left, &slot, 1u, first_frame) &&
          bindings_register_contextual_frame(&framed_right, &slot, 1u, second_frame) &&
          bindings_clone(&copied, &framed_left),
          "capture controls retain distinct all-unbound frame inventories");
    CHECK(bindings_eq(&framed_left, &framed_right) &&
          !bindings_capture_equal(arena, &framed_left, &framed_right) &&
          bindings_capture_equal(arena, &framed_left, &copied),
          "unbound frame authority is visible to capture agreement even without assignments");
    CHECK(bindings_frame_index_grow_slots(&copied, first_frame, 2u) &&
          !bindings_capture_equal(arena, &framed_left, &copied),
          "growing an unbound authority domain changes the captured input");
    bindings_free(&left); bindings_free(&right);
    bindings_init(&left); bindings_init(&right);
    Atom *x=atom_var_with_id(arena,"capture-x",fresh_var_id());
    Atom *inner=atom_expr2(arena,atom_symbol(arena,"Data"),x);
    Atom *marked=atom_expr2(arena,atom_symbol(arena,"Outer"),atom_he_evaluated_copy(arena,inner));
    Atom *plain=atom_expr2(arena,atom_symbol(arena,"Outer"),inner);
    Atom *materialized=atom_freshen_epoch(arena,plain,first_frame);
    CHECK(bindings_add_id(&left,variable,SYMBOL_ID_NONE,marked) &&
          bindings_rewrite_value_id(&left,variable,binding_value_from_context(marked,first_frame)) &&
          bindings_add_id(&right,variable,SYMBOL_ID_NONE,materialized),
          "contextual capture has the same actual variables as the materialized input");
    CHECK(!bindings_capture_equal(arena,&left,&right),
          "materializing a contextual value cannot erase a nested Data/code distinction");
    Atom *kept=binding_value_materialize(arena,binding_value_from_context(marked,first_frame));
    CHECK(kept && atom_is_he_evaluated(kept->expr.elems[1]) &&
          bindings_rewrite_value_id(&right,variable,binding_value_from_atom(kept)) &&
          bindings_capture_equal(arena,&left,&right),
          "freshening preserves nested expression roles and admits the equivalent captured value");
    Atom *cycle_left=atom_expr(arena,(Atom *[]){atom_int(arena,9)},1u);
    Atom *cycle_right=atom_expr(arena,(Atom *[]){atom_int(arena,9)},1u);
    CHECK(bindings_rewrite_value_id(&left,variable,binding_value_from_atom(cycle_left)) &&
          bindings_rewrite_value_id(&right,variable,binding_value_from_atom(cycle_right)),
          "cyclic comparison control starts from ordinary captured values");
    Atom *left_leaf=cycle_left->expr.elems[0],*right_leaf=cycle_right->expr.elems[0];
    cycle_left->expr.elems[0]=cycle_left;cycle_right->expr.elems[0]=cycle_right;
    CHECK(bindings_capture_equal_checked(arena,&left,&right)==CETTA_MATCH_INCOMPLETE,
          "unavailable captured graph inspection is not permission to select a different source");
    cycle_left->expr.elems[0]=left_leaf;cycle_right->expr.elems[0]=right_leaf;
    bindings_free(&copied); bindings_free(&framed_right); bindings_free(&framed_left);
    bindings_free(&alias); bindings_free(&right); bindings_free(&left);
}


typedef struct {
    unsigned references, retains, releases;
    bool refuse;
    uint64_t session,thunk,authority,child,child_authority;
} ReaderOwnerFixture;
static bool reader_owner_retain(void *raw) {
    ReaderOwnerFixture *owner=raw;
    if (owner->refuse) return false;
    owner->references++; owner->retains++;
    return true;
}
static void reader_owner_release(void *raw) {
    ReaderOwnerFixture *owner=raw;
    if (!owner->references) abort();
    owner->references--; owner->releases++;
}
static const PrimeNeedReaderValueOps reader_owner_ops={
    .retain=reader_owner_retain,.release=reader_owner_release,
};
static CettaMatchCompletion reader_owner_settled(const void *raw,const PrimeNeedSnapshot *world) {
    const ReaderOwnerFixture *owner=raw;
    if(!world || world->session_id!=owner->session) return CETTA_MATCH_REFUTED;
    uint64_t ids[]={owner->thunk,owner->child},authorities[]={owner->authority,owner->child_authority};
    for(size_t i=0u;i<2u;i++) {
        PrimeNeedCellView cell;
        if(!ids[i] || !prime_need_snapshot_lookup(world,ids[i],&cell)) return CETTA_MATCH_REFUTED;
        if(cell.authority_id!=authorities[i]) return CETTA_MATCH_INCOMPLETE;
        if(cell.cache_state!=PRIME_NEED_CACHE_VALUE && cell.cache_state!=PRIME_NEED_CACHE_STABLE_FAULT)
            return CETTA_MATCH_REFUTED;
    }
    return CETTA_MATCH_MATCHED;
}
static const PrimeNeedReaderValueOps settled_reader_ops={
    .retain=reader_owner_retain,.release=reader_owner_release,.settled=reader_owner_settled};
static bool reader_context_has(PrimeNeedReaderContext *context,uint64_t key,void *value) {
    PrimeNeedReaderValue found={0};
    return prime_need_reader_context_lookup(context,key,&found) &&
        found.value==value && found.operations==&reader_owner_ops;
}
typedef struct {size_t count;uint64_t sum;bool refuse;} ReaderVisitFixture;
static CettaMatchCompletion reader_visit_entry(uint64_t key,const PrimeNeedReaderValue *value,void *raw) {
    ReaderVisitFixture *visit=raw;
    if (!value->value || value->operations!=&reader_owner_ops) return CETTA_MATCH_INCOMPLETE;
    visit->count++;visit->sum+=key;
    return visit->refuse ? CETTA_MATCH_INCOMPLETE : CETTA_MATCH_MATCHED;
}
static void reader_context_cases(Arena *arena) {
    ReaderOwnerFixture a={.references=1u}, b={.references=1u}, c={.references=1u};
    PrimeNeedReaderValue av={&a,&reader_owner_ops}, bv={&b,&reader_owner_ops}, cv={&c,&reader_owner_ops};
    PrimeNeedReaderContext *base=NULL,*left=NULL,*right=NULL,*joined=NULL,*other=NULL,*removed=NULL;
    CHECK(prime_need_reader_context_update(NULL,11u,&av,&base)==CETTA_MATCH_MATCHED &&
        reader_context_has(base,11u,&a) && a.references==2u,
        "reader context owns its supplied current version");
    CHECK(prime_need_reader_context_update(base,11u,&bv,&left)==CETTA_MATCH_MATCHED &&
        reader_context_has(base,11u,&a) && reader_context_has(left,11u,&b),
        "advancing one reader context preserves its earlier branch version");
    CHECK(prime_need_reader_context_merge(base,left,&joined)==CETTA_MATCH_MATCHED && joined==left,
        "actual reader-context ancestry chooses the advanced version");
    prime_need_reader_context_release(joined);joined=NULL;
    prime_need_reader_context_release(base);base=NULL;
    CHECK(a.references==1u && b.references==2u,
        "lightweight ancestry does not pin an obsolete reader payload");
    CHECK(prime_need_reader_context_update(left,11u,NULL,&removed)==CETTA_MATCH_MATCHED &&
        !reader_context_has(removed,11u,&b) &&
        prime_need_reader_context_merge(left,removed,&joined)==CETTA_MATCH_MATCHED && joined==removed,
        "a retired reader is not resurrected by merging its earlier context");
    prime_need_reader_context_release(joined);joined=NULL;
    prime_need_reader_context_release(left);left=NULL;
    CHECK(b.references==1u,"retiring the last current value releases its owner despite surviving ancestry");
    CHECK(prime_need_reader_context_update(NULL,11u,&av,&other)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(other,removed,&joined)==CETTA_MATCH_REFUTED && !joined,
        "a sibling cannot replace a recorded retirement with an equal old reader");
    prime_need_reader_context_release(other);other=NULL;
    prime_need_reader_context_release(removed);removed=NULL;

    CHECK(prime_need_reader_context_update(NULL,11u,&av,&base)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(base,11u,&bv,&left)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(base,11u,&cv,&right)==CETTA_MATCH_MATCHED,
        "incomparable readers are built from one shared parent");
    CHECK(prime_need_reader_context_merge(left,right,&joined)==CETTA_MATCH_REFUTED && !joined,
        "different sibling reader positions refuse instead of selecting a maximum");
    prime_need_reader_context_release(right);right=NULL;
    CHECK(prime_need_reader_context_update(base,11u,&bv,&right)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(left,right,&joined)==CETTA_MATCH_MATCHED &&
        reader_context_has(joined,11u,&b),
        "independent versions can join when their current owned reader is identical");
    CHECK(prime_need_reader_context_update(joined,11u,&cv,&removed)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(left,removed,&other)==CETTA_MATCH_MATCHED && other==removed,
        "a joined version retains the ancestry of its left parent");
    prime_need_reader_context_release(other);other=NULL;
    CHECK(prime_need_reader_context_merge(right,removed,&other)==CETTA_MATCH_MATCHED && other==removed,
        "a joined version retains the ancestry of its right parent");
    prime_need_reader_context_release(other);other=NULL;
    prime_need_reader_context_release(removed);removed=NULL;
    prime_need_reader_context_release(joined);joined=NULL;
    prime_need_reader_context_release(left);left=NULL;
    prime_need_reader_context_release(right);right=NULL;
    CHECK(prime_need_reader_context_update(NULL,22u,&bv,&other)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(base,other,&joined)==CETTA_MATCH_MATCHED &&
        reader_context_has(joined,11u,&a) && reader_context_has(joined,22u,&b),
        "disjoint source-call reader contexts compose without identifying their owners");
    ReaderVisitFixture visit={0};
    unsigned live_a=a.references,live_b=b.references;
    CHECK(prime_need_reader_context_visit(joined,reader_visit_entry,&visit)==CETTA_MATCH_MATCHED &&
        visit.count==2u && visit.sum==33u && a.references==live_a && b.references==live_b,
        "reader inspection borrows every live key without changing its owners");
    visit=(ReaderVisitFixture){.refuse=true};
    CHECK(prime_need_reader_context_visit(joined,reader_visit_entry,&visit)==CETTA_MATCH_INCOMPLETE &&
        visit.count==1u && a.references==live_a && b.references==live_b,
        "a refused reader visit is incomplete and retains all owning entries");
    b.refuse=true;
    unsigned before_a=a.references,before_b=b.references,before_c=c.references;
    CHECK(prime_need_reader_context_update(joined,11u,&cv,&removed)==CETTA_MATCH_INCOMPLETE && !removed &&
        a.references==before_a && b.references==before_b && c.references==before_c,
        "refused reader retention rolls back all previously prepared ownership shares");
    b.refuse=false;
    prime_need_reader_context_release(joined);joined=NULL;
    prime_need_reader_context_release(other);other=NULL;


    /* Source and requester have different earlier reader values. Only the
     * actual source suffix may cross the production boundary. */
    PrimeNeedReaderContext *entrance=NULL,*production=NULL,*recipient=NULL,*imported=NULL,*later=NULL;
    CHECK(prime_need_reader_context_update(base,11u,&bv,&entrance)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(entrance,22u,&cv,&production)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(base,11u,&cv,&recipient)==CETTA_MATCH_MATCHED,
        "reader production and recipient retain unrelated earlier branch values");
    CHECK(prime_need_reader_context_import(entrance,production,recipient,&imported)==CETTA_MATCH_MATCHED &&
        reader_context_has(imported,11u,&c) && reader_context_has(imported,22u,&c),
        "reader suffix import preserves requester history and transports the earned nested owner");
    CHECK(prime_need_reader_context_update(imported,22u,&av,&later)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_import(entrance,production,later,&removed)==CETTA_MATCH_MATCHED && removed==later,
        "already-held reader production does not roll back later requester progress");
    prime_need_reader_context_release(removed);removed=NULL;
    CHECK(prime_need_reader_context_update(recipient,22u,&bv,&removed)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_import(entrance,production,removed,&joined)==CETTA_MATCH_REFUTED && !joined,
        "rival nested reader progress refuses before any production publication");
    CHECK(prime_need_reader_context_import(entrance,NULL,recipient,&joined)==CETTA_MATCH_INCOMPLETE && !joined,
        "missing production context is not treated as evidence of an unchanged active entrance");
    CHECK(prime_need_reader_context_import(base,production,imported,&joined)==CETTA_MATCH_REFUTED && !joined,
        "holding one reader suffix does not adopt the producer's earlier conflicting reader history");
    CHECK(prime_need_reader_context_merge(production,imported,&joined)==CETTA_MATCH_REFUTED && !joined,
        "a suffix import does not claim full-context ancestry from its producer");
    prime_need_reader_context_release(entrance);prime_need_reader_context_release(production);
    prime_need_reader_context_release(recipient);prime_need_reader_context_release(imported);
    prime_need_reader_context_release(later);prime_need_reader_context_release(removed);removed=NULL;

    Bindings seed, advanced, sibling, moved;
    bindings_init(&seed);bindings_init(&advanced);bindings_init(&sibling);bindings_init(&moved);
    bindings_reader_context_set(&seed,base);
    CHECK(bindings_prime_present(&seed) && bindings_clone(&sibling,&seed) &&
        bindings_reader_context_view(&sibling)==base,
        "a context-only occurrence survives ordinary binding cloning");
    CHECK(prime_need_reader_context_update(base,11u,&bv,&left)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(base,11u,&cv,&right)==CETTA_MATCH_MATCHED,
        "binding branches retain independent versions of their source reader");
    bindings_reader_context_set(&advanced,left);bindings_reader_context_set(&sibling,right);
    CHECK(bindings_try_merge_live_checked(&advanced,&sibling)==CETTA_MATCH_REFUTED &&
        bindings_reader_context_view(&advanced)==left,
        "checked binding merge refutes incompatible reader contexts without changing its destination");
    BindingsBuilder builder,copy;
    CHECK(bindings_builder_init(&builder,&seed),"reader fixture initializes its common binding owner");
    Atom *root=atom_var_with_id(arena,"reader-root",test_id(9801u));
    CHECK(bindings_builder_add_var_fresh(&builder,root,atom_int(arena,7)),
        "reader fixture retains a logical root alongside source context");
    uint32_t mark=bindings_builder_save(&builder);
    CHECK(bindings_builder_try_merge(&builder,&advanced) &&
        bindings_reader_context_view(&builder.current)==left && bindings_builder_clone(&copy,&builder),
        "builder snapshots and clones own their complete reader contexts");
    uint32_t marks[]={mark,bindings_builder_save(&builder)};
    CHECK(bindings_builder_compact_reachable(&builder,&root,1u,marks,2u,NULL,NULL) &&
        bindings_reader_context_view(&builder.current)==left,
        "binding compaction transports reader-owned checkpoints with its logical roots");
    bindings_builder_rollback(&builder,marks[0]);
    CHECK(bindings_reader_context_view(&builder.current)==base &&
        bindings_reader_context_view(&copy.current)==left,
        "rollback restores the exact reader version without rewinding a sibling clone");
    bindings_builder_free(&builder);
    bindings_builder_commit(&copy);bindings_builder_take(&copy,&moved);
    CHECK(bindings_reader_context_view(&moved)==left,
        "commit and ownership transfer preserve the selected reader context");
    Atom *copy_variable=atom_var_with_id(arena,"reader-copy",test_id(9802u));
    Atom *copy_fields[]={atom_symbol(arena,"ReaderPayload"),atom_int(arena,19)};
    Atom *copy_value=atom_expr(arena,copy_fields,2u);
    CHECK(bindings_add_var(&moved,copy_variable,copy_value),
        "copy refusal fixture includes a logical payload in source storage");
    Arena copy_arena;arena_init_detached(&copy_arena);
    AtomDeepCopySession *atoms=atom_deep_copy_session_new(&copy_arena);
    CHECK(atoms && !bindings_copy_owned_atoms_with_session(&moved,&copy_arena,atoms) &&
        bindings_reader_context_view(&moved)==left &&
        bindings_lookup_value_id(&moved,copy_variable->var_id).skeleton==copy_value,
        "an active reader context refuses unmapped owned copying instead of sharing a mutable driver silently");
    BindingsBuilder unmapped;
    CHECK(bindings_builder_init(&unmapped,&moved) &&
        bindings_builder_add_var_fresh(&unmapped,
            atom_var_with_id(arena,"reader-trail-copy",test_id(9803u)),copy_value),
        "copy refusal fixture retains a reader only in its rollback trail");
    bindings_prime_set(&unmapped.current,NULL,NULL,0u,NULL,NULL);
    CHECK(!bindings_builder_copy_owned_atoms_with_session(&unmapped,&copy_arena,atoms) &&
        bindings_lookup_value_id(&unmapped.current,copy_variable->var_id).skeleton==copy_value,
        "an unmapped rollback reader refuses before relocating any logical payload");
    bindings_builder_free(&unmapped);
    atom_deep_copy_session_free(atoms);arena_free(&copy_arena);
    bindings_free(&moved);bindings_free(&advanced);bindings_free(&sibling);
    bindings_prime_set(&seed,NULL,NULL,0u,NULL,NULL);
    CHECK(!bindings_prime_present(&seed) && !bindings_reader_context_view(&seed),
        "an explicit empty occurrence releases its reader ownership edge");
    bindings_free(&seed);
    prime_need_reader_context_release(base);base=NULL;
    prime_need_reader_context_release(left);left=NULL;
    prime_need_reader_context_release(right);right=NULL;
    CHECK(a.references==1u && b.references==1u && c.references==1u,
        "context, builder, compaction and transfer release every retained reader share");

    PrimeNeedReaderContext *chain=NULL;
    bool chain_ok=true;
    for (size_t i=0u;i<8192u && chain_ok;i++)
        chain_ok=prime_need_reader_context_update(chain,31u,i%2u?&av:&bv,&chain)==CETTA_MATCH_MATCHED;
    CHECK(chain_ok && a.references+b.references==3u,
        "a long context lineage retains only its current payload rather than every older reader");
    prime_need_reader_context_release(chain);
    CHECK(a.references==1u && b.references==1u,
        "deep version retirement releases its lineage and current reader completely");
}

static const char reader_copy_owner_kind;
static const PrimeNeedReaderValueOps copied_reader_ops={
    .retain=reader_owner_retain,.release=reader_owner_release,
    .copy_kind=&reader_copy_owner_kind,.copy_size=sizeof(ReaderOwnerFixture)};

static bool copied_reader_has(const PrimeNeedReaderContext *context,uint64_t key,void *owner) {
    PrimeNeedReaderValue value={0};
    return prime_need_reader_context_lookup(context,key,&value) &&
        value.value==owner && value.operations==&copied_reader_ops;
}

static void reader_context_copy_cases(void) {
    ReaderOwnerFixture source_owners[3]={{.references=1u},{.references=1u},{.references=1u}};
    ReaderOwnerFixture image_owners[3]={{.references=1u},{.references=1u},{.references=1u}};
    PrimeNeedReaderValue values[3];
    for (size_t i=0u;i<3u;i++) values[i]=(PrimeNeedReaderValue){&source_owners[i],&copied_reader_ops};
    PrimeNeedReaderContext *source[7]={0},*images[7]={0},*out=NULL,*alias=NULL;
    enum {BASE,LEFT,RIGHT,JOINED,REMOVED,DEEP,IMPORTED};
    Arena destination,syntax;arena_init_detached(&destination);arena_init_detached(&syntax);
    Bindings captured,original;bindings_init(&captured);bindings_init(&original);
    BindingsBuilder builder={0};bool builder_ready=false;
    AtomDeepCopySession *session=NULL;
    bool ready=prime_need_reader_context_update(NULL,11u,&values[0],&source[BASE])==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(source[BASE],22u,&values[1],&source[LEFT])==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(source[BASE],33u,&values[2],&source[RIGHT])==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(source[LEFT],source[RIGHT],&source[JOINED])==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(source[LEFT],22u,NULL,&source[REMOVED])==CETTA_MATCH_MATCHED;
    source[DEEP]=source[RIGHT];prime_need_reader_context_retain(source[DEEP]);
    for (size_t i=0u;ready && i<4096u;i++)
        ready=prime_need_reader_context_update(source[DEEP],44u,&values[i%2u],&source[DEEP])==CETTA_MATCH_MATCHED;
    ready=ready && prime_need_reader_context_import(source[BASE],source[DEEP],source[LEFT],
        &source[IMPORTED])==CETTA_MATCH_MATCHED;
    CHECK(ready,"prepare shared reader ancestry, retirement and a deep imported production");
    if (!ready) goto done;
    out=source[LEFT];prime_need_reader_context_retain(out);
    /* Each failed graph attempt owns its own unpublished session. */
    for (unsigned refusal=0u;refusal<6u;refusal++) {
        session=atom_deep_copy_session_new(&destination);
        CHECK(session!=NULL,"prepare reader transport refusal session");
        if (!session) goto done;
        for (size_t i=0u;i<3u;i++) {
            if (refusal==0u || (refusal==3u && i==1u)) continue;
            void *image=refusal==1u ? (void *)&source_owners[i] : (void *)&image_owners[i];
            if (refusal==5u && i<2u) image=&source_owners[1u-i];
            size_t bytes=sizeof(source_owners[i])-(refusal==2u);
            CHECK(atom_deep_copy_session_object_record(session,&reader_copy_owner_kind,
                &source_owners[i],image,bytes),"register the selected actual opaque owner mapping");
        }
        image_owners[1].refuse=refusal==4u;
        CHECK(prime_need_reader_context_copy_with_session(source[JOINED],&destination,session,&out)==
                CETTA_MATCH_INCOMPLETE && out==source[LEFT] &&
                copied_reader_has(source[JOINED],11u,&source_owners[0]),
            "unregistered, identity, wrong-extent, partial, refused and crossed owner copies preserve both roots");
        image_owners[1].refuse=false;
        atom_deep_copy_session_free(session);session=NULL;
        CHECK(image_owners[0].references==1u && image_owners[1].references==1u &&
            image_owners[2].references==1u,"unpublished reader images release every acquired value");
    }
    session=atom_deep_copy_session_new(&destination);
    CHECK(session!=NULL,"prepare common reader graph transport");
    if (!session) goto done;
    for (size_t i=0u;i<3u;i++)
        ready=ready && atom_deep_copy_session_object_record(session,&reader_copy_owner_kind,
            &source_owners[i],&image_owners[i],sizeof(source_owners[i]));
    for (size_t i=0u;ready && i<7u;i++) {
        ready=prime_need_reader_context_copy_with_session(source[i],&destination,session,&images[i])==
            CETTA_MATCH_MATCHED;
        CHECK(ready && images[i]!=source[i],"reader versions receive independent objects through one session");
    }
    if (!ready) goto done;
    CHECK(copied_reader_has(images[JOINED],11u,&image_owners[0]) &&
        copied_reader_has(images[JOINED],22u,&image_owners[1]) &&
        copied_reader_has(images[JOINED],33u,&image_owners[2]),
        "equal reader owners across versions retain one mapped image and the exact entry keys");
    CHECK(prime_need_reader_context_copy_with_session(source[JOINED],&destination,session,&alias)==
            CETTA_MATCH_MATCHED && alias==images[JOINED] &&
        prime_need_reader_context_copy_with_session(alias,&destination,session,&alias)==
            CETTA_MATCH_MATCHED && alias==images[JOINED],
        "reader graph transport is idempotent for both source and already transported roots");
    CHECK(prime_need_reader_context_merge(images[BASE],images[LEFT],&out)==CETTA_MATCH_MATCHED &&
            out==images[LEFT],"copied reader versions preserve actual ancestry selection");
    CHECK(prime_need_reader_context_merge(images[LEFT],images[RIGHT],&out)==CETTA_MATCH_MATCHED &&
        copied_reader_has(out,11u,&image_owners[0]) && copied_reader_has(out,22u,&image_owners[1]) &&
        copied_reader_has(out,33u,&image_owners[2]),"copied sibling versions preserve their disjoint merge");
    CHECK(prime_need_reader_context_merge(images[LEFT],images[REMOVED],&out)==CETTA_MATCH_MATCHED &&
            out==images[REMOVED] && !copied_reader_has(out,22u,&image_owners[1]),
        "copying a retired version cannot resurrect its earlier reader");
    CHECK(prime_need_reader_context_import(images[BASE],images[DEEP],images[IMPORTED],&out)==
            CETTA_MATCH_MATCHED && out==images[IMPORTED],
        "an imported production remains adopted after all four lineage edges are transported");
    PrimeNeedReaderContext *before=out;
    CHECK(prime_need_reader_context_merge(source[BASE],images[BASE],&out)==CETTA_MATCH_REFUTED && out==before,
        "independent source and image owners are not identified by equal reader keys");
    Atom *variable=atom_var_with_id(&syntax,"transported-reader",test_id(9860u));
    Atom *payload=atom_expr2(&syntax,atom_symbol(&syntax,"ReaderOwned"),atom_int(&syntax,19));
    bindings_reader_context_set(&captured,source[LEFT]);
    CHECK(bindings_add_var(&captured,variable,payload) && bindings_clone(&original,&captured),
        "reader transport has a real binding payload and an independently held source");
    CHECK(bindings_copy_owned_atoms_with_session(&captured,&destination,session) &&
        bindings_reader_context_view(&captured)==images[LEFT] &&
        bindings_reader_context_view(&original)==source[LEFT] &&
        bindings_lookup_value_id(&captured,variable->var_id).skeleton!=payload &&
        atom_eq(bindings_lookup_value_id(&captured,variable->var_id).skeleton,payload),
        "owned bindings use the prepared reader image and relocate their logical payload");
    builder_ready=bindings_builder_init(&builder,&original);
    uint32_t checkpoint=builder_ready ? bindings_builder_save(&builder) : 0u;
    bool built=builder_ready && bindings_builder_add_var_fresh(&builder,
        atom_var_with_id(&syntax,"reader-copy-trail",test_id(9861u)),payload);
    if (built) bindings_reader_context_set(&builder.current,source[RIGHT]);
    bool transported=built && builder.prime_trail_len &&
        bindings_builder_copy_owned_atoms_with_session(&builder,&destination,session);
    CHECK(transported && bindings_reader_context_view(&builder.current)==images[RIGHT],
        "a binding builder transports its current reader and rollback versions together");
    if (transported) {
        bindings_builder_rollback(&builder,checkpoint);
        CHECK(bindings_reader_context_view(&builder.current)==images[LEFT] &&
            bindings_lookup_value_id(&builder.current,variable->var_id).skeleton==
                bindings_lookup_value_id(&captured,variable->var_id).skeleton,
            "rollback selects the corresponding copied version and shared logical image");
    }
    /* Source and target metadata have explicit session holds, not arena
     * lifetime authority. Reset invalidates the registration for reuse. */
    ArenaMark mark=arena_mark(&destination);arena_reset(&destination,mark);
    CHECK(prime_need_reader_context_copy_with_session(source[BASE],&destination,session,&out)==
            CETTA_MATCH_INCOMPLETE && out==before,
        "a reset destination cannot authorize reuse of stale reader-owner mappings");
    bindings_free(&original);
    for (size_t i=0u;i<7u;i++) {
        prime_need_reader_context_release(source[i]);source[i]=NULL;
    }
    atom_deep_copy_session_free(session);session=NULL;
    CHECK(source_owners[0].references==1u && source_owners[1].references==1u &&
        source_owners[2].references==1u && copied_reader_has(images[JOINED],33u,&image_owners[2]) &&
        prime_need_reader_context_import(images[BASE],images[DEEP],images[IMPORTED],&out)==
            CETTA_MATCH_MATCHED && out==images[IMPORTED],
        "copied versions and imported ancestry survive source retirement and session disposal");
done:
    if (builder_ready) bindings_builder_free(&builder);
    bindings_free(&captured);bindings_free(&original);
    prime_need_reader_context_release(out);prime_need_reader_context_release(alias);
    for (size_t i=0u;i<7u;i++) {
        prime_need_reader_context_release(source[i]);
        prime_need_reader_context_release(images[i]);
    }
    atom_deep_copy_session_free(session);
    arena_free(&syntax);arena_free(&destination);
    CHECK(source_owners[0].references==1u && source_owners[1].references==1u &&
        source_owners[2].references==1u && image_owners[0].references==1u &&
        image_owners[1].references==1u && image_owners[2].references==1u,
        "reader graph copy and failure cleanup release both endpoint graphs completely");
}

typedef struct {
    size_t visits,fail_on;
    uint64_t previous;
    bool keep_all,clear_all;
    CettaMatchCompletion refusal;
} ReaderClearFixture;
static CettaMatchCompletion reader_clear_selected(uint64_t key,
        const PrimeNeedReaderValue *value,void *raw,bool *clear) {
    ReaderClearFixture *selection=raw;
    CHECK(value->value && key>selection->previous,"bulk clear visits live keys once in order");
    selection->previous=key;selection->visits++;
    if(selection->fail_on==selection->visits)return selection->refusal;
    *clear=selection->clear_all || (!selection->keep_all && (key==11u || key==33u));
    return CETTA_MATCH_MATCHED;
}

static void reader_context_batch_cases(void) {
    ReaderOwnerFixture owners[4]={{.references=1u},{.references=1u},{.references=1u},{.references=1u}};
    PrimeNeedReaderValue values[4];
    PrimeNeedReaderContext *base=NULL,*serial=NULL,*batch=NULL,*out=NULL,*recipient=NULL;
    PrimeNeedReaderContext *imported=NULL,*foreign=NULL,*conflict=NULL,*retired=NULL;
    bool ready=true;
    for(size_t i=0u;i<4u;i++) {
        values[i]=(PrimeNeedReaderValue){&owners[i],&reader_owner_ops};
        ready=ready && prime_need_reader_context_update(base,11u*(i+1u),&values[i],&base)==CETTA_MATCH_MATCHED;
    }
    CHECK(ready,"prepare ordered reader map for bulk retirement");
    if(!ready)goto done;
    ReaderClearFixture keep={.keep_all=true};
    CHECK(prime_need_reader_context_clear_if(base,reader_clear_selected,&keep,&out)==CETTA_MATCH_MATCHED &&
        out==base && keep.visits==4u,"no-op bulk clear preserves exact context identity");
    prime_need_reader_context_release(out);out=NULL;
    for(unsigned refusal=0u;refusal<2u;refusal++) {
        prime_need_reader_context_retain(base);out=base;
        unsigned before[4];for(size_t i=0u;i<4u;i++)before[i]=owners[i].references;
        ReaderClearFixture decline={.fail_on=3u,
            .refusal=refusal?CETTA_MATCH_REFUTED:CETTA_MATCH_INCOMPLETE};
        CHECK(prime_need_reader_context_clear_if(base,reader_clear_selected,&decline,&out)==decline.refusal &&
            out==base && decline.visits==3u,"partial bulk refusal preserves the owned output version");
        for(size_t i=0u;i<4u;i++)CHECK(owners[i].references==before[i],"bulk refusal rolls back retained prefix owners");
        prime_need_reader_context_release(out);out=NULL;
    }
    prime_need_reader_context_retain(base);out=base;
    owners[3].refuse=true;
    ReaderClearFixture refused_retain={0};
    CHECK(prime_need_reader_context_clear_if(base,reader_clear_selected,&refused_retain,&out)==CETTA_MATCH_INCOMPLETE &&
        out==base,"retained-prefix refusal cannot publish a partial retirement");
    owners[3].refuse=false;
    prime_need_reader_context_release(out);out=NULL;
    for(size_t i=0u;i<4u;i++)CHECK(owners[i].references==2u,"failed bulk preparation leaves only base shares");
    ReaderClearFixture selected={0};
    CHECK(prime_need_reader_context_clear_if(base,reader_clear_selected,&selected,&batch)==CETTA_MATCH_MATCHED &&
        selected.visits==4u && !reader_context_has(batch,11u,&owners[0]) &&
        !reader_context_has(batch,33u,&owners[2]) && reader_context_has(batch,22u,&owners[1]) &&
        reader_context_has(batch,44u,&owners[3]),"bulk clear keeps the selected complement without touching base");
    CHECK(reader_context_has(base,11u,&owners[0]) && reader_context_has(base,33u,&owners[2]),
        "bulk retirement preserves independently retained source versions");
    CHECK(prime_need_reader_context_update(base,11u,NULL,&serial)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_update(serial,33u,NULL,&serial)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(serial,batch,&out)==CETTA_MATCH_MATCHED &&
        !reader_context_has(out,11u,&owners[0]) && !reader_context_has(out,33u,&owners[2]) &&
        reader_context_has(out,22u,&owners[1]) && reader_context_has(out,44u,&owners[3]),
        "batch and sequential retirement have the same merge-visible map");
    prime_need_reader_context_release(out);out=NULL;
    CHECK(prime_need_reader_context_merge(base,batch,&out)==CETTA_MATCH_MATCHED && out==batch,
        "batch lineage remains an actual descendant of its source");
    prime_need_reader_context_release(out);out=NULL;
    CHECK(prime_need_reader_context_update(NULL,11u,&values[0],&foreign)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_merge(batch,foreign,&out)==CETTA_MATCH_REFUTED && !out,
        "batch tombstones prevent sibling resurrection of equal retired readers");
    CHECK(prime_need_reader_context_update(base,22u,&values[0],&recipient)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_import(base,batch,recipient,&imported)==CETTA_MATCH_MATCHED &&
        reader_context_has(imported,22u,&owners[0]) && reader_context_has(imported,44u,&owners[3]) &&
        !reader_context_has(imported,11u,&owners[0]) && !reader_context_has(imported,33u,&owners[2]),
        "bulk import installs retirements while preserving unrelated recipient progress");
    PrimeNeedReaderContext *identity=imported;
    CHECK(prime_need_reader_context_import(base,batch,imported,&imported)==CETTA_MATCH_MATCHED && imported==identity,
        "repeat bulk import retains exact idempotent progress identity");
    CHECK(prime_need_reader_context_update(base,11u,&values[3],&conflict)==CETTA_MATCH_MATCHED,
        "prepare recipient conflicting with a batch retirement");
    prime_need_reader_context_retain(base);out=base;
    CHECK(prime_need_reader_context_import(base,batch,conflict,&out)==CETTA_MATCH_REFUTED && out==base,
        "conflicting batch import leaves the owned output unchanged");
    prime_need_reader_context_release(out);out=NULL;
    ReaderClearFixture all={.clear_all=true};
    CHECK(prime_need_reader_context_clear_if(batch,reader_clear_selected,&all,&retired)==CETTA_MATCH_MATCHED &&
        all.visits==2u && prime_need_reader_context_values_absent(retired),
        "existing tombstones survive a later clear and are not revisited");
    ReaderClearFixture unexpected={.fail_on=1u,.refusal=CETTA_MATCH_INCOMPLETE};
    CHECK(prime_need_reader_context_clear_if(retired,reader_clear_selected,&unexpected,&out)==CETTA_MATCH_MATCHED &&
        out==retired && unexpected.visits==0u,"an already retired map is an allocation-free identity");
    prime_need_reader_context_release(out);out=NULL;
done:
    prime_need_reader_context_release(base);prime_need_reader_context_release(serial);
    prime_need_reader_context_release(batch);prime_need_reader_context_release(out);
    prime_need_reader_context_release(recipient);prime_need_reader_context_release(imported);
    prime_need_reader_context_release(foreign);prime_need_reader_context_release(conflict);
    prime_need_reader_context_release(retired);
    for(size_t i=0u;i<4u;i++)CHECK(owners[i].references==1u && owners[i].retains==owners[i].releases,
        "bulk retirement and import release every owned reader share once");
}

static void reader_settlement_cases(Arena *arena) {
    PrimeNeedSnapshot root={0},child_world={0},base={0},running={0},partial={0},ready={0};
    uint64_t child=0u,thunk=0u,evaluator=prime_need_fresh_evaluator_id();
    Atom *origin=atom_symbol(arena,"settlement-source");
    bool allocated=prime_need_snapshot_begin(&root) &&
        prime_need_snapshot_allocate(arena,&root,origin,&child_world,&child) &&
        prime_need_snapshot_allocate(arena,&child_world,origin,&base,&thunk);
    CHECK(allocated,"settlement controls allocate a source and its actual nested field");
    if(!allocated) return;
    PrimeNeedCellView root_cell,child_cell;
    bool known=prime_need_snapshot_lookup(&base,thunk,&root_cell) && prime_need_snapshot_lookup(&base,child,&child_cell);
    ReaderOwnerFixture owner={.references=1u,.session=base.session_id,.thunk=thunk,.child=child,
        .authority=known?root_cell.authority_id:0u,.child_authority=known?child_cell.authority_id:0u};
    PrimeNeedReaderValue value={.value=&owner,.operations=&settled_reader_ops};
    PrimeNeedReaderContext *context=NULL,*merged=NULL;
    CHECK(known && prime_need_reader_context_update(NULL,71u,&value,&context)==CETTA_MATCH_MATCHED,
        "a settled-reader owner records exact cell authorities");
    Bindings source,promoted;bindings_init(&source);bindings_init(&promoted);
    *bindings_need_mut(&source)=base;bindings_reader_context_set(&source,context);
    Arena destination;arena_init_detached(&destination);
    CHECK(!bindings_promote_atoms_to_arena(&source,&destination) && bindings_reader_context_view(&source)==context,
        "an unforced source refuses ordinary promotion without erasing its reader");
    Atom *nested=prime_need_ref(arena,&base,child);
    Atom *box=nested?atom_expr2(arena,atom_symbol(arena,"Box"),nested):NULL;
    bool resolved=box && prime_need_snapshot_start_evaluation(arena,&base,thunk,evaluator,&running) &&
        prime_need_snapshot_resolve_value(arena,&running,thunk,evaluator,box,&partial);
    CHECK(resolved,"a completed constructor retains the actual still-pending field capability");
    *bindings_need_mut(&source)=partial;
    CHECK(prime_need_reader_context_can_settle(context,&partial)==CETTA_MATCH_REFUTED &&
        !bindings_promote_atoms_to_arena(&source,&destination),
        "a cached root does not settle its pending nested field");
    resolved=resolved && prime_need_snapshot_start_evaluation(arena,&partial,child,evaluator,&running) &&
        prime_need_snapshot_resolve_value(arena,&running,child,evaluator,atom_int(arena,8),&ready);
    CHECK(resolved,"the nested field earns its own completed update");
    *bindings_need_mut(&source)=ready;
    CHECK(bindings_clone(&promoted,&source) && bindings_promote_atoms_to_arena(&promoted,&destination) &&
        prime_need_reader_context_values_absent(bindings_reader_context_view(&promoted)) &&
        prime_need_reader_context_lookup(context,71u,&value),
        "ordinary promotion tombstones only the promoted occurrence and preserves the source frontier owner");
    CHECK(prime_need_reader_context_merge(context,bindings_reader_context_view(&promoted),&merged)==CETTA_MATCH_MATCHED &&
        prime_need_reader_context_values_absent(merged),"an old live version cannot resurrect settled reader metadata");
    AtomDeepCopySession *copy=atom_deep_copy_session_new(&destination);
    CHECK(copy && !bindings_copy_owned_atoms_with_session(&promoted,&destination,copy),
        "settled ordinary promotion grants no independent owned-context copy");
    atom_deep_copy_session_free(copy);
    BindingsBuilder builder={0};
    *bindings_need_mut(&source)=partial;
    Atom *variable=atom_var_with_id(arena,"settlement-rollback",test_id(9810u));
    Atom *logical=atom_expr2(arena,atom_symbol(arena,"HeldLogical"),atom_int(arena,9));
    bool built=bindings_builder_init(&builder,&source) && bindings_builder_add_var_fresh(&builder,variable,logical);
    CHECK(built && builder.prime_trail_len,"a real rollback trail retains the earlier pending reader world");
    if(built) {
        *bindings_need_mut(&builder.current)=ready;
        CHECK(!bindings_builder_promote_atoms_to_arena(&builder,&destination) &&
            bindings_lookup_value_id(&builder.current,variable->var_id).skeleton==logical,
            "rollback-reader preflight refuses before relocating any current logical payload");
    }
    bindings_builder_free(&builder);
    prime_need_reader_context_release(merged);bindings_free(&promoted);bindings_free(&source);
    prime_need_reader_context_release(context);arena_free(&destination);
    CHECK(owner.references==1u,"settlement and refusal release every independently owned reader share");
}

static void materialized_match_cases(Arena *arena) {
    Atom *x = atom_var_with_id(arena, "materialized-x", test_id(70001u));
    Atom *y = atom_var_with_id(arena, "materialized-y", test_id(70002u));
    Atom *payload = atom_expr3(arena, atom_symbol(arena, "Repeated"), y, y);
    Atom *pattern = atom_expr2(arena, atom_symbol(arena, "Envelope"), x);
    Atom *value = atom_expr2(arena, atom_symbol(arena, "Envelope"), payload);
    BindingsBuilder builder;
    CHECK(bindings_builder_init(&builder, NULL), "initialize materialized matching");
    CHECK(match_atoms_builder_checked(pattern, value, &builder, arena, UINT64_MAX) ==
        CETTA_MATCH_MATCHED, "a computed open value needs no fresh rule activation");
    Atom *observed = bindings_apply(&builder.current, arena, x);
    CHECK(atom_eq(observed, payload), "materialized matching preserves existing free-variable identities");
    bindings_builder_free(&builder);

    Bindings seed;
    bindings_init(&seed);
    CHECK(bindings_add_var(&seed, y, atom_int(arena, 9)) &&
        bindings_builder_init(&builder, &seed), "supply an existing computed-variable binding");
    CHECK(match_atoms_builder_checked(pattern, value, &builder, arena, UINT64_MAX) ==
        CETTA_MATCH_MATCHED && atom_eq(
            bindings_apply(&builder.current, arena, x),
            atom_expr3(arena, atom_symbol(arena, "Repeated"),
                atom_int(arena, 9), atom_int(arena, 9))),
        "a computed value shares its caller's variable, rather than freshening it");
    bindings_builder_free(&builder);
    bindings_free(&seed);

    CHECK(bindings_builder_init(&builder, NULL), "initialize checked materialized controls");
    CHECK(match_atoms_builder_checked(pattern, value, &builder, arena, 0u) ==
        CETTA_MATCH_INCOMPLETE && bindings_builder_bindings(&builder)->len == 0u,
        "zero matching allowance cannot prove materialized absence");
    CHECK(match_atoms_builder_checked(
        atom_expr3(arena, atom_symbol(arena, "Pair"), x, x),
        atom_expr3(arena, atom_symbol(arena, "Pair"),
            atom_int(arena, 1), atom_int(arena, 2)),
        &builder, arena, UINT64_MAX) == CETTA_MATCH_REFUTED,
        "a repeated materialized variable refuses incompatible values");
    bindings_builder_free(&builder);
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
    materialized_match_cases(&arena);
    reader_context_cases(&arena);
    reader_context_copy_cases();
    reader_context_batch_cases();
    reader_settlement_cases(&arena);
    check_capture_agreement(&arena);

    for (unsigned mode = 0u; mode < (CETTA_BUILD_WITH_PRIME_CAUSAL_RECEIPTS ? 5u : 1u); mode++)
        for (unsigned route = 0u; route < 2u; route++)
            test_prime_occurrence_product(&arena, mode, route != 0u);

    test_dense_term_instantiation(&arena);
    test_arena_retained_owners();
    test_saved_binding_value_lifetime();
    test_saved_binding_value_concurrent_lifetime();
    test_sparse_frame_write_context_growth(&arena);
    test_resolved_frame_write_boundaries(&arena);

    test_borrowed_root_identity(&arena);
    test_term_stability_summary(&arena);
    test_dense_frame_indexed_suffix(&arena);
    test_joint_world_promotion(0u);
    test_joint_world_promotion(1u);
    test_joint_world_promotion(2u);
    test_saved_contextual_frame_refinement();
    test_generation_checked_frame_handles(&arena);
    test_persistent_frame_index_partition(&arena);
    test_frame_slot_is_value_authority(&arena);
    test_shared_carrier_promotion();
    test_frame_slot_promotion_lifetime();
    test_manufactured_slot_lifecycle();
    test_manufactured_slot_presentation();
    test_manufactured_presentation_reuses_private_capacity();
    test_frame_index_cycle_bridge(&arena);
    test_frame_only_merge_occurs_check(&arena);
    test_frame_schema_branch_lifetime(&arena);
    test_builder_frame_registration_rollback(&arena);
    test_frame_registration_compaction_rebase(&arena);
    test_contextual_previous_rhs_compaction_rollback(&arena);
    test_all_unbound_logical_merge_authority(&arena);
    test_logical_merge_frame_lineage(&arena);
    test_variable_inventory(&arena);
    test_unframed_context_import(&arena);
    test_frame_registration_undo_promotion();
    test_frame_schema_transport_lifetime();
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
    test_binding_batch(&arena);
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
              modern_result->symbol_number == modern_spelling,
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
        NULL, NULL);
    sparse_effect_fixture =
        sparse_effect_fixture &&
        bindings_builder_add_id_fresh(
            &sparse_effect_branch, test_id(6501u), SYMBOL_ID_NONE,
            atom_int(&arena, 6501)) &&
        sparse_effect_branch.trail_len == 2u &&
        sparse_effect_branch.prime_trail_len == 1u &&
        sparse_effect_branch.trail[1].prime_state_present;
    bindings_prime_set(
        &sparse_effect_branch.current, NULL, NULL, 0u, NULL, NULL);
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
        &sparse_compact_branch.current, &compact_need, NULL, 0u, NULL, NULL);
    Atom *sparse_compact_live = atom_var_with_id(
        &arena, "sparse-compact-live", test_id(6510u));
    sparse_compact_fixture =
        sparse_compact_fixture &&
        bindings_builder_add_var_fresh(
            &sparse_compact_branch, sparse_compact_live,
            atom_int(&arena, 6510));
    bindings_prime_set(
        &sparse_compact_branch.current, NULL, NULL, 0u, NULL, NULL);
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
        NULL, NULL);
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
        &sparse_commit_branch.current, &committed_need, NULL, 0u, NULL, NULL);
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
        var_epoch_id(epoch_local->var_id, (CettaFrameIdentity){17u});
    Bindings epoch_environment;
    bindings_init(&epoch_environment);
    bool epoch_fixture =
        bindings_add_id(
            &epoch_environment, test_id(7199u), SYMBOL_ID_NONE,
            atom_int(&arena, -1)) &&
        bindings_add_id(
            &epoch_environment, epoch_local_id,
            atom_symbol_current_id(epoch_local), epoch_outer) &&
        bindings_add_id(
            &epoch_environment, epoch_outer->var_id,
            atom_symbol_current_id(epoch_outer), atom_int(&arena, 77)) &&
        bindings_add_id(
            &epoch_environment, test_id(7202u), SYMBOL_ID_NONE,
            atom_int(&arena, -2));
    BindingsEpochRoot epoch_root = {
        .atom = epoch_local,
        .epoch = {17u},
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
        .epoch = {17u},
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
        .epoch = {17u},
    };
    VarId canonical_epoch_id = support_ready
        ? var_epoch_id(canonical_epoch_local->var_id, (CettaFrameIdentity){17u})
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
            atom_symbol_current_id(canonical_epoch_local), epoch_outer) &&
        bindings_add_id(
            &canonical_epoch_environment, epoch_outer->var_id,
            atom_symbol_current_id(epoch_outer), atom_int(&arena, 77)) &&
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
        .epoch = CETTA_FRAME_IDENTITY_NONE,
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
        &epoch_environment, &arena, epoch_local, (CettaFrameIdentity){17u});
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
              bindings_builder_add_id_fresh(&activation, var_epoch_id(prefix->var_id, (CettaFrameIdentity){29u}),
                  atom_symbol_current_id(prefix), value), "activation prefix fixture");
        VarId ids[] = {source->var_id, prefix->var_id};
        Atom *variables[] = {source, prefix};
        BindingsActivationView view;
        bindings_activation_view_init(&view);
        CHECK(bindings_activation_view_prepare(&view, &activation, ids, variables, 2u, (CettaFrameIdentity){29u}, 1u) &&
              bindings_builder_add_id_fresh(&activation, var_epoch_id(source->var_id, (CettaFrameIdentity){29u}),
                  atom_symbol_current_id(source), outer), "activation writes use versioned slots");
        Atom *local = bindings_apply_epoch_since(&activation.current, &arena, source, (CettaFrameIdentity){29u}, 1u);
        Atom *full = bindings_apply_activation_view_then_all(&activation.current, &arena, source, &view);
        CHECK(local == outer && full == value,
              "activation substitution and outer alias resolution compose exactly");
        Atom *closed = NULL;
        CHECK(bindings_resolve_epoch_view_ground(&activation.current, source, (CettaFrameIdentity){29u}, 1u, &closed) &&
              closed == value, "direct activation observation follows a slot alias");
        CHECK(bindings_resolve_epoch_view_ground(&activation.current, prefix, (CettaFrameIdentity){29u}, 1u, &closed) &&
              !closed, "activation observation excludes the preceding slot version");
        CHECK(!bindings_apply_epoch_since(&activation.current, &arena, source, (CettaFrameIdentity){29u}, 2u) &&
              !bindings_resolve_epoch_view_ground(&activation.current, source, (CettaFrameIdentity){29u}, 2u, &closed),
              "an invalid activation boundary fails closed");
        Bindings retained;
        CHECK(bindings_clone(&retained, &activation.current) &&
              bindings_rewrite_value_id(&activation.current, var_epoch_id(source->var_id, (CettaFrameIdentity){29u}),
                  binding_value_from_atom(atom_int(&arena, 7305))),
              "a slot rewrite retains an independent branch image");
        Atom *rewritten = bindings_apply_activation_view_then_all(&activation.current, &arena, source, &view);
        CHECK(rewritten && atom_eq(rewritten, atom_int(&arena, 7305)) &&
              bindings_apply_epoch_then_all(&retained, &arena, source, (CettaFrameIdentity){29u}, 1u) == value,
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
        CettaFrameIdentity first_identity = CETTA_FRAME_IDENTITY_NONE;
        CettaFrameIdentity next_identity = CETTA_FRAME_IDENTITY_NONE;
        VarId id = var_id_from_base(UINT64_C(1));
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
              !cetta_frame_identity_eq(next_identity, first_identity) &&
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
            if (dense_ids[right_index].base >= dense_ids[left_index].base)
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
        if (var_id_eq(dense_ids[index], dense_left->var_id))
            dense_left_slot = index;
        if (var_id_eq(dense_ids[index], dense_open->var_id))
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
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
            dense_first_entry);
    dense_frame_ready = dense_prepared &&
        bindings_builder_add_id_fresh(
            &dense_frame_builder,
            var_epoch_id(dense_left->var_id, (CettaFrameIdentity){67u}),
            atom_symbol_current_id(dense_left), dense_outer) &&
        bindings_builder_add_id_fresh(
            &dense_frame_builder,
            var_epoch_id(dense_right->var_id, (CettaFrameIdentity){67u}),
            atom_symbol_current_id(dense_right), dense_value);
    dense_prepared = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
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
              (CettaFrameIdentity){67u}, dense_first_entry)
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
        &arena, "dense-coord-a", var_id_from_base(UINT64_C(7600)));
    Atom *dense_coord_b = atom_var_with_id(
        &arena, "dense-coord-b", var_id_from_base(UINT64_C(7601)));
    Atom *dense_coord_c = atom_var_with_id(
        &arena, "dense-coord-c", var_id_from_base(UINT64_C(7602)));
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
            3u, (CettaFrameIdentity){67u}, dense_first_entry);
    bool dense_contiguous_recognized = dense_contiguous_prepared &&
        dense_coordinate_frame.source_ids_contiguous &&
        var_id_eq(dense_coordinate_frame.source_first_id, dense_coord_a->var_id);
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
              dense_coordinate_source, (CettaFrameIdentity){67u}, dense_first_entry)
        : NULL;
    bool dense_sparse_prepared = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_coordinate_frame, &dense_frame_builder,
            dense_sparse_ids, dense_sparse_variables,
            2u, (CettaFrameIdentity){67u}, dense_first_entry);
    Atom *dense_below = atom_var_with_id(
        &arena, "dense-coord-below", var_id_from_base(UINT64_C(7599)));
    Atom *dense_above = atom_var_with_id(
        &arena, "dense-coord-above", var_id_from_base(UINT64_C(7603)));
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
              dense_sparse_source, (CettaFrameIdentity){67u}, dense_first_entry)
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
              var_id_eq(dense_slot_open->var_id, var_epoch_id(dense_open->var_id, (CettaFrameIdentity){67u})) &&
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
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
            dense_first_entry);
    Atom *dense_materialized = dense_reference_ready
        ? bindings_apply_epoch_then_all(
              &dense_reference_builder.current,
              &dense_reference_arena, dense_source,
              (CettaFrameIdentity){67u}, dense_first_entry)
        : NULL;
    bool dense_reference_match = dense_materialized &&
        match_atoms_epoch_builder(
            dense_materialized, dense_query,
            &dense_reference_builder, &dense_reference_arena, (CettaFrameIdentity){71u});
    bool dense_compiled_match = dense_match_prepared &&
        match_atoms_activation_view_builder(
            dense_source, &dense_match_frame, dense_query,
            &dense_compiled_builder, &dense_compiled_arena, (CettaFrameIdentity){71u});
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
            var_epoch_id(dense_open->var_id, (CettaFrameIdentity){67u}),
            atom_symbol_current_id(dense_open), dense_open_value) &&
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
            var_epoch_id(dense_open->var_id, (CettaFrameIdentity){67u}),
            atom_symbol_current_id(dense_open), dense_replacement_value) &&
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
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
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
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
            dense_first_entry);
    Atom *dense_rebuilt_open = dense_rebuilt &&
            dense_open_slot != UINT32_MAX
        ? bindings_apply_activation_view_slot_then_all(
              &dense_frame_builder.current, &arena,
              &dense_frame, dense_open, dense_open_slot)
        : NULL;
    CHECK(dense_rebuilt && dense_rebuilt_open &&
              dense_rebuilt_open->kind == ATOM_VAR &&
              var_id_eq(dense_rebuilt_open->var_id, var_epoch_id(dense_open->var_id, (CettaFrameIdentity){67u})),
          "rollback exposes the restored unbound slot through its identity");

    const void *dense_schema_identity = NULL;
    const void *dense_slot_identity = NULL;
    bool dense_authority_reborrowed = dense_frame_ready &&
        bindings_activation_view_prepare(
            &dense_frame, &dense_frame_builder,
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
            dense_first_entry) &&
        bindings_frame_storage_test_identity(
            &dense_frame_builder.current, (CettaFrameIdentity){67u},
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
              var_id_eq(dense_reborrowed_open->var_id, var_epoch_id(dense_open->var_id, (CettaFrameIdentity){67u})),
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
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
            dense_first_entry);
    Atom *dense_saturation_reference = bindings_apply_epoch_then_all(
        &dense_frame_builder.current, &arena, dense_source,
        (CettaFrameIdentity){67u}, dense_first_entry);
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
                  2u, (CettaFrameIdentity){67u}, dense_first_entry),
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
            dense_ids, dense_variables, 3u, (CettaFrameIdentity){67u},
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
        view_source_ids, view_source_variables, 1u, (CettaFrameIdentity){37u},
        view_first_entry);
    view_fixture = view_fixture && bindings_builder_add_id_fresh(
        &view_seed_builder, var_epoch_id(view_local->var_id, (CettaFrameIdentity){37u}),
        atom_symbol_current_id(view_local), view_outer);
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
              view_left, (CettaFrameIdentity){37u}, view_first_entry)
        : NULL;
    bool view_reference_match = view_materialized &&
        match_atoms_epoch_builder(
            view_materialized, view_right, &view_reference,
            &view_reference_arena, (CettaFrameIdentity){41u});
    bool view_compiled_match = view_fixture && view_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_left, (CettaFrameIdentity){37u}, view_first_entry, view_right,
            &view_compiled, &view_compiled_arena, (CettaFrameIdentity){41u});
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
              view_left, (CettaFrameIdentity){37u}, view_first_entry)
        : NULL;
    bool view_current_reference_match =
        view_current_materialized &&
        match_atoms_builder(
            view_current_materialized, view_right,
            &view_current_reference, &view_current_reference_arena);
    bool view_current_compiled_match =
        view_current_compiled_ready &&
        match_atoms_epoch_view_builder_current(
            view_left, (CettaFrameIdentity){37u}, view_first_entry, view_right,
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
            view_source_ids, view_source_variables, 1u, (CettaFrameIdentity){43u},
            view_open_first_entry) &&
        bindings_builder_add_id_fresh(
            &view_open_seed,
            var_epoch_id(view_local->var_id, (CettaFrameIdentity){43u}),
            atom_symbol_current_id(view_local), view_outer) &&
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
              &view_open_reference_arena, view_left, (CettaFrameIdentity){43u},
              view_open_first_entry)
        : NULL;
    bool view_open_reference_match = view_open_materialized &&
        match_atoms_epoch_builder(
            view_open_materialized, view_open_right,
            &view_open_reference, &view_open_reference_arena, (CettaFrameIdentity){47u});
    bool view_open_compiled_match =
        view_open_fixture && view_open_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_left, (CettaFrameIdentity){43u}, view_open_first_entry, view_open_right,
            &view_open_compiled, &view_open_compiled_arena, (CettaFrameIdentity){47u});
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
              &view_whole_reference_arena, view_left, (CettaFrameIdentity){37u},
              view_first_entry)
        : NULL;
    bool view_whole_reference_match = view_whole_materialized &&
        match_atoms_epoch_builder(
            view_whole_materialized, view_candidate,
            &view_whole_reference, &view_whole_reference_arena, (CettaFrameIdentity){53u});
    bool view_whole_compiled_match = view_whole_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_left, (CettaFrameIdentity){37u}, view_first_entry, view_candidate,
            &view_whole_compiled, &view_whole_compiled_arena, (CettaFrameIdentity){53u});
    Atom *view_whole_key = atom_var_like(
        &arena, view_candidate,
        var_epoch_id(view_candidate->var_id, (CettaFrameIdentity){53u}));
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
                  view_left, (CettaFrameIdentity){37u}, view_compiled.current.len + 1u,
                  view_right, &view_compiled,
                  &view_compiled_arena, (CettaFrameIdentity){59u}) &&
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
              view_fail_left, (CettaFrameIdentity){61u}, 0u)
        : NULL;
    bool view_fail_reference_match = view_fail_materialized &&
        match_atoms_epoch_builder(
            view_fail_materialized, view_fail_right,
            &view_fail_reference, &view_fail_reference_arena, (CettaFrameIdentity){67u});
    bool view_fail_compiled_match = view_fail_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_fail_left, (CettaFrameIdentity){61u}, 0u, view_fail_right,
            &view_fail_compiled, &view_fail_compiled_arena, (CettaFrameIdentity){67u});
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
              view_fail_left, (CettaFrameIdentity){61u}, 0u)
        : NULL;
    bool view_current_fail_reference_match =
        view_current_fail_materialized &&
        match_atoms_builder(
            view_current_fail_materialized, view_fail_right,
            &view_current_fail_reference, &view_current_fail_reference_arena);
    bool view_current_fail_compiled_match =
        view_current_fail_compiled_ready &&
        match_atoms_epoch_view_builder_current(
            view_fail_left, (CettaFrameIdentity){61u}, 0u, view_fail_right,
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
              view_cycle_left, (CettaFrameIdentity){71u}, 0u)
        : NULL;
    bool view_cycle_reference_match = view_cycle_materialized &&
        match_atoms_epoch_builder(
            view_cycle_materialized, view_cycle_right,
            &view_cycle_reference, &view_cycle_reference_arena, (CettaFrameIdentity){73u});
    bool view_cycle_compiled_match = view_cycle_compiled_ready &&
        match_atoms_epoch_view_builder(
            view_cycle_left, (CettaFrameIdentity){71u}, 0u, view_cycle_right,
            &view_cycle_compiled, &view_cycle_compiled_arena, (CettaFrameIdentity){73u});
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
            slot_outer, slot_rule, &slot_alias, &arena, (CettaFrameIdentity){slot_epoch});
    CHECK(slot_alias_matched &&
              bindings_lookup_value_id(&slot_alias.current, var_epoch_id(slot_rule->var_id, (CettaFrameIdentity){slot_epoch})).skeleton ==
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
            &arena, (CettaFrameIdentity){slot_epoch});
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
                  slot_structured_observed->expr.elems[1]->var_id).word ==
                  slot_epoch &&
              var_epoch_suffix(
                  slot_structured_observed->expr.elems[2]->var_id).word ==
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
            &arena, (CettaFrameIdentity){slot_epoch});
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
              var_id_eq(slot_left_value->var_id, slot_right_value->var_id) &&
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
                  &slot_cycle, &arena, (CettaFrameIdentity){slot_epoch}) &&
              !bindings_has_loop(&slot_cycle.current),
          "equation-activation-frame orientation refuses a cyclic substitution at the bind");
    if (slot_cycle_ready)
        bindings_builder_free(&slot_cycle);

    CettaFrameIdentity fresh_before_null = CETTA_FRAME_IDENTITY_NONE;
    CettaFrameIdentity fresh_after_null = CETTA_FRAME_IDENTITY_NONE;
    CHECK(cetta_frame_identity_scope_try(&frame_identity_scope, &fresh_before_null) &&
              !cetta_frame_identity_is_none(fresh_before_null) &&
              !cetta_frame_identity_scope_try(&frame_identity_scope, NULL) &&
              cetta_frame_identity_scope_try(&frame_identity_scope, &fresh_after_null) &&
              !cetta_frame_identity_eq(fresh_after_null, fresh_before_null),
          "frame identity refusal does not consume or alias an identity");
    CettaFrameIdentity retired_identity = CETTA_FRAME_IDENTITY_NONE;
    bool identity_reuse_ok = cetta_frame_identity_acquire(&retired_identity);
    uint32_t retired_handle = cetta_frame_handle(retired_identity);
    cetta_frame_identity_release(retired_identity);
    bool handle_retired = false;
    for (uint32_t generation = 0u;
         identity_reuse_ok && generation <= CETTA_FRAME_GENERATION_MAX;
         generation++) {
        CettaFrameIdentity next_identity = CETTA_FRAME_IDENTITY_NONE;
        identity_reuse_ok = cetta_frame_identity_acquire(&next_identity) &&
            !cetta_frame_identity_eq(next_identity, retired_identity) &&
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
        var_epoch_id(epoch_cycle_source->var_id, (CettaFrameIdentity){23u}));
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
                   &epoch_cycle, &arena, epoch_cycle_source, (CettaFrameIdentity){23u})) != NULL &&
              epoch_applied->kind == ATOM_VAR &&
              var_id_eq(epoch_applied->var_id, epoch_cycle_tail->var_id),
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
              var_id_eq(cyclic_result->var_id, cycle_y->var_id),
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
              var_id_eq(indexed_cycle_result->var_id, memo_vars[95u]->var_id) &&
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
        VarId key = var_epoch_id(var_id_from_base(1u), identity);
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
              !var_id_is_none(fresh_id_before_null) &&
              !fresh_var_id_try(NULL) &&
              fresh_var_id_try(&fresh_id_after_null) &&
              fresh_id_after_null.base == fresh_id_before_null.base + 1u,
          "fresh identity refusal does not consume or alias an identity");
#ifdef CETTA_TEST_HOOKS
    VarId penultimate_id = VAR_ID_NONE;
    VarId final_id = VAR_ID_NONE;
    VarId exhausted_id_sentinel = var_id_from_base(7305u);
    fresh_var_id_test_reset(UINT64_MAX - 2u);
    CHECK(fresh_var_id_try(&penultimate_id) &&
              var_id_eq(penultimate_id, var_id_from_base(UINT64_MAX - 2u)) &&
              fresh_var_id_try(&final_id) &&
              var_id_eq(final_id, var_id_from_base(UINT64_MAX - 1u)) &&
              !fresh_var_id_try(&exhausted_id_sentinel) &&
              var_id_eq(exhausted_id_sentinel, var_id_from_base(7305u)),
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
