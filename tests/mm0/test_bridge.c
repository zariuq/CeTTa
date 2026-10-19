#define _POSIX_C_SOURCE 200809L
#include "mm0_cetta_module.h"
#include "seal.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

bool eval_native_interrupted(void) { return false; }
static unsigned checks, failures;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; \
    fprintf(stderr, "FAIL line %u: %s\n", __LINE__, #c); } } while (0)

static Atom *expr(Arena *arena, const char *head, unsigned n, ...) {
    Atom *items[9] = {atom_symbol(arena, head)};
    if (n > 8) abort();
    va_list args;
    va_start(args, n);
    for (unsigned i = 0; i < n; ++i) items[i + 1] = va_arg(args, Atom *);
    va_end(args);
    return atom_expr(arena, items, n + 1);
}
static Atom *empty(Arena *a) { return atom_expr(a, NULL, 0); }
static Atom *sym(Arena *a, const char *name) { return atom_symbol(a, name); }
static bool tag(Atom *a, const char *head) {
    return a && a->kind == ATOM_EXPR && a->expr.len && atom_is_symbol(a->expr.elems[0], head);
}
static Atom *call(Arena *a, const char *op, unsigned n, ...) {
    Atom *args[6];
    if (n > 6) abort();
    va_list values;
    va_start(values, n);
    for (unsigned i = 0; i < n; ++i) args[i] = va_arg(values, Atom *);
    va_end(values);
    char name[80];
    snprintf(name, sizeof name, "__cetta_lib_mm0_%s", op);
    return cetta_mm0_dispatch(NULL, NULL, a, sym(a, name), args, n);
}
static int64_t field(Atom *a, unsigned n) {
    return a && a->kind == ATOM_EXPR && a->expr.len > n && a->expr.elems[n] &&
        a->expr.elems[n]->kind == ATOM_GROUNDED && a->expr.elems[n]->ground.gkind == GV_INT
        ? a->expr.elems[n]->ground.ival : -1;
}
static Atom *prove(Arena *a, Atom *theory, const char *name, const char *conclusion) {
    Atom *statement = expr(a, "theorem", 4, sym(a, name), empty(a), empty(a), expr(a, conclusion, 0));
    Atom *row = expr(a, "Mm0Row", 3, atom_int(a, 0), expr(a, "thm", 2, sym(a, "axP"), empty(a)), empty(a));
    Atom *rows = atom_expr(a, &row, 1);
    return call(a, "prove", 5, theory, statement, empty(a), rows, atom_int(a, 0));
}

static CettaSealBrandCell cold_brand = CETTA_SEAL_BRAND_CELL_INIT("concurrent-brand");
typedef struct {
    Atom *theory, *lookup, *statement, *name;
    unsigned failed;
    CettaSealObject *brand;
} Worker;
static void *read_concurrently(void *arg) {
    Worker *w = arg;
    w->brand = cetta_seal_brand_get(&cold_brand, NULL);
    for (unsigned i = 0; i < 128; ++i) {
        Arena local;
        arena_init_detached(&local);
        Atom *args[] = {w->theory, w->name};
        Atom *thm = cetta_mm0_dispatch(NULL, NULL, &local, w->lookup, args, 2);
        Atom *one[] = {thm};
        Atom *data = cetta_mm0_dispatch(NULL, NULL, &local, w->statement, one, 1);
        if (!cetta_seal_atom_object(thm) || !tag(data, "axiom")) ++w->failed;
        arena_free(&local);
    }
    return NULL;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols; g_hashcons = NULL; g_var_intern = NULL;
    Arena source, retained, copied;
    arena_init_detached(&source); arena_init_detached(&retained); arena_init_detached(&copied);
    static CettaSealBrandCell retry_cells[] = {
        CETTA_SEAL_BRAND_CELL_INIT("retry-first"), CETTA_SEAL_BRAND_CELL_INIT("retry-second")};
    for (unsigned i = 0; i < 2; ++i) {
        CettaSealStatus status;
        cetta_seal_test_fail_allocation(i + 1);
        CHECK(!cetta_seal_brand_get(&retry_cells[i], &status));
        CHECK(status == CETTA_SEAL_NOMEM);
        CettaSealObject *brand = cetta_seal_brand_get(&retry_cells[i], &status);
        CHECK(brand && status == CETTA_SEAL_OK);
        CHECK(cetta_seal_brand_get(&retry_cells[i], NULL) == brand);
    }
    Arena first;
    arena_init_detached(&first);
    cetta_seal_test_fail_allocation(1);
    CHECK(tag(call(&first, "theory", 1, sym(&first, "empty")), "mm0:error"));
    CHECK(cetta_seal_atom_object(call(&first, "theory", 1, sym(&first, "empty"))));
    arena_free(&first);
    CHECK(field(call(&source, "live", 1, sym(&source, "all")), 1) == 0);
    const char *allocation_faults[] = {"theory-record", "theory-capsule", "theory-owned-atom"};
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(setenv("CETTA_MM0_FAULT", allocation_faults[i], 1) == 0);
        CHECK(tag(call(&source, "theory", 1, sym(&source, "empty")), "mm0:error"));
        CHECK(field(call(&source, "live", 1, sym(&source, "all")), 1) == 0);
    }
    CHECK(unsetenv("CETTA_MM0_FAULT") == 0);
    Atom *theory = call(&source, "theory", 1, sym(&source, "empty"));
    CHECK(cetta_seal_atom_object(theory));
    Atom *flags = atom_expr(&source, (Atom *[]){sym(&source, "strict"), sym(&source, "provable")}, 2);
    CHECK(tag(call(&source, "declare", 2, theory, expr(&source, "sort", 2, sym(&source, "wff"), flags)), "mm0:admitted"));
    for (unsigned i = 0; i < 2; ++i) {
        Atom *decl = expr(&source, "term", 3, sym(&source, i ? "Q" : "P"), empty(&source),
                          expr(&source, "wff", 1, empty(&source)));
        CHECK(tag(call(&source, "declare", 2, theory, decl), "mm0:admitted"));
    }
    Atom *axiom = expr(&source, "axiom", 4, sym(&source, "axP"), empty(&source), empty(&source), expr(&source, "P", 0));
    CHECK(cetta_seal_atom_object(call(&source, "declare", 2, theory, axiom)));
    int64_t before = field(call(&source, "live", 1, sym(&source, "all")), 2);
    CHECK(tag(prove(&retained, theory, "wrong", "Q"), "mm0:refused"));
    CHECK(field(call(&source, "live", 1, sym(&source, "all")), 2) == before);
    const char *publication_faults[] = {"theorem-record", "theorem-capsule", "theorem-owned-atom"};
    for (unsigned i = 0; i < 3; ++i) {
        CHECK(setenv("CETTA_MM0_FAULT", publication_faults[i], 1) == 0);
        CHECK(tag(prove(&retained, theory, "valid", "P"), "mm0:error"));
        CHECK(field(call(&source, "stats", 1, theory), 3) == 1);
        CHECK(field(call(&source, "live", 1, sym(&source, "all")), 2) == before);
    }
    CHECK(unsetenv("CETTA_MM0_FAULT") == 0);
    Atom *theorem = prove(&retained, theory, "valid", "P");
    CHECK(cetta_seal_atom_object(theorem));
    CHECK(field(call(&source, "stats", 1, theory), 3) == 2);
    CHECK(cetta_seal_check_portable(expr(&retained, "nested", 1, theorem)) == CETTA_SEAL_NOT_PORTABLE);

    pthread_t threads[4]; Worker workers[4]; unsigned created = 0;
    Atom *lookup = sym(&source, "__cetta_lib_mm0_theorem");
    Atom *statement = sym(&source, "__cetta_lib_mm0_statement");
    Atom *name = sym(&source, "axP");
    for (unsigned i = 0; i < 4; ++i) {
        workers[i] = (Worker){theory, lookup, statement, name, 0, NULL};
        if (pthread_create(&threads[i], NULL, read_concurrently, &workers[i])) break;
        ++created;
    }
    CHECK(created == 4);
    for (unsigned i = 0; i < created; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(workers[i].failed == 0);
        CHECK(workers[i].brand && workers[i].brand == workers[0].brand);
    }
    Atom *copy = atom_deep_copy(&copied, theorem);
    CHECK(cetta_seal_atom_object(copy) == cetta_seal_atom_object(theorem));
    arena_free(&source);
    CHECK(tag(call(&retained, "statement", 1, theorem), "theorem"));
    arena_free(&retained);
    CHECK(tag(call(&copied, "statement", 1, copy), "theorem"));
    CHECK(field(call(&copied, "live", 1, sym(&copied, "all")), 1) == 1);
    arena_free(&copied);
    Arena final;
    arena_init_detached(&final);
    Atom *live = call(&final, "live", 1, sym(&final, "all"));
    CHECK(field(live, 1) == 0 && field(live, 2) == 0);
    arena_free(&final);
    symbol_table_free(&symbols); g_symbols = NULL;
    printf("Mm0BridgeSummary %u %u\n", checks, failures);
    return failures != 0;
}
