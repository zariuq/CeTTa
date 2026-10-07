/* Owned native return and sequencing for closed, untyped, pure answer
 * producers. HE and PeTTa supply demand and representation adapters; both
 * use the same cursor owner, pending-return state and residual transfer. */
static size_t prepared_pure_nursery_budget_bytes(void);
static uint64_t petta_eval_machine_diagnostic_transition_limit(void);
typedef enum {
    EVAL_OWNED_DATA,
    EVAL_OWNED_NUMBER,
    EVAL_OWNED_BOOLEAN,
} EvalOwnedShape;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
    EvalOwnedShape shape;
} EvalOwnedHead;

typedef struct {
    Space *space;
    SpaceReadToken read;
    EvalOwnedHead *heads;
    size_t len, cap;
} EvalOwnedAdmission;

static bool eval_owned_is_number(const Atom *value) {
    return value && value->kind == ATOM_GROUNDED &&
        (value->ground.gkind == GV_INT || value->ground.gkind == GV_FLOAT);
}

/* A conservative first-order demand check for the direct consumers. All
 * callable inputs are numbers; every arithmetic operand is numeric and all
 * guards are Boolean. Division, foreign calls, effects, free variables and
 * typed calls keep the ordinary dialect boundary before delivery starts.
 * Recursive components preserve these shapes; they need not terminate. */
static bool eval_owned_shape(EvalOwnedAdmission *admission, Atom *term,
    const VarId *variables, size_t variable_count, EvalOwnedShape shape,
    unsigned depth) {
    if (!term || depth > 256u)
        return false;
    if (eval_owned_is_number(term))
        return shape != EVAL_OWNED_BOOLEAN;
    if (term->kind == ATOM_VAR) {
        for (size_t i = 0u; i < variable_count; i++)
            if (variables[i] == term->var_id)
                return shape != EVAL_OWNED_BOOLEAN;
        return false;
    }
    if (term->kind == ATOM_GROUNDED && term->ground.gkind == GV_BOOL)
        return shape != EVAL_OWNED_NUMBER;
    if (term->kind == ATOM_SYMBOL)
        return shape == EVAL_OWNED_DATA ||
            (shape == EVAL_OWNED_BOOLEAN &&
             (atom_is_symbol_id(term, g_builtin_syms.true_text) ||
              atom_is_symbol_id(term, g_builtin_syms.false_text)));
    if (term->kind != ATOM_EXPR || term->expr.len == 0u)
        return shape == EVAL_OWNED_DATA && term->kind == ATOM_EXPR;
    SymbolId head = atom_head_symbol_id(term);
    CettaExprLen arity = expr_nargs(term);
    if (head == g_builtin_syms.empty_form && arity == 0u)
        return true;
    if (head == g_builtin_syms.if_text && arity == 3u)
        return eval_owned_shape(admission, expr_arg(term, 0u), variables,
                   variable_count, EVAL_OWNED_BOOLEAN, depth + 1u) &&
               eval_owned_shape(admission, expr_arg(term, 1u), variables,
                   variable_count, shape, depth + 1u) &&
               eval_owned_shape(admission, expr_arg(term, 2u), variables,
                   variable_count, shape, depth + 1u);
    bool arithmetic = head == g_builtin_syms.op_plus ||
        head == g_builtin_syms.op_minus || head == g_builtin_syms.op_mul;
    bool comparison = head == g_builtin_syms.op_lt ||
        head == g_builtin_syms.op_gt || head == g_builtin_syms.op_le ||
        head == g_builtin_syms.op_ge;
    if (arithmetic || comparison) {
        if (arity != 2u || !prepared_fold_register_program(head, arity) ||
            space_equations_may_match_known_head(admission->space, head) ||
            (arithmetic && shape == EVAL_OWNED_BOOLEAN) ||
            (comparison && shape == EVAL_OWNED_NUMBER))
            return false;
        return eval_owned_shape(admission, expr_arg(term, 0u), variables,
                   variable_count, EVAL_OWNED_NUMBER, depth + 1u) &&
               eval_owned_shape(admission, expr_arg(term, 1u), variables,
                   variable_count, EVAL_OWNED_NUMBER, depth + 1u);
    }
    if (head == SYMBOL_ID_NONE || is_grounded_op(head) ||
        symbol_id_is_builtin(head) ||
        (eval_current_language_id() == CETTA_LANGUAGE_PETTA &&
         petta_program_head_is_intrinsic(head)))
        return false;
    bool defined = space_equations_may_match_known_head(admission->space, head);
    if (eval_current_language_id() == CETTA_LANGUAGE_PETTA && g_library_context &&
        (cetta_library_petta_translator_rule_contains(g_library_context, head) ||
         cetta_library_petta_tabled_relation_contains(g_library_context, head, arity) ||
         cetta_library_petta_memo_contains(g_library_context, head, arity)))
        return false;
    if (!defined) {
        if (shape != EVAL_OWNED_DATA)
            return false;
        for (CettaExprIndex i = 1u; i < term->expr.len; i++)
            if (!eval_owned_shape(admission, term->expr.elems[i], variables,
                                 variable_count, EVAL_OWNED_DATA, depth + 1u))
                return false;
        return true;
    }
    if (arity > 64u || space_head_declares_type(admission->space, head))
        return false;
    for (CettaExprIndex i = 1u; i < term->expr.len; i++)
        if (!eval_owned_shape(admission, term->expr.elems[i], variables,
                             variable_count, EVAL_OWNED_NUMBER, depth + 1u))
            return false;
    for (size_t i = 0u; i < admission->len; i++)
        if (admission->heads[i].head == head &&
            admission->heads[i].arity == arity &&
            admission->heads[i].shape == shape)
            return true;
    if (admission->len == admission->cap) {
        size_t cap = admission->cap ? admission->cap * 2u : 16u;
        if (cap < admission->cap || cap > SIZE_MAX / sizeof(*admission->heads))
            return false;
        admission->heads = cetta_realloc(admission->heads,
                                         cap * sizeof(*admission->heads));
        admission->cap = cap;
    }
    admission->heads[admission->len++] = (EvalOwnedHead){head, arity, shape};
    SpaceEquationCursor cursor;
    if (!space_equation_cursor_init(admission->space, head, &cursor))
        return false;
    bool found = false;
    for (;;) {
        SpaceEquationOccurrenceId id;
        SpaceEquationCursorStep next = space_equation_cursor_next(&cursor, &id);
        if (next == SPACE_EQUATION_CURSOR_END)
            return found && space_read_token_is_current(admission->read);
        SpaceEquationOccurrence occurrence;
        if (next != SPACE_EQUATION_CURSOR_ITEM ||
            !space_equation_occurrence_resolve(id, &occurrence) ||
            occurrence.lhs->kind != ATOM_EXPR || occurrence.lhs->expr.len == 0u ||
            atom_head_symbol_id(occurrence.lhs) != head ||
            expr_nargs(occurrence.lhs) != arity)
            return false;
        VarId parameters[64];
        size_t parameter_count = 0u;
        for (CettaExprIndex i = 1u; i < occurrence.lhs->expr.len; i++) {
            Atom *parameter = occurrence.lhs->expr.elems[i];
            if (parameter->kind == ATOM_VAR)
                parameters[parameter_count++] = parameter->var_id;
            else if (!eval_owned_is_number(parameter))
                return false;
        }
        if (!eval_owned_shape(admission, occurrence.rhs, parameters,
                              parameter_count, shape, depth + 1u))
            return false;
        found = true;
    }
}

static bool eval_owned_producer_admitted(Space *space, Atom *producer,
                                         EvalOwnedShape shape) {
    EvalOwnedAdmission admission = {.space = space, .read = space_read_token(space)};
    bool admitted = eval_owned_shape(&admission, producer, NULL, 0u, shape, 0u) &&
        space_read_token_is_current(admission.read);
    free(admission.heads);
    return admitted;
}
typedef struct {
    CettaLanguageId language;
    CettaPreparedPureBooleanValue boolean_value;
    CettaPreparedPureConstructValue construct_value;
    CettaPreparedPureConstructsExpression constructs_expression;
    CettaPreparedPureOpaqueValue opaque_value;
    CettaPreparedPureRegisterViewFn register_view;
    CettaPreparedPurePatternViewFn pattern_view;
    CettaPreparedPureExpressionViewFn expression_view;
} EvalOwnedDemandAdapter;

static EvalOwnedDemandAdapter eval_owned_demand_adapter(void) {
    CettaLanguageId language = eval_current_language_id();
    bool petta = language == CETTA_LANGUAGE_PETTA;
    return (EvalOwnedDemandAdapter){
        .language = language,
        .boolean_value = petta ? petta_semantics_boolean_value : atom_bool,
        .construct_value = petta ? petta_semantics_construct_value : atom_expr,
        .constructs_expression = petta
            ? petta_semantics_construct_value_is_expression
            : cetta_prepared_pure_constructs_expression_always,
        .opaque_value = petta ? petta_semantics_is_opaque_runtime_value : NULL,
        .register_view = petta
            ? petta_prepared_pure_register_view : cetta_prepared_pure_register_view,
        .pattern_view = petta ? petta_prepared_pure_pattern_view : NULL,
        .expression_view = prepared_pure_expression_view_for_language(language),
    };
}

typedef struct {
    Arena captures;
    CettaPreparedPureProgram *program;
    CettaOwnedExecution execution;
    CettaPreparedPureAnswerCursor *cursor;
    Space *space;
    CettaLibraryContext *library;
    CettaLibPrologReadToken foreign;
    uint64_t admission_revision;
    uint64_t foreign_capability_revision;
    uint32_t active_mask;
    EvalOwnedDemandAdapter adapter;
    bool captures_ready;
} EvalOwnedAnswers;

static bool eval_owned_answers_authority_current(const EvalOwnedAnswers *answers) {
    CettaLibraryContext *library = answers->library;
    if (library != g_library_context || !cetta_prepared_pure_program_is_current(answers->program))
        return false;
    if (!library)
        return true;
    CettaLibPrologReadToken foreign = cetta_lib_prolog_read_token(library->lib_prolog);
    return library->active_mask == answers->active_mask &&
        cetta_library_petta_admission_revision(library) == answers->admission_revision &&
        foreign.instance_id == answers->foreign.instance_id &&
        foreign.revision == answers->foreign.revision &&
        (!library->lib_prolog || petta_libpl_capability_revision(library->lib_prolog) ==
                                answers->foreign_capability_revision);
}

static CettaOwnedExecutionStep eval_owned_answers_next(void *context,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    EvalOwnedAnswers *answers = context;
    if (!eval_owned_answers_authority_current(answers)) {
        *completion = CETTA_EVAL_INCOMPLETE_INVALIDATED;
        return CETTA_OWNED_EXECUTION_HANDOFF;
    }
    return cetta_prepared_pure_answer_cursor_return(answers->cursor, returned, completion);
}

static void eval_owned_answers_release(void *context) {
    EvalOwnedAnswers *answers = context;
    cetta_prepared_pure_answer_cursor_close(answers->cursor);
    answers->cursor = NULL;
}

static inline void eval_owned_answers_note_storage(const EvalOwnedAnswers *answers) {
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaPreparedPureCursorStorage storage =
        cetta_prepared_pure_answer_cursor_storage(answers->cursor);
    cetta_runtime_stats_update_max(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_STORAGE_PEAK,
        storage.live_bytes + arena_accounted_live_bytes(&answers->captures));
#else
    (void)answers;
#endif
}

static void eval_owned_answers_close(EvalOwnedAnswers *answers) {
    cetta_owned_execution_cancel(&answers->execution);
    cetta_prepared_pure_program_free(answers->program);
    if (answers->captures_ready)
        arena_free(&answers->captures);
    memset(answers, 0, sizeof(*answers));
}

static bool eval_owned_answers_open(EvalOwnedAnswers *answers,
    Space *space, Atom *producer, const Bindings *environment, int fuel,
    EvalOwnedShape shape) {
    memset(answers, 0, sizeof(*answers));
    CettaLanguageId language = eval_current_language_id();
    if (!space || !producer || fuel >= 0 ||
        (language != CETTA_LANGUAGE_HE && language != CETTA_LANGUAGE_PETTA) ||
        g_eval_payload_transactional || gslt_match_chain_trace_active() ||
        g_active_search_controller_requested || g_active_search_controller_auto ||
        petta_eval_machine_diagnostic_transition_limit() != 0u ||
        active_search_table_mode() != CETTA_TABLE_MODE_NONE ||
        hyperpose_atom_has_thread_local_resource(producer) ||
        atom_has_identity_grounded(producer))
        return false;
    arena_init_detached(&answers->captures);
    answers->captures_ready = true;
    arena_set_runtime_kind(&answers->captures, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    Atom *closed = environment
        ? bindings_apply_if_vars(environment, &answers->captures, producer)
        : producer;
    closed = closed ? atom_deep_copy(&answers->captures, closed) : NULL;
    if (!closed || atom_has_vars(closed) || closed->kind != ATOM_EXPR ||
        closed->expr.len == 0u || atom_has_identity_grounded(closed) ||
        hyperpose_atom_has_thread_local_resource(closed))
        goto declined;
    if (!eval_owned_producer_admitted(space, closed, shape))
        goto declined;
    /* Only already-completed entry arguments cross this boundary. A source
     * expression requiring interpretation keeps the dialect's original path. */
    for (CettaExprIndex index = 1u; index < closed->expr.len; index++) {
        if (!atom_is_constructor_normal_form(space, &answers->captures,
                                             closed->expr.elems[index], fuel))
            goto declined;
    }
    answers->space = space;
    answers->adapter = eval_owned_demand_adapter();
    EvalOwnedDemandAdapter *adapter = &answers->adapter;
    CettaPreparedPureSourceView source_view =
        prepared_pure_source_view_for_language(language, closed);
    answers->program = cetta_prepared_pure_program_compile_closed_answers(
        space, closed, CETTA_GSLT_PURE_CALL_EAGER,
        adapter->boolean_value, adapter->construct_value,
        adapter->constructs_expression, adapter->opaque_value,
        adapter->register_view, adapter->expression_view, adapter->pattern_view,
        &source_view, true,
        active_composition_uses_total_structural_eq(),
        prepared_pure_match_decision_semantics(CETTA_GSLT_PURE_CALL_EAGER));
    if (!answers->program)
        goto declined;
    size_t scratch_budget = prepared_pure_nursery_budget_bytes();
    if (scratch_budget == 0u)
        scratch_budget = 256u * ARENA_BLOCK_SIZE;
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = {
            .max_transitions = UINT64_MAX,
            .max_scratch_bytes = scratch_budget,
            .construct_allocation_bound = language == CETTA_LANGUAGE_PETTA
                ? petta_semantics_construct_value_allocation_bound
                : atom_expr_allocation_bound,
        },
        .unmatched_call = language == CETTA_LANGUAGE_HE
            ? CETTA_PREPARED_PURE_UNMATCHED_REDUCES_TO_ITSELF
            : CETTA_PREPARED_PURE_UNMATCHED_FAILS,
        .interrupt_poll = eval_prepared_pure_interrupt_poll,
        .interrupt_poll_interval = 64u,
        .allow_continuations = true,
    };
    CettaPreparedPureAnswerCursor *cursor =
        cetta_prepared_pure_answer_cursor_open(answers->program, &options);
    if (!cursor)
        goto declined;
    answers->cursor = cursor;
    answers->library = g_library_context;
    if (answers->library) {
        answers->active_mask = answers->library->active_mask;
        answers->admission_revision = cetta_library_petta_admission_revision(answers->library);
        answers->foreign = cetta_lib_prolog_read_token(answers->library->lib_prolog);
        answers->foreign_capability_revision = answers->library->lib_prolog
            ? petta_libpl_capability_revision(answers->library->lib_prolog) : 0u;
    }
    cetta_owned_execution_init(&answers->execution, answers,
                              eval_owned_answers_next, eval_owned_answers_release);
    if (!cetta_prepared_pure_answer_cursor_detach(cursor))
        goto declined;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_ADMISSION);
    return true;
declined:
    eval_owned_answers_close(answers);
    return false;
}

typedef struct {
    EvalOwnedAnswers *answers;
    Arena *arena;
    Atom *resumption;
    Atom *hole;
    CettaOwnedReturnConsumer consume;
    void *consumer;
    bool ok;
} EvalOwnedResidual;

static Atom *eval_owned_residual_import(void *context, Atom *value) {
    return atom_deep_copy_session_copy(context, value);
}

/* The ordinary evaluator owns a declined equation body. Candidate matching
 * uses its existing single-occurrence primitive, including fresh variables;
 * the decoded continuation owns the already-selected caller values. */
static bool eval_owned_residual_body(Atom *body, const Bindings *bindings,
                                    void *context) {
    EvalOwnedResidual *residual = context;
    OutcomeSet values;
    outcome_set_init(&values);
    eval_for_caller(residual->answers->space, residual->arena,
                    NULL, body, -1, bindings, false, &values);
    for (CettaCount index = 0u; residual->ok && index < values.len; index++) {
        Outcome *outcome = &values.items[index];
        Atom *value = outcome_atom_materialize(residual->arena, outcome);
        if (!value || atom_is_legacy_empty_sentinel(value))
            continue;
        if (residual->resumption) {
            BindingsBuilder selected;
            if (!bindings_builder_init(&selected, &outcome->env)) {
                residual->ok = false;
                break;
            }
            residual->ok = bindings_builder_add_var_fresh(&selected,
                residual->hole, value);
            if (residual->ok) {
                EvalOwnedResidual caller = *residual;
                caller.resumption = NULL;
                caller.hole = NULL;
                (void)eval_owned_residual_body(residual->resumption,
                    bindings_builder_bindings(&selected), &caller);
                residual->ok = caller.ok;
            }
            bindings_builder_free(&selected);
        } else {
            CettaOwnedReturn returned = {
                .outcome = outcome->delayed
                    ? cetta_call_suspended(value, outcome->delayed)
                    : cetta_call_completed_value(value),
                .environment = &outcome->env,
            };
            residual->ok = residual->consume(residual->consumer, &returned);
        }
    }
    outcome_set_free(&values);
    return residual->ok && !eval_unwinding();
}

/* Transfer just the undelivered frontier, innermost first. Accepted returns
 * never re-enter the producer. Stale authority and cancellation are terminal
 * observations, never ordinary exhaustion or permission to read retained code. */
static bool eval_owned_answers_finish_residual(EvalOwnedAnswers *answers,
    CettaOwnedReturnConsumer consume, void *consumer) {
    CettaOwnedExecution *execution = &answers->execution;
    if (execution->stop == CETTA_OWNED_EXECUTION_COMPLETE)
        return true;
    if (execution->stop != CETTA_OWNED_EXECUTION_HANDOFF ||
        execution->completion == CETTA_EVAL_INCOMPLETE_INVALIDATED ||
        !eval_owned_answers_authority_current(answers)) {
        if (execution->completion != CETTA_EVAL_COMPLETE)
            eval_mark_incomplete(execution->completion);
        return false;
    }
    CettaPreparedPureAnswerCursor *cursor = answers->cursor;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_RESIDUAL);
    size_t count = cetta_prepared_pure_answer_cursor_frame_count(cursor);
    for (size_t index = count; index-- > 0u;) {
        CettaPreparedPureAnswerFrame frame;
        if (!cetta_prepared_pure_answer_cursor_frame(cursor, index, &frame))
            return false;
        if (frame.next_ordinal >= frame.equation_count)
            continue;
        Arena arena;
        arena_init_detached(&arena);
        arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
        AtomDeepCopySession *copy = atom_deep_copy_session_new(&arena);
        Atom *query = atom_expr_builder_begin(&arena, frame.arity + 1u);
        bool ok = copy && query;
        if (ok)
            query->expr.elems[0] = atom_symbol_id(&arena, frame.head);
        for (uint32_t argument = 0u; ok && argument < frame.arity; argument++) {
            query->expr.elems[argument + 1u] =
                atom_deep_copy_session_copy(copy, frame.arguments[argument]);
            ok = query->expr.elems[argument + 1u] != NULL;
        }
        query = ok ? atom_expr_builder_finish(&arena, query) : NULL;
        EvalOwnedResidual residual = {
            .answers = answers, .arena = &arena, .consume = consume,
            .consumer = consumer, .ok = query != NULL,
        };
        if (residual.ok && frame.resumes_continuation) {
            residual.hole = atom_var_with_id(&arena, "__owned_return", fresh_var_id());
            residual.ok = residual.hole &&
                cetta_prepared_pure_answer_cursor_frame_resumption(
                    cursor, index, &arena, residual.hole,
                    eval_owned_residual_import, copy, &residual.resumption);
        }
        atom_deep_copy_session_free(copy);
        for (uint32_t ordinal = frame.next_ordinal;
             residual.ok && ordinal < frame.equation_count; ordinal++) {
            Atom *equation = cetta_prepared_pure_answer_cursor_frame_equation(
                cursor, index, ordinal);
            if (!equation) {
                residual.ok = false;
                eval_mark_incomplete(CETTA_EVAL_INCOMPLETE_INVALIDATED);
                break;
            }
            (void)query_equation_visit(equation, query, &arena,
                eval_owned_residual_body, &residual);
            if (eval_unwinding()) residual.ok = false;
        }
        ok = residual.ok;
        arena_free(&arena);
        if (!ok)
            return false;
    }
    execution->stop = CETTA_OWNED_EXECUTION_COMPLETE;
    execution->completion = CETTA_EVAL_COMPLETE;
    return true;
}

typedef struct {
    PettaMachineBorrowedItemConsumer consume;
    void *context;
    EvalOwnedAnswers *answers;
} EvalOwnedItemConsumer;

static bool eval_owned_consume_item(void *context, const CettaOwnedReturn *returned) {
    EvalOwnedItemConsumer *consumer = context;
    eval_owned_answers_note_storage(consumer->answers);
    bool accepted = returned->outcome.kind == CETTA_CALL_VALUE &&
           returned->outcome.result_form == CETTA_CALL_RESULT_COMPLETED_VALUE &&
           !returned->outcome.delayed &&
           consumer->consume(consumer->context, returned->outcome.term);
    if (accepted)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_RETURN);
    return accepted;
}

static bool eval_owned_count_item(void *context, Atom *value) {
    uint64_t *count = context;
    (void)value;
    if (*count >= INT64_MAX)
        return false;
    (*count)++;
    return true;
}

static PreparedFoldResult eval_owned_collapsed_consume(
    Space *space, Atom *collapsed, const Bindings *environment, int fuel,
    PettaMachineBorrowedItemConsumer consume, void *context) {
    if (!collapsed || !expr_head_is_id(collapsed, g_builtin_syms.collapse) ||
        expr_nargs(collapsed) != 1u)
        return PREPARED_FOLD_NOT_APPLICABLE;
    EvalOwnedAnswers answers;
    if (!eval_owned_answers_open(&answers, space, expr_arg(collapsed, 0u),
                                 environment, fuel, EVAL_OWNED_DATA))
        return PREPARED_FOLD_NOT_APPLICABLE;
    EvalOwnedItemConsumer consumer = {consume, context, &answers};
    CettaOwnedExecutionStep step = cetta_owned_execution_sequence(
        &answers.execution, UINT64_MAX, eval_owned_consume_item, &consumer);
    bool complete = step == CETTA_OWNED_EXECUTION_COMPLETE ||
        eval_owned_answers_finish_residual(&answers, eval_owned_consume_item, &consumer);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaPreparedPureCursorStorage storage =
        cetta_prepared_pure_answer_cursor_storage(answers.cursor);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_COPY_BYTES,
        storage.copied_bytes);
#endif
    if (complete)
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_COMMIT);
    eval_owned_answers_close(&answers);
    return complete ? PREPARED_FOLD_VALUE : PREPARED_FOLD_INTERRUPTED;
}

typedef struct {
    EvalOwnedAnswers *answers;
    CettaPreparedPureProgram *step;
    Arena carry[2];
    Arena nursery;
    ArenaMark carry_origin[2];
    ArenaMark nursery_origin;
    Atom *accumulator;
    unsigned active;
} EvalOwnedFold;

static bool eval_owned_fold_return(void *context, const CettaOwnedReturn *returned) {
    EvalOwnedFold *fold = context;
    eval_owned_answers_note_storage(fold->answers);
    if (returned->outcome.kind != CETTA_CALL_VALUE ||
        returned->outcome.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE ||
        returned->outcome.delayed)
        return false;
    arena_reset(&fold->nursery, fold->nursery_origin);
    Atom *candidate = NULL;
    if (!cetta_prepared_pure_program_is_current(fold->step) ||
        !cetta_prepared_pure_program_execute_controlled(fold->step, &fold->nursery,
            fold->accumulator, returned->outcome.term,
            eval_prepared_pure_interrupt_poll, NULL, &candidate) ||
        !candidate || atom_is_error(candidate))
        return false;
    unsigned destination = fold->active ^ 1u;
    arena_reset(&fold->carry[destination], fold->carry_origin[destination]);
    Atom *owned = atom_deep_copy(&fold->carry[destination], candidate);
    if (!owned)
        return false;
    arena_reset(&fold->carry[fold->active], fold->carry_origin[fold->active]);
    fold->accumulator = owned;
    fold->active = destination;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_FOLD_STEP);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_RETURN);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaPreparedPureCursorStorage storage = cetta_prepared_pure_answer_cursor_storage(
        fold->answers->cursor);
    cetta_runtime_stats_update_max(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_STORAGE_PEAK,
        storage.live_bytes + arena_accounted_live_bytes(&fold->answers->captures) +
        arena_accounted_live_bytes(&fold->carry[0]) +
        arena_accounted_live_bytes(&fold->carry[1]) +
        arena_accounted_live_bytes(&fold->nursery));
#endif
    return true;
}

typedef struct {
    Arena *arena;
    Atom **items;
    size_t len, cap;
} EvalOwnedTail;

static bool eval_owned_tail_return(void *context, const CettaOwnedReturn *returned) {
    EvalOwnedTail *tail = context;
    if (returned->outcome.kind != CETTA_CALL_VALUE || returned->outcome.delayed ||
        returned->outcome.result_form != CETTA_CALL_RESULT_COMPLETED_VALUE)
        return false;
    if (tail->len == tail->cap) {
        size_t cap = tail->cap ? tail->cap * 2u : 16u;
        if (cap < tail->cap || cap > SIZE_MAX / sizeof(*tail->items))
            return false;
        tail->items = cetta_realloc(tail->items, cap * sizeof(*tail->items));
        tail->cap = cap;
    }
    Atom *value = atom_deep_copy(tail->arena, returned->outcome.term);
    if (!value)
        return false;
    tail->items[tail->len++] = value;
    return true;
}

/* Direct sequencing of a numeric producer and a single-result lexical fold.
 * A declined step retains its pending item and prior accumulator. Only the
 * undelivered suffix is materialized for the canonical fold; the original
 * producer and accepted prefix are never restarted. */
static bool eval_owned_collapsed_fold(Space *space, Arena *arena, Atom *collapsed,
    Atom *initial, Atom *accumulator_binder, Atom *item_binder, Atom *body,
    const Bindings *environment, int fuel, CettaCallOutcome *result) {
    if (!collapsed || !expr_head_is_id(collapsed, g_builtin_syms.collapse) ||
        expr_nargs(collapsed) != 1u || !initial || !body ||
        !accumulator_binder || accumulator_binder->kind != ATOM_VAR ||
        !item_binder || item_binder->kind != ATOM_VAR ||
        accumulator_binder->var_id == item_binder->var_id || !result)
        return false;
    Atom *closed_initial = environment
        ? bindings_apply_if_vars(environment, arena, initial) : initial;
    Atom *closed_body = environment
        ? bindings_apply_if_vars(environment, arena, body) : body;
    VarId variables[2] = {accumulator_binder->var_id, item_binder->var_id};
    EvalOwnedAdmission admission = {.space = space, .read = space_read_token(space)};
    bool safe = eval_owned_is_number(closed_initial) &&
        eval_owned_shape(&admission, closed_body, variables, 2u,
                          EVAL_OWNED_NUMBER, 0u) &&
        space_read_token_is_current(admission.read);
    free(admission.heads);
    if (!safe)
        return false;
    EvalOwnedAnswers answers;
    if (!eval_owned_answers_open(&answers, space, expr_arg(collapsed, 0u),
                                  environment, fuel, EVAL_OWNED_NUMBER))
        return false;
    closed_body = atom_deep_copy(&answers.captures, closed_body);
    EvalOwnedDemandAdapter *adapter = &answers.adapter;
    CettaGsltPureCallMode mode = adapter->language == CETTA_LANGUAGE_PETTA
        ? CETTA_GSLT_PURE_CALL_EAGER : CETTA_GSLT_PURE_CALL_CALL_BY_NEED;
    CettaPreparedPureSourceView source_view =
        prepared_pure_source_view_for_language(adapter->language, body);
    CettaPreparedPureProgram *step = cetta_prepared_pure_program_compile(
        space, closed_body, variables[0], variables[1], mode,
        adapter->boolean_value, adapter->construct_value,
        adapter->constructs_expression, adapter->opaque_value,
        adapter->register_view, adapter->expression_view, adapter->pattern_view,
        &source_view,
        active_composition_uses_total_structural_eq(),
        prepared_pure_match_decision_semantics(mode));
    if (!step) {
        eval_owned_answers_close(&answers);
        return false;
    }
    EvalOwnedFold fold = {.answers = &answers, .step = step};
    for (unsigned index = 0u; index < 2u; index++) {
        arena_init_detached(&fold.carry[index]);
        arena_set_runtime_kind(&fold.carry[index], CETTA_ARENA_RUNTIME_KIND_SURVIVOR);
        fold.carry_origin[index] = arena_mark(&fold.carry[index]);
    }
    arena_init_detached(&fold.nursery);
    arena_set_runtime_kind(&fold.nursery, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    fold.nursery_origin = arena_mark(&fold.nursery);
    fold.accumulator = atom_deep_copy(&fold.carry[0], closed_initial);
    CettaOwnedExecutionStep stop = fold.accumulator
        ? cetta_owned_execution_sequence(&answers.execution, UINT64_MAX,
                                         eval_owned_fold_return, &fold)
        : CETTA_OWNED_EXECUTION_INTERRUPTED;
    bool complete = stop == CETTA_OWNED_EXECUTION_COMPLETE;
    *result = cetta_call_interrupted();
    if (complete) {
        *result = cetta_call_completed_value(atom_deep_copy(arena, fold.accumulator));
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_COMMIT);
    } else if (!eval_unwinding() && !eval_cancel_check() &&
               (stop == CETTA_OWNED_EXECUTION_PAUSED ||
                stop == CETTA_OWNED_EXECUTION_HANDOFF)) {
        EvalOwnedTail tail = {.arena = arena};
        if (stop == CETTA_OWNED_EXECUTION_PAUSED)
            stop = cetta_owned_execution_sequence(&answers.execution, UINT64_MAX,
                                                   eval_owned_tail_return, &tail);
        bool finished = stop == CETTA_OWNED_EXECUTION_COMPLETE ||
            eval_owned_answers_finish_residual(&answers, eval_owned_tail_return, &tail);
        if (finished) {
            Atom *items = atom_expr(arena, tail.items, tail.len);
            Atom *accumulator = atom_deep_copy(arena, fold.accumulator);
            Atom *elements[6] = {atom_symbol_id(arena, g_builtin_syms.foldl_atom),
                items, accumulator, accumulator_binder, item_binder,
                atom_deep_copy(arena, closed_body)};
            *result = cetta_call_value(atom_expr(arena, elements, 6u));
        }
        free(tail.items);
    }
    if (result->kind == CETTA_CALL_INTERRUPTED && !eval_unwinding())
        eval_mark_incomplete(answers.execution.completion != CETTA_EVAL_COMPLETE
            ? answers.execution.completion : CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
#if CETTA_BUILD_WITH_RUNTIME_STATS
    CettaPreparedPureCursorStorage storage =
        cetta_prepared_pure_answer_cursor_storage(answers.cursor);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_OWNED_EXECUTION_COPY_BYTES,
                            storage.copied_bytes);
#endif
    cetta_prepared_pure_program_free(step);
    arena_free(&fold.nursery);
    arena_free(&fold.carry[1]);
    arena_free(&fold.carry[0]);
    eval_owned_answers_close(&answers);
    return true;
}
