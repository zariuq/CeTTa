/* A Python generator delegates special-method lookup, suppression and exception
 * chaining to Python's own with statement. The MeTTa body remains a lazy query. */
static PyObject *python_context_generator(PyObject *manager) {
    if (!g_python_context_factory) {
        PyObject *globals = PyDict_New();
        if (!globals) return NULL;
        if (PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins()) < 0) {
            Py_DECREF(globals); return NULL;
        }
        PyObject *code = PyRun_String(
            "def _scope(manager):\n"
            "    with manager as value:\n"
            "        yield value\n", Py_file_input, globals, globals);
        if (code) g_python_context_factory = Py_XNewRef(PyDict_GetItemString(globals, "_scope"));
        Py_XDECREF(code); Py_DECREF(globals);
        if (!g_python_context_factory) return NULL;
    }
    return PyObject_CallOneArg(g_python_context_factory, manager);
}

typedef struct {
    atomic_uint references;
    CettaForeignRuntime *runtime;
    PythonIterator scope;
    Arena captured, returned;
    Space *space;
    void *space_hold;
    Registry *registry;
    Arena *persistent;
    Atom *body;
    CettaEvalQuery *query;
    int fuel;
    bool opened;
} PythonContext;

static void python_context_retain(void *pointer) {
    PythonContext *context = pointer;
    atomic_fetch_add_explicit(&context->references, 1u, memory_order_relaxed);
}
static void python_context_close(PythonContext *context) {
    __attribute__((cleanup(cetta_host_foreign_leave))) CettaHostForeignScope host = {0};
    cetta_host_foreign_enter(&host);
    if (context->query) {
        CettaEvalQuery *query = context->query;
        context->query = NULL;
        eval_query_close(query);
    }
    PyGILState_STATE state = PyGILState_Ensure();
    python_iterator_close(&context->scope);
    PyGILState_Release(state);
}
static void python_context_release(void *pointer) {
    PythonContext *context = pointer;
    if (atomic_fetch_sub_explicit(&context->references, 1u, memory_order_acq_rel) != 1u) return;
    python_context_close(context);
    foreign_record_release(context->scope.iterator);
    arena_free(&context->returned);
    arena_free(&context->captured);
    python_space_hold_release(&context->space_hold);
    free(context);
}
static void python_context_execution_release(void *pointer) {
    python_context_close(pointer);
    python_context_release(pointer);
}

/* NULL returns mean successful completion/suppression. An error keeps its
 * original Python instance and traceback inside the ordinary raised outcome. */
static Atom *python_context_finish(PythonContext *context, Atom *raised) {
    __attribute__((cleanup(cetta_host_foreign_leave))) CettaHostForeignScope host = {0};
    cetta_host_foreign_enter(&host);
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject *generator = context->scope.iterator->obj;
    context->scope.closed = true;
    if (context->query) {
        CettaEvalQuery *query = context->query;
        context->query = NULL;
        eval_query_close(query);
    }
    PyObject *result = NULL;
    bool propagate_metta = false;
    if (!generator) PyErr_SetString(PyExc_RuntimeError, "Python context has been released");
    else if (raised) {
        PyObject *original = cetta_foreign_python_exception(raised);
        PyObject *exception = original ? Py_NewRef(original) : PyObject_CallFunction(
            PyExc_RuntimeError, "s", atom_to_string(&context->returned, raised));
        if (exception) result = PyObject_CallMethod(generator, "throw", "O", exception);
        if (PyErr_ExceptionMatches(PyExc_StopIteration)) PyErr_Clear();
        if (!original && exception && PyErr_Occurred()) {
            PyObject *type = NULL, *value = NULL, *traceback = NULL;
            PyErr_Fetch(&type, &value, &traceback);
            PyErr_NormalizeException(&type, &value, &traceback);
            if (value == exception) {
                propagate_metta = true;
                Py_XDECREF(type); Py_XDECREF(value); Py_XDECREF(traceback);
            } else PyErr_Restore(type, value, traceback);
        }
        Py_XDECREF(exception);
    } else result = PyIter_Next(generator);
    Py_XDECREF(result);
    Atom *error = propagate_metta ? raised : PyErr_Occurred()
        ? python_object_error(context->runtime, &context->returned, "Python context exit failed") : NULL;
    PyGILState_Release(state);
    return error;
}

static CettaOwnedExecutionStep python_context_next(void *pointer, CettaOwnedReturn *returned,
        CettaEvalCompletion *completion) {
    __attribute__((cleanup(cetta_host_foreign_leave))) CettaHostForeignScope host = {0};
    cetta_host_foreign_enter(&host);
    PythonContext *context = pointer;
    if (context->scope.closed) return CETTA_OWNED_EXECUTION_COMPLETE;
    arena_free(&context->returned);
    arena_init_detached(&context->returned);
    if (!context->query) {
        PyGILState_STATE state = PyGILState_Ensure();
        PyObject *generator = context->scope.iterator->obj;
        PyObject *value = generator ? PyIter_Next(generator) : NULL;
        if (!generator) PyErr_SetString(PyExc_RuntimeError, "Python context has been released");
        ResultSet one; result_set_init(&one);
        Atom *error = NULL;
        bool ok = value && python_emit_object(context->runtime, &context->captured, value, &one, &error);
        Py_XDECREF(value);
        if (ok && one.len == 1u) {
            Atom *call = atom_expr2(&context->captured, context->body, one.items[0]);
            context->query = eval_query_open_values(context->space, context->persistent,
                context->registry, call, context->fuel);
        }
        result_set_free(&one);
        if (!context->query) {
            error = error ? error : python_object_error(context->runtime, &context->returned,
                "Python context entry failed");
            python_iterator_close(&context->scope);
            *returned = (CettaOwnedReturn){.outcome = cetta_call_raised(error), .final = true};
        }
        PyGILState_Release(state);
        if (!context->query) return CETTA_OWNED_EXECUTION_RETURN;
    }
    EvalOutcome outcome; eval_outcome_init(&outcome);
    CettaEvalQueryAnswer packet = {0};
    uint64_t transitions = 0;
    CettaEvalQueryStep step = eval_query_step(context->query, &context->returned, 256u,
        &outcome, &packet, &transitions);
    Atom *value = outcome.results.len ? outcome.results.items[0] : NULL;
    CettaEvalCompletion cause = outcome.completion;
    bool raised = packet.kind == CETTA_CALL_RAISED || outcome.petta_raised_error ||
        cetta_foreign_python_he_exception(value) != NULL;
    eval_outcome_free(&outcome);
    if (step == CETTA_QUERY_ANSWER && value && !raised) {
        *returned = (CettaOwnedReturn){.outcome = cetta_call_completed_value(value)};
        return CETTA_OWNED_EXECUTION_RETURN;
    }
    if (raised || step == CETTA_QUERY_EXHAUSTED) {
        Atom *error = python_context_finish(context, raised ? value : NULL);
        if (!error) return CETTA_OWNED_EXECUTION_COMPLETE;
        *returned = (CettaOwnedReturn){.outcome = cetta_call_raised(error), .final = true};
        return CETTA_OWNED_EXECUTION_RETURN;
    }
    if (step == CETTA_QUERY_STOPPED || step == CETTA_QUERY_DRIVER_PENDING) {
        *completion = cause != CETTA_EVAL_COMPLETE ? cause : CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        python_context_close(context);
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    return CETTA_OWNED_EXECUTION_PAUSED;
}
static bool python_context_open(void *pointer, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    PythonContext *context = pointer;
    CettaExprLen arity = 0;
    if (context->opened || !cetta_invocation_arity(invocation, &arity) || arity) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE; return false;
    }
    context->opened = true;
    python_context_retain(context);
    cetta_owned_execution_init(execution, context, python_context_next, python_context_execution_release);
    return true;
}
static Atom *python_context_expression(CettaForeignRuntime *runtime, Space *space,
        Arena *arena, PyObject *manager, Atom *body) {
    PyObject *generator = python_context_generator(manager);
    if (!generator) return NULL;
    PythonContext *context = calloc(1, sizeof(*context));
    if (!context) { Py_DECREF(generator); PyErr_NoMemory(); return NULL; }
    atomic_init(&context->references, 1u);
    context->runtime = runtime;
    context->scope.iterator = foreign_new_python_value(runtime, generator, false, true);
    if (!context->scope.iterator) { Py_DECREF(generator); free(context); return NULL; }
    context->scope.iterator->object_mode = true;
    foreign_record_retain(context->scope.iterator);
    Py_DECREF(generator);
    context->space = space;
    context->space_hold = space_lifetime_pin ? space_lifetime_pin(space) : NULL;
    context->registry = eval_current_registry();
    context->persistent = eval_current_persistent_arena();
    context->fuel = eval_current_effective_fuel_limit();
    arena_init_detached(&context->captured);
    arena_init_detached(&context->returned);
    context->body = atom_deep_copy(&context->captured, body);
    CettaForeignProviderSpec spec = {.context = context, .owner_thread = true,
        .retain = python_context_retain, .release = python_context_release, .open = python_context_open};
    Atom *provider = cetta_foreign_provider_atom(arena, &spec);
    python_context_release(context);
    return provider ? atom_expr(arena, &provider, 1u) : NULL;
}
