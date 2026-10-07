/* The structural eager adapter qualifies source before allocating a search
 * episode. Its equation and return policies are independent of PeTTa's source
 * translation. Effectful and typed applications retain their ordinary owner. */
typedef struct {
    Atom **terms;
    size_t length, capacity;
    SymbolId *heads;
    size_t head_count, head_capacity;
    Space *space;
    Arena *arena;
    bool open_root;
} HeExecutionAdmission;

static bool he_execution_intrinsic(SymbolId head) {
    return head == g_builtin_syms.let || head == g_builtin_syms.if_text ||
        head == g_builtin_syms.op_plus || head == g_builtin_syms.op_minus ||
        head == g_builtin_syms.op_mul || head == g_builtin_syms.op_eq ||
        head == g_builtin_syms.op_lt || head == g_builtin_syms.op_gt ||
        head == g_builtin_syms.op_le || head == g_builtin_syms.op_ge ||
        head == g_builtin_syms.empty_form;
}

static bool he_execution_type_name(Atom *type, const char *name) {
    return type && type->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(type), name) == 0;
}

static bool he_execution_intrinsic_signatures(HeExecutionAdmission *admission,
                                             SymbolId head) {
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(admission->space,
        admission->arena, atom_symbol_id(admission->arena, head), &types);
    unsigned mask = 0u;
    bool admitted = true;
    for (uint32_t index = 0u; admitted && index < count; index++) {
        Atom *type = types[index];
        unsigned bit = 0u;
        if (head == g_builtin_syms.empty_form &&
            atom_is_symbol_id(type, g_builtin_syms.undefined_type)) {
            bit = 1u;
        } else if (!is_function_type(type)) {
            admitted = false;
        } else if (head == g_builtin_syms.empty_form) {
            admitted = type->expr.len == 2u &&
                he_execution_type_name(type->expr.elems[1], "Atom");
            bit = 1u;
        } else {
            admitted = (type->expr.len == 4u || type->expr.len == 5u) &&
                he_execution_type_name(type->expr.elems[1], "Bool") &&
                he_execution_type_name(type->expr.elems[2], "Atom");
            if (admitted && type->expr.len == 4u) {
                admitted = atom_is_symbol_id(type->expr.elems[3],
                    g_builtin_syms.undefined_type);
                bit = 1u;
            } else if (admitted) {
                admitted = he_execution_type_name(type->expr.elems[3], "Atom") &&
                    type->expr.elems[4]->kind == ATOM_VAR;
                bit = 2u;
            }
        }
        admitted = admitted && bit && !(mask & bit);
        mask |= bit;
    }
    free(types);
    return admitted && mask == (head == g_builtin_syms.empty_form ? 1u : 3u);
}

static bool he_execution_push(HeExecutionAdmission *admission, Atom *term) {
    if (admission->length == admission->capacity) {
        size_t capacity = admission->capacity ? admission->capacity * 2u : 64u;
        if (capacity < admission->capacity || capacity > SIZE_MAX / sizeof(Atom *))
            return false;
        admission->terms = cetta_realloc(admission->terms, capacity * sizeof(Atom *));
        admission->capacity = capacity;
    }
    admission->terms[admission->length++] = term;
    return true;
}

static bool he_execution_primitive_signature(HeExecutionAdmission *admission,
                                             SymbolId head) {
    Atom **types = NULL;
    uint32_t count = eval_get_atom_types_profiled(admission->space,
        admission->arena, atom_symbol_id(admission->arena, head), &types);
    bool admitted = count == 1u && is_function_type(types[0]) &&
        types[0]->expr.len == 4u;
    if (admitted) {
        Atom **parts = types[0]->expr.elems;
        if (head == g_builtin_syms.op_eq) {
            admitted = parts[1]->kind == ATOM_VAR &&
                parts[2]->kind == ATOM_VAR &&
                parts[1]->var_id == parts[2]->var_id &&
                he_execution_type_name(parts[3], "Bool");
        } else {
            bool arithmetic = head == g_builtin_syms.op_plus ||
                head == g_builtin_syms.op_minus || head == g_builtin_syms.op_mul;
            admitted = he_execution_type_name(parts[1], "Number") &&
                he_execution_type_name(parts[2], "Number") &&
                he_execution_type_name(parts[3], arithmetic ? "Number" : "Bool");
        }
    }
    free(types);
    return admitted;
}

/* Recognize the structural contracts, including every physical occurrence.
 * An additional, reordered, or altered equation is not silently ignored. */
static bool he_execution_intrinsic_equations(Space *space, SymbolId head) {
    if ((eval_admitted_rule_forms() & CETTA_RULE_SYNTAX_DIRECTIONAL) &&
        space_directional_rules_may_match_known_head(space, head)) return false;
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(space, head, &cursor))
        return false;
    unsigned mask = 0u;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        SpaceEquationOccurrence occurrence;
        if (step != SPACE_EQUATION_CURSOR_ITEM ||
            !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(), &occurrence))
            return false;
        Atom *lhs = occurrence.lhs;
        Atom *rhs = occurrence.rhs;
        if (!lhs || lhs->kind != ATOM_EXPR || !lhs->expr.len ||
            atom_head_symbol_id(lhs) != head)
            return false;
        unsigned bit;
        if (head == g_builtin_syms.empty_form) {
            if (lhs->expr.len != 1u || !atom_is_legacy_empty_sentinel(rhs))
                return false;
            bit = 1u;
        } else {
            bool truth = false;
            if ((lhs->expr.len != 3u && lhs->expr.len != 4u) ||
                !petta_semantics_truth_value(lhs->expr.elems[1], &truth) ||
                lhs->expr.elems[2]->kind != ATOM_VAR ||
                (lhs->expr.len == 4u &&
                 lhs->expr.elems[3]->kind != ATOM_VAR))
                return false;
            bit = lhs->expr.len == 3u ? (truth ? 1u : 2u) : (truth ? 4u : 8u);
            if (lhs->expr.len == 3u && !truth) {
                if (!atom_is_computation_zero_form(rhs))
                    return false;
            } else if (!rhs || rhs->kind != ATOM_VAR ||
                       rhs->var_id != lhs->expr.elems[truth ? 2u : 3u]->var_id) {
                return false;
            }
        }
        if (mask & bit)
            return false;
        mask |= bit;
    }
    return mask == (head == g_builtin_syms.empty_form ? 1u : 15u);
}

static bool he_execution_source_admitted(HeExecutionAdmission *admission,
                                         Atom *source) {
    if (!he_execution_push(admission, source))
        return false;
    while (admission->length) {
        Atom *term = admission->terms[--admission->length];
        if (!term || atom_has_registry_refs(term) ||
            hyperpose_atom_has_thread_local_resource(term))
            return false;
        /* Names passed as values can later become a selected call head.
         * Inspect their equation closure as well; merely being inert at the
         * current source position is not a purity certificate. */
        bool expression = term->kind == ATOM_EXPR && term->expr.len;
        if (!expression && term->kind != ATOM_SYMBOL)
            continue;
        SymbolId head = expression ? atom_head_symbol_id(term) : term->sym_id;
        bool intrinsic = he_execution_intrinsic(head);
        if (intrinsic && head != g_builtin_syms.let &&
            head != g_builtin_syms.if_text && head != g_builtin_syms.empty_form &&
            !he_execution_primitive_signature(admission, head))
            return false;
        if (!intrinsic && head != SYMBOL_ID_NONE &&
            petta_semantics_form(head) != PETTA_FORM_NONE)
            return false;
        if ((head == g_builtin_syms.if_text ||
             head == g_builtin_syms.empty_form) &&
            !he_execution_intrinsic_signatures(admission, head))
            return false;
        /* The structural machine interprets computed heads by application.
         * HE can instead match an equation against the entire compound head,
         * including a closed SKI term. That boundary needs HE's canonical
         * owner until the machine has a whole-head equation policy. */
        if (expression && head == SYMBOL_ID_NONE &&
            (admission->open_root || term->expr.elems[0]->kind != ATOM_VAR))
            return false;
        if (expression && head == g_builtin_syms.if_text) {
            bool truth;
            if ((term->expr.len != 3u && term->expr.len != 4u) ||
                (!petta_semantics_truth_value(term->expr.elems[1], &truth) &&
                 (atom_head_symbol_id(term->expr.elems[1]) != g_builtin_syms.op_eq ||
                  expr_nargs(term->expr.elems[1]) != 2u)))
                return false;
        }
        if (intrinsic && eval_space_has_equations(admission->space, head) &&
            ((head != g_builtin_syms.if_text && head != g_builtin_syms.empty_form) ||
             !he_execution_intrinsic_equations(admission->space, head)))
            return false;
        if (!intrinsic && head != SYMBOL_ID_NONE &&
            (is_grounded_op(head) || symbol_id_is_builtin(head) ||
             space_head_declares_type(admission->space, head)))
            return false;
        if (!intrinsic && head != SYMBOL_ID_NONE &&
            eval_space_has_equations(admission->space, head)) {
            bool seen = false;
            for (size_t index = 0u; index < admission->head_count; index++)
                seen |= admission->heads[index] == head;
            if (!seen) {
                if (admission->head_count == admission->head_capacity) {
                    admission->head_capacity = admission->head_capacity
                        ? admission->head_capacity * 2u : 16u;
                    admission->heads = cetta_realloc(admission->heads,
                        admission->head_capacity * sizeof(SymbolId));
                }
                admission->heads[admission->head_count++] = head;
                SpaceEquationCursor cursor;
                if (!space_rule_cursor_init(admission->space, head, eval_admitted_rule_forms(), &cursor))
                    return false;
                for (;;) {
                    SpaceEquationOccurrenceId id;
                    SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
                    if (step == SPACE_EQUATION_CURSOR_END)
                        break;
                    SpaceEquationOccurrence occurrence;
                    /* Pattern constants can become callable values through
                     * a fresh match binding, just as body constants can. */
                    if (step != SPACE_EQUATION_CURSOR_ITEM ||
                        !space_rule_occurrence_resolve(id, eval_admitted_rule_forms(), &occurrence) ||
                        !he_execution_push(admission, occurrence.lhs) ||
                        !he_execution_push(admission, occurrence.rhs))
                        return false;
                }
            }
        }
        CettaExprIndex first = head == g_builtin_syms.let ? 2u :
            head == SYMBOL_ID_NONE ? 0u : 1u;
        for (CettaExprIndex index = first; expression && index < term->expr.len; index++) {
            if (!he_execution_push(admission, term->expr.elems[index]))
                return false;
        }
    }
    return true;
}

static bool he_shared_execution_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("CETTA_HE_SHARED_EXECUTION");
        enabled = !setting || strcmp(setting, "1") == 0;
    }
    return enabled;
}

static bool he_shared_execution_try(Space *space, Arena *arena, Atom *source,
    Atom *type, int fuel, const Bindings *environment, bool preserve_bindings,
    OutcomeSet *outcomes) {
    if (eval_current_language_id() != CETTA_LANGUAGE_HE)
        return false;
    /* A completed scalar is interpreted by HE's value/cast boundary. Only
     * expression calls can acquire a compiled computation owner. */
    if (!source || source->kind != ATOM_EXPR ||
        !he_shared_execution_enabled() || g_he_shared_execution ||
        cetta_parallel_worker_active() ||
        !g_library_context || fuel >= 0 ||
        (type && !atom_is_symbol_id(type, g_builtin_syms.undefined_type)) ||
        space_has_dependencies(space) || g_active_search_controller_requested ||
        g_active_search_controller_auto || gslt_match_chain_trace_active() ||
        petta_eval_machine_diagnostic_transition_limit())
        return false;
    /* Intrinsic controls retain the canonical demand adapter; their ordinary
     * children may select a compiled realization at their own call boundary. */
    SymbolId source_head = atom_head_symbol_id(source);
    if (he_execution_intrinsic(source_head))
        return false;
    /* The source qualification and the program observation form one read
     * window. A suspended program owner alone cannot establish currency.
     * Workers retain their canonical adapter until shared program mutation
     * and compiled episodes have a separately qualified concurrent contract. */
    CETTA_SCOPED_SHARED_TRANSITION(he_execution_observation);
    HeExecutionAdmission admission = {.space = space, .arena = arena,
        .open_root = atom_has_vars(source)};
    bool admitted = he_execution_source_admitted(&admission, source);
    free(admission.terms);
    free(admission.heads);
    if (!admitted)
        return false;
    /* Retain the existing qualified closed realizations before allocating a
     * relational episode. A decline publishes nothing and leaves the source
     * for the general machine; open calls never enter this closed attempt. */
    if (!admission.open_root && source && source->kind == ATOM_EXPR &&
        source_head != SYMBOL_ID_NONE && !symbol_id_is_builtin(source_head)) {
        Bindings empty;
        bindings_init(&empty);
        const Bindings *outer = environment ? environment : &empty;
        PreparedPureProgramCache *cache = eval_execution_prepared_cache();
        Atom *single = prepared_pure_closed_call_try(
            space, arena, source, fuel, false, false, cache);
        if (single) {
            outcome_set_add_prefixed(arena, outcomes, single, &empty, outer,
                preserve_bindings);
            return true;
        }
        if (eval_unwinding() || eval_cancel_check())
            return true;
        CettaPreparedPureAnswersResult answers = prepared_pure_closed_answers_try(
            space, arena, source, fuel, cache, outer, preserve_bindings, outcomes);
        if (answers == CETTA_PREPARED_PURE_ANSWERS_COMPLETE ||
            answers == CETTA_PREPARED_PURE_ANSWERS_STOPPED)
            return true;
    }
    if (!g_library_context->petta_program)
        g_library_context->petta_program = petta_program_new_with_rules(eval_admitted_rule_forms());
    if (!g_library_context->petta_program ||
        !petta_program_synchronize_space(g_library_context->petta_program, space))
        return false;
    bool enclosing_active = g_active_petta_search_machine;
    g_he_shared_execution = true;
    g_active_petta_search_machine = true;
    bool complete = petta_eval_machine_run(space, arena, source, type, fuel,
        environment, preserve_bindings, outcomes, PETTA_RELATION_SAFETY_STATIC,
        CETTA_SEARCH_CONTROLLER_INLINE_DEPTH_FIRST, false, false);
    g_active_petta_search_machine = enclosing_active;
    g_he_shared_execution = false;
    return complete;
}
