#include "../../src/open_equation_machine.c"
#include <assert.h>

static unsigned polls;
static bool stop(void *context) { (void)context; polls++; return true; }

int main(void) {
    SymbolTable symbols;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);
    g_symbols=&symbols;
    CettaOpenEquationProgram program={0};
    program.spelling=symbol_intern_cstr(&symbols,"drop-var");
    CettaOpenEquationCursor cursor={0};
    cursor.program=&program;
    assert(cetta_frame_identity_acquire(&cursor.epoch));
    arena_init_detached(&cursor.names);
    arena_init_detached(&cursor.region);
    Arena source;
    arena_init_detached(&source);
    Atom *end=atom_symbol(&source,"DropEnd");
    Atom *link=atom_symbol(&source,"DropLink");
    OemRelation relation={.algebraic_nil=end->sym_id,.algebraic_cons=link->sym_id};
    Atom *tail=oem_new_cell(&cursor),*item=oem_new_cell(&cursor);
    assert(tail && item);
    Atom *spines[701];
    spines[0]=end;
    for(unsigned n=1;n<701;n++) {
        spines[n]=atom_expr3(&source,link,item,spines[n-1]);
        assert(spines[n]);
    }
    uint64_t checks=0;
    for(unsigned n=0;n<=128;n++)for(unsigned count=0;count<=n+3;count++) {
        Atom *amount=atom_int(&source,count),*out=NULL;
        size_t bytes=arena_accounted_live_bytes(&cursor.region);
        assert(oem_algebraic_drop(&cursor,&relation,amount,spines[n],&out)==
            OEM_ALGEBRAIC_READY);
        assert(out==spines[count>=n?0:n-count]);
        assert(bytes==arena_accounted_live_bytes(&cursor.region));
        checks++;
    }
    Atom *zero=atom_int(&source,0),*one=atom_int(&source,1),*out=NULL;
    Atom *partial=atom_expr3(&source,link,item,tail);
    assert(oem_algebraic_drop(&cursor,&relation,one,partial,&out)==OEM_ALGEBRAIC_READY);
    assert(out==tail);
    OemTrial trial;
    oem_trial_begin(&cursor,&trial);
    assert(oem_unify(&cursor,tail,end)==OEM_UNIFY_OK);
    assert(oem_deref(&cursor,out)==end);
    oem_trial_end(&cursor,&trial,OEM_UNIFY_FAIL);
    assert(oem_deref(&cursor,out)==tail);
    out=NULL;
    assert(oem_algebraic_drop(&cursor,&relation,atom_int(&source,2),partial,&out)==
        OEM_ALGEBRAIC_NOT_APPLICABLE);
    assert(out==NULL);
    assert(oem_algebraic_drop(&cursor,&relation,atom_int(&source,-1),spines[2],&out)==
        OEM_ALGEBRAIC_NOT_APPLICABLE);
    assert(oem_algebraic_drop(&cursor,&relation,atom_float(&source,1.0),spines[2],&out)==
        OEM_ALGEBRAIC_NOT_APPLICABLE);
    assert(oem_algebraic_drop(&cursor,&relation,item,spines[2],&out)==
        OEM_ALGEBRAIC_NOT_APPLICABLE);
    Atom *other=atom_symbol(&source,"Other");
    assert(oem_algebraic_drop(&cursor,&relation,one,other,&out)==OEM_ALGEBRAIC_NOT_APPLICABLE);
    assert(oem_algebraic_drop(&cursor,&relation,zero,other,&out)==OEM_ALGEBRAIC_READY && out==other);
    /* Additive metadata selects only proper child fields. */
    Atom *tip=atom_symbol(&source,"SumTip"),*fork=atom_symbol(&source,"SumFork");
    OemScalarCall children[2]={{0u,0u},{0u,1u}};
    OemScalarArm arms[2]={
        {.tag=tip->sym_id,.fields=1u,.value={.base=1u}},
        {.tag=fork->sym_id,.fields=2u,.value={.base=1u,.calls=children,.count=2u}}};
    OemScalarFunction functions[1]={{.head=0u,.arms=arms,.count=2u}};
    OemScalarProgram scalar={.functions=functions,.len=1u};
    Atom *leaf=atom_expr2(&source,tip,item);
    Atom *trees[201];uint64_t weights[201];trees[0]=leaf;weights[0]=1u;
    for(unsigned index=1u;index<201u;index++) {
        unsigned left=index%7u,right=(index*3u)%7u;
        if(left>=index)left=0u;
        if(right>=index)right=0u;
        trees[index]=atom_expr3(&source,fork,trees[left],trees[right]);
        weights[index]=1u+weights[left]+weights[right];
    }
    for(unsigned index=0u;index<201u;index++) {
        out=NULL;
        assert(oem_scalar_sum(&cursor,&scalar,trees[index],&out)==OEM_ALGEBRAIC_READY);
        assert(out && out->kind==ATOM_GROUNDED && out->ground.gkind==GV_INT &&
               (uint64_t)out->ground.ival==weights[index]);
        checks++;
    }
    Atom *deep=leaf;
    for(unsigned depth=0u;depth<100u;depth++)deep=atom_expr3(&source,fork,deep,leaf);
    out=NULL;
    assert(oem_scalar_sum(&cursor,&scalar,deep,&out)==OEM_ALGEBRAIC_READY && out->ground.ival==201);
    out=NULL;
    assert(oem_scalar_sum(&cursor,&scalar,other,&out)==OEM_ALGEBRAIC_NOT_APPLICABLE && out==NULL);
    out=NULL;
    assert(oem_scalar_sum(&cursor,&scalar,item,&out)==OEM_ALGEBRAIC_NOT_APPLICABLE && out==NULL);
    arms[1].value.base=(uint64_t)INT64_MAX;
    out=NULL;
    assert(oem_scalar_sum(&cursor,&scalar,trees[1],&out)==OEM_ALGEBRAIC_NOT_APPLICABLE && out==NULL);
    arms[1].value.base=1u;
    cursor.runtime.interrupt=stop;
    out=NULL;
    assert(oem_algebraic_drop(&cursor,&relation,atom_int(&source,700),spines[700],&out)==
        OEM_ALGEBRAIC_HANDOFF);
    assert(polls==1 && out==NULL && cursor.handoff==CETTA_OPEN_EQUATION_HANDOFF_INTERRUPT);
    printf("PASS: %llu suffix/fold results, live binding/rollback, suffix allocation, refusal and interruption\n",
        (unsigned long long)checks);
    free(cursor.cells);free(cursor.cell_names);free(cursor.trail);free(cursor.pairs);
    free(cursor.walk);free(cursor.occurs_seen.buckets);free(cursor.occurs_seen.dense);
    free(cursor.occurs_close);free(cursor.ground_facts);
    arena_free(&source);arena_free(&cursor.region);arena_free(&cursor.names);
    cetta_frame_identity_release(cursor.epoch);
    symbol_table_free(&symbols);g_symbols=NULL;
    return 0;
}
