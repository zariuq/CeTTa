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

/* Allocation faults are confined to the interaction implementation in this
 * translation unit. They do not alter the space allocator or production ABI. */
static size_t g_calloc_before_failure = SIZE_MAX;
static size_t g_realloc_before_failure = SIZE_MAX;

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
#include "../src/interaction_core.c"
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
