/* Python handles trace these references. Native arenas borrow the owner;
 * their final releases defer decrefs until the evaluator reaches a safe point. */
struct PythonRoots {
    PyObject_HEAD
    CettaForeignRuntime *runtime;
    CettaForeignValue *values;
    bool clearing;
};

static void python_roots_unlink_locked(CettaForeignValue *value) {
    if (!value->owner) return;
    if (value->owner_prev) value->owner_prev->owner_next = value->owner_next;
    else value->owner->values = value->owner_next;
    if (value->owner_next) value->owner_next->owner_prev = value->owner_prev;
    value->owner = NULL;
    value->owner_prev = value->owner_next = NULL;
}

static int python_roots_traverse(PythonRoots *self, visitproc visit, void *arg) {
    int result = 0;
    pthread_mutex_lock(&g_foreign_records_mutex);
    for (CettaForeignValue *v = self->values; v && !result; v = v->owner_next)
        if (v->obj) result = visit(v->obj, arg);
    pthread_mutex_unlock(&g_foreign_records_mutex);
    return result;
}

static int python_roots_clear(PythonRoots *self) {
    self->clearing = true;
    for (;;) {
        pthread_mutex_lock(&g_foreign_records_mutex);
        CettaForeignValue *value = self->values;
        PyObject *object = value ? value->obj : NULL;
        if (value) {
            python_roots_unlink_locked(value);
            value->obj = NULL;
        }
        pthread_mutex_unlock(&g_foreign_records_mutex);
        if (!value) break;
        Py_XDECREF(object);
    }
    return 0;
}

static void python_roots_destroy(PythonRoots *self) {
    PyObject_GC_UnTrack(self);
    python_roots_clear(self);
    if (self->runtime) self->runtime->owner = NULL;
    PyObject_GC_Del(self);
}

static PyTypeObject PythonRootsType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "cetta._PythonRoots",
    .tp_basicsize = sizeof(PythonRoots),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)python_roots_traverse,
    .tp_clear = (inquiry)python_roots_clear,
    .tp_dealloc = (destructor)python_roots_destroy,
};

void *cetta_foreign_python_owner_new(CettaForeignRuntime *runtime) {
    if (!runtime || runtime->owner) {
        PyErr_SetString(PyExc_RuntimeError, "Python owner already attached");
        return NULL;
    }
    if (PyType_Ready(&PythonRootsType) < 0) return NULL;
    PythonRoots *owner = PyObject_GC_New(PythonRoots, &PythonRootsType);
    if (!owner) return NULL;
    owner->runtime = runtime;
    owner->values = NULL;
    owner->clearing = false;
    runtime->owner = owner;
    PyObject_GC_Track(owner);
    return owner;
}

void *cetta_foreign_python_owner(CettaForeignRuntime *runtime) {
    return runtime ? runtime->owner : NULL;
}
