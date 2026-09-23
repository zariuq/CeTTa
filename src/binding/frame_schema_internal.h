#ifndef CETTA_BINDING_FRAME_SCHEMA_INTERNAL_H
#define CETTA_BINDING_FRAME_SCHEMA_INTERNAL_H

#include "frame_schema.h"
#include <stdatomic.h>

/* Published inventories are immutable and have an owner independent of any
 * branch's slot storage. A recycled, exclusively owned inventory is private
 * until the next activation publishes it. */
struct BindingsFrameSchema {
    _Atomic uint32_t references;
    uint32_t len;
    uint32_t cap;
    VarId source_first_id;
    bool source_ids_contiguous;
    /* Presentation belongs to the contextual variable inventory, not to a
     * chronological binding update.  Keeping it with the immutable schema
     * lets frame-only values cross the observation boundary without
     * reconstructing their keys from a row store. */
    SymbolId *spellings;
    Atom **name_keys;
    VarId source_ids[];
};

/* These mutation operations require exclusive ownership until publication.
 * Only the slot store and schema implementation use this interface. */
BindingsFrameSchema *bindings_frame_schema_alloc(uint32_t capacity);
void bindings_frame_schema_publish(BindingsFrameSchema *schema, uint32_t len);
BindingsFrameSchema *bindings_frame_schema_dense(uint32_t count);
BindingsFrameSchema *bindings_frame_schema_clone(const BindingsFrameSchema *source);
bool bindings_frame_schema_find_slot(const BindingsFrameSchema *schema,
                                     VarId source_id, uint32_t *slot_out);

#endif
