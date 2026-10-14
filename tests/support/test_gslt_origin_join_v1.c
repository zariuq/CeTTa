#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static size_t allocation_count, allocation_failure = SIZE_MAX;
static void *join_malloc(size_t size) {
    return allocation_count++ == allocation_failure ? NULL : malloc(size);
}
static void *join_calloc(size_t count, size_t size) {
    return allocation_count++ == allocation_failure ? NULL : calloc(count, size);
}
#define malloc join_malloc
#define calloc join_calloc
#include "../../native/gslt_composition_v1.c"
#undef malloc
#undef calloc

static unsigned passed, failed;
static void check(bool condition, const char *name) {
    if (condition) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", name); }
}

typedef struct { unsigned origin, label, body; } ModelEntry;
static bool equal(ModelEntry a, ModelEntry b) {
    return a.origin == b.origin && a.label == b.label && a.body == b.body;
}

/* Independent quadratic interpretation of the Lean pairwise contract;
 * production uses two hash indices and an ordered append. */
static bool model_join(ModelEntry *input, size_t len, bool coherent,
                        size_t *indices, size_t *out_len) {
    *out_len = 0u;
    for (size_t i = 0u; i < len; ++i) {
        bool shared = false;
        for (size_t j = 0u; j < *out_len; ++j) {
            ModelEntry previous = input[indices[j]];
            if ((previous.label == input[i].label ||
                 (coherent && previous.origin == input[i].origin)) &&
                !equal(previous, input[i])) return false;
            shared = shared || equal(previous, input[i]);
        }
        if (!shared) indices[(*out_len)++] = i;
    }
    return true;
}

static void compare_case(ModelEntry *input, size_t len, size_t split,
                          bool coherent, Atom **numbers) {
    CettaGsltOriginEntryV1 entries[16], *result = NULL;
    CettaGsltOriginJoinWitnessV1 witness;
    size_t expected[16], expected_len = 0u, result_len = SIZE_MAX;
    for (size_t i = 0u; i < len; ++i)
        entries[i] = (CettaGsltOriginEntryV1){
            numbers[input[i].origin], numbers[input[i].label],
            numbers[input[i].body]};
    bool model = model_join(input, len, coherent, expected, &expected_len);
    bool actual = cetta_gslt_origin_join_v1(entries, split, entries + split,
        len - split, coherent ? CETTA_GSLT_ORIGIN_COHERENT_V1
                             : CETTA_GSLT_ORIGIN_VIEW_V1,
        &result, &result_len, &witness);
    check(actual == model, "acceptance agrees with independent pairwise model");
    if (actual) {
        bool exact = result_len == expected_len;
        for (size_t i = 0u; exact && i < result_len; ++i)
            exact = atom_eq(result[i].origin, entries[expected[i]].origin) &&
                atom_eq(result[i].label, entries[expected[i]].label) &&
                atom_eq(result[i].body, entries[expected[i]].body);
        check(exact, "exact first-position order and full entry multiplicity");
        check(witness.status == CETTA_GSLT_ORIGIN_JOIN_OK_V1,
              "accepted result has no failure witness");
    } else {
        check(result == NULL && result_len == 0u,
              "rejection publishes no accepted prefix");
        bool justified = witness.first < witness.second && witness.second < len;
        if (justified) {
            ModelEntry a = input[witness.first], b = input[witness.second];
            justified = !equal(a, b) &&
                ((witness.status == CETTA_GSLT_ORIGIN_JOIN_LABEL_CONFLICT_V1 &&
                  a.label == b.label) ||
                 (coherent &&
                  witness.status == CETTA_GSLT_ORIGIN_JOIN_ORIGIN_CONFLICT_V1 &&
                  a.origin == b.origin));
        }
        check(justified, "conflict witness names two actual incompatible occurrences");
    }
    free(result);
}

int __wrap_main(int argc, char **argv) {
    (void)argc; (void)argv;
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    Arena arena;
    arena_init(&arena);
    Atom *numbers[8];
    for (unsigned i = 0u; i < 8u; ++i) numbers[i] = atom_int(&arena, i);
    for (unsigned encoding = 0u; encoding < 4096u; ++encoding) {
        unsigned remaining = encoding;
        ModelEntry input[4];
        for (size_t i = 0u; i < 4u; ++i) {
            unsigned value = remaining % 8u;
            input[i] = (ModelEntry){value / 4u, (value / 2u) % 2u, value % 2u};
            remaining /= 8u;
        }
        for (size_t split = 0u; split <= 4u; ++split) {
            compare_case(input, 4u, split, false, numbers);
            compare_case(input, 4u, split, true, numbers);
        }
    }
    uint32_t seed = UINT32_C(0x43bcdf91);
    for (unsigned iteration = 0u; iteration < 12000u; ++iteration) {
        ModelEntry input[16];
        size_t len = iteration % 17u;
        for (size_t i = 0u; i < len; ++i) {
            seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
            input[i] = (ModelEntry){(seed >> 8u) % 8u, (seed >> 16u) % 8u,
                                    (seed >> 24u) % 8u};
        }
        size_t split = iteration % (len + 1u);
        compare_case(input, len, split, false, numbers);
        compare_case(input, len, split, true, numbers);
    }

    CettaGsltOriginEntryV1 one = {numbers[0], numbers[1], numbers[2]};
    CettaGsltOriginEntryV1 *result = NULL;
    size_t result_len;
    CettaGsltOriginJoinWitnessV1 witness;
    allocation_count = 0u;
    check(cetta_gslt_origin_join_v1(&one, 1u, NULL, 0u,
        CETTA_GSLT_ORIGIN_COHERENT_V1, &result, &result_len, &witness),
        "allocation control accepts");
    size_t allocations = allocation_count;
    free(result);
    for (size_t i = 0u; i < allocations; ++i) {
        allocation_count = 0u; allocation_failure = i;
        result = (CettaGsltOriginEntryV1 *)(uintptr_t)1u;
        result_len = SIZE_MAX;
        check(!cetta_gslt_origin_join_v1(&one, 1u, NULL, 0u,
            CETTA_GSLT_ORIGIN_COHERENT_V1, &result, &result_len, &witness) &&
            result == NULL && result_len == 0u &&
            witness.status == CETTA_GSLT_ORIGIN_JOIN_ALLOCATION_FAILURE_V1,
            "each allocator refusal is atomic");
    }
    allocation_failure = SIZE_MAX;
    check(!cetta_gslt_origin_join_v1(&one, SIZE_MAX, &one, 1u,
        CETTA_GSLT_ORIGIN_VIEW_V1, &result, &result_len, &witness) &&
        witness.status == CETTA_GSLT_ORIGIN_JOIN_RESOURCE_LIMIT_V1,
        "sum overflow is refused before input traversal");
    one.body = NULL;
    check(!cetta_gslt_origin_join_v1(&one, 1u, NULL, 0u,
        CETTA_GSLT_ORIGIN_VIEW_V1, &result, &result_len, &witness) &&
        witness.status == CETTA_GSLT_ORIGIN_JOIN_BAD_ARGUMENT_V1,
        "missing full entry field is refused");
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(GsltOriginJoinV1Summary %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
