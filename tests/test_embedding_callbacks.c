#define _POSIX_C_SOURCE 200809L
#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned retains, releases, opens, pulls, closes;
    int mode;
    cetta_engine_t *independent;
} Context;
typedef struct {
    Context *context;
    cetta_engine_t *engine;
    cetta_atom_t *argument;
    unsigned position;
    bool paused;
    int64_t count;
} Cursor;

static bool atoms_equal(const cetta_atom_t *left, const cetta_atom_t *right) {
    int equal = 0;
    assert(cetta_embed_atom_equal(left, right, &equal) == CETTA_OK);
    return equal != 0;
}

typedef struct { cetta_atom_t *atom; cetta_query_t *query; unsigned operation; cetta_status_t status; } RetainedTask;
static void *retained_task(void *pointer) {
    RetainedTask *task = pointer;
    size_t count = 0u;
    int equal = 0;
    switch (task->operation) {
    case 0: task->status = cetta_embed_atom_format(task->atom, NULL, 0u, &count); break;
    case 1: task->status = cetta_embed_atom_get_text(task->atom, NULL, 0u, &count); break;
    case 2: task->status = cetta_embed_atom_equal(task->atom, task->atom, &equal); break;
    case 3: cetta_embed_atom_release(task->atom); task->status = CETTA_OK; break;
    case 4:
        cetta_embed_query_close(task->query);
        cetta_embed_query_release(task->query);
        task->status = CETTA_OK;
        break;
    }
    return NULL;
}
static void retained_storage_retain(void *pointer) { (void)pointer; }
static void retained_storage_release(void *pointer) { atomic_fetch_add((atomic_uint *)pointer, 1u); }
static void retained_cross_thread(cetta_engine_t *engine) {
    cetta_atom_t *symbol = NULL, *atom = NULL, *source = NULL;
    assert(cetta_embed_atom_symbol(engine, "held-name", &symbol) == CETTA_OK);
    int64_t number = 17;
    atomic_uint released;
    atomic_init(&released, 0u);
    cetta_buffer_descriptor_t spec = {.size = sizeof(spec), .dtype = "i64",
        .allocation = &number, .allocation_bytes = sizeof(number)};
    cetta_buffer_owner_t owner = {&released, retained_storage_retain, retained_storage_release};
    cetta_buffer_t *buffer = NULL;
    assert(cetta_embed_buffer_borrow(&spec, &owner, &buffer) == CETTA_OK);
    assert(cetta_embed_atom_buffer(engine, buffer, &atom) == CETTA_OK);
    cetta_embed_buffer_release(buffer);
    const char text[] = "(superpose (1 2))";
    assert(cetta_embed_parse(engine, text, sizeof(text) - 1u, &source) == CETTA_OK);
    cetta_query_t *query = NULL;
    assert(cetta_embed_query_open(engine, source, NULL, &query) == CETTA_OK);
    cetta_embed_atom_release(source);
    RetainedTask tasks[5] = {
        {.atom = symbol, .operation = 0}, {.atom = symbol, .operation = 1},
        {.atom = symbol, .operation = 2}, {.atom = atom, .operation = 3},
        {.query = query, .operation = 4}};
    pthread_t threads[5];
    for (unsigned i = 0u; i < 5u; ++i) assert(!pthread_create(&threads[i], NULL, retained_task, &tasks[i]));
    for (unsigned i = 0u; i < 5u; ++i) assert(!pthread_join(threads[i], NULL));
    for (unsigned i = 0u; i < 3u; ++i) assert(tasks[i].status == CETTA_BUSY);
    assert(tasks[3].status == CETTA_OK && tasks[4].status == CETTA_OK);
    assert(atomic_load(&released) == 0u);
    /* This same-thread release is a safe owning-engine cleanup boundary. */
    cetta_embed_atom_release(symbol);
    assert(atomic_load(&released) == 1u);
}

static void retain_context(void *pointer) { ((Context *)pointer)->retains++; }
static void release_context(void *pointer) { ((Context *)pointer)->releases++; }
static cetta_status_t open_callback(void *pointer, cetta_engine_t *engine,
        cetta_atom_t *const *arguments, size_t arity, void **output) {
    assert(arity == 1);
    Context *context = pointer;
    Cursor *cursor = calloc(1u, sizeof(*cursor));
    assert(cursor);
    *cursor = (Cursor){.context = context, .engine = engine, .argument = arguments[0]};
    context->opens++;
    if (context->mode != 1) assert(cetta_embed_atom_get_integer(arguments[0], &cursor->count) == CETTA_OK);
    *output = cursor;
    return context->mode == 5 ? CETTA_OUT_OF_MEMORY : CETTA_OK;
}
static void close_callback(void *pointer, cetta_callback_close_mode_t mode) {
    (void)mode;
    Cursor *cursor = pointer;
    cursor->context->closes++;
    free(cursor);
}
typedef struct { cetta_engine_t *engine; cetta_status_t status; } CrossThread;
static void *cross_thread(void *pointer) {
    CrossThread *task = pointer;
    cetta_atom_t *value = NULL;
    task->status = cetta_embed_atom_integer(task->engine, 7, &value);
    cetta_embed_atom_release(value);
    return NULL;
}
static cetta_status_t next_callback(void *pointer, cetta_callback_state_t *state, cetta_atom_t **output) {
    Cursor *cursor = pointer;
    Context *context = cursor->context;
    context->pulls++;
    *output = NULL;
    if (context->mode == 1) {
        cetta_embed_atom_retain(cursor->argument);
        *output = cursor->argument;
        *state = CETTA_CALLBACK_VALUE;
        return CETTA_OK;
    }
    if (context->mode == 2) {
        CrossThread tasks[2] = {{cursor->engine, CETTA_OK}, {context->independent, CETTA_ENGINE_ERROR}};
        pthread_t threads[2];
        assert(!pthread_create(&threads[0], NULL, cross_thread, &tasks[0]));
        assert(!pthread_create(&threads[1], NULL, cross_thread, &tasks[1]));
        assert(!pthread_join(threads[0], NULL));
        assert(!pthread_join(threads[1], NULL));
        assert(tasks[0].status == CETTA_BUSY && tasks[1].status == CETTA_OK);
        const char source[] = "!(+ 20 22)";
        cetta_result_t *result = NULL;
        assert(cetta_embed_run(cursor->engine, source, sizeof(source) - 1u, NULL, &result) == CETTA_OK);
        assert(cetta_embed_result_get(result, 0u, output) == CETTA_OK);
        cetta_embed_result_release(result);
        retained_cross_thread(cursor->engine);
        *state = CETTA_CALLBACK_VALUE;
        return CETTA_OK;
    }
    if (context->mode == 3) {
        const char source[] = "(Error callback raised)";
        assert(cetta_embed_parse(cursor->engine, source, sizeof(source) - 1u, output) == CETTA_OK);
        *state = CETTA_CALLBACK_RAISED;
        return CETTA_OK;
    }
    if (context->mode == 4 && !cursor->paused) {
        cursor->paused = true;
        *state = CETTA_CALLBACK_PAUSED;
        return CETTA_OK;
    }
    if (context->mode == 6) {
        *state = CETTA_CALLBACK_VALUE;
        return cetta_embed_atom_boolean(cursor->engine, 1, output);
    }
    if (cursor->position >= (unsigned)cursor->count) {
        *state = CETTA_CALLBACK_COMPLETE;
        return CETTA_OK;
    }
    if (context->mode == 7) {
        unsigned index = cursor->position++;
        *state = CETTA_CALLBACK_VALUE;
        if (!index) return cetta_embed_atom_integer(cursor->engine, 7, output);
        cetta_atom_t *parts[2] = {NULL, NULL};
        assert(cetta_embed_atom_symbol(cursor->engine, "return", &parts[0]) == CETTA_OK);
        assert(cetta_embed_atom_integer(cursor->engine, index == 1u ? 11 : 13, &parts[1]) == CETTA_OK);
        cetta_status_t status = cetta_embed_atom_expression(cursor->engine, parts, 2u, output);
        cetta_embed_atom_release(parts[0]);
        cetta_embed_atom_release(parts[1]);
        return status;
    }
    *state = CETTA_CALLBACK_VALUE;
    return cetta_embed_atom_integer(cursor->engine, cursor->position++ / 2u, output);
}
static cetta_callback_descriptor_t descriptor(Context *context, bool stream) {
    return (cetta_callback_descriptor_t){.size = sizeof(cetta_callback_descriptor_t), .arity = 1u,
        .kind = stream ? CETTA_CALLBACK_STREAM : CETTA_CALLBACK_SINGLE,
        .context = context, .retain = retain_context, .release = release_context,
        .open = open_callback, .next = next_callback, .close = close_callback};
}
static cetta_atom_t *parse(cetta_engine_t *engine, const char *source) {
    cetta_atom_t *atom = NULL;
    assert(cetta_embed_parse(engine, source, strlen(source), &atom) == CETTA_OK);
    return atom;
}
static cetta_query_t *query(cetta_engine_t *engine, const char *source) {
    cetta_atom_t *expression = parse(engine, source);
    cetta_query_t *result = NULL;
    assert(cetta_embed_query_open(engine, expression, NULL, &result) == CETTA_OK);
    cetta_embed_atom_release(expression);
    return result;
}
static cetta_atom_t *take(cetta_query_t *query, uint64_t allowance) {
    for (unsigned i = 0; i < 1000; ++i) {
        cetta_query_step_t step = {.size = sizeof(step)};
        cetta_result_t *result = NULL;
        cetta_status_t status = cetta_embed_query_step(query, allowance, &step, &result);
        if (status != CETTA_OK) fprintf(stderr, "callback query: %s\n", cetta_embed_status_name(status));
        assert(status == CETTA_OK);
        assert(step.physical_steps <= allowance);
        cetta_atom_t *answer = NULL;
        if (step.state == CETTA_QUERY_STATE_ANSWER)
            assert(cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
        cetta_embed_atom_release(step.bindings);
        cetta_embed_atom_release(step.delayed);
        cetta_embed_result_release(result);
        if (answer || step.state == CETTA_QUERY_STATE_COMPLETE) return answer;
        assert(step.state == CETTA_QUERY_STATE_PAUSED);
    }
    assert(false && "callback query did not advance");
    return NULL;
}
static int64_t integer(cetta_atom_t *atom) {
    int64_t result = -1;
    assert(cetta_embed_atom_get_integer(atom, &result) == CETTA_OK);
    return result;
}
static void fuel_partitions(cetta_engine_t *engine, Context *context) {
    const int64_t fuels[] = {-1, 0, 1, 2, 3, 4, 8, 16, 64};
    const uint64_t allowances[] = {1, 2, UINT64_MAX};
    for (size_t f = 0; f < sizeof(fuels) / sizeof(*fuels); ++f) {
        int64_t reference[6] = {0};
        size_t reference_count = 0;
        uint64_t reference_spent = 0;
        cetta_status_t reference_status = CETTA_OK;
        for (size_t partition = 0; partition < 3u; ++partition) {
            unsigned opens = context->opens, closes = context->closes;
            cetta_atom_t *source = parse(engine, "(host:values 6)");
            cetta_eval_options_t options = {.size = sizeof(options), .semantic_fuel = fuels[f]};
            cetta_query_t *work = NULL;
            assert(cetta_embed_query_open(engine, source, &options, &work) == CETTA_OK);
            cetta_embed_atom_release(source);
            int64_t values[6] = {0};
            size_t count = 0;
            uint64_t spent = 0;
            cetta_status_t status = CETTA_OK;
            bool finished = false;
            for (unsigned visits = 0; visits < 1000u && !finished; ++visits) {
                cetta_query_step_t step = {.size = sizeof(step)};
                cetta_result_t *result = NULL;
                status = cetta_embed_query_step(work, allowances[partition], &step, &result);
                assert(step.physical_steps <= allowances[partition]);
                assert(status == CETTA_OK || status == CETTA_FUEL_EXHAUSTED);
                if (status == CETTA_OK && step.state == CETTA_QUERY_STATE_ANSWER) {
                    cetta_atom_t *answer = NULL;
                    assert(count < 6u && cetta_embed_result_get(result, 0u, &answer) == CETTA_OK);
                    values[count++] = integer(answer);
                    cetta_embed_atom_release(answer);
                }
                spent = step.source_steps;
                if (fuels[f] >= 0) assert(spent <= (uint64_t)fuels[f]);
                finished = status != CETTA_OK || step.state == CETTA_QUERY_STATE_COMPLETE;
                cetta_embed_atom_release(step.bindings);
                cetta_embed_atom_release(step.delayed);
                cetta_embed_result_release(result);
            }
            assert(finished);
            cetta_embed_query_release(work);
            assert(context->opens - opens <= 1u);
            assert(context->opens - opens == context->closes - closes);
            if (fuels[f] == 0) assert(context->opens == opens && count == 0u);
            if (fuels[f] == -1 || fuels[f] == 64) {
                assert(count == 6u && status == CETTA_OK);
                for (size_t i = 0; i < count; ++i) assert(values[i] == (int64_t)i / 2);
            }
            if (partition == 0u) {
                memcpy(reference, values, sizeof(values));
                reference_count = count;
                reference_spent = spent;
                reference_status = status;
            } else {
                assert(count == reference_count && spent == reference_spent && status == reference_status);
                assert(!memcmp(reference, values, sizeof(values)));
            }
        }
    }
}

int main(int argc, char **argv) {
    bool he = argc > 1 && !strcmp(argv[1], "--he");
    cetta_engine_config_t config = {.size = sizeof(config), .abi_version = CETTA_EMBED_ABI_VERSION,
        .language = he ? "he" : "petta"};
    cetta_engine_t *engine = NULL, *other = NULL;
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    assert(cetta_embed_engine_new(&config, &other) == CETTA_OK);
    Context stream = {0}, identity = {.mode = 1}, nested = {.mode = 2, .independent = other},
        raised = {.mode = 3}, paused = {.mode = 4}, refused = {.mode = 5}, boolean = {.mode = 6},
        mixed = {.mode = 7};
    Context *contexts[] = {&stream, &identity, &nested, &raised, &paused, &refused, &boolean, &mixed};
    const char *names[] = {"host:values", "host:identity", "host:nested", "host:raised", "host:paused", "host:refused", "host:boolean", "host:mixed"};
    for (size_t i = 0; i < sizeof(contexts) / sizeof(*contexts); ++i) {
        cetta_callback_descriptor_t spec = descriptor(contexts[i], i == 0u || i == 4u || i == 7u);
        cetta_atom_t *callable = NULL;
        assert(cetta_embed_register_callback(engine, names[i], &spec, &callable) == CETTA_OK);
        cetta_embed_atom_release(callable);
    }
    cetta_query_t *work = query(engine, "(host:values 6)");
    cetta_query_step_t zero = {.size = sizeof(zero)};
    cetta_result_t *result = NULL;
    assert(cetta_embed_query_step(work, 0u, &zero, &result) == CETTA_OK);
    assert(stream.opens == 0u && stream.pulls == 0u);
    cetta_embed_result_release(result);
    cetta_embed_atom_release(zero.bindings);
    cetta_embed_atom_release(zero.delayed);
    cetta_atom_t *retained = take(work, 1u);
    assert(integer(retained) == 0 && stream.opens == 1u && stream.pulls == 1u);
    cetta_embed_query_release(work);
    if (he) {
        work = query(engine, "(function (host:mixed 3))");
        cetta_atom_t *value = take(work, 1u);
        assert(integer(value) == 11 && mixed.pulls == 2u);
        cetta_embed_atom_release(value);
        cetta_embed_query_release(work);
        assert(mixed.opens == 1u && mixed.closes == 1u && mixed.pulls == 2u);
    }
    assert(stream.closes == 1u && stream.pulls == 1u);
    work = query(engine, "(host:values 6)");
    for (int64_t i = 0; i < 6; ++i) {
        cetta_atom_t *value = take(work, 2u);
        assert(integer(value) == i / 2);
        cetta_embed_atom_release(value);
    }
    assert(!take(work, 1u));
    cetta_embed_query_release(work);
    assert(stream.opens == 2u && stream.closes == 2u);
    const char source[] = "!(host:values 6)";
    assert(cetta_embed_run(engine, source, sizeof(source) - 1u, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 6u);
    for (size_t i = 0; i < 6u; ++i) {
        cetta_atom_t *value = NULL;
        assert(cetta_embed_result_get(result, i, &value) == CETTA_OK);
        assert(integer(value) == (int64_t)i / 2);
        cetta_embed_atom_release(value);
    }
    cetta_embed_result_release(result);
    cetta_atom_t *head = parse(engine, "host:identity"), *argument = parse(engine, "(+ 2 3)");
    assert(cetta_embed_call(engine, head, &argument, 1u, NULL, &result) == CETTA_OK);
    cetta_atom_t *data = NULL;
    assert(cetta_embed_result_get(result, 0u, &data) == CETTA_OK);
    assert(atoms_equal(data, argument));
    cetta_embed_atom_release(data);
    cetta_embed_result_release(result);
    cetta_embed_atom_release(argument);
    argument = parse(engine, "(Error value data)");
    assert(cetta_embed_call(engine, head, &argument, 1u, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_get(result, 0u, &data) == CETTA_OK);
    assert(atoms_equal(data, argument));
    cetta_embed_atom_release(data);
    cetta_embed_result_release(result);
    cetta_embed_atom_release(head);
    cetta_embed_atom_release(argument);
    work = query(engine, "(host:nested 0)");
    data = take(work, 1u);
    assert(integer(data) == 42);
    cetta_embed_atom_release(data);
    cetta_embed_query_release(work);
    const char *parallel = he
        ? "!(pragma! num-threads 2) !(collapse (superpose ((host:nested 0) (host:nested 0))))"
        : "!(pragma! num-threads 2) !(collapse (hyperpose ((host:nested 0) (host:nested 0))))";
    assert(cetta_embed_run(engine, parallel, strlen(parallel), NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 2u);
    cetta_atom_t *bag = NULL;
    assert(cetta_embed_result_get(result, 1u, &bag) == CETTA_OK);
    assert(cetta_embed_atom_arity(bag) == 2u);
    for (size_t i = 0; i < 2u; ++i) {
        cetta_atom_t *value = NULL;
        assert(cetta_embed_atom_child(bag, i, &value) == CETTA_OK);
        assert(integer(value) == 42);
        cetta_embed_atom_release(value);
    }
    cetta_embed_atom_release(bag);
    cetta_embed_result_release(result);
    const char *caught = he ? "!(host:raised 0)" : "!(catch (host:raised 0))";
    cetta_status_t caught_status = cetta_embed_run(engine, caught, strlen(caught), NULL, &result);
    if (caught_status != (he ? CETTA_LANGUAGE_RAISED : CETTA_OK))
        fprintf(stderr, "callback directive: %s, %zu answers\n",
            cetta_embed_status_name(caught_status), cetta_embed_result_count(result));
    assert(caught_status == (he ? CETTA_LANGUAGE_RAISED : CETTA_OK));
    size_t caught_count = cetta_embed_result_count(result);
    assert(caught_count == 1u);
    cetta_embed_result_release(result);
    work = query(engine, "(host:paused 3)");
    data = take(work, 1u);
    assert(integer(data) == 0);
    cetta_embed_atom_release(data);
    cetta_embed_query_release(work);
    for (size_t i = 0; i < 2u; ++i) {
        work = query(engine, i ? "(host:refused 0)" : "(host:raised 0)");
        cetta_query_step_t step = {.size = sizeof(step)};
        cetta_status_t status = cetta_embed_query_step(work, UINT64_MAX, &step, &result);
        assert(status == (i ? CETTA_RESOURCE_ERROR : CETTA_LANGUAGE_RAISED));
        cetta_embed_atom_release(step.bindings);
        cetta_embed_atom_release(step.delayed);
        cetta_embed_result_release(result);
        cetta_embed_query_release(work);
    }
    assert(refused.opens == 1u && refused.closes == 1u && refused.pulls == 0u);
    work = query(engine, "(host:boolean 0)");
    data = take(work, 1u);
    cetta_atom_t *truth = parse(engine, he ? "True" : "true");
    assert(atoms_equal(data, truth));
    cetta_embed_atom_release(data);
    cetta_embed_atom_release(truth);
    cetta_embed_query_release(work);
    fuel_partitions(engine, &stream);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    assert(integer(retained) == 0);
    cetta_embed_atom_release(retained);
    cetta_embed_engine_release(other);
    for (size_t i = 0; i < sizeof(contexts) / sizeof(*contexts); ++i)
        assert(contexts[i]->retains == 1u && contexts[i]->releases == 1u && contexts[i]->opens == contexts[i]->closes);
    puts("callbacks: pull prefixes, duplicates, value provenance, reentry, busy refusal and closure pass");
    return 0;
}
