#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "cetta_embed.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <errno.h>

typedef struct { PyObject *type, *value, *traceback; } SavedPythonError;
static SavedPythonError save_python_error(void) {
    SavedPythonError error = {0};
#if PY_VERSION_HEX >= 0x030c0000
    error.value = PyErr_GetRaisedException();
#else
    PyErr_Fetch(&error.type, &error.value, &error.traceback);
#endif
    return error;
}
static void restore_python_error(SavedPythonError error) {
#if PY_VERSION_HEX >= 0x030c0000
    PyErr_SetRaisedException(error.value);
#else
    PyErr_Restore(error.type, error.value, error.traceback);
#endif
}
static void finish_capsule_cleanup(PyObject *capsule, SavedPythonError error) {
    if (PyErr_Occurred()) PyErr_WriteUnraisable(capsule);
    restore_python_error(error);
}

static const char engine_capsule[] = "cetta.engine.v1";
static const char atom_capsule[] = "cetta.atom.v1";
static const char query_capsule[] = "cetta.query.v1";
static const char space_capsule[] = "cetta.space.v1";

static void release_engine(cetta_engine_t *engine) {
    Py_BEGIN_ALLOW_THREADS
    cetta_embed_engine_release(engine);
    Py_END_ALLOW_THREADS
}
static void release_atom(cetta_atom_t *atom) {
    Py_BEGIN_ALLOW_THREADS
    cetta_embed_atom_release(atom);
    Py_END_ALLOW_THREADS
}
static void release_result(cetta_result_t *result) {
    Py_BEGIN_ALLOW_THREADS
    cetta_embed_result_release(result);
    Py_END_ALLOW_THREADS
}

static void engine_destroy(PyObject *capsule) {
    SavedPythonError error = save_python_error();
    cetta_engine_t *engine = PyCapsule_GetPointer(capsule, engine_capsule);
    if (engine) release_engine(engine);
    finish_capsule_cleanup(capsule, error);
}
static void atom_destroy(PyObject *capsule) {
    SavedPythonError error = save_python_error();
    cetta_atom_t *atom = PyCapsule_GetPointer(capsule, atom_capsule);
    if (atom) release_atom(atom);
    finish_capsule_cleanup(capsule, error);
}
static void query_destroy(PyObject *capsule) {
    SavedPythonError error = save_python_error();
    cetta_query_t *query = PyCapsule_GetPointer(capsule, query_capsule);
    if (query) {
        Py_BEGIN_ALLOW_THREADS
        cetta_embed_query_release(query);
        Py_END_ALLOW_THREADS
    }
    finish_capsule_cleanup(capsule, error);
}
static void space_destroy(PyObject *capsule) {
    SavedPythonError error = save_python_error();
    cetta_space_t *space = PyCapsule_GetPointer(capsule, space_capsule);
    if (space) {
        Py_BEGIN_ALLOW_THREADS
        cetta_embed_space_release(space);
        Py_END_ALLOW_THREADS
    }
    finish_capsule_cleanup(capsule, error);
}
static PyObject *space_object(cetta_space_t *space) {
    PyObject *object = PyCapsule_New(space, space_capsule, space_destroy);
    if (!object) {
        Py_BEGIN_ALLOW_THREADS
        cetta_embed_space_release(space);
        Py_END_ALLOW_THREADS
    }
    return object;
}
static PyObject *atom_object(cetta_atom_t *atom) {
    PyObject *capsule = PyCapsule_New(atom, atom_capsule, atom_destroy);
    if (!capsule) release_atom(atom);
    return capsule;
}
typedef struct { PyObject *error, *buffer_type; } NativeState;
static PyObject *status_error(PyObject *module, cetta_status_t status) {
    NativeState *state = PyModule_GetState(module);
    PyObject *name = PyUnicode_FromString(cetta_embed_status_name(status));
    if (!name) return NULL;
    PyObject *error = PyObject_CallFunction(state->error, "s", cetta_embed_status_name(status));
    if (error && !PyObject_SetAttrString(error, "status", name))
        PyErr_SetObject(state->error, error);
    Py_XDECREF(error);
    Py_DECREF(name);
    return NULL;
}
static PyObject *diagnostic_object(const cetta_diagnostic_t *diagnostic) {
    return Py_BuildValue("{s:s,s:s,s:K,s:K,s:i,s:i}",
        "message", diagnostic->message, "source", diagnostic->source,
        "line", (unsigned long long)diagnostic->line,
        "column", (unsigned long long)diagnostic->column,
        "exit_code", diagnostic->exit_code, "system_error", diagnostic->system_error);
}

static cetta_status_t python_install_host_hooks(cetta_engine_t *, PyObject *);
static PyObject *engine_new(PyObject *module, PyObject *args) {
    (void)module;
    const char *language, *profile, *root;
    PyObject *owner;
    unsigned long long memory_limit;
    if (!PyArg_ParseTuple(args, "szzOK", &language, &profile, &root, &owner, &memory_limit)) return NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION, language, profile, root};
    cetta_engine_t *engine = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_engine_new_limited(&config, memory_limit, &engine);
    Py_END_ALLOW_THREADS
    if (status != CETTA_OK) return status_error(module, status);
    status = python_install_host_hooks(engine, owner);
    if (status != CETTA_OK) {
        release_engine(engine);
        return PyErr_Occurred() ? NULL : status_error(module, status);
    }
    PyObject *capsule = PyCapsule_New(engine, engine_capsule, engine_destroy);
    if (!capsule) release_engine(engine);
    return capsule;
}
static PyObject *engine_close(PyObject *module, PyObject *object) {
    (void)module;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    Py_BEGIN_ALLOW_THREADS
    cetta_embed_engine_close(engine);
    Py_END_ALLOW_THREADS
    if (PyErr_Occurred()) return NULL;
    Py_RETURN_NONE;
}
static PyObject *parse(PyObject *module, PyObject *args) {
    (void)module;
    PyObject *object;
    const char *text;
    Py_ssize_t length;
    if (!PyArg_ParseTuple(args, "Os#", &object, &text, &length)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    cetta_atom_t *atom = NULL;
    cetta_status_t status;
    cetta_diagnostic_t diagnostic = {.size = sizeof(diagnostic)};
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_parse_diagnostic(engine, text, length, &diagnostic, &atom);
    Py_END_ALLOW_THREADS
    if (status == CETTA_OK) return atom_object(atom);
    NativeState *state = PyModule_GetState(module);
    PyObject *error = PyObject_CallFunction(state->error, "s", diagnostic.message[0]
        ? diagnostic.message : cetta_embed_status_name(status));
    PyObject *details = diagnostic_object(&diagnostic);
    PyObject *name = PyUnicode_FromString(cetta_embed_status_name(status));
    if (error && details && name && !PyObject_SetAttrString(error, "status", name) &&
        !PyObject_SetAttrString(error, "diagnostic", details))
        PyErr_SetObject(state->error, error);
    Py_XDECREF(error); Py_XDECREF(details); Py_XDECREF(name);
    return NULL;
}
static PyObject *scalar(PyObject *module, PyObject *args) {
    (void)module;
    PyObject *object, *value;
    int kind;
    if (!PyArg_ParseTuple(args, "OiO", &object, &kind, &value)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    cetta_status_t status = CETTA_INVALID_ARGUMENT;
    cetta_atom_t *atom = NULL;
    if (kind == CETTA_ATOM_SYMBOL || kind == CETTA_ATOM_STRING || kind == CETTA_ATOM_VARIABLE ||
        kind == CETTA_ATOM_BIG_INTEGER || kind == CETTA_ATOM_RATIONAL) {
        Py_ssize_t size;
        const char *text = PyUnicode_AsUTF8AndSize(value, &size);
        if (!text) return NULL;
        if (kind != CETTA_ATOM_STRING && memchr(text, 0, size)) {
            PyErr_SetString(PyExc_ValueError, "a name cannot contain NUL");
            return NULL;
        }
        Py_BEGIN_ALLOW_THREADS
        status = kind == CETTA_ATOM_SYMBOL ? cetta_embed_atom_symbol(engine, text, &atom)
            : kind == CETTA_ATOM_STRING ? cetta_embed_atom_string_n(engine, text, size, &atom)
            : kind == CETTA_ATOM_VARIABLE ? cetta_embed_atom_variable(engine, text, &atom)
            : kind == CETTA_ATOM_BIG_INTEGER ? cetta_embed_atom_integer_decimal(engine, text, &atom)
            : cetta_embed_atom_rational_decimal(engine, text, &atom);
        Py_END_ALLOW_THREADS
    } else if (kind == CETTA_ATOM_INTEGER) {
        long long integer = PyLong_AsLongLong(value);
        if (PyErr_Occurred()) return NULL;
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_atom_integer(engine, integer, &atom);
        Py_END_ALLOW_THREADS
    } else if (kind == CETTA_ATOM_FLOAT) {
        double number = PyFloat_AsDouble(value);
        if (PyErr_Occurred()) return NULL;
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_atom_float(engine, number, &atom);
        Py_END_ALLOW_THREADS
    } else if (kind == CETTA_ATOM_BOOLEAN) {
        int truth = PyObject_IsTrue(value);
        if (truth < 0) return NULL;
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_atom_boolean(engine, truth, &atom);
        Py_END_ALLOW_THREADS
    }
    return status == CETTA_OK ? atom_object(atom) : status_error(module, status);
}
static PyObject *expression(PyObject *module, PyObject *args) {
    (void)module;
    PyObject *object, *sequence;
    if (!PyArg_ParseTuple(args, "OO", &object, &sequence)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    PyObject *items = PySequence_Fast(sequence, "expression children must be iterable");
    if (!items) return NULL;
    Py_ssize_t count = PySequence_Fast_GET_SIZE(items);
    cetta_atom_t **children = PyMem_Calloc(count ? count : 1, sizeof(*children));
    if (!children) { Py_DECREF(items); return PyErr_NoMemory(); }
    cetta_status_t status = CETTA_OK;
    cetta_atom_t *atom = NULL;
    for (Py_ssize_t i = 0; i < count; ++i) {
        children[i] = PyCapsule_GetPointer(PySequence_Fast_GET_ITEM(items, i), atom_capsule);
        if (!children[i]) { status = CETTA_INVALID_ARGUMENT; break; }
    }
    if (status == CETTA_OK) {
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_atom_expression(engine, children, count, &atom);
        Py_END_ALLOW_THREADS
    }
    PyMem_Free(children);
    Py_DECREF(items);
    if (PyErr_Occurred()) return NULL;
    return status == CETTA_OK ? atom_object(atom) : status_error(module, status);
}

static PyObject *format_atom(PyObject *module, PyObject *object) {
    (void)module;
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule);
    if (!atom) return NULL;
    size_t length;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_atom_format(atom, NULL, 0, &length);
    Py_END_ALLOW_THREADS
    if (status != CETTA_OK) return status_error(module, status);
    char *text = PyMem_Malloc(length);
    if (!text) return PyErr_NoMemory();
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_atom_format(atom, text, length, &length);
    Py_END_ALLOW_THREADS
    PyObject *result = status == CETTA_OK ? PyUnicode_DecodeUTF8(text, length - 1, "strict") : status_error(module, status);
    PyMem_Free(text);
    return result;
}
static PyObject *inspect_atom(PyObject *module, PyObject *object) {
    (void)module;
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule);
    if (!atom) return NULL;
    cetta_atom_kind_t kind = cetta_embed_atom_kind(atom);
    PyObject *value = NULL;
    if (kind == CETTA_ATOM_INTEGER) {
        int64_t number;
        if (cetta_embed_atom_get_integer(atom, &number)) return status_error(module, CETTA_ENGINE_ERROR);
        value = PyLong_FromLongLong(number);
    } else if (kind == CETTA_ATOM_FLOAT) {
        double number;
        if (cetta_embed_atom_get_float(atom, &number)) return status_error(module, CETTA_ENGINE_ERROR);
        value = PyFloat_FromDouble(number);
    } else if (kind == CETTA_ATOM_BOOLEAN) {
        int boolean;
        if (cetta_embed_atom_get_boolean(atom, &boolean)) return status_error(module, CETTA_ENGINE_ERROR);
        value = PyBool_FromLong(boolean);
    } else if (kind == CETTA_ATOM_EXPRESSION) {
        size_t count = cetta_embed_atom_arity(atom);
        value = PyTuple_New(count);
        if (!value) return NULL;
        for (size_t i = 0; i < count; ++i) {
            cetta_atom_t *child = NULL;
            cetta_status_t status = cetta_embed_atom_child(atom, i, &child);
            PyObject *item = status == CETTA_OK ? atom_object(child) : status_error(module, status);
            if (!item) { Py_DECREF(value); return NULL; }
            PyTuple_SET_ITEM(value, i, item);
        }
    } else if (kind != CETTA_ATOM_RESOURCE) {
        size_t length;
        cetta_status_t status;
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_atom_get_text(atom, NULL, 0, &length);
        Py_END_ALLOW_THREADS
        if (status != CETTA_OK) return status_error(module, status);
        char *text = PyMem_Malloc(length ? length : 1);
        if (!text) return PyErr_NoMemory();
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_atom_get_text(atom, text, length, &length);
        Py_END_ALLOW_THREADS
        value = status == CETTA_OK ? PyUnicode_DecodeUTF8(text, length, "strict") : status_error(module, status);
        PyMem_Free(text);
    } else { value = Py_NewRef(Py_None); }
    if (!value) return NULL;
    return Py_BuildValue("iN", kind, value);
}
static PyObject *equal_atom(PyObject *module, PyObject *args) {
    (void)module;
    PyObject *left, *right;
    if (!PyArg_ParseTuple(args, "OO", &left, &right)) return NULL;
    cetta_atom_t *a = PyCapsule_GetPointer(left, atom_capsule);
    if (!a) return NULL;
    cetta_atom_t *b = PyCapsule_GetPointer(right, atom_capsule);
    if (!b) return NULL;
    int equal;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_atom_equal(a, b, &equal);
    Py_END_ALLOW_THREADS
    return status == CETTA_OK ? PyBool_FromLong(equal) : status_error(module, status);
}
static PyObject *kind_atom(PyObject *module, PyObject *object) {
    (void)module;
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule);
    return atom ? PyLong_FromLong(cetta_embed_atom_kind(atom)) : NULL;
}
static PyObject *arity_atom(PyObject *module, PyObject *object) {
    (void)module;
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule);
    return atom ? PyLong_FromSize_t(cetta_embed_atom_arity(atom)) : NULL;
}
static PyObject *text_length_atom(PyObject *module, PyObject *object) {
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule);
    if (!atom) return NULL;
    size_t length;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_atom_get_text(atom, NULL, 0, &length);
    Py_END_ALLOW_THREADS
    return status == CETTA_OK ? PyLong_FromSize_t(length) : status_error(module, status);
}
static PyObject *child_atom(PyObject *module, PyObject *args) {
    PyObject *object;
    Py_ssize_t index;
    if (!PyArg_ParseTuple(args, "On", &object, &index)) return NULL;
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule), *child = NULL;
    if (!atom) return NULL;
    cetta_status_t status = index < 0 ? CETTA_INVALID_ARGUMENT : cetta_embed_atom_child(atom, index, &child);
    return status == CETTA_OK ? atom_object(child) : status_error(module, status);
}
static PyObject *result_object(PyObject *module, cetta_status_t status, cetta_result_t *result) {
    if (PyErr_Occurred()) { release_result(result); return NULL; }
    if (!result) return status_error(module, status);
    size_t count = cetta_embed_result_count(result), group_count = cetta_embed_result_group_count(result);
    PyObject *answers = PyList_New(count), *groups = PyList_New(group_count);
    if (!answers || !groups) goto failure;
    for (size_t i = 0; i < count; ++i) {
        cetta_atom_t *atom = NULL;
        cetta_status_t got = cetta_embed_result_get(result, i, &atom);
        PyObject *value = got == CETTA_OK ? atom_object(atom) : status_error(module, got);
        if (!value) goto failure;
        PyList_SET_ITEM(answers, i, value);
    }
    for (size_t i = 0; i < group_count; ++i) {
        PyObject *size = PyLong_FromSize_t(cetta_embed_result_group_size(result, i));
        if (!size) goto failure;
        PyList_SET_ITEM(groups, i, size);
    }
    cetta_diagnostic_t details = {.size = sizeof(details)};
    if (cetta_embed_result_diagnostic(result, &details) != CETTA_OK) goto failure;
    PyObject *diagnostic = diagnostic_object(&details);
    if (!diagnostic) goto failure;
    release_result(result);
    return Py_BuildValue("sNNN", cetta_embed_status_name(status), answers, groups, diagnostic);
failure:
    Py_XDECREF(answers);
    Py_XDECREF(groups);
    release_result(result);
    return NULL;
}

static PyObject *execute(PyObject *module, PyObject *args) {
    (void)module;
    PyObject *object, *input;
    int document;
    long long fuel;
    if (!PyArg_ParseTuple(args, "OOiL", &object, &input, &document, &fuel)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    cetta_eval_options_t options = {sizeof(options), fuel};
    cetta_result_t *result = NULL;
    cetta_status_t status;
    if (document) {
        Py_ssize_t length;
        const char *text = PyUnicode_AsUTF8AndSize(input, &length);
        if (!text) return NULL;
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_run(engine, text, length, &options, &result);
        Py_END_ALLOW_THREADS
    } else {
        cetta_atom_t *atom = PyCapsule_GetPointer(input, atom_capsule);
        if (!atom) return NULL;
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_eval(engine, atom, &options, &result);
        Py_END_ALLOW_THREADS
    }
    return result_object(module, status, result);
}

static PyObject *load_file(PyObject *module, PyObject *args) {
    PyObject *owner, *path;
    long long fuel;
    if (!PyArg_ParseTuple(args, "OOL", &owner, &path, &fuel)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(owner, engine_capsule);
    if (!engine) return NULL;
    PyObject *filename = PyOS_FSPath(path), *encoded = NULL;
    if (!filename) return NULL;
    if (!PyUnicode_FSConverter(filename, &encoded)) { Py_DECREF(filename); return NULL; }
    const char *name = PyBytes_AS_STRING(encoded);
    if (memchr(name, 0, (size_t)PyBytes_GET_SIZE(encoded))) {
        Py_DECREF(encoded); Py_DECREF(filename);
        PyErr_SetString(PyExc_ValueError, "file path contains an embedded NUL");
        return NULL;
    }
    cetta_eval_options_t options = {sizeof(options), fuel};
    cetta_result_t *result = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_load(engine, name, &options, &result);
    Py_END_ALLOW_THREADS
    Py_DECREF(encoded);
    if (PyErr_CheckSignals() < 0 || PyErr_Occurred()) {
        release_result(result); Py_DECREF(filename); return NULL;
    }
    if (status == CETTA_IO_ERROR) {
        int captured = cetta_embed_result_system_error(result);
        release_result(result);
        int previous = errno;
        errno = captured ? captured : EIO;
        PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, filename);
        errno = previous;
        Py_DECREF(filename);
        return NULL;
    }
    Py_DECREF(filename);
    return result_object(module, status, result);
}

static PyObject *import_module(PyObject *module, PyObject *args) {
    PyObject *owner, *target, *spec_object;
    long long fuel;
    if (!PyArg_ParseTuple(args, "OOOL", &owner, &target, &spec_object, &fuel)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(owner, engine_capsule);
    cetta_atom_t *spec = PyCapsule_GetPointer(spec_object, atom_capsule);
    cetta_atom_t *space = target == Py_None ? NULL : PyCapsule_GetPointer(target, atom_capsule);
    if (!engine || !spec || (target != Py_None && !space)) return NULL;
    cetta_eval_options_t options = {sizeof(options), fuel};
    cetta_result_t *result = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_import(engine, space, spec, &options, &result);
    Py_END_ALLOW_THREADS
    return result_object(module, status, result);
}

static PyObject *call_values(PyObject *module, PyObject *args) {
    PyObject *object, *head_object, *sequence;
    long long fuel;
    if (!PyArg_ParseTuple(args, "OOOL", &object, &head_object, &sequence, &fuel)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    cetta_atom_t *head = PyCapsule_GetPointer(head_object, atom_capsule);
    if (!head) return NULL;
    PyObject *items = PySequence_Fast(sequence, "call arguments must be iterable");
    if (!items) return NULL;
    Py_ssize_t count = PySequence_Fast_GET_SIZE(items);
    cetta_atom_t **arguments = PyMem_Calloc(count ? count : 1, sizeof(*arguments));
    if (!arguments) { Py_DECREF(items); return PyErr_NoMemory(); }
    bool valid = true;
    for (Py_ssize_t i = 0; i < count; ++i) {
        arguments[i] = PyCapsule_GetPointer(PySequence_Fast_GET_ITEM(items, i), atom_capsule);
        if (!arguments[i]) { valid = false; break; }
    }
    cetta_result_t *result = NULL;
    cetta_status_t status = CETTA_INVALID_ARGUMENT;
    if (valid) {
        cetta_eval_options_t options = {sizeof(options), fuel};
        Py_BEGIN_ALLOW_THREADS
        status = cetta_embed_call(engine, head, arguments, count, &options, &result);
        Py_END_ALLOW_THREADS
    }
    PyMem_Free(arguments);
    Py_DECREF(items);
    if (!valid) return NULL;
    return result_object(module, status, result);
}

static PyObject *query_open(PyObject *module, PyObject *args) {
    PyObject *engine_object, *source_object;
    long long fuel;
    if (!PyArg_ParseTuple(args, "OOL", &engine_object, &source_object, &fuel)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(engine_object, engine_capsule);
    cetta_atom_t *source = PyCapsule_GetPointer(source_object, atom_capsule);
    if (!engine || !source) return NULL;
    cetta_query_t *query = NULL;
    cetta_eval_options_t options = {sizeof(options), fuel};
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_query_open(engine, source, &options, &query);
    Py_END_ALLOW_THREADS
    if (status != CETTA_OK) return status_error(module, status);
    PyObject *capsule = PyCapsule_New(query, query_capsule, query_destroy);
    if (!capsule) {
        Py_BEGIN_ALLOW_THREADS
        cetta_embed_query_release(query);
        Py_END_ALLOW_THREADS
    }
    return capsule;
}
static PyObject *query_cancel(PyObject *module, PyObject *object) {
    (void)module;
    cetta_query_t *query = PyCapsule_GetPointer(object, query_capsule);
    if (!query) return NULL;
    cetta_embed_query_cancel(query);
    Py_RETURN_NONE;
}
static PyObject *query_close(PyObject *module, PyObject *object) {
    (void)module;
    cetta_query_t *query = PyCapsule_GetPointer(object, query_capsule);
    if (!query) return NULL;
    Py_BEGIN_ALLOW_THREADS
    cetta_embed_query_close(query);
    Py_END_ALLOW_THREADS
    if (PyErr_Occurred()) return NULL;
    Py_RETURN_NONE;
}
static PyObject *query_step(PyObject *module, PyObject *args) {
    PyObject *object;
    unsigned long long allowance;
    if (!PyArg_ParseTuple(args, "OK", &object, &allowance)) return NULL;
    cetta_query_t *query = PyCapsule_GetPointer(object, query_capsule);
    if (!query) return NULL;
    cetta_query_step_t info = {.size = sizeof(info)};
    cetta_result_t *result = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_query_step(query, allowance, &info, &result);
    Py_END_ALLOW_THREADS
    if (PyErr_Occurred() || PyErr_CheckSignals() < 0) {
        release_result(result);
        if (info.bindings) release_atom(info.bindings);
        if (info.delayed) release_atom(info.delayed);
        return NULL;
    }
    PyObject *bindings = info.bindings ? atom_object(info.bindings) : Py_NewRef(Py_None);
    PyObject *delayed = info.delayed ? atom_object(info.delayed) : Py_NewRef(Py_None);
    PyObject *remaining = info.source_limited
        ? PyLong_FromUnsignedLongLong(info.source_remaining) : Py_NewRef(Py_None);
    PyObject *answers = result_object(module, status, result);
    if (!bindings || !delayed || !remaining || !answers) {
        Py_XDECREF(bindings); Py_XDECREF(delayed); Py_XDECREF(remaining); Py_XDECREF(answers);
        return NULL;
    }
    return Py_BuildValue("NiKKNNNi", answers, info.state, info.physical_steps,
        info.source_steps, remaining, bindings, delayed, (int)info.conditional);
}

static PyObject *space_new(PyObject *module, PyObject *args) {
    PyObject *object;
    int root;
    if (!PyArg_ParseTuple(args, "Op", &object, &root)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(object, engine_capsule);
    if (!engine) return NULL;
    cetta_space_t *space = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = root ? cetta_embed_engine_space(engine, &space) : cetta_embed_space_new(engine, &space);
    Py_END_ALLOW_THREADS
    return status == CETTA_OK ? space_object(space) : status_error(module, status);
}
static PyObject *space_from_atom(PyObject *module, PyObject *args) {
    PyObject *owner, *object;
    if (!PyArg_ParseTuple(args, "OO", &owner, &object)) return NULL;
    cetta_engine_t *engine = PyCapsule_GetPointer(owner, engine_capsule);
    cetta_atom_t *atom = PyCapsule_GetPointer(object, atom_capsule);
    if (!engine || !atom) return NULL;
    cetta_space_t *space = NULL;
    cetta_status_t status = cetta_embed_space_from_atom(engine, atom, &space);
    return status == CETTA_OK ? space_object(space) : status_error(module, status);
}
static PyObject *space_as_atom(PyObject *module, PyObject *object) {
    cetta_space_t *space = PyCapsule_GetPointer(object, space_capsule);
    if (!space) return NULL;
    cetta_atom_t *atom = NULL;
    cetta_status_t status = cetta_embed_space_atom(space, &atom);
    return status == CETTA_OK ? atom_object(atom) : status_error(module, status);
}
static PyObject *space_count(PyObject *module, PyObject *object) {
    cetta_space_t *space = PyCapsule_GetPointer(object, space_capsule);
    if (!space) return NULL;
    uint64_t count = 0;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_space_count(space, &count);
    Py_END_ALLOW_THREADS
    return status == CETTA_OK ? PyLong_FromUnsignedLongLong(count) : status_error(module, status);
}
static PyObject *space_mutate(PyObject *module, PyObject *args) {
    PyObject *object, *value;
    int remove, removed = 0;
    if (!PyArg_ParseTuple(args, "OOp", &object, &value, &remove)) return NULL;
    cetta_space_t *space = PyCapsule_GetPointer(object, space_capsule);
    cetta_atom_t *fact = PyCapsule_GetPointer(value, atom_capsule);
    if (!space || !fact) return NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = remove ? cetta_embed_space_remove_atom(space, fact, &removed) : cetta_embed_space_add_atom(space, fact);
    Py_END_ALLOW_THREADS
    if (status != CETTA_OK) return status_error(module, status);
    if (remove) return PyBool_FromLong(removed);
    Py_RETURN_NONE;
}
static PyObject *space_match(PyObject *module, PyObject *args) {
    PyObject *object, *pattern_object, *template_object;
    long long fuel;
    if (!PyArg_ParseTuple(args, "OOOL", &object, &pattern_object, &template_object, &fuel)) return NULL;
    cetta_space_t *space = PyCapsule_GetPointer(object, space_capsule);
    cetta_atom_t *pattern = PyCapsule_GetPointer(pattern_object, atom_capsule);
    cetta_atom_t *template_value = template_object == Py_None ? NULL
        : PyCapsule_GetPointer(template_object, atom_capsule);
    if (!space || !pattern || (template_object != Py_None && !template_value)) return NULL;
    cetta_eval_options_t options = {sizeof(options), fuel};
    cetta_query_t *query = NULL;
    cetta_status_t status;
    Py_BEGIN_ALLOW_THREADS
    status = cetta_embed_space_match(space, pattern, template_value, &options, &query);
    Py_END_ALLOW_THREADS
    if (status != CETTA_OK) return status_error(module, status);
    PyObject *capsule = PyCapsule_New(query, query_capsule, query_destroy);
    if (!capsule) {
        Py_BEGIN_ALLOW_THREADS
        cetta_embed_query_release(query);
        Py_END_ALLOW_THREADS
    }
    return capsule;
}

#include "callbacks.inc"
#include "host.inc"
#include "buffers.inc"

static PyMethodDef methods[] = {
    {"buffer_from_bytes", buffer_from_bytes, METH_VARARGS, NULL},
    {"buffer_at_address", buffer_at_address, METH_VARARGS, NULL},
    {"buffer_atom", buffer_atom, METH_VARARGS, NULL},
    {"atom_buffer", atom_buffer, METH_O, NULL},
    {"sparse_export", sparse_export, METH_O, NULL},
    {"tensor_from_sparse", tensor_from_sparse, METH_VARARGS, NULL},
    {"register_callback", register_callback, METH_VARARGS, NULL},
    {"space", space_new, METH_VARARGS, NULL},
    {"space_from_atom", space_from_atom, METH_VARARGS, NULL},
    {"space_atom", space_as_atom, METH_O, NULL},
    {"space_count", space_count, METH_O, NULL},
    {"space_mutate", space_mutate, METH_VARARGS, NULL},
    {"space_match", space_match, METH_VARARGS, NULL},
    {"query_open", query_open, METH_VARARGS, NULL},
    {"query_step", query_step, METH_VARARGS, NULL},
    {"query_cancel", query_cancel, METH_O, NULL},
    {"query_close", query_close, METH_O, NULL},
    {"engine", engine_new, METH_VARARGS, NULL},
    {"close", engine_close, METH_O, NULL},
    {"parse", parse, METH_VARARGS, NULL},
    {"scalar", scalar, METH_VARARGS, NULL},
    {"expression", expression, METH_VARARGS, NULL},
    {"format", format_atom, METH_O, NULL},
    {"inspect", inspect_atom, METH_O, NULL},
    {"equal", equal_atom, METH_VARARGS, NULL},
    {"kind", kind_atom, METH_O, NULL},
    {"arity", arity_atom, METH_O, NULL},
    {"text_length", text_length_atom, METH_O, NULL},
    {"child", child_atom, METH_VARARGS, NULL},
    {"execute", execute, METH_VARARGS, NULL},
    {"load", load_file, METH_VARARGS, NULL},
    {"import_module", import_module, METH_VARARGS, NULL},
    {"call", call_values, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}
};
static int module_traverse(PyObject *module, visitproc visit, void *arg) {
    NativeState *state = PyModule_GetState(module);
    Py_VISIT(state->error);
    Py_VISIT(state->buffer_type);
    return 0;
}
static int module_clear(PyObject *module) {
    NativeState *state = PyModule_GetState(module);
    Py_CLEAR(state->error);
    Py_CLEAR(state->buffer_type);
    return 0;
}
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_native", NULL, sizeof(NativeState), methods,
    NULL, module_traverse, module_clear, NULL
};
PyMODINIT_FUNC PyInit__native(void) {
    PyObject *created = PyModule_Create(&module);
    if (!created) return NULL;
    NativeState *state = PyModule_GetState(created);
    state->error = PyErr_NewException("cetta._native.Error", PyExc_RuntimeError, NULL);
    if (!state->error || PyModule_AddObjectRef(created, "Error", state->error)) {
        Py_DECREF(created);
        return NULL;
    }
    state->buffer_type = PyType_FromModuleAndSpec(created, &buffer_type_spec, NULL);
    if (!state->buffer_type || PyModule_AddObjectRef(created, "Buffer", state->buffer_type)) {
        Py_DECREF(created);
        return NULL;
    }
    return created;
}
