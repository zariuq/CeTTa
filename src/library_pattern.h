#ifndef CETTA_LIBRARY_PATTERN_H
#define CETTA_LIBRARY_PATTERN_H

#include "call_outcome.h"
#include "library.h"
#include "term_graph.h"

#define CETTA_NATIVE_IMPORT_PAT (1u << 27)

bool cetta_pattern_module_enabled(const CettaLibraryContext *context);
SymbolId cetta_pattern_resolve_head(const CettaLibraryContext *context, SymbolId head);
/* Controls share the relation kernel and their dialect's body continuations. */
uint32_t cetta_pattern_adapter_arity(SymbolId head);
/* Inputs have already been resolved in the caller's environment. Matching
 * never evaluates them; publication is atomic and cannot refine an existing
 * image by unification. */
CettaTermMatchStatus cetta_pattern_bind_many(Arena *arena,
    const CettaTermMatchPair *pairs, size_t count, BindingsBuilder *builder);
/* Publish a completed retrieval into the calling dialect's environment.
 * A converse retrieval binds only its explicit stored-row output; identity
 * constraints retain every protected query variable during publication. */
CettaTermMatchStatus cetta_pattern_bind_retrieval(Arena *arena,
    Atom *patterns, Atom *rows, Atom *stored_output, bool conjunction,
    BindingsBuilder *builder);
bool cetta_pattern_module_call(CettaLibraryContext *context, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out);
/* Adapter after the calling dialect has evaluated the space operand. The
 * PeTTa machine admits these queries directly as occurrence choices. */
bool eval_pattern_query_ready(Space *space, Arena *arena, Atom *head,
    Atom **arguments, uint32_t count, CettaCallOutcome *out);

#endif
