#define _POSIX_C_SOURCE 200809L
#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void formatted(cetta_atom_t *atom, const char *expected) {
    size_t required = 0;
    assert(cetta_embed_atom_format(atom, NULL, 0, &required) == CETTA_OK);
    char *text = malloc(required);
    assert(text);
    assert(cetta_embed_atom_format(atom, text, required, &required) == CETTA_OK);
    if (strcmp(text, expected)) fprintf(stderr, "expected %s, got %s\n", expected, text);
    assert(!strcmp(text, expected));
    free(text);
}

static cetta_result_t *run(cetta_engine_t *engine, const char *source) {
    cetta_result_t *result = NULL;
    cetta_status_t status = cetta_embed_run(engine, source, strlen(source), NULL, &result);
    if (status) fprintf(stderr, "run failed: %s\n", cetta_embed_status_name(status));
    assert(status == CETTA_OK);
    return result;
}

typedef struct { const char *language, *value; } EngineJob;
static void *independent(void *opaque) {
    const EngineJob *job = opaque;
    const char *value = job->value;
    cetta_engine_t *engine = NULL;
    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION, job->language, NULL, NULL};
    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
    char source[128];
    snprintf(source, sizeof(source), "(= (mine) %s) !(mine)", value);
    cetta_result_t *result = run(engine, source);
    assert(cetta_embed_result_count(result) == 1);
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
    cetta_embed_result_release(result);
    result = run(engine, "(row 1 first) (row 2 second) !(match &self (row $i $v) ($i $v))");
    assert(cetta_embed_result_count(result) == 2);
    cetta_embed_result_release(result);
    for (unsigned i = 0; i < 20; ++i) {
        result = run(engine, "!(mine)");
        cetta_atom_t *current = NULL;
        assert(cetta_embed_result_get(result, 0, &current) == CETTA_OK);
        formatted(current, value);
        cetta_embed_atom_release(current);
        cetta_embed_result_release(result);
    }
    result = run(engine, "!(new-space)");
    cetta_atom_t *space = NULL;
    assert(cetta_embed_result_get(result, 0, &space) == CETTA_OK);
    cetta_embed_result_release(result);
    cetta_embed_engine_close(engine);
    cetta_atom_t *refused = NULL;
    assert(cetta_embed_atom_integer(engine, 1, &refused) == CETTA_ENGINE_CLOSED);
    cetta_embed_engine_release(engine);
    formatted(answer, value);
    cetta_embed_atom_release(answer);
    return space;
}

static void concurrent_engines(void) {
    pthread_t threads[4];
    EngineJob jobs[] = {{"petta", "engine-one"}, {"he", "engine-two"},
                        {"petta", "engine-three"}, {"he", "engine-four"}};
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_create(&threads[i], NULL, independent, &jobs[i]));
    void *retained[4];
    for (unsigned i = 0; i < 4; ++i)
        assert(!pthread_join(threads[i], &retained[i]));
    for (unsigned i = 0; i < 4; ++i) {
        cetta_atom_t *space = retained[i];
        size_t length;
        assert(cetta_embed_atom_kind(space) == CETTA_ATOM_RESOURCE);
        assert(cetta_embed_atom_format(space, NULL, 0, &length) == CETTA_OK);
        assert(length > 0);
        cetta_embed_atom_release(space);
    }
}

static void ready_calls(void) {
    const char *languages[] = {"petta", "he"};
    for (unsigned language = 0; language < 2; ++language) {
        cetta_engine_config_t config = {
            sizeof(config), CETTA_EMBED_ABI_VERSION, languages[language], NULL, NULL};
        cetta_engine_t *engine = NULL;
        assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
        cetta_result_t *result = run(engine,
            "(= (identity $x) $x) (= (code) 9)");
        cetta_embed_result_release(result);
        cetta_atom_t *datum = NULL, *identity = NULL, *unknown = NULL;
        assert(cetta_embed_parse(engine, "(code)", 6, &datum) == CETTA_OK);
        assert(cetta_embed_atom_symbol(engine, "identity", &identity) == CETTA_OK);
        assert(cetta_embed_atom_symbol(engine, "unknown", &unknown) == CETTA_OK);
        for (int metered = 0; metered < 2; ++metered) {
            cetta_eval_options_t options = {sizeof(options), metered ? 100 : -1};
            assert(cetta_embed_call(engine, identity, &datum, 1, &options, &result) == CETTA_OK);
            cetta_atom_t *answer = NULL;
            assert(cetta_embed_result_count(result) == 1);
            assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
            formatted(answer, "(code)");
            cetta_embed_atom_release(answer);
            cetta_embed_result_release(result);
            assert(cetta_embed_call(engine, unknown, &datum, 1, &options, &result) == CETTA_OK);
            assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
            formatted(answer, "(unknown (code))");
            cetta_embed_atom_release(answer);
            cetta_embed_result_release(result);
            if (!language) {
                cetta_atom_t *divide = NULL, *operands[2] = {NULL, NULL};
                assert(cetta_embed_atom_symbol(engine, "/", &divide) == CETTA_OK);
                assert(cetta_embed_atom_integer(engine, 1, &operands[0]) == CETTA_OK);
                assert(cetta_embed_atom_integer(engine, 0, &operands[1]) == CETTA_OK);
                cetta_status_t raised = cetta_embed_call(
                    engine, divide, operands, 2, &options, &result);
                assert(raised == CETTA_LANGUAGE_RAISED);
                assert(cetta_embed_result_count(result) == 1);
                cetta_embed_result_release(result);
                cetta_embed_atom_release(divide);
                cetta_embed_atom_release(operands[0]);
                cetta_embed_atom_release(operands[1]);
            }
        }
        result = run(engine, "!(identity (code))");
        cetta_atom_t *answer = NULL;
        assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
        formatted(answer, "9");
        cetta_embed_atom_release(answer);
        cetta_embed_result_release(result);
        cetta_embed_atom_release(identity);
        cetta_embed_atom_release(unknown);
        cetta_embed_atom_release(datum);
        cetta_embed_engine_release(engine);
    }
}

static void document_contracts(void) {
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    cetta_result_t *result = run(engine, "!(Error data reason) ! 42 !(superpose ())");
    assert(cetta_embed_result_count(result) == 2);
    assert(cetta_embed_result_group_count(result) == 3);
    assert(cetta_embed_result_group_size(result, 2) == 0);
    cetta_embed_result_release(result);
    const char *source = "! 42 !(/ 1 0) !(add-atom &self after-error)";
    assert(cetta_embed_run(engine, source, strlen(source), NULL, &result) == CETTA_LANGUAGE_RAISED);
    assert(cetta_embed_result_group_count(result) == 2);
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
    formatted(answer, "42");
    cetta_embed_atom_release(answer);
    cetta_embed_result_release(result);
    result = run(engine, "!(match &self after-error yes)");
    assert(cetta_embed_result_count(result) == 0);
    cetta_embed_result_release(result);
    source = "!(+ 1 2)";
    cetta_eval_options_t options = {sizeof(options), 0};
    assert(cetta_embed_run(engine, source, strlen(source), &options, &result) == CETTA_FUEL_EXHAUSTED);
    cetta_embed_result_release(result);
    source = "(= (+ $x $y) rejected)";
    assert(cetta_embed_run(engine, source, strlen(source), NULL, &result) == CETTA_LANGUAGE_RAISED);
    assert(cetta_embed_result_count(result) == 1);
    cetta_embed_result_release(result);
    source = "!(add-atom &self parsed) (";
    assert(cetta_embed_run(engine, source, strlen(source), NULL, &result) == CETTA_PARSE_ERROR);
    cetta_embed_result_release(result);
    result = run(engine, "!(match &self parsed yes)");
    assert(cetta_embed_result_count(result) == 0);
    cetta_embed_result_release(result);
    cetta_embed_engine_release(engine);
}

static void public_queries(void) {
    cetta_engine_t *engine = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    cetta_atom_t *source = NULL;
    const char *text = "(superpose (1 2 2))";
    assert(cetta_embed_parse(engine, text, strlen(text), &source) == CETTA_OK);
    cetta_query_t *query = NULL;
    assert(cetta_embed_query_open(engine, source, NULL, &query) == CETTA_OK);
    cetta_embed_atom_release(source);
    unsigned count = 0;
    cetta_atom_t *retained = NULL;
    const int64_t expected[] = {1, 2, 2};
    for (unsigned visits = 0; visits < 1000; ++visits) {
        cetta_query_step_t info = {.size = sizeof(info)};
        cetta_result_t *result = NULL;
        assert(cetta_embed_query_step(query, visits ? 1 : 0, &info, &result) == CETTA_OK);
        assert(info.physical_steps <= (visits ? 1u : 0u));
        if (info.state == CETTA_QUERY_STATE_ANSWER) {
            cetta_atom_t *answer = NULL;
            int64_t value;
            assert(count < 3 && cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
            assert(cetta_embed_atom_get_integer(answer, &value) == CETTA_OK);
            assert(value == expected[count++]);
            cetta_embed_atom_release(retained);
            retained = answer;
        }
        cetta_embed_atom_release(info.bindings);
        cetta_embed_atom_release(info.delayed);
        cetta_embed_result_release(result);
        if (info.state == CETTA_QUERY_STATE_COMPLETE) break;
    }
    assert(count == 3);
    cetta_embed_query_close(query);
    cetta_query_step_t info = {.size = sizeof(info)};
    cetta_result_t *result = NULL;
    assert(cetta_embed_query_step(query, 1, &info, &result) == CETTA_QUERY_CLOSED);
    assert(!result);
    cetta_embed_query_release(query);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    formatted(retained, "2");
    cetta_embed_atom_release(retained);
}

static void public_spaces(void) {
    cetta_engine_t *engine = NULL, *foreign = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    assert(cetta_embed_engine_new(NULL, &foreign) == CETTA_OK);
    cetta_result_t *definitions = run(engine, "(= (row $x) (add-atom &self executed))");
    cetta_embed_result_release(definitions);
    cetta_space_t *space = NULL, *alias = NULL;
    assert(cetta_embed_space_new(engine, &space) == CETTA_OK);
    cetta_atom_t *fact = NULL, *pattern = NULL, *space_atom = NULL;
    const char *source = "(row 42)";
    assert(cetta_embed_parse(engine, source, strlen(source), &fact) == CETTA_OK);
    assert(cetta_embed_space_add_atom(space, fact) == CETTA_OK);
    assert(cetta_embed_space_add_atom(space, fact) == CETTA_OK);
    uint64_t count = 0;
    assert(cetta_embed_space_count(space, &count) == CETTA_OK && count == 2);
    assert(cetta_embed_space_atom(space, &space_atom) == CETTA_OK);
    assert(cetta_embed_space_from_atom(foreign, space_atom, &alias) == CETTA_FOREIGN_ENGINE);
    assert(!alias);
    assert(cetta_embed_space_from_atom(engine, space_atom, &alias) == CETTA_OK);
    assert(cetta_embed_parse(engine, "(row $x)", 8, &pattern) == CETTA_OK);
    cetta_query_t *query = NULL;
    assert(cetta_embed_space_match(space, pattern, NULL, NULL, &query) == CETTA_OK);
    cetta_embed_space_release(space);
    cetta_atom_t *answer = NULL, *bindings = NULL;
    unsigned answers = 0;
    for (unsigned visits = 0; visits < 1000; ++visits) {
        cetta_query_step_t step = {.size = sizeof(step)};
        cetta_result_t *result = NULL;
        assert(cetta_embed_query_step(query, 1, &step, &result) == CETTA_OK);
        if (step.state == CETTA_QUERY_STATE_ANSWER) {
            cetta_embed_atom_release(answer);
            cetta_embed_atom_release(bindings);
            assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
            bindings = step.bindings;
            step.bindings = NULL;
            cetta_atom_t *assignments = NULL;
            assert(cetta_embed_atom_child(bindings, 1, &assignments) == CETTA_OK);
            assert(cetta_embed_atom_arity(assignments) == 1);
            cetta_embed_atom_release(assignments);
            answers++;
        }
        cetta_embed_atom_release(step.bindings);
        cetta_embed_atom_release(step.delayed);
        cetta_embed_result_release(result);
        if (step.state == CETTA_QUERY_STATE_COMPLETE) break;
    }
    assert(answers == 2 && bindings);
    cetta_embed_query_release(query);
    cetta_atom_t *template_value = NULL;
    assert(cetta_embed_atom_symbol(engine, "hit", &template_value) == CETTA_OK);
    assert(cetta_embed_space_match(alias, pattern, template_value, NULL, &query) == CETTA_OK);
    bool saw_export = false;
    for (unsigned visits = 0; visits < 1000; ++visits) {
        cetta_query_step_t step = {.size = sizeof(step)};
        cetta_result_t *result = NULL;
        assert(cetta_embed_query_step(query, 1, &step, &result) == CETTA_OK);
        if (step.state == CETTA_QUERY_STATE_ANSWER) {
            cetta_atom_t *assignments = NULL;
            assert(cetta_embed_atom_child(step.bindings, 1, &assignments) == CETTA_OK);
            assert(cetta_embed_atom_arity(assignments) == 1);
            cetta_embed_atom_release(assignments);
            saw_export = true;
        }
        cetta_embed_atom_release(step.bindings);
        cetta_embed_atom_release(step.delayed);
        cetta_embed_result_release(result);
        if (step.state == CETTA_QUERY_STATE_COMPLETE) break;
    }
    assert(saw_export);
    cetta_embed_query_release(query);
    cetta_embed_atom_release(template_value);
    cetta_result_t *observed_effects = run(engine, "!(match &self executed yes)");
    assert(cetta_embed_result_count(observed_effects) == 0);
    cetta_embed_result_release(observed_effects);
    int removed = 0;
    assert(cetta_embed_space_remove_atom(alias, fact, &removed) == CETTA_OK && removed);
    assert(cetta_embed_space_count(alias, &count) == CETTA_OK && count == 0);
    assert(cetta_embed_space_remove_atom(alias, fact, &removed) == CETTA_OK && !removed);
    cetta_embed_space_release(alias);
    cetta_embed_atom_release(space_atom);
    cetta_embed_atom_release(pattern);
    cetta_embed_atom_release(fact);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    cetta_embed_engine_release(foreign);
    formatted(answer, "(row 42)");
    cetta_embed_atom_release(answer);
    cetta_embed_atom_release(bindings);
}

int main(void) {
    /* Exercise first-use initialization concurrently, before a warm engine. */
    concurrent_engines();
    ready_calls();
    document_contracts();
    public_queries();
    public_spaces();
    assert(cetta_embed_abi_version() == CETTA_EMBED_ABI_VERSION);
    cetta_engine_t *engine = NULL, *other = NULL;
    assert(cetta_embed_engine_new(NULL, &engine) == CETTA_OK);
    assert(cetta_embed_engine_new(NULL, &other) == CETTA_OK);
    cetta_atom_t *code = NULL;
    const char *source = "(+ 2 3)";
    assert(cetta_embed_parse(engine, source, strlen(source), &code) == CETTA_OK);
    cetta_result_t *result = NULL;
    assert(cetta_embed_eval(other, code, NULL, &result) == CETTA_FOREIGN_ENGINE);
    assert(cetta_embed_eval(engine, code, NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_count(result) == 1);
    cetta_atom_t *answer = NULL;
    assert(cetta_embed_result_get(result, 0, &answer) == CETTA_OK);
    formatted(answer, "5");
    cetta_embed_atom_release(answer);
    cetta_embed_atom_release(code);
    cetta_embed_result_release(result);
    result = run(engine, "(= (pick) a) (= (pick) a) !(pick) !(unknown)");
    assert(cetta_embed_result_group_count(result) == 2);
    assert(cetta_embed_result_group_size(result, 0) == 2);
    assert(cetta_embed_result_group_size(result, 1) == 1);
    assert(cetta_embed_result_get(result, 2, &answer) == CETTA_OK);
    cetta_embed_result_release(result);
    cetta_embed_engine_close(engine);
    cetta_embed_engine_release(engine);
    formatted(answer, "(unknown)");
    cetta_embed_atom_release(answer);
    cetta_embed_engine_release(other);
    puts("embedding: owned answers, grouping, foreign-engine refusal and concurrent engines pass");
    return 0;
}
