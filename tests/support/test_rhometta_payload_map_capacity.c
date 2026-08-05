#define CETTA_TEST_HOOKS 1

#include "eval.h"
#include "rhocalc_core.h"
#include "space.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static bool has_flag(const Atom *atom, uint32_t flag) {
    return atom && (atom->flags & flag) != 0;
}

static int fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    return 1;
}

static void init_test_symbols(SymbolTable *symbols) {
    symbol_table_init(symbols);
    symbol_table_init_builtins(symbols, &g_builtin_syms);
    g_symbols = symbols;
    g_var_intern = NULL;
}

static int test_atom_hashflags_soundness(void) {
    SymbolTable symbols;
    init_test_symbols(&symbols);

    HashConsTable hashcons;
    hashcons_init(&hashcons);
    g_hashcons = &hashcons;

    Arena arena;
    arena_init(&arena);
    arena_set_hashcons(&arena, &hashcons);

    Atom *pair = atom_symbol(&arena, "pair");
    Atom *transient_one = atom_int(&arena, 1);
    if (!transient_one || transient_one->arena_id == 0u)
        return fail("numeric construction was ambiently hash-consed");
    Atom *one = atom_deep_copy_shared(&arena, transient_one);
    if (!one || one->arena_id != 0u)
        return fail("explicit numeric publication was not hash-consed");
    Atom *stable_elems[2] = {pair, one};
    Atom *stable_expr_a = atom_expr(&arena, stable_elems, 2);
    Atom *stable_expr_b = atom_expr(&arena, stable_elems, 2);
    if (!has_flag(stable_expr_a, ATOM_FLAG_HASH_STABLE) ||
        !has_flag(stable_expr_a, ATOM_FLAG_HASHCONS_ELIGIBLE))
        return fail("stable expression did not receive hash flags");
    if (stable_expr_a != stable_expr_b)
        return fail("stable expression was not hash-consed");
    atom_hash(stable_expr_a);
    if (!has_flag(stable_expr_a, ATOM_FLAG_HASH_VALID))
        return fail("stable expression hash was not cached");

    Atom *space = atom_space(&arena, (void *)(uintptr_t)0x10u);
    Atom *state = atom_state(&arena, (StateCell *)(uintptr_t)0x20u);
    Atom *capture = atom_capture(&arena, (CaptureClosure *)(uintptr_t)0x30u);
    Atom *foreign = atom_foreign(&arena, (CettaForeignValue *)(uintptr_t)0x40u);
    Atom *mutable_leaves[4] = {space, state, capture, foreign};
    const char *mutable_names[4] = {"space", "state", "capture", "foreign"};
    for (size_t i = 0; i < 4; i++) {
        Atom *leaf = mutable_leaves[i];
        if (has_flag(leaf, ATOM_FLAG_HASH_STABLE) ||
            has_flag(leaf, ATOM_FLAG_HASHCONS_ELIGIBLE))
            return fail("mutable leaf received stable/hashcons flags");
        atom_hash(leaf);
        if (has_flag(leaf, ATOM_FLAG_HASH_VALID))
            return fail("mutable leaf cached a hash");

        Atom *elems[2] = {pair, leaf};
        Atom *expr_a = atom_expr(&arena, elems, 2);
        Atom *expr_b = atom_expr(&arena, elems, 2);
        if (has_flag(expr_a, ATOM_FLAG_HASH_STABLE) ||
            has_flag(expr_a, ATOM_FLAG_HASHCONS_ELIGIBLE))
            return fail("mutable child did not poison parent hash flags");
        if (expr_a == expr_b)
            return fail("mutable-child expression was hash-consed");
        atom_hash(expr_a);
        if (has_flag(expr_a, ATOM_FLAG_HASH_VALID))
            return fail("mutable-child expression cached a hash");
        (void)mutable_names[i];
    }

    Atom *native_head = atom_symbol_id(&arena, g_builtin_syms.native_handle);
    Atom *contextual_elems[2] = {native_head, one};
    Atom *contextual_a = atom_expr(&arena, contextual_elems, 2);
    Atom *contextual_b = atom_expr(&arena, contextual_elems, 2);
    if (!has_flag(contextual_a, ATOM_FLAG_HASH_STABLE))
        return fail("contextual expression should remain hash-stable");
    if (has_flag(contextual_a, ATOM_FLAG_HASHCONS_ELIGIBLE))
        return fail("contextual expression was hashcons-eligible");
    if (contextual_a == contextual_b)
        return fail("contextual expression was hash-consed");
    atom_hash(contextual_a);
    if (!has_flag(contextual_a, ATOM_FLAG_HASH_VALID))
        return fail("contextual but stable expression did not cache its hash");

    arena_free(&arena);
    g_hashcons = NULL;
    hashcons_free(&hashcons);
    g_symbols = NULL;
    symbol_table_free(&symbols);
    return 0;
}

static int test_registry_growth(void) {
    Registry registry;
    Arena arena;
    Atom *values[80];

    arena_init(&arena);
    registry_init(&registry);
    for (uintptr_t i = 1; i <= 80; i++) {
        values[i - 1u] = atom_int(&arena, (int64_t)i);
        registry_bind_id(&registry, (SymbolId)i, values[i - 1u]);
    }
    if (registry.len != 80)
        return fail("registry did not retain 80 entries");
    if (registry.cap < 80 || registry.entries == registry.inline_entries)
        return fail("registry did not spill from inline storage");
    for (uintptr_t i = 1; i <= 80; i++) {
        Atom *value = registry_lookup_id(&registry, (SymbolId)i);
        if (value != values[i - 1u])
            return fail("registry lookup changed after spill");
    }
    registry_free(&registry);
    arena_free(&arena);
    if (registry.len != 0 || registry.entries != registry.inline_entries)
        return fail("registry did not reset to inline storage");
    return 0;
}

static int test_eval_redirect_growth(void) {
    eval_payload_redirects_begin(NULL);
    for (uintptr_t i = 1; i <= 80; i++) {
        if (!eval_payload_note_space_redirect(
                (Space *)(i << 4), (Space *)((i + 1000u) << 4)))
            return fail("space redirect map refused entry past inline storage");
        if (!eval_payload_note_state_redirect(
                (StateCell *)(i << 4), (StateCell *)((i + 2000u) << 4)))
            return fail("state redirect map refused entry past inline storage");
    }
    if (eval_payload_space_redirect_count() != 80 ||
        eval_payload_state_redirect_count() != 80)
        return fail("redirect maps did not retain all entries");
    for (CettaCount i = 0; i < 80; i++) {
        Space *space_orig = NULL;
        Space *space_redirect = NULL;
        StateCell *state_orig = NULL;
        StateCell *state_redirect = NULL;
        if (!eval_payload_space_redirect_at(i, &space_orig, &space_redirect) ||
            space_orig != (Space *)(((uintptr_t)i + 1u) << 4) ||
            space_redirect != (Space *)(((uintptr_t)i + 1001u) << 4))
            return fail("space redirect lookup changed after spill");
        if (!eval_payload_state_redirect_at(i, &state_orig, &state_redirect) ||
            state_orig != (StateCell *)(((uintptr_t)i + 1u) << 4) ||
            state_redirect != (StateCell *)(((uintptr_t)i + 2001u) << 4))
            return fail("state redirect lookup changed after spill");
    }
    eval_payload_redirects_end();
    if (eval_payload_space_redirect_count() != 0 ||
        eval_payload_state_redirect_count() != 0)
        return fail("redirect maps did not reset after teardown");
    return 0;
}

int main(void) {
    if (test_atom_hashflags_soundness() != 0)
        return 1;
    if (test_registry_growth() != 0)
        return 1;
    if (test_eval_redirect_growth() != 0)
        return 1;
    if (!rhocalc_payload_map_capacity_selftest())
        return fail("rhocalc payload maps did not grow past inline storage");
    puts("PASS: rhometta payload map capacity C test");
    return 0;
}
