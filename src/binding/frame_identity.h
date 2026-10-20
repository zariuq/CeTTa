#ifndef CETTA_BINDING_FRAME_IDENTITY_H
#define CETTA_BINDING_FRAME_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>
#include "../generated/cetta_layout_words.generated.h"

/* A frame identity is independent of the substitution version containing it.
 * It names a handle and that handle's generation; reuse of a handle changes
 * the generation, and a handle whose generations are spent is retired.  Zero
 * names the ambient inventory and is never allocated here.
 *
 * The identity is one 64-bit word, declared as the FrameIdentity layout in
 * langdef/layouts/runtime_words.module: the handle in the low half and its
 * generation in the 30 bits above it, each counted from one; the top two
 * bits are zero in every identity (a binding value's context word keeps its
 * kind there).  The handle table grows with use, cells are allocated when
 * their handles are first issued, and a handle retires only after 2^30 - 1
 * reuses: the number of live identities is bounded by memory, and their
 * lifetime supply (2^62) has no practical bound.
 *
 * A struct, so that no identity is ever held in a narrower integer. */
typedef struct {
    uint64_t word;
} CettaFrameIdentity;

#define CETTA_FRAME_IDENTITY_NONE ((CettaFrameIdentity){0u})

/* Handles are numbered from one, below this limit. */
#define CETTA_FRAME_HANDLE_LIMIT UINT32_C(0xffffffff)
/* The largest generation of any handle. */
#define CETTA_FRAME_GENERATION_MAX ((uint32_t)FrameIdentity_GenerationLast)

/* In the declared layout, handles 1 .. LIMIT - 1 carry generations
 * 1 .. MAX, zero is neither, and a spelling reads back its parts. */
_Static_assert(CESTA_LAYOUT_FrameIdentity_admits_BODY(1u, 1u) &&
               CESTA_LAYOUT_FrameIdentity_admits_BODY(
                   CETTA_FRAME_HANDLE_LIMIT - 1u, CETTA_FRAME_GENERATION_MAX) &&
               !CESTA_LAYOUT_FrameIdentity_admits_BODY(0u, 1u) &&
               !CESTA_LAYOUT_FrameIdentity_admits_BODY(1u, 0u) &&
               CESTA_LAYOUT_FrameIdentity_handle_BODY(
                   CESTA_LAYOUT_FrameIdentity_encode_BODY(
                       CETTA_FRAME_HANDLE_LIMIT - 1u,
                       CETTA_FRAME_GENERATION_MAX)) ==
                   CETTA_FRAME_HANDLE_LIMIT - 1u &&
               CESTA_LAYOUT_FrameIdentity_generation_BODY(
                   CESTA_LAYOUT_FrameIdentity_encode_BODY(
                       1u, CETTA_FRAME_GENERATION_MAX)) ==
                   CETTA_FRAME_GENERATION_MAX,
               "frame identity constants differ from the FrameIdentity layout");

/* Every case of the layout places the handle number in the low 32 bits
 * (reserved cases read it offset by a multiple of 2^32) and an identity's
 * generation in the 30 bits above, so the hot readers below take those bits
 * directly. */
_Static_assert((uint32_t)CESTA_LAYOUT_FrameIdentity_handle_BODY(
                   UINT64_C(0xC0000002FFFFFFFE)) == UINT32_C(0xFFFFFFFE) &&
               (uint32_t)CESTA_LAYOUT_FrameIdentity_handle_BODY(
                   UINT64_C(0x4000000000000007)) == 7u &&
               CESTA_LAYOUT_FrameIdentity_generation_BODY(
                   UINT64_C(0x3FFFFFFF00000001)) == FrameIdentity_GenerationLast,
               "frame identity bit positions differ from the FrameIdentity layout");

static inline bool cetta_frame_identity_eq(CettaFrameIdentity left,
                                           CettaFrameIdentity right) {
    return left.word == right.word;
}

static inline bool cetta_frame_identity_is_none(CettaFrameIdentity identity) {
    return identity.word == 0u;
}

/* The handle an identity names.  Live identities name distinct handles, so
 * it keys per-frame tables. */
static inline uint32_t cetta_frame_handle(CettaFrameIdentity identity) {
    return (uint32_t)identity.word;
}

/* The identity's generation within its handle.  Generations of one handle
 * increase with each reuse. */
static inline uint32_t cetta_frame_generation(CettaFrameIdentity identity) {
    return (uint32_t)(identity.word >> 32) &
           (uint32_t)FrameIdentity_GenerationLast;
}

typedef struct {
    uint32_t handle;
    uint32_t generation;
    uint32_t generation_max;
} CettaFrameIdentityParts;

/* Read an identity's handle number and generation.  Every word reads as
 * some handle, so a spelling the facility never issues (zero, or an epoch a
 * test writes by hand) still keys a frame table by its own number; true only
 * for a spelling the facility could issue. */
static inline bool cetta_frame_identity_parts(CettaFrameIdentity identity,
                                              CettaFrameIdentityParts *out) {
    out->handle = cetta_frame_handle(identity);
    out->generation = cetta_frame_generation(identity);
    out->generation_max = CETTA_FRAME_GENERATION_MAX;
    return FrameIdentity_valid(identity.word) &&
           out->handle < CETTA_FRAME_HANDLE_LIMIT;
}

/* Successful acquisition returns one ownership reference.  Retain refuses a
 * stale or foreign identity.  Every retained owner must release exactly once.
 * The allocator does not store binding values or substitution versions. */
bool cetta_frame_identity_acquire(CettaFrameIdentity *identity_out);
/* How many acquisitions were refused because every handle was live.  A
 * refusal fails the operation that asked, which its caller may take for an
 * ordinary failure; work that must not publish such a failure as an
 * observation compares this count across itself. */
uint64_t cetta_frame_identity_exhaustions(void);
/* Bound the handles this process may issue.  A test calls it in an isolated
 * child to reach exhaustion without allocating the whole facility; the
 * runtime never calls it.  Handles already issued stay valid. */
void cetta_frame_identity_limit_handles(uint32_t count);
bool cetta_frame_identity_retain(CettaFrameIdentity identity);
void cetta_frame_identity_release(CettaFrameIdentity identity);
/* Allocate a fresh one-based local slot after the prepared inventory.
 * Slot allocation is shared by branch images and never rewinds on rollback. */
bool cetta_frame_identity_new_slot(CettaFrameIdentity identity,
                                  uint32_t inventory_size, uint32_t *slot_out);
/* Read the allocation extent while the caller owns this identity. Zero means
 * no slots have been allocated through this allocator. */
bool cetta_frame_identity_slot_count(CettaFrameIdentity identity,
                                    uint32_t *count_out);

/* A lexical C scope owns newly minted identities until its result has acquired
 * an owner in syntax or a binding image.  Inline storage covers ordinary
 * activation construction without allocating an ownership list. */
typedef struct {
    CettaFrameIdentity inline_identities[4];
    CettaFrameIdentity *identities;
    uint32_t len;
    uint32_t cap;
} CettaFrameIdentityScope;

bool cetta_frame_identity_scope_try(
    CettaFrameIdentityScope *scope, CettaFrameIdentity *identity_out);
bool cetta_frame_identity_scope_retain(
    CettaFrameIdentityScope *scope, CettaFrameIdentity identity);
CettaFrameIdentity cetta_frame_identity_scope_fresh(
    CettaFrameIdentityScope *scope);
void cetta_frame_identity_scope_clear(CettaFrameIdentityScope *scope);

#define CETTA_FRAME_IDENTITY_SCOPE(name) \
    __attribute__((cleanup(cetta_frame_identity_scope_clear))) \
    CettaFrameIdentityScope name = {0}

#endif
