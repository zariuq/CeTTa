#include "../../src/embedding.c"
#include "transform_core.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static bool copying;
static bool packet_failure, packet_refused;
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

typedef struct { Space *space; size_t removals; } Store;

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

static Atom *unused_add(void *context, Atom *reference, Atom *atom) {
    (void)context; (void)reference; (void)atom;
    assert(false);
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

static size_t trial(size_t failed_copy, bool fail_packet) {
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
    Atom *call = parse_one(&input,
        "(tf:fire &self ((take (job left)) (take (job right)))"
        " (, (done left) (done right)))");
    Store store = {&engine->session.space, 0u};
    CettaTransformHost host = {.context = &store,
        .dialect = CETTA_TRANSFORM_DIALECT_PETTA, .resolve_space = resolve,
        .remove_atom = remove_one, .take_atom = take, .add_atom = unused_add,
        .evaluate = unused_eval};
    calls = made = freed = 0u;
    fail_at = failed_copy;
    packet_failure = fail_packet;
    packet_refused = false;
    copying = true;
    Atom *answer = NULL;
    CettaTransformOutcome outcome = cetta_transform_fire(store.space,
        &output, call, &host, &answer);
    copying = false;
    size_t observed = calls;
    assert(made == freed);
    if (failed_copy || fail_packet) {
        if (failed_copy)
            assert(observed == failed_copy);
        assert(packet_refused == fail_packet);
        assert(outcome == CETTA_TRANSFORM_ERROR || outcome == CETTA_TRANSFORM_DECLINED);
        assert(store.removals == 0u && space_length64(store.space) == 2u);
        assert(atom_eq(space_get_at64(store.space, 0u), left));
        assert(atom_eq(space_get_at64(store.space, 1u), right));
    } else {
        assert(outcome == CETTA_TRANSFORM_FIRED && answer);
        assert(store.removals == 2u && space_length64(store.space) == 0u);
        assert(atom_eq(answer, parse_one(&input,
            "(tf:fired ((, (done left) (done right))))")));
        assert(observed > 0u && made > 0u);
    }
    arena_free(&output);
    arena_free(&input);
    engine_leave(engine, &scope);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    return observed;
}

int main(void) {
    size_t roots = trial(0u, false);
    for (size_t index = 1u; index <= roots; index++)
        trial(index, false);
    trial(0u, true);
    printf("PASS: %zu transform copy failures keep both input occurrences and release every copy session\n", roots);
    puts("PASS: a refused result packet leaves both input occurrences unconsumed");
    return 0;
}
