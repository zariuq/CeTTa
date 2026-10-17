#include "native/regex_class_source_v1.h"
#include "tests/test_runtime_stats_stubs.h"

#include <stdio.h>

static unsigned passed, failed;
static void check(bool value, const char *reason) {
    if (value) ++passed;
    else { ++failed; fprintf(stderr, "FAIL: %s\n", reason); }
}
static Atom *cst(Arena *arena, const char *name, Atom *left, Atom *right) {
    Atom *items[] = {atom_symbol(arena, "CstRuleV1"), atom_string(arena, name),
        atom_int(arena, 2), atom_int(arena, 7), left, right};
    return atom_expr(arena, items, right ? 6u : left ? 5u : 4u);
}
static Atom *scalar(Arena *arena, uint32_t cp) {
    return atom_expr2(arena, atom_symbol(arena, "cp"), atom_int(arena, cp));
}
static Atom *character(Arena *arena, uint32_t cp) {
    return cst(arena, "re-src:class-char", scalar(arena, cp), NULL);
}
static Atom *range(Arena *arena, uint32_t low, uint32_t high) {
    return cst(arena, "re-src:class-range", cst(arena, "re-src:endpoint-char", scalar(arena, low), NULL),
        cst(arena, "re-src:endpoint-char", scalar(arena, high), NULL));
}
static Atom *project(Arena *arena, Atom *tree, bool insensitive, Atom **problem) {
    CettaReClassSourceOptionsV1 options = {.arena = arena, .unicode = true, .insensitive = insensitive};
    return cetta_re_v1_class_source(&options, tree, problem);
}
static bool contains(Atom *result, uint32_t cp) {
    if (!result || result->kind != ATOM_EXPR || result->expr.len != 2u || !atom_is_symbol(result->expr.elems[0], "re:class")) return false;
    Atom *ranges = result->expr.elems[1];
    while (ranges && ranges->kind == ATOM_EXPR && ranges->expr.len == 3u && atom_is_symbol(ranges->expr.elems[0], "LCons")) {
        Atom *r = ranges->expr.elems[1];
        if (r->expr.elems[1]->ground.ival <= cp && cp <= r->expr.elems[2]->ground.ival) return true;
        ranges = ranges->expr.elems[2];
    }
    return false;
}
static bool problem_is(Atom *problem, const char *kind) {
    return problem && problem->kind == ATOM_EXPR && atom_is_symbol(problem->expr.elems[0], kind);
}
static void operations(Arena *arena) {
    Atom *problem = NULL;
    Atom *set = project(arena, cst(arena, "re-src:class-difference", range(arena, 'a', 'z'),
        cst(arena, "re-src:class-first", character(arena, 'a'), character(arena, 'e'))), false, &problem);
    check(set && !problem && contains(set, 'b') && contains(set, 'z'), "difference keeps consonants");
    check(!contains(set, 'a') && !contains(set, 'e') && !contains(set, '7'), "difference does not invent excluded members");
    set = project(arena, cst(arena, "re-src:class-intersection", range(arena, 'a', 'f'), range(arena, 'd', 'z')), false, &problem);
    check(contains(set, 'd') && contains(set, 'f') && !contains(set, 'c') && !contains(set, 'g'), "intersection exact boundaries");
    set = project(arena, cst(arena, "re-src:class-symmetric-difference", range(arena, 'a', 'f'), range(arena, 'd', 'h')), false, &problem);
    check(contains(set, 'a') && contains(set, 'h') && !contains(set, 'd') && !contains(set, 'f'), "symmetric difference excludes overlap");
    set = project(arena, cst(arena, "re-src:class-intersection", range(arena, 'a', 'z'),
        cst(arena, "re-src:class-nested-not", character(arena, 'a'), NULL)), true, &problem);
    check(contains(set, 'b') && contains(set, 'B') && !contains(set, 'a') && !contains(set, 'A'), "nested negation complements folded operand");
    set = project(arena, cst(arena, "re-src:class-difference", character(arena, 'a'), character(arena, 'A')), true, &problem);
    check(set && !contains(set, 'a') && !contains(set, 'A'), "folding precedes difference on both operands");
    set = project(arena, cst(arena, "re-src:class-first-amp", cst(arena, "re-src:class-amp-tilde",
        cst(arena, "re-src:class-tilde-empty", NULL, NULL), NULL), NULL), false, &problem);
    check(contains(set, '&') && contains(set, '~'), "contextual literal ampersand and tilde remain members");
    set = project(arena, cst(arena, "re-src:class", cst(arena, "re-src:first-open", NULL, NULL), NULL), false, &problem);
    check(contains(set, '[') && !contains(set, ']'), "historical literal opening bracket remains expressible");
    Atom *hex = cst(arena, "re-src:hex-more", scalar(arena, '4'), cst(arena, "re-src:hex-one", scalar(arena, '1'), NULL));
    Atom *hex_high = cst(arena, "re-src:hex-more", scalar(arena, '5'), cst(arena, "re-src:hex-one", scalar(arena, 'A'), NULL));
    set = project(arena, cst(arena, "re-src:class-range", cst(arena, "re-src:endpoint-hex", hex, NULL),
        cst(arena, "re-src:endpoint-hex", hex_high, NULL)), false, &problem);
    check(contains(set, 'A') && contains(set, 'Z') && !contains(set, '['), "hexadecimal endpoints denote scalars before range formation");
    set = project(arena, cst(arena, "re-src:first-range", cst(arena, "re-src:first-endpoint-amp", NULL, NULL),
        cst(arena, "re-src:endpoint-tilde", NULL, NULL)), false, &problem);
    check(contains(set, '&') && contains(set, '~') && contains(set, 'a'), "reserved punctuation remains a valid scalar range endpoint");
    problem = NULL;
    set = project(arena, range(arena, 'z', 'a'), false, &problem);
    check(!set && problem_is(problem, "re:source-fault"), "reversed range is not an empty class");
    problem = NULL;
    set = project(arena, cst(arena, "re-src:class-range", cst(arena, "re-src:endpoint-escape", scalar(arena, 'd'), NULL),
        cst(arena, "re-src:endpoint-char", scalar(arena, 'z'), NULL)), false, &problem);
    check(!set && problem_is(problem, "re:source-fault"), "set-valued escape cannot be a scalar range endpoint");
    problem = NULL;
    set = project(arena, cst(arena, "wrong", NULL, NULL), false, &problem);
    check(!set && problem_is(problem, "re:source-fault"), "unknown CST cannot acquire class semantics by default");
}
static void lifetime_and_bounds(Arena *arena) {
    Atom *tree = character(arena, 'a'), *problem = NULL;
    for (size_t i = 0u; i < 20000u; ++i) tree = cst(arena, "re-src:class-nested", tree, NULL);
    Atom *set = project(arena, tree, false, &problem);
    check(set && !problem && contains(set, 'a'), "deep nesting uses an explicit traversal stack without a default cap");
    uint64_t remaining = 9u;
    CettaReClassSourceOptionsV1 options = {.arena = arena, .unicode = true, .bounded = true, .remaining = &remaining};
    set = cetta_re_v1_class_source(&options, tree, &problem);
    check(!set && !remaining && problem_is(problem, "re:incomplete"), "shared work exhaustion is incomplete, not absence");
    problem = NULL; options.bounded = false; options.depth_limit = 4u;
    set = cetta_re_v1_class_source(&options, tree, &problem);
    check(!set && problem_is(problem, "re:incomplete"), "explicit depth request is preserved");
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    Arena arena; arena_init(&arena); g_hashcons = NULL; g_var_intern = NULL;
    operations(&arena); lifetime_and_bounds(&arena);
    arena_free(&arena); symbol_table_free(&symbols); g_symbols = NULL;
    printf("RegexClassSourceV1Summary %u %u\n", passed, failed);
    return failed ? 1 : 0;
}
