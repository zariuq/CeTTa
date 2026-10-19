/* The frame identity facility issues handles in four classes, each spelled
 * by a prefix of leading one bits.  Every class round-trips its spelling,
 * rejects a stale generation and retires a handle whose generations are
 * spent; words no class issues do not decode; and a bounded table refuses
 * without spending an identity, then serves again after a release.  The
 * census counts what was issued, freed to the shared list and retired. */
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

/* What a fresh handle of the class starting at `first` reads as, and its
 * lifetime: reuse raises the generation, the old spelling stays stale, and
 * the handle retires after its last generation. */
static void check_class(uint32_t first, uint32_t prefix, uint32_t prefix_bits,
                        uint32_t generation_max, const char *name) {
    char what[160];
    if (first > 1u)
        cetta_frame_identity_test_issue_from(first);
    CettaFrameIdentity identity = 0u;
    snprintf(what, sizeof what, "%s: a fresh handle is issued", name);
    check(cetta_frame_identity_acquire(&identity), what);
    CettaFrameIdentityParts parts;
    snprintf(what, sizeof what, "%s: its spelling decodes", name);
    check(cetta_frame_identity_parts(identity, &parts), what);
    snprintf(what, sizeof what, "%s: handle %u, first generation", name, first);
    check(parts.handle == first && parts.generation == 1u &&
          parts.generation_max == generation_max, what);
    snprintf(what, sizeof what, "%s: spelled with its class prefix", name);
    check(identity >> (32u - prefix_bits) == prefix, what);
    check(cetta_frame_handle(identity) == first &&
          cetta_frame_generation(identity) == 1u, "handle and generation read back");
    check(cetta_frame_identity_retain(identity), "a live identity is retained");
    cetta_frame_identity_release(identity);

    CettaFrameIdentity previous = identity;
    uint32_t generations = 1u;
    bool retired = false;
    while (!retired) {
        cetta_frame_identity_release(previous);
        CettaFrameIdentity next = 0u;
        if (!cetta_frame_identity_acquire(&next)) {
            check(false, "a released handle serves again");
            return;
        }
        snprintf(what, sizeof what, "%s: a stale generation is rejected", name);
        check(!cetta_frame_identity_retain(previous), what);
        if (cetta_frame_handle(next) != first) {
            /* Held, so that no free handle is left for the next class. */
            retired = true;
            break;
        }
        generations++;
        snprintf(what, sizeof what, "%s: reuse raises the generation", name);
        check(cetta_frame_generation(next) == generations, what);
        previous = next;
    }
    snprintf(what, sizeof what, "%s: retired after generation %u", name,
             generation_max);
    check(generations == generation_max, what);
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
        CettaFrameIdentity refused = 0u;
        bool again = cetta_frame_identity_acquire(&refused);
        bool counted = cetta_frame_identity_exhaustions() == before + 1u;
        cetta_frame_identity_release(held[0]);
        CettaFrameIdentity served = 0u;
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
    check_class(1u, 0u, 1u, CETTA_FRAME_GENERATION_MAX, "class 0");
    check_class(CETTA_FRAME_CLASS1_FIRST, 2u, 2u, 0x3eu, "class 1");
    check_class(CETTA_FRAME_CLASS2_FIRST, 6u, 3u, 0x6u, "class 2");
    check_class(CETTA_FRAME_CLASS3_FIRST, 14u, 4u, 0x2u, "class 3");
    CettaFrameIdentityCensus census;
    cetta_frame_identity_census(&census);
    check(census.retired == 4u, "the census counts one retired handle per class");

    CettaFrameIdentityParts parts;
    check(!cetta_frame_identity_parts(0u, &parts), "zero names no identity");
    check(!cetta_frame_identity_parts(UINT32_C(0xFFEFFFFF), &parts),
          "a word with four leading ones belongs to no class");
    check(!cetta_frame_identity_parts(
              (CettaFrameIdentity)(CETTA_VARIANT_PRIVATE_SLOT_TAG >> 32), &parts),
          "the private variant slot tag names no identity");
    check(!cetta_frame_identity_parts(
              (UINT32_C(0x7ff) << 20) | 5u, &parts),
          "a class-0 spelling past its last generation does not decode");
    check(cetta_frame_identity_parts((UINT32_C(3) << 20) | 5u, &parts) &&
          parts.handle == 5u && parts.generation == 3u,
          "a class-0 spelling decodes to its handle and generation");
    if (failures)
        return 1;
    printf("PASS: frame identities round-trip, go stale and retire in every "
           "class; foreign words do not decode; a bounded table refuses and "
           "serves again; the census counts them\n");
    return 0;
}
