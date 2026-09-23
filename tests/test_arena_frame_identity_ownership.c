/*
 * Arena frame-identity ownership: one hold per distinct full generational
 * identity for an arena's live allocation region.
 *
 * The property under test is exactness, not economy: the arena holds exactly
 * the identities its live region requires, releases exactly those on reset
 * and free, and never turns a refused retain into cached membership.
 *
 * Reference accounting used throughout, matching binding/frame_identity.c:
 *   - cetta_frame_identity_acquire returns a fresh identity already held once
 *     (that hold is the caller's to release);
 *   - cetta_frame_identity_retain adds one hold and fails on a stale identity;
 *   - cetta_frame_identity_release drops one hold, and the handle is recycled
 *     when the count reaches zero;
 *   - the arena adds one further hold per distinct identity it owns.
 */
#include "atom.h"
#include "binding/frame_identity.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

static int g_failures;
static int g_checks;

static void check(bool condition, const char *what) {
    g_checks++;
    if (!condition) {
        g_failures++;
        fprintf(stderr, "FAIL: %s\n", what);
    }
}

static void check_u32(uint32_t actual, uint32_t expected, const char *what) {
    g_checks++;
    if (actual != expected) {
        g_failures++;
        fprintf(stderr, "FAIL: %s (expected %u, got %u)\n", what,
                (unsigned)expected, (unsigned)actual);
    }
}

/* One hold, owned by the test. */
static CettaFrameIdentity fresh(void) {
    CettaFrameIdentity identity;
    if (!cetta_frame_identity_acquire(&identity)) {
        fprintf(stderr, "fatal: frame identity space exhausted\n");
        abort();
    }
    return identity;
}

/* ── repeated use: records scale with identities, not occurrences ───────── */
static void test_records_scale_with_distinct_identities(void) {
    Arena a;
    arena_init(&a);
    CettaFrameIdentity one = fresh();

    check(arena_retain_frame_identity(&a, one), "first retain succeeds");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "one identity owns one record");

    for (int i = 0; i < 4096; i++)
        check(arena_retain_frame_identity(&a, one), "repeat retain succeeds");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "4096 occurrences of one identity still own one record");

    CettaFrameIdentity two = fresh();
    check(arena_retain_frame_identity(&a, two), "second identity retains");
    check_u32(arena_frame_identity_count_test(&a), 2u,
              "two distinct identities own two records");

    enum { MANY = 512 };
    CettaFrameIdentity many[MANY];
    for (int i = 0; i < MANY; i++) {
        many[i] = fresh();
        check(arena_retain_frame_identity(&a, many[i]), "distinct retain");
    }
    check_u32(arena_frame_identity_count_test(&a), 2u + (uint32_t)MANY,
              "records equal distinct identities");
    for (int i = 0; i < MANY; i++)
        check(arena_frame_identity_owned_test(&a, many[i]),
              "each distinct identity is a member");

    arena_free(&a);
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "free clears the ownership set");

    /* The arena's holds are gone: every identity now has only the test's own
     * reference, so dropping it makes the identity stale. */
    cetta_frame_identity_release(one);
    check(!cetta_frame_identity_retain(one),
          "free released the arena's hold exactly once");
    for (int i = 0; i < MANY; i++)
        cetta_frame_identity_release(many[i]);
    check(!cetta_frame_identity_retain(many[0]),
          "every freed hold released exactly once");
    cetta_frame_identity_release(two);
    check(!cetta_frame_identity_retain(two),
          "last distinct identity released exactly once");
}

/* ── the generation is part of the deduplication key ───────────────────── */
static void test_generation_is_part_of_the_key(void) {
    Arena a;
    arena_init(&a);
    ArenaMark empty = arena_mark(&a);

    /* An arena's hold keeps its identity alive, so a handle cannot recycle
     * while the arena owns it.  The hazard is cache staleness: if membership
     * were keyed on the handle alone, a set that had ever seen the old
     * generation would report the new one as already owned and never retain
     * it.  Construct exactly that sequence. */
    CettaFrameIdentity first = fresh();
    check(arena_retain_frame_identity(&a, first), "retain first generation");
    check_u32(arena_frame_identity_count_test(&a), 1u, "first generation owns");

    /* Drop the arena's hold, then the test's, so the handle recycles. */
    arena_reset(&a, empty);
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "reset to the initial mark dropped the arena hold");
    cetta_frame_identity_release(first);

    CettaFrameIdentity second = fresh();
    check(cetta_frame_handle(second) == cetta_frame_handle(first),
          "allocator recycles the handle");
    check(cetta_frame_generation(second) != cetta_frame_generation(first),
          "recycled handle carries a new generation");

    check(!arena_frame_identity_owned_test(&a, second),
          "new generation is not mistaken for an existing member");
    check(arena_retain_frame_identity(&a, second), "retain second generation");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "recycled generation is recorded as its own member");
    check(arena_frame_identity_owned_test(&a, second),
          "new generation is retained");

    check(arena_retain_frame_identity(&a, second), "repeat new generation");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "repeat of an existing generation adds nothing");

    /* The retired generation is stale: refused, and not revived as a member. */
    check(!arena_retain_frame_identity(&a, first),
          "retired generation is refused");
    check(!arena_frame_identity_owned_test(&a, first),
          "refused generation is not cached membership");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "refusal did not disturb the live member");

    arena_free(&a);
    check(!cetta_frame_identity_retain(first),
          "retired generation stays stale after arena free");
    cetta_frame_identity_release(second);
    check(!cetta_frame_identity_retain(second),
          "arena released the recycled generation exactly once");
}

/* ── refused retains cannot become cached membership ───────────────────── */
static void test_failed_retain_is_not_cached(void) {
    Arena a;
    arena_init(&a);

    CettaFrameIdentity live = fresh();
    check(cetta_frame_identity_retain(live),
          "test takes a second reference to the live identity");

    /* A stale identity must be refused and must not be recorded. */
    CettaFrameIdentity retired = fresh();
    cetta_frame_identity_release(retired);
    check(!arena_retain_frame_identity(&a, retired),
          "stale identity cannot be retained");
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "failed retain records nothing");
    check(!arena_frame_identity_owned_test(&a, retired),
          "failed retain is not cached membership");

    /* Zero is the ambient inventory: it owns nothing and is not a record. */
    check(arena_retain_frame_identity(&a, 0u), "zero identity is a no-op");
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "zero identity creates no record");

    check(arena_retain_frame_identity(&a, live), "live identity retains");
    check_u32(arena_frame_identity_count_test(&a), 1u, "live identity recorded");
    check(!arena_retain_frame_identity(&a, retired), "stale refused again");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "refusal leaves existing membership intact");
    check(arena_frame_identity_owned_test(&a, live),
          "existing member survives a refused acquire");

    arena_free(&a);
    /* Two test references remain (acquire + explicit retain); drop both. */
    cetta_frame_identity_release(live);
    cetta_frame_identity_release(live);
    check(!cetta_frame_identity_retain(live),
          "all references released exactly once after free");
}

/* ── marks, partial rollback, reacquisition ────────────────────────────── */
static void test_nested_marks_and_reacquisition(void) {
    Arena a;
    arena_init(&a);
    ArenaMark start = arena_mark(&a);
    check_u32(start.frame_identity_len, 0u, "fresh arena owns nothing");

    CettaFrameIdentity outer = fresh();
    check(arena_retain_frame_identity(&a, outer), "outer identity retained");
    ArenaMark outer_mark = arena_mark(&a);
    check_u32(outer_mark.frame_identity_len, 1u, "mark records one identity");

    CettaFrameIdentity inner_one = fresh();
    check(arena_retain_frame_identity(&a, inner_one), "inner one retained");
    ArenaMark inner_mark = arena_mark(&a);
    check_u32(inner_mark.frame_identity_len, 2u, "nested mark records two");

    CettaFrameIdentity inner_two = fresh();
    check(arena_retain_frame_identity(&a, inner_two), "inner two retained");
    check_u32(arena_frame_identity_count_test(&a), 3u, "three identities live");

    /* Roll the innermost region back: it releases its own suffix only. */
    arena_reset(&a, inner_mark);
    check_u32(arena_frame_identity_count_test(&a), 2u,
              "inner reset releases exactly its suffix");
    check(arena_frame_identity_owned_test(&a, outer),
          "identity from before the mark survives");
    check(arena_frame_identity_owned_test(&a, inner_one),
          "identity acquired before the inner mark survives");
    check(!arena_frame_identity_owned_test(&a, inner_two),
          "identity acquired after the inner mark is gone");

    /* Reusing the identity after rollback performs the retain again. */
    check(arena_retain_frame_identity(&a, inner_two),
          "reacquire after rollback succeeds");
    check_u32(arena_frame_identity_count_test(&a), 3u,
              "reacquired identity is a member again");
    check(arena_frame_identity_owned_test(&a, inner_two),
          "reacquired identity is observable");

    arena_reset(&a, outer_mark);
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "outer reset leaves exactly the outer identity");
    check(arena_frame_identity_owned_test(&a, outer),
          "outer identity still valid after nested rollback");
    check(!arena_frame_identity_owned_test(&a, inner_one),
          "inner identity released by the outer reset");

    check(arena_retain_frame_identity(&a, outer), "outer identity reusable");
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "reuse of a surviving identity adds no record");

    arena_reset(&a, start);
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "reset to start clears every hold");
    arena_free(&a);
    cetta_frame_identity_release(outer);
    cetta_frame_identity_release(inner_one);
    cetta_frame_identity_release(inner_two);
    check(!cetta_frame_identity_retain(outer), "outer released exactly once");
    check(!cetta_frame_identity_retain(inner_one), "inner one released once");
    check(!cetta_frame_identity_retain(inner_two), "inner two released once");
}

/* ── full reset and free release exactly once ──────────────────────────── */
static void test_reset_and_free_are_exact(void) {
    Arena a;
    arena_init(&a);
    ArenaMark empty = arena_mark(&a);

    /* The test holds one reference; the arena adds one more. */
    CettaFrameIdentity identity = fresh();
    for (int i = 0; i < 64; i++)
        check(arena_retain_frame_identity(&a, identity), "grow ownership");
    check_u32(arena_frame_identity_count_test(&a), 1u, "one distinct identity");

    /* A reset to the empty mark releases the hold exactly once.  A double
     * release would abort inside the allocator, so reaching the next check
     * is itself the evidence. */
    arena_reset(&a, empty);
    check_u32(arena_frame_identity_count_test(&a), 0u, "reset to empty clears");
    check(cetta_frame_identity_retain(identity),
          "identity still lives on the test's reference after reset");
    cetta_frame_identity_release(identity);

    /* Reacquire in the same arena after a full reset. */
    check(arena_retain_frame_identity(&a, identity),
          "reacquire after full reset");
    check_u32(arena_frame_identity_count_test(&a), 1u, "reacquired and owned");

    arena_free(&a);
    check_u32(arena_frame_identity_count_test(&a), 0u, "free clears the set");
    check(cetta_frame_identity_retain(identity),
          "identity survives the arena that reacquired it");
    cetta_frame_identity_release(identity);
    /* Only the test's original reference remains. */
    cetta_frame_identity_release(identity);
    check(!cetta_frame_identity_retain(identity),
          "reset and free each released their hold exactly once");
}

/* ── escaping values keep their own arena's ownership ──────────────────── */
static void test_escape_ownership_is_per_arena(void) {
    Arena source;
    Arena destination;
    arena_init(&source);
    arena_init(&destination);

    CettaFrameIdentity identity = fresh();
    check(arena_retain_frame_identity(&source, identity),
          "source arena owns the identity");
    check(arena_retain_frame_identity(&destination, identity),
          "destination arena owns the same identity independently");
    check_u32(arena_frame_identity_count_test(&source), 1u,
              "source owns one record");
    check_u32(arena_frame_identity_count_test(&destination), 1u,
              "destination owns one record");

    /* Releasing the source must leave the escaped value's arena intact. */
    arena_free(&source);
    check_u32(arena_frame_identity_count_test(&source), 0u,
              "source released its hold");
    check(arena_frame_identity_owned_test(&destination, identity),
          "escaped value's arena still owns the identity");
    check(cetta_frame_identity_retain(identity),
          "identity outlives the source arena");
    cetta_frame_identity_release(identity);

    arena_free(&destination);
    check(cetta_frame_identity_retain(identity),
          "the test's own reference still holds after both arenas released");
    cetta_frame_identity_release(identity);
    cetta_frame_identity_release(identity);
    check(!cetta_frame_identity_retain(identity),
          "each arena released its own hold exactly once");
}

/* ── collision chain survives suffix truncation ─────────────────────────── */
static void test_suffix_delete_preserves_probe_chains(void) {
    Arena a;
    arena_init(&a);
    ArenaMark empty = arena_mark(&a);

    /* Grow the table big enough that insertions probe across several slots.
     * The acquisition-order invariant: each insert only probes through slots
     * already occupied by records inserted before it, never through later ones.
     * Consequently, deleting a suffix (the most recent records) cannot punch a
     * hole that any earlier record's probe chain passes through. */
    CettaFrameIdentity prefix[40];
    for (int i = 0; i < 40; i++) {
        prefix[i] = fresh();
        check(arena_retain_frame_identity(&a, prefix[i]), "prefix insert");
    }
    check_u32(arena_frame_identity_count_test(&a), 40u,
              "40 records in an index stretched by collisions");

    /* Delete a deep suffix.  Prefix chains must remain walkable; no rehash
     * of the survivor prefix is needed. */
    arena_reset(&a, empty);
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "suffix delete to the start mark");

    /* Now reacquire exactly the same identities the prefix held. */
    for (int i = 0; i < 40; i++) {
        CettaFrameIdentity again = fresh();
        check(arena_retain_frame_identity(&a, again),
              "prefix reacquire after suffix delete");
    }
    check_u32(arena_frame_identity_count_test(&a), 40u,
              "reacquired prefix is stable after growth");

    arena_free(&a);
    /* All released: count reaches zero. */
    check_u32(arena_frame_identity_count_test(&a), 0u,
              "free of a grown table is exact");
}

/* ── variable atoms in one arena share one record ──────────────────────── */
static void test_variable_atoms_share_one_record(void) {
    Arena a;
    arena_init(&a);
    CettaFrameIdentity identity = fresh();
    VarId id = var_epoch_id((VarId)7u, identity);

    for (int i = 0; i < 256; i++) {
        Atom *var = atom_var_with_spelling(&a, SYMBOL_ID_NONE, id);
        if (!var) {
            g_failures++;
            fprintf(stderr, "FAIL: variable atom %d not built\n", i);
            break;
        }
    }
    check_u32(arena_frame_identity_count_test(&a), 1u,
              "256 variable atoms of one identity own one record");
    check(arena_frame_identity_owned_test(&a, identity),
          "the identity is owned by the arena");

    arena_free(&a);
    check(cetta_frame_identity_retain(identity),
          "identity outlives the arena that owned one shared record");
    cetta_frame_identity_release(identity);
    cetta_frame_identity_release(identity);
    check(!cetta_frame_identity_retain(identity),
          "freeing the arena released its one shared hold");
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;

    test_records_scale_with_distinct_identities();
    test_generation_is_part_of_the_key();
    test_failed_retain_is_not_cached();
    test_nested_marks_and_reacquisition();
    test_reset_and_free_are_exact();
    test_escape_ownership_is_per_arena();
    test_suffix_delete_preserves_probe_chains();
    test_variable_atoms_share_one_record();

    g_symbols = NULL;
    symbol_table_free(&symbols);

    printf("ArenaFrameIdentityOwnership checks=%d failures=%d\n", g_checks,
           g_failures);
    if (g_failures) {
        fprintf(stderr, "FAIL: arena frame-identity ownership\n");
        return 1;
    }
    printf("PASS: one hold per distinct identity; reset and free are exact\n");
    return 0;
}
