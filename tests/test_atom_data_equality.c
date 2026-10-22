#include <stdio.h>
#include <math.h>
#include "atom.h"
#include "symbol.h"

static unsigned checks, failures;
#define CHECK(c, label) do { checks++; if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", label); failures++; } } while (0)

static void retain_identifier(void *raw) { (*(unsigned *)raw)++; }
static void release_identifier(void *raw) { (*(unsigned *)raw)--; }

static Atom *shared(Arena *arena, Atom *leaf, unsigned depth) {
    Atom *head = atom_symbol(arena, "Pair");
    for (unsigned i = 0; i < depth; ++i)
        leaf = atom_expr3(arena, head, leaf, leaf);
    return leaf;
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

    Atom *leaves[] = {
        atom_symbol(&arena, "A"), atom_symbol(&arena, "B"),
        atom_var(&arena, "x"), atom_var(&arena, "y"),
        atom_int(&arena, 0), atom_int(&arena, 1), atom_float(&arena, 1.0),
        atom_float(&arena, -0.0), atom_float(&arena, 0.0),
        atom_bool(&arena, true), atom_bool(&arena, false),
        atom_string(&arena, "A"), atom_string(&arena, "B"),
        atom_bigint(&arena, "123456789012345678901234567890"),
        atom_bigint(&arena, "123456789012345678901234567891"),
#if CETTA_BUILD_WITH_GMP
        atom_rational(&arena, "2/3"), atom_rational(&arena, "4/6"),
#endif
    };
    const size_t n = sizeof(leaves) / sizeof(leaves[0]);
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = 0; j < n; ++j) {
            CHECK(atom_data_equal(leaves[i], leaves[j]) == atom_eq(leaves[i], leaves[j]) &&
                      atom_data_equal_validated(leaves[i], leaves[j]) == atom_eq(leaves[i], leaves[j]),
                  "scalar equality agrees with the established atom comparison");
            Atom *a = shared(&arena, leaves[i], 4u);
            Atom *b = shared(&arena, leaves[j], 4u);
            CHECK(atom_data_equal(a, b) == atom_eq(a, b) &&
                      atom_data_equal_validated(a, b) == atom_eq(a, b),
                  "small graph equality agrees with recursive structural comparison");
        }
    }
    Atom *nan_left = atom_float(&arena, NAN);
    Atom *nan_right = atom_float(&arena, NAN);
    CHECK(atom_data_equal(nan_left, nan_left),
          "NaN pointer reflexivity follows the established scalar comparison");
    CHECK(!atom_data_equal(nan_left, nan_right),
          "distinct NaN leaves follow the established scalar comparison");
    Atom *left = shared(&arena, leaves[0], 100000u);
    Atom *right = shared(&arena, leaves[0], 100000u);
    Atom *wrong = shared(&arena, leaves[1], 100000u);
    CHECK(atom_data_equal(left, right) && atom_data_equal_validated(left, right),
          "deep shared graphs compare without recursion or tree unfolding");
    CHECK(!atom_data_equal(left, wrong) && !atom_data_equal_validated(left, wrong),
          "a deep unequal leaf is still observed");
    Atom *a = atom_expr3(&arena, leaves[0], left, left);
    Atom *b = atom_expr3(&arena, leaves[0], right, wrong);
    CHECK(!atom_data_equal(a, b) && !atom_data_equal_validated(a, b),
          "memoization must remember both coordinates");
    Atom *child = atom_expr2(&arena, leaves[0], leaves[1]);
    Atom *small = shared(&arena, child, 1u);
    Atom *unshared = atom_expr3(&arena, atom_symbol(&arena, "Pair"),
        atom_expr2(&arena, leaves[0], leaves[1]),
        atom_expr2(&arena, leaves[0], leaves[1]));
    CHECK(atom_data_equal(small, unshared), "sharing is not observable in data equality");
    Atom *cycle = atom_expr2(&arena, leaves[0], leaves[0]);
    cycle->expr.elems[1] = cycle;
    CHECK(!atom_data_equal(cycle, cycle), "pointer identity cannot certify a cyclic graph");
    Atom *cycle2 = atom_expr2(&arena, leaves[0], leaves[0]);
    cycle2->expr.elems[1] = cycle2;
    CHECK(!atom_data_equal(cycle, cycle2), "distinct equal-looking cycles are rejected");
    Atom *other_cycle = atom_expr2(&arena, leaves[0], cycle2);
    cycle2->expr.elems[1] = other_cycle;
    CHECK(!atom_data_equal(cycle, cycle2), "cycles of different periods are rejected");
    CHECK(!atom_data_equal(NULL, NULL), "null is not an atom-data value");
    Atom *handle = atom_space(&arena, &arena);
    CHECK(!atom_data_equal(handle, handle), "runtime handles are outside scalar data");
    Atom *with_handle = atom_expr2(&arena, leaves[0], handle);
    CHECK(!atom_data_equal(with_handle, with_handle), "sharing cannot conceal a runtime handle");
    Atom *changeable = atom_expr2(&arena, leaves[0], leaves[0]);
    Atom *before = atom_expr2(&arena, leaves[0], leaves[0]);
    CHECK(atom_data_equal(changeable, before), "first independent comparison succeeds");
    changeable->expr.elems[1] = leaves[1];
    CHECK(!atom_data_equal(changeable, before), "results are not cached between calls");

    CHECK(atom_data_identical(left, right) && !atom_data_identical(left, wrong),
          "exact keys use the shared finite DAG walker without tree unfolding");
    CHECK(atom_data_identical(small, unshared),
          "exact keys preserve data while ignoring expression sharing");
    CHECK(!atom_data_identical(atom_int(&arena, 1), atom_float(&arena, 1.0)),
          "exact keys distinguish numeric kinds");
    CHECK(!atom_data_identical(atom_float(&arena, -0.0), atom_float(&arena, 0.0)),
          "exact keys distinguish the two float zeros");
    CHECK(atom_data_identical(nan_left, nan_right),
          "identical NaN representations are data keys independent of value equality");
    CHECK(!atom_data_identical(atom_symbol(&arena, "True"), atom_bool(&arena, true)),
          "an ambiguous printed spelling is not an exact key");
    Atom *variable = atom_var(&arena, "exactVariable");
    CHECK(atom_data_identical(variable, atom_deep_copy(&arena, variable)),
          "copying an exact variable key preserves identity and spelling");
    CHECK(!atom_data_identical(variable,
              atom_var_with_id(&arena, "anotherSpelling", variable->var_id)),
          "exact variable keys retain their spelling");
    CHECK(!atom_data_identical(variable,
              atom_var_with_id(&arena, "exactVariable", fresh_var_id())),
          "equal printed variables do not erase variable identity");
    Atom *named = atom_var_with_name_key(&arena, small, variable->var_id);
    CHECK(atom_data_identical(named, atom_deep_copy(&arena, named)),
          "structural variable names compare as finite data rather than pointers");
    CHECK(!atom_data_identical(named,
              atom_var_with_name_key(&arena, leaves[0], variable->var_id)),
          "different structural variable names remain distinct");
    CHECK(!atom_data_identical(cycle, cycle) &&
              !atom_data_identical(handle, handle) &&
              !atom_data_identical(with_handle, with_handle),
          "exact key validation rejects cycles and runtime handles");
    CHECK(!atom_data_identical(changeable, before),
          "exact key comparison does not cache a mutable input's old verdict");
    unsigned identifier_holds = 0u;
    Atom *identifier = atom_native_handle_identifier(&arena, 19,
        &identifier_holds, retain_identifier, release_identifier);
    CHECK(identifier_holds == 1u &&
              !atom_data_identical(identifier, atom_int(&arena, 19)) &&
              !atom_data_identical(identifier, identifier) &&
              !atom_data_identical(atom_expr2(&arena, leaves[0], identifier),
                                   atom_expr2(&arena, leaves[0], atom_int(&arena, 19))),
          "integer-shaped native holders and their enclosing expressions are not data keys");

    arena_free(&arena);
    CHECK(identifier_holds == 0u, "exact key inspection does not retain a native holder");
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(AtomDataEqualitySummary checks=%u failures=%u)\n", checks, failures);
    return failures ? 1 : 0;
}
