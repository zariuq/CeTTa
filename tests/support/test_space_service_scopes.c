/* Native scope ownership, dependency waits and resource-accounting checks.
 * Simulated clients use independent account identities without requiring
 * additional operating-system accounts. The real storage owner is exercised.
 */
#define main cetta_space_service_entry
#include "../../src/space_service_main.c"
#undef main

static unsigned checks, failures;
#define expect(condition, what) do { ++checks; \
    if (!(condition)) { ++failures; fprintf(stderr, "FAIL %s (line %d)\n", what, __LINE__); } } while (0)

static Atom *req(Arena *a, const char *id, const char *operation, Atom **args, size_t count) {
    Atom *parts[16]; parts[0] = atom_string(a, id); parts[1] = atom_symbol(a, operation);
    for (size_t i = 0; i < count; ++i) parts[i + 2] = args[i];
    return form(a, "request", parts, count + 2);
}
static unsigned serial;
static const char *fresh_id(Arena *a) {
    char text[32]; snprintf(text, sizeof(text), "harness-%u", ++serial);
    Atom *id = atom_string(a, text); return id->ground.sval;
}
/* Dispatch as client c. A parked wait keeps its request bytes as c->input,
 * which client_wake decodes when it answers. */
static Atom *call(Service *s, Client *c, Arena *a, Atom *request) {
    Bytes raw = {0};
    if (!wire(request, &raw)) return NULL;
    Atom *answer = dispatch(s, c, a, request, raw);
    if (!answer && c->watching) { bytes_free(&c->input); c->input = raw; return NULL; }
    bytes_free(&raw); return answer;
}
static Atom *wake(Service *s, Client *c, Arena *a) {
    if (!client_wake(s, c) || !c->output.data) return NULL;
    Atom *answer = NULL;
    Bytes b = {c->output.data + 8, c->output.size - 8};
    if (!decode(a, b, &answer)) answer = NULL;
    bytes_free(&c->output); bytes_free(&c->input);
    return answer;
}
static bool ok(Atom *answer) { return shape(answer, "reply", 4) && atom_is_symbol(part(answer, 2), "ok"); }
static bool refused(Atom *answer, const char *reason) {
    return shape(answer, "reply", 4) && atom_is_symbol(part(answer, 2), "error") &&
        atom_is_symbol(part(answer, 3), reason);
}
static bool reports(Atom *answer, const char *head) { return ok(answer) && shape(part(answer, 3), head, 4); }
static Atom *E1(Arena *a, const char *head, Atom *x) { return form(a, head, &x, 1); }
static Atom *E2(Arena *a, const char *head, Atom *x, Atom *y) { Atom *v[] = {x, y}; return form(a, head, v, 2); }

static uint64_t open_store(Service *s, Client *owner, Arena *a, const char *name, const char *engine) {
    Atom *args[] = {atom_string(a, s->uuid), atom_string(a, name), atom_symbol(a, engine),
                    atom_int(a, (int64_t)s->catalog_revision)};
    Atom *answer = call(s, owner, a, req(a, fresh_id(a), "open", args, 4));
    expect(ok(answer), "open store");
    return (uint64_t)part(part(answer, 3), 2)->ground.ival;
}
static void grant(Service *s, Client *owner, Arena *a, uint64_t store, uint32_t uid, unsigned rights) {
    Atom *args[] = {atom_string(a, s->uuid), atom_int(a, (int64_t)store),
                    E1(a, "account", atom_int(a, uid)), rights_atom(a, rights),
                    atom_symbol(a, "none"), atom_symbol(a, "none")};
    expect(ok(call(s, owner, a, req(a, fresh_id(a), "grant", args, 6))), "grant");
}
static void batch(Service *s, Client *c, Arena *a, uint64_t store, Atom *ops) {
    Store *st = find_store(s, store);
    Atom *args[] = {atom_string(a, s->uuid), atom_int(a, (int64_t)store),
                    atom_int(a, (int64_t)st->revision), ops};
    expect(ok(call(s, c, a, req(a, fresh_id(a), "batch", args, 4))), "batch");
}
static Atom *observe(Service *s, Client *c, Arena *a, uint64_t store, Atom *reads, Atom *writes, int64_t limit) {
    Atom *args[] = {atom_string(a, s->uuid), atom_int(a, (int64_t)store), reads, writes,
                    atom_int(a, limit), atom_int(a, 60000)};
    return call(s, c, a, req(a, fresh_id(a), "observe", args, 6));
}
static Atom *handle_id(Atom *observed) { return part(part(observed, 3), 2); }
static Atom *fetch(Service *s, Client *c, Arena *a, Atom *id) {
    Atom *args[] = {atom_string(a, s->boot), id, atom_int(a, 0), atom_int(a, 0), atom_int(a, 16)};
    return call(s, c, a, req(a, fresh_id(a), "observe-fetch", args, 5));
}
static Atom *close_handle(Service *s, Client *c, Arena *a, Atom *id) {
    Atom *args[] = {atom_string(a, s->boot), id};
    return call(s, c, a, req(a, fresh_id(a), "observe-close", args, 2));
}
static Atom *commit_observed(Service *s, Client *c, Arena *a, uint64_t store, Atom *id, Atom *ops) {
    Atom *args[] = {atom_string(a, s->uuid), atom_int(a, (int64_t)store), atom_string(a, s->boot), id, ops};
    return call(s, c, a, req(a, fresh_id(a), "batch-observed", args, 5));
}
static Atom *watch(Service *s, Client *c, Arena *a, uint64_t store, Atom *id, int64_t duration) {
    Atom *args[] = {atom_string(a, s->uuid), atom_int(a, (int64_t)store), atom_string(a, s->boot), id,
                    atom_int(a, duration)};
    return call(s, c, a, req(a, fresh_id(a), "observe-watch", args, 5));
}

int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    char directory[] = "runtime/scope-accounts-XXXXXX";
    if (!mkdtemp(directory)) return 2;
    Service *s = calloc(1, sizeof(*s));
    *s = (Service){.journal = -1, .directory = -1, .lock = -1, .max_facts = 1000000,
        .max_live_bytes = 64 * 1024 * 1024, .max_query_bytes = QUERY_BYTES,
        .max_snapshot_bytes = SNAPSHOT_BYTES, .query_ms = 5000, .next_cursor = 1,
        .max_watchers = CLIENTS / 2, .process_uid = (uint32_t)geteuid(), .owner_uid = (uint32_t)geteuid()};
    if (!directory_open(s, directory) || !recover(s) || !random_id(s->boot)) return 2;
    static Client clients[CLIENTS];
    for (size_t i = 0; i < CLIENTS; ++i) clients[i] = (Client){.fd = -1};
    s->clients = clients;
    Client *owner = &clients[0], *alice = &clients[1], *bob = &clients[2];
    *owner = (Client){.fd = -1, .uid = s->process_uid, .owner = true};
    *alice = (Client){.fd = -1, .uid = s->process_uid + 1};
    *bob = (Client){.fd = -1, .uid = s->process_uid + 2};
    Arena arena; arena_init_detached(&arena); Arena *a = &arena;
    Atom *x = atom_var(a, "x");

    /* T1: handles are account-bound for every operation. */
    uint64_t ordered = open_store(s, owner, a, "ordered", "native-ordered");
    grant(s, owner, a, ordered, alice->uid, RIGHT_READ | RIGHT_WRITE);
    grant(s, owner, a, ordered, bob->uid, RIGHT_READ | RIGHT_WRITE);
    Atom *message = E2(a, "Message", atom_symbol(a, "a"), x);
    Atom *observed = observe(s, alice, a, ordered, E1(a, "reads", message), E1(a, "writes", message), 16);
    expect(ok(observed), "T1 alice observes");
    Atom *id = handle_id(observed);
    Atom *add_one = E1(a, "ops", E1(a, "add", E2(a, "Message", atom_symbol(a, "a"), atom_int(a, 1))));
    expect(refused(fetch(s, bob, a, id), "expired-or-closed"), "T1 bob cannot fetch alice's view");
    expect(refused(close_handle(s, bob, a, id), "expired-or-closed"), "T1 bob cannot close alice's handle");
    expect(refused(commit_observed(s, bob, a, ordered, id, add_one), "expired-or-closed"),
           "T1 bob cannot commit with alice's handle");
    expect(refused(watch(s, bob, a, ordered, id, 1000), "expired-or-closed"), "T1 bob cannot watch alice's handle");
    expect(!bob->watching, "T1 bob holds no wait");
    expect(ok(fetch(s, alice, a, id)), "T1 alice's handle survives the attempts");
    expect(ok(commit_observed(s, alice, a, ordered, id, add_one)), "T1 alice commits");

    /* T2: a parked scoped wait needs READ, not merely OBSERVE. */
    observed = observe(s, alice, a, ordered, E1(a, "reads", message), form(a, "writes", NULL, 0), 16);
    expect(ok(observed), "T2 observe");
    expect(!watch(s, alice, a, ordered, handle_id(observed), 60000) && alice->watching, "T2 wait parked");
    grant(s, owner, a, ordered, alice->uid, RIGHT_OBSERVE);
    expect(refused(wake(s, alice, a), "missing-space"), "T2 losing READ ends the scoped wait");
    expect(!alice->watching && !alice->watched.id, "T2 wait and captured views released");
    grant(s, owner, a, ordered, alice->uid, RIGHT_READ | RIGHT_WRITE);

    /* T3: a wait on a write scope wakes when that scope changes. */
    Atom *claim = E1(a, "Claim", x);
    observed = observe(s, alice, a, ordered, form(a, "reads", NULL, 0), E1(a, "writes", claim), 16);
    expect(ok(observed), "T3 observe");
    expect(!watch(s, alice, a, ordered, handle_id(observed), 60000) && alice->watching, "T3 wait parked");
    batch(s, owner, a, ordered, E1(a, "ops", E1(a, "add", E1(a, "Claim", atom_int(a, 1)))));
    expect(reports(wake(s, alice, a), "changed"), "T3 write-scope change wakes the wait");

    /* T4: unordered swap removal reorders a view; the wait reports it. */
    uint64_t bag = open_store(s, owner, a, "bag", "native");
    grant(s, owner, a, bag, alice->uid, RIGHT_READ | RIGHT_WRITE);
    Atom *rows[] = {E1(a, "add", E2(a, "b", atom_int(a, 1), atom_int(a, 0))),
                    E1(a, "add", E2(a, "a", atom_int(a, 1), atom_int(a, 0))),
                    E1(a, "add", E2(a, "a", atom_int(a, 2), atom_int(a, 0)))};
    batch(s, owner, a, bag, form(a, "ops", rows, 3));
    Atom *channel = E2(a, "a", x, atom_int(a, 0));
    observed = observe(s, alice, a, bag, E1(a, "reads", channel), form(a, "writes", NULL, 0), 16);
    expect(ok(observed), "T4 observe");
    expect(!watch(s, alice, a, bag, handle_id(observed), 60000) && alice->watching, "T4 wait parked");
    batch(s, owner, a, bag, E1(a, "ops", E1(a, "remove", E2(a, "b", atom_int(a, 1), atom_int(a, 0)))));
    expect(reports(wake(s, alice, a), "changed"), "T4 reordering removal wakes the wait");

    /* T5: the per-scope pointer vector counts against the allowance. */
    uint64_t saved = s->max_snapshot_bytes;
    s->max_snapshot_bytes = 65536u * sizeof(Bytes);
    observed = observe(s, alice, a, ordered, E1(a, "reads", message), form(a, "writes", NULL, 0), 65536);
    expect(refused(observed, "observation-byte-limit"), "T5 65536-row vector plus values exceeds its pointer-only allowance");
    if (ok(observed)) close_handle(s, alice, a, handle_id(observed));
    /* The retained vector stays charged: two 40000-row vectors do not fit into 65536 pointer slots. */
    Atom *first = observe(s, alice, a, ordered, E1(a, "reads", message), form(a, "writes", NULL, 0), 40000);
    expect(ok(first), "T5 one 40000-row vector fits");
    Atom *second = observe(s, alice, a, ordered, E1(a, "reads", message), form(a, "writes", NULL, 0), 40000);
    expect(refused(second, "observation-byte-limit"), "T5 a retained vector still counts");
    if (ok(second)) close_handle(s, alice, a, handle_id(second));
    if (ok(first)) close_handle(s, alice, a, handle_id(first));
    s->max_snapshot_bytes = saved;

    /* T7 (contract): a refused registration does not consume the observation. */
    observed = observe(s, alice, a, ordered, E1(a, "reads", message), form(a, "writes", NULL, 0), 16);
    size_t watchers = s->max_watchers; s->max_watchers = 0;
    expect(refused(watch(s, alice, a, ordered, handle_id(observed), 1000), "watch-capacity"), "T7 capacity refusal");
    expect(ok(fetch(s, alice, a, handle_id(observed))), "T7 observation still alive after watch-capacity");
    s->max_watchers = watchers;
    expect(ok(close_handle(s, alice, a, handle_id(observed))), "T7 helper must close it explicitly");

    /* T9: a dirty wait whose views are unchanged stays parked. */
    observed = observe(s, alice, a, ordered, E1(a, "reads", message), form(a, "writes", NULL, 0), 16);
    expect(!watch(s, alice, a, ordered, handle_id(observed), 60000) && alice->watching, "T9 wait parked");
    Atom *nine = E2(a, "Message", atom_symbol(a, "a"), atom_int(a, 9));
    Atom *round_trip[] = {E1(a, "add", nine), E1(a, "remove", nine)};
    batch(s, owner, a, ordered, form(a, "ops", round_trip, 2));
    expect(!wake(s, alice, a) && alice->watching, "T9 unchanged views keep the wait parked");
    client_close(s, alice); *alice = (Client){.fd = -1, .uid = s->process_uid + 1};

    /* T10: an expired lease cannot authorize a commit, even before expire() runs. */
    observed = observe(s, alice, a, ordered, form(a, "reads", NULL, 0), E1(a, "writes", message), 16);
    Observation *lease = find_observation(s, (uint64_t)handle_id(observed)->ground.ival);
    expect(lease != NULL, "T10 observation present");
    if (lease) lease->expires = now_ms() - 1;
    expect(refused(commit_observed(s, alice, a, ordered, handle_id(observed), add_one), "expired-or-closed"),
           "T10 expired observation refused at commit");

    /* T11: a write scope alone carries the dependency (no read scope). */
    observed = observe(s, alice, a, ordered, form(a, "reads", NULL, 0), E1(a, "writes", claim), 16);
    batch(s, owner, a, ordered, E1(a, "ops", E1(a, "add", E1(a, "Claim", atom_int(a, 5)))));
    expect(refused(commit_observed(s, alice, a, ordered, handle_id(observed),
                   E1(a, "ops", E1(a, "add", E1(a, "Claim", atom_int(a, 6))))), "scope-conflict"),
           "T11 a changed write scope conflicts");

    /* T12: a read scope does not authorize a write. */
    Atom *fact = E1(a, "Fact", x);
    observed = observe(s, alice, a, ordered, E1(a, "reads", fact), E1(a, "writes", claim), 16);
    expect(refused(commit_observed(s, alice, a, ordered, handle_id(observed),
                   E1(a, "ops", E1(a, "add", E1(a, "Fact", atom_int(a, 1))))), "undeclared-write"),
           "T12 read scopes do not cover writes");

    /* T6: everything released. */
    expect(!s->observation_bytes && !s->snapshot_bytes, "T6 observation and snapshot bytes return to zero");

    /* T8: a member's scoped receipt is recovered under the member's account. */
    observed = observe(s, alice, a, ordered, form(a, "reads", NULL, 0), E1(a, "writes", message), 16);
    Atom *args[] = {atom_string(a, s->uuid), atom_int(a, (int64_t)ordered), atom_string(a, s->boot),
                    handle_id(observed), add_one};
    expect(ok(call(s, alice, a, req(a, "alice-scoped", "batch-observed", args, 5))), "T8 member scoped commit");
    for (size_t i = 0; i < CLIENTS; ++i) client_close(s, &clients[i]);
    for (size_t i = 0; i < STORES; ++i) { store_free(s->stores[i]); s->stores[i] = NULL; }
    for (size_t i = 0; i < SNAPSHOTS; ++i) { snapshot_free(s, &s->snapshots[i]); observation_free(s, &s->observations[i]); }
    for (size_t i = 0; i < s->receipt_count; ++i) receipt_free(&s->receipts[i]);
    close(s->journal); close(s->lock); close(s->directory);
    *s = (Service){.journal = -1, .directory = -1, .lock = -1, .max_facts = 1000000,
        .max_live_bytes = 64 * 1024 * 1024, .max_query_bytes = QUERY_BYTES,
        .max_snapshot_bytes = SNAPSHOT_BYTES, .query_ms = 5000, .next_cursor = 1,
        .max_watchers = CLIENTS / 2, .process_uid = (uint32_t)geteuid(), .owner_uid = (uint32_t)geteuid()};
    expect(directory_open(s, directory) && recover(s) && random_id(s->boot), "T8 recovery");
    s->clients = clients;
    expect(find_receipt(s, (uint32_t)geteuid() + 1, "alice-scoped") != NULL, "T8 receipt recovered for the member");
    expect(find_receipt(s, (uint32_t)geteuid(), "alice-scoped") == NULL, "T8 not attributed to the process account");

    for (size_t i = 0; i < CLIENTS; ++i) client_close(s, &clients[i]);
    for (size_t i = 0; i < STORES; ++i) { store_free(s->stores[i]); s->stores[i] = NULL; }
    for (size_t i = 0; i < SNAPSHOTS; ++i) { snapshot_free(s, &s->snapshots[i]); observation_free(s, &s->observations[i]); }
    for (size_t i = 0; i < s->receipt_count; ++i) receipt_free(&s->receipts[i]);
    close(s->journal); close(s->lock); close(s->directory);
    char path[sizeof(directory) + 20];
    snprintf(path, sizeof(path), "%s/journal.csl", directory); unlink(path);
    snprintf(path, sizeof(path), "%s/writer.lock", directory); unlink(path);
    rmdir(directory);
    arena_free(a); free(s); symbol_table_free(&symbols);
    printf("scope accounts harness: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
