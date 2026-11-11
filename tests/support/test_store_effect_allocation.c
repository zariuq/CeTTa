#include "space.h"
#include "symbol.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool refuse_allocation;
static bool refuse_fallible_allocation;

void *__real_cetta_fallible_malloc(size_t bytes);
void *__wrap_cetta_fallible_malloc(size_t bytes) {
    if (refuse_fallible_allocation) {
        refuse_fallible_allocation = false;
        return NULL;
    }
    return __real_cetta_fallible_malloc(bytes);
}

void *__real_malloc(size_t bytes);
void *__wrap_malloc(size_t bytes) {
    if (refuse_allocation) {
        refuse_allocation = false;
        return NULL;
    }
    return __real_malloc(bytes);
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena persistent, scratch;
    arena_init(&persistent);
    arena_init(&scratch);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    Space space;
    space_init_with_universe(&space, &universe);
    Atom *a_atom = atom_symbol(&scratch, "store-effect-allocation-a");
    Atom *b_atom = atom_symbol(&scratch, "store-effect-allocation-b");
    AtomId a = term_universe_store_atom_id(&universe, NULL, a_atom);
    AtomId b = term_universe_store_atom_id(&universe, NULL, b_atom);
    assert(a != CETTA_ATOM_ID_NONE && b != CETTA_ATOM_ID_NONE);
    space_add_atom_id(&space, a);

    /* Receipt reservation fails before the selected row can be published. */
    uint8_t *mask = cetta_malloc(1u);
    mask[0] = 1u;
    SpaceStoreEffect effect;
    SpaceStoreReceipt receipt;
    space_store_effect_init(&effect);
    space_store_receipt_init(&receipt);
    uint64_t revision = space_revision(&space);
    refuse_allocation = true;
    bool prepared = space_store_effect_prepare_remove_mask(
        &effect, &space, mask, 1u, 1u, NULL, &receipt);
    assert(!refuse_allocation && !prepared);
    assert(space_length64(&space) == 1u && space_revision(&space) == revision);
    assert(space_get_atom_id_at64(&space, 0u) == a && receipt.len == 0u);
    /* Failed preparation left mask ownership with the caller. */
    free(mask);
    space_store_effect_dispose(&effect);
    space_store_receipt_dispose(&receipt);

    /* An append after preparation needs a larger mask.  Its allocation also
     * finishes before publication, so failure leaves both rows intact. */
    mask = cetta_malloc(1u);
    mask[0] = 1u;
    space_store_effect_init(&effect);
    assert(space_store_effect_prepare_remove_mask(
        &effect, &space, mask, 1u, 1u, NULL, NULL));
    space_add_atom_id(&space, b);
    revision = space_revision(&space);
    refuse_allocation = true;
    CettaCount removed = 99u;
    assert(space_store_effect_publish(&effect, NULL, &removed) ==
           SPACE_STORE_PUBLISH_ERROR);
    assert(!refuse_allocation && removed == 0u);
    assert(space_length64(&space) == 2u && space_revision(&space) == revision);
    assert(space_get_atom_id_at64(&space, 0u) == a &&
           space_get_atom_id_at64(&space, 1u) == b);
    space_store_effect_dispose(&effect);

    /* Physical-row preparation likewise refuses before either row changes. */
    CettaIndex selected_position = 0u;
    space_store_effect_init(&effect);
    revision = space_revision(&space);
    refuse_allocation = true;
    prepared = space_store_effect_prepare_remove_positions(
        &effect, &space, &selected_position, 1u, NULL, NULL);
    assert(!refuse_allocation && !prepared);
    assert(space_length64(&space) == 2u && space_revision(&space) == revision);
    assert(space_get_atom_id_at64(&space, 0u) == a &&
           space_get_atom_id_at64(&space, 1u) == b);
    space_store_effect_dispose(&effect);

    /* Immediate mixed removal finishes its fallible row preparation before
     * either the relation fact or its equal row copy can disappear. */
    Space mixed;
    space_init_with_universe(&mixed, &universe);
    mixed.dense_from_first = true;
    Atom *edge_head_atom = atom_symbol(&scratch, "store-effect-allocation-edge");
    SymbolId edge = atom_symbol_current_id(edge_head_atom);
    AtomId edge_head = term_universe_store_atom_id(&universe, NULL, edge_head_atom);
    Atom *row = atom_expr3(&scratch, edge_head_atom, a_atom, b_atom);
    AtomId row_id = term_universe_store_atom_id(&universe, NULL, row);
    AtomId columns[2] = {a, b};
    assert(edge_head != CETTA_ATOM_ID_NONE && row_id != CETTA_ATOM_ID_NONE);
    assert(space_add_dense_tuples(&mixed, edge, edge_head, 2u, columns, 1u));
    space_add_atom_id(&mixed, row_id);
    assert(space_dense_facts(&mixed) == 1u && space_length64(&mixed) == 2u);
    revision = space_revision(&mixed);
    refuse_fallible_allocation = true;
    CettaCount rows = 99u;
    assert(!space_remove_exact_ids_immediate(
        &mixed, edge, &row_id, 1u, row, &removed, &rows));
    assert(!refuse_fallible_allocation && removed == 0u && rows == 0u);
    assert(space_dense_facts(&mixed) == 1u && space_length64(&mixed) == 2u &&
           space_revision(&mixed) == revision);
    assert(space_remove_exact_ids_immediate(
        &mixed, edge, &row_id, 1u, row, &removed, &rows));
    assert(removed == 2u && rows == 1u &&
           space_dense_facts(&mixed) == 0u && space_length64(&mixed) == 0u);
    space_free(&mixed);

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: store-effect allocation refusal publishes no selected, appended, positioned, or immediate mixed update");
    return 0;
}
