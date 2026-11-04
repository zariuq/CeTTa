/* Metadata controls for the actual bounded cache implementation. The
 * prepared-call tests separately exercise classification and mutation. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool refuse_allocations;
static unsigned allocation_attempts;
static void *checked_malloc(size_t n) {
    allocation_attempts++;
    return refuse_allocations ? NULL : malloc(n);
}
static void *checked_calloc(size_t n, size_t size) {
    allocation_attempts++;
    return refuse_allocations ? NULL : calloc(n, size);
}
#define malloc checked_malloc
#define calloc checked_calloc
#include "../src/petta_specializer.c"
#undef malloc
#undef calloc

static bool lookup(SpaceProgramToken program, SymbolId source,
                   PettaRelationRelevance expected) {
    PettaRelationRelevance got = PETTA_RELATION_RELEVANCE_CERTAIN;
    bool found = petta_relation_relevance_cache_lookup(program, source, &got);
    assert(got == expected);
    return found;
}

static void collide(SpaceProgramToken program, SymbolId found[9]) {
    SymbolId candidates[PETTA_RELATION_RELEVANCE_CACHE_BANKS][9] = {{0}};
    unsigned counts[PETTA_RELATION_RELEVANCE_CACHE_BANKS] = {0};
    /* Pigeonhole: eight per bank plus one guarantees nine colliders. */
    for (unsigned n = 0u; n <= 8u * PETTA_RELATION_RELEVANCE_CACHE_BANKS; n++) {
        char name[64];
        snprintf(name, sizeof(name), "cache-collision-%u", n);
        SymbolId source = symbol_intern_cstr(g_symbols, name);
        size_t bank = petta_relation_relevance_cache_index(program, source);
        assert(bank < PETTA_RELATION_RELEVANCE_CACHE_BANKS);
        candidates[bank][counts[bank]++] = source;
        if (counts[bank] == 9u) {
            memcpy(found, candidates[bank], sizeof(candidates[bank]));
            return;
        }
    }
    assert(false);
}

typedef struct { SymbolId source; PettaRelationRelevance value; } ReferenceEntry;
typedef struct { ReferenceEntry oldest[4]; unsigned count; } Reference;

static bool reference_get(Reference *ref, SymbolId source,
                          PettaRelationRelevance *value) {
    for (unsigned i = 0u; i < ref->count; i++) {
        if (ref->oldest[i].source != source)
            continue;
        ReferenceEntry entry = ref->oldest[i];
        memmove(&ref->oldest[i], &ref->oldest[i + 1u],
                (ref->count - i - 1u) * sizeof(entry));
        ref->oldest[ref->count - 1u] = entry;
        *value = entry.value;
        return true;
    }
    *value = PETTA_RELATION_RELEVANCE_UNKNOWN;
    return false;
}

static void reference_put(Reference *ref, SymbolId source,
                          PettaRelationRelevance value) {
    if (value == PETTA_RELATION_RELEVANCE_UNKNOWN)
        return;
    PettaRelationRelevance ignored;
    if (reference_get(ref, source, &ignored)) {
        ref->oldest[ref->count - 1u].value = value;
        return;
    }
    if (ref->count == 4u) {
        memmove(ref->oldest, &ref->oldest[1], 3u * sizeof(*ref->oldest));
        ref->count--;
    }
    ref->oldest[ref->count++] = (ReferenceEntry){source, value};
}

static void collision_and_lru(SpaceProgramToken program, SymbolId sources[9]) {
    petta_relation_relevance_cache_clear();
    for (unsigned i = 0u; i < 4u; i++)
        petta_relation_relevance_cache_store(program, sources[i],
                                              PETTA_RELATION_RELEVANCE_CERTAIN);
    for (unsigned i = 0u; i < 4u; i++)
        assert(lookup(program, sources[i], PETTA_RELATION_RELEVANCE_CERTAIN));
    assert(lookup(program, sources[0], PETTA_RELATION_RELEVANCE_CERTAIN));
    assert(lookup(program, sources[2], PETTA_RELATION_RELEVANCE_CERTAIN));
    petta_relation_relevance_cache_store(program, sources[4],
                                          PETTA_RELATION_RELEVANCE_IRRELEVANT);
    assert(!lookup(program, sources[1], PETTA_RELATION_RELEVANCE_UNKNOWN));
    assert(lookup(program, sources[0], PETTA_RELATION_RELEVANCE_CERTAIN));
    assert(lookup(program, sources[4], PETTA_RELATION_RELEVANCE_IRRELEVANT));

    petta_relation_relevance_cache_clear();
    Reference ref = {0};
    uint64_t random = UINT64_C(0x9e3779b97f4a7c15);
    refuse_allocations = true;
    allocation_attempts = 0u;
    for (unsigned step = 0u; step < 20000u; step++) {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        SymbolId source = sources[random % 9u];
        if (random & (UINT64_C(1) << 31)) {
            PettaRelationRelevance value = step % 7u == 0u
                ? PETTA_RELATION_RELEVANCE_UNKNOWN : (source & 1u)
                    ? PETTA_RELATION_RELEVANCE_CERTAIN
                    : PETTA_RELATION_RELEVANCE_IRRELEVANT;
            reference_put(&ref, source, value);
            petta_relation_relevance_cache_store(program, source, value);
        } else {
            PettaRelationRelevance value;
            bool expected = reference_get(&ref, source, &value);
            assert(lookup(program, source, value) == expected);
        }
    }
    assert(allocation_attempts == 0u);
    refuse_allocations = false;
    puts("PASS: four colliding keys coexist; actual LRU agrees with 20000 independent operations, including transient results under allocation refusal");
}

static void complete_key_guards(SpaceProgramToken program, Space *other,
                                SymbolId source, SymbolTable *other_symbols) {
    petta_relation_relevance_cache_clear();
    petta_relation_relevance_cache_store(program, source,
                                          PETTA_RELATION_RELEVANCE_CERTAIN);
    assert(lookup(program, source, PETTA_RELATION_RELEVANCE_CERTAIN));
    size_t bank = petta_relation_relevance_cache_index(program, source);
    PettaRelationRelevanceCacheSlot saved =
        g_petta_relation_relevance_cache[bank * PETTA_RELATION_RELEVANCE_CACHE_WAYS];
    assert(saved.source == source);
    for (unsigned changed = 0u; changed < 7u; changed++) {
        SpaceProgramToken query = program;
        SymbolId query_source = source;
        SymbolTable *symbols = g_symbols;
        if (changed == 0u) query.space = other;
        if (changed == 1u) query.instance_id++;
        if (changed == 2u) query.equation_revision++;
        if (changed == 3u) query.declaration_revision++;
        if (changed == 4u) query.base_dependency_epoch++;
        if (changed == 5u) g_symbols = other_symbols;
        if (changed == 6u) query_source++;
        /* Place a real old fact in the prospective bank: complete-key
         * rejection must hold independently of hash placement. */
        memset(g_petta_relation_relevance_cache, 0,
                sizeof(g_petta_relation_relevance_cache));
        size_t target = petta_relation_relevance_cache_index(query, query_source);
        g_petta_relation_relevance_cache[target * PETTA_RELATION_RELEVANCE_CACHE_WAYS] = saved;
        assert(!lookup(query, query_source, PETTA_RELATION_RELEVANCE_UNKNOWN));
        g_symbols = symbols;
    }
    petta_relation_relevance_cache_clear();
    petta_relation_relevance_cache_store(program, source,
                                          PETTA_RELATION_RELEVANCE_CERTAIN);
    petta_relation_relevance_cache_clear();
    assert(!lookup(program, source, PETTA_RELATION_RELEVANCE_UNKNOWN));
    /* A wrap must not make a generation-one resident valid again. */
    g_petta_admission_cache_generation = 1u;
    petta_relation_relevance_cache_store(program, source,
                                          PETTA_RELATION_RELEVANCE_CERTAIN);
    g_petta_admission_cache_generation = UINT64_MAX;
    petta_relation_relevance_cache_clear();
    assert(g_petta_admission_cache_generation == 1u &&
           !lookup(program, source, PETTA_RELATION_RELEVANCE_UNKNOWN));
    puts("PASS: every program-token field, source, symbol-table instance, generation change and generation wrap rejects stale facts");
}

int main(void) {
    SymbolTable first, second;
    symbol_table_init(&first);
    symbol_table_init(&second);
    g_symbols = &first;
    Space space = {0}, other = {0};
    SpaceProgramToken program = {.space = &space, .instance_id = 17u,
        .equation_revision = 5u, .declaration_revision = 3u,
        .base_dependency_epoch = 9u};
    SymbolId colliders[9];
    collide(program, colliders);
    assert(sizeof(g_petta_relation_relevance_cache) ==
           256u * sizeof(PettaRelationRelevanceCacheSlot) &&
           sizeof(g_petta_relation_relevance_recency) == 64u);
    collision_and_lru(program, colliders);
    complete_key_guards(program, &other, colliders[0], &second);
    g_symbols = NULL;
    symbol_table_free(&second);
    symbol_table_free(&first);
    puts("PASS: cache retains 256 entries plus 64 recency bytes per thread");
    return 0;
}
