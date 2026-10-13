#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser_pack_native_v1.h"
#include "symbol.h"

/* Exercise the private encoder against the independent Atom construction
 * route, including every allocation owned by the direct implementation. */
static _Thread_local size_t allocation_count;
static _Thread_local size_t failing_allocation;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return allocation_count == failing_allocation ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    allocation_count++;
    return allocation_count == failing_allocation ? NULL : calloc(count, size);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#include "../../experiments/gslt2parse_foundation/native/parser_pack_native_v1.c"
#undef calloc
#undef malloc

static uint32_t passed;
static uint32_t failed;

static void expect(bool condition, const char *label) {
    if (condition) passed++;
    else {
        failed++;
        fprintf(stderr, "FAIL: %s\n", label);
    }
}

static bool equivalent(const PPABIV1Pack *pack,
                       const CettaLpNativeUtf8Forest *forest) {
    PPNativeV1Result reference;
    PPNativeV1Result direct;
    PPNativeV1ForestExtension empty_extension = {0};
    char error[512] = {0};
    bool ok;
    ppnative_v1_result_init(&reference);
    ppnative_v1_result_init(&direct);
    reference.forest = *forest;
    direct.forest = *forest;
    /* A nonnull extension retains the Atom-based oracle; zero prevents
     * materialization so it also works for large record arrays. */
    ok = ppnative_v1_forest_canonicalize(
             &reference, pack, pack->states[0].identity,
             &empty_extension, 0u, error, sizeof(error)) &&
        ppnative_v1_forest_digest_direct(
             &direct, pack, pack->states[0].identity, error, sizeof(error)) &&
        strcmp(reference.forest_digest, direct.forest_digest) == 0 &&
        !direct.canonical_forest_materialized && !direct.canonical_forest;
    arena_free(&reference.arena);
    arena_free(&direct.arena);
    return ok;
}

static void fill_forest(CettaLpNativeUtf8Forest *forest,
                        CettaLpNativeUtf8ForestNode *nodes, uint32_t node_len,
                        CettaLpNativeUtf8ForestChoice *choices,
                        uint32_t choice_len, uint32_t *roots, uint32_t root_len,
                        uint32_t seed) {
    const uint32_t scalars[] = {0u, 9u, 34u, 92u, 127u, 955u, 0xd7ffu,
                                0xe000u, 0x10000u, 0x10ffffu, UINT32_MAX};
    memset(forest, 0, sizeof(*forest));
    forest->nodes = nodes;
    forest->node_len = node_len;
    forest->choices = choices;
    forest->choice_len = choice_len;
    forest->roots = roots;
    forest->root_len = root_len;
    forest->scalar_len = seed;
    for (uint32_t i = 0u; i < node_len; i++) {
        memset(&nodes[i], 0, sizeof(nodes[i]));
        nodes[i].kind = (CettaLpNativeUtf8ForestNodeKind)((i + seed) % 4u);
        nodes[i].symbol_id = i % 2u;
        nodes[i].production_index = i % 2u;
        nodes[i].dot = seed;
        nodes[i].scalar_left = i;
        nodes[i].scalar_right = UINT32_MAX - i;
        nodes[i].terminal_value_kind =
            (CettaLpNativeUtf8TerminalValueKind)((i / 4u + seed) % 3u);
        nodes[i].terminal_scalar = scalars[i % 11u];
        nodes[i].terminal_witness_id = UINT32_MAX - i;
    }
    for (uint32_t i = 0u; i < choice_len; i++) {
        choices[i] = (CettaLpNativeUtf8ForestChoice){
            .parent_node = i % node_len,
            .prefix_node = i % 2u ? (i + 1u) % node_len
                : CETTA_LP_NATIVE_UTF8_FOREST_NONE,
            .child_node = (i + 2u) % node_len,
            .production_index = i % 2u,
            .scalar_pivot = UINT32_MAX - i,
        };
    }
    for (uint32_t i = 0u; i < root_len; i++) roots[i] = (2u * i + seed) % node_len;
}

static void malformed_controls(const PPABIV1Pack *pack,
                                CettaLpNativeUtf8Forest *forest) {
    PPNativeV1Result result;
    char error[512];
    uint32_t saved;
    ppnative_v1_result_init(&result);
    result.forest = *forest;
#define REJECTS(label) do { \
    memset(error, 0, sizeof(error)); \
    expect(!ppnative_v1_forest_digest_direct( \
        &result, pack, pack->states[0].identity, error, sizeof(error)), label); \
} while (0)
    saved = forest->roots[0];
    forest->roots[0] = forest->node_len;
    REJECTS("out-of-range root rejected");
    forest->roots[0] = saved;
    saved = forest->choices[0].parent_node;
    forest->choices[0].parent_node = forest->node_len;
    REJECTS("out-of-range parent rejected");
    forest->choices[0].parent_node = saved;
    saved = forest->choices[0].child_node;
    forest->choices[0].child_node = forest->node_len;
    REJECTS("out-of-range child rejected");
    forest->choices[0].child_node = saved;
    saved = forest->choices[0].prefix_node;
    forest->choices[0].prefix_node = forest->node_len;
    REJECTS("out-of-range prefix rejected");
    forest->choices[0].prefix_node = saved;
    saved = forest->choices[0].production_index;
    forest->choices[0].production_index = pack->production_len;
    REJECTS("out-of-range production rejected");
    forest->choices[0].production_index = saved;
    saved = forest->roots[1];
    forest->roots[1] = forest->roots[0];
    REJECTS("duplicate roots rejected");
    forest->roots[1] = saved;
    {
        CettaLpNativeUtf8ForestNode saved_node = forest->nodes[1];
        forest->nodes[1] = forest->nodes[0];
        REJECTS("duplicate node records rejected");
        forest->nodes[1] = saved_node;
    }
    {
        CettaLpNativeUtf8ForestChoice saved_choice = forest->choices[1];
        forest->choices[1] = forest->choices[0];
        REJECTS("duplicate choice records rejected");
        forest->choices[1] = saved_choice;
    }
    {
        CettaLpNativeUtf8ForestNode saved_node = forest->nodes[0];
        forest->nodes[0].kind = (CettaLpNativeUtf8ForestNodeKind)99;
        REJECTS("unknown node kind rejected");
        forest->nodes[0] = saved_node;
        forest->nodes[0].kind = CETTA_LP_NATIVE_UTF8_FOREST_TERM;
        forest->nodes[0].terminal_value_kind =
            (CettaLpNativeUtf8TerminalValueKind)99;
        REJECTS("unknown terminal payload rejected");
        forest->nodes[0] = saved_node;
    }
#undef REJECTS
    arena_free(&result.arena);
}

static void allocation_controls(const PPABIV1Pack *pack,
                                 const CettaLpNativeUtf8Forest *forest) {
    PPNativeV1Result result;
    char expected[65];
    char error[512] = {0};
    size_t calls;
    ppnative_v1_result_init(&result);
    result.forest = *forest;
    allocation_count = 0u;
    expect(ppnative_v1_forest_digest_direct(
        &result, pack, pack->states[0].identity, error, sizeof(error)),
        "allocation-control baseline");
    calls = allocation_count;
    memcpy(expected, result.forest_digest, sizeof(expected));
    for (size_t fail = 1u; fail <= calls; fail++) {
        allocation_count = 0u;
        failing_allocation = fail;
        memset(error, 0, sizeof(error));
        memcpy(result.forest_digest, expected, sizeof(expected));
        expect(!ppnative_v1_forest_digest_direct(
            &result, pack, pack->states[0].identity, error, sizeof(error)) &&
            error[0] && strcmp(result.forest_digest, expected) == 0,
            "failed owned allocation leaves digest unchanged");
        failing_allocation = 0u;
        memset(error, 0, sizeof(error));
        expect(ppnative_v1_forest_digest_direct(
            &result, pack, pack->states[0].identity, error, sizeof(error)) &&
            strcmp(result.forest_digest, expected) == 0,
            "fresh parse succeeds after each allocation failure");
    }
    {
        char sentinel = 'q';
        allocation_count = 0u;
        failing_allocation = 1u;
        expect(!ppnative_v1_forest_digest_direct(
            &result, pack, pack->states[0].identity, &sentinel + 1, 0u) &&
            sentinel == 'q', "zero-length error buffer is not accessed");
        allocation_count = 0u;
        expect(!ppnative_v1_forest_digest_direct(
            &result, pack, pack->states[0].identity, NULL, 0u),
            "absent error buffer preserves allocation-failure result");
        failing_allocation = 0u;
    }
    printf("(ForestEncodingOwnedAllocationPoints %zu)\n", calls);
    arena_free(&result.arena);
}

static void threshold_controls(const PPABIV1Pack *pack) {
    CettaLpNativeUtf8ForestNode *nodes = calloc(3001u, sizeof(*nodes));
    CettaLpNativeUtf8Forest forest = {0};
    char error[512] = {0};
    expect(nodes != NULL, "allocate threshold forest");
    if (!nodes) return;
    for (uint32_t i = 0u; i < 3001u; i++) {
        nodes[i].kind = CETTA_LP_NATIVE_UTF8_FOREST_EPSILON;
        nodes[i].scalar_left = i;
    }
    forest.nodes = nodes;
    for (uint32_t len = 3000u; len <= 3001u; len++) {
        PPNativeV1Result result;
        char digest[65];
        ppnative_v1_result_init(&result);
        forest.node_len = len;
        result.forest = forest;
        expect(equivalent(pack, &forest), "threshold independent digest agreement");
        expect(ppnative_v1_forest_canonicalize(
            &result, pack, pack->states[0].identity, NULL, 3000u,
            error, sizeof(error)) &&
            result.canonical_forest_materialized == (len == 3000u),
            "materialization threshold unchanged");
        memcpy(digest, result.forest_digest, sizeof(digest));
        expect(ppnative_v1_materialize_canonical_forest(
            &result, pack, pack->states[0].identity, 3001u,
            error, sizeof(error)) && result.canonical_forest_materialized &&
            strcmp(digest, result.forest_digest) == 0,
            "explicit evidence materialization preserves direct digest");
        arena_free(&result.arena);
    }
    free(nodes);
}

typedef struct {
    const PPABIV1Pack *pack;
    const CettaLpNativeUtf8Forest *forest;
    const char *expected;
    bool ok;
} ConcurrentCase;

static void *concurrent_encode(void *opaque) {
    ConcurrentCase *test = opaque;
    PPNativeV1Result result;
    char error[512] = {0};
    ppnative_v1_result_init(&result);
    result.forest = *test->forest;
    test->ok = true;
    for (uint32_t i = 0u; i < 32u; i++) {
        if (!ppnative_v1_forest_digest_direct(
                &result, test->pack, test->pack->states[0].identity,
                error, sizeof(error)) ||
            strcmp(result.forest_digest, test->expected) != 0) {
            test->ok = false;
            break;
        }
    }
    arena_free(&result.arena);
    return NULL;
}

static void concurrent_controls(const PPABIV1Pack *pack,
                                const CettaLpNativeUtf8Forest *forest) {
    PPNativeV1Result result;
    ConcurrentCase cases[4];
    pthread_t threads[4];
    uint32_t created = 0u;
    char error[512] = {0};
    ppnative_v1_result_init(&result);
    result.forest = *forest;
    expect(ppnative_v1_forest_digest_direct(
        &result, pack, pack->states[0].identity, error, sizeof(error)),
        "concurrent baseline");
    for (uint32_t i = 0u; i < 4u; i++) {
        cases[i] = (ConcurrentCase){pack, forest, result.forest_digest, false};
        if (pthread_create(&threads[i], NULL, concurrent_encode, &cases[i])) break;
        created++;
    }
    expect(created == 4u, "create concurrent direct encoders");
    for (uint32_t i = 0u; i < created; i++)
        expect(pthread_join(threads[i], NULL) == 0 && cases[i].ok,
               "immutable pack supports independent encoder caches");
    arena_free(&result.arena);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    PPABIV1Pack pack = {0};
    PPABIV1State states[2] = {0};
    PPABIV1Terminal terminals[2] = {0};
    PPABIV1Production productions[2] = {0};
    CettaLpNativeUtf8ForestNode nodes[64];
    CettaLpNativeUtf8ForestChoice choices[32];
    uint32_t roots[8];
    CettaLpNativeUtf8Forest forest;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    states[0].identity = atom_symbol(&arena, "start");
    states[1].identity = atom_string(&arena, "\"\\\n\t\xce\xbb");
    terminals[0].identity = atom_symbol(&arena, "terminal");
    {
        Atom *identity_args[] = {atom_string(&arena, "nested\r\xce\xbb"),
                                 atom_int(&arena, -13)};
        terminals[1].identity = ppnative_v1_expr(&arena, "identity", identity_args, 2u);
    }
    for (uint32_t i = 0u; i < 2u; i++) {
        Atom *args[] = {i ? atom_string(&arena, "quoted\"\\\n\xce\xbb")
                          : atom_symbol(&arena, "label"),
                        states[0].identity, atom_symbol(&arena, "nil"),
                        atom_symbol(&arena, "nil")};
        productions[i].identity = ppnative_v1_expr(&arena, "pp-production", args, 4u);
    }
    pack.states = states;
    pack.state_len = 2u;
    pack.terminals = terminals;
    pack.terminal_len = 2u;
    pack.productions = productions;
    pack.production_len = 2u;
    for (uint32_t seed = 0u; seed < 32u; seed++) {
        fill_forest(&forest, nodes, 64u, choices, 32u, roots, 8u, seed);
        expect(equivalent(&pack, &forest), "generated record-family digest agreement");
    }
    fill_forest(&forest, nodes, 64u, choices, 32u, roots, 8u, UINT32_MAX);
    expect(equivalent(&pack, &forest), "maximum word payload digest agreement");
    malformed_controls(&pack, &forest);
    allocation_controls(&pack, &forest);
    concurrent_controls(&pack, &forest);
    threshold_controls(&pack);
    memset(&forest, 0, sizeof(forest));
    expect(equivalent(&pack, &forest), "empty forest digest agreement");
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(ParserPackForestEncodingV1Summary %u %u)\n", passed, failed);
    return failed ? 1 : 0;
}
