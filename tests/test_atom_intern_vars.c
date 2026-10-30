#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "atom.h"
#include "symbol.h"

/* Observe allocation lifetime rather than private chunk headers. Library
 * allocations outside a measured owner lifetime are left untouched. */
typedef struct { void *pointer; size_t bytes; bool live; } OwnedAllocation;
static OwnedAllocation allocations[256];
static size_t allocation_count, free_attempts;
static bool observe_allocations;
static pthread_mutex_t allocation_mutex = PTHREAD_MUTEX_INITIALIZER;

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void __real_free(void *);

static void record_allocation(void *pointer, size_t bytes) {
    if (!observe_allocations || !pointer)
        return;
    pthread_mutex_lock(&allocation_mutex);
    assert(allocation_count < sizeof(allocations) / sizeof(*allocations));
    allocations[allocation_count++] = (OwnedAllocation){pointer, bytes, true};
    pthread_mutex_unlock(&allocation_mutex);
}
void *__wrap_malloc(size_t bytes) {
    void *pointer = __real_malloc(bytes);
    record_allocation(pointer, bytes);
    return pointer;
}
void *__wrap_calloc(size_t count, size_t bytes) {
    void *pointer = __real_calloc(count, bytes);
    record_allocation(pointer, count * bytes);
    return pointer;
}
void __wrap_free(void *pointer) {
    if (observe_allocations) {
        pthread_mutex_lock(&allocation_mutex);
        free_attempts++;
        if (pointer) {
            size_t index = allocation_count;
            while (index && !(allocations[index - 1u].live &&
                    allocations[index - 1u].pointer == pointer))
                index--;
            assert(index && "release must name one live allocation owner");
            allocations[index - 1u].live = false;
        }
        pthread_mutex_unlock(&allocation_mutex);
    }
    __real_free(pointer);
}
static void begin_ownership_observation(void) {
    assert(!observe_allocations);
    allocation_count = free_attempts = 0u;
    observe_allocations = true;
}
static void end_ownership_observation(void) {
    for (size_t index = 0u; index < allocation_count; index++)
        assert(!allocations[index].live && "every allocated chunk must retire");
    observe_allocations = false;
}

typedef struct { VarInternTable *table; VarId ids[5]; } InternWorker;
static const SymbolId sparse_spellings[] = {
    1u, (1u << 14u) - 1u, 1u << 14u, UINT32_C(0x80000000), UINT32_MAX};
static void *intern_sparse(void *context) {
    InternWorker *worker = context;
    for (unsigned round = 0u; round < 256u; round++)
        for (unsigned index = 0u; index < 5u; index++) {
            VarId id = var_intern(worker->table, sparse_spellings[index]);
            assert(!var_id_is_none(id));
            if (round)
                assert(var_id_eq(id, worker->ids[index]));
            worker->ids[index] = id;
        }
    return NULL;
}
static void test_sparse_owner_lifetime(void) {
    VarInternTable table;
    VarId earlier = VAR_ID_NONE;
    for (unsigned generation = 0u; generation < 4u; generation++) {
        begin_ownership_observation();
        var_intern_init(&table);
        InternWorker workers[8] = {0};
        pthread_t threads[8];
        for (unsigned worker = 0u; worker < 8u; worker++) {
            workers[worker].table = &table;
            assert(!pthread_create(&threads[worker], NULL, intern_sparse, &workers[worker]));
        }
        for (unsigned worker = 0u; worker < 8u; worker++)
            assert(!pthread_join(threads[worker], NULL));
        for (unsigned index = 0u; index < 5u; index++) {
            for (unsigned worker = 0u; worker < 8u; worker++)
                assert(var_id_eq(workers[0].ids[index], workers[worker].ids[index]));
            for (unsigned other = index + 1u; other < 5u; other++)
                assert(!var_id_eq(workers[0].ids[index], workers[0].ids[other]));
        }
        assert(!var_id_eq(earlier, workers[0].ids[0]));
        earlier = workers[0].ids[0];
        VarId fresh = var_intern(&table, SYMBOL_ID_NONE);
        assert(!var_id_eq(fresh, var_intern(&table, SYMBOL_ID_NONE)));
        size_t owned = allocation_count;
        size_t before = free_attempts;
        var_intern_free(&table);
        assert(!table.chunks && !table.owned_chunks);
        assert(free_attempts - before == owned);
        end_ownership_observation();
    }
    begin_ownership_observation();
    var_intern_init(&table);
    var_intern_free(&table);
    assert(free_attempts == allocation_count);
    end_ownership_observation();
    puts("PASS: sparse variable chunks retire once; concurrent identity, high IDs and repeated owners");
}

static void test_symbol_extent_exhaustion(void) {
    for (unsigned missing = 0u; missing < 2u; missing++) {
        pid_t child = fork();
        assert(child >= 0);
        if (!child) {
            assert(freopen("/dev/null", "w", stderr));
            SymbolTable table;
            symbol_table_init(&table);
            SymbolId existing = symbol_intern_cstr(&table, "existing-at-capacity");
            atomic_store_explicit(&table.entry_len, UINT32_MAX, memory_order_relaxed);
            if (missing)
                (void)symbol_intern_cstr(&table, "exhausted-extent");
            else
                assert(symbol_intern_cstr(&table, "existing-at-capacity") == existing);
            _Exit(0);
        }
        int status = 0;
        assert(waitpid(child, &status, 0) == child);
        if (missing)
            assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
        else
            assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
}

static void test_symbol_owner_lifetime(void) {
    const unsigned sizes[] = {0u, 1u, (1u << SYMBOL_ENTRY_CHUNK_BITS) - 1u,
        1u << SYMBOL_ENTRY_CHUNK_BITS, (1u << SYMBOL_ENTRY_CHUNK_BITS) + 1u};
    SymbolTable *saved = g_symbols;
    for (unsigned sample = 0u; sample < 5u; sample++) {
        begin_ownership_observation();
        SymbolTable table;
        symbol_table_init(&table);
        g_symbols = &table;
        for (unsigned index = 0u; index < sizes[sample]; index++) {
            char name[48];
            snprintf(name, sizeof(name), "owned-symbol-%u", index);
            SymbolId id = symbol_intern_cstr(&table, name);
            assert(id == index + 1u);
            if (!index || index + 1u == sizes[sample]) {
                Atom *canonical = atom_symbol_canonical(id);
                assert(canonical && canonical == atom_symbol_canonical(id));
            }
        }
        size_t before = free_attempts;
        symbol_table_free(&table);
        assert(!table.entry_chunks && !table.slots && !table.spelling_chunks &&
            !table.instance_id && atomic_load(&table.entry_len) == 0u);
        assert(free_attempts - before <= allocation_count + sizes[sample] + 8u);
        end_ownership_observation();
    }
    g_symbols = saved;
    puts("PASS: empty, exact and partial symbol chunks retire with their canonical atoms");
}

/* Interning identifies a variable by its id, its spelling and its
 * structural name.  Two variables that share an id and nothing else stay
 * two atoms, each keeping its own name, and so do the expressions over
 * them; a variable interned twice with the same identity is one atom. */

/* A full single-bucket table forces equality to decide the collision. The
 * two owning tables outlive this borrowed probe; it never inserts or frees. */
static void collision_identity(Atom *stored, Atom *candidate, bool equal) {
    Atom *bucket = stored;
    HashConsTable probe = {.table = &bucket, .size = 1u, .used = 1u};
    assert(hashcons_get(&probe, candidate) == (equal ? stored : candidate));
    assert(probe.lookup_count == 1u && probe.lookup_probes == 1u);
    assert(bucket == stored && probe.used == 1u);
}

int main(void) {
    test_sparse_owner_lifetime();
    test_symbol_owner_lifetime();
    test_symbol_extent_exhaustion();
    SymbolTable symbols;
    HashConsTable table;
    Arena arena;

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    hashcons_init(&table);
    arena_init(&arena);
    arena_set_hashcons(&arena, &table);

    SymbolId x = symbol_intern_cstr(&symbols, "x");
    SymbolId y = symbol_intern_cstr(&symbols, "y");

    /* The same identity interns to one atom. */
    Atom *first = atom_var_with_spelling(&arena, x, var_id_from_base(0u));
    assert(first != NULL && first->kind == ATOM_VAR);
    VarId id = first->var_id;
    Atom *again = atom_var_with_spelling(&arena, x, id);
    assert(again == first);

    /* The same id with another spelling is another atom, and each keeps
     * its own spelling. */
    Atom *other = atom_var_with_spelling(&arena, y, id);
    assert(other != NULL && other != first);
    assert(first->sym_id == x);
    assert(other->sym_id == y);
    assert(atom_var_with_spelling(&arena, y, id) == other);

    /* The same id and spelling with another structural name is another
     * atom too. */
    Atom *key_one = atom_symbol(&arena, "k1");
    Atom *key_two = atom_symbol(&arena, "k2");
    Atom *named_one = atom_var_with_presentation(&arena, x, key_one, id);
    Atom *named_two = atom_var_with_presentation(&arena, x, key_two, id);
    assert(named_one != NULL && named_two != NULL);
    assert(named_one != named_two);
    assert(named_one != first && named_two != first);
    assert(atom_var_with_presentation(&arena, x, key_one, id) == named_one);

    /* Expressions over them follow their children. */
    Atom *head = atom_symbol(&arena, "f");
    Atom *with_x[] = { head, first };
    Atom *with_y[] = { head, other };
    Atom *fx = atom_expr(&arena, with_x, 2u);
    Atom *fy = atom_expr(&arena, with_y, 2u);
    assert(fx != NULL && fy != NULL && fx != fy);
    assert(fx->expr.elems[1]->sym_id == x);
    assert(fy->expr.elems[1]->sym_id == y);
    assert(atom_expr(&arena, with_x, 2u) == fx);

    /* Symbols intern as before. */
    assert(atom_symbol(&arena, "f") == head);

    /* Float constructors are arena-owned. Explicit interning retains their
     * exact representation and value kind, including inside expressions. */
    Atom *positive_zero = hashcons_get(&table, atom_float(&arena, 0.0));
    Atom *negative_zero = hashcons_get(&table, atom_float(&arena, -0.0));
    assert(positive_zero != negative_zero);
    assert(hashcons_get(&table, atom_float(&arena, 0.0)) == positive_zero);
    assert(hashcons_get(&table, atom_float(&arena, -0.0)) == negative_zero);
    assert(atom_expr2(&arena, head, positive_zero) !=
           atom_expr2(&arena, head, negative_zero));
    assert(atom_expr2(&arena, head, hashcons_get(&table, atom_int(&arena, 1))) !=
           atom_expr2(&arena, head, hashcons_get(&table, atom_float(&arena, 1.0))));
    uint64_t nan_bits_one = UINT64_C(0x7ff8000000000001);
    uint64_t nan_bits_two = UINT64_C(0x7ff8000000000002);
    double nan_one, nan_two;
    _Static_assert(sizeof(nan_one) == sizeof(nan_bits_one),
                   "the atom float representation is binary64");
    memcpy(&nan_one, &nan_bits_one, sizeof(nan_one));
    memcpy(&nan_two, &nan_bits_two, sizeof(nan_two));
    Atom *nan_value_one = hashcons_get(&table, atom_float(&arena, nan_one));
    Atom *nan_value_two = hashcons_get(&table, atom_float(&arena, nan_two));
    assert(nan_value_one != nan_value_two);
    assert(hashcons_get(&table, atom_float(&arena, nan_one)) == nan_value_one);
    assert(atom_expr2(&arena, head, nan_value_one) !=
           atom_expr2(&arena, head, nan_value_two));
    assert(atom_expr2(&arena, head, nan_value_one) ==
           atom_expr2(&arena, head, nan_value_one));

    collision_identity(first, other, false);
    collision_identity(named_one, named_two, false);
    collision_identity(fx, fy, false);
    collision_identity(positive_zero, negative_zero, false);
    collision_identity(nan_value_one, nan_value_two, false);
    collision_identity(hashcons_get(&table, atom_int(&arena, 1)),
                       hashcons_get(&table, atom_float(&arena, 1.0)), false);

    /* Independent owners have different child pointers. Equal flat terms
     * still coalesce; deep terms must reach the owned comparison machine. */
    HashConsTable other_table;
    Arena other_arena;
    hashcons_init_compact(&other_table);
    arena_init(&other_arena);
    arena_set_hashcons(&other_arena, &other_table);
    Atom *other_head = atom_symbol(&other_arena, "f");
    Atom *other_x = atom_var_with_spelling(&other_arena, x, id);
    collision_identity(head, other_head, true);
    collision_identity(fx, atom_expr2(&other_arena, other_head, other_x), true);
    Atom *left = first, *right = other_x;
    Atom *different = atom_var_with_spelling(&other_arena, y, id);
    for (unsigned depth = 0u; depth < 32768u; depth++) {
        left = atom_expr2(&arena, head, left);
        right = atom_expr2(&other_arena, other_head, right);
        different = atom_expr2(&other_arena, other_head, different);
    }
    collision_identity(left, right, true);
    collision_identity(left, different, false);
    arena_free(&other_arena);
    hashcons_free(&other_table);

    arena_free(&arena);
    hashcons_free(&table);
    symbol_table_free(&symbols);
    printf("test_atom_intern_vars: ok\n");
    return 0;
}
