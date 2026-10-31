/* Candidate-index qualification against ordinary Space matching. These host
 * fixtures maintain postings in the same batch; public descriptor/mutation
 * integration is a separate gate. Result bags use CAV1, not printed text. */
#include "native_disk_space.h"
#include "atom_wire.h"
#include "parser.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char *data;size_t size; } Packet;
typedef struct { Packet *items;size_t count,capacity; } Bag;
typedef struct { CettaDiskOp *ops;size_t count,capacity; } Batch;
static CettaDiskBytes bytes(const char *text) { return (CettaDiskBytes){(const unsigned char *)text,strlen(text)}; }
static Atom *parse(Arena *arena,const char *text) {
    Atom **forms=NULL;int count=parse_metta_text(text,arena,&forms);assert(count==1 && forms);
    Atom *out=forms[0];free(forms);return out;
}
static void add(Batch *batch,CettaDiskBytes key,CettaDiskBytes value) {
    if (batch->count==batch->capacity) {
        size_t capacity=batch->capacity ? batch->capacity*2 : 32;
        CettaDiskOp *items=realloc(batch->ops,capacity*sizeof(*items));assert(items);batch->ops=items;batch->capacity=capacity;
    }
    unsigned char *k=malloc(key.size ? key.size : 1),*v=malloc(value.size ? value.size : 1);assert(k && v);
    memcpy(k,key.data,key.size);memcpy(v,value.data,value.size);
    batch->ops[batch->count++]=(CettaDiskOp){false,{k,key.size},{v,value.size}};
}
static void clear(Batch *batch) {
    for (size_t i=0;i<batch->count;i++) { free((void *)batch->ops[i].key.data);free((void *)batch->ops[i].value.data); }
    free(batch->ops);*batch=(Batch){0};
}
static void posting(Batch *batch,const CettaDiskSpaceIndex *index,Atom *atom,const char *key) {
    Atom *field=NULL;
    if (atom->kind==ATOM_VAR) field=atom;
    else if (atom->kind==ATOM_EXPR && atom->expr.len==index->arity) field=atom->expr.elems[index->position];
    if (!field) return;
    bool exact=field->kind==ATOM_SYMBOL || (field->kind==ATOM_GROUNDED && field->ground.gkind==GV_STRING);
    unsigned char *encoded=NULL;size_t size=0;
    if (exact) assert(cetta_atom_wire_encode(field,&encoded,&size)==ATOM_WIRE_OK);
    const char *tag=exact ? "/exact/" : "/any/";
    size_t length=index->prefix.size+strlen(tag)+size+(exact ? 1 : 0)+strlen(key);
    unsigned char *name=malloc(length),*cursor=name;assert(name);
    memcpy(cursor,index->prefix.data,index->prefix.size);cursor+=index->prefix.size;
    memcpy(cursor,tag,strlen(tag));cursor+=strlen(tag);
    if (exact) { memcpy(cursor,encoded,size);cursor+=size;*cursor++='/'; }
    memcpy(cursor,key,strlen(key));
    add(batch,(CettaDiskBytes){name,length},bytes(""));free(name);free(encoded);
}
static void insert(Batch *batch,Arena *arena,Space *ordinary,const CettaDiskSpaceIndex *indexes,size_t count,
        unsigned position,const char *text) {
    char key[64];snprintf(key,sizeof(key),"facts/%08u",position);
    Atom *atom=parse(arena,text);space_add(ordinary,atom);
    unsigned char *encoded=NULL;size_t length;
    assert(cetta_atom_wire_encode(atom,&encoded,&length)==ATOM_WIRE_OK);
    add(batch,bytes(key),(CettaDiskBytes){encoded,length});free(encoded);
    for (size_t i=0;i<count;i++) posting(batch,&indexes[i],atom,key);
}
static void bag_push(Bag *bag,Atom *value) {
    if (bag->count==bag->capacity) {
        size_t capacity=bag->capacity ? bag->capacity*2 : 16;
        Packet *items=realloc(bag->items,capacity*sizeof(*items));assert(items);bag->items=items;bag->capacity=capacity;
    }
    Packet *out=&bag->items[bag->count++];assert(cetta_atom_wire_encode(value,&out->data,&out->size)==ATOM_WIRE_OK);
}
static int compare(const void *left,const void *right) {
    const Packet *a=left,*b=right;size_t common=a->size<b->size ? a->size : b->size;
    int compared=memcmp(a->data,b->data,common);
    return compared ? compared : a->size<b->size ? -1 : a->size>b->size;
}
static void bag_free(Bag *bag) {
    for (size_t i=0;i<bag->count;i++) free(bag->items[i].data);
    free(bag->items);*bag=(Bag){0};
}
static bool identity(SpaceReadToken read,Atom *id,void *raw) {
    (void)read;size_t *count=raw;
    assert(id && id->kind==ATOM_EXPR && id->expr.len==3 && atom_is_symbol(id->expr.elems[0],"StoreOccurrenceV1"));
    Atom *key=id->expr.elems[2];assert(key->kind==ATOM_GROUNDED && key->ground.gkind==GV_STRING);
    assert(atom_string_len(key)==strlen("facts/00000000") && !memcmp(key->ground.sval,"facts/",6));
    (*count)++;return true;
}
static Bag drain(SpaceQueryCursor *query,Arena *arena,Atom *projection,bool ids) {
    Bag bag={0};unsigned long work=0;
    for (;;) {
        Bindings bindings;bindings_init(&bindings);SpaceQueryCursorStep step=space_query_cursor_step(query,&bindings);
        assert(++work<10000000);
        if (step==SPACE_QUERY_CURSOR_ROW) {
            bag_push(&bag,bindings_apply_graph(&bindings,arena,projection));
            if (ids) { size_t count=0;assert(space_query_cursor_visit_occurrences(query,identity,&count) && count); }
        }
        bindings_free(&bindings);
        if (step==SPACE_QUERY_CURSOR_END) break;
        assert(step==SPACE_QUERY_CURSOR_ROW || step==SPACE_QUERY_CURSOR_PROGRESS);
    }
    if (bag.count) qsort(bag.items,bag.count,sizeof(*bag.items),compare);
    return bag;
}
static size_t equivalent(Space *indexed,Space *ordinary,const char *syntax) {
    Arena arena;arena_init_detached(&arena);Atom *spec=parse(&arena,syntax);
    assert(spec->kind==ATOM_EXPR && spec->expr.len==2);
    SpaceQueryCursor *query=space_query_cursor_new_captured(indexed,&arena,spec->expr.elems[0],NULL);
    SpaceQueryCursor *reference=space_query_cursor_new_captured(ordinary,&arena,spec->expr.elems[0],NULL);
    assert(query && reference);
    Bag actual=drain(query,&arena,spec->expr.elems[1],true),wanted=drain(reference,&arena,spec->expr.elems[1],false);
    assert(actual.count==wanted.count);
    for (size_t i=0;i<actual.count;i++) assert(!compare(&actual.items[i],&wanted.items[i]));
    size_t count=actual.count;
    bag_free(&actual);bag_free(&wanted);space_query_cursor_free(query);space_query_cursor_free(reference);arena_free(&arena);return count;
}
int main(int argc,char **argv) {
    assert(argc==2);SymbolTable symbols;symbol_table_init(&symbols);symbol_table_init_builtins(&symbols,&g_builtin_syms);g_symbols=&symbols;
    Arena persistent;arena_init_detached(&persistent);
    Space ordinary,indexed,scan;space_init(&ordinary);space_init(&indexed);space_init(&scan);
    CettaDiskStore *store=NULL;CettaDiskStoreView *empty=NULL,*view=NULL;
    assert(cetta_disk_store_open(argv[1],true,NULL,&store)==CETTA_STORE_OK);
    assert(cetta_disk_store_snapshot(store,&empty)==CETTA_STORE_OK);
    CettaDiskSpaceIndex indexes[]={{3,0,bytes("postings/head")},{3,1,bytes("postings/subject")},{1,0,bytes("postings/unary")}};
    Batch batch={0};unsigned count=0;
    const char *facts[]={"(edge alice bob)","(edge alice bob)","(edge bob carol)","(edge $s $s)",
        "($head alice target)","$whole","(edge 0.0 negative-zero)","(edge -0.0 positive-zero)",
        "(edge (nested a) nested-value)","(edge \"alice\" text-value)","(other alice other-value)",
        "(edge alice (danger))","(= (do-not-run) (println! SHOULD_NOT_RUN))","(single)","(edge 1 integer)",
        "(edge 1.0 floating)","(edge True boolean)","(edge \"a\\0b\" nul-value)"};
    for (size_t i=0;i<sizeof(facts)/sizeof(*facts);i++) insert(&batch,&persistent,&ordinary,indexes,3,count++,facts[i]);
    for (unsigned i=0;i<1024;i++) {
        char text[96];snprintf(text,sizeof(text),"(noise subject-%u object-%u)",i,i);
        insert(&batch,&persistent,&ordinary,indexes,3,count++,text);
    }
    assert(cetta_disk_store_commit(store,empty,batch.ops,batch.count,&view)==CETTA_STORE_OK);
    clear(&batch);cetta_disk_store_view_close(empty);
    CettaDiskSpaceIndex invalid[]={{3,3,bytes("bad")}};
    assert(cetta_disk_space_capture_indexed(&indexed,view,bytes("facts/"),count,invalid,1)==CETTA_STORE_INVALID);
    assert(space_length64(&indexed)==0);
    assert(cetta_disk_space_capture_indexed(&indexed,view,bytes("facts/"),count,indexes,3)==CETTA_STORE_OK);
    assert(cetta_disk_space_capture(&scan,view,bytes("facts/"),count)==CETTA_STORE_OK);
    indexes[0].position=99;indexes[1].prefix=bytes("discarded-caller-prefix");
    CettaDiskSpaceStatistics before,after;assert(cetta_disk_space_statistics(&indexed,&before));
    size_t narrow=equivalent(&indexed,&ordinary,"((edge alice $target) $target)");
    assert(narrow>=5 && cetta_disk_space_statistics(&indexed,&after));
    assert(after.indexed_selections>before.indexed_selections && after.candidates-before.candidates<32);
    printf("index candidates=%llu for facts=%u, matching rows=%zu\n",
        (unsigned long long)(after.candidates-before.candidates),count,narrow);
    const char *queries[]={
        "((edge bob $x) $x)","((edge $x $x) (pair $x $x))","(($relation alice $x) ($relation $x))",
        "((edge \"alice\" $x) $x)","((edge \"a\\0b\" $x) $x)","((edge -0.0 $x) $x)",
        "(($relation 1 $x) ($relation $x))","(($relation 0.0 $x) ($relation $x))",
        "((edge (nested a) $x) $x)","((, (edge alice $x) (edge $x $y)) (pair $x $y))",
        "((single) present)","((missing absent $x) $x)","($all $all)","((= $a $b) (pair $a $b))",
        "((edge alice (danger)) (danger))"};
    for (size_t i=0;i<sizeof(queries)/sizeof(*queries);i++) equivalent(&indexed,&ordinary,queries[i]);
    assert(cetta_disk_space_statistics(&indexed,&before));
    equivalent(&indexed,&ordinary,"(($relation 0.0 $x) ($relation $x))");
    assert(cetta_disk_space_statistics(&indexed,&after) && after.candidates-before.candidates==count);
    assert(!after.overflow);
    /* Clone both while matching and between the two selected bucket streams.
     * The ordinary Space cursor transport must retain the correct position. */
    Arena query_arena;arena_init_detached(&query_arena);
    Atom *spec=parse(&query_arena,"((edge alice $target) $target)");
    SpaceQueryCursor *query=space_query_cursor_new_captured(&indexed,&query_arena,spec->expr.elems[0],NULL);assert(query);
    for (unsigned step=0;step<60;step++) {
        SpaceQueryCursor *copy=space_query_cursor_clone(query);assert(copy);
        Bag a=drain(copy,&query_arena,spec->expr.elems[1],true);space_query_cursor_free(copy);
        copy=space_query_cursor_clone(query);assert(copy);
        Bag b=drain(copy,&query_arena,spec->expr.elems[1],true);space_query_cursor_free(copy);
        assert(a.count==b.count);for (size_t i=0;i<a.count;i++) assert(!compare(&a.items[i],&b.items[i]));
        bag_free(&a);bag_free(&b);
        Bindings row;bindings_init(&row);SpaceQueryCursorStep state=space_query_cursor_step(query,&row);bindings_free(&row);
        if (state==SPACE_QUERY_CURSOR_END) break;
        assert(state==SPACE_QUERY_CURSOR_ROW || state==SPACE_QUERY_CURSOR_PROGRESS);
    }
    space_query_cursor_free(query);arena_free(&query_arena);
    /* A dangling or incorrectly keyed posting is unfinished execution, not
     * ordinary empty success. The captured original remains readable. */
    CettaDiskCursor *postings=NULL;
    CettaDiskBytes lower=bytes("postings/subject/exact/"),upper=bytes("postings/subject/exact0");
    assert(cetta_disk_cursor_open_range(cetta_disk_store_view_root(view),&lower,false,&upper,&postings)==CETTA_DISK_OK);
    CettaDiskBuffer posting_key={0};CettaDiskBlob posting_value;
    assert(cetta_disk_cursor_next(postings,&posting_key,&posting_value)==CETTA_DISK_OK);
    CettaDiskOp corrupt={false,{posting_key.data,posting_key.size},bytes("facts/missing!")};
    CettaDiskStoreView *broken=NULL;assert(cetta_disk_store_commit(store,view,&corrupt,1,&broken)==CETTA_STORE_OK);
    CettaDiskSpaceIndex valid_indexes[]={{3,0,bytes("postings/head")},{3,1,bytes("postings/subject")},{1,0,bytes("postings/unary")}};
    Space invalid_source;space_init(&invalid_source);
    assert(cetta_disk_space_capture_indexed(&invalid_source,broken,bytes("facts/"),count,valid_indexes,3)==CETTA_STORE_OK);
    /* The range's first exact value is decoded from its full posting key,
     * so this probe necessarily visits the damaged posting. */
    size_t offset=lower.size;Arena damaged_input;arena_init_detached(&damaged_input);Atom *value=NULL;
    unsigned char *packet=NULL;size_t packet_size=0;
    const char *texts[]={"alice","bob","\"alice\"","\"a\\0b\""};
    bool visited=false;
    for (size_t i=0;i<sizeof(texts)/sizeof(*texts);i++) {
        value=parse(&damaged_input,texts[i]);assert(cetta_atom_wire_encode(value,&packet,&packet_size)==ATOM_WIRE_OK);
        bool matching=posting_key.size>offset+packet_size && !memcmp(posting_key.data+offset,packet,packet_size);
        free(packet);packet=NULL;if (!matching) continue;
        Atom *pattern=atom_expr(&damaged_input,(Atom *[]){atom_var(&damaged_input,"head"),value,atom_var(&damaged_input,"tail")},3);
        query=space_query_cursor_new_captured(&invalid_source,&damaged_input,pattern,NULL);assert(query);
        SpaceQueryCursorStep state;
        do {
            Bindings row;bindings_init(&row);state=space_query_cursor_step(query,&row);bindings_free(&row);
        } while (state==SPACE_QUERY_CURSOR_PROGRESS || state==SPACE_QUERY_CURSOR_ROW);
        assert(state==SPACE_QUERY_CURSOR_INCOMPLETE);space_query_cursor_free(query);visited=true;break;
    }
    assert(visited);arena_free(&damaged_input);space_free(&invalid_source);cetta_disk_store_view_close(broken);
    cetta_disk_buffer_free(&posting_key);cetta_disk_cursor_close(postings);
    /* Captured indexes and their fact lookups survive publication/collection. */
    CettaDiskStoreView *compacted=NULL;assert(cetta_disk_store_compact(store,&compacted)==CETTA_STORE_OK);
    uint64_t removed;assert(cetta_disk_store_collect(store,&removed)==CETTA_STORE_OK && removed>0);
    cetta_disk_store_view_close(view);cetta_disk_store_view_close(compacted);cetta_disk_store_close(store);
    equivalent(&indexed,&ordinary,"((edge alice $x) $x)");
    equivalent(&scan,&ordinary,"((edge alice $x) $x)");
    space_free(&indexed);space_free(&scan);space_free(&ordinary);arena_free(&persistent);
    symbol_table_free(&symbols);g_symbols=NULL;
    puts("PASS: declared candidate indexes preserve ordinary match bags, wildcards, identities, numeric fallback, clone positions and captured roots");
    return 0;
}
