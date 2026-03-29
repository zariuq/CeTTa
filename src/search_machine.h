#ifndef CETTA_SEARCH_MACHINE_H
#define CETTA_SEARCH_MACHINE_H

#include "atom.h"
#include "match.h"
#include "session.h"

typedef struct Space Space;

/*
 * Machine-role seam for the post-SymbolId runtime split.
 *
 * Positive example:
 *   - SearchMachine owns branch-local speculative bindings, rollback marks,
 *     and scratch allocation.
 *   - SpaceEngine owns indexed query planning and shared-space execution.
 *
 * Negative example:
 *   - Treating local rollback and shared-space traversal as one mutable
 *     runtime object.
 */

typedef struct {
    uint32_t bindings_mark;
    ArenaMark scratch_mark;
    bool has_scratch_mark;
} SearchMachineMark;

typedef struct {
    Arena *scratch_arena;
    BindingsBuilder bindings;
    Arena owned_scratch_arena;
    bool owns_scratch_arena;
} SearchMachine;

typedef struct {
    Space *space;
} SpaceEngine;

typedef struct {
    const CettaProfile *profile;
} LanguageProfile;

typedef struct TypeContext TypeContext;

/*
 * First tranche search-machine helpers.
 *
 * Positive example:
 *   - One savepoint captures both speculative bindings and scratch allocations.
 *   - Failed branches roll back through one API instead of open-coded builder
 *     and arena resets at every call site.
 *
 * Negative example:
 *   - Repeating ad hoc `bindings_builder_save/rollback` and `arena_mark/reset`
 *     pairs across evaluator hot paths.
 */

bool search_machine_init(SearchMachine *sm, const Bindings *base,
                         Arena *scratch_arena);
void search_machine_init_owned(SearchMachine *sm, Bindings *owned,
                               Arena *scratch_arena);
void search_machine_free(SearchMachine *sm);
SearchMachineMark search_machine_save(const SearchMachine *sm);
void search_machine_rollback(SearchMachine *sm, SearchMachineMark mark);
void search_machine_commit(SearchMachine *sm);
Arena *search_machine_scratch(SearchMachine *sm);
BindingsBuilder *search_machine_builder(SearchMachine *sm);
const Bindings *search_machine_bindings(const SearchMachine *sm);
void search_machine_take(SearchMachine *sm, Bindings *out);

#endif /* CETTA_SEARCH_MACHINE_H */
