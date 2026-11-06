typedef struct {
    atomic_uint references;
    CettaForeignRuntime *runtime;
    CettaForeignValue *iterator;
    bool opened, closed;
} PythonIterator;

typedef struct {
    PythonIterator *source;
    Arena arena;
} PythonIteratorCursor;

/* Keep the primary exception if close succeeds; chain it if close fails. */
static void python_iterator_close(PythonIterator *iterator) {
    if (iterator->closed) return;
    iterator->closed = true;
    PyObject *object = iterator->iterator->obj;
    if (!object) return;
    PyObject *type = NULL, *value = NULL, *traceback = NULL;
    PyErr_Fetch(&type, &value, &traceback);
    PyErr_NormalizeException(&type, &value, &traceback);
    PyObject *close = PyObject_GetAttrString(object, "close");
    if (!close && PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
    PyObject *result = close ? PyObject_CallNoArgs(close) : NULL;
    Py_XDECREF(result); Py_XDECREF(close);
    if (!PyErr_Occurred()) PyErr_Restore(type, value, traceback);
    else {
        PyObject *new_type = NULL, *new_value = NULL, *new_traceback = NULL;
        PyErr_Fetch(&new_type, &new_value, &new_traceback);
        PyErr_NormalizeException(&new_type, &new_value, &new_traceback);
        if (value && traceback) PyException_SetTraceback(value, traceback);
        if (new_value && value && new_value != value) { PyException_SetContext(new_value, value); value = NULL; }
        Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(traceback);
        PyErr_Restore(new_type, new_value, new_traceback);
    }
}

static void python_iterator_retain(void *pointer) {
    PythonIterator *iterator = pointer;
    atomic_fetch_add_explicit(&iterator->references, 1u, memory_order_relaxed);
}
static void python_iterator_release(void *pointer) {
    PythonIterator *iterator = pointer;
    if (atomic_fetch_sub_explicit(&iterator->references, 1u, memory_order_acq_rel) != 1u) return;
    __attribute__((cleanup(cetta_host_foreign_leave))) CettaHostForeignScope host = {0};
    cetta_host_foreign_enter(&host);
    PyGILState_STATE state = PyGILState_Ensure();
    python_iterator_close(iterator);
    foreign_record_release(iterator->iterator);
    PyGILState_Release(state);
    free(iterator);
}
static void python_iterator_cursor_release(void *pointer) {
    __attribute__((cleanup(cetta_host_foreign_leave))) CettaHostForeignScope host = {0};
    cetta_host_foreign_enter(&host);
    PythonIteratorCursor *cursor = pointer;
    PyGILState_STATE state = PyGILState_Ensure();
    python_iterator_close(cursor->source);
    PyGILState_Release(state);
    arena_free(&cursor->arena);
    python_iterator_release(cursor->source);
    free(cursor);
}
static CettaOwnedExecutionStep python_iterator_next(void *pointer, CettaOwnedReturn *returned,
        CettaEvalCompletion *completion) {
    PythonIteratorCursor *cursor = pointer;
    PythonIterator *source = cursor->source;
    if (source->closed) return CETTA_OWNED_EXECUTION_COMPLETE;
    arena_free(&cursor->arena);
    arena_init_detached(&cursor->arena);
    __attribute__((cleanup(python_execution_guard_leave))) PythonExecutionGuard guard = {0};
    Atom *error = NULL;
    if (!python_execution_guard_enter(&guard, &cursor->arena, &error)) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    PyObject *object = source->iterator->obj;
    PyObject *value = object ? PyIter_Next(object) : NULL;
    if (!object) PyErr_SetString(PyExc_RuntimeError, "Python iterator has been released");
    if (!value) {
        python_iterator_close(source);
        if (!PyErr_Occurred()) return CETTA_OWNED_EXECUTION_COMPLETE;
        *returned = (CettaOwnedReturn){.outcome = cetta_call_raised(
            python_object_error(source->runtime, &cursor->arena, "Python iterator failed")), .final = true};
        return CETTA_OWNED_EXECUTION_RETURN;
    }
    ResultSet one; result_set_init(&one);
    bool ok = python_emit_object(source->runtime, &cursor->arena, value, &one, &error);
    Py_DECREF(value);
    *returned = (CettaOwnedReturn){.outcome = ok && one.len == 1u
        ? cetta_call_completed_value(one.items[0])
        : cetta_call_raised(error ? error : python_object_error(source->runtime, &cursor->arena, "Python item conversion")),
        .final = !ok};
    result_set_free(&one);
    return CETTA_OWNED_EXECUTION_RETURN;
}
static bool python_iterator_open(void *pointer, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    PythonIterator *iterator = pointer;
    CettaExprLen arity = 0;
    if (!cetta_invocation_arity(invocation, &arity) || arity || iterator->opened || iterator->closed) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        return false;
    }
    PythonIteratorCursor *cursor = calloc(1, sizeof(*cursor));
    if (!cursor) { *completion = CETTA_EVAL_INCOMPLETE_CAPACITY; return false; }
    iterator->opened = true;
    python_iterator_retain(iterator);
    cursor->source = iterator;
    arena_init_detached(&cursor->arena);
    cetta_owned_execution_init(execution, cursor, python_iterator_next, python_iterator_cursor_release);
    return true;
}
static Atom *python_iterator_expression(CettaForeignRuntime *runtime, Arena *arena, PyObject *object) {
    PyObject *iterator = PyObject_GetIter(object);
    if (!iterator) return NULL;
    PythonIterator *context = calloc(1, sizeof(*context));
    if (!context) { Py_DECREF(iterator); PyErr_NoMemory(); return NULL; }
    atomic_init(&context->references, 1u);
    context->runtime = runtime;
    context->iterator = foreign_new_python_value(runtime, iterator, false, true);
    if (!context->iterator) { Py_DECREF(iterator); free(context); return NULL; }
    context->iterator->object_mode = true;
    foreign_record_retain(context->iterator);
    Py_DECREF(iterator);
    CettaForeignProviderSpec spec = {.context = context, .owner_thread = true,
        .retain = python_iterator_retain, .release = python_iterator_release, .open = python_iterator_open};
    Atom *provider = cetta_foreign_provider_atom(arena, &spec);
    python_iterator_release(context);
    return provider ? atom_expr(arena, &provider, 1u) : NULL;
}
