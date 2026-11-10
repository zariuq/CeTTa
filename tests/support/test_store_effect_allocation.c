#include "space.h"
#include "symbol.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool refuse_allocation;

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

    space_free(&space);
    term_universe_free(&universe);
    arena_free(&scratch);
    arena_free(&persistent);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    puts("PASS: store-effect allocation refusal publishes no selected, appended, or positioned update");
    return 0;
}
