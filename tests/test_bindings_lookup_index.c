#include "atom.h"
#include "match.h"
#include "variant_shape.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
    return UINT64_C(0x9e3779b900000000) + (VarId)ordinal * UINT64_C(0x10001);
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
    Atom *value = bindings_lookup_id(bindings, id);
    return value && value->kind == ATOM_GROUNDED &&
           value->ground.gkind == GV_INT &&
           value->ground.ival == expected;
}

int main(void) {
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
              modern.legacy_fallback_count == 0u &&
              (modern_result =
                   bindings_apply(&modern, &arena, modern_unbound)) ==
                  modern_unbound &&
              modern_result->sym_id == modern_spelling,
          "modern bindings never capture a different VarId by spelling");

    SymbolId legacy_spelling =
        symbol_intern_cstr(g_symbols, "legacy-spelling");
    Atom *legacy_pair = atom_expr2(
        &arena, atom_symbol_id(&arena, legacy_spelling),
        atom_int(&arena, 6000));
    Atom *legacy_assigns_items[1] = {legacy_pair};
    Atom *legacy_encoded = atom_expr3(
        &arena, atom_symbol_id(&arena, g_builtin_syms.bindings),
        atom_expr(&arena, legacy_assigns_items, 1u),
        atom_expr(&arena, NULL, 0u));
    Bindings legacy;
    Atom *legacy_result = NULL;
    Atom *legacy_probe = atom_var_with_id(
        &arena, "legacy-spelling", test_id(6001u));
    CHECK(bindings_from_atom(legacy_encoded, &legacy) &&
              legacy.legacy_fallback_count == 1u &&
              (legacy_result =
                   bindings_apply(&legacy, &arena, legacy_probe)) &&
              legacy_result->kind == ATOM_GROUNDED &&
              legacy_result->ground.gkind == GV_INT &&
              legacy_result->ground.ival == 6000,
          "legacy serialized bindings retain spelling-keyed fallback");

    SymbolId legacy_cycle_x =
        symbol_intern_cstr(g_symbols, "legacy-cycle-x");
    SymbolId legacy_cycle_y =
        symbol_intern_cstr(g_symbols, "legacy-cycle-y");
    Atom *legacy_cycle_x_var = atom_var_with_id(
        &arena, "legacy-cycle-x", test_id(6100u));
    Atom *legacy_cycle_y_var = atom_var_with_id(
        &arena, "legacy-cycle-y", test_id(6101u));
    Atom *legacy_cycle_pairs[2] = {
        atom_expr2(
            &arena, atom_symbol_id(&arena, legacy_cycle_x),
            legacy_cycle_y_var),
        atom_expr2(
            &arena, atom_symbol_id(&arena, legacy_cycle_y),
            legacy_cycle_x_var),
    };
    Atom *legacy_cycle_encoded = atom_expr3(
        &arena, atom_symbol_id(&arena, g_builtin_syms.bindings),
        atom_expr(&arena, legacy_cycle_pairs, 2u),
        atom_expr(&arena, NULL, 0u));
    Bindings legacy_cycle;
    CHECK(!bindings_from_atom(legacy_cycle_encoded, &legacy_cycle),
          "legacy spelling-keyed cycle is rejected fail-closed");

    BindingsBuilder legacy_branch;
    CHECK(bindings_builder_init(&legacy_branch, &legacy),
          "legacy rollback branch clones its derived lookup state");
    uint32_t legacy_mark = bindings_builder_save(&legacy_branch);
    VarId legacy_dead_id = test_id(6002u);
    uint32_t legacy_marks[1] = {legacy_mark};
    bool legacy_compact_rollback =
        bindings_builder_add_id_fresh(
            &legacy_branch, legacy_dead_id, SYMBOL_ID_NONE,
            atom_int(&arena, 6002)) &&
        legacy_branch.current.legacy_fallback_count == 1u &&
        bindings_builder_compact_reachable(
            &legacy_branch, NULL, 0u, legacy_marks, 1u,
            NULL, NULL) &&
        legacy_marks[0] == 0u;
    bindings_builder_rollback(&legacy_branch, legacy_marks[0]);
    legacy_result = bindings_apply(
        &legacy_branch.current, &arena, legacy_probe);
    legacy_compact_rollback = legacy_compact_rollback &&
        legacy_branch.current.legacy_fallback_count == 1u &&
        bindings_lookup_id(
            &legacy_branch.current, legacy_dead_id) == NULL &&
        legacy_result && legacy_result->kind == ATOM_GROUNDED &&
        legacy_result->ground.gkind == GV_INT &&
        legacy_result->ground.ival == 6000;
    CHECK(legacy_compact_rollback,
          "compacted rollback rebuilds nonzero legacy metadata exactly");
    bindings_builder_free(&legacy_branch);

    VarId late_id = test_id(1000u);
    CHECK(bindings_lookup_id(&base, late_id) == NULL,
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
    bindings_builder_rollback(&branch, branch_mark);
    bool inner_rolled_back =
        binding_is_int(&branch.current, branch_a, 2000) &&
        bindings_lookup_id(&branch.current, branch_b) == NULL;
    bindings_builder_rollback(&branch, root_mark);
    bool root_rolled_back =
        bindings_lookup_id(&branch.current, branch_a) == NULL &&
        bindings_lookup_id(&branch.current, branch_b) == NULL &&
        binding_is_int(&branch.current, late_id, 1000) &&
        branch.current.legacy_fallback_count == 0u &&
        branch.current.private_entry_count == 0u &&
        branch.current.private_constraint_count == 0u &&
        branch.growth_count == growth_after_first + 1u;
    CHECK(branch_added && duplicate_no_growth && branch_visible &&
              inner_rolled_back && root_rolled_back,
          "rollback restores logical state without erasing or inventing work");
    bindings_builder_take(&branch, &clone);

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
              bindings_lookup_id(
                  &compacted_branch.current,
                  old_dead->var_id) == NULL &&
              bindings_lookup_id(
                  &compacted_branch.current,
                  post_live->var_id) == mid_live,
          "compaction retains the transitive live closure and remaps marks");
    bindings_builder_rollback(
        &compacted_branch, compact_marks[1]);
    bool compact_mid_rollback =
        compacted_branch.current.len == 2u &&
        bindings_lookup_id(
            &compacted_branch.current,
            post_live->var_id) == NULL &&
        bindings_lookup_id(
            &compacted_branch.current,
            mid_live->var_id) == old_live;
    bindings_builder_rollback(
        &compacted_branch, compact_marks[0]);
    CHECK(compact_mid_rollback &&
              compacted_branch.current.len == 1u &&
              binding_is_int(
                  &compacted_branch.current,
                  old_live->var_id, 11) &&
              bindings_lookup_id(
                  &compacted_branch.current,
                  mid_live->var_id) == NULL,
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

    const char *lookup_index_setting =
        getenv("CETTA_BINDINGS_LOOKUP_INDEX");
    bool lookup_index_expected =
        !(lookup_index_setting && lookup_index_setting[0] == '0');
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
    uint32_t epoch_projection_marks[3] = {1u, 2u, 4u};
    Bindings epoch_projected;
    CHECK(epoch_fixture &&
              bindings_project_reachable_with_epoch_roots_and_entry_marks(
                  &epoch_environment, NULL, 0u,
                  &epoch_root, 1u,
                  epoch_projection_marks, 3u, &epoch_projected) &&
              epoch_projected.len == 2u &&
              epoch_projection_marks[0] == 0u &&
              epoch_projection_marks[1] == 1u &&
              epoch_projection_marks[2] == 2u &&
              bindings_lookup_id(
                  &epoch_projected, epoch_local_id) == epoch_outer &&
              binding_is_int(
                  &epoch_projected, epoch_outer->var_id, 77) &&
              bindings_lookup_id(
                  &epoch_projected, test_id(7199u)) == NULL,
          "epoch roots retain a lazy activation namespace and its transitive closure");
    bindings_free(&epoch_projected);

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
              epoch_branch.current.len == 2u &&
              epoch_entry_mark == 1u &&
              epoch_discarded == 2u &&
              bindings_lookup_id(
                  &epoch_branch.current, epoch_local_id) == epoch_outer &&
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
              epoch_environment.len == 4u,
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

    Atom *activation_outer = atom_var_with_id(
        &arena, "activation-outer", test_id(7300u));
    Atom *activation_outer_link = atom_var_with_id(
        &arena, "activation-outer-link", test_id(7301u));
    Atom *activation_source = atom_var_with_id(
        &arena, "activation-source", test_id(7302u));
    Atom *activation_prefix_source = atom_var_with_id(
        &arena, "activation-prefix-source", test_id(7305u));
    Atom *activation_value = atom_int(&arena, 7303);
    VarId activation_local_id =
        var_epoch_id(activation_source->var_id, 29u);
    Bindings activation_environment;
    bindings_init(&activation_environment);
    bool activation_fixture =
        bindings_add_var(
            &activation_environment,
            activation_outer_link, activation_value) &&
        bindings_add_var(
            &activation_environment,
            activation_outer, activation_outer_link);
    for (uint32_t filler = 0u;
         activation_fixture && filler < 40u; filler++) {
        activation_fixture = bindings_add_id(
            &activation_environment, test_id(8000u + filler),
            SYMBOL_ID_NONE, atom_int(&arena, (int64_t)filler));
    }
    activation_fixture = activation_fixture &&
        bindings_add_id(
            &activation_environment,
            var_epoch_id(activation_prefix_source->var_id, 29u),
            activation_prefix_source->sym_id, activation_value);
    uint32_t activation_first_entry = activation_environment.len;
    activation_fixture = activation_fixture &&
        bindings_add_id(
            &activation_environment, activation_local_id,
            activation_source->sym_id, activation_outer);
    Atom *activation_local = activation_fixture
        ? bindings_apply_epoch_since(
              &activation_environment, &arena, activation_source,
              29u, activation_first_entry)
        : NULL;
    Atom *activation_fused = activation_fixture
        ? bindings_apply_epoch_then_all(
              &activation_environment, &arena, activation_source,
              29u, activation_first_entry)
        : NULL;
    CHECK(activation_local == activation_outer &&
              activation_fused == activation_value &&
              bindings_apply(
                  &activation_environment, &arena,
                  activation_local) == activation_fused,
          "activation suffix and fused outer resolution compose exactly");
    CHECK(!bindings_apply_epoch_since(
              &activation_environment, &arena, activation_source,
              29u, activation_environment.len + 1u) &&
              activation_environment.len == activation_first_entry + 1u,
          "invalid activation boundary fails without changing bindings");
    Atom *activation_ground_source = atom_var_with_id(
        &arena, "activation-ground-source", test_id(7304u));
    uint32_t activation_ground_first_entry = activation_environment.len;
    bool activation_ground_fixture = bindings_add_id(
        &activation_environment,
        var_epoch_id(activation_ground_source->var_id, 31u),
        activation_ground_source->sym_id, activation_value);
    Atom *activation_ground_resolved = NULL;
    CHECK(activation_ground_fixture &&
              bindings_resolve_epoch_view_ground(
                  &activation_environment, activation_ground_source,
                  31u, activation_ground_first_entry,
                  &activation_ground_resolved) &&
              activation_ground_resolved == activation_value,
          "activation view exposes a directly closed suffix value");
    Atom *activation_chained_resolved = activation_value;
    CHECK(bindings_resolve_epoch_view_ground(
              &activation_environment, activation_source, 29u,
              activation_first_entry, &activation_chained_resolved) &&
              activation_chained_resolved == activation_value,
          "an activation view follows outer variable links to a closed value");
    Atom *activation_prefix_resolved = activation_value;
    CHECK(bindings_resolve_epoch_view_ground(
              &activation_environment, activation_prefix_source, 29u,
              activation_first_entry, &activation_prefix_resolved) &&
              activation_prefix_resolved == NULL,
          "indexed activation lookup excludes an older prefix binding");
    activation_ground_resolved = activation_value;
    CHECK(!bindings_resolve_epoch_view_ground(
              &activation_environment, activation_ground_source, 31u,
              activation_environment.len + 1u,
              &activation_ground_resolved) &&
              activation_ground_resolved == NULL,
          "an invalid direct-view boundary fails closed");
    bindings_free(&activation_environment);

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
    bool view_fixture = bindings_add_var(
        &view_base, view_outer, view_value);
    uint32_t view_first_entry = view_base.len;
    view_fixture = view_fixture && bindings_add_id(
        &view_base, var_epoch_id(view_local->var_id, 37u),
        view_local->sym_id, view_outer);
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

    BindingsBuilder view_open_reference;
    BindingsBuilder view_open_compiled;
    Bindings view_open_base;
    bindings_init(&view_open_base);
    bool view_open_fixture = bindings_add_id(
        &view_open_base, var_epoch_id(view_local->var_id, 43u),
        view_local->sym_id, view_outer);
    uint32_t view_open_first_entry = 0u;
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
    CHECK(view_whole_reference_match && view_whole_compiled_match &&
              bindings_eq(
                  &view_whole_reference.current,
                  &view_whole_compiled.current),
          "activation view materializes only a demanded whole-term variable image");
    uint32_t invalid_view_mark = view_compiled_ready
        ? bindings_builder_save(&view_compiled) : 0u;
    CHECK(view_compiled_ready &&
              !match_atoms_epoch_view_builder(
                  view_left, 37u, view_compiled.current.len + 1u,
                  view_right, &view_compiled,
                  &view_compiled_arena, 59u) &&
              bindings_builder_save(&view_compiled) == invalid_view_mark,
          "activation-view matcher rejects an invalid frame boundary without mutation");

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
    arena_free(&view_compiled_arena);
    arena_free(&view_reference_arena);
    if (view_compiled_ready)
        bindings_builder_free(&view_compiled);
    if (view_reference_ready)
        bindings_builder_free(&view_reference);
    bindings_free(&view_base);

    /* A direct clause frame gives standardized rule variables ownership of
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
              bindings_lookup_id(
                  &slot_alias.current,
                  var_epoch_id(slot_rule->var_id, slot_epoch)) ==
                  slot_outer &&
              bindings_lookup_id(
                  &slot_alias.current, slot_outer->var_id) == NULL,
          "clause-frame variable aliases point from rule slots to caller variables");
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
    Atom *slot_structured_value = slot_structured_matched
        ? bindings_lookup_id(
              &slot_structured.current, slot_outer->var_id)
        : NULL;
    CHECK(slot_structured_value &&
              slot_structured_value->kind == ATOM_EXPR &&
              slot_structured_value->expr.len == 3u &&
              var_epoch_suffix(
                  slot_structured_value->expr.elems[1]->var_id) ==
                  slot_epoch &&
              var_epoch_suffix(
                  slot_structured_value->expr.elems[2]->var_id) ==
                  slot_epoch,
          "clause-frame matching retains a caller's structured rule value");
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
              match_atoms_epoch_builder_rule_local(
                  slot_cycle_call, slot_cycle_rule,
                  &slot_cycle, &arena, slot_epoch) &&
              bindings_has_loop(&slot_cycle.current),
          "clause-frame orientation leaves cyclic substitutions visible to the occurs check");
    if (slot_cycle_ready)
        bindings_builder_free(&slot_cycle);

    uint32_t fresh_before_null = 0u;
    uint32_t fresh_after_null = 0u;
    CHECK(fresh_var_suffix_try(&fresh_before_null) &&
              fresh_before_null != 0u &&
              !fresh_var_suffix_try(NULL) &&
              fresh_var_suffix_try(&fresh_after_null) &&
              fresh_after_null == fresh_before_null + 1u,
          "fresh suffix refusal does not consume or alias an identity");
#ifdef CETTA_TEST_HOOKS
    uint32_t penultimate_suffix = 0u;
    uint32_t final_suffix = 0u;
    uint32_t exhausted_sentinel = 7304u;
    fresh_var_suffix_test_reset((uint64_t)UINT32_MAX - 1u);
    CHECK(fresh_var_suffix_try(&penultimate_suffix) &&
              penultimate_suffix == UINT32_MAX - 1u &&
              fresh_var_suffix_try(&final_suffix) &&
              final_suffix == UINT32_MAX &&
              !fresh_var_suffix_try(&exhausted_sentinel) &&
              exhausted_sentinel == 7304u,
          "fresh suffix allocation reaches the boundary then fails closed");
    fresh_var_suffix_test_reset(1u);
#endif

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
    CHECK(bindings_add_var(&epoch_cycle, epoch_cycle_bound,
                           epoch_cycle_tail) &&
              bindings_add_var(&epoch_cycle, epoch_cycle_tail,
                               epoch_cycle_payload) &&
              bindings_has_loop(&epoch_cycle) &&
              bindings_apply_epoch(
                  &epoch_cycle, &arena, epoch_cycle_source, 23u) ==
                  epoch_cycle_payload,
          "cyclic epoch application retains its active-path guard");
    bindings_free(&epoch_cycle);

    bool original_unchanged =
        bindings_lookup_id(&base, branch_a) == NULL &&
        binding_is_int(&base, late_id, 1000);
    CHECK(original_unchanged,
          "copy-on-write index mutation leaves the source clone unchanged");

    Bindings removed;
    bool removed_ok = bindings_clone(&removed, &base);
    VarId removed_id = test_id(31u);
    removed_ok = removed_ok &&
                 bindings_remove_entry_at(&removed, 31u) &&
                 bindings_lookup_id(&removed, removed_id) == NULL &&
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
    CHECK(bindings_add_var(&cycle, cycle_y, cycle_payload) &&
              bindings_has_loop(&cycle),
          "incremental occurs summary detects a transitive cycle");
    Atom *cyclic_result = bindings_apply(&cycle, &arena, cycle_x);
    CHECK(cyclic_result == cycle_payload,
          "witnessed cycles retain the terminating active-path guard");
    CHECK(bindings_remove_entry_at(&cycle, 1u) &&
              !bindings_has_loop(&cycle),
          "removing the witnessed edge falls back to the full oracle");

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
    indexed_cycle_built = indexed_cycle_built &&
        bindings_add_var(
            &indexed_memo_cycle, memo_vars[95u], indexed_cycle_tail);
    Atom *indexed_cycle_result = indexed_cycle_built
        ? bindings_apply(&indexed_memo_cycle, &arena, memo_vars[0])
        : NULL;
    CHECK(indexed_cycle_result == indexed_cycle_tail &&
              bindings_has_loop(&indexed_memo_cycle),
          "indexed substitution memo preserves long-cycle termination");
    bindings_free(&indexed_memo_cycle);

    BindingsBuilder cycle_branch;
    CHECK(bindings_builder_init(&cycle_branch, &cycle),
          "cycle rollback branch starts from the acyclic summary");
    uint32_t cycle_mark = bindings_builder_save(&cycle_branch);
    CHECK(bindings_builder_add_var_fresh(
              &cycle_branch, cycle_y, cycle_payload) &&
              bindings_has_loop(&cycle_branch.current),
          "speculative insertion records a witnessed cycle");
    bindings_builder_rollback(&cycle_branch, cycle_mark);
    CHECK(!bindings_has_loop(&cycle_branch.current) &&
              bindings_lookup_id(
                  &cycle_branch.current, cycle_y->var_id) == NULL,
          "rollback restores the prior acyclicity summary");
    bindings_builder_free(&cycle_branch);
    bindings_free(&cycle);

    Bindings rewritten;
    bool rewrite_ok = bindings_clone(&rewritten, &base);
    VarId rewritten_old = rewritten.entries[12u].var_id;
    VarId rewritten_new = test_id(4000u);
    rewrite_ok = rewrite_ok &&
                 bindings_lookup_id(&rewritten, rewritten_old) != NULL;
    rewritten.entries[12u].var_id = rewritten_new;
    bindings_invalidate_after_key_rewrite(&rewritten);
    rewrite_ok = rewrite_ok &&
                 bindings_lookup_id(&rewritten, rewritten_old) == NULL &&
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
              shared.entries[0].val == shared.entries[1].val &&
              arena_owns_ptr(&promoted_arena, shared.entries[0].val),
          "one promotion session preserves shared DAG identity");
    Atom *promoted_once = shared.entries[0].val;
    CHECK(bindings_promote_logical_atoms_to_arena(
              &shared, &promoted_arena) &&
              shared.entries[0].val == promoted_once &&
              shared.entries[1].val == promoted_once,
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
    bindings_free(&legacy);
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
