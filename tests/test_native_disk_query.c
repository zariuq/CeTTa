/* An owning service cursor uses the ordinary Space matcher and retires its
 * private arenas. Counts/prefixes in this fixture are trusted host metadata. */
#include "native_disk_query.h"
#include "atom_wire.h"
#include "parser.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned fail_copy;
SpaceQueryCursor *__real_space_query_cursor_copy_to_arena(const SpaceQueryCursor *,Arena *,AtomDeepCopySession *);
SpaceQueryCursor *__wrap_space_query_cursor_copy_to_arena(const SpaceQueryCursor *source,Arena *arena,AtomDeepCopySession *atoms) {
    unsigned failure=fail_copy;fail_copy=0;
    if (failure==1) return NULL;
    SpaceQueryCursor *copy=__real_space_query_cursor_copy_to_arena(source,arena,atoms);
    if (failure==2) { space_query_cursor_free(copy);return NULL; }
    return copy;
}
static CettaDiskBytes bytes(const char *s) { return (CettaDiskBytes){(const unsigned char *)s,strlen(s)}; }
static Atom *parse(Arena *arena,const char *text) {
    Atom **forms=NULL;int count=parse_metta_text(text,arena,&forms);assert(count==1 && forms);
    Atom *atom=forms[0];free(forms);return atom;
}
static size_t descriptors(void) {
    DIR *directory=opendir("/proc/self/fd");
    if (!directory) return SIZE_MAX;
    size_t count=0;struct dirent *entry;
    while ((entry=readdir(directory))) if (strcmp(entry->d_name,".") && strcmp(entry->d_name,"..")) count++;
    closedir(directory);return count;
}
static size_t retained(CettaDiskQuery *query) {
    CettaStorageCensus census={0};cetta_disk_query_storage_census(query,&census);cetta_storage_finish(&census);
    if (census.incomplete) fprintf(stderr,"query census: %s\n",census.incomplete);
    assert(!census.incomplete && !census.overflow && !census.invalidated);
    size_t result=census.bytes;cetta_storage_census_free(&census);return result;
}
static CettaDiskQuery *open_query(CettaDiskStoreView *view,const char *prefix,uint64_t count,const char *syntax) {
    Arena arena;arena_init_detached(&arena);Atom *spec=parse(&arena,syntax);
    assert(spec->kind==ATOM_EXPR && spec->expr.len==2);
    CettaDiskQuery *query=NULL;
    assert(cetta_disk_query_open(view,bytes(prefix),count,spec->expr.elems[0],spec->expr.elems[1],&query)==CETTA_STORE_OK);
    arena_free(&arena);return query; /* Inputs must not be borrowed. */
}
static CettaDiskQueryStep next(CettaDiskQuery *query,uint64_t sequence,uint64_t work,size_t limit,CettaDiskBytes *packet,size_t *needed) {
    CettaDiskQueryStep step;
    do { step=cetta_disk_query_next(query,sequence,work,limit,packet,needed); }
    while (work && step==CETTA_DISK_QUERY_PAUSED);
    return step;
}
static Atom *decode(Arena *arena,CettaDiskBytes packet) {
    Atom *record=NULL;assert(cetta_atom_wire_decode(arena,packet.data,packet.size,&record)==ATOM_WIRE_OK);
    assert(record && record->kind==ATOM_EXPR && record->expr.len==3 && atom_is_symbol(record->expr.elems[0],"DiskQueryRowV1"));
    assert(record->expr.elems[2]->kind==ATOM_EXPR);return record;
}
static size_t drain(CettaDiskQuery *query,size_t expected_ids,size_t page,size_t *low,size_t *high) {
    uint64_t sequence=0;size_t rows=0;CettaDiskBytes packet;
    for (;;) {
        CettaDiskQueryStep step=next(query,sequence,1,CETTA_ATOM_WIRE_BYTES,&packet,NULL);
        if (step==CETTA_DISK_QUERY_END) break;
        assert(step==CETTA_DISK_QUERY_ROW);
        Arena output;arena_init_detached(&output);Atom *row=decode(&output,packet);
        assert(row->expr.elems[2]->expr.len==expected_ids);arena_free(&output);
        rows++;sequence++;
        if (page && rows%page==0) {
            assert(cetta_disk_query_checkpoint(query));size_t size=retained(query);
            if (low && size<*low) *low=size;
            if (high && size>*high) *high=size;
        }
    }
    return rows;
}
static CettaDiskStoreView *seed(CettaDiskStore *store,CettaDiskStoreView *base) {
    const char *values[]={"(p a)","(p a)","(q a)","(p $u)","(q b)","(rel $x $x)",
        "(= (do-not-run) (println! SHOULD_NOT_RUN))","(wide small)"};
    unsigned char *encoded[8];char keys[8][24];CettaDiskOp ops[8];Arena arena;arena_init_detached(&arena);
    for (size_t i=0;i<8;i++) {
        size_t length;assert(cetta_atom_wire_encode(parse(&arena,values[i]),&encoded[i],&length)==ATOM_WIRE_OK);
        snprintf(keys[i],sizeof(keys[i]),"facts/%04zu",i);
        ops[i]=(CettaDiskOp){false,bytes(keys[i]),{encoded[i],length}};
    }
    CettaDiskStoreView *view=NULL;assert(cetta_disk_store_commit(store,base,ops,8,&view)==CETTA_STORE_OK);
    for (size_t i=0;i<8;i++) free(encoded[i]);
    arena_free(&arena);return view;
}
static CettaDiskStoreView *put(CettaDiskStore *store,CettaDiskStoreView *base,const char *key,Atom *value) {
    unsigned char *encoded=NULL;size_t length;
    assert(cetta_atom_wire_encode(value,&encoded,&length)==ATOM_WIRE_OK);
    CettaDiskOp op={false,bytes(key),{encoded,length}};CettaDiskStoreView *view=NULL;
    assert(cetta_disk_store_commit(store,base,&op,1,&view)==CETTA_STORE_OK);free(encoded);return view;
}
static CettaDiskStoreView *point_queries(CettaDiskStore *store,CettaDiskStoreView *base,Arena *data) {
    CettaDiskQuery *query=NULL;
    Atom *pattern=parse(data,"(point $x)"),*projection=pattern->expr.elems[1];
    assert(cetta_disk_query_open_point(base,bytes("one"),pattern,projection,&query)==CETTA_STORE_ABSENT && !query);
    CettaDiskStoreView *child=put(store,base,"one/child",parse(data,"(point child)"));
    assert(cetta_disk_query_open_point(child,bytes("one"),pattern,projection,&query)==CETTA_STORE_ABSENT && !query);
    CettaDiskStoreView *captured=put(store,child,"one",parse(data,"(point original)"));cetta_disk_store_view_close(child);
    assert(cetta_disk_query_open_point(captured,bytes("one"),pattern,projection,&query)==CETTA_STORE_OK && query);
    assert(cetta_disk_query_checkpoint(query));
    CettaDiskStoreView *updated=put(store,captured,"one",parse(data,"(point updated)"));cetta_disk_store_view_close(captured);
    CettaDiskBytes packet;assert(next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
    Atom *row=decode(data,packet);assert(atom_is_symbol(row->expr.elems[1],"original"));
    Atom *ids=row->expr.elems[2];assert(ids->expr.len==1 &&
        atom_string_equals_cstr(ids->expr.elems[0]->expr.elems[2],"one"));
    assert(cetta_disk_query_checkpoint(query));
    assert(next(query,1,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_END);
    CettaDiskSpaceStatistics statistics={0};assert(cetta_disk_query_statistics(query,&statistics) && statistics.candidates==1);
    cetta_disk_query_close(query);
    assert(cetta_disk_query_open_point(updated,bytes("one"),pattern,projection,&query)==CETTA_STORE_OK);
    assert(drain(query,1,1,NULL,NULL)==1);cetta_disk_query_close(query);
    assert(cetta_disk_query_open_point(updated,bytes("one"),parse(data,"(point absent)"),projection,&query)==CETTA_STORE_OK);
    assert(drain(query,1,1,NULL,NULL)==0);cetta_disk_query_close(query);
    CettaDiskStoreView *with_empty=put(store,updated,"",parse(data,"(point empty-key)"));cetta_disk_store_view_close(updated);
    assert(cetta_disk_query_open_point(with_empty,bytes(""),pattern,projection,&query)==CETTA_STORE_OK);
    assert(drain(query,1,1,NULL,NULL)==1);cetta_disk_query_close(query);
    size_t before=descriptors();
    for(size_t i=0;i<1000;i++) {
        assert(cetta_disk_query_open_point(with_empty,bytes("one"),pattern,projection,&query)==CETTA_STORE_OK);
        if(i%2)assert(drain(query,1,1,NULL,NULL)==1);
        else assert(cetta_disk_query_cancel(query)==CETTA_DISK_QUERY_CANCELLED);
        cetta_disk_query_close(query);
    }
    assert(descriptors()==before);
    puts("PASS: exact point queries retain original identities/captured values, exclude prefix siblings and reuse shared matching");
    return with_empty;
}

int main(int argc,char **argv) {
    assert(argc==2);SymbolTable symbols;symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    Arena data;arena_init_detached(&data);
    CettaDiskStore *store=NULL;CettaDiskStoreView *empty=NULL;
    assert(cetta_disk_store_open(argv[1],true,NULL,&store)==CETTA_STORE_OK);
    assert(cetta_disk_store_snapshot(store,&empty)==CETTA_STORE_OK);
    CettaDiskStoreView *view=seed(store,empty);cetta_disk_store_view_close(empty);
    size_t baseline_fd=descriptors();CettaDiskBytes packet;size_t needed=0;

    CettaDiskQuery *query=open_query(view,"facts/",8,"((p a) same)");
    assert(cetta_disk_query_revision(query)==1);
    assert(cetta_disk_query_next(query,9,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_STALE);
    assert(cetta_disk_query_next(query,0,0,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_PAUSED && !packet.data);
    assert(next(query,0,1,0,&packet,&needed)==CETTA_DISK_QUERY_TOO_LARGE && needed>0 && !packet.data);
    assert(next(query,0,0,needed-1,&packet,NULL)==CETTA_DISK_QUERY_TOO_LARGE);
    assert(next(query,0,0,needed,&packet,NULL)==CETTA_DISK_QUERY_ROW);
    unsigned char *replay=malloc(packet.size);assert(replay);memcpy(replay,packet.data,packet.size);size_t replay_size=packet.size;
    for (unsigned fault=1;fault<=2;fault++) {
        fail_copy=fault;assert(!cetta_disk_query_checkpoint(query));
        assert(cetta_disk_query_next(query,0,0,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
        assert(packet.size==replay_size && !memcmp(packet.data,replay,replay_size));
    }
    assert(cetta_disk_query_checkpoint(query));
    assert(next(query,0,0,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW && !memcmp(packet.data,replay,replay_size));
    assert(next(query,1,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
    assert(packet.size!=replay_size || memcmp(packet.data,replay,replay_size));free(replay);
    assert(next(query,0,0,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_STALE);
    assert(next(query,1,0,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
    Arena output;arena_init_detached(&output);Atom *row=decode(&output,packet);
    assert(atom_is_symbol(row->expr.elems[1],"same"));arena_free(&output);
    assert(next(query,2,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
    assert(next(query,3,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_END);
    size_t terminal_bytes=retained(query);assert(descriptors()==baseline_fd);
    cetta_disk_query_cancel(query);assert(next(query,3,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_END);
    cetta_disk_query_close(query);

    query=open_query(view,"facts/",8,"((, (p $x) (q $x)) (answer $x $x))");
    unsigned a_count=0,b_count=0;
    for (uint64_t sequence=0;sequence<4;sequence++) {
        assert(next(query,sequence,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
        arena_init_detached(&output);row=decode(&output,packet);Atom *answer=row->expr.elems[1];
        assert(answer->kind==ATOM_EXPR && answer->expr.len==3 && atom_is_symbol(answer->expr.elems[0],"answer"));
        assert(atom_eq(answer->expr.elems[1],answer->expr.elems[2]) && answer->expr.elems[1]->kind==ATOM_SYMBOL);
        if (atom_is_symbol(answer->expr.elems[1],"a")) a_count++;
        else { assert(atom_is_symbol(answer->expr.elems[1],"b"));b_count++; }
        assert(row->expr.elems[2]->expr.len==2);arena_free(&output);
        assert(cetta_disk_query_checkpoint(query));
    }
    assert(a_count==3 && b_count==1 && next(query,4,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_END);
    assert(retained(query)==terminal_bytes);cetta_disk_query_close(query);
    query=open_query(view,"none/",0,"($row $row)");
    assert(next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_END && !packet.data);
    assert(retained(query)==terminal_bytes && descriptors()==baseline_fd);cetta_disk_query_close(query);
    CettaDiskQuery *readers[16];
    for (size_t i=0;i<16;i++) {
        readers[i]=open_query(view,"facts/",8,"((p a) same)");
        assert(cetta_disk_query_next(readers[i],0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_PAUSED);
        assert(cetta_disk_query_checkpoint(readers[i]));
    }
    for (uint64_t sequence=0;sequence<3;sequence++) for (size_t i=0;i<16;i++) {
        assert(next(readers[i],sequence,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
        assert(cetta_disk_query_checkpoint(readers[i]));
    }
    for (size_t i=0;i<16;i++) {
        assert(next(readers[i],3,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_END);
        cetta_disk_query_close(readers[i]);
    }
    assert(descriptors()==baseline_fd);
    query=open_query(view,"facts/",8,"((rel $x $x) (pair $x $x))");
    assert(next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_ROW);
    arena_init_detached(&output);row=decode(&output,packet);Atom *value=row->expr.elems[1];
    assert(value->kind==ATOM_EXPR && value->expr.len==3 && value->expr.elems[1]->kind==ATOM_VAR &&
        var_id_eq(value->expr.elems[1]->var_id,value->expr.elems[2]->var_id));
    arena_free(&output);cetta_disk_query_cancel(query);assert(retained(query)==terminal_bytes);
    assert(next(query,1,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_CANCELLED && !packet.data);
    cetta_disk_query_close(query);assert(descriptors()==baseline_fd);

    /* Capture, then discard caller handles and replace/compact the file. */
    query=open_query(view,"facts/",8,"($row $row)");
    CettaDiskStoreView *newer=put(store,view,"facts/0000-before",parse(&data,"(p newer)"));
    cetta_disk_store_view_close(view);view=NULL;
    CettaDiskStoreView *compacted=NULL;uint64_t collected=0;
    assert(cetta_disk_store_compact(store,&compacted)==CETTA_STORE_OK);
    cetta_disk_store_view_close(newer);view=compacted;
    assert(cetta_disk_store_collect(store,&collected)==CETTA_STORE_OK);
    assert(drain(query,1,1,NULL,NULL)==8);cetta_disk_query_close(query);
    query=open_query(view,"facts/",9,"($row $row)");assert(drain(query,1,2,NULL,NULL)==9);cetta_disk_query_close(query);

    /* A large scan's retained continuation must not scale with past rows. */
    enum { ROWS=4096 };CettaDiskOp *ops=calloc(ROWS,sizeof(*ops));assert(ops);
    unsigned char **bodies=calloc(ROWS,sizeof(*bodies));char (*keys)[32]=calloc(ROWS,sizeof(*keys));assert(bodies && keys);
    for (size_t i=0;i<ROWS;i++) {
        size_t length;Atom *fact=atom_expr2(&data,atom_symbol(&data,"row"),atom_int(&data,(int64_t)i));
        assert(cetta_atom_wire_encode(fact,&bodies[i],&length)==ATOM_WIRE_OK);
        snprintf(keys[i],32,"large/%08zu",i);ops[i]=(CettaDiskOp){false,bytes(keys[i]),{bodies[i],length}};
    }
    assert(cetta_disk_store_commit(store,view,ops,ROWS,&newer)==CETTA_STORE_OK);
    for (size_t i=0;i<ROWS;i++) free(bodies[i]);
    free(bodies);free(keys);free(ops);
    cetta_disk_store_view_close(view);view=newer;
    query=open_query(view,"large/",ROWS,"((row $x) (answer $x))");
    size_t low=SIZE_MAX,high=0;
    assert(drain(query,1,16,&low,&high)==ROWS && high-low<32768 && retained(query)==terminal_bytes);
    printf("CURSOR_PLATEAU rows=%u low=%zu high=%zu terminal=%zu\n",ROWS,low,high,terminal_bytes);
    cetta_disk_query_close(query);

    /* Repeated request cancellation releases FDs while the small terminal
     * cursor owner still exists. Half cancel in the middle of matching. */
    baseline_fd=descriptors();
    for (unsigned i=0;i<500;i++) {
        query=open_query(view,"large/",ROWS,"((row $x) $x)");
        if (i%2) assert(next(query,0,1,1,&packet,NULL)==CETTA_DISK_QUERY_TOO_LARGE);
        else assert(cetta_disk_query_next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_PAUSED);
        assert(cetta_disk_query_checkpoint(query));cetta_disk_query_cancel(query);
        assert(retained(query)==terminal_bytes && descriptors()==baseline_fd);
        cetta_disk_query_close(query);
    }

    /* A malformed row and a wrong count cannot terminate as empty/complete. */
    CettaDiskOp bad={false,bytes("bad/a"),bytes("not-CAV1")};
    assert(cetta_disk_store_commit(store,view,&bad,1,&newer)==CETTA_STORE_OK);
    cetta_disk_store_view_close(view);view=newer;
    query=open_query(view,"bad/",1,"($row $row)");
    assert(next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_INCOMPLETE && !packet.data);
    assert(retained(query)==terminal_bytes);cetta_disk_query_close(query);
    query=open_query(view,"facts/",0,"($row $row)");
    assert(next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_INCOMPLETE);cetta_disk_query_close(query);

    /* Valid source and query packets can expand to an oversized result. */
    char *large=malloc(600000);assert(large);memset(large,'x',600000);
    Atom *fact=atom_expr2(&data,atom_symbol(&data,"wide"),atom_string_n(&data,large,600000));free(large);
    newer=put(store,view,"huge/a",fact);cetta_disk_store_view_close(view);view=newer;
    query=open_query(view,"huge/",1,"((wide $x) (pair $x $x))");
    assert(next(query,0,1,SIZE_MAX,&packet,NULL)==CETTA_DISK_QUERY_CAPACITY && !packet.data);
    assert(retained(query)==terminal_bytes);cetta_disk_query_close(query);
    Space unavailable;space_init(&unavailable);
    assert(cetta_disk_query_open(view,bytes("facts/"),9,atom_space(&data,&unavailable),atom_int(&data,1),&query)==CETTA_STORE_INVALID && !query);
    space_free(&unavailable);

    newer=point_queries(store,view,&data);cetta_disk_store_view_close(view);view=newer;
    cetta_disk_store_view_close(view);cetta_disk_store_close(store);
    arena_free(&data);symbol_table_free(&symbols);
    puts("PASS: captured query owners preserve replay, bindings and witnesses, release on end/cancel, and bound page-arena history");
    return 0;
}
