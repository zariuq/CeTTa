#ifndef CETTA_OWNED_RETURN_IMAGE_H
#define CETTA_OWNED_RETURN_IMAGE_H

#include "owned_execution.h"

typedef struct CettaOwnedReturnImage CettaOwnedReturnImage;

/* The host identifies a shareable owner of this exact opaque resource leaf,
 * including external objects and service code the leaf can still reach.
 * Describing a hold does not acquire it. Refusal acquires nothing, and a
 * uniquely owned resource must refuse shared retention. The descriptor and
 * source remain stable throughout this synchronous capture episode. */
typedef struct {
    void *owner;
    void (*retain)(void *owner);
    void (*release)(void *owner);
} CettaReturnLeafOwner;
typedef bool (*CettaReturnDescribeLeafOwner)(
    void *context, const Atom *leaf, CettaReturnLeafOwner *out);

typedef struct CettaOwnedReturnImagePolicy {
    CettaReturnDescribeLeafOwner describe_leaf_owner;
    void *context;
} CettaOwnedReturnImagePolicy;

/* Independently own the term, delayed goals and full logical environment,
 * preserving source pointer sharing across all roots, structured variable
 * name keys, and return provenance. Source Atom graphs are finite and acyclic.
 * Plain scalar leaves are copied. Opaque resource leaves require a qualified
 * owner descriptor; no resource permission is inferred from its syntax or
 * ordinary Atom-copy support. A top-level logical Prime occurrence extension
 * is refused here; opaque saved contents remain the descriptor's obligation.
 * Symbol identities keep their original intern context, which the host keeps
 * alive. Storage independence grants no transport to a different context.
 * NULL leaves the source return unchanged.
 * The complete computation and unreturned alternatives stay with its owner. */
CettaOwnedReturnImage *cetta_owned_return_image_capture(
    const CettaOwnedReturn *returned,
    const CettaOwnedReturnImagePolicy *policy);
const CettaOwnedReturn *cetta_owned_return_image_view(
    const CettaOwnedReturnImage *image);
/* Reference operations are serialized by the caller, as is capture. */
bool cetta_owned_return_image_retain(CettaOwnedReturnImage *image);
void cetta_owned_return_image_release(void *image);

/* The view must have been obtained from an image owned by this API, and be
 * protected by a live image share. This conversion does not accept arbitrary
 * evaluator loans. A recipient may retain that image before acknowledging
 * delivery without copying the answer or its environment again. */
CettaOwnedReturnImage *cetta_owned_return_image_from_view(
    const CettaOwnedReturn *view);
/* Control and Atom-arena bytes. Logical binding/index allocations and opaque
 * resource backing storage are separate accounts of their owning services. */
size_t cetta_owned_return_image_syntax_bytes(const CettaOwnedReturnImage *image);

#endif
