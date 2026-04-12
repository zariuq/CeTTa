#include "foreign.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct CettaForeignRuntime {
    int unavailable;
};

static const char *CETTA_FOREIGN_DISABLED_MSG =
    "python foreign modules require a Python-enabled build (BUILD=python, BUILD=main, or BUILD=full)";

static void set_reason(char *reason, size_t reason_sz, const char *msg) {
    if (!reason || reason_sz == 0)
        return;
    snprintf(reason, reason_sz, "%s", msg ? msg : "");
}

static bool path_has_suffix(const char *path, const char *suffix) {
    size_t plen;
    size_t slen;
    if (!path || !suffix)
        return false;
    plen = strlen(path);
    slen = strlen(suffix);
    if (plen < slen)
        return false;
    return strcmp(path + plen - slen, suffix) == 0;
}

static bool directory_has_python_entry(const char *path) {
    char init_path[PATH_MAX];
    int n;
    n = snprintf(init_path, sizeof(init_path), "%s/__init__.py", path);
    if (!(n > 0 && (size_t)n < sizeof(init_path)))
        return false;
    return access(init_path, R_OK) == 0;
}

static Atom *foreign_unavailable_error(Arena *a, Atom *head, Atom **args,
                                       uint32_t nargs) {
    Atom **elems = arena_alloc(a, sizeof(Atom *) * (nargs + 1));
    elems[0] = head;
    for (uint32_t i = 0; i < nargs; i++)
        elems[i + 1] = args[i];
    return atom_error(a, atom_expr(a, elems, nargs + 1),
                      atom_symbol(a, CETTA_FOREIGN_DISABLED_MSG));
}

const char *cetta_module_format_name(CettaModuleFormatKind kind) {
    switch (kind) {
    case CETTA_MODULE_FORMAT_METTA:
        return "metta";
    case CETTA_MODULE_FORMAT_FOREIGN:
        return "foreign";
    case CETTA_MODULE_FORMAT_MORK_ACT:
        return "mork-act";
    case CETTA_MODULE_FORMAT_MM2:
        return "mm2";
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

bool cetta_foreign_resolve_candidate(const char *candidate,
                                     char *out, size_t out_sz,
                                     CettaModuleFormat *format_out,
                                     char *reason, size_t reason_sz) {
    struct stat st;

    (void)out;
    (void)out_sz;
    (void)format_out;
    set_reason(reason, reason_sz, "");
    if (!candidate)
        return false;
    if (path_has_suffix(candidate, ".py")) {
        set_reason(reason, reason_sz, CETTA_FOREIGN_DISABLED_MSG);
        return false;
    }
    if (stat(candidate, &st) == 0 && S_ISDIR(st.st_mode) &&
        directory_has_python_entry(candidate)) {
        set_reason(reason, reason_sz, CETTA_FOREIGN_DISABLED_MSG);
        return false;
    }
    return false;
}

CettaForeignRuntime *cetta_foreign_runtime_new(void) {
    CettaForeignRuntime *rt = cetta_malloc(sizeof(CettaForeignRuntime));
    rt->unavailable = 1;
    return rt;
}

void cetta_foreign_runtime_free(CettaForeignRuntime *rt) {
    free(rt);
}

bool cetta_foreign_load_module(CettaForeignRuntime *rt,
                               const char *canonical_path,
                               Space *target_space,
                               Arena *persistent_arena,
                               Atom **error_out) {
    (void)rt;
    (void)canonical_path;
    (void)target_space;
    if (error_out)
        *error_out = atom_symbol(persistent_arena, CETTA_FOREIGN_DISABLED_MSG);
    return false;
}

bool cetta_foreign_is_callable_atom(Atom *atom) {
    (void)atom;
    return false;
}

bool cetta_foreign_call(CettaForeignRuntime *rt,
                        Space *space,
                        Arena *a,
                        Atom *callable,
                        Atom **args,
                        uint32_t nargs,
                        ResultSet *rs,
                        Atom **error_out) {
    (void)rt;
    (void)space;
    (void)a;
    (void)callable;
    (void)args;
    (void)nargs;
    (void)rs;
    if (error_out)
        *error_out = atom_symbol(a, CETTA_FOREIGN_DISABLED_MSG);
    return false;
}

Atom *cetta_foreign_dispatch_native(CettaForeignRuntime *rt,
                                    Space *space,
                                    Arena *a,
                                    Atom *head,
                                    Atom **args,
                                    uint32_t nargs) {
    (void)rt;
    (void)space;
    if (!head)
        return NULL;
    if (atom_is_symbol_id(head, g_builtin_syms.py_atom) ||
        atom_is_symbol_id(head, g_builtin_syms.py_call) ||
        atom_is_symbol_id(head, g_builtin_syms.py_dot)) {
        return foreign_unavailable_error(a, head, args, nargs);
    }
    return NULL;
}
