#ifndef CETTA_VARIANT_SHAPE_H
#define CETTA_VARIANT_SHAPE_H

/*
 * VariantShape is CeTTa's current runtime representation for open-term
 * sharing: a renaming-insensitive skeleton plus a per-instance slot
 * environment.
 *
 * Informal correspondence to the literature:
 * - explicit substitutions / closures:
 *     skeleton  ~ term-with-holes
 *     slot_env  ~ delayed substitution environment
 *     materialize ~ apply the delayed substitutions
 * - rho-style naming:
 *     the private slot namespace plays a role similar to a restricted,
 *     runtime-only name discipline, but this module does not implement
 *     full rho-calculus semantics.
 *
 * This header intentionally states only the representation idea and the
 * safety boundary. It does not claim formal theorems or specific proof
 * artifacts; those should be referenced only once they actually exist.
 */

#include "match.h"
#include "term_canon.h"

/*
 * VariantShape isolates renaming-insensitive term shape from per-instance
 * variable instantiation.
 *
 * Positive example:
 *   - TableStore canonicalizes a query/result shape once, then re-materializes
 *     it for each concrete query instantiation.
 *
 * Negative example:
 *   - Alpha-like rewriting logic drifting independently inside term_universe,
 *     table_store, and future evaluator caches.
 */

typedef enum {
    CETTA_VARIANT_SLOT_SOURCE_SPELLING = 0,
    CETTA_VARIANT_SLOT_ORDINAL_NAME = 1,
    CETTA_VARIANT_SLOT_FIXED_SPELLING = 2,
} CettaVariantSlotPolicy;

typedef struct {
    CettaVariantSlotPolicy slot_policy;
    const char *slot_name;
    bool share_immutable;
    uint32_t hashcons_initial_size;
} CettaVariantShapeOptions;

typedef struct {
    Arena arena;
    HashConsTable hashcons;
    CettaVariantShapeOptions options;
} VariantBank;

typedef struct {
    Atom *skeleton;
    Bindings slot_env;
} VariantShape;

VarId variant_shape_slot_id(uint32_t ordinal);
uint32_t variant_shape_slot_ordinal(VarId id);
bool variant_private_var_id(VarId id);

void variant_shape_init(VariantShape *shape);
void variant_shape_free(VariantShape *shape);

void variant_bank_init(VariantBank *bank, CettaVariantShapeOptions options);
void variant_bank_free(VariantBank *bank);

bool variant_shape_from_atom(VariantBank *bank, Atom *src, VariantShape *out);
bool variant_shape_from_bound_atom(VariantBank *bank, const Bindings *env,
                                   Atom *src, VariantShape *out);
Atom *variant_shape_materialize(Arena *dst, const VariantShape *shape);

Atom *variant_shape_canonicalize_atom(Arena *dst, Atom *src,
                                      CettaVarMap *src_to_slot,
                                      CettaVarMap *slot_to_src,
                                      const CettaVariantShapeOptions *options);
bool variant_shape_canonicalize_bindings(Arena *dst, const Bindings *src,
                                         CettaVarMap *src_to_slot,
                                         const CettaVariantShapeOptions *options,
                                         Bindings *out);

Atom *variant_shape_materialize_atom(Arena *dst, Atom *src,
                                     const CettaVarMap *goal_instantiation,
                                     CettaVarMap *local_slots);
bool variant_shape_materialize_bindings(Arena *dst, const Bindings *src,
                                        const CettaVarMap *goal_instantiation,
                                        CettaVarMap *local_slots,
                                        Bindings *out);

#endif /* CETTA_VARIANT_SHAPE_H */
