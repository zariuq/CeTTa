#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "atom.h"
#include "tests/test_runtime_stats_stubs.h"

#if !CETTA_MANAGED_SYMBOLS
#error "This experiment requires ENABLE_MANAGED_SYMBOLS=1"
#endif

static void expect_bytes(const Atom *atom, const char *bytes, uint32_t len) {
    assert(atom->kind == ATOM_SYMBOL);
    assert(atom_symbol_length(atom) == len);
    assert(memcmp(atom_symbol_bytes(atom), bytes, len) == 0);
}

static void *register_worker(void *result) {
    Arena arena;
    arena_init(&arena);
    Atom *value = atom_symbol_data(&arena, "concurrently-registered-value");
    *(SymbolId *)result = atom_symbol_register_id(value);
    assert(atom_symbol_current_id(value) == *(SymbolId *)result);
    arena_free(&arena);
    return NULL;
}

int main(void) {
    _Static_assert(sizeof(void *) != 8 || sizeof(Atom) == 56,
                   "Preserve the existing 64-bit Atom layout");
    SymbolTable table;
    symbol_table_init(&table);
    symbol_table_init_builtins(&table, &g_builtin_syms);
    g_symbols = &table;
    g_hashcons = NULL;
    g_var_intern = NULL;
    Arena source, destination;
    arena_init(&source);
    arena_init(&destination);
    ArenaMark empty = arena_mark(&source);
    SymbolTableStats before, after;
    symbol_table_stats_snapshot(&table, &before);
    const char bytes[] = {'s', 0, (char)0xff, (char)0x80, 'x'};
    Atom *left = atom_symbol_data_n(&source, bytes, sizeof(bytes));
    Atom *right = atom_symbol_data_n(&source, bytes, sizeof(bytes));
    Atom *prefix = atom_symbol_data_n(&source, bytes, 1);
    assert(atom_symbol_is_spelled(left));
    assert(atom_symbol_current_id(left) == SYMBOL_ID_NONE);
    assert(atom_eq(left, right));
    assert(atom_hash(left) == atom_hash(right));
    assert(!atom_eq(left, prefix));
    expect_bytes(left, bytes, sizeof(bytes));
    assert(atom_symbol_current_id(get_meta_type(&source, left)) == g_builtin_syms.symbol);
    symbol_table_stats_snapshot(&table, &after);
    assert(after.owned_count == before.owned_count && after.data_count == before.data_count);

    Atom *copied = atom_deep_copy(&destination, left);
    assert(atom_symbol_is_spelled(copied));
    assert(copied->spelling != left->spelling);
    expect_bytes(copied, bytes, sizeof(bytes));
    symbol_table_stats_snapshot(&table, &after);
    assert(after.owned_count == before.owned_count);

    Atom *known = atom_symbol_data(&source, "+");
    assert(!atom_symbol_is_spelled(known));
    assert(atom_symbol_current_id(known) == symbol_lookup_cstr(&table, "+"));

    Atom *late = atom_symbol_data(&source, "defined-after-this-value");
    assert(atom_symbol_current_id(late) == SYMBOL_ID_NONE);
    uint32_t late_hash = atom_hash(late);
    SymbolId registered = atom_symbol_register_id(late);
    assert(registered != SYMBOL_ID_NONE);
    assert(atom_symbol_current_id(late) == registered);
    Atom *numbered = atom_symbol_id(&source, registered);
    assert(atom_eq(late, numbered) && atom_hash(numbered) == late_hash);
    assert(atom_symbol_is_spelled(late)); /* Registration does not mutate a shared value. */

    Atom *blank = atom_symbol_data_n(&source, "", 0);
    expect_bytes(blank, "", 0);
    assert(atom_eq(blank, atom_symbol_id(&source, SYMBOL_ID_NONE)));
    assert(atom_hash(blank) == atom_hash(atom_symbol_id(&source, SYMBOL_ID_NONE)));

    Atom *items[] = {left, left, prefix};
    Atom *expression = atom_expr(&source, items, 3);
    assert(!(left->flags & ATOM_FLAG_HASHCONS_ELIGIBLE));
    assert(!(expression->flags & ATOM_FLAG_HASHCONS_ELIGIBLE));
    Atom *saved = atom_deep_copy(&destination, expression);
    assert(saved->expr.len == 3 && atom_eq(saved->expr.elems[0], saved->expr.elems[1]));
    assert(!atom_eq(saved->expr.elems[0], saved->expr.elems[2]));

    AtomDeepCopySession *session = atom_deep_copy_session_new(&destination);
    assert(atom_deep_copy_session_forward_region(session, &source, empty));
    Atom *forwarded = atom_deep_copy_session_copy(session, right);
    expect_bytes(forwarded, bytes, sizeof(bytes));
    expect_bytes(right, bytes, sizeof(bytes)); /* The union slot now contains forwarding. */
    atom_deep_copy_session_free(session);
    arena_free(&source);
    expect_bytes(copied, bytes, sizeof(bytes));
    expect_bytes(forwarded, bytes, sizeof(bytes));
    expect_bytes(saved->expr.elems[0], bytes, sizeof(bytes));

    pthread_t workers[4];
    SymbolId results[4] = {0};
    for (unsigned i = 0; i < 4; i++)
        assert(pthread_create(&workers[i], NULL, register_worker, &results[i]) == 0);
    for (unsigned i = 0; i < 4; i++) assert(pthread_join(workers[i], NULL) == 0);
    for (unsigned i = 1; i < 4; i++) assert(results[i] == results[0]);

    arena_free(&destination);
    AtomManagedSymbolStats accounting;
    atom_managed_symbol_stats(&accounting);
    assert(accounting.retained_count == 0u && accounting.retained_bytes == 0u);
    Arena churn;
    arena_init(&churn);
    ArenaMark origin = arena_mark(&churn);
    for (unsigned i = 0u; i < 4096u; i++) {
        char text[64];
        int len = snprintf(text, sizeof(text), "unregistered-churn-%u", i);
        (void)atom_symbol_data_n(&churn, text, (uint32_t)len);
        arena_reset(&churn, origin);
    }
    atom_managed_symbol_stats(&accounting);
    assert(accounting.retained_count == 0u && accounting.retained_bytes == 0u);
    arena_free(&churn);

    /* Exercise the real canonicalization table, rather than merely checking
     * eligibility bits. Neither transient leaves nor their parents enter it. */
    HashConsTable canonical;
    hashcons_init_compact(&canonical);
    Arena indexed;
    arena_init(&indexed);
    arena_set_hashcons(&indexed, &canonical);
    Atom *tag = atom_symbol_id(&indexed, g_builtin_syms.atom);
    uint32_t entries = canonical.used;
    uint64_t index_bytes = hashcons_index_reserved_bytes(&canonical);
    ArenaMark indexed_origin = arena_mark(&indexed);
    for (unsigned i = 0u; i < 1024u; i++) {
        char text[64];
        int len = snprintf(text, sizeof(text), "transient-index-candidate-%u", i);
        Atom *leaf = atom_symbol_data_n(&indexed, text, (uint32_t)len);
        Atom *parent = atom_expr2(&indexed, tag, leaf);
        assert(hashcons_get(&canonical, leaf) == leaf);
        assert(hashcons_get(&canonical, parent) == parent);
        assert(canonical.used == entries);
        assert(hashcons_index_reserved_bytes(&canonical) == index_bytes);
        arena_reset(&indexed, indexed_origin);
    }
    arena_free(&indexed);
    hashcons_free(&canonical);
    atom_managed_symbol_stats(&accounting);
    assert(accounting.retained_count == 0u && accounting.retained_bytes == 0u);
    symbol_table_free(&table);
    g_symbols = NULL;
    puts("managed symbol values: binary semantics, retain/copy, forwarding, late registration, concurrent identity passed");
    return 0;
}
