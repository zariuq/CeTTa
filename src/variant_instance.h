#ifndef CETTA_VARIANT_INSTANCE_H
#define CETTA_VARIANT_INSTANCE_H

#include "variant_shape.h"

/*
 * VariantInstance is the dense per-outcome carrier for a canonical skeleton's
 * private slots. It intentionally has no generic Bindings merge semantics.
 *
 * Positive example:
 *   - evaluator transport paths can rewrite or clone slot values without ever
 *     exposing private slots to semantic env merge logic.
 *
 * Negative example:
 *   - treating private runtime slots as ordinary Bindings and relying on
 *     call-site discipline to avoid accidental merges.
 */

typedef struct VariantInstanceStorage VariantInstanceStorage;

typedef struct {
    VariantInstanceStorage *storage;
} VariantInstance;

void variant_instance_init(VariantInstance *instance);
void variant_instance_free(VariantInstance *instance);
void variant_instance_move(VariantInstance *dst, VariantInstance *src);
bool variant_instance_present(const VariantInstance *instance);
bool variant_instance_clone(VariantInstance *dst, const VariantInstance *src);
bool variant_instance_from_shape(VariantInstance *out, const VariantShape *shape);
bool variant_instance_append_rebased(Arena *dst, VariantInstance *instance,
                                     Atom **out_skeleton,
                                     Atom *skeleton,
                                     const VariantInstance *child);
bool variant_instance_sink_env(Arena *dst, VariantInstance *out,
                               const VariantInstance *src,
                               const Bindings *env);
Atom *variant_instance_materialize(Arena *dst, Atom *skeleton,
                                   const VariantInstance *instance);

#endif /* CETTA_VARIANT_INSTANCE_H */
