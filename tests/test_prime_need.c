#include <stdio.h>

#include "atom.h"
#include "prime_need.h"
#include "symbol.h"

static unsigned checks = 0u;
static unsigned failures = 0u;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

int main(void) {
    SymbolTable symbols;
    Arena arena;
    Arena promoted_arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    arena_init(&promoted_arena);

    PrimeNeedSnapshot root;
    prime_need_snapshot_init(&root);
    CHECK(!prime_need_snapshot_present(&root), "empty snapshot is absent");
    CHECK(prime_need_snapshot_begin(&root), "snapshot session begins");
    CHECK(prime_need_snapshot_present(&root), "begun snapshot is present");

    PrimeNeedSnapshot thunk;
    uint64_t thunk_id = 0u;
    Atom *term = atom_symbol(&arena, "delayed");
    CHECK(prime_need_snapshot_allocate(&arena, &root, term, &thunk,
                                       &thunk_id),
          "allocate extends the root");
    CHECK(prime_need_snapshot_is_ancestor(&root, &thunk),
          "root precedes allocation");
    PrimeNeedCellView cell;
    CHECK(prime_need_snapshot_lookup(&thunk, thunk_id, &cell) &&
              cell.state == PRIME_NEED_UNEVALUATED && cell.payload == term,
          "allocation stores the delayed term");

    PrimeNeedSnapshot blackhole;
    CHECK(prime_need_snapshot_update(&arena, &thunk, thunk_id,
                                     PRIME_NEED_BLACKHOLE, NULL,
                                     &blackhole),
          "force installs a blackhole");
    CHECK(prime_need_snapshot_lookup(&blackhole, thunk_id, &cell) &&
              cell.state == PRIME_NEED_BLACKHOLE,
          "newest cell update wins");

    PrimeNeedSnapshot faulted;
    Atom *fault_value = atom_symbol(&arena, "fault");
    CHECK(prime_need_snapshot_update(&arena, &blackhole, thunk_id,
                                     PRIME_NEED_FAULT, fault_value,
                                     &faulted),
          "a forced fault is memoized as a cell update");
    PrimeNeedCellView repeated_fault;
    CHECK(prime_need_snapshot_lookup(&faulted, thunk_id, &cell) &&
              prime_need_snapshot_lookup(&faulted, thunk_id,
                                         &repeated_fault) &&
              cell.state == PRIME_NEED_FAULT &&
              repeated_fault.state == PRIME_NEED_FAULT &&
              cell.payload == repeated_fault.payload,
          "repeated fault lookup returns the cached receipt");

    PrimeNeedSnapshot left;
    PrimeNeedSnapshot right;
    Atom *left_value = atom_symbol(&arena, "left");
    Atom *right_value = atom_symbol(&arena, "right");
    CHECK(prime_need_snapshot_update(&arena, &blackhole, thunk_id,
                                     PRIME_NEED_VALUE, left_value, &left),
          "left branch memoizes its value");
    CHECK(prime_need_snapshot_update(&arena, &blackhole, thunk_id,
                                     PRIME_NEED_VALUE, right_value, &right),
          "right branch memoizes its value");
    CHECK(!prime_need_snapshot_is_ancestor(&left, &right) &&
              !prime_need_snapshot_is_ancestor(&right, &left),
          "sibling branch refinements are incomparable");
    PrimeNeedSnapshot rejected_merge = left;
    CHECK(!prime_need_snapshot_merge(&rejected_merge, &right),
          "sibling heaps cannot merge");
    CHECK(prime_need_snapshot_lookup(&rejected_merge, thunk_id, &cell) &&
              cell.payload == left_value,
          "failed merge leaves destination unchanged");

    PrimeNeedSnapshot ancestor_merge = thunk;
    CHECK(prime_need_snapshot_merge(&ancestor_merge, &left) &&
              prime_need_snapshot_lookup(&ancestor_merge, thunk_id, &cell) &&
              cell.payload == left_value,
          "ancestor merge selects the descendant");
    CHECK(prime_need_snapshot_merge(&ancestor_merge, &thunk) &&
              prime_need_snapshot_lookup(&ancestor_merge, thunk_id, &cell) &&
              cell.payload == left_value,
          "merging an ancestor cannot rewind a branch");

    PrimeNeedSnapshot promoted = left;
    CHECK(prime_need_snapshot_promote(&promoted_arena, &promoted),
          "snapshot promotes across arena ownership");
    CHECK(prime_need_snapshot_is_ancestor(&thunk, &promoted) &&
              prime_need_snapshot_lookup(&promoted, thunk_id, &cell) &&
              atom_is_symbol(cell.payload, "left"),
          "promotion preserves lineage and payload");

    Atom *ref = prime_need_ref(&arena, &left, thunk_id);
    uint64_t parsed_id = 0u;
    CHECK(ref && prime_need_ref_belongs_to(ref, &left, &parsed_id) &&
              parsed_id == thunk_id,
          "reference round-trips within its session");
    PrimeNeedSnapshot other_root;
    prime_need_snapshot_init(&other_root);
    CHECK(prime_need_snapshot_begin(&other_root) &&
              !prime_need_ref_belongs_to(ref, &other_root, NULL),
          "reference cannot cross a session boundary");
    Atom *forged = atom_expr3(
        &arena, atom_symbol(&arena, "__prime_need_ref_v1"),
        atom_int(&arena, -1), atom_int(&arena, 1));
    CHECK(!prime_need_ref_parse(forged, NULL, NULL),
          "malformed reference fails closed");
    CHECK(!prime_need_snapshot_update(&arena, &left, thunk_id + 1u,
                                      PRIME_NEED_VALUE, left_value,
                                      &right),
          "unknown thunk update fails closed");

    printf("(PrimeNeedAlgebraSummary %u %u %u)\n",
           checks, checks - failures, failures);
    arena_free(&promoted_arena);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u && checks == 23u ? 0 : 1;
}
