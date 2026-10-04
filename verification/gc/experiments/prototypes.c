/* Isolated representation experiments, never linked into the evaluator.
 * The weak prototype uses the actual symbol-table implementation. Only this
 * executable has a sweep, under its explicitly enumerated handle contract.
 * Managed spellings use ref-counted byte buffers as a stand-in for arena-owned
 * symbol payloads; this is not a port of the evaluator's symbol ABI. */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../src/symbol.c"

typedef enum { WEAK, SPELLING, SPELLING_GC } Mode;
typedef struct Managed Managed;
struct Managed {
    Managed *next, *previous;
    size_t references;
    uint32_t length;
    SymbolId number;
    uint8_t bytes[];
};
typedef struct { SymbolId id; Managed *spelling; } Value;
typedef struct {
    SymbolTable table;
    Mode mode;
    Managed *managed;
    uint64_t managed_bytes, managed_overhead, allocated, since_collection;
    uint64_t collections, reclaimed, maximum_payload, maximum_metadata;
    bool bad_hash;
} Experiment;

static const char *mode_name(Mode mode) {
    return mode == WEAK ? "weak-intern" : mode == SPELLING ? "managed-spelling"
                                                     : "managed-spelling-key-gc";
}
static void initialize(Experiment *e, Mode mode) {
    memset(e, 0, sizeof(*e));
    e->mode = mode;
    symbol_table_init(&e->table);
}
static Value make_value(Experiment *e, const uint8_t *bytes, uint32_t length) {
    if (!length) return (Value){0}; /* Preserve the existing span API. */
    e->allocated += length + 1u;
    e->since_collection += length + 1u;
    if (e->mode == WEAK)
        return (Value){.id = symbol_intern_data_bytes(&e->table, bytes, length)};
    Managed *m = malloc(sizeof(*m) + length + 1u);
    assert(m);
    *m = (Managed){.next = e->managed, .references = 1, .length = length};
    if (e->managed) e->managed->previous = m;
    e->managed = m;
    memcpy(m->bytes, bytes, length);
    m->bytes[length] = 0;
    e->managed_bytes += length + 1u;
    e->managed_overhead += sizeof(*m);
    return (Value){.spelling = m};
}
static Value retain(Value v) {
    if (v.spelling) v.spelling->references++;
    return v;
}
static void release(Experiment *e, Value *v) {
    Managed *m = v->spelling;
    if (m && --m->references == 0) {
        if (m->previous) m->previous->next = m->next;
        else e->managed = m->next;
        if (m->next) m->next->previous = m->previous;
        e->managed_bytes -= m->length + 1u;
        e->managed_overhead -= sizeof(*m);
        e->reclaimed += m->length + 1u;
        free(m);
    }
    *v = (Value){0};
}
static const uint8_t *value_bytes(Experiment *e, Value v, uint32_t *length) {
    if (v.spelling) { *length = v.spelling->length; return v.spelling->bytes; }
    const SymbolEntry *entry = symbol_table_entry_ref(&e->table, v.id);
    assert(entry && entry->bytes); /* Future reads of retained weak handles. */
    *length = entry->len;
    return (const uint8_t *)entry->bytes;
}
static bool same(Experiment *e, Value a, Value b) {
    uint32_t na, nb;
    const uint8_t *pa = value_bytes(e, a, &na), *pb = value_bytes(e, b, &nb);
    return na == nb && memcmp(pa, pb, na) == 0;
}
static uint64_t hash(Experiment *e, Value v) {
    uint32_t n;
    const uint8_t *p = value_bytes(e, v, &n);
    SymbolId number = v.spelling ? v.spelling->number : v.id;
    if (e->bad_hash && number) return number;
    return symbol_hash_bytes(p, n);
}
static SymbolId number(Experiment *e, Value v) {
    if (!v.spelling) return v.id;
    if (!v.spelling->number) {
        v.spelling->number = e->mode == SPELLING
            ? symbol_intern_bytes(&e->table, v.spelling->bytes, v.spelling->length)
            : symbol_intern_data_bytes(&e->table, v.spelling->bytes, v.spelling->length);
    }
    return v.spelling->number;
}
static SymbolId lookup_span(SymbolTable *table, const uint8_t *bytes, uint32_t length) {
    uint64_t h = symbol_hash_bytes(bytes, length);
    uint32_t slot = h % table->slot_cap;
    while (table->slots[slot].id) {
        SymbolSlot s = table->slots[slot];
        if (s.hash == h && symbol_entry_matches(symbol_table_entry_ref(table, s.id),
                                               bytes, length, h)) return s.id;
        slot = (slot + 1u) % table->slot_cap;
    }
    return SYMBOL_ID_NONE;
}

/* This scanner is complete for these explicit handles, not for arbitrary C
 * runtime states. The canonicalization index is weak. IDs never move/rebind. */
static void collect(Experiment *e, Value *roots, size_t root_count, int omit) {
    SymbolShadowScan scan;
    assert(symbol_shadow_begin(&e->table, &scan));
    if (e->mode == WEAK) {
        for (size_t i = 0; i < root_count; i++)
            if ((int)i != omit) assert(symbol_shadow_mark(&scan, roots[i].id));
    } else if (e->mode == SPELLING_GC) {
        for (Managed *m = e->managed; m; m = m->next)
            assert(symbol_shadow_mark(&scan, m->number));
    }
    for (uint32_t id = 1; id < scan.entry_limit; id++) {
        SymbolEntry *entry = symbol_table_entry_mut(&e->table, id);
        if (!entry || !entry->bytes ||
            !(atomic_load(&entry->flags) & CETTA_SYMBOL_FLAG_RUNTIME_DATA) ||
            (scan.marked[id >> 3u] & (1u << (id & 7u)))) continue;
        uint64_t bytes = entry->len + 1u;
        free((void *)entry->bytes);
        entry->bytes = NULL;
        entry->len = 0;
        entry->hash = 0;
        e->table.stats.data_bytes -= bytes;
        e->table.stats.data_count--;
        e->table.stats.retired_count++;
        e->reclaimed += bytes;
    }
    uint32_t cap = SYMBOL_TABLE_MIN_SLOTS;
    uint64_t live = e->table.stats.owned_count + e->table.stats.data_count;
    while ((live + 1u) * SYMBOL_TABLE_MAX_LOAD_DEN >
           (uint64_t)cap * SYMBOL_TABLE_MAX_LOAD_NUM) cap *= 2u;
    symbol_table_resize(&e->table, cap);
    symbol_shadow_destroy(&scan);
    e->since_collection = 0;
    e->collections++;
}
static void dispose(Experiment *e) {
    assert(!e->managed);
    symbol_table_free(&e->table);
}

static int semantics(Mode mode, bool bad_hash) {
    Experiment e;
    initialize(&e, mode);
    e.bad_hash = bad_hash;
    const uint8_t binary[] = {'n', 0, 0xff, 0x80, 'x'};
    Value values[] = {
        make_value(&e, binary, sizeof(binary)), make_value(&e, binary, sizeof(binary)),
        make_value(&e, (const uint8_t *)"n", 1),
        make_value(&e, (const uint8_t *)"late-function", 13)
    };
    assert(same(&e, values[0], values[1]));
    assert(!same(&e, values[0], values[2]));
    uint64_t before = hash(&e, values[0]);
    assert(!lookup_span(&e.table, (const uint8_t *)"late-function", 13) || mode == WEAK);
    SymbolId id = number(&e, values[0]);
    assert(id);
    if (hash(&e, values[0]) != hash(&e, values[1]) || hash(&e, values[0]) != before) {
        puts("detected-mutant: ID-based hash disagrees with spelling equality/registration");
        for (size_t i = 0; i < 4; i++) release(&e, &values[i]);
        collect(&e, NULL, 0, -1); dispose(&e);
        return 1;
    }
    assert(number(&e, values[1]) == id);
    SymbolId late = number(&e, values[3]);
    assert(lookup_span(&e.table, (const uint8_t *)"late-function", 13) == late);
    Value answers[] = {retain(values[0]), retain(values[0]), retain(values[2])};
    Value roots[] = {values[0], values[1], values[2], values[3]};
    collect(&e, roots, 4, -1);
    assert(same(&e, answers[0], answers[1]));
    assert(!same(&e, answers[0], answers[2]));
    uint32_t length;
    assert(memcmp(value_bytes(&e, answers[0], &length), binary, sizeof(binary)) == 0);
    assert(length == sizeof(binary));
    for (size_t i = 0; i < 3; i++) release(&e, &answers[i]);
    for (size_t i = 0; i < 4; i++) release(&e, &values[i]);
    collect(&e, NULL, 0, -1);
    if (mode != SPELLING) assert(e.table.stats.data_bytes == 0);
    if (mode == WEAK) {
        Value returned = make_value(&e, binary, sizeof(binary));
        assert(returned.id != id); /* Reintroduced spelling, never old-ID resurrection. */
        assert(!symbol_table_entry_ref(&e.table, id)->bytes);
        release(&e, &returned); collect(&e, NULL, 0, -1);
    }
    dispose(&e);
    return 0;
}

static void snapshot(Experiment *e, size_t turns, unsigned keyed_every) {
    SymbolTableStats stats;
    symbol_table_stats_snapshot(&e->table, &stats);
    uint64_t payload = stats.owned_bytes + stats.data_bytes + e->managed_bytes;
    uint64_t metadata = stats.metadata_bytes + e->managed_overhead;
    if (payload > e->maximum_payload) e->maximum_payload = payload;
    if (metadata > e->maximum_metadata) e->maximum_metadata = metadata;
    printf("{\"mode\":\"%s\",\"turns\":%zu,\"keyed_every\":%u,"
           "\"payload_bytes\":%" PRIu64 ",\"metadata_bytes\":%" PRIu64
           ",\"owned_payload_bytes\":%" PRIu64 ",\"retired_ids\":%" PRIu64
           ",\"collections\":%" PRIu64 ",\"managed_live_bytes\":%" PRIu64 "}\n",
           mode_name(e->mode), turns, keyed_every, payload, metadata,
           stats.owned_bytes, stats.retired_count, e->collections, e->managed_bytes);
}
static void churn(Mode mode, size_t turns, unsigned keyed_every) {
    Experiment e;
    initialize(&e, mode);
    enum { ROOTS = 64, WIDTH = 4096 };
    Value roots[ROOTS] = {{0}};
    uint8_t bytes[WIDTH];
    for (size_t i = 1; i <= turns; i++) {
        memset(bytes, 'p', sizeof(bytes));
        int prefix = snprintf((char *)bytes, sizeof(bytes), "unique:%zu:", i);
        assert(prefix > 0 && prefix < WIDTH);
        bytes[prefix] = ':';
        Value value = make_value(&e, bytes, WIDTH);
        size_t slot = i % ROOTS;
        release(&e, &roots[slot]);
        roots[slot] = value;
        if (keyed_every && i % keyed_every == 0) (void)number(&e, value);
        if (e.since_collection >= ROOTS * (WIDTH + 1u)) collect(&e, roots, ROOTS, -1);
        if (i == turns / 4 || i == turns / 2 || i == turns) snapshot(&e, i, keyed_every);
        uint32_t length;
        assert(value_bytes(&e, value, &length)[0] == 'u' && length == WIDTH);
    }
    for (size_t i = 0; i < ROOTS; i++) release(&e, &roots[i]);
    collect(&e, NULL, 0, -1);
    snapshot(&e, turns + 1, keyed_every);
    if (mode != SPELLING) assert(e.table.stats.data_bytes == 0);
    dispose(&e);
}

static int omission_control(void) {
    Experiment e;
    initialize(&e, WEAK);
    Value roots[2] = {make_value(&e, (const uint8_t *)"pending-answer", 14),
                      make_value(&e, (const uint8_t *)"foreign-borrow", 14)};
    collect(&e, roots, 2, 1);
    bool detected = !symbol_table_entry_ref(&e.table, roots[1].id)->bytes;
    assert(symbol_eq_cstr(&e.table, roots[0].id, "pending-answer"));
    puts(detected ? "detected-mutant: omitted retained handle" : "missed-mutant");
    dispose(&e);
    return detected ? 1 : 0;
}

static int reuse_control(void) {
    Experiment e;
    initialize(&e, WEAK);
    Value first = make_value(&e, (const uint8_t *)"old-name", 8);
    SymbolId stale = first.id;
    release(&e, &first); collect(&e, NULL, 0, -1);
    Value next = make_value(&e, (const uint8_t *)"new-name", 8);
    /* Deliberate allocator mutant: rebind a retired ID without a generation. */
    SymbolEntry *old_entry = symbol_table_entry_mut(&e.table, stale);
    SymbolEntry *new_entry = symbol_table_entry_mut(&e.table, next.id);
    old_entry->bytes = new_entry->bytes;
    old_entry->len = new_entry->len;
    old_entry->hash = new_entry->hash;
    new_entry->bytes = NULL;
    bool detected = symbol_len(&e.table, stale) != 0;
    puts(detected ? "detected-mutant: stale ID aliases a new spelling" : "missed-mutant");
    dispose(&e);
    return detected ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "omit-root") == 0) return omission_control();
    if (argc == 2 && strcmp(argv[1], "bad-hash") == 0) return semantics(SPELLING_GC, true);
    if (argc == 2 && strcmp(argv[1], "reuse-id") == 0) return reuse_control();
    for (Mode mode = WEAK; mode <= SPELLING_GC; mode++) assert(!semantics(mode, false));
    size_t turns = argc > 1 ? strtoul(argv[1], NULL, 10) : 32768;
    assert(turns >= 256 && turns <= 1000000);
    for (unsigned keyed = 0; keyed <= 64; keyed += 64)
        for (Mode mode = WEAK; mode <= SPELLING_GC; mode++) churn(mode, turns, keyed);
    return 0;
}
