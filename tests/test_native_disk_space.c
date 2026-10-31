/* Fresh-process qualification of the owning source adapter. Collection count
 * metadata below is a fixture convention, not a new public Space format. */
#include "native_disk_space.h"
#include "atom_wire.h"
#include "parser.h"
#include "petta_search_machine.h"
#include "library_pattern.h"
#include "native_query.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CettaDiskBytes bytes(const char *s) { return (CettaDiskBytes){(const unsigned char *)s,strlen(s)}; }
static Atom *parse(Arena *arena,const char *text) {
    Atom **forms=NULL;int n=parse_metta_text(text,arena,&forms);assert(n==1 && forms);
    Atom *atom=forms[0];free(forms);return atom;
}
static void count_bytes(unsigned char out[8],uint64_t count) {
    for(size_t i=8;i>0;--i) { out[i-1]=(unsigned char)count;count>>=8; }
}
static uint64_t read_count(const CettaDiskStoreView *view) {
    CettaDiskBlob blob;unsigned char data[8];size_t got=0;
    const CettaDiskRoot *root=cetta_disk_store_view_root(view);
    assert(cetta_disk_lookup(root,bytes("meta/count"),&blob)==CETTA_DISK_OK && blob.size==8);
    assert(cetta_disk_blob_read(root,blob,0,data,8,&got)==CETTA_DISK_OK && got==8);
    uint64_t n=0;for(size_t i=0;i<8;i++) n=(n<<8)|data[i];return n;
}
static void write_initial(CettaDiskStore *store,CettaDiskStoreView *base,Arena *arena) {
    const char *keys[]={"facts/a","facts/b","facts/c","facts/r"};
    const char *values[]={"(edge alice bob)","(edge alice bob)","(rel $x $x)",
        "(= (do-not-run) (print! should-not-print))"};
    unsigned char *encoded[4],count[8];CettaDiskOp operations[6];
    for(size_t i=0;i<4;i++) {
        size_t size=0;assert(cetta_atom_wire_encode(parse(arena,values[i]),&encoded[i],&size)==ATOM_WIRE_OK);
        operations[i]=(CettaDiskOp){false,bytes(keys[i]),{encoded[i],size}};
    }
    count_bytes(count,4);
    operations[4]=(CettaDiskOp){false,bytes("meta/count"),{count,8}};
    operations[5]=(CettaDiskOp){false,bytes("receipt/seed"),bytes("committed-seed-v1")};
    CettaDiskStoreView *result=NULL;
    assert(cetta_disk_store_commit(store,base,operations,6,&result)==CETTA_STORE_OK);
    cetta_disk_store_view_close(result);for(size_t i=0;i<4;i++) free(encoded[i]);
}
static PettaMachineHostMode classify(void *ctx,Space *space,Atom *call) {
    (void)ctx;(void)space;
    return atom_head_symbol_id(call)==g_builtin_syms.pat_query ? PETTA_MACHINE_HOST_PATTERN_QUERY : PETTA_MACHINE_HOST_NONE;
}
static Space *resolve(void *ctx,Space *space,Arena *arena,Atom *reference) {
    (void)ctx;(void)arena;return atom_is_symbol(reference,"&self") ? space : NULL;
}
static size_t query(Space *space,Arena *arena,const char *expression,const char *value) {
    PettaMachineHost host={.classify=classify,.resolve_space=resolve};PettaMachine machine;
    assert(petta_machine_init(&machine,space,arena,parse(arena,expression),NULL,&host));
    size_t count=0;PettaMachineStep step;Atom *answer=NULL;
    do {
        Bindings bindings;step=petta_machine_next(&machine,&answer,&bindings);bindings_free(&bindings);
        if(step==PETTA_MACHINE_STEP_ANSWER) { ++count;if(value) assert(atom_eq(answer,parse(arena,value))); }
    } while(step==PETTA_MACHINE_STEP_ANSWER);
    assert(step==PETTA_MACHINE_STEP_EXHAUSTED);petta_machine_destroy(&machine);return count;
}
static size_t drain(SpaceOccurrenceCursor *cursor) {
    size_t count=0;CettaIndex index;SpaceOccurrenceCursorStep step;
    while((step=space_occurrence_cursor_next(cursor,&index,NULL))==SPACE_OCCURRENCE_CURSOR_ITEM) count++;
    assert(step==SPACE_OCCURRENCE_CURSOR_END);return count;
}
int main(int argc,char **argv) {
    assert(argc>=3);const char *mode=argv[1],*directory=argv[2];
    SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    Arena data,answers;arena_init_detached(&data);arena_init_detached(&answers);
    if(argc>3) for(unsigned i=0;i<(unsigned)strtoul(argv[3],NULL,10);i++) {
        char name[64];snprintf(name,sizeof(name),"restart-symbol-%u",i);(void)atom_symbol(&data,name);
    }
    CettaDiskStore *store=NULL;CettaDiskStoreView *view=NULL;
    assert(cetta_disk_store_open(directory,!strcmp(mode,"write"),NULL,&store)==CETTA_STORE_OK);
    assert(cetta_disk_store_snapshot(store,&view)==CETTA_STORE_OK);
    if(!strcmp(mode,"write")) {
        write_initial(store,view,&data);
        puts("DURABLE_ACK");fflush(stdout);
        raise(SIGSTOP);abort(); /* The parent kills us without cleanup. */
    }
    if(!strcmp(mode,"append") || !strcmp(mode,"corrupt")) {
        bool corrupt=!strcmp(mode,"corrupt");unsigned char count[8],*encoded=NULL;size_t size=0;
        if(!corrupt) assert(cetta_atom_wire_encode(parse(&data,"(edge alice carol)"),&encoded,&size)==ATOM_WIRE_OK);
        count_bytes(count,corrupt ? 4 : 5);
        CettaDiskOp operations[]={
            {false,bytes(corrupt ? "facts/r" : "facts/0"),corrupt ? bytes("not-a-CAV1-atom") : (CettaDiskBytes){encoded,size}},
            {false,bytes("meta/count"),{count,8}}};
        CettaDiskStoreView *result=NULL;assert(cetta_disk_store_commit(store,view,operations,2,&result)==CETTA_STORE_OK);
        cetta_disk_store_view_close(result);free(encoded);puts("PUBLISHED");
    } else if(!strcmp(mode,"compact")) {
        CettaDiskStoreView *result=NULL;uint64_t removed=0;
        assert(cetta_disk_store_compact(store,&result)==CETTA_STORE_OK);cetta_disk_store_view_close(result);
        assert(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK);puts("COMPACTED");
    } else {
        uint64_t count=read_count(view);CettaDiskBlob receipt;
        assert(cetta_disk_lookup(cetta_disk_store_view_root(view),bytes("receipt/seed"),&receipt)==CETTA_DISK_OK);
        TermUniverse universe;term_universe_init(&universe);term_universe_set_persistent_arena(&universe,&data);
        Space source;space_init_with_universe(&source,&universe);
        char prefix[]="facts/";
        assert(cetta_disk_space_capture(&source,view,bytes(prefix),count)==CETTA_STORE_OK);
        Atom *pattern=parse(&answers,"(edge alice $who)");
        CettaNativeQuery optimized={.source=&source,.patterns=&pattern,.pattern_count=1,
            .template_atom=pattern,.consumer=CETTA_NATIVE_QUERY_COLLECT};
        CettaNativeQueryResult optimized_result;
        assert(cetta_native_query_run(&optimized,&answers,&optimized_result)==CETTA_NATIVE_QUERY_DECLINED);
        uint64_t optimized_count=999;
        assert(!cetta_native_query_count(&source,&answers,&pattern,1,NULL,&optimized_count));
        assert(optimized_count==999);
        memset(prefix,'x',sizeof(prefix)-1); /* The adapter owns its prefix. */
        cetta_disk_store_view_close(view);view=NULL;cetta_disk_store_close(store);store=NULL;
        if(!strcmp(mode,"hold")) { puts("VIEW_CAPTURED");fflush(stdout);assert(getchar()=='!'); }
        SpaceOccurrenceCursor scan;space_occurrence_cursor_init_empty(&scan);assert(space_occurrence_cursor_init_scan(&source,&scan));
        if(!strcmp(mode,"check-corrupt")) {
            CettaIndex index;size_t rows=0;SpaceOccurrenceCursorStep step;
            while((step=space_occurrence_cursor_next(&scan,&index,NULL))==SPACE_OCCURRENCE_CURSOR_ITEM) rows++;
            assert(rows==3 && step==SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
            assert(space_occurrence_cursor_next(&scan,&index,NULL)==SPACE_OCCURRENCE_CURSOR_INCOMPLETE);
            PettaMachineHost host={.classify=classify,.resolve_space=resolve};PettaMachine machine;
            assert(petta_machine_init(&machine,&source,&answers,
                parse(&answers,"(match &self (edge alice $who) $who)"),NULL,&host));
            size_t yielded=0;PettaMachineStep result;Atom *answer=NULL;
            do {
                Bindings bindings;result=petta_machine_next(&machine,&answer,&bindings);bindings_free(&bindings);
                if(result==PETTA_MACHINE_STEP_ANSWER) yielded++;
            } while(result==PETTA_MACHINE_STEP_ANSWER);
            assert(yielded==2 && result==PETTA_MACHINE_STEP_HOST_ERROR);
            petta_machine_destroy(&machine);
            puts("CORRUPTION_IS_NOT_EMPTY_OR_COMPLETE");
        } else {
            CettaIndex index;assert(space_occurrence_cursor_next(&scan,&index,NULL)==SPACE_OCCURRENCE_CURSOR_ITEM);
            SpaceOccurrenceCursor clone;assert(space_occurrence_cursor_clone(&scan,&clone));
            Atom *first=space_occurrence_cursor_identity(&scan,index,&answers);
            assert(atom_eq(first,space_occurrence_cursor_identity(&clone,index,&answers)));
            assert(drain(&clone)==count-1);
            SpaceOccurrenceCursor ended;assert(space_occurrence_cursor_clone(&clone,&ended));assert(drain(&ended)==0);
            space_occurrence_cursor_release(&ended);space_occurrence_cursor_release(&clone);
            space_occurrence_cursor_release(&scan);assert(space_occurrence_cursor_init_scan(&source,&scan));
            size_t seen=0;SpaceOccurrenceCursorStep step;
            while((step=space_occurrence_cursor_next(&scan,&index,NULL))==SPACE_OCCURRENCE_CURSOR_ITEM) {
                Atom *identity=space_occurrence_cursor_identity(&scan,index,&answers);assert(identity && identity->kind==ATOM_EXPR && identity->expr.len==3);
                printf("IDENTITY %s %s\n",identity->expr.elems[1]->ground.sval,identity->expr.elems[2]->ground.sval);seen++;
            }
            assert(step==SPACE_OCCURRENCE_CURSOR_END && seen==count);
            SpaceOccurrenceCursor variables;assert(space_occurrence_cursor_init_scan(&source,&variables));
            Atom *row=NULL;
            do { assert(space_occurrence_cursor_next(&variables,&index,&row)==SPACE_OCCURRENCE_CURSOR_ITEM); }
            while(!atom_is_symbol(row->expr.elems[0],"rel"));
            SpaceOccurrenceCursor variable_clone;assert(space_occurrence_cursor_clone(&variables,&variable_clone));
            Atom *copied=space_occurrence_cursor_atom(&variable_clone,index);
            assert(copied && var_id_eq(row->expr.elems[1]->var_id,copied->expr.elems[1]->var_id));
            assert(var_id_eq(copied->expr.elems[1]->var_id,copied->expr.elems[2]->var_id));
            space_occurrence_cursor_release(&variable_clone);space_occurrence_cursor_release(&variables);
            assert(query(&source,&answers,"(match &self (edge alice $who) $who)",NULL)==count-2);
            assert(query(&source,&answers,"(match &self (rel 1 2) bad)",NULL)==0);
            assert(query(&source,&answers,"(match &self (rel 1 1) good)","good")==1);
            uint64_t revision=space_revision(&source);space_match_backend_clear_error();
            space_add(&source,parse(&data,"(should-not-be-added)"));
            assert(space_match_backend_last_error_code()==SPACE_MATCH_BACKEND_ERROR_READ_ONLY && space_revision(&source)==revision);
            space_match_backend_clear_error();
            assert(!space_match_backend_try_set(&source,SPACE_ENGINE_NATIVE));
            assert(space_match_backend_last_error_code()==SPACE_MATCH_BACKEND_ERROR_READ_ONLY && space_length64(&source)==count);
            assert(query(&source,&answers,"(match &self (edge alice $who) $who)",NULL)==count-2);
            space_match_backend_clear_error();puts("OWNED_SPACE_REOPEN_OK");
        }
        space_occurrence_cursor_release(&scan);space_free(&source);term_universe_free(&universe);
    }
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);
    arena_free(&answers);arena_free(&data);symbol_table_free(&symbols);return 0;
}
