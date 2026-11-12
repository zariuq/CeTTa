/* The commit protocol of the interaction kernel (src/interaction_core.c,
 * declared in langdef/protocols/interaction_commit.module).
 *
 *   positive      a take and a make commit; the revision moves.
 *   unmoved       the commit's own writes leave a changed space at its opening
 *                 revision: (commit-failed revision-unmoved), and the writes
 *                 stay. The failure invalidates stale revisions before return.
 *   take-failed   a take that cannot be made ends the commit inside the epoch:
 *                 the commit is not reported as committed.
 *   make-failed   a make that cannot be stored ends the commit likewise.
 *   untracked     a write outside the commit that keeps the revision: the
 *                 protocol cannot see it, so a commit validated after it is
 *                 accepted although a query it read no longer holds.  The
 *                 space layer must move the revision on every write. */
#include <stdint.h>
#include <stdlib.h>
#include "../src/space.h"
#include "../src/prime_scoped_judgments.h"
#include "../src/space_path_index.h"

static bool g_forbid_publication_allocation;
static size_t g_publication_allocations;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    if (g_forbid_publication_allocation) { g_publication_allocations++; return NULL; }
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    if (g_forbid_publication_allocation) { g_publication_allocations++; return NULL; }
    return __real_calloc(count, size);
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (g_forbid_publication_allocation) { g_publication_allocations++; return NULL; }
    return __real_realloc(pointer, size);
}

/* Allocation faults are confined to the interaction implementation in this
 * translation unit. They do not alter the space allocator or production ABI. */
static size_t g_calloc_before_failure = SIZE_MAX;
static size_t g_realloc_before_failure = SIZE_MAX;
static size_t g_admits_before_failure = SIZE_MAX;

static bool protocol_admit(Space *space, Arena *arena, Atom *atom) {
    if (g_admits_before_failure == 0u) {
        g_admits_before_failure = SIZE_MAX;
        return false;
    }
    if (g_admits_before_failure != SIZE_MAX) g_admits_before_failure--;
    return space_admit_atom(space, arena, atom);
}

static SpaceNativeBagAppend *protocol_append(Space *space, Arena *arena, Atom *const *atoms, size_t count) {
    if (g_admits_before_failure == 0u) {
        g_admits_before_failure = SIZE_MAX;
        return NULL;
    }
    if (g_admits_before_failure != SIZE_MAX) g_admits_before_failure--;
    return space_native_bag_append_prepare(space, arena, atoms, count);
}

static void *protocol_calloc(size_t count, size_t size) {
    if (g_calloc_before_failure == 0u) {
        g_calloc_before_failure = SIZE_MAX;
        return NULL;
    }
    if (g_calloc_before_failure != SIZE_MAX)
        g_calloc_before_failure--;
    return calloc(count, size);
}

static void *protocol_realloc(void *pointer, size_t size) {
    if (g_realloc_before_failure == 0u) {
        g_realloc_before_failure = SIZE_MAX;
        return NULL;
    }
    if (g_realloc_before_failure != SIZE_MAX)
        g_realloc_before_failure--;
    return realloc(pointer, size);
}

#define calloc protocol_calloc
#define realloc protocol_realloc
#define space_admit_atom protocol_admit
#define space_native_bag_append_prepare protocol_append
#include "../src/interaction_core.c"
#undef space_native_bag_append_prepare
#undef space_admit_atom
#undef realloc
#undef calloc

#include <stdio.h>

static unsigned g_checks, g_failures;

static void check(bool ok, int line, const char *what) {
    g_checks++;
    if (!ok) {
        g_failures++;
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, line, what);
    }
}

#define CHECK(x) check((x), __LINE__, #x)

/* The fault: every space the commit wrote is put back at its opening
 * revision, contents changed. */
static void restore_revisions(IaCommitRun *c) {
    for (size_t k = 0; k < c->written_len; k++)
        c->written[k].space->revision = c->written[k].revision;
}

/* Only the second backend omits mutation publication. The first space's
 * post-write tokens must survive the repair of the second space. */
static struct {
    Space *published;
    Space *unpublished;
    SpaceReadToken old_unpublished;
    SpaceReadToken published_after_writes;
    SpaceReadToken unpublished_after_reset;
    SpaceProgramToken published_program_after_writes;
    SpaceProgramToken unpublished_program_after_reset;
} g_mixed_revision_fault;

static void restore_one_revision(IaCommitRun *c) {
    bool saw_published = false, reset_unpublished = false;
    for (size_t k = 0; k < c->written_len; k++) {
        IaWritten *written = &c->written[k];
        if (written->space == g_mixed_revision_fault.published) {
            CHECK(space_revision(written->space) != written->revision);
            saw_published = true;
        } else if (written->space == g_mixed_revision_fault.unpublished) {
            CHECK(space_revision(written->space) != written->revision);
            written->space->revision = written->revision;
            reset_unpublished = true;
        }
    }
    CHECK(saw_published && reset_unpublished && c->written_len == 2u);
    CHECK(space_read_token_is_current(g_mixed_revision_fault.old_unpublished));
    g_mixed_revision_fault.published_after_writes =
        space_read_token(g_mixed_revision_fault.published);
    g_mixed_revision_fault.unpublished_after_reset =
        space_read_token(g_mixed_revision_fault.unpublished);
    g_mixed_revision_fault.published_program_after_writes =
        space_program_token(g_mixed_revision_fault.published);
    g_mixed_revision_fault.unpublished_program_after_reset =
        space_program_token(g_mixed_revision_fault.unpublished);
}

static Atom *call_value(bool (*op)(Arena *, Atom **, uint32_t, CettaCallOutcome *),
                        Arena *a, Atom **args, uint32_t nargs) {
    CettaCallOutcome out = {0};
    if (!op(a, args, nargs, &out) || out.kind != CETTA_CALL_VALUE)
        return NULL;
    return out.term;
}

static bool committed(Atom *value) {
    return value && ia_head(value, "ia:committed", 3u);
}

static bool commit_failed(Atom *value, const char *reason) {
    if (!value || !ia_head(value, "ia:refused", 2u) ||
        !ia_head_any(value->expr.elems[1], "commit-failed"))
        return false;
    Atom *why = value->expr.elems[1];
    return why->kind == ATOM_EXPR && why->expr.len == 2u &&
           atom_is_symbol(why->expr.elems[1], reason);
}

static Atom *job(Arena *a, const char *name) {
    return atom_expr2(a, atom_symbol(a, "job"), atom_symbol(a, name));
}

/* (commit ((S REV)) ((take S REV 0 (job a))) ((S (done a)))) */
static Atom **take_and_make(Arena *a, Space *s) {
    Atom *loc = atom_space(a, s);
    Atom *rev = atom_int(a, (int64_t)space_revision(s));
    Atom *seen_entry = atom_expr2(a, loc, rev);
    Atom *take_items[5] = {atom_symbol(a, "take"), loc, rev, atom_int(a, 0), job(a, "a")};
    Atom *take = atom_expr(a, take_items, 5u);
    Atom *make = atom_expr2(a, loc, atom_expr2(a, atom_symbol(a, "done"), atom_symbol(a, "a")));
    Atom **args = arena_alloc(a, 3u * sizeof(*args));
    args[0] = atom_expr(a, &seen_entry, 1u);
    args[1] = atom_expr(a, &take, 1u);
    args[2] = atom_expr(a, &make, 1u);
    return args;
}

static bool holds(Space *s, Atom *atom) {
    for (CettaIndex i = 0; i < space_length64(s); i++)
        if (atom_eq(space_get_at64(s, i), atom))
            return true;
    return false;
}

static void mixed_revision_control(Arena *a) {
    Space published, unpublished;
    space_init(&published);
    space_init(&unpublished);
    space_add(&published, job(a, "a"));
    space_add(&unpublished, job(a, "a"));
    SpaceReadToken old_published = space_read_token(&published);
    SpaceReadToken old_unpublished = space_read_token(&unpublished);
    Atom **published_args = take_and_make(a, &published);
    Atom **unpublished_args = take_and_make(a, &unpublished);
    Atom *args[3];
    for (size_t k = 0; k < 3u; k++) {
        Atom *parts[2] = {published_args[k]->expr.elems[0],
                          unpublished_args[k]->expr.elems[0]};
        args[k] = atom_expr(a, parts, 2u);
    }
    g_mixed_revision_fault.published = &published;
    g_mixed_revision_fault.unpublished = &unpublished;
    g_mixed_revision_fault.old_unpublished = old_unpublished;
    g_ia_commit_after_writes = restore_one_revision;
    Atom *result = call_value(op_commit, a, args, 3u);
    g_ia_commit_after_writes = NULL;

    CHECK(commit_failed(result, "revision-unmoved"));
    CHECK(!committed(result));
    Atom *done = atom_expr2(a, atom_symbol(a, "done"), atom_symbol(a, "a"));
    CHECK(space_length64(&published) == 1u && !holds(&published, job(a, "a")) &&
          holds(&published, done));
    CHECK(space_length64(&unpublished) == 1u && !holds(&unpublished, job(a, "a")) &&
          holds(&unpublished, done));
    CHECK(!space_read_token_is_current(old_published));
    CHECK(!space_read_token_is_current(old_unpublished));
    CHECK(space_read_token_is_current(g_mixed_revision_fault.published_after_writes));
    CHECK(space_read_token_prefix_intact(g_mixed_revision_fault.published_after_writes));
    CHECK(space_program_token_is_current(
        g_mixed_revision_fault.published_program_after_writes));
    CHECK(space_revision(&unpublished) == old_unpublished.revision + 1u);
    CHECK(!space_read_token_is_current(g_mixed_revision_fault.unpublished_after_reset));
    CHECK(!space_read_token_prefix_intact(g_mixed_revision_fault.unpublished_after_reset));
    CHECK(!space_program_token_is_current(
        g_mixed_revision_fault.unpublished_program_after_reset));

    /* Validate the second space alone: the already changed first space must
     * not mask failure to invalidate the second space's old evidence. */
    Atom *stale_args[3] = {unpublished_args[0], atom_unit(a), atom_unit(a)};
    Atom *stale = call_value(op_commit, a, stale_args, 3u);
    CHECK(stale && ia_head(stale, "ia:refused", 2u) &&
          ia_head(stale->expr.elems[1], "stale", 2u));
    CHECK(space_length64(&unpublished) == 1u && holds(&unpublished, done));
    space_free(&published);
    space_free(&unpublished);
    memset(&g_mixed_revision_fault, 0, sizeof(g_mixed_revision_fault));
}

static void allocation_controls(Arena *a) {
    /* The first allocation prepares make locations; the next prepares the
     * epoch's revision snapshots. Neither failure may reach a write. */
    for (size_t after = 0u; after < 2u; after++) {
        Space s;
        space_init(&s);
        space_add(&s, job(a, "a"));
        uint64_t revision = space_revision(&s);
        Atom **args = take_and_make(a, &s);
        g_calloc_before_failure = after;
        Atom *result = call_value(op_commit, a, args, 3u);
        CHECK(g_calloc_before_failure == SIZE_MAX);
        g_calloc_before_failure = SIZE_MAX;
        CHECK(result && ia_head(result, "ia:refused", 2u) &&
              atom_is_symbol(result->expr.elems[1], "no-memory"));
        CHECK(space_revision(&s) == revision);
        CHECK(space_length64(&s) == 1u && holds(&s, job(a, "a")));
        CHECK(committed(call_value(op_commit, a, args, 3u)));
        space_free(&s);
    }

    Space s;
    space_init(&s);
    space_add(&s, job(a, "a"));
    uint64_t revision = space_revision(&s);
    Atom **args = take_and_make(a, &s);
    IaCommitRun c = {.a = a, .seen = args[0], .fp = args[1], .makes = args[2]};
    bool prepared = ia_commit_seen(&c) && ia_commit_footprint(&c) &&
                    ia_commit_linear(&c) && ia_commit_makes(&c);
    CHECK(prepared);
    if (prepared) {
        g_realloc_before_failure = 0u;
        CHECK(!ia_commit_epoch_open(&c));
        CHECK(g_realloc_before_failure == SIZE_MAX);
        g_realloc_before_failure = SIZE_MAX;
        CHECK(c.why && atom_is_symbol(c.why, "no-memory") && !c.epoch_open);
        CHECK(space_revision(&s) == revision && holds(&s, job(a, "a")));

        c.why = NULL;
        bool opened = ia_commit_epoch_open(&c);
        CHECK(opened);
        if (opened) {
            g_realloc_before_failure = 0u;
            CHECK(ia_commit_take(&c));
            CHECK(ia_commit_make(&c));
            /* A pending fault remains unused: result bookkeeping had room. */
            CHECK(g_realloc_before_failure == 0u);
            g_realloc_before_failure = SIZE_MAX;
            CHECK(ia_commit_moved(&c));
            ia_commit_epoch_publish(&c);
            CHECK(!c.epoch_open && c.taken_regions.len == 1u &&
                  c.made_regions.len == 1u);
            CHECK(!holds(&s, job(a, "a")) &&
                  holds(&s, atom_expr2(a, atom_symbol(a, "done"), atom_symbol(a, "a"))));
        }
    }
    ia_commit_run_free(&c);
    space_free(&s);
}

static size_t g_fail_publication_at;
static bool fail_prepared_publication(IaCommitRun *c, size_t index) {
    CHECK(c->prepare_publication && c->epoch_open);
    return index != g_fail_publication_at;
}

static bool refuse_allocation_after_first_publication(IaCommitRun *c, size_t index) {
    CHECK(c->prepare_publication);
    if (index == 1u) g_forbid_publication_allocation = true;
    return true;
}

static bool refuse_publication_and_allocation(IaCommitRun *c, size_t index) {
    (void)refuse_allocation_after_first_publication(c, index);
    return index != 1u;
}

static void prepared_machine_allocation_controls(Arena *a) {
    for (unsigned failure = 0; failure < 2u; failure++) {
        Space input, output;
        space_init(&input); space_init(&output);
        space_add(&input, job(a, "a"));
        Atom *in = atom_space(a, &input), *dst = atom_space(a, &output);
        Atom *body[] = {atom_symbol(a, ","),
            atom_expr3(a, atom_symbol(a, "add-atom"), in, atom_symbol(a, "done")),
            atom_expr3(a, atom_symbol(a, "add-atom"), dst, atom_symbol(a, "checked"))};
        Atom *args[] = {in, atom_expr2(a, atom_symbol(a, "take"), job(a, "a")),
            atom_expr(a, body, 3u)};
        g_publication_allocations = 0u;
        g_ia_commit_before_prepared_publish = failure
            ? refuse_publication_and_allocation : refuse_allocation_after_first_publication;
        CettaCallOutcome result = {0};
        CHECK(op_fire_prepared(a, args, 3u, &result));
        g_ia_commit_before_prepared_publish = NULL;
        g_forbid_publication_allocation = false;
        CHECK(g_publication_allocations == 0u);
        CHECK(ia_head(result.term, "ia:done", 2u));
        CHECK(ia_head(result.term->expr.elems[1], failure ? "Error" : "ia:fired", failure ? 3u : 2u));
        CHECK(!holds(&input, job(a, "a")));
        CHECK(holds(&input, atom_symbol(a, "done")));
        CHECK(holds(&output, atom_symbol(a, "checked")) == !failure);
        space_free(&output); space_free(&input);
    }
}

static Atom *effect_prefix(Atom *result) {
    if (!ia_head(result, "ia:refused", 2u)) return NULL;
    Atom *reason = result->expr.elems[1];
    if (!ia_head(reason, "commit-failed", 3u) ||
        !ia_head(reason->expr.elems[2], "ia:effects", 2u)) return NULL;
    return reason->expr.elems[2]->expr.elems[1];
}

static bool indexed_control_match(Arena *a, Atom *pattern, Atom *value) {
    CETTA_FRAME_IDENTITY_SCOPE(scope);
    CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
    bool fresh = cetta_frame_identity_scope_try(&scope, &epoch);
    CHECK(fresh);
    Bindings bindings;
    bindings_init(&bindings);
    CettaMatchCompletion result = fresh ? ia_match(pattern, value, &bindings, a, epoch)
                                       : CETTA_MATCH_INCOMPLETE;
    bindings_free(&bindings);
    if (result == CETTA_MATCH_INCOMPLETE) {
        fputs("index oracle incomplete: ", stderr); atom_print(pattern, stderr);
        fputs(" against ", stderr); atom_print(value, stderr); fputc('\n', stderr);
    }
    CHECK(result != CETTA_MATCH_INCOMPLETE);
    return result == CETTA_MATCH_MATCHED;
}

static size_t indexed_matches_agree_with_scan(Space *space, Arena *a, Atom *pattern) {
    CettaCount length = space_length64(space), expected = 0u;
    Atom **answers = malloc((size_t)(length ? length : 1u) * sizeof(*answers));
    bool *used = calloc((size_t)(length ? length : 1u), sizeof(*used));
    CHECK(answers && used);
    if (!answers || !used) { free(answers); free(used); return 0u; }
    for (CettaIndex i = 0; i < length; i++) {
        Atom *row = space_get_at64(space, i);
        if (indexed_control_match(a, pattern, row)) answers[expected++] = row;
    }
    CettaIndex *coordinates = NULL;
    CettaCount candidates = space_match_candidates64(space, pattern, &coordinates), matched = 0u;
    for (CettaIndex i = 0; i < candidates; i++) {
        Atom *row = space_match_candidate_at64(space, coordinates[i]);
        if (!indexed_control_match(a, pattern, row)) continue;
        CettaIndex witness = 0u;
        while (witness < expected && (used[witness] || !atom_eq(row, answers[witness]))) witness++;
        CHECK(witness < expected);
        if (witness < expected) used[witness] = true;
        matched++;
    }
    CHECK(matched == expected);
    free(coordinates); free(used); free(answers);
    return (size_t)expected;
}

static void prepared_head_index_controls(Arena *a) {
    Space space;
    space_init(&space);
    for (unsigned i = 0; i < 64u; i++)
        CHECK(space_admit_atom(&space, a, atom_expr3(a, atom_symbol(a, "IndexFrame"),
            atom_var(a, "frame-variable"), atom_int(a, i))));
    Atom *metadata = atom_expr3(a, atom_internal_tag(a, CETTA_INTERNAL_TAG_PRIME_OWN),
        atom_symbol(a, "lexical-fresh/per-call/reference"), atom_var(a, "private"));
    Atom *owned = atom_expr3(a, atom_symbol(a, "Owned"), atom_int(a, 7), metadata);
    Atom *parameter = atom_var(a, "owned-value");
    Atom *pattern = atom_expr2(a, atom_symbol(a, "Owned"), parameter);
    CHECK(atom_authored_len(owned) == 2u);
    CHECK(space_admit_atom(&space, a, owned) && space_admit_atom(&space, a, owned));
    CHECK(indexed_control_match(a, pattern, owned));
    CHECK(indexed_matches_agree_with_scan(&space, a, pattern) == 2u);
    CettaSpaceHeadCursor initial;
    CHECK(space_head_index_open(&space, pattern, &initial));
    CettaSpaceHeadIndex *index = initial.index;
    Atom *outputs[] = {owned, owned};
    SpaceNativeBagAppend *append = space_native_bag_append_prepare(&space, a, outputs, 2u);
    CHECK(append && space_native_bag_append_reserve(append) && space_native_bag_append_publish(append));
    space_native_bag_append_free(append);
    CettaSpaceHeadCursor extended;
    CHECK(space_head_index_open(&space, pattern, &extended));
    CHECK(extended.index == index);
    CettaIndex position = 0u, previous = 0u;
    size_t occurrences = 0u;
    while (space_head_index_next(&extended, space.native.len, NULL, 0u, &position)) {
        CHECK(!occurrences || previous < position);
        previous = position; occurrences++;
    }
    CHECK(occurrences == 4u && indexed_matches_agree_with_scan(&space, a, pattern) == 4u);

    /* A printed OWN or a private OWN outside a scope-bearing shape is data. */
    Atom *ordinary = atom_expr2(a, atom_symbol(a, "OWN"), atom_symbol(a, "record"));
    Atom *not_metadata = atom_expr3(a, atom_symbol(a, "Owned"), atom_int(a, 7), ordinary);
    Atom *outside_shape = atom_expr(a, (Atom *[]){atom_symbol(a, "Owned"), atom_int(a, 7),
        atom_int(a, 8), metadata}, 4u);
    CHECK(atom_authored_len(not_metadata) == 3u && atom_authored_len(outside_shape) == 4u);
    CHECK(space_admit_atom(&space, a, not_metadata) && space_admit_atom(&space, a, outside_shape));
    CHECK(indexed_matches_agree_with_scan(&space, a, pattern) == 4u);

    /* Removed rows and settled coordinates cannot be revived by catch-up. */
    CettaIndex removed = 64u;
    CHECK(space_remove_logical_positions(&space, &removed, 1u));
    (void)indexed_matches_agree_with_scan(&space, a, pattern);
    space_settle_marks(&space);
    (void)indexed_matches_agree_with_scan(&space, a, pattern);
    SpaceOccurrenceCursor reader;
    space_occurrence_cursor_init_empty(&reader);
    CHECK(space_occurrence_cursor_init(&space, pattern, &reader));
    CHECK(!space_native_bag_append_admitted(&space));
    CHECK(space_admit_atom(&space, a, owned));
    Atom *row = NULL;
    size_t old_matches = 0u;
    do {
        row = NULL;
        (void)space_occurrence_cursor_next(&reader, NULL, &row);
        if (row && indexed_control_match(a, pattern, row)) old_matches++;
    } while (row);
    CHECK(old_matches == 3u);
    space_occurrence_cursor_release(&reader);
    CHECK(indexed_matches_agree_with_scan(&space, a, pattern) == 4u);
    space_free(&space);

    /* Ordinary open terms have their own complete canonical-match oracle;
     * lexical metadata interpretation is exercised by the Owned cases. */
    space_init(&space);
    for (unsigned i = 0; i < 64u; i++)
        CHECK(space_admit_atom(&space, a, atom_expr3(a, atom_symbol(a, "IndexFrame"),
            atom_var(a, "frame-variable"), atom_int(a, i))));
    Atom *target = atom_expr3(a, atom_symbol(a, "IndexTarget"), atom_int(a, 1), atom_int(a, 1));
    Atom *different = atom_expr3(a, atom_symbol(a, "IndexTarget"), atom_int(a, 1), atom_int(a, 2));
    Atom *open_head = atom_expr3(a, atom_var(a, "open-head"), atom_int(a, 1), atom_int(a, 1));
    CHECK(space_admit_atom(&space, a, target) && space_admit_atom(&space, a, target));
    CHECK(space_admit_atom(&space, a, different) && space_admit_atom(&space, a, open_head));
    CHECK(space_admit_atom(&space, a, atom_var(a, "whole-row")));
#if CETTA_MANAGED_SYMBOLS
    Atom *unregistered = atom_symbol_data(a, "IndexUnregisteredHead");
    CHECK(atom_symbol_current_id(unregistered) == SYMBOL_ID_NONE);
    Atom *unregistered_pattern = atom_expr3(a, unregistered, atom_var(a, "unregistered-left"),
        atom_var(a, "unregistered-right"));
    CettaSpaceHeadCursor unregistered_cursor;
    CHECK(space_head_index_open(&space, unregistered_pattern, &unregistered_cursor));
    size_t unregistered_candidates = 0u;
    while (space_head_index_next(&unregistered_cursor, space.native.len, NULL, 0u, &position))
        unregistered_candidates++;
    CHECK(unregistered_candidates == 2u);
    CettaIndex *unregistered_coordinates = NULL;
    CHECK(space_match_candidates64(&space, unregistered_pattern, &unregistered_coordinates) == 2u);
    free(unregistered_coordinates);
    CHECK(indexed_matches_agree_with_scan(&space, a, unregistered_pattern) == 2u);
#endif
    Atom *repeated = atom_var(a, "repeated-query");
    Atom *repeated_pattern = atom_expr3(a, atom_symbol(a, "IndexTarget"), repeated, repeated);
    CHECK(indexed_matches_agree_with_scan(&space, a, repeated_pattern) == 4u);
    (void)indexed_matches_agree_with_scan(&space, a, atom_var(a, "root-query"));
    space_free(&space);
}

static bool refuse_counted_publication(IaCommitRun *c, size_t index) {
    if (index == 1u) {
        TermUniverse *universe = c->written[index].space->native.universe;
        CHECK(universe->counting && universe->collection_inhibit != 0u);
        CHECK(!term_universe_collect(universe, NULL));
    }
    return refuse_publication_and_allocation(c, index);
}

static void prepared_retirement_controls(Arena *a) {
    Arena persistent;
    arena_init_detached(&persistent);
    TermUniverse universe;
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    term_universe_set_collection_mode(&universe, TERM_UNIVERSE_COLLECTION_RECLAIM);
    Space input, outputs[2];
    space_init_with_universe(&input, &universe);
    for (size_t i = 0; i < 2u; i++) space_init_with_universe(&outputs[i], &universe);
    CHECK(space_admit_atom(&input, &persistent, job(a, "a")));
    CHECK(term_universe_collect(&universe, NULL) && universe.counting);

    /* A failed reservation changes neither the queue nor the source. */
    size_t old_capacity = universe.zero_cap, old_length = universe.zero_len;
    g_forbid_publication_allocation = true;
    CHECK(!term_universe_prepare_releases(&universe, old_capacity + 1u));
    g_forbid_publication_allocation = false;
    CHECK(universe.zero_cap == old_capacity && universe.zero_len == old_length);
    CHECK(!term_universe_prepare_releases(&universe, SIZE_MAX));

    Atom **args = take_and_make(a, &input);
    Atom *first = args[2]->expr.elems[0];
    Atom *makes[] = {first,
        atom_expr2(a, atom_space(a, &outputs[0]), atom_symbol(a, "unpublished-a")),
        atom_expr2(a, atom_space(a, &outputs[1]), atom_symbol(a, "unpublished-b"))};
    args[2] = atom_expr(a, makes, 3u);
    g_publication_allocations = 0u;
    g_ia_commit_before_prepared_publish = refuse_counted_publication;
    Atom *result = call_value(op_commit_prepared, a, args, 3u);
    g_ia_commit_before_prepared_publish = NULL;
    g_forbid_publication_allocation = false;
    CHECK(g_publication_allocations == 0u);
    CHECK(universe.collection_inhibit == 0u);
    Atom *prefix = effect_prefix(result);
    CHECK(prefix && ia_head(prefix, "ia:effect", 3u) &&
          prefix->expr.elems[2]->kind == ATOM_EXPR && !prefix->expr.elems[2]->expr.len);
    CHECK(!holds(&input, job(a, "a")) &&
          holds(&input, atom_expr2(a, atom_symbol(a, "done"), atom_symbol(a, "a"))));
    CHECK(!space_length64(&outputs[0]) && !space_length64(&outputs[1]));
    CHECK(universe.zero_len > old_length);
    for (size_t i = 0; i < 2u; i++) space_free(&outputs[i]);
    space_free(&input);
    term_universe_free(&universe);
    arena_free(&persistent);

    Space authority;
    space_init(&authority);
    Atom *record = atom_expr(a, (Atom *[]){atom_symbol(a, "type:rule"),
        atom_symbol(a, "private-image-record"), atom_int(a, 0), atom_unit(a),
        atom_symbol(a, "True")}, 5u);
    CHECK(space_admit_atom(&authority, a, record));
    prime_scoped_judgment_admit(a, &authority, record);
    CHECK(prime_scoped_judgment_admitted(a, &authority, record));
    Space *image = space_native_bag_publication_clone(&authority, NULL, 0u);
    CHECK(image != NULL);
    if (image) {
        size_t visited = 0u;
        bool empty = false;
        CHECK(prime_scoped_judgment_instance_empty(space_instance_id(image), SIZE_MAX, &visited, &empty));
        CHECK(empty && !prime_scoped_judgment_admitted(a, image, record));
        g_publication_allocations = 0u;
        g_forbid_publication_allocation = true;
        space_native_bag_publication_dispose(image);
        g_forbid_publication_allocation = false;
        CHECK(g_publication_allocations == 0u);
    }
    CHECK(prime_scoped_judgment_admitted(a, &authority, record));
    space_free(&authority);

    /* Copying settled relation facts must keep rows and their exact order,
     * rather than promote the private copy and refuse its publication. */
    Space tuples;
    space_init(&tuples);
    for (unsigned i = 0; i < 4096u; i++)
        CHECK(space_admit_atom(&tuples, a, atom_expr3(a, atom_symbol(a, "image-tuple"),
            atom_int(a, i % 64u), atom_int(a, (i / 64u) % 64u))));
    CHECK(space_dense_facts(&tuples) != 0u);
    space_dense_settle(&tuples, SYMBOL_ID_NONE);
    CHECK(space_dense_facts(&tuples) == 0u && space_length64(&tuples) == 4096u);
    image = space_native_bag_publication_clone(&tuples, NULL, 0u);
    CHECK(image && space_dense_facts(image) == 0u && space_length64(image) == 4096u);
    if (image) {
        SpaceNativeBagPublication publication;
        CHECK(space_native_bag_publication_prepare(&tuples, image, &publication));
        space_native_bag_publication_dispose(image);
    }
    CettaIndex excluded[] = {4095u, 1u};
    image = space_native_bag_publication_clone(&tuples, excluded, 2u);
    CHECK(image && space_dense_facts(image) == 0u && space_length64(image) == 4094u);
    if (image) {
        for (CettaIndex i = 0; i < 4094u; i++)
            CHECK(atom_eq(space_get_at64(image, i), space_get_at64(&tuples, i < 1u ? i : i + 1u)));
        SpaceNativeBagPublication publication;
        CHECK(space_native_bag_publication_prepare(&tuples, image, &publication));
        space_native_bag_publication_dispose(image);
    }
    CettaIndex duplicate[] = {1u, 1u};
    CHECK(!space_native_bag_publication_clone(&tuples, duplicate, 2u));
    CettaIndex outside[] = {4096u};
    CHECK(!space_native_bag_publication_clone(&tuples, outside, 1u));
    space_free(&tuples);
}

static void prepared_append_controls(Arena *a) {
    Space destination;
    space_init(&destination);
    Atom *pattern = atom_expr2(a, atom_symbol(a, "appended"), atom_var(a, "value"));
    for (unsigned i = 0; i < 48u; i++)
        CHECK(space_admit_atom(&destination, a, atom_expr3(a, atom_symbol(a, "frame"),
            atom_var(a, "shared"), atom_int(a, i))));
    CettaIndex *candidates = NULL;
    (void)space_match_candidates64(&destination, pattern, &candidates);
    free(candidates);
    CHECK(destination.match_backend.native.pinned == NULL);
    Atom *added = atom_expr2(a, atom_symbol(a, "appended"), atom_int(a, 7));
    CHECK(!space_contains_exact(&destination, added));
    CettaIndex removed = 0u;
    CHECK(space_remove_logical_positions(&destination, &removed, 1u));
    Atom *first = space_get_at64(&destination, 0u);
    SpaceReadToken before = space_read_token(&destination);
    CettaCount length = space_length64(&destination);
    Atom *values[] = {added, added};
    SpaceNativeBagAppend *append = space_native_bag_append_prepare(&destination, a, values, 2u);
    CHECK(append && space_read_token_is_current(before));
    CHECK(append && space_native_bag_append_reserve(append));
    g_publication_allocations = 0u;
    g_forbid_publication_allocation = true;
    bool published = append && space_native_bag_append_publish(append);
    space_native_bag_append_free(append);
    g_forbid_publication_allocation = false;
    CHECK(published && g_publication_allocations == 0u);
    CHECK(space_length64(&destination) == length + 2u);
    CHECK(atom_eq(first, space_get_at64(&destination, 0u)));
    CHECK(atom_eq(added, space_get_at64(&destination, length)) &&
          atom_eq(added, space_get_at64(&destination, length + 1u)));
    CHECK(space_read_token_prefix_intact(before) && !space_read_token_is_current(before));
    CHECK(space_contains_exact(&destination, added));
    candidates = NULL;
    CettaCount count = space_match_candidates64(&destination, pattern, &candidates);
    size_t outputs = 0u;
    for (CettaCount i = 0; i < count; i++) {
        Atom *candidate = space_match_candidate_at64(&destination, candidates[i]);
        if (candidate && atom_eq(candidate, added)) {
            outputs++;
        }
    }
    CHECK(outputs == 2u);
    free(candidates);
    append = space_native_bag_append_prepare(&destination, a, values, 1u);
    CHECK(append != NULL);
    CHECK(space_admit_atom(&destination, a, atom_symbol(a, "intervening")));
    length = space_length64(&destination);
    CHECK(!space_native_bag_append_reserve(append));
    CHECK(!space_native_bag_append_publish(append));
    CHECK(space_length64(&destination) == length);
    space_native_bag_append_free(append);
    space_free(&destination);

    /* Mark settlement changes physical coordinates while preserving the
     * logical read token. A copied reservation must not revive dead rows. */
    Space settled;
    space_init(&settled);
    for (unsigned i = 0; i < 48u; i++)
        CHECK(space_admit_atom(&settled, a, atom_expr3(a, atom_symbol(a, "frame"),
            atom_var(a, "shared"), atom_int(a, i))));
    CHECK(space_remove_logical_positions(&settled, &removed, 1u));
    CHECK(space_has_marks(&settled));
    CettaCount settled_length = space_length64(&settled);
    Atom *settled_first = space_get_at64(&settled, 0u);
    Atom *settled_last = space_get_at64(&settled, settled_length - 1u);
    size_t growing_count = (size_t)(settled.native.cap - settled.native.len + 1u);
    Atom **growing = malloc(growing_count * sizeof(*growing));
    CHECK(growing != NULL);
    for (size_t i = 0; i < growing_count; i++) growing[i] = added;
    append = space_native_bag_append_prepare(&settled, a, growing, growing_count);
    CHECK(append && space_native_bag_append_reserve(append));
    before = space_read_token(&settled);
    space_settle_marks(&settled);
    CHECK(!space_has_marks(&settled) && space_read_token_is_current(before));
    CHECK(!space_native_bag_append_publish(append));
    CHECK(space_length64(&settled) == settled_length &&
          atom_eq(space_get_at64(&settled, 0u), settled_first) &&
          atom_eq(space_get_at64(&settled, settled_length - 1u), settled_last));
    space_native_bag_append_free(append);
    append = space_native_bag_append_prepare(&settled, a, growing, growing_count);
    CHECK(append && space_native_bag_append_reserve(append) &&
          space_native_bag_append_publish(append));
    CHECK(space_length64(&settled) == settled_length + growing_count);
    CHECK(atom_eq(space_get_at64(&settled, settled_length - 1u), settled_last));
    space_native_bag_append_free(append);
    free(growing);
    space_free(&settled);

    Space empty;
    space_init(&empty);
    append = space_native_bag_append_prepare(&empty, a, values, 1u);
    CHECK(append != NULL);
    g_forbid_publication_allocation = true;
    bool reserved = space_native_bag_append_reserve(append);
    g_forbid_publication_allocation = false;
    CHECK(!reserved && space_length64(&empty) == 0u && space_revision(&empty) == 0u);
    CHECK(space_native_bag_append_reserve(append));
    CHECK(space_native_bag_append_publish(append));
    CHECK(space_contains_exact(&empty, added));
    space_native_bag_append_free(append);
    space_free(&empty);

    Space open;
    space_init(&open);
    Atom *subject = atom_symbol(a, "subject");
    CHECK(!space_type_annotation_may_match_subject(&open, subject));
    Atom *unknown = atom_var(a, "open_row");
    append = space_native_bag_append_prepare(&open, a, &unknown, 1u);
    CHECK(append && space_native_bag_append_reserve(append) &&
          space_native_bag_append_publish(append));
    CHECK(space_type_annotation_may_match_subject(&open, subject));
    space_native_bag_append_free(append);
    space_free(&open);
}

static void prepared_publication_controls(Arena *a) {
    for (size_t fail_at = 0; fail_at < 3u; fail_at++) {
        Space input, output;
        space_init(&input);
        space_init(&output);
        space_add(&input, job(a, "a"));
        Atom **args = take_and_make(a, &input);
        Atom *new_output = atom_expr2(a, atom_space(a, &output), atom_symbol(a, "checked"));
        args[2] = atom_expr2(a, args[2]->expr.elems[0], new_output);
        uint64_t before_input = space_revision(&input), before_output = space_revision(&output);
        g_fail_publication_at = fail_at;
        g_ia_commit_before_prepared_publish = fail_prepared_publication;
        Atom *result = call_value(op_commit_prepared, a, args, 3u);
        g_ia_commit_before_prepared_publish = NULL;
        if (!committed(result) && !effect_prefix(result)) {
            fputs("prepared publication admission: ", stderr);
            atom_print(result, stderr); fputc('\n', stderr);
        }
        if (fail_at < 2u) {
            Atom *prefix = effect_prefix(result);
            CHECK(prefix && (fail_at ? ia_head(prefix, "ia:effect", 3u)
                                    : prefix->kind == ATOM_EXPR && !prefix->expr.len));
            if (fail_at && ia_head(prefix, "ia:effect", 3u)) {
                Atom *publication = prefix->expr.elems[1];
                CHECK(ia_head(publication, "ia:publication", 5u));
                CHECK(ia_space_of(publication->expr.elems[1]) == &input);
                CHECK(publication->expr.elems[3]->expr.len == 1u);
                CHECK(publication->expr.elems[4]->expr.len == 1u);
                CHECK(prefix->expr.elems[2]->kind == ATOM_EXPR &&
                      !prefix->expr.elems[2]->expr.len);
            }
        } else CHECK(committed(result));
        CHECK(holds(&input, job(a, "a")) == (fail_at == 0u));
        CHECK(holds(&output, atom_symbol(a, "checked")) == (fail_at == 2u));
        CHECK((space_revision(&input) == before_input) == (fail_at == 0u));
        CHECK((space_revision(&output) == before_output) == (fail_at < 2u));
        if (fail_at > 0u) {
            /* A retry using the consumed request's original evidence cannot
             * publish or duplicate a completed effect. */
            CHECK(!committed(call_value(op_commit_prepared, a, args, 3u)));
            CHECK(space_length64(&output) == (fail_at == 2u ? 1u : 0u));
        }
        space_free(&input);
        space_free(&output);
    }
    /* Failure of a private output admission leaves every public destination
     * unchanged, including the request which will eventually be consumed. */
    Space input;
    space_init(&input);
    space_add(&input, job(a, "a"));
    Atom **args = take_and_make(a, &input);
    uint64_t before = space_revision(&input);
    g_admits_before_failure = 0u;
    Atom *failed = call_value(op_commit_prepared, a, args, 3u);
    CHECK(g_admits_before_failure == SIZE_MAX);
    CHECK(ia_head(failed, "ia:refused", 2u));
    CHECK(ia_head(failed->expr.elems[1], "prepare-failed", 2u));
    CHECK(atom_is_symbol(failed->expr.elems[1]->expr.elems[1], "prepare-make-failed"));
    CHECK(holds(&input, job(a, "a")) && space_revision(&input) == before);
    CHECK(committed(call_value(op_commit_prepared, a, args, 3u)));
    space_free(&input);

    Space first, second;
    space_init(&first); space_init(&second);
    space_add(&first, job(a, "a"));
    space_add(&second, atom_symbol(a, "retained"));
    SpaceOccurrenceCursor reader;
    space_occurrence_cursor_init_empty(&reader);
    CHECK(space_occurrence_cursor_init(&second, atom_var(a, "pinned"), &reader));
    CHECK(second.match_backend.native.pinned != NULL);
    args = take_and_make(a, &first);
    Atom *make = atom_expr2(a, atom_space(a, &second), atom_symbol(a, "checked"));
    args[2] = atom_expr2(a, args[2]->expr.elems[0], make);
    g_publication_allocations = 0u;
    g_ia_commit_before_prepared_publish = refuse_allocation_after_first_publication;
    Atom *result = call_value(op_commit_prepared, a, args, 3u);
    g_ia_commit_before_prepared_publish = NULL;
    g_forbid_publication_allocation = false;
    CHECK(g_publication_allocations == 0u);
    CHECK(committed(result));
    CHECK(holds(&second, atom_symbol(a, "checked")));
    Atom *old_row = NULL;
    (void)space_occurrence_cursor_next(&reader, NULL, &old_row);
    CHECK(old_row && atom_is_symbol(old_row, "retained"));
    old_row = NULL;
    (void)space_occurrence_cursor_next(&reader, NULL, &old_row);
    CHECK(!old_row);
    space_occurrence_cursor_release(&reader);
    Space *copy = space_heap_clone_shallow(&first);
    CHECK(copy != NULL);
    if (copy) {
        SpaceMatchBackendOps customized = *first.match_backend.ops;
        const SpaceMatchBackendOps *original = first.match_backend.ops;
        first.match_backend.ops = &customized;
        SpaceNativeBagPublication publication;
        CHECK(!space_native_bag_publication_prepare(&first, copy, &publication));
        first.match_backend.ops = original;
        space_free(copy); free(copy);
    }
    space_free(&second); space_free(&first);
}

static void prepared_trace_controls(Arena *a) {
    arena_init_detached(&g_trace_store);
    g_trace_checked = true;
    g_trace = tmpfile();
    CHECK(g_trace != NULL);
    if (!g_trace) return;
    Space inputs[4], outputs[4];
    for (size_t failure = 0; failure < 4u; failure++) {
        Space *input = &inputs[failure], *output = &outputs[failure];
        space_init(input); space_init(output);
        space_add(input, job(a, "a"));
        Atom **args = take_and_make(a, input);
        Atom *extra = atom_expr2(a, atom_space(a, output), atom_symbol(a, "checked"));
        args[2] = atom_expr2(a, args[2]->expr.elems[0], extra);
        ia_trace_sync_commit(args[0], args[1], args[2]);
        uint64_t old_edge = g_trace_edge;
        long old_bytes = ftell(g_trace);
        if (failure < 2u) {
            g_fail_publication_at = failure;
            g_ia_commit_before_prepared_publish = fail_prepared_publication;
        }
        if (failure == 2u) g_ia_commit_after_writes = restore_revisions;
        Atom *result = call_value(op_commit_prepared, a, args, 3u);
        g_ia_commit_before_prepared_publish = NULL;
        g_ia_commit_after_writes = NULL;
        CHECK(g_trace_taken_len == 0u);
        CHECK(g_trace_edge == old_edge + (failure == 3u ? 1u : 0u));
        if (failure < 3u) {
            CHECK(effect_prefix(result) != NULL);
            CHECK(g_trace_incomplete);
            CHECK(ftell(g_trace) == old_bytes);
        } else CHECK(committed(result));
        IaRegion *region = ia_region_of(input, false);
        CHECK(region && region->len == space_length64(input));
        CHECK(region && region->len && atom_eq(region->atoms[0], space_get_at64(input, 0)));
    }
    ia_trace_finish();
    for (size_t i = 0; i < 4u; i++) { space_free(&outputs[i]); space_free(&inputs[i]); }
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    VarInternTable variables;
    var_intern_init(&variables);
    g_var_intern = &variables;
    g_hashcons = NULL;
    Arena a;
    arena_init_detached(&a);

    /* Empty footprints are legitimate when an await has no ready reply. */
    CHECK(ia_occs_conflict(&a, NULL, 0u, false) == NULL);
    CHECK(ia_occs_conflict(&a, NULL, 0u, true) == NULL);
    CHECK(cetta_interaction_picks_linear(NULL, NULL, NULL, 0u, NULL));
    allocation_controls(&a);
    mixed_revision_control(&a);
    prepared_publication_controls(&a);
    prepared_append_controls(&a);
    prepared_retirement_controls(&a);
    prepared_head_index_controls(&a);
    prepared_machine_allocation_controls(&a);
    prepared_trace_controls(&a);

    /* positive */
    Space s;
    space_init(&s);
    space_add(&s, job(&a, "a"));
    uint64_t before = space_revision(&s);
    CHECK(committed(call_value(op_commit, &a, take_and_make(&a, &s), 3u)));
    CHECK(space_revision(&s) != before);
    CHECK(!holds(&s, job(&a, "a")));
    CHECK(holds(&s, atom_expr2(&a, atom_symbol(&a, "done"), atom_symbol(&a, "a"))));

    /* unmoved */
    Space f;
    space_init(&f);
    space_add(&f, job(&a, "a"));
    before = space_revision(&f);
    SpaceReadToken fault_read = space_read_token(&f);
    g_ia_commit_after_writes = restore_revisions;
    Atom *faulted = call_value(op_commit, &a, take_and_make(&a, &f), 3u);
    g_ia_commit_after_writes = NULL;
    CHECK(commit_failed(faulted, "revision-unmoved"));
    CHECK(space_revision(&f) != before && !holds(&f, job(&a, "a")));
    CHECK(!space_read_token_is_current(fault_read));

    /* take-failed: a group whose position and atom the space does not hold
     * reaches the take with every guard holding (nothing else to validate). */
    Space t;
    space_init(&t);
    space_add(&t, job(&a, "a"));
    CettaIndex missing_pos = 7u;
    Atom *missing_atom = job(&a, "absent");
    IaSpaceTakes group = {.space = &t, .loc = {.space = &t, .atom = atom_space(&a, &t)}};
    group.takes = malloc(sizeof(*group.takes));
    group.taken_atoms = malloc(sizeof(*group.taken_atoms));
    CHECK(group.takes && group.taken_atoms);
    if (group.takes && group.taken_atoms) {
        group.takes[0] = missing_pos;
        group.taken_atoms[0] = missing_atom;
        group.take_len = group.take_cap = 1u;
        IaCommitRun tc = {.a = &a, .seen = atom_unit(&a), .fp = atom_unit(&a),
                          .makes = atom_unit(&a)};
        tc.groups = malloc(sizeof(*tc.groups));
        CHECK(tc.groups != NULL);
        if (tc.groups) {
            tc.groups[0] = group;
            tc.group_len = tc.group_cap = 1u;
            CHECK(ia_commit_run(&tc) == ia_commit_run_engine_failure);
            CHECK(tc.why && atom_is_symbol(tc.why, "take-failed"));
            CHECK(!tc.taken && !tc.epoch_open);
            CHECK(space_length64(&t) == 1u && holds(&t, job(&a, "a")));
            ia_commit_run_free(&tc);
        }
    }

    /* make-failed: a make into no space cannot be stored */
    IaLoc nowhere = {0};
    Atom *make = atom_expr2(&a, atom_space(&a, &t), atom_symbol(&a, "lost"));
    IaCommitRun mc = {.a = &a, .seen = atom_unit(&a), .fp = atom_unit(&a),
                      .makes = atom_expr(&a, &make, 1u), .made = &nowhere};
    CHECK(!ia_commit_make(&mc));
    CHECK(mc.why && atom_is_symbol(mc.why, "make-failed") && !mc.made_done);

    /* untracked: the walk saw (lock b) absent at revision r; a write outside
     * the commit adds it and puts the revision back; the commit validates at r
     * and is accepted, though the absence it read no longer holds. */
    Space q;
    space_init(&q);
    space_add(&q, atom_expr2(&a, atom_symbol(&a, "lock"), atom_symbol(&a, "a")));
    uint64_t r = space_revision(&q);
    Atom *lock_b = atom_expr2(&a, atom_symbol(&a, "lock"), atom_symbol(&a, "b"));
    CHECK(!holds(&q, lock_b));
    space_add(&q, lock_b);
    q.revision = r;
    Atom *qloc = atom_space(&a, &q);
    Atom *seen_entry = atom_expr2(&a, qloc, atom_int(&a, (int64_t)r));
    Atom *acquired = atom_expr2(&a, qloc, atom_symbol(&a, "acquired-b"));
    Atom *qargs[3] = {atom_expr(&a, &seen_entry, 1u), atom_unit(&a), atom_expr(&a, &acquired, 1u)};
    CHECK(committed(call_value(op_commit, &a, qargs, 3u)));
    CHECK(holds(&q, lock_b) && holds(&q, atom_symbol(&a, "acquired-b")));

    printf("Interaction commit protocol%s: %u checks, %u failures\n",
           ia_commit_run_CHECKED ? " (checked)" : "", g_checks, g_failures);
    space_free(&s);
    space_free(&f);
    space_free(&t);
    space_free(&q);
    arena_free(&a);
    var_intern_free(&variables);
    g_var_intern = NULL;
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return g_failures ? 1 : 0;
}
