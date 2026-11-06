/* Ready invocations enter once, then lend completed values. */
typedef struct {
    CettaCallReturnCursor result;
    Arena arena;
} PythonCallCursor;

static void python_call_cursor_release(void *pointer) {
    PythonCallCursor *cursor = pointer;
    arena_free(&cursor->arena);
    free(cursor);
}
static CettaOwnedExecutionStep python_call_cursor_next(void *pointer,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    PythonCallCursor *cursor = pointer;
    return cetta_call_return_next(&cursor->result, returned, completion);
}
bool cetta_foreign_object_provider_callable(Space *space, const Atom *atom) {
    CettaLibraryContext *context = eval_current_library_context();
    if (atom && atom->kind == ATOM_SYMBOL && symbol_bytes(g_symbols, atom->sym_id)[0] == '&') {
        Registry *registry = context && context->foreign_runtime ? context->foreign_runtime->object_registry : NULL;
        Atom *bound = registry ? registry_lookup_ref(registry, (Atom *)atom) : NULL;
        if (bound) atom = bound;
    }
    CettaForeignValue *value = foreign_python_record(atom);
    if (value) return value->object_mode && value->callable;
    if (!atom || atom->kind != ATOM_SYMBOL) return false;
    return context && python_object_head_at(context->foreign_runtime, space, symbol_bytes(g_symbols, atom->sym_id));
}
bool cetta_foreign_object_binding_candidate(Space *space, const Atom *atom) {
    if (cetta_foreign_object_borrow(atom)) return true;
    return atom && atom->kind == ATOM_EXPR && atom->expr.len &&
        cetta_foreign_object_provider_callable(space, atom->expr.elems[0]);
}
bool cetta_foreign_object_provider_open(Space *space, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    if (!invocation || !cetta_foreign_object_provider_callable(space, invocation->head.skeleton)) return false;
    *execution = (CettaOwnedExecution){0};
    *completion = CETTA_EVAL_COMPLETE;
    CettaLibraryContext *context = eval_current_library_context();
    CettaExprLen count;
    if (!context || !cetta_invocation_arity(invocation, &count) || count > UINT32_MAX) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE; return true;
    }
    PythonCallCursor *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) { *completion = CETTA_EVAL_INCOMPLETE_CAPACITY; return true; }
    arena_init_detached(&cursor->arena);
    Atom **arguments = cetta_invocation_materialize_arguments(&cursor->arena, invocation);
    Atom *head = binding_value_materialize(&cursor->arena, invocation->head);
    if (head) head = eval_materialize_runtime_value(space, &cursor->arena, head);
    if (!head || (count && !arguments)) {
        python_call_cursor_release(cursor);
        *completion = CETTA_EVAL_INCOMPLETE_CAPACITY; return true;
    }
    bool copied = false;
    for (CettaExprIndex i = 0; i < count; i++) {
        Atom *value = eval_materialize_runtime_value(space, &cursor->arena, arguments[i]);
        if (value == arguments[i]) continue;
        if (!copied) {
            Atom **ready = arena_alloc(&cursor->arena, (size_t)count * sizeof(*ready));
            if (!ready) {
                python_call_cursor_release(cursor);
                *completion = CETTA_EVAL_INCOMPLETE_CAPACITY; return true;
            }
            memcpy(ready, arguments, (size_t)count * sizeof(*ready));
            arguments = ready;
            copied = true;
        }
        arguments[i] = value;
    }
    Space *previous = g_python_callback_space;
    g_python_callback_space = space;
    CettaCallOutcome outcome;
    if (head->kind == ATOM_SYMBOL)
        python_objects_call(context->foreign_runtime, space, &cursor->arena, head, arguments, count, &outcome);
    else {
        ResultSet one; result_set_init(&one);
        Atom *error = NULL;
        bool ok = cetta_foreign_call(context->foreign_runtime, space, &cursor->arena,
            head, arguments, count, &one, &error);
        outcome = ok && one.len == 1u ? cetta_call_completed_value(one.items[0])
            : cetta_call_raised(error ? error : foreign_error_atom(&cursor->arena, "Python call failed"));
        result_set_free(&one);
    }
    g_python_callback_space = previous;
    if (outcome.kind == CETTA_CALL_VALUE && outcome.result_form == CETTA_CALL_RESULT_EXPRESSION &&
        outcome.term && outcome.term->kind == ATOM_EXPR && outcome.term->expr.len == 1u &&
        cetta_foreign_provider_callable(outcome.term->expr.elems[0])) {
        CettaInvocation delegated = {.head = binding_value_from_atom(outcome.term->expr.elems[0])};
        cetta_foreign_provider_open(space, &delegated, execution, completion);
        python_call_cursor_release(cursor);
    } else {
        cursor->result.outcome = outcome;
        cetta_owned_execution_init(execution, cursor, python_call_cursor_next, python_call_cursor_release);
    }
    return true;
}
