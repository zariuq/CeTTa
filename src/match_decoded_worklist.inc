/* Included twice from match.c.  MATCH_DECODED_FN names the function.
 * MATCH_DECODED_WITH_FRAME 0 is the ordinary skeleton walk; 1 carries
 * (skeleton, frame) per side.  Do not include from other translation units. */

#if MATCH_DECODED_WITH_FRAME
__attribute__((noinline))
#endif
static bool MATCH_DECODED_FN(
        Atom *left, Atom *right,
        Bindings *bindings,
        BindingsBuilder *builder,
        bool undefined_is_wildcard
#if MATCH_DECODED_WITH_FRAME
        , BindingsActivationView *left_frame
        , BindingsActivationView *right_frame
#endif
        ) {
    DecodedMatchWorklist work;
    MatchPathSet path;
    Bindings *initial = builder ? &builder->current : bindings;
    uint32_t attempt_start_len = initial ? initial->len : 0u;
    bool attempt_repeated_var = false;
    bool attempt_resolving_bound = false;
    decoded_match_worklist_init(&work);
    match_path_init(&path);
    if (!decoded_match_push(&work, left, right)) goto fail;

    while (work.len > 0) {
        DecodedMatchPair pair = work.items[--work.len];
        if (pair.kind == DECODED_MATCH_EXIT) {
            match_path_leave(&path, pair.left, pair.right, false);
            continue;
        }
        left = pair.left;
        right = pair.right;
        attempt_resolving_bound = false;
        Bindings *current = builder ? &builder->current : bindings;
        /* A variable-only dereference chain cannot be longer than the binding
           table unless it contains a cycle. This is cycle detection derived
           from the graph in hand, not an arbitrary depth cutoff. */
        size_t dereferences = 0;
        size_t dereference_limit = bindings_dereference_limit(current);

retry_pair:
        if (undefined_is_wildcard &&
            (atom_is_symbol_id(left, g_builtin_syms.undefined_type) ||
             atom_is_symbol_id(right, g_builtin_syms.undefined_type)))
            continue;
        if (left == right) {
            if (left && left->kind == ATOM_VAR)
                continue;
            if (match_shared_ground_reflexivity_try(
                    left, right, true, true))
                continue;
        }
        bool closed_equal = false;
        if (!undefined_is_wildcard &&
            match_closed_expression_decision_try(
                left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (!undefined_is_wildcard &&
            match_finite_ground_equal_try(left, right, &closed_equal)) {
            if (!closed_equal)
                goto fail;
            continue;
        }
        if (left->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
#if MATCH_DECODED_WITH_FRAME
            Atom *existing = decoded_var_existing(
                current, left_frame, left, NULL);
#else
            Atom *existing = bindings_lookup_value_id(current, (left)->var_id).skeleton;
#endif
            if (existing) {
                if (++dereferences > dereference_limit) {
                    goto fail;
                }
                attempt_resolving_bound = true;
                left = existing;
                goto retry_pair;
            }
            if (right->kind == ATOM_VAR) {
                cetta_runtime_stats_inc(
                    CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
#if MATCH_DECODED_WITH_FRAME
                Atom *right_existing = decoded_var_existing(
                    current, right_frame, right, NULL);
#else
                Atom *right_existing = bindings_lookup_value_id(current, (right)->var_id).skeleton;
#endif
                if (right_existing) {
                    if (++dereferences > dereference_limit) {
                        goto fail;
                    }
                    right = right_existing;
                    goto retry_pair;
                }
                if (left->var_id == right->var_id) continue;
            }
            bool added = builder
                ? bindings_builder_add_var_fresh(builder, left, right)
                : bindings_add_var(bindings, left, right);
            if (!added) {
                goto fail;
            }
#if MATCH_DECODED_WITH_FRAME
            decoded_frame_record(left_frame, left, right);
#endif
            continue;
        }
        if (right->kind == ATOM_VAR) {
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_DECODED_LOOKUP);
#if MATCH_DECODED_WITH_FRAME
            Atom *existing = decoded_var_existing(
                current, right_frame, right, NULL);
#else
            Atom *existing = bindings_lookup_value_id(current, (right)->var_id).skeleton;
#endif
            if (existing) {
                if (++dereferences > dereference_limit) {
                    goto fail;
                }
                attempt_resolving_bound = true;
                right = existing;
                goto retry_pair;
            }
            bool added = builder
                ? bindings_builder_add_var_fresh(builder, right, left)
                : bindings_add_var(bindings, right, left);
            if (!added) {
                goto fail;
            }
#if MATCH_DECODED_WITH_FRAME
            decoded_frame_record(right_frame, right, left);
#endif
            continue;
        }
        if (left->kind == ATOM_SYMBOL && right->kind == ATOM_SYMBOL) {
            if (left->sym_id != right->sym_id) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            continue;
        }
        if (left->kind == ATOM_GROUNDED && right->kind == ATOM_GROUNDED) {
            if (!atom_eq(left, right)) {
                if (attempt_resolving_bound)
                    attempt_repeated_var = true;
                goto fail;
            }
            continue;
        }
        if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR ||
            left->expr.len != right->expr.len) {
            if (attempt_resolving_bound)
                attempt_repeated_var = true;
            goto fail;
        }
        if (!match_path_enter(&path, left, right, false) ||
            !decoded_match_push_exit(&work, left, right))
            goto fail;
        /* Push in reverse so binding effects retain the recursive
           implementation's left-to-right traversal order. */
        for (CettaExprIndex i = left->expr.len; i > 1u; i--) {
            CettaExprIndex child = i - 1u;
            if (!decoded_match_push(&work, left->expr.elems[child],
                                    right->expr.elems[child])) {
                goto fail;
            }
        }
        /* The first child is next in the reference LIFO traversal. Reuse
         * this frame; leave siblings and the parent's exit marker queued.
         * No matching test, binding, or failure is moved past another. */
        if (left->expr.len != 0u) {
            Atom *next_left = left->expr.elems[0];
            Atom *next_right = right->expr.elems[0];
            cetta_runtime_stats_inc(
                CETTA_RUNTIME_COUNTER_MATCH_WORKLIST_FRAME_ELIDED);
            left = next_left;
            right = next_right;
            dereferences = 0u;
            dereference_limit = bindings_dereference_limit(current);
            goto retry_pair;
        }
    }
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    match_note_unification_attempt(
        true, attempt_start_len,
        builder ? builder->current.len
                : (bindings ? bindings->len : 0u),
        false);
    return true;

fail:
    decoded_match_worklist_free(&work);
    match_path_free(&path);
    match_note_unification_attempt(
        false, attempt_start_len,
        builder ? builder->current.len
                : (bindings ? bindings->len : 0u),
        attempt_repeated_var);
    return false;
}
