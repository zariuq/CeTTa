#include "native/regex_text_v1.h"
#include "src/string_ops.h"
#include "tests/test_runtime_stats_stubs.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned passed, failed;
static void check(bool ok, const char *reason) {
    if (ok) ++passed; else { ++failed; fprintf(stderr, "FAIL: %s\n", reason); }
}
static bool headed(Atom *x, const char *name, size_t arity) {
    return x && x->kind == ATOM_EXPR && x->expr.len == arity + 1u &&
        atom_is_symbol(x->expr.elems[0], name);
}
static bool text_is(Atom *x, const char *text) {
    return x && x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING &&
        atom_string_len(x) == strlen(text) && !memcmp(x->ground.sval, text, strlen(text));
}
static Atom *prepare(Arena *a, const char *source, const char *expected) {
    Atom *out = cetta_re_v1_prepare_text(a, atom_string(a, source));
    check(headed(out, "re:source-text", 2u) && text_is(out->expr.elems[1], expected), source);
    return headed(out, "re:source-text", 2u) ? out->expr.elems[2] : NULL;
}
static Atom *cst(Arena *a, size_t start, size_t stop) {
    return atom_expr(a, (Atom *[]){atom_symbol(a, "CstRuleV1"), atom_string(a, "re-src:literal"),
        atom_int(a, start), atom_int(a, stop)}, 4u);
}
static void lexical(Arena *a) {
    Atom *identity = prepare(a, "a # b", "a # b");
    check(headed(identity, "re:source-identity-v1", 1u), "unchanged text has a compact identity descriptor");
    prepare(a, "(?x) a # comment\n b", "(?x)ab");
    prepare(a, "(?x:a # comment\n b) c", "(?x:ab) c");
    prepare(a, "(?x) a (?-x: # ) b", "(?x)a(?-x: # )b");
    prepare(a, "(?x: a (?-x)b c) d", "(?x:a(?-x)b c) d");
    prepare(a, "(?x)( ? - x ) a b", "(?x)(?-x) a b");
    prepare(a, "(?x)(? # flags\n -x) a b", "(?x)(?-x) a b");
    prepare(a, "(?x s) a b", "(?x s) a b");
    prepare(a, "(?x)\\# \\ a", "(?x)\\#\\ a");
    prepare(a, "(?x)[ # ] # ignored\n a", "(?x)[ # ]a");
    prepare(a, "(?x)[[] # ignored\n a", "(?x)[[]a");
    prepare(a, "(?x)[[a] # ] # ignored\n b", "(?x)[[a] # ]b");
    prepare(a, "(?x)[[]] # ignored\n a", "(?x)[[]]a");
    prepare(a, "(?x)[[] # ] # ignored\n a", "(?x)[[] # ]a");
    prepare(a, "(?x)[a&&[] # ignored\n b", "(?x)[a&&[]b");
    prepare(a, "(?x)[a--[b c] # ] # ignored\n d", "(?x)[a--[b c] # ]d");
    prepare(a, "(?x)[\\] # ] # ignored\n a", "(?x)[\\] # ]a");
    prepare(a, "(?x)[\\[ # ] # ignored\n a", "(?x)[\\[ # ]a");
    prepare(a, "(?x)[[a] # unfinished", "(?x)[[a]");
    prepare(a, "(?x)[a # unfinished", "(?x)[a # unfinished");
    prepare(a, "[a][b] (?x) c", "[a][b] (?x)c");
    prepare(a, "(?x)\xe3\x80\x80\xce\xbb # ignored\n\xce\xb2", "(?x)\xce\xbb\xce\xb2");
    prepare(a, "(?x)\\\xe3\x80\x80 a", "(?x)\\\xe3\x80\x80" "a");
    prepare(a, "(?z) a", "(?z) a");
    prepare(a, "(?x) ( a", "(?x)(a");
    prepare(a, "", "");
    const char malformed[] = {(char)0xed, (char)0xa0, (char)0x80};
    Atom *bad = cetta_re_v1_prepare_text(a, atom_string_n(a, malformed, sizeof(malformed)));
    check(headed(bad, "re:invalid-utf8", 3u), "invalid UTF-8 is not silently repaired");
    bad = cetta_re_v1_prepare_text(a, atom_int(a, 1));
    check(headed(bad, "re:source-fault", 3u), "non-text source has an explicit observation");
}
static void locations(Arena *a) {
    const char *source = "(?x) \xce\xbb #comment\n \xce\xb2";
    Atom *mapping = prepare(a, source, "(?x)\xce\xbb\xce\xb2");
    CettaUtf8Unit *units = NULL; size_t len = 0u, bad = 0u;
    check(cetta_utf8_units(source, strlen(source), &units, &len, &bad), "independent UTF-8 source coordinates");
    size_t beta = 0u, lambda = 0u;
    for (size_t i = 0u; i < len; ++i) {
        if (units[i].scalar == 0x3bbu) lambda = i;
        if (units[i].scalar == 0x3b2u) beta = i;
    }
    Atom *shared = cst(a, 5u, 6u), *empty = cst(a, 5u, 5u);
    uint32_t old_hash = atom_hash(shared);
    Atom *trees = atom_expr(a, (Atom *[]){cst(a, 4u, 5u), shared, shared, empty}, 4u);
    Atom *span = atom_expr(a, (Atom *[]){atom_symbol(a, "ParserSpanV1"), atom_int(a, 0),
        atom_int(a, 8), atom_int(a, 0), atom_int(a, 6)}, 5u);
    Atom *accepted = atom_expr3(a, atom_symbol(a, "ParserAcceptedV1"), trees, span);
    Atom *forest = atom_expr2(a, atom_symbol(a, "pf-v1"), atom_string(a, "lexed coordinates"));
    Atom *observation = atom_expr3(a, atom_symbol(a, "ParserWithForestV1"), accepted, forest);
    Atom *located = cetta_re_v1_locate_source(a, observation, mapping);
    check(headed(located, "ParserWithForestV1", 2u), "located accepted observation preserves its outer shape");
    Atom *actual = located->expr.elems[1]->expr.elems[1];
    check(actual->expr.elems[0]->expr.elems[2]->ground.ival == (int64_t)lambda &&
        actual->expr.elems[0]->expr.elems[3]->ground.ival == (int64_t)lambda + 1,
        "nonempty span stops before removed trivia");
    check(actual->expr.elems[1]->expr.elems[2]->ground.ival == (int64_t)beta &&
        actual->expr.elems[1]->expr.elems[3]->ground.ival == (int64_t)beta + 1,
        "next span starts after removed trivia");
    check(actual->expr.elems[1] == actual->expr.elems[2], "CST DAG sharing survives source relocation");
    check(actual->expr.elems[3]->expr.elems[2]->ground.ival == (int64_t)beta &&
        actual->expr.elems[3]->expr.elems[3]->ground.ival == (int64_t)beta,
        "empty span has an explicit right-biased boundary");
    check(headed(located->expr.elems[2], "re:lexed-forest-v1", 2u) &&
        atom_eq(located->expr.elems[2]->expr.elems[1], forest), "forest keeps its own coordinates with explicit provenance");
    check(shared->expr.elems[2]->ground.ival == 5 && atom_hash(shared) == old_hash,
        "relocation never mutates the original hashed CST");
    Atom *rejected = atom_expr(a, (Atom *[]){atom_symbol(a, "ParserRejectedV1"),
        atom_int(a, 5), atom_int(a, 6), atom_symbol(a, "expected")}, 4u);
    Atom *error = cetta_re_v1_locate_source(a, rejected, mapping);
    check(error->expr.elems[1]->ground.ival == (int64_t)beta &&
        error->expr.elems[2]->ground.ival == (int64_t)units[beta].offset,
        "parser refusal reports original scalar and byte positions");
    Atom *eof = atom_expr(a, (Atom *[]){atom_symbol(a, "ParserIncompleteV1"),
        atom_int(a, 6), atom_int(a, 8), atom_string(a, "unfinished")}, 4u);
    error = cetta_re_v1_locate_source(a, eof, mapping);
    check(error->expr.elems[1]->ground.ival == (int64_t)len &&
        error->expr.elems[2]->ground.ival == (int64_t)strlen(source), "EOF maps to original EOF");
    error = cetta_re_v1_locate_source(a, cst(a, 0u, 100u), mapping);
    check(headed(error, "re:source-fault", 3u), "out-of-source CST span is not clamped");
    error = cetta_re_v1_locate_source(a, rejected, atom_symbol(a, "bad-map"));
    check(headed(error, "re:source-fault", 3u), "invalid provenance is not treated as identity");
    Atom *wrong_byte = atom_expr(a, (Atom *[]){atom_symbol(a, "ParserRejectedV1"),
        atom_int(a, 5), atom_int(a, 5), atom_symbol(a, "expected")}, 4u);
    error = cetta_re_v1_locate_source(a, wrong_byte, mapping);
    check(headed(error, "re:source-fault", 3u), "a byte inside a UTF-8 scalar is not repaired silently");
    free(units);
}
static void deep(Arena *a) {
    size_t depth = 20000u;
    CettaByteBuffer source; cetta_byte_buffer_init(&source);
    cetta_byte_buffer_append(&source, "(?x)", 4u);
    for (size_t i = 0u; i < depth; ++i) cetta_byte_buffer_append_byte(&source, '(');
    cetta_byte_buffer_append(&source, " a ", 3u);
    for (size_t i = 0u; i < depth; ++i) cetta_byte_buffer_append_byte(&source, ')');
    Atom *out = cetta_re_v1_prepare_text(a, atom_string_n(a, source.bytes, source.len));
    check(headed(out, "re:source-text", 2u) && atom_string_len(out->expr.elems[1]) == source.len - 2u,
        "deep lexical scopes use an explicit stack without a default depth cap");
    Atom *tree = cst(a, 4u + depth, 5u + depth);
    for (size_t i = 0u; i < depth; ++i) tree = atom_expr2(a, atom_symbol(a, "wrapper"), tree);
    Atom *located = cetta_re_v1_locate_source(a, tree, out->expr.elems[2]);
    check(located && !headed(located, "re:source-fault", 3u), "deep CST relocation uses an explicit stack");
    cetta_byte_buffer_free(&source);
    cetta_byte_buffer_init(&source);
    cetta_byte_buffer_append(&source, "(?x)", 4u);
    for (size_t i = 0u; i < depth; ++i) cetta_byte_buffer_append_byte(&source, '[');
    cetta_byte_buffer_append(&source, " # ", 3u);
    for (size_t i = 0u; i < depth; ++i) cetta_byte_buffer_append_byte(&source, ']');
    cetta_byte_buffer_append(&source, " a", 2u);
    out = cetta_re_v1_prepare_text(a, atom_string_n(a, source.bytes, source.len));
    check(headed(out, "re:source-text", 2u) && atom_string_len(out->expr.elems[1]) == source.len - 1u,
        "deep class ambiguity uses compact depth intervals and retains internal trivia");
    cetta_byte_buffer_free(&source);
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    Arena arena; arena_init(&arena); g_hashcons = NULL; g_var_intern = NULL;
    lexical(&arena); locations(&arena); deep(&arena);
    HashConsTable shared; hashcons_init(&shared); arena_set_hashcons(&arena, &shared);
    locations(&arena);
    arena_set_hashcons(&arena, NULL); hashcons_free(&shared); arena_free(&arena);
    symbol_table_free(&symbols); g_symbols = NULL;
    printf("RegexTextV1Summary %u %u\n", passed, failed);
    return failed ? 1 : 0;
}
