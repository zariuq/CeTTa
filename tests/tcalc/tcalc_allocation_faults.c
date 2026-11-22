#define _POSIX_C_SOURCE 200809L
#include "tcalc_core.h"
#include "tcalc_syntax.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);

static size_t allocation_calls;
static size_t fail_at;
static unsigned phase;
static unsigned failed_phase;
static bool failure_hit;

static bool refuse(size_t bytes) {
    if (!bytes) return false;
    allocation_calls++;
    if (allocation_calls != fail_at) return false;
    failure_hit = true;
    failed_phase = phase;
    return true;
}

void *__wrap_malloc(size_t bytes) {
    return refuse(bytes) ? NULL : __real_malloc(bytes);
}

void *__wrap_calloc(size_t count, size_t bytes) {
    return refuse(count && bytes ? 1 : 0) ? NULL : __real_calloc(count, bytes);
}

void *__wrap_realloc(void *old, size_t bytes) {
    return refuse(bytes) ? NULL : __real_realloc(old, bytes);
}

static uint64_t mix(uint64_t hash, uint64_t value) {
    return (hash ^ value) * UINT64_C(0x100000001b3);
}

static uint64_t outcome_digest(const TcOutcomes *outcomes) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t count = tc_outcomes_count(outcomes);
    hash = mix(hash, count);
    for (size_t i = 0; i < count; ++i) {
        size_t rows = 0;
        const TcTerm *row = tc_outcome_rows(outcomes, i, &rows);
        hash = mix(hash, rows);
        for (size_t j = 0; j < rows; ++j) hash = mix(hash, row[j]);
    }
    size_t ambiguous = tc_outcomes_ambiguous(outcomes);
    hash = mix(hash, ambiguous);
    for (size_t i = 0; i < ambiguous; ++i) {
        size_t rows = 0, matches = 0;
        const TcTerm *row = tc_ambiguous_rows(outcomes, i, &rows, &matches);
        hash = mix(mix(hash, rows), matches);
        for (size_t j = 0; j < rows; ++j) hash = mix(hash, row[j]);
    }
    hash = mix(hash, tc_outcomes_finished(outcomes));
    hash = mix(hash, tc_outcomes_states(outcomes));
    hash = mix(hash, tc_outcomes_cyclic(outcomes));
    hash = mix(hash, tc_outcomes_decider_failed(outcomes));
    hash = mix(hash, tc_outcomes_shortcuts_disagreed(outcomes));
    return hash;
}

static bool exercise(size_t failure, size_t *calls, uint64_t *digest) {
    static const char program_text[] =
        "(token 1) (token 2) "
        "(rule move-one (take (token 1)) (make (got 1))) "
        "(rule move-two (take (token 2)) (make (got 2)))";
    allocation_calls = 0;
    fail_at = failure;
    phase = failed_phase = 0;
    failure_hit = false;

    tc_terms_free(NULL);
    tc_program_free(NULL);
    tc_outcomes_free(NULL);

    phase = 1;
    TcTerms *terms = tc_terms_new();
    if (!terms) {
        assert(failure_hit && failed_phase == 1);
        *calls = allocation_calls;
        *digest = 0;
        return false;
    }

    phase = 2;
    TcDeclarations declarations = {0};
    TcSyntaxError error = {0};
    TcProgram *program = tc_read_program(terms, program_text,
        strlen(program_text), &declarations, &error);
    free(declarations.region);
    if (!program) {
        assert(failure == SIZE_MAX || failure_hit);
        tc_program_free(program);
        tc_terms_free(terms);
        *calls = allocation_calls;
        *digest = 0;
        return false;
    }
    phase = 3;
    TcInterpretation how = {
        .step = TC_STEP_ONE,
        .ask = TC_ASK_AFTER_TELLS,
        .store = TC_STORE_MULTISET,
        .matches = TC_MATCHES_SYMMETRIC,
        .budget = 1000,
        .backend = TC_BACKEND_C,
        .observe = TC_OBSERVE_OUTCOMES,
        .shortcuts = TC_SHORTCUTS_NONE,
    };
    TcOutcomes *outcomes = tc_explore(program, &how);
    bool complete = outcomes && tc_outcomes_complete_memory(outcomes);
    *digest = complete ? outcome_digest(outcomes) : 0;
    tc_outcomes_free(outcomes);
    tc_program_free(program);
    tc_terms_free(terms);
    *calls = allocation_calls;
    return complete;
}

int main(void) {
    size_t baseline = 0;
    uint64_t expected = 0;
    assert(exercise(SIZE_MAX, &baseline, &expected));
    assert(expected != 0);
    assert(baseline > 0);
    for (size_t failure = 1; failure <= baseline; ++failure) {
        size_t calls = 0;
        uint64_t observed = 0;
        bool complete = exercise(failure, &calls, &observed);
        assert(failure_hit && calls >= failure);
        if (complete) assert(observed == expected);
    }
    printf("PASS: %zu allocation failures preserve incomplete-memory reporting and nullable destruction\n",
           baseline);
    return 0;
}
