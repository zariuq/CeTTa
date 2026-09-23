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
void bindings_frame_schema_retain(BindingsFrameSchema *schema);
void bindings_frame_schema_release(BindingsFrameSchema *schema);
const VarId *bindings_frame_schema_source_ids(const BindingsFrameSchema *schema);
uint32_t bindings_frame_schema_len(const BindingsFrameSchema *schema);

#endif
