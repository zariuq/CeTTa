#include "select/code_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Differential qualification of the code tree against the linear candidate
 * filter, plus the canaries of the Lean carrier and the maintenance laws.
 * Deterministic pseudo-random cases; no process state consulted.
 */

/* Link-time allocation faults affect this standalone executable only. */
static size_t allocation_budget = SIZE_MAX;
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
static bool allocation_allowed(void) {
    if (allocation_budget == SIZE_MAX) return true;
    if (!allocation_budget) return false;
    allocation_budget--;
    return true;
}
void *__wrap_malloc(size_t size) {
    return allocation_allowed() ? __real_malloc(size) : NULL;
}
void *__wrap_calloc(size_t count, size_t size) {
    return allocation_allowed() ? __real_calloc(count, size) : NULL;
}
void *__wrap_realloc(void *pointer, size_t size) {
    return allocation_allowed() ? __real_realloc(pointer, size) : NULL;
}

static unsigned checks;
static unsigned failures;

#define CHECK(cond, what)                                                   \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__); \
        }                                                                   \
    } while (0)

static uint32_t select_checked(const CettaCodeTree *tree, const uint32_t *observation,
                               uint32_t *out, CettaCodeTreeStats *stats) {
    uint32_t count = 0u;
    CHECK(cetta_code_tree_select(tree, observation, out, &count, stats),
          "selection succeeds without hiding allocation failure");
    return count;
}

static uint64_t rng_state = 0x9E3779B97F4A7C15ull;

static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 11);
}

/* A tag from a small alphabet, or unknown with the given percentage. */
static uint32_t random_tag(uint32_t alphabet, uint32_t unknown_percent) {
    if (rng() % 100u < unknown_percent)
        return CETTA_CODE_TREE_UNKNOWN;
    return rng() % alphabet;
}

static uint32_t *random_skeleton(uint32_t coordinates, uint32_t alphabet,
                                 uint32_t unknown_percent) {
    uint32_t *tags = malloc(sizeof(uint32_t) * (coordinates ? coordinates : 1u));
    for (uint32_t d = 0u; d < coordinates; d++)
        tags[d] = random_tag(alphabet, unknown_percent);
    return tags;
}

static void free_patterns(uint32_t **patterns, uint32_t count) {
    for (uint32_t i = 0u; i < count; i++)
        free(patterns[i]);
    free(patterns);
}

/* One random source and several random observations: tree marks equal the
 * reference marks, selection is ascending, and stats stay consistent. */
static void differential_case(uint32_t coordinates, uint32_t occurrences,
                              uint32_t alphabet, uint32_t unknown_percent) {
    uint32_t **patterns = malloc(sizeof(uint32_t *) * (occurrences ? occurrences : 1u));
    for (uint32_t i = 0u; i < occurrences; i++)
        patterns[i] = random_skeleton(coordinates, alphabet, unknown_percent);
    CettaCodeTree *tree = cetta_code_tree_build(
        coordinates, (const uint32_t *const *)patterns, occurrences);
    CHECK(tree != NULL, "build succeeds");
    if (!tree) {
        free_patterns(patterns, occurrences);
        return;
    }
    CHECK(cetta_code_tree_occurrence_capacity(tree) == occurrences, "capacity is the source length");
    uint8_t *expected = calloc(occurrences ? occurrences : 1u, 1u);
    uint8_t *actual = calloc(occurrences ? occurrences : 1u, 1u);
    uint32_t *selected = malloc(sizeof(uint32_t) * (occurrences ? occurrences : 1u));
    for (uint32_t round = 0u; round < 8u; round++) {
        uint32_t *observation = random_skeleton(coordinates, alphabet, round == 0u ? 100u : unknown_percent);
        memset(actual, 0, occurrences);
        CettaCodeTreeStats stats = {0};
        cetta_code_tree_traverse(tree, observation, actual, &stats);
        cetta_code_tree_reference_marks(
            coordinates, (const uint32_t *const *)patterns, occurrences, observation, expected);
        CHECK(memcmp(expected, actual, occurrences) == 0, "tree marks equal the linear filter");
        uint32_t count = select_checked(tree, observation, selected, NULL);
        uint32_t expected_count = 0u;
        for (uint32_t i = 0u; i < occurrences; i++)
            expected_count += expected[i];
        CHECK(count == expected_count, "selection count equals the filter count");
        bool ascending = true;
        for (uint32_t i = 1u; i < count; i++)
            ascending = ascending && selected[i - 1u] < selected[i];
        CHECK(ascending, "selection is in source order");
        for (uint32_t i = 0u; i < count; i++)
            CHECK(expected[selected[i]] == 1u, "every selected occurrence survives the filter");
        CHECK(stats.node_visits >= 1u, "a traversal visits the root");
        /* Fusion: every decided coordinate agrees for every survivor. */
        for (uint32_t i = 0u; i < count; i++) {
            const uint32_t *pattern = patterns[selected[i]];
            for (uint32_t d = 0u; d < coordinates; d++) {
                if (observation[d] != CETTA_CODE_TREE_UNKNOWN &&
                    pattern[d] != CETTA_CODE_TREE_UNKNOWN) {
                    CHECK(observation[d] == pattern[d],
                          "a decided coordinate agrees for every survivor");
                }
            }
        }
        free(observation);
    }
    /* Remove then reinsert a random occurrence: marks unchanged. */
    if (occurrences > 0u) {
        uint32_t victim = rng() % occurrences;
        uint32_t *observation = random_skeleton(coordinates, alphabet, unknown_percent);
        memset(expected, 0, occurrences);
        cetta_code_tree_traverse(tree, observation, expected, NULL);
        CHECK(cetta_code_tree_remove(tree, victim) == 1u, "one leaf held the victim");
        memset(actual, 0, occurrences);
        cetta_code_tree_traverse(tree, observation, actual, NULL);
        CHECK(actual[victim] == 0u, "removed occurrence is never marked");
        for (uint32_t i = 0u; i < occurrences; i++)
            if (i != victim)
                CHECK(actual[i] == expected[i], "removal changes no other occurrence");
        CHECK(cetta_code_tree_insert(tree, victim, patterns[victim]), "reinsert succeeds");
        memset(actual, 0, occurrences);
        cetta_code_tree_traverse(tree, observation, actual, NULL);
        CHECK(memcmp(expected, actual, occurrences) == 0, "reinsert restores the marks");
        free(observation);
    }
    /* Insert a fresh occurrence: exactly it is added, and only when unrefuted. */
    {
        uint32_t *pattern = random_skeleton(coordinates, alphabet, unknown_percent);
        uint32_t *observation = random_skeleton(coordinates, alphabet, unknown_percent);
        uint8_t *before = calloc(occurrences + 1u, 1u);
        uint8_t *after = calloc(occurrences + 1u, 1u);
        cetta_code_tree_traverse(tree, observation, before, NULL);
        CHECK(cetta_code_tree_insert(tree, occurrences, pattern), "insert of a new index succeeds");
        CHECK(cetta_code_tree_occurrence_capacity(tree) == occurrences + 1u, "capacity grows by one");
        cetta_code_tree_traverse(tree, observation, after, NULL);
        for (uint32_t i = 0u; i < occurrences; i++)
            CHECK(before[i] == after[i], "insert changes no existing occurrence");
        uint8_t reference;
        const uint32_t *one[1] = {pattern};
        cetta_code_tree_reference_marks(coordinates, one, 1u, observation, &reference);
        CHECK(after[occurrences] == reference, "inserted occurrence marked iff unrefuted");
        free(before);
        free(after);
        free(pattern);
        free(observation);
    }
    free(selected);
    free(expected);
    free(actual);
    cetta_code_tree_free(tree);
    free_patterns(patterns, occurrences);
}

/* The canaries of the Lean carrier: four occurrences, two coordinates. */
static void lean_canaries(void) {
    const uint32_t U = CETTA_CODE_TREE_UNKNOWN;
    const uint32_t a[2] = {1u, U};
    const uint32_t b[2] = {2u, U};
    const uint32_t c[2] = {U, U};
    const uint32_t d[2] = {1u, 7u};
    const uint32_t *patterns[4] = {a, b, c, d};
    CettaCodeTree *tree = cetta_code_tree_build(2u, patterns, 4u);
    CHECK(tree != NULL, "canary tree builds");
    if (!tree)
        return;
    uint32_t out[4];
    const uint32_t full[2] = {1u, 7u};
    const uint32_t head[2] = {1u, U};
    const uint32_t none[2] = {U, U};
    const uint32_t other[2] = {2u, U};
    uint32_t n = select_checked(tree, full, out, NULL);
    CHECK(n == 3u && out[0] == 0u && out[1] == 2u && out[2] == 3u, "full observation selects 0 2 3");
    n = select_checked(tree, head, out, NULL);
    CHECK(n == 3u && out[0] == 0u && out[1] == 2u && out[2] == 3u, "head-only keeps the unsampled second coordinate");
    n = select_checked(tree, none, out, NULL);
    CHECK(n == 4u, "unknown query is the enumeration");
    n = select_checked(tree, other, out, NULL);
    CHECK(n == 2u && out[0] == 1u && out[1] == 2u, "tag 2 refutes 0 and 3");
    CettaCodeTreeStats stats = {0};
    n = select_checked(tree, full, out, &stats);
    CHECK(stats.node_visits <= 7u, "a full observation visits at most two subtrees per node");
    CHECK(cetta_code_tree_remove(tree, 1u) == 1u, "remove one occurrence");
    n = select_checked(tree, none, out, NULL);
    CHECK(n == 3u && out[0] == 0u && out[1] == 2u && out[2] == 3u, "removed occurrence gone");
    CHECK(cetta_code_tree_insert(tree, 1u, b), "reinsert");
    n = select_checked(tree, none, out, NULL);
    CHECK(n == 4u, "reinsert restores the enumeration");
    uint32_t residual[2];
    CHECK(cetta_code_tree_residual_coordinates(2u, head, residual) == 1u && residual[0] == 1u,
          "residual coordinates are the unobserved ones");
    CHECK(cetta_code_tree_residual_coordinates(2u, full, residual) == 0u,
          "a fully observed query leaves no residual sampled coordinate");
    cetta_code_tree_free(tree);
}

static void empty_cases(void) {
    CettaCodeTree *tree = cetta_code_tree_build(0u, NULL, 0u);
    CHECK(tree != NULL, "empty tree builds");
    if (tree) {
        uint32_t out[1];
        CHECK(select_checked(tree, NULL, out, NULL) == 0u, "empty tree selects nothing");
        const uint32_t nothing[1] = {0u};
        CHECK(cetta_code_tree_insert(tree, 5u, nothing), "insert into a zero-coordinate tree");
        CHECK(select_checked(tree, NULL, out, NULL) == 1u && out[0] == 5u,
              "zero coordinates: every occurrence always survives");
        cetta_code_tree_free(tree);
    }
    const uint32_t U = CETTA_CODE_TREE_UNKNOWN;
    const uint32_t open[3] = {U, U, U};
    const uint32_t *patterns[1] = {open};
    tree = cetta_code_tree_build(3u, patterns, 1u);
    CHECK(tree != NULL, "all-unknown pattern builds");
    if (tree) {
        uint32_t out[1];
        const uint32_t obs[3] = {4u, 5u, 6u};
        CHECK(select_checked(tree, obs, out, NULL) == 1u, "an unconstrained pattern is never refuted");
        cetta_code_tree_free(tree);
    }
}

typedef struct {
    const CettaCodeTreeObservation *coordinates;
    uint32_t fail_at;
} PolicyObservation;

static bool observe_policy(void *context, uint32_t coordinate,
                           CettaCodeTreeObservation *observation) {
    PolicyObservation *policy = context;
    if (coordinate == policy->fail_at) return false;
    *observation = policy->coordinates[coordinate];
    return true;
}

static bool policy_accepts(CettaCodeTreeObservation observation, uint32_t tag) {
    if (tag == CETTA_CODE_TREE_UNKNOWN) return observation.include_free;
    if (observation.all_tags) return true;
    for (uint32_t i = 0u; i < observation.count; i++)
        if (observation.tags[i] == tag) return true;
    return false;
}

static void policy_cases(void) {
    enum { D = 5, N = 47, A = 7 };
    uint32_t patterns[N][D];
    const uint32_t *rows[N];
    for (uint32_t i = 0u; i < N; i++) {
        rows[i] = patterns[i];
        for (uint32_t d = 0u; d < D; d++)
            patterns[i][d] = rng() % 3u ? rng() % A : CETTA_CODE_TREE_UNKNOWN;
    }
    CettaCodeTree *tree = cetta_code_tree_build(D, rows, N);
    CettaCodeTreeCursor *cursor = cetta_code_tree_cursor_new(tree);
    CHECK(tree && cursor, "policy tree and reusable cursor build");
    if (!tree || !cursor) goto done;
    for (uint32_t round = 0u; round < 100u; round++) {
        uint32_t tags[D][A];
        CettaCodeTreeObservation observations[D] = {0};
        for (uint32_t d = 0u; d < D; d++) {
            observations[d].tags = tags[d];
            observations[d].all_tags = rng() % 4u == 0u;
            observations[d].include_free = rng() % 4u != 0u;
            for (uint32_t tag = 0u; tag < A; tag++)
                if (rng() % 2u) tags[d][observations[d].count++] = tag;
        }
        PolicyObservation policy = {observations, UINT32_MAX};
        const uint32_t *out = NULL;
        uint32_t count = 0u, expected_count = 0u;
        CHECK(cetta_code_tree_select_observed(tree, cursor, observe_policy,
              &policy, &out, &count, NULL), "multi-tag selection succeeds");
        for (uint32_t i = 0u; i < N; i++) {
            bool accepted = true;
            for (uint32_t d = 0u; d < D; d++)
                accepted = accepted && policy_accepts(observations[d], patterns[i][d]);
            if (accepted) {
                CHECK(expected_count < count && out[expected_count] == i,
                      "policy traversal equals the independent ordered filter");
                expected_count++;
            }
        }
        CHECK(expected_count == count, "policy selection preserves multiplicity");
        policy.fail_at = 0u;
        CHECK(!cetta_code_tree_select_observed(tree, cursor, observe_policy,
              &policy, &out, &count, NULL) && out == NULL && count == 0u,
              "observer failure is explicit and publishes no partial candidates");
    }
    CHECK(!cetta_code_tree_insert(tree, UINT32_MAX, patterns[0]),
          "unrepresentable occurrence index is rejected");
    CHECK(cetta_code_tree_insert(tree, N + 32u, patterns[0]),
          "a cursor can be reused after tree growth");
    CettaCodeTreeObservation any[D] = {0};
    for (uint32_t d = 0u; d < D; d++)
        any[d] = (CettaCodeTreeObservation){.all_tags = true, .include_free = true};
    PolicyObservation policy = {any, UINT32_MAX};
    const uint32_t *out = NULL;
    uint32_t count = 0u;
    allocation_budget = 0u;
    CHECK(!cetta_code_tree_select_observed(tree, cursor, observe_policy,
          &policy, &out, &count, NULL) && out == NULL && count == 0u,
          "scratch allocation failure is an error, not an empty answer");
    allocation_budget = SIZE_MAX;
    CHECK(cetta_code_tree_select_observed(tree, cursor, observe_policy,
          &policy, &out, &count, NULL) && count == N + 1u && out[N] == N + 32u,
          "grown cursor preserves every old occurrence and the new one");
done:
    cetta_code_tree_cursor_free(cursor);
    cetta_code_tree_free(tree);
}

static void insertion_failures(void) {
    const uint32_t original[] = {1u, 2u, 3u};
    const uint32_t added[] = {9u, 8u, 7u};
    const uint32_t *patterns[] = {original};
    const uint32_t unknown[] = {UINT32_MAX, UINT32_MAX, UINT32_MAX};
    for (size_t budget = 0u; budget < 16u; budget++) {
        CettaCodeTree *tree = cetta_code_tree_build(3u, patterns, 1u);
        CHECK(tree != NULL, "allocation-failure fixture builds");
        if (!tree) return;
        allocation_budget = budget;
        bool inserted = cetta_code_tree_insert(tree, 1u, added);
        allocation_budget = SIZE_MAX;
        uint32_t out[2];
        uint32_t count = select_checked(tree, unknown, out, NULL);
        CHECK(count == (inserted ? 2u : 1u) && out[0] == 0u,
              "failed insertion changes no existing occurrences");
        if (!inserted) {
            CHECK(cetta_code_tree_insert(tree, 1u, added), "failed insertion can be retried");
            CHECK(select_checked(tree, unknown, out, NULL) == 2u && out[1] == 1u,
                  "retry publishes the new occurrence exactly once");
        }
        cetta_code_tree_free(tree);
    }
}

static void deep_case(void) {
    const uint32_t depth = 32768u;
    uint32_t *tags = calloc(depth, sizeof(*tags));
    const uint32_t *patterns[] = {tags};
    CettaCodeTree *tree = cetta_code_tree_build(depth, patterns, 1u);
    CHECK(tags && tree, "deep tree builds");
    if (tree) {
        uint32_t out = UINT32_MAX;
        CHECK(select_checked(tree, tags, &out, NULL) == 1u && out == 0u,
              "deep traversal does not consume the C call stack");
    }
    cetta_code_tree_free(tree);
    free(tags);
}

int main(void) {
    insertion_failures();
    policy_cases();
    deep_case();
    lean_canaries();
    empty_cases();
    static const uint32_t shapes[][4] = {
        /* coordinates, occurrences, alphabet, unknown percent */
        {1u, 8u, 2u, 30u},   {2u, 16u, 3u, 30u},  {3u, 32u, 3u, 40u},
        {4u, 64u, 4u, 50u},  {4u, 64u, 2u, 10u},  {5u, 128u, 5u, 60u},
        {6u, 200u, 3u, 30u}, {2u, 300u, 8u, 20u}, {3u, 1u, 3u, 30u},
        {3u, 0u, 3u, 30u},
    };
    for (size_t s = 0u; s < sizeof(shapes) / sizeof(shapes[0]); s++)
        for (uint32_t repeat = 0u; repeat < 25u; repeat++)
            differential_case(shapes[s][0], shapes[s][1], shapes[s][2], shapes[s][3]);
    printf("(CodeTreeSummary checks=%u failures=%u)\n", checks, failures);
    if (failures == 0u)
        puts("PASS: code tree agrees with the linear candidate filter; maintenance laws hold");
    return failures ? 1 : 0;
}
