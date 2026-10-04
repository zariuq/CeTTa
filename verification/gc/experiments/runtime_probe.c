/* Observation-only linker wrappers. These deliberately measure reviewed
 * retention crossings, not every raw SymbolId store in the runtime. */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "space.h"
#include "symbol.h"
#include "term_universe.h"

void gc_probe_reacquired(SymbolTable *, SymbolId);
void gc_probe_observe_pin(SymbolTable *, SymbolId);
void gc_probe_shadow_summary(void);

enum { PIN, SYNTAX, SPACE, ENCODED, REGISTRY, SITE_COUNT };
typedef struct { uint32_t bytes; uint8_t sites; } Birth;
static Birth *births;
static size_t birth_cap;
static SymbolTable *observed_table;
static uint64_t observed_instance;
static uint64_t birth_count, birth_bytes;
static _Thread_local unsigned syntax_depth;
static pthread_mutex_t probe_mutex = PTHREAD_MUTEX_INITIALIZER;

static void sync_table(SymbolTable *table) {
    uint64_t instance = symbol_table_instance_id(table);
    if (observed_table == table && observed_instance == instance) return;
    free(births);
    births = NULL;
    birth_cap = 0;
    birth_count = birth_bytes = 0;
    observed_table = table;
    observed_instance = instance;
}

static void record_birth(SymbolTable *table, SymbolId id) {
    if (!id || !(symbol_flags(table, id) & CETTA_SYMBOL_FLAG_RUNTIME_DATA)) return;
    pthread_mutex_lock(&probe_mutex);
    sync_table(table);
    if (id >= birth_cap) {
        size_t capacity = birth_cap ? birth_cap : 1024;
        while (id >= capacity) capacity *= 2;
        Birth *next = realloc(births, capacity * sizeof(*births));
        assert(next);
        memset(next + birth_cap, 0, (capacity - birth_cap) * sizeof(*next));
        births = next;
        birth_cap = capacity;
    }
    if (!births[id].bytes) {
        births[id].bytes = symbol_len(table, id) + 1;
        birth_count++;
        birth_bytes += births[id].bytes;
    }
    pthread_mutex_unlock(&probe_mutex);
}

static void record_site(SymbolTable *table, SymbolId id, unsigned site) {
    pthread_mutex_lock(&probe_mutex);
    sync_table(table);
    if (id < birth_cap && births[id].bytes) births[id].sites |= 1u << site;
    pthread_mutex_unlock(&probe_mutex);
}

static bool record_atom_symbol(SymbolId id, void *context) {
    record_site(g_symbols, id, (unsigned)(uintptr_t)context);
    return true;
}

SymbolId __real_symbol_intern_data_bytes(SymbolTable *, const uint8_t *, uint32_t);
SymbolId __wrap_symbol_intern_data_bytes(SymbolTable *table, const uint8_t *text,
                                         uint32_t length) {
    SymbolId id = __real_symbol_intern_data_bytes(table, text, length);
    gc_probe_reacquired(table, id);
    record_birth(table, id);
    return id;
}
SymbolId __real_symbol_intern_data_cstr(SymbolTable *, const char *);
SymbolId __wrap_symbol_intern_data_cstr(SymbolTable *table, const char *text) {
    SymbolId id = __real_symbol_intern_data_cstr(table, text);
    gc_probe_reacquired(table, id);
    record_birth(table, id);
    return id;
}
bool __real_symbol_pin(SymbolTable *, SymbolId);
bool __wrap_symbol_pin(SymbolTable *table, SymbolId id) {
    gc_probe_observe_pin(table, id);
    bool ok = __real_symbol_pin(table, id);
    if (ok) record_site(table, id, syntax_depth ? SYNTAX : PIN);
    return ok;
}
void __real_atom_pin_syntax_symbols(const Atom *);
void __wrap_atom_pin_syntax_symbols(const Atom *atom) {
    /* Visit even an already promoted spelling: historical data births are
     * independent of the table's current ownership flag. */
    atom_visit_syntax_symbols(atom, record_atom_symbol, (void *)(uintptr_t)SYNTAX);
    syntax_depth++;
    __real_atom_pin_syntax_symbols(atom);
    syntax_depth--;
}
void __real_space_add(Space *, Atom *);
void __wrap_space_add(Space *space, Atom *atom) {
    __real_space_add(space, atom);
    atom_visit_syntax_symbols(atom, record_atom_symbol, (void *)(uintptr_t)SPACE);
}
AtomId __real_term_universe_store_atom_id(TermUniverse *, Arena *, Atom *);
AtomId __wrap_term_universe_store_atom_id(TermUniverse *universe, Arena *arena,
                                         Atom *atom) {
    AtomId result = __real_term_universe_store_atom_id(universe, arena, atom);
    if (result != CETTA_ATOM_ID_NONE) atom_visit_syntax_symbols(atom, record_atom_symbol,
                                         (void *)(uintptr_t)ENCODED);
    return result;
}
AtomId __real_term_universe_store_atom_id_from_source_arena(
    TermUniverse *, Arena *, const Arena *, Atom *);
AtomId __wrap_term_universe_store_atom_id_from_source_arena(
    TermUniverse *universe, Arena *arena, const Arena *source, Atom *atom) {
    AtomId result = __real_term_universe_store_atom_id_from_source_arena(
        universe, arena, source, atom);
    if (result != CETTA_ATOM_ID_NONE) atom_visit_syntax_symbols(atom, record_atom_symbol,
                                         (void *)(uintptr_t)ENCODED);
    return result;
}
void __real_registry_bind_id(Registry *, SymbolId, Atom *);
void __wrap_registry_bind_id(Registry *registry, SymbolId key, Atom *value) {
    __real_registry_bind_id(registry, key, value);
    record_site(g_symbols, key, REGISTRY);
}

void __real_symbol_table_stats_snapshot(SymbolTable *, SymbolTableStats *);
void __wrap_symbol_table_stats_snapshot(SymbolTable *table, SymbolTableStats *out) {
    __real_symbol_table_stats_snapshot(table, out);
    gc_probe_shadow_summary();
    if (!getenv("CETTA_GC_PROBE_DUMP")) return;
    uint64_t counts[SITE_COUNT] = {0}, bytes[SITE_COUNT] = {0};
    uint64_t retained_count = 0, retained_bytes = 0;
    pthread_mutex_lock(&probe_mutex);
    sync_table(table);
    for (size_t id = 1; id < birth_cap; id++) {
        Birth birth = births[id];
        if (birth.sites) { retained_count++; retained_bytes += birth.bytes; }
        for (unsigned site = 0; site < SITE_COUNT; site++) {
            if (birth.sites & (1u << site)) {
                counts[site]++; bytes[site] += birth.bytes;
            }
        }
    }
    fprintf(stderr, "gc-key-probe: {\"data_birth_count\":%" PRIu64
            ",\"data_birth_bytes\":%" PRIu64 ",\"retained_count\":%" PRIu64
            ",\"retained_bytes\":%" PRIu64 ",\"sites\":[",
            birth_count, birth_bytes, retained_count, retained_bytes);
    static const char *names[] = {"explicit_pin", "registered_syntax",
        "space_add", "encoded_store", "registry_key"};
    for (unsigned site = 0; site < SITE_COUNT; site++) {
        fprintf(stderr, "%s{\"site\":\"%s\",\"count\":%" PRIu64
                ",\"bytes\":%" PRIu64 "}", site ? "," : "", names[site],
                counts[site], bytes[site]);
    }
    fputs("]}\n", stderr);
    pthread_mutex_unlock(&probe_mutex);
}
