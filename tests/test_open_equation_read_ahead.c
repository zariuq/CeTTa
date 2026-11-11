/* Compile the actual worker here to exercise its private fetch and copy
 * primitives with real occurrence cursors and arena-owned values. */
#ifndef CETTA_OEM_READ_AHEAD_SOURCE
#define CETTA_OEM_READ_AHEAD_SOURCE "open_equation_machine.c"
#endif
#include <stdlib.h>
#include <stdbool.h>
/* Inject only the collector translation unit's fallible staging allocations.
 * The arena allocator and the rest of the engine keep their normal policy. */
static size_t collection_allocation_index,collection_refuse_at;
static bool collection_allocation_tracking;
static bool collection_allocation_refused(void) {
    return collection_allocation_tracking &&
        ++collection_allocation_index==collection_refuse_at;
}
static void *collection_malloc(size_t bytes) {
    return collection_allocation_refused() ? NULL : malloc(bytes);
}
static void *collection_calloc(size_t count,size_t bytes) {
    return collection_allocation_refused() ? NULL : calloc(count,bytes);
}
static void *collection_realloc(void *old,size_t bytes) {
    return collection_allocation_refused() ? NULL : realloc(old,bytes);
}
#define malloc collection_malloc
#define calloc collection_calloc
#define realloc collection_realloc
#include CETTA_OEM_READ_AHEAD_SOURCE
#undef malloc
#undef calloc
#undef realloc
#include "parser.h"
#include <assert.h>

static Atom *parse_boundary(Arena *arena, const char *text) {
    size_t position = 0u;
    Atom *atom = parse_sexpr(arena, text, &position);
    assert(atom);
    return atom;
}

typedef struct {
    unsigned references, final_releases;
} GcOwnerProbe;

static void gc_owner_retain(void *opaque) {
    ((GcOwnerProbe *)opaque)->references++;
}

static void gc_owner_release(void *opaque) {
    GcOwnerProbe *owner = opaque;
    assert(owner->references);
    if (--owner->references == 0u)
        owner->final_releases++;
}

static void test_collection_symbol_policy(void) {
    for (unsigned own_symbols = 0u; own_symbols < 2u; own_symbols++) {
        for (unsigned spelled = 0u; spelled < 2u; spelled++) {
#if !CETTA_MANAGED_SYMBOLS
            if (spelled)
                continue;
#endif
            Arena source, target;
            arena_init(&source);
            arena_init(&target);
            arena_set_own_symbols(&target, own_symbols != 0u);
            char text[96];
            snprintf(text, sizeof(text), "gc-symbol-policy-%u-%u",
                     own_symbols, spelled);
            Atom *symbol = spelled
                ? atom_symbol_data_n(&source, text, (uint32_t)strlen(text))
                : atom_symbol(&source, text);
            if (spelled)
                assert(atom_symbol_is_spelled(symbol));
            SymbolId id = atom_symbol_current_id(symbol);
            if (spelled)
                assert(id == SYMBOL_ID_NONE);
            CettaOpenEquationCursor cursor = {0};
            OemCollect collect = {.from_identity = source.identity, .to = &target};
            assert(oem_trace_atom(&cursor, &collect, symbol));
            /* The collector starts copying with a fresh alias table. */
            free(collect.keys);
            free(collect.values);
            collect.keys = NULL;
            collect.values = NULL;
            collect.cap = collect.len = 0u;
            Atom *copy = oem_collect_atom(&cursor, &collect, symbol);
            assert(copy && copy->kind == ATOM_SYMBOL && atom_symbol_current_id(copy) == id);
            assert(copy == oem_collect_atom(&cursor, &collect, symbol));
            if (spelled)
                assert(atom_symbol_is_spelled(symbol));
            if (own_symbols || spelled)
                assert(copy->arena_id == target.identity);
            else
                assert(copy == atom_symbol_canonical(id));
            arena_free(&source);
            assert(copy->kind == ATOM_SYMBOL && atom_symbol_equals_cstr(copy, text));
            arena_free(&target);
            free(collect.keys);
            free(collect.values);
            free(cursor.walk);
        }
    }
    puts("PASS: collection preserves canonical and owned symbol policies with shared aliases");
}

static void test_collection_leaf_ownership(void) {
    Arena source, target;
    arena_init(&source);
    arena_init(&target);
    CETTA_FRAME_IDENTITY_SCOPE(epoch);
    CettaOpenEquationCursor cursor = {
        .epoch = cetta_frame_identity_scope_fresh(&epoch),
    };
    enum { PAYLOAD = 65536, LINKS = 128 };
    char bytes[PAYLOAD];
    memset(bytes, 'q', sizeof(bytes));
    bytes[37] = '\0';
    Atom *string = atom_string_n(&source, bytes, sizeof(bytes));
    Atom *children[LINKS];
    for (unsigned i = 0u; i < LINKS; i++)
        children[i] = string;
    Atom *graph = atom_expr(&source, children, LINKS);
    Atom *plain = atom_int(&source, 17);
    double negative_zero = -0.0;
    Atom *floating = atom_float(&source, negative_zero);
    Atom *truth = atom_bool(&source, true);
    Atom *name = parse_boundary(&source, "(gc-name 77)");
    Atom *variable = atom_var_with_name_key(&source, name, fresh_var_id());
    VarId variable_id = variable->var_id;
    GcOwnerProbe first = {0}, second = {0};
    Atom *managed = atom_native_handle_identifier(&source, 42, &first,
        gc_owner_retain, gc_owner_release);
    Atom *other = atom_native_handle_identifier(&source, 42, &second,
        gc_owner_retain, gc_owner_release);
    Atom *marked = atom_he_evaluated_copy(&source,
        atom_expr2(&source, plain, managed));
    assert(atom_is_he_evaluated(marked));
    Atom *roots[] = {graph, plain, floating, truth, variable, managed, other, marked};
    OemCollect collect = {.from_identity = source.identity, .to = &target,
                          .first_cap = 16u, .record = true};
    for (unsigned i = 0u; i < sizeof(roots) / sizeof(*roots); i++)
        assert(oem_trace_atom(&cursor, &collect, roots[i]));
    /* A declined trace must restore both forwarding fields and spelling. */
    oem_collect_decline(&collect, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    for (unsigned i = 0u; i < sizeof(roots) / sizeof(*roots); i++)
        assert(roots[i]->name_key == (roots[i] == variable ? name : NULL));
    assert(atom_is_he_evaluated(marked));
    assert(first.references == 1u && second.references == 1u);
    collect = (OemCollect){.from_identity = source.identity, .to = &target,
                           .first_cap = 16u, .record = true};
    for (unsigned i = 0u; i < sizeof(roots) / sizeof(*roots); i++)
        assert(oem_trace_atom(&cursor, &collect, roots[i]));
    /* The real collector finishes tracing before starting its copy table. */
    free(collect.keys);
    free(collect.values);
    free(collect.marked);
    collect.keys = NULL;
    collect.values = NULL;
    collect.marked = NULL;
    collect.cap = collect.len = 0u;
    collect.marked_len = collect.marked_cap = 0u;
    size_t before = arena_accounted_live_bytes(&target);
    Atom *moved_graph = oem_collect_atom(&cursor, &collect, graph);
    assert(moved_graph && moved_graph->expr.len == LINKS);
    assert(arena_accounted_live_bytes(&target) - before <=
        sizeof(bytes) + 1u + (LINKS + 4u) * sizeof(Atom) + LINKS * sizeof(Atom *));
    for (unsigned i = 0u; i < LINKS; i++)
        assert(moved_graph->expr.elems[i] == moved_graph->expr.elems[0]);
    Atom *moved_plain = oem_collect_atom(&cursor, &collect, plain);
    assert(moved_plain == oem_collect_atom(&cursor, &collect, plain));
    Atom *moved_float = oem_collect_atom(&cursor, &collect, floating);
    Atom *moved_truth = oem_collect_atom(&cursor, &collect, truth);
    Atom *moved_variable = oem_collect_atom(&cursor, &collect, variable);
    assert(moved_variable && moved_variable->kind == ATOM_VAR &&
           var_id_eq(moved_variable->var_id, variable_id) &&
           moved_variable->name_key && atom_eq(moved_variable->name_key, name));
    assert(moved_variable == oem_collect_atom(&cursor, &collect, variable));
    Atom *moved_managed = oem_collect_atom(&cursor, &collect, managed);
    Atom *moved_other = oem_collect_atom(&cursor, &collect, other);
    assert(moved_managed == oem_collect_atom(&cursor, &collect, managed));
    assert(atom_native_handle_identifier_matches(moved_managed, &first,
        gc_owner_retain, gc_owner_release));
    assert(atom_native_handle_identifier_matches(moved_other, &second,
        gc_owner_retain, gc_owner_release));
    Atom *marked_copy = oem_collect_atom(&cursor, &collect, marked);
    assert(atom_is_he_evaluated(marked_copy));
    assert(marked_copy->expr.elems[0] == moved_plain &&
           marked_copy->expr.elems[1] == moved_managed);
    arena_free(&source);
    assert(first.references == 1u && second.references == 1u);
    assert(first.final_releases == 0u && second.final_releases == 0u);
    assert(moved_graph->expr.elems[0]->ground.slen == sizeof(bytes) &&
           memcmp(moved_graph->expr.elems[0]->ground.sval, bytes, sizeof(bytes)) == 0);
    assert(moved_plain->ground.gkind == GV_INT && moved_plain->ground.ival == 17);
    assert(moved_float->ground.gkind == GV_FLOAT &&
           memcmp(&moved_float->ground.fval, &negative_zero, sizeof(double)) == 0);
    assert(moved_truth->ground.gkind == GV_BOOL && moved_truth->ground.bval);
    assert(moved_variable->name_key->expr.elems[1]->ground.ival == 77);
    arena_free(&target);
    assert(first.references == 0u && second.references == 0u);
    assert(first.final_releases == 1u && second.final_releases == 1u);
    free(collect.keys);
    free(collect.values);
    free(cursor.walk);
    free(cursor.resolve_stack);
    free(cursor.resolve_results);
    puts("PASS: collection preserves leaf aliases, large shared bytes, variable spelling, managed owners and HE marks");
}

static size_t test_collection_staging_refusal(size_t refuse) {
    CettaOpenEquationCursor cursor={0};arena_init(&cursor.region);
    GcOwnerProbe owner={0};
    Atom *left=atom_symbol_data_n(&cursor.region,"staged-left",11u);
    Atom *right=atom_symbol_data_n(&cursor.region,"staged-right",12u);
    Atom *held=atom_native_handle_identifier(&cursor.region,42,&owner,
        gc_owner_retain,gc_owner_release);
    Atom *root=atom_expr3(&cursor.region,left,right,held);
    cursor.destination=root;
    collection_allocation_index=0u;collection_refuse_at=refuse;
    collection_allocation_tracking=true;
    bool complete=oem_collect_generation(&cursor,0u,true,true);
    collection_allocation_tracking=false;
    size_t reached=collection_allocation_index;
    if(!refuse)assert(complete);
    if(refuse)assert(reached>=refuse);
    if(complete) {
        assert(cursor.old_ready && cursor.stats.collections==1u);
        assert(atom_is_symbol(cursor.destination->expr.elems[0],"staged-left"));
        assert(atom_is_symbol(cursor.destination->expr.elems[1],"staged-right"));
        assert(atom_native_handle_identifier_matches(cursor.destination->expr.elems[2],
            &owner,gc_owner_retain,gc_owner_release));
    } else {
        /* No new root/generation is published. This does not claim that the
         * failed cursor can resume: temporary forwarding headers may change. */
        assert(cursor.destination==root && !cursor.old_ready && !cursor.stats.collections);
        assert(atom_is_symbol(left,"staged-left") && atom_is_symbol(right,"staged-right"));
    }
    assert(owner.references==1u && !owner.final_releases);
    if(cursor.old_ready)arena_free(&cursor.old);
    arena_free(&cursor.region);
    assert(!owner.references && owner.final_releases==1u);
    free(cursor.walk);free(cursor.resolve_stack);free(cursor.resolve_results);
    return reached;
}

static void test_collection_ordered_roots(void) {
    Arena source,target;arena_init(&source);arena_init(&target);
    Atom *left=atom_symbol_data_n(&source,"left-label",10u);
    Atom *right=atom_symbol_data_n(&source,"right-label",11u);
    Atom *a=atom_expr3(&source,left,right,left);
    Atom *b=atom_expr2(&source,right,a);
    Atom **roots=arena_alloc(&source,3u*sizeof(*roots));assert(roots);
    roots[0]=a;roots[1]=a;roots[2]=b;
    CettaOpenEquationCursor cursor={0};
    OemCollect collect={.from_identity=source.identity,.to=&target};
    for(unsigned i=0u;i<3u;i++) assert(oem_trace_atom(&cursor,&collect,roots[i]));
    free(collect.keys);free(collect.values);
    collect.keys=NULL;collect.values=NULL;collect.cap=collect.len=0u;
    bool ok=true;
    Atom **moved=oem_collect_slots(&cursor,&collect,roots,3u,false,&ok);
    assert(ok && moved && moved[0]==moved[1] && moved[0]!=moved[2]);
    assert(moved[0]->expr.len==3u && moved[2]->expr.len==2u);
    assert(moved[0]->expr.elems[0]==moved[0]->expr.elems[2]);
    assert(moved[2]->expr.elems[0]==moved[0]->expr.elems[1]);
    assert(moved[2]->expr.elems[1]==moved[0]);
    arena_free(&source);
    assert(atom_is_symbol(moved[0]->expr.elems[0],"left-label"));
    assert(atom_is_symbol(moved[0]->expr.elems[1],"right-label"));
    arena_free(&target);free(collect.keys);free(collect.values);
    free(cursor.walk);free(cursor.resolve_stack);free(cursor.resolve_results);
    puts("PASS: collection preserves ordered roots, duplicate occurrences and labelled shared children");
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    test_collection_symbol_policy();
    test_collection_ordered_roots();
    test_collection_leaf_ownership();
    size_t allocations=test_collection_staging_refusal(0u);
    assert(allocations>0u);
    for(size_t at=1u;at<=allocations;at++)test_collection_staging_refusal(at);
    printf("PASS: %zu collector staging refusals publish no partial root and balance ownership\n",allocations);
    Arena persistent, source;
    arena_init(&persistent);
    arena_init(&source);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);
    /* Select the existing eager representation for this small fixture. */
    space.dense_from_first = true;
    Atom *warm = parse_boundary(&source, "(read-ahead-boundary-row 0 (bad -1))");
    assert(space_admit_atom(&space, &source, warm));
    AtomId warm_id = term_universe_lookup_atom_id(&universe, warm);
    assert(warm_id != CETTA_ATOM_ID_NONE);
    enum { ROWS = 512 };
    for (unsigned row = 0u; row < ROWS; row++) {
        char text[96];
        snprintf(text, sizeof(text), "(read-ahead-boundary-row 0 (bad %u))", row);
        assert(space_admit_atom(&space, &source, parse_boundary(&source, text)));
    }
    assert(space_remove_atom_id(&space, warm_id));
    Atom *coarse = parse_boundary(&source, "(read-ahead-boundary-row $key $value)");
    Atom *pattern = parse_boundary(&source, "(read-ahead-boundary-row 0 (wanted $x))");
    OemLazyRows lazy = {.universe = &universe};
    assert(space_occurrence_cursor_init(&space, coarse, &lazy.occurrences));
    assert(space_dense_facts(&space) == ROWS);
    assert(lazy.occurrences.kept);
    CettaOpenEquationCursor cursor = {0};
    uint64_t examined = 0u;
    for (unsigned offered = 0u; offered < ROWS / 33u; offered++) {
        assert(oem_lazy_rows_fetch(&cursor, &lazy, pattern));
        examined += 33u;
        assert(cursor.stats.match_candidates == examined);
        assert(lazy.next == CETTA_ATOM_ID_NONE && lazy.next_kept == examined - 1u);
    }
    assert(!oem_lazy_rows_fetch(&cursor, &lazy, pattern));
    assert(cursor.stats.match_candidates == ROWS);
    assert(!oem_lazy_rows_fetch(&cursor, &lazy, pattern));
    assert(cursor.stats.match_candidates == ROWS);
    space_occurrence_cursor_release(&lazy.occurrences);
    lazy = (OemLazyRows){.universe = &universe};
    assert(space_occurrence_cursor_init(&space, coarse, &lazy.occurrences));
    cursor = (CettaOpenEquationCursor){0};
    assert(oem_lazy_rows_fetch(&cursor, &lazy, NULL));
    assert(lazy.next == CETTA_ATOM_ID_NONE && lazy.next_kept == 0u);
    assert(cursor.stats.match_candidates == 1u);
    space_occurrence_cursor_release(&lazy.occurrences);
    space_free(&space);

    space_init_with_universe(&space, &universe);
    Atom *ordinary = parse_boundary(&source, "stored-row");
    AtomId id = term_universe_store_atom_id(&universe, NULL, ordinary);
    assert(id != CETTA_ATOM_ID_NONE);
    space_add_atom_id(&space, id);
    lazy = (OemLazyRows){.universe = &universe};
    assert(space_occurrence_cursor_init(&space,
        parse_boundary(&source, "$any-row"), &lazy.occurrences));
    cursor = (CettaOpenEquationCursor){0};
    assert(oem_lazy_rows_fetch(&cursor, &lazy, pattern));
    assert(lazy.next == id && lazy.next_kept == SIZE_MAX);
    assert(cursor.stats.match_candidates == 1u);
    assert(!oem_lazy_rows_fetch(&cursor, &lazy, pattern));
    assert(cursor.stats.match_candidates == 1u);
    space_occurrence_cursor_release(&lazy.occurrences);
    space_free(&space);
    term_universe_free(&universe);
    arena_free(&source);
    arena_free(&persistent);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: stored rows take one fetch; rejected kept rows screen 32 and return the 33rd unfiltered with exact candidate accounting");
    return 0;
}
