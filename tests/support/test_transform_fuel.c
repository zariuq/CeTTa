#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static cetta_result_t *run(cetta_engine_t *engine, const char *source) {
    cetta_result_t *result = NULL;
    assert(cetta_embed_run(engine, source, strlen(source), NULL, &result) == CETTA_OK);
    assert(cetta_embed_result_status(result) == CETTA_OK);
    return result;
}

static cetta_status_t evaluate(cetta_engine_t *engine, const char *source, int fuel) {
    cetta_atom_t *expression = NULL;
    cetta_result_t *result = NULL;
    assert(cetta_embed_parse(engine, source, strlen(source), &expression) == CETTA_OK);
    cetta_eval_options_t options = {sizeof(options), fuel};
    cetta_status_t status = cetta_embed_eval(engine, expression, &options, &result);
    if (result) {
        assert(status == cetta_embed_result_status(result));
        cetta_embed_result_release(result);
    }
    cetta_embed_atom_release(expression);
    return status;
}

static size_t count(cetta_engine_t *engine, const char *head) {
    char source[160];
    snprintf(source, sizeof(source), "!(collapse (match &self (%s $x) $x))", head);
    cetta_result_t *result = run(engine, source);
    assert(cetta_embed_result_count(result) == 1u);
    cetta_atom_t *tuple = NULL;
    assert(cetta_embed_result_get(result, 0u, &tuple) == CETTA_OK);
    size_t size = cetta_embed_atom_arity(tuple);
    cetta_embed_atom_release(tuple);
    cetta_embed_result_release(result);
    return size;
}

int main(void) {
    const char *languages[] = {"petta", "he"};
    const char *profiles[] = {NULL, "extended"};
    const char *programs[] = {
        "(collapse (transform &self (all (take (job $j))) (done $j) &self))",
        "(collapse (once (transform &self (take (job $j)) (done $j) &self)))",
        "(collapse (transform &self (take (job $j)) (done $j) &self))",
        "(collapse (transform &self (, (take (job $j)) (let $v (spin))) (done $j) &self))",
    };
    size_t trials = 0u, interrupted = 0u, completed = 0u;
    for (size_t lang = 0u; lang < 2u; lang++) {
        for (size_t profile = 0u; profile < 2u; profile++) {
            for (size_t mode = 0u; mode < 4u; mode++) {
                /* Plain HE leaves once uninterpreted and evaluates its
                 * arguments normally; first-answer control is an extension. */
                bool first = mode == 1u && (lang == 0u || profile == 1u);
                size_t previous_done = 0u;
                bool reached = false;
                for (int boundary = 0; boundary <= 259; boundary++) {
                    int fuel = boundary <= 256 ? boundary : (512 << (boundary - 257));
                    cetta_engine_config_t config = {sizeof(config), CETTA_EMBED_ABI_VERSION,
                        languages[lang], profiles[profile], NULL};
                    cetta_engine_t *engine = NULL;
                    assert(cetta_embed_engine_new(&config, &engine) == CETTA_OK);
                    cetta_result_t *setup = run(engine,
                        "!(import! &self (library lib_transform)) "
                        "(job a) (job b) (job c) (job d) "
                        "(= (spin) (let $_ (add-atom &self (tick x)) (spin)))");
                    cetta_embed_result_release(setup);
                    cetta_status_t status = evaluate(engine, programs[mode], fuel);
                    assert(status == CETTA_OK || status == CETTA_FUEL_EXHAUSTED);
                    size_t jobs = count(engine, "job"), done = count(engine, "done");
                    if (jobs + done != 4u || done < previous_done) {
                        fprintf(stderr, "%s/%s mode=%zu fuel=%d jobs=%zu done=%zu previous=%zu\n",
                            languages[lang], profiles[profile] ? profiles[profile] : "plain",
                            mode, fuel, jobs, done, previous_done);
                        assert(false);
                    }
                    previous_done = done;
                    if (mode == 0u) assert(done == 0u || done == 4u);
                    if (first && done > 1u) {
                        fprintf(stderr, "once fired too much: %s/%s fuel=%d status=%d done=%zu\n",
                            languages[lang], profiles[profile] ? profiles[profile] : "plain",
                            fuel, status, done);
                        assert(false);
                    }
                    if (mode == 3u) {
                        assert(status == CETTA_FUEL_EXHAUSTED && done == 0u);
                        assert(count(engine, "tick") <= (size_t)fuel);
                    } else if (status == CETTA_OK) {
                        if (done != (first ? 1u : 4u))
                            fprintf(stderr, "incomplete success: %s/%s mode=%zu fuel=%d jobs=%zu done=%zu\n",
                                languages[lang], profiles[profile] ? profiles[profile] : "plain",
                                mode, fuel, jobs, done);
                        assert(done == (first ? 1u : 4u));
                        reached = true;
                    } else {
                        assert(!reached);
                    }
                    interrupted += status == CETTA_FUEL_EXHAUSTED;
                    completed += status == CETTA_OK;
                    trials++;
                    cetta_embed_engine_close(engine);
                    cetta_embed_engine_release(engine);
                }
                if (mode != 3u && !reached) {
                    fprintf(stderr, "no completion: %s/%s mode=%zu\n", languages[lang],
                        profiles[profile] ? profiles[profile] : "plain", mode);
                    assert(false);
                }
            }
        }
    }
    assert(interrupted && completed);
    printf("PASS: %zu transform fuel boundaries preserve atomic writes, monotone prefixes, once and nested budgets (%zu complete, %zu exhausted)\n",
        trials, completed, interrupted);
    return 0;
}
