#include "match.h"
#include "parser.h"
#include "symbol.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int passed;
static int failed;

#define CHECK(condition, message) do {                                     \
    if (condition) {                                                       \
        passed++;                                                         \
    } else {                                                              \
        failed++;                                                         \
        fprintf(stderr, "FAIL: %s\n", message);                           \
    }                                                                     \
} while (0)

static Atom *parse_one(Arena *arena, const char *source) {
    Atom **forms = NULL;
    int count = parse_metta_text(source, arena, &forms);
    Atom *result = count == 1 ? forms[0] : NULL;
    free(forms);
    return result;
}

static bool is_binary_form(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 3u;
}

static void check_direct_id_partition(ParserBareDollarMode mode,
                                      bool prime_syntax,
                                      bool expect_variables,
                                      bool expect_same) {
    Arena persistent;
    TermUniverse universe;
    AtomId *forms = NULL;
    arena_init(&persistent);
    term_universe_init(&universe);
    term_universe_set_persistent_arena(&universe, &persistent);
    parser_set_bare_dollar_mode(mode);
    parser_set_universal_name_syntax_enabled(prime_syntax);
    int count = parse_metta_text_ids("(P $ $)", &universe, &forms);
    AtomId form = count == 1 ? forms[0] : CETTA_ATOM_ID_NONE;
    AtomId left = form == CETTA_ATOM_ID_NONE
                      ? CETTA_ATOM_ID_NONE
                      : tu_child(&universe, form, 1u);
    AtomId right = form == CETTA_ATOM_ID_NONE
                       ? CETTA_ATOM_ID_NONE
                       : tu_child(&universe, form, 2u);
    CHECK(count == 1 && tu_arity(&universe, form) == 3u,
          "direct-to-ID reader parses the tournament form");
    if (expect_variables) {
        CHECK(tu_kind(&universe, left) == ATOM_VAR &&
                  tu_kind(&universe, right) == ATOM_VAR,
              "direct-to-ID reader classifies bare dollar as a variable");
        CHECK((tu_var_id(&universe, left) == tu_var_id(&universe, right)) ==
                  expect_same,
              "direct-to-ID reader preserves the selected identity partition");
    } else {
        CHECK(tu_kind(&universe, left) == ATOM_SYMBOL &&
                  tu_kind(&universe, right) == ATOM_SYMBOL,
              "direct-to-ID reader keeps bare dollar literal");
    }
    free(forms);
    term_universe_free(&universe);
    arena_free(&persistent);
}

int main(void) {
    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    HashConsTable hashcons;
    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    hashcons_init(&hashcons);
    g_symbols = &symbols;
    g_var_intern = &variables;
    g_hashcons = &hashcons;
    arena_set_hashcons(&arena, &hashcons);

    ParserBareDollarMode old_mode = parser_set_bare_dollar_mode(
        PARSER_BARE_DOLLAR_SYMBOL);
    bool old_universal = parser_set_universal_name_syntax_enabled(true);

    Atom *literal = parse_one(&arena, "$");
    CHECK(literal && literal->kind == ATOM_SYMBOL && atom_is_symbol(literal, "$"),
          "literal tournament control preserves the bare-dollar symbol");

    parser_set_bare_dollar_mode(PARSER_BARE_DOLLAR_FRESH_VARIABLE);
    Atom *fresh_pair = parse_one(&arena, "(P $ $)");
    CHECK(is_binary_form(fresh_pair) &&
              fresh_pair->expr.elems[1]->kind == ATOM_VAR &&
              fresh_pair->expr.elems[2]->kind == ATOM_VAR,
          "fresh contender parses each bare dollar as an ordinary variable");
    CHECK(is_binary_form(fresh_pair) &&
              fresh_pair->expr.elems[1]->var_id !=
                  fresh_pair->expr.elems[2]->var_id,
          "fresh contender allocates a distinct identity per occurrence");
    char *fresh_rendered = parser_render_syntax(
        &arena, fresh_pair, PARSER_SYNTAX_PRINT_COMPACT);
    CHECK(fresh_rendered != NULL,
          "fresh anonymous variables have reader-admissible syntax output");
    Atom *fresh_roundtrip = fresh_rendered
                                ? parse_one(&arena, fresh_rendered)
                                : NULL;
    CHECK(fresh_roundtrip && atom_alpha_eq(fresh_pair, fresh_roundtrip),
          "fresh anonymous variable partition survives syntax round-trip");
    CHECK(is_binary_form(fresh_roundtrip) &&
              fresh_roundtrip->expr.elems[1]->var_id !=
                  fresh_roundtrip->expr.elems[2]->var_id,
          "syntax round-trip does not collapse distinct anonymous variables");

    Atom *ground_pair = parse_one(&arena, "(P a b)");
    Bindings fresh_bindings;
    bindings_init(&fresh_bindings);
    CHECK(fresh_pair && ground_pair &&
              simple_match(fresh_pair, ground_pair, &fresh_bindings),
          "fresh anonymous variables independently match unequal fields");
    CHECK(fresh_bindings.len == 2u,
          "native matcher witnesses both internal anonymous bindings");
    bindings_free(&fresh_bindings);

    Atom *fresh_one = parse_one(&arena, "$");
    Atom *space = atom_space(&arena, (void *)(uintptr_t)0x1u);
    Bindings reference_binding;
    bindings_init(&reference_binding);
    CHECK(fresh_one && fresh_one->kind == ATOM_VAR && space &&
              simple_match(fresh_one, space, &reference_binding),
          "fresh anonymous variable matches a grounded space reference");
    CHECK(fresh_one &&
              bindings_lookup_id(&reference_binding, fresh_one->var_id) == space,
          "grounded reference is present in the internal match witness");
    bindings_free(&reference_binding);

    Atom *named_pair = parse_one(&arena, "(P $x $x)");
    Atom *underscore_pair = parse_one(&arena, "(P $_ $_)");
    Atom *structural_pair = parse_one(&arena, "(P $@(k) $@(k))");
    CHECK(is_binary_form(named_pair) &&
              named_pair->expr.elems[1]->var_id ==
                  named_pair->expr.elems[2]->var_id,
          "named variables retain co-reference in fresh mode");
    CHECK(is_binary_form(underscore_pair) &&
              underscore_pair->expr.elems[1]->var_id ==
                  underscore_pair->expr.elems[2]->var_id,
          "existing $_ remains a named co-referential variable");
    CHECK(is_binary_form(structural_pair) &&
              structural_pair->expr.elems[1]->var_id ==
                  structural_pair->expr.elems[2]->var_id,
          "structural variables retain co-reference in fresh mode");

    parser_set_bare_dollar_mode(PARSER_BARE_DOLLAR_SHARED_VARIABLE);
    Atom *shared_pair = parse_one(&arena, "(P $ $)");
    CHECK(is_binary_form(shared_pair) &&
              shared_pair->expr.elems[1]->kind == ATOM_VAR &&
              shared_pair->expr.elems[1]->var_id ==
                  shared_pair->expr.elems[2]->var_id,
          "shared contender assigns one empty-spelling identity per form");
    Bindings shared_unequal;
    bindings_init(&shared_unequal);
    CHECK(shared_pair && ground_pair &&
              !simple_match(shared_pair, ground_pair, &shared_unequal),
          "shared contender rejects unequal fields through co-reference");
    bindings_free(&shared_unequal);
    Atom *equal_pair = parse_one(&arena, "(P a a)");
    Bindings shared_equal;
    bindings_init(&shared_equal);
    CHECK(shared_pair && equal_pair &&
              simple_match(shared_pair, equal_pair, &shared_equal),
          "shared contender accepts equal fields");
    bindings_free(&shared_equal);

    parser_set_bare_dollar_mode(PARSER_BARE_DOLLAR_FRESH_VARIABLE);
    parser_set_universal_name_syntax_enabled(false);
    Atom *he_dollar = parse_one(&arena, "$");
    CHECK(he_dollar && he_dollar->kind == ATOM_SYMBOL &&
              atom_is_symbol(he_dollar, "$"),
          "experimental mode cannot leak into the HE reader");

    check_direct_id_partition(
        PARSER_BARE_DOLLAR_FRESH_VARIABLE, true, true, false);
    check_direct_id_partition(
        PARSER_BARE_DOLLAR_SHARED_VARIABLE, true, true, true);
    check_direct_id_partition(
        PARSER_BARE_DOLLAR_FRESH_VARIABLE, false, false, false);

    parser_set_bare_dollar_mode(old_mode);
    parser_set_universal_name_syntax_enabled(old_universal);
    printf("(PrimeBareDollarParserSummary %d %d %d)\n",
           passed + failed, passed, failed);

    g_hashcons = NULL;
    g_var_intern = NULL;
    g_symbols = NULL;
    arena_free(&arena);
    hashcons_free(&hashcons);
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    return failed == 0 ? 0 : 1;
}
