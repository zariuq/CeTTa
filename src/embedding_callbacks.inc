typedef struct {
    atomic_uint references;
    cetta_engine_t *engine; /* The containing atom or active call owns it. */
    cetta_callback_descriptor_t descriptor;
} EmbeddingCallback;

typedef struct {
    EmbeddingCallback *callback;
    EmbeddingOwner *owner;
    cetta_atom_t **arguments;
    size_t arity;
    cetta_atom_t *pending;
    void *host_cursor;
    bool opened;
} EmbeddingCallbackCursor;

static void embedding_callback_retain(void *pointer) {
    EmbeddingCallback *callback = pointer;
    atomic_fetch_add_explicit(&callback->references, 1u, memory_order_relaxed);
}
static void embedding_callback_release(void *pointer) {
    EmbeddingCallback *callback = pointer;
    if (atomic_fetch_sub_explicit(&callback->references, 1u, memory_order_acq_rel) != 1u)
        return;
    if (callback->descriptor.release) {
        atomic_fetch_add_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
        callback->descriptor.release(callback->descriptor.context);
        atomic_fetch_sub_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
    }
    free(callback);
}
static CettaEvalCompletion callback_incomplete(cetta_status_t status) {
    return status == CETTA_OUT_OF_MEMORY ? CETTA_EVAL_INCOMPLETE_CAPACITY
        : status == CETTA_CANCELLED || status == CETTA_ENGINE_CLOSED ? CETTA_EVAL_INCOMPLETE_CANCELLED
        : status == CETTA_FUEL_EXHAUSTED ? CETTA_EVAL_INCOMPLETE_FUEL
        : CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
}
static void embedding_callback_cursor_free(void *pointer) {
    EmbeddingCallbackCursor *cursor = pointer;
    EmbeddingCallback *callback = cursor->callback;
    if (cursor->opened) {
        cursor->opened = false;
        atomic_fetch_add_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
        callback->descriptor.close(cursor->host_cursor, g_embedding_deferred_cleanup == callback->engine ? CETTA_CALLBACK_CLOSE_DEFERRED : CETTA_CALLBACK_CLOSE_DIRECT);
        atomic_fetch_sub_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
    }
    cetta_embed_atom_release(cursor->pending);
    for (size_t i = 0; i < cursor->arity; ++i) cetta_embed_atom_release(cursor->arguments[i]);
    free(cursor->arguments);
    embedding_callback_release(callback);
    owner_release(cursor->owner);
    free(cursor);
}
static CettaOwnedExecutionStep embedding_callback_next(void *pointer, CettaOwnedReturn *returned,
        CettaEvalCompletion *completion) {
    EmbeddingCallbackCursor *cursor = pointer;
    EmbeddingCallback *callback = cursor->callback;
    cetta_embed_atom_release(cursor->pending);
    cursor->pending = NULL;
    cetta_callback_state_t state = CETTA_CALLBACK_COMPLETE;
    atomic_fetch_add_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
    cetta_status_t status = callback->descriptor.next(cursor->host_cursor, &state, &cursor->pending);
    atomic_fetch_sub_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
    if (status != CETTA_OK) {
        *completion = callback_incomplete(status);
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    }
    if (state == CETTA_CALLBACK_COMPLETE || state == CETTA_CALLBACK_PAUSED) {
        if (cursor->pending) goto malformed;
        return state == CETTA_CALLBACK_COMPLETE ? CETTA_OWNED_EXECUTION_COMPLETE : CETTA_OWNED_EXECUTION_PAUSED;
    }
    if ((state != CETTA_CALLBACK_VALUE && state != CETTA_CALLBACK_RAISED) || !cursor->pending ||
        cursor->pending->owner->engine != callback->engine) goto malformed;
    *returned = (CettaOwnedReturn){
        .outcome = state == CETTA_CALLBACK_RAISED ? cetta_call_raised(cursor->pending->value)
            : cetta_call_completed_value(cursor->pending->value),
        .final = state == CETTA_CALLBACK_RAISED || callback->descriptor.kind == CETTA_CALLBACK_SINGLE};
    return CETTA_OWNED_EXECUTION_RETURN;
malformed:
    *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
    return CETTA_OWNED_EXECUTION_INTERRUPTED;
}
static bool embedding_callback_open(void *pointer, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    EmbeddingCallback *callback = pointer;
    CettaExprLen arity = 0;
    if (!cetta_invocation_arity(invocation, &arity) || arity != callback->descriptor.arity) {
        *completion = CETTA_EVAL_INCOMPLETE_HOST_FAILURE;
        return false;
    }
    EmbeddingCallbackCursor *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor) { *completion = CETTA_EVAL_INCOMPLETE_CAPACITY; return false; }
    cursor->callback = callback;
    embedding_callback_retain(callback);
    cursor->owner = owner_new(callback->engine);
    cursor->arguments = arity ? calloc((size_t)arity, sizeof(*cursor->arguments)) : NULL;
    if (!cursor->owner || (arity && !cursor->arguments)) goto capacity;
    for (CettaExprIndex i = 0; i < arity; ++i) {
        Atom *value = binding_value_materialize(&cursor->owner->arena,
            cetta_invocation_argument(invocation, i));
        value = value ? atom_deep_copy(&cursor->owner->arena, value) : NULL;
        if (!value || atom_wrap(cursor->owner, value, &cursor->arguments[i]) != CETTA_OK) goto capacity;
        cursor->arity++;
    }
    atomic_fetch_add_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
    cetta_status_t status = callback->descriptor.open(callback->descriptor.context, callback->engine,
        cursor->arguments, cursor->arity, &cursor->host_cursor);
    atomic_fetch_sub_explicit(&callback->engine->active_callbacks, 1u, memory_order_release);
    cursor->opened = status == CETTA_OK || cursor->host_cursor != NULL;
    if (status != CETTA_OK) {
        *completion = callback_incomplete(status);
        embedding_callback_cursor_free(cursor);
        return false;
    }
    cetta_owned_execution_init(execution, cursor, embedding_callback_next, embedding_callback_cursor_free);
    return true;
capacity:
    *completion = CETTA_EVAL_INCOMPLETE_CAPACITY;
    embedding_callback_cursor_free(cursor);
    return false;
}

cetta_status_t cetta_embed_callback_new(cetta_engine_t *engine,
        const cetta_callback_descriptor_t *descriptor, cetta_atom_t **output) {
    if (!engine || !output || !descriptor || descriptor->size != sizeof(*descriptor) ||
        !descriptor->open || !descriptor->next || !descriptor->close ||
        descriptor->arity >= SIZE_MAX / sizeof(cetta_atom_t *) ||
        (!!descriptor->retain != !!descriptor->release) ||
        (descriptor->kind != CETTA_CALLBACK_SINGLE && descriptor->kind != CETTA_CALLBACK_STREAM))
        return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    EmbeddingScope scope;
    cetta_status_t status = engine_enter(engine, &scope);
    if (status != CETTA_OK) return status;
    EmbeddingOwner *owner = owner_new(engine);
    EmbeddingCallback *callback = calloc(1u, sizeof(*callback));
    if (!owner || !callback) status = CETTA_OUT_OF_MEMORY;
    else {
        atomic_init(&callback->references, 1u);
        callback->engine = engine;
        callback->descriptor = *descriptor;
        if (descriptor->retain) {
            atomic_fetch_add_explicit(&engine->active_callbacks, 1u, memory_order_release);
            descriptor->retain(descriptor->context);
            atomic_fetch_sub_explicit(&engine->active_callbacks, 1u, memory_order_release);
        }
        CettaForeignProviderSpec spec = {.context = callback, .owner_thread = true, .retain = embedding_callback_retain,
            .release = embedding_callback_release, .open = embedding_callback_open};
        Atom *atom = cetta_foreign_provider_atom(&owner->arena, &spec);
        status = atom_wrap(owner, atom, output);
        embedding_callback_release(callback);
        callback = NULL;
    }
    free(callback);
    owner_release(owner);
    engine_leave(engine, &scope);
    return status;
}

cetta_status_t cetta_embed_register_callback(cetta_engine_t *engine, const char *name,
        const cetta_callback_descriptor_t *descriptor, cetta_atom_t **output) {
    if (!name || !*name || !output) return CETTA_INVALID_ARGUMENT;
    *output = NULL;
    cetta_atom_t *callable = NULL;
    cetta_status_t status = cetta_embed_callback_new(engine, descriptor, &callable);
    if (status != CETTA_OK) return status;
    EmbeddingScope scope;
    status = engine_enter(engine, &scope);
    if (status == CETTA_OK) {
        Arena arena;
        arena_init_detached(&arena);
        CettaExprLen arity = descriptor->arity;
        Atom *lhs = atom_expr_builder_begin(&arena, arity + 1u);
        Atom *rhs = atom_expr_builder_begin(&arena, arity + 1u);
        bool ok = lhs && rhs;
        if (ok) {
            lhs->expr.elems[0] = atom_symbol(&arena, name);
            rhs->expr.elems[0] = callable->value;
            ok = lhs->expr.elems[0] != NULL;
        }
        for (CettaExprIndex i = 0; ok && i < arity; ++i) {
            Atom *variable = atom_var_with_id(&arena, "argument", fresh_var_id());
            lhs->expr.elems[i + 1u] = rhs->expr.elems[i + 1u] = variable;
            ok = variable != NULL;
        }
        lhs = ok ? atom_expr_builder_finish(&arena, lhs) : NULL;
        rhs = ok ? atom_expr_builder_finish(&arena, rhs) : NULL;
        Atom *equation = lhs && rhs ? atom_expr(&arena,
            (Atom *[]){atom_symbol(&arena, "="), lhs, rhs}, 3u) : NULL;
        cetta_atom_t *fact = NULL;
        cetta_space_t *space = NULL;
        status = equation ? atom_wrap(callable->owner, atom_deep_copy(&callable->owner->arena, equation), &fact)
                          : CETTA_OUT_OF_MEMORY;
        if (status == CETTA_OK) status = cetta_embed_engine_space(engine, &space);
        if (status == CETTA_OK) status = cetta_embed_space_add_atom(space, fact);
        cetta_embed_space_release(space);
        cetta_embed_atom_release(fact);
        arena_free(&arena);
        engine_leave(engine, &scope);
    }
    if (status == CETTA_OK) *output = callable;
    else cetta_embed_atom_release(callable);
    return status;
}
