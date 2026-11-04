static PyObject *python_host_writer(PythonCallbackOwner *owner, unsigned channel) {
    PyObject *engine = python_callback_owner_get(owner);
    if (!engine) return NULL;
    PyObject *writer = channel ? Py_NewRef(Py_None) : PyObject_GetAttrString(engine, "_output");
    Py_DECREF(engine);
    if (writer == Py_None) {
        Py_DECREF(writer);
        PyObject *stream = PySys_GetObject(channel ? "stderr" : "stdout");
        writer = stream ? Py_NewRef(stream) : NULL;
    }
    if (!writer && !PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, "host output is closed");
    return writer;
}
static cetta_status_t python_host_write(void *context, unsigned channel, const char *bytes, size_t length) {
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject *writer = PyErr_Occurred() ? NULL : python_host_writer(context, channel);
    PyObject *text = writer ? PyUnicode_DecodeUTF8(bytes, (Py_ssize_t)length, "surrogateescape") : NULL;
    PyObject *result = text ? PyObject_CallMethod(writer, "write", "O", text) : NULL;
    if (result && PyLong_Check(result)) {
        Py_ssize_t written = PyLong_AsSsize_t(result);
        if (!PyErr_Occurred() && written != PyUnicode_GetLength(text))
            PyErr_SetString(PyExc_OSError, "short host output write");
    }
    bool ok = result && !PyErr_Occurred();
    Py_XDECREF(result); Py_XDECREF(text); Py_XDECREF(writer);
    cetta_status_t status = ok ? CETTA_OK : python_callback_error_status();
    PyGILState_Release(state);
    return status;
}
static cetta_status_t python_host_flush(void *context, unsigned channel) {
    PyGILState_STATE state = PyGILState_Ensure();
    PyObject *writer = PyErr_Occurred() ? NULL : python_host_writer(context, channel);
    PyObject *method = writer ? PyObject_GetAttrString(writer, "flush") : NULL;
    if (!method && PyErr_ExceptionMatches(PyExc_AttributeError)) PyErr_Clear();
    PyObject *result = method ? PyObject_CallNoArgs(method) : NULL;
    bool ok = !PyErr_Occurred();
    Py_XDECREF(result); Py_XDECREF(method); Py_XDECREF(writer);
    cetta_status_t status = ok ? CETTA_OK : python_callback_error_status();
    PyGILState_Release(state);
    return status;
}
static int python_host_interrupt(void *context) {
    (void)context;
    PyGILState_STATE state = PyGILState_Ensure();
    bool stop = PyErr_Occurred() != NULL || PyErr_CheckSignals() < 0;
    PyGILState_Release(state);
    return stop;
}
static cetta_status_t python_install_host_hooks(cetta_engine_t *engine, PyObject *object) {
    PythonCallbackOwner *owner = python_callback_owner_new(object);
    if (!owner) return CETTA_OUT_OF_MEMORY;
    cetta_host_hooks_t hooks = {.size = sizeof(hooks), .context = owner,
        .retain = python_callback_owner_retain, .release = python_callback_owner_release,
        .write = python_host_write, .flush = python_host_flush, .interrupt = python_host_interrupt};
    cetta_status_t status = cetta_embed_engine_set_host_hooks(engine, &hooks);
    python_callback_owner_release(owner);
    return status;
}
