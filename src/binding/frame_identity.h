#ifndef CETTA_BINDING_FRAME_IDENTITY_H
#define CETTA_BINDING_FRAME_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

/* A frame identity is independent of the substitution version containing it.
 * Reuse changes the generation; exhaustion retires the handle.  Zero names
 * the ambient inventory and is never allocated here. */
typedef uint32_t CettaFrameIdentity;

enum {
    CETTA_FRAME_HANDLE_BITS = 20,
    CETTA_FRAME_GENERATION_BITS = 12,
};

#define CETTA_FRAME_HANDLE_MASK UINT32_C(0x000fffff)
#define CETTA_FRAME_GENERATION_MAX UINT32_C(0x00000ffe)

static inline uint32_t cetta_frame_handle(CettaFrameIdentity identity) {
    return identity & CETTA_FRAME_HANDLE_MASK;
}

static inline uint32_t cetta_frame_generation(CettaFrameIdentity identity) {
    return identity >> CETTA_FRAME_HANDLE_BITS;
}

/* Successful acquisition returns one ownership reference.  Retain refuses a
 * stale or foreign identity.  Every retained owner must release exactly once.
 * The allocator does not store binding values or substitution versions. */
bool cetta_frame_identity_acquire(CettaFrameIdentity *identity_out);
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
