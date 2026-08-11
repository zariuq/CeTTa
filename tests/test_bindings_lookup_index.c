#include "atom.h"
#include "match.h"
#include "variant_shape.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

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
          "acyclic epoch application consumes the substitution certificate");
    bindings_free(&epoch_environment);

    Atom *activation_outer = atom_var_with_id(
        &arena, "activation-outer", test_id(7300u));
    Atom *activation_outer_link = atom_var_with_id(
        &arena, "activation-outer-link", test_id(7301u));
    Atom *activation_source = atom_var_with_id(
        &arena, "activation-source", test_id(7302u));
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
              activation_environment.len == 3u,
          "invalid activation boundary fails without changing bindings");
    bindings_free(&activation_environment);

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
          "incremental occurs certificate accepts an acyclic edge");
    CHECK(bindings_add_var(&cycle, cycle_y, cycle_payload) &&
              bindings_has_loop(&cycle),
          "incremental occurs certificate detects a transitive cycle");
    Atom *cyclic_result = bindings_apply(&cycle, &arena, cycle_x);
    CHECK(cyclic_result == cycle_payload,
          "witnessed cycles retain the terminating active-path guard");
    CHECK(bindings_remove_entry_at(&cycle, 1u) &&
              !bindings_has_loop(&cycle),
          "removing the witnessed edge falls back to the full oracle");

    Atom *acyclic_tail = atom_var(&arena, "apply-certificate-tail");
    Atom *acyclic_head = atom_var(&arena, "apply-certificate-head");
    Atom *acyclic_leaf = atom_symbol(&arena, "ApplyCertificateLeaf");
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
          "acyclicity certificate erases only redundant path tracking");
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
          "cycle rollback branch starts from the acyclic certificate");
    uint32_t cycle_mark = bindings_builder_save(&cycle_branch);
    CHECK(bindings_builder_add_var_fresh(
              &cycle_branch, cycle_y, cycle_payload) &&
              bindings_has_loop(&cycle_branch.current),
          "speculative insertion records a witnessed cycle");
    bindings_builder_rollback(&cycle_branch, cycle_mark);
    CHECK(!bindings_has_loop(&cycle_branch.current) &&
              bindings_lookup_id(
                  &cycle_branch.current, cycle_y->var_id) == NULL,
          "rollback restores the prior acyclicity certificate");
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
          "key rewrites invalidate every derived lookup certificate");
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
