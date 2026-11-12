/* Exercise the private traversal and its active Need snapshot directly,
 * including sharing and invalid cycles that parsed programs cannot construct.
 * This translation unit replaces eval.o in the test executable. */
#include "../src/eval.c"
#include <assert.h>
#include <errno.h>

static bool observation_alloc_refused;
static bool completion_native_probe, completion_preparation_refused;
static unsigned completion_native_calls, completion_native_attempts;
static CettaEvalCompletion completion_native_cause;
static CettaEvalCompletion completion_preparation_cause = CETTA_EVAL_INCOMPLETE_CAPACITY;
static unsigned completion_native_packet;

bool __real_cetta_library_call_native_prepared_view(CettaLibraryContext *, Space *,
    Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
bool __wrap_cetta_library_call_native_prepared_view(CettaLibraryContext *library, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t arity, CettaCallOutcome *outcome) {
    if (!completion_native_probe || !atom_is_symbol(head, "__cetta_lib_json_parse_v1"))
        return __real_cetta_library_call_native_prepared_view(
            library, space, arena, head, arguments, arity, outcome);
    completion_native_attempts++;
    if (completion_preparation_refused) {
        eval_mark_incomplete(completion_preparation_cause);
        return false;
    }
    completion_native_calls++;
    if (completion_native_cause != CETTA_EVAL_COMPLETE) {
        eval_native_interrupt(completion_native_cause);
    }
    if (completion_native_packet == 2u) {
        *outcome = cetta_call_completed_value(atom_int(arena, 9));
    } else if (completion_native_packet == 3u) {
        *outcome = cetta_call_failure();
    } else if (completion_native_packet == 4u) {
        *outcome = cetta_call_completed_frontier(atom_expr3(arena,
            atom_int(arena, 7), atom_int(arena, 7), atom_int(arena, 9)));
    } else if (completion_native_cause != CETTA_EVAL_COMPLETE) {
        *outcome = cetta_call_interrupted();
    } else {
        Atom *expression = atom_expr3(arena, atom_symbol(arena, "+"),
            atom_int(arena, 4), atom_int(arena, 5));
        *outcome = completion_native_packet == 1u ? cetta_call_value(expression)
            : cetta_call_expression_frontier(atom_expr(arena, &expression, 1u));
    }
    return true;
}

bool __real_cetta_library_call_native(CettaLibraryContext *, Space *,
    Arena *, Atom *, Atom **, uint32_t, CettaCallOutcome *);
bool __wrap_cetta_library_call_native(CettaLibraryContext *library, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t arity, CettaCallOutcome *outcome) {
    if (completion_native_probe && atom_is_symbol(head, "__cetta_lib_json_parse_v1"))
        return __wrap_cetta_library_call_native_prepared_view(
            library, space, arena, head, arguments, arity, outcome);
    return __real_cetta_library_call_native(library, space, arena, head, arguments, arity, outcome);
}

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size) {
    if (observation_alloc_refused) {
        errno = ENOMEM;
        return NULL;
    }
    return __real_calloc(count, size);
}

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

/* Authored-term observation ignores held-data carriers at every position.
 * Projecting that data must not open or force an embedded Need capability. */
static void held_observation(Arena *arena) {
    Atom *box = atom_symbol(arena, "Box");
    Atom *three = atom_int(arena, 3);
    Atom *left = atom_expr2(arena, box, atom_prime_held_wrap(arena, three));
    Atom *right = atom_expr2(arena, box, three);
    Atom *args[] = {left, right};
    Atom *eq = atom_symbol_id(arena, g_builtin_syms.op_eq);
    assert(is_true_atom(grounded_dispatch(arena, eq, args, 2u)));
    args[1] = atom_expr2(arena, box, atom_int(arena, 4));
    assert(is_false_atom(grounded_dispatch(arena, eq, args, 2u)));
    for (unsigned i = 0u; i < 512u; i++) {
        left = atom_expr2(arena, box, atom_prime_held_wrap(arena, left));
        right = atom_expr2(arena, box, right);
    }
    args[0] = left;
    args[1] = right;
    assert(is_true_atom(grounded_dispatch(arena, eq, args, 2u)));

    Atom *origin = atom_expr2(arena, box, atom_var(arena, "opaque-observation"));
    Atom *ref = suspend(arena, origin);
    uint64_t id = 0u;
    PrimeNeedCellView before, after;
    assert(prime_need_ref_is_active(ref, &id) &&
           prime_need_snapshot_lookup(&g_prime_need_active, id, &before));
    args[0] = atom_expr2(arena, box, atom_prime_held_wrap(arena, ref));
    args[1] = atom_expr2(arena, box, ref);
    assert(is_true_atom(grounded_dispatch(arena, eq, args, 2u)));
    assert(prime_need_snapshot_lookup(&g_prime_need_active, id, &after) &&
           after.cache_state == before.cache_state &&
           after.update_id == before.update_id && after.origin == origin);
}

static void observation_refusal(Arena *arena) {
    Atom *three = atom_int(arena, 3);
    Atom *box = atom_symbol(arena, "Box");
    Atom *held = atom_prime_held_wrap(arena, three);
    Atom *left = atom_expr2(arena, box, held);
    Atom *right = atom_expr2(arena, box, three);
    Atom *chosen = atom_prime_held_wrap(arena, atom_symbol(arena, "chosen"));
    Atom *other = atom_prime_held_wrap(arena, atom_symbol(arena, "other"));
    Atom *args[] = {left, right, chosen, other};
    Atom *heads[] = {atom_symbol_id(arena, g_builtin_syms.op_eq),
                    atom_symbol_id(arena, g_builtin_syms.alpha_eq),
                    atom_symbol_id(arena, g_builtin_syms.if_equal)};
    CettaEvalEpisode *previous = g_eval_completion_tracker;
    for (unsigned op = 0u; op < 3u; op++) {
        uint32_t arity = op == 2u ? 4u : 2u;
        for (unsigned cycle = 0u; cycle < 2u; cycle++) {
            Atom *cyclic = atom_expr3(arena, box, held, three);
            if (cycle) cyclic->expr.elems[2] = cyclic;
            args[0] = cycle ? cyclic : left;
            CettaEvalEpisode episode = cetta_eval_episode_begin(NULL, -1);
            g_eval_completion_tracker = &episode;
            CettaCallOutcome result = {0};
            observation_alloc_refused = !cycle;
            bool handled = grounded_call(arena, heads[op], args, arity, &result);
            observation_alloc_refused = false;
            assert(handled && result.kind == CETTA_CALL_INTERRUPTED && !result.term &&
                   episode.interrupted &&
                   episode.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            eval_native_interrupt(CETTA_EVAL_INCOMPLETE_CANCELLED);
            assert(eval_native_interrupted() &&
                   episode.completion == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            assert(left->expr.elems[1] == held && atom_prime_held_is(held) &&
                   cyclic->expr.elems[2] == (cycle ? cyclic : three));

            CettaEvalCompletion delivered = episode.completion;
            CettaCallOutcome end = {0};
            OutcomeSet outputs;
            outcome_set_init(&outputs);
            petta_eval_machine_deliver_native_return(NULL, NULL, arena, result,
                NULL, &outputs, &end, &delivered);
            assert(delivered == CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
                   end.kind == CETTA_CALL_INTERRUPTED && !outputs.len);
            outcome_set_free(&outputs);

            CettaInvocation invocation = {.head = binding_value_from_atom(heads[op]),
                .supplied = {.atoms = args, .count = arity}};
            CettaOwnedReturn failed_return = {.outcome = result};
            CettaOwnedExecution owner = {0};
            bool completes_callers = false;
            assert(cetta_eval_episode_end(&episode) == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
            g_eval_completion_tracker = previous;

            /* The retained owner carries the cause after the stack episode
             * is gone; its terminal cursor must not replace it by cancel. */
            CettaEvalEpisode unrelated = cetta_eval_episode_begin(NULL, -1);
            unrelated.completion = CETTA_EVAL_INCOMPLETE_FUEL;
            g_eval_completion_tracker = &unrelated;
            observation_alloc_refused = true;
            assert(!prepared_native_completion_open(NULL, NULL, &invocation, true,
                &failed_return, episode.completion, &owner, &completes_callers));
            observation_alloc_refused = false;
            assert(!owner.computation && failed_return.outcome.kind == CETTA_CALL_INTERRUPTED);
            assert(prepared_native_completion_open(NULL, NULL, &invocation, true,
                &failed_return, episode.completion, &owner, &completes_callers) && !completes_callers);
            CettaOwnedReturn returned;
            CettaEvalCompletion retained = CETTA_EVAL_COMPLETE;
            assert(owner.next(owner.computation, &returned, &retained) ==
                       CETTA_OWNED_EXECUTION_INTERRUPTED &&
                   retained == CETTA_EVAL_INCOMPLETE_HOST_FAILURE &&
                   unrelated.completion == CETTA_EVAL_INCOMPLETE_FUEL);
            g_eval_completion_tracker = previous;
            cetta_owned_execution_cancel(&owner);
            if (cycle) cyclic->expr.elems[2] = three;
        }
        args[0] = left;
        CettaEvalEpisode healthy = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &healthy;
        CettaCallOutcome result = {0};
        assert(grounded_call(arena, heads[op], args, arity, &result) &&
               result.kind == CETTA_CALL_VALUE &&
               (op == 2u ? result.term == chosen : is_true_atom(result.term)) &&
               healthy.completion == CETTA_EVAL_COMPLETE && !healthy.interrupted);
        assert(!atom_eq(left, right));
        assert(cetta_eval_episode_end(&healthy) == CETTA_EVAL_COMPLETE);
        g_eval_completion_tracker = previous;
    }
    CettaEvalEpisode cancelled = cetta_eval_episode_begin(NULL, -1);
    g_eval_completion_tracker = &cancelled;
    _Atomic bool stop = true;
    _Atomic bool *old_token = eval_set_cancel_token(&stop);
    bool old_observed = g_eval_cancel_observed;
    assert(eval_native_interrupted() && cancelled.interrupted &&
           cancelled.completion == CETTA_EVAL_INCOMPLETE_CANCELLED);
    eval_set_cancel_token(old_token);
    g_eval_cancel_observed = old_observed;
    assert(cetta_eval_episode_end(&cancelled) == CETTA_EVAL_INCOMPLETE_CANCELLED);
    g_eval_completion_tracker = previous;
}

static void library_observation_refusal(Arena *arena) {
    Atom *three = atom_int(arena, 3);
    Atom *held = atom_prime_held_wrap(arena, three);
    Atom *call = atom_expr3(arena, atom_symbol(arena, "Box"), held, three);
    Atom *arguments[] = {call};
    Atom *head = atom_symbol(arena, "__cetta_lib_json_parse_v1");
    CettaEvalEpisode *previous = g_eval_completion_tracker;
    for (unsigned prepared = 0u; prepared < 2u; prepared++)
    for (unsigned cycle = 0u; cycle < 2u; cycle++) {
        call->expr.elems[2] = cycle ? call : three;
        CettaEvalEpisode episode = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &episode;
        CettaCallOutcome returned;
        observation_alloc_refused = !cycle;
        bool handled = prepared
            ? __real_cetta_library_call_native_prepared_view(g_library_context, NULL,
                arena, head, arguments, 1u, &returned)
            : __real_cetta_library_call_native(g_library_context, NULL,
                arena, head, arguments, 1u, &returned);
        observation_alloc_refused = false;
        assert(handled && returned.kind == CETTA_CALL_INTERRUPTED &&
            cetta_eval_episode_end(&episode) == CETTA_EVAL_INCOMPLETE_HOST_FAILURE);
        g_eval_completion_tracker = previous;
    }
    call->expr.elems[2] = three;
}

/* The real source-view adapter captures the invocation's status. Reopening
 * completion storage later cannot borrow a new receiver's unrelated cause. */
static void native_completion_ownership(Arena *arena) {
    Space space;
    space_init(&space);
    size_t position = 0u;
    Atom *equation = parse_sexpr(arena,
        "(= (completion-probe $x) (let $v (__cetta_lib_json_parse_v1 $x) (Box $v $v)))",
        &position);
    assert(equation && space_admit_atom(&space, arena, equation));
    position = 0u;
    Atom *call = parse_sexpr(arena, "(completion-probe 1)", &position);
    CettaPreparedPureSourceView view = prepared_pure_source_view_for_language(
        CETTA_LANGUAGE_PRIME, call);
    CettaPreparedPureProgram *program = cetta_prepared_pure_program_compile_closed_answers(
        &space, call, CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always, NULL, NULL, NULL, NULL,
        &view, true, true, (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()}, NULL);
    assert(program);
    CettaPreparedPureAnswerCursorOptions options = {
        .limits = {.max_transitions = 4096u, .max_scratch_bytes = 1024u * 1024u,
            .construct_allocation_bound = atom_expr_allocation_bound},
        .allow_continuations = true};
    CettaEvalEpisode *previous = g_eval_completion_tracker;
    completion_native_probe = true;
    for (unsigned fault = 0u; fault < 2u; fault++)
    for (unsigned unrelated = 0u; unrelated < 2u; unrelated++) {
        completion_native_cause = fault ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE : CETTA_EVAL_COMPLETE;
        completion_native_calls = completion_native_attempts = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
        assert(cursor);
        if (fault) cetta_prepared_pure_answer_cursor_defer_native_completion(cursor);
        CettaEvalEpisode producer = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &producer;
        CettaOwnedReturn row;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        assert(cetta_prepared_pure_answer_cursor_return(cursor, &row, &completion) == CETTA_OWNED_EXECUTION_HANDOFF);
        assert(completion_native_calls == 1u && completion_native_attempts == 1u);
        CettaInvocation invocation;
        bool entered;
        CettaOwnedReturn packet;
        CettaEvalCompletion cause;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(
            cursor, &invocation, &entered, &packet, &cause) && entered && cause == completion_native_cause);
        assert(cetta_eval_episode_end(&producer) == completion_native_cause);
        CettaEvalEpisode receiver = cetta_eval_episode_begin(NULL, -1);
        if (unrelated) receiver.completion = CETTA_EVAL_INCOMPLETE_FUEL;
        g_eval_completion_tracker = &receiver;
        CettaOwnedExecution replacement = {0};
        bool completes_callers = false;
        observation_alloc_refused = true;
        assert(!prepared_native_completion_open(&space, cursor, &invocation, entered,
            &packet, cause, &replacement, &completes_callers));
        observation_alloc_refused = false;
        assert(!replacement.computation && completion_native_calls == 1u);
        CettaOwnedReturn still_pending;
        CettaEvalCompletion still_cause;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation,
            &entered, &still_pending, &still_cause) && entered && still_cause == cause &&
            still_pending.outcome.term == packet.outcome.term &&
            still_pending.outcome.kind == packet.outcome.kind);
        assert(prepared_native_completion_open(&space, cursor, &invocation, entered,
            &packet, cause, &replacement, &completes_callers));
        assert(((PreparedNativeCompletion *)replacement.computation)->values.completion == cause);
        completion = CETTA_EVAL_COMPLETE;
        if (fault) {
            assert(replacement.next(replacement.computation, &row, &completion) == CETTA_OWNED_EXECUTION_INTERRUPTED &&
                completion == cause);
            CettaOwnedExecution raw = {0};
            assert(cetta_prepared_pure_answer_cursor_take_native(cursor, &raw));
            const CettaOwnedReturn *loan;
            assert(cetta_owned_execution_loan(&raw, &loan) == CETTA_OWNED_EXECUTION_INTERRUPTED &&
                raw.completion == cause && !loan);
            cetta_owned_execution_cancel(&raw);
        } else {
            assert(cetta_prepared_pure_answer_cursor_complete_native(cursor, &replacement, completes_callers));
            assert(!replacement.computation &&
                cetta_prepared_pure_answer_cursor_return(cursor, &row, &completion) == CETTA_OWNED_EXECUTION_RETURN &&
                completion == CETTA_EVAL_COMPLETE && row.outcome.kind == CETTA_CALL_VALUE);
            position = 0u;
            assert(atom_eq(row.outcome.term, parse_sexpr(arena, "(Box 9 9)", &position)));
        }
        assert(completion_native_calls == 1u && completion_native_attempts == 1u &&
            receiver.completion == (unrelated ? CETTA_EVAL_INCOMPLETE_FUEL : CETTA_EVAL_COMPLETE));
        cetta_owned_execution_cancel(&replacement);
        cetta_prepared_pure_answer_cursor_close(cursor);
        g_eval_completion_tracker = previous;
    }
    /* Scalar library expressions remain inert data. Incomplete packets keep
     * earned values and duplicates, but never claim a completed frontier. */
    for (unsigned kind = 1u; kind <= 4u; kind++) {
        completion_native_packet = kind;
        completion_native_cause = kind == 1u ? CETTA_EVAL_COMPLETE :
            kind == 3u ? CETTA_EVAL_INCOMPLETE_CAPACITY : CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        completion_native_attempts = completion_native_calls = 0u;
        CettaEvalEpisode producer = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &producer;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
        assert(cursor);
        unsigned answers = 0u;
        CettaOwnedReturn row;
        CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
        CettaOwnedExecutionStep step;
        while ((step = cetta_prepared_pure_answer_cursor_return(cursor, &row, &completion)) == CETTA_OWNED_EXECUTION_RETURN) {
            assert(answers < 3u && row.outcome.kind == CETTA_CALL_VALUE);
            char expected[80];
            if (kind == 1u) snprintf(expected, sizeof(expected), "(Box (+ 4 5) (+ 4 5))");
            else snprintf(expected, sizeof(expected), "(Box %u %u)", kind == 4u && answers < 2u ? 7u : 9u,
                kind == 4u && answers < 2u ? 7u : 9u);
            position = 0u;
            assert(atom_eq(row.outcome.term, parse_sexpr(arena, expected, &position)));
            answers++;
        }
        assert(answers == (kind == 3u ? 0u : kind == 4u ? 3u : 1u) &&
            step == (kind == 1u ? CETTA_OWNED_EXECUTION_COMPLETE : CETTA_OWNED_EXECUTION_HANDOFF) &&
            completion == completion_native_cause && completion_native_attempts == 1u && completion_native_calls == 1u);
        assert(cetta_eval_episode_end(&producer) == completion_native_cause);
        g_eval_completion_tracker = previous;
        cetta_prepared_pure_answer_cursor_close(cursor);
    }
    completion_native_packet = 0u;
    CettaEvalCompletion forbidden[] = {CETTA_EVAL_INCOMPLETE_FUEL, CETTA_EVAL_INCOMPLETE_EFFECT_DENIED,
        CETTA_EVAL_INCOMPLETE_CANCELLED, CETTA_EVAL_INCOMPLETE_INVALIDATED};
    for (unsigned i = 0u; i < sizeof(forbidden) / sizeof(*forbidden); i++) {
        completion_preparation_cause = forbidden[i];
        completion_preparation_refused = true;
        completion_native_cause = CETTA_EVAL_COMPLETE;
        completion_native_attempts = completion_native_calls = 0u;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
        CettaOwnedExecution owner = {0};
        assert(cursor && cetta_prepared_pure_answer_cursor_own_execution(&owner, cursor));
        CettaEvalEpisode producer = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &producer;
        const CettaOwnedReturn *loan;
        assert(cetta_owned_execution_loan(&owner, &loan) == CETTA_OWNED_EXECUTION_HANDOFF &&
            !loan && owner.completion == forbidden[i]);
        CettaCallReturnCursor return_cursor = {.outcome = cetta_call_completed_value(atom_int(arena, 9))};
        CettaOwnedExecution replacement;
        cetta_owned_execution_init(&replacement, &return_cursor, cetta_call_return_next, NULL);
        assert(!cetta_prepared_pure_answer_cursor_resume_native(&owner, &replacement) &&
            replacement.computation == &return_cursor && completion_native_attempts == 1u && !completion_native_calls);
        assert(cetta_eval_episode_end(&producer) == forbidden[i]);
        g_eval_completion_tracker = previous;
        cetta_owned_execution_cancel(&replacement);
        cetta_owned_execution_cancel(&owner);
    }
    completion_preparation_cause = CETTA_EVAL_INCOMPLETE_CAPACITY;
    /* A preparation refusal is stopped until an explicit ownership transfer.
     * It cannot be acknowledged by a different cause or stale authority. */
    for (unsigned stale = 0u; stale < 2u; stale++) {
        completion_native_calls = completion_native_attempts = 0u;
        completion_native_cause = CETTA_EVAL_COMPLETE;
        completion_preparation_refused = true;
        CettaPreparedPureAnswerCursor *cursor = cetta_prepared_pure_answer_cursor_open(program, &options);
        assert(cursor);
        CettaOwnedExecution owner = {0};
        assert(cetta_prepared_pure_answer_cursor_own_execution(&owner, cursor));
        CettaEvalEpisode producer = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &producer;
        const CettaOwnedReturn *loan;
        for (unsigned retry = 0u; retry < 3u; retry++)
            assert(cetta_owned_execution_loan(&owner, &loan) == CETTA_OWNED_EXECUTION_HANDOFF &&
                !loan && owner.completion == CETTA_EVAL_INCOMPLETE_CAPACITY &&
                !cetta_prepared_pure_answer_cursor_native_started(cursor) &&
                completion_native_attempts == 1u && !completion_native_calls);
        assert(cetta_eval_episode_end(&producer) == CETTA_EVAL_INCOMPLETE_CAPACITY);
        g_eval_completion_tracker = previous;
        CettaInvocation invocation;
        CettaOwnedReturn pending;
        CettaEvalCompletion cause;
        bool entered, completes_callers;
        assert(cetta_prepared_pure_answer_cursor_native_boundary(cursor, &invocation,
            &entered, &pending, &cause) && !entered && cause == CETTA_EVAL_INCOMPLETE_CAPACITY);
        CettaOwnedExecution replacement = {0};
        observation_alloc_refused = true;
        assert(!prepared_native_completion_open(&space, cursor, &invocation, false,
            NULL, cause, &replacement, &completes_callers) && !replacement.computation);
        observation_alloc_refused = false;
        assert(prepared_native_completion_open(&space, cursor, &invocation, false,
            NULL, cause, &replacement, &completes_callers));
        owner.completion = CETTA_EVAL_INCOMPLETE_FUEL;
        assert(!cetta_prepared_pure_answer_cursor_resume_native(&owner, &replacement) && replacement.computation);
        owner.completion = cause;
        completion_preparation_refused = false;
        if (stale) {
            position = 0u;
            assert(space_admit_atom(&space, arena, parse_sexpr(arena, "(= (completion-new) 1)", &position)));
            assert(!cetta_prepared_pure_answer_cursor_resume_native(&owner, &replacement) && replacement.computation &&
                !completion_native_calls && owner.completion == cause);
        } else {
            CettaEvalEpisode receiver = cetta_eval_episode_begin(NULL, -1);
            receiver.completion = CETTA_EVAL_INCOMPLETE_FUEL;
            g_eval_completion_tracker = &receiver;
            assert(cetta_prepared_pure_answer_cursor_resume_native(&owner, &replacement) &&
                !replacement.computation && owner.completion == CETTA_EVAL_COMPLETE &&
                producer.completion == cause);
            assert(cetta_owned_execution_loan(&owner, &loan) == CETTA_OWNED_EXECUTION_RETURN && loan &&
                owner.completion == CETTA_EVAL_COMPLETE && completion_native_attempts == 2u && completion_native_calls == 1u);
            position = 0u;
            assert(atom_eq(loan->outcome.term, parse_sexpr(arena, "(Box 9 9)", &position)));
            assert(receiver.completion == CETTA_EVAL_INCOMPLETE_FUEL);
            g_eval_completion_tracker = previous;
        }
        cetta_owned_execution_cancel(&replacement);
        cetta_owned_execution_cancel(&owner);
    }
    cetta_prepared_pure_program_free(program);
    /* Terminal native returns carry the same cause without a cursor frame. */
    Atom *x = atom_var(arena, "completion-parameter"), *y = atom_var(arena, "unused-parameter");
    Atom *body = atom_expr2(arena, atom_symbol(arena, "__cetta_lib_json_parse_v1"), x);
    program = cetta_prepared_pure_program_compile(&space, body, x->var_id, y->var_id,
        CETTA_GSLT_PURE_CALL_EAGER, atom_bool, atom_expr,
        cetta_prepared_pure_constructs_expression_always, NULL, NULL, NULL, NULL,
        &view, true, (CettaMatchDecisionSemanticIdentity){
            .compiler_identity = cetta_match_decision_compiler_identity()});
    assert(program && cetta_prepared_pure_program_use_native_tail_returns(program));
    for (unsigned fault = 0u; fault < 3u; fault++) {
        completion_native_calls = completion_native_attempts = 0u;
        completion_native_cause = fault == 1u ? CETTA_EVAL_INCOMPLETE_HOST_FAILURE : CETTA_EVAL_COMPLETE;
        completion_preparation_refused = fault == 2u;
        CettaEvalEpisode producer = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &producer;
        CettaCallOutcome returned;
        CettaEvalCompletion completion;
        Atom *one = atom_int(arena, 1);
        bool handled = cetta_prepared_pure_program_execute_controlled(program, arena,
            one, one, NULL, NULL, &returned, &completion);
        CettaEvalCompletion expected = fault == 2u ? CETTA_EVAL_INCOMPLETE_CAPACITY : completion_native_cause;
        assert(handled && completion == expected &&
            completion_native_attempts == 1u && completion_native_calls == (fault == 2u ? 0u : 1u));
        assert(cetta_eval_episode_end(&producer) == expected);
        g_eval_completion_tracker = previous;
    }
    /* Omitting the optional cause output cannot turn a classified refusal
     * into permission for fallback. The reusable program can then run in a
     * fresh, healthy attempt. */
    completion_preparation_refused = true;
    CettaEvalEpisode refused = cetta_eval_episode_begin(NULL, -1);
    g_eval_completion_tracker = &refused;
    CettaCallOutcome terminal;
    Atom *one = atom_int(arena, 1);
    assert(cetta_prepared_pure_program_execute_controlled(program, arena,
        one, one, NULL, NULL, &terminal, NULL) && terminal.kind == CETTA_CALL_INTERRUPTED);
    assert(cetta_eval_episode_end(&refused) == CETTA_EVAL_INCOMPLETE_CAPACITY);
    completion_preparation_refused = false;
    completion_native_calls = completion_native_attempts = 0u;
    CettaEvalEpisode stopped = cetta_eval_episode_begin(NULL, -1);
    stopped.completion = CETTA_EVAL_INCOMPLETE_CANCELLED;
    stopped.interrupted = true;
    g_eval_completion_tracker = &stopped;
    CettaInvocation invocation = {.head = binding_value_from_atom(body->expr.elems[0]),
        .supplied = {.atoms = &one, .count = 1u}};
    CettaEvalCompletion cause;
    assert(!eval_prepared_pure_native_call(g_library_context, &space, arena,
        &invocation, &terminal, &cause) && cause == CETTA_EVAL_INCOMPLETE_CANCELLED &&
        !completion_native_calls && !completion_native_attempts);
    assert(cetta_eval_episode_end(&stopped) == CETTA_EVAL_INCOMPLETE_CANCELLED);
    CettaEvalEpisode healthy = cetta_eval_episode_begin(NULL, -1);
    g_eval_completion_tracker = &healthy;
    assert(cetta_prepared_pure_program_execute_controlled(program, arena,
        one, one, NULL, NULL, &terminal, &cause) && cause == CETTA_EVAL_COMPLETE &&
        terminal.kind == CETTA_CALL_VALUE && completion_native_calls == 1u && completion_native_attempts == 1u);
    assert(cetta_eval_episode_end(&healthy) == CETTA_EVAL_COMPLETE);
    g_eval_completion_tracker = previous;
    completion_preparation_refused = completion_native_probe = false;
    cetta_prepared_pure_program_free(program);
    space_free(&space);
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

typedef struct {
    Space *space;
    SpaceReadToken read;
    unsigned observations;
    bool refuse;
} TypingReadObservation;

static bool typing_read_observe(void *raw, SpaceReadToken read) {
    TypingReadObservation *observation = raw;
    if (read.space == observation->space) {
        observation->observations++;
        observation->read = read;
        return !observation->refuse;
    }
    return true;
}

static void typing_service_observations(Arena *arena) {
    const char *names[] = {"normalize-type", "check-type-refinements", "is-consistent", "check-type",
        "search-inhabitants", "search-first-inhabitant", "type-forward-step", "type-forward-closure"};
    for (unsigned i = 0u; i < 8u; i++) {
        SymbolId head = symbol_intern_cstr(g_symbols, names[i]);
        CettaHeTypingService service;
        assert(he_typing_is_op(names[i]) && he_typing_is_op_id(head) &&
            he_typing_op_service_id(head, &service));
        assert(service.access == (i >= 6u ? CETTA_HE_TYPING_SPACE_WRITE :
            i == 2u ? CETTA_HE_TYPING_STRUCTURAL : CETTA_HE_TYPING_SPACE_READ));
        assert(service.evaluator_entry == (i != 2u));
        assert(prime_need_typing_read_service(head) == (i < 6u));
    }
    CettaHeTypingService missing = {.access = CETTA_HE_TYPING_SPACE_WRITE};
    assert(!he_typing_op_service_id(symbol_intern_cstr(g_symbols, "typing-uninterpreted"), &missing) &&
        missing.access == CETTA_HE_TYPING_SPACE_WRITE);
    Space space;
    space_init(&space);
    size_t position = 0u;
    assert(space_admit_atom(&space, arena, parse_sexpr(arena, "(: observed-type (u 0))", &position)));
    position = 0u;
    assert(space_admit_atom(&space, arena, parse_sexpr(arena, "(: observed-value observed-type)", &position)));
    Atom *head = atom_symbol(arena, "check-type");
    Atom *arguments[] = {atom_space(arena, &space), atom_symbol(arena, "observed-value"),
        atom_symbol(arena, "observed-type"), atom_int(arena, 10000)};
    CettaEvalEpisode *previous = g_eval_completion_tracker;
    for (unsigned refuse = 0u; refuse < 2u; refuse++) {
        TypingReadObservation observation = {.space = &space, .refuse = refuse};
        CettaEvalReadScope scope = {.observe = typing_read_observe, .context = &observation};
        CettaEvalEpisode episode = cetta_eval_episode_begin(NULL, -1);
        g_eval_completion_tracker = &episode;
        eval_read_scope_enter(&scope);
        Atom *result = he_typing_dispatch(arena, head, arguments, 4u);
        eval_read_scope_leave(&scope);
        assert(result && atom_is_symbol(result->expr.elems[0], refuse ? "he-unknown" : "he-accept") &&
            observation.observations && space_read_token_is_current(observation.read));
        assert(scope.incomplete == (bool)refuse && cetta_eval_episode_end(&episode) ==
            (refuse ? CETTA_EVAL_INCOMPLETE_CAPACITY : CETTA_EVAL_COMPLETE));
        g_eval_completion_tracker = previous;
    }
    space_free(&space);
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
    cetta_library_context_init_for_language_profile(&context, CETTA_LANGUAGE_PRIME,
                                                  cetta_profile_prime_default());
    eval_set_library_context(&context);
    prime_need_snapshot_init(&g_prime_need_active);
    assert(prime_need_snapshot_begin(&g_prime_need_active));
    held_observation(&arena);
    observation_refusal(&arena);
    library_observation_refusal(&arena);
    native_completion_ownership(&arena);
    typing_service_observations(&arena);

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
    cetta_library_context_free(&context);
    arena_free(&arena);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    g_var_intern = NULL;
    puts("PASS: Prime capability opening preserves depth, sharing, opacity and cancellation");
    return 0;
}
