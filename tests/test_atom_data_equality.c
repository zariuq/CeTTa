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
    CETTA_FRAME_IDENTITY_SCOPE(frames);
    CettaFrameIdentity epoch = cetta_frame_identity_scope_fresh(&frames);
    Atom *framed = atom_var_like(&arena, variable, var_epoch_id(variable->var_id, epoch));
    CHECK(!atom_data_identical(variable, framed),
          "equal source variables in different activations remain distinct keys");
    CHECK(atom_data_identical(framed, atom_deep_copy(&arena, framed)),
          "copying an exact variable key preserves its activation identity");
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

    StateCell first = {.value = atom_int(&arena, 1)}, second = {.value = atom_int(&arena, 1)};
    Atom *state_a = atom_state(&arena, &first), *state_b = atom_state(&arena, &second);
    CHECK(atom_eq(state_a, state_b) && !atom_capture_identical(state_a, state_b),
          "capture identity distinguishes equal-valued mutable cells without changing ordinary equality");
    Atom *state_alias = atom_deep_copy(&arena, state_a);
    CHECK(atom_capture_identical(state_a, state_alias), "copied wrappers keep the same captured resource");
    first.value = atom_int(&arena, 7);
    CHECK(atom_capture_identical(state_a, state_alias) && !atom_capture_identical(state_a, state_b),
          "resource mutation does not change which captured handle was read");
    CHECK(atom_capture_identical(shared(&arena, state_a, 10000u), shared(&arena, state_alias, 10000u)) &&
          !atom_capture_identical(shared(&arena, state_a, 10000u), shared(&arena, state_b, 10000u)),
          "the same resource policy applies beneath deep shared input graphs");
    Atom *context = atom_prime_context_bind(&arena, NULL, atom_symbol(&arena, "key"), state_a);
    Atom *context_copy = atom_deep_copy(&arena, context);
    Atom *context_other = atom_prime_context_bind(&arena, NULL, atom_symbol(&arena, "key"), state_b);
    CHECK(atom_capture_identical(context, context_copy) && !atom_capture_identical(context, context_other),
          "immutable context copies retain nested resource identity");
    Atom *cap = atom_prime_need_capability_with_rights(&arena, 11u, 12u, 13u,
        CETTA_PRIME_NEED_RIGHT_ALL);
    CHECK(atom_capture_identical(cap, atom_deep_copy(&arena, cap)) &&
          !atom_capture_identical(cap, atom_prime_need_capability_with_rights(&arena, 11u, 12u, 13u,
              CETTA_PRIME_NEED_RIGHT_INSPECT)) &&
          !atom_capture_identical(cap, atom_prime_need_capability_with_rights(&arena, 11u, 12u, 14u,
              CETTA_PRIME_NEED_RIGHT_ALL)), "captured capabilities preserve authority and rights");
    CHECK(atom_capture_identical(nan_left, atom_deep_copy(&arena, nan_left)) &&
          !atom_capture_identical(atom_float(&arena, 0.0), atom_float(&arena, -0.0)),
          "captured numeric representation compares exact bits rather than language truth equality");
    Atom *marked = atom_expr2(&arena, leaves[0], leaves[1]);
    Atom *unmarked = atom_deep_copy(&arena, marked);
    marked->flags |= ATOM_FLAG_HE_EVALUATED;
    CHECK(!atom_capture_identical(marked, unmarked), "captured data and executable-source roles remain distinct");
    unsigned other_holds = 0u;
    Atom *other_identifier = atom_native_handle_identifier(&arena, 19,
        &other_holds, retain_identifier, release_identifier);
    CHECK(atom_capture_identical(identifier, atom_deep_copy(&arena, identifier)) &&
          !atom_capture_identical(identifier, other_identifier) &&
          !atom_capture_identical(identifier, atom_int(&arena, 19)),
          "integer-shaped handles retain their actual owner and lifetime protocol");
    CHECK(!atom_capture_identical(cycle, cycle), "capture inspection does not certify a malformed cyclic expression");

    arena_free(&arena);
    CHECK(identifier_holds == 0u, "exact key inspection does not retain a native holder");
    symbol_table_free(&symbols);
    g_symbols = NULL;
    printf("(AtomDataEqualitySummary checks=%u failures=%u)\n", checks, failures);
    return failures ? 1 : 0;
}
