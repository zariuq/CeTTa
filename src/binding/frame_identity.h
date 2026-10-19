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
 * The handle table grows: handles are issued in classes, each spelled in
 * the 32-bit identity by a prefix of leading one bits.  A later class has
 * more handles and fewer generations per handle:
 *
 *   class 0  0      gen:11 handle:20   handles 1 .. 2^20 - 1
 *   class 1  10     gen:6  handle:24
 *   class 2  110    gen:3  handle:26
 *   class 3  1110   gen:2  handle:26
 *
 * A handle's number is unique across classes (cetta_frame_handle), so it
 * can key per-frame tables.  Cells are allocated when their handles are
 * first issued; the number of live identities is bounded by memory, not by
 * a table fixed at build time. */
typedef uint32_t CettaFrameIdentity;

enum {
    /* Class 0's spelling: the generation sits above this many handle bits. */
    CETTA_FRAME_HANDLE_BITS = 20,
    CETTA_FRAME_GENERATION_BITS = 11,
};

/* Class 0's handle mask and largest generation. */
#define CETTA_FRAME_HANDLE_MASK UINT32_C(0x000fffff)
#define CETTA_FRAME_GENERATION_MAX UINT32_C(0x000007fe)

/* Handles of every class, numbered from one; zero is never a handle. */
#define CETTA_FRAME_CLASS1_FIRST (UINT32_C(1) << 20)
#define CETTA_FRAME_CLASS2_FIRST (CETTA_FRAME_CLASS1_FIRST + (UINT32_C(1) << 24))
#define CETTA_FRAME_CLASS3_FIRST (CETTA_FRAME_CLASS2_FIRST + (UINT32_C(1) << 26))
#define CETTA_FRAME_HANDLE_LIMIT (CETTA_FRAME_CLASS3_FIRST + (UINT32_C(1) << 26))

typedef struct {
    uint32_t handle;
    uint32_t generation;
    uint32_t generation_max;
} CettaFrameIdentityParts;

/* Read an identity's class spelling: its handle number and generation.
 * Every 32-bit value reads as some handle, so a spelling that no class
 * issues (zero, or an epoch a test writes by hand) still keys a frame table
 * by its own number; true only for a spelling the facility could issue. */
static inline bool cetta_frame_identity_parts(CettaFrameIdentity identity,
                                              CettaFrameIdentityParts *out) {
    if ((identity >> 31) == 0u) {
        out->handle = (uint32_t)FrameIdentity_handle(identity);
        out->generation = (uint32_t)FrameIdentity_generation(identity);
        out->generation_max = CETTA_FRAME_GENERATION_MAX;
    } else if ((identity >> 30) == 2u) {
        out->handle = (uint32_t)FrameIdentity_handle(identity);
        out->generation = (uint32_t)FrameIdentity_generation(identity);
        out->generation_max = UINT32_C(0x3e);
    } else if ((identity >> 29) == 6u) {
        out->handle = (uint32_t)FrameIdentity_handle(identity);
        out->generation = (uint32_t)FrameIdentity_generation(identity);
        out->generation_max = UINT32_C(0x6);
    } else if ((identity >> 28) == 14u) {
        out->handle = (uint32_t)FrameIdentity_handle(identity);
        out->generation = (uint32_t)FrameIdentity_generation(identity);
        out->generation_max = UINT32_C(0x2);
    } else {
        out->handle = (uint32_t)FrameIdentity_handle(identity);
        out->generation = (uint32_t)FrameIdentity_generation(identity);
        out->generation_max = 0u;
        return false;
    }
    return out->handle != 0u && out->handle < CETTA_FRAME_HANDLE_LIMIT &&
           out->generation != 0u && out->generation <= out->generation_max;
}

/* The handle an identity names.  Live identities of every class name
 * distinct handles, so it keys per-frame tables. */
static inline uint32_t cetta_frame_handle(CettaFrameIdentity identity) {
    CettaFrameIdentityParts parts;
    (void)cetta_frame_identity_parts(identity, &parts);
    return parts.handle;
}

/* The identity's generation within its handle.  Generations of one handle
 * increase with each reuse. */
static inline uint32_t cetta_frame_generation(CettaFrameIdentity identity) {
    CettaFrameIdentityParts parts;
    (void)cetta_frame_identity_parts(identity, &parts);
    return parts.generation;
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
