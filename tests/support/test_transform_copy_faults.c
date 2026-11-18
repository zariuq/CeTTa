#include "../../src/embedding.c"
#include "transform_core.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static bool copying;
static bool packet_failure, packet_refused;
static bool count_failure, count_refused;
static size_t calls, fail_at, made, freed;

Atom *__real_atom_deep_copy_session_copy(AtomDeepCopySession *, Atom *);
Atom *__wrap_atom_deep_copy_session_copy(AtomDeepCopySession *session, Atom *source) {
    if (copying && ++calls == fail_at)
        return NULL;
    return __real_atom_deep_copy_session_copy(session, source);
}

AtomDeepCopySession *__real_atom_deep_copy_session_new(Arena *);
AtomDeepCopySession *__wrap_atom_deep_copy_session_new(Arena *arena) {
    AtomDeepCopySession *session = __real_atom_deep_copy_session_new(arena);
    if (copying && session)
        made++;
    return session;
}

void __real_atom_deep_copy_session_free(AtomDeepCopySession *);
void __wrap_atom_deep_copy_session_free(AtomDeepCopySession *session) {
    if (copying && session)
        freed++;
    __real_atom_deep_copy_session_free(session);
}

Atom *__real_atom_expr(Arena *, Atom **, CettaExprLen);
Atom *__wrap_atom_expr(Arena *arena, Atom **elements, CettaExprLen count) {
    if (copying && packet_failure && count == 2u &&
        atom_is_symbol(elements[0], "tf:fired")) {
        packet_refused = true;
        return NULL;
    }
    return __real_atom_expr(arena, elements, count);
}

Atom *__real_atom_int(Arena *, int64_t);
Atom *__wrap_atom_int(Arena *arena, int64_t value) {
    if (copying && count_failure && value == 2) {
        count_refused = true;
        return NULL;
    }
    return __real_atom_int(arena, value);
}

typedef struct {
    Space *space;
    Atom *external_write;
    size_t removals, additions, decisions;
} Store;

static bool resolve(void *context, Atom *reference, Space **space) {
    (void)reference;
    *space = ((Store *)context)->space;
    return true;
}

static Atom *take(void *context, Atom *reference, Atom *atom, size_t count) {
    (void)reference;
    Store *store = context;
    for (size_t i = 0u; i < count; i++) {
        assert(space_remove(store->space, atom));
        store->removals++;
    }
    return NULL;
}

static Atom *remove_one(void *context, Atom *reference, Atom *atom) {
    return take(context, reference, atom, 1u);
}

static Atom *add(void *context, Atom *reference, Atom *atom) {
    (void)reference;
    Store *store = context;
    space_add(store->space, atom);
    store->additions++;
    return NULL;
}

static bool unused_eval(void *context, Arena *arena, Atom *expression,
                        Atom ***results, size_t *count, Atom **error) {
    (void)context; (void)arena; (void)expression;
    (void)results; (void)count; (void)error;
    assert(false);
    return false;
}

static Atom *parse_one(Arena *arena, const char *text) {
    Atom **forms = NULL;
    int count = parse_metta_text(text, arena, &forms);
    assert(count == 1 && forms);
    Atom *atom = forms[0];
    free(forms);
    return atom;
}

typedef enum { RETURN_PACKET, INTO_SPACE, REFUSE_COUNT } Publication;

static size_t trial(size_t failed_copy, bool fail_packet, Publication publication) {
    cetta_engine_t *engine = NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
        "petta", NULL, NULL};
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    Arena input, output;
    arena_init_detached(&input);
    arena_init_detached(&output);
    Atom *left = parse_one(&engine->persistent, "(job left)");
    Atom *right = parse_one(&engine->persistent, "(job right)");
    space_add(&engine->session.space, left);
    space_add(&engine->session.space, right);
    Atom *call = parse_one(&input, publication == RETURN_PACKET
        ? "(tf:fire &self ((take (job left)) (take (job right)))"
          " (, (done left) (done right)))"
        : "(tf:fire-into &self ((all (take (job $name)))) (done $name) &self)");
    Store store = {.space = &engine->session.space};
    CettaTransformHost host = {.context = &store,
        .dialect = CETTA_TRANSFORM_DIALECT_PETTA, .resolve_space = resolve,
        .remove_atom = remove_one, .take_atom = take, .add_atom = add,
        .evaluate = unused_eval};
    calls = made = freed = 0u;
    fail_at = failed_copy;
    packet_failure = fail_packet;
    packet_refused = false;
    count_failure = publication == REFUSE_COUNT;
    count_refused = false;
    copying = true;
    Atom *answer = NULL;
    CettaTransformOutcome outcome = cetta_transform_fire(store.space,
        &output, call, &host, &answer);
    copying = false;
    size_t observed = calls;
    assert(made == freed);
    if (failed_copy || fail_packet || count_failure) {
        if (failed_copy)
            assert(observed == failed_copy);
        assert(packet_refused == fail_packet);
        assert(count_refused == count_failure);
        assert(outcome == CETTA_TRANSFORM_ERROR || outcome == CETTA_TRANSFORM_DECLINED);
        assert(store.removals == 0u && store.additions == 0u && space_length64(store.space) == 2u);
        assert(atom_eq(space_get_at64(store.space, 0u), left));
        assert(atom_eq(space_get_at64(store.space, 1u), right));
    } else if (publication == RETURN_PACKET) {
        assert(outcome == CETTA_TRANSFORM_FIRED && answer);
        assert(store.removals == 2u && store.additions == 0u && space_length64(store.space) == 0u);
        assert(atom_eq(answer, parse_one(&input,
            "(tf:fired ((, (done left) (done right))))")));
        assert(observed > 0u && made > 0u);
    } else {
        assert(outcome == CETTA_TRANSFORM_FIRED && answer && atom_eq(answer, atom_int(&input, 2)));
        assert(store.removals == 2u && store.additions == 2u && space_length64(store.space) == 2u);
        assert(atom_eq(space_get_at64(store.space, 0u), parse_one(&input, "(done left)")));
        assert(atom_eq(space_get_at64(store.space, 1u), parse_one(&input, "(done right)")));
    }
    arena_free(&output);
    arena_free(&input);
    engine_leave(engine, &scope);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    return observed;
}

static CettaTransformOutcome stale_decision(void *context, Arena *arena,
        const CettaTransformFiring *request, Atom **error) {
    (void)arena;
    Store *store = context;
    assert(request->port_count == 1u && request->ports[0].take);
    assert(space_read_token_matches_live_space(request->ports[0].view, store->space));
    store->decisions++;
    /* Stand in for a writer between selection and publication. */
    space_add(store->space, store->external_write);
    *error = NULL;
    return CETTA_TRANSFORM_FIRED;
}

static void check_stale_decision(void) {
    cetta_engine_t *engine = NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
        "petta", NULL, NULL};
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    Arena request;
    arena_init_detached(&request);
    Store store = {.space = &engine->session.space,
        .external_write = parse_one(&engine->persistent, "(external write)")};
    Atom *job = parse_one(&engine->persistent, "(job left)");
    space_add(store.space, job);
    CettaTransformHost host = {.context = &store,
        .dialect = CETTA_TRANSFORM_DIALECT_PETTA, .resolve_space = resolve,
        .remove_atom = remove_one, .take_atom = take, .add_atom = add,
        .evaluate = unused_eval, .decide_firing = stale_decision};
    Atom *call = parse_one(&request,
        "(tf:fire-into &self ((take (job left))) (done left) &self)");
    Atom *answer = NULL;
    assert(cetta_transform_fire(store.space, &request, call, &host, &answer) ==
           CETTA_TRANSFORM_ERROR);
    assert(answer && atom_is_error(answer));
    assert(atom_is_symbol(answer->expr.elems[2], "TransformReadViewInvalidated"));
    assert(store.decisions == 1u && store.removals == 0u && store.additions == 0u);
    assert(space_length64(store.space) == 2u);
    assert(atom_eq(space_get_at64(store.space, 0u), job));
    assert(atom_eq(space_get_at64(store.space, 1u), store.external_write));
    arena_free(&request);
    engine_leave(engine, &scope);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    puts("PASS: a solver decision at a stale version neither takes nor writes");
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--stale") == 0) {
        check_stale_decision();
        return 0;
    }
    size_t roots = trial(0u, false, RETURN_PACKET);
    for (size_t index = 1u; index <= roots; index++)
        trial(index, false, RETURN_PACKET);
    trial(0u, true, RETURN_PACKET);
    trial(0u, false, INTO_SPACE);
    trial(0u, false, REFUSE_COUNT);
    printf("PASS: %zu transform copy failures keep both input occurrences and release every copy session\n", roots);
    puts("PASS: a refused result packet leaves both input occurrences unconsumed");
    puts("PASS: all into a space publishes exactly once and a refused count leaves it untouched");
    return 0;
}
