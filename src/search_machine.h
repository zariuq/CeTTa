#ifndef CETTA_SEARCH_MACHINE_H
#define CETTA_SEARCH_MACHINE_H

#include "atom.h"
#include "match.h"

/*
 * Context-role seam for the post-SymbolId runtime split.
 *
 * Positive example:
 *   - SearchContext owns branch-local speculative bindings, rollback marks,
 *     and scratch allocation.
 *
 * Negative example:
 *   - Repeating ad hoc `bindings_builder_save/rollback` and `arena_mark/reset`
 *     pairs across evaluator hot paths.
 */

typedef struct {
    uint32_t bindings_mark;
    ArenaMark scratch_mark;
    bool has_scratch_mark;
} ChoicePoint;

typedef struct {
    Arena *scratch_arena;
    BindingsBuilder bindings;
    Arena owned_scratch_arena;
    bool owns_scratch_arena;
} SearchContext;

bool search_context_init(SearchContext *ctx, const Bindings *base,
                         Arena *scratch_arena);
void search_context_init_owned(SearchContext *ctx, Bindings *owned,
                               Arena *scratch_arena);
void search_context_free(SearchContext *ctx);
ChoicePoint search_context_save(const SearchContext *ctx);
void search_context_rollback(SearchContext *ctx, ChoicePoint point);
Arena *search_context_scratch(SearchContext *ctx);
BindingsBuilder *search_context_builder(SearchContext *ctx);
const Bindings *search_context_bindings(const SearchContext *ctx);
void search_context_take(SearchContext *ctx, Bindings *out);

#endif /* CETTA_SEARCH_MACHINE_H */
