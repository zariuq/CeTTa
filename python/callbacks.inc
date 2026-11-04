/* Python owns callable/generator graphs in its ordinary GC-visible objects.
 * Native records retain only weak references; an active Engine/Query keeps
 * the corresponding Python owner alive while execution is permitted. */
typedef struct {
    atomic_uint references;
    PyObject *weakref;
} PythonCallbackOwner;

static PythonCallbackOwner *python_callback_owner_new(PyObject *object) {
    PythonCallbackOwner *owner = malloc(sizeof(*owner));
    if (!owner) { PyErr_NoMemory(); return NULL; }
    owner->weakref = PyWeakref_NewRef(object, NULL);
    if (!owner->weakref) { free(owner); return NULL; }
    atomic_init(&owner->references, 1u);
    return owner;
}
static PyObject *python_callback_owner_get(PythonCallbackOwner *owner) {
#if PY_VERSION_HEX >= 0x030d0000
    PyObject *object = NULL;
    if (PyWeakref_GetRef(owner->weakref, &object) < 0) return NULL;
    return object;
#else
    PyObject *object = PyWeakref_GetObject(owner->weakref);
    return object && object != Py_None ? Py_NewRef(object) : NULL;
#endif
}
static void python_callback_owner_retain(void *pointer) {
    PythonCallbackOwner *owner = pointer;
    atomic_fetch_add_explicit(&owner->references, 1u, memory_order_relaxed);
}
static void python_callback_owner_release(void *pointer) {
    PythonCallbackOwner *owner = pointer;
    if (atomic_fetch_sub_explicit(&owner->references, 1u, memory_order_acq_rel) != 1u) return;
    if (Py_IsInitialized()) {
        PyGILState_STATE state = PyGILState_Ensure();
        Py_DECREF(owner->weakref);
        PyGILState_Release(state);
    }
    free(owner);
}
static cetta_status_t python_callback_error_status(void) {
    return PyErr_ExceptionMatches(PyExc_MemoryError) ? CETTA_OUT_OF_MEMORY : CETTA_ENGINE_ERROR;
}
static cetta_status_t python_callback_open(void *pointer, cetta_engine_t *engine,
        cetta_atom_t *const *arguments, size_t arity, void **output) {
    (void)engine;
    *output = NULL;
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject *registration = python_callback_owner_get(pointer);
    cetta_status_t status = CETTA_ENGINE_CLOSED;
    if (registration) {
        PyObject *values = PyTuple_New((Py_ssize_t)arity);
        if (values) {
            for (size_t i = 0; i < arity; ++i) {
                cetta_embed_atom_retain(arguments[i]);
                PyObject *value = atom_object(arguments[i]);
                if (!value) { Py_CLEAR(values); break; }
                PyTuple_SET_ITEM(values, i, value);
            }
        }
        PyObject *invocation = values ? PyObject_CallMethod(registration, "_open", "(O)", values) : NULL;
        Py_XDECREF(values);
        if (invocation) {
            *output = python_callback_owner_new(invocation);
            if (!*output) {
                SavedPythonError primary = save_python_error();
                PyObject *closed = PyObject_CallMethod(invocation, "close", NULL);
                Py_XDECREF(closed);
                if (PyErr_Occurred()) PyErr_WriteUnraisable(NULL);
                restore_python_error(primary);
            }
            Py_DECREF(invocation);
        }
        status = *output ? CETTA_OK : python_callback_error_status();
        Py_DECREF(registration);
    } else if (PyErr_Occurred()) status = python_callback_error_status();
    PyGILState_Release(state);
    return status;
}
static cetta_status_t python_callback_next(void *pointer, cetta_callback_state_t *result_state,
        cetta_atom_t **output) {
    *output = NULL;
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject *invocation = python_callback_owner_get(pointer);
    cetta_status_t status = CETTA_ENGINE_CLOSED;
    if (invocation) {
        PyObject *result = PyObject_CallMethod(invocation, "_pull", NULL);
        int kind = 0;
        PyObject *value = NULL;
        if (result && PyArg_ParseTuple(result, "iO", &kind, &value)) {
            if (value != Py_None) {
                *output = PyCapsule_GetPointer(value, atom_capsule);
                if (*output) cetta_embed_atom_retain(*output);
            }
            *result_state = (cetta_callback_state_t)kind;
        }
        status = PyErr_Occurred() ? python_callback_error_status() : CETTA_OK;
        Py_XDECREF(result);
        Py_DECREF(invocation);
    } else if (PyErr_Occurred()) status = python_callback_error_status();
    PyGILState_Release(state);
    return status;
}
static void python_callback_close(void *pointer, cetta_callback_close_mode_t mode) {
    PythonCallbackOwner *owner = pointer;
    PyGILState_STATE state = PyGILState_Ensure();
    SavedPythonError primary = save_python_error();
    PyObject *invocation = python_callback_owner_get(owner);
    if (invocation) {
        PyObject *closed = PyObject_CallMethod(invocation, "close", NULL);
        Py_XDECREF(closed);
        Py_DECREF(invocation);
    }
    if (primary.type || primary.value || primary.traceback || mode == CETTA_CALLBACK_CLOSE_DEFERRED) {
        if (PyErr_Occurred()) PyErr_WriteUnraisable(NULL);
        restore_python_error(primary);
    }
    python_callback_owner_release(owner);
    PyGILState_Release(state);
}
static PyObject *register_callback(PyObject *module, PyObject *args) {
    PyObject *engine_object, *registration;
    const char *name;
    Py_ssize_t arity;
    int stream;
    if (!PyArg_ParseTuple(args, "OsOnp", &engine_object, &name, &registration, &arity, &stream)) return NULL;
    if (arity < 0) { PyErr_SetString(PyExc_ValueError, "arity cannot be negative"); return NULL; }
    if (PyThreadState_GetInterpreter(PyThreadState_Get()) != PyInterpreterState_Main()) {
        PyErr_SetString(PyExc_NotImplementedError, "callbacks in subinterpreters are not yet supported");
        return NULL;
    }
    cetta_engine_t *engine = PyCapsule_GetPointer(engine_object, engine_capsule);
    if (!engine) return NULL;
    PythonCallbackOwner *owner = python_callback_owner_new(registration);
    if (!owner) return NULL;
    cetta_callback_descriptor_t descriptor = {.size = sizeof(descriptor), .arity = (size_t)arity,
        .kind = stream ? CETTA_CALLBACK_STREAM : CETTA_CALLBACK_SINGLE,
        .context = owner, .retain = python_callback_owner_retain, .release = python_callback_owner_release,
        .open = python_callback_open, .next = python_callback_next, .close = python_callback_close};
    cetta_atom_t *atom = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_register_callback(engine, name, &descriptor, &atom);
    Py_END_ALLOW_THREADS
    python_callback_owner_release(owner);
    if (PyErr_Occurred()) { release_atom(atom); return NULL; }
    return status == CETTA_OK ? atom_object(atom) : status_error(module, status);
}
