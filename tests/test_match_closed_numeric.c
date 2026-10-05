#include "atom.h"
#include "match.h"
#include "lang.h"
#include "stats.h"
#include "tests/test_runtime_stats_stubs.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static CettaLanguageId language = CETTA_LANGUAGE_HE;
static bool compatibility;

CettaLanguageId eval_current_language_id(void) { return language; }
bool eval_current_uses_rust_he_compat_semantics(void) { return compatibility; }

static Atom *box(Arena *arena, HashConsTable *intern, Atom *value) {
    Atom *out = atom_expr2(arena, atom_symbol(arena, "numeric-box"),
                           hashcons_get(intern, value));
    assert(out && out->arena_id == 0u);
    return out;
}

static void compare(Arena *arena, Atom *left, Atom *right, bool expected) {
    Bindings bindings;
    bindings_init(&bindings);
    assert(atom_eq(left, right) == expected);
    bool matched = match_atoms(left, right, &bindings, arena);
    if (matched != expected) {
        fputs("numeric matching mismatch: ", stderr);
        atom_print(left, stderr);
        fputs(" versus ", stderr);
        atom_print(right, stderr);
        fprintf(stderr, " (expected %d, got %d)\n", expected, matched);
    }
    assert(matched == expected);
    bindings_free(&bindings);
}

int main(void) {
    SymbolTable symbols;
    Arena arena;
    HashConsTable intern;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    arena_init(&arena);
    hashcons_init_compact(&intern);
    g_hashcons = &intern;
    arena_set_hashcons(&arena, &intern);
    Atom *one = box(&arena, &intern, atom_int(&arena, 1));
    Atom *floating = box(&arena, &intern, atom_float(&arena, 1.0));
    assert(atom_hash(one) != atom_hash(floating));
    compare(&arena, one, floating, true);
    compare(&arena, box(&arena, &intern, atom_float(&arena, 0.0)),
             box(&arena, &intern, atom_float(&arena, -0.0)), true);
    Atom *large = box(&arena, &intern, atom_int(&arena, INT64_C(9007199254740993)));
    Atom *rounded = box(&arena, &intern, atom_float(&arena, 9007199254740992.0));
    compare(&arena, large, rounded, false);
    compatibility = true;
    compare(&arena, large, rounded, true);
    compatibility = false;
    Atom *nan = box(&arena, &intern, atom_float(&arena, NAN));
    compare(&arena, nan, nan, false);
    language = CETTA_LANGUAGE_PETTA;
    compare(&arena, one, floating, false);
    compare(&arena, nan, nan, true);
    g_hashcons = NULL;
    arena_free(&arena);
    hashcons_free(&intern);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    puts("PASS: closed matching preserves dialect numeric equality");
    return 0;
}
