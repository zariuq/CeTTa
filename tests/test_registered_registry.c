#include "space.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if !CETTA_MANAGED_SYMBOLS
#error This test requires the managed-symbol engine.
#endif

static void registry_keys(Arena *arena) {
    Registry first, second;
    registry_init(&first);
    registry_init(&second);
    const char key_bytes[] = {'&', 'k', 0, (char)255, 'a'};
    Atom *name = atom_symbol_data_n(arena, key_bytes, sizeof(key_bytes));
    Atom *value = atom_int(arena, 71);
    assert(atom_symbol_current_id(name) == SYMBOL_ID_NONE);
    assert(registry_bind_symbol(&first, name, value));
    assert(first.len == 1 && first.entries[0].text_key.hold.token);
    assert(registry_lookup_symbol(&first, name) == value);
    RegisteredSymbolStats stats;
    registered_symbols_stats(first.text_keys, &stats);
    assert(stats.entries == 1 && stats.holds == 1);
    Atom *extracted = registry_entry_symbol(&first, 0, arena);
    assert(atom_eq(name, extracted));
    assert(registry_copy_entry(&second, &first, 0, value));
    assert(registry_unbind_symbol(&first, name));
    assert(!registry_lookup_symbol(&first, name));
    registered_symbols_stats(first.text_keys, &stats);
    assert(!stats.entries && !stats.payload_bytes);
    registry_free(&first);
    assert(registry_lookup_symbol(&second, name) == value);

    /* Numeric registration elsewhere must not hide an existing dynamic key,
       nor duplicate it on the next write. */
    SymbolId later = atom_symbol_register_id(name);
    assert(later != SYMBOL_ID_NONE);
    assert(registry_lookup_id(&second, later) == value);
    Atom *next = atom_int(arena, 72);
    registry_bind_id(&second, later, next);
    assert(second.len == 1 && registry_lookup_symbol(&second, name) == next);
    assert(registry_unbind_symbol(&second, atom_symbol_id(arena, later)));
    assert(!second.len);
    registry_free(&second);
    puts("PASS: registry binary keys, scope copies, last release and later numeric registration");
}

static void registry_churn(Arena *arena, SymbolTable *symbols) {
    Registry registry;
    registry_init(&registry);
    uint32_t initial = atomic_load(&symbols->entry_len);
    enum { ROUNDS = 128, WIDTH = 64, SIZE = 2048 };
    char bytes[SIZE];
    memset(bytes, 'x', sizeof(bytes));
    RegisteredSymbolStats stats = {0};
    uint64_t empty_metadata = 0;
    for (unsigned round = 0; round < ROUNDS; round++) {
        ArenaMark mark = arena_mark(arena);
        Atom *keys[WIDTH];
        for (unsigned n = 0; n < WIDTH; n++) {
            uint64_t serial = (uint64_t)round * WIDTH + n;
            memcpy(bytes, &serial, sizeof(serial));
            keys[n] = atom_symbol_data_n(arena, bytes, sizeof(bytes));
            assert(registry_bind_symbol(&registry, keys[n], atom_int(arena, serial)));
        }
        assert(registry.len == WIDTH);
        for (unsigned n = 0; n < WIDTH; n++) {
            Atom *value = registry_lookup_symbol(&registry, keys[n]);
            assert(value && value->ground.ival == (int64_t)round * WIDTH + n);
            assert(registry_unbind_symbol(&registry, keys[n]));
        }
        registered_symbols_stats(registry.text_keys, &stats);
        assert(!stats.entries && !stats.payload_bytes && !stats.holds);
        if (!round) empty_metadata = stats.metadata_bytes;
        assert(stats.metadata_bytes == empty_metadata);
        assert(registry.entries == registry.inline_entries);
        assert(registry.index_slots == registry.inline_index_slots);
        assert(atomic_load(&symbols->entry_len) == initial);
        arena_reset(arena, mark);
    }
    assert(stats.reclaimed_entries == ROUNDS * WIDTH);
    registry_free(&registry);
    printf("PASS: %u runtime registry keys collected without permanent interning\n", ROUNDS * WIDTH);
}

static void state_names(Arena *arena, SymbolTable *symbols) {
    uint32_t initial = atomic_load(&symbols->entry_len);
    for (unsigned turn = 0; turn < 128; turn++) {
        Registry worker;
        registry_init(&worker);
        ArenaMark mark = arena_mark(arena);
        char bytes[64];
        int len = snprintf(bytes, sizeof(bytes), "fresh-worker-state-%u", turn);
        Atom *name = atom_symbol_data_n(arena, bytes, len);
        Atom *value = atom_symbol_data(arena, "state payload kept through a read");
        assert(registry_state_set_symbol(&worker, name, value));
        assert(registry_state_count(&worker) == 1);
        assert(atom_eq(registry_state_lookup_symbol(&worker, name), value));
        Atom *key_copy, *stored;
        assert(registry_state_entry(&worker, 0, arena, &key_copy, &stored));
        assert(atom_eq(key_copy, name));
        Atom *escaped = atom_deep_copy(arena, stored);
        assert(registry_state_set_symbol(&worker, name, atom_int(arena, 12)));
        assert(atom_eq(escaped, value));
        registry_free(&worker);
        assert(atom_eq(escaped, value));
        assert(atomic_load(&symbols->entry_len) == initial);
        arena_reset(arena, mark);
    }
    puts("PASS: named-state ownership, value replacement and worker registry release");
}

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    g_symbols = &symbols;
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena arena;
    arena_init(&arena);
    registry_keys(&arena);
    registry_churn(&arena, &symbols);
    state_names(&arena, &symbols);
    arena_free(&arena);
    symbol_table_free(&symbols);
    return 0;
}
