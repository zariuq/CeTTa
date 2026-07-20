#include "runtime.h"
#include "match.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed;
static int failed;

#define CHECK(condition, message) do {                                      \
    if (condition) {                                                        \
        passed++;                                                          \
    } else {                                                               \
        failed++;                                                          \
        fprintf(stderr, "FAIL: %s\n", message);                            \
    }                                                                      \
} while (0)

int main(void) {
    Arena arena;
    SymbolTable symbols;
    VarInternTable variables;
    arena_init(&arena);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    var_intern_init(&variables);
    g_symbols = &symbols;
    g_var_intern = &variables;

    Atom *first = cetta_atom_named_var(
        &arena, "(generated \"y\")", (VarId)41u);
    Atom *second = cetta_atom_named_var(
        &arena, "(generated \"y\")", (VarId)42u);

    CHECK(first && first->kind == ATOM_VAR,
          "compiled structural variable is reconstructed");
    CHECK(first && first->var_id == (VarId)41u,
          "compiled structural variable preserves VarId identity");
    CHECK(first && first->name_key && first->name_key->kind == ATOM_EXPR,
          "compiled structural variable preserves its structural key");
    CHECK(first && first->name_key && first->name_key->expr.len == 2u &&
              atom_is_symbol(first->name_key->expr.elems[0], "generated"),
          "reconstructed key retains its head");
    CHECK(first && first->name_key && first->name_key->expr.len == 2u &&
              first->name_key->expr.elems[1]->kind == ATOM_GROUNDED &&
              first->name_key->expr.elems[1]->ground.gkind == GV_STRING,
          "reconstructed key retains its string payload");
    CHECK(second && second->name_key && first && first->name_key &&
              atom_eq(first->name_key, second->name_key),
          "equal structural spellings reconstruct equal keys");
    CHECK(first && second && !atom_eq(first, second),
          "VarId remains authoritative over equal key presentation");
    CHECK(cetta_atom_named_var(&arena, "$open", (VarId)43u) == NULL,
          "open structural keys fail closed");
    CHECK(cetta_atom_named_var(&arena, "one two", (VarId)44u) == NULL,
          "multi-form structural keys fail closed");

    bool old_universal = parser_set_universal_name_syntax_enabled(true);
    size_t notation_count = 0u;
    const ParserSyntaxFormSpec *notations =
        parser_syntax_form_specs(&notation_count);
    CHECK(notations && notation_count == 5u,
          "syntax notation ledger has the five admitted source forms");
    CHECK(strcmp(notations[0].compact, "@") == 0 &&
              strcmp(notations[0].expanded, "quote") == 0 &&
              strcmp(notations[1].compact, "*") == 0 &&
              strcmp(notations[1].expanded, "unquote") == 0 &&
              strcmp(notations[2].compact, "$") == 0 &&
              strcmp(notations[2].expanded, "syn:var") == 0 &&
              strcmp(notations[3].compact, "&") == 0 &&
              strcmp(notations[3].expanded, "syn:ref") == 0 &&
              strcmp(notations[4].compact, "!") == 0 &&
              strcmp(notations[4].expanded, "syn:exec") == 0,
          "reader and printers expose one coherent notation ledger");

    uint32_t symbols_before_plain_parse =
        atomic_load_explicit(&symbols.entry_len, memory_order_acquire);
    Atom **plain_forms = NULL;
    int plain_count = parse_metta_text(
        "(ordinary left right)", &arena, &plain_forms);
    uint32_t symbols_after_plain_parse =
        atomic_load_explicit(&symbols.entry_len, memory_order_acquire);
    CHECK(plain_count == 1 &&
              symbols_after_plain_parse - symbols_before_plain_parse == 3u,
          "recognizing syntax does not eagerly intern unrelated notation names");

    Atom **long_forms = NULL;
    int long_count = parse_metta_text(
        "(pair (syn:var (quote (hole 0))) "
        "      (syn:var (quote (hole 0))))",
        &arena, &long_forms);
    CHECK(long_count == 1,
          "expanded structural-variable syntax parses as one form");
    Atom *long_pair = long_count == 1 ? long_forms[0] : NULL;
    CHECK(long_pair && long_pair->kind == ATOM_EXPR &&
              long_pair->expr.len == 3u &&
              long_pair->expr.elems[1]->kind == ATOM_VAR &&
              long_pair->expr.elems[2]->kind == ATOM_VAR,
          "syn:var lowers to genuine native matcher variables");
    CHECK(long_pair &&
              long_pair->expr.elems[1]->var_id ==
                  long_pair->expr.elems[2]->var_id,
          "repeated expanded structural names preserve variable sharing");
    CHECK(long_pair && long_pair->expr.elems[1]->name_key &&
              atom_eq(long_pair->expr.elems[1]->name_key,
                      long_pair->expr.elems[2]->name_key),
          "expanded variables retain their closed structural key");

    Atom **compact_forms = NULL;
    int compact_count = parse_metta_text(
        "(pair $@(hole 0) $@(hole 0))", &arena, &compact_forms);
    CHECK(compact_count == 1 && long_pair &&
              atom_alpha_eq(long_pair, compact_forms[0]),
          "compact and expanded variable forms are alpha-equivalent");
    char *compact_text = long_pair
        ? parser_render_syntax(&arena, long_pair,
                               PARSER_SYNTAX_PRINT_COMPACT)
        : NULL;
    CHECK(compact_text &&
              strcmp(compact_text, "(pair $@(hole 0) $@(hole 0))") == 0,
          "compact printer preserves structural variable sharing");
    char *expanded_text = long_pair
        ? parser_render_syntax(&arena, long_pair,
                               PARSER_SYNTAX_PRINT_EXPANDED)
        : NULL;
    CHECK(expanded_text &&
              strcmp(expanded_text,
                     "(pair (syn:var (quote (hole 0))) "
                     "(syn:var (quote (hole 0))))") == 0,
          "expanded printer emits pure S-expression variable syntax");

    Atom *collision_elems[3] = {
        atom_symbol(&arena, "pair"), first, second,
    };
    Atom *collision_pair = atom_expr(&arena, collision_elems, 3u);
    char *collision_text = parser_render_syntax(
        &arena, collision_pair, PARSER_SYNTAX_PRINT_COMPACT);
    CHECK(collision_text && strstr(collision_text, "syn:printed-variable"),
          "printer freshens equal presentations with distinct identities");
    Atom **collision_forms = NULL;
    int collision_count = collision_text
        ? parse_metta_text(collision_text, &arena, &collision_forms)
        : -1;
    CHECK(collision_count == 1,
          "freshened collision rendering remains reader-admissible");
    CHECK(collision_count == 1 &&
              atom_alpha_eq(collision_pair, collision_forms[0]),
          "freshened collision rendering preserves the variable partition");

    CHECK(parser_render_syntax(
              &arena, atom_space(&arena, NULL),
              PARSER_SYNTAX_PRINT_COMPACT) == NULL,
          "runtime-only handles are not misrepresented as source syntax");

    Atom *exec_form = parser_read_source_form(&arena, "!(+ 20 22)");
    CHECK(exec_form != NULL,
          "source reader admits the compact top-level execution form");
    Atom *exec_payload = NULL;
    CHECK(parser_syn_exec_payload(exec_form, &exec_payload) && exec_payload &&
              exec_payload->kind == ATOM_EXPR && exec_payload->expr.len == 3u,
          "compact execution is represented as inspectable syn:exec data");

    Atom *legacy_ref = parser_read_source_form(&arena, "(syn:ref self)");
    CHECK(legacy_ref && atom_is_symbol(legacy_ref, "&self"),
          "syn:ref preserves the legacy symbol-reference tier");
    Atom *structural_ref = parser_read_source_form(
        &arena, "(syn:ref (quote (agent zar)))");
    Atom *structural_key = NULL;
    CHECK(structural_ref && registry_ref_name_key(structural_ref,
                                                  &structural_key) &&
              structural_key && structural_key->kind == ATOM_EXPR,
          "syn:ref preserves the structural-name reference tier");

    Atom *open_long = parser_read_source_form(
        &arena, "(syn:var (quote $open))");
    CHECK(open_long == NULL,
          "open structural names fail closed instead of becoming data");
    CHECK(parser_read_source_form(&arena, "one two") == NULL,
          "source reader rejects multiple forms");

    free(long_forms);
    free(compact_forms);
    free(plain_forms);
    free(collision_forms);
    parser_set_universal_name_syntax_enabled(old_universal);

    printf("(RuntimeNamedVarSummary %d %d %d)\n",
           passed + failed, passed, failed);

    g_var_intern = NULL;
    g_symbols = NULL;
    var_intern_free(&variables);
    symbol_table_free(&symbols);
    arena_free(&arena);
    return failed == 0 ? 0 : 1;
}
