/* Exercise the private walker and its cache against an independent traversal. */
#include "../../src/open_equation_machine.c"
#include <assert.h>

static uint64_t checks;
/* Allocation controls must remain visible across the linker-wrapped call. */
static volatile bool reject_reallocation;
static volatile unsigned rejected_reallocations;

void *__real_realloc(void *, size_t);
void *__wrap_realloc(void *pointer, size_t bytes) {
    if (reject_reallocation) {
        reject_reallocation = false;
        rejected_reallocations++;
        return NULL;
    }
    return __real_realloc(pointer, bytes);
}

static bool reference(CettaOpenEquationCursor *cursor, uint32_t target,
                      Atom *root) {
    Atom *pending[8192];
    size_t length = 0u;
    pending[length++] = root;
    while (length) {
        Atom *term = pending[--length];
        uint32_t index;
        if (oem_is_cell(cursor, term, &index)) {
            if (cursor->cells[index])
                pending[length++] = cursor->cells[index];
            else if (index == target)
                return true;
        } else if (term->kind == ATOM_EXPR) {
            assert(term->expr.len <= 8192u - length);
            for (CettaExprIndex child = 0u; child < term->expr.len; child++)
                pending[length++] = term->expr.elems[child];
        }
    }
    return false;
}

static void compare(CettaOpenEquationCursor *cursor, Atom *root) {
    for (uint32_t index = 0u; index < cursor->cell_len; index++) {
        if (cursor->cells[index])
            continue;
        bool failed = false;
        bool actual = oem_occurs(cursor, index, root, &failed);
        assert(!failed && actual == reference(cursor, index, root));
        checks++;
    }
}

static void owned_symbol_trace_keeps_spelling(void) {
    CettaOpenEquationCursor cursor = {0};
    Arena moved;
    arena_init_detached(&cursor.region);
    arena_init_detached(&moved);
    const char *text = "unregistered-symbol-owned-by-collection-region";
    Atom *source = atom_symbol_data(&cursor.region, text);
    assert(source && source->symbol_number == SYMBOL_ID_NONE);
    OemCollect collect = {.from_identity = cursor.region.identity,
                          .to = &moved};
    assert(oem_trace_atom(&cursor, &collect, source));
    assert(strcmp(atom_symbol_bytes(source), text) == 0);
    assert(collect.reached_young == sizeof(Atom) +
        sizeof(CettaSymbolSpelling) + strlen(text) + 1u);
    free(collect.keys); free(collect.values);
    collect.keys = NULL; collect.values = NULL;
    collect.cap = collect.len = 0u;
    Atom *copy = oem_collect_atom(&cursor, &collect, source);
    assert(copy && copy != source);
    arena_free(&cursor.region);
    assert(strcmp(atom_symbol_bytes(copy), text) == 0);
    assert(copy->symbol_number == SYMBOL_ID_NONE);
    free(collect.keys); free(collect.values); free(cursor.walk);
    arena_free(&moved);
    checks += 5u;
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    owned_symbol_trace_keeps_spelling();
    CettaOpenEquationProgram program = {0};
    program.spelling = symbol_intern_cstr(&symbols, "v");
    CettaOpenEquationCursor cursor = {0};
    cursor.program = &program;
    cursor.epoch = (CettaFrameIdentity){17u};
    arena_init_detached(&cursor.names);
    arena_init_detached(&cursor.region);
    Arena source;
    arena_init_detached(&source);
    Atom *tag = atom_symbol(&source, "Node");
    Atom *done = atom_symbol(&source, "done");
    Atom *x = oem_new_cell(&cursor);
    Atom *y = oem_new_cell(&cursor);
    Atom *z = oem_new_cell(&cursor);
    Atom *w = oem_new_cell(&cursor);
    assert(x && y && z && w);
    Atom *base = atom_expr3(&cursor.region, tag, x, y);
    Atom *closed[128];
    closed[0] = base;
    for (unsigned index = 1u; index < 128u; index++)
        closed[index] = atom_expr3(&cursor.region, tag, base, closed[index - 1u]);
    Atom *root = atom_expr3(&cursor.region, tag, closed[127], z);
    /* Two syntax variables force the general walker even when x is bound. */
    Atom *shared = atom_expr3(&cursor.region, tag, z, x);
    Atom *open[64];
    open[0] = shared;
    for (unsigned index = 1u; index < 64u; index++)
        open[index] = atom_expr2(&cursor.region, tag, open[index - 1u]);
    Atom *dag = atom_expr3(&cursor.region, tag, open[63], shared);
    OemFrame outer = {
        .mark = arena_mark(&cursor.region), .cell_mark = cursor.cell_len,
    };
    cursor.frames = &outer;
    cursor.frame_len = 1u;
    for (unsigned trial = 0u; trial < 1000u; trial++) {
        assert(oem_unify(&cursor, x, done) == OEM_UNIFY_OK);
        assert(oem_unify(&cursor, y, done) == OEM_UNIFY_OK);
        compare(&cursor, root);
        assert(cursor.ground_fact_cap && !oem_known_ground(&cursor, root));
        unsigned remembered = 0u;
        for (unsigned index = 0u; index < 128u; index++)
            remembered += oem_known_ground(&cursor, closed[index]);
        assert(remembered); /* Closed components survive an open sibling. */
        compare(&cursor, dag);
        for (unsigned index = 0u; index < 64u; index++)
            assert(!oem_known_ground(&cursor, open[index]));
        assert(!oem_known_ground(&cursor, shared));
        Atom *via_w = atom_expr2(&cursor.region, tag, w);
        assert(oem_unify(&cursor, z, via_w) == OEM_UNIFY_OK);
        compare(&cursor, dag);
        bool failed = false;
        assert(oem_occurs(&cursor, 3u, open[63], &failed) && !failed);
        checks++;
        oem_restore(&cursor, &outer);
        assert(!cursor.ground_facts_live);
        compare(&cursor, closed[127]);
        assert(oem_unify(&cursor, x, base) == OEM_UNIFY_FAIL);
        compare(&cursor, root);
    }
    assert(oem_unify(&cursor, x, done) == OEM_UNIFY_OK);
    assert(oem_unify(&cursor, y, done) == OEM_UNIFY_OK);
    assert(oem_reserve((void **)&cursor.walk, &cursor.walk_cap, 8192u,
                       sizeof(*cursor.walk)));
    /* Both initial completion storage and a later growth may decline. */
    for (unsigned attempt = 0u; attempt < 2u; attempt++) {
        oem_forget_ground_facts(&cursor);
        free(cursor.occurs_close);
        cursor.occurs_close = NULL;
        cursor.occurs_close_cap = 0u;
        if (attempt)
            assert(oem_reserve((void **)&cursor.occurs_close,
                &cursor.occurs_close_cap, 16u, sizeof(*cursor.occurs_close)));
        unsigned before = rejected_reallocations;
        reject_reallocation = true;
        bool failed = false;
        bool actual = oem_occurs(&cursor, 3u, root, &failed);
        reject_reallocation = false;
        assert(rejected_reallocations == before + 1u && !failed &&
               actual == reference(&cursor, 3u, root));
        checks++;
    }
    printf("PASS: %llu independent occurrence checks; closed components with "
           "open siblings, duplicate open DAGs, aliases, cycles, rollback "
           "and optional-allocation failure\n", (unsigned long long)checks);
    free(cursor.cells);
    free(cursor.cell_names);
    free(cursor.trail);
    free(cursor.pairs);
    free(cursor.walk);
    free(cursor.occurs_close);
    free(cursor.occurs_seen.buckets);
    free(cursor.occurs_seen.dense);
    free(cursor.ground_facts);
    arena_free(&source);
    arena_free(&cursor.region);
    arena_free(&cursor.names);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
