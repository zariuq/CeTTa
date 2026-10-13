#ifndef CETTA_BINDING_FRAME_SCHEMA_H
#define CETTA_BINDING_FRAME_SCHEMA_H

#include "atom.h"

typedef struct BindingsFrameSchema BindingsFrameSchema;

/* Prepare an immutable, owned inventory once per equation. Creation checks
 * that ids are unqualified and strictly ascending, and copies the input.
 * Activations retain the schema independently of the compiling plan. */
BindingsFrameSchema *bindings_frame_schema_new(
    const VarId *source_ids, uint32_t source_len);
BindingsFrameSchema *bindings_frame_schema_new_presented(
    const VarId *source_ids, Atom *const *source_variables,
    uint32_t source_len);
/* Borrow an already completed schema image without acquiring a hold. */
BindingsFrameSchema *bindings_frame_schema_copy_lookup(
    const BindingsFrameSchema *source, const AtomDeepCopySession *atoms);
/* Copy immutable presentation roots through the enclosing Atom session,
 * preserving shared schema identity. Returns one owned reference. The
 * session separately holds source and image metadata until it closes;
 * referenced arenas retain the enclosing transport's lifetime contract.
 * Copying an already registered image returns that same image. */
BindingsFrameSchema *bindings_frame_schema_copy_with_session(
    const BindingsFrameSchema *source, AtomDeepCopySession *atoms);
void bindings_frame_schema_retain(BindingsFrameSchema *schema);
void bindings_frame_schema_release(BindingsFrameSchema *schema);
const VarId *bindings_frame_schema_source_ids(const BindingsFrameSchema *schema);
uint32_t bindings_frame_schema_len(const BindingsFrameSchema *schema);

#endif
