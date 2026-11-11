typedef struct {
    PyObject_HEAD
    cetta_buffer_t *handle;
    cetta_buffer_descriptor_t info;
    Py_ssize_t *shape, *strides;
    Py_ssize_t bytes, itemsize;
    char format[3];
} PythonBuffer;

static void buffer_destroy(PyObject *pointer) {
    PythonBuffer *buffer = (PythonBuffer *)pointer;
    SavedPythonError error = save_python_error();
    Py_BEGIN_ALLOW_THREADS
    cetta_embed_buffer_release(buffer->handle);
    Py_END_ALLOW_THREADS
    PyMem_Free(buffer->shape);
    PyMem_Free(buffer->strides);
    finish_capsule_cleanup(pointer, error);
    PyTypeObject *type = Py_TYPE(pointer);
    type->tp_free(pointer);
    Py_DECREF(type);
}
static PyObject *buffer_new_refused(PyTypeObject *type, PyObject *args, PyObject *keywords) {
    (void)type; (void)args; (void)keywords;
    PyErr_SetString(PyExc_TypeError, "buffers are created from tensors or immutable bytes");
    return NULL;
}
static int buffer_export(PyObject *object, Py_buffer *view, int flags) {
    PythonBuffer *buffer = (PythonBuffer *)object;
    if (flags & PyBUF_WRITABLE) { PyErr_SetString(PyExc_BufferError, "CeTTa storage is immutable"); return -1; }
    *view = (Py_buffer){.buf = buffer->info.allocation
        ? (unsigned char *)buffer->info.allocation + buffer->info.offset : NULL,
        .len = buffer->bytes, .readonly = 1, .itemsize = buffer->itemsize,
        .format = buffer->format, .ndim = (int)buffer->info.rank,
        .shape = buffer->shape, .strides = buffer->strides};
    if (((flags & PyBUF_C_CONTIGUOUS) == PyBUF_C_CONTIGUOUS && !PyBuffer_IsContiguous(view, 'C')) ||
        ((flags & PyBUF_F_CONTIGUOUS) == PyBUF_F_CONTIGUOUS && !PyBuffer_IsContiguous(view, 'F')) ||
        ((flags & PyBUF_ANY_CONTIGUOUS) == PyBUF_ANY_CONTIGUOUS && !PyBuffer_IsContiguous(view, 'A')) ||
        ((flags & PyBUF_STRIDES) != PyBUF_STRIDES && !PyBuffer_IsContiguous(view, 'C'))) {
        PyErr_SetString(PyExc_BufferError, "requested buffer layout requires a copy");
        return -1;
    }
    if (!(flags & PyBUF_FORMAT)) view->format = NULL;
    if ((flags & PyBUF_STRIDES) != PyBUF_STRIDES) view->strides = NULL;
    if (!(flags & PyBUF_ND)) { view->shape = NULL; view->ndim = 1; }
    view->obj = Py_NewRef(object);
    return 0;
}
static PyObject *buffer_dimensions(PythonBuffer *buffer, bool strides) {
    PyObject *tuple = PyTuple_New((Py_ssize_t)buffer->info.rank);
    if (!tuple) return NULL;
    for (size_t i = 0; i < buffer->info.rank; ++i) {
        PyObject *value = PyLong_FromSsize_t(strides ? buffer->strides[i] : buffer->shape[i]);
        if (!value) { Py_DECREF(tuple); return NULL; }
        PyTuple_SET_ITEM(tuple, i, value);
    }
    return tuple;
}
static PyObject *buffer_shape(PyObject *object, void *closure) {
    (void)closure; return buffer_dimensions((PythonBuffer *)object, false);
}
static PyObject *buffer_strides(PyObject *object, void *closure) {
    (void)closure; return buffer_dimensions((PythonBuffer *)object, true);
}
static PyObject *buffer_dtype(PyObject *object, void *closure) {
    (void)closure; return PyUnicode_FromString(((PythonBuffer *)object)->info.dtype);
}
static PyObject *buffer_bytes(PyObject *object, void *closure) {
    (void)closure; return PyLong_FromSsize_t(((PythonBuffer *)object)->bytes);
}
static PyObject *buffer_byte_order(PyObject *object, void *closure) {
    (void)closure;
    cetta_byte_order_t order = ((PythonBuffer *)object)->info.byte_order;
    return PyUnicode_FromString(order == CETTA_BYTE_ORDER_LITTLE ? "<" : order == CETTA_BYTE_ORDER_BIG ? ">" : "=");
}
static PyGetSetDef buffer_properties[] = {
    {"shape", buffer_shape, NULL, NULL, NULL}, {"strides", buffer_strides, NULL, NULL, NULL},
    {"dtype", buffer_dtype, NULL, NULL, NULL}, {"nbytes", buffer_bytes, NULL, NULL, NULL},
    {"byte_order", buffer_byte_order, NULL, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL}
};
static PyType_Slot buffer_type_slots[] = {
    {Py_tp_new, buffer_new_refused}, {Py_tp_dealloc, buffer_destroy},
    {Py_tp_getset, buffer_properties}, {Py_bf_getbuffer, buffer_export}, {0, NULL}
};
static PyType_Spec buffer_type_spec = {.name = "cetta._native.Buffer", .basicsize = sizeof(PythonBuffer),
    .flags = Py_TPFLAGS_DEFAULT, .slots = buffer_type_slots};

static PyObject *buffer_object(PyObject *module, cetta_buffer_t *handle) {
    NativeState *state = PyModule_GetState(module);
    PythonBuffer *buffer = (PythonBuffer *)((PyTypeObject *)state->buffer_type)->tp_alloc((PyTypeObject *)state->buffer_type, 0);
    if (!buffer) { cetta_embed_buffer_release(handle); return NULL; }
    buffer->handle = handle;
    buffer->info.size = sizeof(buffer->info);
    cetta_status_t status = cetta_embed_buffer_describe(handle, &buffer->info);
    if (status != CETTA_OK) { Py_DECREF(buffer); return status_error(module, status); }
    char format = 0; Py_ssize_t width = 0;
    if (!strcmp(buffer->info.dtype, "i64")) { format = 'q'; width = 8; }
    else if (!strcmp(buffer->info.dtype, "index")) { format = 'Q'; width = 8; }
    else if (!strcmp(buffer->info.dtype, "f64")) { format = 'd'; width = 8; }
    else if (!strcmp(buffer->info.dtype, "f32")) { format = 'f'; width = 4; }
    else if (!strcmp(buffer->info.dtype, "u8")) { format = 'B'; width = 1; }
    else if (!strcmp(buffer->info.dtype, "bool")) { format = '?'; width = 1; }
    if (!width || buffer->info.rank > INT_MAX || buffer->info.rank > PY_SSIZE_T_MAX / sizeof(Py_ssize_t)) goto too_large;
    size_t rank = buffer->info.rank;
    buffer->shape = rank ? PyMem_Malloc(rank * sizeof(*buffer->shape)) : NULL;
    buffer->strides = rank ? PyMem_Malloc(rank * sizeof(*buffer->strides)) : NULL;
    if (rank && (!buffer->shape || !buffer->strides)) { Py_DECREF(buffer); return PyErr_NoMemory(); }
    Py_ssize_t count = 1;
    for (size_t i = 0; i < rank; ++i) {
        if (buffer->info.shape[i] > PY_SSIZE_T_MAX) goto too_large;
        buffer->shape[i] = (Py_ssize_t)buffer->info.shape[i];
        buffer->strides[i] = buffer->info.strides[i];
        if (__builtin_mul_overflow(count, buffer->shape[i], &count)) goto too_large;
    }
    if (__builtin_mul_overflow(count, width, &buffer->bytes)) goto too_large;
    buffer->itemsize = width;
    size_t prefix = width > 1 && buffer->info.byte_order != CETTA_BYTE_ORDER_NATIVE ? 1u : 0u;
    if (prefix) buffer->format[0] = buffer->info.byte_order == CETTA_BYTE_ORDER_LITTLE ? '<' : '>';
    buffer->format[prefix] = format;
    buffer->format[prefix + 1u] = '\0';
    return (PyObject *)buffer;
too_large:
    Py_DECREF(buffer);
    PyErr_SetString(PyExc_OverflowError, "tensor layout cannot be represented by a Python buffer");
    return NULL;
}
static int parse_buffer_dimensions(PyObject *shape_object, PyObject *strides_object,
        size_t **shape, ptrdiff_t **strides, size_t *rank) {
    *shape = NULL; *strides = NULL;
    PyObject *dimensions = PySequence_Fast(shape_object, "shape must be a sequence");
    if (!dimensions) return -1;
    Py_ssize_t n = PySequence_Fast_GET_SIZE(dimensions);
    *rank = (size_t)n;
    if ((size_t)n > SIZE_MAX / sizeof(size_t) || (size_t)n > SIZE_MAX / sizeof(ptrdiff_t)) {
        Py_DECREF(dimensions); PyErr_SetString(PyExc_OverflowError, "rank is too large"); return -1;
    }
    *shape = n ? PyMem_Malloc((size_t)n * sizeof(**shape)) : NULL;
    if (n && !*shape) { Py_DECREF(dimensions); PyErr_NoMemory(); return -1; }
    for (Py_ssize_t i = 0; i < n; ++i) {
        (*shape)[i] = PyLong_AsSize_t(PySequence_Fast_GET_ITEM(dimensions, i));
        if (PyErr_Occurred()) { Py_DECREF(dimensions); return -1; }
    }
    Py_DECREF(dimensions);
    if (strides_object != Py_None) {
        PyObject *steps = PySequence_Fast(strides_object, "strides must be a sequence");
        if (!steps) return -1;
        if (PySequence_Fast_GET_SIZE(steps) != n) {
            Py_DECREF(steps); PyErr_SetString(PyExc_ValueError, "shape and strides have different ranks"); return -1;
        }
        *strides = n ? PyMem_Malloc((size_t)n * sizeof(**strides)) : NULL;
        if (n && !*strides) { Py_DECREF(steps); PyErr_NoMemory(); return -1; }
        for (Py_ssize_t i = 0; i < n; ++i) {
            (*strides)[i] = (ptrdiff_t)PyLong_AsSsize_t(PySequence_Fast_GET_ITEM(steps, i));
            if (PyErr_Occurred()) { Py_DECREF(steps); return -1; }
        }
        Py_DECREF(steps);
    }
    return 0;
}
static void buffer_bytes_retain(void *object) {
    PyGILState_STATE state = PyGILState_Ensure();
    Py_INCREF((PyObject *)object);
    PyGILState_Release(state);
}
static void buffer_bytes_release(void *object) {
    PyGILState_STATE state = PyGILState_Ensure();
    Py_DECREF((PyObject *)object);
    PyGILState_Release(state);
}
/* Exact bytes have no user attributes or writable aliases. Retaining this
 * exporter cannot hide an engine/callback reference cycle. */
static PyObject *buffer_from_bytes(PyObject *module, PyObject *args) {
    PyObject *bytes, *shape_object, *strides_object, *position;
    const char *dtype;
    int address, byte_order;
    if (!PyArg_ParseTuple(args, "OsOOOpi", &bytes, &dtype, &shape_object, &strides_object, &position, &address, &byte_order)) return NULL;
    if (!PyBytes_CheckExact(bytes)) { PyErr_SetString(PyExc_TypeError, "immutable backing must be exact bytes"); return NULL; }
    size_t *shape = NULL, rank = 0; ptrdiff_t *strides = NULL;
    if (parse_buffer_dimensions(shape_object, strides_object, &shape, &strides, &rank)) goto failure;
    uintptr_t offset = (uintptr_t)PyLong_AsVoidPtr(position);
    if (PyErr_Occurred()) goto failure;
    if (address) {
        uintptr_t base = (uintptr_t)PyBytes_AS_STRING(bytes);
        if (offset < base || offset - base > (size_t)PyBytes_GET_SIZE(bytes)) {
            PyErr_SetString(PyExc_BufferError, "array data lies outside its immutable owner"); goto failure;
        }
        offset -= base;
    }
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = dtype, .byte_order = (cetta_byte_order_t)byte_order,
        .allocation = PyBytes_AS_STRING(bytes), .allocation_bytes = (size_t)PyBytes_GET_SIZE(bytes),
        .offset = offset, .rank = rank, .shape = shape, .strides = strides};
    cetta_buffer_owner_t owner = {bytes, buffer_bytes_retain, buffer_bytes_release};
    cetta_buffer_t *buffer = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_buffer_borrow(&spec, &owner, &buffer);
    Py_END_ALLOW_THREADS
    PyMem_Free(shape); PyMem_Free(strides);
    return status == CETTA_OK ? buffer_object(module, buffer) : status_error(module, status);
failure:
    PyMem_Free(shape); PyMem_Free(strides); return NULL;
}
static PyObject *buffer_at_address(PyObject *module, PyObject *args) {
    PyObject *source, *shape_object, *strides_object, *address;
    const char *dtype;
    int byte_order;
    if (!PyArg_ParseTuple(args, "OsOOOi", &source, &dtype, &shape_object, &strides_object, &address, &byte_order)) return NULL;
    NativeState *state = PyModule_GetState(module);
    if (!PyObject_TypeCheck(source, (PyTypeObject *)state->buffer_type)) {
        PyErr_SetString(PyExc_TypeError, "expected a CeTTa Buffer"); return NULL;
    }
    PythonBuffer *base = (PythonBuffer *)source;
    uintptr_t pointer = (uintptr_t)PyLong_AsVoidPtr(address), start = (uintptr_t)base->info.allocation;
    if (PyErr_Occurred()) return NULL;
    if (pointer < start || pointer - start > base->info.allocation_bytes) {
        PyErr_SetString(PyExc_BufferError, "array data lies outside its retained owner"); return NULL;
    }
    size_t *shape = NULL, rank = 0; ptrdiff_t *strides = NULL;
    if (parse_buffer_dimensions(shape_object, strides_object, &shape, &strides, &rank)) {
        PyMem_Free(shape); PyMem_Free(strides); return NULL;
    }
    cetta_buffer_t *buffer = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_buffer_reinterpret(base->handle, dtype, (cetta_byte_order_t)byte_order,
        shape, strides, rank, pointer - start, &buffer);
    Py_END_ALLOW_THREADS
    PyMem_Free(shape); PyMem_Free(strides);
    return status == CETTA_OK ? buffer_object(module, buffer) : status_error(module, status);
}
static PyObject *buffer_atom(PyObject *module, PyObject *args) {
    PyObject *engine_object, *buffer;
    if (!PyArg_ParseTuple(args, "OO", &engine_object, &buffer)) return NULL;
    NativeState *state = PyModule_GetState(module);
    if (!PyObject_TypeCheck(buffer, (PyTypeObject *)state->buffer_type)) {
        PyErr_SetString(PyExc_TypeError, "expected a CeTTa Buffer"); return NULL;
    }
    cetta_engine_t *engine = python_handle_pointer(engine_object, engine_capsule);
    if (!engine) return NULL;
    cetta_atom_t *atom = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_atom_buffer(engine, ((PythonBuffer *)buffer)->handle, &atom);
    Py_END_ALLOW_THREADS
    return status == CETTA_OK ? atom_object(atom) : status_error(module, status);
}
static PyObject *atom_buffer(PyObject *module, PyObject *object) {
    cetta_atom_t *atom = python_handle_pointer(object, atom_capsule);
    if (!atom) return NULL;
    cetta_buffer_t *buffer = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_buffer_from_atom(atom, &buffer);
    Py_END_ALLOW_THREADS
    if (status == CETTA_BUFFER_INCOMPATIBLE) Py_RETURN_NONE;
    return status == CETTA_OK ? buffer_object(module, buffer) : status_error(module, status);
}
static PyObject *sparse_export(PyObject *module, PyObject *object) {
    cetta_atom_t *atom = python_handle_pointer(object, atom_capsule);
    if (!atom) return NULL;
    cetta_tensor_info_t info = {.size = sizeof(info)};
    cetta_sparse_components_t parts = {.size = sizeof(parts)};
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_tensor_describe(atom, &info);
    if (status == CETTA_OK && info.layout != CETTA_LAYOUT_DENSE)
        status = cetta_embed_tensor_components(atom, &parts);
    Py_END_ALLOW_THREADS
    if (status == CETTA_BUFFER_INCOMPATIBLE || (status == CETTA_OK && info.layout == CETTA_LAYOUT_DENSE)) Py_RETURN_NONE;
    if (status != CETTA_OK) return status_error(module, status);
    if (info.rank > PY_SSIZE_T_MAX) {
        PyErr_SetString(PyExc_OverflowError, "sparse tensor rank is too large");
        goto failed;
    }
    PyObject *shape = PyTuple_New((Py_ssize_t)info.rank);
    if (!shape) goto failed;
    for (size_t i = 0; i < info.rank; ++i) {
        PyObject *dimension = PyLong_FromSize_t(info.shape[i]);
        if (!dimension) { Py_DECREF(shape); goto failed; }
        PyTuple_SET_ITEM(shape, (Py_ssize_t)i, dimension);
    }
    PyObject *items[6] = {PyUnicode_FromString(info.layout == CETTA_LAYOUT_COO ? "coo" : info.layout == CETTA_LAYOUT_CSR ? "csr" : "csc"), shape};
    cetta_buffer_t **handles[] = {&parts.values, &parts.coordinates, &parts.indptr, &parts.indices};
    bool ok = items[0] != NULL;
    for (size_t i = 0; ok && i < 4; ++i) {
        cetta_buffer_t *handle = *handles[i]; *handles[i] = NULL;
        items[i + 2u] = handle ? buffer_object(module, handle) : Py_NewRef(Py_None);
        ok = items[i + 2u] != NULL;
    }
    PyObject *result = ok ? PyTuple_Pack(6, items[0], items[1], items[2], items[3], items[4], items[5]) : NULL;
    for (size_t i = 0; i < 6; ++i) Py_XDECREF(items[i]);
    cetta_embed_buffer_release(parts.values); cetta_embed_buffer_release(parts.coordinates);
    cetta_embed_buffer_release(parts.indptr); cetta_embed_buffer_release(parts.indices);
    return result;
failed:
    cetta_embed_buffer_release(parts.values); cetta_embed_buffer_release(parts.coordinates);
    cetta_embed_buffer_release(parts.indptr); cetta_embed_buffer_release(parts.indices);
    return NULL;
}
static PyObject *tensor_from_sparse(PyObject *module, PyObject *args) {
    PyObject *engine_object, *shape_object, *objects[4];
    int layout, duplicates, copy;
    if (!PyArg_ParseTuple(args, "OOiOOOOii", &engine_object, &shape_object, &layout,
            &objects[0], &objects[1], &objects[2], &objects[3], &duplicates, &copy)) return NULL;
    cetta_engine_t *engine = python_handle_pointer(engine_object, engine_capsule);
    if (!engine) return NULL;
    NativeState *state = PyModule_GetState(module);
    cetta_buffer_t *buffers[4] = {0};
    for (size_t i = 0; i < 4; ++i) if (objects[i] != Py_None) {
        if (!PyObject_TypeCheck(objects[i], (PyTypeObject *)state->buffer_type)) {
            PyErr_SetString(PyExc_TypeError, "sparse components must be CeTTa buffers"); return NULL;
        }
        buffers[i] = ((PythonBuffer *)objects[i])->handle;
    }
    size_t *shape = NULL, rank = 0; ptrdiff_t *strides = NULL;
    if (parse_buffer_dimensions(shape_object, Py_None, &shape, &strides, &rank)) {
        PyMem_Free(shape); return NULL;
    }
    cetta_sparse_descriptor_t spec = {.size = sizeof(spec), .layout = (cetta_tensor_layout_t)layout,
        .shape = shape, .rank = rank, .values = buffers[0], .coordinates = buffers[1],
        .indptr = buffers[2], .indices = buffers[3]};
    cetta_atom_t *atom = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_tensor_from_sparse(engine, &spec, (cetta_duplicates_t)duplicates, (cetta_copy_t)copy, &atom);
    Py_END_ALLOW_THREADS
    PyMem_Free(shape);
    return status == CETTA_OK ? atom_object(atom) : status_error(module, status);
}
