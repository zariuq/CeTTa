/* Isolated archive collector using actual Arena, TermUniverse and Space
 * owners. Publication copies the current ordered fact projection. Retired
 * generations are freed only after every counted reader releases its lease.
 * The evaluator does not yet use this protocol. No SymbolId is reclaimed. */
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atom.h"
#include "space.h"
#include "term_universe.h"

enum { REGION_COUNT = 4, LIVE_WINDOW = 8, POLL_EVERY = 32, PAYLOAD = 4096 };
typedef struct {
    Arena arena;
    TermUniverse universe;
    Space space;
    uint64_t generation;
    unsigned leases;
    bool initialized, active;
} Region;
typedef struct {
    Region regions[REGION_COUNT];
    unsigned current;
    uint64_t publications, collections, released;
    bool omit_lease, reverse_copy;
} Regions;
typedef struct {
    unsigned slot;
    uint64_t generation;
    bool held;
    SpaceOccurrenceCursor cursor;
} Reader;

static Region *reader_region(Regions *store, const Reader *reader) {
    if (!reader->held || reader->slot >= REGION_COUNT) return NULL;
    Region *r = &store->regions[reader->slot];
    return r->initialized && r->generation == reader->generation && r->leases
        ? r : NULL;
}

static bool acquire_reader(Regions *store, Reader *reader, Atom *pattern) {
    Region *r = &store->regions[store->current];
    if (r->leases == UINT32_MAX || !r->initialized) return false;
    space_occurrence_cursor_init_empty(&reader->cursor);
    if (!space_occurrence_cursor_init(&r->space, pattern, &reader->cursor))
        return false;
    if (!store->omit_lease) r->leases++;
    reader->slot = store->current;
    reader->generation = r->generation;
    reader->held = true;
    return true;
}

static bool clone_reader(Regions *store, const Reader *source, Reader *copy) {
    Region *r = reader_region(store, source);
    if (!r || r->leases == UINT32_MAX) return false;
    space_occurrence_cursor_init_empty(&copy->cursor);
    if (!space_occurrence_cursor_clone(&source->cursor, &copy->cursor))
        return false;
    r->leases++;
    copy->slot = source->slot;
    copy->generation = source->generation;
    copy->held = true;
    return true;
}

static void release_reader(Regions *store, Reader *reader) {
    if (!reader->held) return;
    Region *r = reader_region(store, reader);
    /* The omission control is detected before any dangling payload read.
     * The cursor's detached occurrence arrays can still be released. */
    space_occurrence_cursor_release(&reader->cursor);
    if (r) r->leases--;
    reader->held = false;
}

static Atom *reader_next(Regions *store, Reader *reader) {
    if (!reader_region(store, reader)) return NULL;
    CettaIndex index;
    if (space_occurrence_cursor_next(&reader->cursor, &index) !=
        SPACE_OCCURRENCE_CURSOR_ITEM) return NULL;
    return space_occurrence_cursor_atom(&reader->cursor, index);
}

static bool create_region(Region *r) {
    assert(!r->initialized);
    if (r->generation == UINT64_MAX) return false;
    r->generation++;
    arena_init(&r->arena);
    arena_set_block_capacity(&r->arena, 16384u);
    term_universe_init(&r->universe);
    term_universe_set_persistent_arena(&r->universe, &r->arena);
    space_init_with_universe(&r->space, &r->universe);
    assert(space_match_backend_try_set(&r->space, SPACE_ENGINE_NATIVE));
    r->initialized = true;
    r->active = false;
    r->leases = 0;
    return true;
}

static void free_region(Region *r) {
    space_free(&r->space);
    term_universe_free(&r->universe);
    arena_free(&r->arena);
    r->initialized = false;
}

static void collect_regions(Regions *store) {
    store->collections++;
    for (unsigned i = 0; i < REGION_COUNT; i++) {
        Region *r = &store->regions[i];
        if (!r->initialized || r->active || r->leases) continue;
        free_region(r);
        store->released++;
    }
}

static bool publish(Regions *store) {
    collect_regions(store);
    unsigned slot = REGION_COUNT;
    for (unsigned i = 0; i < REGION_COUNT; i++) {
        if (!store->regions[i].initialized &&
            store->regions[i].generation != UINT64_MAX) {
            slot = i;
            break;
        }
    }
    /* All remaining generations are active or leased: explicit backpressure,
     * not an unbounded allocation of extra region slots. */
    if (slot == REGION_COUNT) return false;
    Region *old = &store->regions[store->current];
    Region *next = &store->regions[slot];
    if (!create_region(next)) return false;
    CettaCount length = space_length64(&old->space);
    for (CettaIndex i = 0; i < length; i++) {
        CettaIndex source = store->reverse_copy ? length - i - 1 : i;
        Atom *value = space_get_at64(&old->space, source);
        assert(value);
        space_add(&next->space, value);
    }
    assert(space_length64(&next->space) == length);
    old->active = false;
    next->active = true;
    store->current = slot;
    store->publications++;
    collect_regions(store);
    return true;
}

static void init_regions(Regions *store) {
    assert(create_region(&store->regions[0]));
    store->regions[0].active = true;
}

static void destroy_regions(Regions *store) {
    for (unsigned i = 0; i < REGION_COUNT; i++) {
        Region *r = &store->regions[i];
        assert(!r->leases);
        if (r->initialized) free_region(r);
    }
}

static void add_value(Regions *store, Atom *value) {
    Region *r = &store->regions[store->current];
    CettaCount before = space_length64(&r->space);
    space_add(&r->space, value);
    assert(space_length64(&r->space) == before + 1);
}

static bool semantics(bool omit_lease, bool reverse_copy) {
    Regions store = {.omit_lease = omit_lease, .reverse_copy = reverse_copy};
    init_regions(&store);
    Arena source;
    arena_init(&source);
    const char bytes[] = {'a', 0, (char)0xff};
    Atom *a = atom_symbol_data_n(&source, bytes, sizeof(bytes));
    Atom *b = atom_symbol_data(&source, "region-second");
    Atom *pattern = atom_var(&source, "region-any");
    add_value(&store, a);
    add_value(&store, b);
    add_value(&store, a);
    Reader reader = {0}, copy = {0};
    assert(acquire_reader(&store, &reader, pattern));
    TermUniverseRootToken token;
    Region *first = &store.regions[0];
    assert(term_universe_root_token_capture(&first->universe,
        space_get_atom_id_at64(&first->space, 0), &token));

    /* The cursor's captured duplicate sequence survives removal. Publication
     * preserves the current authoritative sequence, including the removal
     * operation's own ordering policy; it does not impose a new one. */
    assert(space_remove(&first->space, a));
    Atom *expected0 = atom_deep_copy(&source, space_get_at64(&first->space, 0));
    Atom *expected1 = atom_deep_copy(&source, space_get_at64(&first->space, 1));
    assert(publish(&store));
    Region *current = &store.regions[store.current];
    bool ordered = atom_eq(space_get_at64(&current->space, 0), expected0) &&
                   atom_eq(space_get_at64(&current->space, 1), expected1);
    if (!ordered) {
        puts("detected-mutant: publication changed ordered occurrence projection");
        release_reader(&store, &reader);
        destroy_regions(&store);
        arena_free(&source);
        return false;
    }
    if (!reader_region(&store, &reader)) {
        puts("detected-mutant: uncounted reader lost its retired archive");
        release_reader(&store, &reader);
        destroy_regions(&store);
        arena_free(&source);
        return false;
    }
    assert(!term_universe_root_token_resolve(token, &current->universe));
    assert(term_universe_root_token_resolve(token, &first->universe));
    Atom *answer = reader_next(&store, &reader);
    assert(answer && atom_eq(answer, a));
    assert(clone_reader(&store, &reader, &copy));
    release_reader(&store, &reader);
    collect_regions(&store);
    assert(reader_region(&store, &copy));
    assert(atom_eq(reader_next(&store, &copy), b));
    answer = reader_next(&store, &copy);
    assert(answer && atom_symbol_length(answer) == sizeof(bytes));
    assert(!memcmp(atom_symbol_bytes(answer), bytes, sizeof(bytes)));
    assert(!reader_next(&store, &copy));
    release_reader(&store, &copy);
    collect_regions(&store);
    assert(!first->initialized);
    assert(publish(&store));
    current = &store.regions[store.current];
    assert(!term_universe_root_token_resolve(token, &current->universe));
    destroy_regions(&store);
    arena_free(&source);

    /* Bounded slots must refuse a new publication while all are leased. */
    Regions full = {0};
    init_regions(&full);
    arena_init(&source);
    pattern = atom_var(&source, "region-any");
    Reader readers[REGION_COUNT] = {0};
    for (unsigned i = 0; i < REGION_COUNT; i++) {
        assert(acquire_reader(&full, &readers[i], pattern));
        if (i + 1 < REGION_COUNT) assert(publish(&full));
    }
    assert(!publish(&full));
    release_reader(&full, &readers[0]);
    assert(publish(&full));
    for (unsigned i = 1; i < REGION_COUNT; i++)
        release_reader(&full, &readers[i]);
    destroy_regions(&full);
    arena_free(&source);
    return true;
}

typedef struct {
    uint64_t payload, arena_used, arena_reserved, entry_bytes, blob_reserved;
    unsigned regions, leased;
} Gauges;
static Gauges gauges(const Regions *store) {
    Gauges out = {0};
    for (unsigned i = 0; i < REGION_COUNT; i++) {
        const Region *r = &store->regions[i];
        if (!r->initialized) continue;
        out.regions++;
        if (r->leases) out.leased++;
        out.payload += r->arena.managed_symbol_bytes;
        out.arena_used += arena_accounted_live_bytes(&r->arena);
        out.arena_reserved += r->arena.reserved_bytes;
        out.entry_bytes += r->universe.cap * sizeof(TermEntry);
        out.blob_reserved += r->universe.blob_cap;
    }
    return out;
}
static void print_gauges(const Regions *store, unsigned step) {
    Gauges g = gauges(store);
    printf("region-snapshot: {\"step\":%u,\"regions\":%u,\"leased_regions\":%u,"
        "\"managed_payload\":%" PRIu64 ",\"arena_used\":%" PRIu64 ","
        "\"arena_reserved\":%" PRIu64 ",\"entry_bytes\":%" PRIu64 ","
        "\"blob_reserved\":%" PRIu64 ",\"released\":%" PRIu64 "}\n",
        step, g.regions, g.leased, g.payload, g.arena_used, g.arena_reserved,
        g.entry_bytes, g.blob_reserved, store->released);
}

int main(int argc, char **argv) {
    if (!CETTA_MANAGED_SYMBOLS) {
        fputs("region trial requires ENABLE_MANAGED_SYMBOLS=1\n", stderr);
        return 2;
    }
    bool omit = argc == 2 && !strcmp(argv[1], "omit-lease");
    bool reverse = argc == 2 && !strcmp(argv[1], "reverse-copy");
    if (argc > 2 || (argc == 2 && !omit && !reverse)) return 2;
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    bool good = semantics(omit, reverse);
    if (omit || reverse) {
        symbol_table_free(&symbols);
        g_symbols = NULL;
        return good ? 3 : 1;
    }
    assert(good);
    Regions store = {0};
    init_regions(&store);
    Arena source;
    arena_init(&source);
    Reader old = {0};
    char bytes[PAYLOAD];
    memset(bytes, 'x', sizeof(bytes));
    Gauges first = {0}, last = {0};
    for (unsigned i = 1; i <= 32768; i++) {
        ArenaMark mark = arena_mark(&source);
        snprintf(bytes, 32, "region-value-%08u", i);
        bytes[64] = 0;
        bytes[65] = (char)0xff;
        add_value(&store, atom_symbol_data_n(&source, bytes, sizeof(bytes)));
        Region *r = &store.regions[store.current];
        if (space_length64(&r->space) > LIVE_WINDOW) {
            const uint8_t oldest[LIVE_WINDOW + 1] = {1};
            CettaCount removed = 0;
            assert(space_remove_occurrence_mask_stable(&r->space, oldest,
                LIVE_WINDOW + 1, &removed));
            assert(removed == 1);
        }
        if (i == POLL_EVERY)
            assert(acquire_reader(&store, &old, atom_var(&source, "region-any")));
        if (i % POLL_EVERY == 0) assert(publish(&store));
        arena_reset(&source, mark);
        if (i == 8192 || i == 32768) {
            print_gauges(&store, i);
            if (i == 8192) first = gauges(&store);
            else last = gauges(&store);
        }
    }
    assert(first.payload == last.payload);
    assert(first.arena_reserved == last.arena_reserved);
    assert(first.entry_bytes == last.entry_bytes);
    assert(first.blob_reserved == last.blob_reserved);
    assert(first.regions == 2 && last.regions == 2);
    assert(last.leased == 1 && store.released >= 1023);
    /* A reader deliberately kept for the entire churn still sees its original
     * eight binary values, not the latest contents or recycled storage. */
    for (unsigned i = 25; i <= 32; i++) {
        Atom *answer = reader_next(&store, &old);
        assert(answer && atom_symbol_length(answer) == PAYLOAD);
        snprintf(bytes, 32, "region-value-%08u", i);
        assert(!memcmp(atom_symbol_bytes(answer), bytes, sizeof(bytes)));
    }
    assert(!reader_next(&store, &old));
    release_reader(&store, &old);
    collect_regions(&store);
    print_gauges(&store, 32769);
    destroy_regions(&store);
    assert(gauges(&store).payload == 0);
    arena_free(&source);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: actual archive regions, counted cursors, binary duplicates and bounded churn");
    return 0;
}
