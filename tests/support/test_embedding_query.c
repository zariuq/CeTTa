#include "../../src/embedding.c"
#include "eval_query.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

typedef struct {
    cetta_engine_t *engine;
    CettaEvalQuery *core;
    atomic_bool cancelled;
    void (*entered)(void *);
    void *entry_context;
} Query;

static Query query_new(cetta_engine_t *engine, const char *source, int fuel) {
    cetta_atom_t *parsed = NULL;
    assert(cetta_embed_parse(engine, source, strlen(source), &parsed) == CETTA_OK);
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    CettaEvalQuery *query = eval_query_open(&engine->session.space, &engine->persistent,
        &engine->session.registry, parsed->value, fuel);
    assert(query);
    cetta_embed_engine_retain(engine);
    engine_leave(engine, &scope);
    cetta_embed_atom_release(parsed);
    Query result = {.engine = engine, .core = query};
    atomic_init(&result.cancelled, false);
    return result;
}

static CettaEvalQueryStep query_step_packet(Query *query, uint64_t allowance,
    cetta_result_t **result, uint64_t *transitions, uint64_t *spent,
    CettaEvalQueryAnswer *packet) {
    EmbeddingScope scope;
    assert(engine_enter(query->engine, &scope) == CETTA_OK);
    *result = result_new(query->engine);
    assert(*result);
    CettaEvalCancellationScope cancellation;
    eval_cancellation_enter_query(&cancellation, &query->cancelled);
    if (query->entered) query->entered(query->entry_context);
    EvalOutcome outcome;
    eval_outcome_init(&outcome);
    CettaEvalQueryStep step = eval_query_step(query->core,
        &(*result)->owner->arena, allowance, &outcome, packet, transitions);
    if (spent) *spent = outcome.steps_spent;
    append_outcome(*result, &outcome);
    eval_outcome_free(&outcome);
    eval_cancellation_leave(&cancellation);
    engine_leave(query->engine, &scope);
    return step;
}

static CettaEvalQueryStep query_step(Query *query, uint64_t allowance,
    cetta_result_t **result, uint64_t *transitions, uint64_t *spent) {
    return query_step_packet(query, allowance, result, transitions, spent, NULL);
}

static void query_close(Query *query) {
    pthread_mutex_lock(&query->engine->mutex);
    EmbeddingScope scope;
    scope_enter(query->engine, &scope);
    eval_query_close(query->core);
    scope_leave(&scope);
    pthread_mutex_unlock(&query->engine->mutex);
    cetta_embed_engine_release(query->engine);
    query->core = NULL;
}

static cetta_atom_t *next(Query *query, uint64_t allowance) {
    for (unsigned steps = 0; steps < 10000; ++steps) {
        cetta_result_t *result;
        uint64_t physical = 0;
        CettaEvalQueryStep step = query_step(query, allowance, &result, &physical, NULL);
        assert(physical <= allowance);
        if (step == CETTA_QUERY_DRIVER_PENDING)
            fprintf(stderr, "missing canonical retained driver\n");
        assert(step != CETTA_QUERY_DRIVER_PENDING && step != CETTA_QUERY_STOPPED);
        assert(result->status == CETTA_OK);
        if (step == CETTA_QUERY_ANSWER) {
            cetta_atom_t *answer = NULL;
            assert(cetta_embed_result_count(result) == 1);
            assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
            cetta_embed_result_release(result);
            return answer;
        }
        cetta_embed_result_release(result);
        if (step == CETTA_QUERY_EXHAUSTED) return NULL;
    }
    assert(!"query did not advance");
    return NULL;
}

static int64_t integer(cetta_atom_t *atom) {
    int64_t value;
    assert(atom && cetta_embed_atom_get_integer(atom, &value) == CETTA_OK);
    cetta_embed_atom_release(atom);
    return value;
}

static void ordered_streams(cetta_engine_t *engine) {
    const uint64_t allowances[] = {1, 2, UINT64_MAX};
    for (unsigned n = 0; n < 3; ++n) {
        Query query = query_new(engine, "(superpose (1 2 2 3))", -1);
        cetta_result_t *result;
        uint64_t physical = 999;
        assert(query_step(&query, 0, &result, &physical, NULL) == CETTA_QUERY_PAUSED);
        assert(!physical && !result->count);
        cetta_embed_result_release(result);
        const int64_t expected[] = {1, 2, 2, 3};
        for (unsigned i = 0; i < 4; ++i)
            assert(integer(next(&query, allowances[n])) == expected[i]);
        assert(!next(&query, allowances[n]));
        query_close(&query);
    }
}

static void source_budgets_on(cetta_engine_t *engine, const char *source, int limit) {
    for (int fuel = 0; fuel <= limit; ++fuel) {
        int64_t reference[3] = {0};
        size_t reference_count = 0;
        uint64_t reference_spent = 0;
        cetta_status_t reference_status = CETTA_OK;
        const uint64_t slices[] = {UINT64_MAX, 1, 2};
        for (unsigned slice = 0; slice < 3; ++slice) {
            Query query = query_new(engine, source, fuel);
            size_t count = 0;
            uint64_t spent = 0;
            cetta_status_t status = CETTA_OK;
            bool done = false;
            for (unsigned visits = 0; !done && visits < 1000; ++visits) {
                cetta_result_t *result;
                uint64_t unchanged, physical;
                query_step(&query, 0, &result, &physical, &unchanged);
                assert(!physical && unchanged == spent);
                cetta_embed_result_release(result);
                CettaEvalQueryStep step = query_step(&query, slices[slice],
                    &result, &physical, &spent);
                assert(physical <= slices[slice] && spent <= (uint64_t)fuel);
                status = result->status;
                if (step == CETTA_QUERY_ANSWER) {
                    cetta_atom_t *answer;
                    assert(count < 3 && cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
                    int64_t value = integer(answer);
                    if (!slice) reference[count] = value;
                    else assert(reference[count] == value);
                    count++;
                }
                assert(step != CETTA_QUERY_DRIVER_PENDING);
                done = step == CETTA_QUERY_EXHAUSTED || step == CETTA_QUERY_STOPPED;
                cetta_embed_result_release(result);
            }
            assert(done);
            if (!slice) {
                reference_count = count; reference_spent = spent; reference_status = status;
            } else assert(reference_count == count && reference_spent == spent && reference_status == status);
            query_close(&query);
        }
    }
}

static void source_budgets(cetta_engine_t *engine) {
    source_budgets_on(engine, "(superpose (1 2 3))", 24);
}

static void cancellation_scopes(void) {
    atomic_bool owner = false, other = false, cancelled = true, nested = false;
    CettaEvalCancellationScope root, query, same, child, independent;
    eval_cancellation_enter(&root, &owner);
    eval_cancellation_enter_query(&query, &cancelled);
    assert(eval_native_interrupted());
    eval_cancellation_enter(&same, &owner);
    assert(eval_native_interrupted());
    eval_cancellation_enter_query(&child, &nested);
    assert(eval_native_interrupted());
    eval_cancellation_enter(&independent, &other);
    assert(!eval_native_interrupted());
    eval_cancellation_leave(&independent);
    assert(eval_native_interrupted());
    eval_cancellation_leave(&child);
    eval_cancellation_leave(&same);
    eval_cancellation_leave(&query);
    assert(!eval_native_interrupted());
    eval_cancellation_leave(&root);
}

static cetta_result_t *document(cetta_engine_t *engine, const char *source) {
    cetta_result_t *result = NULL;
    assert(cetta_embed_run(engine, source, strlen(source), NULL, &result) == CETTA_OK);
    return result;
}

static void effects_and_cancel(cetta_engine_t *engine) {
    cetta_result_t *result = document(engine,
        "(= (emit $x) (let $u (add-atom &self (seen $x)) $x))");
    cetta_embed_result_release(result);
    Query query = query_new(engine, "(emit (superpose (1 2)))", -1);
    assert(integer(next(&query, 1)) == 1);
    result = document(engine, "!(match &self (seen $x) $x)");
    assert(result->count == 1);
    cetta_atom_t *answer;
    assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
    assert(integer(answer) == 1);
    cetta_embed_result_release(result);
    atomic_store(&query.cancelled, true);
    assert(query_step(&query, 0, &result, NULL, NULL) == CETTA_QUERY_STOPPED);
    assert(result->status == CETTA_CANCELLED);
    cetta_embed_result_release(result);
    query_close(&query);
    result = document(engine, "!(match &self (seen $x) $x)");
    assert(result->count == 1);
    cetta_embed_result_release(result);
}

static void compare_ordinary(cetta_engine_t *engine) {
    cetta_result_t *setup = document(engine,
        "(row 4) (row 8) (= (pick) 5) (= (pick) 5) (= (pick) 9) (= (echo $x) $x)");
    cetta_embed_result_release(setup);
    const char *sources[] = {
        "(+ 1 2)", "(if (== 1 1) 7 8)", "(let $x (superpose (1 2)) (+ $x 10))",
        "(match &self (row $x) $x)", "(collapse (superpose (1 2 2)))",
        "(unknown (+ 1 2))", "(catch (/ 1 0))", "(/ 1 0)",
        "(Error data reason)", "(echo (superpose (1 2 2)))", "(pick)",
        "(superpose ())", "(head-atom (a b))", "(if false a b)",
        "(function (return (+ 1 2)))",
        "(function (let $x (superpose (1 2)) (return $x)))",
        "(function (chain (metta (superpose (1 2)) %Undefined% &self) $x (return $x)))",
        "(let $f capture ($f (function (return (+ 1 2)))))",
        "(superpose ((Error a e) 7 7))",
        "(superpose (Empty (Error a e) Empty))",
        "(superpose ((Error a e) (Error b e)))",
    };
    for (size_t source = 0; source < sizeof(sources) / sizeof(*sources); ++source) {
        cetta_atom_t *expression = NULL;
        assert(cetta_embed_parse(engine, sources[source], strlen(sources[source]), &expression) == CETTA_OK);
        cetta_result_t *ordinary = NULL;
        cetta_status_t expected = cetta_embed_eval(engine, expression, NULL, &ordinary);
        cetta_embed_atom_release(expression);
        /* Independent executions may freshen variables in foreign error
         * contexts. Compare answers up to renaming, preserving sharing. */
        bool *seen = calloc(ordinary->count ? ordinary->count : 1, sizeof(*seen));
        assert(seen);
        Query query = query_new(engine, sources[source], -1);
        size_t count = 0;
        cetta_status_t observed = CETTA_OK;
        bool done = false;
        for (unsigned visits = 0; !done && visits < 10000; ++visits) {
            cetta_result_t *result;
            CettaEvalQueryStep step = query_step(&query, 1, &result, NULL, NULL);
            if (step == CETTA_QUERY_DRIVER_PENDING)
                fprintf(stderr, "missing driver: %s\n", sources[source]);
            assert(step != CETTA_QUERY_DRIVER_PENDING);
            observed = result->status;
            for (size_t i = 0; i < result->count; ++i) {
                size_t found = ordinary->count;
                for (size_t j = 0; j < ordinary->count; ++j) {
                    if (!seen[j] && atom_alpha_eq(result->values[i], ordinary->values[j])) {
                        found = j; break;
                    }
                }
                if (found == ordinary->count) {
                    fprintf(stderr, "unexpected query answer: %s\n", sources[source]);
                    EmbeddingScope diagnostic_scope; scope_enter(engine, &diagnostic_scope);
                    Arena diagnostics; arena_init_detached(&diagnostics);
                    char *actual = atom_to_string(&diagnostics, result->values[i]);
                    fprintf(stderr, "query value: %s; ordinary count: %zu\n", actual, ordinary->count);

                    for (size_t j = 0; j < ordinary->count; ++j) {
                        char *value = atom_to_string(&diagnostics, ordinary->values[j]);
                        fprintf(stderr, "ordinary value: %s\n", value);

                    }
                    arena_free(&diagnostics);
                    scope_leave(&diagnostic_scope);
                }
                assert(found < ordinary->count);
                seen[found] = true; count++;
            }
            done = step == CETTA_QUERY_EXHAUSTED || step == CETTA_QUERY_STOPPED || result->status != CETTA_OK;
            cetta_embed_result_release(result);
        }
        if (!done || expected != observed || count != ordinary->count)
            fprintf(stderr, "query differs: %s, status %d/%d, answers %zu/%zu\n",
                sources[source], observed, expected, count, ordinary->count);
        assert(done && expected == observed && count == ordinary->count);
        query_close(&query);
        free(seen);
        cetta_embed_result_release(ordinary);
    }
}

static void retained_binding_packets(cetta_engine_t *engine) {
    cetta_result_t *result = document(engine,
        "(= (bind-input a) 11) (= (bind-input b) 22)");
    cetta_embed_result_release(result);
    Query query = query_new(engine, "(bind-input $x)", -1);
    cetta_atom_t *snapshots[2] = {NULL, NULL};
    for (unsigned i = 0; i < 2; ++i) {
        CettaEvalQueryAnswer packet;
        CettaEvalQueryStep step;
        do {
            step = query_step_packet(&query, 1, &result, NULL, NULL, &packet);
            if (step != CETTA_QUERY_ANSWER) cetta_embed_result_release(result);
            assert(step == CETTA_QUERY_PAUSED || step == CETTA_QUERY_ANSWER);
        } while (step != CETTA_QUERY_ANSWER);
        assert(packet.kind == CETTA_CALL_VALUE && packet.bindings && !packet.delayed);
        assert(atom_wrap(result->owner, packet.bindings, &snapshots[i]) == CETTA_OK);
        cetta_embed_result_release(result);
    }
    query_close(&query);
    for (unsigned i = 0; i < 2; ++i) {
        Bindings bindings;
        assert(bindings_restore_captured_value(snapshots[i]->value, &bindings));
        assert(bindings.len == 1);
        BindingsIterator iterator = {.bindings = &bindings};
        Binding binding;
        assert(bindings_iterator_next(&iterator, &binding));
        Atom *value = binding_value_materialize(&snapshots[i]->owner->arena, binding.value);
        assert(value && atom_is_symbol_id(value, symbol_intern_cstr(
            &snapshots[i]->owner->engine->names.symbols, i ? "b" : "a")));
        bindings_free(&bindings);
        cetta_embed_atom_release(snapshots[i]);
    }
}

typedef struct {
    Query *query;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool entered, finished;
    cetta_status_t status;
} CancellationJob;

static void cancellation_entered(void *context) {
    CancellationJob *job = context;
    pthread_mutex_lock(&job->mutex);
    job->entered = true;
    pthread_cond_broadcast(&job->changed);
    pthread_mutex_unlock(&job->mutex);
}

static void *cancelled_worker(void *context) {
    CancellationJob *job = context;
    cetta_result_t *result;
    CettaEvalQueryStep step = query_step(job->query, UINT64_MAX, &result, NULL, NULL);
    assert(step == CETTA_QUERY_STOPPED);
    job->status = result->status;
    cetta_embed_result_release(result);
    pthread_mutex_lock(&job->mutex);
    job->finished = true;
    pthread_cond_broadcast(&job->changed);
    pthread_mutex_unlock(&job->mutex);
    return NULL;
}

static void wait_for(CancellationJob *job, const bool *flag) {
    struct timespec deadline;
    assert(!clock_gettime(CLOCK_REALTIME, &deadline));
    deadline.tv_sec += 5;
    while (!*flag) {
        int status = pthread_cond_timedwait(&job->changed, &job->mutex, &deadline);
        assert(status != ETIMEDOUT && !status);
    }
}

static void concurrent_cancellation(void) {
    for (unsigned close_owner = 0; close_owner < 2; ++close_owner) {
        cetta_engine_t *engine = NULL;
        assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
        cetta_result_t *result = document(engine, "(= (spin) (spin))");
        cetta_embed_result_release(result);
        Query query = query_new(engine, "(spin)", -1);
        CancellationJob job = {.query = &query,
            .mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
        query.entered = cancellation_entered;
        query.entry_context = &job;
        pthread_t worker;
        assert(!pthread_create(&worker, NULL, cancelled_worker, &job));
        pthread_mutex_lock(&job.mutex);
        wait_for(&job, &job.entered);
        pthread_mutex_unlock(&job.mutex);
        if (close_owner) cetta_embed_engine_close(engine);
        else atomic_store(&query.cancelled, true);
        pthread_mutex_lock(&job.mutex);
        wait_for(&job, &job.finished);
        pthread_mutex_unlock(&job.mutex);
        assert(!pthread_join(worker, NULL));
        assert(job.status == CETTA_CANCELLED);
        query_close(&query);
        if (!close_owner) {
            result = document(engine, "!(+ 1 2)");
            cetta_atom_t *answer;
            assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
            assert(integer(answer) == 3);
            cetta_embed_result_release(result);
        }
        pthread_cond_destroy(&job.changed);
        pthread_mutex_destroy(&job.mutex);
        cetta_embed_engine_release(engine);
    }
}

static void canonical_driver_boundary(cetta_engine_t *engine, bool required) {
    cetta_result_t *result = document(engine,
        "(: held (-> Atom Atom)) (= (held $x) $x)");
    cetta_embed_result_release(result);
    Query query = query_new(engine, "(held (+ 1 2))", -1);
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    bool ready = eval_query_has_retained_driver(query.core);
    engine_leave(engine, &scope);
    if (required) assert(ready && "canonical HE retained driver remains required");
    if (!ready) {
        uint64_t transitions = 99, spent = 99;
        assert(query_step(&query, 1, &result, &transitions, &spent) == CETTA_QUERY_DRIVER_PENDING);
        assert(!transitions && !spent && !result->count);
        cetta_embed_result_release(result);
        puts("OPEN: canonical HE held-operand queries need a retained driver; source remains unentered");
    } else {
        cetta_atom_t *answer = next(&query, 1);
        assert(answer && cetta_embed_atom_kind(answer) == CETTA_ATOM_EXPRESSION);
        cetta_embed_atom_release(answer);
    }
    query_close(&query);
}

static void canonical_interleaving(cetta_engine_t *engine) {
    cetta_result_t *result = document(engine,
        "(: hold (-> Atom Atom)) (= (hold $x) $x)");
    cetta_embed_result_release(result);
    Query left = query_new(engine, "(hold (+ 1 2))", -1);
    Query right = query_new(engine, "(hold (- 9 4))", -1);
    assert(query_step(&left, 1, &result, NULL, NULL) == CETTA_QUERY_PAUSED);
    cetta_embed_result_release(result);
    assert(query_step(&right, 1, &result, NULL, NULL) == CETTA_QUERY_PAUSED);
    cetta_embed_result_release(result);
    cetta_atom_t *first = next(&left, 1);
    assert(first && cetta_embed_atom_kind(first) == CETTA_ATOM_EXPRESSION);
    query_close(&left);
    cetta_atom_t *second = next(&right, 1);
    assert(second && cetta_embed_atom_kind(second) == CETTA_ATOM_EXPRESSION);
    query_close(&right);
    cetta_atom_t *head = NULL;
    assert(cetta_embed_atom_child(first, 0, &head) == CETTA_OK);
    size_t length = 0;
    char name[4] = {0};
    assert(cetta_embed_atom_get_text(head, name, sizeof(name), &length) == CETTA_OK);
    assert(length == 1 && name[0] == '+');
    cetta_embed_atom_release(head);
    cetta_embed_atom_release(first);
    cetta_embed_atom_release(second);
}

static void canonical_function_prefixes(cetta_engine_t *engine) {
    const char *sources[] = {
        "(function (let $x (superpose (1 2)) (let $u (add-atom &self (function-seen $x)) (return $x))))",
        "(function (chain (metta (superpose (1 2)) %Undefined% &self) $x "
        "(chain (metta (let $u (add-atom &self (function-seen $x)) $x) %Undefined% &self) $r (return $r))))",
        "(let $f capture ($f (function (let $x (superpose (1 2)) "
        "(let $u (add-atom &self (function-seen $x)) (return $x))))))",
    };
    const uint64_t allowances[] = {1, 2, UINT64_MAX};
    for (size_t s = 0; s < sizeof(sources) / sizeof(*sources); ++s) {
        for (size_t a = 0; a < sizeof(allowances) / sizeof(*allowances); ++a) {
            Query query = query_new(engine, sources[s], -1);
            assert(integer(next(&query, allowances[a])) == 1);
            cetta_result_t *result = document(engine, "!(match &self (function-seen $x) $x)");
            assert(result->count == 1u && result->values[0]->kind == ATOM_GROUNDED &&
                result->values[0]->ground.ival == 1);
            cetta_embed_result_release(result);
            query_close(&query);
            result = document(engine, "!(remove-atom &self (function-seen 1))");
            cetta_embed_result_release(result);
        }
    }
    source_budgets_on(engine, "(function (let $x (superpose (1 2)) (return $x)))", 64);
    source_budgets_on(engine,
        "(function (chain (metta (superpose (1 2)) %Undefined% &self) $x (return $x)))", 64);
    source_budgets_on(engine,
        "(let $f capture ($f (function (let $x (superpose (1 2)) (return $x)))))", 64);
}

static void canonical_captured_options(cetta_engine_t *engine) {
    cetta_result_t *result = document(engine,
        "!(pragma! max-stack-depth 113) !(bind! &saved capture) !(pragma! max-stack-depth 997)");
    cetta_embed_result_release(result);
    Query query = query_new(engine,
        "(&saved (function (let $x (superpose (1 2)) (return $x))))", -1);
    unsigned answers = 0u;
    int outside = 997;
    bool done = false;
    for (unsigned visit = 0u; visit < 2000u && !done; ++visit) {
        CettaEvalQueryStep step = query_step(&query, 1u, &result, NULL, NULL);
        assert(engine->session.libraries.session.options.max_stack_depth == outside);
        if (step == CETTA_QUERY_ANSWER) {
            cetta_atom_t *answer = NULL;
            assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
            assert(integer(answer) == (int64_t)++answers);
        }
        done = step == CETTA_QUERY_EXHAUSTED;
        assert(step == CETTA_QUERY_PAUSED || step == CETTA_QUERY_ANSWER || done);
        cetta_embed_result_release(result);
        if (answers == 1u && outside == 997) {
            result = document(engine, "!(pragma! max-stack-depth 998)");
            cetta_embed_result_release(result);
            outside = 998;
        }
    }
    assert(done && answers == 2u);
    query_close(&query);
    assert(engine->session.libraries.session.options.max_stack_depth == outside);
}

typedef struct { unsigned mode, closed; bool wrap_return; } ConditionalContext;
typedef struct { Arena storage; ConditionalContext *context; } ConditionalCursor;

static void conditional_close(void *pointer) {
    ConditionalCursor *cursor = pointer;
    cursor->context->closed++;
    arena_free(&cursor->storage);
    free(cursor);
}
static CettaOwnedExecutionStep conditional_next(void *pointer,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    (void)completion;
    ConditionalCursor *cursor = pointer;
    Arena *arena = &cursor->storage;
    CettaCallOutcome outcome;
    if (cursor->context->mode == 1u) outcome = cetta_call_failure();
    else if (cursor->context->mode == 2u) outcome = cetta_call_interrupted();
    else {
        Atom *variable = atom_var_with_id(arena, "witness", fresh_var_id());
        Atom *payload = atom_expr3(arena, atom_symbol(arena, "payload"), variable, variable);
        Atom *delayed = atom_expr2(arena, atom_symbol(arena, "need"), variable);
        if (cursor->context->wrap_return)
            payload = atom_expr2(arena, atom_symbol(arena, "return"), payload);
        outcome = cetta_call_suspended(payload, delayed);
    }
    *returned = (CettaOwnedReturn){.outcome = outcome, .final = true};
    return CETTA_OWNED_EXECUTION_RETURN;
}
static bool conditional_open(void *context, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    (void)invocation;
    (void)completion;
    ConditionalCursor *cursor = calloc(1u, sizeof(*cursor));
    assert(cursor);
    arena_init_detached(&cursor->storage);
    cursor->context = context;
    cetta_owned_execution_init(execution, cursor, conditional_next, conditional_close);
    return true;
}

static void canonical_conditional_packet_on(cetta_engine_t *engine, bool function) {
    ConditionalContext context = {.wrap_return = function};
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    CettaForeignProviderSpec spec = {.context = &context, .owner_thread = true, .open = conditional_open};
    Atom *provider = cetta_foreign_provider_atom(&engine->persistent, &spec);
    assert(provider);
    registry_bind(&engine->session.registry, "&conditional", provider);
    engine_leave(engine, &scope);
    cetta_atom_t *retained[2] = {NULL, NULL};
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        context.mode = mode;
        unsigned before = context.closed;
        Query query = query_new(engine, function ? "(function (&conditional))" : "(&conditional)", -1);
        cetta_result_t *result;
        CettaEvalQueryAnswer packet;
        CettaEvalQueryStep step;
        do {
            step = query_step_packet(&query, 1u, &result, NULL, NULL, &packet);
            if (step == CETTA_QUERY_PAUSED) cetta_embed_result_release(result);
        } while (step == CETTA_QUERY_PAUSED);
        if (!mode) {
            assert(step == CETTA_QUERY_ANSWER && result->count == 1u);
            assert(packet.kind == CETTA_CALL_SUSPENDED && packet.delayed);
            assert(cetta_embed_result_get(result, 0u, &retained[0]) == CETTA_OK);
            assert(atom_wrap(result->owner, packet.delayed, &retained[1]) == CETTA_OK);
        } else if (mode == 1u) {
            assert(step == CETTA_QUERY_EXHAUSTED && !result->count && result->status == CETTA_OK);
        } else {
            assert(step == CETTA_QUERY_STOPPED && result->status == CETTA_CANCELLED);
        }
        cetta_embed_result_release(result);
        query_close(&query);
        assert(context.closed == before + 1u);
        const char *source = function ? "!(function (&conditional))" : "!(&conditional)";
        cetta_status_t status = cetta_embed_run(engine, source, strlen(source), NULL, &result);
        assert(status == (mode == 2u ? CETTA_CANCELLED : CETTA_OK));
        assert(result->count == (mode == 0u ? 1u : 0u));
        cetta_embed_result_release(result);
        assert(context.closed == before + 2u);
    }
    Atom *payload = retained[0]->value, *delayed = retained[1]->value;
    assert(payload->kind == ATOM_EXPR && payload->expr.len == 3u);
    assert(delayed->kind == ATOM_EXPR && delayed->expr.len == 2u);
    assert(atom_eq(payload->expr.elems[1], payload->expr.elems[2]));
    assert(atom_eq(payload->expr.elems[1], delayed->expr.elems[1]));
    cetta_embed_atom_release(retained[0]);
    cetta_embed_atom_release(retained[1]);
}

static void canonical_conditional_packet(cetta_engine_t *engine) {
    canonical_conditional_packet_on(engine, false);
    canonical_conditional_packet_on(engine, true);
}

int main(int argc, char **argv) {
    bool require_canonical = argc > 1 && !strcmp(argv[1], "--require-canonical");
    bool require_streaming = argc > 1 && !strcmp(argv[1], "--require-streaming");
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    ordered_streams(engine);
    source_budgets(engine);
    cancellation_scopes();
    effects_and_cancel(engine);
    compare_ordinary(engine);
    retained_binding_packets(engine);
    Query left = query_new(engine, "(superpose (1 2))", -1);
    Query right = query_new(engine, "(superpose (10 20))", -1);
    assert(integer(next(&left, 1)) == 1);
    assert(integer(next(&right, 2)) == 10);
    query_close(&left);
    assert(integer(next(&right, 1)) == 20);
    query_close(&right);
    Query literal = query_new(engine, "42", -1);
    cetta_atom_t *retained = next(&literal, 1);
    query_close(&literal);
    Query cancelled = query_new(engine, "(superpose (1 2))", -1);
    atomic_store(&cancelled.cancelled, true);
    cetta_result_t *result;
    assert(query_step(&cancelled, 0, &result, NULL, NULL) == CETTA_QUERY_STOPPED);
    assert(result->status == CETTA_CANCELLED);
    cetta_embed_result_release(result);
    query_close(&cancelled);
    Query closed = query_new(engine, "(superpose (1 2))", -1);
    cetta_embed_engine_close(engine);
    query_close(&closed);
    cetta_embed_engine_release(engine);
    assert(integer(retained) == 42);
    concurrent_cancellation();
    cetta_engine_config_t he = {sizeof(he), CETTA_EMBED_ABI_VERSION, "he", NULL, NULL};
    assert(cetta_embed_engine_new(&he, &engine) == CETTA_OK);
    ordered_streams(engine);
    source_budgets(engine);
    compare_ordinary(engine);
    canonical_driver_boundary(engine, require_canonical);
    retained_binding_packets(engine);
    canonical_interleaving(engine);
    if (require_streaming) {
        effects_and_cancel(engine);
        canonical_function_prefixes(engine);
        canonical_captured_options(engine);
        canonical_conditional_packet(engine);
    }
    cetta_embed_engine_release(engine);
    puts("typed queries: ordered duplicates, bounded work, independent close, cancellation and retained answers pass");
    return 0;
}
