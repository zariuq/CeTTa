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

static bool fail_malloc;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) {
    if (fail_malloc) { fail_malloc = false; return NULL; }
    return __real_malloc(n);
}
static unsigned checks;
static bool same(Atom *a, const char *text) {
    return a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING &&
        atom_string_len(a) == strlen(text) && !memcmp(a->ground.sval,text,strlen(text));
}
static void strip(Arena *arena, const char *source, Atom *chars, const char *expected) {
    const CettaStrNative *op = cetta_str_native("strip-python");
    Atom *args[] = {atom_string(arena,source), chars}; CettaStrRefusal refusal = {0};
    assert(same(op->fn(arena,args,&refusal),expected)); checks++;
}
int main(void) {
    SymbolTable symbols; symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms); g_symbols = &symbols;
    Arena input,out; arena_init_detached(&input); arena_init_detached(&out);
    const CettaStrNative *op = cetta_str_native("sha256-chain"); assert(op && op->arity == 2);
    Atom *parts[] = {atom_string(&input,"a"),atom_string(&input,"a"),atom_string(&input,"")};
    Atom *args[] = {atom_string(&input,""),atom_expr(&input,parts,3)};
    CettaStrRefusal refusal = {0}; Atom *value = op->fn(&out,args,&refusal);
    assert(value && value->kind == ATOM_EXPR && value->expr.len == 3 && atom_is_symbol(value->expr.elems[0],"Sha256ChainV1")); checks++;
    assert(same(value->expr.elems[1],"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")); checks++;
    Atom *rows = value->expr.elems[2]; assert(rows->expr.len == 3); checks++;
    const char *prefixes[] = {
        "3b196fd4907bedf51c3090e9835f2f7cb61e7ee1b2299ea3b8fed9b4183a822a",
        "07b9822b722b7dc299d0a6c12f4e7239b2431ccd0146cdafa5715318a24e0e7a",
        "7d350ea3a6b72d0055ffc411c462ee74054044a518849e033d1c39acdf87015b"};
    for (unsigned i = 0; i < 3; ++i) { assert(same(rows->expr.elems[i]->expr.elems[2],prefixes[i])); checks++; }
    assert(same(rows->expr.elems[0]->expr.elems[1],"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb")); checks++;
    assert(same(rows->expr.elems[1]->expr.elems[1],"ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb")); checks++;
    assert(!same(rows->expr.elems[1]->expr.elems[2],rows->expr.elems[0]->expr.elems[2]->ground.sval)); checks++;
    assert(rows->expr.elems[2]->expr.elems[0]->ground.ival == 0); checks++;
    args[1] = atom_expr(&input,NULL,0); value = op->fn(&out,args,&refusal);
    assert(value->expr.elems[2]->expr.len == 0); checks++;
    args[1] = atom_expr(&input,parts,3);
    arena_set_block_growth(&out,65536); (void)arena_alloc(&out,1);
    fail_malloc = true; refusal = (CettaStrRefusal){0};
    assert(!op->fn(&out,args,&refusal) && !fail_malloc && !strcmp(refusal.reason,"StrCapacityV1")); checks++;
    args[1] = atom_int(&input,2); refusal = (CettaStrRefusal){0};
    assert(!op->fn(&out,args,&refusal) && refusal.expected); checks++;
    Atom *bad[] = {parts[0],atom_int(&input,2)}; args[1] = atom_expr(&input,bad,2); refusal = (CettaStrRefusal){0};
    assert(!op->fn(&out,args,&refusal) && refusal.expected); checks++;
    Atom *space = atom_symbol(&input,"PythonWhitespace");
    strip(&input,"",space,""); strip(&input," \t\r\nx \n",space,"x");
    strip(&input,"a  b",space,"a  b"); strip(&input,"abc",atom_string(&input,"ab"),"c");
    strip(&input,"β🌿x🌿β",atom_string(&input,"🌿β"),"x"); strip(&input," x ",atom_string(&input,"")," x ");
    uint32_t whitespace[] = {9,10,11,12,13,28,29,30,31,32,133,160,5760,8192,8193,8194,8195,
        8196,8197,8198,8199,8200,8201,8202,8232,8233,8239,8287,12288};
    for (size_t i = 0; i < sizeof(whitespace)/sizeof(*whitespace); ++i) {
        char text[16]; uint8_t bytes[4]; size_t n = cetta_utf8_encode(whitespace[i],bytes);
        memcpy(text,bytes,n);text[n]='x';memcpy(text+n+1,bytes,n);text[n*2+1]=0;strip(&input,text,space,"x");
    }
    op = cetta_str_native("strip-python"); Atom *strip_args[] = {atom_string_n(&input,"\0x\0",3),atom_string_n(&input,"\0",1)};
    assert(same(op->fn(&out,strip_args,&refusal),"x")); checks++;
    strip_args[0] = atom_string_n(&input,"a\xff" "b",3); strip_args[1] = space; refusal = (CettaStrRefusal){0};
    assert(!op->fn(&out,strip_args,&refusal) && !strcmp(refusal.reason,"StrMalformedUtf8V1") && refusal.offset == 1); checks++;
    arena_free(&out); arena_free(&input); symbol_table_free(&symbols);
    printf("PASS: %u framed hash and Unicode strip controls\n",checks);
}
