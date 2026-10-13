/* Directional joins reuse the machine's ordinary occurrence choices. The
 * template slot owns an immutable batch: policy, space, patterns, rows,
 * occurrence identities, matching images, protected identities and original
 * call. An extension checks only the new premise. Every saved prefix is
 * ordinary owned syntax, so collection, backtracking and multi-shot capture
 * use the existing machine visitors. No unfinished leg publishes bindings. */
enum {
    PAT_QUERY_POLICY, PAT_QUERY_SPACE, PAT_QUERY_PATTERNS,
    PAT_QUERY_ROWS, PAT_QUERY_OCCURRENCES, PAT_QUERY_WITNESS,
    PAT_QUERY_SOURCE, PAT_QUERY_VARIABLES, PAT_QUERY_VALUES, PAT_QUERY_PROTECTED,
    PAT_QUERY_FIELDS
};

static bool petta_machine_pattern_query_fault(PettaMachineImpl *machine,
                                              Atom *source, const char *reason) {
    return petta_machine_raise_error(machine,
        atom_error(&machine->heap, source, atom_symbol(&machine->heap, reason)));
}

static Space *petta_pattern_resolve_space(PettaMachineImpl *machine,
                                          Atom *reference) {
    Space *target = machine->host.resolve_space
        ? machine->host.resolve_space(machine->host.context, machine->space,
            &machine->heap, reference) : NULL;
    if (!target && atom_is_symbol(reference, "&self")) target = machine->space;
    return target;
}

static Atom *petta_pattern_append(Arena *arena, Atom *sequence, Atom *value) {
    CettaExprLen n = sequence->expr.len;
    if (n == UINT32_MAX) return NULL;
    Atom **items = arena_alloc(arena, ((size_t)n + 1u) * sizeof(*items));
    if (n) memcpy(items, sequence->expr.elems, (size_t)n * sizeof(*items));
    items[n] = value;
    return atom_expr(arena, items, n + 1u);
}

static CettaTermMatchMode petta_pattern_mode(Atom *policy) {
    return atom_is_symbol(policy, "match%") ? CETTA_TERM_MATCH_FORWARD
        : atom_is_symbol(policy, "%match") ? CETTA_TERM_MATCH_REVERSE
        : CETTA_TERM_MATCH_VARIANT;
}

static CettaTermMatchState petta_pattern_prefix(Atom **parts) {
    Atom *variables = parts[PAT_QUERY_VARIABLES];
    Atom *values = parts[PAT_QUERY_VALUES];
    Atom *protected = parts[PAT_QUERY_PROTECTED];
    return (CettaTermMatchState){
        .images = {variables->expr.elems, values->expr.elems, variables->expr.len},
        .protected_variables = protected->expr.elems,
        .protected_count = protected->expr.len,
        .mode = petta_pattern_mode(parts[PAT_QUERY_POLICY])};
}

static Atom *petta_pattern_variable_image(Arena *arena, Atom *variable, void *raw) {
    (void)arena;
    Atom *value = cetta_var_map_lookup(raw, variable->var_id);
    return value ? value : variable;
}

/* The index sees an instantiated approximation. The matcher always sees the
 * original query, so a captured subject variable never becomes a new hole. */
static Atom *petta_pattern_index_pattern(Arena *arena, Atom **parts, Atom *pattern) {
    if (atom_is_symbol(parts[PAT_QUERY_POLICY], "unify"))
        return bindings_apply_saved(arena, parts[PAT_QUERY_WITNESS], pattern);
    if (!atom_is_symbol(parts[PAT_QUERY_POLICY], "match%")) return pattern;
    CettaTermMatchState prefix = petta_pattern_prefix(parts);
    if (!prefix.images.count) return pattern;
    CettaVarMap images;
    cetta_var_map_init(&images);
    for (uint32_t i = 0u; i < prefix.images.count; ++i) {
        if (!cetta_var_map_add(&images, prefix.images.variables[i]->var_id,
                prefix.images.values[i])) {
            cetta_var_map_free(&images);
            return NULL;
        }
    }
    Atom *result = cetta_atom_rewrite_vars(arena, pattern,
        petta_pattern_variable_image, &images, true);
    cetta_var_map_free(&images);
    return result;
}

static CettaTermMatchStatus petta_pattern_extend(Arena *arena, Atom **parts,
    Atom *row) {
    CettaExprLen index = parts[PAT_QUERY_ROWS]->expr.len;
    Atom *pattern = parts[PAT_QUERY_PATTERNS]->expr.elems[index];
    if (atom_is_symbol(parts[PAT_QUERY_POLICY], "unify")) {
        Bindings bindings;
        if (!bindings_restore_captured_value(parts[PAT_QUERY_WITNESS], &bindings))
            return CETTA_TERM_MATCH_NO_MEMORY;
        bool matched = match_atoms(pattern, row, &bindings, arena);
        Atom *witness = matched ? bindings_capture_value(arena, &bindings) : NULL;
        bindings_free(&bindings);
        if (!matched) return CETTA_TERM_MATCH_MISMATCH;
        if (!witness) return CETTA_TERM_MATCH_NO_MEMORY;
        parts[PAT_QUERY_WITNESS] = witness;
        return CETTA_TERM_MATCH_OK;
    }
    CettaTermMatchState prefix = petta_pattern_prefix(parts), next;
    CettaTermMatchPair pair = {pattern, row};
    CettaTermMatchStatus status = term_graph_match_extend(arena, &prefix, &pair,
        1u, prefix.mode, atom_eq, &next);
    if (status != CETTA_TERM_MATCH_OK) return status;
    parts[PAT_QUERY_VARIABLES] = atom_expr(arena, next.images.variables,
        next.images.count);
    parts[PAT_QUERY_VALUES] = atom_expr(arena, next.images.values,
        next.images.count);
    parts[PAT_QUERY_PROTECTED] = atom_expr(arena, next.protected_variables,
        next.protected_count);
    return CETTA_TERM_MATCH_OK;
}

static bool petta_machine_pattern_next(PettaMachineImpl *machine, Atom *state,
    Atom *expected, uint32_t barrier, const PettaPlanNode *plan,
    PettaMachineStep *failure) {
    Atom **parts = state->expr.elems;
    Atom *patterns = parts[PAT_QUERY_PATTERNS];
    Atom *rows = parts[PAT_QUERY_ROWS];
    Atom *witness = parts[PAT_QUERY_WITNESS];
    if (!rows->expr.len && !patterns->expr.len &&
        !petta_pattern_resolve_space(machine, parts[PAT_QUERY_SPACE]))
        return petta_machine_pattern_query_fault(machine, parts[PAT_QUERY_SPACE],
            "ExpectedSpace");
    if (rows->expr.len == patterns->expr.len) {
        Atom *source = parts[PAT_QUERY_SOURCE];
        SymbolId head = atom_head_symbol_id(source);
        SymbolId resolved = eval_current_pattern_head ? eval_current_pattern_head(head) : SYMBOL_ID_NONE;
        if (resolved != SYMBOL_ID_NONE) head = resolved;
        if (head != g_builtin_syms.pat_query) {
            bool reverse = head == g_builtin_syms.pat_match_reverse;
            const Bindings *env = search_context_bindings(&machine->search);
            Atom *output = reverse ? bindings_apply_graph((Bindings *)env,
                &machine->heap, source->expr.elems[3]) : NULL;
            if (reverse && !output) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            CettaTermMatchStatus status = cetta_pattern_bind_retrieval(&machine->heap,
                patterns, rows, output,
                atom_head_symbol_id(source->expr.elems[2]) == g_builtin_syms.comma,
                search_context_builder(&machine->search));
            if (status == CETTA_TERM_MATCH_MISMATCH) return false;
            if (status == CETTA_TERM_MATCH_INVALID)
                return petta_machine_pattern_query_fault(machine, source, "InvalidTermGraph");
            if (status != CETTA_TERM_MATCH_OK) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            CettaExprIndex body = reverse ? 4u : 3u;
            return petta_push_solve_planned(machine, source->expr.elems[body],
                expected, barrier, petta_plan_child(plan, body));
        }
        if (!atom_is_symbol(parts[PAT_QUERY_POLICY], "unify")) {
            CettaTermMatchState prefix = petta_pattern_prefix(parts);
            witness = bindings_capture_images(&machine->heap,
                prefix.images.variables, prefix.images.values, prefix.images.count,
                prefix.mode == CETTA_TERM_MATCH_VARIANT);
            if (!witness) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
        }
        Atom **hits = rows->expr.len ? arena_alloc(&machine->heap,
            (size_t)rows->expr.len * sizeof(*hits)) : NULL;
        for (CettaExprIndex i = 0u; i < rows->expr.len; ++i) {
            Atom *row = rows->expr.elems[i];
            /* A variant witness maps query variables to row variables; it
             * is not a substitution on the row. */
            Atom *view = atom_is_symbol(parts[PAT_QUERY_POLICY], "variant")
                ? row : bindings_apply_saved(&machine->heap, witness, row);
            if (!view) {
                *failure = PETTA_MACHINE_STEP_CAPACITY;
                return false;
            }
            Atom *fields[] = {atom_symbol(&machine->heap, "pat:row"),
                parts[PAT_QUERY_OCCURRENCES]->expr.elems[i], row, view};
            hits[i] = atom_expr(&machine->heap, fields, 4u);
        }
        Atom *fields[] = {atom_symbol(&machine->heap, "pat:hit"),
            atom_expr(&machine->heap, hits, rows->expr.len), witness};
        return petta_push_unify(machine, atom_expr(&machine->heap, fields, 3u),
            expected, barrier);
    }
    Atom *pattern = patterns->expr.elems[rows->expr.len];
    pattern = petta_pattern_index_pattern(&machine->heap, parts, pattern);
    Space *target = petta_pattern_resolve_space(machine, parts[PAT_QUERY_SPACE]);
    if (!target)
        return petta_machine_pattern_query_fault(machine, parts[PAT_QUERY_SPACE],
            "ExpectedSpace");
    if (!pattern) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return petta_machine_start_match_choice_policy(machine, target, pattern,
        state, expected, barrier, plan, true);
}

static bool petta_machine_pattern_candidate(PettaMachineImpl *machine,
    PettaChoice *choice, Atom *candidate, CettaIndex occurrence,
    PettaMachineStep *failure) {
    /* Failed candidates have no published bindings or retained branch heap. */
    if (!petta_choice_restore(machine, choice)) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    machine->stats.match_candidates++;
    Atom *state = choice->as.match.template;
    CETTA_FRAME_IDENTITY_SCOPE(frame);
    Atom *row = atom_has_vars(candidate)
        ? petta_machine_freshen_atom(machine, candidate,
            cetta_frame_identity_scope_fresh(&frame)) : candidate;
    if (!row) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    Atom *parts[PAT_QUERY_FIELDS];
    memcpy(parts, state->expr.elems, sizeof(parts));
    CettaTermMatchStatus status = petta_pattern_extend(&machine->heap, parts, row);
    if (status == CETTA_TERM_MATCH_MISMATCH) return false;
    if (status == CETTA_TERM_MATCH_INVALID)
        return petta_machine_pattern_query_fault(machine, row, "InvalidTermGraph");
    if (status != CETTA_TERM_MATCH_OK || occurrence > INT64_MAX ||
        choice->as.match.read.instance_id > INT64_MAX ||
        choice->as.match.read.revision > INT64_MAX) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    Atom *identity_fields[] = {atom_symbol(&machine->heap, "pat:occurrence"),
        atom_int(&machine->heap, (int64_t)choice->as.match.read.instance_id),
        atom_int(&machine->heap, (int64_t)choice->as.match.read.revision),
        atom_int(&machine->heap, (int64_t)occurrence)};
    Atom *identity = atom_expr(&machine->heap, identity_fields, 4u);
    Atom *occurrences = petta_pattern_append(&machine->heap,
        state->expr.elems[PAT_QUERY_OCCURRENCES], identity);
    if (!occurrences) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    parts[PAT_QUERY_ROWS] = petta_pattern_append(&machine->heap,
        state->expr.elems[PAT_QUERY_ROWS], row);
    parts[PAT_QUERY_OCCURRENCES] = occurrences;
    if (!parts[PAT_QUERY_ROWS]) {
        *failure = PETTA_MACHINE_STEP_CAPACITY;
        return false;
    }
    return petta_goal_push(machine, &(PettaGoal){
        .kind = PETTA_GOAL_PATTERN_NEXT, .barrier = choice->barrier,
        .first = atom_expr(&machine->heap, parts, PAT_QUERY_FIELDS),
        .second = choice->as.match.expected, .plan = choice->as.match.template_plan});
}

static bool petta_machine_pattern_start(PettaMachineImpl *machine, Atom *source,
    Atom *expected, uint32_t barrier, const PettaPlanNode *plan,
    PettaMachineStep *failure) {
    SymbolId head = atom_head_symbol_id(source);
    SymbolId resolved = eval_current_pattern_head ? eval_current_pattern_head(head) : SYMBOL_ID_NONE;
    if (resolved != SYMBOL_ID_NONE) head = resolved;
    bool explicit_policy = head == g_builtin_syms.pat_query;
    if (source->expr.len != (head == g_builtin_syms.pat_match_reverse ? 5u : 4u))
        return petta_machine_pattern_query_fault(machine, source,
            "IncorrectNumberOfArguments");
    Atom *policy = explicit_policy ? source->expr.elems[2]
        : atom_symbol(&machine->heap,
            head == g_builtin_syms.pat_match_forward ? "match%" : "%match");
    if (!atom_is_symbol(policy, "match%") && !atom_is_symbol(policy, "%match") &&
        !atom_is_symbol(policy, "unify") && !atom_is_symbol(policy, "variant"))
        return petta_machine_pattern_query_fault(machine, source,
            "UnknownMatchingPolicy");
    Atom *pattern = source->expr.elems[explicit_policy ? 3u : 2u];
    Atom *patterns = atom_head_symbol_id(pattern) == g_builtin_syms.comma
        ? atom_expr(&machine->heap, pattern->expr.elems + 1u, pattern->expr.len - 1u)
        : atom_expr(&machine->heap, &pattern, 1u);
    Atom *empty = atom_expr(&machine->heap, NULL, 0u);
    Atom *witness = empty;
    if (atom_is_symbol(policy, "unify")) {
        Bindings bindings;
        bindings_init(&bindings);
        witness = bindings_capture_value(&machine->heap, &bindings);
        bindings_free(&bindings);
        if (!witness) {
            *failure = PETTA_MACHINE_STEP_CAPACITY;
            return false;
        }
    }
    Atom *parts[] = {policy, source->expr.elems[1], patterns, empty, empty,
        witness, source, empty, empty, empty};
    return petta_machine_pattern_next(machine,
        atom_expr(&machine->heap, parts, PAT_QUERY_FIELDS), expected, barrier, plan, failure);
}
