#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "foreign.h"

#include "parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct CettaForeignValue {
    CettaForeignBackendKind backend;
    bool callable;
    bool unwrap;
    PyObject *obj;
    struct CettaForeignValue *next;
} CettaForeignValue;

struct CettaForeignRuntime {
    CettaForeignValue *values;
};

static bool g_python_bootstrap_ready = false;
static bool g_python_inittab_ready = false;
static PyObject *g_bridge_module = NULL;
static PyObject *g_bridge_cetta_atom_class = NULL;
static PyObject *g_bridge_operation_atom_class = NULL;
static PyObject *g_bridge_value_atom_class = NULL;
static PyObject *g_bridge_load_module = NULL;
static PyObject *g_bridge_resolve = NULL;
static uint64_t g_bridge_module_counter = 0;
static Space *g_python_callback_space = NULL;

static const char *PYTHON_BOOTSTRAP =
"import sys, types, builtins, importlib, importlib.util, os\n"
"import cetta_bridge as _cetta_bridge\n"
"\n"
"def _split_top_level(text):\n"
"    s = str(text).strip()\n"
"    out = []\n"
"    cur = []\n"
"    depth = 0\n"
"    in_str = False\n"
"    esc = False\n"
"    for ch in s:\n"
"        if in_str:\n"
"            cur.append(ch)\n"
"            if esc:\n"
"                esc = False\n"
"            elif ch == '\\\\':\n"
"                esc = True\n"
"            elif ch == '\"':\n"
"                in_str = False\n"
"            continue\n"
"        if ch == '\"':\n"
"            in_str = True\n"
"            cur.append(ch)\n"
"            continue\n"
"        if ch == '(':\n"
"            depth += 1\n"
"            cur.append(ch)\n"
"            continue\n"
"        if ch == ')':\n"
"            depth -= 1\n"
"            cur.append(ch)\n"
"            continue\n"
"        if depth == 0 and ch.isspace():\n"
"            piece = ''.join(cur).strip()\n"
"            if piece:\n"
"                out.append(piece)\n"
"            cur = []\n"
"            continue\n"
"        cur.append(ch)\n"
"    piece = ''.join(cur).strip()\n"
"    if piece:\n"
"        out.append(piece)\n"
"    return out\n"
"\n"
"class CettaAtom:\n"
"    def __init__(self, text):\n"
"        self.text = str(text).strip()\n"
"    def __str__(self):\n"
"        return self.text\n"
"    __repr__ = __str__\n"
"    def get_name(self):\n"
"        if self.text.startswith('$'):\n"
"            return self.text[1:]\n"
"        return self.text\n"
"    def get_children(self):\n"
"        s = self.text.strip()\n"
"        if len(s) < 2 or s[0] != '(' or s[-1] != ')':\n"
"            return []\n"
"        inner = s[1:-1].strip()\n"
"        if not inner:\n"
"            return []\n"
"        return [CettaAtom(piece) for piece in _split_top_level(inner)]\n"
"\n"
"class OperationAtom:\n"
"    def __init__(self, name, callable_obj, typ=None, unwrap=True):\n"
"        self.name = name\n"
"        self.callable = callable_obj\n"
"        self.typ = typ\n"
"        self.unwrap = unwrap\n"
"\n"
"class ValueAtom:\n"
"    def __init__(self, value, typ=None):\n"
"        self.value = value\n"
"        self.typ = typ\n"
"\n"
"class GroundedObject:\n"
"    def __init__(self, content):\n"
"        self.content = content\n"
"\n"
"class GroundedAtom:\n"
"    def __init__(self, obj):\n"
"        self.obj = obj\n"
"    def get_object(self):\n"
"        return self.obj\n"
"\n"
"class MatchableObject:\n"
"    pass\n"
"\n"
"class NoReduceError(Exception):\n"
"    pass\n"
"\n"
"class AtomType:\n"
"    ATOM = 'Atom'\n"
"    UNDEFINED = '%Undefined%'\n"
"\n"
"class Tokenizer:\n"
"    pass\n"
"\n"
"class SExprParser:\n"
"    pass\n"
"\n"
"class _AtomsHolder:\n"
"    pass\n"
"\n"
"Atoms = _AtomsHolder()\n"
"Atoms.UNIT = CettaAtom('()')\n"
"\n"
"def _decode_string_body(body):\n"
"    return bytes(body, 'utf-8').decode('unicode_escape')\n"
"\n"
"def get_string_value(atom):\n"
"    s = str(atom)\n"
"    if len(s) >= 2 and s[0] == '\"' and s[-1] == '\"':\n"
"        return _decode_string_body(s[1:-1])\n"
"    return s\n"
"\n"
"def S(name):\n"
"    return CettaAtom(str(name))\n"
"\n"
"def V(name):\n"
"    return CettaAtom('$' + str(name))\n"
"\n"
"def E(*args):\n"
"    return CettaAtom('(' + ' '.join(str(a) for a in args) + ')')\n"
"\n"
"def G(x):\n"
"    return x\n"
"\n"
"class MeTTa:\n"
"    def parse_single(self, text):\n"
"        return CettaAtom(text)\n"
"    def parse_all(self, text):\n"
"        return [CettaAtom(piece) for piece in _split_top_level(text)]\n"
"    def run(self, text):\n"
"        return _cetta_bridge.run(text)\n"
"\n"
"class RegisterType:\n"
"    ATOM = 1\n"
"    TOKEN = 2\n"
"\n"
"def _mark_register_function(kind, args, kwargs):\n"
"    if len(args) == 1 and len(kwargs) == 0 and callable(args[0]):\n"
"        func = args[0]\n"
"        func.metta_type = kind\n"
"        func.metta_pass_metta = False\n"
"        return func\n"
"    pass_metta = kwargs.get('pass_metta', False)\n"
"    def decorator(func):\n"
"        func.metta_type = kind\n"
"        func.metta_pass_metta = pass_metta\n"
"        return func\n"
"    return decorator\n"
"\n"
"def register_atoms(*args, **kwargs):\n"
"    return _mark_register_function(RegisterType.ATOM, args, kwargs)\n"
"\n"
"def register_tokens(*args, **kwargs):\n"
"    return _mark_register_function(RegisterType.TOKEN, args, kwargs)\n"
"\n"
"def _cetta_load_module(unique_name, path):\n"
"    if path.endswith('__init__.py'):\n"
"        spec = importlib.util.spec_from_file_location(unique_name, path, submodule_search_locations=[os.path.dirname(path)])\n"
"    else:\n"
"        spec = importlib.util.spec_from_file_location(unique_name, path)\n"
"    module = importlib.util.module_from_spec(spec)\n"
"    sys.modules[unique_name] = module\n"
"    spec.loader.exec_module(module)\n"
"    return module\n"
"\n"
"def _cetta_eval_literal(text):\n"
"    ns = {'__builtins__': builtins.__dict__, 'hyperon': hyperon}\n"
"    return eval(text, ns, ns)\n"
"\n"
"def _cetta_resolve(path, base=None):\n"
"    if base is not None:\n"
"        obj = base\n"
"        for piece in str(path).split('.'):\n"
"            obj = getattr(obj, piece)\n"
"        return obj\n"
"    text = str(path)\n"
"    if text == '':\n"
"        raise ValueError('empty python path')\n"
"    pieces = text.split('.')\n"
"    root = pieces[0]\n"
"    if hasattr(builtins, root):\n"
"        obj = getattr(builtins, root)\n"
"    else:\n"
"        obj = importlib.import_module(root)\n"
"    for piece in pieces[1:]:\n"
"        obj = getattr(obj, piece)\n"
"    return obj\n"
"\n"
"hyperon = types.ModuleType('hyperon')\n"
"hyperon_ext = types.ModuleType('hyperon.ext')\n"
"hyperon_atoms = types.ModuleType('hyperon.atoms')\n"
"hyperon_base = types.ModuleType('hyperon.base')\n"
"hyperonpy = types.ModuleType('hyperonpy')\n"
"hyperonpy.log_error = lambda msg: None\n"
"\n"
"for mod in (hyperon, hyperon_atoms):\n"
"    mod.CettaAtom = CettaAtom\n"
"    mod.OperationAtom = OperationAtom\n"
"    mod.ValueAtom = ValueAtom\n"
"    mod.GroundedObject = GroundedObject\n"
"    mod.GroundedAtom = GroundedAtom\n"
"    mod.MatchableObject = MatchableObject\n"
"    mod.NoReduceError = NoReduceError\n"
"    mod.AtomType = AtomType\n"
"    mod.Tokenizer = Tokenizer\n"
"    mod.SExprParser = SExprParser\n"
"    mod.Atoms = Atoms\n"
"    mod.get_string_value = get_string_value\n"
"    mod.S = S\n"
"    mod.V = V\n"
"    mod.E = E\n"
"    mod.G = G\n"
"    mod.SymbolAtom = CettaAtom\n"
"    mod.VariableAtom = CettaAtom\n"
"    mod.ExpressionAtom = CettaAtom\n"
"    mod.MettaAtom = CettaAtom\n"
"\n"
"hyperon.Metta = MeTTa\n"
"hyperon.MeTTa = MeTTa\n"
"hyperon.register_atoms = register_atoms\n"
"hyperon.register_tokens = register_tokens\n"
"hyperon.ext = hyperon_ext\n"
"hyperon.atoms = hyperon_atoms\n"
"hyperon.base = hyperon_base\n"
"\n"
"hyperon_ext.register_atoms = register_atoms\n"
"hyperon_ext.register_tokens = register_tokens\n"
"hyperon_base.Tokenizer = Tokenizer\n"
"hyperon_base.SExprParser = SExprParser\n"
"\n"
"sys.modules['hyperon'] = hyperon\n"
"sys.modules['hyperon.ext'] = hyperon_ext\n"
"sys.modules['hyperon.atoms'] = hyperon_atoms\n"
"sys.modules['hyperon.base'] = hyperon_base\n"
"sys.modules['hyperonpy'] = hyperonpy\n"
"\n"
"_cetta_bridge.CettaAtom = CettaAtom\n"
"_cetta_bridge.OperationAtom = OperationAtom\n"
"_cetta_bridge.ValueAtom = ValueAtom\n"
"_cetta_bridge.MeTTa = MeTTa\n"
"_cetta_bridge._cetta_load_module = _cetta_load_module\n"
"_cetta_bridge._cetta_resolve = _cetta_resolve\n";

const char *cetta_module_format_name(CettaModuleFormatKind kind) {
    switch (kind) {
    case CETTA_MODULE_FORMAT_METTA:
        return "metta";
    case CETTA_MODULE_FORMAT_FOREIGN:
        return "foreign";
    }
    return "unknown";
}

const char *cetta_foreign_backend_name(CettaForeignBackendKind kind) {
    switch (kind) {
    case CETTA_FOREIGN_BACKEND_NONE:
        return "none";
    case CETTA_FOREIGN_BACKEND_PYTHON:
        return "python";
    }
    return "unknown";
}

static Atom *foreign_error_atom(Arena *a, const char *message) {
    return atom_symbol(a, message);
}

static Atom *python_error_atom(Arena *a, const char *prefix) {
    PyObject *ptype = NULL;
    PyObject *pvalue = NULL;
    PyObject *ptrace = NULL;
    PyErr_Fetch(&ptype, &pvalue, &ptrace);
    PyErr_NormalizeException(&ptype, &pvalue, &ptrace);
    const char *detail = "";
    PyObject *detail_obj = NULL;
    if (pvalue) {
        detail_obj = PyObject_Str(pvalue);
        if (detail_obj) detail = PyUnicode_AsUTF8(detail_obj);
    }
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s%s%s",
             prefix ? prefix : "python bridge error",
             detail && *detail ? ": " : "",
             detail && *detail ? detail : "");
    Py_XDECREF(detail_obj);
    Py_XDECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptrace);
    return atom_symbol(a, buf);
}

static bool path_has_suffix_ci(const char *path, const char *suffix) {
    size_t plen = strlen(path);
    size_t slen = strlen(suffix);
    if (plen < slen) return false;
    return strcmp(path + plen - slen, suffix) == 0;
}

static bool directory_has_python_entry(const char *path, char *resolved, size_t resolved_sz) {
    char init_path[PATH_MAX];
    int n = snprintf(init_path, sizeof(init_path), "%s/__init__.py", path);
    if (!(n > 0 && (size_t)n < sizeof(init_path))) return false;
    if (access(init_path, R_OK) != 0) return false;
    return realpath(init_path, resolved) != NULL && strlen(resolved) < resolved_sz;
}

bool cetta_foreign_resolve_candidate(const char *candidate,
                                     char *out, size_t out_sz,
                                     CettaModuleFormat *format_out,
                                     char *reason, size_t reason_sz) {
    struct stat st;
    char path[PATH_MAX];
    if (reason && reason_sz > 0) reason[0] = '\0';
    if (!candidate) return false;

    snprintf(path, sizeof(path), "%s", candidate);
    if (!path_has_suffix_ci(path, ".py") && access(path, R_OK) != 0) {
        int n = snprintf(path, sizeof(path), "%s.py", candidate);
        if (!(n > 0 && (size_t)n < sizeof(path))) return false;
    }

    if (path_has_suffix_ci(path, ".py")) {
        if (access(path, R_OK) != 0) return false;
        if (!realpath(path, out)) return false;
        if (format_out) {
            format_out->kind = CETTA_MODULE_FORMAT_FOREIGN;
            format_out->foreign_backend = CETTA_FOREIGN_BACKEND_PYTHON;
        }
        return strlen(out) < out_sz;
    }

    if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (directory_has_python_entry(candidate, out, out_sz)) {
            if (format_out) {
                format_out->kind = CETTA_MODULE_FORMAT_FOREIGN;
                format_out->foreign_backend = CETTA_FOREIGN_BACKEND_PYTHON;
            }
            return true;
        }
        if (reason && reason_sz > 0) {
            snprintf(reason, reason_sz,
                     "unsupported foreign module directory (missing __init__.py)");
        }
    }
    return false;
}

static CettaForeignValue *foreign_new_python_value(CettaForeignRuntime *rt,
                                                   PyObject *obj,
                                                   bool callable,
                                                   bool unwrap) {
    CettaForeignValue *value = cetta_malloc(sizeof(CettaForeignValue));
    value->backend = CETTA_FOREIGN_BACKEND_PYTHON;
    value->callable = callable;
    value->unwrap = unwrap;
    value->obj = obj;
    Py_INCREF(obj);
    value->next = rt->values;
    rt->values = value;
    return value;
}

static void cetta_foreign_runtime_init(CettaForeignRuntime *rt) {
    if (!rt) return;
    rt->values = NULL;
}

CettaForeignRuntime *cetta_foreign_runtime_new(void) {
    CettaForeignRuntime *rt = cetta_malloc(sizeof(CettaForeignRuntime));
    cetta_foreign_runtime_init(rt);
    return rt;
}

void cetta_foreign_runtime_free(CettaForeignRuntime *rt) {
    if (!rt) return;
    CettaForeignValue *cur = rt->values;
    while (cur) {
        CettaForeignValue *next = cur->next;
        Py_XDECREF(cur->obj);
        free(cur);
        cur = next;
    }
    rt->values = NULL;
    free(rt);
}

static PyObject *bridge_run(PyObject *self, PyObject *args) {
    (void)self;
    const char *text = NULL;
    if (!PyArg_ParseTuple(args, "s", &text)) return NULL;
    if (!g_python_callback_space) {
        PyErr_SetString(PyExc_RuntimeError, "MeTTa.run is unavailable outside a foreign call/import context");
        return NULL;
    }
    if (!g_bridge_cetta_atom_class) {
        PyErr_SetString(PyExc_RuntimeError, "CeTTa Python bridge is not initialized");
        return NULL;
    }

    PyObject *outer = PyList_New(0);
    if (!outer) return NULL;

    Arena parse_arena;
    arena_init(&parse_arena);
    size_t pos = 0;
    for (;;) {
        Atom *top = parse_sexpr(&parse_arena, text, &pos);
        if (!top) break;
        if (atom_is_symbol(top, "!")) {
            continue;
        }

        Arena eval_arena;
        arena_init(&eval_arena);
        ResultSet rs;
        result_set_init(&rs);
        eval_top_with_registry(g_python_callback_space, &eval_arena,
                               eval_current_persistent_arena(),
                               eval_current_registry(), top, &rs);

        PyObject *inner = PyList_New(0);
        if (!inner) {
            result_set_free(&rs);
            arena_free(&eval_arena);
            arena_free(&parse_arena);
            Py_DECREF(outer);
            return NULL;
        }

        for (uint32_t i = 0; i < rs.len; i++) {
            char *text_atom = atom_to_string(&eval_arena, rs.items[i]);
            PyObject *py_atom = PyObject_CallFunction(g_bridge_cetta_atom_class, "s", text_atom);
            if (!py_atom || PyList_Append(inner, py_atom) != 0) {
                Py_XDECREF(py_atom);
                Py_DECREF(inner);
                result_set_free(&rs);
                arena_free(&eval_arena);
                arena_free(&parse_arena);
                Py_DECREF(outer);
                return NULL;
            }
            Py_DECREF(py_atom);
        }

        if (PyList_Append(outer, inner) != 0) {
            Py_DECREF(inner);
            result_set_free(&rs);
            arena_free(&eval_arena);
            arena_free(&parse_arena);
            Py_DECREF(outer);
            return NULL;
        }
        Py_DECREF(inner);
        result_set_free(&rs);
        eval_release_temporary_spaces();
        arena_free(&eval_arena);
    }

    arena_free(&parse_arena);
    return outer;
}

static PyMethodDef CETTA_BRIDGE_METHODS[] = {
    {"run", bridge_run, METH_VARARGS, "Run MeTTa code in the current CeTTa callback context."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef CETTA_BRIDGE_MODULE = {
    PyModuleDef_HEAD_INIT,
    "cetta_bridge",
    NULL,
    -1,
    CETTA_BRIDGE_METHODS
};

PyMODINIT_FUNC PyInit_cetta_bridge(void) {
    return PyModule_Create(&CETTA_BRIDGE_MODULE);
}

static bool ensure_python_bridge(Arena *error_arena, Atom **error_out) {
    if (!g_python_inittab_ready) {
        if (PyImport_AppendInittab("cetta_bridge", PyInit_cetta_bridge) == -1) {
            if (error_out) *error_out = foreign_error_atom(error_arena, "failed to register cetta_bridge");
            return false;
        }
        g_python_inittab_ready = true;
    }

    if (!Py_IsInitialized()) {
        Py_Initialize();
    }

    if (g_python_bootstrap_ready) return true;

    if (PyRun_SimpleString(PYTHON_BOOTSTRAP) != 0) {
        if (error_out) *error_out = python_error_atom(error_arena, "python bootstrap failed");
        return false;
    }

    g_bridge_module = PyImport_ImportModule("cetta_bridge");
    if (!g_bridge_module) {
        if (error_out) *error_out = python_error_atom(error_arena, "failed to import cetta_bridge");
        return false;
    }
    g_bridge_cetta_atom_class = PyObject_GetAttrString(g_bridge_module, "CettaAtom");
    g_bridge_operation_atom_class = PyObject_GetAttrString(g_bridge_module, "OperationAtom");
    g_bridge_value_atom_class = PyObject_GetAttrString(g_bridge_module, "ValueAtom");
    g_bridge_load_module = PyObject_GetAttrString(g_bridge_module, "_cetta_load_module");
    g_bridge_resolve = PyObject_GetAttrString(g_bridge_module, "_cetta_resolve");
    if (!g_bridge_cetta_atom_class || !g_bridge_operation_atom_class ||
        !g_bridge_load_module || !g_bridge_resolve) {
        if (error_out) *error_out = python_error_atom(error_arena, "python bootstrap missing bridge helpers");
        return false;
    }

    g_python_bootstrap_ready = true;
    return true;
}

static bool literal_token_name(const char *pattern) {
    if (!pattern || !*pattern) return false;
    for (const unsigned char *p = (const unsigned char *)pattern; *p; p++) {
        switch (*p) {
        case '[': case ']': case '(': case ')': case '{': case '}':
        case '+': case '?': case '*': case '|': case '^': case '$':
        case '\\':
            return false;
        default:
            break;
        }
    }
    return true;
}

static PyObject *call_python_load_module(const char *path, Arena *error_arena, Atom **error_out) {
    char unique_name[128];
    snprintf(unique_name, sizeof(unique_name), "cetta_ext_%llu",
             (unsigned long long)++g_bridge_module_counter);
    PyObject *module = PyObject_CallFunction(g_bridge_load_module, "ss", unique_name, path);
    if (!module) {
        if (error_out) *error_out = python_error_atom(error_arena, "python module load failed");
        return NULL;
    }
    return module;
}

static PyObject *python_make_metta_bridge(Arena *error_arena, Atom **error_out) {
    PyObject *cls = PyObject_GetAttrString(g_bridge_module, "MeTTa");
    if (!cls) {
        if (error_out) *error_out = python_error_atom(error_arena, "failed to fetch bridge MeTTa class");
        return NULL;
    }
    PyObject *instance = PyObject_CallObject(cls, NULL);
    Py_DECREF(cls);
    if (!instance && error_out) {
        *error_out = python_error_atom(error_arena, "failed to construct bridge MeTTa");
    }
    return instance;
}

static PyObject *python_wrap_atom(Atom *atom, Arena *a) {
    char *text = atom_to_string(a, atom);
    return PyObject_CallFunction(g_bridge_cetta_atom_class, "s", text);
}

static Atom *parse_text_atom(Arena *a, const char *text) {
    size_t pos = 0;
    Atom *atom = parse_sexpr(a, text, &pos);
    if (!atom) {
        return atom_error(a, atom_symbol(a, "foreign-parse"),
                          atom_string(a, "foreign atom text parse failed"));
    }
    return atom;
}

static bool python_emit_single(CettaForeignRuntime *rt, Arena *a, PyObject *obj,
                               ResultSet *rs, Atom **error_out);

static bool python_emit_many(CettaForeignRuntime *rt, Arena *a, PyObject *obj,
                             ResultSet *rs, Atom **error_out) {
    if (PyList_Check(obj) || PyTuple_Check(obj)) {
        Py_ssize_t n = PySequence_Size(obj);
        for (Py_ssize_t i = 0; i < n; i++) {
            PyObject *item = PySequence_GetItem(obj, i);
            if (!item) {
                if (error_out) *error_out = python_error_atom(a, "failed to read python result item");
                return false;
            }
            bool ok = python_emit_single(rt, a, item, rs, error_out);
            Py_DECREF(item);
            if (!ok) return false;
        }
        return true;
    }
    return python_emit_single(rt, a, obj, rs, error_out);
}

static bool python_emit_single(CettaForeignRuntime *rt, Arena *a, PyObject *obj,
                               ResultSet *rs, Atom **error_out) {
    if (obj == Py_None) {
        result_set_add(rs, atom_unit(a));
        return true;
    }

    if (g_bridge_value_atom_class && PyObject_IsInstance(obj, g_bridge_value_atom_class) == 1) {
        PyObject *value = PyObject_GetAttrString(obj, "value");
        if (!value) {
            if (error_out) *error_out = python_error_atom(a, "failed to read ValueAtom.value");
            return false;
        }
        bool ok = python_emit_single(rt, a, value, rs, error_out);
        Py_DECREF(value);
        return ok;
    }

    if (g_bridge_operation_atom_class &&
        PyObject_IsInstance(obj, g_bridge_operation_atom_class) == 1) {
        PyObject *callable_obj = PyObject_GetAttrString(obj, "callable");
        if (!callable_obj) {
            if (error_out) *error_out = python_error_atom(a, "failed to read OperationAtom.callable");
            return false;
        }
        PyObject *unwrap_obj = PyObject_GetAttrString(obj, "unwrap");
        bool unwrap = !unwrap_obj || PyObject_IsTrue(unwrap_obj);
        Py_XDECREF(unwrap_obj);
        result_set_add(rs, atom_foreign(a,
            foreign_new_python_value(rt, callable_obj, true, unwrap)));
        Py_DECREF(callable_obj);
        return true;
    }

    if (g_bridge_cetta_atom_class && PyObject_IsInstance(obj, g_bridge_cetta_atom_class) == 1) {
        PyObject *text_obj = PyObject_GetAttrString(obj, "text");
        if (!text_obj) {
            if (error_out) *error_out = python_error_atom(a, "failed to read CettaAtom.text");
            return false;
        }
        const char *text = PyUnicode_AsUTF8(text_obj);
        if (!text) {
            Py_DECREF(text_obj);
            if (error_out) *error_out = python_error_atom(a, "failed to decode CettaAtom.text");
            return false;
        }
        result_set_add(rs, parse_text_atom(a, text));
        Py_DECREF(text_obj);
        return true;
    }

    if (PyBool_Check(obj)) {
        result_set_add(rs, atom_bool(a, obj == Py_True));
        return true;
    }
    if (PyLong_Check(obj)) {
        result_set_add(rs, atom_int(a, PyLong_AsLongLong(obj)));
        return true;
    }
    if (PyFloat_Check(obj)) {
        result_set_add(rs, atom_float(a, PyFloat_AsDouble(obj)));
        return true;
    }
    if (PyUnicode_Check(obj)) {
        const char *text = PyUnicode_AsUTF8(obj);
        result_set_add(rs, atom_string(a, text ? text : ""));
        return true;
    }

    if (PyCallable_Check(obj)) {
        result_set_add(rs, atom_foreign(a, foreign_new_python_value(rt, obj, true, true)));
        return true;
    }

    result_set_add(rs, atom_foreign(a, foreign_new_python_value(rt, obj, false, true)));
    return true;
}

static PyObject *python_from_atom(Arena *a, Atom *atom, bool unwrap) {
    if (!unwrap) {
        return python_wrap_atom(atom, a);
    }

    if (atom->kind == ATOM_GROUNDED) {
        switch (atom->ground.gkind) {
        case GV_INT:
            return PyLong_FromLongLong(atom->ground.ival);
        case GV_FLOAT:
            return PyFloat_FromDouble(atom->ground.fval);
        case GV_BOOL:
            return PyBool_FromLong(atom->ground.bval ? 1 : 0);
        case GV_STRING:
            return PyUnicode_FromString(atom->ground.sval);
        case GV_FOREIGN: {
            CettaForeignValue *value = (CettaForeignValue *)atom->ground.ptr;
            if (value && value->backend == CETTA_FOREIGN_BACKEND_PYTHON) {
                Py_INCREF(value->obj);
                return value->obj;
            }
            break;
        }
        default:
            break;
        }
    }

    if (atom->kind == ATOM_SYMBOL) {
        return PyUnicode_FromString(atom->name);
    }
    return python_wrap_atom(atom, a);
}

static bool parse_optional_unwrap(Atom **args, uint32_t nargs,
                                  uint32_t start_index, bool *unwrap_out) {
    bool unwrap = true;
    for (uint32_t i = start_index; i < nargs; i++) {
        Atom *arg = args[i];
        if (arg->kind == ATOM_GROUNDED && arg->ground.gkind == GV_BOOL) {
            unwrap = arg->ground.bval;
        }
    }
    *unwrap_out = unwrap;
    return true;
}

static const char *string_like_atom(Atom *atom) {
    if (!atom) return NULL;
    if (atom->kind == ATOM_SYMBOL) return atom->name;
    if (atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING) {
        return atom->ground.sval;
    }
    return NULL;
}

static PyObject *python_resolve_path(Atom *path_atom, PyObject *base,
                                     Arena *a, Atom **error_out) {
    const char *path = string_like_atom(path_atom);
    if (!path) {
        if (error_out) *error_out = foreign_error_atom(a, "python path must be a symbol or string");
        return NULL;
    }
    PyObject *result = base
        ? PyObject_CallFunction(g_bridge_resolve, "sO", path, base)
        : PyObject_CallFunction(g_bridge_resolve, "s", path);
    if (!result && error_out) {
        *error_out = python_error_atom(a, "python path resolution failed");
    }
    return result;
}

static bool python_call_object(CettaForeignRuntime *rt, Space *space, Arena *a,
                               PyObject *callable, bool unwrap,
                               Atom **args, uint32_t nargs,
                               ResultSet *rs, Atom **error_out) {
    PyObject *tuple = PyTuple_New((Py_ssize_t)nargs);
    if (!tuple) {
        if (error_out) *error_out = python_error_atom(a, "failed to allocate python arg tuple");
        return false;
    }
    for (uint32_t i = 0; i < nargs; i++) {
        PyObject *item = python_from_atom(a, args[i], unwrap);
        if (!item) {
            Py_DECREF(tuple);
            if (error_out) *error_out = python_error_atom(a, "failed to convert CeTTa arg to python");
            return false;
        }
        PyTuple_SET_ITEM(tuple, (Py_ssize_t)i, item);
    }

    Space *saved_space = g_python_callback_space;
    g_python_callback_space = space;
    PyObject *result = PyObject_CallObject(callable, tuple);
    g_python_callback_space = saved_space;
    Py_DECREF(tuple);
    if (!result) {
        if (error_out) *error_out = python_error_atom(a, "python callable failed");
        return false;
    }
    bool ok = python_emit_many(rt, a, result, rs, error_out);
    Py_DECREF(result);
    return ok;
}

static long python_callable_arity(PyObject *callable_obj) {
    long arity = -1;
    PyObject *code = PyObject_GetAttrString(callable_obj, "__code__");
    if (!code) {
        PyErr_Clear();
        return -1;
    }
    PyObject *argc_obj = PyObject_GetAttrString(code, "co_argcount");
    if (argc_obj) {
        arity = PyLong_AsLong(argc_obj);
        Py_DECREF(argc_obj);
    } else {
        PyErr_Clear();
    }
    Py_DECREF(code);
    return arity;
}

static Atom *module_export_lhs(Arena *a, const char *name, long arity, Atom ***vars_out) {
    uint32_t nargs = arity > 0 ? (uint32_t)arity : 0;
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    Atom **vars = nargs > 0 ? arena_alloc(a, sizeof(Atom *) * nargs) : NULL;
    elems[0] = atom_symbol(a, name);
    for (uint32_t i = 0; i < nargs; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "arg%u", i);
        vars[i] = atom_var(a, buf);
        elems[i + 1] = vars[i];
    }
    if (vars_out) *vars_out = vars;
    return atom_expr(a, elems, nargs + 1);
}

static bool module_add_export_constant(CettaForeignRuntime *rt, Space *target_space,
                                       Arena *persistent_arena,
                                       const char *name, PyObject *value_obj,
                                       Atom **error_out) {
    ResultSet rs;
    result_set_init(&rs);
    bool ok = python_emit_single(rt, persistent_arena, value_obj, &rs, error_out) &&
              rs.len == 1;
    if (ok) {
        Atom *lhs = module_export_lhs(persistent_arena, name, 0, NULL);
        Atom *eq = atom_expr3(persistent_arena,
                              atom_symbol(persistent_arena, "="),
                              lhs,
                              rs.items[0]);
        space_add(target_space, eq);
    }
    result_set_free(&rs);
    return ok;
}

static bool module_add_export_callable(CettaForeignRuntime *rt, Space *target_space,
                                       Arena *persistent_arena,
                                       const char *name, PyObject *callable_obj,
                                       bool unwrap, long arity) {
    if (arity < 0) arity = 0;
    Atom **vars = NULL;
    Atom *lhs = module_export_lhs(persistent_arena, name, arity, &vars);
    Atom **rhs_elems = arena_alloc(persistent_arena, sizeof(Atom *) * ((uint32_t)arity + 1));
    rhs_elems[0] = atom_foreign(persistent_arena,
        foreign_new_python_value(rt, callable_obj, true, unwrap));
    for (uint32_t i = 0; i < (uint32_t)arity; i++) {
        rhs_elems[i + 1] = vars[i];
    }
    Atom *rhs = atom_expr(persistent_arena, rhs_elems, (uint32_t)arity + 1);
    Atom *eq = atom_expr3(persistent_arena,
                          atom_symbol(persistent_arena, "="),
                          lhs,
                          rhs);
    space_add(target_space, eq);
    return true;
}

static bool module_add_export_value(CettaForeignRuntime *rt, Space *target_space,
                                    Arena *persistent_arena,
                                    const char *name, PyObject *value_obj,
                                    Atom **error_out) {
    if (g_bridge_operation_atom_class &&
        PyObject_IsInstance(value_obj, g_bridge_operation_atom_class) == 1) {
        PyObject *callable_obj = PyObject_GetAttrString(value_obj, "callable");
        if (!callable_obj) {
            if (error_out) *error_out = python_error_atom(persistent_arena, "failed to read OperationAtom.callable");
            return false;
        }
        PyObject *unwrap_obj = PyObject_GetAttrString(value_obj, "unwrap");
        bool unwrap = !unwrap_obj || PyObject_IsTrue(unwrap_obj);
        Py_XDECREF(unwrap_obj);
        long arity = python_callable_arity(callable_obj);
        bool ok = module_add_export_callable(rt, target_space, persistent_arena,
                                             name, callable_obj, unwrap, arity);
        Py_DECREF(callable_obj);
        return ok;
    }

    if (PyCallable_Check(value_obj)) {
        return module_add_export_callable(rt, target_space, persistent_arena,
                                          name, value_obj, true,
                                          python_callable_arity(value_obj));
    }

    return module_add_export_constant(rt, target_space, persistent_arena,
                                      name, value_obj, error_out);
}

static bool module_export_dict(CettaForeignRuntime *rt, Space *target_space,
                               Arena *persistent_arena,
                               PyObject *mapping, Atom **error_out) {
    PyObject *items = PyMapping_Items(mapping);
    if (!items) {
        if (error_out) *error_out = python_error_atom(persistent_arena, "python module export mapping failed");
        return false;
    }
    Py_ssize_t len = PyList_Size(items);
    for (Py_ssize_t i = 0; i < len; i++) {
        PyObject *pair = PyList_GetItem(items, i);
        PyObject *key = PyTuple_GetItem(pair, 0);
        PyObject *value = PyTuple_GetItem(pair, 1);
        const char *token = PyUnicode_Check(key) ? PyUnicode_AsUTF8(key) : NULL;
        if (!token || !literal_token_name(token)) {
            continue;
        }
        if (!module_add_export_value(rt, target_space, persistent_arena, token, value, error_out)) {
            Py_DECREF(items);
            return false;
        }
    }
    Py_DECREF(items);
    return true;
}

bool cetta_foreign_load_module(CettaForeignRuntime *rt,
                               const char *canonical_path,
                               Space *target_space,
                               Arena *persistent_arena,
                               Atom **error_out) {
    if (!ensure_python_bridge(persistent_arena, error_out)) return false;

    PyObject *module = call_python_load_module(canonical_path, persistent_arena, error_out);
    if (!module) return false;

    PyObject *names = PyObject_Dir(module);
    if (!names) {
        Py_DECREF(module);
        if (error_out) *error_out = python_error_atom(persistent_arena, "failed to enumerate python module exports");
        return false;
    }

    bool ok = true;
    Space *saved_space = g_python_callback_space;
    g_python_callback_space = target_space;

    Py_ssize_t n = PyList_Size(names);
    for (Py_ssize_t i = 0; i < n; i++) {
        PyObject *name_obj = PyList_GetItem(names, i);
        const char *name = PyUnicode_Check(name_obj) ? PyUnicode_AsUTF8(name_obj) : NULL;
        if (!name) continue;
        PyObject *attr = PyObject_GetAttrString(module, name);
        if (!attr) {
            ok = false;
            if (error_out) *error_out = python_error_atom(persistent_arena, "failed to inspect python module attribute");
            break;
        }
        if (!PyObject_HasAttrString(attr, "metta_type")) {
            Py_DECREF(attr);
            continue;
        }

        PyObject *pass_metta_obj = PyObject_GetAttrString(attr, "metta_pass_metta");
        bool pass_metta = pass_metta_obj && PyObject_IsTrue(pass_metta_obj);
        Py_XDECREF(pass_metta_obj);

        PyObject *mapping = pass_metta
            ? python_make_metta_bridge(persistent_arena, error_out)
            : NULL;
        if (pass_metta && !mapping) {
            Py_DECREF(attr);
            ok = false;
            if (error_out) *error_out = python_error_atom(persistent_arena, "failed to construct MeTTa bridge for python module");
            break;
        }

        PyObject *exports = pass_metta
            ? PyObject_CallFunctionObjArgs(attr, mapping, NULL)
            : PyObject_CallFunctionObjArgs(attr, NULL);
        Py_XDECREF(mapping);
        if (!exports) {
            Py_DECREF(attr);
            ok = false;
            if (error_out) *error_out = python_error_atom(persistent_arena, "python register_atoms function failed");
            break;
        }

        if (PyMapping_Check(exports)) {
            ok = module_export_dict(rt, target_space, persistent_arena, exports, error_out);
        }
        Py_DECREF(exports);
        Py_DECREF(attr);
        if (!ok) break;
    }

    g_python_callback_space = saved_space;
    Py_DECREF(names);
    Py_DECREF(module);
    return ok;
}

bool cetta_foreign_is_callable_atom(Atom *atom) {
    return atom &&
           atom->kind == ATOM_GROUNDED &&
           atom->ground.gkind == GV_FOREIGN &&
           ((CettaForeignValue *)atom->ground.ptr)->callable;
}

bool cetta_foreign_call(CettaForeignRuntime *rt,
                        Space *space,
                        Arena *a,
                        Atom *callable,
                        Atom **args,
                        uint32_t nargs,
                        ResultSet *rs,
                        Atom **error_out) {
    if (!callable || callable->kind != ATOM_GROUNDED || callable->ground.gkind != GV_FOREIGN) {
        if (error_out) *error_out = foreign_error_atom(a, "not a foreign callable");
        return false;
    }
    CettaForeignValue *value = (CettaForeignValue *)callable->ground.ptr;
    if (!value->callable || value->backend != CETTA_FOREIGN_BACKEND_PYTHON) {
        if (error_out) *error_out = foreign_error_atom(a, "unsupported foreign callable backend");
        return false;
    }
    return python_call_object(rt, space, a, value->obj, value->unwrap, args, nargs, rs, error_out);
}

static Atom *result_set_collapse_for_native(Arena *a, ResultSet *rs) {
    if (rs->len == 0) return atom_empty(a);
    if (rs->len == 1) return rs->items[0];
    return atom_expr(a, rs->items, rs->len);
}

Atom *cetta_foreign_dispatch_native(CettaForeignRuntime *rt,
                                    Space *space,
                                    Arena *a,
                                    Atom *head,
                                    Atom **args,
                                    uint32_t nargs) {
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    if (!ensure_python_bridge(a, NULL)) return NULL;

    if (strcmp(head->name, "py-atom") == 0) {
        if (nargs < 1 || nargs > 3) {
            return atom_error(a, atom_expr(a, (Atom *[]){ head }, 1), atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        bool unwrap = true;
        parse_optional_unwrap(args, nargs, 1, &unwrap);
        Atom *path_atom = args[0];
        PyObject *obj = python_resolve_path(path_atom, NULL, a, NULL);
        if (!obj) return python_error_atom(a, "py-atom failed");
        ResultSet rs;
        result_set_init(&rs);
        if (PyCallable_Check(obj)) {
            result_set_add(&rs, atom_foreign(a, foreign_new_python_value(rt, obj, true, unwrap)));
        } else {
            python_emit_single(rt, a, obj, &rs, NULL);
        }
        Py_DECREF(obj);
        Atom *result = result_set_collapse_for_native(a, &rs);
        result_set_free(&rs);
        return result;
    }

    if (strcmp(head->name, "py-dot") == 0) {
        if (nargs < 2 || nargs > 4) {
            return atom_error(a, atom_expr(a, (Atom *[]){ head }, 1), atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        bool unwrap = true;
        parse_optional_unwrap(args, nargs, 2, &unwrap);

        PyObject *base = NULL;
        if (args[0]->kind == ATOM_GROUNDED && args[0]->ground.gkind == GV_FOREIGN) {
            CettaForeignValue *value = (CettaForeignValue *)args[0]->ground.ptr;
            if (value->backend == CETTA_FOREIGN_BACKEND_PYTHON) {
                base = value->obj;
                Py_INCREF(base);
            }
        } else {
            base = python_resolve_path(args[0], NULL, a, NULL);
        }
        if (!base) return python_error_atom(a, "py-dot base resolution failed");

        PyObject *obj = python_resolve_path(args[1], base, a, NULL);
        Py_DECREF(base);
        if (!obj) return python_error_atom(a, "py-dot attribute resolution failed");

        ResultSet rs;
        result_set_init(&rs);
        if (PyCallable_Check(obj)) {
            result_set_add(&rs, atom_foreign(a, foreign_new_python_value(rt, obj, true, unwrap)));
        } else {
            python_emit_single(rt, a, obj, &rs, NULL);
        }
        Py_DECREF(obj);
        Atom *result = result_set_collapse_for_native(a, &rs);
        result_set_free(&rs);
        return result;
    }

    if (strcmp(head->name, "py-call") == 0) {
        if (nargs != 1 || args[0]->kind != ATOM_EXPR || args[0]->expr.len < 1) {
            return atom_error(a, atom_expr(a, (Atom *[]){ head }, 1), atom_symbol(a, "IncorrectNumberOfArguments"));
        }
        Atom *call_expr = args[0];
        Atom *call_head = call_expr->expr.elems[0];
        Atom **call_args = call_expr->expr.elems + 1;
        uint32_t call_nargs = call_expr->expr.len - 1;

        Atom *callable_atom = NULL;
        ResultSet tmp;
        result_set_init(&tmp);
        if (call_head->kind == ATOM_GROUNDED && call_head->ground.gkind == GV_FOREIGN) {
            callable_atom = call_head;
        } else {
            PyObject *obj = python_resolve_path(call_head, NULL, a, NULL);
            if (!obj) {
                result_set_free(&tmp);
                return python_error_atom(a, "py-call head resolution failed");
            }
            if (PyCallable_Check(obj)) {
                callable_atom = atom_foreign(a, foreign_new_python_value(rt, obj, true, true));
            } else {
                python_emit_single(rt, a, obj, &tmp, NULL);
            }
            Py_DECREF(obj);
        }
        if (!callable_atom) {
            Atom *result = result_set_collapse_for_native(a, &tmp);
            result_set_free(&tmp);
            return result;
        }

        ResultSet rs;
        result_set_init(&rs);
        Atom *error = NULL;
        if (!cetta_foreign_call(rt, space, a, callable_atom, call_args, call_nargs, &rs, &error)) {
            result_set_free(&rs);
            return error ? error : python_error_atom(a, "py-call failed");
        }
        Atom *result = result_set_collapse_for_native(a, &rs);
        result_set_free(&rs);
        return result;
    }

    return NULL;
}
