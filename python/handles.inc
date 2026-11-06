/* Each public handle makes its engine's Python references visible to GC.
 * The capsule owns the C reference; clearing it precedes dropping the root. */
typedef struct {
    PyObject_HEAD
    PyObject *capsule, *owner;
} PythonHandle;

static int python_handle_traverse(PythonHandle *self, visitproc visit, void *arg) {
    Py_VISIT(self->owner);
    return 0;
}
static int python_handle_clear(PythonHandle *self) {
    Py_CLEAR(self->capsule);
    Py_CLEAR(self->owner);
    return 0;
}
static void python_handle_destroy(PythonHandle *self) {
    PyObject_GC_UnTrack(self);
    python_handle_clear(self);
    PyObject_GC_Del(self);
}
static PyTypeObject PythonHandleType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "cetta._Handle",
    .tp_basicsize = sizeof(PythonHandle),
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)python_handle_traverse,
    .tp_clear = (inquiry)python_handle_clear,
    .tp_dealloc = (destructor)python_handle_destroy,
};

static void *python_handle_pointer(PyObject *object, const char *name) {
    if (Py_IS_TYPE(object, &PythonHandleType)) {
        object = ((PythonHandle *)object)->capsule;
        if (!object) {
            PyErr_SetString(PyExc_RuntimeError, "CeTTa handle is closed");
            return NULL;
        }
    }
    return PyCapsule_GetPointer(object, name);
}

static PyObject *python_handle_new(PyObject *capsule, PyObject *owner) {
    if (!capsule) return NULL;
    if (PyType_Ready(&PythonHandleType) < 0) { Py_DECREF(capsule); return NULL; }
    PythonHandle *handle = PyObject_GC_New(PythonHandle, &PythonHandleType);
    if (!handle) { Py_DECREF(capsule); return NULL; }
    handle->capsule = capsule;
    handle->owner = Py_XNewRef(owner);
    PyObject_GC_Track(handle);
    return (PyObject *)handle;
}
