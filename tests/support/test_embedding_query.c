#include "../../src/embedding.c"
#include "eval_query.h"
#include "petta_search_machine.h"
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

static Query query_new_term_with_plan(cetta_engine_t *engine, Atom *source,
    const PettaPlanNode *plan, int fuel) {
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    CettaEvalQuery *query = plan
        ? eval_query_open_with_plan(&engine->session.space, &engine->persistent,
            &engine->session.registry, source, plan, fuel)
        : eval_query_open(&engine->session.space, &engine->persistent,
            &engine->session.registry, source, fuel);
    assert(query);
    cetta_embed_engine_retain(engine);
    engine_leave(engine, &scope);
    Query result = {.engine = engine, .core = query};
    atomic_init(&result.cancelled, false);
    return result;
}

static Query query_new_term(cetta_engine_t *engine, Atom *source, int fuel) {
    return query_new_term_with_plan(engine, source, NULL, fuel);
}

static Query query_new(cetta_engine_t *engine, const char *source, int fuel) {
    cetta_atom_t *parsed = NULL;
    assert(cetta_embed_parse(engine, source, strlen(source), &parsed) == CETTA_OK);
    Query result = query_new_term(engine, parsed->value, fuel);
    cetta_embed_atom_release(parsed);
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

static PettaCandidateSnapshotLease query_source_candidate(cetta_engine_t *engine,
    const char *name) {
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    PettaCandidateSnapshotLease lease = {0};
    assert(petta_program_candidate_snapshot_lease_profiled(
        engine->session.libraries.petta_program, &engine->session.space,
        symbol_intern_cstr(g_symbols, name), &lease, NULL));
    assert(lease.len == 1u && lease.items[0].rhs_plan);
    assert(petta_program_candidate_snapshot_lease_pin(&lease));
    engine_leave(engine, &scope);
    return lease;
}

static Query query_new_candidate(cetta_engine_t *engine,
    const PettaCandidateSnapshotLease *lease) {
    Atom *equation = lease->items[0].equation;
    assert(equation && equation->kind == ATOM_EXPR && equation->expr.len == 3u);
    return query_new_term_with_plan(engine, equation->expr.elems[2],
        lease->items[0].rhs_plan, -1);
}

static cetta_atom_t *query_project_output(Query *query, CettaEvalQueryOutput expected) {
    EmbeddingScope scope;
    assert(engine_enter(query->engine, &scope) == CETTA_OK);
    cetta_result_t *result = result_new(query->engine);
    assert(result);
    Atom *pattern = NULL;
    CettaEvalCompletion completion = CETTA_EVAL_COMPLETE;
    CettaEvalQueryOutput state = eval_query_project_output(query->core,
        &result->owner->arena, &pattern, &completion);
    assert(state == expected && completion == CETTA_EVAL_COMPLETE);
    assert((state == CETTA_QUERY_OUTPUT_PATTERN) == (pattern != NULL));
    cetta_atom_t *owned = NULL;
    if (pattern) assert(atom_wrap(result->owner, pattern, &owned) == CETTA_OK);
    engine_leave(query->engine, &scope);
    cetta_embed_result_release(result);
    return owned;
}

static void captured_source_outputs(void) {
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    cetta_embed_result_release(document(engine,
        "(= (shape-value) 7) "
        "(= (shape-effect $x) (let $_ignored (add-atom &self (Observed)) (Box $x $x (shape-value)))) "
        "(= (shape-quote) (quote (cons a (b c)))) "
        "(= (shape-frozen) (late-shape-call)) "
        "(= (shape-opaque) (shape-value))"));
    PettaCandidateSnapshotLease shared = query_source_candidate(engine, "shape-effect");
    PettaCandidateSnapshotLease quoted = query_source_candidate(engine, "shape-quote");
    PettaCandidateSnapshotLease frozen = query_source_candidate(engine, "shape-frozen");
    PettaCandidateSnapshotLease opaque = query_source_candidate(engine, "shape-opaque");
    size_t before = space_length64(&engine->session.space);
    Query query = query_new_candidate(engine, &shared);
    cetta_atom_t *shape = query_project_output(&query, CETTA_QUERY_OUTPUT_PATTERN);
    Atom *pattern = shape->value;
    assert(pattern->kind == ATOM_EXPR && pattern->expr.len == 4u);
    assert(pattern->expr.elems[1]->kind == ATOM_VAR && atom_eq(pattern->expr.elems[1], pattern->expr.elems[2]));
    assert(atom_eq(pattern->expr.elems[1], shared.items[0].equation->expr.elems[1]->expr.elems[1]));
    assert(pattern->expr.elems[3]->kind == ATOM_VAR && !atom_eq(pattern->expr.elems[1], pattern->expr.elems[3]));
    cetta_atom_t *again = query_project_output(&query, CETTA_QUERY_OUTPUT_PATTERN);
    assert(!atom_eq(pattern->expr.elems[3], again->value->expr.elems[3]));
    cetta_embed_atom_release(again);
    assert(space_length64(&engine->session.space) == before);
    cetta_result_t *paused;
    uint64_t physical = UINT64_MAX, spent = UINT64_MAX;
    assert(query_step(&query, 0u, &paused, &physical, &spent) == CETTA_QUERY_PAUSED);
    assert(!physical && !spent);
    cetta_embed_result_release(paused);
    cetta_atom_t *answer = next(&query, 1u);
    assert(answer && answer->value->kind == ATOM_EXPR && answer->value->expr.len == 4u);
    assert(answer->value->expr.elems[3]->ground.ival == 7);
    assert(space_length64(&engine->session.space) == before + 1u);
    assert(!query_project_output(&query, CETTA_QUERY_OUTPUT_UNAVAILABLE));
    cetta_embed_atom_release(answer);
    query_close(&query);
    assert(atom_eq(pattern->expr.elems[1], pattern->expr.elems[2]));
    cetta_embed_atom_release(shape);

    query = query_new_candidate(engine, &quoted);
    shape = query_project_output(&query, CETTA_QUERY_OUTPUT_PATTERN);
    cetta_atom_t *raw = NULL;
    const char *cons = "(cons a (b c))";
    assert(cetta_embed_parse(engine, cons, strlen(cons), &raw) == CETTA_OK);
    query_close(&query);
    assert(atom_eq(shape->value, raw->value));
    cetta_embed_atom_release(shape); cetta_embed_atom_release(raw);

    cetta_embed_result_release(document(engine, "(= (late-shape-call) 99)"));
    /* The body was planned before its head had an equation.  Reached now, the
     * occurrence is a call (P-DEF-4), so the plan imposes no output shape. */
    query = query_new_candidate(engine, &frozen);
    shape = query_project_output(&query, CETTA_QUERY_OUTPUT_OPAQUE);
    assert(!shape);
    assert(integer(next(&query, 1u)) == 99);
    query_close(&query);
    query = query_new(engine, "(late-shape-call)", -1);
    assert(integer(next(&query, 1u)) == 99);
    query_close(&query);

    query = query_new_candidate(engine, &opaque);
    assert(!query_project_output(&query, CETTA_QUERY_OUTPUT_OPAQUE));
    assert(integer(next(&query, 1u)) == 7);
    query_close(&query);
    petta_program_candidate_snapshot_lease_release(&shared);
    petta_program_candidate_snapshot_lease_release(&quoted);
    petta_program_candidate_snapshot_lease_release(&frozen);
    petta_program_candidate_snapshot_lease_release(&opaque);
    cetta_embed_engine_release(engine);

    cetta_engine_config_t extended = {sizeof(extended), CETTA_EMBED_ABI_VERSION,
        "petta", "extended", NULL};
    assert(cetta_embed_engine_new(&extended, &engine) == CETTA_OK);
    cetta_embed_result_release(document(engine, "(= (open-shape) (once (Box fixed)))"));
    PettaCandidateSnapshotLease open = query_source_candidate(engine, "open-shape");
    assert(open.items[0].rhs_plan->output == PETTA_PLAN_OUTPUT_CHILD);
    query = query_new_candidate(engine, &open);
    assert(!query_project_output(&query, CETTA_QUERY_OUTPUT_OPAQUE));
    answer = next(&query, 1u);
    assert(answer && answer->value->kind == ATOM_EXPR && answer->value->expr.len == 2u);
    cetta_embed_atom_release(answer);
    query_close(&query);
    petta_program_candidate_snapshot_lease_release(&open);
    cetta_embed_engine_release(engine);
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

typedef struct {
    unsigned mode, opened, closed, pulls;
    bool wrap_return, petta, ready;
    Atom *guard;
} ConditionalContext;
typedef struct {
    Arena storage;
    ConditionalContext *context;
    Atom *input;
    Bindings environment;
    unsigned position;
} ConditionalCursor;

typedef struct { Arena storage; Atom *argument; } GuardCursor;
static CettaOwnedExecutionStep guard_next(void *pointer,
    CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    (void)completion;
    GuardCursor *cursor = pointer;
    *returned = (CettaOwnedReturn){.outcome = atom_is_symbol(cursor->argument, "allowed")
        ? cetta_call_completed_value(atom_symbol(&cursor->storage, "true"))
        : cetta_call_failure(), .final = true};
    return CETTA_OWNED_EXECUTION_RETURN;
}
static void guard_close(void *pointer) {
    GuardCursor *cursor = pointer;
    arena_free(&cursor->storage);
    free(cursor);
}
static bool guard_open(void *context, const CettaInvocation *invocation,
    CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    (void)context; (void)completion;
    CettaExprLen arity;
    assert(cetta_invocation_arity(invocation, &arity) && arity == 1u);
    GuardCursor *cursor = calloc(1u, sizeof(*cursor));
    assert(cursor);
    arena_init_detached(&cursor->storage);
    cursor->argument = binding_value_materialize(&cursor->storage, cetta_invocation_argument(invocation, 0u));
    assert(cursor->argument);
    cetta_owned_execution_init(execution, cursor, guard_next, guard_close);
    return true;
}

static void conditional_close(void *pointer) {
    ConditionalCursor *cursor = pointer;
    cursor->context->closed++;
    bindings_free(&cursor->environment);
    arena_free(&cursor->storage);
    free(cursor);
}
static CettaOwnedExecutionStep conditional_next(void *pointer,
        CettaOwnedReturn *returned, CettaEvalCompletion *completion) {
    ConditionalCursor *cursor = pointer;
    Arena *arena = &cursor->storage;
    cursor->context->pulls++;
    CettaCallOutcome outcome;
    if (cursor->context->mode == 1u) outcome = cetta_call_failure();
    else if (cursor->context->mode == 2u) outcome = cetta_call_interrupted();
    else if (cursor->context->mode == 8u) {
        *returned = (CettaOwnedReturn){.outcome = cetta_call_completed_value(NULL), .final = true};
        return CETTA_OWNED_EXECUTION_RETURN;
    } else if (cursor->context->mode == 9u) {
        cetta_eval_completion_record(completion, CETTA_EVAL_INCOMPLETE_FUEL);
        return CETTA_OWNED_EXECUTION_INTERRUPTED;
    } else if (cursor->context->mode == 10u && !cursor->context->ready)
        return CETTA_OWNED_EXECUTION_PAUSED;
    else if (cursor->context->mode == 11u) {
        assert(cursor->input && cursor->input->kind == ATOM_VAR);
        assert(cursor->position < 2u);
        bindings_free(&cursor->environment);
        bindings_init(&cursor->environment);
        assert(bindings_add_var(&cursor->environment, cursor->input,
            atom_symbol(arena, cursor->position ? "b" : "a")));
        outcome = cetta_call_completed_value(atom_int(arena, cursor->position ? 22 : 11));
        cursor->position++;
        *returned = (CettaOwnedReturn){.outcome = outcome, .environment = &cursor->environment,
            .final = cursor->position == 2u};
        return CETTA_OWNED_EXECUTION_RETURN;
    } else if (cursor->context->mode == 3u || cursor->context->mode == 4u ||
               cursor->context->mode == 10u) {
        Atom *head = atom_symbol(arena, "provider-function");
        Atom *term = atom_expr(arena, &head, 1u);
        outcome = cursor->context->mode == 4u ? cetta_call_value(term) : cetta_call_completed_value(term);
    } else if (cursor->context->mode == 5u) {
        Atom *items[] = {atom_int(arena, 1), atom_int(arena, 2), atom_int(arena, 2), atom_int(arena, 3)};
        outcome = cetta_call_completed_frontier(atom_expr(arena, items, 4u));
    } else if (cursor->context->mode == 6u || cursor->context->mode == 7u) {
        Atom *error = atom_expr3(arena, atom_symbol(arena, "Error"),
            atom_symbol(arena, "provider"), atom_string(arena, "detail"));
        outcome = cursor->context->mode == 6u ? cetta_call_raised(error) : cetta_call_completed_value(error);
    }
    else {
        Atom *variable = atom_var_with_id(arena, "witness", fresh_var_id());
        Atom *payload = atom_expr3(arena, atom_symbol(arena, "payload"), variable, variable);
        Atom *delayed = atom_expr2(arena, atom_symbol(arena, "need"), variable);
        if (cursor->context->petta) {
            Atom *goal = atom_expr2(arena, cursor->context->guard, variable);
            Atom *component = atom_expr2(arena, goal, variable);
            delayed = atom_expr2(arena, atom_expr(arena, NULL, 0u), atom_expr(arena, &component, 1u));
        }
        if (cursor->context->wrap_return)
            payload = atom_expr2(arena, atom_symbol(arena, "return"), payload);
        outcome = cetta_call_suspended(payload, delayed);
    }
    *returned = (CettaOwnedReturn){.outcome = outcome, .final = true};
    return CETTA_OWNED_EXECUTION_RETURN;
}
static bool conditional_open(void *context, const CettaInvocation *invocation,
        CettaOwnedExecution *execution, CettaEvalCompletion *completion) {
    (void)completion;
    ConditionalCursor *cursor = calloc(1u, sizeof(*cursor));
    assert(cursor);
    arena_init_detached(&cursor->storage);
    bindings_init(&cursor->environment);
    cursor->context = context;
    cursor->context->opened++;
    CettaExprLen arity;
    assert(cetta_invocation_arity(invocation, &arity));
    if (arity) cursor->input = binding_value_materialize(&cursor->storage,
        cetta_invocation_argument(invocation, 0u));
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

static Query provider_query(cetta_engine_t *engine, Atom *provider, Atom *input) {
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    Atom *args[] = {provider, input};
    Atom *source = atom_expr(&engine->persistent, args, input ? 2u : 1u);
    engine_leave(engine, &scope);
    return query_new_term(engine, source, -1);
}

static CettaEvalQueryStep first_packet(Query *query, cetta_result_t **result,
    CettaEvalQueryAnswer *packet) {
    for (unsigned visits = 0u; visits < 10000u; ++visits) {
        CettaEvalQueryStep step = query_step_packet(query, 1u, result, NULL, NULL, packet);
        if (step != CETTA_QUERY_PAUSED) return step;
        cetta_embed_result_release(*result);
    }
    assert(!"provider query did not advance");
    return CETTA_QUERY_STOPPED;
}

static PettaMachineHostMode provider_classify(void *context, Space *space, Atom *call) {
    (void)context;
    return call && call->kind == ATOM_EXPR && call->expr.len &&
        cetta_foreign_provider_callable_at(space, call->expr.elems[0])
        ? PETTA_MACHINE_HOST_STRICT_APPLICATION : PETTA_MACHINE_HOST_NONE;
}

static void provider_machine_completion(cetta_engine_t *engine, Atom *provider,
    ConditionalContext *context) {
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    const unsigned modes[] = {2u, 9u, 10u};
    for (unsigned i = 0u; i < 3u; ++i) {
        context->mode = modes[i]; context->ready = false;
        PettaMachine machine;
        PettaMachineHost host = {.classify = provider_classify};
        Atom *call = atom_expr(&engine->persistent, &provider, 1u);
        assert(petta_machine_init(&machine, &engine->session.space,
            &engine->persistent, call, NULL, &host));
        assert(petta_machine_entered_completion(&machine) == CETTA_EVAL_COMPLETE);
        Atom *answer = NULL;
        Bindings environment;
        assert(petta_machine_next(&machine, &answer, &environment) == PETTA_MACHINE_STEP_SUSPENDED);
        bindings_free(&environment);
        CettaEvalCompletion expected = modes[i] == 2u ? CETTA_EVAL_INCOMPLETE_CANCELLED
            : modes[i] == 9u ? CETTA_EVAL_INCOMPLETE_FUEL : CETTA_EVAL_COMPLETE;
        assert(petta_machine_entered_completion(&machine) == expected);
        unsigned pulls = context->pulls;
        context->ready = true;
        assert(petta_machine_next(&machine, &answer, &environment) ==
            (modes[i] == 10u ? PETTA_MACHINE_STEP_ANSWER : PETTA_MACHINE_STEP_SUSPENDED));
        bindings_free(&environment);
        assert(context->pulls == pulls + (modes[i] == 10u ? 1u : 0u));
        petta_machine_destroy(&machine);
    }
    engine_leave(engine, &scope);
}

static void petta_provider_packets(cetta_engine_t *engine) {
    assert(engine->language == CETTA_LANGUAGE_PETTA);
    cetta_result_t *setup = document(engine, "(= (provider-function) 999)");
    cetta_embed_result_release(setup);
    ConditionalContext context = {.petta = true};
    EmbeddingScope scope;
    assert(engine_enter(engine, &scope) == CETTA_OK);
    CettaForeignProviderSpec guard_spec = {.owner_thread = true, .open = guard_open};
    context.guard = cetta_foreign_provider_atom(&engine->persistent, &guard_spec);
    assert(context.guard);
    CettaForeignProviderSpec spec = {.context = &context, .owner_thread = true, .open = conditional_open};
    Atom *provider = cetta_foreign_provider_atom(&engine->persistent, &spec);
    Atom *input = atom_var_with_id(&engine->persistent, "provider-input", fresh_var_id());
    assert(provider && input);
    engine_leave(engine, &scope);
    for (unsigned mode = 0u; mode <= 11u; ++mode) {
        context.mode = mode;
        context.ready = false;
        unsigned opened = context.opened, closed = context.closed;
        Query query = provider_query(engine, provider, mode == 11u ? input : NULL);
        cetta_result_t *result;
        CettaEvalQueryAnswer packet;
        if (mode == 10u) {
            for (unsigned turn = 0; turn < 20; ++turn) {
                assert(query_step_packet(&query, 1u, &result, NULL, NULL, &packet) == CETTA_QUERY_PAUSED);
                assert(!result->count && result->status == CETTA_OK);
                cetta_embed_result_release(result);
            }
            assert(context.opened == opened + 1u && context.closed == closed);
            context.ready = true;
        }
        CettaEvalQueryStep step = first_packet(&query, &result, &packet);
        if (mode == 1u) assert(step == CETTA_QUERY_EXHAUSTED && !result->count && result->status == CETTA_OK);
        else if (mode == 2u || mode == 8u || mode == 9u) {
            assert(step == CETTA_QUERY_STOPPED && !result->count);
            assert(result->status == (mode == 2u ? CETTA_CANCELLED : mode == 9u ? CETTA_FUEL_EXHAUSTED : CETTA_ENGINE_ERROR));
        } else {
            assert(step == CETTA_QUERY_ANSWER && result->count == 1u);
            if (mode == 0u) {
                assert(packet.kind == CETTA_CALL_SUSPENDED && packet.delayed);
                Atom *payload = result->values[0];
                assert(payload->kind == ATOM_EXPR && payload->expr.len == 3u &&
                    var_id_eq(payload->expr.elems[1]->var_id, payload->expr.elems[2]->var_id));
            } else if (mode == 4u || mode == 5u || mode == 11u) {
                cetta_atom_t *answer;
                assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
                assert(integer(answer) == (mode == 4u ? 999 : mode == 5u ? 1 : 11));
            } else if (mode == 6u || mode == 7u) {
                assert(packet.kind == (mode == 6u ? CETTA_CALL_RAISED : CETTA_CALL_VALUE));
            } else {
                Atom *term = result->values[0];
                assert(term->kind == ATOM_EXPR && term->expr.len == 1u);
            }
            if (mode == 11u) {
                assert(engine_enter(engine, &scope) == CETTA_OK);
                Bindings restored;
                assert(bindings_restore_captured_value(packet.bindings, &restored));
                Atom *bound = bindings_apply(&restored, &result->owner->arena, input);
                assert(bound && atom_is_symbol(bound, "a"));
                bindings_free(&restored);
                engine_leave(engine, &scope);
            }
        }
        cetta_embed_result_release(result);
        if (mode == 5u) {
            assert(integer(next(&query, 1u)) == 2);
            assert(integer(next(&query, 1u)) == 2);
            assert(integer(next(&query, 1u)) == 3);
            assert(!next(&query, 1u));
        } else if (mode == 11u) {
            assert(integer(next(&query, 1u)) == 22);
            assert(!next(&query, 1u));
        }
        query_close(&query);
        assert(context.opened == opened + 1u && context.closed == closed + 1u);
    }
    context.mode = 0u;
    for (unsigned allowed = 0u; allowed < 2u; ++allowed) {
        assert(engine_enter(engine, &scope) == CETTA_OK);
        Arena *arena = &engine->persistent;
        Atom *word = atom_symbol(arena, allowed ? "allowed" : "denied");
        Atom *pattern = atom_expr2(arena, atom_symbol(arena, "quote"),
            atom_expr3(arena, atom_symbol(arena, "payload"), word, word));
        Atom *call = atom_expr(arena, &provider, 1u);
        Atom *parts[] = {atom_symbol(arena, "let"), pattern, call, atom_int(arena, 42)};
        Atom *source = atom_expr(arena, parts, 4u);
        engine_leave(engine, &scope);
        Query query = query_new_term(engine, source, -1);
        Atom *answer = NULL;
        cetta_atom_t *observed = next(&query, 1u);
        if (observed) answer = observed->value;
        assert(allowed ? answer && answer->kind == ATOM_GROUNDED &&
            answer->ground.gkind == GV_INT && answer->ground.ival == 42 : !answer);
        cetta_embed_atom_release(observed);
        query_close(&query);
    }
    provider_machine_completion(engine, provider, &context);
}

int main(int argc, char **argv) {
    bool require_canonical = argc > 1 && !strcmp(argv[1], "--require-canonical");
    bool require_streaming = argc > 1 && !strcmp(argv[1], "--require-streaming");
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    ordered_streams(engine);
    source_budgets(engine);
    cancellation_scopes();
    captured_source_outputs();
    petta_provider_packets(engine);
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
