#define _POSIX_C_SOURCE 200809L
#include "match.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

static unsigned checks;
static Atom *chain(Arena *arena, Atom *tag, Atom *leaf, unsigned size) {
    while (size--) leaf=atom_expr2(arena,tag,leaf);
    return leaf;
}
static Atom *tip(Atom *term,unsigned depth) {
    while (depth--) {
        assert(term && term->kind==ATOM_EXPR && term->expr.len==2);
        term=term->expr.elems[1];
    }
    return term;
}
static Atom *rewrite(Arena *arena,Atom *var,void *context) {
    (void)arena;(void)var;
    (*(unsigned *)context)++;
    return context ? var : NULL;
}
static void expect_tip(const Bindings *bindings,Arena *arena,Atom *term,
                       unsigned depth,Atom *expected) {
    Atom *image=bindings_apply_value(bindings,arena,binding_value_from_atom(term));
    assert(image && atom_eq(tip(image,depth),expected));
    checks++;
}
static uint64_t ns(void) {
    struct timespec value;
    assert(clock_gettime(CLOCK_MONOTONIC,&value)==0);
    return (uint64_t)value.tv_sec*1000000000u+(uint64_t)value.tv_nsec;
}
int main(int argc,char **argv) {
    SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    Arena arena;arena_init_detached(&arena);
    Atom *tag=atom_symbol(&arena,"node");
    Atom *x=atom_var_with_id(&arena,"x",fresh_var_id());
    Atom *y=atom_var_with_id(&arena,"y",fresh_var_id());
    Atom *unrelated=atom_var_with_id(&arena,"unrelated",fresh_var_id());
    Atom *leaf=atom_int(&arena,42);
    Bindings base;bindings_init(&base);
    assert(bindings_add_id_acyclic(&base,unrelated->var_id,unrelated->sym_id,leaf));
    BindingsBuilder builder;assert(bindings_builder_init(&builder,&base));
    const Bindings *bindings=bindings_builder_bindings(&builder);
    if (argc>1 && (strcmp(argv[1],"bench")==0 || strcmp(argv[1],"bench-mixed")==0)) {
        bool mixed_mode=strcmp(argv[1],"bench-mixed")==0;
        if (mixed_mode) assert(bindings_builder_add_var_fresh(&builder,y,leaf));
        const unsigned sizes[]={32,256,2048,16384};
        unsigned repetitions=mixed_mode ? 2000u : 10000u;
        for (unsigned i=0;i<4;i++) {
            Atom *term=chain(&arena,tag,x,sizes[i]);
            Atom *input=mixed_mode ? atom_expr3(&arena,tag,term,y) : term;
            uint64_t start=ns();
            for (unsigned repeat=0;repeat<repetitions;repeat++) {
                Atom *image=bindings_apply_value(bindings,&arena,binding_value_from_atom(input));
                assert(image && (mixed_mode
                    ? image->expr.elems[1]==term && atom_eq(image->expr.elems[2],leaf)
                    : image==term));
            }
            printf("size=%u calls=%u elapsed_ns=%llu\n",sizes[i],repetitions,
                   (unsigned long long)(ns()-start));
        }
    } else {
        Atom *term=chain(&arena,tag,x,32);
        for (unsigned round=0;round<1000;round++) {
            uint32_t clear=bindings_builder_save(&builder);
            expect_tip(bindings,&arena,term,32,x);
            assert(bindings_builder_add_var_fresh(&builder,x,y));
            uint32_t alias=bindings_builder_save(&builder);
            expect_tip(bindings,&arena,term,32,y);
            assert(bindings_builder_add_var_fresh(&builder,y,leaf));
            expect_tip(bindings,&arena,term,32,leaf);
            bindings_builder_rollback(&builder,alias);
            expect_tip(bindings,&arena,term,32,y);
            bindings_builder_rollback(&builder,clear);
            expect_tip(bindings,&arena,term,32,x);
        }
        uint32_t before_mixed=bindings_builder_save(&builder);
        Atom *mixed=atom_expr3(&arena,tag,term,y);
        assert(var_id_is_none(atom_single_variable_id(mixed)));
        assert(bindings_builder_add_var_fresh(&builder,y,leaf));
        Atom *mixed_image=bindings_apply_value(bindings,&arena,binding_value_from_atom(mixed));
        assert(mixed_image && mixed_image->expr.elems[1]==term && atom_eq(mixed_image->expr.elems[2],leaf));checks++;
        unsigned mixed_hooks=0;
        assert(bindings_apply_rewrite_vars((Bindings *)bindings,&arena,mixed,rewrite,&mixed_hooks));
        assert(mixed_hooks==1);checks++;
        assert(bindings_builder_add_var_fresh(&builder,x,y));
        mixed_image=bindings_apply_value(bindings,&arena,binding_value_from_atom(mixed));
        assert(mixed_image && atom_eq(tip(mixed_image->expr.elems[1],32),leaf));checks++;
        bindings_builder_rollback(&builder,before_mixed);
        mixed_image=bindings_apply_value(bindings,&arena,binding_value_from_atom(mixed));
        assert(mixed_image==mixed);checks++;
        unsigned hooks=0;
        assert(bindings_apply_rewrite_vars((Bindings *)bindings,&arena,term,rewrite,&hooks));
        assert(hooks==1);checks++;
        CettaFrameIdentity first,second;
        assert(cetta_frame_identity_acquire(&first));
        assert(cetta_frame_identity_acquire(&second));
        VarId ids[]={x->var_id};
        assert(bindings_builder_register_contextual_frame(&builder,ids,1,first));
        assert(bindings_builder_register_contextual_frame(&builder,ids,1,second));
        Atom *qualified=atom_var_with_id(&arena,"qualified",var_epoch_id(x->var_id,first));
        assert(bindings_builder_add_var_fresh(&builder,qualified,leaf));
        Atom *image=bindings_apply_value(bindings,&arena,binding_value_from_context(term,first));
        assert(image && atom_eq(tip(image,32),leaf));checks++;
        image=bindings_apply_value(bindings,&arena,binding_value_from_context(term,second));
        assert(image && tip(image,32)->kind==ATOM_VAR &&
               var_id_eq(tip(image,32)->var_id,var_epoch_id(x->var_id,second)));checks++;
        image=bindings_apply_value(bindings,&arena,binding_value_from_persistent_context(term,first));
        assert(image && atom_eq(tip(image,32),leaf));checks++;
        bindings_builder_free(&builder);bindings_free(&base);
        arena_free(&arena);cetta_frame_identity_release(first);cetta_frame_identity_release(second);
        printf("PASS: %u substitution controls; live aliases, rollback, hooks and lexical frame separation\n",checks);
        symbol_table_free(&symbols);g_symbols=NULL;
        return 0;
    }
    bindings_builder_free(&builder);bindings_free(&base);arena_free(&arena);
    symbol_table_free(&symbols);g_symbols=NULL;return 0;
}
