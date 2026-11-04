#include "eval.h"
#include "state_cell.h"
#include "symbol.h"

#include <assert.h>
#include <stdio.h>

static Atom *one(Space *root, Arena *arena, Atom *request) {
    ResultSet result;
    result_set_init(&result);
    eval_top(root, arena, request, &result);
    assert(result.len == 1u && !atom_is_error(result.items[0]));
    Atom *answer = result.items[0];
    result_set_free(&result);
    return answer;
}

static Atom *new_cell(Space *root, Arena *arena, Atom *seed) {
    Atom *reference = one(root, arena, atom_expr2(arena,
        atom_symbol_id(arena, g_builtin_syms.new_state), seed));
    assert(reference->kind == ATOM_GROUNDED && reference->ground.gkind == GV_STATE &&
           state_cell_is_managed(reference->ground.ptr));
    return reference;
}

static void publish(Space *root, Arena *arena, Atom *reference, Atom *value) {
    Atom *answer = one(root, arena, atom_expr3(arena,
        atom_symbol_id(arena, g_builtin_syms.change_state_bang), reference, value));
    assert(answer->kind == ATOM_GROUNDED && answer->ground.gkind == GV_STATE &&
           answer->ground.ptr == reference->ground.ptr);
}

static void collect(Space *root) {
    /* The universe entry is the runtime's actual graph-collection boundary.
     * Subsequent passes also drain records queued by released image edges. */
    for (unsigned i = 0u; i < 3u; i++) {
        assert(space_reclaim_unheld());
        assert(term_universe_collect(root->native.universe, NULL));
    }
    assert(space_reclaim_unheld());
}

static void same_heap(StateCellDiagnostics before) {
    StateCellDiagnostics after = state_cell_diagnostics();
    if (after.nodes != before.nodes || after.images != before.images ||
        after.image_bytes != before.image_bytes || after.queued != 0u)
        fprintf(stderr, "State heap before=%zu/%zu/%zu after=%zu/%zu/%zu queued=%zu\n",
            before.nodes, before.images, before.image_bytes,
            after.nodes, after.images, after.image_bytes, after.queued);
    assert(after.nodes == before.nodes && after.images == before.images &&
           after.image_bytes == before.image_bytes && after.queued == 0u);
}

static void state_cycle(Space *root, Atom *seed, bool mutual, bool retained) {
    StateCellDiagnostics before = state_cell_diagnostics();
    Arena creation;
    arena_init_detached(&creation);
    Atom *a = new_cell(root, &creation, seed);
    Atom *b = mutual ? new_cell(root, &creation, seed) : a;
    StateCell *first = a->ground.ptr, *second = b->ground.ptr;
    publish(root, &creation, a, b);
    if (mutual)
        publish(root, &creation, b, a);
    StateCellSnapshot reader = {0};
    if (retained)
        assert(state_cell_snapshot_acquire(first, &reader));
    arena_free(&creation);
    collect(root);
    if (retained) {
        assert(state_cell_is_managed(first) && state_cell_is_managed(second));
        assert(reader.value->ground.gkind == GV_STATE && reader.value->ground.ptr == second);
        StateCellSnapshot current = {0};
        assert(state_cell_snapshot_acquire(second, &current));
        assert(current.value->ground.gkind == GV_STATE && current.value->ground.ptr == first);
        state_cell_snapshot_release(&current);
        assert(state_cell_diagnostics().nodes == before.nodes + (mutual ? 2u : 1u));
        state_cell_snapshot_release(&reader);
        collect(root);
    }
    same_heap(before);
}

/* A State image owns a Space directly, or through a native capture. The
 * Space's opaque row owns the same State; a retained image roots the cycle. */
static void mixed_cycle(Space *root, Arena *persistent, Atom *seed,
                         bool capture, bool retained, bool old_image) {
    StateCellDiagnostics before = state_cell_diagnostics();
    const size_t providers = root->native.universe->root_provider_len;
    Arena creation;
    arena_init_detached(&creation);
    Atom *a = new_cell(root, &creation, seed);
    StateCell *cell = a->ground.ptr;
    Atom *space_ref = one(root, &creation, atom_expr(&creation,
        (Atom *[]){atom_symbol_id(&creation, g_builtin_syms.new_space)}, 1u));
    assert(space_ref->kind == ATOM_GROUNDED && space_ref->ground.gkind == GV_SPACE);
    Space *space = space_ref->ground.ptr;
    Atom *row = atom_expr2(&creation, atom_symbol(&creation, "managed-cycle-row"), a);
    assert(space_admit_atom(space, &creation, row));
    Atom *payload = space_ref;
    if (capture) {
        CaptureClosure *closure = arena_alloc(persistent, sizeof(*closure));
        *closure = (CaptureClosure){.space_ptr = space};
        payload = atom_capture(&creation, closure);
    }
    publish(root, &creation, a, payload);
    StateCellSnapshot reader = {0};
    if (retained)
        assert(state_cell_snapshot_acquire(cell, &reader));
    if (old_image)
        publish(root, &creation, a, atom_int(&creation, 23));
    arena_free(&creation);
    collect(root);
    if (retained) {
        assert(state_cell_is_managed(cell) && space_length64(space) == 1u);
        assert(reader.value->kind == ATOM_GROUNDED &&
               reader.value->ground.gkind == (capture ? GV_CAPTURE : GV_SPACE));
        Arena viewing;
        arena_init_detached(&viewing);
        Atom *kept = space_view_at64(space, 0u, &viewing);
        assert(kept && kept->kind == ATOM_EXPR && kept->expr.len == 2u &&
               kept->expr.elems[1]->ground.gkind == GV_STATE &&
               kept->expr.elems[1]->ground.ptr == cell);
        if (old_image) {
            StateCellSnapshot current = {0};
            assert(state_cell_snapshot_acquire(cell, &current));
            assert(current.value->ground.gkind == GV_INT && current.value->ground.ival == 23);
            state_cell_snapshot_release(&current);
        }
        arena_free(&viewing);
        state_cell_snapshot_release(&reader);
        collect(root);
    }
    same_heap(before);
    assert(root->native.universe->root_provider_len == providers);
}

int main(void) {
    SymbolTable symbols;
    VarInternTable variables;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = NULL;
    eval_set_library_context(NULL);
    Arena persistent;
    arena_init_detached(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    Space root;
    space_init_with_universe(&root, &universe);
    Atom *seed = atom_symbol(&persistent, "managed-cycle-seed");
    Atom *declaration = atom_expr3(&persistent,
        atom_symbol_id(&persistent, g_builtin_syms.colon), seed, atom_atom_type(&persistent));
    assert(space_admit_atom(&root, &persistent, declaration));
    collect(&root);
    for (unsigned retained = 0u; retained < 2u; retained++) {
        state_cycle(&root, seed, false, retained);
        state_cycle(&root, seed, true, retained);
        mixed_cycle(&root, &persistent, seed, false, retained, false);
        mixed_cycle(&root, &persistent, seed, true, retained, false);
    }
    mixed_cycle(&root, &persistent, seed, false, true, true);
    mixed_cycle(&root, &persistent, seed, true, true, true);
    assert(state_cell_diagnostics().nodes == 0u && state_cell_diagnostics().images == 0u);
    space_free(&root);
    term_universe_free(&universe);
    arena_free(&persistent);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    g_var_intern = NULL;
    g_symbols = NULL;
    puts("PASS: actual State/self/mutual/Space/capture cycles retire; retained current and old image readers preserve exact identities and rows");
    return 0;
}
