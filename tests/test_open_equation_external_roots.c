/* Exercise the actual collector with a scoped opaque image whose record
 * and source arena have retired before the region's next collection. */
#ifndef CETTA_OEM_EXTERNAL_ROOT_SOURCE
#define CETTA_OEM_EXTERNAL_ROOT_SOURCE "open_equation_machine.c"
#endif
#include CETTA_OEM_EXTERNAL_ROOT_SOURCE
#include <assert.h>

typedef struct {
    unsigned references, final_releases;
} ExternalRootOwner;

static void external_root_retain(void *opaque) {
    ((ExternalRootOwner *)opaque)->references++;
}

static void external_root_release(void *opaque) {
    ExternalRootOwner *owner = opaque;
    assert(owner->references);
    if (--owner->references == 0u)
        owner->final_releases++;
}

static void test_retired_opaque_image(bool nested) {
    Arena source, persistent, target;
    arena_init_detached(&source);
    arena_init_detached(&persistent);
    arena_init_detached(&target);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe,
                                      TERM_UNIVERSE_COLLECTION_RECLAIM);
    ExternalRootOwner owner = {0};
    Atom *managed = atom_native_handle_identifier(&source, 73, &owner,
        external_root_retain, external_root_release);
    assert(managed && owner.references == 1u);
    assert(!term_universe_atom_is_stable(managed));
    Atom *payload = nested ? atom_expr3(&source,
        atom_symbol(&source, "external-alias"), managed, managed) : managed;
    AtomId id = term_universe_store_atom_id(&universe, &source, payload);
    assert(id != CETTA_ATOM_ID_NONE && !tu_hdr_stored(&universe, id));
    assert(owner.references == 2u);
    Space input;
    space_init_with_universe(&input, &universe);
    space_add_atom_id(&input, id);
    assert(space_length64(&input) == 1u);
    CETTA_FRAME_IDENTITY_SCOPE(epoch);
    CettaOpenEquationProgram program = {
        .spelling = symbol_intern_cstr(g_symbols, "external-root-cell"),
    };
    CettaOpenEquationCursor cursor = {
        .program = &program,
        .epoch = cetta_frame_identity_scope_fresh(&epoch),
    };
    arena_init_detached(&cursor.region);
    arena_init_detached(&cursor.names);
    Atom *cell = oem_new_cell(&cursor);
    assert(cell && cursor.cell_len == 1u);
    SpaceOccurrenceCursor rows;
    assert(space_occurrence_cursor_init(&input, cell, &rows));
    AtomId offered = CETTA_ATOM_ID_NONE;
    assert(space_occurrence_cursor_next_id(&rows, NULL, &offered) ==
           SPACE_OCCURRENCE_CURSOR_ITEM && offered == id);
    /* Use the production stored-row importer and unifier. It acquires the
     * scoped image and binds the actual cursor cell to that outside value. */
    assert(oem_try_stored(&cursor, cell, &universe, offered) == OEM_RUN_CALLED);
    Atom *root = oem_deref(&cursor, cell);
    Atom *root_leaf = nested ? root->expr.elems[1] : root;
    assert(root && (!nested || root->expr.elems[1] == root->expr.elems[2]) &&
        atom_native_handle_identifier_matches(root_leaf, &owner,
        external_root_retain, external_root_release));
    /* The producing match is complete and its source Space retires; only
     * the cursor cell's scoped import remains an observer of the image. */
    space_occurrence_cursor_release(&rows);
    space_free(&input);
    assert(universe.root_provider_len == 0u && !persistent.finalizers);
    arena_free(&source);
    assert(owner.references != 0u);
    /* The receiving scope owns the image independently of its record. */
    TermUniverseCollectionReport report;
    assert(term_universe_collect(&universe, &report));
    assert(report.reclaimed != 0u && owner.references != 0u);
    assert(atom_native_handle_identifier_matches(root_leaf, &owner,
        external_root_retain, external_root_release));

    uint8_t revocable[] = {0u};
    OemCollect collect = {.from_identity = cursor.region.identity,
                          .to = &target, .revocable = revocable};
    assert(oem_trace_atom(&cursor, &collect, cell));
    Atom *moved = oem_collect_atom(&cursor, &collect, cell);
    assert(moved);
    /* This is the real collector's commit boundary. Check the lease before
     * reading MOVED, so a broken sharing path fails without a dangling read. */
    arena_reset(&cursor.region, (ArenaMark){0});
    assert(owner.references != 0u && owner.final_releases == 0u);
    Atom *moved_leaf = nested ? moved->expr.elems[1] : moved;
    assert((!nested || moved->expr.elems[1] == moved->expr.elems[2]) &&
        atom_native_handle_identifier_matches(moved_leaf, &owner,
        external_root_retain, external_root_release));
    assert(moved_leaf->kind == ATOM_GROUNDED && moved_leaf->ground.gkind == GV_INT &&
           moved_leaf->ground.ival == 73);
    arena_free(&cursor.region);
    arena_free(&cursor.names);
    arena_free(&target);
    assert(owner.references == 0u && owner.final_releases == 1u);
    term_universe_free(&universe);
    arena_free(&persistent);
    free(collect.keys);
    free(collect.values);
    free(collect.marked);
    free(cursor.walk);
    free(cursor.cells);
    free(cursor.cell_names);
    free(cursor.resolve_stack);
    free(cursor.resolve_results);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    test_retired_opaque_image(false);
    test_retired_opaque_image(true);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: collection retains an external opaque root after its original record and scoped source retire");
    return 0;
}
