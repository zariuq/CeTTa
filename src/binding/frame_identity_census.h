#ifndef CETTA_BINDING_FRAME_IDENTITY_CENSUS_H
#define CETTA_BINDING_FRAME_IDENTITY_CENSUS_H

#include <stdint.h>

/* The handle census of the frame identity facility, read without a lock.
 * Counts change on the slow paths only (issuing a handle, the shared free
 * list, retirement), so a reader sees each count exactly as some moment
 * left it; counts read together may straddle one such change.  Handles in a
 * thread's own free cache (at most 64 per thread) count as in use. */
typedef struct {
    uint32_t limit;    /* handles the process may issue are 1 .. limit - 1 */
    uint32_t issued;   /* handles issued so far */
    uint32_t free;     /* issued handles on the shared free list */
    uint32_t retired;  /* handles whose generations are spent */
} CettaFrameIdentityCensus;

void cetta_frame_identity_census(CettaFrameIdentityCensus *out);

/* Identities this thread has minted since it started; the difference across
 * a piece of work is what that work minted. */
uint64_t cetta_frame_identity_thread_mints(void);

/* Handles holding references, or kept free by a thread. */
static inline uint32_t cetta_frame_identity_census_in_use(
        const CettaFrameIdentityCensus *census) {
    uint32_t idle = census->free + census->retired;
    return census->issued > idle ? census->issued - idle : 0u;
}

/* Handles a mint may still take: never issued, or on the shared free list. */
static inline uint32_t cetta_frame_identity_census_headroom(
        const CettaFrameIdentityCensus *census) {
    uint32_t unissued = census->limit > census->issued + 1u
        ? census->limit - 1u - census->issued : 0u;
    return unissued + census->free;
}

#endif
