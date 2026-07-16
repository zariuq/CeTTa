#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "lib_parse_inference_native.h"
#include "symbol.h"

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
           (uint64_t)ts.tv_nsec;
}

static uint32_t llist_length(Atom *list) {
    uint32_t len = 0;
    Atom *cursor = list;

    while (!atom_is_symbol(cursor, "LNil")) {
        assert(cursor != NULL);
        assert(cursor->kind == ATOM_EXPR);
        assert(cursor->expr.len == 3);
        assert(atom_is_symbol(cursor->expr.elems[0], "LCons"));
        cursor = cursor->expr.elems[2];
        len++;
    }
    return len;
}

static Atom *repeat_token_list(Arena *arena, Atom *token, uint32_t count) {
    Atom *list = atom_symbol(arena, "Nil");
    uint32_t i;

    for (i = 0; i < count; i++)
        list = atom_expr3(arena, atom_symbol(arena, "Cons"), token, list);
    return list;
}

static void run_size(const CettaLpNativeGrammar *grammar, uint32_t count) {
    Arena arena;
    Atom *token;
    Atom *tokens;
    Atom *presentation;
    char error[256] = {0};
    uint64_t start_ns;
    uint64_t elapsed_ns;

    arena_init(&arena);
    token = atom_symbol(&arena, "a");
    tokens = repeat_token_list(&arena, token, count);
    start_ns = monotonic_ns();
    presentation = cetta_lp_native_inference_presentation(
        grammar, atom_string(&arena, "scale-source"), tokens, &arena,
        error, sizeof(error));
    elapsed_ns = monotonic_ns() - start_ns;

    assert(presentation != NULL);
    assert(error[0] == '\0');
    assert(presentation->kind == ATOM_EXPR);
    assert(presentation->expr.len == 4);
    assert(atom_is_symbol(presentation->expr.elems[0], "GPresentation"));
    assert(llist_length(presentation->expr.elems[1]) == count + 6u);
    assert(llist_length(presentation->expr.elems[2]) == 3u);
    assert(llist_length(presentation->expr.elems[3]) == count * 2u + 2u);

    printf("lib_parse_inference_native tokens=%u elapsed_ns=%" PRIu64 "\n",
           count, elapsed_ns);
    arena_free(&arena);
}

int main(void) {
    SymbolTable symbols;
    CettaLpNativeSymbol rhs = {
        .kind = CETTA_LP_NATIVE_SYMBOL_TM,
    };
    CettaLpNativeProduction production = {
        .rhs = &rhs,
        .rhs_len = 1,
    };
    CettaLpNativeEntry entry = {
        .kind = CETTA_LP_NATIVE_ENTRY_PRODUCTION,
        .index = 0,
    };
    CettaLpNativeGrammar grammar = {
        .productions = &production,
        .production_len = 1,
        .entries = &entry,
        .entry_len = 1,
    };

    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;

    rhs.name = symbol_intern_cstr(g_symbols, "a");
    production.label = symbol_intern_cstr(g_symbols, "lit");
    production.lhs = symbol_intern_cstr(g_symbols, "Expr");

    run_size(&grammar, 1024);
    run_size(&grammar, 2048);
    run_size(&grammar, 4096);
    run_size(&grammar, 8192);

    symbol_table_free(&symbols);
    g_symbols = NULL;
    return 0;
}
