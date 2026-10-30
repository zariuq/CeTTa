/* Retained negative queries use the same native machine and commit path.
 * The controlled interrupt and allocation hooks qualify failure branches;
 * they do not replace the production evaluator or space implementation. */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "../src/match.h"

static bool g_cancel;
static bool g_incomplete_match;
static bool g_incomplete_scan_only;
static bool g_cancel_after_match;
static CettaMatchCompletion query_match(Atom *, Atom *, BindingsBuilder *, Arena *,
    CettaFrameIdentity, uint64_t);
static CettaMatchCompletion query_match_materialized(Atom *, Atom *,
    BindingsBuilder *, Arena *, uint64_t);
static bool query_interrupted(void) { return g_cancel; }
static size_t g_calloc_fail = SIZE_MAX, g_realloc_fail = SIZE_MAX;
static bool g_frontier_fail, g_frontier_failed;
void *__real_cetta_fallible_malloc(size_t size);
void *__wrap_cetta_fallible_malloc(size_t size) {
    if (g_frontier_fail && size == sizeof(Atom *)) {
        g_frontier_fail = false;
        g_frontier_failed = true;
        return NULL;
    }
    return __real_cetta_fallible_malloc(size);
}

static void *query_calloc(size_t n, size_t bytes) {
    if (g_calloc_fail == 0u) {
        g_calloc_fail = SIZE_MAX;
        return NULL;
    }
    if (g_calloc_fail != SIZE_MAX) g_calloc_fail--;
    return calloc(n, bytes);
}

static void *query_realloc(void *p, size_t bytes) {
    if (g_realloc_fail == 0u) {
        g_realloc_fail = SIZE_MAX;
        return NULL;
    }
    if (g_realloc_fail != SIZE_MAX) g_realloc_fail--;
    return realloc(p, bytes);
}

#define eval_native_interrupted query_interrupted
#define match_atoms_epoch_builder_checked query_match
#define match_atoms_builder_checked query_match_materialized
#define calloc query_calloc
#define realloc query_realloc
#include "../src/interaction_core.c"
#undef realloc
#undef calloc
#undef eval_native_interrupted
#undef match_atoms_epoch_builder_checked
#undef match_atoms_builder_checked

static CettaMatchCompletion query_match(Atom *left, Atom *right, BindingsBuilder *builder,
    Arena *arena, CettaFrameIdentity epoch, uint64_t budget) {
    if (g_incomplete_match && (!g_incomplete_scan_only || atom_is_symbol(left, "missing")))
        return CETTA_MATCH_INCOMPLETE;
    CettaMatchCompletion result = cetta_frame_identity_is_none(epoch)
        ? match_atoms_builder_checked(left, right, builder, arena, budget)
        : match_atoms_epoch_builder_checked(left, right, builder, arena, epoch, budget);
    if (g_cancel_after_match && result == CETTA_MATCH_MATCHED)
        g_cancel = true;
    return result;
}

static CettaMatchCompletion query_match_materialized(Atom *left, Atom *right,
    BindingsBuilder *builder, Arena *arena, uint64_t budget) {
    return query_match(left, right, builder, arena, CETTA_FRAME_IDENTITY_NONE, budget);
}

static unsigned g_checks, g_failures;
#define CHECK(x) do { \
    g_checks++; \
    if (!(x)) { g_failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } \
} while (0)

static Atom *form(Arena *a, const char *head, Atom *body) {
    return atom_expr2(a, atom_symbol(a, head), body);
}

static Atom *conjunction(Arena *a, Atom **items, size_t n) {
    Atom **parts = arena_alloc(a, (n + 1u) * sizeof(*parts));
    parts[0] = atom_symbol(a, ",");
    for (size_t i = 0; i < n; i++) parts[i + 1u] = items[i];
    return atom_expr(a, parts, (CettaExprLen)(n + 1u));
}

static Atom *computed(Arena *a, const char *name) {
    return atom_expr3(a, atom_symbol(a, "let"), atom_var(a, name), atom_symbol(a, "ask"));
}

static Atom *start(Arena *a, Space *space, Atom *query) {
    Atom *parts[2] = {form(a, "take", atom_symbol(a, "pending")), form(a, "absent", query)};
    Atom *loc = atom_space(a, space);
    Atom *items[4] = {atom_symbol(a, "transform"), loc,
        conjunction(a, parts, 2u), atom_expr3(a, atom_symbol(a, "add-atom"), loc, atom_symbol(a, "done"))};
    return ia_machine_start(IA_MODE_NOW, atom_expr(a, items, 4u), a);
}

static Atom *resume(Arena *a, Atom *token, Atom *values) {
    Atom *args[2] = {token, values};
    CettaCallOutcome out = {0};
    return op_resume(a, args, 2u, &out) && out.kind == CETTA_CALL_VALUE ? out.term : NULL;
}

static bool finished(Atom *value, const char *outcome) {
    return ia_head(value, "ia:done", 2u) && atom_is_symbol(value->expr.elems[1], outcome);
}

static bool fired(Atom *value) {
    return ia_head(value, "ia:done", 2u) && ia_head(value->expr.elems[1], "ia:fired", 2u);
}

static bool failed(Atom *value) {
    return ia_head(value, "ia:done", 2u) && ia_head(value->expr.elems[1], "Error", 3u);
}

static bool holds(Space *space, Atom *value) {
    for (CettaIndex i = 0; i < space_length64(space); i++)
        if (atom_eq(space_get_at64(space, i), value)) return true;
    return false;
}

static void pending(Space *space, Arena *a) { space_add(space, atom_symbol(a, "pending")); }

static void request_ownership(void) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    Atom *parts[2] = {computed(&a, "x"), computed(&a, "y")};
    Atom *first = start(&a, &space, conjunction(&a, parts, 2u));
    CHECK(ia_head(first, "ia:need", 3u));
    CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "pending")));
    if (ia_head(first, "ia:need", 3u)) {
        IaResumeToken *issued = ia_resume_token_of(first->expr.elems[1]);
        IaMachine *owner = issued ? issued->machine : NULL;
        CHECK(owner && !owner->retired && arena_accounted_live_bytes(&owner->arena) > 0u);
        Atom *one = atom_int(&a, 7);
        Atom *second = resume(&a, first->expr.elems[1], atom_expr(&a, &one, 1u));
        CHECK(ia_head(second, "ia:need", 3u));
        CHECK(owner && !owner->retired && owner->waiting);
        CHECK(space_length64(&space) == 1u);
        Atom *stale = resume(&a, first->expr.elems[1], atom_unit(&a));
        CHECK(failed(stale));
        CHECK(space_length64(&space) == 1u && !holds(&space, atom_symbol(&a, "done")));
        if (ia_head(second, "ia:need", 3u)) {
            Atom *last = resume(&a, second->expr.elems[1], atom_unit(&a));
            CHECK(fired(last));
            CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "done")));
            CHECK(!holds(&space, atom_symbol(&a, "pending")));
            CHECK(owner && owner->retired && !owner->waiting &&
                arena_accounted_live_bytes(&owner->arena) == 0u);
            CHECK(owner && !owner->jobs && !owner->ctxs && !owner->alts &&
                !owner->all_seen.items && !owner->firing && !owner->let_job.expr);
            CHECK(failed(resume(&a, second->expr.elems[1], atom_unit(&a))));
        }
    }
    space_free(&space); arena_free(&a);
}

static void malformed_reply_and_cancellation(bool cancel) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    uint64_t revision = space_revision(&space);
    Atom *need = start(&a, &space, computed(&a, "x"));
    CHECK(ia_head(need, "ia:need", 3u));
    if (ia_head(need, "ia:need", 3u)) {
        g_cancel = cancel;
        Atom *result = resume(&a, need->expr.elems[1], cancel ? atom_unit(&a) : atom_symbol(&a, "not-a-bag"));
        g_cancel = false;
        CHECK(cancel ? result == NULL : failed(result));
        CHECK(space_revision(&space) == revision && space_length64(&space) == 1u);
        CHECK(holds(&space, atom_symbol(&a, "pending")) && !holds(&space, atom_symbol(&a, "done")));
        CHECK(failed(resume(&a, need->expr.elems[1], atom_unit(&a))));
    }
    space_free(&space); arena_free(&a);
}

static void changed_dependency(bool removal, bool negative) {
    Arena a; arena_init_detached(&a);
    Space space, dependency; space_init(&space); space_init(&dependency); pending(&space, &a);
    Atom *fact = atom_symbol(&a, "fact");
    if (!negative) space_add(&dependency, fact);
    Atom *lookup = atom_expr3(&a, atom_symbol(&a, negative ? "absent" : "read"),
        atom_space(&a, &dependency), fact);
    Atom *parts[2] = {lookup, computed(&a, "x")};
    Atom *need = start(&a, &space, conjunction(&a, parts, 2u));
    CHECK(ia_head(need, "ia:need", 3u));
    if (ia_head(need, "ia:need", 3u)) {
        if (removal) space_remove(&dependency, fact); else space_add(&dependency, fact);
        uint64_t changed_revision = space_revision(&dependency);
        Atom *result = resume(&a, need->expr.elems[1], atom_unit(&a));
        CHECK(finished(result, "ia:none"));
        CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "pending")));
        CHECK(space_revision(&dependency) == changed_revision);
        CHECK(space_length64(&dependency) == (removal ? 0u : negative ? 1u : 2u));
    }
    space_free(&space); space_free(&dependency); arena_free(&a);
}

static void unread_arrival(bool arrive) {
    Arena a; arena_init_detached(&a);
    Space space, dependency; space_init(&space); space_init(&dependency); pending(&space, &a);
    Atom *v = atom_var(&a, "value");
    Atom *parts[2] = {
        atom_expr3(&a, atom_symbol(&a, "let"), v, atom_symbol(&a, "ask")),
        atom_expr3(&a, atom_symbol(&a, "read"), atom_space(&a, &dependency), v)};
    Atom *need = start(&a, &space, conjunction(&a, parts, 2u));
    CHECK(ia_head(need, "ia:need", 3u));
    if (ia_head(need, "ia:need", 3u)) {
        Atom *seven = atom_int(&a, 7);
        if (arrive) space_add(&dependency, seven);
        Atom *result = resume(&a, need->expr.elems[1], atom_expr(&a, &seven, 1u));
        CHECK(arrive ? finished(result, "ia:none") : fired(result));
        CHECK(holds(&space, atom_symbol(&a, arrive ? "pending" : "done")));
        CHECK(space_length64(&dependency) == (arrive ? 1u : 0u));
    }
    space_free(&space); space_free(&dependency); arena_free(&a);
}

static void unmoved_after_effects(IaCommitRun *commit) {
    for (size_t i = 0; i < commit->written_len; i++)
        commit->written[i].space->revision = commit->written[i].revision;
}

static void post_effect_failure(void) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    Atom *need = start(&a, &space, computed(&a, "x"));
    CHECK(ia_head(need, "ia:need", 3u));
    if (ia_head(need, "ia:need", 3u)) {
        g_ia_commit_after_writes = unmoved_after_effects;
        Atom *result = resume(&a, need->expr.elems[1], atom_unit(&a));
        g_ia_commit_after_writes = NULL;
        CHECK(failed(result) && !finished(result, "ia:none"));
        CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "done")));
        CHECK(!holds(&space, atom_symbol(&a, "pending")));
        if (failed(result)) {
            Atom *reason = result->expr.elems[1]->expr.elems[2];
            CHECK(ia_head(reason, "refused", 2u) && ia_head(reason->expr.elems[1], "commit-failed", 2u));
        }
    }
    space_free(&space); arena_free(&a);
}

typedef struct { unsigned calls; bool error; Space *effects; Space *reads; } Host;

static bool evaluate_empty(void *raw, Arena *a, Atom *expression,
    Atom ***values, size_t *len, Atom **error) {
    Host *host = raw;
    host->calls++;
    CHECK(atom_is_symbol(expression, "ask"));
    *values = NULL; *len = 0u;
    if (host->reads) CHECK(eval_observe_space_read(host->reads) == host->reads);
    if (host->effects) space_add(host->effects, atom_symbol(a, "effect"));
    if (host->error) *error = atom_error(a, expression, atom_symbol(a, "provider-failed"));
    return !host->error;
}

static void common_host_lane(CettaInteractionLane lane, bool error) {
    Arena a; arena_init_detached(&a);
    Space space, effects; space_init(&space); space_init(&effects); pending(&space, &a);
    Host context = {.error = error, .effects = &effects};
    CettaInteractionHost host = {.lane = lane, .context = &context, .evaluate = evaluate_empty};
    Atom *parts[2] = {form(&a, "take", atom_symbol(&a, "pending")),
        form(&a, "absent", computed(&a, "x"))};
    Atom *loc = atom_space(&a, &space), *continuation = NULL;
    CettaInteractionOutcome outcome = cetta_interaction_fire(&host, &a, loc,
        conjunction(&a, parts, 2u), atom_expr3(&a, atom_symbol(&a, "add-atom"), loc,
        atom_symbol(&a, "done")), NULL, &continuation);
    CHECK(context.calls == 1u && space_length64(&effects) == 1u);
    CHECK(outcome == (error ? CETTA_INTERACTION_ERROR : CETTA_INTERACTION_FIRED));
    CHECK(holds(&space, atom_symbol(&a, error ? "pending" : "done")));
    CHECK(space_length64(&space) == 1u);
    if (error) CHECK(ia_head(continuation, "Error", 3u));
    space_free(&space); space_free(&effects); arena_free(&a);
}

static void allocation_refusal(bool calloc_fault, size_t allowance) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    uint64_t revision = space_revision(&space);
    Atom *need = start(&a, &space, computed(&a, "x"));
    CHECK(ia_head(need, "ia:need", 3u));
    if (ia_head(need, "ia:need", 3u)) {
        if (calloc_fault) g_calloc_fail = allowance; else g_realloc_fail = allowance;
        Atom *result = resume(&a, need->expr.elems[1], atom_unit(&a));
        g_calloc_fail = g_realloc_fail = SIZE_MAX;
        if (fired(result)) {
            CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "done")));
            CHECK(space_revision(&space) != revision);
        } else {
            CHECK(result == NULL || failed(result));
            CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "pending")));
            CHECK(space_revision(&space) == revision);
        }
    }
    space_free(&space); arena_free(&a);
}

static void incomplete_matching(bool computation) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    Atom *result;
    if (computation) {
        Atom *need = start(&a, &space, computed(&a, "x"));
        CHECK(ia_head(need, "ia:need", 3u));
        Atom *one = atom_int(&a, 7);
        g_incomplete_match = true;
        result = ia_head(need, "ia:need", 3u)
            ? resume(&a, need->expr.elems[1], atom_expr(&a, &one, 1u)) : NULL;
    } else {
        g_incomplete_scan_only = true;
        g_incomplete_match = true;
        result = start(&a, &space, form(&a, "read", atom_symbol(&a, "missing")));
    }
    g_incomplete_match = false;
    g_incomplete_scan_only = false;
    CHECK(failed(result) && !finished(result, "ia:none") && !fired(result));
    CHECK(space_length64(&space) == 1u && holds(&space, atom_symbol(&a, "pending")));
    space_free(&space); arena_free(&a);
}

static void discovery_allocation_refusal(bool calloc_fault, size_t allowance) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    uint64_t revision = space_revision(&space);
    Atom *query = form(&a, "absent", form(&a, "every", computed(&a, "x")));
    if (calloc_fault) g_calloc_fail = allowance; else g_realloc_fail = allowance;
    Atom *result = start(&a, &space, query);
    g_calloc_fail = g_realloc_fail = SIZE_MAX;
    CHECK(result == NULL || failed(result) || ia_head(result, "ia:need", 3u));
    CHECK(space_revision(&space) == revision && space_length64(&space) == 1u);
    CHECK(holds(&space, atom_symbol(&a, "pending")));
    space_free(&space); arena_free(&a);
}

static void registration_allocation_refusal(void) {
    Arena a; arena_init_detached(&a);
    Space dependency; space_init(&dependency);
    Atom *loc = atom_space(&a, &dependency);
    Atom *read = atom_expr2(&a, loc, atom_int(&a, (int64_t)space_revision(&dependency)));
    Atom *seen = atom_expr(&a, &read, 1u);
    Atom *parts[4] = {atom_symbol(&a, "transform"), loc, atom_unit(&a), atom_unit(&a)};
    Atom *firing = atom_expr(&a, parts, 4u);
    size_t old_len = g_parked_len;
    g_calloc_fail = 0u;
    uint64_t failed_ticket = ia_park(firing, false, seen);
    g_calloc_fail = SIZE_MAX;
    CHECK(failed_ticket == 0u && g_parked_len == old_len);
    uint64_t ticket = ia_park(firing, false, seen);
    CHECK(ticket != 0u);
    IaParked *p = ia_parked_find(ticket);
    CHECK(p && p->seen_len == 1u && !ia_parked_changed(p));
    if (p) {
        IaSeenAt *saved = p->seen;
        g_calloc_fail = 0u;
        bool refreshed = ia_parked_update(ticket, seen, false);
        g_calloc_fail = SIZE_MAX;
        CHECK(!refreshed && p->seen == saved && p->seen_len == 1u);
        CHECK(p->live && p->needs_recheck && ia_parked_changed(p));
        CHECK(ia_parked_update(ticket, seen, false));
        CHECK(p->live && !p->needs_recheck && !ia_parked_changed(p));
        space_add(&dependency, atom_symbol(&a, "arrival"));
        CHECK(ia_parked_changed(p));
        Atom *args[1] = {atom_int(&a, (int64_t)ticket)};
        CettaCallOutcome result;
        CHECK(op_agenda_drop(&a, args, 1u, &result) && atom_eq(result.term, atom_true(&a)));
        CHECK(op_agenda_drop(&a, args, 1u, &result) && atom_eq(result.term, atom_false(&a)));
    }
    space_free(&dependency); arena_free(&a);
}

static void cancellation_before_publication(void) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    uint64_t revision = space_revision(&space);
    Atom *loc = atom_space(&a, &space);
    Atom *parts[4] = {atom_symbol(&a, "transform"), loc,
        form(&a, "take", atom_symbol(&a, "pending")),
        atom_expr3(&a, atom_symbol(&a, "add-atom"), loc, atom_symbol(&a, "done"))};
    g_cancel_after_match = true;
    Atom *result = ia_machine_start(IA_MODE_NOW, atom_expr(&a, parts, 4u), &a);
    g_cancel_after_match = g_cancel = false;
    CHECK(result == NULL);
    CHECK(space_revision(&space) == revision && space_length64(&space) == 1u);
    CHECK(holds(&space, atom_symbol(&a, "pending")));
    space_free(&space); arena_free(&a);
}

static void nested_read_observers(bool capacity_failure) {
    Space first, second; space_init(&first); space_init(&second);
    IaMachine *outer = ia_machine_new(IA_MODE_NOW), *inner = ia_machine_new(IA_MODE_NOW);
    CHECK(outer && inner);
    if (outer && inner) {
        CettaEvalReadScope a = {.observe = ia_machine_observe_read, .context = outer};
        CettaEvalReadScope b = {.observe = ia_machine_observe_read, .context = inner};
        eval_read_scope_enter(&a); eval_read_scope_enter(&b);
        if (capacity_failure) g_realloc_fail = 0u;
        CHECK(eval_observe_space_read(&first) == &first);
        g_realloc_fail = SIZE_MAX;
        CHECK(outer->seen.len == 1u);
        CHECK(inner->seen.len == (capacity_failure ? 0u : 1u));
        CHECK(b.incomplete == capacity_failure && !a.incomplete);
        CHECK((inner->refused != NULL) == capacity_failure);
        eval_read_scope_leave(&b);
        CHECK(eval_observe_space_read(&second) == &second);
        CHECK(outer->seen.len == 2u);
        CHECK(inner->seen.len == (capacity_failure ? 0u : 1u));
        eval_read_scope_leave(&a);
        CHECK(eval_observe_space_read(&first) == &first);
        CHECK(outer->seen.len == 2u);
    }
    ia_machine_release(inner); ia_machine_release(outer);
    space_free(&second); space_free(&first);
}

static void observed_host_dependency(CettaInteractionLane lane, bool changed) {
    Arena a; arena_init_detached(&a);
    Space work, dependency; space_init(&work); space_init(&dependency); pending(&work, &a);
    Host context = {.effects = changed ? &dependency : NULL, .reads = &dependency};
    CettaInteractionHost host = {.lane = lane, .context = &context, .evaluate = evaluate_empty};
    Atom *parts[2] = {form(&a, "take", atom_symbol(&a, "pending")),
        form(&a, "absent", computed(&a, "x"))};
    Atom *loc = atom_space(&a, &work), *continuation = NULL;
    CettaInteractionOutcome result = cetta_interaction_fire(&host, &a, loc,
        conjunction(&a, parts, 2u), atom_expr3(&a, atom_symbol(&a, "add-atom"), loc,
        atom_symbol(&a, "done")), NULL, &continuation);
    CHECK(context.calls == 1u);
    CHECK(result == (changed ? CETTA_INTERACTION_NONE : CETTA_INTERACTION_FIRED));
    CHECK(space_length64(&work) == 1u);
    CHECK(holds(&work, atom_symbol(&a, changed ? "pending" : "done")));
    CHECK(space_length64(&dependency) == (changed ? 1u : 0u));
    space_free(&dependency); space_free(&work); arena_free(&a);
}

static void retained_computed_tokens(void) {
    Arena a; arena_init_detached(&a);
    Space dependency; space_init(&dependency);
    IaMachine *m = ia_machine_new(IA_MODE_NOW);
    CHECK(m && ia_machine_observe_read(m, space_read_token(&dependency)));
    if (m) {
        CHECK(ia_machine_observe_read(m, space_read_token(&dependency)));
        CHECK(m->seen.len == 1u);
        Atom *seen = ia_list_atom(&a, &m->seen);
        IaCommitRun commit = {.a = &a, .seen = seen};
        CHECK(ia_commit_seen_reason(&commit) == NULL);
        Atom *firing_parts[4] = {atom_symbol(&a, "transform"), atom_space(&a, &dependency),
            atom_unit(&a), atom_unit(&a)};
        uint64_t ticket = ia_park(atom_expr(&a, firing_parts, 4u), false, seen);
        IaParked *p = ia_parked_find(ticket);
        CHECK(p && !ia_parked_changed(p));
        space_add(&dependency, atom_symbol(&a, "arrival"));
        CHECK(ia_commit_seen_reason(&commit) != NULL);
        CHECK(p && ia_parked_changed(p));
        CHECK(ia_machine_observe_read(m, space_read_token(&dependency)));
        CHECK(m->seen.len == 2u);
        commit.seen = ia_list_atom(&a, &m->seen);
        CHECK(ia_commit_seen_reason(&commit) != NULL);
        CHECK(ia_parked_update(ticket, NULL, true));
    }
    ia_machine_release(m); space_free(&dependency); arena_free(&a);
}

static void observed_overlay_rewrite(void) {
    Arena a; arena_init_detached(&a);
    Space base, overlay; space_init(&base);
    Atom *marker = atom_symbol(&a, "marker"); space_add(&base, marker);
    space_init_overlay(&overlay, &base);
    SpaceReadToken read = space_read_token(&overlay);
    Atom *entry = ia_captured_read(&a, read), *seen = atom_expr(&a, &entry, 1u);
    IaCommitRun commit = {.a = &a, .seen = seen};
    CHECK(ia_commit_seen_reason(&commit) == NULL);
    CHECK(space_remove(&base, marker));
    CHECK(space_revision(&overlay) == read.revision);
    CHECK(!space_read_token_is_current(read));
    CHECK(ia_commit_seen_reason(&commit) != NULL);
    Atom *malformed = form(&a, "ia:captured-read", atom_space(&a, &overlay));
    commit.seen = atom_expr(&a, &malformed, 1u);
    CHECK(atom_is_symbol(ia_commit_seen_reason(&commit), "malformed-seen"));
    IaSeenAt *parsed = NULL; size_t count = 0u;
    CHECK(!ia_seen_at(commit.seen, &parsed, &count));
    CHECK(parsed == NULL && count == 0u);
    space_free(&overlay); space_free(&base); arena_free(&a);
}

static void in_flight_request_refuses_reentry(void) {
    Arena a; arena_init_detached(&a);
    Space space; space_init(&space); pending(&space, &a);
    Atom *need = start(&a, &space, computed(&a, "x"));
    CHECK(ia_head(need, "ia:need", 3u));
    if (ia_head(need, "ia:need", 3u)) {
        IaResumeToken *token = ia_resume_token_of(need->expr.elems[1]);
        CHECK(token != NULL);
        if (token) {
            token->machine->computing = true;
            CHECK(failed(resume(&a, need->expr.elems[1], atom_unit(&a))));
            Atom *args[1] = {need->expr.elems[1]}; CettaCallOutcome result = {0};
            CHECK(op_compute(&space, &a, args, 1u, &result) && failed(result.term));
            CHECK(holds(&space, atom_symbol(&a, "pending")));
            token->machine->computing = false;
            CHECK(fired(resume(&a, need->expr.elems[1], atom_unit(&a))));
            CHECK(op_compute(&space, &a, args, 1u, &result) && failed(result.term));
        }
    }
    space_free(&space); arena_free(&a);
}

static void computed_frontier_capacity(bool capacity_failure) {
    Arena a; arena_init_detached(&a);
    Space program; space_init(&program);
    IaMachine *m = ia_machine_new(IA_MODE_NOW);
    CettaEvalReadScope scope = {.observe = ia_machine_observe_read, .context = m};
    Atom **values = NULL, *error = NULL; size_t len = 0u;
    g_frontier_fail = capacity_failure; g_frontier_failed = false;
    eval_read_scope_enter(&scope);
    bool complete = eval_interaction_values(&program, &a, atom_int(&a, 7),
        &values, &len, &error);
    eval_read_scope_leave(&scope);
    g_frontier_fail = false;
    CHECK(complete != capacity_failure);
    CHECK(g_frontier_failed == capacity_failure);
    CHECK(m && m->seen.len == 0u); /* the literal needs no mutable read */
    if (capacity_failure) {
        CHECK(values == NULL && len == 0u);
        CHECK(ia_head(error, "Error", 3u));
        CHECK(error && atom_is_symbol(error->expr.elems[2], "TransformLetCapacity"));
    } else {
        CHECK(error == NULL && len == 1u);
        CHECK(values && atom_eq(values[0], atom_int(&a, 7)));
    }
    free(values); ia_machine_release(m);
    space_free(&program); arena_free(&a);
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    VarInternTable variables; var_intern_init(&variables); g_var_intern = &variables;
    g_hashcons = NULL;
    request_ownership();
    malformed_reply_and_cancellation(false);
    malformed_reply_and_cancellation(true);
    changed_dependency(false, false);
    changed_dependency(true, false);
    changed_dependency(false, true);
    unread_arrival(false);
    unread_arrival(true);
    post_effect_failure();
    common_host_lane(CETTA_INTERACTION_LANE_PRIME, false);
    common_host_lane(CETTA_INTERACTION_LANE_PRIME, true);
    common_host_lane(CETTA_INTERACTION_LANE_PETTA, false);
    common_host_lane(CETTA_INTERACTION_LANE_PETTA, true);
    incomplete_matching(false);
    incomplete_matching(true);
    registration_allocation_refusal();
    cancellation_before_publication();
    nested_read_observers(false);
    nested_read_observers(true);
    observed_host_dependency(CETTA_INTERACTION_LANE_PRIME, false);
    observed_host_dependency(CETTA_INTERACTION_LANE_PRIME, true);
    observed_host_dependency(CETTA_INTERACTION_LANE_PETTA, false);
    observed_host_dependency(CETTA_INTERACTION_LANE_PETTA, true);
    retained_computed_tokens();
    observed_overlay_rewrite();
    in_flight_request_refuses_reentry();
    computed_frontier_capacity(false);
    computed_frontier_capacity(true);
    for (size_t i = 0; i < 12u; i++) {
        allocation_refusal(true, i);
        allocation_refusal(false, i);
    }
    for (size_t i = 0; i < 24u; i++) {
        discovery_allocation_refusal(true, i);
        discovery_allocation_refusal(false, i);
    }
    printf("Interaction queries: %u checks, %u failures\n", g_checks, g_failures);
    var_intern_free(&variables); g_var_intern = NULL;
    symbol_table_free(&symbols); g_symbols = NULL;
    return g_failures ? 1 : 0;
}
