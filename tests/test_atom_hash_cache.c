#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include "atom.h"
#include "symbol.h"
#include "tests/test_runtime_stats_stubs.h"

/* An integer leaf hashes as ((5381 * 33 ^ kind) * 33 ^ gkind) * 33 ^ low32,
 * so the leaf whose low word is that prefix times 33 folds to 0. */
static uint32_t integer_leaf_prefix(void) {
    uint32_t h = 5381u;
    h = (h * 33u) ^ (uint32_t)ATOM_GROUNDED;
    h = (h * 33u) ^ (uint32_t)GV_INT;
    return h * 33u;
}

/* An integer whose leaf hash, before spelling, is `target`. */
static Atom *integer_hashing_to(Arena *arena, uint32_t target) {
    return atom_int(arena, (int64_t)(integer_leaf_prefix() ^ target));
}

/* The inverse of the finish an expression's hash takes after its fold. */
static uint32_t hash_unfinish(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7ed1b41du;
    h ^= (h >> 13) ^ (h >> 26);
    h *= 0xa5cb9243u;
    h ^= h >> 16;
    return h;
}

static Atom *fresh_copy(Arena *arena, Atom *atom) {
    if (atom->kind != ATOM_EXPR)
        return atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_INT
            ? atom_int(arena, atom->ground.ival) : atom;
    Atom *elems[8];
    assert(atom->expr.len <= 8u);
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++)
        elems[i] = fresh_copy(arena, atom->expr.elems[i]);
    return atom_expr(arena, elems, atom->expr.len);
}

/* A hash whose computation folds to 0 is spelled as one nonzero word, and
 * a cached node contributes to its parent what a recomputed one does. */
static void test_zero_spelling(Arena *arena) {
    Atom *zero = integer_hashing_to(arena, 0u);
    uint32_t spelled = atom_hash(zero);
    assert(spelled != 0u);
    assert(atom_hash_cache_load(zero) == spelled);
    assert(atom_hash(integer_hashing_to(arena, 0u)) == spelled);

    Atom *f = atom_symbol(arena, "f");
    Atom *cached_first = atom_expr2(arena, f, zero);
    Atom *uncached = atom_expr2(arena, f, integer_hashing_to(arena, 0u));
    assert(atom_hash(cached_first) == atom_hash(uncached));

    /* (f n) folds to X ^ hash(n) for a fixed X before its finish; a child
     * hashing to X makes the expression itself fold to 0. */
    Atom *probe = atom_expr2(arena, f, atom_int(arena, 7));
    uint32_t x = hash_unfinish(atom_hash(probe)) ^
        atom_hash(probe->expr.elems[1]);
    Atom *zero_expr = atom_expr2(arena, f, integer_hashing_to(arena, x));
    uint32_t zero_expr_hash = atom_hash(zero_expr);
    assert(zero_expr_hash == spelled);

    /* The same nesting hashed bottom up (each node cached first) and in one
     * fold over fresh nodes. */
    Atom *g = atom_symbol(arena, "g");
    Atom *deep = zero_expr;
    for (int level = 0; level < 40; level++)
        deep = atom_expr2(arena, g, deep);
    Atom *walk = zero_expr;
    for (int level = 0; level < 40; level++) {
        walk = atom_expr2(arena, g, walk);
        (void)atom_hash(walk);
    }
    assert(atom_hash(fresh_copy(arena, deep)) == atom_hash(walk));
}

enum { SHARED_ATOMS = 4096, HASHING_THREADS = 8 };

typedef struct {
    Atom **atoms;
    uint32_t hashes[SHARED_ATOMS];
    unsigned offset;
} HashingThread;

static void *hash_shared(void *raw) {
    HashingThread *thread = raw;
    for (unsigned i = 0u; i < SHARED_ATOMS; i++) {
        unsigned index = (i + thread->offset) % SHARED_ATOMS;
        thread->hashes[index] = atom_hash(thread->atoms[index]);
    }
    return NULL;
}

/* Threads that hash the same unhashed atoms agree with each other and with
 * a sequential pass over equal fresh atoms. */
static void test_shared_hashing(Arena *arena) {
    static Atom *atoms[SHARED_ATOMS];
    static HashingThread threads[HASHING_THREADS];
    Atom *pair = atom_symbol(arena, "pair");
    for (unsigned i = 0u; i < SHARED_ATOMS; i++) {
        Atom *inner = atom_expr2(arena, pair, atom_int(arena, (int64_t)i));
        atoms[i] = atom_expr3(arena, pair, inner,
                              atom_expr2(arena, pair, inner));
    }
    pthread_t ids[HASHING_THREADS];
    for (unsigned t = 0u; t < HASHING_THREADS; t++) {
        threads[t].atoms = atoms;
        threads[t].offset = t * (SHARED_ATOMS / HASHING_THREADS);
        assert(pthread_create(&ids[t], NULL, hash_shared, &threads[t]) == 0);
    }
    for (unsigned t = 0u; t < HASHING_THREADS; t++)
        assert(pthread_join(ids[t], NULL) == 0);
    for (unsigned i = 0u; i < SHARED_ATOMS; i++) {
        uint32_t expected = atom_hash(fresh_copy(arena, atoms[i]));
        for (unsigned t = 0u; t < HASHING_THREADS; t++)
            assert(threads[t].hashes[i] == expected);
        assert(atom_hash_cache_load(atoms[i]) == expected);
    }
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    arena_set_runtime_kind(&arena, CETTA_ARENA_RUNTIME_KIND_EVAL);

    test_zero_spelling(&arena);
    test_shared_hashing(&arena);

    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: a computed zero hash is spelled alike cached or folded, and threads hashing shared atoms agree");
    return 0;
}
