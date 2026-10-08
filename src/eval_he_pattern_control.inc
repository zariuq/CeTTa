/* Directional controls use HE's existing owned source/body continuations.
 * A completed retrieval publishes bindings before the ordinary body runs. */
static void he_pattern_error(HeCanonicalFrame *frame, const char *reason) {
    outcome_set_add(&frame->paths, atom_error(frame->arena, frame->atom,
        atom_symbol(frame->arena, reason)), &frame->env);
    frame->phase = 2u;
}

static void he_canonical_resume_pattern_retrieval(HeCanonicalFrame *frame, SymbolId head) {
    Arena *a = frame->arena;
    bool reverse = head == g_builtin_syms.pat_match_reverse;
    if (!frame->phase) {
        if (frame->atom->expr.len != cetta_pattern_adapter_arity(head) + 1u) {
            he_pattern_error(frame, "IncorrectNumberOfArguments");
            return;
        }
        frame->phase = 1u;
        he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
            expr_arg(frame->atom, 0u), NULL, frame->fuel, &frame->env, true, &frame->child);
        return;
    }
    if (frame->head_index == frame->work.len) {
        outcome_set_free(&frame->work);
        outcome_set_init(&frame->work);
        frame->head_index = 0u;
        if (frame->index == frame->child.len) {
            frame->phase = 2u;
            return;
        }
        Outcome *source = &frame->child.items[frame->index++];
        Atom *space = outcome_atom_materialize(a, source);
        if (atom_is_error(space)) {
            outcome_set_add_existing(&frame->paths, source);
            return;
        }
        frame->live[0] = bindings_apply_graph(&source->env, a, expr_arg(frame->atom, 1u));
        frame->live[1] = reverse
            ? bindings_apply_graph(&source->env, a, expr_arg(frame->atom, 2u)) : NULL;
        Atom *args[] = {space, atom_symbol(a, reverse ? "%match" : "match%"), frame->live[0]};
        CettaCallOutcome result;
        eval_pattern_query_ready(frame->space, a, atom_symbol_id(a, g_builtin_syms.pat_query),
            args, 3u, &result);
        if (result.kind == CETTA_CALL_RAISED) {
            outcome_set_add(&frame->paths, result.term, &source->env);
            return;
        }
        if (result.kind != CETTA_CALL_VALUE) return;
        for (CettaExprIndex i = 0u; i < result.term->expr.len; ++i)
            outcome_set_add(&frame->work, result.term->expr.elems[i], &source->env);
        if (!frame->work.len) return;
    }
    Outcome *hit = &frame->work.items[frame->head_index++];
    Atom *records = hit->atom->expr.elems[1];
    Atom **rows = records->expr.len ? arena_alloc(a, (size_t)records->expr.len * sizeof(*rows)) : NULL;
    for (CettaExprIndex i = 0u; i < records->expr.len; ++i)
        rows[i] = records->expr.elems[i]->expr.elems[2];
    bool conjunction = atom_head_symbol_id(frame->live[0]) == g_builtin_syms.comma;
    Atom *patterns = conjunction
        ? atom_expr(a, frame->live[0]->expr.elems + 1u, frame->live[0]->expr.len - 1u)
        : atom_expr(a, &frame->live[0], 1u);
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, &hit->env)) {
        he_pattern_error(frame, "BindingPublicationFailed");
        return;
    }
    CettaTermMatchStatus status = cetta_pattern_bind_retrieval(a, patterns,
        atom_expr(a, rows, records->expr.len), frame->live[1], conjunction, &builder);
    if (status == CETTA_TERM_MATCH_OK)
        he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
            expr_arg(frame->atom, reverse ? 3u : 2u), frame->type, frame->fuel,
            bindings_builder_bindings(&builder), frame->preserve_bindings, &frame->paths);
    else if (status != CETTA_TERM_MATCH_MISMATCH)
        he_pattern_error(frame, status == CETTA_TERM_MATCH_INVALID
            ? "InvalidTermGraph" : "BindingPublicationFailed");
    bindings_builder_free(&builder);
}

static void he_canonical_resume_pattern(HeCanonicalFrame *frame) {
    Arena *a = frame->arena;
    SymbolId head = eval_current_pattern_head(atom_head_symbol_id(frame->atom));
    bool branching = head == g_builtin_syms.pat_case_forward;
    bool raw = head == g_builtin_syms.pat_unify_forward;
    if (frame->phase == 2u) {
        he_canonical_publish(frame, &frame->paths, false);
        he_canonical_pop();
        return;
    }
    if (head == g_builtin_syms.pat_match_forward || head == g_builtin_syms.pat_match_reverse) {
        he_canonical_resume_pattern_retrieval(frame, head);
        return;
    }
    if (!frame->phase) {
        uint32_t arity = cetta_pattern_adapter_arity(head);
        if (frame->atom->expr.len != arity + 1u) {
            he_pattern_error(frame, "IncorrectNumberOfArguments");
            return;
        }
        frame->phase = 1u;
        Atom *source = expr_arg(frame->atom, branching ? 0u : 1u);
        if (raw) {
            outcome_set_add(&frame->child,
                bindings_apply_graph(&frame->env, a, source), &frame->env);
        } else {
            he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
                source, NULL, frame->fuel, &frame->env, true, &frame->child);
            return;
        }
    }
    if (frame->index == frame->child.len) {
        frame->phase = 2u;
        return;
    }
    Outcome *source = &frame->child.items[frame->index++];
    Atom *value = outcome_atom_materialize(a, source);
    if (atom_is_legacy_empty_sentinel(value)) return;
    if (!raw && !branching && atom_is_error(value)) {
        outcome_set_add_existing(&frame->paths, source);
        return;
    }
    BindingsBuilder builder;
    if (!bindings_builder_init(&builder, &frame->env)) {
        he_pattern_error(frame, "BindingPublicationFailed");
        return;
    }
    if (!bindings_builder_merge_commit(&builder, &source->env)) {
        bindings_builder_free(&builder);
        return;
    }
    Atom *body = NULL;
    CettaTermMatchStatus status = CETTA_TERM_MATCH_MISMATCH;
    if (branching) {
        Atom *branches = expr_arg(frame->atom, 1u);
        if (branches->kind != ATOM_EXPR) status = CETTA_TERM_MATCH_INVALID;
        else for (CettaExprIndex i = 0u; i < branches->expr.len; ++i) {
            Atom *branch = branches->expr.elems[i];
            if (branch->kind != ATOM_EXPR || branch->expr.len != 2u) {
                status = CETTA_TERM_MATCH_INVALID;
                break;
            }
            CettaTermMatchPair pair = {bindings_apply_graph(
                (Bindings *)bindings_builder_bindings(&builder), a, branch->expr.elems[0]), value};
            status = cetta_pattern_bind_many(a, &pair, 1u, &builder);
            if (status != CETTA_TERM_MATCH_MISMATCH) {
                body = branch->expr.elems[1];
                break;
            }
        }
    } else {
        CettaTermMatchPair pair = {bindings_apply_graph(
            (Bindings *)bindings_builder_bindings(&builder), a, expr_arg(frame->atom, 0u)), value};
        status = cetta_pattern_bind_many(a, &pair, 1u, &builder);
        body = expr_arg(frame->atom, 2u);
        if (raw && status == CETTA_TERM_MATCH_MISMATCH) {
            body = expr_arg(frame->atom, 3u);
            status = CETTA_TERM_MATCH_OK;
        }
    }
    if (status == CETTA_TERM_MATCH_OK) {
        const Bindings *env = bindings_builder_bindings(&builder);
        he_canonical_push(HE_CANONICAL_CALLER, frame->space, a,
            body, frame->type, frame->fuel, env,
            frame->preserve_bindings, &frame->paths);
    } else if (status != CETTA_TERM_MATCH_MISMATCH) {
        he_pattern_error(frame, status == CETTA_TERM_MATCH_INVALID
            ? "InvalidTermGraph" : "BindingPublicationFailed");
    }
    bindings_builder_free(&builder);
}
