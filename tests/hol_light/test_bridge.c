#include "hol_cetta_module.h"
#include "library.h"
#include "seal.h"
#include "test_util.h"
#include <time.h>
#include <pthread.h>

static bool stop;
bool eval_native_interrupted(void) { return stop; }

static Arena arena;
static CettaLibraryContext *host;

static Atom *sym(const char *s) { return atom_symbol(&arena, s); }
static Atom *str(const char *s) { return atom_string(&arena, s); }
static Atom *one(const char *s, Atom *a) { return atom_expr2(&arena, sym(s), a); }
static Atom *two(const char *s, Atom *a, Atom *b) {
    return atom_expr(&arena, (Atom *[]){sym(s), a, b}, 3);
}
static Atom *call(const char *name, Atom **args, uint32_t n) {
    char op[80];
    snprintf(op, sizeof op, "__cetta_lib_hol_%s", name);
    return cetta_hol_dispatch(host, NULL, &arena, sym(op), args, n);
}
static bool tag(Atom *a, const char *s) {
    return a && a->kind == ATOM_EXPR && a->expr.len && atom_is_symbol(a->expr.elems[0], s);
}
static bool handle(Atom *a, const char *kind) {
    return cetta_hol_test_is_value(a, kind);
}
static unsigned live_values(void) {
    return (unsigned)cetta_hol_test_live_values();
}

typedef struct {
    Atom *kernel, *term, *refl_head, *sequent_head;
    unsigned failures;
} ConcurrentCheck;

static void *concurrent_checks(void *data) {
    ConcurrentCheck *work = data;
    for (unsigned round = 0; round < 128; ++round) {
        Arena local;
        arena_init_detached(&local);
        Atom *args[] = {work->kernel, work->term};
        Atom *theorem = cetta_hol_dispatch(host, NULL, &local, work->refl_head, args, 2);
        if (!cetta_hol_test_is_value(theorem, "hol-thm")) ++work->failures;
        Atom *proof_args[] = {theorem};
        Atom *view = cetta_hol_dispatch(host, NULL, &local, work->sequent_head, proof_args, 1);
        if (!tag(view, "thm")) ++work->failures;
        arena_free(&local);
    }
    return NULL;
}

int main(int argc, char **argv) {
    size_t node_count = 10000;
    if (argc > 2) return 2;
    if (argc == 2) {
        char *end;
        unsigned long long count = strtoull(argv[1], &end, 10);
        if (!*argv[1] || *end || !count || count > UINT32_MAX ||
            count > SIZE_MAX / sizeof(Atom *) || count > INT64_MAX / 20) return 2;
        node_count = (size_t)count;
    }
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init_detached(&arena);
    host = calloc(1, sizeof *host);
    if (!host) return 2;
    cetta_seal_test_fail_allocation(1);
    CHECK(tag(call("kernel", NULL, 0), "hol:error"));
    CHECK(live_values() == 0);
    Atom *k = call("kernel", NULL, 0);
    CHECK(handle(k, "hol-kernel"));
    if (!handle(k, "hol-kernel")) {
        arena_free(&arena); free(host);
        symbol_table_free(&symbols); g_symbols = NULL;
        return test_summary("test-hol-bridge");
    }

    SECTION("borrowed shared input and shared output");
    Atom *b = one("T", str("bool"));
    Atom *bb = atom_expr(&arena, (Atom *[]){sym("T"), str("fun"), b, b}, 4);
    Atom *f_ty = atom_expr(&arena, (Atom *[]){sym("T"), str("fun"), b, bb}, 4);
    Atom *f = two("v", str("f"), f_ty), *x = two("v", str("x"), b);
    Atom *term = x;
    for (unsigned i = 0; i < 40; ++i) term = two("A", two("A", f, term), term);
    Atom *resident = call("term", (Atom *[]){k, term}, 2);
    CHECK(handle(resident, "hol-term"));
    size_t before = arena_accounted_live_bytes(&arena);
    Atom *data = call("term-data", (Atom *[]){resident}, 1);
    size_t materialized = arena_accounted_live_bytes(&arena) - before;
    CHECK(tag(data, "A"));
    /* The expanded tree has over a trillion leaves. Each pair of child
     * occurrences must still point at the one materialized child node. */
    Atom *cursor = data;
    for (unsigned i = 0; i < 40; ++i) {
        CHECK(tag(cursor, "A") && tag(cursor->expr.elems[1], "A"));
        CHECK(cursor->expr.elems[1]->expr.elems[2] == cursor->expr.elems[2]);
        cursor = cursor->expr.elems[2];
    }
    CHECK(tag(cursor, "v"));
    CHECK(materialized < 100000);
    Atom *again = call("term", (Atom *[]){k, data}, 2);
    CHECK(handle(again, "hol-term"));
    Atom *equal = call("aconv", (Atom *[]){k, resident, again}, 3);
    CHECK(equal && equal->kind == ATOM_GROUNDED && equal->ground.gkind == GV_BOOL && equal->ground.bval);
    printf("HolBridgeSharing depth=40 expanded-leaves=1099511627776 materialized-bytes=%zu\n", materialized);

    SECTION("iterative conversion and explicit work quotas");
    Atom *unary = two("v", str("unary"), bb);
    Atom *deep = x;
    const size_t term_depth = 4096;
    for (size_t i = 0; i < term_depth; ++i) deep = two("A", unary, deep);
    Atom *deep_handle = call("term", (Atom *[]){k, deep}, 2);
    CHECK(handle(deep_handle, "hol-term"));
    Atom *deep_data = call("term-data", (Atom *[]){deep_handle}, 1);
    bool deep_ok = true;
    cursor = deep_data;
    for (size_t i = 0; i < term_depth && deep_ok; ++i) {
        deep_ok = tag(cursor, "A") && cursor->expr.len == 3 && tag(cursor->expr.elems[1], "v");
        if (deep_ok) cursor = cursor->expr.elems[2];
    }
    CHECK(deep_ok && tag(cursor, "v"));
    CHECK(handle(call("term", (Atom *[]){k, deep_data}, 2), "hol-term"));
    CHECK(atom_is_symbol(call("budget", (Atom *[]){k, atom_int(&arena, 2)}, 2), "hol:ok"));
    CHECK(tag(call("term", (Atom *[]){k, deep}, 2), "hol:error"));
    CHECK(atom_is_symbol(call("budget", (Atom *[]){k, atom_int(&arena, -1)}, 2), "hol:ok"));
    CHECK(handle(call("term", (Atom *[]){k, deep}, 2), "hol-term"));
    /* Quote envelopes stress the host walk beyond the kernel's recursive
     * term height without constructing a deeper logical term. */
    Arena literal_source;
    arena_init_detached(&literal_source);
    Atom *quoted = x;
    const size_t quote_depth = 1000001;
    for (size_t i = 0; i < quote_depth; ++i)
        quoted = atom_expr2(&literal_source, sym("quote"), quoted);
    Atom *quoted_handle = call("term", (Atom *[]){k, quoted}, 2);
    CHECK(handle(quoted_handle, "hol-term"));
    arena_free(&literal_source);
    CHECK(tag(call("term-data", (Atom *[]){quoted_handle}, 1), "v"));
    Atom *deep_cycle = two("A", unary, x);
    Atom *wrapped_cycle = deep_cycle;
    for (size_t i = 0; i < term_depth; ++i) wrapped_cycle = one("quote", wrapped_cycle);
    deep_cycle->expr.elems[2] = wrapped_cycle;
    CHECK(tag(call("term", (Atom *[]){k, wrapped_cycle}, 2), "hol:error"));
    printf("HolBridgeDepth logical=%zu literal-envelopes=%zu\n", term_depth, quote_depth);

    SECTION("cycles, interruption and retry");
    Atom *cycle = two("A", f, x);
    cycle->expr.elems[2] = cycle;
    CHECK(tag(call("term", (Atom *[]){k, cycle}, 2), "hol:error"));
    stop = true;
    CHECK(tag(call("refl", (Atom *[]){k, resident}, 2), "hol:error"));
    stop = false;
    Atom *th = call("refl", (Atom *[]){k, resident}, 2);
    CHECK(handle(th, "hol-thm"));

    SECTION("borrowed proof rows, sparse IDs and repeated premise edges");
    Arena source;
    arena_init_detached(&source);
    Atom *eq_ty = atom_expr(&arena, (Atom *[]){sym("T"), str("fun"), b, bb}, 4);
    Atom *eq_xx = two("A", two("A", two("c", str("="), eq_ty), x), x);
    Atom *empty = atom_expr(&arena, NULL, 0);
    Atom *claim = two("thm", empty, eq_xx);
    Atom *refl_request = one("REFL", x), *trans_request = atom_expr(&arena, (Atom *[]){sym("TRANS")}, 1);
    Atom **nodes = calloc(node_count, sizeof *nodes);
    if (!nodes) return 2;
    for (size_t i = 0; i < node_count; ++i) {
        Atom *parent = i ? atom_int(&source, (int64_t)((i - 1) * 17 + 3)) : NULL;
        Atom *parents = i ? atom_expr2(&source, parent, parent) : empty;
        nodes[i] = atom_expr(&source, (Atom *[]){sym("HolNode"), atom_int(&source, (int64_t)(i * 17 + 3)),
                            i ? trans_request : refl_request, parents, claim}, 5);
    }
    Atom *rows = atom_expr(&source, nodes, (CettaExprLen)node_count);
    Atom *root_id = atom_int(&arena, (int64_t)((node_count - 1) * 17 + 3));
    unsigned handles_before = live_values();
    size_t source_bytes = arena_accounted_live_bytes(&source);
    before = arena_accounted_live_bytes(&arena);
    struct timespec started, finished;
    if (clock_gettime(CLOCK_MONOTONIC, &started)) return 2;
    Atom *root = call("check-dag", (Atom *[]){k, rows, root_id}, 3);
    if (clock_gettime(CLOCK_MONOTONIC, &finished)) return 2;
    double elapsed_ms = (double)(finished.tv_sec - started.tv_sec) * 1000.0 +
                        (double)(finished.tv_nsec - started.tv_nsec) / 1000000.0;
    size_t publication_bytes = arena_accounted_live_bytes(&arena) - before;
    CHECK(handle(root, "hol-thm"));
    CHECK(live_values() == handles_before + 1);
    CHECK(publication_bytes < 4096); /* no copied source rows in the output arena */
    CHECK(arena_accounted_live_bytes(&source) == source_bytes);
    bool unchanged = true;
    for (size_t i = 0; i < node_count; ++i)
        unchanged = unchanged && rows->expr.elems[i] == nodes[i] &&
                    nodes[i]->expr.elems[4] == claim &&
                    nodes[i]->expr.elems[1]->ground.ival == (int64_t)(i * 17 + 3);
    CHECK(unchanged);
    /* A late invalid claim publishes nothing and leaves the earlier root live. */
    nodes[node_count - 1]->expr.elems[4] = two("thm", empty, x);
    CHECK(tag(call("check-dag", (Atom *[]){k, rows, root_id}, 3), "hol:refused"));
    CHECK(live_values() == handles_before + 1);
    nodes[node_count - 1]->expr.elems[4] = claim;
    free(nodes);
    arena_free(&source);
    CHECK(tag(call("sequent", (Atom *[]){root}, 1), "thm"));
    CHECK(handle(root, "hol-thm")); /* source storage has actually been released */
    printf("HolBridgeProofDag nodes=%zu source-bytes=%zu output-arena-bytes=%zu query-ms=%.3f\n",
           node_count, source_bytes, publication_bytes, elapsed_ms);

    SECTION("shared sealed session across independent calling arenas");
    pthread_t threads[4];
    ConcurrentCheck work[4];
    unsigned created = 0;
    Atom *refl_head = sym("__cetta_lib_hol_refl");
    Atom *sequent_head = sym("__cetta_lib_hol_sequent");
    unsigned concurrent_before = live_values();
    for (unsigned i = 0; i < 4; ++i) {
        work[i] = (ConcurrentCheck){k, x, refl_head, sequent_head, 0};
        if (pthread_create(&threads[i], NULL, concurrent_checks, &work[i])) break;
        ++created;
    }
    CHECK(created == 4);
    for (unsigned i = 0; i < created; ++i) {
        CHECK(pthread_join(threads[i], NULL) == 0);
        CHECK(work[i].failures == 0);
    }
    CHECK(live_values() == concurrent_before);

    SECTION("atomic publication of both type-definition theorems");
    Atom *identity = two("L", x, x);
    Atom *witness = call("refl", (Atom *[]){k, identity}, 2);
    CHECK(handle(witness, "hol-thm"));
    unsigned live = live_values();
    Atom *args[] = {k, str("unit_type"), str("unit_abs"), str("unit_rep"), witness};
    cetta_seal_test_fail_allocation(2);
    Atom *failed = call("new-type-definition", args, 5);
    CHECK(tag(failed, "hol:error"));
    CHECK(live_values() == live); /* no leaked first theorem handle */
    Atom *result = call("new-type-definition", args, 5);
    CHECK(result && result->kind == ATOM_EXPR && result->expr.len == 2);
    if (result && result->kind == ATOM_EXPR && result->expr.len == 2) {
        CHECK(handle(result->expr.elems[0], "hol-thm"));
        CHECK(handle(result->expr.elems[1], "hol-thm"));
        Atom *admitted = call("admits", (Atom *[]){k, result->expr.elems[0]}, 2);
        CHECK(admitted && admitted->kind == ATOM_GROUNDED && admitted->ground.bval);
    }

    arena_free(&arena);
    CHECK(live_values() == 0);
    free(host);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return test_summary("test-hol-bridge");
}
