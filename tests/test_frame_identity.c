/* The frame identity facility issues one 64-bit word per identity: a handle
 * and its generation.  At every handle range an identity round-trips its
 * spelling, rejects a stale generation and retires its handle after the last
 * generation, the last handle's retirement leaving nothing to issue; words
 * the facility never issues do not decode; and a bounded
 * table refuses without spending an identity, then serves again after a
 * release.  The census counts what was issued, freed to the shared list and
 * retired. */
#include "binding/frame_identity.h"
#include "binding/frame_identity_census.h"
#include "binding/frame_identity_test.h"
#include "atom.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* What a fresh handle at `first` reads as, and its lifetime: reuse raises
 * the generation, the old spelling stays stale, and the handle retires after
 * its last generation (reached through the aging hook rather than 2^32
 * reuses). */
static void check_handle(uint32_t first, const char *name) {
    char what[160];
    if (first > 1u)
        cetta_frame_identity_test_issue_from(first);
    CettaFrameIdentity identity = CETTA_FRAME_IDENTITY_NONE;
    snprintf(what, sizeof what, "%s: a fresh handle is issued", name);
    check(cetta_frame_identity_acquire(&identity), what);
    CettaFrameIdentityParts parts;
    snprintf(what, sizeof what, "%s: its spelling decodes", name);
    check(cetta_frame_identity_parts(identity, &parts), what);
    snprintf(what, sizeof what, "%s: handle %u, first generation", name, first);
    check(parts.handle == first && parts.generation == 1u &&
          parts.generation_max == CETTA_FRAME_GENERATION_MAX, what);
    snprintf(what, sizeof what, "%s: generation above the handle", name);
    check(identity.word == ((UINT64_C(1) << 32) | first), what);
    check(cetta_frame_handle(identity) == first &&
          cetta_frame_generation(identity) == 1u, "handle and generation read back");
    check(cetta_frame_identity_retain(identity), "a live identity is retained");
    cetta_frame_identity_release(identity);

    cetta_frame_identity_release(identity);
    CettaFrameIdentity next = CETTA_FRAME_IDENTITY_NONE;
    check(cetta_frame_identity_acquire(&next) &&
          cetta_frame_handle(next) == first &&
          cetta_frame_generation(next) == 2u,
          "a released handle serves again at the next generation");
    snprintf(what, sizeof what, "%s: a stale generation is rejected", name);
    check(!cetta_frame_identity_retain(identity), what);

    cetta_frame_identity_release(next);
    cetta_frame_identity_test_age(first, CETTA_FRAME_GENERATION_MAX - 1u);
    CettaFrameIdentity last = CETTA_FRAME_IDENTITY_NONE;
    snprintf(what, sizeof what, "%s: the last generation is issued", name);
    check(cetta_frame_identity_acquire(&last) &&
          cetta_frame_handle(last) == first &&
          cetta_frame_generation(last) == CETTA_FRAME_GENERATION_MAX, what);
    cetta_frame_identity_release(last);
    CettaFrameIdentity after = CETTA_FRAME_IDENTITY_NONE;
    snprintf(what, sizeof what, "%s: retired after its last generation", name);
    if (first == CETTA_FRAME_HANDLE_LIMIT - 1u) {
        /* Nothing is left to issue once the last handle retires. */
        uint64_t before = cetta_frame_identity_exhaustions();
        check(!cetta_frame_identity_acquire(&after) &&
              cetta_frame_identity_exhaustions() == before + 1u, what);
        return;
    }
    /* Held, so that the next check starts from its own fresh handle. */
    check(cetta_frame_identity_acquire(&after) &&
          cetta_frame_handle(after) != first, what);
}

static void check_bounded_refusal(void) {
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        check(false, "fork");
        return;
    }
    if (child == 0) {
        enum { LIMIT = 1u << 12 };
        cetta_frame_identity_limit_handles(LIMIT);
        static CettaFrameIdentity held[LIMIT + 1u];
        uint32_t count = 0u;
        while (count <= LIMIT && cetta_frame_identity_acquire(&held[count]))
            count++;
        uint64_t before = cetta_frame_identity_exhaustions();
        CettaFrameIdentity refused = CETTA_FRAME_IDENTITY_NONE;
        bool again = cetta_frame_identity_acquire(&refused);
        bool counted = cetta_frame_identity_exhaustions() == before + 1u;
        cetta_frame_identity_release(held[0]);
        CettaFrameIdentity served = CETTA_FRAME_IDENTITY_NONE;
        bool serves = cetta_frame_identity_acquire(&served);
        _exit(count == LIMIT && !again && counted && serves &&
              cetta_frame_handle(served) == cetta_frame_handle(held[0]) ? 0 : 1);
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0,
          "a bounded table issues every handle, refuses the next without "
          "spending it, and serves a released handle again");
}

static bool census_is(uint32_t limit, uint32_t issued, uint32_t free_count,
                      uint32_t retired, uint32_t in_use, uint32_t headroom) {
    CettaFrameIdentityCensus census;
    cetta_frame_identity_census(&census);
    return census.limit == limit && census.issued == issued &&
           census.free == free_count && census.retired == retired &&
           cetta_frame_identity_census_in_use(&census) == in_use &&
           cetta_frame_identity_census_headroom(&census) == headroom;
}

/* A handle released by a thread waits in that thread's cache, 64 of them,
 * and counts as in use until it overflows to the shared free list. */
static void check_census(void) {
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        check(false, "fork");
        return;
    }
    if (child == 0) {
        enum { LIMIT = 1000u, RELEASED = 100u, CACHED = 64u };
        cetta_frame_identity_limit_handles(LIMIT);
        bool ok = census_is(LIMIT + 1u, 0u, 0u, 0u, 0u, LIMIT);
        static CettaFrameIdentity held[LIMIT];
        for (uint32_t i = 0u; i < LIMIT; i++)
            ok = ok && cetta_frame_identity_acquire(&held[i]);
        ok = ok && census_is(LIMIT + 1u, LIMIT, 0u, 0u, LIMIT, 0u);
        for (uint32_t i = 0u; i < RELEASED; i++)
            cetta_frame_identity_release(held[i]);
        ok = ok && census_is(LIMIT + 1u, LIMIT, RELEASED - CACHED, 0u,
                             LIMIT - (RELEASED - CACHED), RELEASED - CACHED);
        _exit(ok ? 0 : 1);
    }
    int status = 0;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
          WEXITSTATUS(status) == 0,
          "the census counts issued handles, the shared free list and "
          "headroom, a thread's cached handles counting as in use");
}

int main(void) {
    /* First, while no handle has been issued. */
    check_bounded_refusal();
    check_census();
    check_handle(1u, "the first handle");
    check_handle(UINT32_C(1) << 20, "a handle at 2^20");
    check_handle(UINT32_C(1) << 31, "a handle at 2^31");
    check_handle(CETTA_FRAME_HANDLE_LIMIT - 1u, "the last handle");
    CettaFrameIdentityCensus census;
    cetta_frame_identity_census(&census);
    check(census.retired == 4u, "the census counts each retired handle");

    CettaFrameIdentityParts parts;
    check(!cetta_frame_identity_parts(CETTA_FRAME_IDENTITY_NONE, &parts),
          "zero names no identity");
    check(!cetta_frame_identity_parts((CettaFrameIdentity){5u}, &parts),
          "generation zero is never issued");
    check(!cetta_frame_identity_parts(CETTA_VARIANT_PRIVATE_SLOT_EPOCH, &parts),
          "the private variant slot epoch names no identity");
    check(!cetta_frame_identity_parts(
              (CettaFrameIdentity){UINT64_C(3) << 32}, &parts),
          "handle zero is never issued");
    check(!cetta_frame_identity_parts(
              (CettaFrameIdentity){(UINT64_C(1) << 32) | CETTA_FRAME_HANDLE_LIMIT},
              &parts),
          "no handle is issued at the limit");
    check(cetta_frame_identity_parts(
              (CettaFrameIdentity){(UINT64_C(3) << 32) | 5u}, &parts) &&
              parts.handle == 5u && parts.generation == 3u,
          "a spelling decodes to its handle and generation");
    if (failures)
        return 1;
    printf("PASS: frame identities round-trip, go stale and retire across the "
           "handle range; foreign words do not decode; a bounded table refuses "
           "and serves again; the census counts them\n");
    return 0;
}
