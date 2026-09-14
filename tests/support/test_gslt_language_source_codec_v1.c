#include "finite_horn_ground_term_v1.h"
#include "symbol.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char authored[] = "; note\n (p True 1.0 $x \"\\u03bb\" -9) \n";
    const char canonical[] = "(p True 1.0 $x \"λ\" -9)";
    SymbolTable symbols; symbol_table_init(&symbols); g_symbols = &symbols;
    Arena arena; arena_init(&arena);
    Atom *term = NULL;
    char error[512];
    unsigned checks = 0, failures = 0;
#define CHECK(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL source codec line %d: %s\n", __LINE__, error); failures++; } } while (0)
    CHECK(fh_ground_term_v1_read_source(&arena, (const uint8_t *)authored,
          strlen(authored), &term, error, sizeof(error)));
    CHECK(term && term->kind == ATOM_EXPR && term->expr.len == 6 &&
          atom_is_symbol(term->expr.elems[1], "True") &&
          atom_is_symbol(term->expr.elems[2], "1.0") &&
          atom_is_symbol(term->expr.elems[3], "$x") &&
          term->expr.elems[4]->kind == ATOM_GROUNDED &&
          term->expr.elems[4]->ground.gkind == GV_STRING &&
          !strcmp(term->expr.elems[4]->ground.sval, "λ"));
    CHECK(!fh_ground_term_v1_parse(&arena, (const uint8_t *)authored,
          strlen(authored), &term, error, sizeof(error)) && !term);
    CHECK(fh_ground_term_v1_parse(&arena, (const uint8_t *)canonical,
          strlen(canonical), &term, error, sizeof(error)));
    error[0] = 0;
    CHECK(!fh_ground_term_v1_parse(&arena, (const uint8_t *)canonical,
          strlen(canonical), NULL, error, sizeof(error)) && *error);
    const char *bad[] = {"", "(p ?x)", "(p) (q)", "(p))", "(p", "(p \"\\u0000\")"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
        CHECK(!fh_ground_term_v1_read_source(&arena, (const uint8_t *)bad[i],
              strlen(bad[i]), &term, error, sizeof(error)) && !term && *error);
    const char nul[] = {'(', 'p', 0, ')'};
    CHECK(!fh_ground_term_v1_read_source(&arena, (const uint8_t *)nul,
          sizeof(nul), &term, error, sizeof(error)) && !term);
    arena_free(&arena); g_symbols = NULL; symbol_table_free(&symbols);
    printf("GsltLanguageSourceCodecV1: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
