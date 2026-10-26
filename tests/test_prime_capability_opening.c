/* Exercise the private traversal and its active Need snapshot directly,
 * including sharing and invalid cycles that parsed programs cannot construct.
 * This translation unit replaces eval.o in the test executable. */
#include "../src/eval.c"
#include <assert.h>

static Atom *suspend(Arena *arena, Atom *origin) {
    PrimeNeedSnapshot next;
    uint64_t id;
    assert(prime_need_snapshot_allocate(
        arena, &g_prime_need_active, origin, &next, &id));
    g_prime_need_active = next;
    Atom *ref = prime_need_ref(arena, &g_prime_need_active, id);
    assert(ref);
    return ref;
}

static Atom *nest(Arena *arena, Atom *head, Atom *leaf, size_t depth) {
    for (size_t i = 0u; i < depth; i++) leaf = atom_expr2(arena, head, leaf);
    return leaf;
}

/* Rendered answers cannot expose an otherwise unreachable binding retained
 * in a function's continuation. Check the actual environment boundary as well
 * as the language-level closure examples in need_application.metta. */
static bool function_projection(Arena *arena) {
    Atom *caller = atom_var(arena, "caller");
    Atom *capture = atom_var(arena, "capture");
    Atom *private = atom_var(arena, "private");
    Atom *seven = atom_int(arena, 7);
    Atom *eleven = atom_int(arena, 11);
    Atom *body = atom_expr2(arena, atom_symbol(arena, "input"), caller);
    Atom *result = atom_expr2(arena, atom_symbol(arena, "closure"), capture);
    Bindings full, projected;
    bindings_init(&full);
    assert(bindings_add_var(&full, caller, seven));
    assert(bindings_add_var(&full, capture, eleven));
    assert(bindings_add_var(&full, private, atom_int(arena, 13)));
    assert(bindings_project_function_result(arena, body, result,
                                           &full, &projected));
    bool ok = atom_eq(bindings_apply(&projected, arena, caller), seven) &&
        atom_eq(bindings_apply(&projected, arena, capture), eleven) &&
        !bindings_lookup_value_id(&projected, private->var_id).skeleton;
    bindings_free(&projected);
    bindings_free(&full);
    if (!ok)
        fputs("FAIL: function projection retains caller and closure bindings only\n",
              stderr);
    return ok;
}

static bool cell_projection(Arena *arena) {
    Atom *capture = atom_var(arena, "cell-capture");
    Atom *private = atom_var(arena, "continuation-private");
    Atom *seven = atom_int(arena, 7);
    PrimeNeedCellView cell = {.origin = capture};
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId captured_id = capture->var_id;
    cell.capture_known = true;
    cell.capture_var_ids = &captured_id;
    cell.capture_var_count = 1u;
#endif
    Bindings caller, projected;
    bindings_init(&caller);
    assert(bindings_add_var(&caller, capture, seven));
    assert(bindings_add_var(&caller, private, atom_int(arena, 13)));
    assert(prime_need_project_cell_logical_env(arena, &cell, &caller, &projected, NULL, NULL));
    bool ok = atom_eq(bindings_apply(&projected, arena, capture), seven) &&
        !bindings_lookup_value_id(&projected, private->var_id).skeleton;
    bindings_free(&projected);
    bindings_free(&caller);
    if (!ok)
        fputs("FAIL: cell projection excludes unrelated continuation bindings\n",
              stderr);
    return ok;
}

static bool same_spelling_cell_projection(Arena *arena) {
    Atom *capture = atom_var_with_id(arena, "captured-name", fresh_var_id());
    Atom *caller_variable = atom_var_with_id(arena, "captured-name", fresh_var_id());
    assert(capture && caller_variable && !var_id_eq(capture->var_id, caller_variable->var_id));
    PrimeNeedCellView cell = {.origin = capture};
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId captured_id = capture->var_id;
    cell.capture_known = true;
    cell.capture_var_ids = &captured_id;
    cell.capture_var_count = 1u;
#endif
    Bindings caller, projected;
    bindings_init(&caller);
    assert(bindings_add_var(&caller, caller_variable, atom_int(arena, 99)));
    assert(prime_need_project_cell_logical_env(arena, &cell, &caller, &projected, NULL, NULL));
    bool ok = atom_eq(bindings_apply(&projected, arena, capture), capture) &&
        !bindings_lookup_value_id(&projected, caller_variable->var_id).skeleton;
    bindings_free(&projected);
    bindings_free(&caller);
    if (!ok)
        fputs("FAIL: cell projection preserves captured identity across same-spelled caller bindings\n",
              stderr);
    return ok;
}

/* Delayed equalities are logical state even before any variable is bound.
 * Capture follows their connected component, including constraints reached
 * through a binding or a private Need origin, without importing other roots. */
static bool constrained_projection(Arena *arena, unsigned mode, bool bound_root) {
    Atom *x = atom_var_with_id(arena, "constraint-x", fresh_var_id());
    Atom *y = atom_var_with_id(arena, "constraint-y", fresh_var_id());
    Atom *z = atom_var_with_id(arena, "constraint-z", fresh_var_id());
    Atom *private = atom_var_with_id(arena, "constraint-x", fresh_var_id());
    Atom *other = atom_var_with_id(arena, "constraint-other", fresh_var_id());
    Atom *head = atom_symbol(arena, "constraint-box");
    Atom *bx = atom_expr2(arena, head, x);
    Atom *by = atom_expr2(arena, head, y);
    Atom *bz = atom_expr2(arena, head, z);
    Bindings full, projected;
    bindings_init(&full);
    assert(bindings_add_constraint(&full, bx, by));
    assert(bindings_add_constraint(&full, by, bz));
    assert(bindings_add_constraint(&full, atom_expr2(arena, head, private),
                                   atom_expr2(arena, head, other)));
    assert(full.eq_len == 3u && !bindings_has_bound_values(&full));
    Atom *root = x;
    if (bound_root) {
        root = atom_var_with_id(arena, "constraint-root", fresh_var_id());
        assert(bindings_add_var(&full, root, bx));
    }
    Atom *body = mode == 2u ? suspend(arena, root) : root;
    *bindings_need_mut(&full) = g_prime_need_active;
    assert(bindings_refresh_occurrence_token(&full));
    PrimeNeedCellView cell = {.origin = root};
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
    VarId captured_id = root->var_id;
    cell.capture_known = true;
    cell.capture_var_ids = &captured_id;
    cell.capture_var_count = 1u;
#endif
    bool admitted = mode == 0u
        ? bindings_project_body_exact_env(arena, body, &full, &projected)
        : mode == 1u
        ? prime_need_project_cell_logical_env(arena, &cell, &full, &projected, NULL, NULL)
        : mode == 2u
        ? bindings_project_control_continuation(arena, body, &full, false, &projected)
        : bindings_project_function_result(arena, body, atom_symbol(arena, "returned"),
                                            &full, &projected);
    assert(admitted);
    bool ok = projected.eq_len == 2u &&
        bindings_occurrence_token(&projected) == bindings_occurrence_token(&full) &&
        bindings_need_view(&projected)->top == bindings_need_view(&full)->top &&
        bindings_need_view(&projected)->session_id == bindings_need_view(&full)->session_id;
    /* A conflicting later assignment must still be refused. The unrelated
     * same-spelled variable remains free to take a different value. */
    Atom *seven = atom_int(arena, 7);
    ok = ok && bindings_add_var(&projected, x, seven) &&
        bindings_add_var(&projected, z, seven) &&
        !bindings_add_var(&projected, y, atom_int(arena, 8)) &&
        bindings_add_var(&projected, private, atom_int(arena, 9)) &&
        bindings_add_var(&projected, other, atom_int(arena, 10));
    ok = ok && full.eq_len == 3u &&
        !bindings_lookup_value_id(&full, x->var_id).skeleton;
    bindings_free(&projected);
    bindings_free(&full);
    if (!ok)
        fprintf(stderr, "FAIL: constraint closure survives projection mode %u (%s root)\n",
                mode, bound_root ? "bound" : "unbound");
    return ok;
}

static bool exact_body_resolves_contextual_alias(Arena *arena) {
    CettaFrameIdentityScope identities = {0};
    CettaFrameIdentity outer_epoch = CETTA_FRAME_IDENTITY_NONE;
    CettaFrameIdentity inner_epoch = CETTA_FRAME_IDENTITY_NONE;
    VarId outer_source = var_id_from_base(1u);
    VarId inner_source = var_id_from_base(2u);
    assert(cetta_frame_identity_scope_try(&identities, &outer_epoch));
    assert(cetta_frame_identity_scope_try(&identities, &inner_epoch));
    Atom *inner = atom_var_with_id(arena, "exact-inner", inner_source);
    Atom *outer = atom_var_with_id(arena, "exact-outer",
                                  var_epoch_id(outer_source, outer_epoch));
    Atom *seven = atom_int(arena, 7);
    Atom *body = atom_expr3(arena, atom_symbol(arena, "Pair"), outer, outer);
    Bindings full, captured;
    bindings_init(&full);
    assert(bindings_register_complete_contextual_frame(
        &full, &outer_source, 1u, outer_epoch));
    assert(bindings_register_complete_contextual_frame(
        &full, &inner_source, 1u, inner_epoch));
    assert(bindings_add_id(&full, outer->var_id, SYMBOL_ID_NONE, inner));
    assert(bindings_rewrite_value_id(&full, outer->var_id,
                                    binding_value_from_context(inner, inner_epoch)));
    assert(bindings_add_id(&full, var_epoch_id(inner_source, inner_epoch),
                          SYMBOL_ID_NONE, seven));
    assert(bindings_project_body_exact_env(arena, body, &full, &captured));
    BindingValue alias = bindings_lookup_value_id(&captured, outer->var_id);
    bool instantiated = false;
    Atom *actual = bindings_apply_body_exact_env(arena, body, &full, &instantiated);
    Atom *expected = atom_expr3(arena, atom_symbol(arena, "Pair"), seven, seven);
    Atom *single_pass = bindings_apply_projected_body_visible(&captured, arena, body);
    bool ok = binding_value_is_contextual(alias) &&
        cetta_frame_identity_eq(binding_value_frame_ref(alias).identity, inner_epoch) &&
        instantiated && atom_eq(actual, expected) && !atom_eq(single_pass, expected);
    bindings_free(&captured);
    bindings_free(&full);
    cetta_frame_identity_scope_clear(&identities);
    if (!ok)
        fputs("FAIL: exact body instantiation resolves retained contextual aliases\n", stderr);
    return ok;
}

static bool capture_follows_capabilities_in_bound_values(Arena *arena) {
    Atom *outer = atom_var_with_id(arena, "bound-capability", fresh_var_id());
    Atom *middle = atom_var_with_id(arena, "bound-capability-middle", fresh_var_id());
    Atom *inner = atom_var_with_id(arena, "bound-capability-inner", fresh_var_id());
    Atom *unrelated = atom_var_with_id(arena, "bound-capability-inner", fresh_var_id());
    Atom *middle_ref = suspend(arena, middle);
    Atom *inner_ref = suspend(arena, inner);
    Bindings full, captured;
    bindings_init(&full);
    assert(bindings_add_var(&full, outer, middle_ref));
    assert(bindings_add_var(&full, middle, inner_ref));
    assert(bindings_add_var(&full, inner, atom_int(arena, 7)));
    assert(bindings_add_var(&full, unrelated, atom_int(arena, 99)));
    bindings_prime_set(&full, &g_prime_need_active, NULL, 0u, NULL, NULL);
    bool made = bindings_project_body_exact_env(arena, outer, &full, &captured);
    PrimeNeedCellView first, second;
    uint64_t first_id = 0u, second_id = 0u;
    bool ok = made && atom_eq(bindings_apply(&captured, arena, outer), middle_ref) &&
        atom_eq(bindings_apply(&captured, arena, middle), inner_ref) &&
        atom_eq(bindings_apply(&captured, arena, inner), atom_int(arena, 7)) &&
        !bindings_lookup_value_id(&captured, unrelated->var_id).skeleton &&
        prime_need_ref_belongs_to(middle_ref, bindings_need_view(&captured), &first_id) &&
        prime_need_ref_belongs_to(inner_ref, bindings_need_view(&captured), &second_id) &&
        prime_need_snapshot_lookup(bindings_need_view(&captured), first_id, &first) &&
        prime_need_snapshot_lookup(bindings_need_view(&captured), second_id, &second) &&
        !first.cached && !second.cached;
    bindings_free(&captured);
    bindings_free(&full);
    if (!ok)
        fputs("FAIL: capture follows opaque capabilities in selected binding values without forcing\n",
              stderr);
    return ok;
}

static bool projection_frame(const Bindings *bindings, CettaFrameIdentity epoch,
                             BindingsFrameStateView *view) {
    BindingsFrameStateIterator iterator = {.bindings = bindings};
    while (bindings_frame_state_next(&iterator, view))
        if (cetta_frame_identity_eq(view->ref.identity, epoch))
            return true;
    return false;
}

/* An unbound slot still reaches its frame inventory and activation lineage.
 * A retained image must permit a later write without reusing an old version
 * or hiding it from the activation that was already prepared. */
static bool unbound_frame_projection(Arena *arena, unsigned mode,
                                     bool earlier_write) {
    CettaFrameIdentityScope identities = {0};
    CettaFrameIdentity epoch, private_epoch;
    assert(cetta_frame_identity_scope_try(&identities, &epoch));
    assert(cetta_frame_identity_scope_try(&identities, &private_epoch));
    Atom *variables[] = {
        atom_var_with_id(arena, "frame-past", fresh_var_id()),
        atom_var_with_id(arena, "frame-future", fresh_var_id()),
        atom_var_with_id(arena, "frame-unused", fresh_var_id())};
    VarId ids[] = {variables[0]->var_id, variables[1]->var_id,
                   variables[2]->var_id};
    Atom *root = atom_var_with_id(arena, "frame-future",
                                  var_epoch_id(ids[1], epoch));
    Bindings full, projected, sibling;
    bindings_init(&full);
    bindings_init(&projected);
    bindings_init(&sibling);
    assert(bindings_register_complete_contextual_frame(&full, ids, 3u, epoch));
    assert(bindings_register_complete_contextual_frame(&full, ids, 3u, private_epoch));
    if (earlier_write)
        assert(bindings_add_id(&full, var_epoch_id(ids[0], epoch),
                              SYMBOL_ID_NONE, atom_int(arena, 5)));
    BindingsBuilder builder;
    BindingsActivationView activation;
    bindings_activation_view_init(&activation);
    assert(bindings_builder_init(&builder, &full));
    assert(bindings_activation_view_prepare(&activation, &builder, ids,
                                            variables, 3u, epoch, full.len));
    bindings_free(&full);
    bindings_builder_take(&builder, &full);
    bindings_builder_free(&builder);
    BindingsFrameStateView before;
    assert(projection_frame(&full, epoch, &before));
    assert(before.has_activation_write_boundary);

    Atom *opaque = suspend(arena, root);
    *bindings_need_mut(&full) = g_prime_need_active;
    bool made = false;
    if (mode == 0u) {
        Atom *roots[] = {root};
        made = bindings_project_reachable(&full, roots, 1u, &projected);
    } else if (mode == 1u) {
        made = bindings_project_body_exact_env(arena, root, &full, &projected);
    } else if (mode == 2u) {
        made = bindings_project_control_continuation(arena, root, &full,
                                                     false, &projected);
    } else if (mode == 3u) {
        made = bindings_project_control_continuation(arena, opaque, &full,
                                                     false, &projected);
    } else if (mode == 4u) {
        made = bindings_project_body_visible_env(arena, root, &full, &projected);
    } else if (mode == 5u) {
        Atom *roots[] = {opaque};
        made = prime_eval_stack_capture_value_support(&projected, &full, roots, 1u);
    } else if (mode == 6u) {
        PrimeNeedCellView cell = {.origin = root};
#if CETTA_PRIME_NEED_CLOSURE_CAPTURE
        VarId captured_id = root->var_id;
        cell.capture_known = true;
        cell.capture_var_ids = &captured_id;
        cell.capture_var_count = 1u;
#endif
        made = prime_need_project_cell_logical_env(arena, &cell, &full, &projected, NULL, NULL);
    } else {
        made = bindings_project_function_result(arena, root, root, &full, &projected);
    }
    BindingsFrameStateView kept, private_frame;
    bool frame_kept = made && projection_frame(&projected, epoch, &kept);
    BindingsFrameSlotStateView slot;
    bool ok = frame_kept && kept.slot_count == 3u && kept.schema_complete &&
        kept.owns_identity && kept.write_clock == before.write_clock &&
        kept.has_activation_write_boundary == before.has_activation_write_boundary &&
        kept.activation_write_boundary == before.activation_write_boundary &&
        bindings_frame_state_read_slot(&kept, 1u, &slot) &&
        var_id_eq(slot.var_id, root->var_id) && !slot.value.skeleton &&
        !projection_frame(&projected, private_epoch, &private_frame) &&
        !bindings_lookup_value_id(&projected, var_epoch_id(ids[0], epoch)).skeleton &&
        bindings_activation_view_available(&activation, &projected) &&
        bindings_clone(&sibling, &projected);
    if (ok) {
        Atom *seven = atom_int(arena, 7);
        BindingValue output = binding_value_from_atom(NULL);
        bool present = false;
        ok = bindings_add_var(&projected, root, seven) &&
            projection_frame(&projected, epoch, &kept) &&
            bindings_frame_state_read_slot(&kept, 1u, &slot) &&
            slot.write_version > before.write_clock &&
            kept.write_clock == before.write_clock + 1u &&
            bindings_activation_view_read_slot(&projected, &activation, 1u,
                                               &output, &present) &&
            present && output.skeleton == seven &&
            !bindings_lookup_value_id(&sibling, root->var_id).skeleton &&
            !bindings_lookup_value_id(&full, root->var_id).skeleton;
        BindingsFrameStateView unchanged;
        ok = ok && projection_frame(&full, epoch, &unchanged) &&
            unchanged.write_clock == before.write_clock &&
            unchanged.activation_write_boundary == before.activation_write_boundary;
    }
    if (!ok)
        fprintf(stderr, "FAIL: unbound frame projection mode %u history %u retains authority and activation lineage\n",
                mode, (unsigned)earlier_write);
    bindings_activation_view_free(&activation);
    bindings_free(&sibling);
    bindings_free(&projected);
    bindings_free(&full);
    cetta_frame_identity_scope_clear(&identities);
    return ok;
}

static void full_demand_context(Arena *arena) {
    Space local = {0}, foreign = {0};
    Atom *body = atom_expr2(arena, atom_symbol(arena, "key"),
                            atom_int(arena, 7));
    Atom *parts[] = {atom_symbol(arena, "metta"), body,
        atom_symbol(arena, "%Undefined%"), atom_space(arena, &local)};
    Atom *request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == body);

    parts[3] = atom_space(arena, &foreign);
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == request);
    Atom *current_context = atom_symbol(arena, "context-space");
    parts[3] = atom_expr(arena, &current_context, 1u);
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == body);
    parts[2] = atom_symbol(arena, "Number");
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == request);
    parts[2] = atom_symbol(arena, "%Undefined%");
    parts[3] = atom_expr2(arena, atom_symbol(arena, "context-space"), body);
    request = atom_expr(arena, parts, 4u);
    assert(prepared_full_demand_body(&local, arena, request, NULL) == request);
}

static void hosted_guest_cancellation_boundary(Arena *arena) {
    PrimeHolGuest guest = {0};
    space_init(&guest.space);
    cetta_library_context_init_for_language_profile(
        &guest.context, CETTA_LANGUAGE_PRIME, cetta_profile_prime_default());
    CettaEvalEpisode caller = {.completion = CETTA_EVAL_INCOMPLETE_FUEL};
    CettaEvalEpisode *previous_tracker = g_eval_completion_tracker;
    g_eval_completion_tracker = &caller;
    _Atomic bool cancelled = true;
    _Atomic bool *previous_cancel = eval_set_cancel_token(&cancelled);
    Atom *answer = NULL, *reason = NULL;
    assert(!prime_hosted_guest_run(
        &guest, arena, atom_expr3(arena, atom_symbol(arena, "=="),
                                  atom_int(arena, 1), atom_int(arena, 1)),
        &answer, &reason, "checker-incomplete", "checker-answers"));
    assert(!answer && reason);
    assert(g_eval_completion_tracker == &caller);
    assert(caller.completion == CETTA_EVAL_INCOMPLETE_FUEL);
    eval_set_cancel_token(previous_cancel);
    g_eval_completion_tracker = previous_tracker;
    cetta_library_context_free(&guest.context);
    space_free(&guest.space);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    Arena arena;
    CettaLibraryContext context = {0};
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    arena_init(&arena);
    arena_set_hashcons(&arena, NULL);
    cetta_eval_session_init(&context.session, CETTA_LANGUAGE_PRIME,
                           cetta_profile_prime_default());
    eval_set_library_context(&context);
    prime_need_snapshot_init(&g_prime_need_active);
    assert(prime_need_snapshot_begin(&g_prime_need_active));

    if (!function_projection(&arena) || !cell_projection(&arena) ||
        !same_spelling_cell_projection(&arena) ||
        !exact_body_resolves_contextual_alias(&arena) ||
        !capture_follows_capabilities_in_bound_values(&arena))
        return 1;
    for (unsigned mode = 0u; mode < 4u; mode++) {
        if (!constrained_projection(&arena, mode, false) ||
            !constrained_projection(&arena, mode, true))
            return 1;
    }
    bool frames_ok = true;
    for (unsigned mode = 0u; mode < 8u; mode++) {
        frames_ok = unbound_frame_projection(&arena, mode, false) && frames_ok;
        frames_ok = unbound_frame_projection(&arena, mode, true) && frames_ok;
    }
    if (!frames_ok)
        return 1;
    full_demand_context(&arena);

    Atom *x = atom_var(&arena, "x");
    Atom *y = atom_var(&arena, "y");
    Atom *box = atom_symbol(&arena, "Box");
    Atom *pair = atom_symbol(&arena, "Pair");
    Atom *deep = nest(&arena, box, x, 50000u);
    Atom *ref = suspend(&arena, deep);

    /* Occurrence depth and opening depth are independently unbounded.
     * Repeated references remain shared, and unrelated cells stay opaque. */
    assert(prime_open_capabilities_for_var(&arena, ref, x->var_id) == deep);
    assert(prime_open_capabilities_for_var(&arena, ref, y->var_id) == ref);
    Atom *outer = nest(&arena, box, ref, 50000u);
    Atom *opened = prime_open_capabilities_for_var(&arena, outer, x->var_id);
    assert(opened && opened != outer);
    for (size_t i = 0u; i < 50000u; i++) opened = opened->expr.elems[1];
    assert(opened == deep);
    assert(prime_open_capabilities_for_var(&arena, outer, y->var_id) == outer);

    /* Exponentially many unfolded paths, only a linear number of cells.
     * Rebuilding preserves the original aliasing of every changed node. */
    Atom *diamond = ref;
    for (size_t i = 0u; i < 80u; i++)
        diamond = atom_expr3(&arena, pair, diamond, diamond);
    opened = prime_open_capabilities_for_var(&arena, diamond, x->var_id);
    for (size_t i = 0u; i < 80u; i++) {
        assert(opened->expr.elems[1] == opened->expr.elems[2]);
        opened = opened->expr.elems[1];
    }
    assert(opened == deep);
    Atom *syntax_diamond = x;
    for (size_t i = 0u; i < 80u; i++)
        syntax_diamond = atom_expr3(&arena, pair, syntax_diamond, syntax_diamond);
    PrimeBinderElabMemo mentions = {0};
    bool found = true;
    assert(prime_binder_elab_memo_init(&mentions));
    assert(prime_atom_mentions_var(syntax_diamond, y->var_id, &mentions, &found));
    assert(!found && mentions.used == 82u);
    prime_binder_elab_memo_free(&mentions);

    Atom *pattern = nest(&arena, box, x, 50000u);
    assert(prime_open_capabilities_for_pattern(&arena, pattern, ref) == deep);

    /* An origin can refer to another capability that must also be opened. */
    Atom *chain = x;
    for (size_t i = 0u; i < 200u; i++)
        chain = suspend(&arena, atom_expr3(&arena, pair, x, chain));
    opened = prime_open_capabilities_for_var(&arena, chain, x->var_id);
    assert(opened);
    for (size_t i = 0u; i < 200u; i++) opened = opened->expr.elems[2];
    assert(opened == x);

    /* Manufacture invalid cyclic syntax only in this negative control. */
    Atom *cycle = atom_expr3(&arena, pair, x, x);
    Atom *cyclic_ref = suspend(&arena, cycle);
    cycle->expr.elems[2] = cyclic_ref;
    cycle->flags |= ATOM_FLAG_HAS_REGISTRY_REFS;
    assert(prime_open_capabilities_for_var(&arena, cyclic_ref, x->var_id) == NULL);
    /* An irrelevant cyclic capability is not expanded or forced. */
    assert(prime_open_capabilities_for_var(&arena, cyclic_ref, y->var_id) == cyclic_ref);
    cycle->expr.elems[2] = x;

    _Atomic bool cancelled = true;
    _Atomic bool *previous = eval_set_cancel_token(&cancelled);
    assert(prime_open_capabilities_for_var(&arena, ref, x->var_id) == NULL);
    assert(g_eval_cancel_observed);
    eval_set_cancel_token(previous);
    hosted_guest_cancellation_boundary(&arena);

    prime_need_snapshot_init(&g_prime_need_active);
    eval_set_library_context(NULL);
    arena_free(&arena);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: Prime capability opening preserves depth, sharing, opacity and cancellation");
    return 0;
}
