/* This translation unit replaces relational_cursor.o so the checks inspect
 * the production range navigator, including each retained suspension phase. */
#ifndef CETTA_RELATIONAL_NAVIGATION_SOURCE
#define CETTA_RELATIONAL_NAVIGATION_SOURCE "../src/relational_cursor.c"
#endif
#include CETTA_RELATIONAL_NAVIGATION_SOURCE

#include <assert.h>
#include <stdio.h>

static uint64_t random_state = UINT64_C(0x6846d7fc39a120b5);

static uint64_t random_word(void) {
    random_state ^= random_state << 13u;
    random_state ^= random_state >> 7u;
    random_state ^= random_state << 17u;
    return random_state;
}

static size_t remaining_keys(const RelationalFactor *factor, size_t begin, size_t end) {
    size_t distinct = 0u;
    for (size_t i = begin; i < end; i++)
        if (i == begin || factor->tuples[i] != factor->tuples[i - 1u])
            distinct++;
    return distinct;
}

static CettaRelationalPlan *two_factors(uint64_t left, uint64_t right,
    unsigned repeats, bool large_keys) {
    CettaRelationalPlan *plan = calloc(1u, sizeof(*plan));
    assert(plan);
    atomic_init(&plan->references, 1u);
    plan->info.factor_count = 2u;
    plan->info.variable_count = 1u;
    plan->factors = calloc(2u, sizeof(*plan->factors));
    plan->variables = calloc(1u, sizeof(*plan->variables));
    plan->variable_offsets = calloc(2u, sizeof(*plan->variable_offsets));
    plan->incidences = calloc(2u, sizeof(*plan->incidences));
    assert(plan->factors && plan->variables && plan->variable_offsets && plan->incidences);
    plan->variable_offsets[1] = plan->incidence_count = 2u;
    uint64_t masks[] = {left, right};
    for (size_t f = 0u; f < 2u; f++) {
        RelationalFactor *factor = &plan->factors[f];
        factor->width = 1u;
        factor->capacity = 64u * repeats;
        factor->variables = calloc(1u, sizeof(*factor->variables));
        factor->tuples = calloc(factor->capacity, sizeof(*factor->tuples));
        assert(factor->variables && factor->tuples);
        for (unsigned key = 0u; key < 64u; key++) {
            if (!(masks[f] & (UINT64_C(1) << key)))
                continue;
            uint32_t value = large_keys ? UINT32_MAX - 63u + key : key;
            /* Unequal occurrence weights exercise support/weight separation. */
            size_t weight = repeats == 1u ? 1u : 1u + (key + f) % repeats;
            for (size_t occurrence = 0u; occurrence < weight; occurrence++)
                factor->tuples[factor->length++] = value;
        }
        plan->incidences[f] = (RelationalIncidence){f, 0u};
    }
    return plan;
}

static void projection_case(uint64_t left, uint64_t right, unsigned repeats,
    bool large_keys, bool emit) {
    CettaRelationalPlan *plan = two_factors(left, right, repeats, large_keys);
    CettaRelationalCountCursor *cursor = relational_cursor_initialize(plan);
    CettaRelationalCountCursor *direct = relational_cursor_initialize(plan);
    assert(cursor && direct);
    uint64_t expected[64], found[64], expected_count = 0u;
    size_t expected_length = 0u, found_length = 0u, advances = 0u;
    /* Independent occurrence enumeration establishes both support and weight. */
    for (size_t i = 0u; i < plan->factors[0].length; i++) {
        uint64_t key = plan->factors[0].tuples[i];
        bool present = false;
        for (size_t j = 0u; j < plan->factors[1].length; j++) {
            if (key == plan->factors[1].tuples[j]) {
                expected_count++;
                present = true;
            }
        }
        if (present && (!expected_length || expected[expected_length - 1u] != key))
            expected[expected_length++] = key;
    }
    size_t distinct[2];
    for (size_t f = 0u; f < 2u; f++)
        distinct[f] = remaining_keys(&plan->factors[f], 0u, plan->factors[f].length);
    size_t selected = 0u;
    if (distinct[0] && distinct[1] &&
        plan->factors[0].tuples[0] > plan->factors[1].tuples[0])
        selected = 1u;
    for (size_t ticks = 0u; cursor->status == CETTA_RELATIONAL_COUNT_PAUSED; ticks++) {
        assert(ticks < 10000u);
        RelationalCursorPhase phase = cursor->phase;
        RelationalLevel level = cursor->levels[0];
        bool productive = phase == RELATIONAL_CURSOR_ADVANCE;
        size_t iterator = level.current, before = 0u, factor = 0u;
        if (phase == RELATIONAL_CURSOR_INTERSECT || productive) {
            assert(level.count == 2u && iterator < level.count);
            RelationalIterator *it = &cursor->iters[iterator];
            factor = it->factor;
            uint64_t key = relational_iterator_key(plan, it);
            assert(key <= cursor->maximum);
            assert(cursor->maximum == relational_iterator_key(plan, &cursor->iters[1u - iterator]));
            productive = productive || key != cursor->maximum;
            if (productive) {
                assert(factor == selected);
                before = remaining_keys(&plan->factors[factor], it->position, it->before.end);
            }
        }
        if (phase == RELATIONAL_CURSOR_LEAF) {
            assert(found_length < expected_length && cursor->key == expected[found_length]);
            found[found_length++] = cursor->key;
        }
        uint64_t quantum = 1u;
        relational_cursor_run(cursor, &quantum);
        assert(quantum == 0u);
        if (productive) {
            RelationalIterator *it = &cursor->iters[iterator];
            size_t after = remaining_keys(&plan->factors[factor], it->position, it->before.end);
            assert(after < before);
            advances++;
            selected = 1u - selected;
        }
    }
    assert(cursor->status == CETTA_RELATIONAL_COUNT_COMPLETE);
    assert(found_length == expected_length && cursor->total == expected_count);
    assert(advances <= 2u * (distinct[0] < distinct[1] ? distinct[0] : distinct[1]));
    relational_cursor_run(direct, NULL);
    assert(direct->status == CETTA_RELATIONAL_COUNT_COMPLETE && direct->total == expected_count);
    assert(direct->work.key_seeks == cursor->work.key_seeks);
    assert(direct->work.key_comparisons == cursor->work.key_comparisons);
    assert(direct->work.candidate_prefixes == found_length);
    assert(direct->work.leaf_assignments == found_length);
    if (emit) {
        printf("{\"left\":%llu,\"right\":%llu,\"advances\":%zu,\"keys\":[",
            (unsigned long long)left, (unsigned long long)right, advances);
        for (size_t i = 0u; i < found_length; i++)
            printf("%s%llu", i ? "," : "", (unsigned long long)found[i]);
        puts("]}");
    }
    cetta_relational_count_cursor_free(cursor);
    cetta_relational_count_cursor_free(direct);
    cetta_relational_plan_free(plan);
}

static void range_seek_cases(void) {
    uint32_t tuples[1024];
    RelationalFactor factor = {.width = 2u, .tuples = tuples};
    for (size_t i = 0u; i < 512u; i++) {
        tuples[2u * i] = UINT32_MAX - (uint32_t)i; /* A different column need not be sorted. */
        tuples[2u * i + 1u] = (uint32_t)(i / 3u);
    }
    for (size_t trial = 0u; trial < 20000u; trial++) {
        size_t low = (size_t)(random_word() % 513u);
        size_t high = low + (size_t)(random_word() % (513u - low));
        uint64_t key = trial % 17u ? random_word() % 180u : UINT64_MAX;
        for (unsigned upper = 0u; upper < 2u; upper++) {
            size_t expected = low;
            while (expected < high && (tuples[2u * expected + 1u] < key ||
                   (upper && tuples[2u * expected + 1u] == key)))
                expected++;
            CettaRelationalWork work = {0};
            size_t actual = relational_bound(&factor, 1u, low, high, key, upper, &work);
            assert(actual == expected && work.key_seeks == 1u);
            assert((low == high) == (work.key_comparisons == 0u));
            size_t comparisons = 0u;
            for (size_t width = high - low; width; width >>= 1u)
                comparisons++;
            assert(work.key_comparisons <= comparisons);
        }
    }
    uint32_t unsorted[] = {1u, 3u, 0u, 4u};
    factor = (RelationalFactor){.width = 1u, .tuples = unsorted};
    CettaRelationalWork work = {0};
    assert(relational_bound(&factor, 0u, 0u, 4u, 2u, false, &work) != 1u);
}

int main(int argc, char **argv) {
    bool emit = argc == 2 && strcmp(argv[1], "--json") == 0;
    assert(argc == 1 || emit);
    for (uint64_t left = 0u; left < 64u; left++)
        for (uint64_t right = 0u; right < 64u; right++) {
            projection_case(left, right, 1u, false, emit);
            if (!emit)
                projection_case(left, right, 4u, true, false);
        }
    if (!emit) {
        for (size_t trial = 0u; trial < 2000u; trial++)
            projection_case(random_word(), random_word(), 3u, trial % 2u, false);
        puts("PASS: native intersection rotates, makes strict key progress and preserves support and occurrence weights");
        range_seek_cases();
        puts("PASS: native binary range seeks match independent scans within the proved logarithmic comparison account");
    }
    return 0;
}
