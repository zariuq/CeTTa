/* Object operations shared by executable-owned and Python-owned interpreters. */
static const char *const python_object_operations[] = {
    "py:import", "py:get", "py:set!", "py:del!", "py:item", "py:set-item!",
    "py:del-item!", "py:call", "py:kwargs", "py:list", "py:tuple", "py:dict",
    "py:slice", "py:none", "py:is", "py:eq", "py:to-tuple", "py:close!", "py:bind!", "py:iter", "py:with"
};

void cetta_foreign_enable_objects(CettaForeignRuntime *rt, Registry *registry) {
    if (rt) {
        /* Import introduces these callable names. A managed symbol parsed
         * before import can then resolve its spelling on every execution
         * tier, without registering arbitrary data when it is copied. */
        for (size_t i = 0u; i < sizeof(python_object_operations) / sizeof(*python_object_operations); i++)
            (void)symbol_intern_cstr(g_symbols, python_object_operations[i]);
        rt->objects_enabled = true;
        if (!rt->object_registry) rt->object_registry = registry;
    }
}

bool cetta_foreign_object_head(CettaForeignRuntime *rt, const char *name) {
    if (!rt || !rt->objects_enabled || !name) return false;
    if (!strncmp(name, "py:", 3u))
        for (size_t i = 0; i < sizeof(python_object_operations) / sizeof(*python_object_operations); i++)
            if (!strcmp(name, python_object_operations[i])) return true;
    for (PythonBinding *b = rt->bindings; b; b = b->next)
        if (!strcmp(name, b->name)) return true;
    return false;
}

static bool python_object_head_at(CettaForeignRuntime *rt, Space *space, const char *name) {
    if (!rt || !rt->objects_enabled || !name) return false;
    if (!strncmp(name, "py:", 3u))
        for (size_t i = 0; i < sizeof(python_object_operations) / sizeof(*python_object_operations); i++)
            if (!strcmp(name, python_object_operations[i])) return true;
    for (PythonBinding *b = rt->bindings; b; b = b->next)
        if ((!space || b->space == space) && !strcmp(name, b->name)) return true;
    return false;
}

static PyObject *python_object_dictionary(Arena *arena, Atom **args, uint32_t count, bool keywords) {
    PyObject *dict = PyDict_New();
    if (!dict) return NULL;
    for (uint32_t i = 0; i < count; i++) {
        Atom *pair = args[i];
        if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2u) {
            PyErr_SetString(PyExc_TypeError, "dictionary entries must be (key value) pairs");
            Py_DECREF(dict); return NULL;
        }
        PyObject *key = python_from_atom_policy(arena, pair->expr.elems[0], true, true);
        PyObject *value = key ? python_from_atom_policy(arena, pair->expr.elems[1], true, true) : NULL;
        int exists = value && keywords ? PyDict_Contains(dict, key) : 0;
        if (value && keywords && (!PyUnicode_Check(key) || exists > 0))
            PyErr_SetString(PyExc_TypeError, "keyword names must be distinct strings");
        bool ok = value && !PyErr_Occurred() && PyDict_SetItem(dict, key, value) == 0;
        Py_XDECREF(key); Py_XDECREF(value);
        if (!ok) { Py_DECREF(dict); return NULL; }
    }
    return dict;
}

static bool python_same_callable(PyObject *left, PyObject *right) {
    if (left == right) return true;
    if (PyMethod_Check(left) && PyMethod_Check(right))
        return PyMethod_GET_FUNCTION(left) == PyMethod_GET_FUNCTION(right) &&
               PyMethod_GET_SELF(left) == PyMethod_GET_SELF(right);
    if (PyCFunction_Check(left) && PyCFunction_Check(right))
        return PyCFunction_GetSelf(left) == PyCFunction_GetSelf(right) &&
               PyCFunction_GetFunction(left) == PyCFunction_GetFunction(right) &&
               PyCFunction_GetFlags(left) == PyCFunction_GetFlags(right);
    return false;
}

static bool python_objects_call(CettaForeignRuntime *rt, Space *space, Arena *arena,
        Atom *head, Atom **args, uint32_t count, CettaCallOutcome *out) {
    Atom *error = NULL;
    __attribute__((cleanup(python_execution_guard_leave))) PythonExecutionGuard guard = {0};
    if (!python_execution_guard_enter(&guard, arena, &error)) {
        *out = cetta_call_raised(error); return true;
    }
    const char *name = atom_name_cstr(head);
    PyObject *result = NULL, *x = NULL, *y = NULL, *z = NULL;
    bool boxed = false;
    for (PythonBinding *b = rt->bindings; b; b = b->next) {
        if (b->space == space && !strcmp(name, b->name)) {
            result = python_invoke(arena, b->callable->obj, true, true, args, count);
            goto finish;
        }
    }
#define OP(word, n) (!strcmp(name, "py:" word) && count == (n))
    if (OP("none", 0)) { result = Py_NewRef(Py_None); boxed = true; }
    else if (OP("import", 1)) {
        x = python_from_atom_policy(arena, args[0], true, true);
        if (x && PyUnicode_Check(x)) result = PyImport_Import(x);
        else if (!PyErr_Occurred()) PyErr_SetString(PyExc_TypeError, "module name must be a string or symbol");
    } else if (!strcmp(name, "py:kwargs") || !strcmp(name, "py:dict")) {
        result = python_object_dictionary(arena, args, count, !strcmp(name, "py:kwargs"));
        boxed = true;
    } else if (!strcmp(name, "py:list") || !strcmp(name, "py:tuple")) {
        result = PyTuple_New(count);
        for (uint32_t i = 0; result && i < count; i++) {
            PyObject *item = python_from_atom_policy(arena, args[i], true, true);
            if (!item) { Py_CLEAR(result); break; }
            PyTuple_SET_ITEM(result, i, item);
        }
        if (result && !strcmp(name, "py:list")) {
            PyObject *list = PySequence_List(result); Py_DECREF(result); result = list;
        }
        boxed = true;
    } else if (!strcmp(name, "py:call") && count) {
        x = python_from_atom_policy(arena, args[0], true, true);
        if (x) result = python_invoke(arena, x, true, true, args + 1, count - 1);
    } else if (OP("with", 2)) {
        x = python_from_atom_policy(arena, args[0], true, true);
        Atom *expression = x ? python_context_expression(rt, space, arena, x, args[1]) : NULL;
        if (expression) { *out = cetta_call_value(expression); goto cleanup; }
    } else if (OP("bind!", 3)) {
        const char *binding = string_like_atom(args[1]);
        if (binding && args[1]->kind == ATOM_GROUNDED && args[1]->ground.gkind == GV_STRING &&
            strlen(binding) != args[1]->ground.slen) binding = NULL;
        Space *target = args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_SPACE
            ? args[0]->ground.ptr : atom_is_symbol(args[0], "&self") ? space : NULL;
        x = python_from_atom_policy(arena, args[2], true, true);
        if (!target || !binding || !*binding || !x || !PyCallable_Check(x)) {
            if (!PyErr_Occurred()) PyErr_SetString(PyExc_TypeError, "py:bind! requires a space, name and callable");
            goto finish;
        }
        for (PythonBinding *b = rt->bindings; b; b = b->next) {
            if (b->space != target || strcmp(b->name, binding)) continue;
            if (b->callable->obj && python_same_callable(b->callable->obj, x)) result = Py_NewRef(Py_True);
            else PyErr_SetString(PyExc_ValueError, "Python binding already exists");
            goto finish;
        }
        SymbolId name_id = symbol_intern_cstr(g_symbols, binding);
        if (space_equations_may_match_known_head(target, name_id) ||
            python_object_head_at(rt, target, binding) ||
            (is_grounded_op(name_id) && !cetta_foreign_object_head(rt, binding))) {
            PyErr_SetString(PyExc_ValueError, "name already has an interpretation"); goto finish;
        }
        PythonBinding *b = calloc(1, sizeof(*b));
        if (!b) { PyErr_NoMemory(); goto finish; }
        b->name = strdup(binding);
        if (!b->name) { free(b); PyErr_NoMemory(); goto finish; }
        b->space = target;
        b->space_hold = space_lifetime_pin ? space_lifetime_pin(target) : NULL;
        b->callable = foreign_new_python_value(rt, x, true, true);
        if (!b->callable) {
            python_space_hold_release(&b->space_hold);
            free(b->name); free(b); goto finish;
        }
        b->callable->object_mode = true;
        foreign_record_retain(b->callable);
        b->next = rt->bindings; rt->bindings = b;
        result = Py_NewRef(Py_True);
    } else {
        if (count > 0) x = python_from_atom_policy(arena, args[0], true, true);
        if (count > 1 && x) y = python_from_atom_policy(arena, args[1], true, true);
        if (count > 2 && y) z = python_from_atom_policy(arena, args[2], true, true);
        if (PyErr_Occurred()) goto finish;
        if (OP("iter", 1)) {
            Atom *expression = python_iterator_expression(rt, arena, x);
            if (expression) { *out = cetta_call_value(expression); goto cleanup; }
        }
        else if (OP("get", 2)) result = PyObject_GetAttr(x, y);
        else if (OP("item", 2)) result = PyObject_GetItem(x, y);
        else if (OP("is", 2)) result = PyBool_FromLong(x == y);
        else if (OP("eq", 2)) result = PyObject_RichCompare(x, y, Py_EQ);
        else if (OP("set!", 3)) { if (PyObject_SetAttr(x, y, z) == 0) result = Py_NewRef(Py_None); }
        else if (OP("del!", 2)) { if (PyObject_DelAttr(x, y) == 0) result = Py_NewRef(Py_None); }
        else if (OP("set-item!", 3)) { if (PyObject_SetItem(x, y, z) == 0) result = Py_NewRef(Py_None); }
        else if (OP("del-item!", 2)) { if (PyObject_DelItem(x, y) == 0) result = Py_NewRef(Py_None); }
        else if (OP("slice", 1)) result = PySlice_New(Py_None, x, Py_None);
        else if (OP("slice", 2) || OP("slice", 3)) result = PySlice_New(x, y, z);
        else if (OP("close!", 1)) result = PyObject_CallMethod(x, "close", NULL);
        else if (OP("to-tuple", 1)) {
            PyObject *seq = PySequence_Fast(x, "expected a Python iterable");
            if (!seq) goto finish;
            Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
            if ((uint64_t)n > UINT32_MAX) { Py_DECREF(seq); PyErr_NoMemory(); goto finish; }
            Atom **items = arena_alloc(arena, sizeof(*items) * (size_t)n);
            if (n && !items) { Py_DECREF(seq); PyErr_NoMemory(); goto finish; }
            bool ok = true;
            for (Py_ssize_t i = 0; i < n; i++) {
                ResultSet one; result_set_init(&one);
                ok = python_emit_object(rt, arena, PySequence_Fast_GET_ITEM(seq, i), &one, &error);
                items[i] = ok && one.len == 1 ? one.items[0] : NULL;
                result_set_free(&one);
                if (!ok) break;
            }
            Py_DECREF(seq);
            if (ok) { *out = cetta_call_completed_value(atom_expr(arena, items, (CettaExprLen)n)); goto cleanup; }
        } else PyErr_SetString(PyExc_TypeError, "incorrect Python operation arguments");
    }
finish:
    if (result) {
        ResultSet one; result_set_init(&one);
        bool ok;
        if (boxed) {
            Atom *value = cetta_foreign_object_wrap(rt, arena, result);
            ok = value != NULL;
            if (ok) result_set_add(&one, value);
        }
        else ok = python_emit_object(rt, arena, result, &one, &error);
        if (ok && one.len == 1) {
            if (!strcmp(name, "py:kwargs")) foreign_python_record(one.items[0])->keywords = true;
            *out = cetta_call_completed_value(one.items[0]);
        } else *out = cetta_call_raised(error ? error : python_object_error(rt, arena, "Python result conversion"));
        result_set_free(&one);
    } else *out = cetta_call_raised(error ? error : python_object_error(rt, arena, "Python operation"));
cleanup:
    Py_XDECREF(result); Py_XDECREF(x); Py_XDECREF(y); Py_XDECREF(z);
    return true;
#undef OP
}
