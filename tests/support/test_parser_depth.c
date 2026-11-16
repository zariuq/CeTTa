#include "parser.h"
#include "term_domain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

static void inspect(Atom *root, size_t depth) {
    for (size_t i = 0u; i < depth; ++i) {
        assert(root && root->kind == ATOM_EXPR && root->expr.len == 2u);
        assert(atom_is_symbol(root->expr.elems[0], "node"));
        root = root->expr.elems[1];
    }
    assert(root && root->kind == ATOM_EXPR && root->expr.len == 3u);
    assert(atom_is_symbol(root->expr.elems[0], "Pair"));
    assert(root->expr.elems[1]->kind == ATOM_VAR && atom_eq(root->expr.elems[1], root->expr.elems[2]));
}
int main(int argc, char **argv) {
    bool bounded = argc > 1 && !strcmp(argv[1], "--bounded");
    CettaTermDomain *domain = cetta_term_domain_new_default(false);
    CettaTermDomainScope scope = {0};
    assert(domain && cetta_term_domain_enter(domain, &scope));
    Arena arena;
    arena_init_detached(&arena);
    const size_t depth = bounded ? 20u : 6000u;
    const char *leaf = "(Pair $x $x)";
    char *source = malloc(depth * 7u + strlen(leaf) + 1u); assert(source);
    char *cursor = source;
    for (size_t i = 0u; i < depth; ++i) { memcpy(cursor, "(node ", 6u); cursor += 6u; }
    memcpy(cursor, leaf, strlen(leaf)); cursor += strlen(leaf);
    for (size_t i = 0u; i < depth; ++i) *cursor++ = ')';
    *cursor = '\0';
    assert(parser_text_well_formed(source));
    Atom **atoms = NULL;
    assert(parse_metta_text(source, &arena, &atoms) == 1);
    Atom *root = atoms[0]; free(atoms); inspect(root, depth);
    puts("raw atom reader accepts deep finite syntax and retains variable aliases");
    AtomId *ids = NULL;
    TermUniverse *universe = cetta_term_domain_universe(domain);
    assert(parse_metta_text_ids(source, universe, &ids) == 1);
    Atom *decoded = term_universe_get_atom(universe, ids[0]); assert(decoded);
    inspect(decoded, depth); free(ids);
    assert(parser_project_document_ids(&root, 1u, universe, &ids));
    decoded = term_universe_get_atom(universe, ids[0]); assert(decoded);
    inspect(decoded, depth); free(ids);
    puts("direct IDs and atom-to-ID projection agree on deep syntax");
    bool previous = parser_set_universal_name_syntax_enabled(true);
    for (unsigned compact = 0u; compact < 2u; ++compact) {
        char *printed = parser_render_syntax(&arena, root,
            compact ? PARSER_SYNTAX_PRINT_COMPACT : PARSER_SYNTAX_PRINT_EXPANDED);
        assert(printed);
        Atom *read = parser_read_source_form(&arena, printed);
        assert(read); inspect(read, depth);
    }
    parser_set_universal_name_syntax_enabled(previous);
    puts("both source printers round-trip deep syntax through the same reader");
    Atom *plain = atom_symbol(&arena, "leaf");
    Atom *cycle = atom_expr(&arena, &plain, 1u); assert(cycle);
    cycle->expr.elems[0] = cycle;
    assert(!parser_render_syntax(&arena, cycle, PARSER_SYNTAX_PRINT_EXPANDED));
    ids = NULL;
    assert(!parser_project_document_ids(&cycle, 1u, universe, &ids) && !ids);
    cycle->expr.elems[0] = plain;
    assert(!parser_text_well_formed(")"));
    source[strlen(source) - 1u] = '\0';
    atoms = NULL;
    assert(parse_metta_text(source, &arena, &atoms) == -1 && !atoms);
    puts("cycles and incomplete syntax fail without unbounded traversal");
    free(source);
    if (bounded) {
        const char *prefix = "(node ";
        char limited[1024];
        char *end = limited;
        for (size_t i = 0u; i < 100u; ++i) { memcpy(end, prefix, 6u); end += 6u; }
        memcpy(end, "leaf", 4u); end += 4u;
        for (size_t i = 0u; i < 100u; ++i) *end++ = ')';
        *end = '\0';
        assert(!parser_text_well_formed(limited));
        atoms = NULL;
        assert(parse_metta_text(limited, &arena, &atoms) == -1 && !atoms);
        size_t position = 0u;
        assert(!parse_sexpr(&arena, limited, &position));
        position = 0u;
        assert(parse_sexpr_to_id(universe, limited, &position) == CETTA_ATOM_ID_NONE);
        puts("an explicitly configured reader allowance is still enforced");
    }
    arena_free(&arena);
    cetta_term_domain_leave(&scope); cetta_term_domain_release(domain);
    puts("PASS: deep parser/source-printer/projection controls");
    return 0;
}
