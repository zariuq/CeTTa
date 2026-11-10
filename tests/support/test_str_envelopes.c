#include "atom.h"
#include "str_natives.h"
#include "utf8.h"
#include "stats.h"
#if CETTA_BUILD_WITH_RUNTIME_STATS
#include "tests/test_runtime_stats_stubs.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long fail_at = -1, allocations;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    if (fail_at >= 0 && allocations++ == fail_at) return NULL;
    return __real_malloc(size);
}
void *__wrap_realloc(void *old, size_t size) {
    if (fail_at >= 0 && allocations++ == fail_at) return NULL;
    return __real_realloc(old, size);
}
static const CettaStrNative *operation;
static unsigned checks;
static bool same(Atom *atom, const char *bytes, size_t len) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING &&
        atom->ground.slen == len && !memcmp(atom->ground.sval, bytes, len);
}
static void check_text(Arena *arena, const char *in, const char *head, int depth,
                       const char *out) {
    CettaStrRefusal refusal = {0};
    Atom *args[] = {atom_string(arena, in), atom_string(arena, head), atom_int(arena, depth)};
    Atom *result = operation->fn(arena, args, &refusal);
    assert(same(result, out, strlen(out)));
    checks++;
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms); g_symbols = &symbols;
    Arena arena; arena_init_detached(&arena);
    operation = cetta_str_native("unwrap-envelopes"); assert(operation && operation->arity == 3);
    check_text(&arena, "((batch (send \"x\") (batch (pin y))))", "batch", 64,
               "((send \"x\") (pin y))");
    check_text(&arena, "pre (batch (a) (a)) ) post", "batch", 64, "((a) (a))");
    check_text(&arena, "pre (a) post", "batch", 64, "pre (a) post");
    check_text(&arena, "(batch)", "batch", 64, "()");
    check_text(&arena, "(batch ())", "batch", 64, "(())");
    check_text(&arena, "(batch (run (batch (effect))))", "batch", 64,
               "((run (batch (effect))))");
    check_text(&arena, "(batch (send \"(batch (pin note))\"))", "batch", 64,
               "((send \"(batch (pin note))\"))");
    check_text(&arena, "(batch (send \"a\\\"(b)\\\\c\"))", "batch", 64,
               "((send \"a\\\"(b)\\\\c\"))");
    const char *unchanged[] = {"(batch \"data\")", "(batch (a) junk)", "(batch(a))",
        "(batch (a)) (open", "(batch (a)) \"open", "(batch (send \"unfinished))"};
    for (size_t i = 0; i < sizeof(unchanged)/sizeof(*unchanged); i++)
        check_text(&arena, unchanged[i], "batch", 64, unchanged[i]);
    check_text(&arena, "(batch (a))", "batch", 0, "(batch (a))");
    check_text(&arena, "(batch (batch (a)))", "batch", 1, "((batch (a)))");
    check_text(&arena, "(batch (batch (a)))", "batch", 2, "((a))");
    check_text(&arena, "((batch (a)))", "batch", 1, "((batch (a)))");
    check_text(&arena, "(обёртка (λ 😀))", "обёртка", 64, "((λ 😀))");
    uint32_t whitespace[] = {9,10,11,12,13,28,29,30,31,32,133,160,5760,8192,
        8193,8194,8195,8196,8197,8198,8199,8200,8201,8202,8232,8233,8239,8287,12288};
    for (size_t i = 0; i < sizeof(whitespace)/sizeof(*whitespace); i++) {
        char bytes[5] = {0}, source[64];
        size_t width = cetta_utf8_encode(whitespace[i], (uint8_t *)bytes); bytes[width] = 0;
        snprintf(source, sizeof(source), "(%sbatch%s(a)%s)", bytes, bytes, bytes);
        check_text(&arena, source, "batch", 64, "((a))");
    }
    check_text(&arena, "(batch\xe2\x80\x8b(a))", "batch", 64, "(batch\xe2\x80\x8b(a))");
    char nul[] = "(batch (send \"a\0b\"))";
    char expected[] = "((send \"a\0b\"))";
    Atom *args[] = {atom_string_n(&arena, nul, sizeof(nul)-1),
                   atom_string(&arena, "batch"), atom_int(&arena, 64)};
    CettaStrRefusal refusal = {0};
    assert(same(operation->fn(&arena, args, &refusal), expected, sizeof(expected)-1)); checks++;
    args[0] = atom_string_n(&arena, "\xff", 1);
    assert(!operation->fn(&arena, args, &refusal) && !strcmp(refusal.reason,"StrMalformedUtf8V1")); checks++;
    args[0] = atom_int(&arena, 1); refusal = (CettaStrRefusal){0};
    assert(!operation->fn(&arena, args, &refusal) && refusal.expected); checks++;
    args[0] = atom_string(&arena,"(batch (a))"); args[2] = atom_int(&arena,-1);
    assert(!operation->fn(&arena,args,&refusal) && refusal.expected); checks++;
    args[2] = atom_int(&arena,64); args[1] = atom_string(&arena,"bad head");
    assert(!operation->fn(&arena,args,&refusal) && refusal.expected); checks++;
    /* Deep anonymous nesting exercises frame growth, with a long retained
     * leaf to force output growth. Inject faults only after arena capacity
     * and argument atoms are ready; a failed scan returns no partial text. */
    char deep[8192], wanted[4096]; size_t at = 0;
    for (int i = 0; i < 60; i++) deep[at++] = '(';
    memcpy(deep+at,"batch (leaf ",12); at += 12;
    memset(deep+at,'x',2048); at += 2048; deep[at++] = ')';
    for (int i = 0; i < 60; i++) deep[at++] = ')';
    deep[at] = 0;
    strcpy(wanted,"((leaf "); memset(wanted+7,'x',2048); strcpy(wanted+2055,"))");
    args[0] = atom_string(&arena,deep); args[1] = atom_string(&arena,"batch");
    Arena output_arena; arena_init_detached(&output_arena);
    arena_set_block_growth(&output_arena, 65536);
    (void)arena_alloc(&output_arena, 1);
    ArenaMark mark = arena_mark(&output_arena);
    long failures = 0;
    for (long cut = 0; cut < 32; cut++) {
        arena_reset(&output_arena, mark); allocations = 0; fail_at = cut; refusal = (CettaStrRefusal){0};
        Atom *result = operation->fn(&output_arena,args,&refusal); fail_at = -1;
        if (result) { assert(same(result,wanted,strlen(wanted))); break; }
        assert(refusal.reason && !strcmp(refusal.reason,"StrCapacityV1")); failures++;
    }
    assert(failures >= 5); checks++;
    arena_free(&output_arena); arena_free(&arena); symbol_table_free(&symbols);
    printf("PASS: %u envelope controls and %ld allocation refusals; no partial output\n",checks,failures);
}
