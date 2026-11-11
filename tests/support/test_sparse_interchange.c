#include "tensor.h"
#include "cetta_embed.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static unsigned retained, released;
static bool injecting;
static size_t failure, allocation;
static void *tracked[256];
static size_t tracked_count;
static const void *protected[16];
static size_t protected_count;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void __real_free(void *);
static void *track(void *pointer) {
    if (injecting && pointer) { assert(tracked_count < 256); tracked[tracked_count++] = pointer; }
    return pointer;
}
void *__wrap_malloc(size_t n) { return injecting && allocation++ == failure ? NULL : track(__real_malloc(n)); }
void *__wrap_calloc(size_t n, size_t w) { return injecting && allocation++ == failure ? NULL : track(__real_calloc(n,w)); }
void __wrap_free(void *pointer) {
    for (size_t i=0; i<protected_count; ++i) assert(pointer != protected[i] && "borrowed component was freed");
    for (size_t i=0; i<tracked_count; ++i) if (tracked[i]==pointer) tracked[i]=NULL;
    __real_free(pointer);
}
static void balanced_allocations(void) {
    for (size_t i=0; i<tracked_count; ++i) assert(!tracked[i] && "staging allocation leaked");
    tracked_count=0;
}
static void retain(void *unused) { (void)unused; retained++; }
static void release(void *unused) { (void)unused; released++; }
static const CettaTensorBufferOwner owner = {NULL, retain, release};
static void inspect_support(CettaTensor *tensor) {
    assert(cetta_tensor_stored(tensor)==4);
    bool present; CettaTensorScalar value;
    assert(cetta_tensor_get(tensor, 0, &present, &value) == CETTA_TENSOR_OK && present && value.f64 == 0);
    assert(cetta_tensor_get(tensor, 1, &present, &value) == CETTA_TENSOR_OK && !present);
    assert(cetta_tensor_get(tensor, 2, &present, &value) == CETTA_TENSOR_OK && present && value.f64 == 5);
    CettaTensor *keys=NULL,*values=NULL;
    assert(cetta_tensor_entries(tensor,&keys,&values)==CETTA_TENSOR_OK);
    uint64_t observed_keys[4]; double observed_values[4];
    assert(cetta_tensor_copy_values(keys,observed_keys,sizeof(observed_keys))==CETTA_TENSOR_OK);
    assert(cetta_tensor_copy_values(values,observed_values,sizeof(observed_values))==CETTA_TENSOR_OK);
    assert(!memcmp(observed_keys,(uint64_t[]){0,2,9,11},sizeof(observed_keys)));
    assert(!memcmp(observed_values,(double[]){0,5,2,4},sizeof(observed_values)));
    cetta_tensor_release(keys); cetta_tensor_release(values);
}
static void core_cases(void) {
    const size_t shape[]={3,4},coordinates[]={0,2,9,11},csr_segments[]={0,2,2,4},csr_minor[]={0,2,1,3};
    const size_t csc_segments[]={0,1,2,3,4},csc_minor[]={0,2,0,2};
    const double canonical[]={0,5,2,4},column_values[]={0,2,5,4};
    const void *arrays[]={coordinates,csr_segments,csr_minor,csc_segments,csc_minor,canonical,column_values};
    memcpy(protected,arrays,sizeof(arrays)); protected_count=sizeof(arrays)/sizeof(*arrays);
    unsigned refused=0;
    for(CettaTensorLayout layout=CETTA_TENSOR_COO;layout<=CETTA_TENSOR_CSC;layout++) {
        const size_t *keys=layout==CETTA_TENSOR_COO?coordinates:NULL;
        const size_t *segments=layout==CETTA_TENSOR_CSR?csr_segments:layout==CETTA_TENSOR_CSC?csc_segments:NULL;
        const size_t *minor=layout==CETTA_TENSOR_CSR?csr_minor:layout==CETTA_TENSOR_CSC?csc_minor:NULL;
        const double *values=layout==CETTA_TENSOR_CSC?column_values:canonical;
        bool success=false;
        for(failure=0;failure<16;failure++) {
            unsigned before=retained;
            CettaTensor *tensor=NULL;
            allocation=0; injecting=true;
            CettaTensorStatus status=cetta_tensor_sparse_borrow(CETTA_TENSOR_F64,CETTA_TENSOR_BYTE_ORDER_NATIVE,
                layout,shape,2,keys,segments,minor,values,4,&owner,&tensor);
            injecting=false;
            if(status==CETTA_TENSOR_OK) {
                assert(retained==before+1);
                inspect_support(tensor);
                CettaTensorSparseSpan span;
                assert(cetta_tensor_sparse_span(tensor,&span) && span.values==values && span.entries==4);
                cetta_tensor_release(tensor);
                assert(retained==released);
                balanced_allocations();
                success=true; break;
            }
            assert(status==CETTA_TENSOR_NOMEM && !tensor && retained==before && retained==released);
            balanced_allocations();
            refused++;
        }
        assert(success);
    }
    CettaTensor *tensor=NULL;
    unsigned before=retained;
    assert(cetta_tensor_sparse_borrow(CETTA_TENSOR_F64,CETTA_TENSOR_BYTE_ORDER_NATIVE,CETTA_TENSOR_COO,
        shape,2,(size_t[]){0,2,2,11},NULL,NULL,canonical,4,&owner,&tensor)==CETTA_TENSOR_DUPLICATE && !tensor);
    assert(cetta_tensor_sparse_borrow(CETTA_TENSOR_F64,CETTA_TENSOR_BYTE_ORDER_NATIVE,CETTA_TENSOR_CSR,
        shape,2,NULL,(size_t[]){0,3,2,4},csr_minor,canonical,4,&owner,&tensor)==CETTA_TENSOR_INDEX_ERROR && !tensor);
    assert(retained==before && retained==released);
    protected_count=0;
    printf("sparse core: canonical support, CSC permutation and %u allocation refusals pass\n",refused);
}
static cetta_buffer_t *buffer(const char *dtype,const void *data,size_t count,size_t width) {
    cetta_buffer_t *out=NULL;
    cetta_buffer_descriptor_t spec={.size=sizeof(spec),.dtype=dtype,.allocation=data,
        .allocation_bytes=count*width,.shape=&count,.rank=1};
    assert(cetta_embed_buffer_copy(&spec,&out)==CETTA_OK);
    return out;
}
static void release_parts(cetta_sparse_components_t *parts) {
    cetta_embed_buffer_release(parts->values); cetta_embed_buffer_release(parts->coordinates);
    cetta_embed_buffer_release(parts->indptr); cetta_embed_buffer_release(parts->indices);
}
typedef struct { cetta_engine_t *engine; cetta_atom_t *atom, *head; } SharedSparse;
static void *sum_worker(void *pointer) {
    SharedSparse *work=pointer;
    for(unsigned i=0;i<32;i++) {
        cetta_result_t *result=NULL; cetta_atom_t *answer=NULL; double value;
        assert(cetta_embed_call(work->engine,work->head,&work->atom,1,NULL,&result)==CETTA_OK);
        assert(cetta_embed_result_count(result)==1 && cetta_embed_result_get(result,0,&answer)==CETTA_OK);
        assert(cetta_embed_atom_get_float(answer,&value)==CETTA_OK && value==7);
        cetta_embed_atom_release(answer); cetta_embed_result_release(result);
    }
    return NULL;
}
static cetta_atom_t *sum_head(cetta_engine_t *engine) {
    const char import[]="!(import! &self (library tensor))";
    cetta_result_t *result=NULL; cetta_atom_t *head=NULL;
    assert(cetta_embed_run(engine,import,sizeof(import)-1,NULL,&result)==CETTA_OK);
    cetta_embed_result_release(result);
    assert(cetta_embed_atom_symbol(engine,"tensor:sum",&head)==CETTA_OK);
    return head;
}
static void api_cases(void) {
    cetta_engine_t *engine=NULL;
    assert(cetta_embed_engine_new(NULL,&engine)==CETTA_OK);
    cetta_buffer_t *keys=buffer("index",(uint64_t[]){0,4},2,8);
    cetta_buffer_t *values=buffer("f64",(double[]){0,7},2,8);
    cetta_buffer_descriptor_t described={.size=sizeof(described)};
    assert(cetta_embed_buffer_describe(keys,&described)==CETTA_OK); protected[protected_count++]=described.allocation;
    assert(cetta_embed_buffer_describe(values,&described)==CETTA_OK); protected[protected_count++]=described.allocation;
    size_t shape[]={2,3};
    cetta_sparse_descriptor_t spec={.size=sizeof(spec),.layout=CETTA_LAYOUT_COO,
        .shape=shape,.rank=2,.coordinates=keys,.values=values};
    cetta_atom_t *atom=NULL;
    assert(cetta_embed_tensor_from_sparse(engine,&spec,CETTA_DUPLICATES_REJECT,CETTA_COPY_NEVER,&atom)==CETTA_OK);
    cetta_embed_buffer_release(keys); cetta_embed_buffer_release(values);
    for(unsigned round=0;round<64;round++) {
        cetta_tensor_info_t before={.size=sizeof(before)},after={.size=sizeof(after)};
        assert(cetta_embed_tensor_describe(atom,&before)==CETTA_OK);
        cetta_sparse_components_t parts={.size=sizeof(parts)};
        assert(cetta_embed_tensor_components(atom,&parts)==CETTA_OK);
        cetta_sparse_descriptor_t again={.size=sizeof(again),.layout=CETTA_LAYOUT_COO,
            .shape=shape,.rank=2,.coordinates=parts.coordinates,.values=parts.values};
        cetta_atom_t *next=NULL;
        assert(cetta_embed_tensor_from_sparse(engine,&again,CETTA_DUPLICATES_REJECT,CETTA_COPY_NEVER,&next)==CETTA_OK);
        assert(cetta_embed_tensor_describe(next,&after)==CETTA_OK && after.shape==before.shape);
        cetta_embed_atom_release(atom); atom=next; release_parts(&parts);
    }
    cetta_engine_t *second=NULL; cetta_atom_t *other=NULL;
    assert(cetta_embed_engine_new(NULL,&second)==CETTA_OK);
    cetta_sparse_components_t shared={.size=sizeof(shared)};
    assert(cetta_embed_tensor_components(atom,&shared)==CETTA_OK);
    spec.values=shared.values; spec.coordinates=shared.coordinates;
    assert(cetta_embed_tensor_from_sparse(second,&spec,CETTA_DUPLICATES_REJECT,CETTA_COPY_NEVER,&other)==CETTA_OK);
    release_parts(&shared);
    SharedSparse work[2]={{engine,atom,sum_head(engine)},{second,other,sum_head(second)}};
    pthread_t workers[2];
    for(unsigned i=0;i<2;i++) assert(!pthread_create(&workers[i],NULL,sum_worker,&work[i]));
    for(unsigned i=0;i<2;i++) { assert(!pthread_join(workers[i],NULL)); cetta_embed_atom_release(work[i].head); }
    cetta_embed_atom_release(other); cetta_embed_engine_close(second); cetta_embed_engine_release(second);
    unsigned refused=0; bool complete=false;
    for(failure=0;failure<64;failure++) {
        cetta_sparse_components_t parts={.size=sizeof(parts)};
        allocation=0; injecting=true;
        cetta_status_t status=cetta_embed_tensor_components(atom,&parts);
        injecting=false;
        if(status==CETTA_OK) { release_parts(&parts); balanced_allocations(); complete=true; break; }
        assert(status==CETTA_OUT_OF_MEMORY && !parts.values && !parts.coordinates && !parts.indptr && !parts.indices);
        balanced_allocations();
        refused++;
    }
    assert(complete && refused>10);
    cetta_embed_engine_close(engine); cetta_embed_engine_release(engine);
    cetta_sparse_components_t escaped={.size=sizeof(escaped)};
    assert(cetta_embed_tensor_components(atom,&escaped)==CETTA_OK);
    cetta_embed_atom_release(atom);
    assert(cetta_embed_buffer_describe(escaped.coordinates,&described)==CETTA_OK);
    assert(!memcmp(described.allocation,(uint64_t[]){0,4},16));
    protected_count=0;
    release_parts(&escaped);
    printf("sparse ABI: released inputs, 64 owner round trips, concurrent engines, closed-engine views and %u export refusals pass\n",refused);
}
int main(void) { core_cases(); api_cases(); return 0; }
